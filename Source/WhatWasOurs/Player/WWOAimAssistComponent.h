#pragma once

// Controller aim assist, modeled on Destiny 2 (Godot: aim_assist.gd).
// - Bullet magnetism: a shot that just misses gets bent onto a target inside a small cone,
//   onto the head only within a much smaller cone.
// - Friction: stick look speed drops while the reticle is near a target.
// - Adhesion (off by default): your aim gets dragged along with a moving target.
// Scales with the weapon's Aim Assistance stat, fades past the assist range (stretched by
// zoom while aimed), and gets stronger while aimed. Mouse gets MouseAssist worth (none).

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WWOAimAssistComponent.generated.h"

struct FHitResult;

struct FWWOAssistPart
{
	TWeakObjectPtr<AActor> Target;
	FVector Position = FVector::ZeroVector;
	bool bCritical = false;
	float Angle = 0.f; // radians
	float Cone = 0.f;  // radians
	bool IsValid() const { return Target.IsValid(); }
};

UCLASS(ClassGroup = (WWO))
class WHATWASOURS_API UWWOAimAssistComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWWOAimAssistComponent();

	UPROPERTY(EditAnywhere, Category = "Aim assist") float MouseAssist = 0.f;
	UPROPERTY(EditAnywhere, Category = "Aim assist") float ShowDebug = 0.f;
	// Hidden assist: help that only works while a shot's recoil plays out (the weapons' RecoverySteer,
	// FollowupMagnetism, KickFriction, KickAdhesion). On/off, and markers showing when it acted.
	UPROPERTY(EditAnywhere, Category = "Aim assist") float HiddenAssist = 1.f;
	UPROPERTY(EditAnywhere, Category = "Aim assist") float ShowHiddenAssist = 0.f;

	bool UsingController() const;
	float DeviceScale() const;
	float ConeScale() const;
	float EffectiveRange() const; // meters
	FWWOAssistPart FindPart(float BaseAngleDeg, float CritScale = 1.f) const;
	bool Magnetize(FHitResult& OutHit) const;
	float FrictionScale(); // multiplier for stick look speed
	void UpdateAdhesion();
	// Hidden assist: the after-shot window (1 while the recoil plays, fading out after), the recovery
	// steer (called by the recoil as it falls back by StepRad this frame), and when each part last acted.
	void UpdateHiddenWindow(float DeltaSeconds);
	float HiddenWindow() const { return Window; }
	// Called for every bit of recoil motion: the kick (instant or timed; sideways steer only) and the
	// fall-back (both ways).
	void SteerRecoil(float StepRad, bool bKick);
	void NotifyShot(); // a shot's recoil starts: open the window now
	double LastSteer = -10.0, LastMagnet = -10.0, LastSticky = -10.0; // world seconds

	FWWOAssistPart FrictionPart; // for the debug view

private:
	FString AdhesionKey;
	float Window = 0.f;
	double LastShot = -10.0; // world seconds; the window stays open at least MinWindow after a shot
	static constexpr float MinWindow = 0.15f;
	FVector AdhesionLastDir = FVector::ZeroVector;
	float RangeFalloff(float DistanceM) const;
};
