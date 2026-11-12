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
class AWWOCharacter;
class FWWOMenuCursor;
class FWWOMenuKeys;
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
	void HandleMenuPress(EWWOAction Id); // a menu key (Menu, MenuBack, TabNext / TabPrev, CharacterMenu) went down
	void ToggleSettingsMenu();
	void ToggleCharacterMenu();
	void CloseMenus();
	void OnMenuClosed();
	UWWOSettingsMenu* GetSettingsMenu() const { return SettingsMenu; }
	UWWOLaunchMenu* GetLaunchMenu() const { return LaunchMenu; } // the launch screen, when this map opened on it

	// Key bindings (the settings menu's Controls page). Each row has a keyboard / mouse key and a
	// controller button; changes are kept in Saved/WWO/Keybinds.json.
	static const TArray<FWWOBindRow>& BindRows();
	static FString KeyName(const FKey& Key);
	FKey GetBind(FName Row, bool bPad) const;
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

	// -WWOAutoTest: a scripted play-through that saves screenshots (for checking the port).
	bool bAutoTest = false;
	bool bAutoTestMenusOnly = false; // -WWOAutoTestMenus: just the controller-only menu steps
	bool bAutoTestBodyOnly = false; // -WWOAutoTestBody: just the body animation steps
	bool bAutoTestGraftsOnly = false; // -WWOAutoTestGrafts: just the flora graft steps
	bool bAutoTestMapsOnly = false; // -WWOAutoTestMaps: just the map views
	// Map steps (WWOPlayerControllerAutoMaps.cpp): the level from fixed cameras.
	int32 AutoMapIndex = -1;
	float AutoMapClock = 0.f;
	UPROPERTY() TObjectPtr<class ACameraActor> AutoMapCamera;
	bool UpdateAutoMapTest(class AWWOCharacter* P, float DeltaTime);
	// Weapon mod steps (WWOPlayerControllerAutoMods.cpp): draft mods, configs, perks.
	bool bAutoTestModsOnly = false; // -WWOAutoTestMods
	bool bAutoTestGunsOnly = false; // -WWOAutoTestGuns: gun model screenshots (in the mods test)
	bool bAutoTestHiddenOnly = false; // -WWOAutoTestHidden: the hidden aim assist, off then on (in the mods test)
	TWeakObjectPtr<AActor> AutoHiddenTarget;
	float AutoHiddenOff = 0.f;
	int32 AutoGunIdx = 0;
	int32 AutoGunStage = 0;
	FVector2D AutoGunKick = FVector2D::ZeroVector; // the hand cannon's kick, put back after the diagonal shot
	int32 AutoModStep = 0;
	float AutoModClock = 0.f;
	int32 AutoModConfig = INDEX_NONE;
	TArray<float> AutoModBase;
	float AutoModBaseReload = 0.f, AutoModBaseSwap = 0.f;
	bool UpdateAutoModTest(class AWWOCharacter* P, float DeltaTime);
	// -WWOAutoTestTitle / -WWOAutoTestTitleJoin (WWOPlayerControllerAutoTitle.cpp): the launch screen's
	// flow, across the map loads it causes.
	bool bAutoTestTitle = false;
	void UpdateAutoTitleTest(float DeltaTime);
	// -WWOAutoTestTabs (WWOPlayerControllerAutoTabs.cpp): the Weapon and Shell tabs fit on one screen.
	bool bAutoTestTabs = false;
	void UpdateAutoTabsTest(float DeltaTime);
	// -WWOAutoTestNet (WWOPlayerControllerAutoNet.cpp): online play, run on the host and the joiner.
	bool bAutoTestNet = false;
	float AutoNetWait = 0.f;   // seconds since start
	float AutoNetClock = 0.f; // seconds since both players were in and settled (negative while settling)
	float AutoNetNextShot = 0.f;
	int32 AutoNetShots = 0;
	int32 AutoNetStep = -1;
	int32 AutoNetTotalCorrections = 0;
	bool bAutoNetSawOther = false, bAutoNetMantleJumped = false, bAutoNetSawMantle = false;
	FVector AutoNetCrate = FVector::ZeroVector;
	FBox AutoNetCrateBox = FBox(ForceInit);
	int32 AutoNetDamageEvents = 0;
	TWeakObjectPtr<AActor> AutoNetShootTarget; // the dummy this side shoots at
	TWeakObjectPtr<AActor> AutoNetMeleeTarget; // joiner: the dummy it punches
	float AutoNetMeleeHealth = 0.f;
	float AutoNetReviveSeen = 0.f;
	FTransform AutoNetJoinerStart = FTransform(FVector::ZeroVector); // host: where the joiner began
	TWeakObjectPtr<AActor> AutoNetGrenadeTarget; // joiner: the dummy it threw at
	float AutoNetGrenadeHealth = 0.f; // joiner: the most of the host's revive bar it saw
	float AutoNetShootHealth = 0.f;
	int32 AutoNetShotPulls = 0; // joiner: OnDamaged fired from the host's replicated health
	bool bAutoNetBound = false;
	// -WWOAutoTestPvP (WWOPlayerControllerAutoPvP.cpp): team deathmatch, host (-WWOHostPvP) and joiner.
	bool bAutoTestPvP = false;
	float AutoPvPHostHealth = 0.f;
	int32 AutoPvPRejected = 0;
	void UpdateAutoPvPTest(float DeltaTime);
	float StressClock = 0.f, StressFrames = 0.f, StressWorst = 0.f, StressNext = 5.f;
	int32 StressCorrections = 0;
	void LogJoinerStress(float DeltaTime); // -WWOStress on a joiner
	void UpdateAutoNetTest(float DeltaTime);
	// -WWOPerfTest (WWOPerfTest.cpp): frame and GPU time at each map view, logged, then quit.
	bool bPerfTest = false;
	bool bPerfProfiled = false;
	float PerfStartClock = 0.f;
	TArray<FVector2D> PerfSums; // per view: (frame ms, GPU ms) summed
	TArray<int32> PerfFrames;
	void UpdatePerfTest(class AWWOCharacter* P, float DeltaTime);
	// Graft steps (WWOPlayerControllerAutoGrafts.cpp): loadouts shown in the character preview.
	float AutoGraftClock = 0.f;
	float AutoGraftCaptureFov = 0.f;
	TObjectPtr<class UWWOFloraData> AutoGraftFlora;
	TObjectPtr<class UWWOGrenadeData> AutoGraftGrenade;
	TArray<TObjectPtr<class UWWOAbilityData>> AutoGraftAbilities;
	TArray<TObjectPtr<class UWWOFramePartData>> AutoGraftFrame;
	bool UpdateAutoGraftTest(class AWWOCharacter* P, float DeltaTime);
	// Body steps (WWOPlayerControllerAutoBody.cpp): a row of mannequins in each body state.
	int32 AutoBodyStep = 0;
	float AutoBodyClock = 0.f;
	FVector AutoBodyOrigin = FVector::ZeroVector;
	UPROPERTY() TArray<TObjectPtr<AActor>> AutoBodies;
	UPROPERTY() TArray<TObjectPtr<AActor>> AutoBodyProps;
	UPROPERTY() TObjectPtr<class ACameraActor> AutoBodyCamera;
	bool UpdateAutoBodyTest(class AWWOCharacter* P, float DeltaTime);
	// Menu steps (WWOPlayerControllerAutoMenu.cpp): the menus driven by injected DualSense input.
	int32 AutoMenuStep = 0;
	float AutoMenuClock = 0.f;
	int32 AutoMenuFails = 0;
	float AutoMenuValue = 0.f;
	bool UpdateAutoMenuTest(class AWWOCharacter* P, float DeltaTime);
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
	float AutoLookYaw = 0.f, AutoLookPitch = 0.f, AutoLookFrom = 0.f; // stick look step
	float AutoGunPitch = 0.f; // gun steps
	int32 AutoGunAmmo = 0;
	double AutoGunTime = 0.0;
	// Grenade steps: each grenade kind thrown at a training target in turn (the wall last).
	TArray<TObjectPtr<class UWWOGrenadeData>> AutoGrenades;
	TObjectPtr<class UWWOGrenadeData> AutoGrenadeBefore; // equipped again afterwards
	TWeakObjectPtr<AActor> AutoTarget; // the training target
	TWeakObjectPtr<AActor> AutoGrenadeTarget; // this grenade's (an enemy for blind and slow)
	int32 AutoGrenadeIndex = -1;
	float AutoPhase = 0.f;    // seconds into this grenade (or melee) step
	float AutoGoneAt = -1.f;  // when the thrown grenade went off (-1: still out)
	float AutoTargetHealth = 0.f;
	bool bAutoGrenadeOut = false, bAutoShotBlast = false, bAutoShotArmed = false, bAutoLongThrow = false;
	// Melee steps: a lunge onto the training target, then a punch on the nearest enemy.
	int32 AutoMeleeIndex = -1;
	FVector AutoMeleeFrom = FVector::ZeroVector;
	float AutoLungeTime = 0.f;
	bool bAutoSawLunge = false, bAutoMeleeLogged = false, bAutoShotPunch = false;
	// Ability and ult steps (WWOPlayerControllerAutoAbility.cpp): every ability equipped and
	// set off in turn, then each flora's ult; the loadout is put back afterwards.
	TArray<TObjectPtr<class UWWOAbilityData>> AutoAbilities;
	TArray<TObjectPtr<class UWWOFloraData>> AutoFloras;
	TObjectPtr<class UWWOFloraData> AutoFloraBefore;
	TObjectPtr<class UWWOGrenadeData> AutoAbilityGrenadeBefore;
	TArray<TObjectPtr<class UWWOAbilityData>> AutoAbilitiesBefore;
	TArray<TObjectPtr<class UWWOTraitData>> AutoTraitsBefore;
	TMap<TWeakObjectPtr<AActor>, float> AutoHealths; // living targets' health before a hit
	TWeakObjectPtr<AActor> AutoAbilityTarget;
	int32 AutoAbilityIndex = -1;
	float AutoAbilityPhase = -1.f;
	float AutoAbilityValue = 0.f;
	FVector AutoAbilityFrom = FVector::ZeroVector;
	bool bAutoAbilityShot = false, bAutoAbilityFlag = false;
	bool UpdateAutoAbilityTest(class AWWOCharacter* P, float DeltaTime);
	bool UpdateAutoGrenadeTest(class AWWOCharacter* P, float DeltaTime);
	bool UpdateAutoMeleeTest(class AWWOCharacter* P, float DeltaTime);
	bool PlaceForAutoTest(class AWWOCharacter* P, AActor* Target, float DistanceM, float YawOffset, bool bMove = true);
	void UpdateAutoTest(float DeltaTime);
	void Shot(const TCHAR* Name);
};
