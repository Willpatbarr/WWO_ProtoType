#pragma once

// Input and menus (Godot: the input map, ControllerChord, MenuCursor, and the menu toggles).
// - Enhanced Input actions and mapping contexts are built in code (see BuildInput), so the
//   controls table from the Godot handoff lives in one place.
// - Gameplay code polls like Godot did: IsHeld / WasJustPressed / GetMoveAxis / GetLookAxis.
// - Controller bumpers: L1 = grenade, R1 = melee, both within 0.1 s = ult (ConsumeChord).
// - Tracks whether the last input came from a controller (aim assist and HUD read it).
// - Rumble and hit-stop.

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/ForceFeedbackEffect.h"
#include "WWOTypes.h"
#include "WWOPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
struct FInputActionInstance;
class UWWOHUDWidget;
class UWWOSettingsMenu;
class UWWOCharacterMenu;
class UWWOLaunchMenu;
class UWWOScoreboard;
class UWWOFiringTester;
class AWWOCharacter;
class FWWOMenuCursor;
class FWWOMenuKeys;
class FWWOAutoTest;
class SWidget;

UENUM()
enum class EWWOChord : uint8 { Grenade, Melee, Ult };

// One line of the Controls page: something you do, its keyboard / mouse key and its controller
// button. Count for an action = that device has none; MoveDir 0-3 = forward, back, left, right.
struct FWWOBindRow
{
	FName Id;
	const TCHAR* Label;
	EWWOAction Kbm;
	FKey KbmKey, KbmAlt;
	EWWOAction Pad;
	FKey PadKey;
	int32 MoveDir = -1;
};

UCLASS()
class WHATWASOURS_API AWWOPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AWWOPlayerController();

	virtual void BeginPlay() override;
	// PvP: move to the other team (the host decides; it replicates).
	UFUNCTION(Server, Reliable) void ServerSwitchTeam();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void Tick(float DeltaSeconds) override; // runs while paused too (PlayerTick doesn't)

	// With a menu open your buttons work the menu, not your character: nothing reads as held or
	// pressed (online the game doesn't pause under a menu, so this is what keeps the gun quiet).
	bool IsHeld(EWWOAction Action) const { return (Held[(int32)Action] && !IsMenuOpen()) || ScriptHeld[(int32)Action]; }
	bool WasJustPressed(EWWOAction Action) const { return (JustPressed[(int32)Action] && !IsMenuOpen()) || ScriptNow[(int32)Action]; }
	// Scripted input (the firing tester's ADS / FIRE / STRAFE buttons): read exactly like the real
	// thing, and also while a pane has the cursor. A press lasts one frame.
	void ScriptHold(EWWOAction Action, bool bDown) { ScriptHeld[(int32)Action] = bDown; }
	void ScriptPress(EWWOAction Action) { ScriptQueued[(int32)Action] = true; }
	void ScriptMove(FVector2D Axis) { ScriptMoveValue = Axis; }
	void ClearScript();
	// One step of the forgiving trigger: V is how far in it is (0-1). True on the frame it counts as
	// pulled: when it has come in by Reset from the lowest it has been since the last pull. It counts
	// as let go once it has come back out by Reset from the furthest in it got (or nearly all the way).
	static bool StepTrigger(float V, float Reset, bool& bDown, float& Low, float& High);
	UWWOFiringTester* GetFiringTester() const { return FiringTester; }
	bool IsOtherMenuOpen() const; // a real menu (settings, character, launch), not the tester's own pane
	void RefreshMenuMode() { SetMenuMode(IsMenuOpen()); }
	FVector2D GetMoveAxis() const;  // x = right, y = forward, length <= 1
	FVector2D GetLookAxis() const;  // right stick, x = right, y = up
	bool ConsumeChord(EWWOChord Chord);

	bool bUsingController = false; // last input came from a controller

	// Controller rumble: a new buzz replaces the last one. Scaled by RumbleScale on the gun.
	void Rumble(float Weak, float Strong, float Seconds);

	// Nearly freezes the game for a moment of real time, so a hit lands with weight.
	void Hitstop(float Seconds);

	// Menus
	bool IsMenuOpen() const;
	void HandleMenuPress(EWWOAction Id); // a menu key (Menu, MenuBack, TabNext / TabPrev, CharacterMenu) went down
	void ToggleSettingsMenu();
	void ToggleCharacterMenu();
	void CloseMenus();
	UWWOSettingsMenu* GetSettingsMenu() const { return SettingsMenu; }
	UWWOLaunchMenu* GetLaunchMenu() const { return LaunchMenu; } // the launch screen, when this map opened on it

	// Key bindings (the settings menu's Controls page). Each row has a keyboard / mouse key and a
	// controller button; changes are kept in Saved/WWO/Keybinds.json.
	static const TArray<FWWOBindRow>& BindRows();
	static FString KeyName(const FKey& Key);
	FKey GetBind(FName Row, bool bPad) const;
	FString BindHint(FName Row) const; // "E / Square": that line's keys as bound now, for on-screen hints
	bool IsCharacterMenuOpen() const;
	void SetBind(FName Row, bool bPad, const FKey& Key);
	void ResetBinds();
	void BeginKeyCapture(FName Row, bool bPad, TFunction<void()> OnDone); // the next press becomes that binding
	bool IsCapturing(FName Row, bool bPad) const { return CaptureRow == Row && bCapturePad == bPad; }
	UWWOHUDWidget* GetHUDWidget() const { return HUDWidget; }

	AWWOCharacter* GetWWOCharacter() const;

private:
	UPROPERTY() TObjectPtr<UInputMappingContext> GameContext;
	UPROPERTY() TArray<TObjectPtr<UInputAction>> Actions;
	UPROPERTY() TObjectPtr<UInputAction> MoveAction;
	UPROPERTY() TObjectPtr<UInputAction> LookAction;
	UPROPERTY() TObjectPtr<UInputAction> MouseLookAction;
	TMap<const UInputAction*, EWWOAction> ActionIds;
	TMap<EWWOAction, UInputAction*> ActionFor;
	TMap<FName, FKey> Binds; // "<row>/kbm" or "<row>/pad" -> the key, where it isn't the default
	FName CaptureRow;
	bool bCapturePad = false;
	void MapKeys();
	void LoadBinds();
	void SaveBinds() const;

	UPROPERTY() TObjectPtr<UWWOHUDWidget> HUDWidget;
	UPROPERTY() TObjectPtr<UWWOSettingsMenu> SettingsMenu;
	UPROPERTY() TObjectPtr<UWWOCharacterMenu> CharacterMenu;
	UPROPERTY() TObjectPtr<UWWOLaunchMenu> LaunchMenu; // only when the map opened on the launch screen
	UPROPERTY() TObjectPtr<UWWOScoreboard> Scoreboard; // the end of a PvP match (made when the first one ends)
	void UpdateScoreboard();
	UPROPERTY() TObjectPtr<UWWOFiringTester> FiringTester; // only in the firing tester (AWWOGameMode::bTester)
	bool ScriptHeld[(int32)EWWOAction::Count] = {};
	bool ScriptQueued[(int32)EWWOAction::Count] = {};
	bool ScriptNow[(int32)EWWOAction::Count] = {};
	FVector2D ScriptMoveValue = FVector2D::ZeroVector;
	TSharedPtr<FWWOMenuCursor> MenuCursor;
	TSharedPtr<FWWOMenuKeys> MenuKeys; // Play-in-Editor only
	bool bCreateHeld = false;
	TSharedPtr<SWidget> GameViewportWidget() const;

	bool Held[(int32)EWWOAction::Count] = {};
	bool JustPressed[(int32)EWWOAction::Count] = {};
	bool BlockedUntilReleased[(int32)EWWOAction::Count] = {};
	FVector2D MoveValue = FVector2D::ZeroVector;
	FVector2D LookValue = FVector2D::ZeroVector;
	double MouseRecaptureTime = 0.0; // real time the game last took the mouse back from a menu

	TMap<EWWOChord, float> ChordPending; // bumper -> seconds since pressed
	TMap<EWWOChord, int32> ChordFired;   // chord -> frames it stays readable

	float HitstopLeft = 0.f; // real seconds
	FDynamicForceFeedbackHandle StrongHandle = 0;
	FDynamicForceFeedbackHandle WeakHandle = 0;

	void BuildInput();
	UInputAction* MakeAction(EWWOAction Id, bool bWhenPaused = false);
	void OnActionStarted(const FInputActionInstance& Instance);
	void OnActionCompleted(const FInputActionInstance& Instance);
	void OnMove(const FInputActionInstance& Instance);
	void OnLook(const FInputActionInstance& Instance);
	void OnMouseLook(const FInputActionInstance& Instance);
	void UpdateChord(float DeltaTime);
	void UpdateDevice();
	void SetMenuMode(bool bOpen);

	// The fire trigger, read as an axis (UpdateTrigger).
	bool bTriggerDown = false;
	float TriggerLow = 0.f, TriggerHigh = 0.f;
	double FireStartedAt = -10.0; // real time of the engine's last press of Fire
	void UpdateTrigger();

	// A scripted test run (-WWOAutoTest..., -WWOPerfTest): its driver, made in BeginPlay only when the
	// command line asks for one and ticked from Tick. All test state and code is in Player/WWOAutoTest.h;
	// it is a friend because tests write the input arrays and open the menus directly.
	friend class FWWOAutoTest;
	TSharedPtr<FWWOAutoTest> AutoTest;
};
