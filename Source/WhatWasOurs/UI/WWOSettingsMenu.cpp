#include "UI/WWOSettingsMenu.h"
#include "UI/WWOSettingsMenuPrivate.h"
#include "UI/WWOUI.h"
#include "Player/WWOProf.h"
#include "Systems/WWOSound.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOPlayerController.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
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
#include "Components/WidgetSwitcher.h"
#include "Components/EditableTextBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Net/WWONetSubsystem.h"
#include "HAL/PlatformTime.h"
#include "Engine/World.h"

using namespace WWOSettingsUI;

// --- Styling -----------------------------------------------------------------------------------

UTextBlock* UWWOSettingsMenu::MakeText(const FString& Text, float FontPx, const FLinearColor& Color, bool bWrap)
{
	return WWOUI::Text(WidgetTree, Text, FontPx, Color, bWrap);
}

UButton* UWWOSettingsMenu::MakeButton(UObject* Outer, const FString& Text, TFunction<void()> Fn, float FontPx, const FLinearColor& Color,
	bool bLeftAlign, FMargin Padding, bool bWrap)
{
	// Godot buttons don't wrap: the text sets their width. (Auto-wrap in Slate wraps at last
	// frame's width, which broke two-line list items onto three lines.)
	UButton* B = WWOUI::TextButton(WidgetTree, Text, FontPx, WWOUI::FilledLook(Color, Padding, bLeftAlign), bWrap);
	KeepHandler(WWOUI::OnClick(B, Outer ? Outer : this, MoveTemp(Fn)));
	return B;
}

void UWWOSettingsMenu::KeepHandler(UObject* Handler)
{
	WWOUI::KeepPruned(Handlers, PruneAt, Handler);
}

UButton* UWWOSettingsMenu::PlainButton(UWidget* Inside, TFunction<void()> Fn)
{
	// A button with no box of its own (a tab, a sidebar line): only a faint wash when hovered.
	UButton* B = WWOUI::Button(WidgetTree, Inside, WWOUI::PlainLook());
	KeepHandler(WWOUI::OnClick(B, this, MoveTemp(Fn)));
	return B;
}

// --- Opening, closing, the widget ----------------------------------------------------------

void UWWOSettingsMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	Build();
}

void UWWOSettingsMenu::Build()
{
	// The panels are built once the player exists (the sliders bind to its components).
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Root;
	SetCursor(EMouseCursor::None); // the menu draws Godot's ring cursor instead (NativePaint)
}

void UWWOSettingsMenu::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	WWO_PROF("SettingsMenu.NativeTick");
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bOpen)
	{
		LiveTexts.RemoveAll([](const FLiveText& L) { return !L.Text.IsValid(); });
		for (const FLiveText& L : LiveTexts) L.Text->SetText(FText::FromString(L.Get()));
	}
	if (bOpen && SearchBox && SearchBox->HasKeyboardFocus() && FSlateApplication::IsInitialized())
	{
		// Clicking anywhere else gives the keyboard back to the game (nothing else here takes focus).
		FSlateApplication& App = FSlateApplication::Get();
		if (App.GetPressedMouseButtons().Contains(EKeys::LeftMouseButton) && !SearchBox->GetCachedGeometry().IsUnderLocation(App.GetCursorPos())) App.SetAllUserFocusToGameViewport();
	}
	if (SaveRevertAt > 0.0 && FPlatformTime::Seconds() >= SaveRevertAt)
	{
		SaveRevertAt = 0.0;
		if (UTextBlock* Label = SaveButton ? Cast<UTextBlock>(SaveButton->GetChildAt(0)) : nullptr) Label->SetText(FText::FromString(TEXT("Save as defaults")));
	}
}

int32 UWWOSettingsMenu::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!bOpen) return Layer;
	WWOUI::PaintCursor(this, AllottedGeometry, OutDrawElements, Layer + 1);
	return Layer + 1;
}

float UWWOSettingsMenu::GlobalScrollOffset() const
{
	const UScrollBox* Scroll = GlobalSwitcher ? Cast<UScrollBox>(GlobalSwitcher->GetActiveWidget()) : nullptr; // (an item page isn't one: 0)
	return Scroll ? Scroll->GetScrollOffset() : 0.f;
}

FString UWWOSettingsMenu::OpenItemKey() const
{
	for (const FWWOBrowser& B : Browsers)
	{
		bool bShown = true;
		for (UWidget* Child = B.Switcher; Child && Child->GetParent(); Child = Child->GetParent())
		{
			if (const UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(Child->GetParent())) bShown &= Switcher->GetActiveWidget() == Child;
		}
		if (bShown && B.Switcher->GetActiveWidgetIndex() == 1) return B.OpenKey;
	}
	return FString();
}

void UWWOSettingsMenu::CycleGlobalTab(int32 Step)
{
	// The bumpers and Q / E step through the tabs (past any with no pages: a joiner's menu has fewer).
	int32 Next = TopTab;
	for (int32 Tries = 0; Tries < NumTopTabs; ++Tries)
	{
		Next = ((Next + Step) % NumTopTabs + NumTopTabs) % NumTopTabs;
		if (LastPage.IsValidIndex(Next) && LastPage[Next] != INDEX_NONE) break;
	}
	SelectTopTab(Next);
}

int32 UWWOSettingsMenu::ShownPageCount() const
{
	int32 Count = 0;
	for (const FPage& Page : Pages) if (Page.Label.IsValid()) ++Count;
	return Count;
}

// --- Building the layout -----------------------------------------------------------------------

void UWWOSettingsMenu::Prepare()
{
	// Registers every section (applying the saved defaults) and builds the panels. Runs as
	// soon as the player exists, so saved values apply at startup, not just once opened.
	AWWOCharacter* P = GetOwningPlayer() ? Cast<AWWOCharacter>(GetOwningPlayer()->GetPawn()) : nullptr;
	if (!bBuilt && P)
	{
		bBuilt = true;
		// Someone else's game: the tuning is the host's (see the header). Your own menu, alone or hosting, has everything.
		bRestricted = P->GetNetMode() == NM_Client;
		RegisterSections(P);
		MarkPreferences();
		RecordDefaults();
		LoadSaved();
		UHorizontalBox* Body = BuildFrame();
		BuildPages(UWWOLoadoutSubsystem::Get(P));
		BuildSidebar(Body);
		BuildPageArea(Body);
		// The first page there is (in someone else's game the first of the ones left).
		int32 First = 0;
		for (int32 Tab = NumTopTabs - 1; Tab >= 0; --Tab) if (LastPage[Tab] != INDEX_NONE) First = LastPage[Tab];
		SelectPage(First);
	}
}

namespace
{
	// Which tab each global section sits under, in the order its sidebar lists them. One that
	// isn't named here lands under Player.
	const TArray<TArray<FString>>& TabLayout()
	{
		static const TArray<TArray<FString>> Layout = {
			{ TEXT("Walk / Sprint"), TEXT("Jump"), TEXT("Crouch / Slide"), TEXT("Camera"), TEXT("HUD"), TEXT("Audio"), TEXT("Player"), TEXT("Aim Assist") },
			{ TEXT("Melee"), TEXT("Grenades"), TEXT("Ult"), TEXT("Targets") },
			{ TEXT("Soldier"), TEXT("Swarm"), TEXT("Tank"), TEXT("Bots"), TEXT("Allies") },
			{ TEXT("Maps"), TEXT("Multiplayer"), TEXT("PvP"), TEXT("Waves") },
		};
		return Layout;
	}
}

UHorizontalBox* UWWOSettingsMenu::BuildFrame()
{
	// Prepare's first step: the window and its header. Returns the row under the header that the
	// sidebar and the page column go in.
	UOverlay* Root = Cast<UOverlay>(WidgetTree->RootWidget);
	TWeakObjectPtr<UWWOSettingsMenu> WeakThis = this;
	const FLinearColor Accent = WWOUI::Accent(this);

	// The whole window, in the character menu's style. A little see-through, so a slider's
	// effect on the game behind (field of view, the HUD) can still be watched.
	UBorder* Page = WidgetTree->ConstructWidget<UBorder>();
	Page->SetBrushColor(WWO::Lin(WWO::WithAlpha(WWOUI::PageColor, 0.95f)));
	Page->SetPadding(FMargin(40.f * K, 12.f * K, 40.f * K, 28.f * K));
	UOverlaySlot* PageSlot = Root->AddChildToOverlay(Page);
	PageSlot->SetHorizontalAlignment(HAlign_Fill);
	PageSlot->SetVerticalAlignment(VAlign_Fill);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	Page->SetContent(Column);

	// Header: the name, the six tabs (bumpers or Q / E), the search box.
	TArray<FString> TabTitles;
	for (const TCHAR* Title : TopTabNames) TabTitles.Add(Title);
	WWOUI::FTabStrip Tabs = WWOUI::TabStrip(WidgetTree, TabTitles, [this, WeakThis](UWidget* Tab, int32 i) -> UWidget*
	{
		return PlainButton(Tab, [WeakThis, i] { if (WeakThis.IsValid()) WeakThis->SelectTopTab(i); });
	});
	for (int32 i = 0; i < NumTopTabs; ++i)
	{
		TopLabels.Add(Tabs.Labels[i]);
		TopLines.Add(Tabs.Lines[i]);
		TopTabWidgets.Add(Tabs.Tabs[i]);
		TopTabWidgets.Add(Tabs.Bars[i]);
	}
	// Search: type part of a setting's name to list every match from every tab.
	SearchBox = WidgetTree->ConstructWidget<UEditableTextBox>();
	WWOUI::StyleTextBox(SearchBox, 15.f, FMargin(12.f, 6.f), Accent);
	SearchBox->SetHintText(FText::FromString(TEXT("SEARCH SETTINGS")));
	SearchBox->OnTextChanged.AddDynamic(this, &UWWOSettingsMenu::OnSearchChanged);
	SearchBox->OnTextCommitted.AddDynamic(this, &UWWOSettingsMenu::OnSearchCommitted);
	UHorizontalBox* RightSide = WidgetTree->ConstructWidget<UHorizontalBox>();
	RightSide->AddChildToHorizontalBox(WWOUI::Wide(WidgetTree, SearchBox, 250.f))->SetVerticalAlignment(VAlign_Center);
	WWOUI::AddPageHeader(WidgetTree, Column, WWOUI::HeaderName(WidgetTree, TEXT("SETTINGS"), false), Tabs.Box, RightSide, HAlign_Right);

	UHorizontalBox* Body = WidgetTree->ConstructWidget<UHorizontalBox>();
	Column->AddChildToVerticalBox(Body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	return Body;
}

void UWWOSettingsMenu::BuildPages(UWWOLoadoutSubsystem* L)
{
	// Every page: the global sections, then the item browsers, then the search results.
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
	const TArray<TArray<FString>>& Layout = TabLayout();
	GlobalSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>();
	for (const FString& Key : GlobalKeys)
	{
		const FString Title = Find(Key)->Title;
		int32 Tab = 0;
		for (int32 t = 0; t < Layout.Num(); ++t) if (Layout[t].Contains(Title)) Tab = t;
		UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
		BuildRows(Scroll, Key);
		GlobalSwitcher->AddChild(Scroll);
		Pages.Add({ Title, Tab });
	}
	auto AddItems = [&](const TCHAR* Title, TFunction<TArray<FWWOBrowserItem>()> Provider)
	{
		GlobalSwitcher->AddChild(MakeBrowser(MoveTemp(Provider)));
		Pages.Add({ Title, ItemsTab });
	};
	AddItems(TEXT("Equipped"), [WL]
	{
		TArray<FWWOBrowserItem> Items;
		if (!WL.IsValid()) return Items;
		if (UWWOGrenadeData* G = WL->GetGrenade()) Items.Add({ TEXT("grenade/") + G->Id.ToString(), G->DisplayName, TEXT("Equipped grenade") });
		for (int32 Slot = 0; Slot < UWWOLoadoutSubsystem::AbilitySlots; ++Slot)
		{
			if (UWWOAbilityData* A = WL->GetAbility(Slot)) Items.Add({ TEXT("ability/") + A->Id.ToString(), A->DisplayName, FString::Printf(TEXT("Equipped ability %d"), Slot + 1) });
		}
		if (UWWOWeaponData* W = WL->GetWeapon()) Items.Add({ TEXT("weapon/") + W->Id.ToString(), W->DisplayName, TEXT("Equipped weapon") });
		for (const FName& Slot : UWWOLoadoutSubsystem::FrameSlots())
		{
			if (UWWOFramePartData* Part = WL->GetFramePart(Slot)) Items.Add({ TEXT("frame/") + Part->Id.ToString(), Part->DisplayName, TEXT("Equipped ") + Slot.ToString() });
		}
		return Items;
	});
	AddItems(TEXT("Grenade Types"), [WL]
	{
		TArray<FWWOBrowserItem> Items;
		if (WL.IsValid()) for (UWWOGrenadeData* G : WL->Grenades) Items.Add({ TEXT("grenade/") + G->Id.ToString(), G->DisplayName, G == WL->GetGrenade() ? TEXT("Equipped") : TEXT("") });
		return Items;
	});
	AddItems(TEXT("Abilities"), [WL]
	{
		TArray<FWWOBrowserItem> Items;
		if (!WL.IsValid()) return Items;
		for (UWWOAbilityData* A : WL->Abilities)
		{
			const int32 Slot = WL->AbilitySlotOf(A);
			Items.Add({ TEXT("ability/") + A->Id.ToString(), A->DisplayName, KindName(A->Kind) + (Slot != INDEX_NONE ? FString::Printf(TEXT(" · equipped in slot %d"), Slot + 1) : FString()) });
		}
		return Items;
	});
	AddItems(TEXT("Traits"), [WL]
	{
		TArray<FWWOBrowserItem> Items;
		if (!WL.IsValid()) return Items;
		for (UWWOTraitData* T : WL->Traits)
		{
			Items.Add({ TEXT("trait/") + T->Id.ToString(), T->DisplayName, Capitalize(T->Flora.ToString()) + (WL->IsTraitEquipped(T) ? TEXT(" · equipped") : TEXT("")) });
		}
		return Items;
	});
	AddItems(TEXT("Weapons"), [WL]
	{
		TArray<FWWOBrowserItem> Items;
		if (WL.IsValid()) for (UWWOWeaponData* W : WL->Weapons) Items.Add({ TEXT("weapon/") + W->Id.ToString(), W->DisplayName, W->Archetype + (W == WL->GetWeapon() ? TEXT(" · equipped") : TEXT("")) });
		return Items;
	});
	AddItems(TEXT("Shell"), [WL]
	{
		TArray<FWWOBrowserItem> Items;
		if (!WL.IsValid()) return Items;
		for (UWWOFramePartData* Part : WL->FrameParts)
		{
			Items.Add({ TEXT("frame/") + Part->Id.ToString(), Part->DisplayName, Capitalize(Part->Slot.ToString()) + (Part == WL->GetFramePart(Part->Slot) ? TEXT(" · equipped") : TEXT("")) });
		}
		return Items;
	});
	// Controls: the key bindings.
	BindsPage = GlobalSwitcher->GetChildrenCount();
	BindsList = WidgetTree->ConstructWidget<UScrollBox>();
	GlobalSwitcher->AddChild(BindsList);
	Pages.Add({ TEXT("Key Bindings"), ControlsTab });
	BuildBinds();
	SearchPage = GlobalSwitcher->GetChildrenCount();
	SearchResults = WidgetTree->ConstructWidget<UScrollBox>();
	GlobalSwitcher->AddChild(SearchResults);
}

void UWWOSettingsMenu::BuildSidebar(UHorizontalBox* Body)
{
	TWeakObjectPtr<UWWOSettingsMenu> WeakThis = this;
	const TArray<TArray<FString>>& Layout = TabLayout();
	// Left: the open tab's pages, then the menu's own buttons at the bottom.
	UVerticalBox* Side = WidgetTree->ConstructWidget<UVerticalBox>();
	SideSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>();
	LastPage.Init(INDEX_NONE, NumTopTabs);
	for (int32 Tab = 0; Tab < NumTopTabs; ++Tab)
	{
		UVerticalBox* List = WidgetTree->ConstructWidget<UVerticalBox>();
		TArray<int32> Order;
		if (Layout.IsValidIndex(Tab))
		{
			for (const FString& Title : Layout[Tab]) for (int32 p = 0; p < Pages.Num(); ++p) if (Pages[p].Tab == Tab && Pages[p].Title == Title) Order.Add(p);
		}
		for (int32 p = 0; p < Pages.Num(); ++p) if (Pages[p].Tab == Tab && !Order.Contains(p)) Order.Add(p);
		for (const int32 p : Order)
		{
			// In someone else's game: only the pages with something of yours on them, and the key bindings.
			if (bRestricted && p != BindsPage && !(GlobalKeys.IsValidIndex(p) && SectionAllowed(*Find(GlobalKeys[p])))) continue;
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			UBorder* Mark = WidgetTree->ConstructWidget<UBorder>();
			USizeBox* MarkBox = WidgetTree->ConstructWidget<USizeBox>();
			MarkBox->SetWidthOverride(3.f * K);
			MarkBox->AddChild(Mark);
			Row->AddChildToHorizontalBox(MarkBox)->SetPadding(FMargin(0, 3.f * K));
			UTextBlock* Label = WWOUI::Spaced(WidgetTree, Pages[p].Title, 15.f, FLinearColor::White, 170);
			UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(Label);
			LabelSlot->SetPadding(FMargin(16.f * K, 9.f * K, 8.f * K, 9.f * K));
			LabelSlot->SetVerticalAlignment(VAlign_Center);
			Pages[p].Label = Label;
			Pages[p].Mark = Mark;
			List->AddChildToVerticalBox(PlainButton(Row, [WeakThis, p] { if (WeakThis.IsValid()) WeakThis->SelectPage(p); }))->SetPadding(FMargin(0, 2.f * K));
			if (LastPage[Tab] == INDEX_NONE) LastPage[Tab] = p;
		}
		SideSwitcher->AddChild(List);
	}
	Side->AddChildToVerticalBox(SideSwitcher)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	if (bRestricted)
	{
		// The tabs with nothing left on them go, with the bar beside each.
		bool bFirstShown = true;
		for (int32 Tab = 0; Tab < NumTopTabs; ++Tab)
		{
			const bool bShown = LastPage[Tab] != INDEX_NONE;
			if (UWidget* Button = TopTabWidgets[Tab * 2]) Button->SetVisibility(bShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			if (UWidget* Bar = TopTabWidgets[Tab * 2 + 1]) Bar->SetVisibility(bShown && !bFirstShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			bFirstShown &= !bShown;
		}
		UTextBlock* Note = WWOUI::Spaced(WidgetTree, TEXT("You're in someone else's game: these are your own settings. The game's tuning is the host's."), 10.f, FLinearColor(1, 1, 1, 0.5f), 90);
		Note->SetAutoWrapText(true);
		Side->AddChildToVerticalBox(Note)->SetPadding(FMargin(2.f * K, 0, 0, 6.f * K));
	}
	auto SideButton = [&](const FString& Text, TFunction<void()> Fn) -> UButton*
	{
		UButton* B = WWOUI::TextButton(WidgetTree, Text, 15.f, KeyLook(WWOUI::CardColor, WWOUI::CardEdge, 1.5f, FMargin(10.f, 8.f)));
		KeepHandler(WWOUI::OnClick(B, this, MoveTemp(Fn)));
		Side->AddChildToVerticalBox(B)->SetPadding(FMargin(0, 8.f * K, 0, 0));
		return B;
	};
	SideButton(TEXT("Reset this page"), [WeakThis]
	{
		if (!WeakThis.IsValid()) return;
		// A global page resets its section; an item page, the item that's open.
		if (WeakThis->CurrentPage == WeakThis->BindsPage)
		{
			if (AWWOPlayerController* PC = Cast<AWWOPlayerController>(WeakThis->GetOwningPlayer())) PC->ResetBinds();
			WeakThis->BuildBinds();
		}
		else if (WeakThis->GlobalKeys.IsValidIndex(WeakThis->CurrentPage)) WeakThis->ResetSection(WeakThis->GlobalKeys[WeakThis->CurrentPage]);
		else if (!WeakThis->OpenItemKey().IsEmpty()) WeakThis->ResetSection(WeakThis->OpenItemKey());
	});
	SideButton(TEXT("Reset all"), [WeakThis] { if (WeakThis.IsValid()) WeakThis->ResetAll(); });
	SaveButton = SideButton(TEXT("Save as defaults"), [WeakThis]
	{
		if (!WeakThis.IsValid()) return;
		const bool bSaved = WeakThis->SaveDefaults();
		if (UTextBlock* Label = Cast<UTextBlock>(WeakThis->SaveButton->GetChildAt(0)))
		{
			Label->SetText(FText::FromString(bSaved ? TEXT("Saved ✓") : TEXT("Save failed")));
		}
		WeakThis->SaveRevertAt = FPlatformTime::Seconds() + 1.5; // Godot's 1.5 s timer (real time: the game is paused)
	});
	TWeakObjectPtr<APlayerController> WeakPC = GetOwningPlayer();
	// Out of the match, back to the launch screen: hosting, that ends it for everyone; in someone
	// else's PvP game a bot takes your place there; alone it just ends. (Not on the launch screen itself.)
	const AWWOPlayerController* OwnPC = Cast<AWWOPlayerController>(GetOwningPlayer());
	if (!OwnPC || !OwnPC->GetLaunchMenu())
	{
		SideButton(TEXT("Quit to main menu"), [WeakPC]
		{
			if (UWWONetSubsystem* N = WeakPC.IsValid() ? UWWONetSubsystem::Get(WeakPC.Get()) : nullptr) N->LeaveToTitle();
		});
	}
	SideButton(TEXT("Close"), [WeakPC]
	{
		if (AWWOPlayerController* PC = Cast<AWWOPlayerController>(WeakPC.Get())) PC->ToggleSettingsMenu();
	});
	Side->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, TEXT("Q / E or bumpers: tabs   ·   Esc: back"), 10.f, WWOUI::Faint, 100))->SetPadding(FMargin(2.f * K, 12.f * K, 0, 0));
	USizeBox* SideBox = WidgetTree->ConstructWidget<USizeBox>();
	SideBox->SetWidthOverride(270.f * K);
	SideBox->AddChild(Side);
	Body->AddChildToHorizontalBox(SideBox)->SetPadding(FMargin(0, 28.f * K, 28.f * K, 0));
}

void UWWOSettingsMenu::BuildPageArea(UHorizontalBox* Body)
{
	// Right: the open page, one wide column.
	UVerticalBox* Main = WidgetTree->ConstructWidget<UVerticalBox>();
	PageTitle = WWOUI::Spaced(WidgetTree, TEXT(""), 20.f, FLinearColor::White, 300);
	Main->AddChildToVerticalBox(PageTitle)->SetPadding(FMargin(4.f * K, 26.f * K, 0, 12.f * K));
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrush(WWOUI::RoundBrush(WWOUI::PanelColor, 6.f, WWOUI::PanelEdge, 1.f));
	Panel->SetPadding(FMargin(8.f * K));
	Panel->SetContent(GlobalSwitcher);
	Main->AddChildToVerticalBox(Panel)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Body->AddChildToHorizontalBox(Main)->SetSize(WWOUI::Fill(1.f));
}

// --- Tabs, pages and search -------------------------------------------------------------------

void UWWOSettingsMenu::SelectPage(int32 Index)
{
	// One page of the open tab: its sliders (or its list of items) fill the column on the right.
	if (!Pages.IsValidIndex(Index) || !GlobalSwitcher) return;
	CurrentPage = Index;
	TopTab = Pages[Index].Tab;
	LastPage[TopTab] = Index;
	if (GlobalKeys.IsValidIndex(Index)) GlobalTab = Index;
	if (!SearchText.IsEmpty() && SearchBox) { SearchText.Reset(); SearchBox->SetText(FText::GetEmpty()); } // picking a page leaves the search
	if (Index == BindsPage) BuildBinds(); // as the bindings are now
	GlobalSwitcher->SetActiveWidgetIndex(Index);
	if (SideSwitcher) SideSwitcher->SetActiveWidgetIndex(TopTab);
	if (PageTitle) PageTitle->SetText(FText::FromString(Pages[Index].Title.ToUpper()));
	const FLinearColor Accent = WWOUI::Accent(this);
	WWOUI::SetActiveTab(TopLabels, TopLines, TopTab, Accent);
	for (int32 i = 0; i < Pages.Num(); ++i)
	{
		if (Pages[i].Label.IsValid()) Pages[i].Label->SetColorAndOpacity(FSlateColor(WWO::Lin(i == Index ? FLinearColor::White : WWOUI::TabDim)));
		if (Pages[i].Mark.IsValid()) Pages[i].Mark->SetBrushColor(WWO::Lin(i == Index ? Accent : FLinearColor(1, 1, 1, 0.12f)));
	}
}

void UWWOSettingsMenu::SelectTopTab(int32 Index)
{
	// A tab opens on the page it was last on.
	if (bOpen) UWWOSound::PlayFlat(this, "ui_tab");
	Index = (Index % NumTopTabs + NumTopTabs) % NumTopTabs;
	if (LastPage.IsValidIndex(Index) && LastPage[Index] != INDEX_NONE) SelectPage(LastPage[Index]);
}

bool UWWOSettingsMenu::ShowPage(const FString& Title)
{
	for (int32 i = 0; i < Pages.Num(); ++i)
	{
		// (A page a joiner's menu doesn't list has no label: it can't be opened this way either.)
		if (Pages[i].Title == Title && (!bRestricted || Pages[i].Label.IsValid())) { SelectPage(i); return true; }
	}
	return false;
}

void UWWOSettingsMenu::OnSearchChanged(const FText& Text)
{
	// Every slider whose name (or whose page's or item's name) holds what was typed, from every
	// tab, under the name of where it lives. Emptying the box goes back to the page you were on.
	const FString Query = Text.ToString().TrimStartAndEnd();
	if (Query == SearchText || !SearchResults) return;
	SearchText = Query;
	if (Query.IsEmpty())
	{
		const int32 Back = CurrentPage;
		SelectPage(Back);
		return;
	}
	SearchResults->ClearChildren();
	int32 Found = 0;
	constexpr int32 Most = 80;
	for (const FWWOSettingSection& S : Sections)
	{
		if (!SectionAllowed(S)) continue;
		const bool bWholeSection = S.Title.Contains(Query);
		bool bNamed = false;
		for (int32 i = 0; i < S.Rows.Num() && Found < Most; ++i)
		{
			const FWWOSettingRow& R = S.Rows[i];
			if (!R.Header.IsEmpty() || R.LiveHeader || !Allowed(R) || !(bWholeSection || R.Label.Contains(Query))) continue;
			if (!bNamed)
			{
				bNamed = true;
				UTextBlock* Where = WWOUI::Spaced(WidgetTree, S.Title, 13.f, FLinearColor(1, 1, 1, 0.55f), 220);
				if (UScrollBoxSlot* Slot = Cast<UScrollBoxSlot>(SearchResults->AddChild(Where))) Slot->SetPadding(FMargin(12.f * K, (Found == 0 ? 12.f : 26.f) * K, 12.f * K, 0));
			}
			if (UScrollBoxSlot* Slot = Cast<UScrollBoxSlot>(SearchResults->AddChild(MakeRow(S.Key, i)))) Slot->SetPadding(FMargin(12.f * K, 8.f * K, 12.f * K, 0));
			++Found;
		}
	}
	if (Found == 0)
	{
		if (UScrollBoxSlot* Slot = Cast<UScrollBoxSlot>(SearchResults->AddChild(MakeText(TEXT("No setting has that in its name."), 20.f, FLinearColor(1, 1, 1, 0.6f))))) Slot->SetPadding(FMargin(12.f * K));
	}
	SearchResults->ScrollToStart();
	GlobalSwitcher->SetActiveWidgetIndex(SearchPage);
	if (PageTitle) PageTitle->SetText(FText::FromString(FString::Printf(TEXT("SEARCH  ·  %d%s FOUND"), Found, Found >= Most ? TEXT("+") : TEXT(""))));
}

void UWWOSettingsMenu::AutoTestSearch(const FString& Text)
{
	if (SearchBox) SearchBox->SetText(FText::FromString(Text));
	OnSearchChanged(FText::FromString(Text));
}

void UWWOSettingsMenu::OnSearchCommitted(const FText& Text, ETextCommit::Type Type)
{
	// Enter or Esc hands the keyboard back to the game, so Q / E, Tab and Esc work the menu again.
	if (Type == ETextCommit::OnEnter || Type == ETextCommit::OnCleared) FSlateApplication::Get().SetAllUserFocusToGameViewport();
}

void UWWOSettingsMenu::Open()
{
	Prepare();
	bOpen = true;
	UWWOSound::PlayFlat(this, "ui_open");
	SetVisibility(ESlateVisibility::Visible);
	// Item lists can change (a different grenade equipped), so refresh any showing.
	for (int32 i = 0; i < Browsers.Num(); ++i)
	{
		if (Browsers[i].Switcher->GetActiveWidgetIndex() == 0) ShowList(i);
	}
	if (SearchBox && !SearchText.IsEmpty()) SearchBox->SetText(FText::GetEmpty());
	BuildBinds();
	SaveRevertAt = 0.0;
	if (SaveButton)
	{
		if (UTextBlock* Label = Cast<UTextBlock>(SaveButton->GetChildAt(0))) Label->SetText(FText::FromString(TEXT("Save as defaults")));
	}
	SyncSliders();
}

void UWWOSettingsMenu::Close()
{
	if (bOpen) UWWOSound::PlayFlat(this, "ui_close");
	bOpen = false;
	SetVisibility(ESlateVisibility::Collapsed);
}

void UWWOSettingsMenu::Back()
{
	// Back out of an open item first (the one on screen), then close.
	auto IsShown = [](UWidget* W)
	{
		for (UWidget* Child = W; Child && Child->GetParent(); Child = Child->GetParent())
		{
			if (UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(Child->GetParent()))
			{
				if (Switcher->GetActiveWidget() != Child) return false;
			}
		}
		return true;
	};
	for (int32 i = 0; i < Browsers.Num(); ++i)
	{
		if (Browsers[i].Switcher->GetActiveWidgetIndex() == 1 && IsShown(Browsers[i].Switcher))
		{
			ShowList(i);
			return;
		}
	}
	if (AWWOPlayerController* PC = Cast<AWWOPlayerController>(GetOwningPlayer())) PC->ToggleSettingsMenu();
}
