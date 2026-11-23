#include "Player/WWOPlayerController.h"
#include "WWOSwitches.h"
#include "Player/WWOAutoTest.h"
#include "Player/WWOProf.h"
#include "UI/WWOFiringTester.h"
#include "Game/WWOGameMode.h"
#include "Player/WWOTestRun.h"
#include "Systems/WWOSound.h"
#include "Game/WWOPlayerState.h"
#include "Game/WWOGameMode.h"
#include "Net/WWONetSubsystem.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOHealthComponent.h"
#include "UI/WWOHUDWidget.h"
#include "UI/WWOSettingsMenu.h"
#include "UI/WWOCharacterMenu.h"
#include "UI/WWOLaunchMenu.h"
#include "UI/WWOScoreboard.h"
#include "UI/WWOFootRaceLobby.h"
#include "Game/WWOFootRaceRules.h"
#include "Game/WWOGameState.h"
#include "UI/WWOMenuCursor.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Misc/Paths.h"
#include "Systems/WWOConfig.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/SViewport.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "WhatWasOurs.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"

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

void AWWOPlayerController::ServerLobbyTeam_Implementation(int32 Team)
{
	AWWOGameMode* GM = GetWorld()->GetAuthGameMode<AWWOGameMode>();
	if (FWWOFootRaceRules* F = GM ? GM->FootRace() : nullptr) F->SetLobbyTeam(this, Team);
}

void AWWOPlayerController::LobbyChooseTeam(int32 Team)
{
	if (HasAuthority()) ServerLobbyTeam_Implementation(Team);
	else ServerLobbyTeam(Team);
}

void AWWOPlayerController::LobbyBots(int32 Team, int32 Count)
{
	AWWOGameMode* GM = GetWorld()->GetAuthGameMode<AWWOGameMode>(); // the host's only (a joiner has none)
	if (FWWOFootRaceRules* F = GM ? GM->FootRace() : nullptr) F->SetLobbyBots(Team, Count);
}

bool AWWOPlayerController::LobbyStart()
{
	AWWOGameMode* GM = GetWorld()->GetAuthGameMode<AWWOGameMode>();
	FWWOFootRaceRules* F = GM ? GM->FootRace() : nullptr;
	return F && F->StartRace(this);
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
	AutoTest = FWWOAutoTest::MakeIfAsked(*this); // a scripted test run, when the command line asks for one

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
	FString BindsPath() { return WWOConfig::SavedPath(TEXT("Keybinds.json")); }
	// Test runs press the default keys: they neither read nor write anyone's saved bindings.
	bool BindsTestRun() { return WWOSwitches::UsesDefaultBinds(); }
}

void AWWOPlayerController::SaveBinds() const
{
	if (BindsTestRun()) return;
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	for (const auto& Pair : Binds) Json->SetStringField(Pair.Key.ToString(), Pair.Value.GetFName().ToString());
	WWOConfig::Save(BindsPath(), Json);
}

void AWWOPlayerController::LoadBinds()
{
	Binds.Reset();
	if (BindsTestRun()) return;
	// Your saved bindings, or with none saved yet the ones the build shipped with (Config/ShippedKeybinds.json).
	const TSharedPtr<FJsonObject> Json = WWOConfig::LoadSavedElseShipped(BindsPath(), TEXT("ShippedKeybinds.json"));
	if (!Json.IsValid()) return;
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
	if (*Id == EWWOAction::Fire)
	{
		// The engine's own press: the trigger reading starts from here, so it doesn't count this pull again.
		FireStartedAt = FPlatformTime::Seconds();
		bTriggerDown = true;
		TriggerHigh = 0.f;
	}
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
	return WWOSwitches::IsStress();
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

void AWWOPlayerController::UpdateCameraManager(float DeltaSeconds)
{
	Super::UpdateCameraManager(DeltaSeconds);
	// Your marked targets (health bars, the outline flash) are worked out here, once every actor has
	// ticked and the camera has its view for the frame: the view the HUD then draws. Game code, so it
	// runs whether or not the HUD is showing.
	AWWOCharacter* Me = IsLocalController() ? Cast<AWWOCharacter>(GetPawn()) : nullptr;
	if (Me && Me->Weapon) Me->Weapon->UpdateMarks(this);
}

void AWWOPlayerController::PlayerTick(float DeltaTime)
{
	WWO_PROF("PlayerController.PlayerTick");
	FMemory::Memzero(JustPressed, sizeof(JustPressed));
	FMemory::Memcpy(ScriptNow, ScriptQueued, sizeof(ScriptNow)); // a scripted press: readable for this frame
	FMemory::Memzero(ScriptQueued, sizeof(ScriptQueued));
	Super::PlayerTick(DeltaTime); // processes input: fills Held / JustPressed for this frame
	UpdateTrigger();
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
	WWO_PROF("PlayerController");
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
		UpdateScoreboard();
		UpdateLobby();
	}
	if (AutoTest) AutoTest->Tick(FApp::GetDeltaTime());
}

bool AWWOPlayerController::StepTrigger(float V, float Reset, bool& bDown, float& Low, float& High)
{
	if (bDown)
	{
		High = FMath::Max(High, V);
		if (V <= High - Reset || V < 0.05f)
		{
			bDown = false;
			Low = V;
		}
		return false;
	}
	Low = FMath::Min(Low, V);
	if (V < Low + Reset || V <= 0.1f) return false;
	bDown = true;
	High = V;
	return true;
}

void AWWOPlayerController::UpdateTrigger()
{
	// The engine only reports a trigger as let go when it is nearly all the way out, so a quick
	// second pull that starts from half way never registers. Read as an axis instead, the trigger
	// re-arms as soon as it has come back up a little (AWWOCharacter::TriggerReset), and pulling it
	// in again from there is a fresh press of Fire.
	const FKey Bound = GetBind("fire", true);
	const FKey Axis = Bound == EKeys::Gamepad_RightTrigger ? EKeys::Gamepad_RightTriggerAxis
		: Bound == EKeys::Gamepad_LeftTrigger ? EKeys::Gamepad_LeftTriggerAxis : FKey();
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetPawn());
	if (!Axis.IsValid() || !P)
	{
		bTriggerDown = false;
		TriggerLow = TriggerHigh = 0.f;
		return;
	}
	const int32 Fire = (int32)EWWOAction::Fire;
	if (!StepTrigger(GetInputAnalogKeyState(Axis), FMath::Clamp(P->TriggerReset, 0.02f, 0.9f), bTriggerDown, TriggerLow, TriggerHigh)) return;
	// Only when the engine still has it down (its own press won't come) and didn't just press it.
	if (Held[Fire] && !BlockedUntilReleased[Fire] && FPlatformTime::Seconds() - FireStartedAt > 0.1) JustPressed[Fire] = true;
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
	// (The scoreboard takes the cursor but doesn't pause: a host's pause would freeze everyone.)
	return IsOtherMenuOpen() || (FiringTester && FiringTester->WantsCursor()) || (Scoreboard && Scoreboard->IsOpen()) || (Lobby && Lobby->IsOpen());
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
	if (Lobby && Lobby->IsOpen()) return; // the lobby: nothing opens over it (its buttons are all there is)
	if (Scoreboard && Scoreboard->IsOpen())
	{
		// The scoreboard: Circle / Esc steps back from its map list; nothing else opens over it.
		if (Id == EWWOAction::MenuBack || Id == EWWOAction::Menu) Scoreboard->Back();
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

void AWWOPlayerController::UpdateScoreboard()
{
	// The end of a PvP match (AWWOGameState::bMatchOver, replicated): the scoreboard over everything,
	// the other menus shut. Not in the scripted test runs, which restart by themselves.
	const AWWOGameState* GS = GetWorld()->GetGameState<AWWOGameState>();
	const bool bScripted = WWOSwitches::HasAutoTestText();
	const bool bOver = GS && GS->bMatchOver && !bScripted;
	if (bOver && !(Scoreboard && Scoreboard->IsOpen()))
	{
		if (!Scoreboard)
		{
			Scoreboard = CreateWidget<UWWOScoreboard>(this, UWWOScoreboard::StaticClass());
			Scoreboard->AddToViewport(12); // over the HUD and the other menus
		}
		if (SettingsMenu && SettingsMenu->IsOpen()) SettingsMenu->Close();
		if (CharacterMenu && CharacterMenu->IsOpen()) CharacterMenu->Close();
		Scoreboard->Open();
		SetMenuMode(true);
	}
	else if (!bOver && Scoreboard && Scoreboard->IsOpen())
	{
		Scoreboard->Close();
		SetMenuMode(IsMenuOpen());
	}
	// -WWOTestScoreboard: pictures of the page and of its map list, then NEXT MATCH is pressed.
	static const bool bTest = WWOSwitches::Has(TEXT("WWOTestScoreboard"));
	if (bTest && Scoreboard && Scoreboard->IsOpen())
	{
		static int32 Step = 0;
		const double Open = FPlatformTime::Seconds() - Scoreboard->OpenedAt;
		if (Step == 0 && Open > 2.0) { WWOTest::Shot(TEXT("45_scoreboard")); Step = 1; }
		else if (Step == 1 && Open > 3.0) { Scoreboard->ShowMaps(); Step = 2; }
		else if (Step == 2 && Open > 4.0) { WWOTest::Shot(TEXT("45_scoreboard_maps")); Step = 3; }
		else if (Step == 3 && Open > 5.0) { Scoreboard->Back(); Scoreboard->NextMatch(); Step = 4; }
	}
}

void AWWOPlayerController::UpdateLobby()
{
	// FootRace's lobby (AWWOGameState::bLobby, replicated): open on every machine until the host starts the race.
	const AWWOGameState* GS = GetWorld()->GetGameState<AWWOGameState>();
	const bool bOn = GS && GS->bLobby;
	if (bOn && !(Lobby && Lobby->IsOpen()))
	{
		if (!Lobby)
		{
			Lobby = CreateWidget<UWWOFootRaceLobby>(this, UWWOFootRaceLobby::StaticClass());
			Lobby->AddToViewport(11); // over the HUD, under the scoreboard
		}
		if (SettingsMenu && SettingsMenu->IsOpen()) SettingsMenu->Close();
		if (CharacterMenu && CharacterMenu->IsOpen()) CharacterMenu->Close();
		Lobby->Open();
		SetMenuMode(true);
	}
	else if (!bOn && Lobby && Lobby->IsOpen())
	{
		Lobby->Close();
		SetMenuMode(IsMenuOpen());
	}
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
