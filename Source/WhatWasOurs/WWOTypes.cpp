#include "WWOTypes.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

namespace
{
	bool GGameStream = false;   // set only by the regression scenarios
	FRandomStream GGameRandom;
}

void WWO::SeedGameRandom(int32 Seed)
{
	GGameStream = true;
	GGameRandom.Initialize(Seed);
}

float WWO::FRand() { return GGameStream ? GGameRandom.FRand() : FMath::FRand(); }
float WWO::FRandRange(float Min, float Max) { return GGameStream ? GGameRandom.FRandRange(Min, Max) : FMath::FRandRange(Min, Max); }
int32 WWO::RandRange(int32 Min, int32 Max) { return GGameStream ? GGameRandom.RandRange(Min, Max) : FMath::RandRange(Min, Max); }

bool WWO::IsOnline(const AActor* Actor)
{
	return Actor && Actor->GetNetMode() != NM_Standalone;
}


FActorSpawnParameters WWO::AlwaysSpawn(AActor* Owner)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = Owner;
	return Params;
}

FActorSpawnParameters WWO::AlwaysSpawnDeferred(AActor* Owner)
{
	FActorSpawnParameters Params = AlwaysSpawn(Owner);
	Params.bDeferConstruction = true;
	return Params;
}
