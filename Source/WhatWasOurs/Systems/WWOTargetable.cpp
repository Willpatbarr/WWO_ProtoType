#include "Systems/WWOTargetable.h"
#include "Systems/WWOWorldSubsystem.h"
#include "GameFramework/Actor.h"

bool IWWOTargetable::TakeHit(float Damage, bool bCritical, const FVector& Point, FLinearColor Color)
{
	if (IsDead()) return false;
	const bool bKilled = ApplyHit(Damage, bCritical, Point, Color);
	UWWOWorldSubsystem::ConfirmHit(Cast<AActor>(this), bCritical, bKilled);
	return bKilled;
}
