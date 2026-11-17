// The regression scenario runner (-WWORegress): the scenario loop, isolation between scenarios, the
// helpers scenarios use, and the results and golden files. The scenarios are in WWORegress<Family>.cpp.

#include "Player/WWORegress.h"
#include "WhatWasOurs.h"
#include "WWOSwitches.h"
#include "Player/WWOTestRun.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOBotController.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOAbilityComponent.h"
#include "Player/WWOGrenadeComponent.h"
#include "Player/WWOUltComponent.h"
#include "Player/WWOCharacterMovement.h"
#include "Player/WWOCharacterPrivate.h"
#include "Game/WWOGameMode.h"
#include "Game/WWOGameState.h"
#include "Game/WWOPlayerState.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "Flora/WWOFloraEffects.h"
#include "Enemies/WWOEnemy.h"
#include "Enemies/WWOEnemyTypes.h"
#include "Allies/WWOAlly.h"
#include "World/WWOTarget.h"
#include "UI/WWOHUDWidget.h"
#include "UI/WWOSettingsMenu.h"
#include "Data/WWOData.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "RHI.h"
#include "UObject/UnrealType.h"
#include "Algo/Reverse.h"

namespace
{
	constexpr int32 BootTicks = 90;   // after the player and settings exist, before the first scenario
	constexpr int32 SettleTicks = 12; // after a scenario's setup: bodies on the floor, PvP hitboxes on (the gun's swap is skipped)
	constexpr int32 GapTicks = 3;     // after a teardown: destroyed actors are gone

	FString GoldenPath() { return FPaths::ProjectDir() / TEXT("Tools/regress/golden.json"); }
	FString ResultsDir() { return FPaths::ProjectSavedDir() / TEXT("Regress"); }
	FString LoadoutFile() { return FPaths::ProjectSavedDir() / TEXT("SaveGames/Loadout.sav"); }

	// Ours: an actor of this game's own classes (the engine's own runtime actors are never touched).
	bool IsOurs(const AActor* A)
	{
		const UPackage* P = A ? A->GetClass()->GetOuterUPackage() : nullptr;
		return P && P->GetName() == TEXT("/Script/WhatWasOurs");
	}

	TArray<uint8> GLoadoutAtStart; // the saved loadout's bytes when the run began
	bool GLoadoutExisted = false;
}

// Everything the loadout subsystem holds about what is equipped (put back when the run ends, so the
// owner's loadout is in memory again and nothing is due to be written).
struct FWWORegress::FLoadoutSnapshot
{
	TObjectPtr<UWWOWeaponData> Weapon = nullptr;
	TObjectPtr<UWWOGrenadeData> Grenade = nullptr;
	TObjectPtr<UWWOFloraData> Flora = nullptr;
	TArray<TObjectPtr<UWWOAbilityData>> Abilities;
	TArray<TObjectPtr<UWWOTraitData>> Traits;
	TMap<FName, TObjectPtr<UWWOFramePartData>> Frame;
	TMap<FName, TMap<FName, TObjectPtr<UWWOModData>>> Mods;
	int32 ActiveConfig = INDEX_NONE;
	TArray<int32> SlotConfigs;
	int32 ActiveSlot = 0;
	bool bSaveDue = false;
};

FWWORegress::FWWORegress(AWWOPlayerController& Controller)
	: PC(&Controller)
{
	// A fixed step: every frame is 1/60 s of game time however long it takes to draw (or not).
	FApp::SetUseFixedTimeStep(true);
	FApp::SetFixedDeltaTime(Dt);
	bUpdate = WWOSwitches::Has(TEXT("WWORegressUpdate"));
	bRender = !GUsingNullRHI;
	IFileManager::Get().MakeDirectory(*ResultsDir(), true);
	GLoadoutExisted = FFileHelper::LoadFileToArray(GLoadoutAtStart, *LoadoutFile(), FILEREAD_Silent);
	UE_LOG(LogWWO, Display, TEXT("Regress: starting (%s mode, fixed step %.4f s%s)"), *Mode(), Dt, bUpdate ? TEXT(", updating golden") : TEXT(""));
}

void FWWORegress::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (SavedLoadout.IsValid())
	{
		Collector.AddReferencedObject(SavedLoadout->Weapon);
		Collector.AddReferencedObject(SavedLoadout->Grenade);
		Collector.AddReferencedObject(SavedLoadout->Flora);
		Collector.AddReferencedObjects(SavedLoadout->Abilities);
		Collector.AddReferencedObjects(SavedLoadout->Traits);
		Collector.AddReferencedObjects(SavedLoadout->Frame);
		for (auto& Pair : SavedLoadout->Mods) Collector.AddReferencedObjects(Pair.Value);
	}
}

AWWOCharacter* FWWORegress::Player() const { return PC ? PC->GetWWOCharacter() : nullptr; }
UWorld* FWWORegress::World() const { return PC ? PC->GetWorld() : nullptr; }
UWWOLoadoutSubsystem* FWWORegress::Loadout() const
{
	const UGameInstance* GI = PC ? PC->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UWWOLoadoutSubsystem>() : nullptr;
}

FVector FWWORegress::Ahead(float Meters, float SideMeters) const
{
	const FRotator Yaw(0.f, HomeYawDeg, 0.f);
	return HomeFeet + Yaw.Vector() * Meters * WWO::M + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y) * SideMeters * WWO::M;
}

uint32 FWWORegress::SeedFor(const FString& Name)
{
	// FNV-1a over the name: the same seed for a scenario whatever runs before it.
	uint32 H = 2166136261u;
	for (TCHAR C : Name) { H ^= (uint32)C; H *= 16777619u; }
	return H & 0x7fffffff;
}

FString FWWORegress::GoldenKey(const FWWORegressScenario& S) const
{
	return S.Modes == EWWORegressModes::PerMode ? S.Name + TEXT("@") + Mode() : S.Name;
}

// --- The loop ----------------------------------------------------------------------------------

void FWWORegress::Tick()
{
	AWWOCharacter* P = Player();
	++PhaseTicks;
	switch (Phase)
	{
	case EPhase::Boot:
	{
		const UWWOSettingsMenu* Settings = PC->GetSettingsMenu();
		if (!P || !Settings || !Settings->IsPrepared()) { PhaseTicks = 0; return; }
		if (PhaseTicks == 1)
		{
			// No real input reaches the game (a mouse moved over the window would turn the view), the
			// HUD doesn't draw, and the level's own dummies, enemies and allies go.
			if (UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer())) Input->ClearAllMappings();
			if (UWWOHUDWidget* HUD = PC->GetHUDWidget()) HUD->SetVisibility(ESlateVisibility::Collapsed);
			for (TActorIterator<AWWOEnemy> It(World()); It; ++It) It->Destroy();
			for (TActorIterator<AWWOAlly> It(World()); It; ++It) It->Destroy();
			for (TActorIterator<AWWOTarget> It(World()); It; ++It) It->Destroy();
			// Home: an empty stretch of the range's floor, south of everything on it, looking along it.
			FVector At(-2000.f, -3500.f, 300.f);
			FHitResult Floor;
			if (World()->LineTraceSingleByChannel(Floor, At, At - FVector(0, 0, 2000.f), ECC_Visibility)) At = Floor.ImpactPoint;
			HomeFeet = FVector(At.X, At.Y, At.Z);
			HomeYawDeg = 90.f;
			if (const AWWOPlayerState* PS = P->GetPlayerState<AWWOPlayerState>()) PlayerTeamBefore = PS->Team;
			SnapshotLoadout();
			ResetPlayer();
			P->Weapon->OnHitConfirmed.AddLambda([this](bool bCritical, bool bKilled)
			{
				++Confirms;
				ConfirmCrits += bCritical ? 1 : 0;
				ConfirmKills += bKilled ? 1 : 0;
			});
		}
		if (PhaseTicks < BootTicks) return;
		for (TActorIterator<AActor> It(World()); It; ++It) Baseline.Add(*It);
		BuildScenarios();
		if (WWOSwitches::Has(TEXT("WWORegressReverse"))) Algo::Reverse(Scenarios);
		StartedAt = FPlatformTime::Seconds();
		UE_LOG(LogWWO, Display, TEXT("Regress: %d scenarios, home %s"), Scenarios.Num(), *HomeFeet.ToCompactString());
		Index = -1;
		Phase = EPhase::Gap;
		PhaseTicks = GapTicks;
		return;
	}
	case EPhase::Gap:
		if (PhaseTicks < GapTicks) return;
		++Index;
		while (Scenarios.IsValidIndex(Index) && !bRender && Scenarios[Index].Modes == EWWORegressModes::RenderOnly)
		{
			UE_LOG(LogWWO, Display, TEXT("Regress %s: skipped (render-only: %s)"), *Scenarios[Index].Name, *Scenarios[Index].ModeReason);
			++Skipped;
			++Index;
		}
		if (!Scenarios.IsValidIndex(Index))
		{
			Finish();
			Phase = EPhase::Done;
			return;
		}
		BeginScenario();
		Phase = EPhase::Settle;
		PhaseTicks = 0;
		return;
	case EPhase::Settle:
		if (PhaseTicks < SettleTicks) return;
		// The scenario proper: the same random numbers and the same gun state every time.
		FMath::RandInit(SeedFor(Scenarios[Index].Name));
		FMath::SRandInit(SeedFor(Scenarios[Index].Name));
		WWO::SeedGameRandom(SeedFor(Scenarios[Index].Name));
		if (P)
		{
			// The camera's eased values, exactly at rest (a landing during the settle ticks leaves a
			// dip that only ever approaches zero).
			if (!P->bIsCrouched) P->CameraBaseZ = WWOCharacter::StandCameraZ;
			P->LandOffset = P->LandKick = P->BobWeight = P->BobTime = 0.f;
			P->Camera->SetRelativeLocation(FVector(0.f, 0.f, P->CameraBaseZ));
		}
		if (P && P->Weapon)
		{
			P->Weapon->ResetForRespawn();
			P->Weapon->PerkState.AimHeld = 0.f;
		}
		NotePlayerShot();
		Confirms = ConfirmCrits = ConfirmKills = 0;
		StepTick = 0;
		Phase = EPhase::Run;
		[[fallthrough]];
	case EPhase::Run:
	{
		const FWWORegressScenario& S = Scenarios[Index];
		const bool bDone = S.Step(*this, StepTick);
		++StepTick;
		if (bDone || StepTick > S.MaxTicks)
		{
			EndScenario(!bDone);
			Teardown();
			Phase = EPhase::Gap;
			PhaseTicks = 0;
		}
		return;
	}
	default:
		return;
	}
}

void FWWORegress::BeginScenario()
{
	const FWWORegressScenario& S = Scenarios[Index];
	Current.Reset();
	FMath::RandInit(SeedFor(S.Name));
	FMath::SRandInit(SeedFor(S.Name));
	WWO::SeedGameRandom(SeedFor(S.Name));
	if (AWWOGameState* GS = World()->GetGameState<AWWOGameState>()) GS->bPvP = S.bPvP;
	SetPlayerTeam(0);
	ResetPlayer();
	BaselineLoadout();
	if (S.Setup) S.Setup(*this);
	// Whatever the setup equipped, the gun starts as a first pickup of it.
	if (AWWOCharacter* P = Player(); P && P->Weapon && Loadout())
	{
		P->Weapon->GunStates.Reset();
		P->Weapon->Equip(Loadout()->GetWeapon());
		P->Weapon->SwapLeft = 0.f; // in hand already: no swap to wait out
	}
}

void FWWORegress::EndScenario(bool bTimedOut)
{
	const FWWORegressScenario& S = Scenarios[Index];
	if (bTimedOut) Record(TEXT("timeout"), 1);
	TArray<FString> Parts;
	for (const TPair<FString, FString>& KV : Current) Parts.Add(KV.Key + TEXT("=") + KV.Value);
	UE_LOG(LogWWO, Display, TEXT("Regress %s: %s"), *S.Name, *FString::Join(Parts, TEXT(" ")));
	const FString Key = GoldenKey(S);
	if (!All.Contains(Key)) Order.Add(Key);
	All.Add(Key, Current);
}

void FWWORegress::Teardown()
{
	UWorld* W = World();
	// Bots first (their controllers take their player states with them), then everything of ours
	// that wasn't here when the run started: what the scenario spawned and what that left behind
	// (grenades, pools, fog, walls, effects).
	for (const TWeakObjectPtr<AController>& C : SpawnedControllers)
	{
		if (!C.IsValid()) continue;
		if (APawn* Body = C->GetPawn()) Body->Destroy();
		C->Destroy();
	}
	SpawnedControllers.Reset();
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		if (Baseline.Contains(A) || !IsOurs(A) || A->IsA<APlayerState>() || A->IsA<AController>() || A->IsA<AGameModeBase>() || A->IsA<AGameStateBase>()) continue;
		A->Destroy();
	}
	Spawned.Reset();
	for (int32 i = Tuned.Num() - 1; i >= 0; --i)
	{
		const FTuned& T = Tuned[i];
		if (UObject* O = T.Object.Get())
		{
			if (FFloatProperty* Prop = FindFProperty<FFloatProperty>(O->GetClass(), T.Property)) Prop->SetPropertyValue_InContainer(O, T.Before);
		}
	}
	Tuned.Reset();
	if (AWWOGameState* GS = W->GetGameState<AWWOGameState>()) GS->bPvP = false;
	SetPlayerTeam(PlayerTeamBefore);
	ResetPlayer();
	BaselineLoadout();
}

void FWWORegress::ResetPlayer()
{
	AWWOCharacter* P = Player();
	if (!P) return;
	PC->ClearScript();
	UWWOHealthComponent* H = P->Health;
	if (H->bDead) H->Respawn();
	if (P->Ult->bActive) P->Ult->Abort(false);
	P->bUlting = false;
	P->ClearStatusEffects();
	P->BlindLeft = 0.f;
	P->SlowAmount = 0.f;
	TArray<UWWODamageOverTime*> Poisons;
	P->GetComponents(Poisons);
	for (UWWODamageOverTime* D : Poisons) D->DestroyComponent();
	// Health and shield full, nobody's last hit.
	H->Health = H->MaxHealth();
	H->Shield = H->MaxShield();
	H->SinceDamage = 99.f;
	H->RespawnLeft = 0.f;
	H->LastAttacker = nullptr;
	H->LastAttackTime = -1000.0;
	H->bLastAttackCritical = false;
	H->DamageMarks.Reset();
	H->bRevived = false;
	H->BeingRevived = 0.f;
	// The gun.
	UWWOWeaponComponent* G = P->Weapon;
	G->Interrupt();
	G->ResetForRespawn();
	G->AdsProgress = 0.f;
	G->AdsAmount = 0.f;
	G->SwapLeft = 0.f;
	G->ReadyLeft = 0.f;
	G->FireBuffer = 0.f;
	G->bReadyShotQueued = false;
	G->BurstLeft = G->BurstSize = G->PerkState.BurstHits = 0;
	G->BurstCooldown = 0.f;
	G->PerkState.RefundRounds = 0;
	G->PerkState.bBurstBoosted = false;
	G->HostStaticHits = 0;
	G->bLastShotPredictedKill = false;
	G->KickAge = 1000.f;
	G->Kick = 0.f;
	G->PunchAge = 1e9f;
	G->ThrowClock = 1e9f;
	G->LookDelta = FVector2D::ZeroVector;
	G->bMeleeConnected = false;
	// Kit.
	UWWOAbilityComponent* K = P->Abilities;
	K->ChargeById.Reset();
	K->ActiveLeft.Reset();
	K->Armed.Reset();
	K->GuardLeft = 0.f;
	K->LiveSeeds = K->LivePatches = 0;
	P->Grenades->ReleaseLeft = -1.f;
	P->Grenades->Charge = P->Grenades->MaxCharges();
	P->Ult->Charge = 1.f;
	// Movement and view.
	P->ResetRecoil();
	P->MoveState = FWWOMoveState();
	P->MoveIn = FWWOMoveInput();
	P->bLunging = false;
	P->LungeTarget = nullptr;
	P->bLaunchPending = false;
	P->PendingLaunch = FVector::ZeroVector;
	P->LungeVelocity = FVector::ZeroVector;
	P->WishVelocity = FVector::ZeroVector;
	P->BobTime = P->BobWeight = P->LandOffset = 0.f;
	P->LandKick = P->SprintBlend = P->LastFallSpeed = 0.f;
	PlaceAt(HomeFeet, HomeYawDeg);
	P->CameraBaseZ = WWOCharacter::StandCameraZ; // eased (never exactly arriving) after a crouch: set, not waited for
	if (AWWOPlayerState* PS = P->GetPlayerState<AWWOPlayerState>())
	{
		PS->Kills = PS->Deaths = PS->PrecisionKills = 0;
	}
}

void FWWORegress::PlaceAt(const FVector& Feet, float YawDeg)
{
	AWWOCharacter* P = Player();
	if (!P) return;
	if (P->bIsCrouched) P->GetCharacterMovement()->UnCrouch(false); // now, not on the next move
	const float Half = P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	P->TeleportTo(Feet + FVector(0, 0, Half + 0.5f), FRotator(0.f, YawDeg, 0.f), false, true);
	P->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	P->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	P->SetViewRotation(0.f, YawDeg);
}

// --- The loadout -------------------------------------------------------------------------------

void FWWORegress::SnapshotLoadout()
{
	UWWOLoadoutSubsystem* L = Loadout();
	if (!L) return;
	SavedLoadout = MakeShared<FLoadoutSnapshot>();
	FLoadoutSnapshot& S = *SavedLoadout;
	S.Weapon = L->Weapon;
	S.Grenade = L->Grenade;
	S.Flora = L->Flora;
	S.Abilities = L->EquippedAbilities;
	S.Traits = L->EquippedTraitList;
	S.Frame = L->EquippedFrame;
	S.Mods = L->EquippedMods;
	S.ActiveConfig = L->ActiveConfig;
	S.SlotConfigs = L->SlotConfigs;
	S.ActiveSlot = L->ActiveSlot;
	S.bSaveDue = L->bSaveDue;
}

void FWWORegress::RestoreLoadout()
{
	UWWOLoadoutSubsystem* L = Loadout();
	if (!L || !SavedLoadout.IsValid()) return;
	const FLoadoutSnapshot& S = *SavedLoadout;
	L->Weapon = S.Weapon;
	L->Grenade = S.Grenade;
	L->Flora = S.Flora;
	L->EquippedAbilities = S.Abilities;
	L->EquippedTraitList = S.Traits;
	L->EquippedFrame = S.Frame;
	L->EquippedMods = S.Mods;
	L->ActiveConfig = S.ActiveConfig;
	L->SlotConfigs = S.SlotConfigs;
	L->ActiveSlot = S.ActiveSlot;
	L->OnChanged.Broadcast();
	L->bSaveDue = S.bSaveDue; // nothing new to write: the run never saves
}

void FWWORegress::BaselineLoadout()
{
	// The same kit for every scenario, whatever the owner has saved: the first flora, the first
	// weapon with no mods, no grenade, no abilities, no traits, the Standard frame parts.
	UWWOLoadoutSubsystem* L = Loadout();
	if (!L) return;
	L->Flora = L->Floras.Num() > 0 ? L->Floras[0] : nullptr;
	L->Weapon = L->Weapons.Num() > 0 ? L->Weapons[0] : nullptr;
	L->Grenade = nullptr;
	L->EquippedAbilities.Init(nullptr, UWWOLoadoutSubsystem::AbilitySlots);
	L->EquippedTraitList.Reset();
	L->EquippedMods.Reset();
	L->EquippedFrame.Reset();
	for (const FName& Slot : UWWOLoadoutSubsystem::FrameSlots())
	{
		const TArray<UWWOFramePartData*> Options = L->FramePartsFor(Slot);
		if (Options.Num() > 0) L->EquippedFrame.Add(Slot, Options[0]);
	}
	L->ActiveConfig = INDEX_NONE;
	L->bSaveDue = false;
}

void FWWORegress::EquipWeapon(FName Id)
{
	if (UWWOWeaponData* W = WeaponAsset(Id)) Loadout()->Weapon = W;
	else Record(TEXT("missing_weapon"), Id.ToString());
}

void FWWORegress::EquipMod(FName Id)
{
	UWWOLoadoutSubsystem* L = Loadout();
	for (UWWOModData* M : L->WeaponMods)
	{
		if (M && M->Id == Id && L->Weapon && M->Frame == L->Weapon->Id)
		{
			L->EquippedMods.FindOrAdd(L->Weapon->Id).Add(M->Slot, M);
			return;
		}
	}
	Record(TEXT("missing_mod"), Id.ToString());
}

void FWWORegress::EquipFlora(FName Id)
{
	UWWOLoadoutSubsystem* L = Loadout();
	for (UWWOFloraData* F : L->Floras) if (F && F->Id == Id) { L->Flora = F; return; }
	Record(TEXT("missing_flora"), Id.ToString());
}

void FWWORegress::EquipGrenade(FName Id)
{
	if (UWWOGrenadeData* G = GrenadeAsset(Id)) Loadout()->Grenade = G;
	else Record(TEXT("missing_grenade"), Id.ToString());
}

void FWWORegress::EquipAbility(int32 Slot, FName Id)
{
	UWWOLoadoutSubsystem* L = Loadout();
	if (UWWOAbilityData* A = AbilityAsset(Id); A && L->EquippedAbilities.IsValidIndex(Slot)) L->EquippedAbilities[Slot] = A;
	else Record(TEXT("missing_ability"), Id.ToString());
}

UWWOWeaponData* FWWORegress::WeaponAsset(FName Id) const { return UWWOLoadoutSubsystem::FindById(Loadout()->Weapons, Id); }
UWWOGrenadeData* FWWORegress::GrenadeAsset(FName Id) const { return UWWOLoadoutSubsystem::FindById(Loadout()->Grenades, Id); }
UWWOAbilityData* FWWORegress::AbilityAsset(FName Id) const { return UWWOLoadoutSubsystem::FindById(Loadout()->Abilities, Id); }

void FWWORegress::Tune(UObject* Object, FName Property, float Value)
{
	FFloatProperty* Prop = Object ? FindFProperty<FFloatProperty>(Object->GetClass(), Property) : nullptr;
	if (!Prop)
	{
		Record(TEXT("missing_tunable"), Property.ToString());
		return;
	}
	Tuned.Add({ Object, Property, Prop->GetPropertyValue_InContainer(Object) });
	Prop->SetPropertyValue_InContainer(Object, Value);
}

// --- Spawning ----------------------------------------------------------------------------------

namespace
{
	FRotator FacingFrom(const FVector& At, const FVector& Toward)
	{
		FVector D = Toward - At;
		D.Z = 0.f;
		return FRotator(0.f, D.IsNearlyZero() ? 0.f : D.Rotation().Yaw, 0.f);
	}
}

AWWOTarget* FWWORegress::SpawnDummy(const FVector& At, float Health)
{
	AWWOTarget* T = World()->SpawnActor<AWWOTarget>(AWWOTarget::StaticClass(), At, FacingFrom(At, HomeFeet), WWO::AlwaysSpawn());
	if (!T) { Record(TEXT("spawn_failed"), TEXT("dummy")); return nullptr; }
	if (Health > 0.f) T->Revive(Health);
	Spawned.Add(T);
	return T;
}

AWWOEnemy* FWWORegress::SpawnEnemy(FName Kind, const FVector& At, bool bInert)
{
	UWWOLoadoutSubsystem* L = Loadout();
	UClass* Class = Kind == "tank" ? AWWOTank::StaticClass() : (Kind == "soldier" ? AWWOSoldier::StaticClass() : AWWOSwarm::StaticClass());
	UWWOEnemyData* Data = Kind == "tank" ? (UWWOEnemyData*)L->TankData : (Kind == "soldier" ? (UWWOEnemyData*)L->SoldierData : (UWWOEnemyData*)L->SwarmData);
	if (Data && bInert && Data->AiEnabled >= 0.5f) Tune(Data, "AiEnabled", 0.f);
	const float Half = Class->GetDefaultObject<AWWOEnemy>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	AWWOEnemy* E = World()->SpawnActor<AWWOEnemy>(Class, At + FVector(0, 0, Half), FacingFrom(At, HomeFeet), WWO::AlwaysSpawn());
	if (!E) { Record(TEXT("spawn_failed"), Kind.ToString()); return nullptr; }
	E->bRespawns = false;
	Spawned.Add(E);
	return E;
}

AWWOAlly* FWWORegress::SpawnAlly(const FVector& At)
{
	UWWOLoadoutSubsystem* L = Loadout();
	if (L->AllyData && L->AllyData->AiEnabled >= 0.5f) Tune(L->AllyData, "AiEnabled", 0.f);
	const float Half = AWWOAlly::StaticClass()->GetDefaultObject<AWWOAlly>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	AWWOAlly* A = World()->SpawnActor<AWWOAlly>(AWWOAlly::StaticClass(), At + FVector(0, 0, Half), FacingFrom(At, HomeFeet), WWO::AlwaysSpawn());
	if (!A) { Record(TEXT("spawn_failed"), TEXT("ally")); return nullptr; }
	Spawned.Add(A);
	return A;
}

AWWOCharacter* FWWORegress::SpawnBot(int32 Team, const FVector& At, float FaceYaw)
{
	AWWOGameMode* GM = World()->GetAuthGameMode<AWWOGameMode>();
	AWWOBotController* Bot = World()->SpawnActor<AWWOBotController>();
	if (!GM || !Bot) { Record(TEXT("spawn_failed"), TEXT("bot")); return nullptr; }
	Bot->bParked = true; // stands where it is put and does nothing
	SpawnedControllers.Add(Bot);
	if (AWWOPlayerState* PS = Bot->GetPlayerState<AWWOPlayerState>())
	{
		PS->SetPlayerName(FString::Printf(TEXT("regressbot%d"), SpawnedControllers.Num()));
		PS->Team = Team;
	}
	GM->RestartPlayer(Bot);
	AWWOCharacter* Body = Cast<AWWOCharacter>(Bot->GetPawn());
	if (!Body) { Record(TEXT("spawn_failed"), TEXT("bot body")); return nullptr; }
	// A fixed kit, not the random one bots pick (given the way a bot's own kit is: its loadout, its
	// flora's model, its gun): the first flora, the first weapon, the Standard frame parts.
	{
		const UWWOLoadoutSubsystem* L = Loadout();
		FWWONetLoadout Kit;
		Kit.Flora = L->Floras.Num() > 0 ? L->Floras[0]->Id : NAME_None;
		Kit.Weapon = L->Weapons.Num() > 0 ? L->Weapons[0]->Id : NAME_None;
		for (const FName& Slot : UWWOLoadoutSubsystem::FrameSlots())
		{
			const TArray<UWWOFramePartData*> Options = L->FramePartsFor(Slot);
			if (Options.Num() > 0) { Kit.FrameSlots.Add(Slot); Kit.FrameParts.Add(Options[0]->Id); }
		}
		Body->ServerSetLoadout_Implementation(Kit);
		Body->ServerSetFlora_Implementation(Kit.Flora);
		Body->Weapon->RebindLoadout();
	}
	const float Half = Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float Yaw = FaceYaw >= 999.f ? FacingFrom(At, HomeFeet).Yaw : FaceYaw;
	Body->TeleportTo(At + FVector(0, 0, Half + 0.5f), FRotator(0.f, Yaw, 0.f), false, true);
	Body->SetViewRotation(0.f, Yaw);
	Body->Health->Health = Body->Health->MaxHealth();
	Body->Health->Shield = Body->Health->MaxShield();
	Spawned.Add(Body);
	return Body;
}

void FWWORegress::SetPlayerTeam(int32 Team)
{
	if (AWWOCharacter* P = Player()) if (AWWOPlayerState* PS = P->GetPlayerState<AWWOPlayerState>()) PS->Team = Team;
}

// --- Input and aim -----------------------------------------------------------------------------

void FWWORegress::Press(EWWOAction Action) { PC->ScriptPress(Action); }
void FWWORegress::Hold(EWWOAction Action, bool bDown) { PC->ScriptHold(Action, bDown); }
void FWWORegress::Move(FVector2D Axis) { PC->ScriptMove(Axis); }

void FWWORegress::AimAt(const FVector& Point)
{
	AWWOCharacter* P = Player();
	const FRotator R = (Point - P->EyeLocation()).Rotation();
	P->SetViewRotation(FMath::DegreesToRadians(R.Pitch), R.Yaw);
}

void FWWORegress::Face(float YawDeg, float PitchDeg)
{
	Player()->SetViewRotation(FMath::DegreesToRadians(PitchDeg), YawDeg);
}

// --- Results -----------------------------------------------------------------------------------

void FWWORegress::Record(const FString& Key, const FString& Value)
{
	for (TPair<FString, FString>& KV : Current) if (KV.Key == Key) { KV.Value = Value; return; }
	Current.Add({ Key, Value });
}

void FWWORegress::Record(const FString& Key, float Value)
{
	// Nine significant digits: every float written exactly (a change in the last bit shows).
	FString Text = FString::Printf(TEXT("%.9g"), Value);
	if (Text == TEXT("-0")) Text = TEXT("0");
	Record(Key, Text);
}

void FWWORegress::Record(const FString& Key, int32 Value) { Record(Key, FString::FromInt(Value)); }

void FWWORegress::RecordVec(const FString& Key, const FVector& V)
{
	// To 0.01 cm: positions that come from the player's eye carry a float residue of about 6e-5 cm
	// that depends on which scenarios ran before (see docs/TESTING.md); nothing real is that small.
	const FVector D = V - HomeFeet;
	auto Cm = [](double X) { FString T = FString::Printf(TEXT("%.2f"), X); return T == TEXT("-0.00") ? FString(TEXT("0.00")) : T; };
	Record(Key, Cm(D.X) + TEXT(",") + Cm(D.Y) + TEXT(",") + Cm(D.Z));
}

float FWWORegress::HealthOf(const AActor* A)
{
	const IWWOTargetable* T = Cast<IWWOTargetable>(A);
	if (const AWWOAlly* Ally = Cast<AWWOAlly>(A)) return Ally->Health + Ally->Shield;
	return T ? T->GetHealth() + T->GetShield() : -1.f;
}

void FWWORegress::NotePlayerShot()
{
	ShotsMarkedSince = UWWOWorldSubsystem::HitsMarked;
	TextsAtStart = UWWOWorldSubsystem::Get(World())->FloatingTexts().Num();
}

FString FWWORegress::Quote(const FString& S)
{
	return TEXT("\"") + S.ReplaceCharWithEscapedChar() + TEXT("\"");
}

bool FWWORegress::LoadResults(const FString& Path, TMap<FString, TMap<FString, FString>>& Out)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path)) return false;
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) return false;
	for (const auto& Pair : Root->Values)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Pair.Value->TryGetObject(Obj)) continue;
		TMap<FString, FString>& Values = Out.Add(FString(Pair.Key));
		for (const auto& KV : (*Obj)->Values) Values.Add(FString(KV.Key), KV.Value->AsString());
	}
	return true;
}

bool FWWORegress::SaveResults(const FString& Path, const TMap<FString, TArray<TPair<FString, FString>>>& Results)
{
	// Sorted scenarios, sorted keys, one per line: the same bytes for the same numbers.
	TArray<FString> Names;
	Results.GetKeys(Names);
	Names.Sort();
	FString Out = TEXT("{\n");
	for (int32 i = 0; i < Names.Num(); ++i)
	{
		TArray<TPair<FString, FString>> Values = Results[Names[i]];
		Values.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B) { return A.Key < B.Key; });
		Out += TEXT("  ") + Quote(Names[i]) + TEXT(": {");
		for (int32 j = 0; j < Values.Num(); ++j)
		{
			Out += (j == 0 ? TEXT("\n") : TEXT(",\n")) + FString(TEXT("    ")) + Quote(Values[j].Key) + TEXT(": ") + Quote(Values[j].Value);
		}
		Out += Values.Num() > 0 ? TEXT("\n  }") : TEXT("}");
		Out += i + 1 < Names.Num() ? TEXT(",\n") : TEXT("\n");
	}
	Out += TEXT("}\n");
	return FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void FWWORegress::Finish()
{
	const double Seconds = FPlatformTime::Seconds() - StartedAt;
	FString Only;
	const bool bFiltered = WWOSwitches::Value(TEXT("WWORegressOnly="), Only);
	SaveResults(ResultsDir() / TEXT("results.json"), All);
	SaveResults(ResultsDir() / FString::Printf(TEXT("results-%s.json"), *Mode()), All);

	// Which golden entries this run answers for: shared ones and this mode's own.
	auto IsMine = [this](const FString& Key)
	{
		int32 At = INDEX_NONE;
		if (!Key.FindChar(TEXT('@'), At)) return true;
		return Key.Mid(At + 1) == Mode();
	};
	TMap<FString, TMap<FString, FString>> Golden;
	const bool bHaveGolden = LoadResults(GoldenPath(), Golden);
	int32 Diffs = Problems;
	if (bUpdate)
	{
		// This run's entries replace the golden ones; a full run also drops entries it no longer has
		// (of its own mode). The other mode's entries stay.
		TMap<FString, TArray<TPair<FString, FString>>> Merged;
		for (const auto& Pair : Golden)
		{
			if (!bFiltered && IsMine(Pair.Key) && !All.Contains(Pair.Key))
			{
				// A render-only scenario a no-render run skipped keeps its golden entry.
				bool bSkippedHere = false;
				for (const FWWORegressScenario& S : Scenarios) bSkippedHere |= S.Name == Pair.Key && S.Modes == EWWORegressModes::RenderOnly && !bRender;
				if (!bSkippedHere) continue;
			}
			TArray<TPair<FString, FString>>& Values = Merged.Add(Pair.Key);
			for (const auto& KV : Pair.Value) Values.Add({ KV.Key, KV.Value });
		}
		for (const auto& Pair : All) Merged.Add(Pair.Key, Pair.Value);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(GoldenPath()), true);
		SaveResults(GoldenPath(), Merged);
		UE_LOG(LogWWO, Display, TEXT("Regress: golden updated (%s, %d entries from this run)"), *GoldenPath(), All.Num());
	}
	else
	{
		if (!bHaveGolden) { UE_LOG(LogWWO, Warning, TEXT("Regress DIFF golden: no golden file at %s"), *GoldenPath()); ++Diffs; }
		for (const FString& Key : Order)
		{
			const TArray<TPair<FString, FString>>& Now = All[Key];
			const TMap<FString, FString>* Was = Golden.Find(Key);
			if (!Was)
			{
				UE_LOG(LogWWO, Warning, TEXT("Regress DIFF %s: not in golden"), *Key);
				++Diffs;
				continue;
			}
			TSet<FString> Seen;
			for (const TPair<FString, FString>& KV : Now)
			{
				Seen.Add(KV.Key);
				const FString* G = Was->Find(KV.Key);
				if (!G || *G != KV.Value)
				{
					UE_LOG(LogWWO, Warning, TEXT("Regress DIFF %s.%s: golden %s, now %s"), *Key, *KV.Key, G ? **G : TEXT("(none)"), *KV.Value);
					++Diffs;
				}
			}
			for (const auto& KV : *Was)
			{
				if (Seen.Contains(KV.Key)) continue;
				UE_LOG(LogWWO, Warning, TEXT("Regress DIFF %s.%s: golden %s, now (none)"), *Key, *KV.Key, *KV.Value);
				++Diffs;
			}
		}
		if (!bFiltered)
		{
			TArray<FString> Keys;
			Golden.GetKeys(Keys);
			Keys.Sort();
			for (const FString& Key : Keys)
			{
				if (!IsMine(Key) || All.Contains(Key)) continue;
				bool bSkippedHere = false;
				for (const FWWORegressScenario& S : Scenarios) bSkippedHere |= S.Name == Key && S.Modes == EWWORegressModes::RenderOnly && !bRender;
				if (bSkippedHere) continue;
				UE_LOG(LogWWO, Warning, TEXT("Regress DIFF %s: in golden, not run"), *Key);
				++Diffs;
			}
		}
	}
	// The run never writes the loadout: the bytes on disk must be the ones it started with.
	RestoreLoadout();
	TArray<uint8> Now;
	const bool bExists = FFileHelper::LoadFileToArray(Now, *LoadoutFile(), FILEREAD_Silent);
	const bool bSame = bExists == GLoadoutExisted && Now == GLoadoutAtStart;
	UE_LOG(LogWWO, Display, TEXT("Regress: saved loadout %s"), bSame ? TEXT("untouched (same bytes)") : TEXT("CHANGED during the run"));
	if (!bSame) ++Diffs;
	UE_LOG(LogWWO, Display, TEXT("Regress: %d scenarios run, %d skipped, %d differences, %.1f s (%s mode)"), All.Num(), Skipped, Diffs, Seconds, *Mode());
	WWOTest::ExitWith(Diffs, TEXT("Regress"));
}

// --- The scenario list -------------------------------------------------------------------------

void FWWORegress::BuildScenarios()
{
	const UWWOLoadoutSubsystem* L = Loadout();
	TArray<FWWORegressScenario> List;
	if (L)
	{
		AddGunScenarios(List, *L);
		AddKitScenarios(List, *L);
		AddCombatScenarios(List, *L);
	}
	FString Only;
	const bool bFiltered = WWOSwitches::Value(TEXT("WWORegressOnly="), Only);
	TSet<FString> Names;
	for (FWWORegressScenario& S : List)
	{
		if (Names.Contains(S.Name)) { UE_LOG(LogWWO, Warning, TEXT("Regress DIFF %s: two scenarios have this name"), *S.Name); ++Problems; continue; }
		Names.Add(S.Name);
		if (bFiltered && !S.Name.Contains(Only)) continue;
		Scenarios.Add(MoveTemp(S));
	}
}
