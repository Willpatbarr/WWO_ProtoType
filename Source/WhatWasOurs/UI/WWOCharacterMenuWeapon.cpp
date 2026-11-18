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
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "UI/WWOGunPreviewActor.h"
#include "Engine/TextureRenderTarget2D.h"

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

	// The weapons as cards, and under them the selected gun itself (AWWOGunPreviewActor's render).
	UVerticalBox* Guns = Tree->ConstructWidget<UVerticalBox>();
	Guns->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, TEXT("Weapon"), WWOUI::Type::Caption, WWOUI::TextSecondary, 220))->SetPadding(FMargin(0, 0, 0, 8.f * K));
	for (UWWOWeaponData* Option : L->Catalog.Weapons)
	{
		if (!Option->bUnlocked) continue;
		const FString Fire = Option->BurstCount() > 1 ? FString::Printf(TEXT("%d-round burst"), Option->BurstCount()) : FString(Option->IsSemiAuto() ? TEXT("semi") : TEXT("auto"));
		const FString Dmg = Option->Pellets > 1.f ? FString::Printf(TEXT("%d×%d dmg"), FMath::RoundToInt(Option->Pellets), FMath::RoundToInt(Option->Damage))
			: FString::Printf(TEXT("%d dmg"), FMath::RoundToInt(Option->Damage));
		FItemCard C;
		C.Title = Option->DisplayName;
		C.Sub = FString::Printf(TEXT("%d rpm %s · %s · %d rds"), FMath::RoundToInt(Option->RoundsPerMinute()), *Fire, *Dmg, FMath::RoundToInt(Option->MagazineSize));
		C.bOn = Option == L->GetWeapon();
		C.MinHeight = 54.f;
		Guns->AddChildToVerticalBox(ItemCard(C, [WeakThis, WL, Option] { if (WL.IsValid()) WL->EquipWeapon(Option); if (WeakThis.IsValid()) WeakThis->Refresh(); }))
			->SetPadding(FMargin(0, 0, 0, 8.f * K));
	}
	if (GunPreview && GunPreview->Target)
	{
		// The gun on a well in the page's shade, which is also the render's backdrop: no seam round it.
		UImage* Image = Tree->ConstructWidget<UImage>();
		FSlateBrush Brush;
		Brush.SetResourceObject(GunPreview->Target);
		Brush.ImageSize = AWWOGunPreviewActor::Size * K;
		Image->SetBrush(Brush);
		UScaleBox* Fit = Tree->ConstructWidget<UScaleBox>();
		Fit->SetStretch(EStretch::ScaleToFit);
		Fit->AddChild(Image);
		UOverlay* Stage = Tree->ConstructWidget<UOverlay>();
		UOverlaySlot* FitSlot = Stage->AddChildToOverlay(Fit);
		FitSlot->SetHorizontalAlignment(HAlign_Fill);
		FitSlot->SetVerticalAlignment(VAlign_Fill);
		UTextBlock* Name = WWOUI::Spaced(WidgetTree, L->GetWeapon()->DisplayName, WWOUI::Type::Caption, WWOUI::TextSecondary, 220);
		UOverlaySlot* NameSlot = Stage->AddChildToOverlay(Name);
		NameSlot->SetHorizontalAlignment(HAlign_Left);
		NameSlot->SetVerticalAlignment(VAlign_Bottom);
		NameSlot->SetPadding(FMargin(12.f * K, 0, 0, 10.f * K));
		UBorder* Well = Tree->ConstructWidget<UBorder>();
		Well->SetBrush(WWOUI::RoundBrush(WWOUI::PageColor, WWOUI::Corner, WWOUI::PanelEdge, WWOUI::BoxEdge));
		Well->SetPadding(FMargin(WWOUI::BoxEdge * K));
		Well->SetContent(Stage);
		USizeBox* WellBox = Tree->ConstructWidget<USizeBox>();
		WellBox->SetHeightOverride((AWWOGunPreviewActor::Size.Y + 2.f * WWOUI::BoxEdge) * K);
		WellBox->AddChild(Well);
		Guns->AddChildToVerticalBox(WellBox)->SetPadding(FMargin(0, 8.f * K, 0, 0));
		// Rebuilt only when the gun or its mods change (this page is rebuilt on every hover of a slot).
		FString Key = L->GetWeapon()->Id.ToString();
		for (const FName& ModSlot : UWWOLoadoutSubsystem::ModSlots()) if (const UWWOModData* Mod = L->GetMod(ModSlot)) Key += TEXT("|") + Mod->Id.ToString();
		if (Key != GunKey)
		{
			GunKey = Key;
			GunPreview->ShowGun(L->GetWeapon(), L);
		}
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
	ConfigListBox = nullptr;
	if (!L || !L->HasMods(L->GetWeapon())) return;
	UWidgetTree* Tree = WidgetTree;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
	const FLinearColor LabelColor = WWOUI::TextSecondary;
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
		To->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, Text, WWOUI::Type::Caption, LabelColor, 220))->SetPadding(FMargin(0, Above, 0, 8.f * K));
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
	UTextBlock* PerkText = WWOUI::Text(WidgetTree, Perks.Num() > 0 ? FString::Join(Perks, TEXT("\n")) : TEXT("No perks equipped"), WWOUI::Type::Small, FLinearColor(0.6f, 0.85f, 1.f), true);
	PerkText->SetWrapTextAt(StatsColumn * K);
	Stats->AddChildToVerticalBox(PerkText);

	// Every slot, one column.
	UVerticalBox* Slots = Column(SlotColumn, 18.f);
	Label(Slots, TEXT("Mods"));
	for (const FName& ModSlot : UWWOLoadoutSubsystem::ModSlots())
	{
		const UWWOModData* Mod = L->GetMod(ModSlot);
		FItemCard C;
		C.Color = Mod ? RarityColor(Mod->Rarity) : FLinearColor(0, 0, 0, 0);
		C.Title = Mod ? Mod->DisplayName : FString(TEXT("Empty"));
		C.Sub = UWWOLoadoutSubsystem::ModSlotTitle(ModSlot);
		C.bOn = OpenSlot == ModSlot;
		C.bMark = false;
		C.MinHeight = ModSlotHeight - 8.f;
		UWidget* W = ItemCard(C, [WeakThis, ModSlot] { if (WeakThis.IsValid()) WeakThis->OpenModSlot(ModSlot); });
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
		Panel->SetBrush(WWOUI::RoundBrush(WWOUI::PageColor, WWOUI::Corner, WWOUI::PanelEdge, WWOUI::BoxEdge));
		Panel->SetPadding(FMargin(12.f * K, 12.f * K, 12.f * K, 4.f * K));
		UVerticalBox* List = Tree->ConstructWidget<UVerticalBox>();
		const UWWOModData* Current = L->GetMod(OpenSlot);
		const FName ThisSlot = OpenSlot;
		TArray<UWWOModData*> Options = L->ModsFor(OpenSlot);
		Options.Insert(nullptr, 0); // Empty
		for (UWWOModData* Mod : Options)
		{
			FItemCard C;
			C.Color = Mod ? RarityColor(Mod->Rarity) : FLinearColor(0, 0, 0, 0);
			C.Title = Mod ? Mod->DisplayName : FString(TEXT("Empty"));
			C.Sub = Mod ? Mod->Summary() : FString(TEXT("No changes"));
			C.bOn = Mod == Current;
			C.MinHeight = 64.f;
			UWidget* W = ItemCard(C, [WeakThis, WL, ThisSlot, Mod] { if (WL.IsValid()) WL->EquipMod(ThisSlot, Mod); if (WeakThis.IsValid()) WeakThis->Refresh(); });
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
	Side->AddChildToVerticalBox(Slot(TEXT("Save current as new config"), FVector2D(SideColumn, 44), FLinearColor(0.2f, 0.3f, 0.24f), WWOUI::Type::Body,
		[WeakThis, WL] { if (WL.IsValid()) WL->SaveConfig(INDEX_NONE); if (WeakThis.IsValid()) WeakThis->Refresh(); }));
	UScrollBox* ConfigList = WWOUI::ScrollBox(Tree);
	const TArray<FWWOModConfig>& Configs = L->GetConfigs();
	constexpr float SaveOverWidth = 92.f, DeleteWidth = 72.f, ButtonGap = 6.f;
	for (int32 i = 0; i < Configs.Num(); ++i)
	{
		UHorizontalBox* CRow = Tree->ConstructWidget<UHorizontalBox>();
		const bool bActive = L->GetActiveConfig() == i;
		const UWWOWeaponData* ConfigGun = nullptr;
		for (const UWWOWeaponData* W : L->Catalog.Weapons) if (W && W->Id == Configs[i].Weapon) ConfigGun = W;
		FItemCard C;
		C.Title = Configs[i].Name + TEXT(" \u00B7 ") + (ConfigGun ? ConfigGun->DisplayName : Configs[i].Weapon.ToString());
		C.Sub = UWWOLoadoutSubsystem::ConfigSummary(Configs[i], L);
		C.bOn = bActive;
		C.MinHeight = 58.f;
		C.Tip = TEXT("Apply");
		UHorizontalBoxSlot* ConfigSlot = CRow->AddChildToHorizontalBox(ItemCard(C, [WeakThis, WL, i] { if (WL.IsValid()) WL->ApplyConfig(i); if (WeakThis.IsValid()) WeakThis->Refresh(); }));
		ConfigSlot->SetSize(WWOUI::Fill(1.f));
		CRow->AddChildToHorizontalBox(Slot(TEXT("Save over"), FVector2D(SaveOverWidth, 58), FLinearColor(0.3f, 0.27f, 0.18f), WWOUI::Type::Small,
			[WeakThis, WL, i] { if (WL.IsValid()) WL->SaveConfig(i); if (WeakThis.IsValid()) WeakThis->Refresh(); }))->SetPadding(FMargin(ButtonGap * K, 0, 0, 0));
		CRow->AddChildToHorizontalBox(Slot(TEXT("Delete"), FVector2D(DeleteWidth, 58), FLinearColor(0.32f, 0.17f, 0.17f), WWOUI::Type::Small,
			[WeakThis, WL, i] { if (WL.IsValid()) WL->DeleteConfig(i); if (WeakThis.IsValid()) WeakThis->Refresh(); }))->SetPadding(FMargin(ButtonGap * K, 0, 0, 0));
		if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(ConfigList->AddChild(CRow))) RowSlot->SetPadding(FMargin(0, 0, 0, 8.f * K));
	}
	USizeBox* ListBox = Tree->ConstructWidget<USizeBox>();
	ListBox->SetMaxDesiredHeight(ConfigListHeight * K);
	ListBox->AddChild(ConfigList);
	ConfigListBox = ListBox;
	Side->AddChildToVerticalBox(ListBox)->SetPadding(FMargin(0, 10.f * K, 0, 0));
	Side->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, FString::Printf(TEXT("In game: %s swaps to your other weapon slot's config"),
		Cast<AWWOPlayerController>(GetOwningPlayer()) ? *Cast<AWWOPlayerController>(GetOwningPlayer())->BindHint("next_config") : TEXT("the swap key")), WWOUI::Type::Caption, WWOUI::TextFaint, 80, true))->SetPadding(FMargin(0, 8.f * K, 0, 0));
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
