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
	// The bars under the kit (two weapons, four shell parts) share the rest of the column evenly, the
	// same gap between each; each bar's words centred in it.
	const FMargin BarPad(16.f, 8.f);
	auto AddBar = [&](UWidget* Bar)
	{
		UVerticalBoxSlot* S = Right->AddChildToVerticalBox(Bar);
		S->SetSize(WWOUI::Fill(1.f));
		S->SetPadding(FMargin(0, 14.f * K, 0, 0));
	};
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
		Words->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, Name, WWOUI::Type::Body, FLinearColor::White, 180))->SetPadding(FMargin(0, 6.f * K, 0, 0));
		UHorizontalBoxSlot* WordsSlot = Row->AddChildToHorizontalBox(Words);
		WordsSlot->SetPadding(FMargin(18.f * K, 0, 0, 0));
		WordsSlot->SetVerticalAlignment(VAlign_Center);
		UWidget* Bar = Card(Row, Open(TEXT("Weapons")), WWOUI::CardColor, WWOUI::CardEdgeStrong, WWOUI::BoxEdge, BarPad);
		HoverTiles.Add({ Bar, FString::Printf(TEXT("weapon%d"), WeaponSlot) });
		AddBar(Bar);
	}
	// The four frame parts (the shell), one bar each like the weapons: the slot over the part's name,
	// what it changes at the right. (There is no art for the parts, so no picture: the bars share the
	// column's height with the weapons', evenly.)
	static const TCHAR* const PartSlots[] = { TEXT("head"), TEXT("arms"), TEXT("core"), TEXT("legs") };
	for (int32 i = 0; i < 4; ++i)
	{
		const UWWOFramePartData* Part = L->GetFramePart(PartSlots[i]);
		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();
		Row->AddChildToHorizontalBox(Marked(FLinearColor(1, 1, 1, 0.3f)))->SetPadding(FMargin(0, 6.f * K));
		UVerticalBox* Words = Tree->ConstructWidget<UVerticalBox>();
		Words->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, PartSlots[i], WWOUI::Type::Micro, Dim, 240));
		Words->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, Part ? Part->DisplayName : TEXT("Standard"), WWOUI::Type::Body, FLinearColor::White, 180))->SetPadding(FMargin(0, 6.f * K, 0, 0));
		UHorizontalBoxSlot* WordsSlot = Row->AddChildToHorizontalBox(Words);
		WordsSlot->SetPadding(FMargin(18.f * K, 0, 0, 0));
		WordsSlot->SetVerticalAlignment(VAlign_Center);
		WordsSlot->SetSize(WWOUI::Fill(1.f));
		UTextBlock* Detail = WWOUI::Spaced(WidgetTree, Part ? Part->Summary().Replace(TEXT("\n"), TEXT(" ")) : FString(), WWOUI::Type::Micro, Dim, 60, true);
		Detail->SetJustification(ETextJustify::Right);
		UHorizontalBoxSlot* DetailSlot = Row->AddChildToHorizontalBox(Detail);
		DetailSlot->SetSize(WWOUI::Fill(0.8f));
		DetailSlot->SetVerticalAlignment(VAlign_Center);
		DetailSlot->SetPadding(FMargin(12.f * K, 0, 0, 0));
		UWidget* PartCard = Card(Row, Open(TEXT("Frame Parts")), WWOUI::CardColor, WWOUI::CardEdgeStrong, WWOUI::BoxEdge, BarPad);
		HoverTiles.Add({ PartCard, FString(TEXT("part_")) + PartSlots[i] });
		AddBar(PartCard);
	}
	UHorizontalBoxSlot* RightSlot = Overview->AddChildToHorizontalBox(Right);
	RightSlot->SetSize(WWOUI::Fill(0.31f));
	RightSlot->SetPadding(FMargin(36.f * K, 40.f * K, 16.f * K, 28.f * K));
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
	UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();
	// One choice: a square of initials (wider, with a second line, for a weapon config).
	// Symbol: the item's badge filling the square in place of the initials.
	auto Option = [&](const FString& Label, const FString& Under, const FString& Tip, bool bOn, const FLinearColor& Fill, const FLinearColor& Edge, TFunction<void()> Pick,
		UTexture2D* Symbol = nullptr) -> UWidget*
	{
		if (Symbol)
		{
			UWidget* Box = Card(WWOUI::FillWithSymbol(WidgetTree, Symbol), [WeakThis, Pick] { Pick(); if (WeakThis.IsValid()) WeakThis->BuildOverview(); },
				Fill, Edge, bOn ? 4.f : 2.f, FMargin(bOn ? 4.f : 2.f));
			WWOUI::SetTip(WidgetTree, Box, Tip);
			HoverNames.Add({ Box, Tip });
			USizeBox* Size = Tree->ConstructWidget<USizeBox>();
			Size->SetWidthOverride(56.f * K);
			Size->SetHeightOverride(56.f * K);
			Size->AddChild(Box);
			Row->AddChildToHorizontalBox(Size)->SetPadding(FMargin(Row->GetChildrenCount() > 1 ? 8.f * K : 0.f, 0, 0, 0));
			return Box;
		}
		UVerticalBox* In = Tree->ConstructWidget<UVerticalBox>();
		UTextBlock* Text = WWOUI::Spaced(WidgetTree, Label, Under.IsEmpty() ? WWOUI::Type::Title : WWOUI::Type::Small, bOn ? FLinearColor::White : FLinearColor(1, 1, 1, 0.85f), 40);
		Text->SetJustification(ETextJustify::Center);
		UVerticalBoxSlot* TextSlot = In->AddChildToVerticalBox(Text);
		TextSlot->SetSize(WWOUI::Fill(1.f));
		TextSlot->SetVerticalAlignment(VAlign_Center);
		if (!Under.IsEmpty())
		{
			UTextBlock* Second = WWOUI::Spaced(WidgetTree, Under, WWOUI::Type::Micro, FLinearColor(1, 1, 1, 0.6f), 60);
			Second->SetJustification(ETextJustify::Center);
			In->AddChildToVerticalBox(Second);
		}
		UWidget* Box = Card(In, [WeakThis, Pick] { Pick(); if (WeakThis.IsValid()) WeakThis->BuildOverview(); }, Fill, Edge, bOn ? 4.f : 2.f, FMargin(4.f));
		WWOUI::SetTip(WidgetTree, Box, Tip);
		HoverNames.Add({ Box, Tip }); // its notes in the Build section light up while it's hovered
		USizeBox* Size = Tree->ConstructWidget<USizeBox>();
		Size->SetWidthOverride((Under.IsEmpty() ? 56.f : 132.f) * K);
		Size->SetHeightOverride(56.f * K);
		Size->AddChild(Box);
		Row->AddChildToHorizontalBox(Size)->SetPadding(FMargin(Row->GetChildrenCount() > 1 ? 8.f * K : 0.f, 0, 0, 0));
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
			UHorizontalBoxSlot* None = Row->AddChildToHorizontalBox(WWOUI::Spaced(WidgetTree, TEXT("No saved configs yet · save one on the Weapon tab"), WWOUI::Type::Caption, FLinearColor(1, 1, 1, 0.7f), 100));
			None->SetVerticalAlignment(VAlign_Center);
			None->SetPadding(FMargin(8.f * K, 14.f * K));
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
	if (Row->GetChildrenCount() == 0) return;
	UBorder* Panel = Tree->ConstructWidget<UBorder>();
	Panel->SetBrush(WWOUI::RoundBrush(FLinearColor(0.07f, 0.075f, 0.09f, 0.98f), WWOUI::Corner, WWO::WithAlpha(Accent, 0.9f), 1.5f));
	Panel->SetPadding(FMargin(10.f * K));
	Panel->SetContent(Row);
	// Just under the tile, its left edge on the tile's (pulled in from the window's right edge when
	// the row would run past it; the last layout pass's width is not known yet, so by a count).
	const FGeometry& RootGeo = Root->GetCachedGeometry();
	const FGeometry& TileGeo = Tile->GetCachedGeometry();
	const FVector2D At = RootGeo.AbsoluteToLocal(TileGeo.GetAbsolutePositionAtCoordinates(FVector2D(0.f, 1.f)));
	float Width = 20.f * K;
	const bool bWide = Name.StartsWith(TEXT("weapon")) || Name.StartsWith(TEXT("part_"));
	for (int32 i = 0; i < Row->GetChildrenCount(); ++i) Width += (bWide ? 140.f : 64.f) * K;
	if (Name.StartsWith(TEXT("weapon")) && L->GetConfigs().Num() == 0) Width = 520.f * K;
	const float Left = FMath::Clamp((float)At.X, 8.f, FMath::Max((float)RootGeo.GetLocalSize().X - Width - 8.f, 8.f));
	UOverlaySlot* PopupSlot = Root->AddChildToOverlay(Panel);
	PopupSlot->SetHorizontalAlignment(HAlign_Left);
	// Under the tile, or over it when the tile is low in the window (the bottom row of the shell).
	const float RootHeight = (float)RootGeo.GetLocalSize().Y;
	if (At.Y > RootHeight * 0.8f)
	{
		const FVector2D Top = RootGeo.AbsoluteToLocal(TileGeo.GetAbsolutePositionAtCoordinates(FVector2D(0.f, 0.f)));
		PopupSlot->SetVerticalAlignment(VAlign_Bottom);
		PopupSlot->SetPadding(FMargin(Left, 0, 0, RootHeight - (float)Top.Y + 4.f));
	}
	else
	{
		PopupSlot->SetVerticalAlignment(VAlign_Top);
		PopupSlot->SetPadding(FMargin(Left, At.Y + 4.f, 0, 0));
	}
	HoverPopup = Panel;
	HoverPicker = Name;
	HoverClose.Reset();
}
