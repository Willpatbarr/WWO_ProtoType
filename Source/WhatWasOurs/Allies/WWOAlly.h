#pragma once

// An AI teammate (Godot: allies/ally.gd), for testing team abilities like Shield Share.
// Follows the player (FollowMin to FollowMax meters away, pathing around walls), shoots the
// nearest enemy it can see (hitscan + tracer), has shields and health with regen. Enemies
// target whoever is closest, so it takes real damage. When killed it goes down, then gets
// back up next to you.

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
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

	float Health = 100.f;
	float Shield = 100.f;
	float DownLeft = 0.f;
	bool IsDown() const { return bDown; }

	void TakeAllyDamage(float Amount, bool bMelee = false);
	void RestoreShield(float Amount); // Shield Share
	void ResetAlly();
	FString StatusText() const;

private:
	UPROPERTY() TObjectPtr<USceneComponent> Gun;
	bool bDown = false;
	float SinceHit = 99.f;
	float FireCooldown = 0.5f;
	TArray<FVector> Path;
	float PathAge = 99.f;

	UWWOAllyData* D() const;
	void GoDown();
	void GetUp();
	void Regen(float DeltaSeconds);
	FVector Follow(float DeltaSeconds);
	void Face(const FVector& Direction, float DeltaSeconds);
	AWWOEnemy* NearestVisibleEnemy() const;
	void Shoot(AWWOEnemy* Enemy);
};
