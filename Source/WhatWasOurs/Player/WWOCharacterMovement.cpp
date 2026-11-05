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
	// Godot's CharacterBody3D has no step-up: its round capsule (radius 0.5) only rides over
	// edges up to r * (1 - cos 45°) ≈ 15 cm, and the 25 cm stairs need their ramp collision.
	MaxStepHeight = 15.f;
	// Godot's floor_max_angle (45°) plus its 0.01 rad floor threshold.
	SetWalkableFloorAngle(45.57f);
	bUseFlatBaseForFloorChecks = true;
}

void UWWOCharacterMovement::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (AWWOCharacter* Owner = Cast<AWWOCharacter>(CharacterOwner))
	{
		Owner->PrepareMovement(DeltaTime);
	}
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
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

FVector UWWOCharacterMovement::ComputeGroundMovementDelta(const FVector& Delta, const FHitResult& RampHit, const bool bHitFromLineTrace) const
{
	// Unreal keeps the horizontal speed on a ramp both ways. Godot's move_and_slide (without
	// floor_constant_speed) does that going down, but going up the motion slides along the
	// slope, so you climb at cos²(angle) of your horizontal speed.
	const FVector RampMovement = Super::ComputeGroundMovementDelta(Delta, RampHit, bHitFromLineTrace);
	const FVector& FloorNormal = RampHit.ImpactNormal;
	if (RampMovement.Z > 0.f && (FloorNormal | Delta) < 0.f)
	{
		return Delta - (Delta | FloorNormal) * FloorNormal;
	}
	return RampMovement;
}

bool UWWOCharacterMovement::CanCrouchInCurrentState() const
{
	// The melee lunge flies; stay crouched through it like Godot does.
	const AWWOCharacter* Owner = Cast<AWWOCharacter>(CharacterOwner);
	if (Owner && Owner->IsLunging() && IsFlying() && CanEverCrouch()) return true;
	return Super::CanCrouchInCurrentState();
}

void UWWOCharacterMovement::Crouch(bool bClientSimulation)
{
	// Godot shrinks the capsule toward the feet even in the air (Unreal shrinks it toward the
	// center there, tucking the feet up and popping the eye).
	const bool bWas = bCrouchMaintainsBaseLocation;
	bCrouchMaintainsBaseLocation = true;
	Super::Crouch(bClientSimulation);
	bCrouchMaintainsBaseLocation = bWas;
}

void UWWOCharacterMovement::UnCrouch(bool bClientSimulation)
{
	// Grows back up from the feet, only if there's room overhead (Godot's _can_stand).
	const bool bWas = bCrouchMaintainsBaseLocation;
	bCrouchMaintainsBaseLocation = true;
	Super::UnCrouch(bClientSimulation);
	bCrouchMaintainsBaseLocation = bWas;
}
