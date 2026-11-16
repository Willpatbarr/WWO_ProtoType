// UWWOWeaponComponent: the stats rebuilt each frame from the asset, mods and active perks (RefreshStats),
// what a shot's result does to the perks, kill perks, and the HUD's perk line (PerkStatus).

#include "Player/WWOWeaponComponent.h"
#include "WhatWasOurs.h"
#include "Player/WWOWeaponPrivate.h"
#include "Player/WWOCharacter.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Engine/World.h"
#include "Weapons/WWOPerks.h"
#include "Flora/WWOFloraEffects.h"
#include "FX/WWOFX.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOTargetable.h"

using namespace WWOWeapon; // the helpers these files share (WWOWeaponPrivate.h)

namespace
{
	using namespace WWOPerk; // the perks' numbers (Weapons/WWOPerks.h, shared with the host's checks)
}

void UWWOWeaponComponent::RefreshStats()
{
	// Copy the asset (Settings sliders edit it live), then apply the mods' percentages to the
	// stats read straight off the weapon. Reload, aim-in, swap and ready times go through
	// M() instead, where the loadout adds the mods' percentages itself.
	if (!BaseWeapon || !Weapon) return;
	for (TFieldIterator<FProperty> It(UWWOWeaponData::StaticClass()); It; ++It)
	{
		It->CopyCompleteValue_InContainer(Weapon, BaseWeapon);
	}
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return;
	auto Scale = [L](float& Value, FName Stat) { Value *= FMath::Max(1.f + L->ModPercent(Stat), 0.f); };
	Scale(Weapon->FireRange, "range");
	Scale(Weapon->RecoilSide, "recoil_side");
	Scale(Weapon->RecoilUp, "recoil_up");
	Scale(Weapon->MagazineSize, "magazine_size");
	Scale(Weapon->AdsMoveSpeed, "ads_move_speed");
	Scale(Weapon->Sway, "sway");
	Scale(Weapon->Damage, "damage");
	Scale(Weapon->FireRate, "fire_rate");
	Scale(Weapon->CritMultiplier, "crit_multiplier");
	Scale(Weapon->PelletSpread, "pellet_spread");
	Scale(Weapon->HipSpread, "hip_spread");
	// Zoom is a magnification: +40% means 40% more of the zoom past 1x.
	Weapon->Zoom = 1.f + (Weapon->Zoom - 1.f) * FMath::Max(1.f + L->ModPercent("zoom"), 0.f);
	Weapon->MagazineSize = FMath::Max(1.f, FMath::RoundToFloat(Weapon->MagazineSize));

	// Perks that change stats while they're active.
	const AWWOCharacter* Pl = GetPlayer();
	if (L->HasPerk("focus") && HitStreak >= 5) Weapon->Damage *= Focus;
	if (L->HasPerk("brace") && Pl && (Pl->MoveState.bCrouching || Pl->MoveState.bSliding))
	{
		Weapon->RecoilUp *= 0.6f;
		Weapon->RecoilSide *= 0.6f;
	}
	if (L->HasPerk("hunter")) Weapon->CritMultiplier += HunterCrit * AdsAmount;
	if (L->HasPerk("anchor") && IsAnchored())
	{
		Weapon->RecoilUp *= 0.5f;
		Weapon->RecoilSide *= 0.5f;
	}
	if (L->HasPerk("hammer") && HammerLeft > 0.f) Weapon->FireRate *= HammerRate;
	if (L->HasPerk("breath") && AimHeld >= BreathTime)
	{
		Weapon->Sway *= 0.1f;
		Weapon->RecoilUp *= 0.7f;
		Weapon->RecoilSide *= 0.7f;
	}
	if (L->HasPerk("slide_shot") && Pl && Pl->MoveState.bSliding)
	{
		Weapon->PelletSpread *= 0.5f;
		Weapon->Damage *= SlideShot;
	}
	if (L->HasPerk("tighten")) Weapon->PelletSpread *= 1.f - 0.3f * AdsAmount;
	if (L->HasPerk("fourth_round") && Weapon->BurstCount() > 1) Weapon->Burst = Weapon->BurstCount() + 1.f;
}

bool UWWOWeaponComponent::IsAnchored() const
{
	// Anchor: aimed in and (nearly) standing still.
	const AWWOCharacter* Pl = GetPlayer();
	return Pl && AdsAmount > 0.5f && Pl->GetVelocity().Size2D() < 0.3f * WWO::M;
}

void UWWOWeaponComponent::OnShotResult(AActor* FirstHit, int32 HitCount, int32 Kills, bool bCritical)
{
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return;
	bOverpressure = false; // any shot uses it up, hit or miss
	AimHeld = 0.f;         // Patience and Held Breath start over
	if (HitCount > 0) ++BurstHits; // Full Burst
	if (bCritical && L->HasPerk("refund")) ++RefundRounds; // Marksman
	const bool bJoiner = GetNetMode() == NM_Client; // a joiner's arcs are the host's (ServerGunShot)
	if (!bJoiner && bCritical && FirstHit && L->HasPerk("ricochet"))
	{
		// One bounce from the target that was hit, for 60% of the gun's damage.
		AWWOChainLightning::SpawnFrom(GetWorld(), FirstHit, GetOwner(), FLinearColor(1.f, 0.62f, 0.2f),
			Weapon->Damage * 0.6f, 8.f, 1.f, 1, 0.04f);
	}
	// Focus: a shot that hits nothing ends the streak.
	HitStreak = HitCount > 0 ? HitStreak + 1 : 0;
	ApplyKillPerks(Kills);
	if (HitCount > 0 && L->HasPerk("static"))
	{
		StaticHits += 1;
		if (StaticHits >= 10 && (FirstHit || bJoiner))
		{
			StaticHits = 0;
			// Two jumps from the target that was hit, for twice the gun's damage each.
			if (!bJoiner) AWWOChainLightning::SpawnFrom(GetWorld(), FirstHit, GetOwner(), FLinearColor(0.45f, 0.75f, 1.f),
				Weapon->Damage * 2.f, 6.f, 0.85f, 2, 0.07f);
		}
	}
}

void UWWOWeaponComponent::ApplyKillPerks(int32 Kills)
{
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L || Kills <= 0) return;
	if (L->HasPerk("hammer")) HammerLeft = 3.f;
	if (L->HasPerk("scavenger"))
	{
		Ammo = FMath::Min(Ammo + FMath::Max(1, FMath::RoundToInt(MagazineSize() * 0.2f)) * Kills, MagazineSize());
	}
	if (L->HasPerk("adrenal")) AdrenalLeft = 4.f;
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
	TArray<FString> Active;
	if (L->HasPerk("focus")) Active.Add(HitStreak >= 5 ? FString(TEXT("Focus ACTIVE")) : FString::Printf(TEXT("Focus %d/5"), HitStreak));
	if (L->HasPerk("static")) Active.Add(FString::Printf(TEXT("Static %d/10"), StaticHits));
	if (L->HasPerk("adrenal") && AdrenalLeft > 0.f) Active.Add(FString::Printf(TEXT("Adrenal %.1fs"), AdrenalLeft));
	const AWWOCharacter* Pl = GetPlayer();
	if (L->HasPerk("brace") && Pl && (Pl->MoveState.bCrouching || Pl->MoveState.bSliding)) Active.Add(TEXT("Braced"));
	if (L->HasPerk("hunter") && AdsAmount > 0.5f) Active.Add(TEXT("Hunter"));
	if (L->HasPerk("overpressure") && bOverpressure) Active.Add(TEXT("Overpressure READY"));
	if (L->HasPerk("final_round") && Ammo == 1) Active.Add(TEXT("Final Round"));
	if (L->HasPerk("anchor") && IsAnchored()) Active.Add(TEXT("Anchored"));
	if (L->HasPerk("patience") && AdsAmount > 0.5f) Active.Add(AimHeld >= PatienceTime ? FString(TEXT("Patience READY")) : FString(TEXT("Patience")));
	if (L->HasPerk("hammer") && HammerLeft > 0.f) Active.Add(FString::Printf(TEXT("Hammer %.1fs"), HammerLeft));
	if (L->HasPerk("cold_bore") && SinceShot >= ColdBoreTime) Active.Add(TEXT("Cold Bore READY"));
	if (L->HasPerk("tighten") && AdsAmount > 0.5f) Active.Add(TEXT("Tightened"));
	if (L->HasPerk("refund")) Active.Add(TEXT("Marksman"));
	if (L->HasPerk("fourth_round")) Active.Add(TEXT("Fourth Round"));
	// The ones that depend on what you're aiming at: lit while the target under the reticle qualifies.
	if (const IWWOTargetable* Aimed = Cast<IWWOTargetable>(ReticleTarget.Get()); Aimed && Pl)
	{
		const float Dist = FVector::Dist(Pl->EyeLocation(), Aimed->BodyCenter()) / WWO::M;
		if (L->HasPerk("longshot") && Dist >= LongShotRange) Active.Add(TEXT("Long Shot"));
		if (L->HasPerk("point_blank") && Dist <= PointBlankRange) Active.Add(TEXT("Point Blank"));
		if (L->HasPerk("executioner") && Aimed->GetHealth() + Aimed->GetShield() < 0.5f * (Aimed->GetMaxHealth() + Aimed->GetMaxShield())) Active.Add(TEXT("Finisher"));
	}
	if (L->HasPerk("breath") && AdsAmount > 0.5f) Active.Add(AimHeld >= BreathTime ? FString(TEXT("Breath HELD")) : FString(TEXT("Breath")));
	if (L->HasPerk("slide_shot") && Pl && Pl->MoveState.bSliding) Active.Add(TEXT("Slideshot"));
	if (L->HasPerk("full_burst") && (bFullBurst || bBurstBoosted)) Active.Add(TEXT("Full Burst READY"));
	return FString::Join(Active, TEXT("  ·  "));
}
