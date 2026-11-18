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
	virtual void AnimateRemote(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
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
	// For joiners: how bright the muzzle's windup glow should be (0-255) and whether it is in a fight
	// (gun raised). The host sets both in Animate; a joiner's copy follows them in AnimateRemote.
	UPROPERTY(Replicated) uint8 NetGlow = 0;
	UPROPERTY(Replicated) bool bNetAlerted = false;
	void PlaceGun(float Aim, const FVector* AimAt);
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
	// The regression scenarios (-WWORegress, Player/WWORegress.h) call private steps directly; no gameplay code uses this.
	friend class FWWORegress;
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
	// The regression scenarios (-WWORegress, Player/WWORegress.h) call private steps directly; no gameplay code uses this.
	friend class FWWORegress;
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

protected:
	// Online the host's shield replicates: joiners draw the bubble from it (and guess kills through it).
	// (Protected: the FootRace boss turns the Tank's own shield off.)
	UPROPERTY(ReplicatedUsing = OnRep_Shield) float Shield = 0.f;
	UPROPERTY(Replicated) float MaxShield = 0.f;

private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Bubble;
	UFUNCTION() void OnRep_Shield(float Before);
	void UpdateBubble(float DeltaSeconds);
	float SinceHit = 99.f, LobTimer = 1.5f, SlamCooldown = 0.f, SlamTime = -1.f, Lobbing = 0.f, BubbleFlash = 0.f;
	bool bSlamLanded = false;
	UWWOTankData* D() const;
	void ResetShield();
	void ShieldBreak();
	void Slam();
	void Lob();
	// The regression scenarios (-WWORegress, Player/WWORegress.h) call private steps directly; no gameplay code uses this.
	friend class FWWORegress;

};

// FootRace's boss (Game/WWOFootRaceRules.h): a bigger, tougher Tank, for now. Its health is the game
// mode's BossHealth (the rules set it before it spawns). It has none of the Tank's own shield; instead the
// rules hold a boss shield up (bShielded) that takes every hit, and drop it for a damage window.
UCLASS()
class WHATWASOURS_API AWWOBoss : public AWWOTank
{
	GENERATED_BODY()

public:
	AWWOBoss(const FObjectInitializer& ObjectInitializer);

	static constexpr float Size = 1.6f;   // times a Tank
	float BossHealth = 0.f;               // host, before FinishSpawning: its health (0: the Tank's)
	UPROPERTY(ReplicatedUsing = OnRep_Shielded) bool bShielded = true;
	void SetShielded(bool bOn);           // host
	void Heal(float Amount);              // host: up to full health

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool IsRankAndFile() const override { return false; }
	float GetHealthFraction() const { return MaxHealth > 0.f ? Health / MaxHealth : 0.f; }

protected:
	virtual void Setup() override;
	virtual float Absorb(float Damage, bool bCritical, const FVector& Point) override;
	virtual float BodyHeight() const override { return 280.f * Size; }
	virtual float BodyRadius() const override { return 45.f * Size; }

private:
	UFUNCTION() void OnRep_Shielded();
	void ShowShield();
	UPROPERTY() TObjectPtr<UStaticMeshComponent> BossBubble;
};
