#pragma once

// Anything shootable (target dummies, enemies and, in PvP, other players). Replaces Godot's "targets" group and
// the shared hit / aim_parts / is_critical_shape duck typing.

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "WWOTypes.h"
#include "WWOTargetable.generated.h"

UINTERFACE(MinimalAPI, NotBlueprintable)
class UWWOTargetable : public UInterface
{
	GENERATED_BODY()
};

class WHATWASOURS_API IWWOTargetable
{
	GENERATED_BODY()

public:
	// Crit spot first, then the body (the last part is always the body).
	virtual TArray<FWWOAimPart> GetAimParts() const = 0;
	virtual bool IsCriticalComponent(const UPrimitiveComponent* Component) const = 0;
	// Whether a shot that landed as Hit, fired from From, counts as a crit. By default that's the
	// part it met first; players also count a shot whose line goes on through the head.
	virtual bool IsCriticalHit(const FHitResult& Hit, const FVector& From) const { return IsCriticalComponent(Hit.GetComponent()); }
	// Hurts it: ApplyHit unless it is already dead, then the attacker's hit marker
	// (UWWOWorldSubsystem::ConfirmHit). Returns true if this hit killed it. Color tints the damage
	// number (alpha 0 = default).
	bool TakeHit(float Damage, bool bCritical, const FVector& Point, FLinearColor Color = FLinearColor(0, 0, 0, 0));
	// What TakeHit does to this one. Call TakeHit, not this.
	virtual bool ApplyHit(float Damage, bool bCritical, const FVector& Point, FLinearColor Color) = 0;
	virtual bool IsDead() const = 0;
	virtual float GetHealth() const = 0;
	virtual float GetMaxHealth() const = 0;
	virtual float GetShield() const { return 0.f; }
	virtual float GetMaxShield() const { return 0.f; }
	virtual float GetBodyHeight() const { return 225.f; } // cm, where the HUD health bar goes
	virtual FVector FeetLocation() const = 0;          // Godot's origin for targets and enemies
	virtual void KnockBack(const FVector& Push) {} // cm/s
	virtual void ResetTarget() {}

	// The body's center (lightning and poison aim here).
	FVector BodyCenter() const
	{
		const TArray<FWWOAimPart> Parts = GetAimParts();
		for (const FWWOAimPart& Part : Parts)
		{
			if (!Part.bCritical) return Part.Position;
		}
		return Parts.Num() > 0 ? Parts.Last().Position : FVector::ZeroVector;
	}
};
