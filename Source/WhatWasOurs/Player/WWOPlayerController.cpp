#include "Player/WWOPlayerController.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOWeaponComponent.h"
#include "UI/WWOHUDWidget.h"
#include "UI/WWOSettingsMenu.h"
#include "UI/WWOCharacterMenu.h"
#include "UI/WWOMenuCursor.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "UnrealClient.h"
#include "Player/WWOGrenadeComponent.h"
#include "Player/WWOUltComponent.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "WhatWasOurs.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"

namespace
{
	constexpr float ChordWindow = 0.1f; // seconds to wait for the second bumper
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
	HUDWidget->AddToViewport(0);
	SettingsMenu = CreateWidget<UWWOSettingsMenu>(this, UWWOSettingsMenu::StaticClass());
	SettingsMenu->AddToViewport(10);
	SettingsMenu->SetVisibility(ESlateVisibility::Collapsed);
	CharacterMenu = CreateWidget<UWWOCharacterMenu>(this, UWWOCharacterMenu::StaticClass());
	CharacterMenu->AddToViewport(11);
	CharacterMenu->SetVisibility(ESlateVisibility::Collapsed);

	SetMenuMode(false);
	bAutoTest = FParse::Param(FCommandLine::Get(), TEXT("WWOAutoTest"));
}

void AWWOPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (MenuCursor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(MenuCursor);
	}
	MenuCursor.Reset();
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

void AWWOPlayerController::BuildInput()
{
	// The controls from the Godot handoff (keyboard/mouse and DualSense).
	GameContext = NewObject<UInputMappingContext>(this);
	auto Map = [this](UInputAction* Action, std::initializer_list<FKey> Keys)
	{
		for (const FKey& Key : Keys) GameContext->MapKey(Action, Key);
	};

	Map(MakeAction(EWWOAction::Jump), { EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom });
	Map(MakeAction(EWWOAction::Sprint), { EKeys::LeftShift, EKeys::Gamepad_LeftThumbstick });
	Map(MakeAction(EWWOAction::Crouch), { EKeys::C, EKeys::LeftControl, EKeys::Gamepad_FaceButton_Right });
	Map(MakeAction(EWWOAction::Aim), { EKeys::RightMouseButton, EKeys::Gamepad_LeftTrigger });
	Map(MakeAction(EWWOAction::Fire), { EKeys::LeftMouseButton, EKeys::Gamepad_RightTrigger });
	Map(MakeAction(EWWOAction::Reload), { EKeys::R, EKeys::Gamepad_FaceButton_Left });
	Map(MakeAction(EWWOAction::Melee), { EKeys::V });
	Map(MakeAction(EWWOAction::MeleePad), { EKeys::Gamepad_RightShoulder });
	Map(MakeAction(EWWOAction::Grenade), { EKeys::G });
	Map(MakeAction(EWWOAction::GrenadePad), { EKeys::Gamepad_LeftShoulder });
	Map(MakeAction(EWWOAction::Ult), { EKeys::F });
	Map(MakeAction(EWWOAction::Ability1), { EKeys::One, EKeys::Gamepad_DPad_Left });
	Map(MakeAction(EWWOAction::Ability2), { EKeys::Two, EKeys::Gamepad_DPad_Right });
	Map(MakeAction(EWWOAction::WeaponNext), { EKeys::MouseScrollUp, EKeys::Gamepad_FaceButton_Top });
	Map(MakeAction(EWWOAction::WeaponPrev), { EKeys::MouseScrollDown });
	// Menu keys work while paused (the menus pause the game).
	Map(MakeAction(EWWOAction::Menu, true), { EKeys::Tab, EKeys::Escape, EKeys::Gamepad_Special_Right });
	Map(MakeAction(EWWOAction::MenuBack, true), { EKeys::Escape, EKeys::Gamepad_FaceButton_Right });
	Map(MakeAction(EWWOAction::TabNext, true), { EKeys::E, EKeys::Gamepad_RightShoulder });
	Map(MakeAction(EWWOAction::TabPrev, true), { EKeys::Q, EKeys::Gamepad_LeftShoulder });
	Map(MakeAction(EWWOAction::CharacterMenu, true), { EKeys::I, EKeys::Gamepad_Special_Left });

	// Movement: WASD and the left stick (dead zone 0.2).
	MoveAction = NewObject<UInputAction>(this);
	MoveAction->ValueType = EInputActionValueType::Axis2D;
	{
		UInputModifierSwizzleAxis* ToY = NewObject<UInputModifierSwizzleAxis>(this); // X -> Y
		UInputModifierNegate* Negate = NewObject<UInputModifierNegate>(this);
		GameContext->MapKey(MoveAction, EKeys::W).Modifiers.Add(ToY);
		FEnhancedActionKeyMapping& S = GameContext->MapKey(MoveAction, EKeys::S);
		S.Modifiers.Add(ToY);
		S.Modifiers.Add(Negate);
		GameContext->MapKey(MoveAction, EKeys::A).Modifiers.Add(Negate);
		GameContext->MapKey(MoveAction, EKeys::D);
		UInputModifierDeadZone* Dead = NewObject<UInputModifierDeadZone>(this);
		Dead->LowerThreshold = 0.2f;
		Dead->Type = EDeadZoneType::Radial;
		GameContext->MapKey(MoveAction, EKeys::Gamepad_Left2D).Modifiers.Add(Dead);
	}
	// Looking: right stick (dead zone 0.15) and the mouse.
	LookAction = NewObject<UInputAction>(this);
	LookAction->ValueType = EInputActionValueType::Axis2D;
	{
		UInputModifierDeadZone* Dead = NewObject<UInputModifierDeadZone>(this);
		Dead->LowerThreshold = 0.15f;
		Dead->Type = EDeadZoneType::Radial;
		// The stick's Y came out inverted compared to the Godot build: flip it.
		UInputModifierNegate* FlipY = NewObject<UInputModifierNegate>(this);
		FlipY->bX = false;
		FlipY->bZ = false;
		FEnhancedActionKeyMapping& Stick = GameContext->MapKey(LookAction, EKeys::Gamepad_Right2D);
		Stick.Modifiers.Add(Dead);
		Stick.Modifiers.Add(FlipY);
	}
	MouseLookAction = NewObject<UInputAction>(this);
	MouseLookAction->ValueType = EInputActionValueType::Axis2D;
	GameContext->MapKey(MouseLookAction, EKeys::Mouse2D);
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
	Held[(int32)*Id] = true;
	JustPressed[(int32)*Id] = true;
	if (*Id == EWWOAction::GrenadePad && !IsPaused()) ChordPending.Add(EWWOChord::Grenade, 0.f);
	if (*Id == EWWOAction::MeleePad && !IsPaused()) ChordPending.Add(EWWOChord::Melee, 0.f);
	HandleMenuPress(*Id);
}

void AWWOPlayerController::OnActionCompleted(const FInputActionInstance& Instance)
{
	if (const EWWOAction* Id = ActionIds.Find(Instance.GetSourceAction()))
	{
		Held[(int32)*Id] = false;
	}
}

void AWWOPlayerController::OnMove(const FInputActionInstance& Instance)
{
	MoveValue = Instance.GetValue().Get<FVector2D>();
}

void AWWOPlayerController::OnLook(const FInputActionInstance& Instance)
{
	LookValue = Instance.GetValue().Get<FVector2D>();
}

void AWWOPlayerController::OnMouseLook(const FInputActionInstance& Instance)
{
	if (IsMenuOpen() || IsPaused()) return;
	AWWOCharacter* Player = GetWWOCharacter();
	if (!Player || Player->bUlting) return;
	const FVector2D Delta = Instance.GetValue().Get<FVector2D>();
	// The cursor jumps when the window first captures it: ignore the first moments of play.
	if (GetWorld()->GetRealTimeSeconds() < 2.0 || Delta.Size() > 250.f) return;
	Player->Look(Delta * Player->MouseSensitivity * 0.001f);
}

FVector2D AWWOPlayerController::GetMoveAxis() const
{
	if (IsMenuOpen()) return FVector2D::ZeroVector;
	return MoveValue.GetClampedToMaxSize(1.f);
}

FVector2D AWWOPlayerController::GetLookAxis() const
{
	if (IsMenuOpen()) return FVector2D::ZeroVector;
	return LookValue.GetClampedToMaxSize(1.f);
}

// --- Each frame ---------------------------------------------------------------------------

void AWWOPlayerController::PlayerTick(float DeltaTime)
{
	FMemory::Memzero(JustPressed, sizeof(JustPressed));
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
	if (bAutoTest) UpdateAutoTest(FApp::GetDeltaTime());
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
				EKeys::Gamepad_RightTrigger, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Right, EKeys::Gamepad_LeftThumbstick,
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
	HitstopLeft = Seconds;
	UGameplayStatics::SetGlobalTimeDilation(this, 0.03f);
}

// --- Menus -------------------------------------------------------------------------------------

bool AWWOPlayerController::IsMenuOpen() const
{
	return (SettingsMenu && SettingsMenu->IsOpen()) || (CharacterMenu && CharacterMenu->IsOpen());
}

void AWWOPlayerController::HandleMenuPress(EWWOAction Id)
{
	if (!SettingsMenu || !CharacterMenu) return;
	if (CharacterMenu->IsOpen())
	{
		if (Id == EWWOAction::CharacterMenu) ToggleCharacterMenu();
		else if (Id == EWWOAction::MenuBack || Id == EWWOAction::Menu) CharacterMenu->Back();
		return;
	}
	if (SettingsMenu->IsOpen())
	{
		if (Id == EWWOAction::MenuBack || Id == EWWOAction::Menu) SettingsMenu->Back();
		else if (Id == EWWOAction::TabNext) SettingsMenu->CycleGlobalTab(1);
		else if (Id == EWWOAction::TabPrev) SettingsMenu->CycleGlobalTab(-1);
		return;
	}
	if (Id == EWWOAction::Menu) ToggleSettingsMenu();
	else if (Id == EWWOAction::CharacterMenu) ToggleCharacterMenu();
}

void AWWOPlayerController::ToggleSettingsMenu()
{
	const bool bOpen = !SettingsMenu->IsOpen();
	if (bOpen) SettingsMenu->Open();
	else SettingsMenu->Close();
	SetMenuMode(bOpen);
}

void AWWOPlayerController::ToggleCharacterMenu()
{
	const bool bOpen = !CharacterMenu->IsOpen();
	if (bOpen) CharacterMenu->Open();
	else CharacterMenu->Close();
	SetMenuMode(bOpen);
}

void AWWOPlayerController::CloseMenus()
{
	if (SettingsMenu->IsOpen()) SettingsMenu->Close();
	if (CharacterMenu->IsOpen()) CharacterMenu->Close();
	SetMenuMode(false);
}

void AWWOPlayerController::OnMenuClosed()
{
	if (!IsMenuOpen()) SetMenuMode(false);
}

void AWWOPlayerController::SetMenuMode(bool bOpen)
{
	SetPause(bOpen);
	bShowMouseCursor = bOpen;
	if (bOpen)
	{
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
		if (!MenuCursor.IsValid() && FSlateApplication::IsInitialized())
		{
			MenuCursor = MakeShared<FWWOMenuCursor>();
			FSlateApplication::Get().RegisterInputPreProcessor(MenuCursor);
		}
		if (MenuCursor.IsValid()) MenuCursor->CenterCursor();
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
		if (MenuCursor.IsValid() && FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(MenuCursor);
		}
		MenuCursor.Reset();
		if (AWWOCharacter* Player = GetWWOCharacter()) Player->ResetInputEdges();
		// The button that closed the menu shouldn't also act in the game.
		FMemory::Memzero(Held, sizeof(Held));
	}
}

// --- Autotest ------------------------------------------------------------------------------

void AWWOPlayerController::Shot(const TCHAR* Name)
{
	const FString Path = FPaths::ProjectSavedDir() / TEXT("AutoTest") / FString(Name) + TEXT(".png");
	FScreenshotRequest::RequestScreenshot(Path, true, false);
	UE_LOG(LogWWO, Display, TEXT("AutoTest shot %s"), *Path);
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
	if (AutoTestStep == 0) AutoTestStep = 100; // start at 10 s (shaders, AI settle)
	if (At(10.f)) { Shot(TEXT("01_start")); Next(11.f); }
	else if (At(11.f)) { AimAtTarget(); Held[(int32)EWWOAction::Fire] = true; Next(12.f); }
	else if (At(12.f)) { Shot(TEXT("02_firing")); Next(13.f); }
	else if (At(13.f)) { Held[(int32)EWWOAction::Fire] = false; Held[(int32)EWWOAction::Aim] = true; Next(13.5f); }
	else if (At(13.5f)) { Shot(TEXT("03_ads")); Held[(int32)EWWOAction::Aim] = false; Next(14.f); }
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
	else if (At(25.f)) { UE_LOG(LogWWO, Display, TEXT("AutoTest done")); FPlatformMisc::RequestExit(false); }
}
