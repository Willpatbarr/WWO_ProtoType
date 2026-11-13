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
	void UpdateCharge(float DeltaSeconds); // the recharge (the host runs a joiner's too, to check its throws)
	bool HostThrow(const FVector& Hand, const FVector& Dir); // host: a joiner's throw (see AWWOCharacter::ServerThrowGrenade)
	// the host's tally of joiners' throws
	static int32 NetThrows;
	static int32 NetThrowsRejected;

private:
	float ReleaseLeft = -1.f; // seconds until the grenade leaves the hand
public:
	void CancelRelease() { ReleaseLeft = -1.f; } // its owner died with one wound up
private:
	void Release();
	void ThrowFrom(const FVector& Hand, const FVector& Dir); // build the grenade from the loadout and let go
	const UWWOGrenadeData* Current(const UWWOGrenadeData* Grenade) const;
};
