#pragma once

// The flora's world effects (Godot: flora/*.gd).
// - AWWOSporePool: a damage puddle left by pool grenades (VoltVine's crackles with lightning).
// - AWWOSporePatch: a little mushroom that puffs out spore fog (Spore Patches).
// - UWWODamageOverTime: poison on a target (Poison Spike), purple numbers and drips.
// - AWWOBarkWall: a bark slab facing the thrower (Wall Grenade); enemies use it as cover.
// - AWWOEntrenchRoots: the ring of roots around you during the Entrench ult.
// - AWWOChainLightning: Static Charge's lightning, jumping enemy to enemy.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "WWOFloraEffects.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UBoxComponent;
class AWWOParticles;
class UWWOAbilityData;

UCLASS()
class WHATWASOURS_API AWWOSporePool : public AActor
{
	GENERATED_BODY()

public:
	AWWOSporePool();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	static AWWOSporePool* Spawn(UWorld* World, const FVector& At, float RadiusM, float Dps, float Duration, const FLinearColor& Color, FName Flavor, AActor* Thrower);

private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Disc;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Mat;
	TWeakObjectPtr<AWWOParticles> Bubbles;
	TWeakObjectPtr<AActor> Thrower;
	float Radius = 3.f, Dps = 25.f, Duration = 5.f, Age = 0.f, TickLeft = 0.f, NextArc = 0.f;
	FLinearColor Color;
	FName Flavor;
};

UCLASS()
class WHATWASOURS_API AWWOSporePatch : public AActor
{
	GENERATED_BODY()

public:
	AWWOSporePatch();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	static AWWOSporePatch* Spawn(UWorld* World, const FVector& At, float RadiusM, float Duration);

private:
	float Radius = 2.f, Duration = 6.f, Age = 0.f;
	bool bCounted = false;
};

UCLASS()
class WHATWASOURS_API UWWODamageOverTime : public UActorComponent
{
	GENERATED_BODY()

public:
	UWWODamageOverTime();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	static void Apply(AActor* Target, float DamagePerSecond, float Seconds);
	static FLinearColor Color() { return FLinearColor(0.7f, 0.4f, 0.95f); }

	float Dps = 20.f;
	float TimeLeft = 5.f;

private:
	float TickLeft = 0.5f;
	TWeakObjectPtr<AWWOParticles> Drips;
};

UCLASS()
class WHATWASOURS_API AWWOBarkWall : public AActor
{
	GENERATED_BODY()

public:
	AWWOBarkWall();
	virtual void Tick(float DeltaSeconds) override;
	static AWWOBarkWall* Raise(UWorld* World, const FVector& Center, const FVector& FacingFrom, float WidthM, float HeightM, float Seconds);

	float Width = 5.f, Height = 2.6f, Duration = 10.f;

private:
	UPROPERTY() TObjectPtr<UBoxComponent> Box;
	UPROPERTY() TObjectPtr<USceneComponent> Visual;
	float Age = 0.f;
	bool bSinking = false;
	void Build();
	void Dust(float Amount);
};

UCLASS()
class WHATWASOURS_API AWWOEntrenchRoots : public AActor
{
	GENERATED_BODY()

public:
	AWWOEntrenchRoots();
	virtual void Tick(float DeltaSeconds) override;
	static AWWOEntrenchRoots* Spawn(UWorld* World, AActor* Player, float Duration);

private:
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RingMat;
	float Duration = 10.f, Age = 0.f;
	bool bEnding = false;
	void BurstDirt();
};

UCLASS()
class WHATWASOURS_API AWWOChainLightning : public AActor
{
	GENERATED_BODY()

public:
	AWWOChainLightning();
	virtual void Tick(float DeltaSeconds) override;
	static AWWOChainLightning* Spawn(UWorld* World, const UWWOAbilityData* Ability, AActor* First, AActor* Player, const FLinearColor& Color);

private:
	TArray<TWeakObjectPtr<AActor>> Hit;
	TWeakObjectPtr<AActor> Current;
	TWeakObjectPtr<AActor> Player;
	FLinearColor Color;
	float Damage = 120.f, Range = 8.f, Falloff = 0.85f, Delay = 0.07f, Wait = 0.f;
	int32 JumpsLeft = 6;
	AActor* NextTarget() const;
	void Burst(AActor* Target, int32 Arcs, float Reach, float Time);
	void DamageTarget(AActor* Target, float Amount);
};
