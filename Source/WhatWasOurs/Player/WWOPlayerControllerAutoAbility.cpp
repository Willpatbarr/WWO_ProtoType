// -WWOAutoTest: the ability and ult steps. Every ability in the data is equipped in slot 1 and
// set off in turn (passives logged, actives pressed, triggered ones set off, Static Charge filled
// by real damage, armed and discharged on an enemy), then each flora's ult is cast (Blast, Veil,
// Entrench) with its after-effects checked. Logs as `AutoTest ability:` / `AutoTest ult:`, with
// screenshots 23_ability_* and 24_ult_*. The loadout is put back at the end.

#include "Player/WWOPlayerController.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOGrenadeComponent.h"
#include "Player/WWOAbilityComponent.h"
#include "Player/WWOUltComponent.h"
#include "Flora/WWOSporeFog.h"
#include "Flora/WWOFloraEffects.h"
#include "Weapons/WWOGrenade.h"
#include "Enemies/WWOEnemy.h"
#include "Allies/WWOAlly.h"
#include "World/WWOTarget.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "WhatWasOurs.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"

namespace
{
	const TCHAR* KindName(EWWOAbilityKind Kind)
	{
		switch (Kind)
		{
		case EWWOAbilityKind::Active: return TEXT("active");
		case EWWOAbilityKind::Triggered: return TEXT("triggered");
		case EWWOAbilityKind::Charged: return TEXT("charged");
		default: return TEXT("passive");
		}
	}

	const TCHAR* UltName(EWWOUltKind Kind)
	{
		switch (Kind)
		{
		case EWWOUltKind::Veil: return TEXT("veil");
		case EWWOUltKind::Entrench: return TEXT("entrench");
		default: return TEXT("blast");
		}
	}

	float Vitals(const AActor* Actor)
	{
		const IWWOTargetable* T = Cast<IWWOTargetable>(Actor);
		return T ? T->GetHealth() + T->GetShield() : 0.f;
	}
}

bool AWWOPlayerController::UpdateAutoAbilityTest(AWWOCharacter* P, float DeltaTime)
{
	UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(this);
	if (!Loadout || !WS) return true;
	P->Health->GodMode = 1.f; // the waves keep running
	if (P->Health->bDead) P->Health->Respawn();

	if (AutoAbilityIndex < 0)
	{
		AutoFloraBefore = Loadout->GetFlora();
		AutoAbilityGrenadeBefore = Loadout->GetGrenade();
		AutoAbilitiesBefore.Reset();
		for (int32 Slot = 0; Slot < UWWOLoadoutSubsystem::AbilitySlots; ++Slot) AutoAbilitiesBefore.Add(Loadout->GetAbility(Slot));
		AutoTraitsBefore.Reset();
		for (UWWOTraitData* T : Loadout->EquippedTraits()) AutoTraitsBefore.Add(T);
		AutoAbilities = Loadout->Abilities;
		AutoFloras = Loadout->Floras;
		UE_LOG(LogWWO, Display, TEXT("AutoTest ability: %d abilities, %d flora ults"), AutoAbilities.Num(), AutoFloras.Num());
		AutoAbilityIndex = 0;
		AutoAbilityPhase = -1.f;
	}

	const int32 NumAbilities = AutoAbilities.Num();
	if (AutoAbilityIndex >= NumAbilities + AutoFloras.Num())
	{
		// Put the loadout back the way it was.
		if (Loadout->GetFlora() != AutoFloraBefore) Loadout->EquipFlora(AutoFloraBefore);
		Loadout->EquipGrenade(AutoAbilityGrenadeBefore);
		for (int32 Slot = 0; Slot < AutoAbilitiesBefore.Num(); ++Slot)
		{
			if (AutoAbilitiesBefore[Slot]) Loadout->EquipAbility(Slot, AutoAbilitiesBefore[Slot]);
		}
		for (UWWOTraitData* T : Loadout->EquippedTraits()) if (!AutoTraitsBefore.Contains(T)) Loadout->ToggleTrait(T);
		for (UWWOTraitData* T : AutoTraitsBefore) if (!Loadout->IsTraitEquipped(T)) Loadout->ToggleTrait(T);
		MoveValue = FVector2D::ZeroVector;
		UE_LOG(LogWWO, Display, TEXT("AutoTest ability: loadout restored (%s, %s, %s)"), Loadout->GetFlora() ? *Loadout->GetFlora()->Id.ToString() : TEXT("none"),
			Loadout->GetAbility(0) ? *Loadout->GetAbility(0)->Id.ToString() : TEXT("none"), Loadout->GetAbility(1) ? *Loadout->GetAbility(1)->Id.ToString() : TEXT("none"));
		return true;
	}

	// --- Helpers ---
	auto Press = [this](EWWOAction Action) { JustPressed[(int32)Action] = true; };
	auto TrainingTarget = [&]() -> AActor*
	{
		AActor* Best = nullptr;
		float BestD = 1e9f;
		for (TActorIterator<AWWOTarget> It(GetWorld()); It; ++It)
		{
			const float D = FVector::Dist(It->GetActorLocation(), P->GetActorLocation());
			if (It->StrafeDistance <= 0.f && !It->IsDead() && D < BestD && PlaceForAutoTest(P, *It, 12.f, 0.f, false)) { BestD = D; Best = *It; }
		}
		return Best;
	};
	auto NearestEnemy = [&](bool bRankAndFile) -> AWWOEnemy*
	{
		AWWOEnemy* Best = nullptr;
		float BestD = 1e9f;
		for (AWWOEnemy* E : WS->LivingEnemies())
		{
			const float D = FVector::Dist(E->GetActorLocation(), P->GetActorLocation());
			if (D < BestD && (!bRankAndFile || E->IsRankAndFile()) && E->GetCharacterMovement()->IsMovingOnGround()) { BestD = D; Best = E; }
		}
		return Best;
	};
	auto Snapshot = [&]()
	{
		AutoHealths.Reset();
		for (AActor* A : WS->LivingTargets()) AutoHealths.Add(A, Vitals(A));
	};
	auto Report = [&](const TCHAR* What)
	{
		FString Hits;
		int32 Count = 0;
		for (const TPair<TWeakObjectPtr<AActor>, float>& Pair : AutoHealths)
		{
			const IWWOTargetable* T = Cast<IWWOTargetable>(Pair.Key.Get());
			if (!T) continue;
			const float Lost = Pair.Value - (T->IsDead() ? 0.f : Vitals(Pair.Key.Get()));
			if (Lost < 0.5f) continue;
			Count++;
			Hits += FString::Printf(TEXT(" %s -%.0f%s;"), *Pair.Key->GetName(), Lost, T->IsDead() ? TEXT(" (killed)") : TEXT(""));
		}
		UE_LOG(LogWWO, Display, TEXT("AutoTest ability: %s hurt %d:%s"), What, Count, *Hits);
	};
	auto AimAt = [&](const FVector& Point)
	{
		const FVector Dir = (Point - P->EyeLocation()).GetSafeNormal();
		P->SetViewRotation(FMath::Asin(Dir.Z), Dir.Rotation().Yaw);
	};
	auto Finish = [&]()
	{
		AutoAbilityIndex++;
		AutoAbilityPhase = -1.f;
		MoveValue = FVector2D::ZeroVector;
		FMemory::Memzero(Held, sizeof(Held));
	};

	const bool bUlt = AutoAbilityIndex >= NumAbilities;
	UWWOAbilityData* A = bUlt ? nullptr : AutoAbilities[AutoAbilityIndex].Get();
	UWWOFloraData* UltFlora = bUlt ? AutoFloras[AutoAbilityIndex - NumAbilities].Get() : nullptr;
	const FName Id = A ? A->Id : (UltFlora ? UltFlora->Id : NAME_None);
	UWWOAbilityComponent* Abilities = P->Abilities;

	// --- Set up this step ---
	if (AutoAbilityPhase < 0.f)
	{
		AutoAbilityPhase = 0.f;
		bAutoAbilityShot = bAutoAbilityFlag = false;
		AutoAbilityValue = 0.f;
		AutoAbilityTarget = nullptr;
		if (A)
		{
			for (UWWOFloraData* F : AutoFloras) if (F && F->Id == A->Flora && Loadout->GetFlora() != F) Loadout->EquipFlora(F);
			Loadout->EquipAbility(0, A);
			UE_LOG(LogWWO, Display, TEXT("AutoTest ability: %s (%s, flora %s) in slot 1 (slot 2: %s, flora now %s); charge %.2f, effect on %d"),
				*Id.ToString(), KindName(A->Kind), *A->Flora.ToString(), Loadout->GetAbility(1) ? *Loadout->GetAbility(1)->Id.ToString() : TEXT("none"),
				Loadout->GetFlora() ? *Loadout->GetFlora()->Id.ToString() : TEXT("none"), Abilities->Charge(A), Abilities->IsEffectActive(A));
			if (Id == "thornfist" || Id == "melee_guard")
			{
				AutoAbilityTarget = TrainingTarget();
				if (AutoAbilityTarget.IsValid()) PlaceForAutoTest(P, AutoAbilityTarget.Get(), 2.5f, 0.f);
			}
			else if (Id == "static_charge")
			{
				AutoAbilityTarget = NearestEnemy(false);
				if (AutoAbilityTarget.IsValid()) PlaceForAutoTest(P, AutoAbilityTarget.Get(), 2.5f, 0.f);
			}
			else if (Id == "seedburst" || Id == "spore_patches" || Id == "spore_dash")
			{
				AutoAbilityTarget = TrainingTarget();
				if (AutoAbilityTarget.IsValid()) PlaceForAutoTest(P, AutoAbilityTarget.Get(), 8.f, 0.f);
			}
			else if (Id == "root_bombs")
			{
				AutoAbilityTarget = TrainingTarget();
				if (AutoAbilityTarget.IsValid()) PlaceForAutoTest(P, AutoAbilityTarget.Get(), 12.f, 0.f);
			}
			else if (Id == "spore_fog_damage")
			{
				AutoAbilityTarget = NearestEnemy(true);
				if (AutoAbilityTarget.IsValid()) PlaceForAutoTest(P, AutoAbilityTarget.Get(), 7.f, 0.f);
			}
		}
		else if (UltFlora)
		{
			if (Loadout->GetFlora() != UltFlora) Loadout->EquipFlora(UltFlora);
			P->Ult->Fill();
			AutoAbilityTarget = TrainingTarget();
			if (AutoAbilityTarget.IsValid()) PlaceForAutoTest(P, AutoAbilityTarget.Get(), 2.5f, 0.f);
			const UWWOUltComponent* U = P->Ult;
			UE_LOG(LogWWO, Display, TEXT("AutoTest ult: %s (%s) ready %d; damage %.0f radius %.1f m, windup %.2f hold %.2f blend %.2f, camera %.1f back %.1f up %.1f side; veil %.0f s fog %.1f m / %.0f s melee x%.1f blend %.2f hold %.2f; entrench %.0f s grenades x%.1f"),
				*Id.ToString(), UltName(UltFlora->Ult), U->IsReady(), U->Damage, U->BlastRadius, U->WindupTime, U->HoldTime, U->CameraBlend, U->CameraDistance,
				U->CameraHeight, U->CameraSide, U->VeilTime, U->VeilFogRadius, U->VeilFogTime, U->VeilMeleeMult, U->VeilCameraBlend, U->VeilHold,
				U->EntrenchTime, U->EntrenchGrenadeMult);
		}
		return false;
	}

	const float Dt = DeltaTime;
	AutoAbilityPhase += Dt;
	auto Cross = [&](float T) { return AutoAbilityPhase - Dt < T && AutoAbilityPhase >= T; };
	auto ShotNamed = [&](const TCHAR* Prefix, const TCHAR* What) { Shot(*FString::Printf(TEXT("%s_%s_%s"), Prefix, *Id.ToString(), What)); };
	AActor* Target = AutoAbilityTarget.Get();

	// --- Ults ---
	if (bUlt)
	{
		UWWOUltComponent* U = P->Ult;
		auto LogView = [&](const TCHAR* What)
		{
			const USkeletalMeshComponent* Mesh = P->GetMesh();
			const UCameraComponent* View = nullptr;
			for (UActorComponent* C : P->GetComponents()) if (UCameraComponent* Cam = Cast<UCameraComponent>(C)) if (Cam->IsActive()) View = Cam;
			UE_LOG(LogWWO, Display, TEXT("AutoTest ult: %s %s at %.2f s: active %d ulting %d; camera %s %.2f m from the eye; body owner-no-see %d first-person type %d; weapon visible %d"),
				*Id.ToString(), What, AutoAbilityPhase - 0.1f, U->bActive, P->bUlting, View ? *View->GetName() : TEXT("none"),
				View ? FVector::Dist(View->GetComponentLocation(), P->Camera->GetComponentLocation()) / WWO::M : -1.f,
				Mesh->bOwnerNoSee, (int32)Mesh->FirstPersonPrimitiveType, P->Weapon->IsVisible());
		};
		if (Cross(0.1f))
		{
			Snapshot();
			const bool bOk = U->ActivateUlt();
			UE_LOG(LogWWO, Display, TEXT("AutoTest ult: %s activated %d, charge now %.2f"), *Id.ToString(), bOk, U->Charge);
		}
		if (AutoAbilityPhase > 0.1f && !U->bActive && !bAutoAbilityFlag)
		{
			bAutoAbilityFlag = true;
			LogView(TEXT("finished"));
		}
		if (UltFlora->Ult == EWWOUltKind::Blast)
		{
			if (Cross(0.22f)) { LogView(TEXT("gliding out")); ShotNamed(TEXT("24_ult"), TEXT("a_glide")); }
			else if (Cross(0.42f)) { LogView(TEXT("casting")); ShotNamed(TEXT("24_ult"), TEXT("b_cast")); }
			else if (Cross(0.6f)) { LogView(TEXT("blast")); ShotNamed(TEXT("24_ult"), TEXT("c_blast")); }
			else if (Cross(1.1f)) { LogView(TEXT("gliding back")); ShotNamed(TEXT("24_ult"), TEXT("d_back")); }
			else if (Cross(1.8f)) { Report(TEXT("ult blast")); Finish(); }
			return false;
		}
		const bool bVeil = UltFlora->Ult == EWWOUltKind::Veil;
		if (Cross(0.2f)) { LogView(TEXT("out")); ShotNamed(TEXT("24_ult"), TEXT("a_out")); }
		else if (Cross(0.4f)) { LogView(TEXT("release")); ShotNamed(TEXT("24_ult"), TEXT("b_release")); }
		else if (Cross(1.f))
		{
			if (Target) PlaceForAutoTest(P, Target, 2.5f, 0.f);
			if (bVeil)
			{
				int32 Alerted = 0;
				for (AWWOEnemy* E : WS->LivingEnemies()) Alerted += E->bAlerted;
				UE_LOG(LogWWO, Display, TEXT("AutoTest ult: veil left %.1f s, invisible %d, in fog %d, melee x%.2f, enemies alerted %d"), P->VeilLeft, P->IsInvisible(),
					AWWOSporeFog::Contains(GetWorld(), P->GetActorLocation()), P->VeilMeleeMultiplier(), Alerted);
			}
			else
			{
				const float Before = P->Health->Health + P->Health->Shield;
				P->Health->GodMode = 0.f;
				P->Health->TakeDamage(30.f);
				const float After = P->Health->Health + P->Health->Shield;
				P->Health->GodMode = 1.f;
				const float Base = P->Grenades->EffectiveRechargeTime() * P->EntrenchRechargeMultiplier();
				UE_LOG(LogWWO, Display, TEXT("AutoTest ult: entrench left %.1f s; took 30 damage: vitals %.0f -> %.0f; grenade recharge %.2f s (x%.1f faster than %.2f s)"),
					P->EntrenchLeft, Before, After, P->Grenades->EffectiveRechargeTime(), P->EntrenchRechargeMultiplier(), Base);
				AutoAbilityFrom = P->GetActorLocation();
				MoveValue = FVector2D(0.f, 1.f); // try to walk: rooted
			}
			Snapshot();
			Press(EWWOAction::Melee);
		}
		if (AutoAbilityPhase > 1.f && AutoAbilityPhase < 1.6f) bAutoAbilityShot |= P->IsLunging();
		if (Cross(1.25f)) ShotNamed(TEXT("24_ult"), TEXT("c_after"));
		else if (Cross(1.6f))
		{
			Report(bVeil ? TEXT("veiled melee") : TEXT("entrenched melee"));
			UE_LOG(LogWWO, Display, TEXT("AutoTest ult: %s melee lunged %d; moved %.2f m while holding forward"), *Id.ToString(), bAutoAbilityShot,
				bVeil ? -1.f : FVector::Dist2D(AutoAbilityFrom, P->GetActorLocation()) / WWO::M);
			MoveValue = FVector2D::ZeroVector;
			if (!bVeil) P->SetViewRotation(FMath::DegreesToRadians(-50.f), P->GetActorRotation().Yaw); // down at the roots
		}
		else if (Cross(1.75f) && !bVeil) ShotNamed(TEXT("24_ult"), TEXT("d_roots"));
		else if (Cross(1.9f) && bVeil) Finish();
		else if (Cross(P->Ult->EntrenchTime + 0.6f))
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest ult: entrench over: left %.2f s, roots still up %d, recharge %.2f s"), P->EntrenchLeft,
				TActorIterator<AWWOEntrenchRoots>(GetWorld()) ? 1 : 0, P->Grenades->EffectiveRechargeTime());
			AutoAbilityFrom = P->GetActorLocation();
			MoveValue = FVector2D(0.f, 1.f);
		}
		else if (Cross(P->Ult->EntrenchTime + 1.f))
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest ult: after entrench moved %.2f m in 0.4 s"), FVector::Dist2D(AutoAbilityFrom, P->GetActorLocation()) / WWO::M);
			Finish();
		}
		return false;
	}

	// --- Abilities ---
	if (A->Kind == EWWOAbilityKind::Passive)
	{
		if (Cross(0.2f))
		{
			if (Id == "overgrowth") UE_LOG(LogWWO, Display, TEXT("AutoTest ability: overgrowth speed x%.2f, extra jumps %d"), Abilities->SpeedMultiplier(), Abilities->ExtraJumps());
			if (Id == "extra_grenade")
			{
				P->Grenades->Charge = 1.5f;
				UE_LOG(LogWWO, Display, TEXT("AutoTest ability: extra grenade max charges %.0f (extra %.0f)"), P->Grenades->MaxCharges(), Abilities->ExtraCharges());
			}
			if (Id == "spore_fog_damage")
			{
				if (AWWOEnemy* E = Cast<AWWOEnemy>(Target))
				{
					Snapshot();
					AWWOSporeFog::SpawnFog(GetWorld(), E->FeetLocation() + FVector(0, 0, 80.f), 4.f, 6.f);
					UE_LOG(LogWWO, Display, TEXT("AutoTest ability: spore fog (4 m, 6 s) on %s; fog dps %.0f"), *E->GetName(), A->FogDps);
				}
				else UE_LOG(LogWWO, Display, TEXT("AutoTest ability: spore fog damage has no enemy"));
			}
			if (Id == "shield_share")
			{
				AWWOAlly* Mate = nullptr;
				for (AWWOAlly* M : WS->LivingTeammates()) Mate = M;
				if (Mate)
				{
					AutoAbilityTarget = Mate;
					Mate->TakeAllyDamage(40.f);
					AutoAbilityValue = Mate->Shield;
					FWWOGrenadeSpec Spec;
					Spec.Color = A->Color;
					Spec.Damage = 0.f;
					Spec.BlastRadius = 2.f;
					AWWOGrenade::ExplodeAt(GetWorld(), Spec, Mate->GetActorLocation(), P);
				}
				else UE_LOG(LogWWO, Display, TEXT("AutoTest ability: shield share has no teammate"));
			}
		}
		if (Cross(0.4f) && Id == "shield_share")
		{
			if (const AWWOAlly* Mate = Cast<AWWOAlly>(AutoAbilityTarget.Get()))
			{
				UE_LOG(LogWWO, Display, TEXT("AutoTest ability: shield share: teammate shield %.0f -> %.0f after a blast (share %.0f)"), AutoAbilityValue, Mate->Shield, A->ShieldAmount);
			}
		}
		if (Cross(0.5f)) ShotNamed(TEXT("23_ability"), TEXT("hud"));
		const float End = Id == "spore_fog_damage" ? 3.f : 0.8f;
		if (Cross(1.2f) && Id == "spore_fog_damage") ShotNamed(TEXT("23_ability"), TEXT("fog"));
		if (Cross(End))
		{
			if (Id == "spore_fog_damage") Report(TEXT("spore fog damage"));
			Finish();
		}
		return false;
	}

	if (Id == "thornfist" || Id == "melee_guard")
	{
		const bool bThorn = Id == "thornfist";
		if (Cross(0.1f))
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest ability: %s before: charge %.2f, effect on %d, melee x%.2f, incoming melee x%.2f"), *Id.ToString(), Abilities->Charge(A),
				Abilities->IsEffectActive(A), Abilities->MeleeMultiplier(), Abilities->IncomingMeleeMultiplier());
			if (bThorn) Press(EWWOAction::Ability1);
		}
		if (Cross(0.2f))
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest ability: %s pressed: charge %.2f, effect on %d, melee x%.2f (melee damage %.0f)"), *Id.ToString(), Abilities->Charge(A),
				Abilities->IsEffectActive(A), Abilities->MeleeMultiplier(), P->Weapon->MeleeDamage);
			if (Target) PlaceForAutoTest(P, Target, 2.5f, 0.f);
			Snapshot();
			Press(EWWOAction::Melee);
		}
		if (!bAutoAbilityShot && AutoAbilityPhase > 0.2f && P->Weapon->bMeleeing && P->Weapon->MeleeProgress >= 0.45f)
		{
			bAutoAbilityShot = true;
			ShotNamed(TEXT("23_ability"), TEXT("hit"));
		}
		if (Cross(1.2f))
		{
			Report(bThorn ? TEXT("thornfist punch") : TEXT("melee guard punch"));
			UE_LOG(LogWWO, Display, TEXT("AutoTest ability: %s after the punch: charge %.3f, effect on %d, incoming melee x%.2f"), *Id.ToString(), Abilities->Charge(A),
				Abilities->IsEffectActive(A), Abilities->IncomingMeleeMultiplier());
			if (!bThorn)
			{
				P->Health->GodMode = 0.f;
				const float Before = P->Health->Health + P->Health->Shield;
				P->Health->TakeDamage(20.f, true);
				const float Mid = P->Health->Health + P->Health->Shield;
				P->Health->TakeDamage(20.f, false);
				const float After = P->Health->Health + P->Health->Shield;
				P->Health->GodMode = 1.f;
				UE_LOG(LogWWO, Display, TEXT("AutoTest ability: melee guard: 20 melee damage took %.1f, 20 ranged took %.1f"), Before - Mid, Mid - After);
				P->Health->Health = P->Health->MaxHealth();
				P->Health->Shield = P->Health->MaxShield();
			}
			ShotNamed(TEXT("23_ability"), TEXT("hud"));
		}
		const float End = bThorn ? 1.4f : 1.2f + A->Duration;
		if (Cross(End))
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest ability: %s at %.1f s: charge %.3f, effect on %d, incoming melee x%.2f"), *Id.ToString(), AutoAbilityPhase,
				Abilities->Charge(A), Abilities->IsEffectActive(A), Abilities->IncomingMeleeMultiplier());
			Finish();
		}
		return false;
	}

	if (Id == "static_charge")
	{
		if (!Target)
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest ability: static charge has no enemy"));
			Finish();
			return false;
		}
		if (Cross(0.1f))
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest ability: static charge starts at %.2f (needs %.0f damage); pressing while empty"), Abilities->Charge(A), A->ChargeDamage);
			Press(EWWOAction::Ability1);
		}
		if (Cross(0.2f))
		{
			const float Empty = Abilities->Charge(A);
			const bool bArmedEmpty = Abilities->IsEffectActive(A);
			P->Health->GodMode = 0.f;
			P->Health->TakeDamage(A->ChargeDamage * 0.5f);
			const float Half = Abilities->Charge(A);
			P->Health->TakeDamage(A->ChargeDamage * 0.5f);
			P->Health->GodMode = 1.f;
			UE_LOG(LogWWO, Display, TEXT("AutoTest ability: static charge: armed while empty %d (charge %.2f); after %.0f damage %.2f, after %.0f %.2f"), bArmedEmpty, Empty,
				A->ChargeDamage * 0.5f, Half, A->ChargeDamage, Abilities->Charge(A));
			P->Health->Health = P->Health->MaxHealth();
			P->Health->Shield = P->Health->MaxShield();
		}
		if (Cross(0.3f)) Press(EWWOAction::Ability1);
		if (Cross(0.4f))
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest ability: static charge armed %d (charge %.2f); jumps %.0f, range %.1f m, damage %.0f falling off x%.2f every %.2f s"),
				Abilities->IsEffectActive(A), Abilities->Charge(A), A->ChainJumps, A->ChainRange, A->ChainDamage, A->ChainFalloff, A->ChainDelay);
			ShotNamed(TEXT("23_ability"), TEXT("armed"));
		}
		if (Cross(0.5f))
		{
			const IWWOTargetable* T = Cast<IWWOTargetable>(Target);
			if (!T || T->IsDead()) AutoAbilityTarget = Target = NearestEnemy(false);
			if (Target) PlaceForAutoTest(P, Target, 2.5f, 0.f);
			Snapshot();
			Press(EWWOAction::Melee);
		}
		if (!bAutoAbilityShot && AutoAbilityPhase > 0.5f && P->Weapon->bMeleeing && P->Weapon->MeleeProgress >= 0.5f)
		{
			bAutoAbilityShot = true;
			ShotNamed(TEXT("23_ability"), TEXT("chain"));
		}
		if (Cross(2.2f))
		{
			Report(TEXT("static charge chain"));
			UE_LOG(LogWWO, Display, TEXT("AutoTest ability: static charge after: armed %d, charge %.2f"), Abilities->IsEffectActive(A), Abilities->Charge(A));
			Finish();
		}
		return false;
	}

	if (Id == "seedburst" || Id == "spore_patches")
	{
		const bool bSeeds = Id == "seedburst";
		if (Cross(0.1f))
		{
			if (const IWWOTargetable* T = Cast<IWWOTargetable>(Target)) AimAt(T->FeetLocation());
			Snapshot();
			P->Grenades->Charge = P->Grenades->MaxCharges();
			const bool bThrown = P->Grenades->ThrowGrenade();
			UE_LOG(LogWWO, Display, TEXT("AutoTest ability: %s threw %s %d (seeds %.0f at x%.2f, spread %.1f, fuse %.2f; patches up to %.0f, %.1f m, %.0f s)"), *Id.ToString(),
				Loadout->GetGrenade() ? *Loadout->GetGrenade()->Id.ToString() : TEXT("none"), bThrown, A->SubCount, A->SubScale, A->SubSpread, A->SubFuse,
				A->PatchMax, A->PatchRadius, A->PatchTime);
		}
		const int32 Live = bSeeds ? Abilities->LiveSeeds : Abilities->LivePatches;
		AutoAbilityValue = FMath::Max(AutoAbilityValue, float(Live));
		if (!bAutoAbilityFlag && AutoAbilityPhase > 0.1f && (bSeeds ? Live >= 2 : Live >= 1))
		{
			bAutoAbilityFlag = true;
			AutoAbilityFrom.X = AutoAbilityPhase;
			UE_LOG(LogWWO, Display, TEXT("AutoTest ability: %s lit at %.2f s: %d live, effect on %d"), *Id.ToString(), AutoAbilityPhase, Live, Abilities->IsEffectActive(A));
		}
		if (bAutoAbilityFlag && !bAutoAbilityShot && AutoAbilityPhase >= AutoAbilityFrom.X + (bSeeds ? 0.1f : 0.8f))
		{
			bAutoAbilityShot = true;
			ShotNamed(TEXT("23_ability"), bSeeds ? TEXT("seeds") : TEXT("patches"));
		}
		if (Cross(4.f))
		{
			Report(*Id.ToString());
			UE_LOG(LogWWO, Display, TEXT("AutoTest ability: %s at 4 s: most live %.0f, live now %d, effect on %d"), *Id.ToString(), AutoAbilityValue, Live, Abilities->IsEffectActive(A));
			Finish();
		}
		return false;
	}

	if (Id == "spore_dash")
	{
		if (Cross(0.1f))
		{
			AutoAbilityFrom = P->GetActorLocation();
			Press(EWWOAction::Ability1);
		}
		if (Cross(0.15f))
		{
			int32 Thick = 0;
			for (const TWeakObjectPtr<AWWOSporeFog>& Fog : WS->Fogs()) Thick += Fog.IsValid() && Fog->bBlocksSight && FVector::Dist2D(Fog->GetActorLocation(), AutoAbilityFrom) < 100.f;
			const FVector V = P->GetVelocity() / WWO::M;
			const float Back = -FVector::DotProduct(FVector(V.X, V.Y, 0.f), P->GetActorForwardVector());
			UE_LOG(LogWWO, Display, TEXT("AutoTest ability: spore dash: velocity %.2f m/s back, %.2f flat, %.2f up (dash %.1f / %.1f); thick fogs where you stood %d; charge %.2f, cooldown %.0f s"),
				Back, FVector2D(V).Size(), V.Z, A->DashSpeed, A->DashUp, Thick, Abilities->Charge(A), A->Cooldown);
		}
		if (Cross(0.45f)) ShotNamed(TEXT("23_ability"), TEXT("dash"));
		if (Cross(1.6f))
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest ability: spore dash moved %.2f m, on floor %d, charge %.3f"), FVector::Dist2D(AutoAbilityFrom, P->GetActorLocation()) / WWO::M,
				P->IsOnFloor(), Abilities->Charge(A));
			Finish();
		}
		return false;
	}

	if (Id == "root_bombs")
	{
		if (Cross(0.1f))
		{
			if (const IWWOTargetable* T = Cast<IWWOTargetable>(Target)) AimAt(T->GetAimParts().Last().Position);
			Snapshot();
			Press(EWWOAction::Ability1);
		}
		if (Cross(0.15f))
		{
			int32 Bombs = 0;
			for (TActorIterator<AWWOGrenade> It(GetWorld()); It; ++It) Bombs += It->Spec.bExplodeOnImpact && It->Spec.SizeScale > 1.5f;
			UE_LOG(LogWWO, Display, TEXT("AutoTest ability: root bombs out %d (damage %.0f, radius %.1f m, speed %.0f m/s); charge %.2f"), Bombs, A->BombDamage, A->BombRadius,
				A->BombSpeed, Abilities->Charge(A));
		}
		if (Cross(0.3f)) ShotNamed(TEXT("23_ability"), TEXT("thrown"));
		if (Cross(2.f))
		{
			Report(TEXT("root bombs"));
			UE_LOG(LogWWO, Display, TEXT("AutoTest ability: root bombs charge %.3f (cooldown %.0f s)"), Abilities->Charge(A), A->Cooldown);
			Finish();
		}
		return false;
	}

	// Anything else: log it and move on.
	if (Cross(0.5f))
	{
		ShotNamed(TEXT("23_ability"), TEXT("hud"));
		Finish();
	}
	return false;
}
