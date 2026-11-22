#include "Player/WWOAbilityComponent.h"
#include "Characters/WWOBodyComponent.h"
#include "Systems/WWOSound.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOBotController.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOPlayerController.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Camera/CameraComponent.h"
#include "WhatWasOurs.h"
#include "WWOIds.h"
#include "Player/WWOAbilities.h"
#include "Flora/WWODawnLotus.h"

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
	for (const FWWOAbility& E : WWOAbility::Table()) if (E.Tick) E.Tick(State(E.Id), DeltaSeconds);
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	// While Thornfist is up (or Static Charge is armed) the fists glow in its color, so whoever is
	// about to be hit can see it coming.
	// It's known on the host (for everyone) and on your own machine (for you); the host passes it on
	// to the rest (AWWOCharacter::NetFists).
	if (P && (P->HasAuthority() || P->IsLocalHuman()))
	{
		const UWWOAbilityData* Lit = nullptr;
		for (const FWWOAbility& E : WWOAbility::Table())
		{
			const UWWOAbilityData* A = E.bFistsGlow ? Equipped(E.Id) : nullptr;
			if (A && IsEffectActive(A)) { Lit = A; break; }
		}
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
			// No button: once it's full it arms itself, and the next melee hit discharges it. (Each
			// machine that follows this player's charge, theirs and the host's, arms its own copy.)
			if (Charge(A) >= 1.f && !State(A->Id).bArmed)
			{
				State(A->Id).bArmed = true;
				if (P->IsLocallyControlled() || Bot) UWWOSound::PlayFor(P, "ability"); // the sound of it coming ready
			}
			continue;
		}
		if (!A || A->Kind != EWWOAbilityKind::Active) continue;
		const float Recharged = FMath::Min(Charge(A) + DeltaSeconds / FMath::Max(WWODawnLotus::AbilityCooldown(*A), 0.1f), 1.f);
		FWWOAbilityState& S = State(A->Id);
		S.Charge = Recharged;
		S.ActiveLeft = FMath::Max(S.ActiveLeft - DeltaSeconds, 0.f);
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
	const UWWOAbilityData* Dash = Equipped(WWOIds::Ability::SporeDash);
	return Dash && (Charge(Dash) >= 0.9f || Charge(Dash) * FMath::Max(Dash->Cooldown, 0.1f) < 1.5f);
}

void UWWOAbilityComponent::UndoUse(int32 Slot)
{
	// The host refused it: what this machine started is taken back and the charge returned.
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	const UWWOAbilityData* A = Loadout ? Loadout->GetAbility(Slot) : nullptr;
	if (!A) return;
	FWWOAbilityState& S = State(A->Id);
	S.bArmed = false;
	S.ActiveLeft = 0.f;
	if (A->Kind == EWWOAbilityKind::Active) S.Charge = 1.f;
}

EWWOKitRefusal UWWOAbilityComponent::HostUseAbility(int32 Slot)
{
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this); // the joiner's copy
	UWWOAbilityData* A = Loadout ? Loadout->GetAbility(Slot) : nullptr;
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	if (!A || !P || P->Health->bDead)
	{
		const EWWOKitRefusal Why = A ? EWWOKitRefusal::Dead : EWWOKitRefusal::EmptySlot;
		UE_LOG(LogWWO, Display, TEXT("NetAbility refused from %s: slot %d %s"), *GetNameSafe(P), Slot, *WWOKit::RefusalText(EWWOKit::Ability, Why));
		return Why;
	}
	constexpr float Ready = 0.95f; // a little slack: the two machines' recharges run a frame or two apart
	bool bDone = false;
	if (A->Kind == EWWOAbilityKind::Charged && Charge(A) >= Ready)
	{
		State(A->Id).bArmed = true;
		bDone = true;
	}
	else if (A->Kind == EWWOAbilityKind::Active) bDone = ActivateAbility(A, Ready);
	const EWWOKitRefusal Why = bDone ? EWWOKitRefusal::None : EWWOKitRefusal::NotReady;
	UE_LOG(LogWWO, Display, TEXT("NetAbility %s from %s: %s (charge %.2f)"), *A->Id.ToString(), *P->GetName(), *WWOKit::RefusalText(EWWOKit::Ability, Why), Charge(A));
	return Why;
}

bool UWWOAbilityComponent::ActivateAbility(UWWOAbilityData* A, float Ready)
{
	if (A->Kind != EWWOAbilityKind::Active || Charge(A) < Ready) return false;
	FWWOAbilityState& S = State(A->Id);
	S.Charge = 0.f;
	S.ActiveLeft = A->Duration;
	UWWOSound::PlayFor(GetOwner(), "ability");
	if (const FWWOAbility* E = WWOAbility::Find(A->Id); E && E->OnActivate) E->OnActivate(*this, *A);
	return true;
}

void UWWOAbilityComponent::AddEnergy(float Amount, FName Id)
{
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	if (!Loadout) return;
	for (int32 Slot = 0; Slot < UWWOLoadoutSubsystem::AbilitySlots; ++Slot)
	{
		const UWWOAbilityData* A = Loadout->GetAbility(Slot);
		if (!A || (!Id.IsNone() && A->Id != Id)) continue;
		if (A->Kind != EWWOAbilityKind::Active && A->Kind != EWWOAbilityKind::Charged) continue;
		FWWOAbilityState& S = State(A->Id);
		if (!S.bArmed) S.Charge = FMath::Min(WWOAbility::ChargeOf(*A, &S) + Amount, 1.f);
	}
}

float UWWOAbilityComponent::Charge(const UWWOAbilityData* A) const
{
	// Charged abilities start empty; everything else starts ready.
	return WWOAbility::ChargeOf(*A, FindState(A->Id));
}

bool UWWOAbilityComponent::IsEffectActive(const UWWOAbilityData* A) const
{
	const FWWOAbilityState* S = FindState(A->Id);
	switch (A->Kind)
	{
	case EWWOAbilityKind::Active: return S && S->ActiveLeft > 0.f;
	case EWWOAbilityKind::Charged: return S && S->bArmed;
	case EWWOAbilityKind::Triggered:
	{
		const FWWOAbility* E = WWOAbility::Find(A->Id);
		return E && E->Lit && E->Lit(S ? *S : FWWOAbilityState());
	}
	default: return true; // passives are always on
	}
}

float UWWOAbilityComponent::GuardTimeLeft() const
{
	const FWWOAbilityState* S = FindState(WWOIds::Ability::MeleeGuard);
	return S ? S->LitLeft : 0.f;
}

float UWWOAbilityComponent::SpeedMultiplier() const
{
	float Mult = 1.f;
	for (const FWWOAbility& E : WWOAbility::Table())
	{
		if (const UWWOAbilityData* A = E.MoveSpeed ? Equipped(E.Id) : nullptr) Mult *= E.MoveSpeed(*A);
	}
	return Mult;
}

int32 UWWOAbilityComponent::ExtraJumps() const
{
	int32 Jumps = 0;
	for (const FWWOAbility& E : WWOAbility::Table())
	{
		if (const UWWOAbilityData* A = E.ExtraJumps ? Equipped(E.Id) : nullptr) Jumps += E.ExtraJumps(*A);
	}
	return Jumps;
}

void UWWOAbilityComponent::OnMeleeLanded()
{
	for (const FWWOAbility& E : WWOAbility::Table())
	{
		if (const UWWOAbilityData* A = E.OnMeleeLanded ? Equipped(E.Id) : nullptr) E.OnMeleeLanded(*A, State(E.Id));
	}
}

void UWWOAbilityComponent::OnDamageTaken(float Amount)
{
	for (const FWWOAbility& E : WWOAbility::Table())
	{
		if (const UWWOAbilityData* A = E.OnDamageTaken ? Equipped(E.Id) : nullptr) E.OnDamageTaken(*A, State(E.Id), Amount);
	}
}

UWWOAbilityData* UWWOAbilityComponent::ConsumeStaticCharge()
{
	UWWOAbilityData* A = Equipped(WWOIds::Ability::StaticCharge);
	if (!A || !FindState(A->Id) || !FindState(A->Id)->bArmed) return nullptr;
	FWWOAbilityState& S = State(A->Id);
	S.bArmed = false;
	S.Charge = 0.f;
	return A;
}

float UWWOAbilityComponent::IncomingMeleeMultiplier() const
{
	float Mult = 1.f;
	for (const FWWOAbility& E : WWOAbility::Table())
	{
		if (const UWWOAbilityData* A = E.MeleeTaken ? Equipped(E.Id) : nullptr)
		{
			const FWWOAbilityState* S = FindState(E.Id);
			Mult *= E.MeleeTaken(*A, S ? *S : FWWOAbilityState());
		}
	}
	return Mult;
}

float UWWOAbilityComponent::ExtraCharges() const
{
	float Extra = 0.f;
	for (const FWWOAbility& E : WWOAbility::Table())
	{
		if (const UWWOAbilityData* A = E.GrenadeCharges ? Equipped(E.Id) : nullptr) Extra += E.GrenadeCharges(*A);
	}
	return Extra;
}

UWWOAbilityData* UWWOAbilityComponent::ThornfistActive() const
{
	UWWOAbilityData* A = Equipped(WWOIds::Ability::Thornfist);
	return A && IsEffectActive(A) ? A : nullptr;
}

float UWWOAbilityComponent::MeleeMultiplier() const
{
	float Mult = 1.f;
	for (const FWWOAbility& E : WWOAbility::Table())
	{
		if (const UWWOAbilityData* A = E.MeleeDealt ? Equipped(E.Id) : nullptr)
		{
			const FWWOAbilityState* S = FindState(E.Id);
			Mult *= E.MeleeDealt(*A, S ? *S : FWWOAbilityState());
		}
	}
	return Mult;
}
