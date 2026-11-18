#pragma once

// A spore fog cloud (Godot: flora/spore_fog.gd). Soft purple puffs and drifting motes.
// - Thick fog (Spore Dash, the Veil ult) blocks enemy sight lines through it.
// - While veiled (the VeilSpore ult), you're invisible inside any spore fog.
// - With Spore Fog Damage equipped, fog hurts Swarm and Soldiers inside it (not the Tank).
// - The Longer Fog trait stretches its time.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Flora/WWOEffectLife.h"
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
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	static FLinearColor Color() { return FLinearColor(0.62f, 0.45f, 0.85f); }
	// Owner: the player whose kit made it (online the host spawns it and it replicates).
	static AWWOSporeFog* SpawnFog(UWorld* World, const FVector& At, float RadiusM, float Seconds, bool bThick = false, AActor* Owner = nullptr);
	static bool Contains(const UWorld* World, const FVector& Point);
	static bool Blocks(const UWorld* World, const FVector& From, const FVector& To);

	UPROPERTY(Replicated) float Radius = 3.f; // meters
	UPROPERTY(Replicated) float Duration = 6.f;
	UPROPERTY(Replicated) bool bBlocksSight = false;
	UPROPERTY(Replicated) float Opacity = 0.22f;

	float Strength() const;

private:
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Puffs;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
	TArray<FVector> Sizes;
	TWeakObjectPtr<AWWOParticles> Motes;
	TWeakObjectPtr<AWWOParticles> Smoke; // wisps rising inside it
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Edge; // its outline on the floor
	FWWOEffectLife Life; // its numbers are in the constructor
	float TickLeft = 0.f;
	friend class FWWORegress; // the life scenarios read its looks
};
