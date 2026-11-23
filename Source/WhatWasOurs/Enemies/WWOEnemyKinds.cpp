#include "Enemies/WWOEnemyKinds.h"
#include "Enemies/WWOEnemyTypes.h"
#include "Data/WWOData.h"
#include "WWOIds.h"
#include "WWOTypes.h"
#include "Components/CapsuleComponent.h"
#include "NavigationSystem.h"
#include "Engine/World.h"

const TArray<FWWOEnemyKind>& WWOEnemyKinds::All()
{
	// Built on first use (StaticClass needs the classes registered).
	static const TArray<FWWOEnemyKind> Kinds = []
	{
		TArray<FWWOEnemyKind> K;
		K.Add({ WWOIds::Enemy::Soldier, AWWOSoldier::StaticClass(), UWWOSoldierData::StaticClass() });
		K.Add({ WWOIds::Enemy::Swarm, AWWOSwarm::StaticClass(), UWWOSwarmData::StaticClass() });
		// The Tank drops right on its marker; it turns slowly on purpose and plants itself to slam.
		K.Add({ WWOIds::Enemy::Tank, AWWOTank::StaticClass(), UWWOTankData::StaticClass(), 0.f, 60.f });
		return K;
	}();
	return Kinds;
}

const FWWOEnemyKind* WWOEnemyKinds::Find(FName Id)
{
	return All().FindByPredicate([Id](const FWWOEnemyKind& K) { return K.Id == Id; });
}

const FWWOEnemyKind& WWOEnemyKinds::FindOrSwarm(FName Id)
{
	const FWWOEnemyKind* Kind = Find(Id);
	return Kind ? *Kind : *Find(WWOIds::Enemy::Swarm);
}

const FWWOEnemyKind* WWOEnemyKinds::Of(const AWWOEnemy* Enemy)
{
	return Enemy ? All().FindByPredicate([Enemy](const FWWOEnemyKind& K) { return Enemy->IsA(K.Class); }) : nullptr;
}

AWWOEnemy* WWOEnemyKinds::SpawnDeferred(UWorld* World, FName Kind, const FVector& Marker, FTransform& OutWhere, float ScatterCm,
	TOptional<float> FacingYaw)
{
	// Moved from FWWOWaveRules::SpawnEnemy as it was (the random draws in the same order).
	const FWWOEnemyKind& Entry = FindOrSwarm(Kind);
	UClass* Class = Entry.Class;
	// Godot drops the enemy within 1.5 m of the marker (the Tank right on it: the kind's SpawnScatter) with its feet on
	// the marker, wherever that lands. Here the actor is its capsule's center, and a spot inside a
	// crate stack or off the navmesh is tried again (up to 8 times), so nobody starts boxed in.
	const UCapsuleComponent* Capsule = Class->GetDefaultObject<AWWOEnemy>()->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	FCollisionQueryParams Overlap(SCENE_QUERY_STAT(WWOWaveSpawn), false);
	FVector Feet = Marker;
	for (int32 Try = 0; Try < 8; ++Try)
	{
		const float Scatter = ScatterCm >= 0.f ? ScatterCm : Entry.SpawnScatter;
		const FVector Offset = Scatter > 0.f ? FVector(WWO::FRandRange(-Scatter, Scatter), WWO::FRandRange(-Scatter, Scatter), 0.f) : FVector::ZeroVector;
		const FVector Spot = Marker + Offset;
		FNavLocation OnMesh;
		const bool bOnMesh = !Nav || Nav->ProjectPointToNavigation(Spot, OnMesh, FVector(Radius, Radius, 100.f));
		const bool bClear = !World->OverlapBlockingTestByChannel(Spot + FVector(0, 0, HalfHeight), FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(Radius, HalfHeight - 2.f), Overlap);
		if (bOnMesh && bClear)
		{
			Feet = Spot;
			break;
		}
		FNavLocation Near;
		if (Try == 7 && Nav && Nav->ProjectPointToNavigation(Marker, Near, FVector(300.f, 300.f, 100.f)))
		{
			Feet = FVector(Near.Location.X, Near.Location.Y, Marker.Z); // nothing clear nearby: the closest navmesh spot
		}
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Params.bDeferConstruction = true;
	const float Yaw = FacingYaw.IsSet() ? FacingYaw.GetValue() : WWO::FRandRange(0.f, 360.f);
	OutWhere = FTransform(FRotator(0.f, Yaw, 0.f), Feet + FVector(0, 0, HalfHeight));
	return World->SpawnActor<AWWOEnemy>(Class, OutWhere, Params);
}
