#include "Player/WWOAbilityComponent.h"
#include "Characters/WWOBodyComponent.h"
#include "Systems/WWOSound.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOBotController.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOPlayerController.h"
#include "Weapons/WWOGrenade.h"
#include "Flora/WWOSporeFog.h"
#include "FX/WWOFX.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Camera/CameraComponent.h"
#include "WhatWasOurs.h"

UWWOAbilityComponent::UWWOAbilityComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UWWOAbilityComponent::PlayerBusy() const
{
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	return P && P->bUlting;
}

UWWOAbilityData* UWWOAbilityComponent::Equipped(FName Id) const
{
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	if (!Loadout) return nullptr;
	for (int32 Slot = 0; Slot < UWWOLoadoutSubsystem::AbilitySlots; ++Slot)
	{
		UWWOAbilityData* A = Loadout->GetAbility(Slot);
		if (A && A->Id == Id) return A;
	}
	return nullptr;
}

void UWWOAbilityComponent::UpdateAbilities(float DeltaSeconds)
{
	GuardLeft = FMath::Max(GuardLeft - DeltaSeconds, 0.f);
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	// While Thornfist is up (or Static Charge is armed) the fists glow in its color, so whoever is
	// about to be hit can see it coming.
	// It's known on the host (for everyone) and on your own machine (for you); the host passes it on
	// to the rest (AWWOCharacter::NetFists).
	if (P && (P->HasAuthority() || (P->IsLocallyControlled() && P->IsPlayerControlled())))
	{
		const UWWOAbilityData* Lit = ThornfistActive();
		if (!Lit) { if (const UWWOAbilityData* Charge = Equipped("static_charge")) if (Armed.FindRef(Charge->Id)) Lit = Charge; }
		if (Lit != FistsLit.Get())
		{
			FistsLit = Lit;
			P->Body->SetHandGlow(Lit ? Lit->Color : FLinearColor::Black, Lit ? 0.85f : 0.f);
			if (P->HasAuthority()) const_cast<AWWOCharacter*>(P)->NetFists = Lit ? Lit->Id : NAME_None;
		}
	}
	const AWWOPlayerController* PC = P ? P->GetWWOController() : nullptr;
	const AWWOBotController* Bot = P ? P->GetBot() : nullptr; // a bot presses its own buttons
	if (!Loadout || (!PC && !Bot)) return;
	for (int32 Slot = 0; Slot < UWWOLoadoutSubsystem::AbilitySlots; ++Slot)
	{
		UWWOAbilityData* A = Loadout->GetAbility(Slot);
		const EWWOAction Button = Slot == 0 ? EWWOAction::Ability1 : EWWOAction::Ability2;
		const bool bPressed = Bot ? Bot->WasJustPressed(Button) : PC->WasJustPressed(Button);
		if (A && A->Kind == EWWOAbilityKind::Charged)
		{
			if (bPressed && !PlayerBusy() && Charge(A) >= 1.f)
			{
				Armed.Add(A->Id, true); // the next melee hit discharges it
				UWWOSound::PlayFor(P, "ability");
				if (AWWOCharacter* Me = Cast<AWWOCharacter>(GetOwner()); Me && GetNetMode() == NM_Client) Me->ServerUseAbility(Slot, Me->AimForward());
			}
			continue;
		}
		if (!A || A->Kind != EWWOAbilityKind::Active) continue;
		ChargeById.Add(A->Id, FMath::Min(Charge(A) + DeltaSeconds / FMath::Max(A->Cooldown, 0.1f), 1.f));
		ActiveLeft.Add(A->Id, FMath::Max(ActiveLeft.FindRef(A->Id) - DeltaSeconds, 0.f));
		if (bPressed && !PlayerBusy() && ActivateAbility(A) && GetNetMode() == NM_Client)
		{
			if (AWWOCharacter* Me = Cast<AWWOCharacter>(GetOwner())) Me->ServerUseAbility(Slot, Me->AimForward()); // the host runs it too
		}
	}
}

bool UWWOAbilityComponent::CanLaunchNow() const
{
	// Its use reaches the host on another channel than the move that carries the launch, so either
	// order is fine: still charged (the use is on its way) or just spent (it came first).
	const UWWOAbilityData* Dash = Equipped("spore_dash");
	return Dash && (Charge(Dash) >= 0.9f || Charge(Dash) * FMath::Max(Dash->Cooldown, 0.1f) < 1.5f);
}

void UWWOAbilityComponent::UndoUse(int32 Slot)
{
	// The host refused it: what this machine started is taken back and the charge returned.
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	const UWWOAbilityData* A = Loadout ? Loadout->GetAbility(Slot) : nullptr;
	if (!A) return;
	Armed.Remove(A->Id);
	ActiveLeft.Add(A->Id, 0.f);
	if (A->Kind == EWWOAbilityKind::Active) ChargeById.Add(A->Id, 1.f);
}

bool UWWOAbilityComponent::HostUseAbility(int32 Slot)
{
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this); // the joiner's copy
	UWWOAbilityData* A = Loadout ? Loadout->GetAbility(Slot) : nullptr;
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	if (!A || !P || P->Health->bDead)
	{
		UE_LOG(LogWWO, Display, TEXT("NetAbility refused from %s: slot %d %s"), *GetNameSafe(P), Slot, A ? TEXT("dead") : TEXT("empty"));
		return false;
	}
	constexpr float Ready = 0.95f; // a little slack: the two machines' recharges run a frame or two apart
	bool bDone = false;
	if (A->Kind == EWWOAbilityKind::Charged && Charge(A) >= Ready)
	{
		Armed.Add(A->Id, true);
		bDone = true;
	}
	else if (A->Kind == EWWOAbilityKind::Active) bDone = ActivateAbility(A, Ready);
	UE_LOG(LogWWO, Display, TEXT("NetAbility %s from %s: %s (charge %.2f)"), *A->Id.ToString(), *P->GetName(), bDone ? TEXT("used") : TEXT("refused, not ready"), Charge(A));
	return bDone;
}

bool UWWOAbilityComponent::ActivateAbility(UWWOAbilityData* A, float Ready)
{
	if (A->Kind != EWWOAbilityKind::Active || Charge(A) < Ready) return false;
	ChargeById.Add(A->Id, 0.f);
	ActiveLeft.Add(A->Id, A->Duration);
	UWWOSound::PlayFor(GetOwner(), "ability");
	if (A->Id == "spore_dash") SporeDash(A);
	if (A->Id == "root_bombs") RootBombs(A);
	return true;
}

float UWWOAbilityComponent::Charge(const UWWOAbilityData* A) const
{
	// Charged abilities start empty; everything else starts ready.
	const float* Found = ChargeById.Find(A->Id);
	return Found ? *Found : (A->Kind == EWWOAbilityKind::Charged ? 0.f : 1.f);
}

bool UWWOAbilityComponent::IsEffectActive(const UWWOAbilityData* A) const
{
	switch (A->Kind)
	{
	case EWWOAbilityKind::Active: return ActiveLeft.FindRef(A->Id) > 0.f;
	case EWWOAbilityKind::Charged: return Armed.FindRef(A->Id);
	case EWWOAbilityKind::Triggered:
		if (A->Id == "melee_guard") return GuardLeft > 0.f;
		if (A->Id == "spore_patches") return LivePatches > 0;
		return LiveSeeds > 0;
	default: return true; // passives are always on
	}
}

float UWWOAbilityComponent::SpeedMultiplier() const
{
	const UWWOAbilityData* A = Equipped("overgrowth");
	return A ? A->SpeedMult : 1.f;
}

int32 UWWOAbilityComponent::ExtraJumps() const
{
	const UWWOAbilityData* A = Equipped("overgrowth");
	return A ? FMath::RoundToInt(A->ExtraJumps) : 0;
}

void UWWOAbilityComponent::OnMeleeLanded()
{
	if (const UWWOAbilityData* Guard = Equipped("melee_guard")) GuardLeft = Guard->Duration;
}

void UWWOAbilityComponent::OnDamageTaken(float Amount)
{
	// Static Charge fills up from the damage you take (not while it's armed and waiting).
	const UWWOAbilityData* A = Equipped("static_charge");
	if (A && !Armed.FindRef(A->Id))
	{
		ChargeById.Add(A->Id, FMath::Min(Charge(A) + Amount / FMath::Max(A->ChargeDamage, 1.f), 1.f));
	}
}

UWWOAbilityData* UWWOAbilityComponent::ConsumeStaticCharge()
{
	UWWOAbilityData* A = Equipped("static_charge");
	if (!A || !Armed.FindRef(A->Id)) return nullptr;
	Armed.Add(A->Id, false);
	ChargeById.Add(A->Id, 0.f);
	return A;
}

float UWWOAbilityComponent::IncomingMeleeMultiplier() const
{
	const UWWOAbilityData* Guard = Equipped("melee_guard");
	return Guard && GuardLeft > 0.f ? 1.f - Guard->GuardReduction : 1.f;
}

float UWWOAbilityComponent::ExtraCharges() const
{
	const UWWOAbilityData* A = Equipped("extra_grenade");
	return A ? A->ExtraCharges : 0.f;
}

UWWOAbilityData* UWWOAbilityComponent::ThornfistActive() const
{
	UWWOAbilityData* A = Equipped("thornfist");
	return A && IsEffectActive(A) ? A : nullptr;
}

float UWWOAbilityComponent::MeleeMultiplier() const
{
	const UWWOAbilityData* A = ThornfistActive();
	return A ? A->MeleeDamageMult : 1.f;
}

void UWWOAbilityComponent::SporeDash(UWWOAbilityData* A)
{
	// Launch backwards (and a little up, to keep the speed through the air) and leave a
	// thick fog where you were standing.
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	const FVector At = P->GetActorLocation();
	if (P->HasAuthority()) AWWOSporeFog::SpawnFog(GetWorld(), At, A->DashFogRadius, A->DashFogTime, true, GetOwner()); // online it replicates
	AWWOParticles::Burst(GetWorld(), At, WWO::WithAlpha(WWO::Lightened(AWWOSporeFog::Color(), 0.15f), 0.5f), 18, 1.f, 1.5f, 4.f, 0.9f,
		FVector(0, 0, 0.3f), EWWOParticleStyle::Smoke);
	AWWOParticles::Burst(GetWorld(), At, WWO::Lightened(AWWOSporeFog::Color(), 0.4f), 24, 0.7f, 2.f, 5.f, 0.06f, FVector(0, 0, -2.f));
	if (!P->IsLocallyControlled()) return; // the launch is the dashing player's own move
	FVector Back = -P->GetActorForwardVector();
	Back.Z = 0.f;
	P->Launch((Back.GetSafeNormal() * A->DashSpeed + FVector(0, 0, A->DashUp)) * WWO::M);
}

void UWWOAbilityComponent::RootBombs(UWWOAbilityData* A)
{
	// Two big bombs, thrown a little left and right of where you're looking, that blow up the
	// moment they hit something.
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	if (!P->HasAuthority()) return; // the host throws a joiner's bombs (they replicate)
	const float Bigger = 1.f + UWWOLoadoutSubsystem::Bonus(this, "blast_bigger");
	// Where the thrower is looking: its own aim here, or for a joiner (whose copy here has no camera
	// pitch) the direction it sent with the ability.
	const FVector Aim = P->IsLocallyControlled() ? P->AimForward() : P->NetAim;
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Aim).GetSafeNormal(UE_SMALL_NUMBER, P->GetActorRightVector());
	TArray<AWWOGrenade*> Thrown;
	for (const float Side : { -1.f, 1.f })
	{
		FWWOGrenadeSpec Spec;
		Spec.Color = A->Color;
		Spec.Damage = A->BombDamage;
		Spec.BlastRadius = A->BombRadius * Bigger;
		Spec.Fuse = 6.f; // backup, in case it never hits anything
		Spec.SizeScale = 2.f;
		Spec.bExplodeOnImpact = true;
		Spec.Flavor = A->Flora;
		Spec.ThrownFrom = P->GetActorLocation();
		const FVector At = P->EyeLocation() + Aim * 60.f + Right * 35.f * Side - FVector(0, 0, 20.f);
		FVector Dir = Aim.RotateAngleAxis(1.5f * Side, FVector::UpVector);
		Dir = Dir.RotateAngleAxis(-2.f, Right); // a little upward
		AWWOGrenade* Bomb = AWWOGrenade::Throw(GetWorld(), Spec, At, Dir.GetSafeNormal() * A->BombSpeed * WWO::M, P);
		if (!Bomb) continue;
		for (AWWOGrenade* Other : Thrown) Bomb->IgnoreActor(Other); // the pair never hit each other
		Thrown.Add(Bomb);
	}
}
