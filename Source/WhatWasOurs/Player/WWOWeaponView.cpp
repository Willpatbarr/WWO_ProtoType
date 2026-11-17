// UWWOWeaponComponent: the view: the first-person gun's pose each frame (UpdatePose, which then runs
// the hands), the field of view, the camera punch, shakes and rumble.

#include "Player/WWOWeaponComponent.h"
#include "WhatWasOurs.h"
#include "Player/WWOWeaponPrivate.h"
#include "Player/WWOCharacter.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Engine/World.h"
#include "Player/WWOPlayerController.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/WWOTestTally.h"

using namespace WWOWeapon; // the helpers these files share (WWOWeaponPrivate.h)

namespace
{
	constexpr float SwayPerTurnSpeed = 0.025f;
	constexpr float SwayMax = 0.08f;
	const FVector SprintOffset(-0.1f, 0.03f, 0.02f);
	const FVector SprintRotation(-0.15f, 0.55f, 0.45f); // muzzle down and left, canted
	const FVector SlideOffset(0.f, 0.07f, 0.f);
	constexpr float AirLagPerSpeed = 0.008f;
	constexpr float AirLagMax = 0.06f;
	const FVector MantleOffset(0.02f, -0.14f, 0.06f);
	const FVector MantleRotation(-0.5f, 0.f, 0.25f);
	const FVector ThrowOffset(0.03f, -0.1f, 0.04f);
	const FVector ThrowRotation(-0.35f, -0.15f, 0.f);
	const FVector SwapOffset(0.f, -0.22f, 0.06f);
	const FVector SwapRotation(-0.7f, 0.f, 0.f);
	constexpr float ReloadTilt = 0.35f;
	const FVector ReloadLift(-0.05f, 0.1f, -0.12f);
	constexpr float ReloadRoll = -0.7f;
	const FVector MeleeGunDropOffset(0.03f, -0.3f, 0.12f);
	const FVector MeleeGunDropRotation(-0.9f, 0.f, 0.25f);
}

void UWWOWeaponComponent::StartThrowPose(const FLinearColor& GrenadeColor)
{
	ThrowTime = ThrowPoseLength();
	ThrowClock = 0.f;
	ThrowColor = GrenadeColor;
}

void UWWOWeaponComponent::StartPunch(float FovDegrees, float RollDegrees, float Seconds)
{
	// A quick camera jolt: the FOV and roll snap to these, then ease back over Seconds.
	PunchAge = 0.f;
	PunchFovValue = FovDegrees;
	PunchRollValue = RollDegrees;
	PunchLength = FMath::Max(Seconds, 0.01f);
}

void UWWOWeaponComponent::Shake(float Strength, float Seconds)
{
	// The view widens and tips for a moment (your aim doesn't move) and the controller thumps.
	if (Strength <= 0.01f) return;
	Strength = FMath::Min(Strength, 2.f);
	StartPunch(3.f * Strength, 2.2f * Strength * (WWO::FRand() < 0.5f ? 1.f : -1.f), Seconds);
	Rumble(0.35f * Strength, 0.8f * Strength, Seconds * 0.8f);
}

void UWWOWeaponComponent::Rumble(float Weak, float Strong, float Seconds)
{
	if (AWWOPlayerController* PC = GetPlayer()->GetWWOController()) PC->Rumble(Weak, Strong, Seconds);
}

float UWWOWeaponComponent::PunchAmount() const
{
	// Snaps up over a few ms, then eases back to 0.
	if (PunchAge >= PunchLength) return 0.f;
	const float Attack = FMath::Min(0.015f, PunchLength * 0.25f);
	if (PunchAge < Attack) return PunchAge / Attack;
	const float T = (PunchAge - Attack) / (PunchLength - Attack);
	return (1.f - T) * (1.f - T);
}

float UWWOWeaponComponent::CameraRoll() const
{
	return -PunchRollValue * PunchAmount() * PunchScale; // Godot +roll = tilt left
}

void UWWOWeaponComponent::UpdateFov()
{
	UCameraComponent* Camera = GetPlayer()->Camera;
	const float Punch = PunchAmount() * PunchScale;
	// The punch widens the hip FOV, so aiming in (which divides by zoom) scales it down too.
	const float Fov = FMath::Clamp(HipFov + PunchFovValue * Punch, 1.f, 170.f);
	const float Half = FMath::Tan(FMath::DegreesToRadians(Fov) * 0.5f) / FMath::Lerp(1.f, Weapon->Zoom, AdsAmount);
	Camera->SetFieldOfView(FMath::RadiansToDegrees(FMath::Atan(Half) * 2.f));
	// The gun and the hands draw with their own FOV.
	Camera->FirstPersonFieldOfView = Weapon->ViewmodelFov;
	// Scoped weapons hide the gun once the scope is up (the HUD draws the scope).
	const bool bShowModel = !(Weapon->ScopeOverlay >= 0.5f && AdsAmount > 0.85f);
	for (const auto& Pair : Parts)
	{
		// Mod parts that would block the view through the sight (scopes, holo frames) fade out of the way.
		const bool bAdsHidden = AdsHiddenParts.Contains(Pair.Key) && AdsAmount > 0.6f;
		// (The dot on the gun is never drawn: the HUD draws the aim mark at the aim point, where the
		// gun's kick and sway can't move it. See UWWOWeaponData::AdsMark.)
		if (Pair.Value) Pair.Value->SetVisibility(!bFirstPersonHidden && bShowModel && !bAdsHidden && Pair.Key != "SightDot" && (!IsFlashPart(Pair.Key) || bFlashVisible));
	}
}

void UWWOWeaponComponent::UpdatePose(float DeltaSeconds)
{
	AWWOCharacter* P = GetPlayer();
	const float Settle = 1.f - 0.75f * AdsAmount; // aiming steadies the gun

	// Sway: the gun lags behind your turning, then catches up.
	const FVector2D TurnSpeed = LookDelta / FMath::Max(DeltaSeconds, 0.001f);
	LookDelta = FVector2D::ZeroVector;
	// Look amounts are + right / + up; the Godot pose convention has + down for pitch.
	FVector2D Target = FVector2D(TurnSpeed.X, -TurnSpeed.Y) * SwayPerTurnSpeed;
	Target = Target.GetClampedToMaxSize(SwayMax) * Weapon->Sway * Settle;
	SwayValue = FMath::Lerp(SwayValue, Target, FMath::Min(10.f * DeltaSeconds, 1.f));

	// Action poses fade in and out; aiming mostly cancels them.
	SprintPose = WWO::MoveToward(SprintPose, P->MoveState.bSprinting ? 1.f : 0.f, 5.f * DeltaSeconds);
	SlidePose = WWO::MoveToward(SlidePose, P->MoveState.bSliding ? 1.f : 0.f, 6.f * DeltaSeconds);
	const float HipOnly = 1.f - AdsAmount;
	const float SprintAmount = WWO::SmoothStep(0.f, 1.f, SprintPose) * HipOnly;
	const float SlideAmount = WWO::SmoothStep(0.f, 1.f, SlidePose) * (1.f - 0.8f * AdsAmount); // the tilt
	const float SlideShift = WWO::SmoothStep(0.f, 1.f, SlidePose) * HipOnly;                   // the move

	// The rule the rest of this follows: aimed in, the gun's sight point (the model's origin) stays
	// on the aim point, where the HUD draws the aim mark. Everything that turns the gun turns it
	// about that point, and everything that moves it bodily (bob, air lag, landing, the slide's
	// shift) fades out as you aim in. Only the strafe's small sideways shift is kept.

	// Air: the gun lags behind vertical motion.
	float AirTarget = 0.f;
	if (!P->IsOnFloor() && !P->IsLunging())
	{
		AirTarget = FMath::Clamp(-P->GetVelocity().Z / WWO::M * AirLagPerSpeed, -AirLagMax, AirLagMax);
	}
	AirLag = FMath::Lerp(AirLag, AirTarget, FMath::Min(8.f * DeltaSeconds, 1.f));
	const FVector AirOffset = FVector(0.f, AirLag + P->LandOffset * 0.6f, 0.f) * HipOnly;

	// Bob: follows the player's step timing. Sprinting swings it much harder.
	const FVector Swing(FMath::Cos(P->BobTime * 0.5f), -FMath::Abs(FMath::Sin(P->BobTime)), 0.f);
	// From the hip the gun only dips (each step pushes it down from rest). Aimed in, the same dip is
	// centred on rest instead, so the sight rides evenly above and below the aim point.
	const FVector AimedSwing = Swing + FVector(0.f, 0.5f * AdsAmount, 0.f);
	FVector Bob = AimedSwing * FVector(0.012f, 0.01f, 0.f) * Weapon->GunBob * FMath::Lerp(1.f, FMath::Clamp(Weapon->AdsGunBob, 0.f, 1.f), AdsAmount); // (the one bodily move kept when aimed, with the strafe shift)
	Bob.X *= HipOnly; // aimed in the bob is up and down only: sideways the gun is where the strafe shift puts it
	Bob += Swing * FVector(0.03f, 0.025f, 0.f) * Weapon->GunBob * SprintAmount;
	Bob *= P->BobWeight;
	const float BobRoll = FMath::Cos(P->BobTime * 0.5f) * 0.08f * Weapon->GunBob * SprintAmount * P->BobWeight;

	FVector Rot(SwayValue.Y + AirLag * 2.f * Settle, SwayValue.X, BobRoll);
	Rot += SprintRotation * SprintAmount + FVector(FMath::DegreesToRadians(Weapon->SlideTilt), 0.f, 0.f) * SlideAmount;
	// Strafing: the gun trails the way you move and rolls with it.
	const float SideSpeed = FVector::DotProduct(P->GetVelocity(), P->GetActorRightVector()) / WWO::M;
	// (Against the speed you can go right now, so a full strafe is the whole of the sliders' amounts
	// aimed in too, where you move slower.)
	const float TopSpeed = FMath::Max(P->M("walk_speed", P->WalkSpeed) * FMath::Lerp(1.f, Weapon->AdsMoveSpeed, AdsAmount), 1.f);
	StrafeLean = FMath::Lerp(StrafeLean, FMath::Clamp(SideSpeed / TopSpeed, -1.f, 1.f), FMath::Min(8.f * DeltaSeconds, 1.f));
	Rot.Z += FMath::DegreesToRadians(Weapon->StrafeRoll) * StrafeLean;
	const float KickNow = Kick * 0.06f * FMath::Lerp(1.f, FMath::Clamp(Weapon->AdsKick, 0.f, 1.f), AdsAmount); // how much is left aimed in is the frame's
	Rot.X += KickNow * FMath::Max(Weapon->VerticalKick, 0.f);
	// The sideways part (the swing and the shift) comes back sooner than the tilt: it follows the
	// kick squared, so half way down the tilt it is a quarter of what it was (as in the footage).
	Rot.Y -= KickNow * Kick * Weapon->HorizontalKick; // Godot yaw: + turns left, so + kick swings the muzzle right
	// The low reticle: the gun (drawn with its own FOV) points at it, and aimed in, its sight point
	// (the model's origin) moves down onto it.
	const float GunDrop = P->ReticleAngleForFov(Weapon->ViewmodelFov);
	Rot.X -= GunDrop;
	FVector Pos = FMath::Lerp(Weapon->HipPosition, Weapon->AdsPosition, AdsAmount) + Bob + AirOffset;
	// Aimed in, level the top: the gun tips down about its sight point until its top's plane passes
	// through your eye, so the top is seen edge-on (the sight stays on the reticle).
	const float EyeToSight = FMath::Max(-Weapon->AdsPosition.Z * 100.f, 5.f); // cm
	const float LevelAngle = FMath::Atan2(TopBelowSight * FMath::Lerp(1.f, FMath::Max(AdsGunScale, 0.05f), AdsAmount), EyeToSight);
	Rot.X -= FMath::Min(LevelAngle, FMath::DegreesToRadians(8.f)) * FMath::Clamp(AdsLevel, 0.f, 1.f) * AdsAmount; // muzzle down: the top's near end comes up to the eye line
	Rot.X -= FMath::DegreesToRadians(Weapon->AdsTilt) * AdsAmount; // the gun's own aimed-in angle, also about the sight point
	Pos.Y -= -Pos.Z * FMath::Tan(GunDrop) * AdsAmount; // Godot: z is forward (negative), y up
	Pos += SprintOffset * SprintAmount + SlideOffset * SlideShift;
	Pos.X -= StrafeLean * Weapon->StrafeShift * 0.01f;
	// Mantling: hands are busy, so the gun dips out of the way.
	MantlePose = WWO::MoveToward(MantlePose, P->IsMantling() ? 1.f : 0.f, 8.f * DeltaSeconds);
	const float MantleAmount = WWO::SmoothStep(0.f, 1.f, MantlePose);
	Pos += MantleOffset * MantleAmount;
	Rot += MantleRotation * MantleAmount;
	// Grenade throw: quick dip down, then back up.
	ThrowTime = FMath::Max(ThrowTime - DeltaSeconds, 0.f);
	const float ThrowAmount = ThrowTime > 0.f ? FMath::Sin(UE_PI * ThrowTime / ThrowPoseLength()) : 0.f;
	Pos += ThrowOffset * ThrowAmount;
	Rot += ThrowRotation * ThrowAmount;
	Pos.Z += Kick * Weapon->KickPush * 0.01f; // back toward you at the peak
	Pos.X += Kick * Kick * Weapon->KickSide * 0.01f;
	{
		// The kick's turn is about a point below and ahead of the sight, not the sight itself: the
		// sight point swings down and back around it as the muzzle comes up.
		const float Turn = KickNow * FMath::Max(Weapon->VerticalKick, 0.f);
		const float Down = Weapon->KickPivotDown * 0.01f, Ahead = Weapon->KickPivotAhead * 0.01f;
		Pos.Y += -Ahead * FMath::Sin(Turn) - Down * (1.f - FMath::Cos(Turn));
		Pos.Z += -Ahead * (1.f - FMath::Cos(Turn)) + Down * FMath::Sin(Turn);
	}
	// Weapon swap: dip down and back up.
	SwapLeft = FMath::Max(SwapLeft - DeltaSeconds, 0.f);
	const float SwapAmount = SwapLeft > 0.f ? FMath::Sin(UE_PI * SwapLeft / SwapLength) : 0.f;
	Pos += SwapOffset * SwapAmount;
	Rot += SwapRotation * SwapAmount;
	// Reload: tip up and roll so the magazine well faces you; the magazine drops out and back.
	const float RP = bReloading ? ReloadProgress : 0.f;
	const float Tilt = WWO::SmoothStep(0.f, 0.2f, RP) * (1.f - WWO::SmoothStep(0.8f, 1.f, RP));
	Rot.X += ReloadTilt * Tilt;
	Rot.Z += ReloadRoll * Tilt;
	Pos += ReloadLift * Tilt;
	const float MagOut = WWO::SmoothStep(0.2f, 0.4f, RP) * (1.f - WWO::SmoothStep(0.5f, ReloadSeated, RP));
	if (UStaticMeshComponent* Mag = Part("Magazine")) Mag->SetRelativeLocation(G2U(MagRest + FVector(0.f, -ReloadMagDrop * MagOut, 0.f)) + ModelShift);
	// Melee: the gun drops out of the way while the arm swings.
	const float MP = bMeleeing ? MeleeProgress : 0.f;
	const float Drop = WWO::SmoothStep(0.f, 0.12f, MP) * (1.f - WWO::SmoothStep(0.72f, 0.97f, MP));
	// (It stays down until the punching hand is back: HandsActionWeight, from UpdateHands a frame ago.)
	const float DropHeld = FMath::Max(Drop, HandsAction == 1 ? WWO::SmoothStep(0.f, 1.f, HandsActionWeight) : 0.f);
	Pos += MeleeGunDropOffset * DropHeld;
	Rot += MeleeGunDropRotation * DropHeld;

	FWWOTestTally::AddGunLift(this, Pos, Rot); // the body autotest's pictures of the hands
	SetRelativeLocationAndRotation(G2U(Pos), GRot(Rot));
	ModelRoot->SetRelativeScale3D(FVector(FMath::Lerp(1.f, FMath::Max(AdsGunScale, 0.05f), AdsAmount))); // the model's origin is its sight point

	// The punch and the throw are played by the hands (UpdateHands).
	ThrowClock += DeltaSeconds;
	HandsDelta = DeltaSeconds;
	UpdateHands();
	// The grenade sits in the throwing (right) hand until it leaves.
	bool bBall = HandsMesh && HandsMesh->IsVisible() && HandsOwner.Num() > 0 && ThrowClock < ThrowRelease;
	const int32 Thrower = 1; // the left hand
	const FName ThrowBone = bBall ? HandsAsset->GetRefSkeleton().GetBoneName(HandsArms[Thrower].Hand) : NAME_None;
	// (Not while the hand is beside your head: that close the ball would fill the view.)
	if (bBall && HandsMesh->GetBoneTransformByName(ThrowBone, EBoneSpaces::ComponentSpace).GetLocation().X < 25.f) bBall = false;
	ThrowBall->SetVisibility(bBall);
	if (bBall)
	{
		const FTransform HandWorld = HandsMesh->GetBoneTransformByName(ThrowBone, EBoneSpaces::WorldSpace);
		const FQuat FromRef = HandsMesh->GetBoneTransformByName(ThrowBone, EBoneSpaces::ComponentSpace).GetRotation() * HandsArms[Thrower].HandRef.Inverse();
		const FVector InPalm = bHandsRigid ? FromRef.RotateVector(HandsArms[Thrower].RigidRef.GetAxisX() * 9.f + HandsArms[Thrower].RigidRef.GetAxisZ() * 4.f) : FVector::ZeroVector;
		ThrowBall->SetWorldLocation(HandWorld.GetLocation() + HandsMesh->GetComponentTransform().TransformVectorNoScale(InPalm));
	}
	if (UMaterialInstanceDynamic* Mat = Cast<UMaterialInstanceDynamic>(ThrowBall->GetMaterial(0)))
	{
		Mat->SetVectorParameterValue("Color", ThrowColor);
		Mat->SetScalarParameterValue("Glow", 2.f); // Godot: the held ball's glow
	}
}
