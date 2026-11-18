#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOWorldSubsystem.h"
#include "WhatWasOurs.h"
#include "Player/WWOCharacter.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Systems/WWOConfig.h"

namespace
{
	const TCHAR* SaveSlot = TEXT("Loadout");
}

UWWOLoadoutSubsystem* UWWOLoadoutSubsystem::Get(const UObject* WorldContext)
{
	if (const AWWOCharacter* C = UWWOWorldSubsystem::PlayerOf(WorldContext))
	{
		// A copy kept for someone else's character: a joiner's or a bot's on the host, anyone else's on a
		// joiner (AWWOCharacter::OnRep_NetLook). Never your own character on your own machine.
		const bool bMine = C->IsLocalHuman();
		if (C->NetLoadout && !bMine) return C->NetLoadout;
	}
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UWWOLoadoutSubsystem>() : nullptr;
}

void UWWOLoadoutSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Catalog.Load(this);
	Equipped.Abilities.SetNum(AbilitySlots);
	if (!LoadSave())
	{
		LoadDefaultLoadout();
	}
	if (!Equipped.Flora && Catalog.Floras.Num() > 0) Equipped.Flora = Catalog.Floras[0];
	Equipped.FitFlora(); // drop a saved grenade/ability that isn't part of this flora
	for (const FName& Slot : FrameSlots())
	{
		if (!Equipped.Frame.Contains(Slot))
		{
			const TArray<UWWOFramePartData*> Options = FramePartsFor(Slot);
			if (Options.Num() > 0) Equipped.Frame.Add(Slot, Options[0]); // the Standard one
		}
	}
	if (!Equipped.Weapon && Catalog.Weapons.Num() > 0) Equipped.Weapon = Catalog.Weapons[0];
	Equipped.FillFloraSlots(Catalog);
	UE_LOG(LogWWO, Log, TEXT("Loadout: %d weapons, %d grenades, %d abilities, %d frame parts, %d floras, %d traits, %d weapon mods, %d configs"),
		Catalog.Weapons.Num(), Catalog.Grenades.Num(), Catalog.Abilities.Num(), Catalog.FrameParts.Num(), Catalog.Floras.Num(), Catalog.Traits.Num(),
		Catalog.WeaponMods.Num(), ModConfigs.Configs.Num());
}

// --- Equipping --------------------------------------------------------------------------

void UWWOLoadoutSubsystem::EquipFlora(UWWOFloraData* Choice)
{
	// Switching flora swaps in that flora's first grenade and abilities and clears traits.
	if (Choice == Equipped.Flora) return;
	Equipped.SwitchFlora(Choice, Catalog);
	Changed();
}

void UWWOLoadoutSubsystem::EquipGrenade(UWWOGrenadeData* Choice)
{
	Equipped.Grenade = Choice;
	Changed();
}

void UWWOLoadoutSubsystem::EquipWeapon(UWWOWeaponData* Choice)
{
	Equipped.Weapon = Choice;
	Changed();
}

void UWWOLoadoutSubsystem::CycleWeapon(int32 Step)
{
	if (UWWOWeaponData* Next = Equipped.CycledWeapon(Step, Catalog)) EquipWeapon(Next);
}

void UWWOLoadoutSubsystem::EquipFramePart(UWWOFramePartData* Part)
{
	Equipped.Frame.Add(Part->Slot, Part);
	Changed();
}

void UWWOLoadoutSubsystem::EquipAbility(int32 Slot, UWWOAbilityData* Choice)
{
	Equipped.SetAbility(Slot, Choice);
	Changed();
}

bool UWWOLoadoutSubsystem::ToggleTrait(UWWOTraitData* Choice)
{
	if (!Equipped.ToggleTrait(Choice)) return false;
	Changed();
	return true;
}

void UWWOLoadoutSubsystem::EquipMod(FName Slot, UWWOModData* Mod)
{
	if (Equipped.SetMod(Slot, Mod)) Changed();
}

float UWWOLoadoutSubsystem::Bonus(const UObject* WorldContext, FName Effect)
{
	const UWWOLoadoutSubsystem* Loadout = Get(WorldContext);
	return Loadout ? Loadout->TraitBonus(Effect) : 0.f;
}

void UWWOLoadoutSubsystem::Changed()
{
	// Written to disk later (FlushSave: when the character menu closes, on a map change and at
	// exit), not on every change: a weapon swap mid-fight is a change too.
	bSaveDue = true;
	OnChanged.Broadcast();
}

void UWWOLoadoutSubsystem::FlushSave()
{
	if (!bSaveDue) return;
	bSaveDue = false;
	Save();
}

void UWWOLoadoutSubsystem::Deinitialize()
{
	FlushSave();
	Super::Deinitialize();
}

// --- Saved configs and weapon slots ----------------------------------------------------------

void UWWOLoadoutSubsystem::SaveConfig(int32 Index)
{
	if (ModConfigs.Save(Index, Equipped)) Changed();
}

void UWWOLoadoutSubsystem::ApplyConfig(int32 Index)
{
	if (ModConfigs.Apply(Index, Equipped, Catalog)) Changed();
}

void UWWOLoadoutSubsystem::SetSlotConfig(int32 Slot, int32 ConfigIndex)
{
	if (!ModConfigs.SetSlot(Slot, ConfigIndex)) return;
	if (Slot == ModConfigs.ActiveSlot) ApplyConfig(ConfigIndex); // it's the one in your hands
	else Changed();
}

bool UWWOLoadoutSubsystem::SwapWeaponSlot()
{
	const int32 Other = ModConfigs.OtherSlot();
	if (GetSlotConfig(Other) == INDEX_NONE) return false;
	ModConfigs.ActiveSlot = Other;
	ApplyConfig(ModConfigs.SlotConfigs[Other]);
	return true;
}

int32 UWWOLoadoutSubsystem::CycleConfig()
{
	const TArray<FWWOModConfig>& Configs = ModConfigs.Configs;
	if (Configs.Num() == 0) return INDEX_NONE;
	const int32 Next = Configs.IsValidIndex(ModConfigs.Active) ? (ModConfigs.Active + 1) % Configs.Num() : 0;
	ApplyConfig(Next);
	return Next;
}

void UWWOLoadoutSubsystem::DeleteConfig(int32 Index)
{
	if (ModConfigs.Delete(Index)) Changed();
}

void UWWOLoadoutSubsystem::AutoTestSetConfigs(const TArray<FWWOModConfig>& Configs)
{
	if (!bNeverSave) UE_LOG(LogWWO, Display, TEXT("Loadout: test configs in memory only; the saved file is not written again this run"));
	bNeverSave = true;
	ModConfigs.Configs = Configs;
	ModConfigs.Active = INDEX_NONE;
	for (int32 i = 0; i < WeaponSlotCount; ++i) ModConfigs.SlotConfigs[i] = Configs.IsValidIndex(i) ? i : INDEX_NONE;
	Changed();
}

// --- Saving -------------------------------------------------------------------------------

void UWWOLoadoutSubsystem::Save() const
{
	if (bCopy) return; // a joiner's copy: their own machine saves theirs
	if (bNeverSave) return; // the hover test's configs (AutoTestSetConfigs)
	if (Catalog.WeaponMods.Num() == 0 || Catalog.Weapons.Num() == 0)
	{
		// The mod file didn't load (a slip in a hand edit) or no assets did: what's equipped now is
		// missing things the save still has. Keep the save.
		UE_LOG(LogWWO, Warning, TEXT("Loadout not saved: the mods or weapons didn't load, and saving would drop what's equipped"));
		return;
	}
	UGameplayStatics::SaveGameToSlot(MakeSave(), SaveSlot, 0);
}

UWWOLoadoutSave* UWWOLoadoutSubsystem::MakeSave() const
{
	UWWOLoadoutSave* Data = Cast<UWWOLoadoutSave>(UGameplayStatics::CreateSaveGameObject(UWWOLoadoutSave::StaticClass()));
	Equipped.ToSave(*Data);
	ModConfigs.ToSave(*Data);
	return Data;
}

bool UWWOLoadoutSubsystem::LoadSave()
{
	if (!UGameplayStatics::DoesSaveGameExist(SaveSlot, 0)) return false;
	const UWWOLoadoutSave* Data = Cast<UWWOLoadoutSave>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0));
	if (!Data) return false;
	ApplySave(Data);
	return true;
}

void UWWOLoadoutSubsystem::ApplySave(const UWWOLoadoutSave* Data)
{
	Equipped.FromSave(*Data, Catalog);
	ModConfigs.FromSave(*Data);
}

void UWWOLoadoutSubsystem::LoadDefaultLoadout()
{
	// First run: start from Config/DefaultLoadout.json (the loadout carried over from the
	// Godot prototype), in the same format Godot's loadout.json used.
	const TSharedPtr<FJsonObject> Json = WWOConfig::Load(WWOConfig::Path(TEXT("DefaultLoadout.json")));
	if (!Json.IsValid()) return;
	UWWOLoadoutSave* Data = NewObject<UWWOLoadoutSave>();
	Data->Flora = FName(Json->GetStringField(TEXT("flora")));
	Data->Weapon = FName(Json->GetStringField(TEXT("weapon")));
	Data->Grenade = FName(Json->GetStringField(TEXT("grenade")));
	for (const TSharedPtr<FJsonValue>& V : Json->GetArrayField(TEXT("abilities"))) Data->Abilities.Add(FName(V->AsString()));
	for (const TSharedPtr<FJsonValue>& V : Json->GetArrayField(TEXT("traits"))) Data->Traits.Add(FName(V->AsString()));
	const TSharedPtr<FJsonObject>* Frame = nullptr;
	if (Json->TryGetObjectField(TEXT("frame"), Frame))
	{
		for (const auto& Pair : (*Frame)->Values) Data->Frame.Add(FName(*Pair.Key), FName(Pair.Value->AsString()));
	}
	ApplySave(Data);
}

// --- Online --------------------------------------------------------------------------------------

UWWOLoadoutSubsystem* UWWOLoadoutSubsystem::NewCopy(const UWWOLoadoutSubsystem* Source)
{
	if (!Source) return nullptr;
	UWWOLoadoutSubsystem* Copy = NewObject<UWWOLoadoutSubsystem>(Source->GetOuter()); // not registered: GetSubsystem still finds this machine's
	Copy->bCopy = true;
	Copy->PointValue = Source->PointValue;
	Copy->Catalog = Source->Catalog;
	Copy->Equipped.Abilities.SetNum(AbilitySlots);
	return Copy;
}

void UWWOLoadoutSubsystem::ApplyNet(const FWWONetLoadout& Net)
{
	// Exactly what the joiner has (nothing filled in for them).
	Equipped.Reset();
	UWWOLoadoutSave* Data = NewObject<UWWOLoadoutSave>();
	Data->Flora = Net.Flora;
	Data->Weapon = Net.Weapon;
	Data->Grenade = Net.Grenade;
	Data->Abilities = Net.Abilities;
	Data->Traits = Net.Traits;
	for (int32 i = 0; i < FMath::Min(Net.FrameSlots.Num(), Net.FrameParts.Num()); ++i) Data->Frame.Add(Net.FrameSlots[i], Net.FrameParts[i]);
	for (int32 i = 0; i < FMath::Min(Net.ModKeys.Num(), Net.ModIds.Num()); ++i) Data->WeaponMods.Add(Net.ModKeys[i], Net.ModIds[i]);
	ApplySave(Data);
	// A loadout always has a flora (without one the one-flora-kit rule below would pass anything)
	// and an unlocked gun.
	if (!Equipped.Flora && Catalog.Floras.Num() > 0) Equipped.Flora = Catalog.Floras[0];
	if (Equipped.Weapon && !Equipped.Weapon->bUnlocked)
	{
		Equipped.Weapon = nullptr;
		for (UWWOWeaponData* W : Catalog.Weapons) if (W && W->bUnlocked) { Equipped.Weapon = W; break; }
	}
	Equipped.FitFlora(); // a grenade, ability or trait from another flora doesn't count
	OnChanged.Broadcast();
}
