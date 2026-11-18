// Regression scenarios: the loadout subsystem on its own (no shots). What the catalog lists (counts and
// ids, in order, and every picker list), what equipping does (swaps, refusals, the flora's kit), every
// stat the stat maths gives for a set of fixed loadouts (bare, each flora's kit, every trait slot filled,
// non-standard frame parts, mods in every slot), the saved configs, and what the equipped state serialises
// to: an md5 of the save bytes (UGameplayStatics::SaveGameToMemory of what Save writes, from the class name on) and of the loadout
// sent to the host (FWWONetLoadout, binary). loadout.file.* read a saved file (the owner's Loadout.sav as
// of 2026-11-16, kept in Tools/regress/fixtures) and Config/DefaultLoadout.json into a copy and save
// them back, so the saved format is checked against a real file. Nothing is written to disk.

#include "Player/WWORegress.h"
#include "Player/WWOCharacter.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/MemoryWriter.h"

namespace
{
	template <typename T>
	FString Ids(const TArray<T*>& List)
	{
		if (List.Num() == 0) return TEXT("-");
		return FString::JoinBy(List, TEXT(","), [](const T* X) { return X ? X->Id.ToString() : FString(TEXT("null")); });
	}

	template <typename T>
	FString Ids(const TArray<TObjectPtr<T>>& List)
	{
		TArray<T*> Raw;
		for (T* X : List) Raw.Add(X);
		return Ids(Raw);
	}

	FString Names(const TArray<FName>& List)
	{
		return List.Num() == 0 ? FString(TEXT("-")) : FString::JoinBy(List, TEXT(","), [](const FName& N) { return N.ToString(); });
	}

	// The loadout's listeners (the body's grafts, the gun) muted while a scenario equips: what equipping
	// rebuilds isn't what these record, and the next scenarios start from the baseline anyway.
	struct FMuted
	{
		UWWOLoadoutSubsystem* Lo;
		FWWOOnLoadoutChanged Saved;
		explicit FMuted(UWWOLoadoutSubsystem* In) : Lo(In), Saved(MoveTemp(In->OnChanged)) { In->OnChanged.Clear(); }
		~FMuted() { Lo->OnChanged = MoveTemp(Saved); }
	};

	FString Md5(const TArray<uint8>& Bytes) { return FMD5::HashBytes(Bytes.GetData(), Bytes.Num()); }

	// A save's bytes from its class name on: the header before it lists the engine's custom versions
	// registered in the process writing it (which modules have loaded), not anything of the loadout's.
	TArray<uint8> SaveBody(const TArray<uint8>& Bytes)
	{
		const char Key[] = "/Script/WhatWasOurs.";
		const int32 KeyLen = UE_ARRAY_COUNT(Key) - 1;
		for (int32 i = 0; i + KeyLen <= Bytes.Num(); ++i)
		{
			if (FMemory::Memcmp(Bytes.GetData() + i, Key, KeyLen) == 0) return TArray<uint8>(Bytes.GetData() + i, Bytes.Num() - i);
		}
		return Bytes;
	}

	FString SaveBytesMd5(USaveGame* Data, int32* OutSize = nullptr)
	{
		TArray<uint8> Bytes;
		if (!Data || !UGameplayStatics::SaveGameToMemory(Data, Bytes)) return TEXT("failed");
		const TArray<uint8> Body = SaveBody(Bytes);
		if (OutSize) *OutSize = Body.Num();
		return Md5(Body);
	}

	FString NetText(const FWWONetLoadout& N)
	{
		TArray<FString> Frame, Mods;
		for (int32 i = 0; i < N.FrameSlots.Num(); ++i) Frame.Add(N.FrameSlots[i].ToString() + TEXT(":") + (N.FrameParts.IsValidIndex(i) ? N.FrameParts[i].ToString() : TEXT("?")));
		for (int32 i = 0; i < N.ModKeys.Num(); ++i) Mods.Add(N.ModKeys[i].ToString() + TEXT("=") + (N.ModIds.IsValidIndex(i) ? N.ModIds[i].ToString() : TEXT("?")));
		return FString::Printf(TEXT("%s|%s|%s|%s|%s|%s|%s"), *N.Flora.ToString(), *N.Weapon.ToString(), *N.Grenade.ToString(), *Names(N.Abilities),
			*Names(N.Traits), Frame.Num() ? *FString::Join(Frame, TEXT(",")) : TEXT("-"), Mods.Num() ? *FString::Join(Mods, TEXT(",")) : TEXT("-"));
	}

	FString NetMd5(const FWWONetLoadout& N)
	{
		TArray<uint8> Bytes;
		FMemoryWriter Ar(Bytes);
		FWWONetLoadout Copy = N;
		FWWONetLoadout::StaticStruct()->SerializeBin(Ar, &Copy);
		return Md5(Bytes);
	}

	FString SaveText(const UWWOLoadoutSave* D)
	{
		if (!D) return TEXT("none");
		TArray<FString> Frame, Mods, Configs;
		for (const auto& Pair : D->Frame) Frame.Add(Pair.Key.ToString() + TEXT(":") + Pair.Value.ToString());
		for (const auto& Pair : D->WeaponMods) Mods.Add(Pair.Key.ToString() + TEXT("=") + Pair.Value.ToString());
		for (const FWWOModConfig& C : D->ModConfigs)
		{
			TArray<FString> M;
			for (const auto& Pair : C.Mods) M.Add(Pair.Key.ToString() + TEXT(":") + Pair.Value.ToString());
			Configs.Add(C.Name.Replace(TEXT(" "), TEXT("_")) + TEXT("/") + C.Weapon.ToString() + TEXT("/") + FString::Join(M, TEXT("+")));
		}
		TArray<FString> Slots;
		for (int32 V : D->WeaponSlots) Slots.Add(FString::FromInt(V));
		return FString::Printf(TEXT("%s|%s|%s|%s|%s|%s|%s|%s|%s"), *D->Flora.ToString(), *D->Weapon.ToString(), *D->Grenade.ToString(),
			*Names(D->Abilities), *Names(D->Traits), Frame.Num() ? *FString::Join(Frame, TEXT(",")) : TEXT("-"),
			Mods.Num() ? *FString::Join(Mods, TEXT(",")) : TEXT("-"), Configs.Num() ? *FString::Join(Configs, TEXT(";")) : TEXT("-"),
			Slots.Num() ? *FString::Join(Slots, TEXT(",")) : TEXT("-"));
	}

	// Every stat key the stat maths knows of (the group stats, the other named ones, the weapon mod stats),
	// then any other stat a trait or mod in the catalog names, in catalog order.
	TArray<FName> AllStats(const UWWOLoadoutSubsystem& L)
	{
		TArray<FName> Stats;
		for (const FName& Group : UWWOLoadoutSubsystem::GroupOrder()) for (const FName& Stat : UWWOLoadoutSubsystem::StatsInGroup(Group)) Stats.AddUnique(Stat);
		for (const TCHAR* Extra : { TEXT("shield_regen"), TEXT("melee_damage"), TEXT("grenade_recharge"), TEXT("range"), TEXT("recoil_side"),
			TEXT("recoil_up"), TEXT("magazine_size"), TEXT("ads_move_speed"), TEXT("sway"), TEXT("zoom"), TEXT("damage"), TEXT("fire_rate"),
			TEXT("crit_multiplier"), TEXT("pellet_spread"), TEXT("hip_spread"), TEXT("not_a_stat") }) Stats.AddUnique(FName(Extra));
		for (const UWWOTraitData* T : L.Catalog.Traits) if (T) for (const FWWOStatModifier& M : T->Mods) Stats.AddUnique(M.Stat);
		for (const UWWOModData* Mod : L.Catalog.WeaponMods) if (Mod) for (const FWWOStatModifier& M : Mod->Mods) Stats.AddUnique(M.Stat);
		return Stats;
	}
}

void FWWORegress::AddLoadoutScenarios(TArray<FWWORegressScenario>& Out, const UWWOLoadoutSubsystem& L)
{
	const TArray<FName> Stats = AllStats(L);
	TArray<FName> Effects, Perks;
	for (const UWWOTraitData* T : L.Catalog.Traits) if (T && !T->Effect.IsNone()) Effects.AddUnique(T->Effect);
	for (const UWWOModData* Mod : L.Catalog.WeaponMods) if (Mod && !Mod->Perk.IsNone()) Perks.AddUnique(Mod->Perk);
	Effects.AddUnique("not_an_effect");
	Perks.AddUnique("not_a_perk");

	// The saved configs and weapon slots are the owner's (the baseline keeps them): a scenario that saves
	// or edits configs works on an empty set and puts the owner's back before it returns.
	struct FConfigStash { TArray<FWWOModConfig> Configs; TArray<int32> Slots; int32 Active = INDEX_NONE, ActiveSlot = 0; };
	auto Stash = [](UWWOLoadoutSubsystem* Lo)
	{
		FConfigStash S{ Lo->ModConfigs.Configs, Lo->ModConfigs.SlotConfigs, Lo->ModConfigs.Active, Lo->ModConfigs.ActiveSlot };
		Lo->ModConfigs.Configs.Reset();
		Lo->ModConfigs.SlotConfigs = { INDEX_NONE, INDEX_NONE };
		Lo->ModConfigs.Active = INDEX_NONE;
		Lo->ModConfigs.ActiveSlot = 0;
		return S;
	};
	auto Unstash = [](UWWOLoadoutSubsystem* Lo, const FConfigStash& S)
	{
		Lo->ModConfigs.Configs = S.Configs;
		Lo->ModConfigs.SlotConfigs = S.Slots;
		Lo->ModConfigs.Active = S.Active;
		Lo->ModConfigs.ActiveSlot = S.ActiveSlot;
	};

	// What is equipped, as ids.
	auto RecordEquipped = [](FWWORegress& R, const UWWOLoadoutSubsystem* Lo, const FString& P)
	{
		R.Record(P + TEXT("flora"), Lo->GetFlora() ? Lo->GetFlora()->Id.ToString() : TEXT("none"));
		R.Record(P + TEXT("weapon"), Lo->GetWeapon() ? Lo->GetWeapon()->Id.ToString() : TEXT("none"));
		R.Record(P + TEXT("grenade"), Lo->GetGrenade() ? Lo->GetGrenade()->Id.ToString() : TEXT("none"));
		TArray<UWWOAbilityData*> Abilities;
		for (int32 i = 0; i < UWWOLoadoutSubsystem::AbilitySlots; ++i) Abilities.Add(Lo->GetAbility(i));
		R.Record(P + TEXT("abilities"), Ids(Abilities));
		R.Record(P + TEXT("traits"), Ids(Lo->EquippedTraits()));
		TArray<FString> Frame;
		for (const FName& Slot : UWWOLoadoutSubsystem::FrameSlots()) Frame.Add(Slot.ToString() + TEXT(":") + (Lo->GetFramePart(Slot) ? Lo->GetFramePart(Slot)->Id.ToString() : TEXT("none")));
		R.Record(P + TEXT("frame"), FString::Join(Frame, TEXT(",")));
		TArray<FString> Mods;
		for (const FName& Slot : UWWOLoadoutSubsystem::ModSlots()) if (const UWWOModData* M = Lo->GetMod(Slot)) Mods.Add(Slot.ToString() + TEXT(":") + M->Id.ToString());
		R.Record(P + TEXT("mods"), Mods.Num() ? FString::Join(Mods, TEXT(",")) : FString(TEXT("-")));
	};

	// Every number the stat maths gives for what is equipped: group points and the summary, every stat
	// through Apply at two bases and through the character's M, the mods' percent, the trait bonuses,
	// the perks; then the save and net payloads (configs set aside).
	auto RecordStats = [Stats, Effects, Perks, RecordEquipped, Stash, Unstash](FWWORegress& R)
	{
		UWWOLoadoutSubsystem* Lo = R.Loadout();
		RecordEquipped(R, Lo, TEXT(""));
		for (const FName& Group : UWWOLoadoutSubsystem::GroupOrder()) R.Record(TEXT("points.") + Group.ToString(), Lo->GroupPoints(Group));
		R.Record(TEXT("summary"), FString::Join(Lo->GroupSummary(), TEXT("|")).Replace(TEXT(" "), TEXT("_")));
		for (const FName& Stat : Stats)
		{
			const FString K = TEXT("stat.") + Stat.ToString();
			R.Record(K + TEXT(".1"), Lo->Apply(Stat, 1.f));
			R.Record(K + TEXT(".base"), Lo->Apply(Stat, 123.456f));
			R.Record(K + TEXT(".m"), R.Player()->M(Stat, 7.3f));
			R.Record(K + TEXT(".mods"), Lo->ModPercent(Stat));
		}
		for (const FName& Effect : Effects) R.Record(TEXT("bonus.") + Effect.ToString(), Lo->TraitBonus(Effect));
		R.Record(TEXT("bonus_via_player"), UWWOLoadoutSubsystem::Bonus(R.Player(), Effects[0]));
		TArray<FName> On;
		for (const FName& Perk : Perks) if (Lo->HasPerk(Perk)) On.Add(Perk);
		R.Record(TEXT("perks"), Names(On));
		const FConfigStash S = Stash(Lo);
		int32 Size = 0;
		R.Record(TEXT("save.md5"), SaveBytesMd5(Lo->MakeSave(), &Size));
		R.Record(TEXT("save.size"), Size);
		R.Record(TEXT("save.text"), SaveText(Lo->MakeSave()));
		Unstash(Lo, S);
		const FWWONetLoadout Net = Lo->ToNet();
		R.Record(TEXT("net.md5"), NetMd5(Net));
		R.Record(TEXT("net.text"), NetText(Net));
		// The host's copy from that payload: the same payload back, the same numbers.
		UWWOLoadoutSubsystem* Copy = UWWOLoadoutSubsystem::NewCopy(Lo);
		Copy->ApplyNet(Net);
		R.RecordBool(TEXT("net.round_trip"), Copy->ToNet() == Net);
		int32 Same = 0;
		for (const FName& Stat : Stats) Same += Copy->Apply(Stat, 123.456f) == Lo->Apply(Stat, 123.456f) ? 1 : 0;
		R.Record(TEXT("net.copy_stats_same"), Same);
	};

	// A fixed loadout: put on (from the baseline) and recorded in the first tick.
	auto Add = [&Out, RecordStats](const FString& Name, TFunction<void(FWWORegress&, UWWOLoadoutSubsystem*)> Put)
	{
		FWWORegressScenario S;
		S.Name = TEXT("loadout.stats.") + Name;
		S.Step = [Put, RecordStats](FWWORegress& R, int32 Tick)
		{
			FMuted Muted(R.Loadout());
			Put(R, R.Loadout());
			RecordStats(R);
			return true;
		};
		Out.Add(S);
	};
	// A flora with its kit, the way the menu gives it (EquipFlora from another flora: its first grenade and abilities).
	auto PutFlora = [](UWWOLoadoutSubsystem* Lo, UWWOFloraData* F)
	{
		Lo->Equipped.Flora = nullptr;
		Lo->EquipFlora(F);
	};
	auto PutTraits = [](UWWOLoadoutSubsystem* Lo)
	{
		for (UWWOTraitData* T : Lo->TraitsForFlora()) if (Lo->EquippedTraits().Num() < UWWOLoadoutSubsystem::TraitSlots) Lo->ToggleTrait(T);
	};
	auto PutFrame = [](UWWOLoadoutSubsystem* Lo, bool bLast)
	{
		for (const FName& Slot : UWWOLoadoutSubsystem::FrameSlots())
		{
			const TArray<UWWOFramePartData*> Options = Lo->FramePartsFor(Slot);
			if (Options.Num() > 0) Lo->EquipFramePart(bLast ? Options.Last() : Options[FMath::Min(1, Options.Num() - 1)]);
		}
	};
	auto PutMods = [](UWWOLoadoutSubsystem* Lo, UWWOWeaponData* W, bool bLast)
	{
		Lo->EquipWeapon(W);
		for (const FName& Slot : UWWOLoadoutSubsystem::ModSlots())
		{
			const TArray<UWWOModData*> Options = Lo->ModsFor(Slot);
			if (Options.Num() > 0) Lo->EquipMod(Slot, bLast ? Options.Last() : Options[0]);
		}
	};

	Add(TEXT("bare"), [](FWWORegress&, UWWOLoadoutSubsystem*) {});
	for (UWWOFloraData* F : L.Catalog.Floras)
	{
		if (!F) continue;
		Add(TEXT("flora.") + F->Id.ToString(), [PutFlora, F](FWWORegress&, UWWOLoadoutSubsystem* Lo) { PutFlora(Lo, F); });
		Add(TEXT("traits.") + F->Id.ToString(), [PutFlora, PutTraits, F](FWWORegress&, UWWOLoadoutSubsystem* Lo) { PutFlora(Lo, F); PutTraits(Lo); });
	}
	Add(TEXT("frame.second"), [PutFrame](FWWORegress&, UWWOLoadoutSubsystem* Lo) { PutFrame(Lo, false); });
	Add(TEXT("frame.last"), [PutFrame](FWWORegress&, UWWOLoadoutSubsystem* Lo) { PutFrame(Lo, true); });
	UWWOWeaponData* FirstModded = nullptr;
	for (UWWOWeaponData* W : L.Catalog.Weapons)
	{
		if (!W || !L.HasMods(W)) continue;
		if (!FirstModded) FirstModded = W;
		Add(TEXT("mods.") + W->Id.ToString() + TEXT(".first"), [PutMods, W](FWWORegress&, UWWOLoadoutSubsystem* Lo) { PutMods(Lo, W, false); });
		Add(TEXT("mods.") + W->Id.ToString() + TEXT(".last"), [PutMods, W](FWWORegress&, UWWOLoadoutSubsystem* Lo) { PutMods(Lo, W, true); });
	}
	// Everything at once: the last flora's kit and traits, the last frame parts, mods in every slot.
	UWWOFloraData* LastFlora = L.Catalog.Floras.Num() > 0 ? L.Catalog.Floras.Last().Get() : nullptr;
	Add(TEXT("everything"), [PutFlora, PutTraits, PutFrame, PutMods, LastFlora, FirstModded](FWWORegress&, UWWOLoadoutSubsystem* Lo)
	{
		if (LastFlora) PutFlora(Lo, LastFlora);
		PutTraits(Lo);
		PutFrame(Lo, true);
		if (FirstModded) PutMods(Lo, FirstModded, true);
	});

	// --- The catalog: every list in order, every picker list ---
	{
		FWWORegressScenario S;
		S.Name = TEXT("loadout.catalog");
		S.Step = [](FWWORegress& R, int32 Tick)
		{
			UWWOLoadoutSubsystem* Lo = R.Loadout();
			R.Record(TEXT("weapons"), Ids(Lo->Catalog.Weapons));
			R.Record(TEXT("grenades"), Ids(Lo->Catalog.Grenades));
			R.Record(TEXT("abilities"), Ids(Lo->Catalog.Abilities));
			R.Record(TEXT("frame_parts"), Ids(Lo->Catalog.FrameParts));
			R.Record(TEXT("floras"), Ids(Lo->Catalog.Floras));
			R.Record(TEXT("traits"), Ids(Lo->Catalog.Traits));
			R.Record(TEXT("mods"), Ids(Lo->Catalog.WeaponMods));
			R.Record(TEXT("counts"), FString::Printf(TEXT("%d,%d,%d,%d,%d,%d,%d"), Lo->Catalog.Weapons.Num(), Lo->Catalog.Grenades.Num(), Lo->Catalog.Abilities.Num(),
				Lo->Catalog.FrameParts.Num(), Lo->Catalog.Floras.Num(), Lo->Catalog.Traits.Num(), Lo->Catalog.WeaponMods.Num()));
			R.Record(TEXT("ally_data"), Lo->Catalog.AllyData ? Lo->Catalog.AllyData->GetName() : FString(TEXT("none")));
			R.Record(TEXT("point_value"), Lo->PointValue);
			TArray<FString> Unlocked;
			for (const UWWOWeaponData* W : Lo->Catalog.Weapons) if (W && W->bUnlocked) Unlocked.Add(W->Id.ToString());
			R.Record(TEXT("weapons_unlocked"), FString::Join(Unlocked, TEXT(",")));
			// The mod data read from Config/WeaponMods.json, one value each.
			for (const UWWOModData* Mod : Lo->Catalog.WeaponMods)
			{
				TArray<FString> M;
				for (const FWWOStatModifier& X : Mod->Mods) M.Add(FString::Printf(TEXT("%s%d%.9g"), *X.Stat.ToString(), (int32)X.Op, X.Value));
				R.Record(TEXT("mod.") + Mod->Id.ToString(), FString::Printf(TEXT("%s/%s/%d/%.9g/%s/%d/%s"), *Mod->Frame.ToString(), *Mod->Slot.ToString(),
					Mod->Rarity, Mod->Order, *Mod->Perk.ToString(), Mod->VisualParts.Num(), *FString::Join(M, TEXT("+"))));
			}
			// Picker lists: per flora (and none), per frame slot, per weapon and mod slot.
			UWWOFloraData* FloraBefore = Lo->Equipped.Flora;
			Lo->Equipped.Flora = nullptr;
			R.Record(TEXT("for.none"), Ids(Lo->GrenadesForFlora()) + TEXT("|") + Ids(Lo->AbilitiesForFlora()) + TEXT("|") + Ids(Lo->TraitsForFlora()));
			for (UWWOFloraData* F : Lo->Catalog.Floras)
			{
				Lo->Equipped.Flora = F;
				R.Record(TEXT("for.") + F->Id.ToString(), Ids(Lo->GrenadesForFlora()) + TEXT("|") + Ids(Lo->AbilitiesForFlora()) + TEXT("|") + Ids(Lo->TraitsForFlora()));
			}
			Lo->Equipped.Flora = FloraBefore;
			for (const FName& Slot : UWWOLoadoutSubsystem::FrameSlots()) R.Record(TEXT("frame.") + Slot.ToString(), Ids(Lo->FramePartsFor(Slot)));
			UWWOWeaponData* WeaponBefore = Lo->Equipped.Weapon;
			for (UWWOWeaponData* W : Lo->Catalog.Weapons)
			{
				Lo->Equipped.Weapon = W;
				TArray<FString> PerSlot;
				for (const FName& Slot : UWWOLoadoutSubsystem::ModSlots()) PerSlot.Add(Ids(Lo->ModsFor(Slot)));
				R.Record(TEXT("modsfor.") + W->Id.ToString(), FString::Printf(TEXT("%d|"), Lo->HasMods(W) ? 1 : 0) + FString::Join(PerSlot, TEXT("|")));
			}
			Lo->Equipped.Weapon = nullptr;
			R.Record(TEXT("modsfor.none"), Ids(Lo->ModsFor("barrel")));
			R.RecordBool(TEXT("hasmods.null"), Lo->HasMods(nullptr));
			Lo->Equipped.Weapon = WeaponBefore;
			// The fixed tables.
			R.Record(TEXT("groups"), Names(UWWOLoadoutSubsystem::GroupOrder()));
			for (const FName& Group : UWWOLoadoutSubsystem::GroupOrder())
			{
				R.Record(TEXT("group.") + Group.ToString(), Names(UWWOLoadoutSubsystem::StatsInGroup(Group)) + TEXT("|") + UWWOLoadoutSubsystem::GroupTitle(Group));
			}
			R.Record(TEXT("group.none"), Names(UWWOLoadoutSubsystem::StatsInGroup("nothing")));
			R.Record(TEXT("frame_slots"), Names(UWWOLoadoutSubsystem::FrameSlots()));
			TArray<FString> Titles;
			for (const FName& Slot : UWWOLoadoutSubsystem::ModSlots()) Titles.Add(UWWOLoadoutSubsystem::ModSlotTitle(Slot));
			R.Record(TEXT("mod_slots"), Names(UWWOLoadoutSubsystem::ModSlots()) + TEXT("|") + FString::Join(Titles, TEXT(",")));
			for (const FName& Stat : AllStats(*Lo))
			{
				R.Record(TEXT("statname.") + Stat.ToString(), UWWOLoadoutSubsystem::StatName(Stat).Replace(TEXT(" "), TEXT("_")) + TEXT("|") + UWWOLoadoutSubsystem::GroupOf(Stat).ToString());
			}
			return true;
		};
		Out.Add(S);
	}

	// --- Equipping: swaps, refusals, the flora's kit, cycling weapons ---
	{
		FWWORegressScenario S;
		S.Name = TEXT("loadout.equip");
		S.Step = [RecordEquipped](FWWORegress& R, int32 Tick)
		{
			UWWOLoadoutSubsystem* Lo = R.Loadout();
			FMuted Muted(Lo);
			UWWOFloraData* F = Lo->Catalog.Floras.Num() > 0 ? Lo->Catalog.Floras[0].Get() : nullptr;
			UWWOFloraData* Other = Lo->Catalog.Floras.Num() > 1 ? Lo->Catalog.Floras[1].Get() : nullptr;
			Lo->Equipped.Flora = nullptr;
			Lo->EquipFlora(F);
			RecordEquipped(R, Lo, TEXT("a."));
			// Ability swap: slot 0's ability put in slot 1 swaps them.
			Lo->EquipAbility(1, Lo->GetAbility(0));
			RecordEquipped(R, Lo, TEXT("b."));
			// Traits: up to the slots, then refused; another flora's refused; one taken off.
			TArray<FString> Toggles;
			for (UWWOTraitData* T : Lo->Catalog.Traits) Toggles.Add(FString::FromInt(Lo->ToggleTrait(T) ? 1 : 0));
			R.Record(TEXT("c.toggles"), FString::Join(Toggles, TEXT("")));
			RecordEquipped(R, Lo, TEXT("c."));
			if (Lo->EquippedTraits().Num() > 0) R.RecordBool(TEXT("d.untoggle"), Lo->ToggleTrait(Lo->EquippedTraits()[0]));
			RecordEquipped(R, Lo, TEXT("d."));
			// Another flora: its kit, traits cleared; the same flora again changes nothing.
			Lo->EquipFlora(Other);
			RecordEquipped(R, Lo, TEXT("e."));
			Lo->EquipGrenade(nullptr);
			Lo->EquipFlora(Other);
			RecordEquipped(R, Lo, TEXT("f."));
			// FitFlora drops what isn't this flora's; FillFloraSlots fills the empty slots.
			if (Lo->Catalog.Grenades.Num() > 0) Lo->Equipped.Grenade = Lo->Catalog.Grenades[0];
			if (Lo->Catalog.Abilities.Num() > 0) Lo->Equipped.Abilities[0] = Lo->Catalog.Abilities[0];
			Lo->Equipped.FitFlora();
			RecordEquipped(R, Lo, TEXT("g."));
			Lo->Equipped.FillFloraSlots(Lo->Catalog);
			RecordEquipped(R, Lo, TEXT("h."));
			// A mod for the wrong slot or frame is refused; null empties the slot.
			UWWOWeaponData* Gun = nullptr;
			for (UWWOWeaponData* W : Lo->Catalog.Weapons) if (W && Lo->HasMods(W)) { Gun = W; break; }
			Lo->EquipWeapon(Gun);
			const TArray<UWWOModData*> Barrels = Lo->ModsFor("barrel");
			if (Barrels.Num() > 0)
			{
				Lo->EquipMod("magazine", Barrels[0]);
				RecordEquipped(R, Lo, TEXT("i."));
				Lo->EquipMod("barrel", Barrels[0]);
				RecordEquipped(R, Lo, TEXT("j."));
				Lo->EquipMod("barrel", nullptr);
				RecordEquipped(R, Lo, TEXT("k."));
				Lo->EquipMod("barrel", Barrels.Last());
			}
			// Each frame keeps its own mods: away and back.
			TArray<FString> Cycle;
			for (int32 i = 0; i < Lo->Catalog.Weapons.Num() + 1; ++i)
			{
				Lo->CycleWeapon(1);
				Cycle.Add(Lo->GetWeapon() ? Lo->GetWeapon()->Id.ToString() : TEXT("none"));
			}
			R.Record(TEXT("l.cycle"), FString::Join(Cycle, TEXT(",")));
			Lo->CycleWeapon(-1);
			R.Record(TEXT("l.back"), Lo->GetWeapon() ? Lo->GetWeapon()->Id.ToString() : TEXT("none"));
			Lo->EquipWeapon(Gun);
			RecordEquipped(R, Lo, TEXT("m."));
			R.Record(TEXT("m.slot_of"), Lo->AbilitySlotOf(Lo->GetAbility(1)));
			R.Record(TEXT("m.slot_of_null"), Lo->AbilitySlotOf(nullptr));
			R.RecordBool(TEXT("m.trait_on"), Lo->EquippedTraits().Num() > 0 && Lo->IsTraitEquipped(Lo->EquippedTraits()[0]));
			R.RecordBool(TEXT("save_due"), Lo->bSaveDue);
			return true;
		};
		Out.Add(S);
	}

	// --- Saved configs and the two weapon slots ---
	{
		FWWORegressScenario S;
		S.Name = TEXT("loadout.configs");
		S.Step = [Stash, Unstash, RecordEquipped](FWWORegress& R, int32 Tick)
		{
			UWWOLoadoutSubsystem* Lo = R.Loadout();
			FMuted Muted(Lo);
			const FConfigStash Before = Stash(Lo);
			TArray<UWWOWeaponData*> Modded;
			for (UWWOWeaponData* W : Lo->Catalog.Weapons) if (W && Lo->HasMods(W)) Modded.Add(W);
			auto Configs = [Lo]()
			{
				TArray<FString> Lines;
				for (const FWWOModConfig& C : Lo->GetConfigs()) Lines.Add(C.Name.Replace(TEXT(" "), TEXT("_")) + TEXT("/") + C.Weapon.ToString() + TEXT("/") + UWWOLoadoutSubsystem::ConfigSummary(C, Lo).Replace(TEXT(" "), TEXT("_")).Replace(TEXT("'"), TEXT("")));
				return Lines.Num() ? FString::Join(Lines, TEXT(";")) : FString(TEXT("-"));
			};
			auto Slots = [Lo]() { return FString::Printf(TEXT("%d,%d active_slot=%d active=%d"), Lo->GetSlotConfig(0), Lo->GetSlotConfig(1), Lo->GetActiveSlot(), Lo->GetActiveConfig()); };
			R.Record(TEXT("a.cycle_empty"), Lo->CycleConfig());
			R.RecordBool(TEXT("a.swap_empty"), Lo->SwapWeaponSlot());
			for (int32 i = 0; i < Modded.Num() && i < 2; ++i)
			{
				Lo->EquipWeapon(Modded[i]);
				const TArray<UWWOModData*> Barrels = Lo->ModsFor("barrel");
				if (Barrels.Num() > 0) Lo->EquipMod("barrel", Barrels[0]);
				Lo->SaveConfig(INDEX_NONE);
				const TArray<UWWOModData*> Sights = Lo->ModsFor("sight");
				if (Sights.Num() > 0) Lo->EquipMod("sight", Sights.Last());
				Lo->SaveConfig(INDEX_NONE);
			}
			R.Record(TEXT("b.configs"), Configs());
			Lo->SaveConfig(0); // overwrite the first with what is on now, keeping its name
			R.Record(TEXT("c.configs"), Configs());
			Lo->SetSlotConfig(0, 0);
			Lo->SetSlotConfig(1, FMath::Min(2, Lo->GetConfigs().Num() - 1));
			Lo->SetSlotConfig(5, 0); // no such slot
			R.Record(TEXT("d.slots"), Slots());
			RecordEquipped(R, Lo, TEXT("d."));
			R.RecordBool(TEXT("e.swap"), Lo->SwapWeaponSlot());
			R.Record(TEXT("e.slots"), Slots());
			RecordEquipped(R, Lo, TEXT("e."));
			R.Record(TEXT("f.cycle"), Lo->CycleConfig());
			R.Record(TEXT("f.cycle2"), Lo->CycleConfig());
			R.Record(TEXT("f.slots"), Slots());
			RecordEquipped(R, Lo, TEXT("f."));
			Lo->DeleteConfig(0);
			R.Record(TEXT("g.configs"), Configs());
			R.Record(TEXT("g.slots"), Slots());
			Lo->DeleteConfig(99);
			Lo->SaveConfig(INDEX_NONE); // takes the lowest free number
			R.Record(TEXT("h.configs"), Configs());
			FWWOModConfig Odd;
			Odd.Mods.Add("barrel", "no_such_mod");
			R.Record(TEXT("h.summary_odd"), UWWOLoadoutSubsystem::ConfigSummary(Odd, Lo).Replace(TEXT(" "), TEXT("_")));
			int32 Size = 0;
			R.Record(TEXT("save.md5"), SaveBytesMd5(Lo->MakeSave(), &Size));
			R.Record(TEXT("save.size"), Size);
			R.Record(TEXT("save.text"), SaveText(Lo->MakeSave()));
			Unstash(Lo, Before);
			return true;
		};
		Out.Add(S);
	}

	// --- Saved files read into a copy and saved back ---
	struct FFile { FString Name; FString Path; bool bJson = false; };
	const TArray<FFile> Files = {
		{ TEXT("owner"), FPaths::ProjectDir() / TEXT("Tools/regress/fixtures/Loadout.sav"), false },
		{ TEXT("default"), TEXT(""), true },
	};
	for (const FFile& File : Files)
	{
		FWWORegressScenario S;
		S.Name = TEXT("loadout.file.") + File.Name;
		S.Step = [File, RecordEquipped](FWWORegress& R, int32 Tick)
		{
			UWWOLoadoutSubsystem* Copy = UWWOLoadoutSubsystem::NewCopy(R.Loadout());
			Copy->Equipped.Abilities.SetNum(UWWOLoadoutSubsystem::AbilitySlots);
			if (File.bJson)
			{
				Copy->LoadDefaultLoadout();
			}
			else
			{
				TArray<uint8> Bytes;
				if (!FFileHelper::LoadFileToArray(Bytes, *File.Path)) { R.Record(TEXT("missing"), File.Path); return true; }
				R.Record(TEXT("file.md5"), Md5(Bytes));
				const UWWOLoadoutSave* Data = Cast<UWWOLoadoutSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
				if (!Data) { R.Record(TEXT("unreadable"), 1); return true; }
				R.Record(TEXT("file.text"), SaveText(Data));
				Copy->ApplySave(Data);
				// Saved back: the same bytes as the file?
				TArray<uint8> Again;
				UGameplayStatics::SaveGameToMemory(Copy->MakeSave(), Again);
				R.RecordBool(TEXT("resave_same_body"), SaveBody(Again) == SaveBody(Bytes));
				R.Record(TEXT("file.body_md5"), Md5(SaveBody(Bytes)));
			}
			RecordEquipped(R, Copy, TEXT("read."));
			R.Record(TEXT("read.configs"), Copy->GetConfigs().Num());
			R.Record(TEXT("read.slots"), FString::Printf(TEXT("%d,%d"), Copy->GetSlotConfig(0), Copy->GetSlotConfig(1)));
			int32 Size = 0;
			R.Record(TEXT("read.save.md5"), SaveBytesMd5(Copy->MakeSave(), &Size));
			R.Record(TEXT("read.save.size"), Size);
			R.Record(TEXT("read.save.text"), SaveText(Copy->MakeSave()));
			R.Record(TEXT("read.net.md5"), NetMd5(Copy->ToNet()));
			R.Record(TEXT("read.net.text"), NetText(Copy->ToNet()));
			// What Initialize does after reading: fit the flora, fill the frame and the flora's slots.
			if (!Copy->Equipped.Flora && Copy->Catalog.Floras.Num() > 0) Copy->Equipped.Flora = Copy->Catalog.Floras[0];
			Copy->Equipped.FitFlora();
			for (const FName& Slot : UWWOLoadoutSubsystem::FrameSlots())
			{
				if (!Copy->GetFramePart(Slot) && Copy->FramePartsFor(Slot).Num() > 0) Copy->Equipped.Frame.Add(Slot, Copy->FramePartsFor(Slot)[0]);
			}
			if (!Copy->Equipped.Weapon && Copy->Catalog.Weapons.Num() > 0) Copy->Equipped.Weapon = Copy->Catalog.Weapons[0];
			Copy->Equipped.FillFloraSlots(Copy->Catalog);
			RecordEquipped(R, Copy, TEXT("fit."));
			R.Record(TEXT("fit.save.md5"), SaveBytesMd5(Copy->MakeSave()));
			R.Record(TEXT("fit.net.md5"), NetMd5(Copy->ToNet()));
			R.Record(TEXT("fit.walk_speed"), Copy->Apply("walk_speed", 4.f));
			R.Record(TEXT("fit.max_health"), Copy->Apply("max_health", 100.f));
			R.Record(TEXT("fit.reload_time"), Copy->Apply("reload_time", 2.1f));
			R.Record(TEXT("fit.range"), Copy->Apply("range", 100.f));
			return true;
		};
		Out.Add(S);
	}
}
