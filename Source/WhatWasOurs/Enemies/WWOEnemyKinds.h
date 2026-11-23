#pragma once

// The enemy registry: one entry per enemy kind, looked up by id (WWOIds::Enemy). The waves name kinds by
// id (AWWOGameMode's wave list), a spawn point's tags name the kinds it allows, the loadout subsystem
// keeps one tuning asset per kind (UWWOLoadoutSubsystem::EnemyData), and the wave spawn's rules for a kind
// are fields here. Adding an enemy type: its class and data asset class, then one entry in All().

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"

class AWWOEnemy;
class UWWOEnemyData;
class UWorld;

struct FWWOEnemyKind
{
	FName Id;                             // WWOIds::Enemy::...
	TSubclassOf<AWWOEnemy> Class;
	TSubclassOf<UWWOEnemyData> DataClass; // its shared tuning (one asset of this class in Content/WWO/Data)
	float SpawnScatter = 150.f;           // cm: a wave spawn lands up to this far from its marker on each axis (0: right on it)
	float WaveTestFacing = 30.f;          // degrees off its target the wave test (-WWOWaveTest) lets it be before counting it as not facing
};

namespace WWOEnemyKinds
{
	// Every kind, in this order (the order the loadout subsystem fills its data in).
	const TArray<FWWOEnemyKind>& All();
	// The entry for this id, or null.
	const FWWOEnemyKind* Find(FName Id);
	// The entry for this id; an id no entry has is a Swarm (as the wave spawn always treated one).
	const FWWOEnemyKind& FindOrSwarm(FName Id);
	// The entry whose class this enemy is, or null.
	const FWWOEnemyKind* Of(const AWWOEnemy* Enemy);
	// Spawns one of this kind at a spawn marker, deferred (set its fields, then FinishSpawning(OutWhere)):
	// within the kind's SpawnScatter of the marker, its feet on the marker's height, on the navmesh and
	// clear of anything solid (8 tries, then the nearest navmesh spot). Null if it couldn't be spawned.
	// The waves' placement (FWWOWaveRules::SpawnEnemy), shared with FootRace's rooms. ScatterCm >= 0
	// replaces the kind's SpawnScatter (FootRace's wall markers: a tight one, so it comes out at the wall),
	// and FacingYaw (degrees, Unreal yaw) replaces the random facing; the defaults draw exactly as the
	// waves always have.
	AWWOEnemy* SpawnDeferred(UWorld* World, FName Kind, const FVector& Marker, FTransform& OutWhere, float ScatterCm = -1.f,
		TOptional<float> FacingYaw = {});
}
