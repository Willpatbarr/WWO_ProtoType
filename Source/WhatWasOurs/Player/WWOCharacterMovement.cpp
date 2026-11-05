#include "Player/WWOCharacterMovement.h"
#include "Player/WWOCharacter.h"

UWWOCharacterMovement::UWWOCharacterMovement()
{
	// The character sets velocity directly, so Unreal's own acceleration model is neutral.
	MaxAcceleration = 100000.f;
	BrakingDecelerationWalking = 100000.f;
	GroundFriction = 8.f;
	AirControl = 1.f;
	MaxWalkSpeed = 5000.f;
	MaxWalkSpeedCrouched = 5000.f;
	MaxFlySpeed = 10000.f;
	JumpZVelocity = 600.f;
	bCanWalkOffLedgesWhenCrouching = true;
	NavAgentProps.bCanCrouch = true;
	SetCrouchedHalfHeight(60.f); // 1.2 m tall when crouched (2 m standing)
	bOrientRotationToMovement = false;
	MaxStepHeight = 30.f;
	SetWalkableFloorAngle(46.f);
	bUseFlatBaseForFloorChecks = true;
}

void UWWOCharacterMovement::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	const AWWOCharacter* Owner = Cast<AWWOCharacter>(CharacterOwner);
	if (!Owner || HasAnimRootMotion())
	{
		Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
		return;
	}
	if (Owner->IsLunging() || Owner->IsMantling())
	{
		Velocity = Owner->LungeVelocity; // flying: full 3D, no gravity
		return;
	}
	Velocity.X = Owner->WishVelocity.X;
	Velocity.Y = Owner->WishVelocity.Y;
}

float UWWOCharacterMovement::GetGravityZ() const
{
	const AWWOCharacter* Owner = Cast<AWWOCharacter>(CharacterOwner);
	return Owner ? Owner->CurrentGravityZ() : Super::GetGravityZ();
}
