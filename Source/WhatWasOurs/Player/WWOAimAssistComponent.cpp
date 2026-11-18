#include "Player/WWOAimAssistComponent.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOWeaponComponent.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Components/CapsuleComponent.h"

UWWOAimAssistComponent::UWWOAimAssistComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UWWOAimAssistComponent::UsingController() const
{
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	const AWWOPlayerController* PC = P ? P->GetWWOController() : nullptr;
	return PC && PC->bUsingController;
}

float UWWOAimAssistComponent::DeviceScale() const
{
	return UsingController() ? 1.f : MouseAssist;
}

float UWWOAimAssistComponent::ConeScale() const
{
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	const UWWOWeaponData* W = P->Weapon->GetWeaponData();
	return W ? DeviceScale() * W->AimAssistance / 100.f * FMath::Lerp(1.f, W->AdsBoost, P->Weapon->AdsAmount) : 0.f;
}

float UWWOAimAssistComponent::EffectiveRange() const
{
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	const UWWOWeaponData* W = P->Weapon->GetWeaponData();
	return W ? W->AssistRange * FMath::Lerp(1.f, W->Zoom, P->Weapon->AdsAmount) : 0.f; // zoomed guns reach further
}

float UWWOAimAssistComponent::RangeFalloff(float DistanceM) const
{
	const float Full = EffectiveRange();
	return Full > 0.f ? FMath::Clamp(1.f - (DistanceM - Full) / (Full * 0.5f), 0.f, 1.f) : 0.f;
}

FWWOAssistPart UWWOAimAssistComponent::FindPart(float BaseAngleDeg, float CritScale) const
{
	// The head or body closest to the reticle inside the cone.
	FWWOAssistPart Best;
	const float Scale = ConeScale();
	if (Scale <= 0.f || BaseAngleDeg <= 0.f) return Best;
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	const FVector Origin = P->EyeLocation();
	const FVector Forward = P->AimForward();
	// A part's cone: the base angle, scaled for the device and the range, narrower for a crit spot.
	auto ConeOf = [&](const FWWOAimPart& Part, float Distance)
	{
		float Cone = FMath::DegreesToRadians(BaseAngleDeg) * Scale * RangeFalloff(Distance / WWO::M);
		if (Part.bCritical) Cone *= CritScale;
		return Cone;
	};
	// Scored by the angle off the reticle to the part's edge; head first, so heads win ties; must be in line of sight.
	const FWWOAimPick Pick = UWWOWorldSubsystem::Get(this)->BestAimPart(this, [&](const AActor*, const FWWOAimPart& Part, float, float& Score)
	{
		const FVector To = Part.Position - Origin;
		const float Distance = To.Size();
		if (Distance < 1.f) return false;
		const float Cone = ConeOf(Part, Distance);
		if (Cone <= 0.f) return false;
		const float Angle = FMath::Max(FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward, To / Distance), -1.f, 1.f)) - FMath::Atan(Part.Radius / Distance), 0.f);
		if (Angle > Cone) return false;
		Score = Angle;
		return true;
	}, [P](const FVector& To, FHitResult& Hit) { return P->Weapon->CastRayTo(To, Hit); });
	if (!Pick.IsValid()) return Best;
	Best.Target = Pick.Actor;
	Best.Position = Pick.Part.Position;
	Best.bCritical = Pick.Part.bCritical;
	Best.Angle = Pick.Score;
	Best.Cone = ConeOf(Pick.Part, (Pick.Part.Position - Origin).Size());
	return Best;
}

bool UWWOAimAssistComponent::Magnetize(FHitResult& OutHit) const
{
	// For a shot that missed: bend it onto the nearest target part in the magnetism cone.
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	const UWWOWeaponData* W = P->Weapon->GetWeaponData();
	if (!W) return false;
	// Hidden assist: a follow-up shot (the last one's recoil still playing) gets a wider cone.
	const float Boost = FMath::Lerp(1.f, FMath::Max(W->FollowupMagnetism, 1.f), Window);
	FWWOAssistPart Part = FindPart(W->MagnetismAngle * Boost, W->CritMagnetism);
	if (!Part.IsValid()) return false;
	if (!P->Weapon->CastRayTo(Part.Position, OutHit) || OutHit.GetActor() != Part.Target.Get()) return false;
	if (Boost > 1.001f && !FindPart(W->MagnetismAngle, W->CritMagnetism).IsValid())
	{
		const_cast<UWWOAimAssistComponent*>(this)->LastMagnet = GetWorld()->GetTimeSeconds(); // only the boost caught it
	}
	return true;
}

void UWWOAimAssistComponent::UpdateHiddenWindow(float DeltaSeconds)
{
	// Open while the recoil kicks and falls back (at least MinWindow after a shot, for guns whose
	// recoil is instant), then fades out over 0.12 s.
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	if (HiddenAssist < 0.5f || !P) Window = 0.f;
	else if (P->IsRecoiling() || GetWorld()->GetTimeSeconds() - LastShot < MinWindow) Window = 1.f;
	else Window = FMath::Max(Window - DeltaSeconds / 0.12f, 0.f);
}

void UWWOAimAssistComponent::NotifyShot()
{
	LastShot = GetWorld()->GetTimeSeconds();
	if (HiddenAssist >= 0.5f) Window = 1.f;
}

void UWWOAimAssistComponent::SteerRecoil(float StepRad, bool bKick)
{
	// As the recoil moves the view by StepRad (kicking up or falling back), also bend it toward the
	// target in the friction cone, by up to RecoverySteer of that step: the camera is already
	// moving, so this hides in it.
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	const UWWOWeaponData* W = P ? P->Weapon->GetWeaponData() : nullptr;
	const float Strength = !W ? 0.f : bKick ? W->KickSteer : W->RecoverySteer;
	if (!W || Window <= 0.f || Strength <= 0.f || StepRad <= 0.f || P->IsLunging()) return;
	const FWWOAssistPart Part = FindPart(W->FrictionAngle);
	if (!Part.IsValid()) return;
	const FVector Aim = P->AimForward();
	const FVector To = (Part.Position - P->EyeLocation()).GetSafeNormal();
	const float YawRad = FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(Aim.Rotation().Yaw, To.Rotation().Yaw));
	// The kick only steers sideways: its climb stays the same as over a wall.
	const float PitchRad = bKick ? 0.f : FMath::Asin(FMath::Clamp(To.Z, -1.f, 1.f)) - FMath::Asin(FMath::Clamp(Aim.Z, -1.f, 1.f));
	const float Off = FMath::Sqrt(YawRad * YawRad + PitchRad * PitchRad);
	if (Off < 0.0002f) return;
	const float Budget = Strength * StepRad * Window;
	const float T = FMath::Min(Budget / Off, 1.f);
	const float YawNow = P->Controller ? P->Controller->GetControlRotation().Yaw : P->GetActorRotation().Yaw;
	P->SetViewRotation(P->GetPitch() + PitchRad * T, YawNow + FMath::RadiansToDegrees(YawRad) * T);
	if (Budget > 0.0005f) LastSteer = GetWorld()->GetTimeSeconds();
}

float UWWOAimAssistComponent::FrictionScale()
{
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	const UWWOWeaponData* W = P->Weapon->GetWeaponData();
	FrictionPart = (P->IsLunging() || !W) ? FWWOAssistPart() : FindPart(W->FrictionAngle); // the lunge steers the camera alone
	if (!FrictionPart.IsValid()) return 1.f;
	const float Closeness = 1.f - FrictionPart.Angle / FrictionPart.Cone;
	// Hidden assist: stronger while a shot's recoil plays out.
	const float Kick = FMath::Lerp(1.f, FMath::Max(W->KickFriction, 1.f), Window);
	const AWWOPlayerController* PC = P->GetWWOController();
	if (Kick > 1.001f && PC && PC->GetLookAxis().Size() > 0.1f) LastSticky = GetWorld()->GetTimeSeconds(); // only matters while you turn
	const float Slow = W->FrictionStrength * Kick * Closeness * FMath::Lerp(1.f, W->AdsBoost, P->Weapon->AdsAmount);
	return 1.f - FMath::Clamp(Slow, 0.f, 0.9f);
}

void UWWOAimAssistComponent::UpdateAdhesion()
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	const UWWOWeaponData* W = P->Weapon->GetWeaponData();
	const AWWOPlayerController* PC = P->GetWWOController();
	if (!W || !PC) return;
	const float Adhesion = W->Adhesion + FMath::Max(W->KickAdhesion, 0.f) * Window; // hidden assist: more during the recoil
	const bool bActive = Adhesion > 0.f && (PC->GetLookAxis().Size() > 0.1f || PC->GetMoveAxis().Size() > 0.1f) && !P->IsLunging();
	const FWWOAssistPart Part = bActive ? FindPart(W->FrictionAngle) : FWWOAssistPart();
	if (!Part.IsValid())
	{
		AdhesionKey.Reset();
		return;
	}
	// Direction from your eyes to the target, ignoring where you're looking, so this only
	// measures movement (theirs or yours), not your own turning.
	// Godot's origin stays 1 m above the feet when crouched (the capsule shrinks from the feet),
	// so this eye point doesn't move on a crouch; Unreal's capsule center drops, so measure from the feet.
	const float FeetZ = P->GetActorLocation().Z - P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Eye(P->GetActorLocation().X, P->GetActorLocation().Y, FeetZ + 160.f); // feet + 1 m + STAND_EYE_HEIGHT
	const FVector Dir = (Part.Position - Eye).GetSafeNormal();
	const FString Key = FString::Printf(TEXT("%d:%d"), Part.Target->GetUniqueID(), Part.bCritical ? 1 : 0);
	if (Key == AdhesionKey)
	{
		const float YawChange = FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(AdhesionLastDir.Rotation().Yaw, Dir.Rotation().Yaw));
		const float PitchChange = FMath::Asin(Dir.Z) - FMath::Asin(AdhesionLastDir.Z);
		const float Pull = Adhesion * DeviceScale();
		if (W->KickAdhesion > 0.f && Window > 0.f) LastSticky = GetWorld()->GetTimeSeconds();
		P->ApplyRecoil(PitchChange * Pull, -YawChange * Pull); // ApplyRecoil's yaw is + to the left
	}
	AdhesionKey = Key;
	AdhesionLastDir = Dir;
}
