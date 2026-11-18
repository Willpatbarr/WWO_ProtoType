#pragma once

// A player's loadout (Godot: Loadout + Modifiers). A GameInstance subsystem, so it lives across map changes.
// A thin owner of four parts, each its own type:
//   Catalog     FWWOLoadoutCatalog (WWOLoadoutCatalog.h): every option there is, loaded from data
//   Equipped    FWWOEquipped (WWOLoadoutEquipped.h): what this player has on
//   stat maths  WWOLoadoutStats (WWOLoadoutStats.h): what the equipped state does to a stat
//   ModConfigs  FWWOModConfigs (WWOModConfigs.h): saved weapon configs and the two weapon slots
// The subsystem saves and loads Loadout.sav (UWWOLoadoutSave, WWOLoadoutTypes.h), sends the loadout to the
// host (FWWONetLoadout), keeps joiners' copies (NewCopy), and announces every change (OnChanged). Its
// equip and stat functions are one-line forwards to the parts, so callers reach them as before.

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Data/WWOData.h"
#include "Systems/WWOLoadoutTypes.h"
#include "Systems/WWOLoadoutCatalog.h"
#include "Systems/WWOLoadoutEquipped.h"
#include "Systems/WWOLoadoutStats.h"
#include "Systems/WWOModConfigs.h"
#include "WWOLoadoutSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FWWOOnLoadoutChanged);

UCLASS()
class WHATWASOURS_API UWWOLoadoutSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 TraitSlots = FWWOEquipped::TraitSlots;
	static constexpr int32 AbilitySlots = FWWOEquipped::AbilitySlots;
	static constexpr int32 WeaponSlotCount = FWWOModConfigs::WeaponSlotCount;

	// This machine's loadout; or, on the host, a joiner's copy when the context is that joiner's
	// character or something it owns (its components, its grenades and what they leave behind).
	static UWWOLoadoutSubsystem* Get(const UObject* WorldContext);

	// Online (see FWWONetLoadout).
	FWWONetLoadout ToNet() const { return Equipped.ToNet(); }
	static UWWOLoadoutSubsystem* NewCopy(const UWWOLoadoutSubsystem* Source); // all the data, nothing equipped, never saved
	void ApplyNet(const FWWONetLoadout& Net);
	bool IsCopy() const { return bCopy; }

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	void FlushSave(); // writes the loadout if it changed since the last write

	FWWOOnLoadoutChanged OnChanged;

	// How much one group point changes each stat in its group (0.05 = 5%). A Settings row binds it by name.
	UPROPERTY(EditAnywhere, Category = "Frame parts") float PointValue = 0.05f;

	// Every option (Catalog.Weapons, Catalog.Floras, ...), the enemies' and allies' tuning.
	UPROPERTY() FWWOLoadoutCatalog Catalog;
	UWWOEnemyData* EnemyDataFor(FName Kind) const { return Catalog.EnemyDataFor(Kind); }
	template <typename T>
	T* EnemyDataAs(FName Kind) const { return Cast<T>(Catalog.EnemyDataFor(Kind)); }

	// --- What is equipped (FWWOEquipped) ---
	const FWWOEquipped& GetEquipped() const { return Equipped; }
	UWWOWeaponData* GetWeapon() const { return Equipped.Weapon; }
	UWWOGrenadeData* GetGrenade() const { return Equipped.Grenade; }
	UWWOFloraData* GetFlora() const { return Equipped.Flora; }
	UWWOAbilityData* GetAbility(int32 Slot) const { return Equipped.GetAbility(Slot); }
	int32 AbilitySlotOf(const UWWOAbilityData* Choice) const { return Equipped.AbilitySlotOf(Choice); }
	UWWOFramePartData* GetFramePart(FName Slot) const { return Equipped.GetFramePart(Slot); }
	TArray<UWWOTraitData*> EquippedTraits() const { return Equipped.TraitList(); }
	bool IsTraitEquipped(const UWWOTraitData* Choice) const { return Equipped.Traits.Contains(Choice); }

	// Picker lists for the equipped flora (FWWOLoadoutCatalog).
	TArray<UWWOGrenadeData*> GrenadesForFlora() const { return Catalog.GrenadesFor(Equipped.Flora); }
	TArray<UWWOAbilityData*> AbilitiesForFlora() const { return Catalog.AbilitiesFor(Equipped.Flora); }
	TArray<UWWOTraitData*> TraitsForFlora() const { return Catalog.TraitsFor(Equipped.Flora); }
	TArray<UWWOFramePartData*> FramePartsFor(FName Slot) const { return Catalog.FramePartsFor(Slot); }

	// Equipping: each change is announced (OnChanged) and saved later (FlushSave).
	void EquipFlora(UWWOFloraData* Choice);
	void EquipGrenade(UWWOGrenadeData* Choice);
	void EquipWeapon(UWWOWeaponData* Choice);
	void CycleWeapon(int32 Step);
	void EquipFramePart(UWWOFramePartData* Part);
	void EquipAbility(int32 Slot, UWWOAbilityData* Choice);
	bool ToggleTrait(UWWOTraitData* Choice);

	// --- Weapon mods (draft): every slot of a frame is open ---
	static const TArray<FName>& ModSlots() { return FWWOModConfigs::ModSlots(); }
	static FString ModSlotTitle(FName Slot) { return FWWOModConfigs::ModSlotTitle(Slot); }
	bool HasMods(const UWWOWeaponData* W) const { return Catalog.HasMods(W); } // is this weapon a frame with mods?
	TArray<UWWOModData*> ModsFor(FName Slot) const { return Catalog.ModsFor(Equipped.Weapon, Slot); } // for the equipped weapon
	UWWOModData* GetMod(FName Slot) const { return Equipped.GetMod(Slot); } // on the equipped weapon, or null
	void EquipMod(FName Slot, UWWOModData* Mod); // null empties the slot
	bool HasPerk(FName Perk) const { return Equipped.HasPerk(Perk); } // a mod with this perk is on the equipped weapon

	// --- Saved configs and the two weapon slots (FWWOModConfigs) ---
	const TArray<FWWOModConfig>& GetConfigs() const { return ModConfigs.Configs; }
	void SaveConfig(int32 Index); // INDEX_NONE adds a new one
	void ApplyConfig(int32 Index);
	int32 CycleConfig(); // applies the next saved config; returns its index or INDEX_NONE
	int32 GetActiveConfig() const { return ModConfigs.Active; }
	void DeleteConfig(int32 Index);
	static FString ConfigSummary(const FWWOModConfig& Config, const UWWOLoadoutSubsystem* L) { return FWWOModConfigs::Summary(Config, L ? &L->Catalog : nullptr); }
	// The two weapons you carry: each slot holds one saved config (any config in either slot, for
	// now). The active slot's is the one in your hands; swapping weapons goes to the other slot.
	int32 GetSlotConfig(int32 Slot) const { return ModConfigs.GetSlotConfig(Slot); }
	int32 GetActiveSlot() const { return ModConfigs.ActiveSlot; }
	void SetSlotConfig(int32 Slot, int32 ConfigIndex);
	bool SwapWeaponSlot(); // false when the other slot is empty
	// The hover test (Player/WWOAutoTestHover.cpp): put these configs in place of the saved ones, in
	// memory only. From the first call this loadout never writes Loadout.sav again (this run).
	void AutoTestSetConfigs(const TArray<FWWOModConfig>& Configs);

	// --- Stat maths (WWOLoadoutStats) ---
	float Apply(FName Stat, float Base) const { return WWOLoadoutStats::Apply(Equipped, PointValue, Stat, Base); }
	float GroupPoints(FName Group) const { return WWOLoadoutStats::GroupPoints(Equipped, Group); }
	TArray<FString> GroupSummary() const { return WWOLoadoutStats::GroupSummary(Equipped, PointValue); }
	float ModPercent(FName Stat) const { return WWOLoadoutStats::ModPercent(Equipped, Stat); } // sum of the equipped weapon's mods for this stat
	float TraitBonus(FName Effect) const { return WWOLoadoutStats::TraitBonus(Equipped, Effect); } // 0 if no equipped trait has it
	static float Bonus(const UObject* WorldContext, FName Effect);

	static const TArray<FName>& GroupOrder() { return WWOLoadoutStats::GroupOrder(); }
	static const TArray<FName>& StatsInGroup(FName Group) { return WWOLoadoutStats::StatsInGroup(Group); }
	static FString GroupTitle(FName Group) { return WWOLoadoutStats::GroupTitle(Group); }
	static FString StatName(FName Stat) { return WWOLoadoutStats::StatName(Stat); }
	static FName GroupOf(FName Stat) { return WWOLoadoutStats::GroupOf(Stat); }
	static const TArray<FName>& FrameSlots() { return FWWOEquipped::FrameSlots(); }

private:
	UPROPERTY() FWWOEquipped Equipped;
	FWWOModConfigs ModConfigs;

	bool bCopy = false; // a joiner's loadout on the host
	bool bSaveDue = false;
	bool bNeverSave = false; // set by AutoTestSetConfigs: test data that must not reach the saved file
	void Save() const;
	UWWOLoadoutSave* MakeSave() const; // what Save writes (the regression scenarios hash it)
	bool LoadSave();
	void LoadDefaultLoadout();
	void ApplySave(const UWWOLoadoutSave* Data);
	void Changed();

	// The regression scenarios (-WWORegress, Player/WWORegress.h) read and reset private state between runs; no gameplay code uses this.
	friend class FWWORegress;
};
