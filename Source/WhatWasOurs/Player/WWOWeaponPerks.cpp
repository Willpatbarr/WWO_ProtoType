// UWWOWeaponComponent: the stats rebuilt each frame from the asset, mods and active perks (RefreshStats),
// what a shot's result does to the perks, kill perks, and the HUD's perk line (PerkStatus). What each
// perk does is its entry in the perk table (WWOPerk::Table(), Weapons/WWOPerks.cpp); these run it.

#include "Player/WWOWeaponComponent.h"
#include "WhatWasOurs.h"
#include "Player/WWOWeaponPrivate.h"
#include "Player/WWOCharacter.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Engine/World.h"
#include "Weapons/WWOPerks.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOTargetable.h"

using namespace WWOWeapon; // the helpers these files share (WWOWeaponPrivate.h)

void UWWOWeaponComponent::RefreshStats()
{
	// Copy the asset (Settings sliders edit it live), then apply the mods' percentages to the
	// stats read straight off the weapon (WWOPerk::ComputeGunStats, as the host does for a joiner's
	// gun). Reload, aim-in, swap and ready times go through M() instead, where the loadout adds the
	// mods' percentages itself.
	if (!BaseWeapon || !Weapon) return;
	for (TFieldIterator<FProperty> It(UWWOWeaponData::StaticClass()); It; ++It)
	{
		It->CopyCompleteValue_InContainer(Weapon, BaseWeapon);
	}
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return;
	FWWOGunStats Stats = WWOPerk::ComputeGunStats(*Weapon, [L](FName Stat) { return L->ModPercent(Stat); });
	// Perks that change stats while they're active.
	const FWWOPerkView View = PerkView();
	for (const FWWOPerk* Perk : WWOPerk::Equipped(*L)) if (Perk->Stats) Perk->Stats(Stats, View);
	Stats.WriteTo(*Weapon);
}

bool UWWOWeaponComponent::IsAnchored() const
{
	// Anchor: aimed in and (nearly) standing still.
	const AWWOCharacter* Pl = GetPlayer();
	return Pl && AdsAmount > 0.5f && Pl->GetVelocity().Size2D() < 0.3f * WWO::M;
}

FWWOPerkView UWWOWeaponComponent::PerkView() const
{
	const AWWOCharacter* Pl = GetPlayer();
	FWWOPerkView View{ PerkState };
	View.AdsAmount = AdsAmount;
	View.Ammo = Ammo;
	View.bCrouching = Pl && Pl->MoveState.bCrouching;
	View.bSliding = Pl && Pl->MoveState.bSliding;
	View.bAnchored = IsAnchored();
	return View;
}

void UWWOWeaponComponent::OnShotResult(AActor* FirstHit, int32 HitCount, int32 Kills, bool bCritical)
{
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return;
	PerkState.bOverpressure = false; // any shot uses it up, hit or miss
	PerkState.AimHeld = 0.f;         // Patience and Held Breath start over
	if (HitCount > 0) ++PerkState.BurstHits; // Full Burst
	// Focus: a shot that hits nothing ends the streak.
	PerkState.HitStreak = HitCount > 0 ? PerkState.HitStreak + 1 : 0;
	// A joiner's arcs are the host's (ServerGunShot runs FWWOPerk::HostShot).
	FWWOPerkShot Shot{ PerkState, GetWorld(), GetOwner(), FirstHit, HitCount, bCritical, GetNetMode() == NM_Client, Weapon->Damage };
	for (const FWWOPerk* Perk : WWOPerk::Equipped(*L)) if (Perk->OnShot) Perk->OnShot(Shot);
	ApplyKillPerks(Kills);
}

void UWWOWeaponComponent::ApplyKillPerks(int32 Kills)
{
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L || Kills <= 0) return;
	FWWOPerkKill Kill{ PerkState, Ammo, MagazineSize(), Kills };
	for (const FWWOPerk* Perk : WWOPerk::Equipped(*L)) if (Perk->OnKill) Perk->OnKill(Kill);
}

void UWWOWeaponComponent::OnKillsConfirmed(int32 Kills, bool bMelee)
{
	if (Kills <= 0) return;
	if (!bMelee)
	{
		ApplyKillPerks(Kills);
		if (!bLastShotPredictedKill) OnHitConfirmed.Broadcast(false, true); // a kill this machine didn't see coming
		bLastShotPredictedKill = false;
	}
}

FString UWWOWeaponComponent::PerkStatus() const
{
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return FString();
	FWWOPerkView View = PerkView();
	// The ones that depend on what you're aiming at: lit while the target under the reticle qualifies.
	const AWWOCharacter* Pl = GetPlayer();
	if (const IWWOTargetable* Aimed = Cast<IWWOTargetable>(ReticleTarget.Get()); Aimed && Pl)
	{
		View.Aimed = Aimed;
		View.AimedMeters = FVector::Dist(Pl->EyeLocation(), Aimed->BodyCenter()) / WWO::M;
	}
	TArray<FString> Active;
	for (const FWWOPerk* Perk : WWOPerk::Equipped(*L))
	{
		if (!Perk->Hud) continue;
		FString Text = Perk->Hud(View);
		if (!Text.IsEmpty()) Active.Add(MoveTemp(Text));
	}
	return FString::Join(Active, TEXT("  ·  "));
}
