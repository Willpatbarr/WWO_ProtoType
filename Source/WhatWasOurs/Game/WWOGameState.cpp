#include "Game/WWOGameState.h"
#include "Net/UnrealNetwork.h"

void AWWOGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWWOGameState, StatusLine);
	DOREPLIFETIME(AWWOGameState, StatusColor);
}
