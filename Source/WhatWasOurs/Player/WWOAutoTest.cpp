// The scripted test driver: which test the command line asks for, the one update a frame, and the
// full -WWOAutoTest run (a ladder of timed steps) with its grenade and melee steps. The other tests'
// steps are in Player/WWOAutoTest<Name>.cpp and WWOPerfTest.cpp.

#include "Player/WWOAutoTest.h"
#include "Player/WWOPlayerController.h"
#include "Camera/CameraActor.h"
#include "WWOSwitches.h"
#include "Player/WWOProf.h"
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
#include "UI/WWOScoreboard.h"
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
#include "Player/WWORegress.h"

FWWOAutoTestModes FWWOAutoTestModes::Read(const AWWOPlayerController& Controller)
{
	FWWOAutoTestModes M;
	M.bAutoTest = WWOSwitches::Has(TEXT("WWOAutoTest"));
	M.bAutoTestMenusOnly = WWOSwitches::Has(TEXT("WWOAutoTestMenus"));
	M.bAutoTestBodyOnly = WWOSwitches::Has(TEXT("WWOAutoTestBody"));
	M.bAutoTestGraftsOnly = WWOSwitches::Has(TEXT("WWOAutoTestGrafts"));
	M.bAutoTestMapsOnly = WWOSwitches::Has(TEXT("WWOAutoTestMaps"));
	M.bPerfTest = WWOSwitches::Has(TEXT("WWOPerfTest"));
	M.bAutoTestNet = WWOSwitches::Has(TEXT("WWOAutoTestNet"));
	M.bAutoTestTabs = WWOSwitches::Has(TEXT("WWOAutoTestTabs")) || WWOSwitches::Has(TEXT("WWOAutoTestVignette"))
		|| WWOSwitches::Has(TEXT("WWOAutoTestPose"));
	M.bAutoTestTitle = WWOSwitches::Has(TEXT("WWOAutoTestTitle")) || WWOSwitches::Has(TEXT("WWOAutoTestTitleJoin"))
		|| WWOSwitches::Has(TEXT("WWOAutoTestTitleJoinFail"));
	M.bAutoTestPvP = WWOSwitches::Has(TEXT("WWOAutoTestPvP"));
	M.bAutoTestTester = WWOSwitches::Has(TEXT("WWOAutoTestTester"));
	M.bAutoTestFootRace = WWOSwitches::Has(TEXT("WWOAutoTestFootRace"));
	M.bAutoTestGunsOnly = WWOSwitches::Has(TEXT("WWOAutoTestGuns"));
	M.bAutoTestHiddenOnly = WWOSwitches::Has(TEXT("WWOAutoTestHidden"));
	M.bAutoTestModsOnly = M.bAutoTestGunsOnly || M.bAutoTestHiddenOnly || WWOSwitches::Has(TEXT("WWOAutoTestMods"))
		|| WWOSwitches::Has(TEXT("WWOAutoTestFrames"));
	M.bAutoTest |= M.bAutoTestMenusOnly || M.bAutoTestBodyOnly || M.bAutoTestGraftsOnly || M.bAutoTestMapsOnly || M.bAutoTestModsOnly;
	M.bJoinerStress = Controller.GetNetMode() == NM_Client && WWOSwitches::IsStress();
	M.bRegress = WWOSwitches::IsRegress() && Controller.IsLocalController();
	return M;
}

bool FWWOAutoTestModes::Any() const
{
	return bRegress || bPerfTest || bAutoTestTitle || bAutoTestTabs || bAutoTestTester || bAutoTestFootRace || bAutoTestPvP || bJoinerStress || bAutoTestNet || bAutoTest;
}

TSharedPtr<FWWOAutoTest> FWWOAutoTest::MakeIfAsked(AWWOPlayerController& Controller)
{
	if (WWOSwitches::HasAutoTestText() || WWOSwitches::IsRegress())
	{
		WWOTest::Watch();        // count the FAIL lines of a test run
		WWOTest::GuardLoadout(); // and the saved loadout comes back as it was when the run closes
	}
	const FWWOAutoTestModes Modes = FWWOAutoTestModes::Read(Controller);
	if (!Modes.Any()) return nullptr; // no test asked for: no driver
	return MakeShared<FWWOAutoTest>(Controller, Modes);
}

FWWOAutoTest::FWWOAutoTest(AWWOPlayerController& Controller, const FWWOAutoTestModes& Modes)
	: FWWOAutoTestModes(Modes)
	, PC(&Controller)
{
	if (bRegress) Regress = MakeShared<FWWORegress>(Controller);
}

void FWWOAutoTest::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(AutoMapCamera);
	Collector.AddReferencedObject(AutoBodyCamera);
	Collector.AddReferencedObjects(AutoBodies);
	Collector.AddReferencedObjects(AutoBodyProps);
}

// Called from AWWOPlayerController::Tick, which runs while the game is paused (PlayerTick, where
// JustPressed is cleared, does not).
void FWWOAutoTest::Tick(float DeltaTime)
{
	if (bRegress)
	{
		Regress->Tick(); // counts ticks, not DeltaTime
		return;
	}
	if (bPerfTest)
	{
		if (AWWOCharacter* P = PC->GetWWOCharacter()) UpdatePerfTest(P, DeltaTime);
	}
	else if (bAutoTestTitle) { if (PC->IsLocalController()) UpdateAutoTitleTest(DeltaTime); }
	else if (bAutoTestTabs) { if (PC->IsLocalController()) UpdateAutoTabsTest(DeltaTime); }
	else if (bAutoTestTester) { if (PC->IsLocalController()) UpdateAutoTesterTest(DeltaTime); }
	else if (bAutoTestFootRace) { if (PC->IsLocalController()) UpdateAutoFootRaceTest(DeltaTime); }
	else if (bAutoTestPvP) UpdateAutoPvPTest(DeltaTime);
	else if (bJoinerStress) LogJoinerStress(DeltaTime);
	else if (bAutoTestNet) UpdateAutoNetTest(DeltaTime);
	else if (bAutoTest) UpdateAutoTest(DeltaTime);
}

void FWWOAutoTest::Shot(const TCHAR* Name)
{
	const FString Path = WWOTest::Shot(Name);
	// With the frame's timings, to see what costs what (menus open, effects...).
	UE_LOG(LogWWO, Display, TEXT("AutoTest shot %s (frame %.1f ms, game %.1f, render %.1f, gpu %.1f)"), *Path, FApp::GetDeltaTime() * 1000.0,
		FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime), FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
}

void FWWOAutoTest::UpdateAutoTest(float DeltaTime)
{
	AWWOCharacter* P = PC->GetWWOCharacter();
	if (!P) return;
	AutoTestClock += DeltaTime;
	auto At = [this](float T) { return AutoTestClock >= T && AutoTestStep == FMath::RoundToInt(T * 10.f); };
	auto Next = [this](float T) { AutoTestStep = FMath::RoundToInt(T * 10.f); };
	// Aim at the nearest living target for the shooting steps.
	auto AimAtTarget = [this, P]()
	{
		AActor* Best = nullptr;
		float BestD = 1e9f;
		for (AActor* A : UWWOWorldSubsystem::Get(PC)->LivingTargets())
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
			const float Len = PC->GetWorld()->SweepSingleByChannel(Hit, From, To, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(45.f), Params) ? Hit.Distance : 4000.f;
			if (Len > BestLen) { BestLen = Len; BestYaw = I * 15.f; }
		}
		P->SetViewRotation(0.f, BestYaw);
		return BestLen / WWO::M;
	};
	if (bAutoTrackSlide && !P->MoveState.bSliding && AutoTestClock > 26.85f)
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
		PC->JustPressed[(int32)EWWOAction::Jump] = true;
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
					AutoJumpPeak, AutoJumpPeakTime, AutoJumpLandTime, Rise, P->MoveState.JumpsUsed, bAutoSawMantle, AutoJumpDip);
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
	else if (At(11.f)) { AimAtTarget(); PC->Held[(int32)EWWOAction::Fire] = true; Next(12.f); }
	else if (At(12.f)) { Shot(TEXT("02_firing")); Next(13.f); }
	else if (At(13.f)) { PC->Held[(int32)EWWOAction::Fire] = false; PC->Held[(int32)EWWOAction::Aim] = true; Next(13.5f); }
	else if (At(13.5f))
	{
		UE_LOG(LogWWO, Display, TEXT("AutoTest look: ADS amount %.2f fov %.2f look multiplier %.3f"), P->Weapon->AdsAmount,
			P->Camera->FieldOfView, P->Weapon->LookMultiplier());
		Shot(TEXT("03_ads")); PC->Held[(int32)EWWOAction::Aim] = false; Next(14.f);
	}
	else if (At(14.f)) { P->Grenades->ThrowGrenade(); Next(15.2f); }
	else if (At(15.2f)) { Shot(TEXT("04_grenade")); Next(16.f); }
	else if (At(16.f)) { PC->JustPressed[(int32)EWWOAction::Melee] = true; Next(16.2f); }
	else if (At(16.2f)) { Shot(TEXT("05_melee")); Next(17.f); }
	else if (At(17.f)) { P->Ult->ActivateUlt(); Next(17.5f); }
	else if (At(17.5f)) { Shot(TEXT("06_ult_cast")); Next(18.2f); }
	else if (At(18.2f)) { Shot(TEXT("07_ult_blast")); Next(20.f); }
	else if (At(20.f)) { PC->ToggleSettingsMenu(); Next(21.f); }
	else if (At(21.f)) { Shot(TEXT("08_settings")); Next(21.5f); }
	else if (At(21.5f)) { PC->ToggleSettingsMenu(); PC->ToggleCharacterMenu(); Next(22.5f); }
	else if (At(22.5f)) { Shot(TEXT("09_character")); Next(23.f); }
	else if (At(23.f)) { PC->ToggleCharacterMenu(); P->SetViewRotation(0.f, P->GetActorRotation().Yaw + 180.f); Next(24.f); }
	else if (At(24.f)) { Shot(TEXT("10_allies")); Next(25.f); }
	// Ground movement: walk, crouch, sprint (stands you up), slide and its distance, stop.
	// Speeds in m/s; each run faces the longest clear line so walls don't cut it short.
	else if (At(25.f))
	{
		UE_LOG(LogWWO, Display, TEXT("AutoTest move: clear run %.1f m; stats walk %.2f sprint %.2f crouch %.2f slide %.2f over %.2f m"), FaceOpen(),
			P->M("walk_speed", P->WalkSpeed), P->M("sprint_speed", P->SprintSpeed), P->M("crouch_speed", P->CrouchSpeed),
			P->M("slide_speed", P->SlideSpeed), P->M("slide_distance", P->SlideDistance));
		PC->MoveValue = FVector2D(0.f, 1.f);
		Next(25.5f);
	}
	else if (At(25.5f)) { UE_LOG(LogWWO, Display, TEXT("AutoTest move: walk %.2f"), Speed2D()); PC->Held[(int32)EWWOAction::Crouch] = true; Next(25.6f); }
	else if (At(25.6f)) { PC->Held[(int32)EWWOAction::Crouch] = false; Next(26.3f); }
	else if (At(26.3f))
	{
		UE_LOG(LogWWO, Display, TEXT("AutoTest move: crouch %.2f crouched %d eye %.1f cm above feet"), Speed2D(), P->bIsCrouched,
			P->EyeLocation().Z - (P->GetActorLocation().Z - P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
		Shot(TEXT("11_crouched"));
		UE_LOG(LogWWO, Display, TEXT("AutoTest move: clear run %.1f m"), FaceOpen());
		PC->JustPressed[(int32)EWWOAction::Sprint] = true;
		Next(26.8f);
	}
	else if (At(26.8f))
	{
		UE_LOG(LogWWO, Display, TEXT("AutoTest move: sprint %.2f crouched %d"), Speed2D(), P->bIsCrouched);
		PC->Held[(int32)EWWOAction::Crouch] = true;
		AutoSlideFrom = P->GetActorLocation();
		bAutoTrackSlide = true;
		Next(26.9f);
	}
	else if (At(26.9f)) { UE_LOG(LogWWO, Display, TEXT("AutoTest move: slide %.2f sliding %d"), Speed2D(), P->MoveState.bSliding); PC->Held[(int32)EWWOAction::Crouch] = false; Next(28.5f); }
	else if (At(28.5f)) { PC->MoveValue = FVector2D::ZeroVector; Next(28.6f); }
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
		for (TActorIterator<AStaticMeshActor> It(PC->GetWorld()); It; ++It)
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
	else if (At(32.3f)) { PC->JustPressed[(int32)EWWOAction::Jump] = true; Next(34.f); }
	else if (At(34.f))
	{
		// Up against the crate: jump standing, then steer onto it with air control.
		const FVector Fwd = P->GetActorForwardVector();
		P->TeleportTo(AutoCrateAt - Fwd * (56.f + 60.f) + FVector(0.f, 0.f, 102.f - AutoCrateAt.Z), P->GetActorRotation());
		Next(34.3f);
	}
	else if (At(34.3f)) { StartJumpTrack(); Next(34.4f); }
	else if (At(34.4f)) { PC->MoveValue = FVector2D(0.f, 1.f); Next(34.6f); }
	else if (At(34.6f)) { PC->MoveValue = FVector2D::ZeroVector; Next(34.7f); } // drift on with the speed you built
	else if (At(34.7f)) { Shot(TEXT("12_crate_jump")); Next(35.6f); }
	else if (At(35.6f))
	{
		PC->MoveValue = FVector2D::ZeroVector;
		UE_LOG(LogWWO, Display, TEXT("AutoTest jump: after the crate jump feet %.1f cm (crate top 112), on floor %d"), FeetZ(), P->IsOnFloor());
		Shot(TEXT("13_crate_top"));
		P->Health->GodMode = 0.f;
		Next(36.f);
	}
	// Stick look: half a second at full right and half up (y = up after the mapping's flip).
	else if (At(36.f)) // (a while after the screenshot, whose long frame would skew the timing)
	{
		AutoLookYaw = PC->GetControlRotation().Yaw;
		AutoLookPitch = P->GetPitch();
		AutoLookFrom = AutoTestClock;
		PC->LookValue = FVector2D(1.f, 0.5f);
		Next(36.5f);
	}
	else if (At(36.5f))
	{
		PC->LookValue = FVector2D::ZeroVector;
		const float Seconds = AutoTestClock - AutoLookFrom;
		UE_LOG(LogWWO, Display, TEXT("AutoTest look: stick %.2f s turned %.1f deg right, %.1f deg up (no friction: %.1f / %.1f), hip fov %.2f"),
			Seconds, FMath::FindDeltaAngleDegrees(AutoLookYaw, PC->GetControlRotation().Yaw), FMath::RadiansToDegrees(P->GetPitch() - AutoLookPitch),
			FMath::RadiansToDegrees(P->StickSensitivity * Seconds), FMath::RadiansToDegrees(P->StickSensitivity * Seconds * 0.5f), P->Camera->FieldOfView);
		Next(37.f);
	}
	// Guns: cycle through every weapon (ending back on the first), fire each, and log recoil,
	// ammo and fire rate as `AutoTest gun:`.
	else if (At(37.f) || At(39.5f) || At(40.6f) || At(42.f))
	{
		const int32 Now = AutoTestStep;
		P->Health->GodMode = 1.f; // no dying mid-test (waves are running)
		if (UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(PC)) Loadout->CycleWeapon(1);
		const UWWOWeaponData* W = P->Weapon->GetWeaponData();
		UE_LOG(LogWWO, Display, TEXT("AutoTest gun: equipped %s, ammo %d / %d"), W ? *W->Id.ToString() : TEXT("none"), P->Weapon->Ammo, P->Weapon->MagazineSize());
		Next(Now == 370 ? 37.5f : Now == 395 ? 40.f : Now == 406 ? 41.f : 42.5f);
	}
	else if (At(37.5f))
	{
		AimAtTarget();
		AutoGunPitch = P->GetPitch();
		AutoGunTime = PC->GetWorld()->GetTimeSeconds();
		PC->JustPressed[(int32)EWWOAction::Fire] = true;
		Next(37.6f);
	}
	else if (At(37.6f))
	{
		UE_LOG(LogWWO, Display, TEXT("AutoTest gun: hand cannon kick %.2f deg after %.3f game s (recoil up %.2f over %.3f s)"),
			FMath::RadiansToDegrees(P->GetPitch() - AutoGunPitch), PC->GetWorld()->GetTimeSeconds() - AutoGunTime,
			P->Weapon->GetWeaponData()->RecoilUp, P->Weapon->GetWeaponData()->RecoilKickTime);
		Shot(TEXT("14_handcannon"));
		Next(38.1f);
	}
	else if (At(38.1f))
	{
		const UWWOWeaponData* W = P->Weapon->GetWeaponData();
		UE_LOG(LogWWO, Display, TEXT("AutoTest gun: hand cannon at %.2f deg after %.3f game s (settles to %.2f at %.3f s), ammo %d"),
			FMath::RadiansToDegrees(P->GetPitch() - AutoGunPitch), PC->GetWorld()->GetTimeSeconds() - AutoGunTime, W->RecoilUp * (1.f - W->RecoilRecovery),
			60.f / W->FireRate + W->RecoilRecoveryDelay, P->Weapon->Ammo);
		PC->JustPressed[(int32)EWWOAction::Reload] = true;
		Next(38.7f);
	}
	else if (At(38.7f)) { Shot(TEXT("15_reload")); UE_LOG(LogWWO, Display, TEXT("AutoTest gun: reloading %d progress %.2f"), P->Weapon->bReloading, P->Weapon->ReloadProgress); Next(39.5f); }
	else if (At(40.f)) { AimAtTarget(); PC->JustPressed[(int32)EWWOAction::Fire] = true; Next(40.1f); }
	else if (At(40.1f)) { Shot(TEXT("16_shotgun")); UE_LOG(LogWWO, Display, TEXT("AutoTest gun: shotgun ammo %d"), P->Weapon->Ammo); Next(40.6f); }
	else if (At(41.f)) { AimAtTarget(); PC->Held[(int32)EWWOAction::Aim] = true; Next(41.5f); }
	else if (At(41.5f)) { Shot(TEXT("17_sniper_scope")); PC->JustPressed[(int32)EWWOAction::Fire] = true; Next(41.6f); }
	else if (At(41.6f))
	{
		UE_LOG(LogWWO, Display, TEXT("AutoTest gun: sniper ADS %.2f fov %.2f ammo %d"), P->Weapon->AdsAmount, P->Camera->FieldOfView, P->Weapon->Ammo);
		PC->Held[(int32)EWWOAction::Aim] = false;
		Next(42.f);
	}
	else if (At(42.5f)) { AimAtTarget(); PC->Held[(int32)EWWOAction::Aim] = true; Next(42.8f); }
	else if (At(42.8f)) { Shot(TEXT("18_rifle_ads")); Next(43.f); } // (the screenshot's long frame would fire a burst)
	else if (At(43.f)) { PC->Held[(int32)EWWOAction::Fire] = true; AutoGunAmmo = P->Weapon->Ammo; AutoGunTime = PC->GetWorld()->GetTimeSeconds(); Next(44.f); }
	else if (At(44.f))
	{
		PC->Held[(int32)EWWOAction::Fire] = PC->Held[(int32)EWWOAction::Aim] = false;
		const float Seconds = PC->GetWorld()->GetTimeSeconds() - AutoGunTime;
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

bool FWWOAutoTest::PlaceForAutoTest(AWWOCharacter* P, AActor* Target, float DistanceM, float YawOffset, bool bMove)
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
			bClear &= !PC->GetWorld()->SweepSingleByObjectType(Hit, Feet + Dir * 50.f + FVector(0, 0, Up), Stand + Dir * 60.f + FVector(0, 0, Up), FQuat::Identity,
				Static, FCollisionShape::MakeSphere(35.f), Params);
		}
		for (float D = 1.f; bClear && D <= DistanceM + 0.01f; D += 1.f)
		{
			const FVector At = Feet + Dir * D * WWO::M;
			bClear = PC->GetWorld()->LineTraceSingleByObjectType(Hit, At + FVector(0, 0, 60.f), At - FVector(0, 0, 60.f), Static, Params)
				&& FMath::Abs(Hit.ImpactPoint.Z - Feet.Z) < 15.f;
		}
		if (!bClear) continue;
		if (!bMove) return true;
		PC->GetWorld()->LineTraceSingleByObjectType(Hit, Stand + FVector(0, 0, 60.f), Stand - FVector(0, 0, 60.f), Static, Params);
		if (!P->TeleportTo(Hit.ImpactPoint + FVector(0, 0, P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f), FRotator(0.f, (-Dir).Rotation().Yaw, 0.f))) continue;
		P->GetCharacterMovement()->Velocity = FVector::ZeroVector;
		P->SetViewRotation(0.f, (-Dir).Rotation().Yaw + YawOffset);
		return true;
	}
	return false;
}

bool FWWOAutoTest::UpdateAutoGrenadeTest(AWWOCharacter* P, float DeltaTime)
{
	auto bShortThrow = [](const UWWOGrenadeData* G) { return G->Kind == EWWOGrenadeKind::Spike || G->Kind == EWWOGrenadeKind::Tripmine; };
	UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(PC);
	if (!Loadout) return true;
	P->Health->GodMode = 1.f; // the waves keep running
	if (P->Health->bDead) P->Health->Respawn();
	if (AutoGrenadeIndex < 0)
	{
		// The nearest standing (not strafing) training target, and every grenade, the wall last
		// (it stays up for 10 s and would block the rest).
		AutoTarget = nullptr;
		float BestD = 1e9f;
		for (TActorIterator<AWWOTarget> It(PC->GetWorld()); It; ++It)
		{
			const float D = FVector::Dist(It->GetActorLocation(), P->GetActorLocation());
			if (It->StrafeDistance <= 0.f && D < BestD && PlaceForAutoTest(P, *It, 17.f, 0.f, false)) { BestD = D; AutoTarget = *It; }
		}
		AutoGrenadeBefore = Loadout->GetGrenade();
		AutoGrenades.Reset();
		for (UWWOGrenadeData* G : Loadout->Catalog.Grenades) if (G && G->Kind != EWWOGrenadeKind::Wall) AutoGrenades.Add(G);
		for (UWWOGrenadeData* G : Loadout->Catalog.Grenades) if (G && G->Kind == EWWOGrenadeKind::Wall) AutoGrenades.Add(G);
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
			for (AWWOEnemy* E : UWWOWorldSubsystem::Get(PC)->LivingEnemies())
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
	for (TActorIterator<AWWOGrenade> It(PC->GetWorld()); It; ++It) bOut |= It->Spec.Data == G;
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
		for (TActorIterator<AWWOGrenade> It(PC->GetWorld()); It; ++It) if (It->Spec.Data == G) It->Destroy(); // (a timed-out one)
		AutoGrenadeIndex++;
		AutoPhase = -1.f;
	}
	return false;
}

bool FWWOAutoTest::UpdateAutoMeleeTest(AWWOCharacter* P, float DeltaTime)
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
			PC->JustPressed[(int32)EWWOAction::Melee] = true;
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
			for (AWWOEnemy* E : UWWOWorldSubsystem::Get(PC)->LivingEnemies())
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
		PC->JustPressed[(int32)EWWOAction::Melee] = true;
		UE_LOG(LogWWO, Display, TEXT("AutoTest melee: at %s from %.2f m (placed %d); reach %.1f, lunge range %.1f angle %.0f speed %.1f, damage %.0f, knockback %.1f"),
			*TargetActor->GetName(), FVector2D(P->GetActorLocation() - Target->FeetLocation()).Size() / WWO::M, bPlaced, P->Weapon->MeleeReach,
			P->Weapon->LungeRange, P->Weapon->LungeAngle, P->Weapon->LungeSpeed, P->Weapon->MeleeDamage, P->Weapon->MeleeKnockback);
		AutoPhase = 0.f;
		return false;
	}
	AutoPhase += DeltaTime;
	if (P->IsLunging()) { bAutoSawLunge = true; AutoLungeTime += PC->GetWorld()->GetDeltaSeconds(); }
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
			AutoTargetHealth, Target->GetHealth(), PC->GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation(),
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
