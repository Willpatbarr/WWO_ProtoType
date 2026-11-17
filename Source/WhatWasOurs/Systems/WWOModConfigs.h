#pragma once

// Saved weapon configs (draft): a frame and the mod in each of its slots, by name ("Config 1", ...), and
// the two weapon slots you carry, each holding one config (the active slot's is the one in your hands).
// Owned by UWWOLoadoutSubsystem (ModConfigs), saved with the loadout; these functions change only the
// configs and the equipped state they are applied to, the subsystem announces the change.

#include "CoreMinimal.h"
#include "Systems/WWOLoadoutTypes.h"

struct FWWOEquipped;
struct FWWOLoadoutCatalog;

struct FWWOModConfigs
{
	static constexpr int32 WeaponSlotCount = 2;
	static const TArray<FName>& ModSlots(); // every slot of a frame, in menu order
	static FString ModSlotTitle(FName Slot);
	static FString Summary(const FWWOModConfig& Config, const FWWOLoadoutCatalog* Catalog); // the mods' names, or "No mods"

	TArray<FWWOModConfig> Configs;
	int32 Active = INDEX_NONE; // the last one applied (this session)
	TArray<int32> SlotConfigs = { INDEX_NONE, INDEX_NONE };
	int32 ActiveSlot = 0;

	int32 GetSlotConfig(int32 Slot) const { return SlotConfigs.IsValidIndex(Slot) && Configs.IsValidIndex(SlotConfigs[Slot]) ? SlotConfigs[Slot] : INDEX_NONE; }

	bool Save(int32 Index, const FWWOEquipped& E); // the equipped weapon's mods; INDEX_NONE adds a new one; false: no weapon
	bool Apply(int32 Index, FWWOEquipped& E, const FWWOLoadoutCatalog& Catalog); // false: no such config
	bool SetSlot(int32 Slot, int32 ConfigIndex); // false: no such slot or config
	bool Delete(int32 Index);                    // false: no such config
	int32 OtherSlot() const { return (ActiveSlot + 1) % WeaponSlotCount; }

	void ToSave(UWWOLoadoutSave& Data) const;
	void FromSave(const UWWOLoadoutSave& Data); // the configs and the slots as saved, else the first two configs
};
