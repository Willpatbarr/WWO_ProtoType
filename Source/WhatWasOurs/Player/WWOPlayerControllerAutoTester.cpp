// -WWOAutoTestTester (with -WWOTester on the command line, which opens the firing tester): looks
// the screen over and plays its buttons, with screenshots 86_tester_*. Logs `AutoTest tester: ... ok|FAIL`.

#include "Player/WWOPlayerController.h"
#include "Player/WWOTestRun.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOWeaponComponent.h"
#include "UI/WWOFiringTester.h"
#include "Game/WWOGameMode.h"
#include "World/WWOTarget.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WhatWasOurs.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "EngineUtils.h"

namespace
{
	float TesterClock = 0.f;
	int32 TesterAmmo = 0;
	FVector TesterStart;
	float TesterSideMost = 0.f;
	TWeakObjectPtr<UWWOWeaponData> TesterGunBefore;
}

void AWWOPlayerController::UpdateAutoTesterTest(float DeltaTime)
{
	AWWOCharacter* P = GetWWOCharacter();
	const AWWOGameMode* GM = GetWorld()->GetAuthGameMode<AWWOGameMode>();
	if (!P || !GM || !FiringTester) return;
	const float Before = TesterClock;
	TesterClock += DeltaTime;
	auto Crossed = [&](float T) { return Before < T && TesterClock >= T; };
	auto Check = [](bool bOk, const FString& What) { UE_LOG(LogWWO, Display, TEXT("AutoTest tester: %s %s"), *What, bOk ? TEXT("ok") : TEXT("FAIL")); };
	// -WWOTesterGun=<id>: the gun to look at (the saved loadout isn't changed on disk: the run never closes a menu).
	// The saved loadout is copied aside first and put back at the end, byte for byte.
	const FString SaveFile = FPaths::ProjectSavedDir() / TEXT("SaveGames/Loadout.sav"), Backup = FPaths::ProjectSavedDir() / TEXT("SaveGames/Loadout.tester-backup");
	if (Crossed(3.f))
	{
		if (IFileManager::Get().FileExists(*Backup)) IFileManager::Get().Copy(*SaveFile, *Backup); // left by a run that stopped half way
		else if (IFileManager::Get().FileExists(*SaveFile)) IFileManager::Get().Copy(*Backup, *SaveFile);
	}
	if (Crossed(4.f))
	{
		FString Gun;
		UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
		if (L && FParse::Value(FCommandLine::Get(), TEXT("WWOTesterGun="), Gun))
		{
			TesterGunBefore = L->GetWeapon();
			for (UWWOWeaponData* W : L->Weapons) if (W && W->Id == FName(*Gun)) L->EquipWeapon(W);
		}
	}
	// -WWOTesterFrames=N (run with -benchmark -fps=60 so every frame is 1/60 s): aim in, fire one
	// shot, and save N frames in a row from just before it (87_frame_000 ...), to lay beside footage.
	static int32 Frames = -1, FrameAt = 0;
	if (Frames < 0) { Frames = 0; FParse::Value(FCommandLine::Get(), TEXT("WWOTesterFrames="), Frames); }
	if (Frames > 0)
	{
		static bool bStrafeToo = FParse::Param(FCommandLine::Get(), TEXT("WWOTesterStrafe"));
		static bool bNoShot = FParse::Param(FCommandLine::Get(), TEXT("WWOTesterNoShot"));
		if (Crossed(6.f)) { FiringTester->ResetPlayer(); if (bStrafeToo) FiringTester->SetStrafe(true); }
		static bool bHip = FParse::Param(FCommandLine::Get(), TEXT("WWOTesterHip")); // from the hip instead of aimed in
		ScriptHold(EWWOAction::Aim, !bHip && TesterClock >= 6.2f);
		static bool bFired = false;
		if (TesterClock >= 7.f)
		{
			if (FrameAt == 9 && !bFired && !bNoShot) { ScriptPress(EWWOAction::Fire); bFired = true; }
			ScriptHold(EWWOAction::Fire, !bNoShot && FrameAt >= 9 && FrameAt <= 11); // (a held-trigger gun reads the hold)
			if (FrameAt < Frames) Shot(*FString::Printf(TEXT("87_frame_%03d"), FrameAt));
			else if (FrameAt == Frames + 3)
			{
				if (UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this); L && TesterGunBefore.IsValid()) L->EquipWeapon(TesterGunBefore.Get());
				if (UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this)) L->FlushSave();
				if (IFileManager::Get().FileExists(*Backup)) { IFileManager::Get().Copy(*SaveFile, *Backup); IFileManager::Get().Delete(*Backup); }
				UE_LOG(LogWWO, Display, TEXT("AutoTest tester: %d frames saved"), Frames);
				WWOTest::Exit();
			}
			++FrameAt;
		}
		return;
	}
	if (Crossed(6.f))
	{
		int32 Dummies = 0;
		for (TActorIterator<AWWOTarget> It(GetWorld()); It; ++It) ++Dummies;
		const ULocalPlayer* LP = GetLocalPlayer();
		Check(Dummies == 1 && GM->TesterTarget.IsValid(), FString::Printf(TEXT("%d target dummy, no waves"), Dummies));
		Check(LP && FMath::IsNearlyEqual((float)LP->Size.X, UWWOFiringTester::WinW, 0.01f), FString::Printf(TEXT("the view is drawn in the window (%.2f of the width)"), LP ? (float)LP->Size.X : -1.f));
		Check(FiringTester->WantsCursor() && !IsPaused(), TEXT("the pane has the cursor and the game runs under it"));
		Shot(TEXT("86_tester_1_pane"));
	}
	if (Crossed(7.f)) { TesterAmmo = P->Weapon->Ammo; FiringTester->Play(UWWOFiringTester::EAct::AdsFire); }
	if (Crossed(7.35f)) Shot(TEXT("86_tester_2a_ads_before"));
	if (Crossed(7.9f)) Shot(TEXT("86_tester_2_ads_firing"));
	if (Crossed(8.4f)) Shot(TEXT("86_tester_2b_ads_firing"));
	if (Crossed(11.5f))
	{
		Check(P->Weapon->Ammo != TesterAmmo, FString::Printf(TEXT("ADS + FIRE fired (ammo %d -> %d)"), TesterAmmo, P->Weapon->Ammo));
		Check(FiringTester->Playing() == UWWOFiringTester::EAct::None, TEXT("the action ended on its own"));
		TesterAmmo = P->Weapon->Ammo;
		FiringTester->Play(UWWOFiringTester::EAct::HipFire);
	}
	if (Crossed(12.3f)) Shot(TEXT("86_tester_3_hip_firing"));
	if (Crossed(15.f))
	{
		Check(P->Weapon->Ammo != TesterAmmo, FString::Printf(TEXT("HIP FIRE fired (ammo %d -> %d)"), TesterAmmo, P->Weapon->Ammo));
		TesterStart = P->GetActorLocation();
		TesterSideMost = 0.f;
		FiringTester->SetStrafe(true);
		FiringTester->Play(UWWOFiringTester::EAct::Ads);
	}
	if (TesterClock > 15.f && TesterClock < 19.f) TesterSideMost = FMath::Max(TesterSideMost, FVector::Dist2D(P->GetActorLocation(), TesterStart));
	if (Crossed(16.f)) Shot(TEXT("86_tester_4_ads_strafe"));
	if (Crossed(19.f))
	{
		Check(TesterSideMost > 100.f, FString::Printf(TEXT("STRAFE moved the player side to side (%.1f m from the start at most)"), TesterSideMost / 100.f));
		FiringTester->SetStrafe(false);
		FiringTester->SetControl(true);
	}
	if (Crossed(19.5f))
	{
		Check(FiringTester->HasControl() && !IsMenuOpen(), TEXT("TAKE CONTROL hands over the player"));
		Shot(TEXT("86_tester_5_control"));
		HandleMenuPress(EWWOAction::Menu);
	}
	if (Crossed(20.f))
	{
		Check(!FiringTester->HasControl() && FiringTester->WantsCursor(), TEXT("the Settings key gives the cursor back to the pane"));
		if (UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this); L && TesterGunBefore.IsValid()) L->EquipWeapon(TesterGunBefore.Get()); // the gun you had
		if (UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this)) L->FlushSave(); // anything due is written now, then replaced
		if (IFileManager::Get().FileExists(*Backup)) { IFileManager::Get().Copy(*SaveFile, *Backup); IFileManager::Get().Delete(*Backup); }
		UE_LOG(LogWWO, Display, TEXT("AutoTest tester: done"));
		WWOTest::Exit();
	}
}
