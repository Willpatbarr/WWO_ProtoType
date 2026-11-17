// Online gunfire and melee (see FWWOGunShot in WWOCharacter.h).
// A joiner shows its shot at once and sends the host what it hit. The host checks each hit before
// applying it: the target is alive, the point is on it, nothing solid is in the way from the shooter's
// eye, it's within the gun's range, the gun (with the mods the shot names) could have fired that often
// and done that much damage.
// Melee works the same way, one hit per swing (ServerMeleeHit).
// No rewinding of targets to the shot's moment yet: on a LAN the delay is a few milliseconds and the
// point check allows for it. Then everyone else sees the shot (MulticastGunShot).

#include "Player/WWOCharacter.h"
#include "Systems/WWOSound.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOGrenadeComponent.h"
#include "Characters/WWOBodyComponent.h"
#include "Data/WWOData.h"
#include "FX/WWOFX.h"
#include "Flora/WWOFloraEffects.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "Systems/WWOWorldSubsystem.h"
#include "WWOTypes.h"
#include "Weapons/WWOPerks.h"
#include "Player/WWOAbilityComponent.h"
#include "WhatWasOurs.h"
#include "Engine/World.h"

int32 AWWOCharacter::NetShotsTaken = 0;
int32 AWWOCharacter::NetHitsApplied = 0;
int32 AWWOCharacter::NetHitsRejected = 0;
int32 AWWOCharacter::NetShotsSeen = 0;
int32 AWWOCharacter::NetMeleeSwings = 0;
int32 AWWOCharacter::NetMeleeApplied = 0;
int32 AWWOCharacter::NetMeleeRejected = 0;
int32 AWWOCharacter::NetKillsCredited = 0;

namespace
{
	constexpr float OriginSlack = 150.f; // cm between the shooter's claimed eye and the host's copy of it
	constexpr float PointSlack = 75.f;   // cm a hit point may sit outside the target's bounds (movement since)
	constexpr float BurstShots = 3.f;    // shots that may arrive bunched together
	constexpr float GunSlack = 1.1f;     // over the most the setup can do (rounding, timing)
	constexpr float MeleeDamageSlack = 10.f; // over the base punch: Thornfist (2.5x) x veiled (3x) x melee traits
	constexpr float MeleeReachSlack = 1.5f;  // m over the reach (the lunge's own 0.5 m, plus movement since)
	constexpr float MeleeRateSlack = 0.5f;   // hits may come this fraction of a swing apart
}

// The most this gun can do with these mods (same data on every machine), perks at their best:
// Hammer's faster fire, Overpressure / Final Round on one shot, Focus, Hunter and Patience on crits.
// Cold Bore, Full Burst, Long Shot, Point Blank, Finisher and Slideshot on their shots.
// Mirrors UWWOWeaponComponent::RefreshStats and Shoot.
bool AWWOCharacter::GunLimits(const UWWOLoadoutSubsystem* L, const UWWOWeaponData* Gun, const TArray<FName>& ModIds, FWWOGunLimits& Out, FString& Why, TSet<FName>& Perks)
{
	TMap<FName, float> Percent;
	TSet<FName> Slots;
	for (const FName& Id : ModIds)
	{
		const UWWOModData* Mod = nullptr;
		for (const UWWOModData* M : L->WeaponMods) if (M && M->Id == Id) { Mod = M; break; }
		if (!Mod || Mod->Frame != Gun->Id) { Why = FString::Printf(TEXT("mod %s doesn't fit %s"), *Id.ToString(), *Gun->Id.ToString()); return false; }
		bool bTaken = false;
		Slots.Add(Mod->Slot, &bTaken);
		if (bTaken) { Why = FString::Printf(TEXT("two mods in the %s slot"), *Mod->Slot.ToString()); return false; }
		for (const FWWOStatModifier& S : Mod->Mods) if (S.Op == EWWOStatOp::PercentAdd) Percent.FindOrAdd(S.Stat) += S.Value;
		if (!Mod->Perk.IsNone()) Perks.Add(Mod->Perk);
	}
	auto Scaled = [&Percent](float Value, FName Stat) { return Value * FMath::Max(1.f + Percent.FindRef(Stat), 0.f); };
	// The perks the host has no way to check stay at their best in every shot's allowance...
	float ShotScale = 1.f;
	if (Perks.Contains("overpressure")) ShotScale *= WWOPerk::Overpressure;
	if (Perks.Contains("final_round")) ShotScale *= WWOPerk::FinalRound;
	if (Perks.Contains("focus")) ShotScale *= WWOPerk::Focus;
	if (Perks.Contains("full_burst")) ShotScale *= WWOPerk::FullBurst;
	if (Perks.Contains("slide_shot")) ShotScale *= WWOPerk::SlideShot;
	// ...and the ones it can (range, the target's health, time since the last shot) count per hit, in CheckGunHit.
	Out.bLongShot = Perks.Contains("longshot");
	Out.bPointBlank = Perks.Contains("point_blank");
	Out.bFinisher = Perks.Contains("executioner");
	Out.bColdBore = Perks.Contains("cold_bore");
	float Crit = Scaled(Gun->CritMultiplier, "crit_multiplier");
	if (Perks.Contains("hunter")) Crit += WWOPerk::HunterCrit;
	if (Perks.Contains("patience")) Crit += WWOPerk::PatienceCrit;
	Out.Always = ShotScale;
	Out.Crit = FMath::Max(Crit, 1.f);
	Out.Pellets = FMath::Max(1, FMath::RoundToInt(Gun->Pellets));
	Out.Damage = Scaled(Gun->Damage, "damage") * Out.Pellets * ShotScale * WWOPerk::LongShot * WWOPerk::PointBlank * WWOPerk::Finisher * WWOPerk::ColdBore * Out.Crit * GunSlack;
	// A burst gun's fire rate is bursts per minute; Fourth Round adds a round to each.
	Out.Burst = Gun->BurstCount() > 1 ? Gun->BurstCount() + (Perks.Contains("fourth_round") ? 1 : 0) : 1;
	Out.Rate = Scaled(FMath::Max(Gun->FireRate, 1.f), "fire_rate") / 60.f * Out.Burst * (Perks.Contains("hammer") ? WWOPerk::HammerRate : 1.f) * GunSlack;
	Out.Range = (Scaled(Gun->FireRange, "range") + 2.f) * WWO::M;
	Out.Round = Scaled(Gun->Damage, "damage"); // what the arc perks scale from, as on the shooter's machine
	return true;
}

void AWWOCharacter::ServerGunShot_Implementation(const FWWOGunShot& Shot)
{
	// The gun this player says they fired, with its mods.
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	const UWWOWeaponData* Gun = nullptr;
	if (L)
	{
		for (const UWWOWeaponData* W : L->Weapons) if (W && W->Id == Shot.WeaponId) { Gun = W; break; }
	}
	FWWOGunLimits Limits;
	TSet<FName> Perks;
	FString Why = Gun ? TEXT("dead") : FString::Printf(TEXT("unknown gun %s"), *Shot.WeaponId.ToString());
	if (!Gun || Health->bDead || !GunLimits(L, Gun, Shot.Mods, Limits, Why, Perks))
	{
		NetHitsRejected += Shot.Hits.Num();
		UE_LOG(LogWWO, Display, TEXT("NetShot rejected from %s: %s"), *GetName(), *Why);
		return;
	}

	if (!Gun->bUnlocked)
	{
		NetHitsRejected += Shot.Hits.Num();
		UE_LOG(LogWWO, Display, TEXT("NetShot rejected from %s: %s is locked"), *GetName(), *Gun->Id.ToString());
		return;
	}
	// One shot hits each target once and can't hit more targets than it has pellets.
	TSet<const AActor*> Struck;
	for (const FWWOGunHit& Hit : Shot.Hits) Struck.Add(Hit.Target);
	if (Shot.Hits.Num() > Limits.Pellets || Struck.Num() != Shot.Hits.Num() || Shot.Ends.Num() > Limits.Pellets)
	{
		NetHitsRejected += Shot.Hits.Num();
		UE_LOG(LogWWO, Display, TEXT("NetShot rejected from %s: %d hits and %d impacts from one shot of %d pellet(s)"), *GetName(), Shot.Hits.Num(), Shot.Ends.Num(), Limits.Pellets);
		return;
	}
	// Fire rate: a budget per gun that refills at that gun's rate, holding a few shots for bunched arrivals.
	const double Now = GetWorld()->GetTimeSeconds();
	FShotBudget& Budget = ShotBudgets.FindOrAdd(Gun->Id);
	Budget.Shots = FMath::Min(FMath::Max(BurstShots, Limits.Burst + 1.f), Budget.Shots + float(Now - Budget.Time) * Limits.Rate);
	Budget.Time = Now;
	if (Budget.Shots < 1.f)
	{
		NetHitsRejected += Shot.Hits.Num();
		UE_LOG(LogWWO, Display, TEXT("NetShot rejected from %s: faster than %s fires"), *GetName(), *Gun->Id.ToString());
		return;
	}
	Budget.Shots -= 1.f;
	Limits.SinceShot = float(Now - Budget.LastShot);
	Budget.LastShot = Now;
	++NetShotsTaken;
	int32 Kills = 0;
	FWWODamageBy By(this); // PvP kill credit
	TGuardValue<bool> Seen(UWWOWorldSubsystem::bAttackerSawHit, true); // the joiner marked its own shot
	AActor* FirstHit = nullptr;
	AActor* FirstCrit = nullptr;

	for (const FWWOGunHit& Hit : Shot.Hits)
	{
		FString HitWhy;
		if (!CheckGunHit(Shot, Hit, Limits, HitWhy))
		{
			++NetHitsRejected;
			UE_LOG(LogWWO, Display, TEXT("NetShot hit rejected from %s on %s: %s"), *GetName(), *GetNameSafe(Hit.Target), *HitWhy);
			continue;
		}
		++NetHitsApplied;
		Kills += Cast<IWWOTargetable>(Hit.Target)->TakeHit(Hit.Damage, Hit.bCritical, Hit.Point) ? 1 : 0;
		if (!FirstHit) FirstHit = Hit.Target;
		if (Hit.bCritical && !FirstCrit) FirstCrit = Hit.Target;
	}
	// The joiner's arc perks (UWWOWeaponComponent::OnShotResult runs them for the host's own shots).
	if (FirstCrit && Perks.Contains("ricochet"))
	{
		AWWOChainLightning::SpawnFrom(GetWorld(), FirstCrit, this, FLinearColor(1.f, 0.62f, 0.2f), Limits.Round * 0.6f, 8.f, 1.f, 1, 0.04f);
	}
	if (Weapon->HostStaticGun != Shot.WeaponId)
	{
		Weapon->HostStaticGun = Shot.WeaponId; // another gun: its own count, as the joiner's starts over on a swap
		Weapon->HostStaticHits = 0;
	}
	if (FirstHit && Perks.Contains("static") && ++Weapon->HostStaticHits >= 10)
	{
		Weapon->HostStaticHits = 0;
		AWWOChainLightning::SpawnFrom(GetWorld(), FirstHit, this, FLinearColor(0.45f, 0.75f, 1.f), Limits.Round * 2.f, 6.f, 0.85f, 2, 0.07f);
	}
	if (Kills > 0)
	{
		NetKillsCredited += Kills;
		ClientKillCredit(Kills, false);
	}
	MulticastGunShot(Shot.Origin, Shot.Ends); // everyone else sees it
}

bool AWWOCharacter::CheckGunHit(const FWWOGunShot& Shot, const FWWOGunHit& Hit, const FWWOGunLimits& Limits, FString& Why) const
{
	IWWOTargetable* Target = Cast<IWWOTargetable>(Hit.Target);
	if (!IsValid(Hit.Target) || !Target) { Why = TEXT("not a target"); return false; }
	if (Target->IsDead()) { Why = TEXT("already dead"); return false; }
	if (!UWWOWorldSubsystem::CanHurt(this, Hit.Target)) { Why = TEXT("can't hurt it (teammate)"); return false; }
	if (FVector::Dist(Shot.Origin, EyeLocation()) > OriginSlack) { Why = TEXT("shot from somewhere else"); return false; }
	const float Range = FVector::Dist(Shot.Origin, FVector(Hit.Point));
	if (Range > Limits.Range) { Why = FString::Printf(TEXT("out of range (%.0f m)"), Range / WWO::M); return false; }
	// The most this hit could be: every pellet on it, the unchecked perks at their best, and each
	// perk the host can check only where its own numbers agree (with room for what moved since).
	float Most = Limits.Round * Limits.Pellets * Limits.Always;
	const float Meters = Range / WWO::M;
	if (Limits.bLongShot && Meters >= WWOPerk::LongShotRange - 3.f) Most *= WWOPerk::LongShot;
	if (Limits.bPointBlank && Meters <= WWOPerk::PointBlankRange + 2.f) Most *= WWOPerk::PointBlank;
	if (Limits.bFinisher && Target->GetHealth() + Target->GetShield() < 0.65f * (Target->GetMaxHealth() + Target->GetMaxShield())) Most *= WWOPerk::Finisher;
	if (Limits.bColdBore && Limits.SinceShot >= WWOPerk::ColdBoreTime - 0.5f) Most *= WWOPerk::ColdBore;
	if (Hit.bCritical)
	{
		// A crit lands by the crit spot (a head): within arm's reach of it, give or take its movement.
		bool bNearCrit = false;
		for (const FWWOAimPart& Part : Target->GetAimParts()) bNearCrit |= Part.bCritical && FVector::Dist(Part.Position, FVector(Hit.Point)) <= Part.Radius + PointSlack;
		if (bNearCrit) Most *= Limits.Crit;
	}
	Most *= GunSlack;
	if (Hit.Damage <= 0.f || Hit.Damage > Most) { Why = FString::Printf(TEXT("damage %.0f over %.0f"), Hit.Damage, Most); return false; }
	return CheckReach(Hit.Target, Shot.Origin, Hit.Point, Why);
}

bool AWWOCharacter::CheckReach(AActor* Target, const FVector& Origin, const FVector& Point, FString& Why) const
{
	// On the target: inside its bounds, give or take its movement since.
	const FBox Bounds = Target->GetComponentsBoundingBox(true).ExpandBy(PointSlack);
	if (!Bounds.IsInside(Point)) { Why = TEXT("point not on the target"); return false; }

	// Nothing solid between the shooter's eye and the point (the target itself doesn't count).
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWONetShotCheck), false, this);
	Params.AddIgnoredActor(Target);
	FHitResult Block;
	const FVector To = Point - (Point - Origin).GetSafeNormal() * 10.f; // stop just short of the surface
	if (FVector::DistSquared(Origin, Point) > 100.f && GetWorld()->LineTraceSingleByChannel(Block, Origin, To, ECC_Visibility, Params))
	{
		Why = FString::Printf(TEXT("blocked by %s"), *GetNameSafe(Block.GetActor()));
		return false;
	}
	return true;
}

void AWWOCharacter::MulticastGunShot_Implementation(FVector_NetQuantize From, const TArray<FVector_NetQuantize>& Ends)
{
	// The shooter already showed it; everyone else draws its tracers and impacts.
	if (IsLocallyControlled() || GetNetMode() == NM_DedicatedServer) return;
	++NetShotsSeen;
	ShowShotFromBody(From, Ends, true);
}

void AWWOCharacter::ShowShotFromBody(const FVector& From, TConstArrayView<FVector_NetQuantize> Ends, bool bMarks)
{
	if (Ends.Num() == 0) return;
	UWorld* World = GetWorld();
	// From about where their gun is: a forearm past the right hand, toward where the shot went (or,
	// without a hand to go by, in front of the eye, a little right and down).
	const FVector Aim = (FVector(Ends[0]) - From).GetSafeNormal();
	FVector Muzzle = From + GetActorForwardVector() * 45.f + GetActorRightVector() * 18.f - FVector(0.f, 0.f, 15.f);
	static const FName Hand("hand_r");
	if (GetMesh() && GetMesh()->GetBoneIndex(Hand) != INDEX_NONE) Muzzle = GetMesh()->GetSocketLocation(Hand) + Aim * 40.f;
	static const FLinearColor Color(1.f, 0.8f, 0.45f);
	AWWOFXActor::MuzzleFlash(World, Muzzle, Aim, Color);
	{
		const FName Own(*(TEXT("fire_") + (Weapon->GetWeaponData() ? Weapon->GetWeaponData()->Id.ToString() : FString())));
		UWWOSound::Play(this, UWWOSound::HasEvent(this, Own) ? Own : FName("fire"), Muzzle, 0.8f);
	}
	for (const FVector_NetQuantize& End : Ends)
	{
		AWWOFXActor::BulletTrail(World, Muzzle, End, Color);
		// What it landed on, if anything (a miss's end is out in the open air): sparks off a body,
		// gold for a headshot; a mark on anything else.
		const FVector Dir = (FVector(End) - From).GetSafeNormal();
		FHitResult Near;
		if (!World->LineTraceSingleByChannel(Near, FVector(End) - Dir * 20.f, FVector(End) + Dir * 20.f, ECC_Visibility,
			FCollisionQueryParams(SCENE_QUERY_STAT(WWOShotMark), false, this))) continue;
		const IWWOTargetable* Target = Cast<IWWOTargetable>(Near.GetActor());
		if (Target && !Target->IsDead() && UWWOWorldSubsystem::CanHurt(this, Near.GetActor())) AWWOFXActor::HitSpark(World, End, -Dir, Target->IsCriticalHit(Near, From));
		else if (bMarks) AWWOFXActor::ImpactMark(World, End);
	}
}

// --- Melee ---------------------------------------------------------------------------------------

void AWWOCharacter::PlaySwingOnBody()
{
	Body->PlayUpperShot("Melee_Hook", 0.05f, 0.2f);
}

void AWWOCharacter::ServerMeleeSwing_Implementation()
{
	if (Health->bDead) return;
	LastSwingTime = GetWorld()->GetTimeSeconds();
	++NetMeleeSwings;
	PlaySwingOnBody(); // replicates to everyone but the joiner (it plays its own)
}

void AWWOCharacter::ServerMeleeHit_Implementation(AActor* Target, float Damage, FVector_NetQuantize Origin, FVector_NetQuantize Point)
{
	FString Why;
	if (!CheckMeleeHit(Target, Damage, Origin, Point, Why))
	{
		++NetMeleeRejected;
		UE_LOG(LogWWO, Display, TEXT("NetMelee hit rejected from %s on %s: %s"), *GetName(), *GetNameSafe(Target), *Why);
		return;
	}
	++NetMeleeApplied;
	LastMeleeHitTime = GetWorld()->GetTimeSeconds();
	FWWODamageBy By(this);
	bool bKilled;
	{
		TGuardValue<bool> Seen(UWWOWorldSubsystem::bAttackerSawHit, true); // the joiner marked its own punch
		bKilled = Cast<IWWOTargetable>(Target)->TakeHit(Damage, false, Point);
	}
	Weapon->MeleeKnockBack(Target, bKilled);
	Weapon->MeleeKitEffects(Target, Point); // Melee Guard, Thornfist's discharge, Static Charge's chain
	if (bKilled)
	{
		++NetKillsCredited;
		ClientKillCredit(1, true);
	}
}

void AWWOCharacter::ClientHitMarker_Implementation(bool bKilled)
{
	Weapon->OnHitConfirmed.Broadcast(false, bKilled);
	if (bKilled) ++NetKillsCredited;
}

void AWWOCharacter::ServerThrowGrenade_Implementation(FVector_NetQuantize Hand, FVector_NetQuantizeNormal Dir)
{
	if (!Grenades->HostThrow(Hand, Dir) && !Health->bDead) ClientKitRefused(0, 0); // its charge comes back
}

void AWWOCharacter::ClientKillCredit_Implementation(int32 Kills, bool bMelee)
{
	NetKillsCredited += Kills;
	Weapon->OnKillsConfirmed(Kills, bMelee);
}

bool AWWOCharacter::CheckMeleeHit(AActor* Target, float Damage, const FVector& Origin, const FVector& Point, FString& Why)
{
	if (Health->bDead) { Why = TEXT("puncher is dead"); return false; }
	IWWOTargetable* T = Cast<IWWOTargetable>(Target);
	if (!IsValid(Target) || !T) { Why = TEXT("not a target"); return false; }
	if (T->IsDead()) { Why = TEXT("already dead"); return false; }
	if (!UWWOWorldSubsystem::CanHurt(this, Target)) { Why = TEXT("can't hurt it (teammate)"); return false; }

	// PvP: what the host's own copy of this player says a punch can do (its loadout, Thornfist when
	// it has it on, the veil), with a little room. Co-op keeps the loose cap: there the joiner's own
	// Melee damage slider counts, and the host doesn't know it.
	float MaxDamage = Weapon->MeleeDamage * MeleeDamageSlack;
	if (UWWOWorldSubsystem::IsPvP(this))
	{
		const UWWOAbilityData* Thornfist = Abilities->Equipped("thornfist");
		const float Fist = FMath::Max(Abilities->MeleeMultiplier(), Thornfist ? Thornfist->MeleeDamageMult : 1.f); // on here, or about to be (its use is on the way)
		MaxDamage = M("melee_damage", Weapon->MeleeDamage) * Fist * FMath::Max(VeilMeleeMultiplier(), 1.f) * 1.15f;
	}
	if (Damage <= 0.f || Damage > MaxDamage) { Why = FString::Printf(TEXT("damage %.0f over %.0f"), Damage, MaxDamage); return false; }
	if (GetWorld()->GetTimeSeconds() - LastSwingTime > 2.0) { Why = TEXT("no swing came before it"); return false; }

	const double Since = GetWorld()->GetTimeSeconds() - LastMeleeHitTime;
	if (Since < Weapon->MeleeSwingTime() * MeleeRateSlack) { Why = FString::Printf(TEXT("%.2f s after the last hit"), Since); return false; }

	if (FVector::Dist(Origin, EyeLocation()) > OriginSlack) { Why = TEXT("punch from somewhere else"); return false; }
	const float Range = FVector::Dist(Origin, Point) / WWO::M;
	if (Range > Weapon->MeleeReach + MeleeReachSlack) { Why = FString::Printf(TEXT("out of reach (%.1f m)"), Range); return false; }
	return CheckReach(Target, Origin, Point, Why);
}
