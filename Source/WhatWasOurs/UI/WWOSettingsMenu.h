#pragma once

// Settings menu (Godot: settings_menu.gd). It fills the window, in the character menu's style.
// - Six tabs across the top (Q/E or L1/R1): Player, Combat, Enemies, Match, Items, Controls
//   (the key bindings). Each lists
//   its pages down the left; the open page's sliders fill one column on the right.
// - Items' pages (Equipped, Grenade Types, Abilities, Traits, Weapons, Shell) list items: pick
//   one to open its sliders; Back (or circle / Esc) returns to the list.
// - The search box lists every slider whose name matches, from every tab.
// Every slider binds to a float UPROPERTY by name through reflection. "Save as defaults"
// writes every section to Saved/WWO/SavedSettings.json; the game loads it on startup and
// Reset goes back to it. Tab / Esc / Options opens it; it pauses the game.
// Online, the tuning is the host's to change: a joiner's menu (IsRestricted) shows only a
// player's own preferences (FWWOSettingRow::bPref: field of view, sensitivity, rumble and camera
// feel, volume, HUD) and the key bindings; the other pages, the Items tab, search results, Reset
// and Save all leave the rest alone.

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/WWOUI.h"
#include "WWOSettingsMenu.generated.h"

class UVerticalBox;
class UScrollBox;
class UWidgetSwitcher;
class UTextBlock;
class USlider;
class UButton;
class UWrapBox;
class UPanelWidget;
class UProgressBar;
class AWWOCharacter;

struct FWWOSettingRow
{
	FString Header; // set = a header line instead of a slider
	FName Id;
	FString Label;
	float Min = 0.f, Max = 1.f, Step = 0.1f;
	TFunction<float()> Get;
	TFunction<void(float)> Set;
	TFunction<FString(float)> Show;
	TFunction<FString()> LiveHeader; // a header whose text is redrawn each frame while the menu is open
	// A player's own preference (field of view, sensitivity, volume, HUD). Everything else is game
	// tuning for development: in someone else's game only these show (see UWWOSettingsMenu::bRestricted).
	bool bPref = false;
};

struct FWWOMenuButton
{
	FString Text;
	TFunction<void()> Fn;
	bool bPref = false; // shown to a joiner too
};

struct FWWOSettingSection
{
	FString Key;
	FString Title;
	TArray<FWWOSettingRow> Rows;
	TArray<FWWOMenuButton> Buttons;
};

struct FWWOBrowserItem
{
	FString Key, Title, Subtitle;
};

struct FWWOBrowser
{
	TFunction<TArray<FWWOBrowserItem>()> Provider;
	UWidgetSwitcher* Switcher = nullptr; // 0 = list, 1 = item
	UScrollBox* List = nullptr;
	UTextBlock* Title = nullptr;
	UScrollBox* Rows = nullptr;
	FString OpenKey;
};

UCLASS()
class WHATWASOURS_API UWWOSettingsMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	bool IsOpen() const { return bOpen; }
	bool IsPrepared() const { return bBuilt; }
	bool IsRestricted() const { return bRestricted; }
	int32 ShownPageCount() const; // the pages its sidebars list (for the autotest)
	void Prepare();
	void Open();
	void Close();
	void Back();
	void CycleGlobalTab(int32 Step);
	int32 GetGlobalTab() const { return GlobalTab; }
	bool ShowPage(const FString& Title);      // opens the page with that title, on whichever tab it's under (false: none)
	int32 GetTopTab() const { return TopTab; }
	void AutoTestSearch(const FString& Text); // for the autotest: as if typed into the search box ("" clears it)
	// What's on screen, for the autotest: the open item of the browser that's showing ("" = its list).
	FString OpenItemKey() const;
	float GlobalScrollOffset() const; // the Global tab's scroll position

	float GetValue(const FString& Key, FName Id) const;
	void SetValue(const FString& Key, FName Id, float Value);
	void ResetSection(const FString& Key);
	void ResetAll();
	bool SaveDefaults();
	// One slider row of a section ("weapon/rifle", "global/Camera"), for another screen to show (the
	// firing tester). Null when the section has no such row.
	UWidget* MakeRowFor(const FString& Key, FName Id);

	// Text and a filled text button built in this menu's tree (WWOUI::Text / TextButton). The other
	// menus use WWOUI directly; these stay for the many calls in this file.
	UButton* MakeButton(UObject* Outer, const FString& Text, TFunction<void()> Fn, float FontPx = 22.f, const FLinearColor& Color = FLinearColor(0.2f, 0.21f, 0.25f),
		bool bLeftAlign = false, FMargin Padding = FMargin(10.f, 4.f), bool bWrap = false);
	UTextBlock* MakeText(const FString& Text, float FontPx, const FLinearColor& Color = FLinearColor::White, bool bWrap = false);
private:
	// Collapsible slider groups (BuildRows): open or closed per section and header, and the sections
	// whose first group has been opened by default.
	TMap<FString, bool> GroupState;
	TSet<FString> FirstGroupSeen;

	bool bOpen = false;
	bool bBuilt = false;
	bool bRestricted = false; // in someone else's game: only your own preferences (set in Prepare)
	void MarkPreferences();
	bool Allowed(const FWWOSettingRow& Row) const { return !bRestricted || Row.bPref; }
	bool SectionAllowed(const FWWOSettingSection& S) const;
	TArray<FWWOSettingSection> Sections;
	TArray<FString> GlobalKeys;
	TMap<FString, TMap<FName, float>> Defaults;
	struct FLiveSlider { TWeakObjectPtr<USlider> Slider; TWeakObjectPtr<UTextBlock> Value; TWeakObjectPtr<UProgressBar> Fill; FString Key; int32 Row; };
	TArray<FLiveSlider> Live;
	struct FLiveText { TWeakObjectPtr<UTextBlock> Text; TFunction<FString()> Get; };
	TArray<FLiveText> LiveTexts;
	TArray<FWWOBrowser> Browsers;
	UPROPERTY() TArray<TObjectPtr<UObject>> Handlers;
	int32 PruneAt = 512;
	void KeepHandler(UObject* Handler); // adds it, and now and then drops those whose widget has left the page
	UPROPERTY() TObjectPtr<UWidgetSwitcher> GlobalSwitcher;
	UPROPERTY() TObjectPtr<UButton> SaveButton;
	double SaveRevertAt = 0.0; // real time the "Saved ✓" label goes back to "Save as defaults"
	int32 GlobalTab = 0;

	void Build();
	void RegisterSections(AWWOCharacter* P);
	void RecordDefaults();
	void LoadSaved();
	FWWOSettingSection* Find(const FString& Key);
	const FWWOSettingSection* Find(const FString& Key) const;
	const FWWOSettingRow* RowFor(const FString& Key, FName Id) const;
	void BuildRows(UPanelWidget* Container, const FString& Key);
	UWidget* MakeRow(const FString& Key, int32 RowIndex);
	void SyncSliders();
	UWidget* MakeBrowser(TFunction<TArray<FWWOBrowserItem>()> Provider);
	void ShowList(int32 BrowserIndex);
	void OpenItem(int32 BrowserIndex, const FString& Key);
	// The layout: five tabs across the top (Player, Combat, Enemies, Match, Items), each with its
	// pages listed down the left, and the open page in one column on the right. A page is a global
	// section's sliders or a list of items to open (Items). GlobalSwitcher holds every page.
	static constexpr int32 NumTopTabs = 6;
	static constexpr int32 ItemsTab = 4;
	static constexpr int32 ControlsTab = 5;
	static inline const TCHAR* const TopTabNames[NumTopTabs] = { TEXT("PLAYER"), TEXT("COMBAT"), TEXT("ENEMIES"), TEXT("MATCH"), TEXT("ITEMS"), TEXT("CONTROLS") };
	struct FPage { FString Title; int32 Tab = 0; TWeakObjectPtr<UTextBlock> Label; TWeakObjectPtr<class UBorder> Mark; };
	UPROPERTY() TArray<TObjectPtr<UWidget>> TopTabWidgets; // each tab's button, then the bar before it (null for the first)
	TArray<FPage> Pages;
	TArray<int32> LastPage; // per tab, the page it was last on
	int32 CurrentPage = 0, TopTab = 0, SearchPage = 0;
	FString SearchText;
	UPROPERTY() TObjectPtr<UWidgetSwitcher> SideSwitcher;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> TopLabels;
	UPROPERTY() TArray<TObjectPtr<class UBorder>> TopLines;
	UPROPERTY() TObjectPtr<UTextBlock> PageTitle;
	UPROPERTY() TObjectPtr<class UEditableTextBox> SearchBox;
	UPROPERTY() TObjectPtr<UScrollBox> SearchResults;
	void SelectPage(int32 Index);
	// Controls: the key bindings page (AWWOPlayerController keeps the bindings).
	int32 BindsPage = INDEX_NONE;
	UPROPERTY() TObjectPtr<UScrollBox> BindsList;
	void BuildBinds();
	void SelectTopTab(int32 Index);
	UTextBlock* Spaced(const FString& Text, float FontPx, const FLinearColor& Color, int32 Spacing);
	UButton* PlainButton(UWidget* Inside, TFunction<void()> Fn);
	UFUNCTION() void OnSearchChanged(const FText& Text);
	UFUNCTION() void OnSearchCommitted(const FText& Text, ETextCommit::Type Type);
	static FString ValueText(const FWWOSettingRow& Row, float V);
};
