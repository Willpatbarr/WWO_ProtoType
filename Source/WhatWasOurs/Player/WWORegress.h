#pragma once

// The regression scenarios (-WWORegress): many tiny, isolated, exactly repeatable runs of game logic,
// each set up, run for a fixed number of ticks, recorded and torn down, compared against a golden file
// in git (Tools/regress/golden.json). See docs/TESTING.md, "Regression scenarios".
// - Made by the test driver (FWWOAutoTest) only when the command line has -WWORegress, ticked once a
//   frame from the player controller's Tick (before the characters tick).
// - The game runs on a fixed time step (1/60 s a frame, set here), so scenarios count ticks, never
//   real time. The global random generator is seeded at the start of each scenario from its name.
// - Each scenario starts from the same state: the player reset and put at the same spot, a fixed
//   loadout (no traits, standard frame parts, no mods), the shipped settings, nothing else in the
//   world. Everything a scenario spawned (and everything spawned because of it) is destroyed after.
// - Scenario families live in Player/WWORegress<Family>.cpp; each adds its scenarios in a Register
//   function called from FWWORegress::BuildScenarios.

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "WWOTypes.h"

class AWWOPlayerController;
class AWWOCharacter;
class AWWOTarget;
class AWWOEnemy;
class AWWOAlly;
class AWWOBotController;
class UWWOLoadoutSubsystem;
class UWWOWeaponData;
class UWWOGrenadeData;
class UWWOAbilityData;
class UWWOFloraData;
class UWWOModData;
class FWWORegress;

// Which run modes a scenario's numbers hold for.
enum class EWWORegressModes : uint8
{
	Same,       // one set of numbers for both modes (the default; a disagreement between modes fails)
	PerMode,    // the numbers legitimately differ with rendering: golden keeps one set per mode
	RenderOnly, // needs the game to draw: skipped (and reported skipped) without rendering
};

struct FWWORegressScenario
{
	FString Name;
	bool bPvP = false;          // team deathmatch rules (the game state's bPvP; no match, no bots of its own)
	EWWORegressModes Modes = EWWORegressModes::Same;
	FString ModeReason;         // why, when Modes isn't Same
	int32 MaxTicks = 1200;      // a scenario still running after this many ticks is cut off (timeout=1)
	// Spawn what it needs, set its loadout and tunables. Runs before the settle ticks.
	TFunction<void(FWWORegress&)> Setup;
	// One tick of its script (Tick counts from 0 after the settle ticks). Returns true when it is
	// done, having recorded its numbers with Record.
	TFunction<bool(FWWORegress&, int32)> Step;
};

class FWWORegress : public FGCObject
{
public:
	explicit FWWORegress(AWWOPlayerController& Controller);
	void Tick();

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FWWORegress"); }

	// --- For scenarios -------------------------------------------------------------------------
	AWWOPlayerController* PC = nullptr;
	AWWOCharacter* Player() const;
	UWorld* World() const;
	UWWOLoadoutSubsystem* Loadout() const;
	static constexpr float Dt = 1.f / 60.f;
	static int32 Ticks(float Seconds) { return FMath::RoundToInt(Seconds * 60.f); }

	// Where every scenario happens: an empty stretch of floor, the player facing +X... (Home()).
	FVector Home() const { return HomeFeet; }                 // the player's feet
	FVector Ahead(float Meters, float SideMeters = 0.f) const; // a floor point in front of the player
	float HomeYaw() const { return HomeYawDeg; }

	// Spawns (tracked, destroyed after the scenario). Feet on the floor at At, facing the player.
	AWWOTarget* SpawnDummy(const FVector& At, float Health = -1.f);
	AWWOEnemy* SpawnEnemy(FName Kind, const FVector& At, bool bInert = true); // soldier, swarm, tank
	AWWOAlly* SpawnAlly(const FVector& At);
	AWWOCharacter* SpawnBot(int32 Team, const FVector& At, float FaceYaw = 1000.f); // parked; 1000 = face the player
	void SetPlayerTeam(int32 Team);

	// The loadout (in memory only; put back after the scenario).
	void EquipWeapon(FName Id);
	void EquipMod(FName Id);              // a mod on the equipped weapon (its own slot)
	void EquipFlora(FName Id);            // clears grenade, abilities, traits as the game does
	void EquipGrenade(FName Id);
	void EquipAbility(int32 Slot, FName Id);
	UWWOWeaponData* WeaponAsset(FName Id) const;
	UWWOGrenadeData* GrenadeAsset(FName Id) const;
	UWWOAbilityData* AbilityAsset(FName Id) const;

	// A float property on an object set for this scenario only (by name, as Settings does).
	void Tune(UObject* Object, FName Property, float Value);

	// Input, read by the character next tick (as a scripted press).
	void Press(EWWOAction Action);
	void Hold(EWWOAction Action, bool bDown);
	void Move(FVector2D Axis);
	void AimAt(const FVector& Point); // turn the player's aim (the reticle) onto a point
	void Face(float YawDeg, float PitchDeg = 0.f);
	void PlaceAt(const FVector& Feet, float YawDeg); // the player's feet there, stopped

	// Results.
	void Record(const FString& Key, const FString& Value);
	void Record(const FString& Key, float Value);
	void Record(const FString& Key, int32 Value);
	void RecordBool(const FString& Key, bool bValue) { Record(Key, bValue ? 1 : 0); }
	void RecordVec(const FString& Key, const FVector& V); // relative to Home, cm

	// Helpers many scenarios share.
	static float HealthOf(const AActor* A);  // health + shield of anything targetable
	static FString Rel(float Cm) { return FString::Printf(TEXT("%.2f"), Cm / 100.f); }
	void NotePlayerShot(); // hits marked and texts shown since the last call (for the damage pipeline)
	int32 ShotsMarkedSince = 0;
	int32 TextsAtStart = 0;
	// The player's hit confirmations (OnHitConfirmed: the hit marker), counted from the scenario's start.
	int32 Confirms = 0, ConfirmCrits = 0, ConfirmKills = 0;

private:
	TArray<FWWORegressScenario> Scenarios;
	void BuildScenarios();
	// The scenario families, one file each (members, so their scenarios may read the private state
	// the gameplay classes open to this class).
	static void AddGunScenarios(TArray<FWWORegressScenario>& Out, const UWWOLoadoutSubsystem& L);    // WWORegressGuns.cpp: weapons, perks, mod stats
	static void AddKitScenarios(TArray<FWWORegressScenario>& Out, const UWWOLoadoutSubsystem& L);    // WWORegressKit.cpp: grenades, abilities, ults, flora effects
	static void AddCombatScenarios(TArray<FWWORegressScenario>& Out, const UWWOLoadoutSubsystem& L); // WWORegressCombat.cpp: melee, damage pipeline, enemies
	static void AddAreaScenarios(TArray<FWWORegressScenario>& Out, const UWWOLoadoutSubsystem& L);   // WWORegressArea.cpp: the edges of area effects and aim cones
	enum class EPhase : uint8 { Boot, Setup, Settle, Run, Teardown, Gap, Done };
	EPhase Phase = EPhase::Boot;
	int32 PhaseTicks = 0;
	int32 Index = -1;
	int32 StepTick = 0;
	bool bUpdate = false;
	bool bRender = true;
	FString Mode() const { return bRender ? TEXT("render") : TEXT("norender"); }
	double StartedAt = 0.0;
	int32 Skipped = 0;
	int32 Problems = 0; // scenario list errors (two with one name)

	// What this scenario recorded, in order.
	TArray<TPair<FString, FString>> Current;
	TMap<FString, TArray<TPair<FString, FString>>> All; // golden key -> values
	TArray<FString> Order;

	// Isolation.
	TSet<TWeakObjectPtr<AActor>> Baseline;
	TArray<TWeakObjectPtr<AActor>> Spawned;
	TArray<TWeakObjectPtr<AController>> SpawnedControllers;
	struct FTuned { TWeakObjectPtr<UObject> Object; FName Property; float Before = 0.f; };
	TArray<FTuned> Tuned;
	struct FLoadoutSnapshot;
	TSharedPtr<FLoadoutSnapshot> SavedLoadout;
	FVector HomeFeet = FVector::ZeroVector;
	float HomeYawDeg = 180.f;
	int32 PlayerTeamBefore = 0;

	void BeginScenario();
	void EndScenario(bool bTimedOut);
	void Teardown();
	void ResetPlayer();
	void BaselineLoadout();
	void SnapshotLoadout();
	void RestoreLoadout();
	void Finish();
	FString GoldenKey(const FWWORegressScenario& S) const;
	static uint32 SeedFor(const FString& Name);
	static FString Quote(const FString& S);
	static bool ParseResults(const FString& Text, TMap<FString, TMap<FString, FString>>& Out);
	static bool LoadResults(const FString& Path, TMap<FString, TMap<FString, FString>>& Out);
	static FString ResultsText(const TMap<FString, TArray<TPair<FString, FString>>>& Results);
	static bool CheckResultsRoundTrip();
	static bool SaveResults(const FString& Path, const TMap<FString, TArray<TPair<FString, FString>>>& Results);
};
