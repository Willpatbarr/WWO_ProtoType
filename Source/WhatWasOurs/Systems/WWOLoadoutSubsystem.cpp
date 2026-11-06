#include "Systems/WWOLoadoutSubsystem.h"
#include "WhatWasOurs.h"
#include "Engine/AssetManager.h"
#include "Algo/StableSort.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	const TCHAR* SaveSlot = TEXT("Loadout");

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

UWWOLoadoutSubsystem* UWWOLoadoutSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UWWOLoadoutSubsystem>() : nullptr;
}

void UWWOLoadoutSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadAllData();
	LoadMods();
	EquippedAbilities.SetNum(AbilitySlots);
	if (!LoadSave())
	{
		LoadDefaultLoadout();
	}
	if (!Flora && Floras.Num() > 0) Flora = Floras[0];
	FitFlora(); // drop a saved grenade/ability that isn't part of this flora
	for (const FName& Slot : FrameSlots())
	{
		if (!EquippedFrame.Contains(Slot))
		{
			const TArray<UWWOFramePartData*> Options = FramePartsFor(Slot);
			if (Options.Num() > 0) EquippedFrame.Add(Slot, Options[0]); // the Standard one
		}
	}
	if (!Weapon && Weapons.Num() > 0) Weapon = Weapons[0];
	FillFloraSlots();
	UE_LOG(LogWWO, Log, TEXT("Loadout: %d weapons, %d grenades, %d abilities, %d frame parts, %d floras, %d traits, %d weapon mods, %d configs"),
		Weapons.Num(), Grenades.Num(), Abilities.Num(), FrameParts.Num(), Floras.Num(), Traits.Num(), WeaponMods.Num(), Configs.Num());
}

void UWWOLoadoutSubsystem::LoadAllData()
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
		if (UWWOSoldierData* S = Cast<UWWOSoldierData>(Enemy)) SoldierData = S;
		if (UWWOSwarmData* S = Cast<UWWOSwarmData>(Enemy)) SwarmData = S;
		if (UWWOTankData* T = Cast<UWWOTankData>(Enemy)) TankData = T;
	}
	TArray<TObjectPtr<UWWOAllyData>> Allies;
	LoadType(FPrimaryAssetType("WWOAlly"), Allies);
	if (Allies.Num() > 0) AllyData = Allies[0];

	// Missing assets (first run before Tools/create_assets.py): fall back to code defaults.
	if (!SoldierData) SoldierData = NewObject<UWWOSoldierData>(this);
	if (!SwarmData) SwarmData = NewObject<UWWOSwarmData>(this);
	if (!TankData) TankData = NewObject<UWWOTankData>(this);
	if (!AllyData) AllyData = NewObject<UWWOAllyData>(this);
	if (Weapons.Num() == 0)
	{
		UWWOWeaponData* Rifle = NewObject<UWWOWeaponData>(this);
		Rifle->Id = "rifle";
		Rifle->DisplayName = TEXT("Rifle");
		Rifle->Archetype = TEXT("Auto rifle");
		Weapons.Add(Rifle);
	}
}

// --- Picker lists ---------------------------------------------------------------------

TArray<UWWOGrenadeData*> UWWOLoadoutSubsystem::GrenadesForFlora() const
{
	TArray<UWWOGrenadeData*> Out;
	for (UWWOGrenadeData* G : Grenades)
	{
		if (!Flora || G->Flora == Flora->Id) Out.Add(G);
	}
	return Out;
}

TArray<UWWOAbilityData*> UWWOLoadoutSubsystem::AbilitiesForFlora() const
{
	TArray<UWWOAbilityData*> Out;
	for (UWWOAbilityData* A : Abilities)
	{
		if (!Flora || A->Flora == Flora->Id) Out.Add(A);
	}
	return Out;
}

TArray<UWWOTraitData*> UWWOLoadoutSubsystem::TraitsForFlora() const
{
	TArray<UWWOTraitData*> Out;
	for (UWWOTraitData* T : Traits)
	{
		if (!Flora || T->Flora == Flora->Id) Out.Add(T);
	}
	return Out;
}

TArray<UWWOFramePartData*> UWWOLoadoutSubsystem::FramePartsFor(FName Slot) const
{
	TArray<UWWOFramePartData*> Out;
	for (UWWOFramePartData* P : FrameParts)
	{
		if (P->Slot == Slot) Out.Add(P);
	}
	return Out;
}

TArray<UWWOTraitData*> UWWOLoadoutSubsystem::EquippedTraits() const
{
	TArray<UWWOTraitData*> Out;
	for (UWWOTraitData* T : EquippedTraitList) Out.Add(T);
	return Out;
}

int32 UWWOLoadoutSubsystem::AbilitySlotOf(const UWWOAbilityData* Choice) const
{
	for (int32 i = 0; i < EquippedAbilities.Num(); ++i)
	{
		if (EquippedAbilities[i] == Choice) return i;
	}
	return INDEX_NONE;
}

UWWOFramePartData* UWWOLoadoutSubsystem::GetFramePart(FName Slot) const
{
	const TObjectPtr<UWWOFramePartData>* Found = EquippedFrame.Find(Slot);
	return Found ? Found->Get() : nullptr;
}

// --- Equipping --------------------------------------------------------------------------

void UWWOLoadoutSubsystem::EquipFlora(UWWOFloraData* Choice)
{
	// Switching flora swaps in that flora's first grenade and abilities and clears traits.
	if (Choice == Flora) return;
	Flora = Choice;
	Grenade = nullptr;
	EquippedAbilities.Init(nullptr, AbilitySlots);
	EquippedTraitList.Reset();
	FillFloraSlots();
	Changed();
}

void UWWOLoadoutSubsystem::EquipGrenade(UWWOGrenadeData* Choice)
{
	Grenade = Choice;
	Changed();
}

void UWWOLoadoutSubsystem::EquipWeapon(UWWOWeaponData* Choice)
{
	Weapon = Choice;
	Changed();
}

void UWWOLoadoutSubsystem::CycleWeapon(int32 Step)
{
	TArray<UWWOWeaponData*> Unlocked;
	for (UWWOWeaponData* W : Weapons)
	{
		if (W->bUnlocked) Unlocked.Add(W);
	}
	if (Unlocked.Num() > 1)
	{
		const int32 Index = Unlocked.IndexOfByKey(Weapon);
		EquipWeapon(Unlocked[(Index + Step + Unlocked.Num()) % Unlocked.Num()]);
	}
}

void UWWOLoadoutSubsystem::EquipFramePart(UWWOFramePartData* Part)
{
	EquippedFrame.Add(Part->Slot, Part);
	Changed();
}

void UWWOLoadoutSubsystem::EquipAbility(int32 Slot, UWWOAbilityData* Choice)
{
	// If it's already in the other slot, the two swap places.
	const int32 Other = AbilitySlotOf(Choice);
	if (Other != INDEX_NONE && Other != Slot)
	{
		EquippedAbilities[Other] = EquippedAbilities[Slot];
	}
	EquippedAbilities[Slot] = Choice;
	Changed();
}

bool UWWOLoadoutSubsystem::ToggleTrait(UWWOTraitData* Choice)
{
	if (EquippedTraitList.Contains(Choice))
	{
		EquippedTraitList.Remove(Choice);
	}
	else if (EquippedTraitList.Num() >= TraitSlots || (Flora && Choice->Flora != Flora->Id))
	{
		return false;
	}
	else
	{
		EquippedTraitList.Add(Choice);
	}
	Changed();
	return true;
}

float UWWOLoadoutSubsystem::TraitBonus(FName Effect) const
{
	float Total = 0.f;
	for (const UWWOTraitData* T : EquippedTraitList)
	{
		if (T->Effect == Effect) Total += T->Amount;
	}
	return Total;
}

float UWWOLoadoutSubsystem::Bonus(const UObject* WorldContext, FName Effect)
{
	const UWWOLoadoutSubsystem* Loadout = Get(WorldContext);
	return Loadout ? Loadout->TraitBonus(Effect) : 0.f;
}

void UWWOLoadoutSubsystem::FitFlora()
{
	if (!Flora) return;
	if (Grenade && Grenade->Flora != Flora->Id) Grenade = nullptr;
	for (int32 Slot = 0; Slot < AbilitySlots; ++Slot)
	{
		const UWWOAbilityData* A = EquippedAbilities[Slot];
		if (A && (A->Flora != Flora->Id || !A->bUnlocked)) EquippedAbilities[Slot] = nullptr;
	}
	EquippedTraitList.RemoveAll([this](const TObjectPtr<UWWOTraitData>& T) { return T->Flora != Flora->Id; });
}

void UWWOLoadoutSubsystem::FillFloraSlots()
{
	const TArray<UWWOGrenadeData*> FloraGrenades = GrenadesForFlora();
	if (!Grenade && FloraGrenades.Num() > 0) Grenade = FloraGrenades[0];
	for (int32 Slot = 0; Slot < AbilitySlots; ++Slot)
	{
		if (EquippedAbilities[Slot]) continue;
		for (UWWOAbilityData* Option : AbilitiesForFlora())
		{
			if (Option->bUnlocked && !EquippedAbilities.Contains(Option))
			{
				EquippedAbilities[Slot] = Option;
				break;
			}
		}
	}
}

void UWWOLoadoutSubsystem::Changed()
{
	Save();
	OnChanged.Broadcast();
}

// --- Saving -------------------------------------------------------------------------------

void UWWOLoadoutSubsystem::Save() const
{
	UWWOLoadoutSave* Data = Cast<UWWOLoadoutSave>(UGameplayStatics::CreateSaveGameObject(UWWOLoadoutSave::StaticClass()));
	Data->Flora = Flora ? Flora->Id : NAME_None;
	Data->Weapon = Weapon ? Weapon->Id : NAME_None;
	Data->Grenade = Grenade ? Grenade->Id : NAME_None;
	for (const UWWOAbilityData* A : EquippedAbilities) Data->Abilities.Add(A ? A->Id : NAME_None);
	for (const UWWOTraitData* T : EquippedTraitList) Data->Traits.Add(T->Id);
	for (const auto& Pair : EquippedFrame) Data->Frame.Add(Pair.Key, Pair.Value->Id);
	for (const auto& Frame : EquippedMods)
	{
		for (const auto& Pair : Frame.Value)
		{
			if (Pair.Value) Data->WeaponMods.Add(FName(Frame.Key.ToString() + TEXT(":") + Pair.Key.ToString()), Pair.Value->Id);
		}
	}
	Data->ModConfigs = Configs;
	UGameplayStatics::SaveGameToSlot(Data, SaveSlot, 0);
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
	Flora = FindById(Floras, Data->Flora);
	Weapon = FindById(Weapons, Data->Weapon);
	Grenade = FindById(Grenades, Data->Grenade);
	for (const FName& Id : Data->Traits)
	{
		UWWOTraitData* T = FindById(Traits, Id);
		if (T && !EquippedTraitList.Contains(T) && EquippedTraitList.Num() < TraitSlots) EquippedTraitList.Add(T);
	}
	for (const auto& Pair : Data->Frame)
	{
		UWWOFramePartData* Part = FindById(FrameParts, Pair.Value);
		if (Part && Part->Slot == Pair.Key) EquippedFrame.Add(Pair.Key, Part);
	}
	for (const auto& Pair : Data->WeaponMods)
	{
		FString WeaponId, ModSlot;
		if (!Pair.Key.ToString().Split(TEXT(":"), &WeaponId, &ModSlot)) continue;
		UWWOModData* Mod = FindById(WeaponMods, Pair.Value);
		if (Mod && Mod->Frame == FName(WeaponId) && Mod->Slot == FName(ModSlot)) EquippedMods.FindOrAdd(Mod->Frame).Add(Mod->Slot, Mod);
	}
	Configs = Data->ModConfigs;
	for (int32 Slot = 0; Slot < FMath::Min(Data->Abilities.Num(), AbilitySlots); ++Slot)
	{
		UWWOAbilityData* A = FindById(Abilities, Data->Abilities[Slot]);
		if (A && !EquippedAbilities.Contains(A)) EquippedAbilities[Slot] = A;
	}
}

void UWWOLoadoutSubsystem::LoadDefaultLoadout()
{
	// First run: start from Config/DefaultLoadout.json (the loadout carried over from the
	// Godot prototype), in the same format Godot's loadout.json used.
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectConfigDir() / TEXT("DefaultLoadout.json")))) return;
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()) return;
	UWWOLoadoutSave* Data = NewObject<UWWOLoadoutSave>();
	Data->Flora = FName(Json->GetStringField(TEXT("flora")));
	Data->Weapon = FName(Json->GetStringField(TEXT("weapon")));
	Data->Grenade = FName(Json->GetStringField(TEXT("grenade")));
	for (const TSharedPtr<FJsonValue>& V : Json->GetArrayField(TEXT("abilities"))) Data->Abilities.Add(FName(V->AsString()));
	for (const TSharedPtr<FJsonValue>& V : Json->GetArrayField(TEXT("traits"))) Data->Traits.Add(FName(V->AsString()));
	const TSharedPtr<FJsonObject>* Frame;
	if (Json->TryGetObjectField(TEXT("frame"), Frame))
	{
		for (const auto& Pair : (*Frame)->Values) Data->Frame.Add(FName(*Pair.Key), FName(Pair.Value->AsString()));
	}
	ApplySave(Data);
}

// --- Modifiers ----------------------------------------------------------------------------

const TArray<FName>& UWWOLoadoutSubsystem::GroupOrder()
{
	static const TArray<FName> Order = { "agility", "aerial", "dexterity", "durability" };
	return Order;
}

const TArray<FName>& UWWOLoadoutSubsystem::FrameSlots()
{
	static const TArray<FName> Slots = { "head", "arms", "legs", "core" };
	return Slots;
}

const TArray<FName>& UWWOLoadoutSubsystem::StatsInGroup(FName Group)
{
	static const TMap<FName, TArray<FName>> Groups = {
		{ "agility", { "walk_speed", "sprint_speed", "crouch_speed", "slide_speed", "slide_distance", "melee_time", "throw_distance" } },
		{ "aerial", { "jump_height", "air_control" } },
		{ "dexterity", { "reload_time", "ads_time", "swap_time", "ready_time" } },
		{ "durability", { "max_health", "max_shield", "recharge_delay" } },
	};
	static const TArray<FName> Empty;
	const TArray<FName>* Found = Groups.Find(Group);
	return Found ? *Found : Empty;
}

FString UWWOLoadoutSubsystem::GroupTitle(FName Group)
{
	FString S = Group.ToString();
	return S.Left(1).ToUpper() + S.Mid(1);
}

namespace
{
	const TArray<TPair<FName, FString>>& StatNames()
	{
		static const TArray<TPair<FName, FString>> Names = {
			{ "walk_speed", TEXT("Walk speed") }, { "sprint_speed", TEXT("Sprint speed") },
			{ "crouch_speed", TEXT("Crouch speed") }, { "slide_speed", TEXT("Slide speed") },
			{ "slide_distance", TEXT("Slide distance") }, { "melee_time", TEXT("Melee time") },
			{ "throw_distance", TEXT("Throw distance") }, { "jump_height", TEXT("Jump height") },
			{ "air_control", TEXT("Air control") }, { "reload_time", TEXT("Reload time") },
			{ "ads_time", TEXT("Aim-in time") }, { "swap_time", TEXT("Swap time") },
			{ "ready_time", TEXT("Handling (ready time)") }, { "max_health", TEXT("Health") },
			{ "max_shield", TEXT("Shields") }, { "recharge_delay", TEXT("Recovery delay") },
			{ "shield_regen", TEXT("Shield refill speed") }, { "melee_damage", TEXT("Melee damage") },
			{ "grenade_recharge", TEXT("Grenade recharge time") },
			// Weapon mod stats (read off the weapon, not through Apply's group points).
			{ "range", TEXT("Range") }, { "recoil_side", TEXT("Side recoil") }, { "recoil_up", TEXT("Vertical recoil") },
			{ "magazine_size", TEXT("Magazine size") }, { "ads_move_speed", TEXT("Move speed while aiming") },
			{ "sway", TEXT("Sway") }, { "zoom", TEXT("Zoom") }, { "damage", TEXT("Damage") },
			{ "fire_rate", TEXT("Fire rate") }, { "crit_multiplier", TEXT("Crit multiplier") },
		};
		return Names;
	}

	// For these, lower is better, so a point shortens them.
	bool LowerIsBetter(FName Stat)
	{
		static const TSet<FName> Lower = { "melee_time", "reload_time", "ads_time", "swap_time", "ready_time", "recharge_delay" };
		return Lower.Contains(Stat);
	}
}

FString UWWOLoadoutSubsystem::StatName(FName Stat)
{
	for (const auto& Pair : StatNames())
	{
		if (Pair.Key == Stat) return Pair.Value;
	}
	return Stat.ToString();
}

FName UWWOLoadoutSubsystem::GroupOf(FName Stat)
{
	for (const FName& Group : GroupOrder())
	{
		if (StatsInGroup(Group).Contains(Stat)) return Group;
	}
	return NAME_None;
}

float UWWOLoadoutSubsystem::GroupPoints(FName Group) const
{
	float Total = 0.f;
	for (const FName& Slot : FrameSlots())
	{
		if (const UWWOFramePartData* Part = GetFramePart(Slot)) Total += Part->Points(Group);
	}
	return Total;
}

float UWWOLoadoutSubsystem::Apply(FName Stat, float Base) const
{
	float Flat = 0.f;
	float Percent = 0.f;
	float Multiply = 1.f;
	const FName Group = GroupOf(Stat);
	if (!Group.IsNone())
	{
		Percent += GroupPoints(Group) * PointValue * (LowerIsBetter(Stat) ? -1.f : 1.f);
	}
	for (const UWWOTraitData* T : EquippedTraitList)
	{
		for (const FWWOStatModifier& Mod : T->Mods)
		{
			if (Mod.Stat != Stat) continue;
			switch (Mod.Op)
			{
			case EWWOStatOp::Flat: Flat += Mod.Value; break;
			case EWWOStatOp::PercentAdd: Percent += Mod.Value; break;
			case EWWOStatOp::Multiply: Multiply *= Mod.Value; break;
			}
		}
	}
	Percent += ModPercent(Stat);
	return (Base + Flat) * FMath::Max(1.f + Percent, 0.f) * Multiply;
}

TArray<FString> UWWOLoadoutSubsystem::GroupSummary() const
{
	TArray<FString> Lines;
	for (const FName& Group : GroupOrder())
	{
		const float Pts = GroupPoints(Group);
		if (FMath::Abs(Pts) < 0.01f) continue;
		Lines.Add(FString::Printf(TEXT("%s %+d  (each stat %+d%%)"), *GroupTitle(Group), FMath::RoundToInt(Pts),
			FMath::RoundToInt(Pts * PointValue * 100.f)));
	}
	return Lines;
}

TArray<FString> UWWOLoadoutSubsystem::NetChanges() const
{
	TArray<FString> Lines;
	for (const auto& Pair : StatNames())
	{
		const float Scale = Apply(Pair.Key, 1.f) - Apply(Pair.Key, 0.f);
		const float Flat = Apply(Pair.Key, 0.f) / FMath::Max(Scale, 0.001f);
		if (FMath::Abs(Scale - 1.f) < 0.001f && FMath::Abs(Flat) < 0.001f) continue;
		FString Line = Pair.Value;
		if (FMath::Abs(Flat) >= 0.001f) Line += FString::Printf(TEXT(" %+.2f"), Flat);
		if (FMath::Abs(Scale - 1.f) >= 0.001f) Line += FString::Printf(TEXT(" x%.2f"), Scale);
		Lines.Add(Line);
	}
	return Lines;
}

// --- Weapon mods (draft) --------------------------------------------------------------------

void UWWOLoadoutSubsystem::LoadMods()
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectConfigDir() / TEXT("WeaponMods.json")))) return;
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
	{
		UE_LOG(LogWWO, Warning, TEXT("Config/WeaponMods.json isn't valid JSON"));
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>* List;
	if (!Json->TryGetArrayField(TEXT("mods"), List)) return;
	int32 Order = 0;
	for (const TSharedPtr<FJsonValue>& V : *List)
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		if (!O.IsValid()) continue;
		UWWOModData* Mod = NewObject<UWWOModData>(this);
		Mod->Id = FName(O->GetStringField(TEXT("id")));
		Mod->DisplayName = O->GetStringField(TEXT("name"));
		Mod->Frame = FName(O->GetStringField(TEXT("frame")));
		Mod->Slot = FName(O->GetStringField(TEXT("slot")));
		Mod->Rarity = O->HasField(TEXT("rarity")) ? (int32)O->GetNumberField(TEXT("rarity")) : 1;
		Mod->Order = Order++;
		O->TryGetStringField(TEXT("perk_text"), Mod->PerkText);
		FString Perk;
		if (O->TryGetStringField(TEXT("perk"), Perk)) Mod->Perk = FName(Perk);
		const TSharedPtr<FJsonObject>* Stats;
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
		const TArray<TSharedPtr<FJsonValue>>* Parts;
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

const TArray<FName>& UWWOLoadoutSubsystem::ModSlots()
{
	static const TArray<FName> Slots = { "barrel", "magazine", "stock", "sight", "chambering", "grip" };
	return Slots;
}

FString UWWOLoadoutSubsystem::ModSlotTitle(FName Slot)
{
	FString S = Slot.ToString();
	return S.Left(1).ToUpper() + S.Mid(1);
}

bool UWWOLoadoutSubsystem::HasMods(const UWWOWeaponData* W) const
{
	if (!W) return false;
	for (const UWWOModData* Mod : WeaponMods)
	{
		if (Mod->Frame == W->Id) return true;
	}
	return false;
}

TArray<UWWOModData*> UWWOLoadoutSubsystem::ModsFor(FName Slot) const
{
	TArray<UWWOModData*> Out;
	for (UWWOModData* Mod : WeaponMods)
	{
		if (Weapon && Mod->Frame == Weapon->Id && Mod->Slot == Slot) Out.Add(Mod);
	}
	return Out;
}

UWWOModData* UWWOLoadoutSubsystem::GetMod(FName Slot) const
{
	if (!Weapon) return nullptr;
	const TMap<FName, TObjectPtr<UWWOModData>>* Frame = EquippedMods.Find(Weapon->Id);
	const TObjectPtr<UWWOModData>* Found = Frame ? Frame->Find(Slot) : nullptr;
	return Found ? Found->Get() : nullptr;
}

void UWWOLoadoutSubsystem::EquipMod(FName Slot, UWWOModData* Mod)
{
	if (!Weapon) return;
	TMap<FName, TObjectPtr<UWWOModData>>& Frame = EquippedMods.FindOrAdd(Weapon->Id);
	if (Mod && (Mod->Frame != Weapon->Id || Mod->Slot != Slot)) return;
	if (Mod) Frame.Add(Slot, Mod);
	else Frame.Remove(Slot);
	Changed();
}

bool UWWOLoadoutSubsystem::HasPerk(FName Perk) const
{
	if (!Weapon) return false;
	if (const TMap<FName, TObjectPtr<UWWOModData>>* Frame = EquippedMods.Find(Weapon->Id))
	{
		for (const auto& Pair : *Frame)
		{
			if (Pair.Value && Pair.Value->Perk == Perk) return true;
		}
	}
	return false;
}

float UWWOLoadoutSubsystem::ModPercent(FName Stat) const
{
	float Total = 0.f;
	if (!Weapon) return Total;
	if (const TMap<FName, TObjectPtr<UWWOModData>>* Frame = EquippedMods.Find(Weapon->Id))
	{
		for (const auto& Pair : *Frame)
		{
			if (!Pair.Value) continue;
			for (const FWWOStatModifier& Mod : Pair.Value->Mods)
			{
				if (Mod.Stat == Stat && Mod.Op == EWWOStatOp::PercentAdd) Total += Mod.Value;
			}
		}
	}
	return Total;
}

TMap<FName, FName> UWWOLoadoutSubsystem::ModIds(FName WeaponId) const
{
	TMap<FName, FName> Ids;
	if (const TMap<FName, TObjectPtr<UWWOModData>>* Frame = EquippedMods.Find(WeaponId))
	{
		for (const auto& Pair : *Frame)
		{
			if (Pair.Value) Ids.Add(Pair.Key, Pair.Value->Id);
		}
	}
	return Ids;
}

void UWWOLoadoutSubsystem::SetModIds(FName WeaponId, const TMap<FName, FName>& Ids)
{
	TMap<FName, TObjectPtr<UWWOModData>>& Frame = EquippedMods.FindOrAdd(WeaponId);
	Frame.Reset();
	for (const auto& Pair : Ids)
	{
		UWWOModData* Mod = FindById(WeaponMods, Pair.Value);
		if (Mod && Mod->Frame == WeaponId && Mod->Slot == Pair.Key) Frame.Add(Pair.Key, Mod);
	}
}

void UWWOLoadoutSubsystem::SaveConfig(int32 Index)
{
	if (!Weapon) return;
	FWWOModConfig Config;
	Config.Weapon = Weapon->Id;
	Config.Mods = ModIds(Weapon->Id);
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
	Changed();
}

void UWWOLoadoutSubsystem::ApplyConfig(int32 Index)
{
	if (!Configs.IsValidIndex(Index)) return;
	const FWWOModConfig& Config = Configs[Index];
	if (UWWOWeaponData* W = FindById(Weapons, Config.Weapon)) Weapon = W;
	SetModIds(Config.Weapon, Config.Mods);
	ActiveConfig = Index;
	Changed();
}

int32 UWWOLoadoutSubsystem::CycleConfig()
{
	if (Configs.Num() == 0) return INDEX_NONE;
	const int32 Next = Configs.IsValidIndex(ActiveConfig) ? (ActiveConfig + 1) % Configs.Num() : 0;
	ApplyConfig(Next);
	return Next;
}

void UWWOLoadoutSubsystem::DeleteConfig(int32 Index)
{
	if (!Configs.IsValidIndex(Index)) return;
	Configs.RemoveAt(Index);
	if (ActiveConfig == Index) ActiveConfig = INDEX_NONE;
	else if (ActiveConfig > Index) --ActiveConfig;
	Changed();
}

FString UWWOLoadoutSubsystem::ConfigSummary(const FWWOModConfig& Config, const UWWOLoadoutSubsystem* L)
{
	TArray<FString> Names;
	for (const FName& Slot : ModSlots())
	{
		const FName* Id = Config.Mods.Find(Slot);
		const UWWOModData* Mod = Id && L ? FindById(L->WeaponMods, *Id) : nullptr;
		if (Mod) Names.Add(Mod->DisplayName);
	}
	return Names.Num() > 0 ? FString::Join(Names, TEXT(", ")) : FString(TEXT("No mods"));
}
