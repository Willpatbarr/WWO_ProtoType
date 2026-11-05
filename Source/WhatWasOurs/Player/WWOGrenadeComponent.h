#pragma once

// Throws the equipped grenade (L1 / G) and handles its recharge (Godot: grenade_thrower.gd).
// Throw distance, recharge and fuse are global base values; each grenade scales them.
// Charge counts grenades: 1 = one ready; Extra Grenade raises the maximum.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WWOGrenadeComponent.generated.h"

class UWWOGrenadeData;

UCLASS(ClassGroup = (WWO))
class WHATWASOURS_API UWWOGrenadeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWWOGrenadeComponent();
	void UpdateGrenades(float DeltaSeconds); // called by the character

	UPROPERTY(EditAnywhere, Category = "Grenades") float ThrowDistance = 18.f; // meters on flat ground when you look level
	UPROPERTY(EditAnywhere, Category = "Grenades") float RechargeTime = 8.f;
	UPROPERTY(EditAnywhere, Category = "Grenades") float DetonationTime = 1.f;

	static constexpr float LaunchAngle = 7.f; // degrees above where you're looking: a fast, flat throw

	float Charge = 1.f;

	float EffectiveThrowDistance(const UWWOGrenadeData* Grenade = nullptr) const;
	float EffectiveRechargeTime(const UWWOGrenadeData* Grenade = nullptr) const;
	float EffectiveFuse(const UWWOGrenadeData* Grenade = nullptr) const;
	float MaxCharges() const;
	bool IsReady() const { return Charge >= 1.f; }
	bool ThrowGrenade();

private:
	float ReleaseLeft = -1.f; // seconds until the grenade leaves the hand
	void Release();
	const UWWOGrenadeData* Current(const UWWOGrenadeData* Grenade) const;
};
