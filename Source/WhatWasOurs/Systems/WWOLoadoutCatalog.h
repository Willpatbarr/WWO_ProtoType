#pragma once

// The loadout catalog: every option there is, loaded once from data (the data assets in Content/WWO/Data
// and Config/WeaponMods.json), in picker order, and the lists the pickers show. Read-only after Load; a
// joiner's copy on the host shares it. Owned by UWWOLoadoutSubsystem (Catalog).

#include "CoreMinimal.h"
#include "Data/WWOData.h"
#include "WWOLoadoutCatalog.generated.h"

USTRUCT()
struct FWWOLoadoutCatalog
{
	GENERATED_BODY()

	// Every option, in picker order.
	UPROPERTY() TArray<TObjectPtr<UWWOWeaponData>> Weapons;
	UPROPERTY() TArray<TObjectPtr<UWWOGrenadeData>> Grenades;
	UPROPERTY() TArray<TObjectPtr<UWWOAbilityData>> Abilities;
	UPROPERTY() TArray<TObjectPtr<UWWOFramePartData>> FrameParts;
	UPROPERTY() TArray<TObjectPtr<UWWOFloraData>> Floras;
	UPROPERTY() TArray<TObjectPtr<UWWOTraitData>> Traits;
	UPROPERTY() TArray<TObjectPtr<UWWOModData>> WeaponMods; // Config/WeaponMods.json

	// Shared enemy tuning, one asset per kind of the enemy registry (WWOEnemyKinds, by id), and the allies'.
	UPROPERTY() TMap<FName, TObjectPtr<UWWOEnemyData>> EnemyData;
	UPROPERTY() TObjectPtr<UWWOAllyData> AllyData;

	// Loads everything; what's missing falls back to code defaults made in Outer.
	void Load(UObject* Outer);

	UWWOEnemyData* EnemyDataFor(FName Kind) const { const TObjectPtr<UWWOEnemyData>* Found = EnemyData.Find(Kind); return Found ? Found->Get() : nullptr; }

	// Picker lists. A null flora lists every flora's.
	TArray<UWWOGrenadeData*> GrenadesFor(const UWWOFloraData* Flora) const;
	TArray<UWWOAbilityData*> AbilitiesFor(const UWWOFloraData* Flora) const;
	TArray<UWWOTraitData*> TraitsFor(const UWWOFloraData* Flora) const;
	TArray<UWWOFramePartData*> FramePartsFor(FName Slot) const;
	TArray<UWWOModData*> ModsFor(const UWWOWeaponData* Weapon, FName Slot) const; // none for a null weapon
	bool HasMods(const UWWOWeaponData* W) const; // is this weapon a frame with mods?

	// The first item with this id, or null.
	template <typename T>
	static T* FindById(const TArray<TObjectPtr<T>>& List, FName Id)
	{
		for (T* Item : List)
		{
			if (Item && Item->Id == Id) return Item;
		}
		return nullptr;
	}

private:
	void LoadAssets(UObject* Outer);
	void LoadMods(UObject* Outer);
};
