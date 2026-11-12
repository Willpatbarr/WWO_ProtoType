// Weapon mod steps of the autotest (-WWOAutoTestMods, and the end of -WWOAutoTest): the draft
// rifle and hand cannon mods. Checks that each plain mod changes the rifle's stats by its percentages,
// that a saved config comes back when applied (and by the quick-swap cycle), fires the perk
// config at a target and logs the perk counters, then screenshots the Weapons tile and a slot
// picker. Then the same for the hand cannon (its stats checked against the mod data, its perk
// set fired, its tile screenshotted). The saved loadout is backed up first and put back at the end.
// Logs `AutoTest mods: ... ok` or `... FAIL`, with screenshots 37_mods_*.

#include "Player/WWOPlayerController.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOWeaponComponent.h"
#include "Characters/WWOBodyComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOAimAssistComponent.h"
#include "UI/WWOCharacterMenu.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "Data/WWOData.h"
#include "WhatWasOurs.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "FX/WWOFX.h"
#include "Enemies/WWOEnemyProjectile.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	FString SavePath() { return FPaths::ProjectSavedDir() / TEXT("SaveGames/Loadout.sav"); }
	FString BackupPath() { return FPaths::ProjectSavedDir() / TEXT("SaveGames/Loadout.autotest-backup"); }

	UWWOModData* ModById(UWWOLoadoutSubsystem* L, FName Id)
	{
		for (UWWOModData* M : L->WeaponMods)
		{
			if (M->Id == Id) return M;
		}
		return nullptr;
	}

	void Check(bool bOk, const FString& What)
	{
		UE_LOG(LogWWO, Display, TEXT("AutoTest mods: %s %s"), *What, bOk ? TEXT("ok") : TEXT("FAIL"));
	}

	bool Near(float A, float B) { return FMath::Abs(A - B) <= FMath::Max(0.01f, FMath::Abs(B) * 0.01f); }
}

bool AWWOPlayerController::UpdateAutoModTest(AWWOCharacter* P, float DeltaTime)
{
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return true;
	AutoModClock += DeltaTime;
	auto Step = [this](int32 Next) { AutoModStep = Next; AutoModClock = 0.f; };
	UWWOWeaponComponent* Gun = P->Weapon;
	static const FName Plain[] = { "long_barrel", "extended_mag", "heavy_stock", "magnified_sight", "heavy_rounds", "light_grip" };
	static const FName Perky[] = { "focus_barrel", "scavenger_mag", "brace_stock", "hunters_lens", "static_rounds", "adrenal_grip" };
	static const FName CannonPlain[] = { "ported_barrel", "speed_loader", "recoil_pad", "peep_sight", "magnum_rounds", "quickdraw_grip" };
	static const FName CannonPerky[] = { "overpressure_barrel", "final_round_mag", "anchor_stock", "patient_sight", "ricochet_rounds", "hammer_grip" };
	const bool bCannon = Gun->GetBaseWeaponData() && Gun->GetBaseWeaponData()->Id == "handcannon";
	auto EquipWeaponId = [L](FName Id)
	{
		for (UWWOWeaponData* W : L->Weapons)
		{
			if (W->Id == Id) L->EquipWeapon(W);
		}
	};
	// The summed percent of one stat over a set of mods, read from the mod data.
	auto Sum = [L](const FName* Ids, int32 Count, FName Stat)
	{
		float Total = 0.f;
		for (int32 i = 0; i < Count; ++i)
		{
			if (const UWWOModData* Mod = ModById(L, Ids[i]))
			{
				for (const FWWOStatModifier& M : Mod->Mods) Total += M.Stat == Stat ? M.Value : 0.f;
			}
		}
		return Total;
	};

	switch (AutoModStep)
	{
	case 0:
	{
		// Back up the real save, then start from a bare rifle.
		IFileManager::Get().Delete(*BackupPath());
		if (IFileManager::Get().FileExists(*SavePath())) IFileManager::Get().Copy(*BackupPath(), *SavePath());
		P->Health->GodMode = 1.f;
		Check(L->WeaponMods.Num() == 60, FString::Printf(TEXT("%d mods loaded"), L->WeaponMods.Num()));
		EquipWeaponId("rifle");
		for (const FName& Slot : UWWOLoadoutSubsystem::ModSlots()) L->EquipMod(Slot, nullptr);
		AutoGunIdx = AutoGunStage = 0;
		Step(bAutoTestHiddenOnly ? 60 : bAutoTestGunsOnly ? 40 : FParse::Param(FCommandLine::Get(), TEXT("WWOAutoTestFrames")) ? 70 : 1);
		break;
	}
	case 1:
		if (AutoModClock < 0.3f) break;
		{
			// Base stats, then every plain mod.
			const UWWOWeaponData* B = Gun->GetBaseWeaponData();
			AutoModBase = { B->Damage, B->FireRate, B->MagazineSize, B->FireRange, B->RecoilUp, B->RecoilSide, B->Zoom, B->Sway, B->AdsMoveSpeed };
			AutoModBaseReload = P->M("reload_time", B->ReloadTime);
			AutoModBaseSwap = P->M("swap_time", Gun->SwapTime);
			for (const FName& Id : Plain)
			{
				UWWOModData* Mod = ModById(L, Id);
				Check(Mod != nullptr, Id.ToString() + TEXT(" exists"));
				if (Mod) L->EquipMod(Mod->Slot, Mod);
			}
			Step(2);
		}
		break;
	case 2:
		if (AutoModClock < 0.3f) break;
		{
			const UWWOWeaponData* W = Gun->GetWeaponData();
			const TArray<float>& B = AutoModBase;
			Check(Near(W->Damage, B[0] * 1.2f), FString::Printf(TEXT("damage %.2f -> %.2f (+20%%)"), B[0], W->Damage));
			Check(Near(W->FireRate, B[1] * 0.85f), FString::Printf(TEXT("fire rate %.1f -> %.1f (-15%%)"), B[1], W->FireRate));
			Check(Near(W->MagazineSize, FMath::RoundToFloat(B[2] * 1.4f)), FString::Printf(TEXT("magazine %.0f -> %.0f (+40%%)"), B[2], W->MagazineSize));
			Check(Gun->Ammo == Gun->MagazineSize(), FString::Printf(TEXT("magazine refilled to %d"), Gun->Ammo));
			Check(Near(W->FireRange, B[3] * 1.25f), FString::Printf(TEXT("range %.1f -> %.1f (+25%%)"), B[3], W->FireRange));
			Check(Near(W->RecoilUp, B[4] * 0.75f), FString::Printf(TEXT("vertical recoil %.3f -> %.3f (-25%%)"), B[4], W->RecoilUp));
			Check(Near(W->RecoilSide, B[5] * 1.15f), FString::Printf(TEXT("side recoil %.3f -> %.3f (+15%%)"), B[5], W->RecoilSide));
			Check(Near(W->Zoom, 1.f + (B[6] - 1.f) * 1.4f), FString::Printf(TEXT("zoom %.2f -> %.2f"), B[6], W->Zoom));
			Check(Near(W->Sway, B[7] * 1.1f), FString::Printf(TEXT("sway %.2f -> %.2f (+10%%)"), B[7], W->Sway));
			Check(Near(W->AdsMoveSpeed, B[8] * 0.9f), FString::Printf(TEXT("aim move speed %.2f -> %.2f (-10%%)"), B[8], W->AdsMoveSpeed));
			const float Reload = P->M("reload_time", Gun->GetBaseWeaponData()->ReloadTime);
			// Reload and swap times add the mods' percentages to the loadout's own (shell parts, traits).
			Check(Reload > AutoModBaseReload,
				FString::Printf(TEXT("reload time %.2f -> %.2f"), AutoModBaseReload, Reload));
			const float Swap = P->M("swap_time", Gun->SwapTime);
			Check(Swap < AutoModBaseSwap, FString::Printf(TEXT("swap time %.3f -> %.3f"), AutoModBaseSwap, Swap));
			Check(Gun->GetBaseWeaponData()->Damage == B[0], TEXT("the weapon asset is unchanged"));

			// Save it as a config, swap to every perk mod, then bring the config back.
			const int32 Before = L->GetConfigs().Num();
			L->SaveConfig(INDEX_NONE);
			AutoModConfig = L->GetConfigs().Num() - 1;
			Check(L->GetConfigs().Num() == Before + 1, TEXT("saved ") + L->GetConfigs().Last().Name + TEXT(": ") + UWWOLoadoutSubsystem::ConfigSummary(L->GetConfigs().Last(), L));
			for (const FName& Id : Perky)
			{
				if (UWWOModData* Mod = ModById(L, Id)) L->EquipMod(Mod->Slot, Mod);
			}
			bool bAllPerks = true;
			for (const FName& Perk : { FName("focus"), FName("scavenger"), FName("brace"), FName("hunter"), FName("static"), FName("adrenal") })
			{
				bAllPerks &= L->HasPerk(Perk);
			}
			Check(bAllPerks, TEXT("all six perks equipped"));
			L->SaveConfig(INDEX_NONE); // a second config: the perk set
			L->ApplyConfig(AutoModConfig);
			bool bBack = true;
			for (const FName& Id : Plain) bBack &= ModById(L, Id) && L->GetMod(ModById(L, Id)->Slot) == ModById(L, Id);
			Check(bBack, TEXT("applying the first config brings its mods back"));
			L->CycleConfig();
			Check(L->HasPerk("static") && L->GetActiveConfig() == AutoModConfig + 1, TEXT("quick swap (next config) goes to the perk set"));
			Step(3);
		}
		break;
	case 3:
	{
		// Fire the perk set at the nearest target for a few seconds.
		AActor* Best = nullptr;
		float BestD = 1e9f;
		for (AActor* A : UWWOWorldSubsystem::Get(this)->LivingTargets())
		{
			const float D = FVector::Dist(A->GetActorLocation(), P->GetActorLocation());
			if (D < BestD && Cast<IWWOTargetable>(A)->FeetLocation().Z < 100.f) { BestD = D; Best = A; }
		}
		if (AutoModClock < DeltaTime * 1.5f && Best) PlaceForAutoTest(P, Best, 10.f, 0.f);
		if (Best)
		{
			const FVector Dir = Cast<IWWOTargetable>(Best)->GetAimParts().Last().Position - P->EyeLocation();
			P->SetViewRotation(FMath::Asin(Dir.GetSafeNormal().Z), Dir.Rotation().Yaw);
		}
		// Semi-auto guns need fresh trigger pulls: tap it about three times a second.
		{
			const bool bDown = AutoModClock > 0.5f && AutoModClock < 3.5f && (!bCannon || FMath::Fmod(AutoModClock, 0.32f) < 0.16f);
			if (bDown && !Held[(int32)EWWOAction::Fire]) JustPressed[(int32)EWWOAction::Fire] = true;
			Held[(int32)EWWOAction::Fire] = bDown;
		}
		if (FMath::IsNearlyEqual(AutoModClock, 2.5f, DeltaTime * 0.6f)) Shot(bCannon ? TEXT("37_mods_cannon_perk_firing") : TEXT("37_mods_perk_firing"));
		if (AutoModClock >= 3.6f)
		{
			Held[(int32)EWWOAction::Fire] = false;
			UE_LOG(LogWWO, Display, TEXT("AutoTest mods: perk HUD after firing '%s', ammo %d / %d"), *Gun->PerkStatus(), Gun->Ammo, Gun->MagazineSize());
			if (bCannon) Check(Gun->Ammo < Gun->MagazineSize() && !Gun->PerkStatus().Contains(TEXT("Overpressure")), TEXT("hand cannon fired and used up Overpressure"));
			else Check(!Gun->PerkStatus().IsEmpty(), TEXT("perk status shows on the HUD"));
			if (!CharacterMenu->IsOpen()) ToggleCharacterMenu();
			CharacterMenu->AutoTestShow(TEXT("Weapons"), bCannon ? TEXT("mod_magazine:final_round_mag") : FString());
			Step(bCannon ? 23 : 4);
		}
		break;
	}
	case 4:
		if (AutoModClock < 1.f) break;
		CharacterMenu->AutoTestScroll(-1.f);
		Step(9);
		break;
	case 9:
		if (AutoModClock < 0.3f) break;
		Shot(TEXT("37_mods_weapons_tile"));
		Step(5);
		break;
	case 5:
		if (AutoModClock < 0.5f) break;
		CharacterMenu->AutoTestShow(TEXT("Weapons"), TEXT("mod_barrel:long_barrel")); // its popup, previewing Long Barrel
		Step(6);
		break;
	case 6:
		if (AutoModClock < 0.5f) break;
		CharacterMenu->AutoTestScroll(-1.f);
		Step(7);
		break;
	case 7:
		if (AutoModClock < 0.5f) break;
		Shot(TEXT("37_mods_barrel_preview"));
		Step(12);
		break;
	case 12:
		if (AutoModClock < 0.3f) break; // let the screenshot happen first
		CharacterMenu->AutoTestShow(TEXT("Weapons"), TEXT("mod_chambering:heavy_rounds"));
		Step(10);
		break;
	case 10:
		if (AutoModClock < 0.5f) break;
		CharacterMenu->AutoTestScroll(-1.f);
		Step(11);
		break;
	case 11:
		if (AutoModClock < 0.4f) break;
		Shot(TEXT("37_mods_chambering_preview"));
		Step(13);
		break;
	case 13:
		if (AutoModClock < 0.3f) break;
		CharacterMenu->AutoTestShow(TEXT("Weapons"), FString());
		if (CharacterMenu->IsOpen()) ToggleCharacterMenu();
		// The hand cannon: a bare frame first.
		EquipWeaponId("handcannon");
		for (const FName& Slot : UWWOLoadoutSubsystem::ModSlots()) L->EquipMod(Slot, nullptr);
		Step(20);
		break;
	case 20:
		if (AutoModClock < 0.5f) break;
		{
			Check(bCannon, TEXT("switched to the hand cannon"));
			Check(L->ModsFor("barrel").Num() == 2 && L->ModsFor("barrel")[0]->Frame == "handcannon", TEXT("the barrel slot lists the hand cannon's two mods"));
			const UWWOWeaponData* B = Gun->GetBaseWeaponData();
			AutoModBase = { B->Damage, B->FireRange, B->RecoilUp, B->RecoilSide, B->MagazineSize, B->Zoom };
			AutoModBaseReload = P->M("reload_time", B->ReloadTime);
			for (const FName& Id : CannonPlain)
			{
				UWWOModData* Mod = ModById(L, Id);
				Check(Mod != nullptr, Id.ToString() + TEXT(" exists"));
				if (Mod) L->EquipMod(Mod->Slot, Mod);
			}
			Step(21);
		}
		break;
	case 21:
		if (AutoModClock < 0.3f) break;
		{
			const UWWOWeaponData* W = Gun->GetWeaponData();
			const TArray<float>& B = AutoModBase;
			const int32 N = UE_ARRAY_COUNT(CannonPlain);
			auto Expect = [&](float Got, float Base, FName Stat, const TCHAR* Label)
			{
				const float Want = Base * (1.f + Sum(CannonPlain, N, Stat));
				Check(Near(Got, Want), FString::Printf(TEXT("hand cannon %s %.3f -> %.3f (want %.3f)"), Label, Base, Got, Want));
			};
			Expect(W->Damage, B[0], "damage", TEXT("damage"));
			Expect(W->FireRange, B[1], "range", TEXT("range"));
			Expect(W->RecoilUp, B[2], "recoil_up", TEXT("vertical recoil"));
			Expect(W->RecoilSide, B[3], "recoil_side", TEXT("side recoil"));
			Check(Near(W->MagazineSize, FMath::RoundToFloat(B[4] * (1.f + Sum(CannonPlain, N, "magazine_size")))), FString::Printf(TEXT("hand cannon magazine %.0f -> %.0f"), B[4], W->MagazineSize));
			Check(Near(W->Zoom, 1.f + (B[5] - 1.f) * (1.f + Sum(CannonPlain, N, "zoom"))), FString::Printf(TEXT("hand cannon zoom %.2f -> %.2f"), B[5], W->Zoom));
			const float Reload = P->M("reload_time", Gun->GetBaseWeaponData()->ReloadTime);
			Check(Reload < AutoModBaseReload, FString::Printf(TEXT("hand cannon reload time %.2f -> %.2f"), AutoModBaseReload, Reload));
			Check(L->HasPerk("static") == false, TEXT("the rifle's perks don't carry over"));
			for (const FName& Id : CannonPerky)
			{
				if (UWWOModData* Mod = ModById(L, Id)) L->EquipMod(Mod->Slot, Mod);
			}
			bool bAllPerks = true;
			for (const FName& Perk : { FName("overpressure"), FName("final_round"), FName("anchor"), FName("patience"), FName("ricochet"), FName("hammer") })
			{
				bAllPerks &= L->HasPerk(Perk);
			}
			Check(bAllPerks, TEXT("all six hand cannon perks equipped"));
			Step(3); // fire them, then the tile
		}
		break;
	case 40:
	{
		// -WWOAutoTestGuns: each gun at the hip and aimed, and the modded ones with each mod set.
		// Screenshots 38_gun_<weapon>_<state>.
		static const FName Ids[] = { "rifle", "handcannon", "shotgun", "sniper", "burstrifle" };
		// -WWOFxShots: each gun also fires once from the hip in slow motion, with a second shot drawn
		// across the view as another player's would be. Screenshots 39_fx_<weapon>_flash (the muzzle
		// flash) and _trail (the shots in the air a moment later).
		static const bool bFxShots = FParse::Param(FCommandLine::Get(), TEXT("WWOFxShots"));
		static int32 FxFrames = -1;
		if (bFxShots && FxFrames >= 0 && AutoGunIdx < (int32)UE_ARRAY_COUNT(Ids))
		{
			++FxFrames;
			if (FxFrames == 5) Shot(*(TEXT("39_fx_") + Ids[AutoGunIdx].ToString() + TEXT("_flash")));
			if (FxFrames == 70) Shot(*(TEXT("39_fx_") + Ids[AutoGunIdx].ToString() + TEXT("_trail")));
			// Then a little faster, to catch the sparks and the death burst mid-flight (_burst).
			if (FxFrames == 71) UGameplayStatics::SetGlobalTimeDilation(this, 0.15f);
			if (FxFrames == 95) Shot(*(TEXT("39_fx_") + Ids[AutoGunIdx].ToString() + TEXT("_burst")));
		}
		if (AutoModClock < (bFxShots && FxFrames >= 0 ? 2.f : 0.9f)) break;
		if (bFxShots && FxFrames >= 0)
		{
			FxFrames = -1;
			UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
			Held[(int32)EWWOAction::Fire] = false;
		}
		AutoModClock = 0.f;
		if (AutoGunIdx >= (int32)UE_ARRAY_COUNT(Ids))
		{
			Held[(int32)EWWOAction::Aim] = false;
			Step(8);
			break;
		}
		const FName Id = Ids[AutoGunIdx];
		// Every frame's two mod sets, from the mod data: the mod without a perk and the one with, per slot.
		FName PlainSet[6], PerkSet[6];
		bool bModded = false;
		for (const UWWOModData* Mod : L->WeaponMods)
		{
			const int32 SlotIndex = Mod && Mod->Frame == Id ? UWWOLoadoutSubsystem::ModSlots().IndexOfByKey(Mod->Slot) : INDEX_NONE;
			if (SlotIndex < 0 || SlotIndex >= 6) continue;
			(Mod->Perk.IsNone() ? PlainSet : PerkSet)[SlotIndex] = Mod->Id;
			bModded = true;
		}
		const FString Tag = TEXT("38_gun_") + Id.ToString();
		auto EquipSet = [&](const FName* Set)
		{
			for (int32 i = 0; i < 6; ++i)
			{
				if (UWWOModData* Mod = ModById(L, Set[i])) L->EquipMod(Mod->Slot, Mod);
			}
		};
		switch (AutoGunStage++)
		{
		case 0: EquipWeaponId(Id); for (const FName& Slot : UWWOLoadoutSubsystem::ModSlots()) L->EquipMod(Slot, nullptr); break;
		case 1:
			Shot(*(Tag + TEXT("_hip")));
			if (bFxShots)
			{
				FxFrames = 0;
				UGameplayStatics::SetGlobalTimeDilation(this, 0.02f);
				JustPressed[(int32)EWWOAction::Fire] = Held[(int32)EWWOAction::Fire] = true;
				const FVector Side = P->EyeLocation() + P->AimForward() * 350.f - P->GetActorRightVector() * 150.f;
				AWWOFXActor::MuzzleFlash(GetWorld(), Side, P->GetActorRightVector(), FLinearColor(1.f, 0.8f, 0.45f));
				AWWOFXActor::BulletTrail(GetWorld(), Side, Side + P->GetActorRightVector() * 3000.f, FLinearColor(1.f, 0.8f, 0.45f));
				// A body hit, a headshot, a death and a soldier's bolt, in a row in front of you.
				const FVector Ahead = P->EyeLocation() + P->AimForward() * 400.f;
				const FVector Right = P->GetActorRightVector();
				AWWOFXActor::HitSpark(GetWorld(), Ahead - Right * 120.f, -P->AimForward(), false);
				AWWOFXActor::HitSpark(GetWorld(), Ahead - Right * 40.f, -P->AimForward(), true);
				AWWOFXActor::DeathBurst(GetWorld(), Ahead + Right * 110.f + P->AimForward() * 200.f - FVector(0, 0, 60.f), FLinearColor(1.f, 0.55f, 0.2f));
				AWWOEnemyProjectile::Fire(GetWorld(), Ahead - Right * 200.f + FVector(0, 0, 60.f), Right * 5300.f, 0.f, 0.5f);
				// The mark a blast leaves.
				const FVector Floor = P->GetActorLocation() - FVector(0, 0, P->GetSimpleCollisionHalfHeight()) + P->AimForward().GetSafeNormal2D() * 450.f;
				AWWOFXActor::Scorch(GetWorld(), Floor + Right * 250.f, 2.5f);
			}
			break;
		case 2: Held[(int32)EWWOAction::Aim] = true; break;
		case 3: Shot(*(Tag + TEXT("_ads"))); break;
		case 4: Held[(int32)EWWOAction::Aim] = false; if (bModded) EquipSet(PlainSet); else AutoGunStage = 99; break;
		case 5: Shot(*(Tag + TEXT("_plain"))); break;
		case 6: EquipSet(PerkSet); break;
		case 7: Shot(*(Tag + TEXT("_perk"))); break;
		case 8: Held[(int32)EWWOAction::Aim] = true; break;
		case 9: Shot(*(Tag + TEXT("_perk_ads"))); break;
		case 10:
			// The rifle again with the sight height slider at 3 cm: the gun drops below the dot.
			if (Id == "rifle") { Gun->GetBaseWeaponData()->SightHeight = 3.f; break; }
			Held[(int32)EWWOAction::Aim] = false;
			// The hand cannon with a diagonal kick (vertical 8, horizontal 4), shot from the hip.
			if (Id == "handcannon")
			{
				AutoGunKick = FVector2D(Gun->GetBaseWeaponData()->VerticalKick, Gun->GetBaseWeaponData()->HorizontalKick);
				Gun->GetBaseWeaponData()->VerticalKick = 8.f;
				Gun->GetBaseWeaponData()->HorizontalKick = 4.f;
				break;
			}
			AutoGunStage = 99; break;
		case 11:
			if (Id == "handcannon")
			{
				JustPressed[(int32)EWWOAction::Fire] = true;
				Shot(*(Tag + TEXT("_kick_diagonal"))); // captured as the gun jolts
				break;
			}
			Shot(*(Tag + TEXT("_perk_ads_height3"))); break;
		case 12:
			if (Id == "handcannon")
			{
				Gun->GetBaseWeaponData()->VerticalKick = AutoGunKick.X;
				Gun->GetBaseWeaponData()->HorizontalKick = AutoGunKick.Y;
			}
			else Gun->GetBaseWeaponData()->SightHeight = 0.f;
			Held[(int32)EWWOAction::Aim] = false; AutoGunStage = 99; break;
		}
		if (AutoGunStage >= 99)
		{
			for (const FName& Slot : UWWOLoadoutSubsystem::ModSlots()) L->EquipMod(Slot, nullptr);
			++AutoGunIdx;
			AutoGunStage = 0;
		}
		break;
	}
	// -WWOAutoTestFrames: the shotgun, sniper and burst rifle frames. Each one's plain mods change
	// its stats by the mod data's percentages; then the burst rifle's bursts are counted (three
	// rounds a pull, four with Fourth Round, and how many go out in 2 s of holding the trigger).
	case 70:
	case 71:
	case 72:
	case 73:
	case 74:
	case 75:
	case 76:
	case 77:
	case 78:
	{
		static const FName Frames[] = { "shotgun", "sniper", "burstrifle" };
		static int32 AmmoBefore = 0;
		if (AutoGunIdx >= (int32)UE_ARRAY_COUNT(Frames)) { Held[(int32)EWWOAction::Fire] = false; Step(8); break; }
		const FName Id = Frames[AutoGunIdx];
		FName PlainSet[6];
		int32 PlainCount = 0, PerkCount = 0;
		for (const UWWOModData* Mod : L->WeaponMods)
		{
			if (!Mod || Mod->Frame != Id) continue;
			if (Mod->Perk.IsNone() && PlainCount < 6) PlainSet[PlainCount++] = Mod->Id;
			else if (!Mod->Perk.IsNone()) ++PerkCount;
		}
		auto Bare = [&] { for (const FName& Slot : UWWOLoadoutSubsystem::ModSlots()) L->EquipMod(Slot, nullptr); };
		const UWWOWeaponData* W = Gun->GetWeaponData();
		switch (AutoModStep)
		{
		case 70:
			EquipWeaponId(Id);
			Bare();
			Step(71);
			break;
		case 71:
			if (AutoModClock < 0.6f) break;
			Check(L->HasMods(L->GetWeapon()) && PlainCount == 6 && PerkCount == 6, FString::Printf(TEXT("%s is a frame: %d plain mods, %d with perks"), *Id.ToString(), PlainCount, PerkCount));
			AutoModBase = { W->Damage, W->FireRate, W->MagazineSize, W->FireRange, W->RecoilUp, W->RecoilSide, W->Zoom, W->Sway, W->AdsMoveSpeed, W->CritMultiplier, W->PelletSpread };
			for (int32 i = 0; i < PlainCount; ++i) if (UWWOModData* Mod = ModById(L, PlainSet[i])) L->EquipMod(Mod->Slot, Mod);
			Step(72);
			break;
		case 72:
		{
			if (AutoModClock < 0.4f) break;
			const TArray<float>& B = AutoModBase;
			auto Pct = [&](FName Stat) { return FMath::Max(1.f + Sum(PlainSet, PlainCount, Stat), 0.f); };
			const bool bOk = Near(W->Damage, B[0] * Pct("damage")) && Near(W->FireRate, B[1] * Pct("fire_rate"))
				&& Near(W->MagazineSize, FMath::Max(1.f, FMath::RoundToFloat(B[2] * Pct("magazine_size")))) && Near(W->FireRange, B[3] * Pct("range"))
				&& Near(W->RecoilUp, B[4] * Pct("recoil_up")) && Near(W->RecoilSide, B[5] * Pct("recoil_side")) && Near(W->Zoom, 1.f + (B[6] - 1.f) * Pct("zoom"))
				&& Near(W->Sway, B[7] * Pct("sway")) && Near(W->AdsMoveSpeed, B[8] * Pct("ads_move_speed")) && Near(W->CritMultiplier, B[9] * Pct("crit_multiplier"))
				&& Near(W->PelletSpread, B[10] * Pct("pellet_spread"));
			Check(bOk, FString::Printf(TEXT("%s plain mods: damage %.1f -> %.1f, rate %.0f -> %.0f, magazine %.0f -> %.0f, range %.0f -> %.0f, recoil %.2f -> %.2f, zoom %.2f -> %.2f, crit %.2f -> %.2f, spread %.2f -> %.2f"),
				*Id.ToString(), B[0], W->Damage, B[1], W->FireRate, B[2], W->MagazineSize, B[3], W->FireRange, B[4], W->RecoilUp, B[6], W->Zoom, B[9], W->CritMultiplier, B[10], W->PelletSpread));
			Check(Gun->Ammo == Gun->MagazineSize(), FString::Printf(TEXT("%s magazine refilled to %d"), *Id.ToString(), Gun->Ammo));
			Bare();
			if (Id == "burstrifle") Step(73);
			else { ++AutoGunIdx; Step(70); }
			break;
		}
		case 73:
			// One pull of the trigger: a whole burst, though the trigger is let go at once.
			if (AutoModClock < 0.8f) break;
			Check(W->BurstCount() == 3 && !W->IsSemiAuto(), FString::Printf(TEXT("burst rifle fires %d-round bursts, %.0f a minute at %.0f rpm inside one, range %.0f m"), W->BurstCount(), W->FireRate, W->BurstRate, W->FireRange));
			AmmoBefore = Gun->Ammo;
			Held[(int32)EWWOAction::Fire] = true;
			Step(74);
			break;
		case 74:
			if (AutoModClock > 0.03f) Held[(int32)EWWOAction::Fire] = false;
			if (AutoModClock < 0.35f) break;
			Check(AmmoBefore - Gun->Ammo == 3, FString::Printf(TEXT("one pull fired %d rounds (3)"), AmmoBefore - Gun->Ammo));
			if (UWWOModData* Mod = ModById(L, "fourth_round")) L->EquipMod(Mod->Slot, Mod);
			Step(75);
			break;
		case 75:
			if (AutoModClock < 0.8f) break;
			AmmoBefore = Gun->Ammo;
			Held[(int32)EWWOAction::Fire] = true;
			Step(76);
			break;
		case 76:
			if (AutoModClock > 0.03f) Held[(int32)EWWOAction::Fire] = false;
			if (AutoModClock < 0.4f) break;
			Check(AmmoBefore - Gun->Ammo == 4, FString::Printf(TEXT("with Fourth Round one pull fired %d rounds (4)"), AmmoBefore - Gun->Ammo));
			Bare();
			Step(77);
			break;
		case 77:
			if (AutoModClock < 0.8f) break;
			AmmoBefore = Gun->Ammo;
			Held[(int32)EWWOAction::Fire] = true; // held: a burst every 60 / fire rate seconds
			Step(78);
			break;
		default:
			if (AutoModClock < 1.9f) break; // bursts start at 0, 0.4, 0.8, 1.2 and 1.6 s
			Held[(int32)EWWOAction::Fire] = false;
			{
				const int32 Fired = AmmoBefore - Gun->Ammo;
				Check(Fired == 15, FString::Printf(TEXT("1.9 s of holding the trigger fired %d rounds (5 bursts of 3)"), Fired));
			}
			++AutoGunIdx;
			Step(70);
			break;
		}
		break;
	}
	case 60:
	case 62:
	{
		// -WWOAutoTestHidden: the rifle on a controller, aimed 2 degrees right of a target's head,
		// fires a burst; once with the hidden assist off (60), once on (62). Logs how far off the
		// aim ends up after the recoil settles and how many hits landed.
		const bool bOn = AutoModStep == 62;
		if (AutoModClock <= DeltaTime * 1.5f)
		{
			bUsingController = true;
			P->AimAssist->HiddenAssist = bOn ? 1.f : 0.f;
			P->AimAssist->ShowHiddenAssist = 1.f;
			if (!bOn)
			{
				EquipWeaponId("rifle");
				AActor* Best = nullptr;
				float BestD = 1e9f;
				for (AActor* A : UWWOWorldSubsystem::Get(this)->LivingTargets())
				{
					const float D = FVector::Dist(A->GetActorLocation(), P->GetActorLocation());
					if (D < BestD && Cast<IWWOTargetable>(A)->FeetLocation().Z < 100.f) { BestD = D; Best = A; }
				}
				AutoHiddenTarget = Best;
				if (Best) PlaceForAutoTest(P, Best, 10.f, 0.f);
			}
		}
		AActor* T = AutoHiddenTarget.Get();
		const IWWOTargetable* Target = Cast<IWWOTargetable>(T);
		if (!Target)
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest hidden: no target"));
			Step(8);
			break;
		}
		const FVector Head = Target->GetAimParts()[0].Position;
		if (AutoModClock < 0.4f)
		{
			// Aim 2 degrees right of the head (and keep the gun loaded).
			const FVector Dir = (Head - P->EyeLocation()).GetSafeNormal();
			P->SetViewRotation(FMath::Asin(Dir.Z), Dir.Rotation().Yaw + 2.f);
			Gun->Ammo = Gun->MagazineSize();
		}
		const bool bFire = AutoModClock > 0.5f && AutoModClock < 1.1f;
		Held[(int32)EWWOAction::Fire] = bFire;
		if (bOn && FMath::IsNearlyEqual(AutoModClock, 0.9f, DeltaTime * 0.6f)) Shot(TEXT("39_hidden_assist_markers"));
		if (AutoModClock >= 2.f)
		{
			Held[(int32)EWWOAction::Fire] = false;
			const FVector Dir = (Head - P->EyeLocation()).GetSafeNormal();
			const float Off = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(P->AimForward(), Dir), -1.f, 1.f)));
			const FVector Aim = P->AimForward();
			const float Side = FMath::FindDeltaAngleDegrees(Dir.Rotation().Yaw, Aim.Rotation().Yaw);
			const float Up = FMath::RadiansToDegrees(FMath::Asin(Aim.Z) - FMath::Asin(Dir.Z));
			UE_LOG(LogWWO, Display, TEXT("AutoTest hidden: assist %s, aim ends %.2f deg from the head (%.2f sideways, %.2f above), target health %.0f / %.0f"),
				bOn ? TEXT("on") : TEXT("off"), Off, Side, Up, Target->GetHealth(), Target->GetMaxHealth());
			if (!bOn) { AutoHiddenOff = Off; Step(61); }
			else
			{
				UE_LOG(LogWWO, Display, TEXT("AutoTest hidden: on is %s (%.2f vs %.2f deg)"), Off < AutoHiddenOff ? TEXT("closer ok") : TEXT("not closer FAIL"), Off, AutoHiddenOff);
				P->AimAssist->ShowHiddenAssist = 0.f;
				bUsingController = false;
				Step(63);
			}
		}
		break;
	}
	case 61:
		if (AutoModClock < 1.f) break; // let the target recover between runs
		Step(62);
		break;
	case 63:
	{
		// Readability: aimed straight at the target's chest (its tint and health bar, and the bars of
		// enemies near the reticle), then a short burst (damage numbers off to the side).
		if (AutoModClock <= DeltaTime * 1.5f)
		{
			// The nearest enemy with a body (the tint goes on bodies), 6 m away.
			AActor* Best = nullptr;
			float BestD = 1e9f;
			for (AActor* A : UWWOWorldSubsystem::Get(this)->LivingTargets())
			{
				const float D = FVector::Dist(A->GetActorLocation(), P->GetActorLocation());
				if (D < BestD && A->FindComponentByClass<UWWOBodyComponent>()) { BestD = D; Best = A; }
			}
			AutoHiddenTarget = Best;
			if (Best) PlaceForAutoTest(P, Best, 6.f, 0.f);
		}
		const IWWOTargetable* Target = Cast<IWWOTargetable>(AutoHiddenTarget.Get());
		if (!Target) { Step(8); break; }
		const FVector Chest = Target->FeetLocation() + FVector(0, 0, Target->GetBodyHeight() * 0.6f);
		const FVector Dir = (Chest - P->EyeLocation()).GetSafeNormal();
		if (AutoModClock < 0.6f) P->SetViewRotation(FMath::Asin(Dir.Z), Dir.Rotation().Yaw);
		if (FMath::IsNearlyEqual(AutoModClock, 0.7f, DeltaTime * 0.6f)) Shot(TEXT("40_readability_hover"));
		Held[(int32)EWWOAction::Fire] = AutoModClock > 0.9f && AutoModClock < 1.15f;
		if (FMath::IsNearlyEqual(AutoModClock, 1.3f, DeltaTime * 0.6f)) Shot(TEXT("40_readability_numbers"));
		if (AutoModClock >= 1.6f)
		{
			Held[(int32)EWWOAction::Fire] = false;
			Step(8);
		}
		break;
	}
	case 23:
		if (AutoModClock < 1.f) break;
		CharacterMenu->AutoTestScroll(-1.f);
		Step(24);
		break;
	case 24:
		if (AutoModClock < 0.5f) break;
		Shot(TEXT("37_mods_cannon_preview"));
		Step(25);
		break;
	case 25:
		if (AutoModClock < 0.3f) break;
		CharacterMenu->AutoTestShow(TEXT("Weapons"), FString());
		EquipWeaponId("rifle");
		Step(8);
		break;
	case 8:
		if (AutoModClock < 0.5f) break;
		if (CharacterMenu->IsOpen()) ToggleCharacterMenu();
		// Put the real save back (it's reloaded on the next launch).
		if (IFileManager::Get().FileExists(*BackupPath()))
		{
			IFileManager::Get().Copy(*SavePath(), *BackupPath());
			IFileManager::Get().Delete(*BackupPath());
		}
		else
		{
			IFileManager::Get().Delete(*SavePath());
		}
		UE_LOG(LogWWO, Display, TEXT("AutoTest mods: done (saved loadout restored)"));
		P->Health->GodMode = 0.f;
		AutoModStep = 0;
		AutoModClock = 0.f;
		return true;
	}
	return false;
}
