#include "Game/WWOGameMode.h"
#include "WWOTypes.h"
#include "WWOSwitches.h"
#include "Player/WWOProf.h"
#include "FX/WWOFX.h"
#include "TimerManager.h"
#include "Player/WWOBotController.h"
#include "Allies/WWOAlly.h"
#include "World/WWOTarget.h"
#include "Game/WWOTesterRules.h"
#include "Game/WWOWaveRules.h"
#include "Game/WWOPlayerState.h"
#include "Game/WWOGameState.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOHealthComponent.h"
#include "Enemies/WWOEnemy.h"
#include "Enemies/WWOEnemyKinds.h"
#include "WWOIds.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Net/WWONetSubsystem.h"
#include "WhatWasOurs.h"
#include "Components/CapsuleComponent.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"
#include "Engine/TargetPoint.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/PlayerStartPIE.h"
#include "EngineUtils.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	AWWOGameMode* WaveMode(UWorld* World) { return World ? Cast<AWWOGameMode>(World->GetAuthGameMode()) : nullptr; }

	// Adds the players and bots on each PvP team to Count, leaving out Except (someone about to be placed).
	void CountTeams(const AGameStateBase* GameState, int32 Count[2], const AWWOPlayerState* Except = nullptr)
	{
		for (const APlayerState* Other : GameState->PlayerArray)
		{
			if (const AWWOPlayerState* PS = Cast<AWWOPlayerState>(Other); PS && PS != Except && PS->Team >= 0 && PS->Team <= 1) ++Count[PS->Team];
		}
	}

	// Console helpers for testing later waves.
	FAutoConsoleCommandWithWorld SkipWaveCommand(TEXT("wwo.SkipWave"), TEXT("Kill everything in the current wave"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { if (AWWOGameMode* GM = WaveMode(World)) GM->SkipWave(); }));
	FAutoConsoleCommandWithWorldAndArgs WaveCommand(TEXT("wwo.Wave"), TEXT("wwo.Wave <n>: clear this wave and start wave n now"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AWWOGameMode* GM = WaveMode(World)) GM->JumpToWave(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : (GM->Waves() ? GM->Waves()->Wave : 0) + 1);
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

int32 AWWOGameMode::PvPKills = 0;
int32 AWWOGameMode::PvPMatchesEnded = 0;

void AWWOGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	bPvP = UGameplayStatics::HasOption(Options, TEXT("pvp"));
	bTester = !bPvP && (UGameplayStatics::HasOption(Options, TEXT("tester")) || WWOSwitches::Has(TEXT("WWOTester")));
	if (bTester) Rules = MakeUnique<FWWOTesterRules>(*this);
	else if (!bPvP) Rules = MakeUnique<FWWOWaveRules>(*this);
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
	UE_LOG(LogWWO, Display, TEXT("Game: %s on %s%s"), bPvP ? TEXT("PvP team deathmatch") : TEXT("waves"), *MapName,
		bPvP ? *FString::Printf(TEXT(" (kill target %.0f, time limit %.0f min, respawn %.1f s)"), KillTarget, TimeLimit, PvPRespawnTime) : TEXT(""));
}

AWWOGameState* AWWOGameMode::WGS() const
{
	return GetGameState<AWWOGameState>();
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
	if (bPvP)
	{
		const bool bMidMatch = State == EWWOWaveState::Match && !NewPlayer->IsLocalController();
		if (bMidMatch) Roster.RetireFor(*this, NewPlayer); // before the team is picked: the person takes the bot's side
		BalanceTeam(NewPlayer);
		TeamGiven.Add(NewPlayer->GetPlayerState<AWWOPlayerState>());
		// Into a match under way: at a spot away from the other team, not the map's one Player Start.
		if (AWWOCharacter* Body = bMidMatch ? Cast<AWWOCharacter>(NewPlayer->GetPawn()) : nullptr)
		{
			const FTransform Spot = PvPSpawnFor(Body);
			Body->TeleportTo(Spot.GetLocation(), Spot.Rotator());
			Body->Health->SpawnYaw = Spot.Rotator().Yaw;
			NewPlayer->ClientSetRotation(Spot.Rotator());
		}
	}
}

void AWWOGameMode::Logout(AController* Exiting)
{
	// A person leaving a PvP match (Quit to main menu, or a dropped connection): a bot fills their
	// place, so the teams stay the size they were. The roster spawns it and puts it on the smaller team.
	const APlayerController* Person = Cast<APlayerController>(Exiting);
	if (bPvP && Person && !Person->IsLocalController() && State == EWWOWaveState::Match)
	{
		Roster.PersonLeft();
		UE_LOG(LogWWO, Display, TEXT("PvP: %s left, a bot takes the place"), *GetNameSafe(Exiting->PlayerState));
	}
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
	AWWOPlayerState* PS = Player ? Player->GetPlayerState<AWWOPlayerState>() : nullptr;
	if (!bPvP || !PS || State != EWWOWaveState::Match) return;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - PS->LastSwitch < 10.0) return;
	int32 Count[2] = { 0, 0 };
	CountTeams(GameState, Count);
	const int32 To = 1 - PS->Team;
	if (Count[To] + 1 > Count[PS->Team]) return; // it would leave the other side the bigger one
	PS->LastSwitch = Now;
	SetTeam(PS, To);
	if (AWWOCharacter* Body = Cast<AWWOCharacter>(Player->GetPawn()); Body && !Body->Health->bDead)
	{
		const FTransform Spot = PvPSpawnFor(Body);
		Body->TeleportTo(Spot.GetLocation(), Spot.Rotator());
		Body->Health->Health = Body->Health->MaxHealth();
		Body->Health->Shield = Body->Health->MaxShield();
		Body->Health->LastAttacker = nullptr;
		Body->Health->SpawnYaw = Spot.Rotator().Yaw;
		Player->ClientSetRotation(Spot.Rotator());
		Body->SetViewRotation(0.f, Spot.Rotator().Yaw);
	}
}

void AWWOGameMode::BalanceTeam(AController* Player)
{
	AWWOPlayerState* Mine = Player ? Player->GetPlayerState<AWWOPlayerState>() : nullptr;
	if (!Mine || !GameState) return;
	int32 Count[2] = { 0, 0 };
	CountTeams(GameState, Count, Mine);
	SetTeam(Mine, Count[1] < Count[0] ? 1 : 0);
}

void AWWOGameMode::SetTeam(AWWOPlayerState* PS, int32 Team)
{
	if (!PS) return;
	PS->Team = FMath::Clamp(Team, 0, 1);
	UE_LOG(LogWWO, Display, TEXT("PvP: %s is on %s"), *PS->GetPlayerName(), *AWWOPlayerState::TeamName(PS->Team));
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
	if (bPvP)
	{
		// Team deathmatch: no waves, no enemies, no AI teammates.
		for (TActorIterator<AWWOEnemy> It(GetWorld()); It; ++It) It->Destroy();
		for (TActorIterator<AWWOAlly> It(GetWorld()); It; ++It) It->Destroy();
		if (AWWOGameState* GS = WGS())
		{
			GS->bPvP = true;
			GS->TeamScores = { 0, 0 };
			GS->bMatchOver = false;
			GS->MatchWinner = -1;
		}
		MatchLeft = TimeLimit * 60.f;
		MatchElapsed = 0.f;
		Winner = -1;
		State = EWWOWaveState::Match;
		return;
	}
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
	if (Rules) Rules->Update(DeltaSeconds);
	if (bWaveTest) UpdateWaveTest(DeltaSeconds);
	if (bPvP) UpdateMatch(DeltaSeconds);
	if (bPvP)
	{
		if (Stress) Stress->LogBots(*this); // stress runs only: bot states, sight lines, test shots at every head
		Roster.Update(*this);
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
	const AWWOGameState* GS = GetGameState<AWWOGameState>();
	const int32 Blue = GS && GS->TeamScores.Num() > 0 ? GS->TeamScores[0] : 0;
	const int32 Orange = GS && GS->TeamScores.Num() > 1 ? GS->TeamScores[1] : 0;
	switch (State)
	{
	case EWWOWaveState::Match:
	{
		FString Line = FString::Printf(TEXT("BLUE %d  –  %d ORANGE"), Blue, Orange);
		if (KillTarget >= 1.f) Line += FString::Printf(TEXT("   ·   first to %d"), FMath::RoundToInt(KillTarget));
		if (TimeLimit > 0.f)
		{
			const int32 Secs = FMath::CeilToInt(FMath::Max(MatchLeft, 0.f));
			Line += FString::Printf(TEXT("   ·   %d:%02d"), Secs / 60, Secs % 60);
		}
		return Line;
	}
	case EWWOWaveState::MatchOver:
		OutColor = Winner < 0 ? FLinearColor(0.9f, 0.9f, 0.9f) : AWWOPlayerState::TeamColor(Winner);
		return Winner < 0 ? FString::Printf(TEXT("DRAW  %d – %d%s"), Blue, Orange, bTestRun ? TEXT("   ·   restarting") : TEXT(""))
			: FString::Printf(TEXT("%s WINS  %d – %d%s"), *AWWOPlayerState::TeamName(Winner), Blue, Orange, bTestRun ? TEXT("   ·   restarting") : TEXT(""));
	default:
		return FString();
	}
}

// --- PvP ---------------------------------------------------------------------------------------

void AWWOGameMode::UpdateMatch(float DeltaSeconds)
{
	// Score every player's deaths (they bind once, like the waves' OnPlayerDied).
	for (AWWOCharacter* P : UWWOWorldSubsystem::Get(this)->GetPlayers())
	{
		if (PvPBound.Contains(P)) continue;
		PvPBound.Add(P);
		TWeakObjectPtr<AWWOCharacter> Weak = P;
		P->Health->OnDied.AddWeakLambda(this, [this, Weak] { if (Weak.IsValid()) OnPvPDeath(Weak.Get()); });
		// A player who joined before PostLogin gave them a team (the host's own at start).
		if (AWWOPlayerState* PS = P->GetPlayerState<AWWOPlayerState>(); PS && P->GetController() && !TeamGiven.Contains(PS))
		{
			TeamGiven.Add(PS);
			BalanceTeam(P->GetController());
		}
	}
	if (State != EWWOWaveState::Match) return;
	if (CmdKillTarget >= 0.f) KillTarget = CmdKillTarget; // a test's own target, over the saved setting loaded since
	// By time played, so the limit can be turned on, off or changed mid-match.
	MatchElapsed += DeltaSeconds;
	MatchLeft = FMath::Max(TimeLimit * 60.f - MatchElapsed, 0.f);
	if (TimeLimit > 0.f && MatchElapsed >= TimeLimit * 60.f)
	{
		const AWWOGameState* GS = WGS();
		const int32 Blue = GS && GS->TeamScores.Num() > 0 ? GS->TeamScores[0] : 0;
		const int32 Orange = GS && GS->TeamScores.Num() > 1 ? GS->TeamScores[1] : 0;
		EndMatch(Blue == Orange ? -1 : (Blue > Orange ? 0 : 1)); // time: the most kills
	}
}

void AWWOGameMode::OnPvPDeath(AWWOCharacter* Victim)
{
	if (State != EWWOWaveState::Match || !Victim) return;
	AWWOPlayerState* VictimPS = Victim->GetPlayerState<AWWOPlayerState>();
	if (VictimPS) ++VictimPS->Deaths;
	// Whoever hit them last, if that was in the last 10 s (a fall long after a fight is nobody's kill).
	const bool bRecent = GetWorld()->GetTimeSeconds() - Victim->Health->LastAttackTime <= 10.0;
	AWWOCharacter* Killer = bRecent ? Cast<AWWOCharacter>(Victim->Health->LastAttacker.Get()) : nullptr;
	AWWOPlayerState* KillerPS = Killer ? Killer->GetPlayerState<AWWOPlayerState>() : nullptr;
	if (!KillerPS || !UWWOWorldSubsystem::CanHurt(Killer, Victim))
	{
		UE_LOG(LogWWO, Display, TEXT("PvP: %s died (no kill credit)"), *GetNameSafe(Victim));
		return; // a fall or anything that isn't the other team: nobody scores
	}
	++KillerPS->Kills;
	++PvPKills;
	// A precision kill (the hit that killed was a head hit) scores more.
	const bool bPrecision = Victim->Health->bLastAttackCritical && GetWorld()->GetTimeSeconds() - Victim->Health->LastAttackTime <= 0.05;
	const int32 Points = bPrecision ? AWWOGameState::PrecisionKillPoints : AWWOGameState::KillPoints;
	if (bPrecision) ++KillerPS->PrecisionKills;
	AWWOGameState* GS = WGS();
	if (GS)
	{
		if (GS->TeamScores.Num() < 2) GS->TeamScores.SetNum(2);
		GS->TeamScores[KillerPS->Team] += Points;
	}
	UE_LOG(LogWWO, Display, TEXT("PvP: %s (%s) killed %s; %s %d"), *KillerPS->GetPlayerName(), *AWWOPlayerState::TeamName(KillerPS->Team),
		VictimPS ? *VictimPS->GetPlayerName() : *Victim->GetName(), *AWWOPlayerState::TeamName(KillerPS->Team), GS ? GS->TeamScores[KillerPS->Team] : 0);
	// The kill feed, on everyone's screen: killer > killee.
	if (GS) GS->ReportKill(KillerPS, KillerPS->GetPlayerName(), KillerPS->Team, VictimPS ? VictimPS->GetPlayerName() : Victim->GetName(), VictimPS ? VictimPS->Team : -1,
		bPrecision ? AWWOGameState::KillPrecision : FName(), Points);
	if (GS && KillTarget >= 1.f && GS->TeamScores[KillerPS->Team] >= FMath::RoundToInt(KillTarget)) EndMatch(KillerPS->Team);
}

void AWWOGameMode::EndMatch(int32 WinningTeam)
{
	State = EWWOWaveState::MatchOver;
	Winner = WinningTeam;
	++PvPMatchesEnded;
	// The scoreboard comes up on every machine and stays until the host picks what's next
	// (UWWOScoreboard: next match, change map, quit). A test run restarts by itself as before.
	if (AWWOGameState* GS = WGS())
	{
		GS->bMatchOver = true;
		GS->MatchWinner = (int8)WinningTeam;
	}
	if (bTestRun) RestartLeft = 6.f;
	UE_LOG(LogWWO, Display, TEXT("PvP: match over, %s"), WinningTeam < 0 ? TEXT("a draw") : *(AWWOPlayerState::TeamName(WinningTeam) + TEXT(" wins")));
}

FTransform AWWOGameMode::PvPSpawnFor(const AWWOCharacter* Player) const
{
	// The spawn spot (enemy spawn points and player starts) farthest from the nearest living
	// player of the other team. Enemy spawn points tagged PvEOnly (L_Cascade's, out on the nodes and
	// terminals) are for the waves only.
	TArray<FTransform> Spots;
	for (AActor* Point : SpawnPoints())
	{
		if (!Point->ActorHasTag("PvEOnly")) Spots.Add(FTransform(Point->GetActorRotation(), Point->GetActorLocation() + FVector(0, 0, 100.f)));
	}
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It) Spots.Add(It->GetActorTransform());
	if (Spots.Num() == 0) return Player ? Player->GetActorTransform() : FTransform::Identity;
	TArray<const AWWOCharacter*> Others;
	for (const AWWOCharacter* Other : UWWOWorldSubsystem::Get(this)->GetPlayers())
	{
		if (Other != Player && !Other->Health->bDead && UWWOWorldSubsystem::CanHurt(Player, Other)) Others.Add(Other);
	}
	// Rank them by how far the nearest foe is, leave out any somebody is standing on, and take one
	// of the best three at random: a team that dies together doesn't all come back on one spot.
	TArray<TPair<float, int32>> Ranked;
	for (int32 i = 0; i < Spots.Num(); ++i)
	{
		bool bTaken = false;
		for (const AWWOCharacter* Any : UWWOWorldSubsystem::Get(this)->GetPlayers())
		{
			bTaken |= Any != Player && !Any->Health->bDead && FVector::Dist2D(Any->GetActorLocation(), Spots[i].GetLocation()) < 150.f;
		}
		if (bTaken) continue;
		float Nearest = Others.Num() > 0 ? TNumericLimits<float>::Max() : 0.f;
		for (const AWWOCharacter* Other : Others) Nearest = FMath::Min(Nearest, FVector::Dist(Other->GetActorLocation(), Spots[i].GetLocation()));
		Ranked.Add({ Nearest, i });
	}
	Ranked.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key > B.Key; });
	const int32 Best = Ranked.Num() > 0 ? Ranked[WWO::RandRange(0, FMath::Min(Ranked.Num(), Others.Num() > 0 ? 3 : Ranked.Num()) - 1)].Value
		: WWO::RandRange(0, Spots.Num() - 1);
	// Facing the middle of the map.
	FTransform Spot = Spots[Best];
	FVector ToMiddle = -Spot.GetLocation();
	ToMiddle.Z = 0.f;
	if (!ToMiddle.IsNearlyZero()) Spot.SetRotation(ToMiddle.Rotation().Quaternion());
	return Spot;
}
