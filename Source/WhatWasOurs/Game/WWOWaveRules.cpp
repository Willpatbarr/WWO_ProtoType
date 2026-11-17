// The PvE waves (Game/WWOWaveRules.h). Moved from AWWOGameMode (the waves branch of Begin, the
// players' death binding at the top of its Tick, its wave state machine, StartWave, WaveCleared,
// OnPlayerDied, PickPoint, SpawnEnemy, SkipWave, JumpToWave and the wave status lines) as they were.

#include "Game/WWOWaveRules.h"
#include "Game/WWOGameMode.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
#include "Enemies/WWOEnemy.h"
#include "Enemies/WWOEnemyKinds.h"
#include "Systems/WWOWorldSubsystem.h"
#include "WWOIds.h"
#include "WWOTypes.h"
#include "Components/CapsuleComponent.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "EngineUtils.h"

namespace
{
	constexpr float MinSpawnDistance = 1400.f; // enemies don't appear right next to you (cm)
	constexpr float CorpseCleanup = 4.f;

	// The waves, in order: kinds of the enemy registry (WWOEnemyKinds) and how many. Past the last one, it repeats the last wave 25% bigger each time.
	const TArray<TMap<FName, int32>>& Waves()
	{
		static const TArray<TMap<FName, int32>> List = {
			{ { WWOIds::Enemy::Swarm, 6 } },
			{ { WWOIds::Enemy::Swarm, 6 }, { WWOIds::Enemy::Soldier, 2 } },
			{ { WWOIds::Enemy::Swarm, 8 }, { WWOIds::Enemy::Soldier, 3 } },
			{ { WWOIds::Enemy::Soldier, 4 }, { WWOIds::Enemy::Tank, 1 } },
			{ { WWOIds::Enemy::Swarm, 10 }, { WWOIds::Enemy::Soldier, 3 }, { WWOIds::Enemy::Tank, 1 } },
		};
		return List;
	}
}

FWWOWaveRules::~FWWOWaveRules()
{
	for (const TPair<TWeakObjectPtr<UWWOHealthComponent>, FDelegateHandle>& B : Bindings)
	{
		if (UWWOHealthComponent* H = B.Key.Get()) H->OnDied.Remove(B.Value);
	}
}

void FWWOWaveRules::Begin()
{
	if (Mode.Enabled < 0.5f || Mode.SpawnPoints().Num() == 0)
	{
		State = EWWOWaveState::Idle;
		return;
	}
	for (TActorIterator<AWWOEnemy> It(Mode.GetWorld()); It; ++It) It->Destroy(); // waves replace the hand-placed enemies
	Wave = 0;
	Countdown(Mode.StartDelay);
}

void FWWOWaveRules::Update(float DeltaSeconds)
{
	// Every player's death counts (online, players join later and respawn as new characters).
	for (AWWOCharacter* P : UWWOWorldSubsystem::Get(&Mode)->GetPlayers())
	{
		if (BoundPlayers.Contains(P)) continue;
		Bindings.Add({ P->Health, P->Health->OnDied.AddRaw(this, &FWWOWaveRules::OnAnyoneDown) });
		BoundPlayers.Add(P);
	}
}

void FWWOWaveRules::Advance(float DeltaSeconds)
{
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
				Timer = Mode.SpawnStagger;
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

void FWWOWaveRules::OnPlayerLeft(AController* Exiting)
{
	// Co-op: if the last player standing leaves, those who are down have nobody to revive them.
	// (Checked a moment later, once the leaver's character is gone.)
	AWWOGameMode* GM = &Mode;
	GM->GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(GM, [GM] { if (FWWOWaveRules* W = GM->Waves()) W->OnAnyoneDown(); }));
}

bool FWWOWaveRules::IsOver() const
{
	return State == EWWOWaveState::Won || State == EWWOWaveState::Lost;
}

void FWWOWaveRules::SkipWave()
{
	Queue.Reset();
	for (const TWeakObjectPtr<AWWOEnemy>& Enemy : Alive)
	{
		if (Enemy.IsValid() && !Enemy->IsDead()) Enemy->TakeHit(99999.f, false, Enemy->GetActorLocation());
	}
}

void FWWOWaveRules::JumpToWave(int32 Number)
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

void FWWOWaveRules::Countdown(float Seconds)
{
	State = EWWOWaveState::Countdown;
	Timer = Seconds;
}

void FWWOWaveRules::StartWave()
{
	Wave += 1;
	const TMap<FName, int32>& Plan = Waves()[FMath::Min(Wave - 1, Waves().Num() - 1)];
	const float Growth = FMath::Pow(1.25f, FMath::Max(Wave - Waves().Num(), 0));
	Queue.Reset();
	for (const auto& Pair : Plan)
	{
		const int32 Count = FMath::Max(FMath::RoundToInt(Pair.Value * Mode.SizeScale * Growth), 1);
		for (int32 i = 0; i < Count; ++i) Queue.Add(Pair.Key);
	}
	for (int32 i = Queue.Num() - 1; i > 0; --i) Queue.Swap(i, WWO::RandRange(0, i)); // shuffle
	Timer = 0.f;
	State = EWWOWaveState::Spawning;
}

void FWWOWaveRules::WaveCleared()
{
	if (Wave >= FMath::RoundToInt(Mode.WavesToWin))
	{
		State = EWWOWaveState::Won;
		return;
	}
	Countdown(Mode.BreakTime);
}

void FWWOWaveRules::OnAnyoneDown()
{
	if (State == EWWOWaveState::Idle || State == EWWOWaveState::Won || State == EWWOWaveState::Lost) return;
	// Alone, a living ally keeps the run going (you respawn). Online, downed players stay down
	// until revived, and only players can revive: everyone down loses.
	bool bAnyoneUp = Mode.GetNetMode() == NM_Standalone && UWWOWorldSubsystem::Get(&Mode)->LivingTeammates().Num() > 0;
	for (const AWWOCharacter* P : UWWOWorldSubsystem::Get(&Mode)->GetPlayers()) bAnyoneUp |= !P->Health->bDead;
	if (!bAnyoneUp)
	{
		State = EWWOWaveState::Lost; // nobody left standing
		Mode.RestartLeft = 4.f;
	}
}

AActor* FWWOWaveRules::PickPoint(FName Kind) const
{
	// A spawn point that suits this enemy (if any are marked for it), away from you.
	const TArray<AActor*> All = Mode.SpawnPoints();
	TArray<AActor*> Points = All.FilterByPredicate([Kind](const AActor* P) { return P->Tags.Num() <= 1 || P->ActorHasTag(Kind); });
	if (Points.Num() == 0) Points = All;
	const TArray<AWWOCharacter*> Players = UWWOWorldSubsystem::Get(&Mode)->GetPlayers();
	const TArray<AActor*> Far = Points.FilterByPredicate([&Players](const AActor* P)
	{
		for (const AWWOCharacter* Player : Players)
		{
			if (FVector::Dist(P->GetActorLocation(), Player->GetActorLocation()) < MinSpawnDistance) return false; // away from everyone
		}
		return true;
	});
	if (Far.Num() > 0) Points = Far;
	return Points.Num() > 0 ? Points[WWO::RandRange(0, Points.Num() - 1)] : nullptr;
}

void FWWOWaveRules::SpawnEnemy(FName Kind)
{
	const AActor* Point = PickPoint(Kind);
	if (!Point) return;
	FTransform Where;
	AWWOEnemy* Enemy = WWOEnemyKinds::SpawnDeferred(Mode.GetWorld(), Kind, Point->GetActorLocation(), Where);
	if (!Enemy) return;
	Enemy->bRespawns = false; // wave enemies don't respawn
	Enemy->FinishSpawning(Where);
	Enemy->bAlerted = true; // waves come looking for you
	Enemy->OnDied.AddLambda([](AWWOEnemy* Dead) { Dead->SetLifeSpan(CorpseCleanup); });
	Alive.Add(Enemy);
}

FString FWWOWaveRules::StatusText(FLinearColor& OutColor) const
{
	const int32 Total = FMath::RoundToInt(Mode.WavesToWin);
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
