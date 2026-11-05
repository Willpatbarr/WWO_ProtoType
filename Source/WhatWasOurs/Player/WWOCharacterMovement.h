#pragma once

// The standard character movement, with hooks so the character's own movement rules
// (ported from the Godot prototype) decide speed and gravity:
// - TickComponent: runs AWWOCharacter::PrepareMovement first, so this frame's input moves you
//   this frame (Godot sets velocity, then move_and_slide, in one step).
// - CalcVelocity: horizontal velocity comes from AWWOCharacter::WishVelocity (and the full
//   3D velocity while lunging), instead of Unreal's acceleration/friction model.
// - GetGravityZ: separate ascent and descent gravity, eased near the jump's apex.
// - ComputeGroundMovementDelta: up a slope you slide along it like Godot's move_and_slide
//   (slower), down a slope you keep your horizontal speed.
// - Crouch / UnCrouch: the capsule shrinks and grows from the feet, in the air too (Godot).
// Floors, steps and collision are Unreal's.

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "WWOCharacterMovement.generated.h"

UCLASS()
class WHATWASOURS_API UWWOCharacterMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UWWOCharacterMovement();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual float GetGravityZ() const override;
	virtual FVector ComputeGroundMovementDelta(const FVector& Delta, const FHitResult& RampHit, const bool bHitFromLineTrace) const override;
	virtual bool CanCrouchInCurrentState() const override;
	virtual void Crouch(bool bClientSimulation = false) override;
	virtual void UnCrouch(bool bClientSimulation = false) override;
};
