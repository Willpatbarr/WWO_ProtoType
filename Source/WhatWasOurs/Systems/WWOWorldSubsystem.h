#pragma once

// Per-level registry and shared settings (Godot: the "targets", "enemies", "teammates",
// "spore_fog" groups, TargetManager and DamageNumbers).
// - Targets register here on BeginPlay; aim assist, melee, grenades and the HUD read them.
// - Floating damage numbers and words ("BLINDED") are queued here; the HUD draws them.
// - Target health / auto respawn settings (Settings -> Targets).

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "WWOWorldSubsystem.generated.h"

class AWWOEnemy;
class AWWOAlly;
class AWWOSporeFog;
class AWWOCharacter;

struct FWWOFloatingText
{
	FString Text;
	FVector Start;
	FLinearColor Color;
	bool bCritical = false;
	float Age = 0.f;
	float Jitter = 0.f;
	TWeakObjectPtr<AActor> Follow; // attached labels (ally status) stay over their actor

	// Godot's DamageNumbers tween: rise 0.6 m over 0.7 s; fade out over 0.7 s after 0.25 s.
	static constexpr float RiseTime = 0.7f;
	static constexpr float FadeDelay = 0.25f;
	static constexpr float FadeTime = 0.7f;
	static constexpr float Lifetime = FadeDelay + FadeTime;
	float Alpha() const { return 1.f - FMath::Clamp((Age - FadeDelay) / FadeTime, 0.f, 1.f); }
};

// While one of these lives, hits are credited to Attacker (see UWWOWorldSubsystem::CurrentAttacker).
struct WHATWASOURS_API FWWODamageBy
{
	explicit FWWODamageBy(const UObject* Attacker);
	~FWWODamageBy();
private:
	TWeakObjectPtr<AActor> Previous;
};

UCLASS()
class WHATWASOURS_API UWWOWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UWWOWorldSubsystem* Get(const UObject* WorldContext);

	// Settings -> Targets
	UPROPERTY(EditAnywhere, Category = "Targets") float TargetHealth = 200.f;
	UPROPERTY(EditAnywhere, Category = "Targets") float RespawnTime = 2.f; // 0 = only on reset

	void RegisterTarget(AActor* Actor);
	void UnregisterTarget(AActor* Actor);
	void RegisterTeammate(AWWOAlly* Ally);
	void UnregisterTeammate(AWWOAlly* Ally);
	void RegisterFog(AWWOSporeFog* Fog);
	void UnregisterFog(AWWOSporeFog* Fog);

	// Living targets (dummies and enemies that aren't dead). In PvP, with For (a player, or anything
	// a player owns: their gun, grenade, pool...), the other team's living players too.
	TArray<AActor*> LivingTargets(const UObject* For = nullptr) const;
	TArray<AActor*> AllTargets() const;
	TArray<AWWOEnemy*> LivingEnemies() const;
	TArray<AWWOAlly*> LivingTeammates() const;
	TArray<AWWOAlly*> AllTeammates() const;
	const TArray<TWeakObjectPtr<AWWOSporeFog>>& Fogs() const { return SporeFogs; }
	AWWOCharacter* GetPlayer() const; // the player on this machine
	TArray<AWWOCharacter*> GetPlayers() const; // every player in the match (online: all of them)

	// PvP (team deathmatch).
	static bool IsPvP(const UObject* WorldContext);
	static int32 TeamOf(const AActor* Player); // -1: not a player (or no team yet)
	// Can Attacker (a player or anything they own) hurt Victim? Enemies and dummies always; players
	// only in PvP, and only the other team's (no friendly fire, never yourself).
	static bool CanHurt(const UObject* Attacker, const AActor* Victim);
	// The player a context belongs to: itself, a component's owner, or an actor's owner (or
	// instigator), a few levels up (a grenade's thrower, a pool's...).
	static AWWOCharacter* PlayerOf(const UObject* Context);
	// Who is dealing the damage being applied right now (host): set around a hit so a player's
	// TakeHit knows who to credit (FWWODamageBy).
	static TWeakObjectPtr<AActor> CurrentAttacker;
	// Any damage a player deals marks their reticle, whatever dealt it (gun, fist, grenade, fog,
	// lightning...): every TakeHit ends here. On the attacker's own machine it shows at once; the
	// host tells a joiner. Neither happens while bAttackerSawHit is set: the attacker marks that hit
	// itself, once (the host applying a joiner's shot or punch; the gun's Shoot and the fist's
	// MeleeStrike on the shooter's own machine, which mark once per shot with the predicted kill).
	static void ConfirmHit(AActor* Victim, bool bCritical, bool bKilled);
	static bool bAttackerSawHit;
	static int32 HitsMarked; // how many reached this machine's own reticle (tests read it)

	// Bring every target back at full health; enemies reset to their spawn. Teammates too.
	void ResetTargets();
	void SetTargetHealth(float Value);

	// Damage numbers and floating words.
	static FLinearColor CritColor() { return FLinearColor(1.f, 0.85f, 0.2f); }
	void SpawnDamageNumber(float Damage, bool bCritical, const FVector& Point, FLinearColor Color = FLinearColor(0, 0, 0, 0));
	void ShowText(const FString& Text, bool bCritical, const FVector& Point, FLinearColor Color = FLinearColor(0, 0, 0, 0));
	const TArray<FWWOFloatingText>& FloatingTexts() const { return Texts; }

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UWWOWorldSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickableWhenPaused() const override { return false; }

private:
	TArray<TWeakObjectPtr<AActor>> Targets;
	TArray<TWeakObjectPtr<AWWOAlly>> Teammates;
	TArray<TWeakObjectPtr<AWWOSporeFog>> SporeFogs;
	TArray<FWWOFloatingText> Texts;
};
