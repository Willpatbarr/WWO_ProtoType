#include "Game/WWOGameMode.h"
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
#include "Misc/CommandLine.h"
#include "Engine/TargetPoint.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/PlayerStartPIE.h"
#include "EngineUtils.h"
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
}

void AWWOGameMode::StartPlay()
{
	Super::StartPlay();
	bWaveTest = FParse::Param(FCommandLine::Get(), TEXT("WWOWaveTest"));
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
	return World->SpawnActor<APawn>(PawnClass, Spot, Rotation, SpawnParams);
}

void AWWOGameMode::Restart()
{
	LoadMap(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
}

void AWWOGameMode::LoadMap(UObject* WorldContext, FName Level)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World) return;
	switch (World->GetNetMode())
	{
	case NM_ListenServer:
		UE_LOG(LogWWO, Display, TEXT("Net: host moving everyone to %s"), *Level.ToString());
		World->ServerTravel(Level.ToString() + TEXT("?listen"));
		break;
	case NM_Client:
		if (UWWONetSubsystem* Net = UWWONetSubsystem::Get(World)) Net->Status = TEXT("Only the host can restart or change maps");
		UE_LOG(LogWWO, Display, TEXT("Net: only the host changes maps (asked for %s)"), *Level.ToString());
		break;
	default:
		UGameplayStatics::OpenLevel(World, Level);
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
	Super::Tick(DeltaSeconds);
	// Every player's death counts (online, players join later and respawn as new characters).
	for (AWWOCharacter* P : UWWOWorldSubsystem::Get(this)->GetPlayers())
	{
		if (BoundPlayers.Contains(P)) continue;
		P->Health->OnDied.AddUObject(this, &AWWOGameMode::OnPlayerDied);
		BoundPlayers.Add(P);
	}
	if (bWaveTest) UpdateWaveTest(DeltaSeconds);
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
	if (State == EWWOWaveState::Idle || State == EWWOWaveState::Won || State == EWWOWaveState::Lost) return;
	bool bAnyoneUp = UWWOWorldSubsystem::Get(this)->LivingTeammates().Num() > 0;
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
	switch (State)
	{
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
