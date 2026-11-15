// -WWOAutoTestTester (with -WWOTester on the command line, which opens the firing tester): looks
// the screen over and plays its buttons, with screenshots 86_tester_*. Logs `AutoTest tester: ... ok|FAIL`.

#include "UI/WWOSettingsMenu.h"
#include "WWOSwitches.h"
#include "UI/WWOHUDWidget.h"
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
	auto Crossed = [&](float T) { return WWOTest::Crossed(Before, TesterClock, T); };
	auto Check = [](bool bOk, const FString& What) { WWOTest::Check(TEXT("tester"), bOk, What); };
	// -WWOTesterGun=<id>: the gun to look at (the saved loadout isn't changed on disk: the run never closes a menu).
	// The saved loadout was copied aside when the run started and is put back at the end, byte for byte (WWOTest::GuardLoadout).
	if (Crossed(4.f))
	{
		FString Gun;
		UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
		if (L && WWOSwitches::Value(TEXT("WWOTesterGun="), Gun))
		{
			TesterGunBefore = L->GetWeapon();
			for (UWWOWeaponData* W : L->Weapons) if (W && W->Id == FName(*Gun)) L->EquipWeapon(W);
			// -WWOTesterNoMods: the bare frame, so what is measured is the gun's own numbers (the saved
			// loadout comes back at the end with the rest).
			if (WWOSwitches::Has(TEXT("WWOTesterNoMods")))
			{
				for (const TCHAR* Slot : { TEXT("barrel"), TEXT("magazine"), TEXT("stock"), TEXT("sight"), TEXT("chambering"), TEXT("grip") }) L->EquipMod(Slot, nullptr);
			}
		}
	}
	// -WWOTesterFrames=N (run with -benchmark -fps=60 so every frame is 1/60 s): aim in, fire one
	// shot, and save N frames in a row from just before it (87_frame_000 ...), to lay beside footage.
	static int32 Frames = -1, FrameAt = 0;
	if (Frames < 0) { Frames = 0; WWOSwitches::Value(TEXT("WWOTesterFrames="), Frames); }
	if (Frames > 0)
	{
		static bool bStrafeToo = WWOSwitches::Has(TEXT("WWOTesterStrafe"));
		static bool bNoShot = WWOSwitches::Has(TEXT("WWOTesterNoShot"));
		if (Crossed(6.f)) { FiringTester->ResetPlayer(); if (bStrafeToo) FiringTester->SetStrafe(true); }
		static bool bHip = WWOSwitches::Has(TEXT("WWOTesterHip")); // from the hip instead of aimed in
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
				WWOTest::RestoreLoadout();
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
		{
			// The radar: fixed distances first, then the dummy 20 m straight ahead.
			Check(UWWOHUDWidget::RadarBands(31.f, 30.f, 4.f, 4) == 0 && UWWOHUDWidget::RadarBands(29.f, 30.f, 4.f, 4) == 1
				&& UWWOHUDWidget::RadarBands(15.f, 30.f, 4.f, 4) == 2 && UWWOHUDWidget::RadarBands(5.f, 30.f, 4.f, 4) == 3
				&& UWWOHUDWidget::RadarBands(3.f, 30.f, 4.f, 4) == 4, TEXT("radar: bands by distance (out of range 0, rim 1, ..., melee all)"));
			TArray<float> Lit;
			const int32 Steps = UWWOHUDWidget::Radar(P, Lit);
			const int32 Want = UWWOHUDWidget::RadarBands(GM->TesterTargetDistance, P->RadarRange, P->RadarFullRange, Steps);
			int32 Ahead = 0, Elsewhere = 0;
			for (int32 i = 0; i < Lit.Num(); ++i) (i < Steps ? Ahead : Elsewhere) += Lit[i] > 0.f ? 1 : 0;
			Check(Ahead == Want && Elsewhere == 0 && (Want == 0 || Lit[0] > 0.95f),
				FString::Printf(TEXT("radar: the dummy ahead lights %d bands of the front section, solid, and nothing else (%d, %d elsewhere)"), Want, Ahead, Elsewhere));
		}
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
	static float SecondPull = 0.f;
	if (Crossed(19.5f))
	{
		Check(FiringTester->HasControl() && !IsMenuOpen(), TEXT("TAKE CONTROL hands over the player"));
		Shot(TEXT("86_tester_5_control"));
		HandleMenuPress(EWWOAction::Menu);
	}
	if (Crossed(20.f))
	{
		Check(!FiringTester->HasControl() && FiringTester->WantsCursor(), TEXT("the Settings key gives the cursor back to the pane"));
		// The forgiving trigger: half way out and back in is a new pull; a wobble isn't.
		bool bDown = false; float Low = 0.f, High = 0.f; int32 Pulls = 0;
		for (const float V : { 0.f, 0.5f, 1.f, 1.f, 0.95f, 0.6f, 0.9f, 1.f, 0.97f, 1.f, 0.3f, 0.34f, 0.3f, 1.f, 0.f }) Pulls += StepTrigger(V, 0.15f, bDown, Low, High) ? 1 : 0;
		Check(Pulls == 3, FString::Printf(TEXT("trigger: three pulls counted from one that never fully lets go (%d)"), Pulls));
	}
	// A semi-auto pull just before the gun can fire again still fires. (The gun is made one-shot-a-pull
	// for the check through its setting, and put back after: nothing is saved in between.)
	static float SemiBefore = 0.f;
	const FString GunKey = FString(TEXT("weapon/")) + P->Weapon->GetWeaponData()->Id.ToString();
	if (Crossed(20.05f) && SettingsMenu)
	{
		SemiBefore = SettingsMenu->GetValue(GunKey, "SemiAuto");
		SettingsMenu->SetValue(GunKey, "SemiAuto", 1.f);
	}
	if (Crossed(20.2f))
	{
		TesterAmmo = P->Weapon->Ammo;
		ScriptPress(EWWOAction::Fire);
		SecondPull = TesterClock + 60.f / FMath::Max(P->Weapon->GetWeaponData()->FireRate, 1.f) - 0.12f; // 0.12 s too early
	}
	if (SecondPull > 0.f && Crossed(SecondPull)) ScriptPress(EWWOAction::Fire);
	if (Crossed(21.4f))
	{
		const int32 Per = FMath::Max(P->Weapon->GetWeaponData()->BurstCount(), 1);
		if (SettingsMenu) SettingsMenu->SetValue(GunKey, "SemiAuto", SemiBefore);
		Check(TesterAmmo - P->Weapon->Ammo == 2 * Per, FString::Printf(TEXT("semi-auto: a pull 0.12 s early still fired (%d rounds gone, %d wanted)"), TesterAmmo - P->Weapon->Ammo, 2 * Per));
		if (UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this); L && TesterGunBefore.IsValid()) L->EquipWeapon(TesterGunBefore.Get()); // the gun you had
		if (UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this)) L->FlushSave(); // anything due is written now, then replaced
		WWOTest::RestoreLoadout();
		UE_LOG(LogWWO, Display, TEXT("AutoTest tester: done"));
		WWOTest::Exit();
	}
}
