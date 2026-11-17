#include "Enemies/WWOEnemyKinds.h"
#include "Enemies/WWOEnemyTypes.h"
#include "Data/WWOData.h"
#include "WWOIds.h"

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
