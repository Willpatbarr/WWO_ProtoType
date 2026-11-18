#pragma once

// Runs the two equipped flora abilities (Godot: ability_manager.gd). What each one does is its entry in
// WWOAbility::Table() (Player/WWOAbilities.h), what it remembers its FWWOAbilityState here.
// VoltVine: Overgrowth (passive speed + extra jump), Thornfist (active melee boost),
//   Melee Guard (triggered: landing a melee cuts incoming melee damage), Static Charge
//   (charged by damage taken; arms, then the next melee hit chains lightning).
// VeilSpore: Spore Fog Damage (passive), Spore Dash (active), Spore Patches (triggered).
// IronBark: Root Bombs (active), Shield Share (passive), Extra Grenade (passive).
// Seedburst (triggered) belongs to no flora.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Player/WWOKitRefusal.h"
#include "Player/WWOAbilities.h"
#include "WWOAbilityComponent.generated.h"

class UWWOAbilityData;

UCLASS(ClassGroup = (WWO))
class WHATWASOURS_API UWWOAbilityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWWOAbilityComponent();
	void UpdateAbilities(float DeltaSeconds); // called by the character

	bool ActivateAbility(UWWOAbilityData* Ability, float Ready = 1.f);
	// Host: a joiner used the ability in this slot (its own machine already did; this runs the
	// world side: fog, bombs, Thornfist's boost and an armed Static Charge for its punches).
	EWWOKitRefusal HostUseAbility(int32 Slot); // None: used; otherwise refused (the joiner is told)
	void UndoUse(int32 Slot);
	bool CanLaunchNow() const;       // host: Spore Dash is equipped and ready, or was just used        // a joiner's own, when the host refused it
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
	void FillCharges() { for (TPair<FName, FWWOAbilityState>& S : States) if (S.Value.Charge.IsSet()) S.Value.Charge = 1.f; } // everything ready again (-WWOBotsBusy)
	UWWOAbilityData* ThornfistActive() const;
	float MeleeMultiplier() const;

	// What an ability remembers, by id (made on first use).
	FWWOAbilityState& State(FName Id) { return States.FindOrAdd(Id); }
	const FWWOAbilityState* FindState(FName Id) const { return States.Find(Id); }
	// Seedburst's grenades and seeds out, Spore Patches' patches up (keep their HUD box lit).
	int32& LiveCount(FName Id) { return State(Id).Live; }
	float GuardTimeLeft() const; // for the HUD's status chip

private:
	TMap<FName, FWWOAbilityState> States;
	TWeakObjectPtr<const UWWOAbilityData> FistsLit; // the ability the fists are glowing for

	bool PlayerBusy() const;

	// The regression scenarios (-WWORegress, Player/WWORegress.h) read and reset private state between runs; no gameplay code uses this.
	friend class FWWORegress;
};
