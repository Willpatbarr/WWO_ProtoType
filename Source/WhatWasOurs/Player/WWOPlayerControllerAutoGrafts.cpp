// -WWOAutoTest: the flora graft steps (also alone with -WWOAutoTestGrafts). Six loadouts, two per
// flora, each with frame parts that color the body's regions, equipped in turn with the character
// menu open: the preview from the front and from behind (34_grafts_*), then again with the
// preview's camera moved in close (35_grafts_*). The loadout is put back
// afterwards. Logs `AutoTest grafts:` with what each body wears.

#include "Player/WWOPlayerController.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
#include "Characters/WWOBodyComponent.h"
#include "UI/WWOCharacterMenu.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WhatWasOurs.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"

namespace
{
	struct FGraftLoadout
	{
		const TCHAR* Name;
		FName Flora, Grenade, Ability0, Ability1;
		TArray<FName> Frame;
	};
	const FGraftLoadout GraftLoadouts[] = {
		{ TEXT("volt_charge_thorn"), "voltvine", "volt_grenade", "static_charge", "thornfist", { "strong_arms", "spring_legs" } },
		{ TEXT("volt_growth_guard"), "voltvine", "snare_mine", "overgrowth", "melee_guard", { "scout_head", "thruster_core" } },
		{ TEXT("spore_fog_dash"), "veilspore", "spore_blast", "spore_fog_damage", "spore_dash", { "standard_head", "standard_arms", "standard_core", "standard_legs" } },
		{ TEXT("spore_patches_fog"), "veilspore", "spore_pool", "spore_patches", "spore_fog_damage", { "focus_head", "light_core" } },
		{ TEXT("bark_shield_roots"), "ironbark", "iron_grenade", "shield_share", "root_bombs", { "armored_core", "sturdy_legs", "standard_head", "standard_arms" } },
		{ TEXT("bark_extra_seeds"), "ironbark", "wall_grenade", "extra_grenade", "seedburst", { "quick_arms", "guard_head" } },
	};
	constexpr int32 NumGraftLoadouts = UE_ARRAY_COUNT(GraftLoadouts);
	constexpr float MenuEvery = 2.f, CloseEvery = 1.6f;

	template <typename T>
	T* ById(const TArray<TObjectPtr<T>>& List, FName Id)
	{
		for (T* Item : List) if (Item && Item->Id == Id) return Item;
		return nullptr;
	}
}

bool AWWOPlayerController::UpdateAutoGraftTest(AWWOCharacter* P, float DeltaTime)
{
	UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	if (!Loadout || !CharacterMenu) return true;
	const float Before = AutoGraftClock;
	// The saved loadout is copied aside first and put back at the end, so a run that stops half way
	// can't leave a test loadout as yours (the next run puts a left-over copy back before it starts).
	const FString SaveFile = FPaths::ProjectSavedDir() / TEXT("SaveGames/Loadout.sav"), Backup = FPaths::ProjectSavedDir() / TEXT("SaveGames/Loadout.grafts-backup");
	if (Before <= 0.f)
	{
		if (IFileManager::Get().FileExists(*Backup)) IFileManager::Get().Copy(*SaveFile, *Backup);
		else if (IFileManager::Get().FileExists(*SaveFile)) IFileManager::Get().Copy(*Backup, *SaveFile);
	}
	AutoGraftClock += DeltaTime;
	auto Crossed = [&](float T) { return Before < T && AutoGraftClock >= T; };
	auto Equip = [&](const FGraftLoadout& L)
	{
		if (UWWOFloraData* F = ById(Loadout->Floras, L.Flora)) Loadout->EquipFlora(F);
		if (UWWOGrenadeData* G = ById(Loadout->Grenades, L.Grenade)) Loadout->EquipGrenade(G);
		if (UWWOAbilityData* A = ById(Loadout->Abilities, L.Ability0)) Loadout->EquipAbility(0, A);
		if (UWWOAbilityData* A = ById(Loadout->Abilities, L.Ability1)) Loadout->EquipAbility(1, A);
		for (const FName& Part : L.Frame) if (UWWOFramePartData* FP = ById(Loadout->FrameParts, Part)) Loadout->EquipFramePart(FP);
		const AWWOPreviewActor* Preview = CharacterMenu->GetPreviewActor();
		int32 Parts = 0;
		if (Preview) for (UActorComponent* C : Preview->GetInstanceComponents()) Parts += C && C->IsRegistered() ? 1 : 0;
		UE_LOG(LogWWO, Display, TEXT("AutoTest grafts: %s (%s, %s, %s + %s) preview parts %d"), L.Name, *L.Flora.ToString(), *L.Grenade.ToString(),
			*L.Ability0.ToString(), *L.Ability1.ToString(), Parts);
	};
	auto Turn = [&](float Yaw)
	{
		if (AWWOPreviewActor* Preview = CharacterMenu->GetPreviewActor()) Preview->Turntable->SetRelativeRotation(FRotator(0.f, Yaw, 0.f));
	};

	if (Before == 0.f)
	{
		P->Health->GodMode = 1.f;
		AutoGraftFlora = Loadout->GetFlora();
		AutoGraftGrenade = Loadout->GetGrenade();
		AutoGraftAbilities.Reset();
		for (int32 Slot = 0; Slot < UWWOLoadoutSubsystem::AbilitySlots; ++Slot) AutoGraftAbilities.Add(Loadout->GetAbility(Slot));
		AutoGraftFrame.Reset();
		for (const FName& Slot : UWWOLoadoutSubsystem::FrameSlots()) AutoGraftFrame.Add(Loadout->GetFramePart(Slot));
		if (!CharacterMenu->IsOpen()) ToggleCharacterMenu();
	}
	// The menu preview: front, then from behind.
	for (int32 k = 0; k < NumGraftLoadouts; ++k)
	{
		const float At = 0.5f + k * MenuEvery;
		if (Crossed(At)) Equip(GraftLoadouts[k]);
		if (Crossed(At + 0.8f)) { Turn(0.f); Shot(*FString::Printf(TEXT("34_grafts_%d_%s_front"), k, GraftLoadouts[k].Name)); }
		if (Crossed(At + 1.4f)) { Turn(200.f); Shot(*FString::Printf(TEXT("34_grafts_%d_%s_back"), k, GraftLoadouts[k].Name)); }
	}
	// Up close: the preview's own camera moved in (its world has no fog or sky, like Godot's).
	const float CloseAt = 0.5f + NumGraftLoadouts * MenuEvery;
	auto Zoom = [&](bool bIn)
	{
		AWWOPreviewActor* Preview = CharacterMenu->GetPreviewActor();
		if (!Preview) return;
		if (bIn)
		{
			AutoGraftCaptureFov = Preview->Capture->FOVAngle;
			Preview->Capture->SetRelativeLocation(FVector(300.f, 0.f, 105.f));
			Preview->Capture->FOVAngle = 34.f;
		}
		else
		{
			Preview->Capture->SetRelativeLocation(FVector(440.f, 0.f, 110.f));
			Preview->Capture->FOVAngle = AutoGraftCaptureFov;
		}
	};
	if (Crossed(CloseAt)) Zoom(true);
	for (int32 k = 0; k < NumGraftLoadouts; ++k)
	{
		const float At = CloseAt + 0.5f + k * CloseEvery;
		if (Crossed(At)) Equip(GraftLoadouts[k]);
		if (Crossed(At + 0.7f)) { Turn(-35.f); Shot(*FString::Printf(TEXT("35_grafts_%d_%s_close"), k, GraftLoadouts[k].Name)); }
		if (Crossed(At + 1.2f)) { Turn(150.f); Shot(*FString::Printf(TEXT("35_grafts_%d_%s_close_back"), k, GraftLoadouts[k].Name)); }
	}
	const float EndAt = CloseAt + 0.5f + NumGraftLoadouts * CloseEvery + 0.3f;
	if (AutoGraftClock < EndAt) return false;
	// Put the loadout back.
	if (AutoGraftFlora && Loadout->GetFlora() != AutoGraftFlora) Loadout->EquipFlora(AutoGraftFlora);
	if (AutoGraftGrenade) Loadout->EquipGrenade(AutoGraftGrenade);
	for (int32 Slot = 0; Slot < AutoGraftAbilities.Num(); ++Slot) if (AutoGraftAbilities[Slot]) Loadout->EquipAbility(Slot, AutoGraftAbilities[Slot]);
	for (UWWOFramePartData* Part : AutoGraftFrame) if (Part) Loadout->EquipFramePart(Part);
	Zoom(false);
	if (CharacterMenu->IsOpen()) ToggleCharacterMenu();
	Loadout->FlushSave();
	if (IFileManager::Get().FileExists(*Backup)) { IFileManager::Get().Copy(*SaveFile, *Backup); IFileManager::Get().Delete(*Backup); }
	UE_LOG(LogWWO, Display, TEXT("AutoTest grafts: loadout restored (%s)"), Loadout->GetFlora() ? *Loadout->GetFlora()->Id.ToString() : TEXT("none"));
	return true;
}
