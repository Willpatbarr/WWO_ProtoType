#include "UI/WWOLaunchMenu.h"
#include "UI/WWOSettingsMenu.h"
#include "UI/WWOMenuCursor.h"
#include "Net/WWONetSubsystem.h"
#include "Player/WWOPlayerController.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOSound.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "WhatWasOurs.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"

namespace
{
	constexpr float K = UWWOSettingsMenu::K;
	// The character menu's colors.
	const FLinearColor PageColor(0.1f, 0.11f, 0.13f);
	const FLinearColor CardColor(0.115f, 0.125f, 0.148f);
	const FLinearColor CardEdge(1.f, 1.f, 1.f, 0.45f);
	const FLinearColor Dim(1.f, 1.f, 1.f, 0.6f);
	const FLinearColor Faint(1.f, 1.f, 1.f, 0.4f);
	constexpr double SearchEvery = 4.0; // s between automatic searches while the Join list shows
	constexpr double StartGiveUp = 10.0; // s after START before the button comes back (a join that went nowhere)

	FSlateChildSize FillSize(float Value = 1.f)
	{
		FSlateChildSize Size(ESlateSizeRule::Fill);
		Size.Value = Value;
		return Size;
	}

	UWidget* Rule(UWidgetTree* Tree, const FLinearColor& Color, float Height = 1.5f)
	{
		UBorder* Line = Tree->ConstructWidget<UBorder>();
		Line->SetBrushColor(WWO::Lin(Color));
		USizeBox* Box = Tree->ConstructWidget<USizeBox>();
		Box->SetHeightOverride(Height);
		Box->AddChild(Line);
		return Box;
	}

	UWidget* Sized(UWidgetTree* Tree, UWidget* Inside, float MinWidth, float MinHeight)
	{
		USizeBox* Box = Tree->ConstructWidget<USizeBox>();
		if (MinWidth > 0.f) Box->SetMinDesiredWidth(MinWidth * K);
		if (MinHeight > 0.f) Box->SetMinDesiredHeight(MinHeight * K);
		Box->AddChild(Inside);
		return Box;
	}
}

void UWWOLaunchMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// A spare settings menu, never shown: only its MakeText helper is used (as the character menu does).
	Styler = CreateWidget<UWWOSettingsMenu>(GetOwningPlayer(), UWWOSettingsMenu::StaticClass());
	WidgetTree->RootWidget = WidgetTree->ConstructWidget<UOverlay>();
	SetCursor(EMouseCursor::None); // the menu draws the ring cursor instead (NativePaint)
}

int32 UWWOLaunchMenu::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!bOpen) return Layer;
	UWWOSettingsMenu::PaintCursor(this, AllottedGeometry, OutDrawElements, Layer + 1);
	return Layer + 1;
}

FLinearColor UWWOLaunchMenu::Accent() const
{
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	return L && L->GetFlora() ? WWO::Lightened(L->GetFlora()->Color, 0.15f) : FLinearColor(0.72f, 0.5f, 1.f);
}

UTextBlock* UWWOLaunchMenu::Spaced(const FString& Text, float FontPx, const FLinearColor& Color, int32 Spacing, bool bWrap) const
{
	// Capitals with wide letter spacing: the menus' label style.
	UTextBlock* T = Styler->MakeText(Text.ToUpper(), FontPx, Color, bWrap);
	FSlateFontInfo F = T->GetFont();
	F.LetterSpacing = Spacing;
	T->SetFont(F);
	return T;
}

UButton* UWWOLaunchMenu::Boxed(UWidget* Inside, const FLinearColor& Fill, const FLinearColor& Edge, float EdgeWidth, const FMargin& Padding, TFunction<void()> Fn)
{
	UButton* B = Styler->WidgetTree->ConstructWidget<UButton>();
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	B->IsFocusable = false; // the cursor drives everything, not focus
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	const bool bPlain = Fill.A <= 0.f;
	FButtonStyle Style;
	Style.SetNormal(UWWOSettingsMenu::RoundBrush(Fill, 5.f, Edge, EdgeWidth));
	Style.SetHovered(UWWOSettingsMenu::RoundBrush(bPlain ? FLinearColor(1, 1, 1, 0.06f) : WWO::Lightened(Fill, 0.08f), 5.f, bPlain ? Edge : FLinearColor(1, 1, 1, 0.9f), EdgeWidth));
	Style.SetPressed(UWWOSettingsMenu::RoundBrush(bPlain ? FLinearColor(1, 1, 1, 0.1f) : WWO::Lightened(Fill, 0.14f), 5.f, bPlain ? Edge : FLinearColor::White, EdgeWidth));
	Style.SetNormalPadding(FMargin(0));
	Style.SetPressedPadding(FMargin(0));
	B->SetStyle(Style);
	UButtonSlot* Slot = Cast<UButtonSlot>(B->AddChild(Inside));
	Slot->SetPadding(Padding * K);
	Slot->SetHorizontalAlignment(HAlign_Fill);
	Slot->SetVerticalAlignment(VAlign_Center);
	UWWOClickHandler* H = NewObject<UWWOClickHandler>(this);
	H->Fn = [Fn = MoveTemp(Fn), Weak = TWeakObjectPtr<UWWOLaunchMenu>(this)] { if (Weak.IsValid()) UWWOSound::PlayFlat(Weak.Get(), "ui_click"); if (Fn) Fn(); };
	B->OnClicked.AddDynamic(H, &UWWOClickHandler::Handle);
	Handlers.Add(H);
	return B;
}

UButton* UWWOLaunchMenu::Choice(const FString& Title, const FString& Sub, bool bOn, TFunction<void()> Fn, float TitlePx, const FString& Tag)
{
	UWidgetTree* Tree = Styler->WidgetTree;
	const FLinearColor A = Accent();
	UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();
	// The picked one carries the accent: a bar down its left edge, its outline and a tinted fill.
	UBorder* Mark = Tree->ConstructWidget<UBorder>();
	Mark->SetBrushColor(WWO::Lin(bOn ? A : FLinearColor(1, 1, 1, 0.12f)));
	USizeBox* MarkBox = Tree->ConstructWidget<USizeBox>();
	MarkBox->SetWidthOverride(3.f * K);
	MarkBox->AddChild(Mark);
	Row->AddChildToHorizontalBox(MarkBox)->SetVerticalAlignment(VAlign_Fill);
	UVerticalBox* Words = Tree->ConstructWidget<UVerticalBox>();
	Words->AddChildToVerticalBox(Spaced(Title, TitlePx, FLinearColor::White, 220));
	if (!Sub.IsEmpty()) Words->AddChildToVerticalBox(Spaced(Sub, 11.f, Dim, 90, true))->SetPadding(FMargin(0, 6.f * K, 0, 0));
	UHorizontalBoxSlot* WordsSlot = Row->AddChildToHorizontalBox(Words);
	WordsSlot->SetSize(FillSize());
	WordsSlot->SetVerticalAlignment(VAlign_Center);
	WordsSlot->SetPadding(FMargin(16.f * K, 0, 8.f * K, 0));
	if (!Tag.IsEmpty())
	{
		UHorizontalBoxSlot* TagSlot = Row->AddChildToHorizontalBox(Spaced(Tag, 12.f, bOn ? A : Dim, 160));
		TagSlot->SetVerticalAlignment(VAlign_Center);
		TagSlot->SetPadding(FMargin(8.f * K, 0, 4.f * K, 0));
	}
	return Boxed(Row, bOn ? WWO::Lightened(CardColor, 0.07f) : CardColor, bOn ? A : CardEdge, bOn ? 2.f : 1.5f, FMargin(10.f, 12.f, 14.f, 12.f), MoveTemp(Fn));
}

UWidget* UWWOLaunchMenu::StepLabel(int32 Number, const FString& Text) const
{
	UHorizontalBox* Row = Styler->WidgetTree->ConstructWidget<UHorizontalBox>();
	Row->AddChildToHorizontalBox(Spaced(FString::Printf(TEXT("%02d"), Number), 13.f, Accent(), 160))->SetVerticalAlignment(VAlign_Center);
	UHorizontalBoxSlot* Slot = Row->AddChildToHorizontalBox(Spaced(Text, 13.f, Dim, 260));
	Slot->SetVerticalAlignment(VAlign_Center);
	Slot->SetPadding(FMargin(14.f * K, 0, 0, 0));
	return Row;
}

FString UWWOLaunchMenu::GamesSignature() const
{
	FString Text;
	if (const UWWONetSubsystem* Net = UWWONetSubsystem::Get(this))
	{
		for (const FWWOGameInfo& G : Net->Games) Text += FString::Printf(TEXT("%s|%s|%d|%d|%d;"), *G.Host, *G.Map.ToString(), G.bPvP, G.Players, G.MaxPlayers);
	}
	return Text;
}

void UWWOLaunchMenu::SetWorldHidden(bool bHidden)
{
	// The page is opaque: the paused arena behind it needn't be drawn. (The character menu's
	// preview is a scene capture of its own and still renders.)
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr) Viewport->bDisableWorldRendering = bHidden;
}

FString UWWOLaunchMenu::StatusLine() const
{
	// The network's last word: on the title (a lost connection), while looking for games, and
	// while a game is starting. Not beside the host and local choices, where a search that
	// finished late would say "no games found".
	const UWWONetSubsystem* Net = UWWONetSubsystem::Get(this);
	if (!Net || (bSetup && Connection != ConnJoin && StartedAt <= 0.0)) return FString();
	return Net->Status;
}

// --- Open, close, steps ------------------------------------------------------------------------

void UWWOLaunchMenu::Open()
{
	bOpen = true;
	bSetup = false;
	Connection = ConnNone;
	Mode = ModeNone;
	Map = NAME_None;
	Game = INDEX_NONE;
	StartedAt = 0.0;
	SetVisibility(ESlateVisibility::Visible);
	SetWorldHidden(true);
	Rebuild();
}

void UWWOLaunchMenu::NativeDestruct()
{
	SetWorldHidden(false); // the map it started is loading: that one is drawn
	Super::NativeDestruct();
}

void UWWOLaunchMenu::Close()
{
	SetWorldHidden(false);
	bOpen = false;
	SetVisibility(ESlateVisibility::Collapsed);
}

void UWWOLaunchMenu::Back()
{
	// One choice back at a time, then the title.
	if (!bSetup || StartedAt > 0.0) return;
	if (!Map.IsNone() || Game != INDEX_NONE) { Map = NAME_None; Game = INDEX_NONE; GameKey.Reset(); }
	else if (Mode != ModeNone) Mode = ModeNone;
	else if (Connection != ConnNone) Connection = ConnNone;
	else bSetup = false;
	UWWOSound::PlayFlat(this, "ui_close");
	Rebuild();
}

void UWWOLaunchMenu::Play()
{
	bSetup = true;
	Rebuild();
}

void UWWOLaunchMenu::ChooseConnection(int32 Choice)
{
	if (StartedAt > 0.0) return;
	bSetup = true;
	Connection = Choice;
	Mode = ModeNone;
	Map = NAME_None;
	Game = INDEX_NONE;
	GameKey.Reset();
	UWWONetSubsystem* Net = UWWONetSubsystem::Get(this);
	if (Net) Net->Status.Reset(); // the last search's or the last game's word doesn't belong to this choice
	if (Connection == ConnJoin)
	{
		// Look right away; NativeTick keeps looking while the list shows.
		if (Net) Net->FindGames();
		NextSearch = FPlatformTime::Seconds() + SearchEvery;
	}
	Rebuild();
}

void UWWOLaunchMenu::ChooseMode(int32 Choice)
{
	if (StartedAt > 0.0 || Connection == ConnNone || Connection == ConnJoin) return;
	Mode = Choice;
	// The picked map stays if it suits the new mode too.
	const FWWOMapInfo* Info = UWWONetSubsystem::FindMap(Map);
	if (!Info || !(Mode == ModePvP ? Info->bPvP : Info->bPvE)) Map = NAME_None;
	Rebuild();
}

void UWWOLaunchMenu::ChooseMap(FName Level)
{
	if (StartedAt > 0.0 || Mode == ModeNone) return;
	Map = Level;
	Rebuild();
}

void UWWOLaunchMenu::ChooseGame(int32 Index)
{
	const UWWONetSubsystem* Net = UWWONetSubsystem::Get(this);
	if (StartedAt > 0.0 || Connection != ConnJoin || !Net || !Net->Games.IsValidIndex(Index)) return;
	if (Net->Games[Index].Players >= Net->Games[Index].MaxPlayers) return; // full
	Game = Index;
	GameKey = Net->Games[Index].Host + TEXT("|") + Net->Games[Index].Map.ToString();
	Rebuild();
}

bool UWWOLaunchMenu::CanStart() const
{
	if (!bSetup || StartedAt > 0.0) return false;
	if (Connection == ConnJoin) return Game != INDEX_NONE;
	return Connection != ConnNone && Mode != ModeNone && !Map.IsNone();
}

void UWWOLaunchMenu::Start()
{
	UWWONetSubsystem* Net = UWWONetSubsystem::Get(this);
	if (!Net || !CanStart()) return;
	const bool bPvP = Mode == ModePvP;
	const FWWOMapInfo* Info = UWWONetSubsystem::FindMap(Map);
	UE_LOG(LogWWO, Display, TEXT("Launch: starting (%s)"), *DebugState());
	StartedAt = FPlatformTime::Seconds();
	switch (Connection)
	{
	case ConnHost: Net->Host(Map, bPvP); break; // bots: the host's Bots setting (Settings, Enemies)
	// Alone, PvP is against bots: enough to fill both teams of the map's size.
	case ConnLocal: Net->PlayLocal(Map, bPvP, bPvP && Info ? Info->TeamSize * 2 - 1 : -1); break;
	case ConnJoin: Net->JoinGame(Game); break;
	default: break;
	}
	Rebuild();
}

FString UWWOLaunchMenu::DebugState() const
{
	if (!bSetup) return TEXT("title");
	static const TCHAR* const Conns[] = { TEXT("none"), TEXT("host"), TEXT("join"), TEXT("local") };
	static const TCHAR* const Modes[] = { TEXT("none"), TEXT("pvp"), TEXT("pve") };
	return FString::Printf(TEXT("setup %s %s %s%s%s"), Conns[Connection + 1], Modes[Mode + 1], Map.IsNone() ? TEXT("-") : *Map.ToString(),
		Game != INDEX_NONE ? *FString::Printf(TEXT(" game %d"), Game) : TEXT(""), CanStart() ? TEXT(" ready") : TEXT(""));
}

void UWWOLaunchMenu::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bOpen) return;
	UWWONetSubsystem* Net = UWWONetSubsystem::Get(this);
	if (StatusText) StatusText->SetText(FText::FromString(StatusLine().ToUpper()));
	// Settings is see-through and the character menu has its preview: with either up, the world draws.
	if (const AWWOPlayerController* PC = Cast<AWWOPlayerController>(GetOwningPlayer()))
	{
		SetWorldHidden(!(PC->IsCharacterMenuOpen() || (PC->GetSettingsMenu() && PC->GetSettingsMenu()->IsOpen())));
	}
	const double Now = FPlatformTime::Seconds();
	if (StartedAt > 0.0 && Now - StartedAt > StartGiveUp)
	{
		StartedAt = 0.0; // it never left this screen: offer the button again
		Rebuild();
	}
	if (!bSetup || Connection != ConnJoin || !Net || StartedAt > 0.0) return;
	// The games on the network: search again every few seconds, and redraw when a search starts or ends.
	if (!Net->bSearching && Now >= NextSearch)
	{
		Net->FindGames();
		NextSearch = Now + SearchEvery;
	}
	if (Net->GamesSerial != SeenGames && GamesSignature() == ShownGames)
	{
		SeenGames = Net->GamesSerial; // the same games as are showing: leave the list alone (no dropped click, no jump to the top)
		for (int32 i = 0; i < Net->Games.Num(); ++i)
		{
			if (!GameKey.IsEmpty() && Net->Games[i].Host + TEXT("|") + Net->Games[i].Map.ToString() == GameKey) Game = i;
		}
	}
	else if (Net->GamesSerial != SeenGames)
	{
		// Keep the picked game picked if it's still there.
		Game = INDEX_NONE;
		for (int32 i = 0; i < Net->Games.Num(); ++i)
		{
			if (!GameKey.IsEmpty() && Net->Games[i].Host + TEXT("|") + Net->Games[i].Map.ToString() == GameKey) Game = i;
		}
		if (Game == INDEX_NONE) GameKey.Reset();
		Rebuild();
	}
	else if (Net->bSearching != bSeenSearching && Net->Games.Num() == 0) Rebuild();
}

// --- Building ----------------------------------------------------------------------------------

void UWWOLaunchMenu::Rebuild()
{
	UOverlay* Root = Cast<UOverlay>(WidgetTree->RootWidget);
	if (!Root || !Styler) return;
	Root->ClearChildren();
	Handlers.Reset();
	StatusText = nullptr;
	if (const UWWONetSubsystem* Net = UWWONetSubsystem::Get(this))
	{
		SeenGames = Net->GamesSerial;
		bSeenSearching = Net->bSearching;
	}
	ShownGames = GamesSignature();
	UBorder* Page = Styler->WidgetTree->ConstructWidget<UBorder>();
	Page->SetBrushColor(WWO::Lin(PageColor));
	Page->SetPadding(FMargin(40.f * K, 12.f * K, 40.f * K, 28.f * K));
	Page->SetContent(bSetup ? BuildSetup() : BuildTitle());
	UOverlaySlot* PageSlot = Root->AddChildToOverlay(Page);
	PageSlot->SetHorizontalAlignment(HAlign_Fill);
	PageSlot->SetVerticalAlignment(VAlign_Fill);
}

UWidget* UWWOLaunchMenu::BuildTitle()
{
	UWidgetTree* Tree = Styler->WidgetTree;
	TWeakObjectPtr<UWWOLaunchMenu> WeakThis = this;
	TWeakObjectPtr<APlayerController> WeakPC = GetOwningPlayer();
	const FLinearColor A = Accent();
	UOverlay* Whole = Tree->ConstructWidget<UOverlay>();

	// The middle: the mark, the name, a rule, then PLAY.
	UVerticalBox* Middle = Tree->ConstructWidget<UVerticalBox>();
	Middle->AddChildToVerticalBox(Spaced(TEXT("//"), 44.f, A, 0))->SetHorizontalAlignment(HAlign_Center);
	UTextBlock* Name = Spaced(TEXT("WHAT WAS OURS"), 84.f, FLinearColor::White, 520);
	FSlateFontInfo NameFont = Name->GetFont();
	NameFont.TypefaceFontName = FName("Bold");
	Name->SetFont(NameFont);
	UVerticalBoxSlot* NameSlot = Middle->AddChildToVerticalBox(Name);
	NameSlot->SetHorizontalAlignment(HAlign_Center);
	NameSlot->SetPadding(FMargin(0, 14.f * K, 0, 0));
	USizeBox* RuleBox = Tree->ConstructWidget<USizeBox>();
	RuleBox->SetWidthOverride(560.f * K);
	RuleBox->AddChild(Rule(Tree, FLinearColor(1, 1, 1, 0.3f)));
	UVerticalBoxSlot* RuleSlot = Middle->AddChildToVerticalBox(RuleBox);
	RuleSlot->SetHorizontalAlignment(HAlign_Center);
	RuleSlot->SetPadding(FMargin(0, 26.f * K, 0, 0));

	UTextBlock* PlayLabel = Spaced(TEXT("PLAY"), 46.f, FLinearColor::White, 700);
	PlayLabel->SetJustification(ETextJustify::Center);
	UOverlay* PlayInside = Tree->ConstructWidget<UOverlay>();
	UOverlaySlot* PlayLabelSlot = PlayInside->AddChildToOverlay(PlayLabel);
	PlayLabelSlot->SetHorizontalAlignment(HAlign_Center);
	PlayLabelSlot->SetVerticalAlignment(VAlign_Center);
	UButton* PlayButton = Boxed(PlayInside, WWO::Lightened(CardColor, 0.05f), A, 2.5f, FMargin(0), [WeakThis] { if (WeakThis.IsValid()) WeakThis->Play(); });
	UVerticalBoxSlot* PlaySlot = Middle->AddChildToVerticalBox(Sized(Tree, PlayButton, 460.f, 124.f));
	PlaySlot->SetHorizontalAlignment(HAlign_Center);
	PlaySlot->SetPadding(FMargin(0, 64.f * K, 0, 0));

	// Under it, the small ones.
	UHorizontalBox* Links = Tree->ConstructWidget<UHorizontalBox>();
	auto Link = [&](const FString& Text, TFunction<void()> Fn)
	{
		if (Links->GetChildrenCount() > 0)
		{
			UHorizontalBoxSlot* Bar = Links->AddChildToHorizontalBox(Spaced(TEXT("|"), 15.f, FLinearColor(1, 1, 1, 0.25f), 0));
			Bar->SetVerticalAlignment(VAlign_Center);
			Bar->SetPadding(FMargin(18.f * K, 0));
		}
		Links->AddChildToHorizontalBox(Boxed(Spaced(Text, 15.f, FLinearColor(1, 1, 1, 0.8f), 220), FLinearColor(0, 0, 0, 0), FLinearColor(0, 0, 0, 0), 0.f, FMargin(14.f, 9.f, 10.f, 9.f), MoveTemp(Fn)));
	};
	Link(TEXT("Loadout"), [WeakPC] { if (AWWOPlayerController* PC = Cast<AWWOPlayerController>(WeakPC.Get())) PC->ToggleCharacterMenu(); });
	Link(TEXT("Settings"), [WeakPC] { if (AWWOPlayerController* PC = Cast<AWWOPlayerController>(WeakPC.Get())) PC->ToggleSettingsMenu(); });
	Link(TEXT("Quit"), [WeakPC] { if (WeakPC.IsValid()) UKismetSystemLibrary::QuitGame(WeakPC.Get(), WeakPC.Get(), EQuitPreference::Quit, false); });
	UVerticalBoxSlot* LinksSlot = Middle->AddChildToVerticalBox(Links);
	LinksSlot->SetHorizontalAlignment(HAlign_Center);
	LinksSlot->SetPadding(FMargin(0, 34.f * K, 0, 0));

	UOverlaySlot* MiddleSlot = Whole->AddChildToOverlay(Middle);
	MiddleSlot->SetHorizontalAlignment(HAlign_Center);
	MiddleSlot->SetVerticalAlignment(VAlign_Center);

	// Along the bottom: what happened last (a lost connection), and the keys.
	StatusText = Spaced(StatusLine(), 13.f, FLinearColor(1.f, 0.72f, 0.4f), 160);
	UOverlaySlot* StatusSlot = Whole->AddChildToOverlay(StatusText);
	StatusSlot->SetHorizontalAlignment(HAlign_Center);
	StatusSlot->SetVerticalAlignment(VAlign_Bottom);
	StatusSlot->SetPadding(FMargin(0, 0, 0, 44.f * K));
	UOverlaySlot* KeysSlot = Whole->AddChildToOverlay(Spaced(TEXT("Stick or mouse  move   ·   Cross or click  select"), 11.f, Faint, 120));
	KeysSlot->SetHorizontalAlignment(HAlign_Center);
	KeysSlot->SetVerticalAlignment(VAlign_Bottom);
	return Whole;
}

UWidget* UWWOLaunchMenu::BuildSetup()
{
	UWidgetTree* Tree = Styler->WidgetTree;
	TWeakObjectPtr<UWWOLaunchMenu> WeakThis = this;
	const UWWONetSubsystem* Net = UWWONetSubsystem::Get(this);
	const FLinearColor A = Accent();
	UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>();

	// Header, as on the other menus: the game's name, where you are, the keys.
	UHorizontalBox* Header = Tree->ConstructWidget<UHorizontalBox>();
	UHorizontalBox* Name = Tree->ConstructWidget<UHorizontalBox>();
	Name->AddChildToHorizontalBox(Spaced(TEXT("//"), 26.f, FLinearColor(0.72f, 0.5f, 1.f), 0))->SetVerticalAlignment(VAlign_Center);
	UHorizontalBoxSlot* NameText = Name->AddChildToHorizontalBox(Spaced(TEXT("WHAT WAS OURS"), 17.f, FLinearColor::White, 420));
	NameText->SetVerticalAlignment(VAlign_Center);
	NameText->SetPadding(FMargin(18.f * K, 0, 0, 0));
	UHorizontalBoxSlot* NameSlot = Header->AddChildToHorizontalBox(Name);
	NameSlot->SetSize(FillSize());
	NameSlot->SetVerticalAlignment(VAlign_Center);
	UVerticalBox* Tab = Tree->ConstructWidget<UVerticalBox>();
	Tab->AddChildToVerticalBox(Spaced(TEXT("GAME SETUP"), 17.f, FLinearColor::White, 180))->SetPadding(FMargin(8.f * K, 6.f * K, 4.f * K, 6.f * K));
	UBorder* TabLine = Tree->ConstructWidget<UBorder>();
	TabLine->SetBrushColor(WWO::Lin(A));
	USizeBox* TabLineBox = Tree->ConstructWidget<USizeBox>();
	TabLineBox->SetHeightOverride(3.f * K);
	TabLineBox->AddChild(TabLine);
	Tab->AddChildToVerticalBox(TabLineBox);
	Header->AddChildToHorizontalBox(Tab)->SetVerticalAlignment(VAlign_Center);
	UTextBlock* Keys = Spaced(TEXT("Cross or click  select   ·   Circle or Esc  back"), 11.f, Faint, 120);
	Keys->SetJustification(ETextJustify::Right);
	UHorizontalBoxSlot* KeysSlot = Header->AddChildToHorizontalBox(Keys);
	KeysSlot->SetSize(FillSize());
	KeysSlot->SetVerticalAlignment(VAlign_Center);
	Column->AddChildToVerticalBox(Header);
	Column->AddChildToVerticalBox(Rule(Tree, FLinearColor(1, 1, 1, 0.3f)))->SetPadding(FMargin(0, 4.f * K, 0, 0));

	UHorizontalBox* Body = Tree->ConstructWidget<UHorizontalBox>();
	Column->AddChildToVerticalBox(Body)->SetSize(FillSize());

	// Left: the connection, then the mode.
	UVerticalBox* Left = Tree->ConstructWidget<UVerticalBox>();
	Left->AddChildToVerticalBox(StepLabel(1, TEXT("Connection")))->SetPadding(FMargin(0, 30.f * K, 0, 12.f * K));
	auto AddChoice = [&](UVerticalBox* To, UButton* Button)
	{
		To->AddChildToVerticalBox(Sized(Tree, Button, 0.f, 74.f))->SetPadding(FMargin(0, 0, 0, 10.f * K));
	};
	AddChoice(Left, Choice(TEXT("Online host"), TEXT("Start a game others on your network can join"), Connection == ConnHost,
		[WeakThis] { if (WeakThis.IsValid()) WeakThis->ChooseConnection(ConnHost); }));
	AddChoice(Left, Choice(TEXT("Online join"), TEXT("Find a game on your network"), Connection == ConnJoin,
		[WeakThis] { if (WeakThis.IsValid()) WeakThis->ChooseConnection(ConnJoin); }));
	AddChoice(Left, Choice(TEXT("Local"), TEXT("Play on this machine only"), Connection == ConnLocal,
		[WeakThis] { if (WeakThis.IsValid()) WeakThis->ChooseConnection(ConnLocal); }));
	if (Connection == ConnHost || Connection == ConnLocal)
	{
		Left->AddChildToVerticalBox(StepLabel(2, TEXT("Mode")))->SetPadding(FMargin(0, 26.f * K, 0, 12.f * K));
		AddChoice(Left, Choice(TEXT("PvP"), Connection == ConnLocal ? TEXT("Team deathmatch against bots") : TEXT("Team deathmatch, up to 6 v 6"), Mode == ModePvP,
			[WeakThis] { if (WeakThis.IsValid()) WeakThis->ChooseMode(ModePvP); }));
		AddChoice(Left, Choice(TEXT("PvE"), Connection == ConnLocal ? TEXT("Wave survival") : TEXT("Wave survival together, up to 6 players"), Mode == ModePvE,
			[WeakThis] { if (WeakThis.IsValid()) WeakThis->ChooseMode(ModePvE); }));
	}
	USizeBox* LeftBox = Tree->ConstructWidget<USizeBox>();
	LeftBox->SetWidthOverride(440.f * K);
	LeftBox->AddChild(Left);
	Body->AddChildToHorizontalBox(LeftBox)->SetPadding(FMargin(0, 0, 40.f * K, 0));

	// Right: the maps (Host, Local) or the games found (Join), then the summary and START.
	UVerticalBox* Right = Tree->ConstructWidget<UVerticalBox>();
	UScrollBox* List = Tree->ConstructWidget<UScrollBox>();
	auto AddToList = [&](UWidget* W, float Height)
	{
		if (UScrollBoxSlot* Slot = Cast<UScrollBoxSlot>(List->AddChild(Height > 0.f ? Sized(Tree, W, 0.f, Height) : W))) Slot->SetPadding(FMargin(0, 0, 14.f * K, 10.f * K));
	};
	FString Summary;
	if (Connection == ConnJoin)
	{
		UHorizontalBox* Title = Tree->ConstructWidget<UHorizontalBox>();
		Title->AddChildToHorizontalBox(StepLabel(2, TEXT("Games on your network")))->SetSize(FillSize());
		Title->AddChildToHorizontalBox(Boxed(Spaced(TEXT("Refresh"), 12.f, FLinearColor(1, 1, 1, 0.85f), 180), CardColor, CardEdge, 1.5f, FMargin(14.f, 6.f, 10.f, 6.f),
			[WeakThis] { if (!WeakThis.IsValid()) return; if (UWWONetSubsystem* N = UWWONetSubsystem::Get(WeakThis.Get())) N->FindGames(); WeakThis->Rebuild(); }));
		Right->AddChildToVerticalBox(Title)->SetPadding(FMargin(0, 24.f * K, 14.f * K, 8.f * K));
		const int32 Count = Net ? Net->Games.Num() : 0;
		for (int32 i = 0; i < Count; ++i)
		{
			const FWWOGameInfo& G = Net->Games[i];
			const bool bFull = G.Players >= G.MaxPlayers;
			const FString Sub = FString::Printf(TEXT("%s  ·  %d of %d players%s"), *G.MapTitle, G.Players, G.MaxPlayers, bFull ? TEXT("  ·  full") : TEXT(""));
			AddToList(Choice(G.Host.IsEmpty() ? FString(TEXT("A game")) : G.Host + TEXT("'s game"), Sub, Game == i,
				[WeakThis, i] { if (WeakThis.IsValid()) WeakThis->ChooseGame(i); }, 19.f, G.bPvP ? TEXT("PvP") : TEXT("PvE")), 74.f);
		}
		if (Count == 0)
		{
			AddToList(Spaced(Net && Net->bSearching ? TEXT("Searching...") : TEXT("No games found yet. This list refreshes on its own; a host has to be on the same network."),
				14.f, Dim, 120, true), 0.f);
		}
		if (Net && Net->Games.IsValidIndex(Game)) Summary = FString::Printf(TEXT("Join  ·  %s  ·  %s"), Net->Games[Game].bPvP ? TEXT("PvP") : TEXT("PvE"), *Net->Games[Game].MapTitle);
	}
	else if (Connection != ConnNone && Mode != ModeNone)
	{
		Right->AddChildToVerticalBox(StepLabel(3, TEXT("Map")))->SetPadding(FMargin(0, 30.f * K, 0, 12.f * K));
		for (const FWWOMapInfo& Info : UWWONetSubsystem::Maps())
		{
			if (!(Mode == ModePvP ? Info.bPvP : Info.bPvE)) continue;
			const FName Level = Info.Level;
			AddToList(Choice(Info.Title, Info.Blurb, Map == Level, [WeakThis, Level] { if (WeakThis.IsValid()) WeakThis->ChooseMap(Level); }, 21.f), 80.f);
		}
		if (!Map.IsNone())
		{
			Summary = FString::Printf(TEXT("%s  ·  %s  ·  %s"), Connection == ConnHost ? TEXT("Online host") : TEXT("Local"), Mode == ModePvP ? TEXT("PvP") : TEXT("PvE"),
				*UWWONetSubsystem::MapTitle(Map));
		}
	}
	else
	{
		// Nothing to list yet: say what comes next.
		const FString Hint = Connection == ConnNone ? TEXT("Choose how to play.") : TEXT("Choose a mode.");
		Right->AddChildToVerticalBox(Spaced(Hint, 14.f, Faint, 140))->SetPadding(FMargin(0, 34.f * K, 0, 0));
	}
	Right->AddChildToVerticalBox(List)->SetSize(FillSize());

	// The bottom row: the status, what you picked, and the big button once everything is picked.
	UHorizontalBox* Bottom = Tree->ConstructWidget<UHorizontalBox>();
	UVerticalBox* Words = Tree->ConstructWidget<UVerticalBox>();
	if (!Summary.IsEmpty()) Words->AddChildToVerticalBox(Spaced(Summary, 15.f, FLinearColor::White, 200));
	StatusText = Spaced(StatusLine(), 12.f, FLinearColor(1.f, 0.72f, 0.4f), 140);
	Words->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0, 8.f * K, 0, 0));
	UHorizontalBoxSlot* WordsSlot = Bottom->AddChildToHorizontalBox(Words);
	WordsSlot->SetSize(FillSize());
	WordsSlot->SetVerticalAlignment(VAlign_Center);
	if (CanStart() || StartedAt > 0.0)
	{
		const bool bStarting = StartedAt > 0.0;
		const FString Text = bStarting ? (Connection == ConnJoin ? TEXT("Joining...") : TEXT("Starting...")) : (Connection == ConnJoin ? TEXT("Join game") : TEXT("Start game"));
		UTextBlock* Label = Spaced(Text, 28.f, bStarting ? Dim : FLinearColor::White, 420);
		Label->SetJustification(ETextJustify::Center);
		UOverlay* Inside = Tree->ConstructWidget<UOverlay>();
		UOverlaySlot* LabelSlot = Inside->AddChildToOverlay(Label);
		LabelSlot->SetHorizontalAlignment(HAlign_Center);
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		UButton* StartButton = Boxed(Inside, WWO::Lightened(CardColor, 0.05f), bStarting ? CardEdge : A, 2.5f, FMargin(0), [WeakThis] { if (WeakThis.IsValid()) WeakThis->Start(); });
		Bottom->AddChildToHorizontalBox(Sized(Tree, StartButton, 420.f, 92.f))->SetVerticalAlignment(VAlign_Center);
	}
	Right->AddChildToVerticalBox(Sized(Tree, Bottom, 0.f, 92.f))->SetPadding(FMargin(0, 14.f * K, 14.f * K, 0));
	Body->AddChildToHorizontalBox(Right)->SetSize(FillSize());
	return Column;
}
