#pragma once

// The standard character movement, with hooks so the character's own movement rules
// (ported from the Godot prototype) decide speed and gravity:
// - TickComponent: runs AWWOCharacter::PrepareMovement first, so this frame's input moves you
//   this frame (Godot sets velocity, then move_and_slide, in one step).
// - CalcVelocity: horizontal velocity comes from AWWOCharacter::WishVelocity (and the full
//   3D velocity while lunging), instead of Unreal's acceleration/friction model.
// - GetGravityZ: separate ascent and descent gravity, eased near the jump's apex.
// - NewFallVelocity: no terminal velocity (Godot has none).
// - ComputeGroundMovementDelta: up a slope you slide along it like Godot's move_and_slide
//   (slower), down a slope you keep your horizontal speed.
// - Crouch / UnCrouch: the capsule shrinks and grows from the feet, in the air too (Godot).
// Floors, steps and collision are Unreal's.
// - Online (predicted, host-authoritative): every move runs AWWOCharacter::SimulateMove from the
//   character's MoveIn (UpdateCharacterStateBeforeMovement). Your machine saves each move's input
//   (FWWOSavedMove) and sends it with the move (FWWOMoveData); the host runs the same move from it
//   (MoveAutonomous) and checks where you ended up. When it disagrees, its correction carries the
//   movement rules' state (FWWOMoveResponse) and your machine replays its unacknowledged moves from there.

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"
#include "Player/WWOCharacter.h"
#include "WWOCharacterMovement.generated.h"

// One move as your machine saved it: the input, kept for replays after a correction.
class FWWOSavedMove : public FSavedMove_Character
{
public:
	using Super = FSavedMove_Character;
	FWWOMoveInput Input;
	virtual void Clear() override;
	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, class FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* C) override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
};

class FWWOPredictionData : public FNetworkPredictionData_Client_Character
{
public:
	FWWOPredictionData(const UCharacterMovementComponent& Movement) : FNetworkPredictionData_Client_Character(Movement) {}
	virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FWWOSavedMove()); }
};

// A move as sent to the host: the standard move plus the input.
struct FWWOMoveData : public FCharacterNetworkMoveData
{
	FWWOMoveInput Input;
	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType) override;
};

struct FWWOMoveDataContainer : public FCharacterNetworkMoveDataContainer
{
	FWWOMoveData Moves[3];
	FWWOMoveDataContainer()
	{
		NewMoveData = &Moves[0];
		PendingMoveData = &Moves[1];
		OldMoveData = &Moves[2];
	}
};

// The host's answer: on a correction, also the movement rules' state.
struct FWWOMoveResponse : public FCharacterMoveResponseDataContainer
{
	FWWOMoveState State;
	virtual void ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap) override;
};

UCLASS()
class WHATWASOURS_API UWWOCharacterMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UWWOCharacterMovement();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual float GetGravityZ() const override;
	virtual FVector NewFallVelocity(const FVector& InitialVelocity, const FVector& Gravity, float DeltaTime) const override;
	virtual FVector ComputeGroundMovementDelta(const FVector& Delta, const FHitResult& RampHit, const bool bHitFromLineTrace) const override;
	virtual bool CanCrouchInCurrentState() const override;
	virtual void Crouch(bool bClientSimulation = false) override;
	virtual void UnCrouch(bool bClientSimulation = false) override;

	// Online movement.
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel) override;
	virtual void ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse) override;

private:
	FWWOMoveDataContainer MoveDataContainer;
	FWWOMoveResponse MoveResponse;
};
