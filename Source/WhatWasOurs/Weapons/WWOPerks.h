#pragma once

// The weapon perks, in one place: one FWWOPerk per perk id (a mod's perk in Config/WeaponMods.json,
// where its words are), in WWOPerk::Table() (Weapons/WWOPerks.cpp). The gun (UWWOWeaponComponent:
// RefreshStats, Shoot, PerkStatus, OnShotResult, kill perks, EndBurst) and the host's check of a
// joiner's shot (GunLimits, CheckGunHit, ServerGunShot in WWOCharacterGunNet.cpp) both read the table,
// and both work out a gun's modded stats with ComputeGunStats, so the two can't drift.
// Adding a perk: an entry in the table (see docs/RECIPES.md, "Add a weapon perk").

#include "CoreMinimal.h"

class UWWOWeaponData;
class UWWOLoadoutSubsystem;
class IWWOTargetable;
class AActor;
class UWorld;

// The perks' numbers. The table uses them; the host's allowances are the same numbers.
namespace WWOPerk
{
	constexpr float Overpressure = 1.5f;   // first shot after a reload
	constexpr float FinalRound = 2.f;      // the last round in the magazine
	constexpr float Focus = 1.15f;         // after 5 hits in a row
	constexpr float FullBurst = 1.2f;      // the burst after one that landed every round
	constexpr float SlideShot = 1.15f;     // while sliding (its pellet spread is halved too)
	constexpr float ColdBore = 1.25f;      // first shot after ColdBoreTime without firing
	constexpr float ColdBoreTime = 4.f;    // s
	constexpr float LongShot = 1.2f;       // targets LongShotRange or farther
	constexpr float LongShotRange = 30.f;  // m
	constexpr float PointBlank = 1.25f;    // targets within PointBlankRange
	constexpr float PointBlankRange = 4.f; // m
	constexpr float Finisher = 1.3f;       // targets under half their health and shield
	constexpr float HunterCrit = 0.3f;     // added to the crit multiplier while aimed in
	constexpr float PatienceCrit = 0.6f;   // added to the crit multiplier after PatienceTime aimed
	constexpr float PatienceTime = 0.6f;   // s
	constexpr float HammerRate = 1.25f;    // fire rate for HammerTime after a kill
	constexpr float HammerTime = 3.f;      // s
	constexpr float AdrenalReload = 1.4f;  // reload speed for AdrenalTime after a kill
	constexpr float AdrenalTime = 4.f;     // s
	constexpr float BreathTime = 1.f;      // s aimed in before Held Breath steadies the gun
}

// What the perks remember on the gun (UWWOWeaponComponent::PerkState). Overpressure and Cold Bore
// also live on in the gun's own store across a swap; the rest is reset on equip, mod change, respawn.
struct FWWOPerkState
{
	int32 HitStreak = 0;        // Focus: hits in a row (counted with or without the perk)
	int32 StaticHits = 0;       // Static: hits toward the next arc
	float AdrenalLeft = 0.f;    // Adrenal: seconds of faster reloads left
	bool bOverpressure = true;  // Overpressure: the next shot is the first since a reload
	float AimHeld = 0.f;        // Patience, Held Breath: seconds fully aimed in
	float HammerLeft = 0.f;     // Hammer: seconds of faster fire left
	float SinceShot = 100.f;    // Cold Bore: seconds since the last shot
	int32 RefundRounds = 0;     // Marksman: rounds to put back after this shot (a crit)
	int32 BurstHits = 0;        // Full Burst: rounds of the burst under way that hit
	bool bFullBurst = false;    // Full Burst: the last burst landed every round
	bool bBurstBoosted = false; // ...so this one hits harder

	void Tick(float DeltaSeconds)
	{
		AdrenalLeft = FMath::Max(AdrenalLeft - DeltaSeconds, 0.f);
		HammerLeft = FMath::Max(HammerLeft - DeltaSeconds, 0.f);
		SinceShot += DeltaSeconds;
	}
};

// The numbers mods and perks change on a gun: its asset's values, mods applied (ComputeGunStats),
// then the perks that are on (FWWOPerk::Stats). The gun writes them into its modded copy each frame.
struct FWWOGunStats
{
	float FireRange = 0.f, RecoilSide = 0.f, RecoilUp = 0.f, MagazineSize = 1.f, AdsMoveSpeed = 1.f, Sway = 0.f;
	float Damage = 0.f, FireRate = 0.f, CritMultiplier = 1.f, PelletSpread = 0.f, HipSpread = 0.f, Zoom = 1.f;
	float Burst = 1.f, Pellets = 1.f;
	int32 BurstCount() const { return FMath::Max(1, FMath::RoundToInt(Burst)); } // as UWWOWeaponData::BurstCount
	void WriteTo(UWWOWeaponData& Gun) const;
};

// What a perk's condition sees on the shooter's machine, this frame.
struct FWWOPerkView
{
	const FWWOPerkState& State;
	float AdsAmount = 0.f;
	int32 Ammo = 0;
	bool bCrouching = false, bSliding = false;
	bool bAnchored = false;                // aimed in and (nearly) standing still
	const IWWOTargetable* Aimed = nullptr; // the HUD only: the target under the reticle, if any...
	float AimedMeters = 0.f;               // ...and how far it is
};

// A shot's result, for the perks that react to it (on the shooter's machine).
struct FWWOPerkShot
{
	FWWOPerkState& State;
	UWorld* World = nullptr;
	AActor* Shooter = nullptr;
	AActor* FirstHit = nullptr;
	int32 HitCount = 0;
	bool bCritical = false;
	bool bJoiner = false; // online joiner: the host runs the arcs (FWWOPerk::HostShot)
	float Damage = 0.f;   // the gun's damage as it is now
};

// Kills, for the kill perks (the shooter's own, or the host's word for a joiner's).
struct FWWOPerkKill
{
	FWWOPerkState& State;
	int32& Ammo;
	int32 MagazineSize = 1;
	int32 Kills = 0;
};

// A joiner's shot the host has applied, for what the host runs for it (the arcs).
struct FWWOPerkHostShot
{
	UWorld* World = nullptr;
	AActor* Shooter = nullptr;
	AActor* FirstHit = nullptr;
	AActor* FirstCrit = nullptr;
	float Round = 0.f; // one round's modded damage (FWWOGunLimits::Round)
	int32& StaticHits; // the host's count toward this joiner's Static arc
};

// One perk. A field left empty is something the perk doesn't do. Everything goes through the table in
// its order, which is the HUD line's order; where two perks scale the same number, it is also the
// order of the multiplications (add a new perk at the place it should show in the line).
struct FWWOPerk
{
	FName Id;

	// --- On the shooter's machine ---
	void (*Stats)(FWWOGunStats& Gun, const FWWOPerkView& V) = nullptr; // RefreshStats, every frame
	bool (*ShotOn)(const FWWOPerkView& V) = nullptr; // Shoot: this shot gets ShotScale and ShotCrit
	float ShotScale = 1.f;
	float ShotCrit = 0.f;                            // added to the crit multiplier
	bool (*TargetOn)(float Meters, const IWWOTargetable& Target) = nullptr; // Shoot: per target hit
	float TargetScale = 1.f;
	FString (*Hud)(const FWWOPerkView& V) = nullptr; // PerkStatus: its text while lit, else empty
	void (*OnShot)(FWWOPerkShot& Shot) = nullptr;    // after every shot, hit or miss
	void (*OnKill)(FWWOPerkKill& Kill) = nullptr;
	void (*OnBurstEnd)(FWWOPerkState& State, bool bEveryRoundHit) = nullptr;

	// --- On the host, checking a joiner's shot (it can't see the joiner's perk state) ---
	// HostDamage: how much more a hit may do. With no check below it is allowed on every shot (the
	// perk at its best); with one, on the hits where the host's own numbers agree, with some room.
	float HostDamage = 1.f;
	bool (*HostOnTarget)(float Meters, const IWWOTargetable& Target) = nullptr;
	bool (*HostOnShot)(float SinceShot) = nullptr; // s since that gun last fired, on the host
	float HostCrit = 0.f;  // added to the crit multiplier allowed
	float HostRate = 1.f;  // the fire rate allowed, times this
	int32 HostBurst = 0;   // rounds added to a burst gun's burst
	void (*HostShot)(FWWOPerkHostShot& Shot) = nullptr; // after the host applied the shot
};

using FWWOPerkList = TArray<const FWWOPerk*, TInlineAllocator<6>>;

namespace WWOPerk
{
	TConstArrayView<FWWOPerk> Table();
	// The table's perks that Has says are on the gun, in table order.
	FWWOPerkList Collect(TFunctionRef<bool(FName Id)> Has);
	FWWOPerkList Equipped(const UWWOLoadoutSubsystem& Loadout); // on the loadout's equipped weapon
	// A gun's stats with its mods: Percent gives the mods' summed percentage for a stat (0.25 is +25%).
	FWWOGunStats ComputeGunStats(const UWWOWeaponData& Base, TFunctionRef<float(FName Stat)> Percent);
}
