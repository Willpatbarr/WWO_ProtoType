#include "Game/WWOPlayerState.h"
#include "Net/UnrealNetwork.h"

void AWWOPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWWOPlayerState, Team);
	DOREPLIFETIME(AWWOPlayerState, Kills);
	DOREPLIFETIME(AWWOPlayerState, PrecisionKills);
	DOREPLIFETIME(AWWOPlayerState, Deaths);
}
