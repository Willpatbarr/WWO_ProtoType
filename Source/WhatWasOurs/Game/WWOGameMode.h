#pragma once

// The game mode: a shell around one match's rules. It logs players in (names, the full-game
// check), spawns them (the ring of spots around the one Player Start), changes maps, writes the
// status line every tick, restarts when the rules ask, and owns:
// - one rules object (Rules, Game/WWOMatchRules.h), chosen in InitGame: the PvE waves
//   (FWWOWaveRules), team deathmatch with ?pvp (FWWOTeamDeathmatch), or the firing tester with
//   ?tester / -WWOTester (FWWOTesterRules);
// - the PvP match's bot roster (Roster, FWWOBotRoster);
// - the tunables of all of them, here because Settings binds its sliders to these properties by name.
// Runs on the host only (a joiner has no game mode). The stress harness and the wave test are test
// code beside it (WWOGameModeStress.*, WWOGameModeWaveTest.cpp).

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Game/WWOGameModeStress.h"
#include "Game/WWOBotRoster.h"
#include "Game/WWOMatchRules.h"
#include "WWOGameMode.generated.h"

class AWWOEnemy;

UCLASS()
class WHATWASOURS_API AWWOGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AWWOGameMode();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	// Names the player from the gamer tag in their login options (?Tag=, UWWONetSubsystem::WithName).
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;
	virtual void Logout(AController* Exiting) override; // the rules hear of it before and after the engine's own
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
	// Team calls, forwarded to FWWOTeamDeathmatch (the health component respawns at PvPSpawnFor
	// whenever the game state says PvP; tests set teams with SetTeam).
	FTransform PvPSpawnFor(const class AWWOCharacter* Player) const; // away from the other team
	void SetTeam(class AWWOPlayerState* PS, int32 Team);
	void BalanceTeam(AController* Player); // the smaller team
	void SwitchTeam(AController* Player); // a player asks to change sides (in a match only)
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	int32 People() const; // players who aren't bots
	// The firing tester (?tester, from the launch screen; -WWOTester): no waves, no enemies, one
	// target dummy ahead of the start (FWWOTesterRules). UWWOFiringTester is the screen over it.
	bool bTester = false;
	// The rules of this match (Game/WWOMatchRules.h), chosen in InitGame, and typed views of them.
	TUniquePtr<FWWOMatchRules> Rules;
	FWWOTesterRules* Tester() const { return Rules ? Rules->AsTester() : nullptr; }
	FWWOWaveRules* Waves() const { return Rules ? Rules->AsWaves() : nullptr; }
	FWWOTeamDeathmatch* Deathmatch() const { return Rules ? Rules->AsDeathmatch() : nullptr; }
	EWWOWaveState MatchState() const { return Rules ? Rules->State : EWWOWaveState::Idle; }
	// The PvP match's bot players (-WWOBots=N, ?bots=N, the Bots setting; Game/WWOBotRoster.h). -WWOStress logs the load.
	FWWOBotRoster Roster;
	float CmdKillTarget = -1.f; // -WWOKillTarget= (tests): wins over the saved setting
	bool bTestRun = false;      // a -WWOAutoTest... run
	float PublishLeft = 0.f; // host: seconds to the next refresh of the advertised game
	TUniquePtr<FWWOStressHarness> Stress; // test runs with a WWOStress switch only (WWOGameModeStress.cpp)

	void Restart(); // the same map again (online: the host takes everyone along)
	void SkipWave(); // test helper (wwo.SkipWave, Settings): FWWOWaveRules::SkipWave, in waves only
	void JumpToWave(int32 Number); // test helper (wwo.Wave): FWWOWaveRules::JumpToWave, in waves only
	TArray<AActor*> SpawnPoints() const; // the level's enemy spawn points (Target Points tagged EnemySpawn)
	// Alone: opens the map. Hosting: moves everyone to it (ServerTravel, still a LAN game).
	// In someone else's game: only the host changes maps, so it just says so.
	static void LoadMap(UObject* WorldContext, FName Level);
	FString StatusText(FLinearColor& OutColor) const; // the rules' line

private:
	float RestartLeft = -1.f; // seconds to Restart (a lost round, a test run's match over); the rules set it
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

	// The regression scenarios (-WWORegress, Player/WWORegress.h) drive the rules directly; no gameplay code uses this.
	friend class FWWORegress;
	// The rules and the roster read and set the shell's private state (bStarted, RestartLeft).
	friend struct FWWOBotRoster;
	friend class FWWOTesterRules;
	friend class FWWOWaveRules;
	friend class FWWOTeamDeathmatch;
};
