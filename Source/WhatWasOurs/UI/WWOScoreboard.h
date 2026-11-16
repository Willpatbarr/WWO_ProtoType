#pragma once

// The end of a PvP match: a page over the game with the result, every player's and bot's kills
// and precision kills (by team, most kills first), and three buttons: NEXT MATCH (the same map
// again), CHANGE MAP (pick one of the PvP maps) and QUIT (back to the launch screen).
// AWWOPlayerController opens it when AWWOGameState::bMatchOver turns on. Next match and the map
// are the host's to pick (AWWOGameMode::Restart / LoadMap); a joiner sees them dimmed and can quit.

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WWOScoreboard.generated.h"

class UWidget;
class UButton;

UCLASS()
class WHATWASOURS_API UWWOScoreboard : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	bool IsOpen() const { return bOpen; }
	void Open();
	void Close();
	void Back(); // from the map list to the buttons
	// The buttons (the test run calls these too).
	void NextMatch();
	void ShowMaps();
	void ChooseMap(FName Level);
	void Quit();
	bool CanPick() const; // this machine decides the next match (the host, or playing alone)
	double OpenedAt = 0.0; // real seconds

private:
	bool bOpen = false;
	bool bMaps = false;   // showing the map list
	bool bLeaving = false; // a button was pressed: the next map is loading
	FString Shown;        // what the page was built from (rebuilt when the scores replicate in late)
	FString Signature() const;
	void Rebuild();
	UPROPERTY() TArray<TObjectPtr<UObject>> Handlers;
	UWidget* TeamTable(int32 Team) const;
	UButton* Button(const FString& Title, const FString& Sub, bool bEnabled, TFunction<void()> Fn);
};
