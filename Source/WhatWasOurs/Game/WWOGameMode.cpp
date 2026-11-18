// The game mode shell (Game/WWOGameMode.h): logins, spawning, map changes, the status line, the
// restart timer, and the order it runs its rules (Game/WWOMatchRules.h) and bot roster in each tick.

#include "Game/WWOGameMode.h"
#include "Game/WWOTesterRules.h"
#include "Game/WWOFootRaceRules.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Game/WWOWaveRules.h"
#include "Game/WWOTeamDeathmatch.h"
#include "Game/WWOPlayerState.h"
#include "Game/WWOGameState.h"
#include "WWOSwitches.h"
#include "Player/WWOProf.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOPlayerController.h"
#include "Net/WWONetSubsystem.h"
#include "WhatWasOurs.h"
#include "HAL/IConsoleManager.h"
#include "Engine/TargetPoint.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/PlayerStartPIE.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	AWWOGameMode* WaveMode(UWorld* World) { return World ? Cast<AWWOGameMode>(World->GetAuthGameMode()) : nullptr; }

	// Console helpers for testing later waves.
	FAutoConsoleCommandWithWorld SkipWaveCommand(TEXT("wwo.SkipWave"), TEXT("Kill everything in the current wave"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { if (AWWOGameMode* GM = WaveMode(World)) GM->SkipWave(); }));
	FAutoConsoleCommandWithWorldAndArgs WaveCommand(TEXT("wwo.Wave"), TEXT("wwo.Wave <n>: clear this wave and start wave n now"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AWWOGameMode* GM = WaveMode(World)) GM->JumpToWave(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : (GM->Waves() ? GM->Waves()->Wave : 0) + 1);
		}));
	// FootRace, until boss cores exist: drop a boss's shield for its next damage window.
	FAutoConsoleCommandWithWorldAndArgs BossWindowCommand(TEXT("wwo.BossWindow"),
		TEXT("wwo.BossWindow [team]: FootRace: drop that team's boss shield for its next damage window (default: your team's)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AWWOGameMode* GM = WaveMode(World);
			FWWOFootRaceRules* F = GM ? GM->FootRace() : nullptr;
			if (!F) return;
			const int32 Team = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : FMath::Max(0, UWWOWorldSubsystem::TeamOf(UWWOWorldSubsystem::Get(World)->GetPlayer()));
			F->OpenBossWindow(Team);
		}));
}

AWWOGameMode::AWWOGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = AWWOCharacter::StaticClass();
	PlayerControllerClass = AWWOPlayerController::StaticClass();
	GameStateClass = AWWOGameState::StaticClass(); // carries the wave line to every player
	PlayerStateClass = AWWOPlayerState::StaticClass(); // team and score
}


void AWWOGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	bPvP = UGameplayStatics::HasOption(Options, TEXT("pvp"));
	bTester = !bPvP && (UGameplayStatics::HasOption(Options, TEXT("tester")) || WWOSwitches::Has(TEXT("WWOTester")));
	// The one rules object of this match.
	bFootRace = !bPvP && !bTester && (UGameplayStatics::HasOption(Options, TEXT("footrace")) || MapName.Contains(TEXT("FootRace")));
	if (bPvP) Rules = MakeUnique<FWWOTeamDeathmatch>(*this);
	else if (bFootRace) Rules = MakeUnique<FWWOFootRaceRules>(*this);
	else if (bTester) Rules = MakeUnique<FWWOTesterRules>(*this);
	else Rules = MakeUnique<FWWOWaveRules>(*this);
	FString Value;
	if (WWOSwitches::Value(TEXT("WWOKillTarget="), Value)) KillTarget = CmdKillTarget = FCString::Atof(*Value); // tests
	bTestRun = WWOSwitches::HasAutoTestText();
	if (WWOSwitches::HasStressText()) Stress = MakeUnique<FWWOStressHarness>();
	if (WWOSwitches::Value(TEXT("WWOTimeLimit="), Value)) TimeLimit = FCString::Atof(*Value);
	if (bPvP && WWOSwitches::Value(TEXT("WWOBots="), Value)) Roster.BotsWanted = FMath::Clamp(FCString::Atoi(*Value), 0, 11);
	// ?bots=N: the launch screen's count for this match (a local PvP match fills both teams).
	const FString BotsOption = UGameplayStatics::ParseOption(Options, TEXT("bots"));
	Roster.OptionBots = bPvP && !BotsOption.IsEmpty() ? FMath::Clamp(FCString::Atoi(*BotsOption), 0, 11) : -1;
	// (The launch screen's count is exact, not a floor under the Bots setting: see FWWOBotRoster::Wanted.)
	UE_LOG(LogWWO, Display, TEXT("Game: %s on %s%s"), bPvP ? TEXT("PvP team deathmatch") : bFootRace ? TEXT("FootRace") : TEXT("waves"), *MapName,
		bPvP ? *FString::Printf(TEXT(" (kill target %.0f, time limit %.0f min, respawn %.1f s)"), KillTarget, TimeLimit, PvPRespawnTime) : TEXT(""));
}

FString AWWOGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	const FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	// The engine has just named them after their computer; their gamer tag replaces that. (Cleaned
	// again here: the options come from the other machine.)
	const FString Tag = UWWONetSubsystem::CleanTag(UGameplayStatics::ParseOption(Options, TEXT("Tag")));
	if (!Tag.IsEmpty() && NewPlayerController) ChangeName(NewPlayerController, Tag, false);
	return Error;
}

void AWWOGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	if (Rules) Rules->OnPlayerJoined(NewPlayer);
}

void AWWOGameMode::Logout(AController* Exiting)
{
	if (Rules) Rules->OnPlayerLeaving(Exiting);
	Super::Logout(Exiting);
	if (Rules) Rules->OnPlayerLeft(Exiting);
}

int32 AWWOGameMode::People() const
{
	int32 Count = 0;
	if (GameState) for (const APlayerState* PS : GameState->PlayerArray) if (PS && !PS->IsABot()) ++Count;
	return Count;
}

void AWWOGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	// The advertised places are a promise nothing else keeps: a full game says so here.
	const int32 Most = bPvP ? UWWONetSubsystem::MaxPvPPlayers : UWWONetSubsystem::MaxPlayers;
	if (ErrorMessage.IsEmpty() && People() >= Most) ErrorMessage = FString::Printf(TEXT("The game is full (%d players)"), Most);
}

void AWWOGameMode::SwitchTeam(AController* Player)
{
	if (FWWOTeamDeathmatch* Match = Deathmatch()) Match->SwitchTeam(Player);
}

void AWWOGameMode::BalanceTeam(AController* Player)
{
	FWWOTeamDeathmatch::BalanceTeam(GameState, Player);
}

void AWWOGameMode::SetTeam(AWWOPlayerState* PS, int32 Team)
{
	FWWOTeamDeathmatch::SetTeam(PS, Team);
}

FTransform AWWOGameMode::PvPSpawnFor(const AWWOCharacter* Player) const
{
	if (const FWWOFootRaceRules* F = FootRace()) return F->SpawnSpot(Player);
	return FWWOTeamDeathmatch::SpawnSpot(*this, Player);
}

void AWWOGameMode::StartPlay()
{
	Super::StartPlay();
	bWaveTest = WWOSwitches::Has(TEXT("WWOWaveTest"));
	if (bWaveTest)
	{
		StartDelay = 3.f;
		UE_LOG(LogWWO, Display, TEXT("WaveTest: %d waves, size scale %.2f, break %.1f s, stagger %.2f s"), FMath::RoundToInt(WavesToWin), SizeScale, BreakTime, SpawnStagger);
	}
	Begin();
}

TArray<AActor*> AWWOGameMode::SpawnPoints() const
{
	TArray<AActor*> Points;
	for (TActorIterator<ATargetPoint> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag("EnemySpawn")) Points.Add(*It);
	}
	return Points;
}

void AWWOGameMode::Begin()
{
	bStarted = true;
	if (Rules) Rules->Begin();
}

AActor* AWWOGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	// The editor's "Play from Here" start first, else the map's; taken or not (SpawnDefaultPawnFor finds room).
	APlayerStart* Found = nullptr;
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		if (It->IsA<APlayerStartPIE>()) return *It;
		if (!Found) Found = *It;
	}
	return Found ? Found : Super::ChoosePlayerStart_Implementation(Player);
}

APawn* AWWOGameMode::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
	UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer);
	const APawn* Fit = PawnClass ? PawnClass->GetDefaultObject<APawn>() : nullptr;
	if (!StartSpot || !Fit) return Super::SpawnDefaultPawnFor_Implementation(NewPlayer, StartSpot);
	UWorld* World = GetWorld();
	const FRotator Rotation(0.f, StartSpot->GetActorRotation().Yaw, 0.f);
	// Tested 20 cm up: a capsule at the start's own height can just touch the floor and count as blocked.
	const FVector Lift(0.f, 0.f, 20.f);
	FVector Spot = StartSpot->GetActorLocation() + Lift;
	// The start itself, then rings of 8 spots 1.5, 3 and 4.5 m out (with a clear line from the start,
	// so nobody lands behind a wall).
	bool bFound = !World->EncroachingBlockingGeometry(Fit, Spot, Rotation);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOPlayerSpawn), false);
	for (int32 Ring = 1; Ring <= 3 && !bFound; ++Ring)
	{
		for (int32 i = 0; i < 8 && !bFound; ++i)
		{
			const FVector Try = Spot + FRotator(0.f, Rotation.Yaw + i * 45.f, 0.f).Vector() * Ring * 150.f;
			FHitResult Hit;
			// Walls only (by object type): a channel trace would also stop at whoever stands on the start.
			if (World->LineTraceSingleByObjectType(Hit, Spot, Try, FCollisionObjectQueryParams(ECC_WorldStatic), Params)) continue;
			if (World->EncroachingBlockingGeometry(Fit, Try, Rotation)) continue;
			Spot = Try;
			bFound = true;
			UE_LOG(LogWWO, Display, TEXT("%s spawns %.1f m from the Player Start"), *GetNameSafe(NewPlayer), Ring * 1.5f);
		}
	}
	if (!bFound) UE_LOG(LogWWO, Warning, TEXT("No free spot near the Player Start for %s; spawning on it"), *GetNameSafe(NewPlayer));
	FActorSpawnParameters SpawnParams;
	SpawnParams.Instigator = GetInstigator();
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	return World->SpawnActor<APawn>(PawnClass, Spot - Lift, Rotation, SpawnParams); // on the ground, not falling the lift
}

void AWWOGameMode::Restart()
{
	LoadMap(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
}

void AWWOGameMode::LoadMap(UObject* WorldContext, FName Level)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World) return;
	const AWWOGameMode* Mode = World->GetAuthGameMode<AWWOGameMode>();
	const bool bStayPvP = Mode && Mode->bPvP; // a PvP match stays PvP (with its bots) across restarts and map changes
	const int32 StayBots = Mode ? Mode->Roster.OptionBots : -1;
	switch (World->GetNetMode())
	{
	case NM_ListenServer:
		UE_LOG(LogWWO, Display, TEXT("Net: host moving everyone to %s%s"), *Level.ToString(), bStayPvP ? TEXT(" (PvP)") : TEXT(""));
		World->ServerTravel(Level.ToString() + TEXT("?") + UWWONetSubsystem::MatchOptions(true, bStayPvP, StayBots));
		break;
	case NM_Client:
		if (UWWONetSubsystem* Net = UWWONetSubsystem::Get(World)) Net->Status = TEXT("Only the host can restart or change maps");
		UE_LOG(LogWWO, Display, TEXT("Net: only the host changes maps (asked for %s)"), *Level.ToString());
		break;
	default:
		UGameplayStatics::OpenLevel(World, Level, true, UWWONetSubsystem::MatchOptions(false, bStayPvP, StayBots));
		break;
	}
}

void AWWOGameMode::SkipWave()
{
	if (FWWOWaveRules* W = Waves()) W->SkipWave();
}

void AWWOGameMode::JumpToWave(int32 Number)
{
	if (FWWOWaveRules* W = Waves()) W->JumpToWave(Number);
}

void AWWOGameMode::Tick(float DeltaSeconds)
{
	WWO_PROF("GameMode");
	Super::Tick(DeltaSeconds);
	// Hosting: the game others see in their list follows this match (map, mode, players in it).
	if (GetNetMode() == NM_ListenServer && (PublishLeft -= DeltaSeconds) <= 0.f)
	{
		PublishLeft = 2.f;
		if (UWWONetSubsystem* Net = UWWONetSubsystem::Get(this)) Net->PublishMatch(this);
	}
	// The order is the old single class's: the mode's own bookkeeping, the wave test, the bots, the
	// stress harness, the status line, a due restart, then the waves' state machine.
	if (Rules) Rules->Update(DeltaSeconds);
	if (bWaveTest) UpdateWaveTest(DeltaSeconds);
	if (FWWOTeamDeathmatch* Match = Deathmatch())
	{
		if (Stress) Stress->LogBots(*this); // stress runs only: bot states, sight lines, test shots at every head
		Roster.Update(*this, *Match);
		Roster.FollowSettings(*this);
	}
	if (Stress) Stress->Tick(*this, DeltaSeconds);
	if (AWWOGameState* GS = GetGameState<AWWOGameState>())
	{
		FLinearColor Color;
		GS->StatusLine = StatusText(Color);
		GS->StatusColor = Color;
	}
	if (RestartLeft > 0.f && (RestartLeft -= DeltaSeconds) <= 0.f)
	{
		Restart();
		return;
	}
	if (Rules) Rules->Advance(DeltaSeconds);
}

FString AWWOGameMode::StatusText(FLinearColor& OutColor) const
{
	if (Rules) return Rules->StatusText(OutColor);
	OutColor = FLinearColor::White;
	return FString();
}
