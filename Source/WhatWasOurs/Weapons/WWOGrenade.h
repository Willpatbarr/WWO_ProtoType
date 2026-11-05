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

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WWOGrenade.generated.h"

class USphereComponent;
class UProjectileMovementComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UWWOGrenadeData;
class UWWOAbilityData;
class AWWOParticles;
class AWWOCharacter;

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

UCLASS()
class WHATWASOURS_API AWWOGrenade : public AActor
{
	GENERATED_BODY()

public:
	AWWOGrenade();

	static AWWOGrenade* Throw(UWorld* World, const FWWOGrenadeSpec& Spec, const FVector& At, const FVector& VelocityCm, AActor* Thrower);
	// A blast that goes off the moment it exists (the ult).
	static void ExplodeAt(UWorld* World, const FWWOGrenadeSpec& Spec, const FVector& At, AActor* Instigator);

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	void IgnoreActor(AActor* Other);
	void Explode();

	static constexpr float Radius = 0.12f; // meters (seeds are smaller)
	static constexpr float EdgeDamage = 0.5f;
	static constexpr float Proximity = 0.45f; // the Poison Spike counts as a hit this far from a body
	static constexpr float LinearDamp = 0.1f;     // Godot's default rigid-body damping (per second)
	static constexpr float ContactFriction = 0.8f; // the Godot grenade's physics material friction

	FWWOGrenadeSpec Spec;

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Collision;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProjectileMovementComponent> Movement;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Ball;
	UPROPERTY() TObjectPtr<UPointLightComponent> Light;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Halo;
	TWeakObjectPtr<AWWOParticles> Trail;
	TWeakObjectPtr<AActor> Thrower;

	bool bSpent = false;
	bool bStuck = false;
	float ArmLeft = -1.f;
	float Age = 0.f;
	float LifeLimit = 0.f; // fuse or fizzle time
	bool bFuseIsFizzle = false;
	int32 PatchesLeft = 0;
	float PatchCooldown = 0.f;
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
	void SpawnBlast(const FVector& Center);
	void Confirm(bool bKilled);
};
