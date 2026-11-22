#include "Systems/WWOTargetable.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Player/WWOCharacter.h"
#include "Flora/WWODawnLotus.h"
#include "GameFramework/Actor.h"

bool IWWOTargetable::TakeHit(float Damage, bool bCritical, const FVector& Point, FLinearColor Color)
{
	if (IsDead()) return false;
	// What this hit really took, for whoever is credited with it (Petalfall counts it; the host's word only).
	AWWOCharacter* By = Cast<AWWOCharacter>(UWWOWorldSubsystem::CurrentAttacker.Get());
	if (By && (!By->HasAuthority() || By == Cast<AActor>(this))) By = nullptr;
	const float Before = By ? GetHealth() + GetShield() : 0.f;
	const bool bKilled = ApplyHit(Damage, bCritical, Point, Color);
	if (By) By->DawnLotus->OnDamageDealt(Before - (GetHealth() + GetShield()));
	UWWOWorldSubsystem::ConfirmHit(Cast<AActor>(this), bCritical, bKilled);
	return bKilled;
}
