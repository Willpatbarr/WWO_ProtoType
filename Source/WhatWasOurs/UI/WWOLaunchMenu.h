#pragma once

// The launch screen: what the game opens on (see UWWONetSubsystem::WantsTitle). It fills the
// window in the character and settings menus' style and sits over the start map, paused.
// - Title: the game's name and one big PLAY button (plus Loadout, Settings and Quit).
// - Setup, after PLAY, one choice at a time:
//   1. Connection: Online host (others on your network can join), Online join, or Local.
//   2. Host and Local: the mode, PvP (team deathmatch; alone it's against bots) or PvE (waves).
//      Join: the open games on the network, refreshed every few seconds; pick one.
//   3. Host and Local: the map, from the ones that suit the mode (UWWONetSubsystem::Maps).
//   Then START GAME (JOIN GAME) appears and starts it (UWWONetSubsystem: Host / PlayLocal / JoinGame).
// Circle / Esc steps back one choice, then to the title. The menu cursor drives it (FWWOMenuCursor).

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/WWOUI.h"
#include "WWOLaunchMenu.generated.h"

class UTextBlock;
class UButton;
class UWidget;

UCLASS()
class WHATWASOURS_API UWWOLaunchMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	enum { ConnNone = -1, ConnHost, ConnJoin, ConnLocal, ConnTester }; // ConnTester: the firing tester (UWWOFiringTester), no mode or map to pick
	enum { ModeNone = -1, ModePvP, ModePvE };

	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	bool IsOpen() const { return bOpen; }
	void Open();
	void Close();
	void Back();

	// Each step (the buttons call these; so does the autotest).
	void Play();
	void ChooseConnection(int32 Choice);
	void ChooseMode(int32 Choice);
	void ChooseMap(FName Level);
	void ChooseGame(int32 Index);
	bool CanStart() const;
	void Start();
	FString DebugState() const; // "setup host pvp L_Arboretum ready"

private:
	bool bOpen = false;
	bool bSetup = false; // false: the title
	int32 Connection = ConnNone, Mode = ModeNone, Game = INDEX_NONE;
	FName Map;
	FString GameKey;        // the picked game (host and map), to keep it picked across refreshes
	int32 SeenGames = -1;   // UWWONetSubsystem::GamesSerial the list was built from
	FString ShownGames;     // what that list said (a search that finds the same games redraws nothing)
	FString GamesSignature() const;
	void SetWorldHidden(bool bHidden); // nothing renders behind this opaque screen
	bool bSeenSearching = false;
	double NextSearch = 0.0; // real time of the next automatic search (Join)
	double StartedAt = 0.0;  // real time START was pressed (0: not starting)
	UPROPERTY() TArray<TObjectPtr<UObject>> Handlers;
	UPROPERTY() TObjectPtr<UTextBlock> StatusText;

	void Rebuild();
	UPROPERTY() TObjectPtr<class UEditableTextBox> TagBox; // the gamer tag (Host and Join)
	UFUNCTION() void OnTagChanged(const FText& Text);
	bool NeedsTag() const; // hosting or joining with no gamer tag entered yet
	UWidget* BuildTitle();
	UWidget* BuildSetup();
	// A bordered box to click: a title and a line under it; bOn draws it as the picked one.
	UButton* Choice(const FString& Title, const FString& Sub, bool bOn, TFunction<void()> Fn, float TitlePx = WWOUI::Type::Title, const FString& Tag = FString());
	UButton* Boxed(UWidget* Inside, const FLinearColor& Fill, const FLinearColor& Edge, float EdgeWidth, const FMargin& Padding, TFunction<void()> Fn);
	UWidget* StepLabel(int32 Number, const FString& Text) const;
	FString StatusLine() const;
};
