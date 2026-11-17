#pragma once

// A slow glowing shot fired by enemies (Godot: enemies/enemy_projectile.gd). Moves each
// step with a ray so it can't skip through thin things; hurts the player or a teammate on
// contact and pops on anything else. Enemies don't hit each other. With Gravity it arcs;
// with SplashRadius it explodes where it lands (full damage at the center, half at the edge).
// Online: the host's shot replicates once, at spawn (its velocity, gravity, look); each joiner flies
// its own copy the same way, for looks only. Only the host's copy hurts anyone; a joiner's stops on
// contact and pops (or splashes) when the host's is destroyed.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WWOEnemyProjectile.generated.h"

UCLASS()
class WHATWASOURS_API AWWOEnemyProjectile : public AActor
{
	GENERATED_BODY()

public:
	AWWOEnemyProjectile();
	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool IsComponentRelevantForNavigation(UActorComponent* Component) const override { return false; } // never shapes the navmesh

	static AWWOEnemyProjectile* Fire(UWorld* World, const FVector& From, const FVector& VelocityCm, float Damage, float Life = 3.f,
		float GravityCm = 0.f, float SplashRadiusM = 0.f, const FLinearColor& Color = FLinearColor(1.f, 0.35f, 0.1f), float SizeM = 0.09f);

	UPROPERTY(Replicated) FVector Velocity = FVector::ZeroVector; // cm/s
	float Damage = 8.f;
	UPROPERTY(Replicated) float Life = 3.f;
	UPROPERTY(Replicated) float Gravity = 0.f;      // cm/s^2
	UPROPERTY(Replicated) float SplashRadius = 0.f; // meters
	UPROPERTY(Replicated) FLinearColor Color = FLinearColor(1.f, 0.35f, 0.1f);
	UPROPERTY(Replicated) float Size = 0.09f;       // meters
	static int32 PlayerHits;  // direct hits on the player, for the wave test log
	static int32 SeenRemote;  // joiner: host shots that arrived here (for -WWOAutoTestNet)

private:
	bool bStopped = false; // a joiner's copy that hit something: waits for the host's to go
	void Splash(const FVector& Point);
	void SplashLooks(const FVector& Point);
	void LandSparks(const FVector& Point);
	// The regression scenarios (-WWORegress, Player/WWORegress.h) call private steps directly; no gameplay code uses this.
	friend class FWWORegress;

	void Build();
};
