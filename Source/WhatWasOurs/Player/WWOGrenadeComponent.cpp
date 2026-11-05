#include "Player/WWOGrenadeComponent.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOAbilityComponent.h"
#include "Weapons/WWOGrenade.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Camera/CameraComponent.h"

UWWOGrenadeComponent::UWWOGrenadeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

const UWWOGrenadeData* UWWOGrenadeComponent::Current(const UWWOGrenadeData* Grenade) const
{
	if (Grenade) return Grenade;
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	return Loadout ? Loadout->GetGrenade() : nullptr;
}

float UWWOGrenadeComponent::EffectiveThrowDistance(const UWWOGrenadeData* Grenade) const
{
	const UWWOGrenadeData* G = Current(Grenade);
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	return P->M("throw_distance", ThrowDistance * (G ? G->ThrowDistanceMult : 1.f));
}

float UWWOGrenadeComponent::EffectiveRechargeTime(const UWWOGrenadeData* Grenade) const
{
	const UWWOGrenadeData* G = Current(Grenade);
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	const float Time = P->M("grenade_recharge", RechargeTime * (G ? G->RechargeMult : 1.f)); // traits
	return Time / P->EntrenchRechargeMultiplier(); // the IronBark ult speeds it up
}

float UWWOGrenadeComponent::EffectiveFuse(const UWWOGrenadeData* Grenade) const
{
	const UWWOGrenadeData* G = Current(Grenade);
	return DetonationTime * (G ? G->FuseMult : 1.f);
}

float UWWOGrenadeComponent::MaxCharges() const
{
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	return 1.f + P->Abilities->ExtraCharges(); // Extra Grenade adds a charge
}

void UWWOGrenadeComponent::UpdateGrenades(float DeltaSeconds)
{
	if (Charge < MaxCharges()) Charge = FMath::Min(Charge + DeltaSeconds / FMath::Max(EffectiveRechargeTime(), 0.1f), MaxCharges());
	Charge = FMath::Min(Charge, MaxCharges()); // Extra Grenade unequipped
	if (ReleaseLeft >= 0.f)
	{
		ReleaseLeft -= DeltaSeconds;
		if (ReleaseLeft < 0.f) Release();
	}
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	AWWOPlayerController* PC = P->GetWWOController();
	const bool bPressed = PC && (PC->WasJustPressed(EWWOAction::Grenade) || PC->ConsumeChord(EWWOChord::Grenade)); // G, or L1 alone
	if (bPressed && !P->bUlting) ThrowGrenade();
}

bool UWWOGrenadeComponent::ThrowGrenade()
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	UWWOWeaponComponent* Gun = P->Weapon;
	if (!IsReady() || Gun->bMeleeing || P->IsMantling() || !Loadout || !Loadout->GetGrenade() || Gun->IsWindingThrow()) return false;
	Charge -= 1.f;
	Gun->CancelReload(); // throwing cancels a reload, like Destiny
	Gun->StartThrowPose(Loadout->GetGrenade()->Color);
	ReleaseLeft = FMath::Max(Gun->ThrowRelease, 0.f); // the left arm winds up; the grenade leaves partway through
	return true;
}

void UWWOGrenadeComponent::Release()
{
	// The grenade leaves your hand, toward wherever you're looking now: a bit above it,
	// fast enough to land ThrowDistance away on flat ground when you look level.
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	UWWOGrenadeData* Data = Loadout ? Loadout->GetGrenade() : nullptr;
	if (!Data) return;
	const FVector Dir = P->AimForward().RotateAngleAxis(-LaunchAngle, P->Camera->GetRightVector()).GetSafeNormal();
	const float Gravity = 9.8f;
	const float Speed = FMath::Sqrt(EffectiveThrowDistance() * Gravity / FMath::Sin(FMath::DegreesToRadians(2.f * LaunchAngle)));

	FWWOGrenadeSpec Spec;
	Spec.Color = Data->Color;
	Spec.Damage = Data->Damage;
	Spec.BlastRadius = Data->BlastRadius * (1.f + Loadout->TraitBonus("blast_bigger")); // Bigger Explosions
	Spec.Fuse = EffectiveFuse();
	Spec.Data = Data;
	Spec.Seedburst = P->Abilities->Equipped("seedburst");
	Spec.SporePatches = P->Abilities->Equipped("spore_patches");
	Spec.bExplodeOnImpact = Loadout->TraitBonus("impact_fuse") > 0.f && Data->Kind != EWWOGrenadeKind::Spike && Data->Kind != EWWOGrenadeKind::Tripmine;
	Spec.ThrownFrom = P->GetActorLocation();
	// Where it leaves your (left) hand, relative to the camera.
	const FVector Hand = P->Camera->GetComponentTransform().TransformPosition(FVector(50.f, -8.f, -6.f));
	AWWOGrenade::Throw(GetWorld(), Spec, Hand, Dir * Speed * WWO::M, P);
}
