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
	virtual void Logout(AController* Exiting) override; // PvP: a bot takes a leaving player's place
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
	// Bot players (PvP; AWWOBotController reads these, the host's values count).
	UPROPERTY(EditAnywhere, Category = "Bots") float Bots = 0.f;            // how many join (more can be added mid-match; -WWOBots=N too)
	UPROPERTY(EditAnywhere, Category = "Bots") float BotHealth = 100.f;     // every bot's health and shield, before its own shell parts and traits
	UPROPERTY(EditAnywhere, Category = "Bots") float BotShield = 100.f;     // (a change refills them at once)
	UPROPERTY(EditAnywhere, Category = "Bots") float BotReaction = 0.35f;   // s before a bot acts on an enemy it just noticed
	UPROPERTY(EditAnywhere, Category = "Bots") float BotTurnSpeed = 260.f;  // degrees per second, the fastest it turns
	UPROPERTY(EditAnywhere, Category = "Bots") float BotAimError = 1.6f;    // degrees its aim wanders off the target (0 = perfect)
	UPROPERTY(EditAnywhere, Category = "Bots") float BotViewAngle = 120.f;  // degrees, the whole cone it sees in
	UPROPERTY(EditAnywhere, Category = "Bots") float BotSightRange = 60.f;  // m
	UPROPERTY(EditAnywhere, Category = "Bots") float BotMemory = 6.f;       // s it hunts an enemy it lost sight of
	UPROPERTY(EditAnywhere, Category = "Bots") float BotAggression = 0.5f;  // 0 = backs off to cover early, 1 = never backs off
	UPROPERTY(EditAnywhere, Category = "Bots") float BotKitUse = 1.f;       // 0-1: how readily it uses grenades, abilities and its ult
	UPROPERTY(EditAnywhere, Category = "Bots") float BotStickToPlayer = 0.5f; // 0-1: the share of a team's bots that escort its human (the rest pair up)
	UPROPERTY(EditAnywhere, Category = "Bots") float BotCalloutRange = 40.f;  // m: teammates this near hear about an enemy one of them sees (0 = no callouts)

	bool bPvP = false; // this match is team deathmatch (the map was opened with ?pvp)
	float MatchLeft = 0.f; // seconds
	int32 Winner = -1;     // the team that won (-1: a draw or not over)
	FTransform PvPSpawnFor(const class AWWOCharacter* Player) const; // away from the other team
	void SetTeam(class AWWOPlayerState* PS, int32 Team);
	void BalanceTeam(AController* Player); // the smaller team
	// A player asks to change sides: only onto a team no bigger than theirs would become, at most
	// every 10 s, and they start over at a spawn spot (no switching mid-fight to dodge a shot).
	void SwitchTeam(AController* Player);
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	int32 People() const; // players who aren't bots
	static int32 PvPKills, PvPMatchesEnded; // for -WWOAutoTestNet
	// -WWOBots=N (PvP): N bot players join the match (AWWOBotController). -WWOStress logs the load.
	int32 BotsWanted = 0, BotsSpawned = 0;
	float CmdKillTarget = -1.f; // -WWOKillTarget= (tests): wins over the saved setting
	bool bTestRun = false;      // a -WWOAutoTest... run
	int32 BotsExtra = 0;   // +1 for each person who left this match, -1 for each who joined it since
	float MatchElapsed = 0.f; // seconds of this PvP match played
	void RetireBotFor(AController* Joiner); // a person joined: a bot makes room
	int32 OptionBots = -1; // ?bots=N from the launch screen (-1: none given); kept across restarts
	float PublishLeft = 0.f; // host: seconds to the next refresh of the advertised game
	float BotLogNext = 10.f, BotLogClock = 0.f;
	float StressGame = 0.f, StressRender = 0.f, StressGpu = 0.f; // ms summed since the last stress line
	float StressClock = 0.f, StressFrames = 0.f, StressWorst = 0.f, StressNext = 5.f;
	void UpdateBots();
	void LogStress(float DeltaSeconds);

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
