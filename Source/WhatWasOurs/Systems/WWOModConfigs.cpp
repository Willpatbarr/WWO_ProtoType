#include "Systems/WWOModConfigs.h"
#include "Systems/WWOLoadoutCatalog.h"
#include "Systems/WWOLoadoutEquipped.h"

const TArray<FName>& FWWOModConfigs::ModSlots()
{
	static const TArray<FName> Slots = { "barrel", "magazine", "stock", "sight", "chambering", "grip" };
	return Slots;
}

FString FWWOModConfigs::ModSlotTitle(FName Slot)
{
	FString S = Slot.ToString();
	return S.Left(1).ToUpper() + S.Mid(1);
}

FString FWWOModConfigs::Summary(const FWWOModConfig& Config, const FWWOLoadoutCatalog* Catalog)
{
	TArray<FString> Names;
	for (const FName& Slot : ModSlots())
	{
		const FName* Id = Config.Mods.Find(Slot);
		const UWWOModData* Mod = Id && Catalog ? FWWOLoadoutCatalog::FindById(Catalog->WeaponMods, *Id) : nullptr;
		if (Mod) Names.Add(Mod->DisplayName);
	}
	return Names.Num() > 0 ? FString::Join(Names, TEXT(", ")) : FString(TEXT("No mods"));
}

bool FWWOModConfigs::Save(int32 Index, const FWWOEquipped& E)
{
	if (!E.Weapon) return false;
	FWWOModConfig Config;
	Config.Weapon = E.Weapon->Id;
	Config.Mods = E.ModIds(E.Weapon->Id);
	if (Configs.IsValidIndex(Index))
	{
		Config.Name = Configs[Index].Name;
		Configs[Index] = Config;
	}
	else
	{
		// Names are "Config 1", "Config 2", ...: the lowest number not taken.
		int32 N = 1;
		while (Configs.ContainsByPredicate([N](const FWWOModConfig& C) { return C.Name == FString::Printf(TEXT("Config %d"), N); })) ++N;
		Config.Name = FString::Printf(TEXT("Config %d"), N);
		Configs.Add(Config);
	}
	return true;
}

bool FWWOModConfigs::Apply(int32 Index, FWWOEquipped& E, const FWWOLoadoutCatalog& Catalog)
{
	if (!Configs.IsValidIndex(Index)) return false;
	const FWWOModConfig& Config = Configs[Index];
	if (UWWOWeaponData* W = FWWOLoadoutCatalog::FindById(Catalog.Weapons, Config.Weapon)) E.Weapon = W;
	E.SetModIds(Config.Weapon, Config.Mods, Catalog);
	Active = Index;
	return true;
}

bool FWWOModConfigs::SetSlot(int32 Slot, int32 ConfigIndex)
{
	if (!SlotConfigs.IsValidIndex(Slot) || !Configs.IsValidIndex(ConfigIndex)) return false;
	SlotConfigs[Slot] = ConfigIndex;
	return true;
}

bool FWWOModConfigs::Delete(int32 Index)
{
	if (!Configs.IsValidIndex(Index)) return false;
	Configs.RemoveAt(Index);
	if (Active == Index) Active = INDEX_NONE;
	else if (Active > Index) --Active;
	for (int32& InSlot : SlotConfigs)
	{
		if (InSlot == Index) InSlot = INDEX_NONE;
		else if (InSlot > Index) --InSlot;
	}
	return true;
}

void FWWOModConfigs::ToSave(UWWOLoadoutSave& Data) const
{
	Data.ModConfigs = Configs;
	Data.WeaponSlots = SlotConfigs;
}

void FWWOModConfigs::FromSave(const UWWOLoadoutSave& Data)
{
	Configs = Data.ModConfigs;
	// The two weapon slots: as saved, else the first two saved configs.
	SlotConfigs = { INDEX_NONE, INDEX_NONE };
	for (int32 Slot = 0; Slot < WeaponSlotCount; ++Slot)
	{
		if (Data.WeaponSlots.IsValidIndex(Slot) && Configs.IsValidIndex(Data.WeaponSlots[Slot])) SlotConfigs[Slot] = Data.WeaponSlots[Slot];
		else if (Data.WeaponSlots.Num() == 0 && Configs.IsValidIndex(Slot)) SlotConfigs[Slot] = Slot;
	}
}
