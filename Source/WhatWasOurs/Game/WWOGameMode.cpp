#include "Game/WWOGameMode.h"
#include "WWOSwitches.h"
#include "Player/WWOProf.h"
#include "Characters/WWOBodyComponent.h"
#include "FX/WWOFX.h"
#include "Weapons/WWOGrenade.h"
#include "Flora/WWOFloraEffects.h"
#include "Systems/WWOSound.h"
#include "TimerManager.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Player/WWOBotController.h"
#include "Allies/WWOAlly.h"
#include "World/WWOTarget.h"
#include "Game/WWOPlayerState.h"
#include "Game/WWOGameState.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOHealthComponent.h"
#include "Enemies/WWOEnemy.h"
#include "Enemies/WWOEnemyTypes.h"
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
#include "RenderTimer.h"
#include "Flora/WWOSporeFog.h"
#include "Player/WWOAbilityComponent.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "UObject/UObjectIterator.h"
#include "Components/StaticMeshComponent.h"
#include "UnrealClient.h"
#include "RHI.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	constexpr float MinSpawnDistance = 1400.f; // enemies don't appear right next to you (cm)
	constexpr float CorpseCleanup = 4.f;

	AWWOGameMode* WaveMode(UWorld* World) { return World ? Cast<AWWOGameMode>(World->GetAuthGameMode()) : nullptr; }

	// Console helpers for testing later waves.
	FAutoConsoleCommandWithWorld SkipWaveCommand(TEXT("wwo.SkipWave"), TEXT("Kill everything in the current wave"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { if (AWWOGameMode* GM = WaveMode(World)) GM->SkipWave(); }));
	FAutoConsoleCommandWithWorldAndArgs WaveCommand(TEXT("wwo.Wave"), TEXT("wwo.Wave <n>: clear this wave and start wave n now"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AWWOGameMode* GM = WaveMode(World)) GM->JumpToWave(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : GM->Wave + 1);
		}));

	// The waves, in order. Past the last one, it repeats the last wave 25% bigger each time.
	const TArray<TMap<FName, int32>>& Waves()
	{
		static const TArray<TMap<FName, int32>> List = {
			{ { "swarm", 6 } },
			{ { "swarm", 6 }, { "soldier", 2 } },
			{ { "swarm", 8 }, { "soldier", 3 } },
			{ { "soldier", 4 }, { "tank", 1 } },
			{ { "swarm", 10 }, { "soldier", 3 }, { "tank", 1 } },
		};
		return List;
	}
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
	FString Value;
	if (WWOSwitches::Value(TEXT("WWOKillTarget="), Value)) KillTarget = CmdKillTarget = FCString::Atof(*Value); // tests
	bTestRun = WWOSwitches::HasAutoTestText();
	if (WWOSwitches::Value(TEXT("WWOTimeLimit="), Value)) TimeLimit = FCString::Atof(*Value);
	if (bPvP && WWOSwitches::Value(TEXT("WWOBots="), Value)) BotsWanted = FMath::Clamp(FCString::Atoi(*Value), 0, 11);
	// ?bots=N: the launch screen's count for this match (a local PvP match fills both teams).
	const FString BotsOption = UGameplayStatics::ParseOption(Options, TEXT("bots"));
	OptionBots = bPvP && !BotsOption.IsEmpty() ? FMath::Clamp(FCString::Atoi(*BotsOption), 0, 11) : -1;
	// (The launch screen's count is exact, not a floor under the Bots setting: see UpdateBots.)
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
		if (bMidMatch) RetireBotFor(NewPlayer); // before the team is picked: the person takes the bot's side
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

void AWWOGameMode::RetireBotFor(AController* Joiner)
{
	// One bot leaves, from the bigger team, so the match stays the size it was.
	int32 Count[2] = { 0, 0 };
	TArray<AWWOBotController*> BotsNow;
	for (TActorIterator<AWWOBotController> It(GetWorld()); It; ++It)
	{
		BotsNow.Add(*It);
		if (const AWWOPlayerState* PS = It->GetPlayerState<AWWOPlayerState>(); PS && PS->Team >= 0 && PS->Team <= 1) ++Count[PS->Team];
	}
	if (BotsNow.Num() == 0) return;
	const int32 From = Count[1] > Count[0] ? 1 : 0;
	AWWOBotController* Leaving = BotsNow[0];
	for (AWWOBotController* Bot : BotsNow)
	{
		if (const AWWOPlayerState* PS = Bot->GetPlayerState<AWWOPlayerState>(); PS && PS->Team == From) Leaving = Bot;
	}
	UE_LOG(LogWWO, Display, TEXT("PvP: %s makes room for %s"), *GetNameSafe(Leaving->PlayerState), *GetNameSafe(Joiner));
	if (APawn* Body = Leaving->GetPawn()) Body->Destroy();
	Leaving->Destroy();
	--BotsSpawned;
	--BotsExtra;
}

void AWWOGameMode::Logout(AController* Exiting)
{
	// A person leaving a PvP match (Quit to main menu, or a dropped connection): a bot fills their
	// place, so the teams stay the size they were. UpdateBots spawns it and puts it on the smaller team.
	const APlayerController* Person = Cast<APlayerController>(Exiting);
	if (bPvP && Person && !Person->IsLocalController() && State == EWWOWaveState::Match)
	{
		++BotsExtra;
		UE_LOG(LogWWO, Display, TEXT("PvP: %s left, a bot takes the place"), *GetNameSafe(Exiting->PlayerState));
	}
	Super::Logout(Exiting);
	// Co-op: if the last player standing leaves, those who are down have nobody to revive them.
	// (Checked a moment later, once the leaver's character is gone.)
	if (!bPvP) GetWorldTimerManager().SetTimerForNextTick(this, &AWWOGameMode::OnPlayerDied);
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
	for (const APlayerState* Other : GameState->PlayerArray)
	{
		if (const AWWOPlayerState* O = Cast<AWWOPlayerState>(Other); O && O->Team >= 0 && O->Team <= 1) ++Count[O->Team];
	}
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
	for (APlayerState* Other : GameState->PlayerArray)
	{
		const AWWOPlayerState* PS = Cast<AWWOPlayerState>(Other);
		if (PS && PS != Mine && PS->Team >= 0 && PS->Team <= 1) ++Count[PS->Team];
	}
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
	if (bTester)
	{
		// The firing tester: nothing to fight. The map's own dummies go too; one is put in front of
		// the player once they are in (UpdateTester).
		for (TActorIterator<AWWOEnemy> It(GetWorld()); It; ++It) It->Destroy();
		for (TActorIterator<AWWOAlly> It(GetWorld()); It; ++It) It->Destroy();
		for (TActorIterator<AWWOTarget> It(GetWorld()); It; ++It) It->Destroy();
		State = EWWOWaveState::Idle;
		return;
	}
	if (Enabled < 0.5f || SpawnPoints().Num() == 0)
	{
		State = EWWOWaveState::Idle;
		return;
	}
	for (TActorIterator<AWWOEnemy> It(GetWorld()); It; ++It) It->Destroy(); // waves replace the hand-placed enemies
	Wave = 0;
	Countdown(StartDelay);
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
	const int32 StayBots = Mode ? Mode->OptionBots : -1;
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
	Queue.Reset();
	for (const TWeakObjectPtr<AWWOEnemy>& Enemy : Alive)
	{
		if (Enemy.IsValid() && !Enemy->IsDead()) Enemy->TakeHit(99999.f, false, Enemy->GetActorLocation());
	}
}

void AWWOGameMode::JumpToWave(int32 Number)
{
	if (State == EWWOWaveState::Idle || Number < 1) return;
	Queue.Reset();
	for (const TWeakObjectPtr<AWWOEnemy>& Enemy : Alive)
	{
		if (Enemy.IsValid()) Enemy->Destroy();
	}
	Alive.Reset();
	Wave = Number - 1;
	StartWave();
}

void AWWOGameMode::Countdown(float Seconds)
{
	State = EWWOWaveState::Countdown;
	Timer = Seconds;
}

void AWWOGameMode::Tick(float DeltaSeconds)
{
	WWO_PROF("GameMode");
	Super::Tick(DeltaSeconds);
	// Every player's death counts (online, players join later and respawn as new characters).
	for (AWWOCharacter* P : UWWOWorldSubsystem::Get(this)->GetPlayers())
	{
		if (BoundPlayers.Contains(P)) continue;
		P->Health->OnDied.AddUObject(this, &AWWOGameMode::OnPlayerDied);
		BoundPlayers.Add(P);
	}
	// Hosting: the game others see in their list follows this match (map, mode, players in it).
	if (GetNetMode() == NM_ListenServer && (PublishLeft -= DeltaSeconds) <= 0.f)
	{
		PublishLeft = 2.f;
		if (UWWONetSubsystem* Net = UWWONetSubsystem::Get(this)) Net->PublishMatch(this);
	}
	if (bTester) UpdateTester();
	if (bWaveTest) UpdateWaveTest(DeltaSeconds);
	if (bPvP) UpdateMatch(DeltaSeconds);
	if (bPvP) UpdateBots();
	if (bPvP)
	{
		// Bots' health and shield follow the Bots settings (a player's own are on the Player page).
		for (AWWOCharacter* P : UWWOWorldSubsystem::Get(this)->GetPlayers())
		{
			if (!P->GetBot() || (P->Health->BaseHealth == BotHealth && P->Health->BaseShield == BotShield)) continue;
			P->Health->BaseHealth = BotHealth;
			P->Health->BaseShield = BotShield;
			if (!P->Health->bDead)
			{
				P->Health->Health = P->Health->MaxHealth();
				P->Health->Shield = P->Health->MaxShield();
			}
		}
	}
	if (WWOSwitches::IsStress()) LogStress(DeltaSeconds);
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
	switch (State)
	{
	case EWWOWaveState::Countdown:
		Timer -= DeltaSeconds;
		if (Timer <= 0.f) StartWave();
		break;
	case EWWOWaveState::Spawning:
		Alive.RemoveAll([](const TWeakObjectPtr<AWWOEnemy>& E) { return !E.IsValid() || E->IsDead(); });
		if (Queue.Num() > 0)
		{
			Timer -= DeltaSeconds;
			if (Timer <= 0.f)
			{
				Timer = SpawnStagger;
				SpawnEnemy(Queue[0]);
				Queue.RemoveAt(0);
			}
		}
		else if (Alive.Num() == 0)
		{
			WaveCleared();
		}
		break;
	default:
		break;
	}
}

void AWWOGameMode::StartWave()
{
	Wave += 1;
	const TMap<FName, int32>& Plan = Waves()[FMath::Min(Wave - 1, Waves().Num() - 1)];
	const float Growth = FMath::Pow(1.25f, FMath::Max(Wave - Waves().Num(), 0));
	Queue.Reset();
	for (const auto& Pair : Plan)
	{
		const int32 Count = FMath::Max(FMath::RoundToInt(Pair.Value * SizeScale * Growth), 1);
		for (int32 i = 0; i < Count; ++i) Queue.Add(Pair.Key);
	}
	for (int32 i = Queue.Num() - 1; i > 0; --i) Queue.Swap(i, FMath::RandRange(0, i)); // shuffle
	Timer = 0.f;
	State = EWWOWaveState::Spawning;
}

void AWWOGameMode::WaveCleared()
{
	if (Wave >= FMath::RoundToInt(WavesToWin))
	{
		State = EWWOWaveState::Won;
		return;
	}
	Countdown(BreakTime);
}

void AWWOGameMode::OnPlayerDied()
{
	if (bPvP) return; // PvP deaths are scored per player (OnPvPDeath)
	if (State == EWWOWaveState::Idle || State == EWWOWaveState::Won || State == EWWOWaveState::Lost) return;
	// Alone, a living ally keeps the run going (you respawn). Online, downed players stay down
	// until revived, and only players can revive: everyone down loses.
	bool bAnyoneUp = GetNetMode() == NM_Standalone && UWWOWorldSubsystem::Get(this)->LivingTeammates().Num() > 0;
	for (const AWWOCharacter* P : UWWOWorldSubsystem::Get(this)->GetPlayers()) bAnyoneUp |= !P->Health->bDead;
	if (!bAnyoneUp)
	{
		State = EWWOWaveState::Lost; // nobody left standing
		RestartLeft = 4.f;
	}
}

AActor* AWWOGameMode::PickPoint(FName Kind) const
{
	// A spawn point that suits this enemy (if any are marked for it), away from you.
	const TArray<AActor*> All = SpawnPoints();
	TArray<AActor*> Points = All.FilterByPredicate([Kind](const AActor* P) { return P->Tags.Num() <= 1 || P->ActorHasTag(Kind); });
	if (Points.Num() == 0) Points = All;
	const TArray<AWWOCharacter*> Players = UWWOWorldSubsystem::Get(this)->GetPlayers();
	const TArray<AActor*> Far = Points.FilterByPredicate([&Players](const AActor* P)
	{
		for (const AWWOCharacter* Player : Players)
		{
			if (FVector::Dist(P->GetActorLocation(), Player->GetActorLocation()) < MinSpawnDistance) return false; // away from everyone
		}
		return true;
	});
	if (Far.Num() > 0) Points = Far;
	return Points.Num() > 0 ? Points[FMath::RandRange(0, Points.Num() - 1)] : nullptr;
}

void AWWOGameMode::SpawnEnemy(FName Kind)
{
	const AActor* Point = PickPoint(Kind);
	if (!Point) return;
	UClass* Class = Kind == "tank" ? AWWOTank::StaticClass() : (Kind == "soldier" ? AWWOSoldier::StaticClass() : AWWOSwarm::StaticClass());
	// Godot drops the enemy within 1.5 m of the marker (the Tank right on it) with its feet on
	// the marker, wherever that lands. Here the actor is its capsule's center, and a spot inside a
	// crate stack or off the navmesh is tried again (up to 8 times), so nobody starts boxed in.
	const UCapsuleComponent* Capsule = Class->GetDefaultObject<AWWOEnemy>()->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FCollisionQueryParams Overlap(SCENE_QUERY_STAT(WWOWaveSpawn), false);
	FVector Feet = Point->GetActorLocation();
	for (int32 Try = 0; Try < 8; ++Try)
	{
		const FVector Offset = Kind != "tank" ? FVector(FMath::FRandRange(-150.f, 150.f), FMath::FRandRange(-150.f, 150.f), 0.f) : FVector::ZeroVector;
		const FVector Spot = Point->GetActorLocation() + Offset;
		FNavLocation OnMesh;
		const bool bOnMesh = !Nav || Nav->ProjectPointToNavigation(Spot, OnMesh, FVector(Radius, Radius, 100.f));
		const bool bClear = !GetWorld()->OverlapBlockingTestByChannel(Spot + FVector(0, 0, HalfHeight), FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(Radius, HalfHeight - 2.f), Overlap);
		if (bOnMesh && bClear)
		{
			Feet = Spot;
			break;
		}
		FNavLocation Near;
		if (Try == 7 && Nav && Nav->ProjectPointToNavigation(Point->GetActorLocation(), Near, FVector(300.f, 300.f, 100.f)))
		{
			Feet = FVector(Near.Location.X, Near.Location.Y, Point->GetActorLocation().Z); // nothing clear nearby: the closest navmesh spot
		}
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Params.bDeferConstruction = true;
	const FTransform Where(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Feet + FVector(0, 0, HalfHeight));
	AWWOEnemy* Enemy = GetWorld()->SpawnActor<AWWOEnemy>(Class, Where, Params);
	if (!Enemy) return;
	Enemy->bRespawns = false; // wave enemies don't respawn
	Enemy->FinishSpawning(Where);
	Enemy->bAlerted = true; // waves come looking for you
	Enemy->OnDied.AddLambda([](AWWOEnemy* Dead) { Dead->SetLifeSpan(CorpseCleanup); });
	Alive.Add(Enemy);
}

FString AWWOGameMode::StatusText(FLinearColor& OutColor) const
{
	const int32 Total = FMath::RoundToInt(WavesToWin);
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
	case EWWOWaveState::Countdown:
		return Wave > 0 ? FString::Printf(TEXT("Wave %d of %d in %d"), Wave + 1, Total, FMath::CeilToInt(Timer))
			: FString::Printf(TEXT("Get ready: wave 1 of %d in %d"), Total, FMath::CeilToInt(Timer));
	case EWWOWaveState::Spawning:
		OutColor = FLinearColor(1.f, 0.9f, 0.75f);
		return FString::Printf(TEXT("WAVE %d / %d   ·   %d enemies left"), Wave, Total, Alive.Num() + Queue.Num());
	case EWWOWaveState::Won:
		OutColor = FLinearColor(0.6f, 1.f, 0.65f);
		return FString::Printf(TEXT("ARENA CLEARED   ·   %d waves"), Wave);
	case EWWOWaveState::Lost:
		OutColor = FLinearColor(1.f, 0.45f, 0.4f);
		return FString::Printf(TEXT("DEFEATED on wave %d   ·   restarting"), Wave);
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
	const int32 Best = Ranked.Num() > 0 ? Ranked[FMath::RandRange(0, FMath::Min(Ranked.Num(), Others.Num() > 0 ? 3 : Ranked.Num()) - 1)].Value
		: FMath::RandRange(0, Spots.Num() - 1);
	// Facing the middle of the map.
	FTransform Spot = Spots[Best];
	FVector ToMiddle = -Spot.GetLocation();
	ToMiddle.Z = 0.f;
	if (!ToMiddle.IsNearlyZero()) Spot.SetRotation(ToMiddle.Rotation().Quaternion());
	return Spot;
}

// --- Firing tester ---------------------------------------------------------------------------------

void AWWOGameMode::UpdateTester()
{
	// One dummy straight ahead of where the player starts, far enough to read the kick against.
	AWWOCharacter* P = UWWOWorldSubsystem::Get(this)->GetPlayer();
	if (TesterTarget.IsValid() || !P || !bStarted) return;
	if (!bTesterHomeSet)
	{
		bTesterHomeSet = true;
		TesterHome = P->GetActorTransform();
	}
	const FVector Forward = FRotator(0.f, TesterHome.Rotator().Yaw, 0.f).Vector();
	FVector At = TesterHome.GetLocation() + Forward * TesterTargetDistance * WWO::M;
	// Stand it on the floor under that point.
	FHitResult Floor;
	if (GetWorld()->LineTraceSingleByChannel(Floor, At + FVector(0, 0, 300.f), At - FVector(0, 0, 1000.f), ECC_Visibility)) At = Floor.ImpactPoint;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TesterTarget = GetWorld()->SpawnActor<AWWOTarget>(AWWOTarget::StaticClass(), At, FRotator(0.f, TesterHome.Rotator().Yaw + 180.f, 0.f), Params);
	UE_LOG(LogWWO, Display, TEXT("Tester: target dummy %.0f m ahead of the start (%s)"), TesterTargetDistance, TesterTarget.IsValid() ? TEXT("placed") : TEXT("FAILED"));
}

// --- Bots and load (PvP stress test) -------------------------------------------------------------

void AWWOGameMode::UpdateBots()
{
	// The diagnostics below (bot states, sight lines, test shots at every head) are for the stress
	// runs only: a normal match doesn't pay for them or fill its log with them.
	const bool bStressRun = WWOSwitches::HasStressText();
	// One a frame, once the host's own player is in.
	if (bStressRun && BotsSpawned > 0 && (BotLogClock += GetWorld()->GetDeltaSeconds()) >= BotLogNext)
	{
		// How the bots are getting on (every 10 s).
		BotLogNext += WWOSwitches::Has(TEXT("WWOStressShots")) ? 4.f : 10.f;
		int32 Count[4] = {}, Following = 0;
		for (TActorIterator<AWWOBotController> It(GetWorld()); It; ++It)
		{
			++Count[FMath::Clamp(It->StateIndex(), 0, 3)];
			Following += It->IsFollowing() ? 1 : 0;
		}
		UE_LOG(LogWWO, Display, TEXT("Bots team: %d following a leader, callouts acted on %d"), Following, AWWOBotController::Callouts);
		UE_LOG(LogWWO, Display, TEXT("Bots: t=%.0f roam %d hunt %d fight %d retreat %d; sightings %d, shots %d, kills %d, paths %d (%d failed), stuck hops %d, gave up %d"),
			BotLogClock, Count[0], Count[1], Count[2], Count[3], AWWOBotController::Sightings, AWWOBotController::ShotsFired, PvPKills,
			AWWOBotController::PathsFound, AWWOBotController::PathsFailed, AWWOBotController::StuckJumps, AWWOBotController::Regoals);
		// -WWOStressShots: the host's player turns to the nearest other player in sight, a teammate and
		// a foe in turn, and the view is saved a moment later (Saved/AutoTest/41_pvp_view_<t>.png).
		if (WWOSwitches::Has(TEXT("WWOStressShots")))
		{
			APlayerController* PC = GetWorld()->GetFirstPlayerController();
			AWWOCharacter* Me = PC ? Cast<AWWOCharacter>(PC->GetPawn()) : nullptr;
			static bool bWantFoe = false;
			bWantFoe = !bWantFoe;
			AWWOCharacter* Best = nullptr;
			float BestD = 6000.f;
			for (TActorIterator<AWWOCharacter> It(GetWorld()); Me && It; ++It)
			{
				if (*It == Me || It->Health->bDead || UWWOWorldSubsystem::CanHurt(Me, *It) != bWantFoe) continue;
				const float D = FVector::Dist(It->GetActorLocation(), Me->GetActorLocation());
				FHitResult Block;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOStressShot), false, Me);
				Params.AddIgnoredActor(*It);
				if (D < BestD && !GetWorld()->LineTraceSingleByChannel(Block, Me->EyeLocation(), It->GetActorLocation(), ECC_Visibility, Params)) { BestD = D; Best = *It; }
			}
			if (Best)
			{
				const FVector Dir = (Best->GetActorLocation() - Me->EyeLocation()).GetSafeNormal();
				Me->SetViewRotation(FMath::Asin(Dir.Z), Dir.Rotation().Yaw);
				// And something for the HUD to show: a hit from behind and to the right, and a slow.
				Me->Health->DamageCameFrom(Me->GetActorLocation() - Dir * 400.f + FVector::CrossProduct(FVector::UpVector, Dir) * 400.f);
				Me->ApplySlow(0.4f, 3.f);
				const FString Path = FPaths::ProjectSavedDir() / TEXT("AutoTest") / FString::Printf(TEXT("41_pvp_view_%03d_%s.png"), FMath::RoundToInt(BotLogClock), bWantFoe ? TEXT("foe") : TEXT("mate"));
				FTimerHandle Handle;
				GetWorld()->GetTimerManager().SetTimer(Handle, [Path] { FScreenshotRequest::RequestScreenshot(Path, true, false); }, 0.25f, false);
				UE_LOG(LogWWO, Display, TEXT("Bots view: %s at %.0f m, team %d (mine %d) -> %s"), bWantFoe ? TEXT("foe") : TEXT("teammate"), BestD / 100.f,
					UWWOWorldSubsystem::TeamOf(Best), UWWOWorldSubsystem::TeamOf(Me), *Path);
			}
		}
		UE_LOG(LogWWO, Display, TEXT("Bots sounds: %s"), *UWWOSound::Summary());
		int32 HeadShots = 0, HeadCrits = 0, HeadOldCrits = 0;
		float HeadOffEyes = 0.f;
		for (TActorIterator<AWWOCharacter> It(GetWorld()); It; ++It) It->ProbeHeadShots(HeadShots, HeadCrits, HeadOldCrits, HeadOffEyes);
		UE_LOG(LogWWO, Display, TEXT("Bots heads: %d test shots at the drawn heads, %d count as headshots (%d under the old hitboxes); head up to %.0f cm from the eye point"),
			HeadShots, HeadCrits, HeadOldCrits, HeadOffEyes);
		UE_LOG(LogWWO, Display, TEXT("Bots kit: melees %d, grenades %d, abilities %d, ults %d, retreats %d (%d found no cover), gun swaps %d; stuck hops roaming %d hunting %d fighting %d retreating %d"),
			AWWOBotController::Melees, AWWOBotController::Grenades, AWWOBotController::AbilitiesUsed, AWWOBotController::Ults,
			AWWOBotController::Retreats, AWWOBotController::NoCover, AWWOBotController::Swaps,
			AWWOBotController::StuckIn[0], AWWOBotController::StuckIn[1], AWWOBotController::StuckIn[2], AWWOBotController::StuckIn[3]);
	}
	// How many: the launch screen's count when it gave one (a local match fills the map's teams
	// exactly), else the Bots setting (or -WWOBots); then one more per person who left, one fewer
	// per person who joined.
	// (A test run takes only what its command line asks for, not the saved Bots setting.)
	const int32 Asked = OptionBots >= 0 ? OptionBots : FMath::Max(BotsWanted, bTestRun ? 0 : FMath::RoundToInt(Bots));
	const int32 Wanted = FMath::Clamp(Asked + BotsExtra, 0, 11);
	if (BotsSpawned >= Wanted || State != EWWOWaveState::Match || !UWWOWorldSubsystem::Get(this)->GetPlayer()) return;
	AWWOBotController* Bot = GetWorld()->SpawnActor<AWWOBotController>();
	if (!Bot) return;
	++BotsSpawned;
	if (Bot->PlayerState) Bot->PlayerState->SetPlayerName(FString::Printf(TEXT("bot%d"), BotsSpawned));
	BalanceTeam(Bot);
	// -WWOBotsPeace: every bot on one team, so nobody fights and they all just roam the map (a
	// steady scene of moving characters for performance tests).
	if (WWOSwitches::Has(TEXT("WWOBotsPeace"))) SetTeam(Bot->GetPlayerState<AWWOPlayerState>(), 0);
	TeamGiven.Add(Bot->GetPlayerState<AWWOPlayerState>());
	RestartPlayer(Bot);
	if (AWWOCharacter* Body = Cast<AWWOCharacter>(Bot->GetPawn()))
	{
		const FTransform Spot = PvPSpawnFor(Body);
		Body->TeleportTo(Spot.GetLocation(), Spot.Rotator());
		// -WWOBotsLineUp: a repeatable scene for performance tests. The bots stand in rows of four in
		// front of the host, facing it, and do nothing.
		if (const AWWOCharacter* Host = UWWOWorldSubsystem::Get(this)->GetPlayer(); Host && WWOSwitches::Has(TEXT("WWOBotsLineUp")))
		{
			Bot->bParked = true;
			const int32 Index = BotsSpawned - 1;
			const FVector Forward = Host->GetActorForwardVector(), Right = Host->GetActorRightVector();
			const FVector At = Host->GetActorLocation() + Forward * (500.f + (Index / 4) * 220.f) + Right * ((Index % 4) - 1.5f) * 170.f + FVector(0, 0, 150.f);
			Body->TeleportTo(At, (-Forward).Rotation(), false, true);
			Body->SetViewRotation(0.f, (-Forward).Rotation().Yaw);
		}
	}
	UE_LOG(LogWWO, Display, TEXT("PvP: bot %d of %d joined (%s)"), BotsSpawned, Wanted, Bot->GetPawn() ? *Bot->GetPawn()->GetName() : TEXT("no body"));
}

void AWWOGameMode::LogStress(float DeltaSeconds)
{
	StressClock += DeltaSeconds;
	StressFrames += 1.f;
	StressWorst = FMath::Max(StressWorst, DeltaSeconds);
	StressGame += FPlatformTime::ToMilliseconds(GGameThreadTime);
	StressRender += FPlatformTime::ToMilliseconds(GRenderThreadTime);
	StressGpu += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
	// -WWOProfileGPU: one GPU pass breakdown in the log, 12 s in (or -WWOProfileAt=seconds), and a screenshot 2 s before it.
	// -WWOFogTest=N: N thick Veil-sized spore fogs around the host 3 s in (-WWOFogAhead=meters: how far in front).
	int32 FogCount = 0;
	if (StressClock >= 3.f && StressClock - DeltaSeconds < 3.f && WWOSwitches::Value(TEXT("WWOFogTest="), FogCount))
	{
		float Ahead = 0.f;
		WWOSwitches::Value(TEXT("WWOFogAhead="), Ahead);
		if (const AWWOCharacter* Host = UWWOWorldSubsystem::Get(this)->GetPlayer())
		{
			for (int32 i = 0; i < FogCount; ++i)
			{
				AWWOSporeFog::SpawnFog(GetWorld(), Host->GetActorLocation() + Host->GetActorForwardVector() * (Ahead + i * 2.f) * WWO::M, 7.f, 600.f, true);
			}
		}
	}
	// -WWOBotShots (with -WWOHostPvP -WWOBots=N): the host stands still, unkillable, and keeps
	// looking at the nearest bot, saving a picture every 1.5 s (Saved/AutoTest/43_bot_NN.png): what
	// a bot coming at you and aiming at you looks like from where you stand.
	if (WWOSwitches::Has(TEXT("WWOBotShots")))
	{
		AWWOCharacter* Host = UWWOWorldSubsystem::Get(this)->GetPlayer();
		AWWOCharacter* Near = nullptr;
		if (Host)
		{
			for (TActorIterator<AWWOCharacter> It(GetWorld()); It; ++It)
			{
				if (*It == Host || It->Health->bDead) continue;
				if (!Near || FVector::DistSquared(It->GetActorLocation(), Host->GetActorLocation()) < FVector::DistSquared(Near->GetActorLocation(), Host->GetActorLocation())) Near = *It;
			}
			Host->Health->GodMode = 1.f;
		}
		// -WWOTestKills: every 4 s the host kills an enemy bot, every other one with a precision hit,
		// and saves a picture half a second later (44_kill_NN.png): the "Kill +1" by the reticle.
		if (Host && WWOSwitches::Has(TEXT("WWOTestKills")))
		{
			static float NextKill = 8.f, ShotAt = -1.f;
			static int32 Done = 0;
			if (StressClock >= NextKill)
			{
				for (TActorIterator<AWWOCharacter> It(GetWorld()); It; ++It)
				{
					if (*It == Host || It->Health->bDead || !UWWOWorldSubsystem::CanHurt(Host, *It)) continue;
					FWWODamageBy By(Host);
					It->TakeHit(99999.f, Done % 2 == 1, It->GetActorLocation());
					NextKill = StressClock + (Done % 3 == 2 ? 0.3f : 4.f); // now and then two close together
					ShotAt = StressClock + 0.5f;
					++Done;
					break;
				}
			}
			if (ShotAt > 0.f && StressClock >= ShotAt)
			{
				ShotAt = -1.f;
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("AutoTest") / FString::Printf(TEXT("44_kill_%02d.png"), Done), true, false);
			}
		}
		// -WWOTestFog (with -WWOBotsIdle): the host gets Spore Fog Damage, and every 12 s a fog of
		// theirs (4 m, 8 s) lands on an enemy bot; the bot's shield and health are logged each second.
		if (Host && WWOSwitches::Has(TEXT("WWOTestFog")))
		{
			static float NextFog = 8.f, NextLog = 0.f;
			static TWeakObjectPtr<AWWOCharacter> Fogged;
			if (StressClock >= NextFog)
			{
				NextFog = StressClock + 12.f;
				UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(Host);
				if (Loadout && !Host->Abilities->Equipped("spore_fog_damage"))
				{
					for (UWWOAbilityData* A : Loadout->Abilities) if (A && A->Id == "spore_fog_damage") Loadout->EquipAbility(0, A);
				}
				for (TActorIterator<AWWOCharacter> It(GetWorld()); It; ++It)
				{
					if (*It == Host || It->Health->bDead || !UWWOWorldSubsystem::CanHurt(Host, *It)) continue;
					Fogged = *It;
					AWWOSporeFog::SpawnFog(GetWorld(), It->GetActorLocation(), 4.f, 8.f, false, Host);
					UE_LOG(LogWWO, Display, TEXT("FogTest: fog on %s (equipped %d)"), *It->GetName(), Host->Abilities->Equipped("spore_fog_damage") ? 1 : 0);
					break;
				}
			}
			if (Fogged.IsValid() && StressClock >= NextLog)
			{
				NextLog = StressClock + 1.f;
				UE_LOG(LogWWO, Display, TEXT("FogTest: t %.0f %s shield %.0f health %.0f dead %d, hit markers %d"), StressClock, *Fogged->GetName(), Fogged->Health->Shield, Fogged->Health->Health, Fogged->Health->bDead ? 1 : 0, UWWOWorldSubsystem::HitsMarked);
			}
		}
		// -WWOTestCharge: the host gets Static Charge, filled (so it arms itself); pictures of the
		// screen with it charged (46_static_N.png).
		if (Host && WWOSwitches::Has(TEXT("WWOTestCharge")))
		{
			static int32 Shots = 0;
			static float NextShot = 9.f;
			UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(Host);
			if (Loadout && !Host->Abilities->Equipped("static_charge"))
			{
				for (UWWOAbilityData* A : Loadout->Abilities) if (A && A->Id == "static_charge") Loadout->EquipAbility(0, A);
			}
			if (StressClock > 6.f) Host->Abilities->OnDamageTaken(99999.f);
			if (StressClock >= NextShot && Shots < 3)
			{
				NextShot = StressClock + 0.7f;
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("AutoTest") / FString::Printf(TEXT("46_static_%d.png"), Shots++), true, false);
			}
		}
		if (Host && Near)
		{
			const FRotator To = (Near->GetActorLocation() - Host->GetActorLocation()).Rotation();
			Host->SetViewRotation(FMath::DegreesToRadians(To.Pitch), To.Yaw);
			static int32 Saved = 0;
			static float Next = 6.f;
			if (StressClock >= Next && FVector::Dist(Near->GetActorLocation(), Host->GetActorLocation()) < 1500.f)
			{
				Next = StressClock + 1.5f;
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("AutoTest") / FString::Printf(TEXT("43_bot_%02d.png"), Saved++), true, false);
			}
		}
	}
	// -WWOFxGallery (with -WWOHostPvP -WWOBots=1 -WWOBotsIdle, on L_TestRange): stages the kit effects
	// in front of the host one after another and saves each (Saved/AutoTest/42_fx_*.png).
	if (WWOSwitches::Has(TEXT("WWOFxGallery")))
	{
		auto At = [this, DeltaSeconds](float T) { return StressClock >= T && StressClock - DeltaSeconds < T; };
		auto Save = [this](const TCHAR* Name, float After)
		{
			const FString Path = FPaths::ProjectSavedDir() / TEXT("AutoTest") / FString::Printf(TEXT("42_fx_%s.png"), Name);
			FTimerHandle Handle;
			GetWorld()->GetTimerManager().SetTimer(Handle, [Path] { FScreenshotRequest::RequestScreenshot(Path, true, false); }, FMath::Max(After, 0.03f), false); // (a timer of 0 never fires)
		};
		AWWOCharacter* Host = UWWOWorldSubsystem::Get(this)->GetPlayer();
		AWWOCharacter* Bot = nullptr;
		for (TActorIterator<AWWOCharacter> It(GetWorld()); It; ++It) if (*It != Host && !Bot) Bot = *It;
		if (Host)
		{
			const FVector Fwd = Host->GetActorForwardVector(), Right = Host->GetActorRightVector(), Feet = Host->FeetLocation();
			const FLinearColor Orange(0.95f, 0.5f, 0.1f);
			if (At(3.f))
			{
				// Someone standing in front of you, a damage pool to the right, a spore fog beyond on the left.
				Host->SetViewRotation(-0.12f, Host->GetActorRotation().Yaw);
				if (Bot) Bot->TeleportTo(Host->GetActorLocation() + Fwd * 330.f - Right * 60.f, (-Fwd).Rotation());
				AWWOSporePool::Spawn(GetWorld(), Feet + Fwd * 620.f + Right * 330.f, 2.6f, 0.f, 60.f, FLinearColor(0.3f, 0.6f, 1.f), "voltvine", nullptr);
				AWWOSporeFog::SpawnFog(GetWorld(), Feet + Fwd * 1150.f - Right * 520.f + FVector(0, 0, 150.f), 3.6f, 60.f, true);
			}
			if (At(6.f)) Save(TEXT("1_pool_and_fog"), 0.f);
			if (At(6.5f) && Bot) { Bot->Body->SetHandGlow(Orange, 0.85f); Save(TEXT("2_glowing_fists"), 0.4f); }
			if (At(9.5f))
			{
				// A blast, then the scorch it leaves.
				FWWOGrenadeLook Look;
				Look.Color = Orange;
				Look.Flavor = "ironbark";
				AWWOGrenade::SpawnBlast(GetWorld(), Look, Feet + Fwd * 560.f - Right * 300.f + FVector(0, 0, 30.f), 3.f);
				Save(TEXT("4_blast"), 0.08f);
				Save(TEXT("5_scorch"), 1.2f);
			}
		}
	}
	static const float ProfileAt = [] { float At = 12.f; WWOSwitches::Value(TEXT("WWOProfileAt="), At); return At; }(); // read once, not every frame
	if (StressClock >= ProfileAt - 2.f && StressClock - DeltaSeconds < ProfileAt - 2.f && WWOSwitches::Has(TEXT("WWOProfileGPU"))) FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("AutoTest") / TEXT("profile_view.png"), true, false);
	if (StressClock >= ProfileAt && StressClock - DeltaSeconds < ProfileAt && WWOSwitches::Has(TEXT("WWOProfileGPU")))
	{
		// What is casting shadows and can move (each one makes the sun's shadow map redraw around it).
		TMap<FString, FVector3f> Casters; // name -> count, biggest radius (m), visible count
		for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
		{
			const UPrimitiveComponent* C = *It;
			if (C->GetWorld() != GetWorld() || !C->IsRegistered() || !C->CastShadow || C->Mobility != EComponentMobility::Movable) continue;
			if (!C->IsVisible() && !C->bCastHiddenShadow) continue;
			const UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(C);
			const FString Name = FString::Printf(TEXT("%s / %s / %s"), *GetNameSafe(C->GetOwner() ? C->GetOwner()->GetClass() : nullptr), *C->GetClass()->GetName(),
				SM ? *GetNameSafe(SM->GetStaticMesh()) : TEXT("-"));
			FVector3f& V = Casters.FindOrAdd(Name);
			V.X += 1.f;
			V.Y = FMath::Max(V.Y, (float)C->Bounds.SphereRadius / WWO::M);
		}
		Casters.ValueSort([](const FVector3f& A, const FVector3f& B) { return A.X * A.Y > B.X * B.Y; });
		for (const auto& Pair : Casters) UE_LOG(LogWWO, Display, TEXT("Shadow caster (movable): %3.0f x %s, biggest radius %.1f m"), Pair.Value.X, *Pair.Key, Pair.Value.Y);
	}
	if (StressClock >= ProfileAt && StressClock - DeltaSeconds < ProfileAt && WWOSwitches::Has(TEXT("WWOProfileGPU"))) GEngine->Exec(GetWorld(), TEXT("ProfileGPU"));
	if (StressClock < StressNext) return;
	StressNext += 5.f;
	const UNetDriver* Driver = GetWorld()->GetNetDriver();
	int32 Out = 0, In = 0, Lost = 0;
	if (Driver)
	{
		for (const UNetConnection* C : Driver->ClientConnections)
		{
			if (!C) continue;
			Out += C->OutBytesPerSecond;
			In += C->InBytesPerSecond;
			Lost += C->OutPacketsLost;
		}
	}
	UE_LOG(LogWWO, Display, TEXT("Stress host: t=%.0f %d players (%d bots), frame avg %.1f ms worst %.1f ms, out %.1f KB/s in %.1f KB/s to %d joiners, lost %d, kills %d, %s"),
		StressClock, UWWOWorldSubsystem::Get(this)->GetPlayers().Num(), BotsSpawned, 5000.f / FMath::Max(StressFrames, 1.f), StressWorst * 1000.f,
		Out / 1024.f, In / 1024.f, Driver ? Driver->ClientConnections.Num() : 0, Lost, PvPKills, *GetGameState<AWWOGameState>()->StatusLine);
	UE_LOG(LogWWO, Display, TEXT("Stress threads: t=%.0f game %.1f ms, render %.1f ms, GPU %.1f ms (averages)"), StressClock,
		StressGame / FMath::Max(StressFrames, 1.f), StressRender / FMath::Max(StressFrames, 1.f), StressGpu / FMath::Max(StressFrames, 1.f));
	// What's alive right now (effects, grenades, fogs...), to line slow stretches up with.
	TMap<FString, int32> AliveNow;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		const FString ClassName = It->GetClass()->GetName();
		if (ClassName.StartsWith(TEXT("WWO")) && !It->IsA<APawn>() && !It->IsA<AController>() && !It->IsA<AInfo>()) ++AliveNow.FindOrAdd(ClassName);
	}
	FString AliveText;
	for (const auto& Pair : AliveNow) AliveText += FString::Printf(TEXT(" %s %d"), *Pair.Key.RightChop(3), Pair.Value);
	UE_LOG(LogWWO, Display, TEXT("Stress alive: t=%.0f%s"), StressClock, AliveText.IsEmpty() ? TEXT(" nothing") : *AliveText);
	StressGame = StressRender = StressGpu = 0.f;
	StressFrames = 0.f;
	StressWorst = 0.f;
	FString Value;
	if (WWOSwitches::Value(TEXT("WWOStressTime="), Value) && StressClock >= FCString::Atof(*Value))
	{
		UE_LOG(LogWWO, Display, TEXT("Stress host: done"));
		FPlatformMisc::RequestExit(false);
	}
}
