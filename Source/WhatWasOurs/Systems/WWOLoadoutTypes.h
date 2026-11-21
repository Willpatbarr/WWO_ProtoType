#pragma once

// The loadout's saved and sent shapes: the save file (Saved/SaveGames/Loadout.sav), a saved weapon config,
// and the loadout a joiner sends the host. Their class, struct and property names and types are the file
// and wire format: never rename, retype or reorder them.

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "WWOLoadoutTypes.generated.h"

// A saved weapon config (draft): a frame and the mod in each of its slots.
USTRUCT()
struct FWWOModConfig
{
	GENERATED_BODY()

	UPROPERTY() FString Name;
	UPROPERTY() FName Weapon;
	UPROPERTY() TMap<FName, FName> Mods; // slot -> mod id
};

// One flora's own picks, as ids, kept while another flora is equipped. Ids rather than assets so a pick
// the game no longer has stays in the file and is only dropped when that flora is put on.
USTRUCT()
struct FWWOFloraSetup
{
	GENERATED_BODY()

	UPROPERTY() FName Grenade;
	UPROPERTY() TArray<FName> Abilities; // by slot (None = empty)
	UPROPERTY() TArray<FName> Traits;
};

// A player's loadout as ids, for the host (online: a joiner sends it, the host keeps a copy of the
// loadout system for that player; see UWWOLoadoutSubsystem::Get). Maps are parallel arrays.
USTRUCT()
struct FWWONetLoadout
{
	GENERATED_BODY()

	UPROPERTY() FName Flora;
	UPROPERTY() FName Weapon;
	UPROPERTY() FName Grenade;
	UPROPERTY() TArray<FName> Abilities;
	UPROPERTY() TArray<FName> Traits;
	UPROPERTY() TArray<FName> FrameSlots;
	UPROPERTY() TArray<FName> FrameParts;
	UPROPERTY() TArray<FName> ModKeys; // "weapon:slot", the equipped weapon's
	UPROPERTY() TArray<FName> ModIds;
	// The shell parts' colours (FWWOEquipped::ShellColors): empty while every one is the parts' own.
	UPROPERTY() TArray<FColor> ShellColors;

	bool operator==(const FWWONetLoadout& O) const
	{
		return Flora == O.Flora && Weapon == O.Weapon && Grenade == O.Grenade && Abilities == O.Abilities && Traits == O.Traits
			&& FrameSlots == O.FrameSlots && FrameParts == O.FrameParts && ModKeys == O.ModKeys && ModIds == O.ModIds && ShellColors == O.ShellColors;
	}
	bool operator!=(const FWWONetLoadout& O) const { return !(*this == O); }
};

UCLASS()
class WHATWASOURS_API UWWOLoadoutSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY() FName Flora;
	UPROPERTY() FName Weapon;
	UPROPERTY() FName Grenade;
	UPROPERTY() TArray<FName> Abilities;
	UPROPERTY() TArray<FName> Traits;
	UPROPERTY() TMap<FName, FName> Frame;
	UPROPERTY() TMap<FName, FName> WeaponMods; // slot -> mod id
	UPROPERTY() TArray<FWWOModConfig> ModConfigs;
	UPROPERTY() TArray<int32> WeaponSlots; // the saved config in each of the two weapon slots (-1 = none)
	UPROPERTY() TArray<FColor> ShellColors; // FWWOEquipped::ShellColors; empty (and missing from older saves): the parts' own colours
	// The floras that are not equipped, by flora id. The equipped flora's picks stay in Grenade, Abilities and
	// Traits above, so a save from before this field is simply "that flora's setup, no others yet". Empty: not written.
	UPROPERTY() TMap<FName, FWWOFloraSetup> FloraSetups;
};
