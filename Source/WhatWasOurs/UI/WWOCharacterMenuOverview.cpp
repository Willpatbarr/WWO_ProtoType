#include "UI/WWOCharacterMenu.h"
#include "UI/WWOCharacterMenuPrivate.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/ScaleBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "WWOIds.h"

using namespace WWOCharacterMenuUI;

namespace
{
	// The Loadout tab's hover row of choices (Godot pixels): a square choice's side (and every choice's
	// height), a weapon config's width, a shell part's (its two-stat line whole), the gap between choices.
	constexpr float ChoiceSide = 56.f, WideChoice = 150.f, PartChoice = 162.f, ChoiceGap = 8.f;

	// Up to two initials, standing in for an icon until there is art.
	FString Initials(const FString& Name)
	{
		TArray<FString> Words;
		Name.ParseIntoArray(Words, TEXT(" "));
		FString Out;
		for (const FString& Word : Words) if (Out.Len() < 2 && !Word.IsEmpty()) Out += Word.Left(1).ToUpper();
		return Out;
	}
}

void UWWOCharacterMenu::BuildOverview()
{
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L || !Overview) return;
	UWidgetTree* Tree = WidgetTree;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	CardHandlers.Reset();
	HoverTiles.Reset();
	CloseHoverPicker();
	Overview->ClearChildren();
	const UWWOFloraData* Flora = L->GetFlora();
	const FLinearColor Accent = WWOUI::Accent(this);
	const FLinearColor Dim = WWOUI::DimBright;
	auto Open = [WeakThis](const TCHAR* Tile) { return [WeakThis, Tile] { if (WeakThis.IsValid()) WeakThis->Expand(Tile); }; };
	// An icon's stand-in: the item's initials in a bordered square.
	auto Icon = [&](const FString& Name, float Side, float FontPx, const FLinearColor& Edge) -> UWidget*
	{
		UTextBlock* Text = WWOUI::Spaced(WidgetTree, Initials(Name), FontPx, Edge, 40);
		Text->SetJustification(ETextJustify::Center);
		UBorder* Box = Tree->ConstructWidget<UBorder>();
		Box->SetBrush(WWOUI::RoundBrush(WWOUI::CardColor, WWOUI::Corner, Edge, 1.5f));
		Box->SetHorizontalAlignment(HAlign_Center);
		Box->SetVerticalAlignment(VAlign_Center);
		Box->SetContent(Text);
		USizeBox* Size = Tree->ConstructWidget<USizeBox>();
		Size->SetWidthOverride(Side * K);
		Size->SetHeightOverride(Side * K);
		Size->AddChild(Box);
		return Size;
	};

	// --- Left: the frame's stats, then everything in the build with what it does.
	UVerticalBox* Left = Tree->ConstructWidget<UVerticalBox>();
	static const TCHAR* const Groups[] = { TEXT("agility"), TEXT("dexterity"), TEXT("aerial"), TEXT("durability") };
	StatBars4.Reset();
	NoteRows.Reset();
	OutlinedSource.Reset();
	for (const TCHAR* Group : Groups)
	{
		// White for what you have, a colored piece for what a hovered part adds or takes
		// (UpdateStatPreview), dark for the rest.
		FStatBar Bar;
		Bar.Group = Group;
		UHorizontalBox* Title = Tree->ConstructWidget<UHorizontalBox>();
		Title->AddChildToHorizontalBox(WWOUI::Spaced(WidgetTree, Group, WWOUI::Type::Title, FLinearColor::White, 260))->SetSize(WWOUI::Fill(1.f));
		Bar.Change = WWOUI::Spaced(WidgetTree, TEXT(""), WWOUI::Type::Small, Dim, 60);
		UHorizontalBoxSlot* ChangeSlot = Title->AddChildToHorizontalBox(Bar.Change);
		ChangeSlot->SetVerticalAlignment(VAlign_Bottom);
		ChangeSlot->SetPadding(FMargin(0, 0, 10.f * K, 0));
		Bar.Value = WWOUI::Spaced(WidgetTree, TEXT(""), WWOUI::Type::Small, Dim, 60);
		Title->AddChildToHorizontalBox(Bar.Value)->SetVerticalAlignment(VAlign_Bottom);
		Left->AddChildToVerticalBox(Title)->SetPadding(FMargin(0, Left->GetChildrenCount() > 1 ? 22.f * K : 0.f, 0, 8.f * K));
		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();
		Bar.Have = Tree->ConstructWidget<UBorder>();
		Bar.Have->SetBrushColor(WWO::Lin(FLinearColor(0.93f, 0.93f, 0.95f)));
		Bar.Shift = Tree->ConstructWidget<UBorder>();
		Bar.Rest = Tree->ConstructWidget<UBorder>();
		Bar.Rest->SetBrushColor(WWO::Lin(FLinearColor(0.24f, 0.25f, 0.28f)));
		Row->AddChildToHorizontalBox(Bar.Have);
		Row->AddChildToHorizontalBox(Bar.Shift);
		Row->AddChildToHorizontalBox(Bar.Rest);
		USizeBox* BarBox = Tree->ConstructWidget<USizeBox>();
		BarBox->SetHeightOverride(11.f * K);
		BarBox->AddChild(Row);
		Left->AddChildToVerticalBox(BarBox);
		StatBars4.Add(Bar);
	}
	PreviewPart = nullptr;
	UpdateStatPreview(nullptr, false);
	Left->AddChildToVerticalBox(WWOUI::Rule(Tree, WWOUI::RuleColor))->SetPadding(FMargin(-24.f * K, 38.f * K, 0, 0));
	UHorizontalBox* BuildTitle = Tree->ConstructWidget<UHorizontalBox>();
	BuildTitle->AddChildToHorizontalBox(WWOUI::Spaced(WidgetTree, TEXT("//"), WWOUI::Type::Title, Accent, 0))->SetVerticalAlignment(VAlign_Center);
	BuildTitle->AddChildToHorizontalBox(WWOUI::Spaced(WidgetTree, TEXT("BUILD"), WWOUI::Type::Title, FLinearColor::White, 380))->SetPadding(FMargin(16.f * K, 0, 0, 0));
	Left->AddChildToVerticalBox(BuildTitle)->SetPadding(FMargin(-18.f * K, 16.f * K, 0, 16.f * K));
	Left->AddChildToVerticalBox(WWOUI::Rule(Tree, WWOUI::RuleColor))->SetPadding(FMargin(-24.f * K, 0, 0, 12.f * K));
	// Notes on the build: what it does to things, and what it does for you, each with where it
	// comes from. (What each item is belongs to the Flora tab.)
	UScrollBox* List = WWOUI::ScrollBox(Tree);
	auto Pad = [](UWidget* W, const FMargin& M) { if (UScrollBoxSlot* S = Cast<UScrollBoxSlot>(W->Slot)) S->SetPadding(M); };
	auto Heading = [&](const FString& Text)
	{
		UTextBlock* T = WWOUI::Spaced(WidgetTree, Text, WWOUI::Type::Caption, Accent, 300);
		List->AddChild(T);
		Pad(T, FMargin(0, List->GetChildrenCount() > 1 ? 18.f * K : 2.f * K, 0, 6.f * K));
	};
	auto Note = [&](const FString& What, const FString& From)
	{
		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();
		UHorizontalBoxSlot* WhatSlot = Row->AddChildToHorizontalBox(WWOUI::Spaced(WidgetTree, What, WWOUI::Type::Small, FLinearColor::White, 90, true));
		WhatSlot->SetSize(WWOUI::Fill(1.f));
		WhatSlot->SetVerticalAlignment(VAlign_Center);
		UTextBlock* Source = WWOUI::Spaced(WidgetTree, From, WWOUI::Type::Micro, Dim, 120);
		Source->SetJustification(ETextJustify::Right);
		UHorizontalBoxSlot* SourceSlot = Row->AddChildToHorizontalBox(Source);
		SourceSlot->SetVerticalAlignment(VAlign_Center);
		SourceSlot->SetPadding(FMargin(12.f * K, 0, 4.f * K, 0));
		// In a frame that lights up while the cursor is on the item the note comes from
		// (UpdateNoteOutline). The frame is always there, so nothing moves when it does.
		UBorder* Frame = Tree->ConstructWidget<UBorder>();
		Frame->SetBrush(WWOUI::RoundBrush(FLinearColor(0, 0, 0, 0), WWOUI::Corner));
		Frame->SetPadding(FMargin(8.f * K, 3.f * K, 4.f * K, 3.f * K));
		Frame->SetContent(Row);
		List->AddChild(Frame);
		Pad(Frame, FMargin(-8.f * K, 0, 0, 0));
		if (!From.IsEmpty()) NoteRows.Add({ Frame, From });
	};
	auto Num = [](float V) { return FString::SanitizeFloat(FMath::RoundToFloat(V * 10.f) / 10.f, 0); };
	auto Pct = [](float V) { return FString::Printf(TEXT("%+d%%"), FMath::RoundToInt(V * 100.f)); };
	const TArray<UWWOTraitData*> BuildTraits = L->EquippedTraits();
	Heading(TEXT("Effects you apply"));
	int32 Effects = 0;
	if (const UWWOGrenadeData* G = L->GetGrenade())
	{
		FString What;
		switch (G->Kind)
		{
		case EWWOGrenadeKind::Pool: What = FString::Printf(TEXT("Damage pool · %s/s for %s s"), *Num(G->PoolDps), *Num(G->PoolTime)); break;
		case EWWOGrenadeKind::Spike: What = FString::Printf(TEXT("Poison · %s/s for %s s"), *Num(G->PoisonDps), *Num(G->PoisonTime)); break;
		case EWWOGrenadeKind::Tripmine: What = FString::Printf(TEXT("Slow · %d%% for %s s"), FMath::RoundToInt(G->SlowAmount * 100.f), *Num(G->SlowTime)); break;
		case EWWOGrenadeKind::Wall: What = FString::Printf(TEXT("Wall · %s s"), *Num(G->WallTime)); break;
		case EWWOGrenadeKind::Blind: What = FString::Printf(TEXT("Blind · %s s"), *Num(G->BlindTime)); break;
		default: What = FString::Printf(TEXT("Blast · %s dmg, %s m"), *Num(G->Damage), *Num(G->BlastRadius)); break;
		}
		Note(What, G->DisplayName);
		++Effects;
	}
	for (int32 i = 0; i < UWWOLoadoutSubsystem::AbilitySlots; ++i)
	{
		const UWWOAbilityData* A = L->GetAbility(i);
		if (!A) continue;
		FString What;
		if (A->Id == WWOIds::Ability::StaticCharge) What = FString::Printf(TEXT("Chain lightning · %s dmg, %s jumps"), *Num(A->ChainDamage), *Num(A->ChainJumps));
		else if (A->Id == WWOIds::Ability::Seedburst) What = FString::Printf(TEXT("Seed grenades · %s more blasts"), *Num(A->SubCount));
		else if (A->Id == WWOIds::Ability::SporeFogDamage) What = FString::Printf(TEXT("Fog damage · %s/s"), *Num(A->FogDps));
		else if (A->Id == WWOIds::Ability::SporeDash) What = FString::Printf(TEXT("Hiding fog · %s s"), *Num(A->DashFogTime));
		else if (A->Id == WWOIds::Ability::SporePatches) What = FString::Printf(TEXT("Fog patches · %s s each"), *Num(A->PatchTime));
		else if (A->Id == WWOIds::Ability::RootBombs) What = FString::Printf(TEXT("Root bombs · %s dmg, %s m"), *Num(A->BombDamage), *Num(A->BombRadius));
		else if (A->Id == WWOIds::Ability::ShieldShare) What = FString::Printf(TEXT("Shield teammates · +%s"), *Num(A->ShieldAmount));
		if (!What.IsEmpty()) { Note(What, A->DisplayName); ++Effects; }
	}
	if (Flora && !Flora->UltName.IsEmpty()) { Note(TEXT("Ult · ") + Flora->UltName, Flora->DisplayName); ++Effects; }
	// Traits that change those effects rather than your own stats.
	static const TMap<FName, FString> EffectNames = {
		{ "fog_longer", TEXT("fog time") }, { "wall_longer", TEXT("wall time") }, { "slow_longer", TEXT("slow time") },
		{ "more_bounce", TEXT("grenade bounce") }, { "blast_bigger", TEXT("blast size") }, { "poison_damage", TEXT("poison, pool and fog damage") },
	};
	for (const UWWOTraitData* T : BuildTraits)
	{
		if (T->Effect.IsNone()) continue;
		const FString* Named = EffectNames.Find(T->Effect);
		Note(Named ? Pct(T->Amount) + TEXT(" ") + *Named : T->Description, T->DisplayName);
		++Effects;
	}
	if (Effects == 0) Note(TEXT("None"), TEXT(""));
	Heading(TEXT("Buffs on you"));
	int32 Buffs = 0;
	for (int32 i = 0; i < UWWOLoadoutSubsystem::AbilitySlots; ++i)
	{
		const UWWOAbilityData* A = L->GetAbility(i);
		if (!A) continue;
		auto Buff = [&](const FString& What) { Note(What, A->DisplayName); ++Buffs; };
		if (A->Id == WWOIds::Ability::Overgrowth)
		{
			if (!FMath::IsNearlyEqual(A->SpeedMult, 1.f)) Buff(Pct(A->SpeedMult - 1.f) + TEXT(" move speed"));
			if (A->ExtraJumps > 0.f) Buff(FString::Printf(TEXT("+%s jump"), *Num(A->ExtraJumps)));
		}
		else if (A->Id == WWOIds::Ability::Thornfist) Buff(FString::Printf(TEXT("Melee damage x%s for %s s"), *Num(A->MeleeDamageMult), *Num(A->Duration)));
		else if (A->Id == WWOIds::Ability::MeleeGuard) Buff(Pct(-A->GuardReduction) + TEXT(" melee damage taken after a melee hit"));
		else if (A->Id == WWOIds::Ability::ExtraGrenade) Buff(FString::Printf(TEXT("+%s grenade charge"), *Num(A->ExtraCharges)));
	}
	for (const UWWOTraitData* T : BuildTraits)
	{
		for (const FWWOStatModifier& Mod : T->Mods)
		{
			Note((Mod.Op == EWWOStatOp::Flat ? FString::Printf(TEXT("%+g"), Mod.Value) : Pct(Mod.Op == EWWOStatOp::Multiply ? Mod.Value - 1.f : Mod.Value))
				+ TEXT(" ") + UWWOLoadoutSubsystem::StatName(Mod.Stat), T->DisplayName);
			++Buffs;
		}
	}
	if (Buffs == 0) Note(TEXT("None"), TEXT(""));
	Left->AddChildToVerticalBox(List)->SetSize(WWOUI::Fill(1.f));
	UHorizontalBoxSlot* LeftSlot = Overview->AddChildToHorizontalBox(Left);
	LeftSlot->SetSize(WWOUI::Fill(0.31f));
	LeftSlot->SetPadding(FMargin(44.f * K, 60.f * K, 30.f * K, 0));

	// --- Middle: the character, between two faint lines.
	UHorizontalBoxSlot* LineA = Overview->AddChildToHorizontalBox(WWOUI::RuleDown(Tree, FLinearColor(1, 1, 1, 0.1f)));
	LineA->SetPadding(FMargin(0, 40.f * K, 0, 0));
	UHorizontalBoxSlot* Middle = Overview->AddChildToHorizontalBox(OverviewPreview);
	Middle->SetSize(WWOUI::Fill(0.38f));
	Middle->SetHorizontalAlignment(HAlign_Fill);
	Middle->SetVerticalAlignment(VAlign_Fill);
	Middle->SetPadding(FMargin(0, 40.f * K, 0, 0)); // level with the lines either side
	UHorizontalBoxSlot* LineB = Overview->AddChildToHorizontalBox(WWOUI::RuleDown(Tree, FLinearColor(1, 1, 1, 0.1f)));
	LineB->SetPadding(FMargin(0, 40.f * K, 0, 0));

	// --- Right: what's equipped, as cards. Each opens the tab that changes it.
	UVerticalBox* Right = Tree->ConstructWidget<UVerticalBox>();
	// The flora, with its grenade and abilities over its traits.
	UHorizontalBox* Top = Tree->ConstructWidget<UHorizontalBox>();
	{
		UVerticalBox* In = Tree->ConstructWidget<UVerticalBox>();
		// The flora's badge (its symbol on its colour), on its own (Will: "just have it be the symbol");
		// its initials and name for a flora with no symbol.
		UWidget* Big = Flora ? WWOUI::MakeFloraSymbol(WidgetTree, Flora->Id, FVector2D(124.f, 124.f) * K) : nullptr;
		const bool bSymbol = Big != nullptr;
		if (bSymbol)
		{
			// The badge covers the whole card, which is square with it (below).
			UScaleBox* Fit = Tree->ConstructWidget<UScaleBox>();
			Fit->SetStretch(EStretch::ScaleToFill);
			Fit->AddChild(Big);
			Big = Fit;
		}
		if (!Big)
		{
			UTextBlock* Letters = WWOUI::Spaced(WidgetTree, Initials(Flora ? Flora->DisplayName : TEXT("?")), WWOUI::Type::Display, Accent, 0);
			Letters->SetJustification(ETextJustify::Center);
			Big = Letters;
		}
		UVerticalBoxSlot* BigSlot = In->AddChildToVerticalBox(Big);
		BigSlot->SetSize(WWOUI::Fill(1.f));
		BigSlot->SetHorizontalAlignment(bSymbol ? HAlign_Fill : HAlign_Center);
		BigSlot->SetVerticalAlignment(bSymbol ? VAlign_Fill : VAlign_Center);
		if (!bSymbol)
		{
			UTextBlock* Label = WWOUI::Spaced(WidgetTree, Flora ? Flora->DisplayName : TEXT("No flora"), WWOUI::Type::Small, FLinearColor::White, 200);
			Label->SetJustification(ETextJustify::Center);
			In->AddChildToVerticalBox(Label);
		}
		USizeBox* Size = Tree->ConstructWidget<USizeBox>();
		Size->SetWidthOverride((bSymbol ? 156.f : 172.f) * K); // with its badge: square, as tall as the row (TopBox, 156)
		// The badge's edge is the flora's colour at 20% (Tools/make_flora_symbols.py, EDGE_SHADE).
		const FLinearColor BadgeEdge = Flora ? WWOUI::BadgeEdge(Flora->Color) : WWOUI::CardColor;
		UWidget* FloraCard = Card(In, Open(TEXT("Flora")), bSymbol ? BadgeEdge : WWOUI::CardColor, Accent, 2.f, bSymbol ? FMargin(2.f) : FMargin(8.f, 10.f));
		HoverTiles.Add({ FloraCard, FString(TEXT("flora")) });
		Size->AddChild(FloraCard);
		Top->AddChildToHorizontalBox(Size);
	}
	UVerticalBox* Small = Tree->ConstructWidget<UVerticalBox>();
	// Symbol: the item's badge (UI/WWOUI.h), filling the tile in place of its initials.
	auto Tile = [&](UHorizontalBox* Row, const FString& Name, const FLinearColor& Fill, const FLinearColor& Edge, const FString& PickerName, UTexture2D* Symbol = nullptr)
	{
		UWidget* In = nullptr;
		if (Symbol && !Name.IsEmpty())
		{
			In = WWOUI::FillWithSymbol(WidgetTree, Symbol);
		}
		else
		{
			// An empty slot (a trait not chosen yet): a faint "+" in an outline, nothing filled in.
			UTextBlock* Text = WWOUI::Spaced(WidgetTree, Name.IsEmpty() ? FString(TEXT("+")) : Initials(Name), WWOUI::Type::Title,
				Name.IsEmpty() ? WWOUI::TextFaint : FLinearColor(1, 1, 1, 0.92f), 40);
			Text->SetJustification(ETextJustify::Center);
			UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>();
			UVerticalBoxSlot* TextSlot = Column->AddChildToVerticalBox(Text);
			TextSlot->SetSize(WWOUI::Fill(1.f));
			TextSlot->SetVerticalAlignment(VAlign_Center);
			In = Column;
		}
		UWidget* Box = Card(In, Open(TEXT("Flora")), Name.IsEmpty() ? FLinearColor(0, 0, 0, 0) : Fill, Name.IsEmpty() ? FLinearColor(1, 1, 1, 0.22f) : Edge,
			Name.IsEmpty() ? WWOUI::BoxEdge : 2.f, FMargin(2.f));
		HoverTiles.Add({ Box, PickerName }); // hovering it offers the other choices (UpdateHoverPicker)
		UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Box);
		S->SetSize(WWOUI::Fill(1.f));
		S->SetPadding(FMargin(Row->GetChildrenCount() > 1 ? 10.f * K : 0.f, 0, 0, 0));
	};
	UHorizontalBox* Kit = Tree->ConstructWidget<UHorizontalBox>();
	// The same colors as on the Flora tab: each item's own, darkened, inside an edge of it; traits blue.
	{
		const UWWOGrenadeData* G = L->GetGrenade();
		UTexture2D* Badge = G ? WWOUI::SymbolTexture("Grenade", G->Id) : nullptr;
		Tile(Kit, G ? G->DisplayName : FString(), G ? (Badge ? WWOUI::BadgeEdge(G->Color) : WWO::Darkened(G->Color, 0.6f)) : WWOUI::CardColor,
			G ? G->Color : WWOUI::CardEdgeStrong, TEXT("grenade"), Badge);
	}
	for (int32 i = 0; i < 2; ++i)
	{
		const UWWOAbilityData* A = L->GetAbility(i);
		UTexture2D* Badge = A ? WWOUI::SymbolTexture("Ability", A->Id) : nullptr;
		Tile(Kit, A ? A->DisplayName : FString(), A ? (Badge ? WWOUI::BadgeEdge(A->Color) : WWO::Darkened(A->Color, 0.6f)) : WWOUI::CardColor,
			A ? A->Color : WWOUI::CardEdgeStrong, FString::Printf(TEXT("ability%d"), i), Badge);
	}
	UHorizontalBox* TraitRow = Tree->ConstructWidget<UHorizontalBox>();
	const TArray<UWWOTraitData*> Traits = L->EquippedTraits();
	for (int32 i = 0; i < FMath::Max(Traits.Num(), 4); ++i)
	{
		// A trait's badge is on its flora's colour.
		UTexture2D* Badge = Traits.IsValidIndex(i) ? WWOUI::SymbolTexture("Trait", Traits[i]->Id) : nullptr;
		const UWWOFloraData* TraitFlora = L->GetFlora();
		Tile(TraitRow, Traits.IsValidIndex(i) ? Traits[i]->DisplayName : FString(), Badge && TraitFlora ? WWOUI::BadgeEdge(TraitFlora->Color) : TraitFill,
			TraitEdge, FString::Printf(TEXT("trait%d"), i), Badge);
	}
	Small->AddChildToVerticalBox(Kit)->SetSize(WWOUI::Fill(1.15f));
	UVerticalBoxSlot* TraitSlot = Small->AddChildToVerticalBox(TraitRow);
	TraitSlot->SetSize(WWOUI::Fill(1.f));
	TraitSlot->SetPadding(FMargin(0, 10.f * K, 0, 0));
	UHorizontalBoxSlot* SmallSlot = Top->AddChildToHorizontalBox(Small);
	SmallSlot->SetSize(WWOUI::Fill(1.f));
	SmallSlot->SetPadding(FMargin(14.f * K, 0, 0, 0));
	USizeBox* TopBox = Tree->ConstructWidget<USizeBox>();
	TopBox->SetHeightOverride(156.f * K);
	TopBox->AddChild(Top);
	Right->AddChildToVerticalBox(TopBox);
	const FMargin BarPad(16.f, 10.f);
	// A bar's mark at its left: a short upright line (the accent for the weapon in hand).
	auto Marked = [&](const FLinearColor& Color) -> UWidget*
	{
		UBorder* Mark = Tree->ConstructWidget<UBorder>();
		Mark->SetBrush(WWOUI::RoundBrush(Color, 1.5f));
		USizeBox* MarkBox = Tree->ConstructWidget<USizeBox>();
		MarkBox->SetWidthOverride(3.f * K);
		MarkBox->AddChild(Mark);
		return MarkBox;
	};
	// Your two weapons: each bar is a weapon slot holding a saved config. Hovering one offers the
	// saved configs; clicking it opens the Weapon tab.
	for (int32 WeaponSlot = 0; WeaponSlot < UWWOLoadoutSubsystem::WeaponSlotCount; ++WeaponSlot)
	{
		const int32 ConfigIndex = L->GetSlotConfig(WeaponSlot);
		const bool bInHand = WeaponSlot == L->GetActiveSlot();
		FString Name = TEXT("Empty · hover to choose");
		if (ConfigIndex != INDEX_NONE)
		{
			const FWWOModConfig& Config = L->GetConfigs()[ConfigIndex];
			const UWWOWeaponData* Gun = nullptr;
			for (const UWWOWeaponData* W : L->Catalog.Weapons) if (W->Id == Config.Weapon) Gun = W;
			Name = (Gun ? Gun->DisplayName : Config.Weapon.ToString()) + TEXT("  ·  ") + Config.Name;
		}
		else if (bInHand && L->GetWeapon()) Name = L->GetWeapon()->DisplayName + TEXT("  ·  not saved");
		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();
		Row->AddChildToHorizontalBox(Marked(bInHand ? Accent : FLinearColor(1, 1, 1, 0.3f)))->SetPadding(FMargin(0, 6.f * K));
		UVerticalBox* Words = Tree->ConstructWidget<UVerticalBox>();
		Words->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, FString::Printf(TEXT("Weapon %d%s"), WeaponSlot + 1, bInHand ? TEXT("  ·  in hand") : TEXT("")), WWOUI::Type::Micro, Dim, 240));
		// A long config name ends in "..." at the bar's edge (hovering the bar lists the configs with their whole names in tips).
		UTextBlock* NameText = WWOUI::Spaced(WidgetTree, Name, WWOUI::Type::Body, FLinearColor::White, 180);
		WWOUI::Ellipsis(NameText, 10000.f); // left-aligned: cut at the bar's edge, whatever its width
		Words->AddChildToVerticalBox(NameText)->SetPadding(FMargin(0, 6.f * K, 0, 0));
		UHorizontalBoxSlot* WordsSlot = Row->AddChildToHorizontalBox(Words);
		WordsSlot->SetSize(WWOUI::Fill(1.f));
		WordsSlot->SetPadding(FMargin(18.f * K, 0, 0, 0));
		WordsSlot->SetVerticalAlignment(VAlign_Center);
		UWidget* Bar = Card(Row, Open(TEXT("Weapons")), WWOUI::CardColor, WWOUI::CardEdgeStrong, WWOUI::BoxEdge, BarPad);
		HoverTiles.Add({ Bar, FString::Printf(TEXT("weapon%d"), WeaponSlot) });
		Right->AddChildToVerticalBox(Bar)->SetPadding(FMargin(0, 26.f * K, 0, 0));
	}
	// The four frame parts (the shell): a two by two grid of tall cards filling the rest of the column.
	// Each card's upper area shows the part's picture (AWWOPartPictures: the part alone, in your colours);
	// its words sit at the bottom: the slot, the part's name, what it changes.
	UUniformGridPanel* Parts = Tree->ConstructWidget<UUniformGridPanel>();
	Parts->SetSlotPadding(FMargin(7.f * K));
	static const TCHAR* const PartSlots[] = { TEXT("head"), TEXT("arms"), TEXT("core"), TEXT("legs") };
	for (int32 i = 0; i < 4; ++i)
	{
		const UWWOFramePartData* Part = L->GetFramePart(PartSlots[i]);
		UOverlay* In = Tree->ConstructWidget<UOverlay>();
		UOverlaySlot* Tick = In->AddChildToOverlay(WWOUI::Spaced(WidgetTree, TEXT("/"), WWOUI::Type::Body, Accent, 0));
		Tick->SetHorizontalAlignment(HAlign_Right);
		Tick->SetVerticalAlignment(VAlign_Top);
		UVerticalBox* Words = Tree->ConstructWidget<UVerticalBox>();
		Words->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, PartSlots[i], WWOUI::Type::Caption, Dim, 220));
		Words->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, Part ? Part->DisplayName : TEXT("Standard"), WWOUI::Type::Small, FLinearColor::White, 160, true))->SetPadding(FMargin(0, 6.f * K, 0, 0));
		if (Part) Words->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, Part->Summary().Replace(TEXT("\n"), TEXT(" ")), WWOUI::Type::Micro, Dim, 40, true))->SetPadding(FMargin(0, 6.f * K, 0, 0));
		UVerticalBox* Stack = Tree->ConstructWidget<UVerticalBox>();
		if (UMaterialInterface* Picture = PartPicture(Part, PartSlots[i]))
		{
			UVerticalBoxSlot* PictureSlot = Stack->AddChildToVerticalBox(PictureWidget(Picture, FVector2D(150.f, 100.f)));
			PictureSlot->SetSize(WWOUI::Fill(1.f));
			PictureSlot->SetHorizontalAlignment(HAlign_Center);
			PictureSlot->SetVerticalAlignment(VAlign_Center);
			PictureSlot->SetPadding(FMargin(0, 10.f * K, 0, 8.f * K)); // (clear of the corner's tick)
		}
		else Stack->AddChildToVerticalBox(Tree->ConstructWidget<USpacer>())->SetSize(WWOUI::Fill(1.f));
		Stack->AddChildToVerticalBox(Words);
		UOverlaySlot* WordsSlot = In->AddChildToOverlay(Stack);
		WordsSlot->SetHorizontalAlignment(HAlign_Fill);
		WordsSlot->SetVerticalAlignment(VAlign_Fill);
		UWidget* PartCard = Card(In, Open(TEXT("Frame Parts")), WWOUI::CardColor, WWOUI::CardEdgeStrong, WWOUI::BoxEdge, FMargin(18.f, 12.f));
		HoverTiles.Add({ PartCard, FString(TEXT("part_")) + PartSlots[i] });
		UUniformGridSlot* Cell = Parts->AddChildToUniformGrid(PartCard, i / 2, i % 2);
		Cell->SetHorizontalAlignment(HAlign_Fill);
		Cell->SetVerticalAlignment(VAlign_Fill);
	}
	UVerticalBoxSlot* PartsSlot = Right->AddChildToVerticalBox(Parts);
	PartsSlot->SetSize(WWOUI::Fill(1.f));
	PartsSlot->SetPadding(FMargin(-7.f * K, 20.f * K, -7.f * K, -7.f * K));
	UHorizontalBoxSlot* RightSlot = Overview->AddChildToHorizontalBox(Right);
	RightSlot->SetSize(WWOUI::Fill(0.31f));
	RightSlot->SetPadding(FMargin(36.f * K, 40.f * K, 16.f * K, 16.f * K));
}

void UWWOCharacterMenu::UpdateStatPreview(const UWWOFramePartData* Part, bool bEquipped)
{
	// The four stat bars, and what the shell part under the cursor means for them.
	// - A part you could equip: green for what it would add, red for what it would take away.
	// - The part you have on: yellow for what it is adding, faint red for what it is costing you.
	PreviewPart = const_cast<UWWOFramePartData*>(Part);
	bPreviewEquipped = bEquipped;
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return;
	// No points is a quarter of the bar; eight fills it (two a part).
	auto Fill = [](float Points) { return FMath::Clamp(0.25f + Points * 0.75f / 8.f, 0.04f, 1.f); };
	static const FLinearColor Green(0.35f, 0.9f, 0.4f), Red(1.f, 0.25f, 0.2f), Yellow(1.f, 0.82f, 0.2f), FaintRed(1.f, 0.25f, 0.2f, 0.35f);
	const FLinearColor Dim = WWOUI::DimBright;
	for (const FStatBar& Bar : StatBars4)
	{
		if (!Bar.Have || !Bar.Shift || !Bar.Rest) continue;
		float Now = 0.f;
		for (const FName& FrameSlot : UWWOLoadoutSubsystem::FrameSlots())
		{
			if (const UWWOFramePartData* On = L->GetFramePart(FrameSlot)) Now += On->Points(Bar.Group);
		}
		float White = Fill(Now), Piece = 0.f, Delta = 0.f;
		FLinearColor Color(0, 0, 0, 0);
		if (Part && bEquipped)
		{
			// What it's doing for you now: the bar without it, and the difference.
			Delta = Part->Points(Bar.Group);
			const float Without = Fill(Now - Delta);
			if (Delta > 0.f) { White = Without; Piece = Fill(Now) - Without; Color = Yellow; }
			else if (Delta < 0.f) { Piece = Without - Fill(Now); Color = FaintRed; }
		}
		else if (Part)
		{
			// What swapping to it would do.
			const UWWOFramePartData* On = L->GetFramePart(Part->Slot);
			Delta = Part->Points(Bar.Group) - (On ? On->Points(Bar.Group) : 0.f);
			const float After = Fill(Now + Delta);
			if (Delta > 0.f) { Piece = After - Fill(Now); Color = Green; }
			else if (Delta < 0.f) { White = After; Piece = Fill(Now) - After; Color = Red; }
		}
		auto Size = [](UBorder* Box, float Share)
		{
			if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Box->Slot)) S->SetSize(WWOUI::Fill(FMath::Max(Share, 0.0001f)));
			Box->SetVisibility(Share > 0.0005f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		};
		Size(Bar.Have, White);
		Size(Bar.Shift, Piece);
		Size(Bar.Rest, 1.f - White - Piece);
		Bar.Shift->SetBrushColor(WWO::Lin(Color));
		if (Bar.Value) Bar.Value->SetText(FText::FromString(FString::Printf(TEXT("%+d"), FMath::RoundToInt(Now))));
		if (Bar.Change)
		{
			const bool bShow = Part && FMath::Abs(Delta) >= 0.01f;
			Bar.Change->SetText(FText::FromString(bShow ? FString::Printf(TEXT("%+d"), FMath::RoundToInt(Delta)) + (bEquipped ? TEXT(" FROM THIS") : TEXT("")) : FString()));
			Bar.Change->SetColorAndOpacity(FSlateColor(WWO::Lin(bEquipped ? (Delta > 0.f ? Yellow : Red) : (Delta > 0.f ? Green : Red))));
		}
	}
}

void UWWOCharacterMenu::UpdateNoteOutline(const FString& Source)
{
	OutlinedSource = Source;
	const FLinearColor Accent = WWOUI::Accent(this);
	for (const TPair<TObjectPtr<UBorder>, FString>& Row : NoteRows)
	{
		const bool bOn = !Source.IsEmpty() && Row.Value == Source;
		if (Row.Key) Row.Key->SetBrush(WWOUI::RoundBrush(bOn ? WWO::WithAlpha(Accent, 0.1f) : FLinearColor(0, 0, 0, 0), WWOUI::Corner, bOn ? Accent : FLinearColor(0, 0, 0, 0), bOn ? 1.5f : 0.f));
	}
}

void UWWOCharacterMenu::CloseHoverPicker()
{
	HoverPicker.Reset();
	HoverClose.Reset();
	HoverParts.Reset();
	HoverNames.Reset();
	if (HoverPopup) HoverPopup->RemoveFromParent();
	HoverPopup = nullptr;
	HoverScroll = nullptr;
}

void UWWOCharacterMenu::UpdateHoverPicker(float DeltaTime)
{
	// On the Loadout tab, resting the cursor on the flora, the grenade, an ability, a trait or a
	// weapon slot drops a row of the other choices under it (icons only: the tabs explain them).
	// It stays while the cursor is on the tile or the row, and goes a moment after it leaves both.
	if (!bOpen || !Expanded.IsEmpty() || !FSlateApplication::IsInitialized())
	{
		if (HoverPopup) CloseHoverPicker();
		if (PreviewPart.IsValid()) UpdateStatPreview(nullptr, false);
		if (!OutlinedSource.IsEmpty()) UpdateNoteOutline(FString());
		return;
	}
	const FVector2D Cursor = FSlateApplication::Get().GetCursorPos();
	FString Over;
	UWidget* OverTile = nullptr;
	for (const TPair<TWeakObjectPtr<UWidget>, FString>& Tile : HoverTiles)
	{
		if (Tile.Key.IsValid() && Tile.Key->GetCachedGeometry().IsUnderLocation(Cursor)) { Over = Tile.Value; OverTile = Tile.Key.Get(); }
	}
	const bool bOnPopup = HoverPopup && HoverPopup->GetCachedGeometry().IsUnderLocation(Cursor);
	// The shell part under the cursor, for the stat bars: one of the choices in the open row, else
	// the part on the card being hovered.
	const UWWOFramePartData* Part = nullptr;
	for (const TPair<TWeakObjectPtr<UWidget>, TWeakObjectPtr<UWWOFramePartData>>& Choice : HoverParts)
	{
		if (Choice.Key.IsValid() && Choice.Key->GetCachedGeometry().IsUnderLocation(Cursor)) Part = Choice.Value.Get();
	}
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	if (!Part && !bOnPopup && Over.StartsWith(TEXT("part_")) && Loadout) Part = Loadout->GetFramePart(FName(*Over.Mid(5)));
	const bool bEquipped = Part && Loadout && Loadout->GetFramePart(Part->Slot) == Part;
	if (Part != PreviewPart.Get() || bEquipped != bPreviewEquipped) UpdateStatPreview(Part, bEquipped);
	// The item under the cursor, for the Build notes: a choice in the open row, else what's on the
	// tile being hovered. Its notes (the rows it is the source of) get an outline.
	FString Source;
	for (const TPair<TWeakObjectPtr<UWidget>, FString>& Choice : HoverNames)
	{
		if (Choice.Key.IsValid() && Choice.Key->GetCachedGeometry().IsUnderLocation(Cursor)) Source = Choice.Value;
	}
	if (Source.IsEmpty() && !bOnPopup && Loadout)
	{
		const TArray<UWWOTraitData*> Traits = Loadout->EquippedTraits();
		if (Over == TEXT("flora") && Loadout->GetFlora()) Source = Loadout->GetFlora()->DisplayName;
		else if (Over == TEXT("grenade") && Loadout->GetGrenade()) Source = Loadout->GetGrenade()->DisplayName;
		else if (Over.StartsWith(TEXT("ability")) && Loadout->GetAbility(FCString::Atoi(*Over.Mid(7)))) Source = Loadout->GetAbility(FCString::Atoi(*Over.Mid(7)))->DisplayName;
		else if (Over.StartsWith(TEXT("trait")) && Traits.IsValidIndex(FCString::Atoi(*Over.Mid(5)))) Source = Traits[FCString::Atoi(*Over.Mid(5))]->DisplayName;
	}
	if (Source != OutlinedSource) UpdateNoteOutline(Source);
	if (!Over.IsEmpty() && !bOnPopup && Over != HoverPicker) OpenHoverPicker(Over, OverTile);
	else if (HoverClose.ShouldClose(HoverPopup != nullptr, !Over.IsEmpty() || bOnPopup, DeltaTime)) CloseHoverPicker();
}

void UWWOCharacterMenu::OpenHoverPicker(const FString& Name, UWidget* Tile)
{
	CloseHoverPicker();
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	UOverlay* Root = Cast<UOverlay>(WidgetTree->RootWidget);
	if (!L || !Root || !Tile) return;
	UWidgetTree* Tree = WidgetTree;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
	const FLinearColor Accent = WWOUI::Accent(this);
	// The choices, laid out in rows below (as many rows as it takes to keep the row on screen).
	TArray<UWidget*> Items;
	const float Wide = Name.StartsWith(TEXT("part_")) ? PartChoice : WideChoice; // a choice with a second line
	// One choice: a square of initials (wider, with a second line, for a weapon config).
	// Symbol: the item's badge filling the square in place of the initials.
	auto Option = [&](const FString& Label, const FString& Under, const FString& Tip, bool bOn, const FLinearColor& Fill, const FLinearColor& Edge, TFunction<void()> Pick,
		UTexture2D* Symbol = nullptr) -> UWidget*
	{
		UWidget* Inside = nullptr;
		if (Symbol) Inside = WWOUI::FillWithSymbol(WidgetTree, Symbol);
		else
		{
			UVerticalBox* In = Tree->ConstructWidget<UVerticalBox>();
			// A name too long for the box ends in "..." (the tip has it whole) instead of running into the next one.
			UTextBlock* Text = WWOUI::Spaced(WidgetTree, Label, Under.IsEmpty() ? WWOUI::Type::Title : WWOUI::Type::Small, bOn ? FLinearColor::White : FLinearColor(1, 1, 1, 0.85f), 40);
			Text->SetJustification(ETextJustify::Center);
			const float TextWidth = ((Under.IsEmpty() ? ChoiceSide : Wide) - 12.f) * K; // inside the card's edge and padding
			const FMargin Inset(4.f * K, 0); // a cut-off name keeps clear of the card's edge
			const bool bCut = WWOUI::Ellipsis(Text, TextWidth);
			UVerticalBoxSlot* TextSlot = In->AddChildToVerticalBox(Text);
			TextSlot->SetSize(WWOUI::Fill(1.f));
			TextSlot->SetVerticalAlignment(VAlign_Center);
			if (bCut) TextSlot->SetPadding(Inset);
			if (!Under.IsEmpty())
			{
				UTextBlock* Second = WWOUI::Spaced(WidgetTree, Under, WWOUI::Type::Micro, FLinearColor(1, 1, 1, 0.6f), 60);
				Second->SetJustification(ETextJustify::Center);
				const bool bSecondCut = WWOUI::Ellipsis(Second, TextWidth);
				UVerticalBoxSlot* SecondSlot = In->AddChildToVerticalBox(Second);
				if (bSecondCut) SecondSlot->SetPadding(Inset);
			}
			Inside = In;
		}
		UWidget* Box = Card(Inside, [WeakThis, Pick] { Pick(); if (WeakThis.IsValid()) WeakThis->BuildOverview(); }, Fill, Edge, bOn ? 4.f : 2.f,
			FMargin(Symbol ? (bOn ? 4.f : 2.f) : 4.f));
		WWOUI::SetTip(WidgetTree, Box, Tip);
		HoverNames.Add({ Box, Tip }); // its notes in the Build section light up while it's hovered
		USizeBox* Size = Tree->ConstructWidget<USizeBox>();
		Size->SetWidthOverride((Symbol || Under.IsEmpty() ? ChoiceSide : Wide) * K);
		Size->SetHeightOverride(ChoiceSide * K);
		Size->AddChild(Box);
		Items.Add(Size);
		return Box;
	};
	// An item's own color darkened inside an edge of it, lighter and thicker for the equipped one.
	auto In = [](const FLinearColor& C) { return WWO::Darkened(C, 0.5f); };
	auto Rim = [](const FLinearColor& C, bool bOn) { return bOn ? WWO::Lightened(C, 0.3f) : C; };
	if (Name == TEXT("flora"))
	{
		for (UWWOFloraData* F : L->Catalog.Floras)
		{
			UTexture2D* Badge = WWOUI::SymbolTexture("Flora", F->Id);
			if (F->bUnlocked) Option(Initials(F->DisplayName), FString(), F->DisplayName, F == L->GetFlora(), Badge ? WWOUI::BadgeEdge(F->Color) : In(F->Color),
				Rim(F->Color, F == L->GetFlora()), [WL, F] { if (WL.IsValid()) WL->EquipFlora(F); }, Badge);
		}
	}
	else if (Name == TEXT("grenade"))
	{
		for (UWWOGrenadeData* G : L->GrenadesForFlora())
		{
			UTexture2D* Badge = WWOUI::SymbolTexture("Grenade", G->Id);
			if (G->bUnlocked) Option(Initials(G->DisplayName), FString(), G->DisplayName, G == L->GetGrenade(), Badge ? WWOUI::BadgeEdge(G->Color) : In(G->Color),
				Rim(G->Color, G == L->GetGrenade()), [WL, G] { if (WL.IsValid()) WL->EquipGrenade(G); }, Badge);
		}
	}
	else if (Name.StartsWith(TEXT("ability")))
	{
		const int32 AbilitySlot = FCString::Atoi(*Name.Mid(7));
		for (UWWOAbilityData* A : L->AbilitiesForFlora())
		{
			// One already in the other slot swaps places with this one.
			UTexture2D* Badge = WWOUI::SymbolTexture("Ability", A->Id);
			if (A->bUnlocked) Option(Initials(A->DisplayName), FString(), A->DisplayName, L->AbilitySlotOf(A) == AbilitySlot, Badge ? WWOUI::BadgeEdge(A->Color) : In(A->Color),
				Rim(A->Color, L->AbilitySlotOf(A) == AbilitySlot), [WL, A, AbilitySlot] { if (WL.IsValid()) WL->EquipAbility(AbilitySlot, A); }, Badge);
		}
	}
	else if (Name.StartsWith(TEXT("trait")))
	{
		const int32 TraitSlot = FCString::Atoi(*Name.Mid(5));
		const TArray<UWWOTraitData*> Equipped = L->EquippedTraits();
		UWWOTraitData* InSlot = Equipped.IsValidIndex(TraitSlot) ? Equipped[TraitSlot] : nullptr;
		for (UWWOTraitData* T : L->TraitsForFlora())
		{
			if (!T->bUnlocked) continue;
			const bool bOn = L->IsTraitEquipped(T);
			// An equipped one comes off; any other takes this slot's place.
			UTexture2D* Badge = WWOUI::SymbolTexture("Trait", T->Id);
			const UWWOFloraData* TraitFlora = L->GetFlora();
			const FLinearColor Fill = Badge && TraitFlora ? WWOUI::BadgeEdge(TraitFlora->Color) : (bOn ? TraitFill : TraitOffFill);
			Option(Initials(T->DisplayName), FString(), T->DisplayName, bOn, Fill, bOn ? TraitEdge : TraitOffEdge, [WL, T, InSlot, bOn]
			{
				if (!WL.IsValid()) return;
				if (!bOn && InSlot) WL->ToggleTrait(InSlot);
				WL->ToggleTrait(T);
			}, Badge);
		}
	}
	else if (Name.StartsWith(TEXT("weapon")))
	{
		const int32 WeaponSlot = FCString::Atoi(*Name.Mid(6));
		const TArray<FWWOModConfig>& Configs = L->GetConfigs();
		for (int32 i = 0; i < Configs.Num(); ++i)
		{
			const UWWOWeaponData* Gun = nullptr;
			for (const UWWOWeaponData* W : L->Catalog.Weapons) if (W->Id == Configs[i].Weapon) Gun = W;
			const FString GunName = Gun ? Gun->DisplayName : Configs[i].Weapon.ToString();
			Option(GunName, Configs[i].Name, GunName + TEXT(" · ") + Configs[i].Name + TEXT("\n") + UWWOLoadoutSubsystem::ConfigSummary(Configs[i], L),
				L->GetSlotConfig(WeaponSlot) == i, WWOUI::CardColor, L->GetSlotConfig(WeaponSlot) == i ? FLinearColor::White : WWOUI::CardEdgeStrong, [WL, WeaponSlot, i] { if (WL.IsValid()) WL->SetSlotConfig(WeaponSlot, i); });
		}
		if (Configs.Num() == 0)
		{
			UBorder* None = Tree->ConstructWidget<UBorder>();
			None->SetBrush(WWOUI::RoundBrush(FLinearColor(0, 0, 0, 0), 0.f));
			None->SetPadding(FMargin(8.f * K, 14.f * K));
			None->SetContent(WWOUI::Spaced(WidgetTree, TEXT("No saved configs yet · save one on the Weapon tab"), WWOUI::Type::Caption, FLinearColor(1, 1, 1, 0.7f), 100));
			Items.Add(None);
		}
	}
	else if (Name.StartsWith(TEXT("part_")))
	{
		// The shell's parts for this slot. Resting on one shows what it would do to the stat bars.
		const FName PartSlot(*Name.Mid(5));
		for (UWWOFramePartData* Part : L->FramePartsFor(PartSlot))
		{
			if (!Part->bUnlocked) continue;
			const bool bOn = Part == L->GetFramePart(PartSlot);
			UWidget* Box = Option(Part->DisplayName, Part->Summary().Replace(TEXT("\n"), TEXT("  ")), Part->DisplayName, bOn, WWOUI::CardColor, bOn ? FLinearColor::White : WWOUI::CardEdgeStrong,
				[WL, Part] { if (WL.IsValid()) WL->EquipFramePart(Part); });
			HoverParts.Add({ Box, Part });
		}
	}
	if (Items.Num() == 0) return;
	// The rows. One row, starting under the tile, as long as that fits; else it wraps into as few rows as
	// keep it inside the window (wider rows only as far as the height needs), and only if even rows as
	// wide as the window are too tall for the room above or below the tile does it scroll (the mouse
	// wheel, or the right stick through the menu cursor).
	const FGeometry& RootGeo = Root->GetCachedGeometry();
	const FGeometry& TileGeo = Tile->GetCachedGeometry();
	const FVector2D RootSize = RootGeo.GetLocalSize();
	const FVector2D TileTop = RootGeo.AbsoluteToLocal(TileGeo.GetAbsolutePositionAtCoordinates(FVector2D(0.f, 0.f)));
	const FVector2D TileFoot = RootGeo.AbsoluteToLocal(TileGeo.GetAbsolutePositionAtCoordinates(FVector2D(0.f, 1.f)));
	const bool bMessage = Items.Num() == 1 && !Cast<USizeBox>(Items[0]);
	const bool bWide = Name.StartsWith(TEXT("weapon")) || Name.StartsWith(TEXT("part_"));
	const float Gap = ChoiceGap * K, Pad = 10.f * K, Margin = 8.f, Off = 4.f; // Margin: from the window's edges; Off: from the tile
	const float ItemW = (bMessage ? 500.f : bWide ? Wide : ChoiceSide) * K, ItemH = (bMessage ? 44.f : ChoiceSide) * K;
	const float RoomBelow = RootSize.Y - TileFoot.Y - Off - Margin, RoomAbove = TileTop.Y - Off - Margin;
	const int32 FitAcross = FMath::Max(1, FMath::FloorToInt((RootSize.X - 2.f * Margin - 2.f * Pad + Gap) / (ItemW + Gap)));
	auto Tall = [&](int32 Across) { const int32 Rows = FMath::DivideAndRoundUp(Items.Num(), Across); return Rows * ItemH + (Rows - 1) * Gap + 2.f * Pad; };
	// Rows of up to PreferAcross (a weapon config's row would otherwise sprawl over the character), wider if that's too tall.
	const int32 PreferAcross = FMath::Min(Items.Num(), FMath::Min(bWide ? 5 : 10, FitAcross));
	int32 Across = PreferAcross;
	while (Across < FitAcross && Tall(Across) > FMath::Max(RoomBelow, RoomAbove)) ++Across;
	Across = FMath::Min(Across, Items.Num());
	const float Height = Tall(Across);
	// Under the tile, or over it when the tile is low in the window (the bottom row of the shell) or the
	// rows only fit there; with neither, the side with more room, scrolling.
	const bool bLow = TileFoot.Y > RootSize.Y * 0.8f;
	bool bAbove = bLow ? (Height <= RoomAbove || RoomAbove >= RoomBelow) : (Height > RoomBelow && RoomAbove > RoomBelow);
	const float Room = bAbove ? RoomAbove : RoomBelow;
	const bool bScroll = Height > Room + 0.5f;
	UVerticalBox* Rows = Tree->ConstructWidget<UVerticalBox>();
	for (int32 i = 0; i < Items.Num(); i += Across)
	{
		UHorizontalBox* Line = Tree->ConstructWidget<UHorizontalBox>();
		for (int32 j = i; j < FMath::Min(i + Across, Items.Num()); ++j) Line->AddChildToHorizontalBox(Items[j])->SetPadding(FMargin(j > i ? Gap : 0.f, 0, 0, 0));
		Rows->AddChildToVerticalBox(Line)->SetPadding(FMargin(0, i > 0 ? Gap : 0.f, 0, 0));
	}
	UWidget* Inside = Rows;
	const float ScrollGutter = bScroll ? 12.f * K : 0.f; // the scroll bar's thumb, clear of the choices
	if (bScroll)
	{
		HoverScroll = WWOUI::ScrollBox(Tree);
		HoverScroll->SetConsumeMouseWheel(EConsumeMouseWheel::Always);
		HoverScroll->AddChild(Rows);
		if (UScrollBoxSlot* RowsSlot = Cast<UScrollBoxSlot>(Rows->Slot)) RowsSlot->SetPadding(FMargin(0, 0, ScrollGutter - 4.f * K, 0));
		Inside = HoverScroll;
	}
	UBorder* Panel = Tree->ConstructWidget<UBorder>();
	Panel->SetBrush(WWOUI::RoundBrush(FLinearColor(0.07f, 0.075f, 0.09f, 0.98f), WWOUI::Corner, WWO::WithAlpha(Accent, 0.9f), 1.5f));
	Panel->SetPadding(FMargin(Pad));
	Panel->SetContent(Inside);
	// Exactly the rows' size (so the window-edge clamp below is exact), and no taller than the room.
	// (The "no configs" line sizes itself; its width here is only for the clamp.)
	const float Width = bMessage ? 520.f * K : Across * ItemW + (Across - 1) * Gap + 2.f * Pad + ScrollGutter;
	USizeBox* PanelBox = Tree->ConstructWidget<USizeBox>();
	if (!bMessage)
	{
		PanelBox->SetWidthOverride(Width);
		PanelBox->SetHeightOverride(FMath::Min(Height, FMath::Max(Room, ItemH + 2.f * Pad)));
	}
	PanelBox->AddChild(Panel);
	// Its left edge on the tile's, pulled in from the window's right edge when the rows would run past it.
	const float Left = FMath::Clamp((float)TileFoot.X, Margin, FMath::Max((float)RootSize.X - Width - Margin, Margin));
	UOverlaySlot* PopupSlot = Root->AddChildToOverlay(PanelBox);
	PopupSlot->SetHorizontalAlignment(HAlign_Left);
	if (bAbove)
	{
		PopupSlot->SetVerticalAlignment(VAlign_Bottom);
		PopupSlot->SetPadding(FMargin(Left, 0, 0, RootSize.Y - (float)TileTop.Y + Off));
	}
	else
	{
		PopupSlot->SetVerticalAlignment(VAlign_Top);
		PopupSlot->SetPadding(FMargin(Left, TileFoot.Y + Off, 0, 0));
	}
	HoverPopup = PanelBox;
	HoverPicker = Name;
	HoverClose.Reset();
}
