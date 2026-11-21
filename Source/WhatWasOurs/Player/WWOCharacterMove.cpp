// AWWOCharacter: movement. PrepareMovement (the local part of a frame before the move: health,
// recoil, looking, then CaptureMoveInput), and the movement rules every move runs (SimulateMove and
// what it calls: sprint, crouch and slide, air and jumps, horizontal speed, landing, lunge flight,
// launch, mantle), the move state and input on the wire, and the move stats (MoveStatTable, M()).
// SimulateMove reads only MoveIn and MoveState (AGENTS.md rule 5).

#include "Player/WWOCharacter.h"
#include "WhatWasOurs.h"
#include "Player/WWOCharacterPrivate.h"
#include "WWOTypes.h"
#include "Engine/World.h"
#include "Systems/WWOSound.h"
#include "Player/WWOBotController.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Data/WWOData.h"
#include "Player/WWOCharacterMovement.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOAimAssistComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOAbilityComponent.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "Components/CapsuleComponent.h"

using namespace WWOCharacter; // the constants these files share (WWOCharacterPrivate.h)

namespace
{
	constexpr int32 MaxJumps = 2;             // before abilities (Overgrowth adds more)
	constexpr float ApexSpeed = 2.f;          // apex hang applies while vertical speed is below this (m/s)
	constexpr float LandingDipPerSpeed = 0.012f;
	constexpr float LandingDipMax = 0.25f;
	constexpr float LandSlideWindow = 0.2f;   // seconds after landing that crouch still turns into a slide
	constexpr float LungeStopDistance = 1.f;  // meters (center to center)
	constexpr float LungeMinTime = 0.15f;
	constexpr float LungeMaxTime = 0.6f;
	constexpr float MantleWallCheck = 0.85f;
	constexpr float MantleStepIn = 0.6f;
	constexpr float MantleMinHeight = 0.3f;
}

namespace
{
	// The movement values a player's machine and the host exchange (ServerSetMoveStats,
	// ClientSetMoveBase, BaseMoveStats), in the order they go on the wire. Stat: the loadout stat
	// that modifies it (M()), or null if loadouts don't. Low/High: the host keeps a joiner's value
	// inside these (twice the Settings sliders' ranges: loadout bonuses go over them).
	// Adding a movement value is one line here (at the end: tests read some by index).
	struct FWWOMoveStat
	{
		float AWWOCharacter::* Value;
		const TCHAR* Stat;
		float Low, High;
	};
	const FWWOMoveStat MoveStatTable[] = {
		{ &AWWOCharacter::WalkSpeed,      TEXT("walk_speed"),     0.5f,  30.f },
		{ &AWWOCharacter::SprintSpeed,    TEXT("sprint_speed"),   0.5f,  50.f },
		{ &AWWOCharacter::CrouchSpeed,    TEXT("crouch_speed"),   0.25f, 20.f },
		{ &AWWOCharacter::SlideSpeed,     TEXT("slide_speed"),    0.5f,  60.f },
		{ &AWWOCharacter::SlideDistance,  TEXT("slide_distance"), 0.5f,  40.f },
		{ &AWWOCharacter::JumpHeight1,    TEXT("jump_height"),    0.1f,  10.f },
		{ &AWWOCharacter::JumpHeight2,    TEXT("jump_height"),    0.f,   10.f },
		{ &AWWOCharacter::AirControl,     TEXT("air_control"),    0.f,   80.f },
		{ &AWWOCharacter::AscentGravity,  nullptr,                1.f,   160.f },
		{ &AWWOCharacter::DescentGravity, nullptr,                1.f,   160.f },
		{ &AWWOCharacter::ApexHang,       nullptr,                0.f,   0.95f },
		{ &AWWOCharacter::CoyoteTime,     nullptr,                0.f,   0.6f },
		{ &AWWOCharacter::MantleReach,    nullptr,                0.f,   7.f },
		{ &AWWOCharacter::MantleTime,     nullptr,                0.05f, 2.f },
		{ &AWWOCharacter::GroundAccel,    nullptr,                5.f,   600.f },
		{ &AWWOCharacter::GroundDecel,    nullptr,                5.f,   600.f },
		{ &AWWOCharacter::SlideSteer,     nullptr,                0.f,   360.f },
	};
	constexpr int32 MoveStatCount = UE_ARRAY_COUNT(MoveStatTable);
}

float AWWOCharacter::M(FName Stat, float Base) const
{
	// The host's copy of a joiner in co-op: its movement values came already modified
	// (ServerSetMoveStats). Everything else (health, shields, grenades) still takes its loadout here.
	static const TSet<FName> Sent = []
	{
		TSet<FName> Names;
		for (const FWWOMoveStat& S : MoveStatTable) if (S.Stat) Names.Add(FName(S.Stat));
		return Names;
	}();
	if (bNetStats && Sent.Contains(Stat)) return Base;
	const UWWOLoadoutSubsystem* Loadout = GetLoadout();
	return Loadout ? Loadout->Apply(Stat, Base) : Base;
}

FVector2D AWWOCharacter::MoveInput() const
{
	if (const AWWOBotController* Bot = GetBot()) return Bot->GetMoveAxis();
	const AWWOPlayerController* PC = GetWWOController();
	return PC ? PC->GetMoveAxis() : FVector2D::ZeroVector;
}

FVector AWWOCharacter::WishDirection(const FVector2D& Input) const
{
	const FRotator Yaw(0, GetActorRotation().Yaw, 0);
	const FVector Dir = Yaw.RotateVector(FVector(Input.Y, Input.X, 0.f));
	return Dir.GetSafeNormal();
}

bool AWWOCharacter::IsOnFloor() const
{
	return GetCharacterMovement()->IsMovingOnGround();
}

void AWWOCharacter::PrepareMovement(float DeltaSeconds)
{
	// Called by the movement component right before it moves, so input from this frame moves
	// you this frame (Godot sets the velocity and calls move_and_slide in the same step).
	PreparedFrame = GFrameCounter;
	Health->UpdateHealth(DeltaSeconds); // same order as the Godot nodes: health, then movement
	if (Health->bDead)
	{
		WishVelocity = FVector::ZeroVector;
		MoveIn = FWWOMoveInput();
		return; // no moving
	}
	TickMovement(DeltaSeconds);
	// Online: keep the host's copy of your movement values and your flora current.
	if (GetNetMode() != NM_Standalone && (MoveStatsCheck -= DeltaSeconds) <= 0.f)
	{
		MoveStatsCheck = 0.5f;
		const TArray<float> Stats = LocalMoveStats();
		if (!HasAuthority() && Stats != SentMoveStats)
		{
			SentMoveStats = Stats;
			ServerSetMoveStats(Stats);
		}
		const UWWOLoadoutSubsystem* Loadout = GetLoadout();
		if (Loadout && !HasAuthority())
		{
			const FWWONetLoadout Net = Loadout->ToNet();
			if (Net != SentLoadout)
			{
				SentLoadout = Net;
				ServerSetLoadout(Net);
			}
		}
		// The host keeps everyone's look current for every machine (its own, the joiners', the bots').
		if (Loadout && HasAuthority())
		{
			const FWWONetLoadout Now = Loadout->ToNet();
			if (Now != NetLook)
			{
				NetLook = Now;
				OnRep_NetLook(); // the host shows it too
			}
		}
		const FName Flora = Loadout && Loadout->GetFlora() ? Loadout->GetFlora()->Id : NAME_None;
		if (Flora != NetFlora)
		{
			if (HasAuthority()) NetFlora = Flora; // the host's own: replicates from here
			else ServerSetFlora(Flora);
		}
	}
}

void AWWOCharacter::TickMovement(float DeltaSeconds)
{
	// The local part of a frame (only on the machine that controls you): status timers, recoil,
	// looking, then this frame's move input. The movement rules run in SimulateMove.
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const AWWOPlayerController* PC = GetWWOController();
	const FVector2D LookStick = PC ? PC->GetLookAxis() : FVector2D::ZeroVector;

	AimAssist->UpdateHiddenWindow(DeltaSeconds);
	UpdateRecoil(DeltaSeconds);
	TickStatusTimers(DeltaSeconds);

	if ((bUlting || EntrenchLeft > 0.f) && bLunging)
	{
		bLunging = false; // a lunge can't move while you're frozen or rooted; drop it so the punch lands
		LungeVelocity = FVector::ZeroVector;
	}
	// Steer the lunge from where you are now; the velocity goes into this move's input, so the host
	// flies the same lunge (it doesn't need the target, and can't drift from you on its position).
	if (bLunging) ApplyLunge(DeltaSeconds);
	(void)Move;
	// Frozen for the ult; rooted while entrenched you can still look (and shoot).
	if (!bUlting) Look(LookStick * StickSensitivity * DeltaSeconds * AimAssist->FrictionScale()); // friction: slower near targets
	CaptureMoveInput();
}

void AWWOCharacter::CaptureMoveInput()
{
	FWWOMoveInput In;
	In.Move = MoveInput();
	auto Bit = [&](EWWOAction Action, uint8 Flag)
	{
		if (Pressed(Action)) In.Held |= Flag;
		if (JustPressed(Action)) In.Pressed |= Flag;
	};
	Bit(EWWOAction::Jump, MoveJump);
	Bit(EWWOAction::Sprint, MoveSprint);
	Bit(EWWOAction::Crouch, MoveCrouch);
	Bit(EWWOAction::Fire, MoveFire);
	if (Weapon->IsAiming()) In.Held |= MoveAim;
	if (bUlting || EntrenchLeft > 0.f) In.Held |= MoveFrozen;
	if (bLunging)
	{
		In.Held |= MoveLunge;
		In.Push = LungeVelocity;
	}
	if (bLaunchPending)
	{
		In.Pressed |= MoveLaunch;
		In.Push = PendingLaunch;
		bLaunchPending = false;
	}
	In.SpeedScale = Abilities->SpeedMultiplier() * Weapon->MoveMultiplier() * SlowMultiplier();
	In.ExtraJumps = (uint8)FMath::Clamp(Abilities->ExtraJumps(), 0, 255);
	if (GetNetMode() == NM_Client) In.Quantize();
	MoveIn = In;
}

void AWWOCharacter::SimulateMove(float DeltaSeconds)
{
	// The movement rules for one move, from MoveIn only (see FWWOMoveInput).
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (Health->bDead)
	{
		WishVelocity = FVector::ZeroVector;
		MoveState.GroundVel = FVector2D::ZeroVector;
		return;
	}
	if (MoveIn.Pressed & MoveLaunch) ApplyLaunch(MoveIn.Push);
	if (MoveIn.Held & MoveFrozen)
	{
		if (MoveState.bLungeFlight) EndLungeFlight();
		MoveState.bSprinting = false;
		MoveState.bSliding = false;
		UpdateAir(DeltaSeconds);
		WishVelocity = FVector::ZeroVector;
		MoveState.GroundVel = FVector2D::ZeroVector;
		return;
	}
	const FVector2D Input = MoveIn.Move;
	const FVector WishDir = WishDirection(Input);

	if (MoveState.bMantling || TryMantle(Input))
	{
		if (MoveState.bLungeFlight) EndLungeFlight();
		UpdateMantle(DeltaSeconds);
		MoveState.GroundVel = FVector2D::ZeroVector; // you come over the top from a standstill
		return;
	}

	UpdateSprint(Input);
	UpdateCrouchAndSlide(WishDir, DeltaSeconds);
	UpdateAir(DeltaSeconds);
	// Godot's is_on_floor() is where the last move left you, so the frame you jump still uses
	// the ground rule for horizontal speed (full speed toward the stick, not air steering).
	const bool bOnFloor = IsOnFloor();
	HandleJump(WishDir);
	ApplyHorizontal(WishDir, bOnFloor, DeltaSeconds);
	if (MoveIn.Held & MoveLunge)
	{
		// Flying at the velocity the lunging machine sent (CalcVelocity uses LungeVelocity).
		if (!MoveState.bLungeFlight) MoveState.bSprinting = MoveState.bSliding = false; // a lunge ends a sprint or slide, on both machines' copies of the move
		MoveState.bLungeFlight = true;
		MoveState.GroundVel = FVector2D::ZeroVector; // a lunge ends at a dead stop
		LungeVelocity = MoveIn.Push;
		if (Move->MovementMode != MOVE_Flying) Move->SetMovementMode(MOVE_Flying);
	}
	else if (MoveState.bLungeFlight) EndLungeFlight();
	if (!IsOnFloor()) LastFallSpeed = -Move->Velocity.Z / WWO::M;
}

void FWWOMoveState::Serialize(FArchive& Ar)
{
	uint8 Bits = (bSprinting ? 1 : 0) | (bCrouching ? 2 : 0) | (bSliding ? 4 : 0) | (bCrouchToggled ? 8 : 0)
		| (bSlideQueued ? 16 : 0) | (bCrouchWasHeld ? 32 : 0) | (bLandedSprinting ? 64 : 0) | (bMantling ? 128 : 0);
	Ar << Bits;
	bSprinting = Bits & 1; bCrouching = Bits & 2; bSliding = Bits & 4; bCrouchToggled = Bits & 8;
	bSlideQueued = Bits & 16; bCrouchWasHeld = Bits & 32; bLandedSprinting = Bits & 64; bMantling = Bits & 128;
	uint8 More = bLungeFlight ? 1 : 0;
	Ar << More;
	bLungeFlight = More & 1;
	uint8 Jumps = (uint8)FMath::Clamp(JumpsUsed, 0, 255); // one byte on the wire
	Ar << Jumps << TakeoffSpeed << SlideSpeedNow << LaunchTime << AirTime << SinceLanded << MantleT << SlideLockout;
	JumpsUsed = Jumps;
	Ar << SlideDir << MantleFrom << MantleTo << GroundVel;
}

void FWWOMoveInput::Serialize(FArchive& Ar)
{
	// The stick to 1/127, the speed scale to 1/100.
	int8 X = (int8)FMath::RoundToInt(FMath::Clamp(Move.X, -1.f, 1.f) * 127.f);
	int8 Y = (int8)FMath::RoundToInt(FMath::Clamp(Move.Y, -1.f, 1.f) * 127.f);
	uint16 Scale = (uint16)FMath::Clamp(FMath::RoundToInt(SpeedScale * 100.f), 0, 65535);
	Ar << X << Y << Held << Pressed << Scale << ExtraJumps;
	Move = FVector2D(X / 127.f, Y / 127.f);
	SpeedScale = Scale / 100.f;
	if (HasPush())
	{
		// Whole cm/s, plenty for a lunge or a dash.
		int16 PX = (int16)FMath::Clamp(FMath::RoundToInt(Push.X), -32767, 32767);
		int16 PY = (int16)FMath::Clamp(FMath::RoundToInt(Push.Y), -32767, 32767);
		int16 PZ = (int16)FMath::Clamp(FMath::RoundToInt(Push.Z), -32767, 32767);
		Ar << PX << PY << PZ;
		Push = FVector(PX, PY, PZ);
	}
	else Push = FVector::ZeroVector;
}

void FWWOMoveInput::Quantize()
{
	Move = FVector2D(FMath::RoundToInt(FMath::Clamp(Move.X, -1.f, 1.f) * 127.f) / 127.f, FMath::RoundToInt(FMath::Clamp(Move.Y, -1.f, 1.f) * 127.f) / 127.f);
	SpeedScale = FMath::Clamp(FMath::RoundToInt(SpeedScale * 100.f), 0, 65535) / 100.f;
	Push = HasPush() ? FVector(FMath::Clamp(FMath::RoundToInt(Push.X), -32767, 32767), FMath::Clamp(FMath::RoundToInt(Push.Y), -32767, 32767),
		FMath::Clamp(FMath::RoundToInt(Push.Z), -32767, 32767)) : FVector::ZeroVector;
}

FString FWWOMoveState::ToString() const
{
	// Every field, in Serialize's order after the flags.
	return FString::Printf(TEXT("sprint %d crouch %d slide %d toggled %d queued %d held %d landsprint %d mantle %d lunge %d jumps %d takeoff %.2f slidespeed %.2f launch %.2f air %.2f landed %.2f mantlet %.2f lockout %.2f slidedir %s mantle from %s to %s groundvel %s"),
		bSprinting, bCrouching, bSliding, bCrouchToggled, bSlideQueued, bCrouchWasHeld, bLandedSprinting, bMantling, bLungeFlight, JumpsUsed,
		TakeoffSpeed, SlideSpeedNow, LaunchTime, AirTime, SinceLanded, MantleT, SlideLockout, *SlideDir.ToCompactString(), *MantleFrom.ToCompactString(), *MantleTo.ToCompactString(), *GroundVel.ToString());
}

TArray<float> AWWOCharacter::LocalMoveStats() const
{
	// MoveStatTable's order, after loadout modifiers.
	TArray<float> Values;
	Values.Reserve(MoveStatCount);
	for (const FWWOMoveStat& S : MoveStatTable) Values.Add(S.Stat ? M(S.Stat, this->*S.Value) : this->*S.Value);
	return Values;
}

TArray<float> AWWOCharacter::BaseMoveStats() const
{
	TArray<float> Values;
	Values.Reserve(MoveStatCount);
	for (const FWWOMoveStat& S : MoveStatTable) Values.Add(this->*S.Value);
	return Values;
}

void AWWOCharacter::ApplyMoveBase(const TArray<float>& V)
{
	if (V.Num() < MoveStatCount) return;
	for (int32 i = 0; i < MoveStatCount; ++i) this->*MoveStatTable[i].Value = V[i];
}

void AWWOCharacter::ClientSetMoveBase_Implementation(const TArray<float>& V)
{
	ApplyMoveBase(V); // M() then adds this player's own loadout, as the host's copy does
}

void AWWOCharacter::ServerSetMoveStats_Implementation(const TArray<float>& V)
{
	if (UWWOWorldSubsystem::IsPvP(this)) return; // PvP: the host's settings and the player's loadout, not their word
	if (V.Num() < MoveStatCount) return;
	// As sent, but sane: finite and inside the table's Low/High (twice the Settings sliders' ranges).
	TArray<float> Safe;
	for (int32 i = 0; i < MoveStatCount; ++i)
	{
		const FWWOMoveStat& S = MoveStatTable[i];
		Safe.Add(FMath::IsFinite(V[i]) ? FMath::Clamp(V[i], S.Low, S.High) : S.Low);
	}
	ApplyMoveBase(Safe);
	bNetStats = true; // M() now returns these as they are
}

void AWWOCharacter::UpdateSprint(const FVector2D& Input)
{
	// Press once to sprint; it stays on until you stop moving or crouch. Aiming and firing
	// stop a sprint and block starting one. Sprinting from a crouch stands you up, and sprinting
	// in a slide breaks out of it (while the stick is pushed: there is nowhere to sprint otherwise).
	const bool bAiming = MoveIn.Held & MoveAim, bFiring = MoveIn.Held & MoveFire;
	if ((MoveIn.Pressed & MoveSprint) && !bAiming && !bFiring && (!MoveState.bSliding || Input.Size() >= 0.1f))
	{
		if (MoveState.bSliding) BreakSlide();
		MoveState.bSprinting = true;
		MoveState.bCrouchToggled = false;
	}
	if (Input.Size() < 0.1f || bAiming || bFiring)
	{
		MoveState.bSprinting = false;
	}
}

bool AWWOCharacter::HasSprintMomentum() const
{
	return MoveState.bSprinting || MoveState.TakeoffSpeed > M("walk_speed", WalkSpeed) * 1.05f;
}

void AWWOCharacter::UpdateCrouchAndSlide(const FVector& WishDir, float DeltaSeconds)
{
	const bool bCrouchHeld = MoveIn.Held & MoveCrouch;
	const bool bCrouchStarted = bCrouchHeld && !MoveState.bCrouchWasHeld;
	MoveState.bCrouchWasHeld = bCrouchHeld;

	// Tapping crouch while sprinting on the ground starts a slide. From a sprint jump,
	// crouch in the air queues a slide for landing, and crouch just after landing slides.
	// Tapping crouch in a slide ends it there, crouched. After breaking out of a slide early, the
	// next ground slide waits until that one would have ended (SlideLockout), so slide, break out,
	// slide again is no faster than sliding; crouch just crouches until then.
	MoveState.SinceLanded += DeltaSeconds;
	MoveState.SlideLockout = FMath::Max(MoveState.SlideLockout - DeltaSeconds, 0.f);
	if (bCrouchStarted)
	{
		const bool bCanSlide = M("slide_speed", SlideSpeed) > M("crouch_speed", CrouchSpeed);
		const bool bJustLanded = MoveState.SinceLanded <= LandSlideWindow && MoveState.bLandedSprinting;
		if (bCanSlide && MoveState.SlideLockout <= 0.f && IsOnFloor() && (MoveState.bSprinting || bJustLanded) && !MoveState.bSliding)
		{
			StartSlide(WishDir);
		}
		else if (bCanSlide && !IsOnFloor() && HasSprintMomentum())
		{
			MoveState.bSlideQueued = !MoveState.bSlideQueued; // tap again in the air to cancel
		}
		else if (MoveState.bSliding)
		{
			BreakSlide();
			MoveState.bCrouchToggled = true;
		}
		else
		{
			MoveState.bCrouchToggled = !MoveState.bCrouchToggled;
		}
	}
	if (MoveState.bSlideQueued && !IsOnFloor()) return; // stay standing in the air; the slide starts on landing

	if (MoveState.bSliding || MoveState.bCrouchToggled)
	{
		MoveState.bCrouching = true;
		MoveState.bSprinting = false;
	}
	else
	{
		MoveState.bCrouching = false; // Unreal keeps you crouched under a low ceiling until you can stand
	}
	if (MoveState.bCrouching && !bIsCrouched) Crouch();
	else if (!MoveState.bCrouching && bIsCrouched) UnCrouch();
}

void AWWOCharacter::StartSlide(const FVector& WishDir)
{
	MoveState.bSliding = true;
	MoveState.bSlideQueued = false;
	// A slide goes the way you are travelling (your real velocity: after a jump that is your flight
	// path, along a wall it is the wall), not the way the stick points at that instant. Only from
	// a near standstill does the stick, then your facing, pick it. The stick steers it after (SlideSteer).
	const FVector Moving(GetVelocity().X, GetVelocity().Y, 0.f);
	MoveState.SlideDir = Moving.Size() > 1.f * WWO::M ? Moving.GetSafeNormal() : (!WishDir.IsNearlyZero() ? WishDir : GetActorForwardVector());
	MoveState.SlideSpeedNow = M("slide_speed", SlideSpeed);
	MoveState.bCrouching = true;
	MoveState.bSprinting = false;
}

float AWWOCharacter::SlideDecel() const
{
	const float Start = M("slide_speed", SlideSpeed);
	const float End = M("crouch_speed", CrouchSpeed);
	return FMath::Max((Start * Start - End * End) / (2.f * M("slide_distance", SlideDistance)), 0.01f);
}

void AWWOCharacter::BreakSlide()
{
	MoveState.SlideLockout = FMath::Max(MoveState.SlideSpeedNow - M("crouch_speed", CrouchSpeed), 0.f) / SlideDecel();
	MoveState.bSliding = false;
}

void AWWOCharacter::Launch(const FVector& NewVelocityCm)
{
	// Goes out with the next move (MoveLaunch), so the host launches you in the same move.
	PendingLaunch = NewVelocityCm;
	bLaunchPending = true;
}

void AWWOCharacter::ApplyLaunch(const FVector& NewVelocityCm)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->Velocity = NewVelocityCm;
	Move->SetMovementMode(MOVE_Falling);
	MoveState.TakeoffSpeed = FVector2D(NewVelocityCm.X, NewVelocityCm.Y).Size() / WWO::M;
	MoveState.LaunchTime = 0.15f;
	MoveState.bSliding = false;
	MoveState.bSprinting = false;
}

void AWWOCharacter::EndLungeFlight()
{
	// The lunge is over: stop dead and let gravity take over (this move's stick too, like Godot's velocity = ZERO).
	MoveState.bLungeFlight = false;
	LungeVelocity = FVector::ZeroVector;
	GetCharacterMovement()->Velocity = FVector::ZeroVector;
	WishVelocity = FVector::ZeroVector;
	GetCharacterMovement()->SetMovementMode(MOVE_Falling);
}

float AWWOCharacter::CurrentGravityZ() const
{
	const FVector V = GetCharacterMovement()->Velocity;
	float G = V.Z > 0.f ? AscentGravity : DescentGravity;
	if (MoveState.JumpsUsed > 0 && FMath::Abs(V.Z) < ApexSpeed * WWO::M)
	{
		G *= 1.f - ApexHangClamped(); // float a moment at the top of the jump
	}
	return -G * WWO::M;
}

void AWWOCharacter::UpdateAir(float DeltaSeconds)
{
	MoveState.LaunchTime = FMath::Max(MoveState.LaunchTime - DeltaSeconds, 0.f);
	if (IsOnFloor() && MoveState.LaunchTime <= 0.f)
	{
		MoveState.JumpsUsed = 0;
		MoveState.TakeoffSpeed = 0.f;
		MoveState.AirTime = 0.f;
		return;
	}
	MoveState.AirTime += DeltaSeconds;
	if (MoveState.JumpsUsed == 0 && MoveState.AirTime > CoyoteTime)
	{
		MoveState.JumpsUsed = 1; // walked off a ledge and missed the coyote window: only the air jump is left
	}
}

float AWWOCharacter::LaunchSpeed(float Height) const
{
	// Upward speed that reaches exactly Height, accounting for the lighter gravity near the top.
	const float HangGravity = AscentGravity * (1.f - ApexHangClamped());
	const float HangSpeed = FMath::Min(ApexSpeed, FMath::Sqrt(2.f * HangGravity * Height));
	const float HangHeight = HangSpeed * HangSpeed / (2.f * HangGravity);
	return FMath::Sqrt(HangSpeed * HangSpeed + 2.f * AscentGravity * FMath::Max(Height - HangHeight, 0.f));
}

void AWWOCharacter::HandleJump(const FVector& WishDir)
{
	if (!(MoveIn.Pressed & MoveJump) || MoveState.JumpsUsed >= MaxJumps + MoveIn.ExtraJumps) return;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const float Height = M("jump_height", MoveState.JumpsUsed == 0 ? JumpHeight1 : JumpHeight2);
	Move->Velocity.Z = LaunchSpeed(Height) * WWO::M;
	Move->SetMovementMode(MOVE_Falling);
	MoveState.JumpsUsed += 1;
	UWWOSound::PlayFor(this, "jump");
	float AirSpeed = FVector2D(Move->Velocity.X, Move->Velocity.Y).Size() / WWO::M;
	if (MoveState.JumpsUsed > 1 && !WishDir.IsNearlyZero())
	{
		// The air jump lets you change direction: aim your momentum where you're pushing.
		AirSpeed = FMath::Max(AirSpeed, M("walk_speed", WalkSpeed));
		Move->Velocity.X = WishDir.X * AirSpeed * WWO::M;
		Move->Velocity.Y = WishDir.Y * AirSpeed * WWO::M;
	}
	MoveState.TakeoffSpeed = FMath::Max(MoveState.TakeoffSpeed, AirSpeed);
	MoveState.bSliding = false;
}

void AWWOCharacter::ApplyHorizontal(const FVector& WishDir, bool bOnFloor, float DeltaSeconds)
{
	const FVector V = GetCharacterMovement()->Velocity / WWO::M; // m/s
	if (MoveState.bSliding)
	{
		// Constant slow-down chosen so the slide covers exactly SlideDistance.
		const float End = M("crouch_speed", CrouchSpeed);
		MoveState.SlideSpeedNow = WWO::MoveToward(MoveState.SlideSpeedNow, End, SlideDecel() * DeltaSeconds);
		// The stick (so the camera too, with the stick held) turns the slide, slowly. Pulling back doesn't.
		if (SlideSteer > 0.f && !WishDir.IsNearlyZero() && (WishDir | MoveState.SlideDir) > -0.5f)
		{
			const float Now = FMath::Atan2(MoveState.SlideDir.Y, MoveState.SlideDir.X);
			const float Step = FMath::DegreesToRadians(SlideSteer) * DeltaSeconds;
			const float Yaw = Now + FMath::Clamp(FMath::FindDeltaAngleRadians(Now, FMath::Atan2(WishDir.Y, WishDir.X)), -Step, Step);
			MoveState.SlideDir = FVector(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.f);
		}
		WishVelocity = MoveState.SlideDir * MoveState.SlideSpeedNow * WWO::M;
		MoveState.GroundVel = FVector2D(WishVelocity.X, WishVelocity.Y) / WWO::M;
		if (MoveState.SlideSpeedNow <= End) MoveState.bSliding = false;
		return;
	}

	float Speed = M("walk_speed", WalkSpeed);
	if (bIsCrouched || MoveState.bCrouching) Speed = M("crouch_speed", CrouchSpeed);
	else if (MoveState.bSprinting) Speed = M("sprint_speed", SprintSpeed);
	Speed *= MoveIn.SpeedScale; // abilities x aiming x slow, from the move input
	if (!bOnFloor || MoveState.LaunchTime > 0.f)
	{
		// In the air you keep your momentum and can only steer it gradually.
		Speed = FMath::Max(Speed, MoveState.TakeoffSpeed);
		FVector2D H(V.X, V.Y);
		if (!WishDir.IsNearlyZero())
		{
			H = WWO::MoveToward2D(H, FVector2D(WishDir.X, WishDir.Y) * Speed, M("air_control", AirControl) * DeltaSeconds);
		}
		WishVelocity = FVector(H.X, H.Y, 0.f) * WWO::M;
		MoveState.GroundVel = H; // landing carries on from here
		return;
	}
	// On the ground, a little momentum: the velocity the rules asked for last move (GroundVel, not
	// the real one: a wall or a slope would eat it every move) heads for the stick's at GroundAccel,
	// or for a stop at GroundDecel. Coming out of a slide or a landing faster than you can run, you
	// slow to your speed the same way.
	const bool bPushing = !WishDir.IsNearlyZero();
	const FVector2D Target = bPushing ? FVector2D(WishDir.X, WishDir.Y) * Speed : FVector2D::ZeroVector;
	MoveState.GroundVel = WWO::MoveToward2D(MoveState.GroundVel, Target, (bPushing ? GroundAccel : GroundDecel) * DeltaSeconds);
	WishVelocity = FVector(MoveState.GroundVel.X, MoveState.GroundVel.Y, 0.f) * WWO::M;
}

void AWWOCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	const float FallSpeed = FMath::Max(-GetCharacterMovement()->Velocity.Z / WWO::M, LastFallSpeed);
	LastFallSpeed = 0.f;
	UWWOSound::PlayFor(this, "land", FMath::Clamp(FallSpeed / 8.f, 0.3f, 1.2f));
	Land(FallSpeed);
}

void AWWOCharacter::BaseChange()
{
	// Unreal bounces a character off any pawn it lands on (JumpOff). Godot's player stands on
	// enemies and allies like on any other body, so skip that and keep the floor.
	const AActor* Base = GetMovementBaseActor(this);
	if (Base && Base->IsA<APawn>()) return;
	Super::BaseChange();
}

void AWWOCharacter::Land(float FallSpeed)
{
	MoveState.SinceLanded = 0.f;
	MoveState.bLandedSprinting = MoveState.bSprinting || MoveState.TakeoffSpeed > M("walk_speed", WalkSpeed) * 1.05f;
	if (MoveState.bSlideQueued)
	{
		StartSlide(WishDirection(MoveIn.Move));
	}
	LandKick = -FMath::Clamp(FallSpeed * LandingDipPerSpeed, 0.f, LandingDipMax) * LandingDip;
}

// --- Lunge ---------------------------------------------------------------------------------

void AWWOCharacter::StartLunge(AActor* Target)
{
	bLunging = true;
	LungeTarget = Target;
	LungeTime = 0.f;
	LungeLastFlat = TNumericLimits<float>::Max();
	MoveState.bSprinting = false;
	MoveState.bSliding = false; // the next move flies it (MoveLunge)
}

void AWWOCharacter::ApplyLunge(float DeltaSeconds)
{
	const IWWOTargetable* Target = Cast<IWWOTargetable>(LungeTarget.Get());
	if (!Target)
	{
		bLunging = false; // the next move ends the flight
		LungeVelocity = FVector::ZeroVector;
		return;
	}
	LungeTime += DeltaSeconds;
	// Aim for where the target's "player-height center" would be: a flat dash on level
	// ground, pulled up or down toward a raised or lowered target.
	// Measured from where Godot's player origin is: 1 m above the feet (the standing capsule's
	// center), crouched or not; Unreal's capsule center drops when you crouch.
	const FVector Goal = Target->FeetLocation() + FVector(0, 0, 1.f * WWO::M);
	const FVector Origin = GetActorLocation() - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 1.f * WWO::M);
	FVector To = Goal - Origin;
	To.Z *= Weapon->LungeVertical;
	const float Flat = FVector2D(To.X, To.Y).Size();
	const TArray<FWWOAimPart> Parts = Target->GetAimParts();
	const float BodyRadius = Parts.Last().Radius;
	const float Reach = LungeStopDistance * WWO::M + BodyRadius * 0.5f;
	const bool bStalled = LungeTime > 0.1f && Flat >= LungeLastFlat - 2.f;
	LungeLastFlat = Flat;
	const bool bArrived = Flat <= Reach || bStalled;
	if ((bArrived && LungeTime >= LungeMinTime) || LungeTime > LungeMaxTime)
	{
		bLunging = false; // arrived: this move ends the flight (EndLungeFlight stops you dead)
		LungeVelocity = FVector::ZeroVector;
		return;
	}
	LungeVelocity = bArrived ? FVector::ZeroVector : To.GetSafeNormal() * Weapon->LungeSpeed * WWO::M;
	if (!bArrived && DeltaSeconds > 0.f && To.Size() > 1.f)
	{
		// Godot checks for arrival every 60 Hz step, so a lunge ends up to one step (20 cm at
		// 12 m/s) past the stop distance. Stop where that step lands, at any frame rate.
		const float Step = Weapon->LungeSpeed * WWO::M / 60.f * Flat / To.Size(); // flat cm per 60 Hz step
		const float Left = FMath::CeilToFloat((Flat - Reach) / FMath::Max(Step, 1.f)) * Step;
		const float FlatThisFrame = Step * 60.f * DeltaSeconds;
		if (FlatThisFrame > Left) LungeVelocity *= Left / FlatThisFrame;
	}
	TurnToward(Parts[0].Position, Weapon->LungeAim, DeltaSeconds); // upper body, so you arrive looking at it
}

// --- Mantle --------------------------------------------------------------------------------

bool AWWOCharacter::TryMantle(const FVector2D& Input)
{
	// Mantle when airborne, pushing forward, facing a wall with a flat top in reach and
	// room to stand up there.
	if (MantleReach <= 0.f || IsOnFloor() || MoveState.bSliding || bLunging || MoveState.bLungeFlight || Input.Y < 0.5f) return false; // (the host's copy only knows the flight)
	UWorld* World = GetWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOMantle), false, this);
	FVector Forward = GetActorForwardVector();
	Forward.Z = 0.f;
	Forward.Normalize();
	const float Feet = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	// 1. A wall right in front of you at chest height.
	// Godot's body origin stays 1 m above the feet when crouched (the capsule shrinks to the
	// feet), so the chest is 1.2 m up either way.
	const FVector Chest(GetActorLocation().X, GetActorLocation().Y, Feet + StandHalfHeight + 20.f);
	FHitResult Wall;
	if (!World->LineTraceSingleByChannel(Wall, Chest, Chest + Forward * MantleWallCheck * WWO::M, ECC_Visibility, Params)) return false;
	if (Cast<IWWOTargetable>(Wall.GetActor())) return false;

	// 2. A flat top within reach, just past the wall face.
	const FVector Probe = Wall.ImpactPoint + Forward * MantleStepIn * WWO::M;
	FHitResult Top;
	if (!World->LineTraceSingleByChannel(Top, FVector(Probe.X, Probe.Y, Feet + (MantleReach + 0.1f) * WWO::M),
		FVector(Probe.X, Probe.Y, Feet + MantleMinHeight * WWO::M), ECC_Visibility, Params)) return false;
	if (Top.bStartPenetrating || Top.ImpactNormal.Z < 0.7f || Cast<IWWOTargetable>(Top.GetActor())) return false;

	// 3. Room to stand there.
	const FVector Stand = Top.ImpactPoint + FVector(0, 0, StandHalfHeight + 5.f);
	if (World->OverlapAnyTestByChannel(Stand, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(CapsuleRadius, StandHalfHeight), Params))
	{
		return false;
	}

	MoveState.bMantling = true;
	MoveState.MantleT = 0.f;
	MoveState.bCrouchToggled = false;
	MoveState.bCrouching = false;
	MoveState.bSprinting = false;
	if (bIsCrouched) UnCrouch(); // grows from the feet, so the capsule center moves up
	MoveState.MantleFrom = GetActorLocation();
	MoveState.MantleTo = Stand;
	LungeVelocity = FVector::ZeroVector;
	GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	return true;
}

void AWWOCharacter::UpdateMantle(float DeltaSeconds)
{
	// Rise up along the wall first, then roll forward over the edge.
	MoveState.MantleT += DeltaSeconds / FMath::Max(MantleTime, 0.05f);
	const float T = FMath::Min(MoveState.MantleT, 1.f);
	const float Up = WWO::SmoothStep(0.f, 0.55f, T);
	const float Over = WWO::SmoothStep(0.45f, 1.f, T);
	SetActorLocation(FVector(FMath::Lerp(MoveState.MantleFrom.X, MoveState.MantleTo.X, Over), FMath::Lerp(MoveState.MantleFrom.Y, MoveState.MantleTo.Y, Over),
		FMath::Lerp(MoveState.MantleFrom.Z, MoveState.MantleTo.Z, Up)), false, nullptr, ETeleportType::TeleportPhysics);
	LungeVelocity = FVector::ZeroVector;
	WishVelocity = FVector::ZeroVector;
	if (T >= 1.f)
	{
		MoveState.bMantling = false;
		MoveState.JumpsUsed = 0;
		MoveState.TakeoffSpeed = 0.f;
		GetCharacterMovement()->Velocity = FVector::ZeroVector;
		GetCharacterMovement()->SetMovementMode(MOVE_Falling);
		Land(4.f); // a small landing dip as you come over the top (and a queued slide starts)
	}
}

void AWWOCharacter::ResetInputEdges()
{
	// The button that closed a menu (circle is also crouch) shouldn't count as a fresh press.
	MoveState.bCrouchWasHeld = Pressed(EWWOAction::Crouch);
}
