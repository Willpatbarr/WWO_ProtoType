#pragma once

// What the player has equipped, plus the stat modifier system (Godot: Loadout + Modifiers).
// A GameInstance subsystem, so it lives across map changes. Saved with a SaveGame slot.
//
// Stats: final = (base + sum of Flat) * (1 + sum of PercentAdd) * (product of Multiply).
// Frame parts give group points; each point moves every stat in its group by PointValue
// (5% by default) in the good direction. Equipped traits add their own modifiers.

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameFramework/SaveGame.h"
#include "Data/WWOData.h"
#include "WWOLoadoutSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FWWOOnLoadoutChanged);

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

UCLASS()
class WHATWASOURS_API UWWOLoadoutSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 TraitSlots = 4; // placeholder until decided
	static constexpr int32 AbilitySlots = 2;

	// This machine's loadout; or, on the host, a joiner's copy when the context is that joiner's
	// character or something it owns (its components, its grenades and what they leave behind).
	static UWWOLoadoutSubsystem* Get(const UObject* WorldContext);

	// Online (see FWWONetLoadout).
	FWWONetLoadout ToNet() const;
	static UWWOLoadoutSubsystem* NewCopy(const UWWOLoadoutSubsystem* Source); // all the data, nothing equipped, never saved
	void ApplyNet(const FWWONetLoadout& Net);
	bool IsCopy() const { return bCopy; }

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	void FlushSave(); // writes the loadout if it changed since the last write

	FWWOOnLoadoutChanged OnChanged;

	// How much one group point changes each stat in its group (0.05 = 5%).
	UPROPERTY(EditAnywhere, Category = "Frame parts") float PointValue = 0.05f;

	// Every option, in picker order.
	UPROPERTY() TArray<TObjectPtr<UWWOWeaponData>> Weapons;
	UPROPERTY() TArray<TObjectPtr<UWWOGrenadeData>> Grenades;
	UPROPERTY() TArray<TObjectPtr<UWWOAbilityData>> Abilities;
	UPROPERTY() TArray<TObjectPtr<UWWOFramePartData>> FrameParts;
	UPROPERTY() TArray<TObjectPtr<UWWOFloraData>> Floras;
	UPROPERTY() TArray<TObjectPtr<UWWOTraitData>> Traits;
	UPROPERTY() TArray<TObjectPtr<UWWOModData>> WeaponMods; // Config/WeaponMods.json

	// Shared enemy and ally tuning (one asset each).
	UPROPERTY() TObjectPtr<UWWOSoldierData> SoldierData;
	UPROPERTY() TObjectPtr<UWWOSwarmData> SwarmData;
	UPROPERTY() TObjectPtr<UWWOTankData> TankData;
	UPROPERTY() TObjectPtr<UWWOAllyData> AllyData;

	UWWOWeaponData* GetWeapon() const { return Weapon; }
	UWWOGrenadeData* GetGrenade() const { return Grenade; }
	UWWOFloraData* GetFlora() const { return Flora; }
	UWWOAbilityData* GetAbility(int32 Slot) const { return EquippedAbilities.IsValidIndex(Slot) ? EquippedAbilities[Slot].Get() : nullptr; }
	int32 AbilitySlotOf(const UWWOAbilityData* Choice) const;
	UWWOFramePartData* GetFramePart(FName Slot) const;
	TArray<UWWOTraitData*> EquippedTraits() const;
	bool IsTraitEquipped(const UWWOTraitData* Choice) const { return EquippedTraitList.Contains(Choice); }

	TArray<UWWOGrenadeData*> GrenadesForFlora() const;
	TArray<UWWOAbilityData*> AbilitiesForFlora() const;
	TArray<UWWOTraitData*> TraitsForFlora() const;
	TArray<UWWOFramePartData*> FramePartsFor(FName Slot) const;

	void EquipFlora(UWWOFloraData* Choice);
	void EquipGrenade(UWWOGrenadeData* Choice);
	void EquipWeapon(UWWOWeaponData* Choice);
	void CycleWeapon(int32 Step);
	void EquipFramePart(UWWOFramePartData* Part);
	void EquipAbility(int32 Slot, UWWOAbilityData* Choice);
	bool ToggleTrait(UWWOTraitData* Choice);

	// --- Weapon mods (draft): every slot of a frame is open ---
	static const TArray<FName>& ModSlots();
	static FString ModSlotTitle(FName Slot);
	bool HasMods(const UWWOWeaponData* W) const; // is this weapon a frame with mods?
	TArray<UWWOModData*> ModsFor(FName Slot) const; // for the equipped weapon
	UWWOModData* GetMod(FName Slot) const; // on the equipped weapon, or null
	void EquipMod(FName Slot, UWWOModData* Mod); // null empties the slot
	bool HasPerk(FName Perk) const; // a mod with this perk is on the equipped weapon
	float ModPercent(FName Stat) const; // sum of the equipped weapon's mods for this stat
	const TArray<FWWOModConfig>& GetConfigs() const { return Configs; }
	void SaveConfig(int32 Index); // INDEX_NONE adds a new one
	void ApplyConfig(int32 Index);
	int32 CycleConfig(); // applies the next saved config; returns its index or INDEX_NONE
	int32 GetActiveConfig() const { return ActiveConfig; }
	void DeleteConfig(int32 Index);
	static FString ConfigSummary(const FWWOModConfig& Config, const UWWOLoadoutSubsystem* L);
	// The two weapons you carry: each slot holds one saved config (any config in either slot, for
	// now). The active slot's is the one in your hands; swapping weapons goes to the other slot.
	static constexpr int32 WeaponSlotCount = 2;
	int32 GetSlotConfig(int32 Slot) const { return SlotConfigs.IsValidIndex(Slot) && Configs.IsValidIndex(SlotConfigs[Slot]) ? SlotConfigs[Slot] : INDEX_NONE; }
	int32 GetActiveSlot() const { return ActiveSlot; }
	void SetSlotConfig(int32 Slot, int32 ConfigIndex);
	bool SwapWeaponSlot(); // false when the other slot is empty

	// Sum of Amount for equipped traits with this effect (0 if none).
	float TraitBonus(FName Effect) const;
	static float Bonus(const UObject* WorldContext, FName Effect);

	// --- Modifiers ---
	float Apply(FName Stat, float Base) const;
	float GroupPoints(FName Group) const;
	TArray<FString> GroupSummary() const;

	static const TArray<FName>& GroupOrder();
	static const TArray<FName>& StatsInGroup(FName Group);
	static FString GroupTitle(FName Group);
	static FString StatName(FName Stat);
	static FName GroupOf(FName Stat);
	static const TArray<FName>& FrameSlots();

private:
	UPROPERTY() TObjectPtr<UWWOWeaponData> Weapon;
	UPROPERTY() TObjectPtr<UWWOGrenadeData> Grenade;
	UPROPERTY() TObjectPtr<UWWOFloraData> Flora;
	UPROPERTY() TArray<TObjectPtr<UWWOAbilityData>> EquippedAbilities;
	UPROPERTY() TArray<TObjectPtr<UWWOTraitData>> EquippedTraitList;
	UPROPERTY() TMap<FName, TObjectPtr<UWWOFramePartData>> EquippedFrame;
	// Mods per frame (weapon id), each slot -> mod, so every frame keeps its own setup.
	TMap<FName, TMap<FName, TObjectPtr<UWWOModData>>> EquippedMods;
	TArray<FWWOModConfig> Configs;
	int32 ActiveConfig = INDEX_NONE; // the last one applied (this session)
	TArray<int32> SlotConfigs = { INDEX_NONE, INDEX_NONE };
	int32 ActiveSlot = 0;

	bool bCopy = false; // a joiner's loadout on the host
	bool bSaveDue = false;
	void LoadMods();
	TMap<FName, FName> ModIds(FName WeaponId) const;
	void SetModIds(FName WeaponId, const TMap<FName, FName>& Ids);

	void LoadAllData();
	void FitFlora();
	void FillFloraSlots();
	void Save() const;
	bool LoadSave();
	void LoadDefaultLoadout();
	void ApplySave(const UWWOLoadoutSave* Data);
	void Changed();

	template <typename T>
	static T* FindById(const TArray<TObjectPtr<T>>& List, FName Id)
	{
		for (T* Item : List)
		{
			if (Item && Item->Id == Id) return Item;
		}
		return nullptr;
	}

	// The regression scenarios (-WWORegress, Player/WWORegress.h) read and reset private state between runs; no gameplay code uses this.
	friend class FWWORegress;
};
