#pragma once

// Runs the two equipped flora abilities (Godot: ability_manager.gd).
// VoltVine: Overgrowth (passive speed + extra jump), Thornfist (active melee boost),
//   Melee Guard (triggered: landing a melee cuts incoming melee damage), Static Charge
//   (charged by damage taken; arms, then the next melee hit chains lightning).
// VeilSpore: Spore Fog Damage (passive), Spore Dash (active), Spore Patches (triggered).
// IronBark: Root Bombs (active), Shield Share (passive), Extra Grenade (passive).
// Seedburst (triggered) belongs to no flora.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WWOAbilityComponent.generated.h"

class UWWOAbilityData;

UCLASS(ClassGroup = (WWO))
class WHATWASOURS_API UWWOAbilityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWWOAbilityComponent();
	void UpdateAbilities(float DeltaSeconds); // called by the character

	bool ActivateAbility(UWWOAbilityData* Ability);
	float Charge(const UWWOAbilityData* Ability) const;
	bool IsEffectActive(const UWWOAbilityData* Ability) const;
	UWWOAbilityData* Equipped(FName Id) const;

	// Effects read by the rest of the game.
	float SpeedMultiplier() const;
	int32 ExtraJumps() const;
	void OnMeleeLanded();
	void OnDamageTaken(float Amount);
	UWWOAbilityData* ConsumeStaticCharge();
	float IncomingMeleeMultiplier() const;
	float ExtraCharges() const;
	UWWOAbilityData* ThornfistActive() const;
	float MeleeMultiplier() const;

	// Live Seedburst grenades and seeds (keeps the Seedburst HUD box lit).
	int32 LiveSeeds = 0;
	int32 LivePatches = 0;

private:
	TMap<FName, float> ChargeById;     // activated abilities: 0..1 recharge
	TMap<FName, float> ActiveLeft;     // seconds of effect left
	TMap<FName, bool> Armed;           // charged abilities: armed and waiting for a melee hit
	float GuardLeft = 0.f;

	bool PlayerBusy() const;
	void SporeDash(UWWOAbilityData* Ability);
	void RootBombs(UWWOAbilityData* Ability);
};
