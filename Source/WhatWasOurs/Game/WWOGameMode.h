#pragma once

// The game's rules and the arena wave loop (Godot: wave_manager.gd).
// - If the level has enemy spawn points (Target Points tagged "EnemySpawn", plus a tag per
//   enemy kind they allow), the hand-placed enemies are cleared, a countdown runs, then waves.
// - Each wave's enemies come in one by one from spawn points away from you and hunt you.
//   Clear them all and the next wave starts after a short break.
// - Clear WavesToWin waves: ARENA CLEARED. Go down with no teammate standing: DEFEATED, and
//   the run restarts. Enabled = 0 keeps the level as a sandbox.
// - PvP (hosted with ?pvp): team deathmatch instead of waves. Players join the smaller team, kill
//   the other team's players, respawn after PvPRespawnTime away from them; first team to KillTarget
//   kills wins, or the most kills when TimeLimit runs out (either can be 0 = off). Then it restarts.

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "WWOGameMode.generated.h"

class AWWOEnemy;

UENUM()
enum class EWWOWaveState : uint8 { Idle, Countdown, Spawning, Won, Lost, Match, MatchOver };

UCLASS()
class WHATWASOURS_API AWWOGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AWWOGameMode();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	// Every map has one Player Start. Online, each player arriving takes the first free spot in
	// rings around it (the engine's own pick gives up when the start is taken and there's no room
	// right beside it, and the player never spawns).
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;

	UPROPERTY(EditAnywhere, Category = "Waves") float Enabled = 1.f;
	UPROPERTY(EditAnywhere, Category = "Waves") float WavesToWin = 8.f;
	UPROPERTY(EditAnywhere, Category = "Waves") float StartDelay = 6.f;
	UPROPERTY(EditAnywhere, Category = "Waves") float BreakTime = 2.5f;
	UPROPERTY(EditAnywhere, Category = "Waves") float SizeScale = 2.f;
	UPROPERTY(EditAnywhere, Category = "Waves") float SpawnStagger = 0.35f;
	UPROPERTY(EditAnywhere, Category = "PvP") float KillTarget = 50.f;   // team kills to win (0 = off)
	UPROPERTY(EditAnywhere, Category = "PvP") float TimeLimit = 10.f;    // minutes (0 = off)
	UPROPERTY(EditAnywhere, Category = "PvP") float PvPRespawnTime = 3.f;

	bool bPvP = false; // this match is team deathmatch (the map was opened with ?pvp)
	float MatchLeft = 0.f; // seconds
	int32 Winner = -1;     // the team that won (-1: a draw or not over)
	FTransform PvPSpawnFor(const class AWWOCharacter* Player) const; // away from the other team
	void SetTeam(class AWWOPlayerState* PS, int32 Team);
	void BalanceTeam(AController* Player); // the smaller team
	static int32 PvPKills, PvPMatchesEnded; // for -WWOAutoTestNet

	EWWOWaveState State = EWWOWaveState::Idle;
	int32 Wave = 0;

	void Restart(); // the same map again (online: the host takes everyone along)
	void SkipWave(); // test helper: kill everything in the current wave
	void JumpToWave(int32 Number); // test helper: clear this wave and start wave Number now
	// Alone: opens the map. Hosting: moves everyone to it (ServerTravel, still a LAN game).
	// In someone else's game: only the host changes maps, so it just says so.
	static void LoadMap(UObject* WorldContext, FName Level);
	FString StatusText(FLinearColor& OutColor) const;

private:
	float Timer = 0.f;
	float RestartLeft = -1.f;
	TArray<FName> Queue;
	TArray<TWeakObjectPtr<AWWOEnemy>> Alive;
	TSet<TWeakObjectPtr<class AWWOCharacter>> BoundPlayers; // players whose death the waves listen for
	TSet<TWeakObjectPtr<class AWWOCharacter>> PvPBound;     // players whose death PvP scores
	TSet<TWeakObjectPtr<class AWWOPlayerState>> TeamGiven;  // players BalanceTeam already placed
	bool bStarted = false;

	// -WWOWaveTest: plays every wave with god mode on, cuts each fight short after a while
	// (SkipWave), and logs what each enemy is doing, flagging any stuck or not facing its target.
	bool bWaveTest = false;
	bool bWaveTestNavChecked = false;
	FVector WaveTestHome = FVector::ZeroVector; // where you started
	float WaveTestFight = 0.f;
	float WaveTestLog = 0.f;
	float WaveTestSample = 0.f;
	int32 WaveTestStuck = 0, WaveTestBlocked = 0, WaveTestHolding = 0, WaveTestTurned = 0, WaveTestOverlaps = 0;
	struct FWaveTestTrack
	{
		FVector Pos = FVector::ZeroVector, Wish = FVector::ZeroVector;
		float Still = 0.f, Off = 0.f, Air = 0.f;
		bool bFlipped = false, bStuckLogged = false, bTurnLogged = false, bAirLogged = false;
	};
	TMap<TWeakObjectPtr<ACharacter>, FWaveTestTrack> WaveTestTracks;
	void UpdateWaveTest(float DeltaSeconds);

	void Begin();
	void Countdown(float Seconds);
	void UpdateMatch(float DeltaSeconds);
	void OnPvPDeath(class AWWOCharacter* Victim);
	void EndMatch(int32 WinningTeam);
	class AWWOGameState* WGS() const;
	void StartWave();
	void WaveCleared();
	void OnPlayerDied();
	void SpawnEnemy(FName Kind);
	AActor* PickPoint(FName Kind) const;
	TArray<AActor*> SpawnPoints() const;
};
