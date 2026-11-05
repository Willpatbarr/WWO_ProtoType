#include "Game/WWOGameMode.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOHealthComponent.h"
#include "Enemies/WWOEnemy.h"
#include "Enemies/WWOEnemyTypes.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	constexpr float MinSpawnDistance = 1400.f; // enemies don't appear right next to you (cm)
	constexpr float CorpseCleanup = 4.f;

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
}

void AWWOGameMode::StartPlay()
{
	Super::StartPlay();
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

void AWWOGameMode::Restart()
{
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
}

void AWWOGameMode::LoadMap(UObject* WorldContext, FName Level)
{
	UGameplayStatics::OpenLevel(WorldContext, Level);
}

void AWWOGameMode::SkipWave()
{
	Queue.Reset();
	for (const TWeakObjectPtr<AWWOEnemy>& Enemy : Alive)
	{
		if (Enemy.IsValid() && !Enemy->IsDead()) Enemy->TakeHit(99999.f, false, Enemy->GetActorLocation());
	}
}

void AWWOGameMode::Countdown(float Seconds)
{
	State = EWWOWaveState::Countdown;
	Timer = Seconds;
}

void AWWOGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBoundToPlayer)
	{
		if (AWWOCharacter* P = UWWOWorldSubsystem::Get(this)->GetPlayer())
		{
			P->Health->OnDied.AddUObject(this, &AWWOGameMode::OnPlayerDied);
			bBoundToPlayer = true;
		}
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
	if (UWWOWorldSubsystem::Get(this)->LivingTeammates().Num() == 0)
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
	const AWWOCharacter* Player = UWWOWorldSubsystem::Get(this)->GetPlayer();
	const TArray<AActor*> Far = Points.FilterByPredicate([Player](const AActor* P)
	{
		return !Player || FVector::Dist(P->GetActorLocation(), Player->GetActorLocation()) >= MinSpawnDistance;
	});
	if (Far.Num() > 0) Points = Far;
	return Points.Num() > 0 ? Points[FMath::RandRange(0, Points.Num() - 1)] : nullptr;
}

void AWWOGameMode::SpawnEnemy(FName Kind)
{
	const AActor* Point = PickPoint(Kind);
	if (!Point) return;
	UClass* Class = Kind == "tank" ? AWWOTank::StaticClass() : (Kind == "soldier" ? AWWOSoldier::StaticClass() : AWWOSwarm::StaticClass());
	const FVector Offset = Kind != "tank" ? FVector(FMath::FRandRange(-150.f, 150.f), FMath::FRandRange(-150.f, 150.f), 0.f) : FVector::ZeroVector;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Params.bDeferConstruction = true;
	const FTransform Where(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Point->GetActorLocation() + Offset + FVector(0, 0, 150.f));
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
