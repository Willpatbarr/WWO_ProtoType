#include "Player/WWOPlayerController.h"
#include "UI/WWOFiringTester.h"
#include "Game/WWOGameMode.h"
#include "Player/WWOTestRun.h"
#include "Systems/WWOSound.h"
#include "Game/WWOPlayerState.h"
#include "Game/WWOGameMode.h"
#include "Net/WWONetSubsystem.h"
#include "RHI.h"
#include "RenderCore.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOHealthComponent.h"
#include "UI/WWOHUDWidget.h"
#include "UI/WWOSettingsMenu.h"
#include "UI/WWOCharacterMenu.h"
#include "UI/WWOLaunchMenu.h"
#include "UI/WWOMenuCursor.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/SViewport.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "UnrealClient.h"
#include "Player/WWOGrenadeComponent.h"
#include "Player/WWOUltComponent.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "Systems/WWOTargetable.h"
#include "WhatWasOurs.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "World/WWOTarget.h"
#include "Weapons/WWOGrenade.h"
#include "Enemies/WWOEnemy.h"

namespace
{
	constexpr float ChordWindow = 0.1f; // seconds to wait for the second bumper
}

void AWWOPlayerController::ServerSwitchTeam_Implementation()
{
	AWWOGameMode* GM = GetWorld()->GetAuthGameMode<AWWOGameMode>();
	AWWOPlayerState* PS = GetPlayerState<AWWOPlayerState>();
	if (GM && GM->bPvP && PS) GM->SwitchTeam(this);
}

AWWOPlayerController::AWWOPlayerController()
{
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

AWWOCharacter* AWWOPlayerController::GetWWOCharacter() const
{
	return Cast<AWWOCharacter>(GetPawn());
}

void AWWOPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController()) return;

	if (UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Input->AddMappingContext(GameContext, 0);
	}

	HUDWidget = CreateWidget<UWWOHUDWidget>(this, UWWOHUDWidget::StaticClass());
	HUDWidget->AddToPlayerScreen(0); // the player's own view rectangle (the whole screen, but for the firing tester's window)
	SettingsMenu = CreateWidget<UWWOSettingsMenu>(this, UWWOSettingsMenu::StaticClass());
	SettingsMenu->AddToViewport(10);
	SettingsMenu->SetVisibility(ESlateVisibility::Collapsed);
	CharacterMenu = CreateWidget<UWWOCharacterMenu>(this, UWWOCharacterMenu::StaticClass());
	CharacterMenu->AddToViewport(11);
	CharacterMenu->SetVisibility(ESlateVisibility::Collapsed);

	// The launch screen: over the start map, paused, when the game opens (and after leaving a game).
	UWWONetSubsystem* Net = UWWONetSubsystem::Get(this);
	if (Net && Net->WantsTitle(this))
	{
		LaunchMenu = CreateWidget<UWWOLaunchMenu>(this, UWWOLaunchMenu::StaticClass());
		LaunchMenu->AddToViewport(5); // over the HUD, under the settings and character menus it can open
		LaunchMenu->Open();
		HUDWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
	// The firing tester's screen: its pane on the left, the player's view in a window.
	if (const AWWOGameMode* GM = GetWorld()->GetAuthGameMode<AWWOGameMode>(); GM && GM->bTester && !LaunchMenu)
	{
		FiringTester = CreateWidget<UWWOFiringTester>(this, UWWOFiringTester::StaticClass());
		FiringTester->AddToViewport(4); // over the HUD (drawn in the window), under the menus
		FiringTester->Open();
	}
	SetMenuMode(IsMenuOpen());
	if (Net) Net->HandleCommandLine(this); // -WWOHost / -WWOJoin
	if (FCString::Stristr(FCommandLine::Get(), TEXT("WWOAutoTest"))) WWOTest::Watch(); // count the FAIL lines of a test run
	bAutoTest = FParse::Param(FCommandLine::Get(), TEXT("WWOAutoTest"));
	bAutoTestMenusOnly = FParse::Param(FCommandLine::Get(), TEXT("WWOAutoTestMenus"));
	bAutoTestBodyOnly = FParse::Param(FCommandLine::Get(), TEXT("WWOAutoTestBody"));
	bAutoTestGraftsOnly = FParse::Param(FCommandLine::Get(), TEXT("WWOAutoTestGrafts"));
	bAutoTestMapsOnly = FParse::Param(FCommandLine::Get(), TEXT("WWOAutoTestMaps"));
	bPerfTest = FParse::Param(FCommandLine::Get(), TEXT("WWOPerfTest"));
	bAutoTestNet = FParse::Param(FCommandLine::Get(), TEXT("WWOAutoTestNet"));
	bAutoTestTabs = FParse::Param(FCommandLine::Get(), TEXT("WWOAutoTestTabs")) || FParse::Param(FCommandLine::Get(), TEXT("WWOAutoTestVignette"))
		|| FParse::Param(FCommandLine::Get(), TEXT("WWOAutoTestPose"));
	bAutoTestTitle = FParse::Param(FCommandLine::Get(), TEXT("WWOAutoTestTitle")) || FParse::Param(FCommandLine::Get(), TEXT("WWOAutoTestTitleJoin"))
		|| FParse::Param(FCommandLine::Get(), TEXT("WWOAutoTestTitleJoinFail"));
	bAutoTestPvP = FParse::Param(FCommandLine::Get(), TEXT("WWOAutoTestPvP"));
	bAutoTestTester = FParse::Param(FCommandLine::Get(), TEXT("WWOAutoTestTester"));
	bAutoTestGunsOnly = FParse::Param(FCommandLine::Get(), TEXT("WWOAutoTestGuns"));
	bAutoTestHiddenOnly = FParse::Param(FCommandLine::Get(), TEXT("WWOAutoTestHidden"));
	bAutoTestModsOnly = bAutoTestGunsOnly || bAutoTestHiddenOnly || FParse::Param(FCommandLine::Get(), TEXT("WWOAutoTestMods"))
		|| FParse::Param(FCommandLine::Get(), TEXT("WWOAutoTestFrames"));
	bAutoTest |= bAutoTestMenusOnly || bAutoTestBodyOnly || bAutoTestGraftsOnly || bAutoTestMapsOnly || bAutoTestModsOnly;

	// Play-in-Editor: the editor takes Esc to stop PIE before the game sees it. Send it to the
	// menus instead while the game viewport has focus (stop PIE with the toolbar's Stop button).
	if (GetWorld()->WorldType == EWorldType::PIE && FSlateApplication::IsInitialized())
	{
		MenuKeys = MakeShared<FWWOMenuKeys>();
		TWeakObjectPtr<AWWOPlayerController> WeakThis = this;
		MenuKeys->OnEscape = [WeakThis] { if (WeakThis.IsValid()) WeakThis->HandleMenuPress(EWWOAction::Menu); };
		FSlateApplication::Get().RegisterInputPreProcessor(MenuKeys);
	}
}

TSharedPtr<SWidget> AWWOPlayerController::GameViewportWidget() const
{
	const UGameViewportClient* Client = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	return Client ? StaticCastSharedPtr<SWidget>(Client->GetGameViewportWidget()) : nullptr;
}

void AWWOPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (IsLocalController()) if (const UGameInstance* GI = GetGameInstance()) if (UWWOLoadoutSubsystem* L = GI->GetSubsystem<UWWOLoadoutSubsystem>()) L->FlushSave(); // a map change or quit
	if (MenuCursor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(MenuCursor);
	}
	MenuCursor.Reset();
	if (MenuKeys.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(MenuKeys);
	}
	MenuKeys.Reset();
	UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	Super::EndPlay(Reason);
}

// --- Input setup ---------------------------------------------------------------------------

UInputAction* AWWOPlayerController::MakeAction(EWWOAction Id, bool bWhenPaused)
{
	UInputAction* Action = NewObject<UInputAction>(this);
	Action->ValueType = EInputActionValueType::Boolean;
	Action->bTriggerWhenPaused = bWhenPaused;
	Actions.Add(Action);
	ActionIds.Add(Action, Id);
	return Action;
}

const TArray<FWWOBindRow>& AWWOPlayerController::BindRows()
{
	// What the Controls page lists: one line per thing you do, with its keyboard / mouse key and its
	// controller button. (Melee and the grenade are two actions each, one per device, because the
	// bumpers also chord into the ult.) The menu's own keys aren't here: they stay put.
	const FKey None = EKeys::Invalid;
	static const TArray<FWWOBindRow> Rows = {
		{ "move_forward", TEXT("Move forward"), EWWOAction::Count, EKeys::W, None, EWWOAction::Count, None, 0 },
		{ "move_back", TEXT("Move back"), EWWOAction::Count, EKeys::S, None, EWWOAction::Count, None, 1 },
		{ "move_left", TEXT("Move left"), EWWOAction::Count, EKeys::A, None, EWWOAction::Count, None, 2 },
		{ "move_right", TEXT("Move right"), EWWOAction::Count, EKeys::D, None, EWWOAction::Count, None, 3 },
		{ "jump", TEXT("Jump"), EWWOAction::Jump, EKeys::SpaceBar, None, EWWOAction::Jump, EKeys::Gamepad_FaceButton_Bottom },
		{ "sprint", TEXT("Sprint"), EWWOAction::Sprint, EKeys::LeftShift, None, EWWOAction::Sprint, EKeys::Gamepad_LeftThumbstick },
		{ "crouch", TEXT("Crouch / slide"), EWWOAction::Crouch, EKeys::C, EKeys::LeftControl, EWWOAction::Crouch, EKeys::Gamepad_FaceButton_Right },
		{ "aim", TEXT("Aim"), EWWOAction::Aim, EKeys::RightMouseButton, None, EWWOAction::Aim, EKeys::Gamepad_LeftTrigger },
		{ "fire", TEXT("Fire"), EWWOAction::Fire, EKeys::LeftMouseButton, None, EWWOAction::Fire, EKeys::Gamepad_RightTrigger },
		{ "reload", TEXT("Reload"), EWWOAction::Reload, EKeys::R, None, EWWOAction::Reload, EKeys::Gamepad_FaceButton_Left },
		{ "melee", TEXT("Melee"), EWWOAction::Melee, EKeys::V, None, EWWOAction::MeleePad, EKeys::Gamepad_RightShoulder },
		{ "grenade", TEXT("Grenade"), EWWOAction::Grenade, EKeys::G, None, EWWOAction::GrenadePad, EKeys::Gamepad_LeftShoulder },
		{ "ult", TEXT("Ult (controller: melee + grenade together)"), EWWOAction::Ult, EKeys::F, None, EWWOAction::Count, None },
		{ "ability1", TEXT("Ability 1"), EWWOAction::Ability1, EKeys::One, None, EWWOAction::Ability1, EKeys::Gamepad_DPad_Left },
		{ "ability2", TEXT("Ability 2"), EWWOAction::Ability2, EKeys::Two, None, EWWOAction::Ability2, EKeys::Gamepad_DPad_Right },
		{ "weapon_next", TEXT("Swap weapon"), EWWOAction::WeaponNext, EKeys::MouseScrollUp, None, EWWOAction::WeaponNext, EKeys::Gamepad_FaceButton_Top },
		{ "weapon_prev", TEXT("Swap weapon (other way)"), EWWOAction::WeaponPrev, EKeys::MouseScrollDown, None, EWWOAction::Count, None },
		{ "next_config", TEXT("Other weapon slot (names it)"), EWWOAction::NextConfig, EKeys::B, None, EWWOAction::NextConfig, EKeys::Gamepad_DPad_Down },
		{ "revive", TEXT("Revive (hold)"), EWWOAction::Revive, EKeys::E, None, EWWOAction::Revive, EKeys::Gamepad_FaceButton_Left },
		{ "character_menu", TEXT("Character menu"), EWWOAction::CharacterMenu, EKeys::I, None, EWWOAction::CharacterMenu, EKeys::Gamepad_Special_Left },
	};
	return Rows;
}

FString AWWOPlayerController::BindHint(FName Row) const
{
	TArray<FString> Keys;
	for (const bool bPad : { false, true })
	{
		if (const FKey Key = GetBind(Row, bPad); Key.IsValid()) Keys.Add(KeyName(Key));
	}
	return FString::Join(Keys, TEXT(" / "));
}

bool AWWOPlayerController::IsCharacterMenuOpen() const
{
	return CharacterMenu && CharacterMenu->IsOpen();
}

FKey AWWOPlayerController::GetBind(FName Row, bool bPad) const
{
	if (const FKey* Changed = Binds.Find(FName(*(Row.ToString() + (bPad ? TEXT("/pad") : TEXT("/kbm")))))) return *Changed;
	for (const FWWOBindRow& R : BindRows()) if (R.Id == Row) return bPad ? R.PadKey : R.KbmKey;
	return EKeys::Invalid;
}

FString AWWOPlayerController::KeyName(const FKey& Key)
{
	// Controller buttons by what's printed on them (PlayStation / Xbox); everything else by its own name.
	static const TMap<FKey, FString> Pad = {
		{ EKeys::Gamepad_FaceButton_Bottom, TEXT("Cross / A") }, { EKeys::Gamepad_FaceButton_Right, TEXT("Circle / B") },
		{ EKeys::Gamepad_FaceButton_Left, TEXT("Square / X") }, { EKeys::Gamepad_FaceButton_Top, TEXT("Triangle / Y") },
		{ EKeys::Gamepad_LeftShoulder, TEXT("L1 / LB") }, { EKeys::Gamepad_RightShoulder, TEXT("R1 / RB") },
		{ EKeys::Gamepad_LeftTrigger, TEXT("L2 / LT") }, { EKeys::Gamepad_RightTrigger, TEXT("R2 / RT") },
		{ EKeys::Gamepad_LeftThumbstick, TEXT("L3 (stick click)") }, { EKeys::Gamepad_RightThumbstick, TEXT("R3 (stick click)") },
		{ EKeys::Gamepad_DPad_Up, TEXT("D-pad up") }, { EKeys::Gamepad_DPad_Down, TEXT("D-pad down") },
		{ EKeys::Gamepad_DPad_Left, TEXT("D-pad left") }, { EKeys::Gamepad_DPad_Right, TEXT("D-pad right") },
		{ EKeys::Gamepad_Special_Left, TEXT("Touchpad / View") }, { EKeys::Gamepad_Special_Right, TEXT("Options / Menu") },
	};
	if (!Key.IsValid()) return TEXT("—");
	if (const FString* Name = Pad.Find(Key)) return *Name;
	return Key.GetDisplayName(false).ToString();
}

void AWWOPlayerController::SetBind(FName Row, bool bPad, const FKey& Key)
{
	// A key already on another line of the same device changes places with this one.
	const FKey Old = GetBind(Row, bPad);
	if (Old == Key) return;
	for (const FWWOBindRow& R : BindRows())
	{
		if (R.Id != Row && GetBind(R.Id, bPad) == Key && (bPad ? R.PadKey : R.KbmKey).IsValid())
		{
			Binds.Add(FName(*(R.Id.ToString() + (bPad ? TEXT("/pad") : TEXT("/kbm")))), Old); // every line that had it (Reload and Revive share Square)
		}
	}
	Binds.Add(FName(*(Row.ToString() + (bPad ? TEXT("/pad") : TEXT("/kbm")))), Key);
	UWWOSound::PlayFlat(this, "ui_bind");
	SaveBinds();
	MapKeys();
}

void AWWOPlayerController::ResetBinds()
{
	Binds.Reset();
	SaveBinds();
	MapKeys();
}

namespace
{
	FString BindsPath() { return FPaths::ProjectSavedDir() / TEXT("WWO/Keybinds.json"); }
	// Test runs press the default keys: they neither read nor write anyone's saved bindings.
	bool BindsTestRun() { return FCString::Stristr(FCommandLine::Get(), TEXT("WWOAutoTest")) || FParse::Param(FCommandLine::Get(), TEXT("WWOStress")); }
}

void AWWOPlayerController::SaveBinds() const
{
	if (BindsTestRun()) return;
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	for (const auto& Pair : Binds) Json->SetStringField(Pair.Key.ToString(), Pair.Value.GetFName().ToString());
	FString Text;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
	FFileHelper::SaveStringToFile(Text, *BindsPath());
}

void AWWOPlayerController::LoadBinds()
{
	Binds.Reset();
	if (BindsTestRun()) return;
	FString Text;
	TSharedPtr<FJsonObject> Json;
	if (!FFileHelper::LoadFileToString(Text, *BindsPath()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()) return;
	for (const auto& Pair : Json->Values)
	{
		const FKey Key(FName(*Pair.Value->AsString()));
		if (Key.IsValid()) Binds.Add(FName(*Pair.Key), Key);
	}
}

void AWWOPlayerController::BeginKeyCapture(FName Row, bool bPad, TFunction<void()> OnDone)
{
	// The next key, mouse button or controller button pressed becomes this line's (of the matching
	// device; the other device's presses are ignored). Esc cancels. The menu cursor's input hook
	// sees every press first, so the press doesn't also click or type.
	CaptureRow = Row;
	bCapturePad = bPad;
	if (!MenuCursor.IsValid()) return;
	TWeakObjectPtr<AWWOPlayerController> WeakThis = this;
	MenuCursor->Capture = [WeakThis, Row, bPad, OnDone](const FKey& Key)
	{
		if (!WeakThis.IsValid()) return true;
		// Esc cancels, and so does Options (the menu button, never bindable): with only a controller
		// in hand there has to be a way out of a keyboard line clicked by mistake.
		const bool bCancel = Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Right;
		// Not the menu's own keys, and only this line's device.
		if (!bCancel && (Key == EKeys::Tab || Key.IsGamepadKey() != bPad)) return false;
		WeakThis->CaptureRow = NAME_None;
		if (!bCancel) WeakThis->SetBind(Row, bPad, Key);
		if (OnDone) OnDone();
		return true;
	};
}

void AWWOPlayerController::BuildInput()
{
	// The controls from the Godot handoff (keyboard/mouse and DualSense), with whatever the player
	// has rebound on the settings menu's Controls page (Saved/WWO/Keybinds.json) on top.
	GameContext = NewObject<UInputMappingContext>(this);
	for (int32 Id = 0; Id < (int32)EWWOAction::Count; ++Id)
	{
		const EWWOAction A = (EWWOAction)Id;
		// Menu keys work while paused (the menus pause the game).
		const bool bMenu = A == EWWOAction::Menu || A == EWWOAction::MenuBack || A == EWWOAction::TabNext || A == EWWOAction::TabPrev || A == EWWOAction::CharacterMenu;
		ActionFor.Add(A, MakeAction(A, bMenu));
	}
	MoveAction = NewObject<UInputAction>(this);
	MoveAction->ValueType = EInputActionValueType::Axis2D;
	LookAction = NewObject<UInputAction>(this);
	LookAction->ValueType = EInputActionValueType::Axis2D;
	MouseLookAction = NewObject<UInputAction>(this);
	MouseLookAction->ValueType = EInputActionValueType::Axis2D;
	LoadBinds();
	MapKeys();
}

void AWWOPlayerController::MapKeys()
{
	// Every key onto its action, from scratch (at startup, and again whenever a binding changes).
	GameContext->UnmapAll();
	auto Map = [this](EWWOAction Action, const FKey& Key)
	{
		if (Key.IsValid() && ActionFor.Contains(Action)) GameContext->MapKey(ActionFor[Action], Key);
	};
	UInputModifierSwizzleAxis* ToY = NewObject<UInputModifierSwizzleAxis>(this); // X -> Y
	UInputModifierNegate* Negate = NewObject<UInputModifierNegate>(this);
	for (const FWWOBindRow& R : BindRows())
	{
		const FKey Kbm = GetBind(R.Id, false);
		if (R.MoveDir >= 0)
		{
			// Movement: forward and back go onto Y, back and left are negative.
			if (!Kbm.IsValid()) continue;
			FEnhancedActionKeyMapping& M = GameContext->MapKey(MoveAction, Kbm);
			if (R.MoveDir <= 1) M.Modifiers.Add(ToY);
			if (R.MoveDir == 1 || R.MoveDir == 2) M.Modifiers.Add(Negate);
			continue;
		}
		Map(R.Kbm, Kbm);
		if (Kbm == R.KbmKey) Map(R.Kbm, R.KbmAlt); // the default's second key (Ctrl also crouches) goes with the default
		Map(R.Pad, GetBind(R.Id, true));
	}
	// The menu's own keys. (Gamepad_Special_Left is the DualSense touchpad click on Mac, Godot's
	// button too. The Create button isn't a key in Unreal's Apple controller code; Tick polls it.)
	for (const FKey& Key : { EKeys::Tab, EKeys::Escape, EKeys::Gamepad_Special_Right }) Map(EWWOAction::Menu, Key);
	Map(EWWOAction::MenuBack, EKeys::Gamepad_FaceButton_Right); // Esc is Menu, which also backs out (one step per press)
	for (const FKey& Key : { EKeys::E, EKeys::Gamepad_RightShoulder }) Map(EWWOAction::TabNext, Key);
	for (const FKey& Key : { EKeys::Q, EKeys::Gamepad_LeftShoulder }) Map(EWWOAction::TabPrev, Key);

	// The sticks and the mouse: the left stick moves (dead zone 0.2), the right one looks (0.15).
	UInputModifierDeadZone* MoveDead = NewObject<UInputModifierDeadZone>(this);
	MoveDead->LowerThreshold = 0.2f;
	MoveDead->Type = EDeadZoneType::Radial;
	GameContext->MapKey(MoveAction, EKeys::Gamepad_Left2D).Modifiers.Add(MoveDead);
	UInputModifierDeadZone* LookDead = NewObject<UInputModifierDeadZone>(this);
	LookDead->LowerThreshold = 0.15f;
	LookDead->Type = EDeadZoneType::Radial;
	// The stick's Y came out inverted compared to the Godot build: flip it.
	UInputModifierNegate* FlipY = NewObject<UInputModifierNegate>(this);
	FlipY->bX = false;
	FlipY->bZ = false;
	FEnhancedActionKeyMapping& Stick = GameContext->MapKey(LookAction, EKeys::Gamepad_Right2D);
	Stick.Modifiers.Add(LookDead);
	Stick.Modifiers.Add(FlipY);
	GameContext->MapKey(MouseLookAction, EKeys::Mouse2D);
	if (UEnhancedInputLocalPlayerSubsystem* Input = GetLocalPlayer() ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()) : nullptr)
	{
		Input->RequestRebuildControlMappings();
	}
}

void AWWOPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	BuildInput();
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC) return;
	for (UInputAction* Action : Actions)
	{
		EIC->BindAction(Action, ETriggerEvent::Started, this, &AWWOPlayerController::OnActionStarted);
		EIC->BindAction(Action, ETriggerEvent::Completed, this, &AWWOPlayerController::OnActionCompleted);
	}
	EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AWWOPlayerController::OnMove);
	EIC->BindAction(MoveAction, ETriggerEvent::Completed, this, &AWWOPlayerController::OnMove);
	EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &AWWOPlayerController::OnLook);
	EIC->BindAction(LookAction, ETriggerEvent::Completed, this, &AWWOPlayerController::OnLook);
	EIC->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AWWOPlayerController::OnMouseLook);
}

void AWWOPlayerController::OnActionStarted(const FInputActionInstance& Instance)
{
	const EWWOAction* Id = ActionIds.Find(Instance.GetSourceAction());
	if (!Id) return;
	if (BlockedUntilReleased[(int32)*Id]) return; // still held from the press that closed a menu
	Held[(int32)*Id] = true;
	JustPressed[(int32)*Id] = true;
	// The bumpers are grenade and melee in play and the tab keys in a menu: never both.
	if (*Id == EWWOAction::GrenadePad && !IsPaused() && !IsMenuOpen()) ChordPending.Add(EWWOChord::Grenade, 0.f);
	if (*Id == EWWOAction::MeleePad && !IsPaused() && !IsMenuOpen()) ChordPending.Add(EWWOChord::Melee, 0.f);
	HandleMenuPress(*Id);
}

void AWWOPlayerController::OnActionCompleted(const FInputActionInstance& Instance)
{
	if (const EWWOAction* Id = ActionIds.Find(Instance.GetSourceAction()))
	{
		Held[(int32)*Id] = false;
		BlockedUntilReleased[(int32)*Id] = false;
	}
}

void AWWOPlayerController::OnMove(const FInputActionInstance& Instance)
{
	MoveValue = Instance.GetValue().Get<FVector2D>();
}

// Test runs (-WWOStress) ignore the mouse and sticks: the window opens in the background, and the
// camera following the mouse there would make every run look at a different scene.
static bool TestRunIgnoresInput()
{
	static const bool bIgnore = FParse::Param(FCommandLine::Get(), TEXT("WWOStress"));
	return bIgnore;
}

void AWWOPlayerController::OnLook(const FInputActionInstance& Instance)
{
	LookValue = Instance.GetValue().Get<FVector2D>();
}

void AWWOPlayerController::OnMouseLook(const FInputActionInstance& Instance)
{
	if (TestRunIgnoresInput()) return;
	if (IsMenuOpen() || IsPaused()) return;
	AWWOCharacter* Player = GetWWOCharacter();
	if (!Player || Player->bUlting) return;
	const FVector2D Delta = Instance.GetValue().Get<FVector2D>();
	// The cursor jumps when the window first captures it: ignore the first moments of play, and a
	// big jump right after a menu hands the mouse back. Godot has no filter, so real input
	// (fast flicks, deltas piled up over a slow frame) always gets through otherwise.
	const double Now = GetWorld()->GetRealTimeSeconds();
	if (Now < 2.0) return;
	if (Now - MouseRecaptureTime < 0.25 && Delta.Size() > 250.f) return;
	Player->Look(Delta * Player->MouseSensitivity * 0.001f);
}

FVector2D AWWOPlayerController::GetMoveAxis() const
{
	if (!ScriptMoveValue.IsZero()) return ScriptMoveValue.GetClampedToMaxSize(1.f);
	if (IsMenuOpen() || TestRunIgnoresInput()) return FVector2D::ZeroVector;
	return MoveValue.GetClampedToMaxSize(1.f);
}

FVector2D AWWOPlayerController::GetLookAxis() const
{
	if (IsMenuOpen() || TestRunIgnoresInput()) return FVector2D::ZeroVector;
	return LookValue.GetClampedToMaxSize(1.f);
}

// --- Each frame ---------------------------------------------------------------------------

void AWWOPlayerController::PlayerTick(float DeltaTime)
{
	FMemory::Memzero(JustPressed, sizeof(JustPressed));
	FMemory::Memcpy(ScriptNow, ScriptQueued, sizeof(ScriptNow)); // a scripted press: readable for this frame
	FMemory::Memzero(ScriptQueued, sizeof(ScriptQueued));
	Super::PlayerTick(DeltaTime); // processes input: fills Held / JustPressed for this frame
	UpdateDevice();
	UpdateChord(DeltaTime);
	if (SettingsMenu && !SettingsMenu->IsPrepared() && GetPawn()) SettingsMenu->Prepare(); // applies the saved settings

	if (HitstopLeft > 0.f)
	{
		HitstopLeft -= FApp::GetDeltaTime(); // real time
		if (HitstopLeft <= 0.f) UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	}
}

void AWWOPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsLocalController())
	{
		// The DualSense Create button toggles the character menu, like the touchpad.
		const bool bCreate = FWWOMenuKeys::IsCreateButtonDown() && FPlatformApplicationMisc::IsThisApplicationForeground();
		if (bCreate && !bCreateHeld)
		{
			bUsingController = true;
			HandleMenuPress(EWWOAction::CharacterMenu);
		}
		bCreateHeld = bCreate;
		if (MenuKeys.IsValid() && !MenuKeys->Viewport.IsValid()) MenuKeys->Viewport = GameViewportWidget();
	}
	if (bPerfTest)
	{
		if (AWWOCharacter* P = GetWWOCharacter()) UpdatePerfTest(P, FApp::GetDeltaTime());
	}
	else if (bAutoTestTitle) { if (IsLocalController()) UpdateAutoTitleTest(FApp::GetDeltaTime()); }
	else if (bAutoTestTabs) { if (IsLocalController()) UpdateAutoTabsTest(FApp::GetDeltaTime()); }
	else if (bAutoTestTester) { if (IsLocalController()) UpdateAutoTesterTest(FApp::GetDeltaTime()); }
	else if (bAutoTestPvP) UpdateAutoPvPTest(FApp::GetDeltaTime());
	else if (GetNetMode() == NM_Client && FParse::Param(FCommandLine::Get(), TEXT("WWOStress"))) LogJoinerStress(FApp::GetDeltaTime());
	else if (bAutoTestNet) UpdateAutoNetTest(FApp::GetDeltaTime());
	else if (bAutoTest) UpdateAutoTest(FApp::GetDeltaTime());
}

void AWWOPlayerController::UpdateDevice()
{
	// Which device you're using, ignoring tiny stick drift and mouse jitter.
	static const FKey PadAxes[] = { EKeys::Gamepad_LeftX, EKeys::Gamepad_LeftY, EKeys::Gamepad_RightX, EKeys::Gamepad_RightY,
		EKeys::Gamepad_LeftTriggerAxis, EKeys::Gamepad_RightTriggerAxis };
	for (const FKey& Axis : PadAxes)
	{
		if (FMath::Abs(GetInputAnalogKeyState(Axis)) > 0.25f) { bUsingController = true; return; }
	}
	for (int32 i = 0; i < (int32)EWWOAction::Count; ++i)
	{
		if (JustPressed[i])
		{
			// Any just-pressed action: decide by which key types are down.
			bool bPad = false;
			for (const FKey Key : { EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Left,
				EKeys::Gamepad_FaceButton_Top, EKeys::Gamepad_LeftShoulder, EKeys::Gamepad_RightShoulder, EKeys::Gamepad_LeftTrigger,
				EKeys::Gamepad_RightTrigger, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Right, EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_LeftThumbstick,
				EKeys::Gamepad_RightThumbstick, EKeys::Gamepad_Special_Left, EKeys::Gamepad_Special_Right })
			{
				if (IsInputKeyDown(Key)) { bPad = true; break; }
			}
			bUsingController = bPad;
			return;
		}
	}
	float DX = 0.f, DY = 0.f;
	GetInputMouseDelta(DX, DY);
	if (FVector2D(DX, DY).Size() > 2.f) bUsingController = false;
}

void AWWOPlayerController::UpdateChord(float DeltaTime)
{
	for (auto It = ChordFired.CreateIterator(); It; ++It)
	{
		if (--It.Value() < 0) It.RemoveCurrent();
	}
	if (ChordPending.Contains(EWWOChord::Grenade) && ChordPending.Contains(EWWOChord::Melee))
	{
		ChordPending.Reset();
		ChordFired.Add(EWWOChord::Ult, 1);
		return;
	}
	for (auto It = ChordPending.CreateIterator(); It; ++It)
	{
		It.Value() += DeltaTime;
		if (It.Value() >= ChordWindow)
		{
			ChordFired.Add(It.Key(), 1);
			It.RemoveCurrent();
		}
	}
}

bool AWWOPlayerController::ConsumeChord(EWWOChord Chord)
{
	if (IsMenuOpen())
	{
		ChordFired.Remove(Chord); // pressed as a menu opened: it doesn't wait for the menu to close
		return false;
	}
	return ChordFired.Remove(Chord) > 0;
}

void AWWOPlayerController::Rumble(float Weak, float Strong, float Seconds)
{
	// A controller has two motors: the big one (strong) is a low thump, the small one (weak) a sharp buzz.
	const AWWOCharacter* Player = GetWWOCharacter();
	const float Scale = Player ? Player->Weapon->RumbleScale : 1.f;
	Weak = FMath::Clamp(Weak * Scale, 0.f, 1.f);
	Strong = FMath::Clamp(Strong * Scale, 0.f, 1.f);
	if (Seconds <= 0.f || (Weak <= 0.f && Strong <= 0.f)) return;
	StrongHandle = PlayDynamicForceFeedback(Strong, Seconds, true, false, true, false, EDynamicForceFeedbackAction::Start, StrongHandle);
	WeakHandle = PlayDynamicForceFeedback(Weak, Seconds, false, true, false, true, EDynamicForceFeedbackAction::Start, WeakHandle);
}

void AWWOPlayerController::Hitstop(float Seconds)
{
	if (Seconds <= 0.f) return;
	if (GetNetMode() != NM_Standalone) return; // time dilation is the whole world's: online a punch would slow everyone
	HitstopLeft = Seconds;
	UGameplayStatics::SetGlobalTimeDilation(this, 0.03f);
}

// --- Menus -------------------------------------------------------------------------------------

bool AWWOPlayerController::IsMenuOpen() const
{
	return IsOtherMenuOpen() || (FiringTester && FiringTester->WantsCursor());
}

bool AWWOPlayerController::IsOtherMenuOpen() const
{
	return (SettingsMenu && SettingsMenu->IsOpen()) || (CharacterMenu && CharacterMenu->IsOpen()) || (LaunchMenu && LaunchMenu->IsOpen());
}

void AWWOPlayerController::ClearScript()
{
	FMemory::Memzero(ScriptHeld, sizeof(ScriptHeld));
	FMemory::Memzero(ScriptQueued, sizeof(ScriptQueued));
	FMemory::Memzero(ScriptNow, sizeof(ScriptNow));
	ScriptMoveValue = FVector2D::ZeroVector;
}

void AWWOPlayerController::HandleMenuPress(EWWOAction Id)
{
	if (!SettingsMenu || !CharacterMenu) return;
	if (CharacterMenu->IsOpen())
	{
		if (Id == EWWOAction::CharacterMenu) ToggleCharacterMenu();
		else if (Id == EWWOAction::MenuBack || Id == EWWOAction::Menu) CharacterMenu->Back();
		else if (Id == EWWOAction::TabNext) CharacterMenu->CycleTab(1);
		else if (Id == EWWOAction::TabPrev) CharacterMenu->CycleTab(-1);
		return;
	}
	if (SettingsMenu->IsOpen())
	{
		if (Id == EWWOAction::MenuBack || Id == EWWOAction::Menu) SettingsMenu->Back();
		else if (Id == EWWOAction::TabNext) SettingsMenu->CycleGlobalTab(1);
		else if (Id == EWWOAction::TabPrev) SettingsMenu->CycleGlobalTab(-1);
		return;
	}
	if (LaunchMenu && LaunchMenu->IsOpen())
	{
		// The launch screen: back one step (its own buttons open the other menus).
		if (Id == EWWOAction::MenuBack || Id == EWWOAction::Menu) LaunchMenu->Back();
		return;
	}
	if (FiringTester && FiringTester->IsOpen())
	{
		// The tester's pane is its settings: the Settings key hands the cursor back to it.
		// (Circle is crouch while you have control, so only the Settings key does it.)
		if (Id == EWWOAction::Menu) { FiringTester->SetControl(false); return; }
		if (Id == EWWOAction::MenuBack) return;
	}
	if (Id == EWWOAction::Menu) ToggleSettingsMenu();
	else if (Id == EWWOAction::CharacterMenu) ToggleCharacterMenu();
}

void AWWOPlayerController::ToggleSettingsMenu()
{
	const bool bOpen = !SettingsMenu->IsOpen();
	if (bOpen) SettingsMenu->Open();
	else SettingsMenu->Close();
	SetMenuMode(IsMenuOpen()); // the launch screen may still be up under it
}

void AWWOPlayerController::ToggleCharacterMenu()
{
	const bool bOpen = !CharacterMenu->IsOpen();
	if (bOpen) CharacterMenu->Open();
	else CharacterMenu->Close();
	SetMenuMode(IsMenuOpen());
}

void AWWOPlayerController::CloseMenus()
{
	if (SettingsMenu->IsOpen()) SettingsMenu->Close();
	if (CharacterMenu->IsOpen()) CharacterMenu->Close();
	SetMenuMode(IsMenuOpen());
}

void AWWOPlayerController::SetMenuMode(bool bOpen)
{
	// Alone, a menu pauses the game. Online the match keeps running (the host's pause would freeze
	// everyone); your character just ignores your input while a menu is open (AWWOCharacter::Pressed).
	// (The firing tester's pane doesn't pause: its buttons run the player while the cursor is out.)
	if (GetNetMode() == NM_Standalone || !bOpen) SetPause(bOpen && IsOtherMenuOpen());
	bShowMouseCursor = bOpen;
	if (bOpen)
	{
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
		const bool bNewCursor = !MenuCursor.IsValid();
		if (!MenuCursor.IsValid() && FSlateApplication::IsInitialized())
		{
			MenuCursor = MakeShared<FWWOMenuCursor>();
			FSlateApplication::Get().RegisterInputPreProcessor(MenuCursor);
		}
		if (MenuCursor.IsValid()) MenuCursor->SetViewport(GameViewportWidget());
		if (MenuCursor.IsValid() && bNewCursor) MenuCursor->CenterCursor(); // not when a menu closes onto the launch screen
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
		MouseRecaptureTime = GetWorld()->GetRealTimeSeconds();
		CaptureRow = NAME_None; // a key capture left waiting doesn't outlive the menu
		if (MenuCursor.IsValid() && FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(MenuCursor);
		}
		MenuCursor.Reset();
		if (AWWOCharacter* Player = GetWWOCharacter()) Player->ResetInputEdges();
		// The button that closed the menu shouldn't also act in the game (Godot: reset_input_edges).
		// Circle is also crouch, and Enhanced Input would start Crouch on the first unpaused frame
		// while it's still held, so every game action whose key is down waits for its release.
		FMemory::Memzero(Held, sizeof(Held));
		for (const FEnhancedActionKeyMapping& Mapping : GameContext->GetMappings())
		{
			const EWWOAction* Id = ActionIds.Find(Mapping.Action.Get());
			if (Id && !Mapping.Action->bTriggerWhenPaused && IsInputKeyDown(Mapping.Key)) BlockedUntilReleased[(int32)*Id] = true;
		}
	}
}

// --- Autotest ------------------------------------------------------------------------------

void AWWOPlayerController::Shot(const TCHAR* Name)
{
	const FString Path = FPaths::ProjectSavedDir() / TEXT("AutoTest") / FString(Name) + TEXT(".png");
	FScreenshotRequest::RequestScreenshot(Path, true, false);
	// With the frame's timings, to see what costs what (menus open, effects...).
	UE_LOG(LogWWO, Display, TEXT("AutoTest shot %s (frame %.1f ms, game %.1f, render %.1f, gpu %.1f)"), *Path, FApp::GetDeltaTime() * 1000.0,
		FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime), FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
}

void AWWOPlayerController::UpdateAutoTest(float DeltaTime)
{
	AWWOCharacter* P = GetWWOCharacter();
	if (!P) return;
	AutoTestClock += DeltaTime;
	auto At = [this](float T) { return AutoTestClock >= T && AutoTestStep == FMath::RoundToInt(T * 10.f); };
	auto Next = [this](float T) { AutoTestStep = FMath::RoundToInt(T * 10.f); };
	// Aim at the nearest living target for the shooting steps.
	auto AimAtTarget = [this, P]()
	{
		AActor* Best = nullptr;
		float BestD = 1e9f;
		for (AActor* A : UWWOWorldSubsystem::Get(this)->LivingTargets())
		{
			const float D = FVector::Dist(A->GetActorLocation(), P->GetActorLocation());
			if (D < BestD && Cast<IWWOTargetable>(A)->FeetLocation().Z < 100.f) { BestD = D; Best = A; }
		}
		if (!Best) return;
		const FVector Dir = Cast<IWWOTargetable>(Best)->GetAimParts().Last().Position - P->EyeLocation();
		P->SetViewRotation(FMath::Asin(Dir.GetSafeNormal().Z), Dir.Rotation().Yaw);
	};
	auto Speed2D = [P]() { return FVector2D(P->GetVelocity()).Size() / WWO::M; };
	auto FaceOpen = [this, P]()
	{
		float BestYaw = P->GetActorRotation().Yaw, BestLen = 0.f;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOAutoTest), false, P);
		for (int32 I = 0; I < 24; ++I)
		{
			const FVector From = P->GetActorLocation();
			const FVector To = From + FRotator(0.f, I * 15.f, 0.f).Vector() * 4000.f;
			FHitResult Hit;
			const float Len = GetWorld()->SweepSingleByChannel(Hit, From, To, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(45.f), Params) ? Hit.Distance : 4000.f;
			if (Len > BestLen) { BestLen = Len; BestYaw = I * 15.f; }
		}
		P->SetViewRotation(0.f, BestYaw);
		return BestLen / WWO::M;
	};
	if (bAutoTrackSlide && !P->bSliding && AutoTestClock > 26.85f)
	{
		bAutoTrackSlide = false;
		UE_LOG(LogWWO, Display, TEXT("AutoTest move: slide covered %.2f m"), FVector2D(P->GetActorLocation() - AutoSlideFrom).Size() / WWO::M);
	}
	auto FeetZ = [P]() { return P->GetActorLocation().Z - P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(); };
	auto StartJumpTrack = [this, P, FeetZ]()
	{
		bAutoTrackJump = true;
		bAutoJumpLanded = false;
		bAutoSawMantle = false;
		AutoJumpTime = AutoJumpPeak = AutoJumpPeakTime = AutoJumpLandTime = AutoJumpDip = 0.f;
		AutoJumpFromZ = FeetZ();
		JustPressed[(int32)EWWOAction::Jump] = true;
	};
	if (bAutoTrackJump)
	{
		AutoJumpTime += DeltaTime;
		bAutoSawMantle |= P->IsMantling();
		const float Rise = (FeetZ() - AutoJumpFromZ) / WWO::M;
		if (!bAutoJumpLanded)
		{
			if (Rise > AutoJumpPeak) { AutoJumpPeak = Rise; AutoJumpPeakTime = AutoJumpTime; }
			if (AutoJumpTime > 0.05f && P->IsOnFloor() && !P->IsMantling()) { bAutoJumpLanded = true; AutoJumpLandTime = AutoJumpTime; }
		}
		else
		{
			AutoJumpDip = FMath::Min(AutoJumpDip, P->LandOffset);
			if (AutoJumpTime > AutoJumpLandTime + 0.3f)
			{
				bAutoTrackJump = false;
				UE_LOG(LogWWO, Display, TEXT("AutoTest jump: apex %.3f m at %.3f s, landed at %.3f s (%.3f m from takeoff), jumps used %d, mantled %d, dip %.3f m"),
					AutoJumpPeak, AutoJumpPeakTime, AutoJumpLandTime, Rise, P->JumpsUsed, bAutoSawMantle, AutoJumpDip);
			}
		}
	}
	if (AutoTestStep == 0) AutoTestStep = 100; // start at 10 s (shaders, AI settle)
	if (At(10.f))
	{
		Shot(TEXT("01_start"));
		if (bAutoTestMenusOnly) { AutoTestClock = 45.3f; Next(45.3f); }
		else if (bAutoTestBodyOnly) { AutoTestClock = 45.4f; Next(45.4f); }
		else if (bAutoTestGraftsOnly) { AutoTestClock = 45.5f; Next(45.5f); }
		else if (bAutoTestMapsOnly) { AutoTestClock = 45.6f; Next(45.6f); }
		else if (bAutoTestModsOnly) { AutoTestClock = 45.7f; Next(45.7f); }
		else Next(11.f);
	}
	else if (At(11.f)) { AimAtTarget(); Held[(int32)EWWOAction::Fire] = true; Next(12.f); }
	else if (At(12.f)) { Shot(TEXT("02_firing")); Next(13.f); }
	else if (At(13.f)) { Held[(int32)EWWOAction::Fire] = false; Held[(int32)EWWOAction::Aim] = true; Next(13.5f); }
	else if (At(13.5f))
	{
		UE_LOG(LogWWO, Display, TEXT("AutoTest look: ADS amount %.2f fov %.2f look multiplier %.3f"), P->Weapon->AdsAmount,
			P->Camera->FieldOfView, P->Weapon->LookMultiplier());
		Shot(TEXT("03_ads")); Held[(int32)EWWOAction::Aim] = false; Next(14.f);
	}
	else if (At(14.f)) { P->Grenades->ThrowGrenade(); Next(15.2f); }
	else if (At(15.2f)) { Shot(TEXT("04_grenade")); Next(16.f); }
	else if (At(16.f)) { JustPressed[(int32)EWWOAction::Melee] = true; Next(16.2f); }
	else if (At(16.2f)) { Shot(TEXT("05_melee")); Next(17.f); }
	else if (At(17.f)) { P->Ult->ActivateUlt(); Next(17.5f); }
	else if (At(17.5f)) { Shot(TEXT("06_ult_cast")); Next(18.2f); }
	else if (At(18.2f)) { Shot(TEXT("07_ult_blast")); Next(20.f); }
	else if (At(20.f)) { ToggleSettingsMenu(); Next(21.f); }
	else if (At(21.f)) { Shot(TEXT("08_settings")); Next(21.5f); }
	else if (At(21.5f)) { ToggleSettingsMenu(); ToggleCharacterMenu(); Next(22.5f); }
	else if (At(22.5f)) { Shot(TEXT("09_character")); Next(23.f); }
	else if (At(23.f)) { ToggleCharacterMenu(); P->SetViewRotation(0.f, P->GetActorRotation().Yaw + 180.f); Next(24.f); }
	else if (At(24.f)) { Shot(TEXT("10_allies")); Next(25.f); }
	// Ground movement: walk, crouch, sprint (stands you up), slide and its distance, stop.
	// Speeds in m/s; each run faces the longest clear line so walls don't cut it short.
	else if (At(25.f))
	{
		UE_LOG(LogWWO, Display, TEXT("AutoTest move: clear run %.1f m; stats walk %.2f sprint %.2f crouch %.2f slide %.2f over %.2f m"), FaceOpen(),
			P->M("walk_speed", P->WalkSpeed), P->M("sprint_speed", P->SprintSpeed), P->M("crouch_speed", P->CrouchSpeed),
			P->M("slide_speed", P->SlideSpeed), P->M("slide_distance", P->SlideDistance));
		MoveValue = FVector2D(0.f, 1.f);
		Next(25.5f);
	}
	else if (At(25.5f)) { UE_LOG(LogWWO, Display, TEXT("AutoTest move: walk %.2f"), Speed2D()); Held[(int32)EWWOAction::Crouch] = true; Next(25.6f); }
	else if (At(25.6f)) { Held[(int32)EWWOAction::Crouch] = false; Next(26.3f); }
	else if (At(26.3f))
	{
		UE_LOG(LogWWO, Display, TEXT("AutoTest move: crouch %.2f crouched %d eye %.1f cm above feet"), Speed2D(), P->bIsCrouched,
			P->EyeLocation().Z - (P->GetActorLocation().Z - P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
		Shot(TEXT("11_crouched"));
		UE_LOG(LogWWO, Display, TEXT("AutoTest move: clear run %.1f m"), FaceOpen());
		JustPressed[(int32)EWWOAction::Sprint] = true;
		Next(26.8f);
	}
	else if (At(26.8f))
	{
		UE_LOG(LogWWO, Display, TEXT("AutoTest move: sprint %.2f crouched %d"), Speed2D(), P->bIsCrouched);
		Held[(int32)EWWOAction::Crouch] = true;
		AutoSlideFrom = P->GetActorLocation();
		bAutoTrackSlide = true;
		Next(26.9f);
	}
	else if (At(26.9f)) { UE_LOG(LogWWO, Display, TEXT("AutoTest move: slide %.2f sliding %d"), Speed2D(), P->bSliding); Held[(int32)EWWOAction::Crouch] = false; Next(28.5f); }
	else if (At(28.5f)) { MoveValue = FVector2D::ZeroVector; Next(28.6f); }
	else if (At(28.6f)) { UE_LOG(LogWWO, Display, TEXT("AutoTest move: stopped %.2f"), Speed2D()); Next(29.5f); }
	else if (At(29.5f))
	{
		UE_LOG(LogWWO, Display, TEXT("AutoTest move: after slide crouched %d eye %.1f cm above feet"), P->bIsCrouched,
			P->EyeLocation().Z - (P->GetActorLocation().Z - P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
		Next(30.f);
	}
	// Jump and air, next to the nearest crate (1.12 m) with damage off so the waves can't
	// interrupt: a standing ground jump, a ground jump with the air jump 0.3 s in (heights,
	// timings, landing dip), then a running jump onto the crate from 3.5 m.
	else if (At(30.f))
	{
		P->Health->GodMode = 1.f;
		if (P->Health->bDead) P->Health->Respawn();
		AStaticMeshActor* Crate = nullptr;
		float BestD = 1e9f;
		for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
		{
			const FVector S = It->GetActorScale3D();
			if (!It->Tags.Contains(FName("Cover")) || !FMath::IsNearlyEqual(S.Z, 1.12f, 0.01f) || FMath::Abs(It->GetActorLocation().Z - 56.f) > 5.f) continue;
			const float D = FVector::Dist(It->GetActorLocation(), P->GetActorLocation());
			if (D < BestD) { BestD = D; Crate = *It; }
		}
		if (Crate)
		{
			const FVector Face = -Crate->GetActorForwardVector();
			const FVector From = Crate->GetActorLocation() + Face * (56.f + 350.f);
			const bool bMoved = P->TeleportTo(FVector(From.X, From.Y, 102.f), P->GetActorRotation());
			AutoCrateAt = Crate->GetActorLocation();
			P->GetCharacterMovement()->Velocity = FVector::ZeroVector;
			P->SetViewRotation(0.f, (-Face).Rotation().Yaw);
			UE_LOG(LogWWO, Display, TEXT("AutoTest jump: crate %s at %s (teleported %d, %.0f cm away)"), *Crate->GetName(),
				*Crate->GetActorLocation().ToCompactString(), bMoved, FVector2D(P->GetActorLocation() - Crate->GetActorLocation()).Size());
		}
		UE_LOG(LogWWO, Display, TEXT("AutoTest jump: heights %.2f / %.2f, gravity %.1f / %.1f, apex hang %.2f, air control %.1f, coyote %.2f"),
			P->M("jump_height", P->JumpHeight1), P->M("jump_height", P->JumpHeight2), P->AscentGravity, P->DescentGravity, P->ApexHang,
			P->M("air_control", P->AirControl), P->CoyoteTime);
		Next(30.5f);
	}
	else if (At(30.5f)) { StartJumpTrack(); Next(32.f); }
	else if (At(32.f)) { StartJumpTrack(); Next(32.3f); }
	else if (At(32.3f)) { JustPressed[(int32)EWWOAction::Jump] = true; Next(34.f); }
	else if (At(34.f))
	{
		// Up against the crate: jump standing, then steer onto it with air control.
		const FVector Fwd = P->GetActorForwardVector();
		P->TeleportTo(AutoCrateAt - Fwd * (56.f + 60.f) + FVector(0.f, 0.f, 102.f - AutoCrateAt.Z), P->GetActorRotation());
		Next(34.3f);
	}
	else if (At(34.3f)) { StartJumpTrack(); Next(34.4f); }
	else if (At(34.4f)) { MoveValue = FVector2D(0.f, 1.f); Next(34.6f); }
	else if (At(34.6f)) { MoveValue = FVector2D::ZeroVector; Next(34.7f); } // drift on with the speed you built
	else if (At(34.7f)) { Shot(TEXT("12_crate_jump")); Next(35.6f); }
	else if (At(35.6f))
	{
		MoveValue = FVector2D::ZeroVector;
		UE_LOG(LogWWO, Display, TEXT("AutoTest jump: after the crate jump feet %.1f cm (crate top 112), on floor %d"), FeetZ(), P->IsOnFloor());
		Shot(TEXT("13_crate_top"));
		P->Health->GodMode = 0.f;
		Next(36.f);
	}
	// Stick look: half a second at full right and half up (y = up after the mapping's flip).
	else if (At(36.f)) // (a while after the screenshot, whose long frame would skew the timing)
	{
		AutoLookYaw = GetControlRotation().Yaw;
		AutoLookPitch = P->GetPitch();
		AutoLookFrom = AutoTestClock;
		LookValue = FVector2D(1.f, 0.5f);
		Next(36.5f);
	}
	else if (At(36.5f))
	{
		LookValue = FVector2D::ZeroVector;
		const float Seconds = AutoTestClock - AutoLookFrom;
		UE_LOG(LogWWO, Display, TEXT("AutoTest look: stick %.2f s turned %.1f deg right, %.1f deg up (no friction: %.1f / %.1f), hip fov %.2f"),
			Seconds, FMath::FindDeltaAngleDegrees(AutoLookYaw, GetControlRotation().Yaw), FMath::RadiansToDegrees(P->GetPitch() - AutoLookPitch),
			FMath::RadiansToDegrees(P->StickSensitivity * Seconds), FMath::RadiansToDegrees(P->StickSensitivity * Seconds * 0.5f), P->Camera->FieldOfView);
		Next(37.f);
	}
	// Guns: cycle through every weapon (ending back on the first), fire each, and log recoil,
	// ammo and fire rate as `AutoTest gun:`.
	else if (At(37.f) || At(39.5f) || At(40.6f) || At(42.f))
	{
		const int32 Now = AutoTestStep;
		P->Health->GodMode = 1.f; // no dying mid-test (waves are running)
		if (UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this)) Loadout->CycleWeapon(1);
		const UWWOWeaponData* W = P->Weapon->GetWeaponData();
		UE_LOG(LogWWO, Display, TEXT("AutoTest gun: equipped %s, ammo %d / %d"), W ? *W->Id.ToString() : TEXT("none"), P->Weapon->Ammo, P->Weapon->MagazineSize());
		Next(Now == 370 ? 37.5f : Now == 395 ? 40.f : Now == 406 ? 41.f : 42.5f);
	}
	else if (At(37.5f))
	{
		AimAtTarget();
		AutoGunPitch = P->GetPitch();
		AutoGunTime = GetWorld()->GetTimeSeconds();
		JustPressed[(int32)EWWOAction::Fire] = true;
		Next(37.6f);
	}
	else if (At(37.6f))
	{
		UE_LOG(LogWWO, Display, TEXT("AutoTest gun: hand cannon kick %.2f deg after %.3f game s (recoil up %.2f over %.3f s)"),
			FMath::RadiansToDegrees(P->GetPitch() - AutoGunPitch), GetWorld()->GetTimeSeconds() - AutoGunTime,
			P->Weapon->GetWeaponData()->RecoilUp, P->Weapon->GetWeaponData()->RecoilKickTime);
		Shot(TEXT("14_handcannon"));
		Next(38.1f);
	}
	else if (At(38.1f))
	{
		const UWWOWeaponData* W = P->Weapon->GetWeaponData();
		UE_LOG(LogWWO, Display, TEXT("AutoTest gun: hand cannon at %.2f deg after %.3f game s (settles to %.2f at %.3f s), ammo %d"),
			FMath::RadiansToDegrees(P->GetPitch() - AutoGunPitch), GetWorld()->GetTimeSeconds() - AutoGunTime, W->RecoilUp * (1.f - W->RecoilRecovery),
			60.f / W->FireRate + W->RecoilRecoveryDelay, P->Weapon->Ammo);
		JustPressed[(int32)EWWOAction::Reload] = true;
		Next(38.7f);
	}
	else if (At(38.7f)) { Shot(TEXT("15_reload")); UE_LOG(LogWWO, Display, TEXT("AutoTest gun: reloading %d progress %.2f"), P->Weapon->bReloading, P->Weapon->ReloadProgress); Next(39.5f); }
	else if (At(40.f)) { AimAtTarget(); JustPressed[(int32)EWWOAction::Fire] = true; Next(40.1f); }
	else if (At(40.1f)) { Shot(TEXT("16_shotgun")); UE_LOG(LogWWO, Display, TEXT("AutoTest gun: shotgun ammo %d"), P->Weapon->Ammo); Next(40.6f); }
	else if (At(41.f)) { AimAtTarget(); Held[(int32)EWWOAction::Aim] = true; Next(41.5f); }
	else if (At(41.5f)) { Shot(TEXT("17_sniper_scope")); JustPressed[(int32)EWWOAction::Fire] = true; Next(41.6f); }
	else if (At(41.6f))
	{
		UE_LOG(LogWWO, Display, TEXT("AutoTest gun: sniper ADS %.2f fov %.2f ammo %d"), P->Weapon->AdsAmount, P->Camera->FieldOfView, P->Weapon->Ammo);
		Held[(int32)EWWOAction::Aim] = false;
		Next(42.f);
	}
	else if (At(42.5f)) { AimAtTarget(); Held[(int32)EWWOAction::Aim] = true; Next(42.8f); }
	else if (At(42.8f)) { Shot(TEXT("18_rifle_ads")); Next(43.f); } // (the screenshot's long frame would fire a burst)
	else if (At(43.f)) { Held[(int32)EWWOAction::Fire] = true; AutoGunAmmo = P->Weapon->Ammo; AutoGunTime = GetWorld()->GetTimeSeconds(); Next(44.f); }
	else if (At(44.f))
	{
		Held[(int32)EWWOAction::Fire] = Held[(int32)EWWOAction::Aim] = false;
		const float Seconds = GetWorld()->GetTimeSeconds() - AutoGunTime;
		UE_LOG(LogWWO, Display, TEXT("AutoTest gun: rifle fired %d rounds in %.2f game s (fire rate %.0f rpm: about %.1f)"), AutoGunAmmo - P->Weapon->Ammo,
			Seconds, P->Weapon->GetWeaponData()->FireRate, Seconds * P->Weapon->GetWeaponData()->FireRate / 60.f);
		Next(44.5f);
	}
	else if (At(44.5f)) { AutoGrenadeIndex = -1; AutoMeleeIndex = -1; Next(45.f); }
	// Grenades: every grenade, one by one, at a training target. Then melee.
	else if (At(45.f)) { if (UpdateAutoGrenadeTest(P, DeltaTime)) Next(45.1f); }
	else if (At(45.1f)) { if (UpdateAutoMeleeTest(P, DeltaTime)) Next(45.2f); }
	// Abilities and ults: each ability equipped and set off, then each flora's ult.
	else if (At(45.2f)) { if (UpdateAutoAbilityTest(P, DeltaTime)) Next(45.3f); }
	// Menus by controller alone: injected DualSense buttons and sticks (and Esc / Tab / I).
	else if (At(45.3f)) { if (UpdateAutoMenuTest(P, DeltaTime)) Next(bAutoTestMenusOnly ? 45.8f : 45.4f); }
	// Body animations: a row of mannequins in each state, then your body crouching and sliding.
	else if (At(45.4f)) { if (UpdateAutoBodyTest(P, DeltaTime)) Next(bAutoTestBodyOnly ? 45.8f : 45.5f); }
	// Flora grafts and frame part colors: loadouts in the character preview, then close up.
	else if (At(45.5f)) { if (UpdateAutoGraftTest(P, DeltaTime)) Next(bAutoTestGraftsOnly ? 45.8f : 45.6f); }
	// Map look: the level from fixed cameras (spawn, overviews, platforms, halls).
	else if (At(45.6f)) { if (UpdateAutoMapTest(P, DeltaTime)) Next(bAutoTestMapsOnly ? 45.8f : 45.7f); }
	// Weapon mods (draft): stat changes, saved configs and quick swap, perks firing, the menu.
	else if (At(45.7f)) { if (UpdateAutoModTest(P, DeltaTime)) Next(45.8f); }
	else if (At(45.8f)) { P->Health->GodMode = 0.f; UE_LOG(LogWWO, Display, TEXT("AutoTest done")); WWOTest::Exit(); }
}

bool AWWOPlayerController::PlaceForAutoTest(AWWOCharacter* P, AActor* Target, float DistanceM, float YawOffset, bool bMove)
{
	// Stand DistanceM from the target on the clearest side, facing it (turned YawOffset
	// degrees off it): flat ground all the way and nothing in between at chest or eye height.
	// Returns false if no side is clear. bMove false only checks.
	const IWWOTargetable* T = Cast<IWWOTargetable>(Target);
	if (!T) return false;
	const FVector Feet = T->FeetLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOAutoTest), false, P);
	Params.AddIgnoredActor(Target);
	FCollisionObjectQueryParams Static(ECC_WorldStatic);
	for (int32 I = 0; I < 16; ++I)
	{
		const FVector Dir = FRotator(0.f, 180.f + I * 22.5f, 0.f).Vector();
		const FVector Stand = Feet + Dir * DistanceM * WWO::M;
		FHitResult Hit;
		bool bClear = true;
		for (const float Up : { 100.f, 175.f })
		{
			bClear &= !GetWorld()->SweepSingleByObjectType(Hit, Feet + Dir * 50.f + FVector(0, 0, Up), Stand + Dir * 60.f + FVector(0, 0, Up), FQuat::Identity,
				Static, FCollisionShape::MakeSphere(35.f), Params);
		}
		for (float D = 1.f; bClear && D <= DistanceM + 0.01f; D += 1.f)
		{
			const FVector At = Feet + Dir * D * WWO::M;
			bClear = GetWorld()->LineTraceSingleByObjectType(Hit, At + FVector(0, 0, 60.f), At - FVector(0, 0, 60.f), Static, Params)
				&& FMath::Abs(Hit.ImpactPoint.Z - Feet.Z) < 15.f;
		}
		if (!bClear) continue;
		if (!bMove) return true;
		GetWorld()->LineTraceSingleByObjectType(Hit, Stand + FVector(0, 0, 60.f), Stand - FVector(0, 0, 60.f), Static, Params);
		if (!P->TeleportTo(Hit.ImpactPoint + FVector(0, 0, P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f), FRotator(0.f, (-Dir).Rotation().Yaw, 0.f))) continue;
		P->GetCharacterMovement()->Velocity = FVector::ZeroVector;
		P->SetViewRotation(0.f, (-Dir).Rotation().Yaw + YawOffset);
		return true;
	}
	return false;
}

bool AWWOPlayerController::UpdateAutoGrenadeTest(AWWOCharacter* P, float DeltaTime)
{
	auto bShortThrow = [](const UWWOGrenadeData* G) { return G->Kind == EWWOGrenadeKind::Spike || G->Kind == EWWOGrenadeKind::Tripmine; };
	UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	if (!Loadout) return true;
	P->Health->GodMode = 1.f; // the waves keep running
	if (P->Health->bDead) P->Health->Respawn();
	if (AutoGrenadeIndex < 0)
	{
		// The nearest standing (not strafing) training target, and every grenade, the wall last
		// (it stays up for 10 s and would block the rest).
		AutoTarget = nullptr;
		float BestD = 1e9f;
		for (TActorIterator<AWWOTarget> It(GetWorld()); It; ++It)
		{
			const float D = FVector::Dist(It->GetActorLocation(), P->GetActorLocation());
			if (It->StrafeDistance <= 0.f && D < BestD && PlaceForAutoTest(P, *It, 17.f, 0.f, false)) { BestD = D; AutoTarget = *It; }
		}
		AutoGrenadeBefore = Loadout->GetGrenade();
		AutoGrenades.Reset();
		for (UWWOGrenadeData* G : Loadout->Grenades) if (G && G->Kind != EWWOGrenadeKind::Wall) AutoGrenades.Add(G);
		for (UWWOGrenadeData* G : Loadout->Grenades) if (G && G->Kind == EWWOGrenadeKind::Wall) AutoGrenades.Add(G);
		UE_LOG(LogWWO, Display, TEXT("AutoTest grenade: %d grenades at %s; throw distance %.1f m, fuse %.2f s, release %.2f s, recharge %.1f s"),
			AutoGrenades.Num(), AutoTarget.IsValid() ? *AutoTarget->GetName() : TEXT("no target"), P->Grenades->EffectiveThrowDistance(),
			P->Grenades->DetonationTime, P->Weapon->ThrowRelease, P->Grenades->EffectiveRechargeTime());
		if (!AutoTarget.IsValid()) return true;
		AutoGrenadeIndex = 0;
		AutoPhase = -1.f; // set up the first throw
	}
	if (AutoGrenadeIndex >= AutoGrenades.Num())
	{
		Loadout->EquipGrenade(AutoGrenadeBefore);
		return true;
	}
	UWWOGrenadeData* G = AutoGrenades[AutoGrenadeIndex];
	const FString Id = G->Id.ToString();
	auto ShotNamed = [this, &Id](const TCHAR* What) { Shot(*FString::Printf(TEXT("19_grenade_%s_%s"), *Id, What)); };
	if (AutoPhase < -0.5f)
	{
		// The Blinding Grenade and the Snare Mine go at the nearest enemy (they blind and slow
		// enemies); the rest at the training target. Stand 17 m (or 6 m) away, and throw a few
		// frames later (a screenshot's long frame would otherwise release it at once and fly it
		// 7 m in one step).
		AutoGrenadeTarget = AutoTarget;
		if (G->Kind == EWWOGrenadeKind::Blind || G->Kind == EWWOGrenadeKind::Tripmine)
		{
			float BestD = 1e9f;
			for (AWWOEnemy* E : UWWOWorldSubsystem::Get(this)->LivingEnemies())
			{
				const float D = FVector::Dist(E->GetActorLocation(), P->GetActorLocation());
				if (D < BestD && E->GetCharacterMovement()->IsMovingOnGround() && PlaceForAutoTest(P, E, bShortThrow(G) ? 6.f : 17.f, 0.f, false))
				{
					BestD = D;
					AutoGrenadeTarget = E;
				}
			}
		}
		const IWWOTargetable* T = Cast<IWWOTargetable>(AutoGrenadeTarget.Get());
		if (!T) return true;
		if (T->IsDead()) return false; // wait for the training target to respawn
		Loadout->EquipGrenade(G);
		bAutoLongThrow = !bShortThrow(G) && PlaceForAutoTest(P, AutoGrenadeTarget.Get(), 17.f, 0.f);
		if (!bAutoLongThrow) PlaceForAutoTest(P, AutoGrenadeTarget.Get(), 6.f, 0.f);
		AutoPhase = -0.5f;
		return false;
	}
	IWWOTargetable* Target = Cast<IWWOTargetable>(AutoGrenadeTarget.Get());
	if (!Target || (AutoPhase < 0.f && Target->IsDead()))
	{
		AutoPhase = -1.f; // pick again
		return false;
	}
	if (AutoPhase < 0.f)
	{
		AutoPhase += DeltaTime;
		if (AutoPhase < 0.f) return false;
		// Fuse grenades: from 17 m, looking level (a level throw lands about 18 m out, right as
		// the 1 s fuse runs out). The Poison Spike from 6 m at the target's body (it homes in
		// when it passes close); the Snare Mine from 6 m at the ground 1.5 m short of it.
		const bool bPlaced = FVector2D(P->GetActorLocation() - Target->FeetLocation()).Size() > 5.f * WWO::M;
		const FVector Aim = G->Kind == EWWOGrenadeKind::Spike ? Target->GetAimParts().Last().Position
			: Target->FeetLocation() + (P->GetActorLocation() - Target->FeetLocation()).GetSafeNormal2D() * 150.f;
		const FVector Dir = (Aim - P->EyeLocation()).GetSafeNormal();
		P->SetViewRotation(!bAutoLongThrow ? FMath::Asin(Dir.Z) : 0.f, Dir.Rotation().Yaw);
		P->Grenades->Charge = P->Grenades->MaxCharges();
		AutoTargetHealth = Target->GetHealth();
		const bool bThrown = P->Grenades->ThrowGrenade();
		UE_LOG(LogWWO, Display, TEXT("AutoTest grenade: %s (%s, %s) at %s thrown %d from %.1f m (placed %d): damage %.0f, radius %.1f m, bounce %.2f, fuse %.2f s"), *Id,
			*UEnum::GetValueAsString(G->Kind), *G->Flora.ToString(), *AutoGrenadeTarget->GetName(), bThrown, FVector2D(P->GetActorLocation() - Target->FeetLocation()).Size() / WWO::M, bPlaced,
			G->Damage, G->BlastRadius, G->Bounce, P->Grenades->EffectiveFuse(G));
		AutoPhase = 0.f;
		AutoGoneAt = -1.f;
		bAutoGrenadeOut = bAutoShotBlast = bAutoShotArmed = false;
		return false;
	}
	AutoPhase += DeltaTime;
	// The throwing arm (one moment per throw: a screenshot's long frame would skip the next).
	const float ArmShotAt = AutoGrenadeIndex == 0 ? 0.17f : AutoGrenadeIndex == 1 ? 0.2f : AutoGrenadeIndex == 2 ? 0.235f : -1.f;
	if (ArmShotAt > 0.f && AutoPhase >= ArmShotAt && AutoPhase - DeltaTime < ArmShotAt)
	{
		Shot(*FString::Printf(TEXT("19_throw_arm_%03d"), FMath::RoundToInt(P->Weapon->GetThrowClock() * 1000.f)));
	}
	bool bOut = false;
	for (TActorIterator<AWWOGrenade> It(GetWorld()); It; ++It) bOut |= It->Spec.Data == G;
	bAutoGrenadeOut |= bOut;
	if (bAutoGrenadeOut && !bOut && AutoGoneAt < 0.f) AutoGoneAt = AutoPhase;
	if (G->Kind == EWWOGrenadeKind::Tripmine && bOut && !bAutoShotArmed && AutoPhase > 1.f) { bAutoShotArmed = true; ShotNamed(TEXT("armed")); }
	if (AutoGoneAt >= 0.f && !bAutoShotBlast && AutoPhase >= AutoGoneAt + 0.06f) { bAutoShotBlast = true; ShotNamed(TEXT("blast")); }
	const float AfterFor = G->Kind == EWWOGrenadeKind::Pool ? 1.5f : G->Kind == EWWOGrenadeKind::Wall ? 0.8f : 1.f;
	const bool bTimedOut = AutoPhase > 9.f;
	if ((AutoGoneAt >= 0.f && AutoPhase >= AutoGoneAt + AfterFor) || bTimedOut)
	{
		ShotNamed(TEXT("after"));
		UE_LOG(LogWWO, Display, TEXT("AutoTest grenade: %s went off %.2f s after the throw%s; target health %.0f -> %.0f; charge now %.2f"), *Id, AutoGoneAt,
			bTimedOut ? TEXT(" (TIMED OUT)") : TEXT(""), AutoTargetHealth, Target->GetHealth(), P->Grenades->Charge);
		for (TActorIterator<AWWOGrenade> It(GetWorld()); It; ++It) if (It->Spec.Data == G) It->Destroy(); // (a timed-out one)
		AutoGrenadeIndex++;
		AutoPhase = -1.f;
	}
	return false;
}

bool AWWOPlayerController::UpdateAutoMeleeTest(AWWOCharacter* P, float DeltaTime)
{
	// 0: lunge onto the training target from 3.2 m, starting 8 degrees off it. 1: punch the
	// nearest enemy from 2.5 m and log its knockback. 2: a punch at nothing (the arm winding up).
	P->Health->GodMode = 1.f;
	if (P->Health->bDead) P->Health->Respawn();
	if (AutoMeleeIndex < 0) { AutoMeleeIndex = 0; AutoPhase = -1.f; }
	if (AutoMeleeIndex > 2) return true;
	if (AutoMeleeIndex == 2)
	{
		if (AutoPhase < 0.f)
		{
			P->SetViewRotation(FMath::DegreesToRadians(60.f), P->GetActorRotation().Yaw); // at the sky: nothing to lunge at
			JustPressed[(int32)EWWOAction::Melee] = true;
			AutoPhase = 0.f;
			return false;
		}
		AutoPhase += DeltaTime;
		if (!bAutoShotPunch && P->Weapon->bMeleeing && P->Weapon->MeleeProgress >= 0.33f)
		{
			bAutoShotPunch = true;
			Shot(*FString::Printf(TEXT("22_melee_swing_%02d"), FMath::RoundToInt(P->Weapon->MeleeProgress * 100.f)));
		}
		if ((!P->Weapon->bMeleeing && AutoPhase > 0.2f) || AutoPhase > 3.f) AutoMeleeIndex++;
		return false;
	}
	AActor* TargetActor = AutoTarget.Get();
	if (AutoMeleeIndex == 1)
	{
		if (AutoPhase < 0.f)
		{
			AWWOEnemy* Best = nullptr;
			float BestD = 1e9f;
			for (AWWOEnemy* E : UWWOWorldSubsystem::Get(this)->LivingEnemies())
			{
				const float D = FVector::Dist(E->GetActorLocation(), P->GetActorLocation());
				if (D < BestD && E->GetCharacterMovement()->IsMovingOnGround()) { BestD = D; Best = E; }
			}
			AutoTarget = Best;
			TargetActor = Best;
		}
	}
	IWWOTargetable* Target = Cast<IWWOTargetable>(TargetActor);
	if (!Target)
	{
		UE_LOG(LogWWO, Display, TEXT("AutoTest melee: step %d has no target"), AutoMeleeIndex);
		AutoMeleeIndex++;
		AutoPhase = -1.f;
		return false;
	}
	if (AutoPhase < 0.f && Target->IsDead()) return false; // wait for it to respawn
	if (AutoPhase < 0.f)
	{
		const bool bPlaced = PlaceForAutoTest(P, TargetActor, AutoMeleeIndex == 0 ? 3.2f : 2.5f, AutoMeleeIndex == 0 ? 8.f : 0.f);
		AutoMeleeFrom = P->GetActorLocation();
		AutoTargetHealth = Target->GetHealth();
		AutoLungeTime = 0.f;
		bAutoSawLunge = bAutoMeleeLogged = bAutoShotPunch = false;
		JustPressed[(int32)EWWOAction::Melee] = true;
		UE_LOG(LogWWO, Display, TEXT("AutoTest melee: at %s from %.2f m (placed %d); reach %.1f, lunge range %.1f angle %.0f speed %.1f, damage %.0f, knockback %.1f"),
			*TargetActor->GetName(), FVector2D(P->GetActorLocation() - Target->FeetLocation()).Size() / WWO::M, bPlaced, P->Weapon->MeleeReach,
			P->Weapon->LungeRange, P->Weapon->LungeAngle, P->Weapon->LungeSpeed, P->Weapon->MeleeDamage, P->Weapon->MeleeKnockback);
		AutoPhase = 0.f;
		return false;
	}
	AutoPhase += DeltaTime;
	if (P->IsLunging()) { bAutoSawLunge = true; AutoLungeTime += GetWorld()->GetDeltaSeconds(); }
	const bool bLanded = P->Weapon->bMeleeing && P->Weapon->MeleeProgress >= 0.4f;
	if (bLanded && !bAutoMeleeLogged)
	{
		bAutoMeleeLogged = true;
		const FVector ToHead = Target->GetAimParts()[0].Position - P->EyeLocation();
		const AWWOEnemy* Enemy = Cast<AWWOEnemy>(TargetActor);
		UE_LOG(LogWWO, Display, TEXT("AutoTest melee: landed at progress %.2f; lunged %d for %.3f game s, moved %.2f m, now %.2f m from it (flat); view %.1f deg off its head; health %.0f -> %.0f; time dilation %.2f%s"),
			P->Weapon->MeleeProgress, bAutoSawLunge, AutoLungeTime, FVector::Dist(AutoMeleeFrom, P->GetActorLocation()) / WWO::M,
			FVector2D(P->GetActorLocation() - Target->FeetLocation()).Size() / WWO::M,
			FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(ToHead.GetSafeNormal(), P->AimForward()), -1.f, 1.f))),
			AutoTargetHealth, Target->GetHealth(), GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation(),
			Enemy ? *FString::Printf(TEXT("; enemy pushed at %.2f m/s"), FVector2D(Enemy->GetVelocity()).Size() / WWO::M) : TEXT(""));
	}
	// The punching arm: at the hit on the lunge, winding up on the enemy.
	if (bAutoMeleeLogged && !bAutoShotPunch && P->Weapon->bMeleeing && P->Weapon->MeleeProgress >= 0.42f)
	{
		bAutoShotPunch = true;
		Shot(*FString::Printf(TEXT("%s_%02d"), AutoMeleeIndex == 0 ? TEXT("20_melee_lunge") : TEXT("21_melee_enemy"), FMath::RoundToInt(P->Weapon->MeleeProgress * 100.f)));
	}
	if ((!P->Weapon->bMeleeing && AutoPhase > 0.2f) || AutoPhase > 3.f)
	{
		if (!bAutoMeleeLogged) UE_LOG(LogWWO, Display, TEXT("AutoTest melee: step %d never landed (lunged %d)"), AutoMeleeIndex, bAutoSawLunge);
		AutoMeleeIndex++;
		AutoPhase = -1.f;
		bAutoShotPunch = false;
	}
	return false;
}
