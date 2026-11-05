#pragma once

// Input and menus (Godot: the input map, ControllerChord, MenuCursor, and the menu toggles).
// - Enhanced Input actions and mapping contexts are built in code (see BuildInput), so the
//   controls table from the Godot handoff lives in one place.
// - Gameplay code polls like Godot did: IsHeld / WasJustPressed / GetMoveAxis / GetLookAxis.
// - Controller bumpers: L1 = grenade, R1 = melee, both within 0.1 s = ult (ConsumeChord).
// - Tracks whether the last input came from a controller (aim assist and HUD read it).
// - Rumble and hit-stop.

#include "CoreMinimal.h"
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
class AWWOCharacter;
class FWWOMenuCursor;

UENUM()
enum class EWWOChord : uint8 { Grenade, Melee, Ult };

UCLASS()
class WHATWASOURS_API AWWOPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AWWOPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void Tick(float DeltaSeconds) override; // runs while paused too (PlayerTick doesn't)

	bool IsHeld(EWWOAction Action) const { return Held[(int32)Action]; }
	bool WasJustPressed(EWWOAction Action) const { return JustPressed[(int32)Action]; }
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
	void ToggleSettingsMenu();
	void ToggleCharacterMenu();
	void CloseMenus();
	void OnMenuClosed();
	UWWOSettingsMenu* GetSettingsMenu() const { return SettingsMenu; }
	UWWOHUDWidget* GetHUDWidget() const { return HUDWidget; }

	AWWOCharacter* GetWWOCharacter() const;

private:
	UPROPERTY() TObjectPtr<UInputMappingContext> GameContext;
	UPROPERTY() TArray<TObjectPtr<UInputAction>> Actions;
	UPROPERTY() TObjectPtr<UInputAction> MoveAction;
	UPROPERTY() TObjectPtr<UInputAction> LookAction;
	UPROPERTY() TObjectPtr<UInputAction> MouseLookAction;
	TMap<const UInputAction*, EWWOAction> ActionIds;

	UPROPERTY() TObjectPtr<UWWOHUDWidget> HUDWidget;
	UPROPERTY() TObjectPtr<UWWOSettingsMenu> SettingsMenu;
	UPROPERTY() TObjectPtr<UWWOCharacterMenu> CharacterMenu;
	TSharedPtr<FWWOMenuCursor> MenuCursor;

	bool Held[(int32)EWWOAction::Count] = {};
	bool JustPressed[(int32)EWWOAction::Count] = {};
	FVector2D MoveValue = FVector2D::ZeroVector;
	FVector2D LookValue = FVector2D::ZeroVector;

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
	void HandleMenuPress(EWWOAction Id);
	void UpdateChord(float DeltaTime);
	void UpdateDevice();
	void SetMenuMode(bool bOpen);

	// -WWOAutoTest: a scripted play-through that saves screenshots (for checking the port).
	bool bAutoTest = false;
	float AutoTestClock = 0.f;
	int32 AutoTestStep = 0;
	FVector AutoSlideFrom = FVector::ZeroVector; // ground movement steps: where the slide started
	bool bAutoTrackSlide = false;
	// Jump steps: one jump's arc (feet height above takeoff), then the landing dip.
	bool bAutoTrackJump = false;
	bool bAutoJumpLanded = false;
	bool bAutoSawMantle = false;
	float AutoJumpTime = 0.f, AutoJumpFromZ = 0.f, AutoJumpPeak = 0.f, AutoJumpPeakTime = 0.f, AutoJumpLandTime = 0.f, AutoJumpDip = 0.f;
	FVector AutoCrateAt = FVector::ZeroVector;
	void UpdateAutoTest(float DeltaTime);
	void Shot(const TCHAR* Name);
};
