#include "UI/WWOCharacterMenu.h"
#include "UI/WWOCharacterMenuPrivate.h"
#include "UI/WWOStatBars.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOPlayerController.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

using namespace WWOCharacterMenuUI;

void UWWOCharacterMenu::BuildWeaponRows()
{
	// The whole Weapon tab on one screen, no scrolling: four columns across the full width.
	// Weapons | the frame's stat bars, perks under them | its six mod slots | saved configs, or
	// the hovered slot's mods in their place (BuildModRows fills the last three).
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L || !L->GetWeapon()) return;
	UWidgetTree* Tree = WidgetTree;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
	UHorizontalBox* Columns = Tree->ConstructWidget<UHorizontalBox>();
	ModRowsStart = Columns;

	UVerticalBox* Guns = Tree->ConstructWidget<UVerticalBox>();
	Guns->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, TEXT("Weapon"), 13.f, FLinearColor(1, 1, 1, 0.6f), 220))->SetPadding(FMargin(0, 0, 0, 12.f * K));
	const FLinearColor OnEdge(0.9f, 0.9f, 0.95f);
	for (UWWOWeaponData* Option : L->Weapons)
	{
		if (!Option->bUnlocked) continue;
		const bool bOn = Option == L->GetWeapon();
		const FString Fire = Option->BurstCount() > 1 ? FString::Printf(TEXT("%d-round burst"), Option->BurstCount()) : FString(Option->IsSemiAuto() ? TEXT("semi") : TEXT("auto"));
		const FString Dmg = Option->Pellets > 1.f ? FString::Printf(TEXT("%d×%d dmg"), FMath::RoundToInt(Option->Pellets), FMath::RoundToInt(Option->Damage))
			: FString::Printf(TEXT("%d dmg"), FMath::RoundToInt(Option->Damage));
		const FString Stats = FString::Printf(TEXT("%d rpm %s · %s · %d rds"), FMath::RoundToInt(Option->RoundsPerMinute()), *Fire, *Dmg, FMath::RoundToInt(Option->MagazineSize));
		UWidget* B = Slot(Option->DisplayName + (bOn ? TEXT("  ✓") : TEXT("")) + TEXT("\n") + Stats, FVector2D(WeaponColumn, 62), bOn ? FLinearColor(0.24f, 0.25f, 0.3f) : FLinearColor(0.2f, 0.21f, 0.26f), 15.f,
			[WeakThis, WL, Option] { if (WL.IsValid()) WL->EquipWeapon(Option); if (WeakThis.IsValid()) WeakThis->Refresh(); }, bOn ? &OnEdge : nullptr, 3.f);
		Guns->AddChildToVerticalBox(B)->SetPadding(FMargin(0, 0, 0, 8.f * K));
	}
	USizeBox* GunsBox = Tree->ConstructWidget<USizeBox>();
	GunsBox->SetWidthOverride(WeaponColumn * K);
	GunsBox->AddChild(Guns);
	Columns->AddChildToHorizontalBox(GunsBox);
	BuildModRows(Columns);
	AddContent(Columns);
}

namespace
{
	// Rarity colors (draft): common gray up to legendary gold.
	FLinearColor RarityColor(int32 Rarity)
	{
		switch (Rarity)
		{
		case 2: return FLinearColor(0.35f, 0.75f, 0.4f);
		case 3: return FLinearColor(0.35f, 0.6f, 1.f);
		case 4: return FLinearColor(0.7f, 0.45f, 1.f);
		case 5: return FLinearColor(1.f, 0.75f, 0.3f);
		default: return FLinearColor(0.75f, 0.75f, 0.78f);
		}
	}
}

namespace
{
	// One stat bar: the mod stat it reads, whether lower is better, and how to show the value.
	struct FWWOBarDef
	{
		const TCHAR* Label;
		FName Stat;
		bool bLowerBetter;
		bool bThroughLoadout; // a time the loadout's Apply() scales (shell parts, traits, mods)
		int32 Decimals;
		const TCHAR* Suffix;
	};

	const TArray<FWWOBarDef>& BarDefs()
	{
		static const TArray<FWWOBarDef> Defs = {
			{ TEXT("Damage"), "damage", false, false, 1, TEXT("") },
			{ TEXT("Fire rate"), "fire_rate", false, false, 0, TEXT(" rpm") },
			{ TEXT("Range"), "range", false, false, 0, TEXT(" m") },
			{ TEXT("Magazine"), "magazine_size", false, false, 0, TEXT("") },
			{ TEXT("Reload speed"), "reload_time", true, true, 2, TEXT(" s") },
			{ TEXT("Aim-in speed"), "ads_time", true, true, 2, TEXT(" s") },
			{ TEXT("Handling"), "swap_time", true, true, 2, TEXT(" s") },
			{ TEXT("Stability"), "recoil_up", true, false, 2, TEXT("°") },
			{ TEXT("Side stability"), "recoil_side", true, false, 2, TEXT("°") },
			{ TEXT("Zoom"), "zoom", false, false, 2, TEXT("x") },
			{ TEXT("Steadiness"), "sway", true, false, 2, TEXT("") },
			{ TEXT("Aim move speed"), "ads_move_speed", false, false, 0, TEXT("%") },
		};
		return Defs;
	}

	float SumPercent(const TMap<FName, UWWOModData*>& Set, FName Stat)
	{
		float Total = 0.f;
		for (const auto& Pair : Set)
		{
			if (!Pair.Value) continue;
			for (const FWWOStatModifier& M : Pair.Value->Mods)
			{
				if (M.Stat == Stat && M.Op == EWWOStatOp::PercentAdd) Total += M.Value;
			}
		}
		return Total;
	}
}

void UWWOCharacterMenu::BuildModRows(UHorizontalBox* Columns)
{
	// Draft weapon mods: the stat bars with the equipped perks under them, every slot at once, and
	// the saved configs. While the cursor is over a slot, that slot's mods take the configs' place
	// (see UpdateModHover).
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	SlotButtons.Reset();
	PopupButtons.Reset();
	PopupPanel = nullptr;
	if (!L || !L->HasMods(L->GetWeapon())) return;
	UWidgetTree* Tree = WidgetTree;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
	const FLinearColor OnEdge(0.9f, 0.9f, 0.95f);
	const FLinearColor LabelColor(1, 1, 1, 0.6f);
	auto Column = [&](float Width, float Gap) -> UVerticalBox*
	{
		UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>();
		USizeBox* Size = Tree->ConstructWidget<USizeBox>();
		Size->SetWidthOverride(Width * K);
		Size->AddChild(Box);
		Columns->AddChildToHorizontalBox(Size)->SetPadding(FMargin(Gap * K, 0, 0, 0));
		return Box;
	};
	auto Label = [&](UVerticalBox* To, const FString& Text)
	{
		const float Above = To->GetChildrenCount() > 0 ? 18.f * K : 0.f; // read before the label is added
		To->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, Text, 13.f, LabelColor, 220))->SetPadding(FMargin(0, Above, 0, 12.f * K));
	};

	// Stat bars, and what the equipped mods' perks do.
	UVerticalBox* Stats = Column(StatsColumn, 24.f);
	Label(Stats, L->GetWeapon()->DisplayName + TEXT(" frame"));
	StatBars = Tree->ConstructWidget<UWWOStatBars>(UWWOStatBars::StaticClass());
	USizeBox* BarsBox = Tree->ConstructWidget<USizeBox>();
	BarsBox->SetWidthOverride(StatsColumn * K);
	BarsBox->SetHeightOverride(UWWOStatBars::RowHeight * BarDefs().Num() * K);
	BarsBox->AddChild(StatBars);
	Stats->AddChildToVerticalBox(BarsBox);
	UpdateStatBars(nullptr, NAME_None, false);
	Label(Stats, TEXT("Perks"));
	TArray<FString> Perks;
	for (const FName& ModSlot : UWWOLoadoutSubsystem::ModSlots())
	{
		const UWWOModData* Mod = L->GetMod(ModSlot);
		if (Mod && !Mod->PerkText.IsEmpty()) Perks.Add(Mod->PerkText);
	}
	UTextBlock* PerkText = WWOUI::Text(WidgetTree, Perks.Num() > 0 ? FString::Join(Perks, TEXT("\n")) : TEXT("No perks equipped"), 14.f, FLinearColor(0.6f, 0.85f, 1.f), true);
	PerkText->SetWrapTextAt(StatsColumn * K);
	Stats->AddChildToVerticalBox(PerkText);

	// Every slot, one column.
	UVerticalBox* Slots = Column(SlotColumn, 18.f);
	Label(Slots, TEXT("Mods"));
	for (const FName& ModSlot : UWWOLoadoutSubsystem::ModSlots())
	{
		const UWWOModData* Mod = L->GetMod(ModSlot);
		const FLinearColor Edge = Mod ? RarityColor(Mod->Rarity) : FLinearColor(0.4f, 0.42f, 0.5f);
		const bool bOpenSlot = OpenSlot == ModSlot;
		UWidget* W = Slot(UWWOLoadoutSubsystem::ModSlotTitle(ModSlot) + TEXT("\n") + (Mod ? Mod->DisplayName : FString(TEXT("Empty"))),
			FVector2D(SlotColumn, ModSlotHeight - 8.f), Mod ? WWO::Darkened(Edge, 0.65f) : WWOUI::ButtonColor, 16.f,
			[WeakThis, ModSlot] { if (WeakThis.IsValid()) WeakThis->OpenModSlot(ModSlot); }, bOpenSlot ? &OnEdge : &Edge, bOpenSlot ? 5.f : 2.f);
		if (USizeBox* Box = Cast<USizeBox>(W)) SlotButtons.Add({ Cast<UButton>(Box->GetChildAt(0)), ModSlot });
		Slots->AddChildToVerticalBox(W)->SetPadding(FMargin(0, 0, 0, 8.f * K));
	}

	UVerticalBox* Side = Column(SideColumn, 22.f);
	if (!OpenSlot.IsNone())
	{
		// The open slot's mods, lined up with the slot (a row up, and never past the column's foot).
		const int32 SlotIndex = UWWOLoadoutSubsystem::ModSlots().IndexOfByKey(OpenSlot);
		Label(Side, UWWOLoadoutSubsystem::ModSlotTitle(OpenSlot) + TEXT(" mods"));
		UBorder* Panel = Tree->ConstructWidget<UBorder>();
		Panel->SetBrush(WWOUI::RoundBrush(FLinearColor(0.12f, 0.13f, 0.16f), 8.f, FLinearColor(0.55f, 0.58f, 0.66f), 2.f));
		Panel->SetPadding(FMargin(12.f * K, 12.f * K, 12.f * K, 4.f * K));
		UVerticalBox* List = Tree->ConstructWidget<UVerticalBox>();
		const UWWOModData* Current = L->GetMod(OpenSlot);
		const FName ThisSlot = OpenSlot;
		TArray<UWWOModData*> Options = L->ModsFor(OpenSlot);
		Options.Insert(nullptr, 0); // Empty
		for (UWWOModData* Mod : Options)
		{
			const bool bOn = Mod == Current;
			const FLinearColor Base = Mod ? RarityColor(Mod->Rarity) : FLinearColor(0.4f, 0.42f, 0.5f);
			const FString Title = (Mod ? Mod->DisplayName : FString(TEXT("Empty"))) + (bOn ? TEXT("   ✓ equipped") : TEXT(""));
			const FString Body = Mod ? Mod->Summary() : FString(TEXT("No changes"));
			UWidget* W = Slot(Title + TEXT("\n") + Body, FVector2D(SideColumn - 28.f, 84), Mod ? WWO::Darkened(Base, 0.7f) : WWOUI::ButtonColor, 15.f,
				[WeakThis, WL, ThisSlot, Mod] { if (WL.IsValid()) WL->EquipMod(ThisSlot, Mod); if (WeakThis.IsValid()) WeakThis->Refresh(); },
				bOn ? &OnEdge : &Base, bOn ? 5.f : 2.f, FString(), true);
			if (USizeBox* Box = Cast<USizeBox>(W)) PopupButtons.Add({ Cast<UButton>(Box->GetChildAt(0)), Mod });
			List->AddChildToVerticalBox(W)->SetPadding(FMargin(0, 0, 0, 8.f * K));
		}
		Panel->SetContent(List);
		PopupPanel = Panel;
		Side->AddChildToVerticalBox(Panel)->SetPadding(FMargin(0, FMath::Clamp(SlotIndex - 1.f, 0.f, 1.6f) * ModSlotHeight * K, 0, 0));
		return;
	}

	// Saved configs: save the current mods as a new one; click one to apply it, or save over or
	// delete it. The list alone scrolls if there are more than fit (about eight).
	Label(Side, TEXT("Configs"));
	Side->AddChildToVerticalBox(Slot(TEXT("Save current as new config"), FVector2D(SideColumn, 44), FLinearColor(0.2f, 0.3f, 0.24f), 16.f,
		[WeakThis, WL] { if (WL.IsValid()) WL->SaveConfig(INDEX_NONE); if (WeakThis.IsValid()) WeakThis->Refresh(); }));
	UScrollBox* ConfigList = Tree->ConstructWidget<UScrollBox>();
	const TArray<FWWOModConfig>& Configs = L->GetConfigs();
	constexpr float SaveOverWidth = 92.f, DeleteWidth = 72.f, ButtonGap = 6.f;
	for (int32 i = 0; i < Configs.Num(); ++i)
	{
		UHorizontalBox* CRow = Tree->ConstructWidget<UHorizontalBox>();
		const bool bActive = L->GetActiveConfig() == i;
		const UWWOWeaponData* ConfigGun = nullptr;
		for (const UWWOWeaponData* W : L->Weapons) if (W && W->Id == Configs[i].Weapon) ConfigGun = W;
		const FString Name = Configs[i].Name + TEXT(" · ") + (ConfigGun ? ConfigGun->DisplayName : Configs[i].Weapon.ToString()) + (bActive ? TEXT("  ✓") : TEXT(""));
		CRow->AddChildToHorizontalBox(Slot(Name + TEXT("\n") + UWWOLoadoutSubsystem::ConfigSummary(Configs[i], L),
			FVector2D(SideColumn - SaveOverWidth - DeleteWidth - ButtonGap * 2.f - 14.f, 58), FLinearColor(0.2f, 0.21f, 0.26f), 13.f,
			[WeakThis, WL, i] { if (WL.IsValid()) WL->ApplyConfig(i); if (WeakThis.IsValid()) WeakThis->Refresh(); },
			bActive ? &OnEdge : nullptr, 2.f, TEXT("Apply"), true));
		CRow->AddChildToHorizontalBox(Slot(TEXT("Save over"), FVector2D(SaveOverWidth, 58), FLinearColor(0.3f, 0.27f, 0.18f), 13.f,
			[WeakThis, WL, i] { if (WL.IsValid()) WL->SaveConfig(i); if (WeakThis.IsValid()) WeakThis->Refresh(); }))->SetPadding(FMargin(ButtonGap * K, 0, 0, 0));
		CRow->AddChildToHorizontalBox(Slot(TEXT("Delete"), FVector2D(DeleteWidth, 58), FLinearColor(0.32f, 0.17f, 0.17f), 13.f,
			[WeakThis, WL, i] { if (WL.IsValid()) WL->DeleteConfig(i); if (WeakThis.IsValid()) WeakThis->Refresh(); }))->SetPadding(FMargin(ButtonGap * K, 0, 0, 0));
		if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(ConfigList->AddChild(CRow))) RowSlot->SetPadding(FMargin(0, 0, 0, 8.f * K));
	}
	USizeBox* ListBox = Tree->ConstructWidget<USizeBox>();
	ListBox->SetMaxDesiredHeight(ConfigListHeight * K);
	ListBox->AddChild(ConfigList);
	Side->AddChildToVerticalBox(ListBox)->SetPadding(FMargin(0, 10.f * K, 0, 0));
	Side->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, FString::Printf(TEXT("In game: %s swaps to your other weapon slot's config"),
		Cast<AWWOPlayerController>(GetOwningPlayer()) ? *Cast<AWWOPlayerController>(GetOwningPlayer())->BindHint("next_config") : TEXT("the swap key")), 10.f, FLinearColor(1, 1, 1, 0.4f), 80, true))->SetPadding(FMargin(0, 6.f * K, 0, 0));
}

void UWWOCharacterMenu::OpenModSlot(FName ModSlot)
{
	if (OpenSlot == ModSlot) return;
	OpenSlot = ModSlot;
	ModClose.Reset();
	Refresh();
}

void UWWOCharacterMenu::UpdateStatBars(const UWWOModData* PreviewMod, FName PreviewSlot, bool bPreview)
{
	// Values with the equipped mods, and with PreviewMod in PreviewSlot instead (null = empty).
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	const UWWOWeaponData* W = L ? L->GetWeapon() : nullptr;
	if (!StatBars || !W) return;
	TMap<FName, UWWOModData*> Now;
	for (const FName& ModSlot : UWWOLoadoutSubsystem::ModSlots()) Now.Add(ModSlot, L->GetMod(ModSlot));
	TMap<FName, UWWOModData*> Then = Now;
	if (bPreview) Then.Add(PreviewSlot, const_cast<UWWOModData*>(PreviewMod));
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwningPlayerPawn());
	const float SwapBase = P && P->Weapon ? P->Weapon->SwapTime : 0.35f;

	auto Base = [W, SwapBase](FName Stat) -> float
	{
		if (Stat == "damage") return W->Damage;
		if (Stat == "fire_rate") return W->RoundsPerMinute();
		if (Stat == "range") return W->FireRange;
		if (Stat == "magazine_size") return W->MagazineSize;
		if (Stat == "reload_time") return W->ReloadTime;
		if (Stat == "ads_time") return W->AdsTime;
		if (Stat == "swap_time") return SwapBase;
		if (Stat == "recoil_up") return W->RecoilUp;
		if (Stat == "recoil_side") return W->RecoilSide;
		if (Stat == "zoom") return W->Zoom;
		if (Stat == "sway") return W->Sway;
		if (Stat == "ads_move_speed") return W->AdsMoveSpeed;
		return 1.f;
	};
	auto Value = [L, &Base](const FWWOBarDef& D, const TMap<FName, UWWOModData*>& Set) -> float
	{
		const float B = Base(D.Stat);
		const float Pct = SumPercent(Set, D.Stat);
		if (D.Stat == "zoom") return 1.f + (B - 1.f) * FMath::Max(1.f + Pct, 0.f);
		if (D.Stat == "magazine_size") return FMath::Max(1.f, FMath::RoundToFloat(B * FMath::Max(1.f + Pct, 0.f)));
		if (D.bThroughLoadout)
		{
			// Apply() = base * (1 + shell parts + traits + equipped mods): swap the mods' share.
			return FMath::Max(L->Apply(D.Stat, B) + B * (Pct - L->ModPercent(D.Stat)), 0.f);
		}
		return B * FMath::Max(1.f + Pct, 0.f);
	};
	const TMap<FName, UWWOModData*> NoMods;
	StatBars->Rows.Reset();
	for (const FWWOBarDef& D : BarDefs())
	{
		const float Bare = FMath::Max(Value(D, NoMods), 0.0001f);
		auto Fill = [&D, Bare](float V) { return 0.5f * (D.bLowerBetter ? Bare / FMath::Max(V, 0.0001f) : V / Bare); };
		auto Shown = [&D](float V) { return FString::Printf(TEXT("%.*f%s"), D.Decimals, D.Stat == FName("ads_move_speed") ? V * 100.f : V, D.Suffix); };
		const float A = Value(D, Now), B = Value(D, Then);
		FWWOStatBar R;
		R.Label = D.Label;
		R.Fill = Fill(A);
		R.PreviewFill = Fill(B);
		R.Value = Shown(A);
		R.PreviewValue = Shown(B);
		R.bPreview = bPreview;
		StatBars->Rows.Add(R);
	}
}

void UWWOCharacterMenu::UpdateModHover(float DeltaTime)
{
	// Hovering a slot opens its popup; the popup stays while the cursor is on it or a slot, and
	// closes a moment after it leaves both. Hovering a mod in the popup previews it on the bars.
	bool bOnSlot = false;
	for (const auto& Pair : SlotButtons)
	{
		if (Pair.Key.IsValid() && Pair.Key->IsHovered() && AutoTestPreview.IsNone())
		{
			bOnSlot = true;
			if (Pair.Value != OpenSlot)
			{
				OpenModSlot(Pair.Value);
				return; // rebuilt
			}
		}
	}
	const bool bOnPopup = PopupPanel && PopupPanel->IsHovered();
	// (The autotest's preview holds it open.)
	if (ModClose.ShouldClose(!OpenSlot.IsNone() && AutoTestPreview.IsNone(), bOnSlot || bOnPopup, DeltaTime))
	{
		OpenSlot = NAME_None;
		Refresh();
		return;
	}
	const UWWOModData* Hovered = nullptr;
	bool bPreview = false;
	for (const auto& Pair : PopupButtons)
	{
		if (Pair.Key.IsValid() && Pair.Key->IsHovered())
		{
			Hovered = Pair.Value.Get();
			bPreview = true;
		}
	}
	if (!AutoTestPreview.IsNone())
	{
		// The autotest previews a mod by id ("empty" for an empty slot).
		for (const auto& Pair : PopupButtons)
		{
			if ((Pair.Value.IsValid() && Pair.Value->Id == AutoTestPreview) || (!Pair.Value.IsValid() && AutoTestPreview == "empty"))
			{
				Hovered = Pair.Value.Get();
				bPreview = true;
			}
		}
	}
	UpdateStatBars(Hovered, OpenSlot, bPreview);
}
