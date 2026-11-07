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
	// Substeps no longer than Godot's 60 Hz physics step, so at low frame rates gravity still
	// switches between ascent, apex hang and descent as often as it does there (a 30 fps frame
	// that crossed the apex-hang speed would otherwise rise with full gravity, cutting cm off a jump).
	MaxSimulationTimeStep = 1.f / 60.f;
	// Online: moves carry the input, corrections carry the movement rules' state.
	SetNetworkMoveDataContainer(MoveDataContainer);
	SetMoveResponseDataContainer(MoveResponse);
}

// --- Online movement ----------------------------------------------------------------------------

void FWWOSavedMove::Clear()
{
	Super::Clear();
	Input = FWWOMoveInput();
}

void FWWOSavedMove::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);
	if (const AWWOCharacter* W = Cast<AWWOCharacter>(C)) Input = W->MoveIn;
}

void FWWOSavedMove::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);
	if (AWWOCharacter* W = Cast<AWWOCharacter>(C)) W->MoveIn = Input; // a replay runs on the input it had
}

bool FWWOSavedMove::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	// Never merge a move with a button press in it (the press would happen once, not twice), or
	// two moves with different input.
	const FWWOSavedMove* Next = static_cast<const FWWOSavedMove*>(NewMove.Get());
	if (Input.Pressed != 0 || Next->Input.Pressed != 0 || !(Input == Next->Input)) return false;
	return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

FNetworkPredictionData_Client* UWWOCharacterMovement::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UWWOCharacterMovement* Self = const_cast<UWWOCharacterMovement*>(this);
		Self->ClientPredictionData = new FWWOPredictionData(*this);
	}
	return ClientPredictionData;
}

void FWWOMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType)
{
	FCharacterNetworkMoveData::ClientFillNetworkMoveData(ClientMove, MoveType);
	Input = static_cast<const FWWOSavedMove&>(ClientMove).Input;
}

bool FWWOMoveData::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
	FCharacterNetworkMoveData::Serialize(CharacterMovement, Ar, PackageMap, MoveType);
	Input.Serialize(Ar);
	return !Ar.IsError();
}

void FWWOMoveResponse::ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment)
{
	FCharacterMoveResponseDataContainer::ServerFillResponseData(CharacterMovement, PendingAdjustment);
	if (const AWWOCharacter* W = Cast<AWWOCharacter>(CharacterMovement.GetCharacterOwner())) State = W->GetMoveState();
}

bool FWWOMoveResponse::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap)
{
	if (!FCharacterMoveResponseDataContainer::Serialize(CharacterMovement, Ar, PackageMap)) return false;
	if (IsCorrection()) State.Serialize(Ar);
	return !Ar.IsError();
}

void UWWOCharacterMovement::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	// The movement rules for this move, wherever it runs (see the header).
	// Not for other players' characters on your machine: they follow the host's replicated position.
	AWWOCharacter* Owner = Cast<AWWOCharacter>(CharacterOwner);
	if (Owner && Owner->GetLocalRole() != ROLE_SimulatedProxy) Owner->SimulateMove(DeltaSeconds);
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
}

void UWWOCharacterMovement::MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel)
{
	// The host, running a move that arrived from its player: their input first.
	AWWOCharacter* Owner = Cast<AWWOCharacter>(CharacterOwner);
	if (Owner && GetCurrentNetworkMoveData())
	{
		Owner->MoveIn = static_cast<const FWWOMoveData*>(GetCurrentNetworkMoveData())->Input;
		// A move it can't simulate yet (lunge, Spore Dash): take the player's position for it.
		const bool bClientAuth = (Owner->MoveIn.Held & MoveClientAuth) != 0;
		bIgnoreClientMovementErrorChecksAndCorrection = bClientAuth;
		bServerAcceptClientAuthoritativePosition = bClientAuth;
	}
	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
}

void UWWOCharacterMovement::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& Response)
{
	// A correction: put the movement rules back where the host had them before replaying.
	if (Response.IsCorrection())
	{
		if (AWWOCharacter* Owner = Cast<AWWOCharacter>(CharacterOwner)) Owner->SetMoveState(static_cast<const FWWOMoveResponse&>(Response).State);
	}
	Super::ClientHandleMoveResponse(Response);
}

void UWWOCharacterMovement::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// On the machine that controls this character: this frame's local work and move input first.
	// (The host's copy of someone else moves only as their moves arrive.)
	AWWOCharacter* Owner = Cast<AWWOCharacter>(CharacterOwner);
	if (Owner && Owner->IsLocallyControlled()) Owner->PrepareMovement(DeltaTime);
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

FVector UWWOCharacterMovement::NewFallVelocity(const FVector& InitialVelocity, const FVector& Gravity, float DeltaTime) const
{
	// Godot's player just adds gravity each step; there's no terminal-velocity cap.
	return DeltaTime > 0.f ? InitialVelocity + Gravity * DeltaTime : InitialVelocity;
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
