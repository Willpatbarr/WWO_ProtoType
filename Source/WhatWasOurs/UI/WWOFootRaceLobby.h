#pragma once

// FootRace's lobby (Game/WWOFootRaceRules.h): the page every machine sees while a FootRace match waits for
// its host (AWWOGameState::bLobby). Two columns, BLUE and ORANGE: the people on each team, a button to
// join it (anyone, freely, until the start), and its bots: a stepper for the host (0 up to filling the team
// to the map's team size), the count for a joiner. Under them the host's START RACE, or for a joiner a line
// saying the host starts it. AWWOPlayerController opens it when the lobby opens and closes it when the race
// starts; the buttons go through the controller (LobbyChooseTeam, LobbyBots, LobbyStart), so a joiner's
// team choice is a server call and only the host's player can start. Built like the scoreboard: rebuilt
// when what it shows changes (people, teams, bot counts), the ring cursor painted over it, so a controller
// drives it as it does the other menus.

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WWOFootRaceLobby.generated.h"

class UWidget;
class UButton;

UCLASS()
class WHATWASOURS_API UWWOFootRaceLobby : public UUserWidget
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
	bool IsHost() const; // this machine picks the bots and starts the race
	// The buttons (the test run calls these too).
	void JoinTeam(int32 Team);
	void StepBots(int32 Team, int32 Delta);
	bool StartRace();
	// What the page lists (tests): the names in a team's column, as last built.
	TArray<FString> Listed[2];
	int32 BotsShown[2] = { 0, 0 };

private:
	bool bOpen = false;
	FString Shown;
	FString Signature() const;
	void Rebuild();
	UPROPERTY() TArray<TObjectPtr<UObject>> Handlers;
	UWidget* TeamColumn(int32 Team);
	UButton* Button(const FString& Title, bool bEnabled, float Width, TFunction<void()> Fn);
};
