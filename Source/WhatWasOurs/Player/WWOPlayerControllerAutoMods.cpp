// Weapon mod steps of the autotest (-WWOAutoTestMods, and the end of -WWOAutoTest): the draft
// Assault frame mods. Checks that each plain mod changes the rifle's stats by its percentages,
// that a saved config comes back when applied (and by the quick-swap cycle), fires the perk
// config at a target and logs the perk counters, then screenshots the Weapons tile and a slot
// picker. The saved loadout is backed up first and put back at the end.
// Logs `AutoTest mods: ... ok` or `... FAIL`, with screenshots 37_mods_*.

#include "Player/WWOPlayerController.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOHealthComponent.h"
#include "UI/WWOCharacterMenu.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "Data/WWOData.h"
#include "WhatWasOurs.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

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

	switch (AutoModStep)
	{
	case 0:
	{
		// Back up the real save, then start from a bare rifle.
		IFileManager::Get().Delete(*BackupPath());
		if (IFileManager::Get().FileExists(*SavePath())) IFileManager::Get().Copy(*BackupPath(), *SavePath());
		P->Health->GodMode = 1.f;
		Check(L->WeaponMods.Num() == 12, FString::Printf(TEXT("%d mods loaded"), L->WeaponMods.Num()));
		for (UWWOWeaponData* W : L->Weapons)
		{
			if (W->Id == "rifle") L->EquipWeapon(W);
		}
		for (const FName& Slot : UWWOLoadoutSubsystem::ModSlots()) L->EquipMod(Slot, nullptr);
		Step(1);
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
		Held[(int32)EWWOAction::Fire] = AutoModClock > 0.5f && AutoModClock < 3.5f;
		if (FMath::IsNearlyEqual(AutoModClock, 2.5f, DeltaTime * 0.6f)) Shot(TEXT("37_mods_perk_firing"));
		if (AutoModClock >= 3.6f)
		{
			Held[(int32)EWWOAction::Fire] = false;
			UE_LOG(LogWWO, Display, TEXT("AutoTest mods: perk HUD after firing '%s', ammo %d / %d"), *Gun->PerkStatus(), Gun->Ammo, Gun->MagazineSize());
			Check(!Gun->PerkStatus().IsEmpty(), TEXT("perk status shows on the HUD"));
			if (!CharacterMenu->IsOpen()) ToggleCharacterMenu();
			CharacterMenu->AutoTestShow(TEXT("Weapons"), FString());
			Step(4);
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
		CharacterMenu->AutoTestShow(TEXT("Weapons"), TEXT("mod_barrel"));
		Step(6);
		break;
	case 6:
		if (AutoModClock < 0.5f) break;
		CharacterMenu->AutoTestScroll(-1.f);
		Step(7);
		break;
	case 7:
		if (AutoModClock < 0.5f) break;
		Shot(TEXT("37_mods_barrel_picker"));
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
