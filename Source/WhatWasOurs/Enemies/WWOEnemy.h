#pragma once

// What every enemy (Soldier, Swarm, Tank) shares (Godot: enemies/enemy.gd):
// health, hits, damage numbers, the white hit flash, death, corpse and respawn; the target
// interface; a crit spot pinned to a bone; finding and seeing the player (or the closest
// teammate), turning, and pathing around walls on the navmesh.
// Each type fills in Think() (returns the velocity it wants, m/s) and Animate().

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Systems/WWOTargetable.h"
#include "Systems/WWONav.h"
#include "WWOEnemy.generated.h"

class UBoxComponent;
class AWWOCharacter;
class UCapsuleComponent;
class UWWOBodyComponent;
class UWWOEnemyData;

DECLARE_MULTICAST_DELEGATE_OneParam(FWWOOnEnemyDied, class AWWOEnemy*);

// Enemy movement: the velocity eases toward what the AI wants at a set acceleration (and
// knockback / leaps are left alone while bSteering is off).
UCLASS()
class WHATWASOURS_API UWWOEnemyMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UWWOEnemyMovement();
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;

	FVector Wish = FVector::ZeroVector; // cm/s
	bool bSteering = true;
	float Accel = 1200.f; // cm/s^2 toward the wanted velocity
};

UCLASS(Abstract)
class WHATWASOURS_API AWWOEnemy : public ACharacter, public IWWOTargetable
{
	GENERATED_BODY()

public:
	AWWOEnemy(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// IWWOTargetable
	virtual TArray<FWWOAimPart> GetAimParts() const override;
	virtual bool IsCriticalComponent(const UPrimitiveComponent* Component) const override;
	virtual bool ApplyHit(float Damage, bool bCritical, const FVector& Point, FLinearColor Color) override;
	virtual bool IsDead() const override { return bDead; }
	virtual float GetHealth() const override { return Health; }
	virtual float GetMaxHealth() const override { return MaxHealth; }
	virtual float GetBodyHeight() const override { return BodyHeight() + 25.f; }
	virtual void KnockBack(const FVector& Push) override;
	virtual void ResetTarget() override;
	virtual FVector FeetLocation() const override;

	UPROPERTY(VisibleAnywhere) TObjectPtr<UCapsuleComponent> BodyHitbox;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> CritBox;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UWWOBodyComponent> Body;

	FWWOOnEnemyDied OnDied;
	TWeakObjectPtr<class AWWOCharacter> LastHitBy; // host: the player whose hit landed last (FootRace's kill credit)
	bool bAlerted = false;
	bool bRespawns = true; // wave enemies don't
	float SlowLeft = 0.f, SlowAmount = 0.f, BlindLeft = 0.f;

	// For the wave test log: what it's doing, who it's after, and the velocity it wants (cm/s).
	virtual FString DebugState() const { return bAlerted ? TEXT("hunt") : TEXT("idle"); }
	AActor* GetTargetActor() const { return TargetActor.Get(); }
	FVector GetWish() const;
	FString PathDebug() const;

	// An enemy's attack lands on a friendly (Victim: a player or an AI teammate; anything else is left
	// alone). A player's hit mark points back at CameFrom, then the damage (shield first; bMelee for
	// Melee Guard); an AI teammate takes it on its shield, then its health. Every bite, leap, bolt,
	// splash and slam goes through here: no attacker scope (FWWODamageBy), no push. Returns the player
	// hurt, if it was one (the slam slows them).
	static AWWOCharacter* HurtFriendly(AActor* Victim, float Amount, const FVector& CameFrom, bool bMelee = false);

	void ApplyBlind(float Seconds);
	void ApplySlow(float Amount, float Seconds);
	virtual bool IsRankAndFile() const { return true; } // spore fog hurts these (not the Tank)

protected:
	virtual UWWOEnemyData* Data() const { return nullptr; }
	virtual void Setup() {}
	virtual FVector Think(float DeltaSeconds) { return FVector::ZeroVector; }
	virtual void Animate(float DeltaSeconds) {}
	virtual void AnimateRemote(float DeltaSeconds) {} // a joiner's copy: the looks the host's Animate sets by hand
	virtual float Absorb(float Damage, bool bCritical, const FVector& Point) { return Damage; }
	virtual void OnHit(bool bCritical) {}
	virtual void OnReset() {}
	virtual void OnDeath() {}
	virtual void StandDown() {}
	virtual void AfterMove(float DeltaSeconds) {}
	virtual float CritRadius() const { return 15.f; }
	virtual float BodyHeight() const { return 180.f; }
	virtual float BodyRadius() const { return 35.f; }

	// Helpers for the types.
	UWWOEnemyMovement* EnemyMovement() const;
	FVector PathDirection(const FVector& Goal, float DeltaSeconds);
	bool WallAhead(const FVector& Direction, FVector* OutNormal = nullptr) const;
	FVector FlatToTarget() const;
	FVector TargetAimPoint() const;
	bool CanSeeTarget(const FVector& From) const;
	bool LedgeAhead(const FVector& Direction) const; // no floor a step that way: don't walk off it
	float HeightToTarget() const;                    // cm between its feet and the target's, unsigned
	void TurnToward(const FVector& Direction, float DeltaSeconds, float Speed = -1.f);
	void DamageTarget(float Amount, bool bMelee = false);
	void PinCrit(FName Bone, const FVector& WorldPoint, const FVector& HalfExtentCm);
	FCollisionQueryParams IgnoreEnemies() const;

	TWeakObjectPtr<AActor> TargetActor; // the player or the closest teammate
	UPROPERTY(Replicated) float Health = 100.f;
	UPROPERTY(Replicated) float MaxHealth = 100.f;
	UPROPERTY(ReplicatedUsing = OnRep_Dead) bool bDead = false;
	UFUNCTION() void OnRep_Dead(); // a joiner: a corpse stops blocking its shots and its steps too
	float ModelScale = 1.f;

private:
	FTransform SpawnTransform;
	float Flash = 0.f;
	bool bDeathShown = false; // the death burst has played for this death
	float RetargetLeft = 0.f;
	float CorpseLeft = -1.f;
	float RespawnLeft = -1.f;
	FWWOPathFollower Path = FWWOPathFollower(FVector(1e9f));
	float SidestepSide = 1.f;

	float StuckFor = 0.f, UnstickLeft = 0.f, UnstickSide = 1.f;
	FVector UnstickDir = FVector::ZeroVector, LastWishDir = FVector::ZeroVector;

	void Retarget(float DeltaSeconds);
	FVector Unstick(const FVector& Wish, float DeltaSeconds);
	void Die();
	void SetCollision(bool bOn);
	bool TargetIsInvisible() const;
	// The regression scenarios (-WWORegress, Player/WWORegress.h) call private steps directly; no gameplay code uses this.
	friend class FWWORegress;
};
