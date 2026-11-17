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

	bool operator==(const FWWONetLoadout& O) const
	{
		return Flora == O.Flora && Weapon == O.Weapon && Grenade == O.Grenade && Abilities == O.Abilities && Traits == O.Traits
			&& FrameSlots == O.FrameSlots && FrameParts == O.FrameParts && ModKeys == O.ModKeys && ModIds == O.ModIds;
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
};
