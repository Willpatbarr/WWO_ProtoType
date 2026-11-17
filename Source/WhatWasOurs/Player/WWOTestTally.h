#pragma once

// Test tallies and probes: counters gameplay bumps so the tests can see what happened (how many of a
// joiner's shots the host took, the corrections this machine got, the grenades it drew for the host),
// and the few hooks a test uses to look at or move gameplay objects. Gameplay only writes here, one
// line at the place it happens:
//
//     FWWOTestTally::Count(&FWWOTestTally::HitsRejected, Shot.Hits.Num());
//
// and never reads any of it back; the readers are the autotests (WWOAutoTest*.cpp), the regression
// scenarios (WWORegress*.cpp) and the stress and wave-test logs. In a shipping build every write is
// an empty inline, the probes do nothing and the readers see zeros.
//
// A new test counter goes here as a field (with who counts it, on which machine), not as a static on
// the gameplay class. A test switch that gameplay reads to decide what to do (log more, behave
// differently) is not a tally: it stays with the code it changes (UWWOCharacterMovement::bLogNetCorrections).

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class AWWOCharacter;
class UWWOWeaponComponent;

struct FWWOTestTally
{
	// --- The host's tally of what joiners sent it (AWWOCharacter's gun net, net and grenade RPCs) ---
	int32 ShotsTaken = 0;      // joiners' gun shots the host took (passed the shot checks)
	int32 HitsApplied = 0;     // their hits it applied
	int32 HitsRejected = 0;    // their hits it refused (a refused shot counts all its hits)
	int32 MeleeSwings = 0;
	int32 MeleeApplied = 0;
	int32 MeleeRejected = 0;
	int32 KillsCredited = 0;   // joiner: kills the host confirmed; host: kills it credited to joiners
	int32 AbilitiesUsed = 0;   // joiners' abilities the host ran
	int32 UltsCast = 0;        // and their ults
	int32 Throws = 0;          // joiners' grenade throws the host made
	int32 ThrowsRejected = 0;

	// --- What this machine drew for someone else ---
	int32 ShotsSeen = 0;       // shots from someone else (MulticastGunShot)
	int32 GrenadesSeen = 0;    // a joiner: the host's grenades
	int32 PoolsSeen = 0;       // a joiner: the host's spore pools
	int32 EnemyShotsSeen = 0;  // a joiner: the host's enemy shots
	int32 BlastsSeen = 0;      // a joiner: the host's blasts (AWWOGameState::Blast)

	// --- Corrections this machine got from the host since the last reset (UWWOCharacterMovement) ---
	// Error: how far the host's position was from yours after the same move, in cm (world-space corrections only).
	int32 Corrections = 0;
	float CorrectionErrorMax = 0.f;
	float CorrectionErrorSum = 0.f;

	// --- Hits and kills ---
	int32 HitsMarked = 0;       // hits that reached this machine's own reticle (UWWOWorldSubsystem::ConfirmHit)
	int32 EnemyHitsOnPlayer = 0; // enemy shots that hit a player directly (the wave test log)
	int32 PvPKills = 0;         // team deathmatch kills scored (host)
	int32 PvPMatchesEnded = 0;

	// The tests read the tallies here.
	static FWWOTestTally& Get();

	// Gameplay: add By to one tally. Correction: one correction's error (its max and sum).
	static void Count(int32 FWWOTestTally::* Tally, int32 By = 1);
	static void CorrectionError(float Error);
	static void ResetCorrections();

	// The body autotest's pictures of the hands: one gun moved (Godot space, m) and turned (radians:
	// pitch, yaw, roll) to bring its grip into view. Zero, zero: back in place.
	static void LiftGun(const UWWOWeaponComponent* Gun, const FVector& Offset, const FVector& Rotation);
	// Gameplay (the gun's view): adds Gun's lift, if it is the lifted one, to its position and turn.
	static void AddGunLift(const UWWOWeaponComponent* Gun, FVector& Pos, FVector& Rot);

	// Shots at Player's drawn head from all around (the stress log's "Bots heads" line): how many
	// count as headshots now, and how many did when the head hitbox sat at the eyes and only the
	// first part hit counted. Adds to the counts; HeadOffEyes is the largest head-to-eye distance.
	static void ProbeHeadShots(const AWWOCharacter& Player, int32& Shots, int32& Crits, int32& OldCrits, float& HeadOffEyes);

private:
	TWeakObjectPtr<const UWWOWeaponComponent> LiftedGun;
	FVector LiftOffset = FVector::ZeroVector;
	FVector LiftRotation = FVector::ZeroVector;
};

#if UE_BUILD_SHIPPING
inline void FWWOTestTally::Count(int32 FWWOTestTally::*, int32) {}
inline void FWWOTestTally::CorrectionError(float) {}
inline void FWWOTestTally::ResetCorrections() {}
inline void FWWOTestTally::LiftGun(const UWWOWeaponComponent*, const FVector&, const FVector&) {}
inline void FWWOTestTally::AddGunLift(const UWWOWeaponComponent*, FVector&, FVector&) {}
inline void FWWOTestTally::ProbeHeadShots(const AWWOCharacter&, int32&, int32&, int32&, float&) {}
#else
inline void FWWOTestTally::Count(int32 FWWOTestTally::* Tally, int32 By) { Get().*Tally += By; }
inline void FWWOTestTally::CorrectionError(float Error)
{
	FWWOTestTally& T = Get();
	T.CorrectionErrorMax = FMath::Max(T.CorrectionErrorMax, Error);
	T.CorrectionErrorSum += Error;
}
inline void FWWOTestTally::ResetCorrections()
{
	FWWOTestTally& T = Get();
	T.Corrections = 0;
	T.CorrectionErrorMax = T.CorrectionErrorSum = 0.f;
}
#endif
