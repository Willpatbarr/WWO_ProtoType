#include "Systems/WWOLoadoutCatalog.h"
#include "Systems/WWOConfig.h"
#include "Enemies/WWOEnemyKinds.h"
#include "WhatWasOurs.h"
#include "Engine/AssetManager.h"
#include "Algo/StableSort.h"

namespace
{
	template <typename T>
	void LoadType(FPrimaryAssetType Type, TArray<TObjectPtr<T>>& Out)
	{
		UAssetManager& Manager = UAssetManager::Get();
		TArray<FPrimaryAssetId> Ids;
		Manager.GetPrimaryAssetIdList(Type, Ids);
		for (const FPrimaryAssetId& Id : Ids)
		{
			const FSoftObjectPath Path = Manager.GetPrimaryAssetPath(Id);
			if (T* Asset = Cast<T>(Path.TryLoad()))
			{
				Out.Add(Asset);
			}
		}
	}

	template <typename T>
	void SortByOrder(TArray<TObjectPtr<T>>& List)
	{
		Algo::StableSort(List, [](const TObjectPtr<T>& A, const TObjectPtr<T>& B) { return A->Order < B->Order; });
	}
}

void FWWOLoadoutCatalog::Load(UObject* Outer)
{
	LoadAssets(Outer);
	LoadMods(Outer);
}

void FWWOLoadoutCatalog::LoadAssets(UObject* Outer)
{
	LoadType(FPrimaryAssetType("WWOWeapon"), Weapons);
	for (UWWOWeaponData* W : Weapons)
	{
		if (W && W->VerticalKick < 0.f) W->VerticalKick = W->GunKick; // before the kick split
	}
	LoadType(FPrimaryAssetType("WWOGrenade"), Grenades);
	LoadType(FPrimaryAssetType("WWOAbility"), Abilities);
	LoadType(FPrimaryAssetType("WWOFramePart"), FrameParts);
	LoadType(FPrimaryAssetType("WWOFlora"), Floras);
	LoadType(FPrimaryAssetType("WWOTrait"), Traits);
	SortByOrder(Weapons);
	SortByOrder(Grenades);
	SortByOrder(Abilities);
	SortByOrder(FrameParts);
	SortByOrder(Floras);
	SortByOrder(Traits);

	TArray<TObjectPtr<UWWOEnemyData>> Enemies;
	LoadType(FPrimaryAssetType("WWOEnemy"), Enemies);
	for (UWWOEnemyData* Enemy : Enemies)
	{
		for (const FWWOEnemyKind& Kind : WWOEnemyKinds::All())
		{
			if (Enemy && Enemy->IsA(Kind.DataClass)) EnemyData.Add(Kind.Id, Enemy); // the last asset of a kind's class wins
		}
	}
	TArray<TObjectPtr<UWWOAllyData>> Allies;
	LoadType(FPrimaryAssetType("WWOAlly"), Allies);
	if (Allies.Num() > 0) AllyData = Allies[0];

	// Missing assets (first run before Tools/create_assets.py): fall back to code defaults.
	for (const FWWOEnemyKind& Kind : WWOEnemyKinds::All())
	{
		if (!EnemyDataFor(Kind.Id)) EnemyData.Add(Kind.Id, NewObject<UWWOEnemyData>(Outer, Kind.DataClass));
	}
	if (!AllyData) AllyData = NewObject<UWWOAllyData>(Outer);
	if (Weapons.Num() == 0)
	{
		UWWOWeaponData* Rifle = NewObject<UWWOWeaponData>(Outer);
		Rifle->Id = "rifle";
		Rifle->DisplayName = TEXT("Rifle");
		Rifle->Archetype = TEXT("Auto rifle");
		Weapons.Add(Rifle);
	}
}

void FWWOLoadoutCatalog::LoadMods(UObject* Outer)
{
	bool bFileRead = false;
	const TSharedPtr<FJsonObject> Json = WWOConfig::Load(WWOConfig::Path(TEXT("WeaponMods.json")), &bFileRead);
	if (!Json.IsValid())
	{
		if (bFileRead) UE_LOG(LogWWO, Warning, TEXT("Config/WeaponMods.json isn't valid JSON")); // a missing file is silent
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!Json->TryGetArrayField(TEXT("mods"), List)) return;
	int32 Order = 0;
	for (const TSharedPtr<FJsonValue>& V : *List)
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		if (!O.IsValid()) continue;
		UWWOModData* Mod = NewObject<UWWOModData>(Outer);
		Mod->Id = FName(O->GetStringField(TEXT("id")));
		Mod->DisplayName = O->GetStringField(TEXT("name"));
		Mod->Frame = FName(O->GetStringField(TEXT("frame")));
		Mod->Slot = FName(O->GetStringField(TEXT("slot")));
		Mod->Rarity = O->HasField(TEXT("rarity")) ? (int32)O->GetNumberField(TEXT("rarity")) : 1;
		Mod->Order = Order++;
		O->TryGetStringField(TEXT("perk_text"), Mod->PerkText);
		FString Perk;
		if (O->TryGetStringField(TEXT("perk"), Perk)) Mod->Perk = FName(Perk);
		const TSharedPtr<FJsonObject>* Stats = nullptr;
		if (O->TryGetObjectField(TEXT("stats"), Stats))
		{
			for (const auto& Pair : (*Stats)->Values)
			{
				FWWOStatModifier M;
				M.Stat = FName(Pair.Key);
				M.Op = EWWOStatOp::PercentAdd;
				M.Value = (float)Pair.Value->AsNumber();
				Mod->Mods.Add(M);
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
		if (O->TryGetArrayField(TEXT("parts"), Parts))
		{
			for (const TSharedPtr<FJsonValue>& PartValue : *Parts)
			{
				if (PartValue->AsObject().IsValid()) Mod->VisualParts.Add(PartValue->AsObject());
			}
		}
		Mod->Description = Mod->Summary();
		WeaponMods.Add(Mod);
	}
}

// --- Picker lists ---------------------------------------------------------------------

TArray<UWWOGrenadeData*> FWWOLoadoutCatalog::GrenadesFor(const UWWOFloraData* Flora) const
{
	TArray<UWWOGrenadeData*> Out;
	for (UWWOGrenadeData* G : Grenades)
	{
		if (!Flora || G->Flora == Flora->Id) Out.Add(G);
	}
	return Out;
}

TArray<UWWOAbilityData*> FWWOLoadoutCatalog::AbilitiesFor(const UWWOFloraData* Flora) const
{
	TArray<UWWOAbilityData*> Out;
	for (UWWOAbilityData* A : Abilities)
	{
		if (!Flora || A->Flora == Flora->Id) Out.Add(A);
	}
	return Out;
}

TArray<UWWOTraitData*> FWWOLoadoutCatalog::TraitsFor(const UWWOFloraData* Flora) const
{
	TArray<UWWOTraitData*> Out;
	for (UWWOTraitData* T : Traits)
	{
		if (!Flora || T->Flora == Flora->Id) Out.Add(T);
	}
	return Out;
}

TArray<UWWOFramePartData*> FWWOLoadoutCatalog::FramePartsFor(FName Slot) const
{
	TArray<UWWOFramePartData*> Out;
	for (UWWOFramePartData* P : FrameParts)
	{
		if (P->Slot == Slot) Out.Add(P);
	}
	return Out;
}

TArray<UWWOModData*> FWWOLoadoutCatalog::ModsFor(const UWWOWeaponData* Weapon, FName Slot) const
{
	TArray<UWWOModData*> Out;
	for (UWWOModData* Mod : WeaponMods)
	{
		if (Weapon && Mod->Frame == Weapon->Id && Mod->Slot == Slot) Out.Add(Mod);
	}
	return Out;
}

bool FWWOLoadoutCatalog::HasMods(const UWWOWeaponData* W) const
{
	if (!W) return false;
	for (const UWWOModData* Mod : WeaponMods)
	{
		if (Mod->Frame == W->Id) return true;
	}
	return false;
}
