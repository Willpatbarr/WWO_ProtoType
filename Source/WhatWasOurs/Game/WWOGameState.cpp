#include "Game/WWOGameState.h"
#include "FX/WWOFX.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

void AWWOGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWWOGameState, StatusLine);
	DOREPLIFETIME(AWWOGameState, StatusColor);
}

void AWWOGameState::Bolt(UWorld* World, const FVector& From, const FVector& To, const FLinearColor& Color, float Lifetime, AActor* FromActor, AActor* ToActor)
{
	if (!World) return;
	AWWOLightningBolt::Spawn(World, From, To, Color, Lifetime, FromActor, ToActor);
	if (World->GetNetMode() == NM_ListenServer)
	{
		if (AWWOGameState* GS = World->GetGameState<AWWOGameState>()) GS->MulticastBolt(From, To, Color, Lifetime, FromActor, ToActor);
	}
}

void AWWOGameState::Blast(UWorld* World, const FWWOGrenadeLook& Look, const FVector& Center, float RadiusM, AActor* Ignore)
{
	if (!World) return;
	AWWOGrenade::SpawnBlast(World, Look, Center, RadiusM, Ignore);
	if (World->GetNetMode() == NM_ListenServer)
	{
		if (AWWOGameState* GS = World->GetGameState<AWWOGameState>()) GS->MulticastBlast(Look, Center, RadiusM);
	}
}

int32 AWWOGameState::BlastsSeen = 0;

void AWWOGameState::MulticastBlast_Implementation(const FWWOGrenadeLook& Look, FVector_NetQuantize Center, float RadiusM)
{
	if (HasAuthority()) return; // the host drew it already
	++BlastsSeen;
	AWWOGrenade::SpawnBlast(GetWorld(), Look, Center, RadiusM);
}

void AWWOGameState::Discharge(UWorld* World, AActor* Target, const FVector& Impact, const FVector& Away, const FLinearColor& Color,
	int32 Arcs, float RadiusM, float Lifetime, AActor* Skip)
{
	if (!World) return;
	AWWODischarge::Spawn(World, Target, Impact, Away, Color, Arcs, RadiusM, Lifetime);
	if (World->GetNetMode() == NM_ListenServer)
	{
		if (AWWOGameState* GS = World->GetGameState<AWWOGameState>()) GS->MulticastDischarge(Target, Impact, Away, Color, Arcs, RadiusM, Lifetime, Skip);
	}
}

void AWWOGameState::MulticastBolt_Implementation(FVector_NetQuantize From, FVector_NetQuantize To, FLinearColor Color, float Lifetime, AActor* FromActor, AActor* ToActor)
{
	if (HasAuthority()) return; // the host drew it already
	AWWOLightningBolt::Spawn(GetWorld(), From, To, Color, Lifetime, FromActor, ToActor);
}

void AWWOGameState::MulticastDischarge_Implementation(AActor* Target, FVector_NetQuantize Impact, FVector_NetQuantizeNormal Away, FLinearColor Color,
	int32 Arcs, float RadiusM, float Lifetime, AActor* Skip)
{
	if (HasAuthority()) return;
	const APawn* SkipPawn = Cast<APawn>(Skip);
	if (SkipPawn && SkipPawn->IsLocallyControlled()) return; // this machine predicted it
	if (!Target) return;
	AWWODischarge::Spawn(GetWorld(), Target, Impact, Away, Color, Arcs, RadiusM, Lifetime);
}
