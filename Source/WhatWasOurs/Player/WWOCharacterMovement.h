#pragma once

// The standard character movement, with two hooks so the character's own movement rules
// (ported from the Godot prototype) decide speed and gravity:
// - CalcVelocity: horizontal velocity comes from AWWOCharacter::WishVelocity (and the full
//   3D velocity while lunging), instead of Unreal's acceleration/friction model.
// - GetGravityZ: separate ascent and descent gravity, eased near the jump's apex.
// Floors, slopes, steps, crouching and collision are all Unreal's.

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "WWOCharacterMovement.generated.h"

UCLASS()
class WHATWASOURS_API UWWOCharacterMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UWWOCharacterMovement();

	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual float GetGravityZ() const override;
};
