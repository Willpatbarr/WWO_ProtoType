#pragma once

// The flora abilities, in one place: one FWWOAbility per ability id (WWOIds::Ability) in
// WWOAbility::Table() (Player/WWOAbilities.cpp), and what each remembers in one FWWOAbilityState.
// UWWOAbilityComponent runs them: the recharge and arming go by kind (EWWOAbilityKind); everything an
// ability does of its own is a field of its entry. Adding one: docs/RECIPES.md, "An ability or a grenade".

#include "CoreMinimal.h"
#include "Misc/Optional.h"

class UWWOAbilityComponent;
class UWWOAbilityData;

// What an ability remembers on its owner (UWWOAbilityComponent::State). Kept across loadout changes,
// emptied with the rest of the kit by the regression runner.
struct FWWOAbilityState
{
	TOptional<float> Charge; // 0..1: Active, the recharge; Charged, the fill. Unset: WWOAbility::StartCharge
	float ActiveLeft = 0.f;  // Active: seconds of effect left
	bool bArmed = false;     // Charged: full and waiting for a melee hit
	float LitLeft = 0.f;     // Melee Guard: seconds the guard is still up
	int32 Live = 0;          // Seedburst: its grenades and seeds out; Spore Patches: patches up (this machine's player only)
};

// One ability. A field left empty is something it doesn't do. "Equipped" below means in a slot of its
// owner's loadout; where the game asks every ability (an effect, a hit taken), the table is walked in
// its order and each entry answers for its own asset.
struct FWWOAbility
{
	FName Id;

	// Active: just used, its charge spent and its time started (also on the host for a joiner's use).
	void (*OnActivate)(UWWOAbilityComponent& K, const UWWOAbilityData& A) = nullptr;
	// Every update, equipped or not (a timer that runs down on its own).
	void (*Tick)(FWWOAbilityState& S, float DeltaSeconds) = nullptr;
	// Triggered: lit (the HUD box) while this is true.
	bool (*Lit)(const FWWOAbilityState& S) = nullptr;
	// The fists glow in its colour while its effect is on (the first such entry that is on wins).
	bool bFistsGlow = false;

	// Equipped: what it does to its owner. Missing: no change.
	float (*MoveSpeed)(const UWWOAbilityData& A) = nullptr;                                 // times the move speed
	int32 (*ExtraJumps)(const UWWOAbilityData& A) = nullptr;                                // more air jumps
	float (*GrenadeCharges)(const UWWOAbilityData& A) = nullptr;                            // more grenade charges
	float (*MeleeDealt)(const UWWOAbilityData& A, const FWWOAbilityState& S) = nullptr;     // times its melee damage
	float (*MeleeTaken)(const UWWOAbilityData& A, const FWWOAbilityState& S) = nullptr;     // times the melee damage it takes
	// Equipped: things that happen to its owner.
	void (*OnMeleeLanded)(const UWWOAbilityData& A, FWWOAbilityState& S) = nullptr;
	void (*OnDamageTaken)(const UWWOAbilityData& A, FWWOAbilityState& S, float Amount) = nullptr;
};

namespace WWOAbility
{
	TConstArrayView<FWWOAbility> Table();
	const FWWOAbility* Find(FName Id); // the entry for an ability id, or null
	// Charge before it was ever used or filled: Charged abilities start empty, the rest ready.
	float StartCharge(const UWWOAbilityData& A);
	float ChargeOf(const UWWOAbilityData& A, const FWWOAbilityState* S);
}
