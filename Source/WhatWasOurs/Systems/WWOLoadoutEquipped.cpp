#include "Systems/WWOLoadoutEquipped.h"
#include "Systems/WWOLoadoutCatalog.h"

const TArray<FName>& FWWOEquipped::FrameSlots()
{
	static const TArray<FName> Slots = { "head", "arms", "legs", "core" };
	return Slots;
}

// --- Reading --------------------------------------------------------------------------------

int32 FWWOEquipped::AbilitySlotOf(const UWWOAbilityData* Choice) const
{
	for (int32 i = 0; i < Abilities.Num(); ++i)
	{
		if (Abilities[i] == Choice) return i;
	}
	return INDEX_NONE;
}

UWWOFramePartData* FWWOEquipped::GetFramePart(FName Slot) const
{
	const TObjectPtr<UWWOFramePartData>* Found = Frame.Find(Slot);
	return Found ? Found->Get() : nullptr;
}

TArray<UWWOTraitData*> FWWOEquipped::TraitList() const
{
	TArray<UWWOTraitData*> Out;
	for (UWWOTraitData* T : Traits) Out.Add(T);
	return Out;
}

UWWOModData* FWWOEquipped::GetMod(FName Slot) const
{
	if (!Weapon) return nullptr;
	const TMap<FName, TObjectPtr<UWWOModData>>* OnFrame = Mods.Find(Weapon->Id);
	const TObjectPtr<UWWOModData>* Found = OnFrame ? OnFrame->Find(Slot) : nullptr;
	return Found ? Found->Get() : nullptr;
}

bool FWWOEquipped::HasPerk(FName Perk) const
{
	if (!Weapon) return false;
	if (const TMap<FName, TObjectPtr<UWWOModData>>* OnFrame = Mods.Find(Weapon->Id))
	{
		for (const auto& Pair : *OnFrame)
		{
			if (Pair.Value && Pair.Value->Perk == Perk) return true;
		}
	}
	return false;
}

TMap<FName, FName> FWWOEquipped::ModIds(FName WeaponId) const
{
	TMap<FName, FName> Ids;
	if (const TMap<FName, TObjectPtr<UWWOModData>>* OnFrame = Mods.Find(WeaponId))
	{
		for (const auto& Pair : *OnFrame)
		{
			if (Pair.Value) Ids.Add(Pair.Key, Pair.Value->Id);
		}
	}
	return Ids;
}

// --- Changing -------------------------------------------------------------------------------

void FWWOEquipped::SwitchFlora(UWWOFloraData* Choice, const FWWOLoadoutCatalog& Catalog)
{
	Flora = Choice;
	Grenade = nullptr;
	Abilities.Init(nullptr, AbilitySlots);
	Traits.Reset();
	FillFloraSlots(Catalog);
}

void FWWOEquipped::SetAbility(int32 Slot, UWWOAbilityData* Choice)
{
	const int32 Other = AbilitySlotOf(Choice);
	if (Other != INDEX_NONE && Other != Slot)
	{
		Abilities[Other] = Abilities[Slot];
	}
	Abilities[Slot] = Choice;
}

bool FWWOEquipped::ToggleTrait(UWWOTraitData* Choice)
{
	if (Traits.Contains(Choice))
	{
		Traits.Remove(Choice);
	}
	else if (Traits.Num() >= TraitSlots || (Flora && Choice->Flora != Flora->Id))
	{
		return false;
	}
	else
	{
		Traits.Add(Choice);
	}
	return true;
}

bool FWWOEquipped::SetMod(FName Slot, UWWOModData* Mod)
{
	if (!Weapon) return false;
	TMap<FName, TObjectPtr<UWWOModData>>& OnFrame = Mods.FindOrAdd(Weapon->Id); // before the check, as always: the frame's entry exists from here
	if (Mod && (Mod->Frame != Weapon->Id || Mod->Slot != Slot)) return false;
	if (Mod) OnFrame.Add(Slot, Mod);
	else OnFrame.Remove(Slot);
	return true;
}

void FWWOEquipped::SetModIds(FName WeaponId, const TMap<FName, FName>& Ids, const FWWOLoadoutCatalog& Catalog)
{
	TMap<FName, TObjectPtr<UWWOModData>>& OnFrame = Mods.FindOrAdd(WeaponId);
	OnFrame.Reset();
	for (const auto& Pair : Ids)
	{
		UWWOModData* Mod = FWWOLoadoutCatalog::FindById(Catalog.WeaponMods, Pair.Value);
		if (Mod && Mod->Frame == WeaponId && Mod->Slot == Pair.Key) OnFrame.Add(Pair.Key, Mod);
	}
}

UWWOWeaponData* FWWOEquipped::CycledWeapon(int32 Step, const FWWOLoadoutCatalog& Catalog) const
{
	TArray<UWWOWeaponData*> Unlocked;
	for (UWWOWeaponData* W : Catalog.Weapons)
	{
		if (W->bUnlocked) Unlocked.Add(W);
	}
	if (Unlocked.Num() <= 1) return nullptr;
	const int32 Index = Unlocked.IndexOfByKey(Weapon);
	return Unlocked[(Index + Step + Unlocked.Num()) % Unlocked.Num()];
}

void FWWOEquipped::FitFlora()
{
	if (!Flora) return;
	if (Grenade && Grenade->Flora != Flora->Id) Grenade = nullptr;
	for (int32 Slot = 0; Slot < AbilitySlots; ++Slot)
	{
		const UWWOAbilityData* A = Abilities[Slot];
		if (A && (A->Flora != Flora->Id || !A->bUnlocked)) Abilities[Slot] = nullptr;
	}
	Traits.RemoveAll([this](const TObjectPtr<UWWOTraitData>& T) { return T->Flora != Flora->Id; });
}

void FWWOEquipped::FillFloraSlots(const FWWOLoadoutCatalog& Catalog)
{
	const TArray<UWWOGrenadeData*> FloraGrenades = Catalog.GrenadesFor(Flora);
	if (!Grenade && FloraGrenades.Num() > 0) Grenade = FloraGrenades[0];
	for (int32 Slot = 0; Slot < AbilitySlots; ++Slot)
	{
		if (Abilities[Slot]) continue;
		for (UWWOAbilityData* Option : Catalog.AbilitiesFor(Flora))
		{
			if (Option->bUnlocked && !Abilities.Contains(Option))
			{
				Abilities[Slot] = Option;
				break;
			}
		}
	}
}

// --- The saved file and the wire --------------------------------------------------------------

void FWWOEquipped::ToSave(UWWOLoadoutSave& Data) const
{
	Data.Flora = Flora ? Flora->Id : NAME_None;
	Data.Weapon = Weapon ? Weapon->Id : NAME_None;
	Data.Grenade = Grenade ? Grenade->Id : NAME_None;
	for (const UWWOAbilityData* A : Abilities) Data.Abilities.Add(A ? A->Id : NAME_None);
	for (const UWWOTraitData* T : Traits) Data.Traits.Add(T->Id);
	for (const auto& Pair : Frame) Data.Frame.Add(Pair.Key, Pair.Value->Id);
	for (const auto& OnFrame : Mods)
	{
		for (const auto& Pair : OnFrame.Value)
		{
			if (Pair.Value) Data.WeaponMods.Add(FName(OnFrame.Key.ToString() + TEXT(":") + Pair.Key.ToString()), Pair.Value->Id);
		}
	}
}

void FWWOEquipped::FromSave(const UWWOLoadoutSave& Data, const FWWOLoadoutCatalog& Catalog)
{
	Flora = FWWOLoadoutCatalog::FindById(Catalog.Floras, Data.Flora);
	Weapon = FWWOLoadoutCatalog::FindById(Catalog.Weapons, Data.Weapon);
	Grenade = FWWOLoadoutCatalog::FindById(Catalog.Grenades, Data.Grenade);
	for (const FName& Id : Data.Traits)
	{
		UWWOTraitData* T = FWWOLoadoutCatalog::FindById(Catalog.Traits, Id);
		if (T && !Traits.Contains(T) && Traits.Num() < TraitSlots) Traits.Add(T);
	}
	for (const auto& Pair : Data.Frame)
	{
		UWWOFramePartData* Part = FWWOLoadoutCatalog::FindById(Catalog.FrameParts, Pair.Value);
		if (Part && Part->Slot == Pair.Key) Frame.Add(Pair.Key, Part);
	}
	for (const auto& Pair : Data.WeaponMods)
	{
		FString WeaponId, ModSlot;
		if (!Pair.Key.ToString().Split(TEXT(":"), &WeaponId, &ModSlot)) continue;
		UWWOModData* Mod = FWWOLoadoutCatalog::FindById(Catalog.WeaponMods, Pair.Value);
		if (Mod && Mod->Frame == FName(WeaponId) && Mod->Slot == FName(ModSlot)) Mods.FindOrAdd(Mod->Frame).Add(Mod->Slot, Mod);
	}
	for (int32 Slot = 0; Slot < FMath::Min(Data.Abilities.Num(), AbilitySlots); ++Slot)
	{
		UWWOAbilityData* A = FWWOLoadoutCatalog::FindById(Catalog.Abilities, Data.Abilities[Slot]);
		if (A && !Abilities.Contains(A)) Abilities[Slot] = A;
	}
}

FWWONetLoadout FWWOEquipped::ToNet() const
{
	FWWONetLoadout Net;
	Net.Flora = Flora ? Flora->Id : NAME_None;
	Net.Weapon = Weapon ? Weapon->Id : NAME_None;
	Net.Grenade = Grenade ? Grenade->Id : NAME_None;
	for (const UWWOAbilityData* A : Abilities) Net.Abilities.Add(A ? A->Id : NAME_None);
	for (const UWWOTraitData* T : Traits) Net.Traits.Add(T->Id);
	for (const auto& Pair : Frame)
	{
		Net.FrameSlots.Add(Pair.Key);
		Net.FrameParts.Add(Pair.Value ? Pair.Value->Id : NAME_None);
	}
	if (Weapon)
	{
		for (const auto& Pair : ModIds(Weapon->Id))
		{
			Net.ModKeys.Add(FName(Weapon->Id.ToString() + TEXT(":") + Pair.Key.ToString()));
			Net.ModIds.Add(Pair.Value);
		}
	}
	return Net;
}

void FWWOEquipped::Reset()
{
	Flora = nullptr;
	Weapon = nullptr;
	Grenade = nullptr;
	Abilities.Reset();
	Abilities.SetNum(AbilitySlots);
	Traits.Reset();
	Frame.Reset();
	Mods.Reset();
}
