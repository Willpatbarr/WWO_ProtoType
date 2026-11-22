#pragma once

// A thrown grenade (Godot: grenade.gd): a glowing ball in the grenade's color that bounces,
// then explodes. The blast damages targets in range (full at the center, half at the edge)
// unless a wall is in the way. By kind:
// - Explosive: blows up on its fuse.
// - Pool: small blast that leaves a lingering damage pool.
// - Spike: no fuse; bounces (curving toward targets) until it touches one, then poisons it.
// - Tripmine: sticks where it lands, arms, blows up near an enemy and slows enemies.
// - Wall: blows up and raises a bark wall facing the thrower.
// - Blind: blows up and blinds enemies.
// Seedburst splits it into seeds; Spore Patches sprouts patches where it touches down.
// Online the host runs every grenade (a joiner's throw is spawned there from its loadout); it
// replicates its look once and its position, everyone sees it fly, and its blast is multicast.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/NetSerialization.h"
#include "WWOTypes.h"
#include "WWOGrenade.generated.h"

class USphereComponent;
class UProjectileMovementComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UWWOGrenadeData;
class UWWOAbilityData;
class AWWOParticles;
class AWWOCharacter;
class AWWOGrenade;
class IWWOTargetable;

USTRUCT()
struct FWWOGrenadeSpec
{
	GENERATED_BODY()

	FLinearColor Color = FLinearColor::White;
	float Damage = 120.f;
	float BlastRadius = 4.f; // meters
	float Fuse = 1.5f;
	UPROPERTY() TObjectPtr<UWWOGrenadeData> Data;         // the grenade it was thrown as (null for seeds and the ult)
	UPROPERTY() TObjectPtr<UWWOAbilityData> Seedburst;    // set with Seedburst equipped
	UPROPERTY() TObjectPtr<UWWOAbilityData> SporePatches; // set with Spore Patches equipped
	bool bIsSeed = false;
	bool bExplodeOnImpact = false; // Impact Grenades trait, and Root Bombs
	float SizeScale = 1.f;
	FVector ThrownFrom = FVector::ZeroVector; // walls face this way
	FName Flavor; // which flora's look the blast has (voltvine / veilspore / ironbark)
	bool bVisible = true;
};

// What one kind of grenade (EWWOGrenadeKind) does besides flying, bouncing and blowing up: one entry per
// kind in AWWOGrenade::KindRules (Weapons/WWOGrenade.cpp), read on the host. A field left empty is
// something the kind doesn't do. Seeds, the ult's blast and Root Bombs have no grenade asset, so no kind.
struct FWWOGrenadeKindRules
{
	EWWOGrenadeKind Kind = EWWOGrenadeKind::Explosive;
	// When thrown.
	bool bImpactFuse = true;   // the Impact Grenades trait makes it go off on its first touch
	bool bBrightFlash = false; // its blast flashes brighter (FWWOGrenadeLook::bBlind)
	float (*Life)(const UWWOGrenadeData& Data) = nullptr; // no fuse: it waits this long for a target, then fizzles
	bool bLifeExplodes = false; // ...or goes off properly when that time is up
	// While it flies or waits (every host tick).
	void (*OnTick)(AWWOGrenade& G, float DeltaSeconds) = nullptr;
	// On a touch (Target: something it may hurt, else null). True: handled, nothing else happens on it.
	bool (*OnTouch)(AWWOGrenade& G, AActor* Other, IWWOTargetable* Target) = nullptr;
	// When it bursts, after the blast's damage, Seedburst and Shield Share.
	void (*OnBurst)(AWWOGrenade& G, const FVector& Center) = nullptr;
};

// What other machines need to draw a grenade and its blast (sent once).
USTRUCT()
struct FWWOGrenadeLook
{
	GENERATED_BODY()
	UPROPERTY() FLinearColor Color = FLinearColor::White;
	UPROPERTY() float Size = 12.f;        // cm radius
	UPROPERTY() float BlastRadius = 4.f;  // m
	UPROPERTY() FName Flavor;
	UPROPERTY() bool bVisible = true;
	UPROPERTY() bool bBlind = false;      // the blind grenade's brighter flash
};

UCLASS()
class WHATWASOURS_API AWWOGrenade : public AActor
{
	GENERATED_BODY()

public:
	AWWOGrenade();

	static AWWOGrenade* Throw(UWorld* World, const FWWOGrenadeSpec& Spec, const FVector& At, const FVector& VelocityCm, AActor* Thrower);
	// A blast that goes off the moment it exists (the ult).
	static void ExplodeAt(UWorld* World, const FWWOGrenadeSpec& Spec, const FVector& At, AActor* Instigator);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void IgnoreActor(AActor* Other);
	void Explode();
	// A blast's looks (no damage) at Center; Ignore: left out of the ground check.
	static void SpawnBlast(UWorld* World, const FWWOGrenadeLook& Look, const FVector& Center, float RadiusM, AActor* Ignore = nullptr);
	// Each kind's own behaviour (see FWWOGrenadeKindRules).
	static const FWWOGrenadeKindRules& KindRules(EWWOGrenadeKind Kind);

	static constexpr float Radius = 0.12f; // meters (seeds are smaller)
	static constexpr float EdgeDamage = 0.5f;
	static constexpr float Proximity = 0.45f; // the Poison Spike counts as a hit this far from a body
	static constexpr float LinearDamp = 0.1f;     // Godot's default rigid-body damping (per second)
	static constexpr float ContactFriction = 0.8f; // the Godot grenade's physics material friction

	FWWOGrenadeSpec Spec;
	UPROPERTY(Replicated) FWWOGrenadeLook Look;

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Collision;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProjectileMovementComponent> Movement;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Ball;
	UPROPERTY() TObjectPtr<UPointLightComponent> Light;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Halo;
	TWeakObjectPtr<AWWOParticles> Trail;
	TWeakObjectPtr<AActor> Thrower;

	bool bSpent = false;
	UPROPERTY(ReplicatedUsing = OnRep_Stuck) bool bStuck = false;
	UFUNCTION() void OnRep_Stuck();
	void BuildLooks();
	void Blast(const FVector& Center); // the looks here and, online, everywhere
	void Finish();                     // gone (online: hidden for a moment so the blast reaches everyone first)
	void AddHalo();
	float ArmLeft = -1.f;
	float Age = 0.f;
	float LifeLimit = 0.f; // fuse or fizzle time
	bool bFuseIsFizzle = false;
	int32 PatchesLeft = 0;
	float PatchCooldown = 0.f;
	TWeakObjectPtr<class AWWOCharacter> CountedFor;
	bool bCounted = false;
	FVector ReleasedAt = FVector::ZeroVector;
	bool bBounced = false;
	FVector Spin = FVector::ZeroVector; // angular velocity (rad/s), for rolling contact friction

	void Setup(AActor* InThrower);
	UFUNCTION() void OnBounce(const FHitResult& Hit, const FVector& ImpactVelocity);
	void ApplyContactFriction(const FHitResult& Hit, const FVector& ImpactVelocity);
	void Seek(float DeltaSeconds);
	void UpdateMine(float DeltaSeconds);
	void Stick();
	void SpikeHit(AActor* Target);
	void Fizzle();
	void Burst(const FVector& Center);
	void ShareShields(const FVector& Center);
	const FWWOGrenadeKindRules* Rules() const; // its kind's, or none (no grenade asset)
	// The kinds' bursts (KindRules).
	void RaiseWall(const FVector& Center);
	void BlindOrSlow(const FVector& Center, bool bBlind); // blind (Blind), else slow (Tripmine)
	void LeavePool(const FVector& Center);
	bool GroundTouch(const AActor* Other); // Bloom Pool, Reveille: walkable ground sets it off; anything else it glances off
	void FallCheck();                      // ...and it never falls out of the world live
	FVector TouchNormal = FVector::UpVector; // the surface of the touch being handled

	// The regression scenarios (-WWORegress, Player/WWORegress.h) call private steps directly; no gameplay code uses this.
	friend class FWWORegress;

};
