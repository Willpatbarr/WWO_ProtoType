#pragma once

// The three enemy types (Godot: enemies/soldier.gd, swarm.gd, tank.gd).
// Soldier: keeps 15-25 m away and strafes, windup glow then 3-shot bursts (soldiers space
//   their bursts out), heads for cover at low health. Head is the crit spot.
// Swarm: fast melee packs that spread out, leap from a few meters, then bite.
// Tank: big and slow, an energy shield that refills, a glowing weak spot on its back (the only
//   crit), lobbed splash shells at range, an arms-up slam up close that slows you.

#include "CoreMinimal.h"
#include "Enemies/WWOEnemy.h"
#include "WWOEnemyTypes.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UWWOSoldierData;
class UWWOSwarmData;
class UWWOTankData;

UCLASS()
class WHATWASOURS_API AWWOSoldier : public AWWOEnemy
{
	GENERATED_BODY()

public:
	AWWOSoldier(const FObjectInitializer& ObjectInitializer);
	virtual void MoveBlockedBy(const FHitResult& Impact) override;
	virtual FString DebugState() const override;

protected:
	virtual UWWOEnemyData* Data() const override;
	virtual void Setup() override;
	virtual FVector Think(float DeltaSeconds) override;
	virtual void Animate(float DeltaSeconds) override;
	virtual void OnReset() override;
	virtual void OnDeath() override;
	virtual void OnHit(bool bCritical) override;
	virtual void StandDown() override { CancelAttack(); }
	virtual void AfterMove(float DeltaSeconds) override;
	virtual float CritRadius() const override { return 14.f; }
	virtual float BodyHeight() const override { return 190.f; }

private:
	UPROPERTY() TObjectPtr<USceneComponent> Gun;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GlowMat;
	UPROPERTY() TObjectPtr<UPointLightComponent> MuzzleLight;
	static double NextBurstTime; // shared by every soldier: spaces out their bursts
	float StrafeDir = 1.f, StrafeLeft = 0.f, Cooldown = 1.f, Windup = -1.f, ShotTimer = 0.f, Glow = 0.f, AimBlend = 0.f, CoverCheck = 0.f;
	int32 ShotsLeft = 0;
	bool bSees = false;
	bool bBlocked = false;
	TWeakObjectPtr<AActor> Cover;
	UWWOSoldierData* D() const;
	FVector ChooseMove(const FVector& ToTarget, float Distance, float DeltaSeconds);
	AActor* NearestCover() const;
	FVector CoverSpot(const AActor* CoverActor) const;
	void UpdateAttack(float DeltaSeconds, bool bCanSee);
	void CancelAttack() { Windup = -1.f; ShotsLeft = 0; }
	void Fire();
	FVector MuzzleLocation() const;
};

UCLASS()
class WHATWASOURS_API AWWOSwarm : public AWWOEnemy
{
	GENERATED_BODY()

public:
	AWWOSwarm(const FObjectInitializer& ObjectInitializer);
	virtual FString DebugState() const override;

protected:
	virtual UWWOEnemyData* Data() const override;
	virtual void Setup() override;
	virtual FVector Think(float DeltaSeconds) override;
	virtual void Animate(float DeltaSeconds) override;
	virtual void OnReset() override;
	virtual void AfterMove(float DeltaSeconds) override;
	virtual float CritRadius() const override { return 15.f; }
	virtual float BodyHeight() const override { return 140.f; }
	virtual float BodyRadius() const override { return 36.f; }

private:
	float LeapCooldown = 0.f, BiteTimer = 0.f, LeapAge = 0.f;
	bool bLeaping = false, bLeapHit = false;
	UWWOSwarmData* D() const;
	void Leap(const FVector& Toward);
	FVector SpreadPush() const;
};

UCLASS()
class WHATWASOURS_API AWWOTank : public AWWOEnemy
{
	GENERATED_BODY()

public:
	AWWOTank(const FObjectInitializer& ObjectInitializer);

	virtual bool IsCriticalComponent(const UPrimitiveComponent* Component) const override;
	virtual float GetShield() const override { return Shield; }
	virtual float GetMaxShield() const override { return MaxShield; }
	virtual bool IsRankAndFile() const override { return false; }
	virtual FString DebugState() const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual UWWOEnemyData* Data() const override;
	virtual void Setup() override;
	virtual FVector Think(float DeltaSeconds) override;
	virtual void Animate(float DeltaSeconds) override;
	virtual float Absorb(float Damage, bool bCritical, const FVector& Point) override;
	virtual void OnReset() override;
	virtual void OnDeath() override;
	virtual void StandDown() override { SlamTime = -1.f; }
	virtual float CritRadius() const override { return 18.f; }
	virtual float BodyHeight() const override { return 280.f; }
	virtual float BodyRadius() const override { return 45.f; }

private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Bubble;
	// Online the host's shield replicates: joiners draw the bubble from it (and guess kills through it).
	UPROPERTY(ReplicatedUsing = OnRep_Shield) float Shield = 0.f;
	UPROPERTY(Replicated) float MaxShield = 0.f;
	UFUNCTION() void OnRep_Shield(float Before);
	void UpdateBubble(float DeltaSeconds);
	float SinceHit = 99.f, LobTimer = 1.5f, SlamCooldown = 0.f, SlamTime = -1.f, Lobbing = 0.f, BubbleFlash = 0.f;
	bool bSlamLanded = false;
	UWWOTankData* D() const;
	void ResetShield();
	void ShieldBreak();
	void Slam();
	void Lob();
};
