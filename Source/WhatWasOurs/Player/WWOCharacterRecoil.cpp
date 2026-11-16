// AWWOCharacter: the recoil state machine: a shot's kick (AddRecoil), the rise and the fall back each
// frame (UpdateRecoil, the shaped RiseShape / FallShape tables), and kicking the view (ApplyRecoil).
// Matched to footage frame by frame (AGENTS.md rule 6): change it only as a pure move.

#include "Player/WWOCharacter.h"
#include "WhatWasOurs.h"
#include "Player/WWOCharacterPrivate.h"
#include "WWOTypes.h"
#include "Engine/World.h"
#include "Player/WWOAimAssistComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Controller.h"

using namespace WWOCharacter; // the constants these files share (WWOCharacterPrivate.h)

// --- Recoil -----------------------------------------------------------------------------------

void AWWOCharacter::ApplyRecoil(float PitchRad, float YawRad)
{
	const float Yaw = (Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw) - FMath::RadiansToDegrees(YawRad);
	SetViewRotation(Pitch + PitchRad, Yaw);
	// Godot sets camera.rotation.x directly, so a shot's kick shows (and aims the next shot)
	// this frame instead of at the next UpdateCamera.
	if (!bUlting)
	{
		const FRotator CameraRotation = Camera->GetRelativeRotation();
		Camera->SetRelativeRotation(FRotator(FMath::RadiansToDegrees(Pitch + AppliedReticleDrop), CameraRotation.Yaw, CameraRotation.Roll));
	}
}

void AWWOCharacter::ResetRecoil()
{
	KickLeft = KickYawLeft = 0.f;
	bRecovering = false;
}

namespace
{
	// The shaped recoil (UWWOWeaponData::RecoilShape): how a hand cannon's view moves in Destiny 2,
	// read off 60 fps footage. The rise is quick then steady (it reaches the top in the kick time);
	// the fall starts slowly, speeds up, and tails off (it takes the recovery time).
	const float RiseShape[] = { 0.f, 0.288f, 0.475f, 0.644f, 0.729f, 0.797f, 0.864f, 0.932f, 1.f };
	const float FallShape[] = { 1.f, 0.915f, 0.83f, 0.746f, 0.627f, 0.458f, 0.356f, 0.271f, 0.186f, 0.136f, 0.105f, 0.08f, 0.055f, 0.035f, 0.015f, 0.f };
	template <int32 N>
	float Shape(const float (&Table)[N], float Progress)
	{
		const float At = FMath::Clamp(Progress, 0.f, 1.f) * (N - 1);
		const int32 i = FMath::Min(FMath::FloorToInt(At), N - 2);
		return FMath::Lerp(Table[i], Table[i + 1], At - i);
	}
}

void AWWOCharacter::AddRecoil(float PitchRad, float YawRad, float KickTime, float Recovery, float InRecoveryTime, float InRecoveryCurve)
{
	// With a kick time, the view rises over that time, then falls back by Recovery of the
	// kick. Shooting again before it settles stacks from where you are.
	AimAssist->NotifyShot(); // hidden assist: the after-shot window opens
	if (KickTime <= 0.f && Recovery <= 0.f)
	{
		ApplyRecoil(PitchRad, YawRad);
		AimAssist->SteerRecoil(FMath::Sqrt(PitchRad * PitchRad + YawRad * YawRad), true); // hides in the instant kick
		return;
	}
	KickLeft += PitchRad;
	KickYawLeft += YawRad;
	KickRate = KickLeft / FMath::Max(KickTime, 0.001f);
	bKickShaped = InRecoveryCurve < 0.f; // a negative curve asks for the shaped rise and fall
	KickShapeTotal = KickLeft;
	KickShapeTime = FMath::Max(KickTime, 0.001f);
	KickShapeAge = 0.f;
	const float Peak = Pitch + KickLeft;
	// Only this shot's kick falls back. Shaped recoil also still owes whatever the shots before it
	// hadn't fallen back yet, so a burst, or shots in quick succession, come back down as one.
	const float Owed = bKickShaped && bRecovering && RecoveryCurve < 0.f ? TimedLeft(RecoveryAge) : 0.f;
	RecoilRest = Peak - PitchRad * FMath::Clamp(Recovery, 0.f, 1.f) - Owed;
	RecoveryTime = FMath::Max(InRecoveryTime, 0.01f);
	bRecovering = Recovery > 0.f;
	RecoveryCurve = InRecoveryCurve;
	RecoveryAge = 0.f;
	RecoveryDrop = Peak - RecoilRest;
	RecoveryLeft = RecoveryDrop;
}

float AWWOCharacter::TimedLeft(float Age) const
{
	const float Progress = FMath::Clamp(Age / RecoveryTime, 0.f, 1.f);
	if (RecoveryCurve < 0.f) return RecoveryDrop * Shape(FallShape, Progress);
	return RecoveryDrop * FMath::Pow(1.f - Progress, RecoveryCurve);
}

void AWWOCharacter::UpdateRecoil(float DeltaSeconds)
{
	if (KickLeft > 0.f)
	{
		float Step = KickRate * DeltaSeconds;
		if (bKickShaped)
		{
			const float Before = Shape(RiseShape, KickShapeAge / KickShapeTime);
			KickShapeAge += DeltaSeconds;
			Step = KickShapeAge >= KickShapeTime ? KickLeft : KickShapeTotal * (Shape(RiseShape, KickShapeAge / KickShapeTime) - Before);
		}
		Step = FMath::Min(KickLeft, Step);
		const float Fraction = Step / KickLeft;
		const float YawStep = KickYawLeft * Fraction;
		KickLeft -= Step;
		KickYawLeft -= YawStep;
		ApplyRecoil(Step, YawStep);
		AimAssist->SteerRecoil(FMath::Sqrt(Step * Step + YawStep * YawStep), true); // hides in the kick
		return;
	}
	if (!bRecovering) return;
	const float YawNow = Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw;
	if (Pitch <= RecoilRest + 0.0002f)
	{
		bRecovering = false; // settled, or you already pulled it down yourself
		return;
	}
	if (RecoveryCurve != 0.f)
	{
		// Timed: what's left falls as (1 - progress)^curve, landing on time. Moves by the
		// change each frame, so your own aiming adds on top.
		const float Before = TimedLeft(RecoveryAge);
		RecoveryAge += DeltaSeconds;
		const float After = TimedLeft(RecoveryAge);
		SetViewRotation(FMath::Max(Pitch - (Before - After), RecoilRest), YawNow);
		AimAssist->SteerRecoil(Before - After, false); // hidden assist: the fall-back bends toward the target
		if (After <= 0.f) bRecovering = false;
		return;
	}
	// Free: what's left of the kick eases out exponentially.
	const float Before = RecoveryLeft;
	RecoveryLeft *= FMath::Exp(-DeltaSeconds * 3.f / RecoveryTime);
	SetViewRotation(FMath::Max(Pitch - (Before - RecoveryLeft), RecoilRest), YawNow);
	AimAssist->SteerRecoil(Before - RecoveryLeft, false);
	if (RecoveryLeft <= 0.0002f) bRecovering = false;
}
