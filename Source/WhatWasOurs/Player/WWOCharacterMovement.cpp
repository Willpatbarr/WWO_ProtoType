#include "Player/WWOCharacterMovement.h"
#include "Player/WWOProf.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOAbilityComponent.h"
#include "Systems/WWOWorldSubsystem.h"
#include "WWOTypes.h"
#include "Player/WWOWeaponComponent.h"
#include "WhatWasOurs.h"

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

void FWWOSavedMove::SetInitialPosition(ACharacter* C)
{
	Super::SetInitialPosition(C);
	if (const AWWOCharacter* W = Cast<AWWOCharacter>(C)) StartState = W->GetMoveState();
}

void FWWOSavedMove::CombineWith(const FSavedMove_Character* OldMove, ACharacter* C, APlayerController* PC, const FVector& OldStartLocation)
{
	Super::CombineWith(OldMove, C, PC, OldStartLocation);
	if (AWWOCharacter* W = Cast<AWWOCharacter>(C)) W->SetMoveState(static_cast<const FWWOSavedMove*>(OldMove)->StartState);
}

void FWWOSavedMove::PostUpdate(ACharacter* C, EPostUpdateMode PostUpdateMode)
{
	Super::PostUpdate(C, PostUpdateMode);
	if (const AWWOCharacter* W = Cast<AWWOCharacter>(C)) EndState = W->GetMoveState();
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
		// A lunge or a launch comes with the velocity the player's machine gave it: no faster than
		// a lunge or a dash can go (a tampered move would just be slow to arrive, not teleport).
		if (UWWOWorldSubsystem::IsPvP(Owner))
		{
			// PvP: no faster than this player's loadout allows (aiming and slows only lower it).
			Owner->MoveIn.SpeedScale = FMath::Min(Owner->MoveIn.SpeedScale, Owner->Abilities->SpeedMultiplier() + 0.01f);
			Owner->MoveIn.ExtraJumps = (uint8)FMath::Min<int32>(Owner->MoveIn.ExtraJumps, Owner->Abilities->ExtraJumps());
		}
		// PvP: a lunge follows a melee swing the host heard of (with a little credit, since the
		// swing and the first lunging moves arrive on different channels); a launch is Spore Dash's,
		// once per use; and a player the host has frozen (its ult, Entrench) is frozen whatever its moves say.
		if (UWWOWorldSubsystem::IsPvP(Owner))
		{
			const double Now = GetWorld()->GetTimeSeconds();
			if (Owner->MoveIn.Held & MoveLunge)
			{
				const bool bSwung = Now - Owner->LastSwingTime < 1.5;
				Owner->LungeOnCredit = bSwung ? 0.f : Owner->LungeOnCredit + DeltaTime;
				if (!bSwung && Owner->LungeOnCredit > 0.4f) Owner->MoveIn.Held &= ~MoveLunge;
			}
			if (Owner->MoveIn.Pressed & MoveLaunch)
			{
				if (Now - Owner->LastLaunchTime < 1.0 || !Owner->Abilities->CanLaunchNow()) Owner->MoveIn.Pressed &= ~MoveLaunch;
				else Owner->LastLaunchTime = Now;
			}
			if (Owner->bUlting || Owner->EntrenchLeft > 0.f) Owner->MoveIn.Held |= MoveFrozen;
		}
		if (Owner->MoveIn.HasPush())
		{
			const float Cap = (Owner->MoveIn.Held & MoveLunge) ? Owner->Weapon->LungeSpeed * WWO::M * 1.05f : 30.f * WWO::M;
			Owner->MoveIn.Push = Owner->MoveIn.Push.GetClampedToMaxSize(Cap);
		}
	}
	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
}

int32 UWWOCharacterMovement::NetCorrections = 0;
float UWWOCharacterMovement::NetCorrectionErrorMax = 0.f;
float UWWOCharacterMovement::NetCorrectionErrorSum = 0.f;
bool UWWOCharacterMovement::bLogNetCorrections = false;

void UWWOCharacterMovement::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& Response)
{
	// A correction: put the movement rules back where the host had them before replaying.
	if (Response.IsCorrection())
	{
		++NetCorrections;
		// Compare with where this machine was after the same move (not now: newer moves have run since).
		const FClientAdjustment& Adjust = Response.ClientAdjustment;
		const FNetworkPredictionData_Client_Character* Data = GetPredictionData_Client_Character();
		const FWWOSavedMove* Same = nullptr;
		for (const FSavedMovePtr& M : Data->SavedMoves)
		{
			if (M->TimeStamp == Adjust.TimeStamp) { Same = static_cast<const FWWOSavedMove*>(M.Get()); break; }
		}
		if (Same && !Adjust.bBaseRelativePosition)
		{
			const float Error = FVector::Dist(Adjust.NewLoc, Same->SavedLocation);
			NetCorrectionErrorMax = FMath::Max(NetCorrectionErrorMax, Error);
			NetCorrectionErrorSum += Error;
		}
		if (bLogNetCorrections)
		{
			const FWWOMoveState& Host = static_cast<const FWWOMoveResponse&>(Response).State;
			if (Same)
			{
				UE_LOG(LogWWO, Display, TEXT("NetCorrection t=%.3f off %s cm | host vel %s mode %d ground %d | mine vel %s | input move %s held %d pressed %d scale %.2f | moves pending %d"),
					Adjust.TimeStamp, *(Adjust.NewLoc - Same->SavedLocation).ToCompactString(), *Adjust.NewVel.ToCompactString(), Adjust.MovementMode, Adjust.bAckGoodMove,
					*Same->SavedVelocity.ToCompactString(), *Same->Input.Move.ToString(), Same->Input.Held, Same->Input.Pressed, Same->Input.SpeedScale, Data->SavedMoves.Num());
				UE_LOG(LogWWO, Display, TEXT("NetCorrection   host: %s"), *Host.ToString());
				UE_LOG(LogWWO, Display, TEXT("NetCorrection   mine: %s"), *Same->EndState.ToString());
			}
			else UE_LOG(LogWWO, Display, TEXT("NetCorrection t=%.3f: no saved move with that time (%d pending)"), Adjust.TimeStamp, Data->SavedMoves.Num());
		}
		if (AWWOCharacter* Owner = Cast<AWWOCharacter>(CharacterOwner)) Owner->SetMoveState(static_cast<const FWWOMoveResponse&>(Response).State);
	}
	Super::ClientHandleMoveResponse(Response);
}

bool UWWOCharacterMovement::ForcePositionUpdate(float DeltaTime)
{
	UE_LOG(LogWWO, Display, TEXT("NetForcedUpdate: %s, no moves for %.2f s"), *GetNameSafe(CharacterOwner), DeltaTime);
	return Super::ForcePositionUpdate(DeltaTime);
}

void UWWOCharacterMovement::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	WWO_PROF("CharacterMovement");
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
	if (Owner->IsInLungeFlight() || Owner->IsMantling())
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
	if (Owner && (Owner->IsLunging() || Owner->IsInLungeFlight()) && IsFlying() && CanEverCrouch()) return true;
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
