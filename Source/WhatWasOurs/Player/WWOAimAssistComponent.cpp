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
	for (AActor* Actor : UWWOWorldSubsystem::Get(this)->LivingTargets())
	{
		const IWWOTargetable* Target = Cast<IWWOTargetable>(Actor);
		for (const FWWOAimPart& Part : Target->GetAimParts()) // head first, so heads win ties
		{
			const FVector To = Part.Position - Origin;
			const float Distance = To.Size();
			if (Distance < 1.f) continue;
			float Cone = FMath::DegreesToRadians(BaseAngleDeg) * Scale * RangeFalloff(Distance / WWO::M);
			if (Part.bCritical) Cone *= CritScale;
			if (Cone <= 0.f) continue;
			const float Angle = FMath::Max(FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward, To / Distance), -1.f, 1.f)) - FMath::Atan(Part.Radius / Distance), 0.f);
			if (Angle > Cone || (Best.IsValid() && Angle >= Best.Angle)) continue;
			FHitResult Check;
			if (!P->Weapon->CastRayTo(Part.Position, Check) || Check.GetActor() != Actor) continue; // must be in line of sight
			Best.Target = Actor;
			Best.Position = Part.Position;
			Best.bCritical = Part.bCritical;
			Best.Angle = Angle;
			Best.Cone = Cone;
		}
	}
	return Best;
}

bool UWWOAimAssistComponent::Magnetize(FHitResult& OutHit) const
{
	// For a shot that missed: bend it onto the nearest target part in the magnetism cone.
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	const UWWOWeaponData* W = P->Weapon->GetWeaponData();
	if (!W) return false;
	const FWWOAssistPart Part = FindPart(W->MagnetismAngle, W->CritMagnetism);
	if (!Part.IsValid()) return false;
	return P->Weapon->CastRayTo(Part.Position, OutHit) && OutHit.GetActor() == Part.Target.Get();
}

float UWWOAimAssistComponent::FrictionScale()
{
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	const UWWOWeaponData* W = P->Weapon->GetWeaponData();
	FrictionPart = (P->IsLunging() || !W) ? FWWOAssistPart() : FindPart(W->FrictionAngle); // the lunge steers the camera alone
	if (!FrictionPart.IsValid()) return 1.f;
	const float Closeness = 1.f - FrictionPart.Angle / FrictionPart.Cone;
	const float Slow = W->FrictionStrength * Closeness * FMath::Lerp(1.f, W->AdsBoost, P->Weapon->AdsAmount);
	return 1.f - FMath::Clamp(Slow, 0.f, 0.9f);
}

void UWWOAimAssistComponent::UpdateAdhesion()
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	const UWWOWeaponData* W = P->Weapon->GetWeaponData();
	const AWWOPlayerController* PC = P->GetWWOController();
	if (!W || !PC) return;
	const bool bActive = W->Adhesion > 0.f && (PC->GetLookAxis().Size() > 0.1f || PC->GetMoveAxis().Size() > 0.1f) && !P->IsLunging();
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
		const float Pull = W->Adhesion * DeviceScale();
		P->ApplyRecoil(PitchChange * Pull, -YawChange * Pull); // ApplyRecoil's yaw is + to the left
	}
	AdhesionKey = Key;
	AdhesionLastDir = Dir;
}
