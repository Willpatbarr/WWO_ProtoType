#include "WWOTypes.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

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
