// Online gunfire (see FWWOGunShot in WWOCharacter.h).
// A joiner shows its shot at once and sends the host what it hit. The host checks each hit before
// applying it: the target is alive, the point is on it, nothing solid is in the way from the shooter's
// eye, it's within the gun's range, the gun could have fired that often and done that much damage.
// No rewinding of targets to the shot's moment yet: on a LAN the delay is a few milliseconds and the
// point check allows for it. Then everyone else sees the shot (MulticastGunShot).

#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
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

namespace
{
	constexpr float OriginSlack = 150.f; // cm between the shooter's claimed eye and the host's copy of it
	constexpr float PointSlack = 75.f;   // cm a hit point may sit outside the target's bounds (movement since)
	constexpr float BurstShots = 3.f;    // shots that may arrive bunched together
	constexpr float RateSlack = 1.35f;   // fire rate allowance over the gun's own (mods, Hammer)
	constexpr float DamageSlack = 2.6f;  // damage allowance over the gun's own (mods, perks: Overpressure, Final Round, Patience)
}

void AWWOCharacter::ServerGunShot_Implementation(const FWWOGunShot& Shot)
{
	// The gun this player says they fired (its base tuning; their mods aren't known here yet).
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	const UWWOWeaponData* Gun = nullptr;
	if (L)
	{
		for (const UWWOWeaponData* W : L->Weapons) if (W && W->Id == Shot.WeaponId) { Gun = W; break; }
	}
	if (!Gun || Health->bDead)
	{
		NetHitsRejected += Shot.Hits.Num();
		UE_LOG(LogWWO, Display, TEXT("NetShot rejected from %s: %s"), *GetName(), Gun ? TEXT("dead") : *FString::Printf(TEXT("unknown gun %s"), *Shot.WeaponId.ToString()));
		return;
	}

	// Fire rate: a budget that refills at the gun's rate, holding a few shots for bunched arrivals.
	const double Now = GetWorld()->GetTimeSeconds();
	const float Rate = FMath::Max(Gun->FireRate, 1.f) / 60.f * RateSlack;
	ShotBudget = FMath::Min(BurstShots, ShotBudget + float(Now - ShotBudgetTime) * Rate);
	ShotBudgetTime = Now;
	if (ShotBudget < 1.f)
	{
		NetHitsRejected += Shot.Hits.Num();
		UE_LOG(LogWWO, Display, TEXT("NetShot rejected from %s: faster than %s fires"), *GetName(), *Gun->Id.ToString());
		return;
	}
	ShotBudget -= 1.f;
	++NetShotsTaken;

	for (const FWWOGunHit& Hit : Shot.Hits)
	{
		FString Why;
		if (!CheckGunHit(Shot, Hit, Gun, Why))
		{
			++NetHitsRejected;
			UE_LOG(LogWWO, Display, TEXT("NetShot hit rejected from %s on %s: %s"), *GetName(), *GetNameSafe(Hit.Target), *Why);
			continue;
		}
		++NetHitsApplied;
		Cast<IWWOTargetable>(Hit.Target)->TakeHit(Hit.Damage, Hit.bCritical, Hit.Point);
	}
	MulticastGunShot(Shot.Origin, Shot.Ends); // everyone else sees it
}

bool AWWOCharacter::CheckGunHit(const FWWOGunShot& Shot, const FWWOGunHit& Hit, const UWWOWeaponData* Gun, FString& Why) const
{
	IWWOTargetable* Target = Cast<IWWOTargetable>(Hit.Target);
	if (!IsValid(Hit.Target) || !Target) { Why = TEXT("not a target"); return false; }
	if (Target->IsDead()) { Why = TEXT("already dead"); return false; }

	const float MaxDamage = Gun->Damage * FMath::Max(1.f, Gun->Pellets) * (Gun->CritMultiplier + 0.6f) * DamageSlack;
	if (Hit.Damage <= 0.f || Hit.Damage > MaxDamage) { Why = FString::Printf(TEXT("damage %.0f over %.0f"), Hit.Damage, MaxDamage); return false; }

	if (FVector::Dist(Shot.Origin, EyeLocation()) > OriginSlack) { Why = TEXT("shot from somewhere else"); return false; }
	const float Range = FVector::Dist(Shot.Origin, FVector(Hit.Point));
	if (Range > (Gun->FireRange + 2.f) * WWO::M) { Why = FString::Printf(TEXT("out of range (%.0f m)"), Range / WWO::M); return false; }

	// On the target: inside its bounds, give or take its movement since.
	const FBox Bounds = Hit.Target->GetComponentsBoundingBox(true).ExpandBy(PointSlack);
	if (!Bounds.IsInside(Hit.Point)) { Why = TEXT("point not on the target"); return false; }

	// Nothing solid between the shooter's eye and the point (the target itself doesn't count).
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWONetShotCheck), false, this);
	Params.AddIgnoredActor(Hit.Target);
	FHitResult Block;
	const FVector To = FVector(Hit.Point) - (FVector(Hit.Point) - Shot.Origin).GetSafeNormal() * 10.f; // stop just short of the surface
	if (GetWorld()->LineTraceSingleByChannel(Block, Shot.Origin, To, ECC_Visibility, Params))
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
