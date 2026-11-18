// -WWOAutoTestTabs: the character menu's Weapon and Shell tabs each fit on one screen.
// With the fullest case set up (every perk mod on the rifle, four more saved configs), it opens
// the Weapon tab, the Weapon tab with a slot's mods showing, and the Shell tab, and checks that
// none of them has anything to scroll to. Shots 80_tab_weapon, 81_tab_weapon_mods, 82_tab_shell, 82_tab_flora, 82_tab_loadout;
// lines `AutoTest tabs: ... ok|FAIL`. The saved loadout file is put back at the end (WWOTest's guard).
// Run it at a few window sizes (-ResX / -ResY): the menus scale with the window.

#include "Player/WWOAutoTest.h"
#include "Player/WWOPlayerController.h"
#include "WWOSwitches.h"
#include "Player/WWOTestRun.h"
#include "Player/WWOCharacter.h"
#include "UI/WWOCharacterMenu.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WhatWasOurs.h"
#include "Camera/CameraActor.h"
#include "Characters/WWOBodyComponent.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOGrenadeComponent.h"

namespace
{
	int32 TabsStage = 0;
	float TabsClock = 0.f;
}

void FWWOAutoTest::UpdateAutoTabsTest(float DeltaTime)
{
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(PC);
	if (!L || !PC->GetPawn() || !PC->CharacterMenu) return;
	TabsClock += DeltaTime;
	// -WWOAutoTestPose: what others see of your upper body. Your own body, shown whole and seen from
	// outside: gun down, then aimed in looking level, up 35 degrees and down 35 (84_pose_*).
	static const bool bPose = WWOSwitches::Has(TEXT("WWOAutoTestPose"));
	if (bPose)
	{
		static const TCHAR* const Names[] = { TEXT("84_pose_hip"), TEXT("84_pose_aim_level"), TEXT("84_pose_aim_up"), TEXT("84_pose_aim_down") };
		static const float Looks[] = { 0.f, 0.f, 35.f, -35.f };
		static TWeakObjectPtr<ACameraActor> Camera;
		AWWOCharacter* P = PC->GetWWOCharacter();
		if (TabsStage == 0)
		{
			if (TabsClock < 8.f) return;
			P->Health->GodMode = 1.f;
			P->Body->SetShadowOnly(false);
			P->Weapon->SetFirstPersonHidden(true);
			P->Weapon->bForceWorldGun = true;
			// -WWOPoseWeapon=<id>: with that weapon in hand (the saved loadout comes back when the
			// run closes: WWOTest::GuardLoadout).
			FString Want;
			if (WWOSwitches::Value(TEXT("WWOPoseWeapon="), Want))
			{
				if (UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(P))
				{
					for (int32 i = 0; i < 12 && Loadout->GetWeapon() && Loadout->GetWeapon()->Id != FName(*Want); ++i) Loadout->CycleWeapon(1);
				}
			}
			// From the right side and a little ahead, close on the upper body: the gun's line against the look shows plainly.
			const FVector Side = P->GetActorLocation() + P->GetActorForwardVector() * 60.f + P->GetActorRightVector() * 190.f + FVector(0, 0, 55.f);
			Camera = PC->GetWorld()->SpawnActor<ACameraActor>(Side, (P->GetActorLocation() + P->GetActorForwardVector() * 25.f + FVector(0, 0, 45.f) - Side).Rotation());
			if (Camera.IsValid()) PC->SetViewTarget(Camera.Get());
			++TabsStage;
			TabsClock = 0.f;
			return;
		}
		const int32 Index = TabsStage - 1;
		if (Index >= (int32)UE_ARRAY_COUNT(Names))
		{
			if (TabsClock > 0.5f) { WWOTest::Exit(); TabsClock = -100.f; }
			return;
		}
		PC->Held[(int32)EWWOAction::Aim] = Index > 0;
		P->SetViewRotation(FMath::DegreesToRadians(Looks[Index]), P->GetActorRotation().Yaw);
		if (TabsClock > 1.3f)
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest pose: %s (gun up %.2f, looking %.0f degrees)"), Names[Index], P->PoseRaise, P->PosePitch);
			Shot(Names[Index]);
			++TabsStage;
			TabsClock = 0.f;
		}
		return;
	}
	// -WWOAutoTestVignette: the low-health vignette at full, 75%, 50%, 25% and 8% of health and
	// shield together, held there for a moment each (83_vignette_<percent>), then healing back.
	static const bool bVignette = WWOSwitches::Has(TEXT("WWOAutoTestVignette"));
	if (bVignette)
	{
		static const float Levels[] = { 1.f, 0.75f, 0.5f, 0.25f, 0.08f };
		AWWOCharacter* P = PC->GetWWOCharacter();
		if (TabsStage == 0) { if (TabsClock > 8.f) { ++TabsStage; TabsClock = 0.f; } return; }
		const int32 Index = TabsStage - 1;
		if (Index >= (int32)UE_ARRAY_COUNT(Levels))
		{
			if (TabsClock > 6.f) // left alone: regeneration brings it back and the red fades
			{
				UE_LOG(LogWWO, Display, TEXT("AutoTest vignette: healed back to %.0f + %.0f"), P->Health->Health, P->Health->Shield);
				Shot(TEXT("83_vignette_healed"));
				WWOTest::Exit();
				TabsClock = -100.f;
			}
			return;
		}
		// Hold the level (regeneration would raise it): the shield empties first, then health.
		const float Left = Levels[Index] * (P->Health->MaxHealth() + P->Health->MaxShield());
		P->Health->Health = FMath::Min(Left, P->Health->MaxHealth());
		P->Health->Shield = FMath::Max(Left - P->Health->MaxHealth(), 0.f);
		if (TabsClock > 1.2f)
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest vignette: at %.0f%% (health %.0f, shield %.0f)"), Levels[Index] * 100.f, P->Health->Health, P->Health->Shield);
			Shot(*FString::Printf(TEXT("83_vignette_%03d"), FMath::RoundToInt(Levels[Index] * 100.f)));
			++TabsStage;
			TabsClock = 0.f;
		}
		return;
	}
	auto Next = [] { ++TabsStage; TabsClock = 0.f; };
	auto Fits = [this](const TCHAR* What, const TCHAR* ShotName)
	{
		const float Over = PC->CharacterMenu->ScrollOverflow();
		UE_LOG(LogWWO, Display, TEXT("AutoTest tabs: %s fits on one screen (%.0f px to scroll) %s"), What, Over, Over < 1.f ? TEXT("ok") : TEXT("FAIL"));
		Shot(ShotName);
	};
	switch (TabsStage)
	{
	case 0:
		if (TabsClock < 8.f) break;
		// The fullest Weapon tab: six perks' worth of text and a long list of configs.
		for (UWWOWeaponData* W : L->Catalog.Weapons) if (W->Id == "rifle") L->EquipWeapon(W);
		for (UWWOModData* Mod : L->Catalog.WeaponMods) if (Mod->Frame == "rifle" && !Mod->Perk.IsNone()) L->EquipMod(Mod->Slot, Mod);
		for (int32 i = 0; i < 4; ++i) L->SaveConfig(INDEX_NONE);
		PC->ToggleCharacterMenu();
		PC->CharacterMenu->AutoTestShow(TEXT("Weapons"), FString());
		Next();
		break;
	case 1: if (TabsClock > 1.2f) { Fits(TEXT("the Weapon tab"), TEXT("80_tab_weapon")); Next(); } break;
	case 2: if (TabsClock > 0.6f) { PC->CharacterMenu->AutoTestShow(TEXT("Weapons"), TEXT("mod_sight:magnified_sight")); Next(); } break;
	case 3: if (TabsClock > 1.f) { Fits(TEXT("the Weapon tab with a slot's mods open"), TEXT("81_tab_weapon_mods")); Next(); } break;
	case 4: if (TabsClock > 0.6f) { PC->CharacterMenu->AutoTestShow(TEXT("Frame Parts"), FString()); Next(); } break;
	case 5: if (TabsClock > 1.f) { Fits(TEXT("the Shell tab"), TEXT("82_tab_shell")); Next(); } break;
	// The Flora tab with its picker open: each flora's badge (its symbol on its colour) beside its card.
	case 6: if (TabsClock > 0.6f) { PC->CharacterMenu->AutoTestShow(TEXT("Flora"), TEXT("flora")); Next(); } break;
	case 7: if (TabsClock > 1.f) { Fits(TEXT("the Flora tab with its picker open"), TEXT("82_tab_flora")); Next(); } break;
	// The Loadout tab (the overview): the flora's card among the rest.
	case 8: if (TabsClock > 0.6f) { PC->CharacterMenu->AutoTestShow(TEXT(""), FString()); Next(); } break;
	case 9: if (TabsClock > 1.f) { Fits(TEXT("the Loadout tab"), TEXT("82_tab_loadout")); Next(); } break;
	// Each flora equipped in turn (its grenades change with it), six steps each from stage 10 (a shot is
	// taken a frame late, so nothing changes on screen in the step that takes one):
	//   the Loadout tab: the flora's badge in its card, the grenade's in its tile (82_tab_loadout_<flora>);
	//   the cursor on the grenade tile: its hover row of the flora's grenades' badges (82_tab_loadout_<flora>_grenades);
	//   the Flora tab with the grenade picker open: the badges beside the slot and above each choice (82_tab_flora_<flora>_grenades).
	case 10: case 11: case 12: case 13: case 14: case 15: case 16: case 17: case 18: case 19: case 20: case 21:
	case 22: case 23: case 24: case 25: case 26: case 27:
	{
		const int32 Index = (TabsStage - 10) / 6;
		const int32 Step = (TabsStage - 10) % 6;
		const FString Id = L->Catalog.Floras.IsValidIndex(Index) ? L->Catalog.Floras[Index]->Id.ToString() : FString(TEXT("none"));
		if (TabsClock < (Step % 2 == 0 ? 0.6f : 1.f)) break;
		switch (Step)
		{
		case 0:
			if (L->Catalog.Floras.IsValidIndex(Index)) L->EquipFlora(L->Catalog.Floras[Index]);
			PC->CharacterMenu->AutoTestShow(TEXT(""), FString());
			break;
		case 1: Fits(*FString::Printf(TEXT("the Loadout tab with %s"), *Id), *FString::Printf(TEXT("82_tab_loadout_%s"), *Id)); break;
		case 2: PC->CharacterMenu->AutoTestShow(TEXT(""), TEXT("grenade")); break;
		case 3: Shot(*FString::Printf(TEXT("82_tab_loadout_%s_grenades"), *Id)); break;
		case 4: PC->CharacterMenu->AutoTestShow(TEXT("Flora"), TEXT("grenade")); break;
		default: Fits(*FString::Printf(TEXT("the Flora tab with %s's grenade picker"), *Id), *FString::Printf(TEXT("82_tab_flora_%s_grenades"), *Id)); break;
		}
		Next();
		break;
	}
	// The HUD's grenade (the last flora's): its badge, charged (82_hud_grenade) and refilling (82_hud_grenade_charging).
	case 28: if (TabsClock > 0.6f) { PC->ToggleCharacterMenu(); Next(); } break;
	case 29: if (TabsClock > 1.f) { Shot(TEXT("82_hud_grenade")); Next(); } break;
	case 30:
		if (AWWOCharacter* P = PC->GetWWOCharacter()) P->Grenades->Charge = 0.4f;
		if (TabsClock > 0.5f) { Shot(TEXT("82_hud_grenade_charging")); Next(); }
		break;
	case 31:
		if (TabsClock < 1.f) break;
		// Put the real save back (it's reloaded on the next launch; and again when the run closes).
		WWOTest::RestoreLoadout();
		UE_LOG(LogWWO, Display, TEXT("AutoTest tabs: done (saved loadout restored)"));
		WWOTest::Exit();
		Next();
		break;
	default: break;
	}
}
