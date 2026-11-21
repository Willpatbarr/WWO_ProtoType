#pragma once

// What one player has equipped: a flora and its grenade, abilities and traits, a weapon and each frame's
// mods, the frame parts. Owned by UWWOLoadoutSubsystem (Equipped), which saves it, sends it and tells
// listeners it changed; these functions only change the state. Options come from the catalog.

#include "CoreMinimal.h"
#include "Data/WWOData.h"
#include "Systems/WWOLoadoutTypes.h"
#include "WWOLoadoutEquipped.generated.h"

struct FWWOLoadoutCatalog;

USTRUCT()
struct FWWOEquipped
{
	GENERATED_BODY()

	static constexpr int32 TraitSlots = 4; // placeholder until decided
	static constexpr int32 AbilitySlots = 2;
	static const TArray<FName>& FrameSlots(); // head, arms, legs, core

	UPROPERTY() TObjectPtr<UWWOWeaponData> Weapon;
	UPROPERTY() TObjectPtr<UWWOGrenadeData> Grenade;
	UPROPERTY() TObjectPtr<UWWOFloraData> Flora;
	UPROPERTY() TArray<TObjectPtr<UWWOAbilityData>> Abilities; // AbilitySlots long once set up
	UPROPERTY() TArray<TObjectPtr<UWWOTraitData>> Traits;
	UPROPERTY() TMap<FName, TObjectPtr<UWWOFramePartData>> Frame;
	// Mods per frame (weapon id), each slot -> mod, so every frame keeps its own setup. (The mods are the
	// catalog's, kept alive there.)
	TMap<FName, TMap<FName, TObjectPtr<UWWOModData>>> Mods;
	// The shell parts' colours: per slot (head, core, arms, legs: UWWOBodyComponent::EShellSlot's order) a primary,
	// secondary and glow colour (sRGB), ShellSlots x ShellRoles long once any is set. Alpha 0 is "default": that part keeps
	// its own material's colour (so an empty array is a robot exactly as imported).
	static constexpr int32 ShellSlots = 4, ShellRoles = 3;
	enum EShellRole { ShellPrimary, ShellSecondary, ShellGlow };
	UPROPERTY() TArray<FColor> ShellColors;
	// Every flora but the equipped one: its picks, waiting to be put back (SwitchFlora). Only ever saved: never
	// sent, and nothing reads it for stats or kit, so a stored setup grants nothing.
	TMap<FName, FWWOFloraSetup> FloraSetups;

	// --- Reading ---
	UWWOAbilityData* GetAbility(int32 Slot) const { return Abilities.IsValidIndex(Slot) ? Abilities[Slot].Get() : nullptr; }
	int32 AbilitySlotOf(const UWWOAbilityData* Choice) const;
	UWWOFramePartData* GetFramePart(FName Slot) const;
	TArray<UWWOTraitData*> TraitList() const;
	UWWOModData* GetMod(FName Slot) const;  // on the equipped weapon, or null
	bool HasPerk(FName Perk) const;         // a mod with this perk is on the equipped weapon
	TMap<FName, FName> ModIds(FName WeaponId) const;
	FColor GetShellColor(int32 Slot, int32 Role) const; // alpha 0: the part's own
	bool AnyShellColor() const;

	// --- Changing (the subsystem announces the change) ---
	void SwitchFlora(UWWOFloraData* Choice, const FWWOLoadoutCatalog& Catalog); // keeps this flora's picks, puts on Choice's (first time: its first grenade and abilities, no traits)
	void RestoreFloraSetup(const FWWOLoadoutCatalog& Catalog); // the equipped flora's stored picks, if any, minus any no longer valid
	void SetAbility(int32 Slot, UWWOAbilityData* Choice); // already in the other slot: the two swap
	bool ToggleTrait(UWWOTraitData* Choice);              // false: refused (slots full, another flora's)
	bool SetMod(FName Slot, UWWOModData* Mod);            // null empties the slot; false: refused (no weapon, wrong frame or slot)
	void SetModIds(FName WeaponId, const TMap<FName, FName>& Ids, const FWWOLoadoutCatalog& Catalog);
	bool SetShellColor(int32 Slot, int32 Role, FColor Color); // Slot INDEX_NONE: every slot; alpha 0: default. False: nothing changed
	void SetShellColors(const TArray<FColor>& Colors);       // from a save or the wire (only the first ShellSlots x ShellRoles)
	UWWOWeaponData* CycledWeapon(int32 Step, const FWWOLoadoutCatalog& Catalog) const; // the unlocked weapon Step away, or null (fewer than two)
	void FitFlora();                                       // drops a grenade, ability or trait that isn't this flora's
	void FillFloraSlots(const FWWOLoadoutCatalog& Catalog); // an empty grenade or ability slot gets the flora's first free one

	// --- The saved file and the wire ---
	void ToSave(UWWOLoadoutSave& Data) const;  // everything but the configs
	void FromSave(const UWWOLoadoutSave& Data, const FWWOLoadoutCatalog& Catalog); // adds to what is equipped
	FWWONetLoadout ToNet() const;
	void Reset(); // nothing equipped (AbilitySlots empty slots)
};
