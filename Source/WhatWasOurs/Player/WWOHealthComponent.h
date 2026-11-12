#pragma once

// The player's health and shields, Destiny-style (Godot: player_health.gd).
// Damage hits the shield first. After RechargeDelay without damage, health refills, then
// the shield. At 0 health you die and respawn at the Player Start after RespawnTime.
// Online (co-op) you go down instead: you stay where you fell until a teammate revives you (holds
// Revive within ReviveRange for ReviveTime); no respawn. Everyone down loses the round.
// Falling below KillZ counts as a death too. Durability points raise the maximums.
// Online the host owns every player's health: damage, regen, death and respawn happen there, and
// Health / Shield / bDead replicate to everyone. Your own machine replays the effects from them
// (OnDamaged when they drop, death and respawn when bDead flips).

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WWOHealthComponent.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FWWOOnDamaged, float);
DECLARE_MULTICAST_DELEGATE(FWWOOnDied);

UCLASS(ClassGroup = (WWO))
class WHATWASOURS_API UWWOHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWWOHealthComponent();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void UpdateHealth(float DeltaSeconds); // called by the character

	UPROPERTY(EditAnywhere, Category = "Player") float BaseHealth = 100.f;
	UPROPERTY(EditAnywhere, Category = "Player") float BaseShield = 100.f;
	UPROPERTY(EditAnywhere, Category = "Player") float RechargeDelay = 4.f;
	UPROPERTY(EditAnywhere, Category = "Player") float HealthRegen = 40.f;
	UPROPERTY(EditAnywhere, Category = "Player") float ShieldRegen = 60.f;
	UPROPERTY(EditAnywhere, Category = "Player") float RespawnTime = 2.f;
	UPROPERTY(EditAnywhere, Category = "Player") float GodMode = 0.f;
	UPROPERTY(EditAnywhere, Category = "Player") float ReviveTime = 3.f;    // s of holding Revive
	UPROPERTY(EditAnywhere, Category = "Player") float ReviveRange = 2.5f;  // m from the downed player
	UPROPERTY(EditAnywhere, Category = "Player") float ReviveHealth = 0.5f; // of max health, no shield

	static constexpr float KillZ = -3000.f; // cm
	static float MapKillHeight(const UWorld* World); // this map's (enemies and allies die below it too)
	float MapKillZ = KillZ; // cm: a map can raise it with a TargetPoint tagged KillZ (L_Cascade: its chasm kills at -9 m)

	UPROPERTY(ReplicatedUsing = OnRep_Health) float Health = 100.f;
	UPROPERTY(ReplicatedUsing = OnRep_Health) float Shield = 100.f;
	UPROPERTY(ReplicatedUsing = OnRep_Dead) bool bDead = false;
	UPROPERTY(Replicated) bool bRevived = false;      // the last time bDead cleared was a revive (in place)
	UPROPERTY(Replicated) float SpawnYaw = 0.f;       // the way the last respawn faced (a joiner turns its own view to it)
	double LastAttackTime = -1000.0;                  // host: when LastAttacker last hit you
	UPROPERTY(Replicated) float BeingRevived = 0.f;   // while down: how far a teammate's revive is (0-1)
	TWeakObjectPtr<AActor> LastAttacker; // host: who hit you last (PvP kill credit)
	bool bWantsRevive = false; // host: this player holds Revive (their own input, or ServerSetRevive)
	float ReviveProgress = 0.f; // host: this player's revive of ReviveTarget
	TWeakObjectPtr<class AWWOCharacter> ReviveTarget;
	float SinceDamage = 99.f;
	float RespawnLeft = 0.f;

	FWWOOnDamaged OnDamaged;
	FWWOOnDied OnDied;
	FWWOOnDied OnRespawned;

	float MaxHealth() const;
	float MaxShield() const;
	void TakeDamage(float Amount, bool bMelee = false); // bMelee: a bite, punch or slam
	// Where damage came from, for the HUD's direction markers. The host calls DamageCameFrom next to
	// the damage; it reaches the machine of the player who was hit, which keeps the marks.
	struct FDamageMark { FVector From; double Time; };
	TArray<FDamageMark> DamageMarks; // your own player's, on your machine (newest last)
	void DamageCameFrom(const FVector& From);
	void AddDamageMark(const FVector& From);
	void Kill();
	void Respawn();
	void Revive();         // host: back up where you fell
	bool IsOnline() const; // co-op: down instead of dead-and-respawn
	class AWWOCharacter* FindReviveTarget() const; // the nearest downed teammate in reach (any machine)

private:
	FTransform Spawn;
	float LastSeen = -1.f; // Health + Shield when last replicated (a drop = damage)
	void Die();
	void DieLocally();     // the effects of dying (no moving), on whichever machine
	void UpdateRevive(float DeltaSeconds); // host: this player reviving someone
	void SetBodyBlocks(bool bBlocks);      // down: other players walk through you (on every machine, so moves agree)
	UFUNCTION() void OnRep_Health();
	UFUNCTION() void OnRep_Dead();
	bool IsHost() const;
};
