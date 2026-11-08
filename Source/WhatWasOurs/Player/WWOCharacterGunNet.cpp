// Online gunfire and melee (see FWWOGunShot in WWOCharacter.h).
// A joiner shows its shot at once and sends the host what it hit. The host checks each hit before
// applying it: the target is alive, the point is on it, nothing solid is in the way from the shooter's
// eye, it's within the gun's range, the gun (with the mods the shot names) could have fired that often
// and done that much damage.
// Melee works the same way, one hit per swing (ServerMeleeHit).
// No rewinding of targets to the shot's moment yet: on a LAN the delay is a few milliseconds and the
// point check allows for it. Then everyone else sees the shot (MulticastGunShot).

#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOWeaponComponent.h"
#include "Characters/WWOBodyComponent.h"
#include "Data/WWOData.h"
#include "FX/WWOFX.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "WWOTypes.h"
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

	// The most this gun can do with these mods (same data on every machine), perks at their best:
	// Hammer's faster fire, Overpressure / Final Round on one shot, Focus, Hunter and Patience on crits.
	// Mirrors UWWOWeaponComponent::RefreshStats and Shoot.
	bool GunLimits(const UWWOLoadoutSubsystem* L, const UWWOWeaponData* Gun, const TArray<FName>& ModIds, FWWOGunLimits& Out, FString& Why)
	{
		TMap<FName, float> Percent;
		TSet<FName> Perks, Slots;
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
		float ShotScale = 1.f;
		if (Perks.Contains("overpressure")) ShotScale *= 1.5f;
		if (Perks.Contains("final_round")) ShotScale *= 2.f;
		if (Perks.Contains("focus")) ShotScale *= 1.15f;
		float Crit = Scaled(Gun->CritMultiplier, "crit_multiplier");
		if (Perks.Contains("hunter")) Crit += 0.3f;
		if (Perks.Contains("patience")) Crit += 0.6f;
		Out.Damage = Scaled(Gun->Damage, "damage") * FMath::Max(1.f, FMath::RoundToFloat(Gun->Pellets)) * ShotScale * FMath::Max(Crit, 1.f) * GunSlack;
		Out.Rate = Scaled(FMath::Max(Gun->FireRate, 1.f), "fire_rate") / 60.f * (Perks.Contains("hammer") ? 1.25f : 1.f) * GunSlack;
		Out.Range = (Scaled(Gun->FireRange, "range") + 2.f) * WWO::M;
		return true;
	}
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
	FString Why = Gun ? TEXT("dead") : FString::Printf(TEXT("unknown gun %s"), *Shot.WeaponId.ToString());
	if (!Gun || Health->bDead || !GunLimits(L, Gun, Shot.Mods, Limits, Why))
	{
		NetHitsRejected += Shot.Hits.Num();
		UE_LOG(LogWWO, Display, TEXT("NetShot rejected from %s: %s"), *GetName(), *Why);
		return;
	}

	// Fire rate: a budget that refills at the gun's rate, holding a few shots for bunched arrivals.
	const double Now = GetWorld()->GetTimeSeconds();
	ShotBudget = FMath::Min(BurstShots, ShotBudget + float(Now - ShotBudgetTime) * Limits.Rate);
	ShotBudgetTime = Now;
	if (ShotBudget < 1.f)
	{
		NetHitsRejected += Shot.Hits.Num();
		UE_LOG(LogWWO, Display, TEXT("NetShot rejected from %s: faster than %s fires"), *GetName(), *Gun->Id.ToString());
		return;
	}
	ShotBudget -= 1.f;
	++NetShotsTaken;
	int32 Kills = 0;

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
	if (Hit.Damage <= 0.f || Hit.Damage > Limits.Damage) { Why = FString::Printf(TEXT("damage %.0f over %.0f"), Hit.Damage, Limits.Damage); return false; }
	if (FVector::Dist(Shot.Origin, EyeLocation()) > OriginSlack) { Why = TEXT("shot from somewhere else"); return false; }
	const float Range = FVector::Dist(Shot.Origin, FVector(Hit.Point));
	if (Range > Limits.Range) { Why = FString::Printf(TEXT("out of range (%.0f m)"), Range / WWO::M); return false; }
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
	// From about where their gun is: in front of the eye, a little right and down.
	const FVector Forward = GetActorForwardVector();
	const FVector Muzzle = FVector(From) + Forward * 45.f + GetActorRightVector() * 18.f - FVector(0.f, 0.f, 15.f);
	for (const FVector_NetQuantize& End : Ends)
	{
		AWWOFXActor::Tracer(GetWorld(), Muzzle, End, FLinearColor(1.f, 0.85f, 0.55f), 1.5f, 0.06f);
		AWWOFXActor::ImpactMark(GetWorld(), End);
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
	const bool bKilled = Cast<IWWOTargetable>(Target)->TakeHit(Damage, false, Point);
	Weapon->MeleeKnockBack(Target, bKilled);
	if (bKilled)
	{
		++NetKillsCredited;
		ClientKillCredit(1, true);
	}
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

	const float MaxDamage = Weapon->MeleeDamage * MeleeDamageSlack;
	if (Damage <= 0.f || Damage > MaxDamage) { Why = FString::Printf(TEXT("damage %.0f over %.0f"), Damage, MaxDamage); return false; }

	const double Since = GetWorld()->GetTimeSeconds() - LastMeleeHitTime;
	if (Since < Weapon->MeleeSwingTime() * MeleeRateSlack) { Why = FString::Printf(TEXT("%.2f s after the last hit"), Since); return false; }

	if (FVector::Dist(Origin, EyeLocation()) > OriginSlack) { Why = TEXT("punch from somewhere else"); return false; }
	const float Range = FVector::Dist(Origin, Point) / WWO::M;
	if (Range > Weapon->MeleeReach + MeleeReachSlack) { Why = FString::Printf(TEXT("out of reach (%.1f m)"), Range); return false; }
	return CheckReach(Target, Origin, Point, Why);
}
