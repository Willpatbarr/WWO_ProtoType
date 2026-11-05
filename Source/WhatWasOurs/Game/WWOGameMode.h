#pragma once

// The game's rules and the arena wave loop (Godot: wave_manager.gd).
// - If the level has enemy spawn points (Target Points tagged "EnemySpawn", plus a tag per
//   enemy kind they allow), the hand-placed enemies are cleared, a countdown runs, then waves.
// - Each wave's enemies come in one by one from spawn points away from you and hunt you.
//   Clear them all and the next wave starts after a short break.
// - Clear WavesToWin waves: ARENA CLEARED. Go down with no teammate standing: DEFEATED, and
//   the run restarts. Enabled = 0 keeps the level as a sandbox.

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "WWOGameMode.generated.h"

class AWWOEnemy;

UENUM()
enum class EWWOWaveState : uint8 { Idle, Countdown, Spawning, Won, Lost };

UCLASS()
class WHATWASOURS_API AWWOGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AWWOGameMode();
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "Waves") float Enabled = 1.f;
	UPROPERTY(EditAnywhere, Category = "Waves") float WavesToWin = 8.f;
	UPROPERTY(EditAnywhere, Category = "Waves") float StartDelay = 6.f;
	UPROPERTY(EditAnywhere, Category = "Waves") float BreakTime = 2.5f;
	UPROPERTY(EditAnywhere, Category = "Waves") float SizeScale = 2.f;
	UPROPERTY(EditAnywhere, Category = "Waves") float SpawnStagger = 0.35f;

	EWWOWaveState State = EWWOWaveState::Idle;
	int32 Wave = 0;

	void Restart();
	void SkipWave(); // test helper: kill everything in the current wave
	static void LoadMap(UObject* WorldContext, FName Level);
	FString StatusText(FLinearColor& OutColor) const;

private:
	float Timer = 0.f;
	float RestartLeft = -1.f;
	TArray<FName> Queue;
	TArray<TWeakObjectPtr<AWWOEnemy>> Alive;
	bool bBoundToPlayer = false;
	bool bStarted = false;

	void Begin();
	void Countdown(float Seconds);
	void StartWave();
	void WaveCleared();
	void OnPlayerDied();
	void SpawnEnemy(FName Kind);
	AActor* PickPoint(FName Kind) const;
	TArray<AActor*> SpawnPoints() const;
};
