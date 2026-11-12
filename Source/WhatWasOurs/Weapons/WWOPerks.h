#pragma once

// The weapon perks' numbers, in one place: UWWOWeaponComponent applies them (Shoot, RefreshStats,
// PerkStatus) and the host's check of a joiner's shot allows for them (GunLimits and CheckGunHit in
// WWOCharacterGunNet.cpp). Retune a perk here and both sides follow; the perk's words are its
// perk_text in Config/WeaponMods.json.

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
	constexpr float HammerRate = 1.25f;    // fire rate for 3 s after a kill
	constexpr float BreathTime = 1.f;      // s aimed in before Held Breath steadies the gun
}
