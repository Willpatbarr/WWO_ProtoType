#include "UI/WWOLaunchMenu.h"
#include "Fonts/CompositeFont.h"
#include "Misc/Paths.h"
#include "UI/WWOSettingsMenu.h"
#include "UI/WWOUI.h"
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
#include "Components/EditableTextBox.h"
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
	constexpr float K = WWOUI::K;
	constexpr double SearchEvery = 4.0; // s between automatic searches while the Join list shows
	constexpr double StartGiveUp = 10.0; // s after START before the button comes back (a join that went nowhere)
}

void UWWOLaunchMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	WidgetTree->RootWidget = WidgetTree->ConstructWidget<UOverlay>();
	SetCursor(EMouseCursor::None); // the menu draws the ring cursor instead (NativePaint)
}

int32 UWWOLaunchMenu::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!bOpen) return Layer;
	WWOUI::PaintCursor(this, AllottedGeometry, OutDrawElements, Layer + 1);
	return Layer + 1;
}

UButton* UWWOLaunchMenu::Boxed(UWidget* Inside, const FLinearColor& Fill, const FLinearColor& Edge, float EdgeWidth, const FMargin& Padding, TFunction<void()> Fn)
{
	UButton* B = WidgetTree->ConstructWidget<UButton>();
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	B->IsFocusable = false; // the cursor drives everything, not focus
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	const bool bPlain = Fill.A <= 0.f;
	FButtonStyle Style;
	Style.SetNormal(WWOUI::RoundBrush(Fill, 5.f, Edge, EdgeWidth));
	Style.SetHovered(WWOUI::RoundBrush(bPlain ? FLinearColor(1, 1, 1, 0.06f) : WWO::Lightened(Fill, 0.08f), 5.f, bPlain ? Edge : WWOUI::HoverEdge, EdgeWidth));
	Style.SetPressed(WWOUI::RoundBrush(bPlain ? FLinearColor(1, 1, 1, 0.1f) : WWO::Lightened(Fill, 0.14f), 5.f, bPlain ? Edge : FLinearColor::White, EdgeWidth));
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
	UWidgetTree* Tree = WidgetTree;
	const FLinearColor A = WWOUI::Accent(this);
	UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();
	// The picked one carries the accent: a bar down its left edge, its outline and a tinted fill.
	UBorder* Mark = Tree->ConstructWidget<UBorder>();
	Mark->SetBrushColor(WWO::Lin(bOn ? A : FLinearColor(1, 1, 1, 0.12f)));
	USizeBox* MarkBox = Tree->ConstructWidget<USizeBox>();
	MarkBox->SetWidthOverride(3.f * K);
	MarkBox->AddChild(Mark);
	Row->AddChildToHorizontalBox(MarkBox)->SetVerticalAlignment(VAlign_Fill);
	UVerticalBox* Words = Tree->ConstructWidget<UVerticalBox>();
	Words->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, Title, TitlePx, FLinearColor::White, 220));
	if (!Sub.IsEmpty()) Words->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, Sub, 11.f, WWOUI::Dim, 90, true))->SetPadding(FMargin(0, 6.f * K, 0, 0));
	UHorizontalBoxSlot* WordsSlot = Row->AddChildToHorizontalBox(Words);
	WordsSlot->SetSize(WWOUI::Fill());
	WordsSlot->SetVerticalAlignment(VAlign_Center);
	WordsSlot->SetPadding(FMargin(16.f * K, 0, 8.f * K, 0));
	if (!Tag.IsEmpty())
	{
		UHorizontalBoxSlot* TagSlot = Row->AddChildToHorizontalBox(WWOUI::Spaced(WidgetTree, Tag, 12.f, bOn ? A : WWOUI::Dim, 160));
		TagSlot->SetVerticalAlignment(VAlign_Center);
		TagSlot->SetPadding(FMargin(8.f * K, 0, 4.f * K, 0));
	}
	return Boxed(Row, bOn ? WWO::Lightened(WWOUI::CardColor, 0.07f) : WWOUI::CardColor, bOn ? A : WWOUI::CardEdge, bOn ? 2.f : 1.5f, FMargin(10.f, 12.f, 14.f, 12.f), MoveTemp(Fn));
}

UWidget* UWWOLaunchMenu::StepLabel(int32 Number, const FString& Text) const
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	Row->AddChildToHorizontalBox(WWOUI::Spaced(WidgetTree, FString::Printf(TEXT("%02d"), Number), 13.f, WWOUI::Accent(this), 160))->SetVerticalAlignment(VAlign_Center);
	UHorizontalBoxSlot* Slot = Row->AddChildToHorizontalBox(WWOUI::Spaced(WidgetTree, Text, 13.f, WWOUI::Dim, 260));
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
	// Hosting or joining needs a gamer tag first (once everything else is picked, that is what's missing).
	if (bSetup && StartedAt <= 0.0 && NeedsTag() && CanStart()) return TEXT("Enter a gamer tag to ") + FString(Connection == ConnJoin ? TEXT("join") : TEXT("host"));
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
	if (StartedAt > 0.0 || Connection == ConnNone || Connection == ConnJoin || Connection == ConnTester) return;
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
	if (Connection == ConnTester) return true;
	return Connection != ConnNone && Mode != ModeNone && !Map.IsNone();
}

bool UWWOLaunchMenu::NeedsTag() const
{
	const UWWONetSubsystem* Net = UWWONetSubsystem::Get(this);
	return (Connection == ConnHost || Connection == ConnJoin) && (!Net || Net->GetGamerTag().IsEmpty());
}

void UWWOLaunchMenu::OnTagChanged(const FText& Text)
{
	// Kept as it is typed (cleaned: letters, digits, _ and - only); the box shows the cleaned tag.
	UWWONetSubsystem* Net = UWWONetSubsystem::Get(this);
	if (!Net) return;
	Net->SetGamerTag(Text.ToString());
	if (TagBox && Net->GetGamerTag() != Text.ToString()) TagBox->SetText(FText::FromString(Net->GetGamerTag()));
	if (StatusText) StatusText->SetText(FText::FromString(StatusLine()));
}

void UWWOLaunchMenu::Start()
{
	UWWONetSubsystem* Net = UWWONetSubsystem::Get(this);
	if (!Net || !CanStart()) return;
	if (NeedsTag()) // nobody hosts or joins without a name for the others to see
	{
		if (StatusText) StatusText->SetText(FText::FromString(StatusLine()));
		if (TagBox) TagBox->SetKeyboardFocus();
		return;
	}
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
	case ConnTester: Net->PlayTester(); break;
	default: break;
	}
	Rebuild();
}

FString UWWOLaunchMenu::DebugState() const
{
	if (!bSetup) return TEXT("title");
	static const TCHAR* const Conns[] = { TEXT("none"), TEXT("host"), TEXT("join"), TEXT("local"), TEXT("tester") };
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
	if (!Root) return;
	Root->ClearChildren();
	Handlers.Reset();
	StatusText = nullptr;
	if (const UWWONetSubsystem* Net = UWWONetSubsystem::Get(this))
	{
		SeenGames = Net->GamesSerial;
		bSeenSearching = Net->bSearching;
	}
	ShownGames = GamesSignature();
	UBorder* Page = WidgetTree->ConstructWidget<UBorder>();
	Page->SetBrushColor(WWO::Lin(WWOUI::PageColor));
	Page->SetPadding(FMargin(40.f * K, 12.f * K, 40.f * K, 28.f * K));
	Page->SetContent(bSetup ? BuildSetup() : BuildTitle());
	UOverlaySlot* PageSlot = Root->AddChildToOverlay(Page);
	PageSlot->SetHorizontalAlignment(HAlign_Fill);
	PageSlot->SetVerticalAlignment(VAlign_Fill);
}

UWidget* UWWOLaunchMenu::BuildTitle()
{
	UWidgetTree* Tree = WidgetTree;
	TWeakObjectPtr<UWWOLaunchMenu> WeakThis = this;
	TWeakObjectPtr<APlayerController> WeakPC = GetOwningPlayer();
	const FLinearColor A = WWOUI::Accent(this);
	UOverlay* Whole = Tree->ConstructWidget<UOverlay>();

	// The middle: the mark, the name, a rule, then PLAY.
	UVerticalBox* Middle = Tree->ConstructWidget<UVerticalBox>();
	// (The old "//" mark above the name is gone; its room is kept so nothing else moves.)
	UTextBlock* OldMark = WWOUI::Spaced(WidgetTree, TEXT("//"), 44.f, A, 0);
	OldMark->SetVisibility(ESlateVisibility::Hidden);
	Middle->AddChildToVerticalBox(OldMark)->SetHorizontalAlignment(HAlign_Center);
	UTextBlock* Name = WWOUI::Spaced(WidgetTree, TEXT("WHAT WAS OURS"), 84.f, FLinearColor::White, 520);
	FSlateFontInfo NameFont = Name->GetFont();
	NameFont.TypefaceFontName = FName("Bold");
	Name->SetFont(NameFont);
	// The name is the wordmark logo, as wide as the text it replaced (which keeps its room under it).
	UVerticalBoxSlot* NameSlot = Middle->AddChildToVerticalBox(WWOUI::MakeLogo(Tree, true, FVector2D(1060.f, 132.5f) * K, Name));
	NameSlot->SetHorizontalAlignment(HAlign_Center);
	NameSlot->SetPadding(FMargin(0, 14.f * K, 0, 0));
	USizeBox* RuleBox = Tree->ConstructWidget<USizeBox>();
	RuleBox->SetWidthOverride(560.f * K);
	RuleBox->AddChild(WWOUI::Rule(Tree, WWOUI::RuleColor));
	UVerticalBoxSlot* RuleSlot = Middle->AddChildToVerticalBox(RuleBox);
	RuleSlot->SetHorizontalAlignment(HAlign_Center);
	RuleSlot->SetPadding(FMargin(0, 26.f * K, 0, 0));

	UTextBlock* PlayLabel = WWOUI::Spaced(WidgetTree, TEXT("PLAY"), 46.f, FLinearColor::White, 700);
	{
		// PLAY is set in Poppins Light (Content/WWO/Fonts, SIL Open Font License), read from its file
		// once and kept. Without the file it stays in the menus' own font.
		static TSharedPtr<FCompositeFont> Poppins;
		const FString File = FPaths::ProjectContentDir() / TEXT("WWO/Fonts/Poppins-Light.ttf");
		if (!Poppins.IsValid() && FPaths::FileExists(File))
		{
			Poppins = MakeShared<FCompositeFont>(FName("Light"), File, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		}
		if (Poppins.IsValid())
		{
			FSlateFontInfo F = PlayLabel->GetFont();
			F.FontObject = nullptr;
			F.CompositeFont = Poppins;
			F.TypefaceFontName = FName("Light");
			PlayLabel->SetFont(F);
		}
	}
	PlayLabel->SetJustification(ETextJustify::Center);
	UOverlay* PlayInside = Tree->ConstructWidget<UOverlay>();
	UOverlaySlot* PlayLabelSlot = PlayInside->AddChildToOverlay(PlayLabel);
	PlayLabelSlot->SetHorizontalAlignment(HAlign_Center);
	PlayLabelSlot->SetVerticalAlignment(VAlign_Center);
	UButton* PlayButton = Boxed(PlayInside, WWO::Lightened(WWOUI::CardColor, 0.05f), A, 2.5f, FMargin(0), [WeakThis] { if (WeakThis.IsValid()) WeakThis->Play(); });
	UVerticalBoxSlot* PlaySlot = Middle->AddChildToVerticalBox(WWOUI::Sized(Tree, PlayButton, 460.f, 124.f));
	PlaySlot->SetHorizontalAlignment(HAlign_Center);
	PlaySlot->SetPadding(FMargin(0, 64.f * K, 0, 0));

	// Under it, the small ones.
	UHorizontalBox* Links = Tree->ConstructWidget<UHorizontalBox>();
	auto Link = [&](const FString& Text, TFunction<void()> Fn)
	{
		if (Links->GetChildrenCount() > 0)
		{
			UHorizontalBoxSlot* Bar = Links->AddChildToHorizontalBox(WWOUI::Spaced(WidgetTree, TEXT("|"), 15.f, WWOUI::TabBar, 0));
			Bar->SetVerticalAlignment(VAlign_Center);
			Bar->SetPadding(FMargin(18.f * K, 0));
		}
		Links->AddChildToHorizontalBox(Boxed(WWOUI::Spaced(WidgetTree, Text, 15.f, FLinearColor(1, 1, 1, 0.8f), 220), FLinearColor(0, 0, 0, 0), FLinearColor(0, 0, 0, 0), 0.f, FMargin(14.f, 9.f, 10.f, 9.f), MoveTemp(Fn)));
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
	StatusText = WWOUI::Spaced(WidgetTree, StatusLine(), 13.f, FLinearColor(1.f, 0.72f, 0.4f), 160);
	UOverlaySlot* StatusSlot = Whole->AddChildToOverlay(StatusText);
	StatusSlot->SetHorizontalAlignment(HAlign_Center);
	StatusSlot->SetVerticalAlignment(VAlign_Bottom);
	StatusSlot->SetPadding(FMargin(0, 0, 0, 44.f * K));
	UOverlaySlot* KeysSlot = Whole->AddChildToOverlay(WWOUI::Spaced(WidgetTree, TEXT("Stick or mouse  move   ·   Cross or click  select"), 11.f, WWOUI::Faint, 120));
	KeysSlot->SetHorizontalAlignment(HAlign_Center);
	KeysSlot->SetVerticalAlignment(VAlign_Bottom);
	return Whole;
}

UWidget* UWWOLaunchMenu::BuildSetup()
{
	UWidgetTree* Tree = WidgetTree;
	TWeakObjectPtr<UWWOLaunchMenu> WeakThis = this;
	const UWWONetSubsystem* Net = UWWONetSubsystem::Get(this);
	const FLinearColor A = WWOUI::Accent(this);
	UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>();

	// Header, as on the other menus: the game's name, where you are, the keys.
	UHorizontalBox* Header = Tree->ConstructWidget<UHorizontalBox>();
	UHorizontalBox* Name = Tree->ConstructWidget<UHorizontalBox>();
	Name->AddChildToHorizontalBox(WWOUI::MakeLogo(Tree, false, FVector2D(30.f, 30.f) * K, WWOUI::Spaced(WidgetTree, TEXT("//"), 26.f, WWOUI::DefaultAccent, 0)))->SetVerticalAlignment(VAlign_Center);
	UHorizontalBoxSlot* NameText = Name->AddChildToHorizontalBox(WWOUI::Spaced(WidgetTree, TEXT("WHAT WAS OURS"), 17.f, FLinearColor::White, 420));
	NameText->SetVerticalAlignment(VAlign_Center);
	NameText->SetPadding(FMargin(18.f * K, 0, 0, 0));
	UHorizontalBoxSlot* NameSlot = Header->AddChildToHorizontalBox(Name);
	NameSlot->SetSize(WWOUI::Fill());
	NameSlot->SetVerticalAlignment(VAlign_Center);
	UVerticalBox* Tab = Tree->ConstructWidget<UVerticalBox>();
	Tab->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, TEXT("GAME SETUP"), 17.f, FLinearColor::White, 180))->SetPadding(FMargin(8.f * K, 6.f * K, 4.f * K, 6.f * K));
	UBorder* TabLine = Tree->ConstructWidget<UBorder>();
	TabLine->SetBrushColor(WWO::Lin(A));
	USizeBox* TabLineBox = Tree->ConstructWidget<USizeBox>();
	TabLineBox->SetHeightOverride(3.f * K);
	TabLineBox->AddChild(TabLine);
	Tab->AddChildToVerticalBox(TabLineBox);
	Header->AddChildToHorizontalBox(Tab)->SetVerticalAlignment(VAlign_Center);
	UTextBlock* Keys = WWOUI::Spaced(WidgetTree, TEXT("Cross or click  select   ·   Circle or Esc  back"), 11.f, WWOUI::Faint, 120);
	Keys->SetJustification(ETextJustify::Right);
	UHorizontalBoxSlot* KeysSlot = Header->AddChildToHorizontalBox(Keys);
	KeysSlot->SetSize(WWOUI::Fill());
	KeysSlot->SetVerticalAlignment(VAlign_Center);
	Column->AddChildToVerticalBox(Header);
	Column->AddChildToVerticalBox(WWOUI::Rule(Tree, WWOUI::RuleColor))->SetPadding(FMargin(0, 4.f * K, 0, 0));

	UHorizontalBox* Body = Tree->ConstructWidget<UHorizontalBox>();
	Column->AddChildToVerticalBox(Body)->SetSize(WWOUI::Fill());

	// Left: the connection, then the mode.
	UVerticalBox* Left = Tree->ConstructWidget<UVerticalBox>();
	Left->AddChildToVerticalBox(StepLabel(1, TEXT("Connection")))->SetPadding(FMargin(0, 30.f * K, 0, 12.f * K));
	auto AddChoice = [&](UVerticalBox* To, UButton* Button)
	{
		To->AddChildToVerticalBox(WWOUI::Sized(Tree, Button, 0.f, 74.f))->SetPadding(FMargin(0, 0, 0, 10.f * K));
	};
	AddChoice(Left, Choice(TEXT("Online host"), TEXT("Start a game others on your network can join"), Connection == ConnHost,
		[WeakThis] { if (WeakThis.IsValid()) WeakThis->ChooseConnection(ConnHost); }));
	AddChoice(Left, Choice(TEXT("Online join"), TEXT("Find a game on your network"), Connection == ConnJoin,
		[WeakThis] { if (WeakThis.IsValid()) WeakThis->ChooseConnection(ConnJoin); }));
	AddChoice(Left, Choice(TEXT("Local"), TEXT("Play on this machine only"), Connection == ConnLocal,
		[WeakThis] { if (WeakThis.IsValid()) WeakThis->ChooseConnection(ConnLocal); }));
	AddChoice(Left, Choice(TEXT("Firing tester"), TEXT("Tune how guns move and kick, with one target"), Connection == ConnTester,
		[WeakThis] { if (WeakThis.IsValid()) WeakThis->ChooseConnection(ConnTester); }));
	if (Connection == ConnHost || Connection == ConnLocal)
	{
		Left->AddChildToVerticalBox(StepLabel(2, TEXT("Mode")))->SetPadding(FMargin(0, 26.f * K, 0, 12.f * K));
		AddChoice(Left, Choice(TEXT("PvP"), Connection == ConnLocal ? TEXT("Team deathmatch against bots") : TEXT("Team deathmatch, up to 6 v 6"), Mode == ModePvP,
			[WeakThis] { if (WeakThis.IsValid()) WeakThis->ChooseMode(ModePvP); }));
		AddChoice(Left, Choice(TEXT("PvE"), Connection == ConnLocal ? TEXT("Wave survival") : TEXT("Wave survival together, up to 6 players"), Mode == ModePvE,
			[WeakThis] { if (WeakThis.IsValid()) WeakThis->ChooseMode(ModePvE); }));
	}
	TagBox = nullptr;
	if (Connection == ConnHost || Connection == ConnJoin)
	{
		// The gamer tag: what the others see above your health bar. Needed before START / JOIN.
		Left->AddChildToVerticalBox(StepLabel(Connection == ConnHost ? 4 : 3, TEXT("Gamer tag")))->SetPadding(FMargin(0, 26.f * K, 0, 12.f * K));
		TagBox = Tree->ConstructWidget<UEditableTextBox>();
		WWOUI::StyleTextBox(TagBox, 20.f, FMargin(14.f, 10.f), A);
		TagBox->SetHintText(FText::FromString(TEXT("Type a name (letters, digits, _ -)")));
		TagBox->SetText(FText::FromString(Net ? Net->GetGamerTag() : FString()));
		TagBox->OnTextChanged.AddDynamic(this, &UWWOLaunchMenu::OnTagChanged);
		Left->AddChildToVerticalBox(WWOUI::Sized(Tree, TagBox, 0.f, 54.f));
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
		if (UScrollBoxSlot* Slot = Cast<UScrollBoxSlot>(List->AddChild(Height > 0.f ? WWOUI::Sized(Tree, W, 0.f, Height) : W))) Slot->SetPadding(FMargin(0, 0, 14.f * K, 10.f * K));
	};
	FString Summary;
	if (Connection == ConnJoin)
	{
		UHorizontalBox* Title = Tree->ConstructWidget<UHorizontalBox>();
		Title->AddChildToHorizontalBox(StepLabel(2, TEXT("Games on your network")))->SetSize(WWOUI::Fill());
		Title->AddChildToHorizontalBox(Boxed(WWOUI::Spaced(WidgetTree, TEXT("Refresh"), 12.f, FLinearColor(1, 1, 1, 0.85f), 180), WWOUI::CardColor, WWOUI::CardEdge, 1.5f, FMargin(14.f, 6.f, 10.f, 6.f),
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
			AddToList(WWOUI::Spaced(WidgetTree, Net && Net->bSearching ? TEXT("Searching...") : TEXT("No games found yet. This list refreshes on its own; a host has to be on the same network."),
				14.f, WWOUI::Dim, 120, true), 0.f);
		}
		if (Net && Net->Games.IsValidIndex(Game)) Summary = FString::Printf(TEXT("Join  ·  %s  ·  %s"), Net->Games[Game].bPvP ? TEXT("PvP") : TEXT("PvE"), *Net->Games[Game].MapTitle);
	}
	else if (Connection == ConnTester)
	{
		Right->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, TEXT("A range with one target dummy and no enemies. The gun's settings are on the left and the player's view on the right; take control to shoot it yourself, or use the buttons to aim, fire and strafe."),
			14.f, WWOUI::Dim, 120, true))->SetPadding(FMargin(0, 34.f * K, 14.f * K, 0));
		Summary = TEXT("Firing tester  ·  Test Range");
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
		Right->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, Hint, 14.f, WWOUI::Faint, 140))->SetPadding(FMargin(0, 34.f * K, 0, 0));
	}
	Right->AddChildToVerticalBox(List)->SetSize(WWOUI::Fill());

	// The bottom row: the status, what you picked, and the big button once everything is picked.
	UHorizontalBox* Bottom = Tree->ConstructWidget<UHorizontalBox>();
	UVerticalBox* Words = Tree->ConstructWidget<UVerticalBox>();
	if (!Summary.IsEmpty()) Words->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, Summary, 15.f, FLinearColor::White, 200));
	StatusText = WWOUI::Spaced(WidgetTree, StatusLine(), 12.f, FLinearColor(1.f, 0.72f, 0.4f), 140);
	Words->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0, 8.f * K, 0, 0));
	UHorizontalBoxSlot* WordsSlot = Bottom->AddChildToHorizontalBox(Words);
	WordsSlot->SetSize(WWOUI::Fill());
	WordsSlot->SetVerticalAlignment(VAlign_Center);
	if (CanStart() || StartedAt > 0.0)
	{
		const bool bStarting = StartedAt > 0.0;
		const FString Text = bStarting ? (Connection == ConnJoin ? TEXT("Joining...") : TEXT("Starting...")) : (Connection == ConnJoin ? TEXT("Join game") : TEXT("Start game"));
		UTextBlock* Label = WWOUI::Spaced(WidgetTree, Text, 28.f, bStarting ? WWOUI::Dim : FLinearColor::White, 420);
		Label->SetJustification(ETextJustify::Center);
		UOverlay* Inside = Tree->ConstructWidget<UOverlay>();
		UOverlaySlot* LabelSlot = Inside->AddChildToOverlay(Label);
		LabelSlot->SetHorizontalAlignment(HAlign_Center);
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		UButton* StartButton = Boxed(Inside, WWO::Lightened(WWOUI::CardColor, 0.05f), bStarting ? WWOUI::CardEdge : A, 2.5f, FMargin(0), [WeakThis] { if (WeakThis.IsValid()) WeakThis->Start(); });
		Bottom->AddChildToHorizontalBox(WWOUI::Sized(Tree, StartButton, 420.f, 92.f))->SetVerticalAlignment(VAlign_Center);
	}
	Right->AddChildToVerticalBox(WWOUI::Sized(Tree, Bottom, 0.f, 92.f))->SetPadding(FMargin(0, 14.f * K, 14.f * K, 0));
	Body->AddChildToHorizontalBox(Right)->SetSize(WWOUI::Fill());
	return Column;
}
