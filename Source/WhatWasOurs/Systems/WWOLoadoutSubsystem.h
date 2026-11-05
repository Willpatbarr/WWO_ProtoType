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
};

UCLASS()
class WHATWASOURS_API UWWOLoadoutSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 TraitSlots = 4; // placeholder until decided
	static constexpr int32 AbilitySlots = 2;

	static UWWOLoadoutSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

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

	// Sum of Amount for equipped traits with this effect (0 if none).
	float TraitBonus(FName Effect) const;
	static float Bonus(const UObject* WorldContext, FName Effect);

	// --- Modifiers ---
	float Apply(FName Stat, float Base) const;
	float GroupPoints(FName Group) const;
	TArray<FString> GroupSummary() const;
	TArray<FString> NetChanges() const;

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
};
