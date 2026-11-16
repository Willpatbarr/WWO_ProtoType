// AWWOCharacter: the view: looking (Look, SetViewRotation, TurnToward), the camera each frame (eye
// height, landing dip, head bob: UpdateCamera) and the low reticle (AimForward, the drop angle).

#include "Player/WWOCharacter.h"
#include "WhatWasOurs.h"
#include "Player/WWOCharacterPrivate.h"
#include "WWOTypes.h"
#include "Engine/World.h"
#include "Systems/WWOSound.h"
#include "Player/WWOWeaponComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/Controller.h"

using namespace WWOCharacter; // the constants these files share (WWOCharacterPrivate.h)

namespace
{
	constexpr float CrouchCameraZ = 20.f;     // above the crouched capsule center (eye 0.8 m off the floor)
	constexpr float CameraSmoothing = 12.f;
	constexpr float WalkBobUp = 0.06f;        // meters of up/down at intensity 1
	constexpr float SprintBobUp = 0.11f;
	constexpr float WalkBobSide = 0.04f;
	constexpr float SprintBobSide = 0.07f;
	constexpr float LungeTurnRate = 20.f;
}

FVector AWWOCharacter::EyeLocation() const
{
	return Camera->GetComponentLocation();
}

FVector AWWOCharacter::AimForward() const
{
	// The camera looks up by the reticle drop; the aim is that much below its center.
	if (AppliedReticleDrop <= 0.f || bUlting) return Camera->GetForwardVector();
	const FQuat Down = FRotator(-FMath::RadiansToDegrees(AppliedReticleDrop), 0.f, 0.f).Quaternion();
	return (Camera->GetComponentQuat() * Down).GetForwardVector();
}

float AWWOCharacter::ReticleAngleForFov(float HorizontalFovDeg) const
{
	// A point Drop of the screen height below center is atan(2 * Drop * tan(vertical half FOV))
	// below the view axis; the FOV is horizontal, so the vertical one comes from the aspect.
	if (bUlting || ReticleDrop <= 0.f) return 0.f;
	FVector2D Viewport(16.f, 9.f);
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(Viewport);
	const float TanHalfV = FMath::Tan(FMath::DegreesToRadians(HorizontalFovDeg) * 0.5f) * Viewport.Y / FMath::Max(Viewport.X, 1.f);
	return FMath::Atan(2.f * ReticleDrop * TanHalfV);
}

float AWWOCharacter::ReticleDropAngle() const
{
	// For the world camera's FOV, so the reticle keeps its place on screen as ADS zooms in.
	return ReticleAngleForFov(Camera->FieldOfView);
}

// --- Looking --------------------------------------------------------------------------------

void AWWOCharacter::SetViewRotation(float PitchRad, float YawDeg)
{
	// The camera looks up by the low reticle's angle on top of this: keep the two under straight up.
	Pitch = FMath::Clamp(PitchRad, FMath::DegreesToRadians(-89.f), FMath::DegreesToRadians(89.f) - FMath::Max(AppliedReticleDrop, 0.f));
	if (Controller)
	{
		Controller->SetControlRotation(FRotator(FMath::RadiansToDegrees(Pitch), YawDeg, 0.f));
	}
	SetActorRotation(FRotator(0.f, YawDeg, 0.f));
}

void AWWOCharacter::Look(FVector2D Amount)
{
	Amount *= Weapon->LookMultiplier(); // aiming down sights slows turning
	Weapon->AddLook(Amount);
	const float Yaw = (Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw) + FMath::RadiansToDegrees(Amount.X);
	SetViewRotation(Pitch + Amount.Y, Yaw);
}

void AWWOCharacter::TurnToward(const FVector& Point, float Strength, float DeltaSeconds)
{
	// Swing the view toward a point. Strength 1 = locks on almost instantly, 0 = no pull.
	if (Strength <= 0.f) return;
	const FVector Dir = (Point - EyeLocation()).GetSafeNormal();
	const float Weight = FMath::Min(Strength * LungeTurnRate * DeltaSeconds, 1.f);
	const float CurrentYaw = Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw;
	const float TargetYaw = Dir.Rotation().Yaw;
	const float Yaw = CurrentYaw + FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw) * Weight;
	SetViewRotation(FMath::Lerp(Pitch, FMath::Asin(Dir.Z), Weight), Yaw);
}

void AWWOCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	CameraBaseZ += ScaledHalfHeightAdjust; // the capsule dropped: keep the eye where it was, then ease down
}

void AWWOCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	CameraBaseZ -= ScaledHalfHeightAdjust;
}

void AWWOCharacter::UpdateCamera(float DeltaSeconds)
{
	// Godot's ult/entrench branch returns before any camera update, so the eye, dip and bob
	// hold still while you're frozen or rooted.
	const bool bFrozen = bUlting || EntrenchLeft > 0.f;
	if (bFrozen) DeltaSeconds = 0.f;
	// Eye height eases between standing and crouched.
	const float Target = bIsCrouched ? CrouchCameraZ : StandCameraZ;
	CameraBaseZ = FMath::Lerp(CameraBaseZ, Target, WWO::Damp(CameraSmoothing, DeltaSeconds)); // the same ease at any frame rate

	// Landing dip: the camera drops quickly toward the kick while the kick eases back.
	LandOffset = FMath::Lerp(LandOffset, LandKick, WWO::Damp(25.f, DeltaSeconds));
	LandKick = FMath::Lerp(LandKick, 0.f, WWO::Damp(8.f, DeltaSeconds));

	// Head bob: up/down once per step, side to side once per two steps.
	const float Speed = FVector2D(GetVelocity().X, GetVelocity().Y).Size() / WWO::M;
	// Godot skips the head bob while mantling, so the bob holds where it was until you're up.
	const float BobDelta = MoveState.bMantling ? 0.f : DeltaSeconds;
	const bool bWalking = IsOnFloor() && !MoveState.bSliding && Speed > 0.5f;
	SprintBlend = WWO::MoveToward(SprintBlend, MoveState.bSprinting ? 1.f : 0.f, 3.f * BobDelta);
	const float Stride = FMath::Max(FMath::Lerp(WalkStride, SprintStride, SprintBlend), 0.1f);
	const int32 StepBefore = FMath::FloorToInt(BobTime / UE_PI);
	if (bWalking) BobTime += Speed / Stride * UE_PI * BobDelta; // one step = half a sine wave
	if (FMath::FloorToInt(BobTime / UE_PI) != StepBefore && !MoveState.bCrouching) UWWOSound::PlayFor(this, "footstep", MoveState.bSprinting ? 1.f : 0.75f); // a foot comes down (yours flat, anyone else's from where they are)
	BobWeight = WWO::MoveToward(BobWeight, bWalking ? 1.f : 0.f, 4.f * BobDelta);
	const float Amount = BobIntensity * BobWeight * FMath::Lerp(1.f, FMath::Clamp(AdsBob, 0.f, 1.f), Weapon->AdsAmount); // the view bobs aimed in too; the aim mark is on the HUD, so it holds still
	const float Up = FMath::Lerp(WalkBobUp, SprintBobUp, SprintBlend) * Amount;
	const float Side = FMath::Lerp(WalkBobSide, SprintBobSide, SprintBlend) * Amount;
	if (!bUlting)
	{
		Camera->SetRelativeLocation(FVector(0.f, FMath::Cos(BobTime * 0.5f) * Side * WWO::M,
			CameraBaseZ + (LandOffset + FMath::Abs(FMath::Sin(BobTime)) * Up) * WWO::M));
		AppliedReticleDrop = ReticleDropAngle();
		Camera->SetRelativeRotation(FRotator(FMath::RadiansToDegrees(Pitch + AppliedReticleDrop), 0.f, Weapon->CameraRoll()));
	}
}
