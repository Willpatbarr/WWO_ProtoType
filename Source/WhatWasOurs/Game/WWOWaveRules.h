#pragma once

// The PvE waves (Godot: wave_manager.gd), the game mode's rules unless the map was opened with ?pvp
// or ?tester.
// - If the level has enemy spawn points (Target Points tagged "EnemySpawn", plus a tag per enemy
//   kind they allow) and the game mode's Enabled is on, the hand-placed enemies are cleared, a
//   countdown runs (StartDelay), then waves. Otherwise it stays Idle: the level is a sandbox.
// - Each wave's enemies come in one by one (SpawnStagger) from spawn points away from every player
//   and hunt them. Clear them all and the next wave starts after BreakTime.
// - Clear WavesToWin waves: ARENA CLEARED. Every player down (alone: and no AI teammate standing):
//   DEFEATED, and the run restarts 4 s later.
// The tunables are the game mode's (AWWOGameMode's Waves category: Settings binds them there).

#include "CoreMinimal.h"
#include "Game/WWOMatchRules.h"

class AWWOEnemy;
class AActor;
class UWWOHealthComponent;

class FWWOWaveRules final : public FWWOMatchRules
{
public:
	using FWWOMatchRules::FWWOMatchRules;
	virtual ~FWWOWaveRules() override;

	int32 Wave = 0;  // the wave under way, or the last one (0 before the first)
	float Timer = 0.f; // seconds: the countdown, or to the next spawn
	TArray<FName> Queue; // this wave's enemies still to come, in order (kinds of WWOEnemyKinds)
	TArray<TWeakObjectPtr<AWWOEnemy>> Alive; // this wave's enemies in the level (dead ones drop out each tick)

	virtual void Begin() override;
	virtual void Update(float DeltaSeconds) override;  // listens for every player's death (new ones as they come)
	virtual void Advance(float DeltaSeconds) override; // the countdown, the spawning, a cleared wave
	virtual void OnPlayerLeft(AController* Exiting) override; // the last one standing left: checked next tick
	virtual bool IsOver() const override;
	virtual FString StatusText(FLinearColor& OutColor) const override;
	virtual FWWOWaveRules* AsWaves() override { return this; }

	void SkipWave(); // test helper: kill everything in the current wave
	void JumpToWave(int32 Number); // test helper: clear this wave and start wave Number now
	void Countdown(float Seconds);
	void StartWave();
	void WaveCleared();
	// A player went down (or left): alone, a living AI teammate keeps the run going; online, downed
	// players stay down until revived and only players revive, so everyone down loses.
	void OnAnyoneDown();
	AActor* PickPoint(FName Kind) const;
	void SpawnEnemy(FName Kind);

private:
	TSet<TWeakObjectPtr<class AWWOCharacter>> BoundPlayers; // players whose death the waves listen for
	TArray<TPair<TWeakObjectPtr<UWWOHealthComponent>, FDelegateHandle>> Bindings; // removed when these rules go
};
