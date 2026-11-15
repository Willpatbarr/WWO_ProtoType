#include "WWOTypes.h"
#include "GameFramework/Actor.h"

bool WWO::IsOnline(const AActor* Actor)
{
	return Actor && Actor->GetNetMode() != NM_Standalone;
}
