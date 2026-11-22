#pragma once

// An AI teammate (Godot: allies/ally.gd), for testing team abilities like Shield Share.
// Follows the player (FollowMin to FollowMax meters away, pathing around walls), shoots the
// nearest enemy it can see (hitscan + tracer), has shields and health with regen. Enemies
// target whoever is closest, so it takes real damage. When killed it goes down, then gets
// back up next to you.

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Systems/WWONav.h"
#include "WWOAlly.generated.h"

class UWWOBodyComponent;
class UWWOAllyData;
class AWWOEnemy;

UCLASS()
class WHATWASOURS_API AWWOAlly : public ACharacter
{
	GENERATED_BODY()

public:
	AWWOAlly(const FObjectInitializer& ObjectInitializer);
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere) TObjectPtr<UWWOBodyComponent> Body;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// The host's; replicated so a joiner's HUD shows them and a downed ally stops blocking it.
	UPROPERTY(Replicated) float Health = 100.f;
	UPROPERTY(Replicated) float Shield = 100.f;
	UPROPERTY(Replicated) float DownLeft = 0.f;
	bool IsDown() const { return bDown; }

	void TakeAllyDamage(float Amount, bool bMelee = false);
	void RestoreShield(float Amount); // Shield Share
	float Heal(float Amount);         // host: health, then shield; returns what it restored (nothing while down)
	void ResetAlly();
	FString StatusText() const;

private:
	UPROPERTY() TObjectPtr<USceneComponent> Gun;
	UPROPERTY(ReplicatedUsing = OnRep_Down) bool bDown = false;
	UFUNCTION() void OnRep_Down();
	void ShowDown(bool bIsDown); // collision and the gun, on every machine
	float SinceHit = 99.f;
	float FireCooldown = 0.5f;
	FWWOPathFollower Path;

	UWWOAllyData* D() const;
	void GoDown();
	void GetUp();
	void Regen(float DeltaSeconds);
	FVector Follow(float DeltaSeconds);
	void Face(const FVector& Direction, float DeltaSeconds);
	AWWOEnemy* NearestVisibleEnemy() const;
	void Shoot(AWWOEnemy* Enemy);
	// The regression scenarios (-WWORegress, Player/WWORegress.h) call private steps directly; no gameplay code uses this.
	friend class FWWORegress;
};
