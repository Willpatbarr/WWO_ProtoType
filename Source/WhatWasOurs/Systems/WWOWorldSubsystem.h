#pragma once

// Per-level registry and shared settings (Godot: the "targets", "enemies", "teammates",
// "spore_fog" groups, TargetManager and DamageNumbers).
// - Targets register here on BeginPlay; aim assist, melee, grenades and the HUD read them.
// - Floating damage numbers and words ("BLINDED") are queued here; the HUD draws them.
// - Target health / auto respawn settings (Settings -> Targets).

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "Engine/HitResult.h"
#include "WWOWorldSubsystem.generated.h"

struct FCollisionQueryParams;

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

// --- Area queries: "everything within R of this point" (UWWOWorldSubsystem::ForEachTargetInRadius) ---
// The area effects each measure their own way and include their own victims; a query states all of it,
// so every difference between two effects is a field here, not a hand-written loop.

// Which of the registered targets a query visits (first, in the order they registered).
enum class EWWOAreaTargets : uint8
{
	None,
	Living,  // every living target: dummies and enemies (LivingTargets without the players)
	Enemies, // living enemies only (LivingEnemies: no dummies)
};

// Which players it visits (next, in actor-iterator order: GetPlayers' order). For is the query's For.
enum class EWWOAreaPlayers : uint8
{
	None,
	FoesOf,    // PvP only: the living players For may hurt (CanHurt(For, P)), as LivingTargets(For) adds them
	FriendsOf, // the living players For may not hurt, For's own player left out (co-op: every other player)
	All,       // every player, dead or not
};

// Where on a victim the distance is measured to. Actors that aren't IWWOTargetable (AI teammates)
// are measured to their actor location whatever this says.
enum class EWWOAreaMeasure : uint8
{
	Same,        // (PlayersMeasure only) as Measure
	Feet,        // IWWOTargetable::FeetLocation
	Centre,      // the actor location (a character's capsule centre)
	Chest,       // the feet + 0.8 m
	NearestPart, // the nearest aim part's surface (distance minus its radius, never below 0); the point is that part's centre
	BodyCenter,  // IWWOTargetable::BodyCenter (lightning and poison aim here)
	LastPart,    // the last aim part's centre (the body); its radius comes with the hit
};

struct FWWOAreaQuery
{
	FVector Center = FVector::ZeroVector;
	float Radius = 0.f;             // cm
	bool bInclusive = true;         // within is <= Radius; false: < Radius
	bool bFloatDistance = false;    // the distance is rounded to a float before the test (sites that kept it in a float)
	// Flat: the distance on the ground plane only, and the height difference must be under MaxHeight
	// (bHeightInclusive: at most MaxHeight).
	bool bFlat = false;
	float MaxHeight = 0.f;          // cm
	bool bHeightInclusive = false;
	EWWOAreaMeasure Measure = EWWOAreaMeasure::Feet;
	EWWOAreaMeasure PlayersMeasure = EWWOAreaMeasure::Same; // the players may be measured another way
	// Who: targets, then players, then living AI teammates (bTeammatesFirst: the teammates first).
	EWWOAreaTargets Targets = EWWOAreaTargets::Living;
	EWWOAreaPlayers Players = EWWOAreaPlayers::FoesOf;
	bool bTeammates = false;
	bool bTeammatesFirst = false;
	const UObject* For = nullptr;   // whose effect (the team rules of FoesOf / FriendsOf)
	// Line of sight: with SightParams set, a victim counts only if a Visibility trace from SightFrom to
	// its measured point hits nothing or hits the victim.
	const FCollisionQueryParams* SightParams = nullptr;
	FVector SightFrom = FVector::ZeroVector;
};

// One victim of a query.
struct FWWOAreaHit
{
	AActor* Actor = nullptr;
	IWWOTargetable* Target = nullptr;    // null for an AI teammate
	FVector Point = FVector::ZeroVector; // the point measured to
	float Distance = 0.f;                // cm, as measured (flat for a flat query)
	float PartRadius = 0.f;              // NearestPart / LastPart: that part's radius
};

// The best aim part a cone helper found (UWWOWorldSubsystem::BestAimPart).
struct FWWOAimPick
{
	AActor* Actor = nullptr;
	FWWOAimPart Part;
	FHitResult Hit; // the sight line's hit on it
	float Score = TNumericLimits<float>::Max();
	bool IsValid() const { return Actor != nullptr; }
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

	// Area effects: every victim Query includes within its radius (and in sight, if it asks), in order
	// (targets, players, teammates). Visit returns false to stop there. Builds no list.
	void ForEachTargetInRadius(const FWWOAreaQuery& Query, TFunctionRef<bool(const FWWOAreaHit&)> Visit) const;
	// The nearest victim within the radius that Admit lets through and (if the query asks) is in sight;
	// only a victim nearer than the best so far is traced, and a tie goes to the first. False: none.
	bool NearestTargetInRadius(const FWWOAreaQuery& Query, TFunctionRef<bool(const AActor*)> Admit, FWWOAreaHit& Out) const;
	// Aim cones (melee, lunge, aim assist): over the aim parts of LivingTargets(For), in order (head first
	// within each), Score gives a part's score, or false when it is not in this cone (Best: the best
	// score so far, Max at first, for callers that skip early). A part scoring below the best is checked
	// with Sight (a trace to its centre, which must hit that actor) and then kept. Builds no list.
	FWWOAimPick BestAimPart(const UObject* For, TFunctionRef<bool(const AActor* Actor, const FWWOAimPart& Part, float Best, float& OutScore)> Score,
		TFunctionRef<bool(const FVector& To, FHitResult& OutHit)> Sight) const;

	// PvP (team deathmatch).
	static bool IsPvP(const UObject* WorldContext);
	static int32 TeamOf(const AActor* Player); // -1: not a player (or no team yet)
	// Can Attacker (a player or anything they own) hurt Victim? Enemies and dummies always; players
	// only in PvP, and only the other team's (no friendly fire, never yourself); AI teammates
	// (AWWOAlly) never by a player in co-op (anyone else, enemies, still can).
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
