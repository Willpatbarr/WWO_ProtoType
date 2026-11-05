#pragma once

// A spore fog cloud (Godot: flora/spore_fog.gd). Soft purple puffs and drifting motes.
// - Thick fog (Spore Dash, the Veil ult) blocks enemy sight lines through it.
// - While veiled (the VeilSpore ult), you're invisible inside any spore fog.
// - With Spore Fog Damage equipped, fog hurts Swarm and Soldiers inside it (not the Tank).
// - The Longer Fog trait stretches its time.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WWOSporeFog.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class AWWOParticles;

UCLASS()
class WHATWASOURS_API AWWOSporeFog : public AActor
{
	GENERATED_BODY()

public:
	AWWOSporeFog();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	static FLinearColor Color() { return FLinearColor(0.62f, 0.45f, 0.85f); }
	static AWWOSporeFog* SpawnFog(UWorld* World, const FVector& At, float RadiusM, float Seconds, bool bThick = false);
	static bool Contains(const UWorld* World, const FVector& Point);
	static bool Blocks(const UWorld* World, const FVector& From, const FVector& To);

	float Radius = 3.f; // meters
	float Duration = 6.f;
	bool bBlocksSight = false;
	float Opacity = 0.22f;

	float Strength() const;

private:
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Puffs;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
	TArray<FVector> Sizes;
	TWeakObjectPtr<AWWOParticles> Motes;
	float Age = 0.f;
	float TickLeft = 0.f;
};
