// Regression scenarios: the wave path. Each enemy kind spawned through the game mode's own SpawnEnemy at
// a spawn point put in the test space (its class, data asset, where the offset draws put it, its health
// and shield), a wave's queue as StartWave builds and shuffles it, and one small wave spawned the way the
// game mode's Tick spawns it (spawn point choice included). The AI stays off; the game mode's wave state
// is put back at the end of each scenario, within the tick, so its own Tick never sees a wave.

#include "Player/WWORegress.h"
#include "Game/WWOGameMode.h"
#include "Game/WWOWaveRules.h"
#include "Enemies/WWOEnemy.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "Data/WWOData.h"
#include "Engine/TargetPoint.h"
#include "NavigationSystem.h"
#include "Engine/World.h"

namespace
{
	bool NavBusy(UWorld* World)
	{
		UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		return Nav && Nav->IsNavigationBuildInProgress();
	}

	// The spawn points a scenario put down (engine actors: the teardown only removes this game's, so the
	// scenario removes them itself at its end).
	TArray<TWeakObjectPtr<ATargetPoint>> GPoints;

	void RemovePoints()
	{
		for (const TWeakObjectPtr<ATargetPoint>& P : GPoints) if (P.IsValid()) P->Destroy();
		GPoints.Reset();
	}

	FString Joined(const TArray<FName>& Names)
	{
		return Names.Num() == 0 ? FString(TEXT("-")) : FString::JoinBy(Names, TEXT(","), [](const FName& N) { return N.ToString(); });
	}
}

void FWWORegress::AddWaveScenarios(TArray<FWWORegressScenario>& Out, const UWWOLoadoutSubsystem& L)
{
	// A spawn point in the test space, tagged EnemySpawn and Tags (removed by RemovePoints).
	auto AddPoint = [](FWWORegress& R, const FVector& At, const TArray<FName>& Tags)
	{
		ATargetPoint* Point = R.World()->SpawnActor<ATargetPoint>(ATargetPoint::StaticClass(), At, FRotator::ZeroRotator, WWO::AlwaysSpawn());
		if (!Point) { R.Record(TEXT("spawn_failed"), TEXT("point")); return; }
		Point->Tags.Add("EnemySpawn");
		Point->Tags.Append(Tags);
		GPoints.Add(Point);
	};
	// The AI off for every kind (wave enemies come alerted: they would walk at once).
	auto Inert = [](FWWORegress& R)
	{
		for (const auto& Pair : R.Loadout()->Catalog.EnemyData)
		{
			UWWOEnemyData* D = Pair.Value;
			if (D && D->AiEnabled >= 0.5f) R.Tune(D, "AiEnabled", 0.f);
		}
	};
	// Every enemy the game mode added to its Alive list since the scenario began, recorded once each.
	auto RecordEnemies = [](FWWORegress& R, FWWOWaveRules* W, int32 From)
	{
		for (int32 i = From; i < W->Alive.Num(); ++i)
		{
			const AWWOEnemy* E = W->Alive[i].Get();
			const FString K = FString::Printf(TEXT("e%d"), i - From);
			if (!E) { R.Record(K, TEXT("gone")); continue; }
			const UWWOEnemyData* D = E->Data();
			R.Record(K + TEXT(".class"), E->GetClass()->GetName());
			R.Record(K + TEXT(".data"), D ? D->GetClass()->GetName() + TEXT(":") + D->GetName() : FString(TEXT("none")));
			R.RecordVec(K + TEXT(".at"), E->GetActorLocation());
			R.Record(K + TEXT(".yaw"), FString::Printf(TEXT("%.4f"), E->GetActorRotation().Yaw));
			R.Record(K + TEXT(".health"), E->GetHealth());
			R.Record(K + TEXT(".max_health"), E->GetMaxHealth());
			R.Record(K + TEXT(".shield"), E->GetShield());
			R.Record(K + TEXT(".max_shield"), E->GetMaxShield());
			R.RecordBool(K + TEXT(".respawns"), E->bRespawns);
			R.RecordBool(K + TEXT(".alerted"), E->bAlerted);
		}
	};

	// --- Each kind, three times at one spawn point 20 m ahead (the later ones find the first in the way) ---
	for (const FName Kind : { FName("soldier"), FName("swarm"), FName("tank"), FName("unknown") })
	{
		FWWORegressScenario S;
		S.Name = FString::Printf(TEXT("wave.spawn.%s"), *Kind.ToString());
		S.MaxTicks = 700;
		S.Setup = [Kind, AddPoint, Inert](FWWORegress& R)
		{
			Inert(R);
			AddPoint(R, R.Ahead(20.f), { Kind });
		};
		S.Step = [Kind, RecordEnemies](FWWORegress& R, int32 Tick)
		{
			if (NavBusy(R.World()) && Tick < 600) return false; // the spawn checks the navmesh
			AWWOGameMode* GM = R.World()->GetAuthGameMode<AWWOGameMode>();
			FWWOWaveRules* W = GM ? GM->Waves() : nullptr;
			if (!W) { R.Record(TEXT("no_game_mode"), 1); RemovePoints(); return true; }
			const int32 From = W->Alive.Num();
			R.Record(TEXT("points"), GM->SpawnPoints().Num());
			for (int32 i = 0; i < 3; ++i) W->SpawnEnemy(Kind);
			RecordEnemies(R, W, From);
			R.Record(TEXT("spawned"), W->Alive.Num() - From);
			R.Record(TEXT("rand_next"), WWO::FRand());
			W->Alive.SetNum(From);
			RemovePoints();
			return true;
		};
		Out.Add(S);
	}

	// --- The queue StartWave builds for waves 1 to 7 (6 and 7: past the list, 25% bigger each) ---
	for (int32 Number = 1; Number <= 7; ++Number)
	{
		FWWORegressScenario S;
		S.Name = FString::Printf(TEXT("wave.queue.%d"), Number);
		S.Step = [Number](FWWORegress& R, int32 Tick)
		{
			AWWOGameMode* GM = R.World()->GetAuthGameMode<AWWOGameMode>();
			FWWOWaveRules* W = GM ? GM->Waves() : nullptr;
			if (!W) { R.Record(TEXT("no_game_mode"), 1); RemovePoints(); return true; }
			const EWWOWaveState StateBefore = W->State;
			const int32 WaveBefore = W->Wave;
			const float TimerBefore = W->Timer;
			const TArray<FName> QueueBefore = W->Queue;
			R.Record(TEXT("state_before"), (int32)StateBefore);
			R.Record(TEXT("size_scale"), GM->SizeScale);
			W->Wave = Number - 1;
			W->StartWave();
			R.Record(TEXT("wave"), W->Wave);
			R.Record(TEXT("state"), (int32)W->State);
			R.Record(TEXT("timer"), W->Timer);
			R.Record(TEXT("count"), W->Queue.Num());
			R.Record(TEXT("queue"), Joined(W->Queue));
			R.Record(TEXT("rand_next"), WWO::FRand());
			W->State = StateBefore;
			W->Wave = WaveBefore;
			W->Timer = TimerBefore;
			W->Queue = QueueBefore;
			return true;
		};
		Out.Add(S);
	}

	// --- Wave 2 spawned as the game mode's Tick spawns it: three points, one too near the player, one for soldiers only ---
	{
		FWWORegressScenario S;
		S.Name = TEXT("wave.run.2");
		S.MaxTicks = 700;
		S.Setup = [AddPoint, Inert](FWWORegress& R)
		{
			Inert(R);
			AddPoint(R, R.Ahead(20.f, -6.f), {});
			AddPoint(R, R.Ahead(22.f, 6.f), { FName("soldier") });
			AddPoint(R, R.Ahead(5.f), {});
		};
		S.Step = [RecordEnemies](FWWORegress& R, int32 Tick)
		{
			if (NavBusy(R.World()) && Tick < 600) return false;
			AWWOGameMode* GM = R.World()->GetAuthGameMode<AWWOGameMode>();
			FWWOWaveRules* W = GM ? GM->Waves() : nullptr;
			if (!W) { R.Record(TEXT("no_game_mode"), 1); RemovePoints(); return true; }
			const EWWOWaveState StateBefore = W->State;
			const int32 WaveBefore = W->Wave;
			const float TimerBefore = W->Timer;
			const TArray<FName> QueueBefore = W->Queue;
			const int32 From = W->Alive.Num();
			W->Wave = 1;
			W->StartWave();
			R.Record(TEXT("queue"), Joined(W->Queue));
			while (W->Queue.Num() > 0)
			{
				W->SpawnEnemy(W->Queue[0]);
				W->Queue.RemoveAt(0);
			}
			RecordEnemies(R, W, From);
			R.Record(TEXT("spawned"), W->Alive.Num() - From);
			R.Record(TEXT("rand_next"), WWO::FRand());
			W->Alive.SetNum(From);
			W->State = StateBefore;
			W->Wave = WaveBefore;
			W->Timer = TimerBefore;
			W->Queue = QueueBefore;
			RemovePoints();
			return true;
		};
		Out.Add(S);
	}
}
