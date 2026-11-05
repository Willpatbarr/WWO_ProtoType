#include "Player/WWOCharacter.h"
#include "Player/WWOCharacterMovement.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOAimAssistComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOAbilityComponent.h"
#include "Player/WWOGrenadeComponent.h"
#include "Player/WWOUltComponent.h"
#include "Characters/WWOBodyComponent.h"
#include "Flora/WWOSporeFog.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

namespace
{
	constexpr int32 MaxJumps = 2;             // before abilities (Overgrowth adds more)
	constexpr float StandCameraZ = 60.f;      // eye height above the standing capsule center (cm)
	constexpr float CrouchCameraZ = 20.f;     // above the crouched capsule center (eye 0.8 m off the floor)
	constexpr float CameraSmoothing = 12.f;
	constexpr float WalkBobUp = 0.06f;        // meters of up/down at intensity 1
	constexpr float SprintBobUp = 0.11f;
	constexpr float WalkBobSide = 0.04f;
	constexpr float SprintBobSide = 0.07f;
	constexpr float ApexSpeed = 2.f;          // apex hang applies while vertical speed is below this (m/s)
	constexpr float LandingDipPerSpeed = 0.012f;
	constexpr float LandingDipMax = 0.25f;
	constexpr float LandSlideWindow = 0.2f;   // seconds after landing that crouch still turns into a slide
	constexpr float LungeStopDistance = 1.f;  // meters (center to center)
	constexpr float LungeMinTime = 0.15f;
	constexpr float LungeMaxTime = 0.6f;
	constexpr float LungeTurnRate = 20.f;
	constexpr float MantleWallCheck = 0.85f;
	constexpr float MantleStepIn = 0.6f;
	constexpr float MantleMinHeight = 0.3f;
	constexpr float StandHalfHeight = 100.f;
	constexpr float CapsuleRadius = 50.f;
}

AWWOCharacter::AWWOCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UWWOCharacterMovement>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	GetCapsuleComponent()->InitCapsuleSize(CapsuleRadius, StandHalfHeight);
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0, 0, StandCameraZ));
	Camera->bUsePawnControlRotation = false; // pitch and roll are set directly (see UpdateCamera)
	Camera->bEnableFirstPersonFieldOfView = true;
	Camera->bEnableFirstPersonScale = true;
	Camera->FirstPersonFieldOfView = 65.f;
	Camera->FirstPersonScale = 0.4f;
	Camera->SetFieldOfView(100.f);

	// The body: in first person only its shadow shows (you see from inside the head).
	USkeletalMeshComponent* BodyMesh = GetMesh();
	BodyMesh->SetRelativeLocationAndRotation(FVector(0, 0, -StandHalfHeight), FRotator(0, -90.f, 0));
	BodyMesh->SetOwnerNoSee(true);
	BodyMesh->bCastHiddenShadow = true;
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;

	Weapon = CreateDefaultSubobject<UWWOWeaponComponent>(TEXT("Weapon"));
	Weapon->SetupAttachment(Camera);
	AimAssist = CreateDefaultSubobject<UWWOAimAssistComponent>(TEXT("AimAssist"));
	Health = CreateDefaultSubobject<UWWOHealthComponent>(TEXT("Health"));
	Abilities = CreateDefaultSubobject<UWWOAbilityComponent>(TEXT("Abilities"));
	Grenades = CreateDefaultSubobject<UWWOGrenadeComponent>(TEXT("Grenades"));
	Ult = CreateDefaultSubobject<UWWOUltComponent>(TEXT("Ult"));
	Body = CreateDefaultSubobject<UWWOBodyComponent>(TEXT("Body"));
	Body->BodyColor = FLinearColor(0.3f, 0.42f, 0.55f); // steel blue

	Tags.Add("Player");
}

void AWWOCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (Controller) Pitch = FMath::DegreesToRadians(Controller->GetControlRotation().Pitch);
	Pitch = FMath::Clamp(FRotator::NormalizeAxis(FMath::RadiansToDegrees(Pitch)), -89.f, 89.f) * UE_PI / 180.f;
	Body->FollowLoadout(true);
}

AWWOPlayerController* AWWOCharacter::GetWWOController() const
{
	return Cast<AWWOPlayerController>(Controller);
}

UWWOLoadoutSubsystem* AWWOCharacter::GetLoadout() const
{
	return UWWOLoadoutSubsystem::Get(this);
}

float AWWOCharacter::M(FName Stat, float Base) const
{
	const UWWOLoadoutSubsystem* Loadout = GetLoadout();
	return Loadout ? Loadout->Apply(Stat, Base) : Base;
}

bool AWWOCharacter::Pressed(EWWOAction Action) const
{
	const AWWOPlayerController* PC = GetWWOController();
	return PC && PC->IsHeld(Action);
}

bool AWWOCharacter::JustPressed(EWWOAction Action) const
{
	const AWWOPlayerController* PC = GetWWOController();
	return PC && PC->WasJustPressed(Action);
}

FVector2D AWWOCharacter::MoveInput() const
{
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

FVector AWWOCharacter::EyeLocation() const
{
	return Camera->GetComponentLocation();
}

FVector AWWOCharacter::AimForward() const
{
	return Camera->GetForwardVector();
}

// --- Looking --------------------------------------------------------------------------------

void AWWOCharacter::SetViewRotation(float PitchRad, float YawDeg)
{
	Pitch = FMath::Clamp(PitchRad, FMath::DegreesToRadians(-89.f), FMath::DegreesToRadians(89.f));
	if (Controller)
	{
		Controller->SetControlRotation(FRotator(FMath::RadiansToDegrees(Pitch), YawDeg, 0.f));
	}
	SetActorRotation(FRotator(0.f, YawDeg, 0.f));
}

void AWWOCharacter::Look(FVector2D Amount)
{
	Amount *= Weapon->LookMultiplier(); // aiming down sights slows turning
	Weapon->AddLook(Amount);
	const float Yaw = (Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw) + FMath::RadiansToDegrees(Amount.X);
	SetViewRotation(Pitch + Amount.Y, Yaw);
}

void AWWOCharacter::ApplyRecoil(float PitchRad, float YawRad)
{
	const float Yaw = (Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw) - FMath::RadiansToDegrees(YawRad);
	SetViewRotation(Pitch + PitchRad, Yaw);
}

void AWWOCharacter::TurnToward(const FVector& Point, float Strength, float DeltaSeconds)
{
	// Swing the view toward a point. Strength 1 = locks on almost instantly, 0 = no pull.
	if (Strength <= 0.f) return;
	const FVector Dir = (Point - EyeLocation()).GetSafeNormal();
	const float Weight = FMath::Min(Strength * LungeTurnRate * DeltaSeconds, 1.f);
	const float CurrentYaw = Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw;
	const float TargetYaw = Dir.Rotation().Yaw;
	const float Yaw = CurrentYaw + FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw) * Weight;
	SetViewRotation(FMath::Lerp(Pitch, FMath::Asin(Dir.Z), Weight), Yaw);
}

// --- Each frame -------------------------------------------------------------------------------

void AWWOCharacter::PrepareMovement(float DeltaSeconds)
{
	// Called by the movement component right before it moves, so input from this frame moves
	// you this frame (Godot sets the velocity and calls move_and_slide in the same step).
	PreparedFrame = GFrameCounter;
	Health->UpdateHealth(DeltaSeconds); // same order as the Godot nodes: health, then movement
	if (Health->bDead)
	{
		WishVelocity = FVector::ZeroVector;
		return; // no moving
	}
	TickMovement(DeltaSeconds);
}

void AWWOCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// The movement component has already run PrepareMovement and moved (it ticks first);
	// this runs the rest in Godot's order: camera (after the move), gun, abilities, grenades, ult.
	if (PreparedFrame != GFrameCounter) PrepareMovement(DeltaSeconds); // movement didn't tick this frame
	if (Health->bDead) return; // no shooting or throwing
	UpdateCamera(DeltaSeconds);
	AimAssist->UpdateAdhesion();
	if (!bUlting) Weapon->UpdateWeapon(DeltaSeconds);
	Abilities->UpdateAbilities(DeltaSeconds);
	Grenades->UpdateGrenades(DeltaSeconds);
	Ult->UpdateUlt(DeltaSeconds);
}

void AWWOCharacter::TickMovement(float DeltaSeconds)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const AWWOPlayerController* PC = GetWWOController();
	const FVector2D LookStick = PC ? PC->GetLookAxis() : FVector2D::ZeroVector;

	UpdateRecoil(DeltaSeconds);
	SlowLeft = FMath::Max(SlowLeft - DeltaSeconds, 0.f);
	VeilLeft = FMath::Max(VeilLeft - DeltaSeconds, 0.f);
	EntrenchLeft = FMath::Max(EntrenchLeft - DeltaSeconds, 0.f);

	if ((bUlting || EntrenchLeft > 0.f) && bLunging)
	{
		bLunging = false; // a lunge can't move while you're frozen or rooted; drop it so the punch lands
		Move->SetMovementMode(MOVE_Falling);
	}
	if (bUlting || EntrenchLeft > 0.f)
	{
		// Frozen for the ult, rooted while entrenched (you can still look and shoot).
		if (EntrenchLeft > 0.f && !bUlting)
		{
			Look(LookStick * StickSensitivity * DeltaSeconds * AimAssist->FrictionScale());
			bSprinting = false;
			bSliding = false;
		}
		UpdateAir(DeltaSeconds);
		WishVelocity = FVector::ZeroVector;
		return;
	}
	Look(LookStick * StickSensitivity * DeltaSeconds * AimAssist->FrictionScale()); // friction: slower near targets

	const FVector2D Input = MoveInput();
	const FVector WishDir = WishDirection(Input);

	if (bMantling || TryMantle(Input))
	{
		UpdateMantle(DeltaSeconds);
		return;
	}

	UpdateSprint(Input);
	UpdateCrouchAndSlide(WishDir, DeltaSeconds);
	UpdateAir(DeltaSeconds);
	HandleJump(WishDir);
	ApplyHorizontal(WishDir, DeltaSeconds);
	if (bLunging) ApplyLunge(DeltaSeconds);
	if (!IsOnFloor()) LastFallSpeed = -Move->Velocity.Z / WWO::M;
}

void AWWOCharacter::UpdateSprint(const FVector2D& Input)
{
	// Press once to sprint; it stays on until you stop moving or crouch. Aiming and firing
	// stop a sprint and block starting one. Sprinting from a crouch stands you up.
	if (JustPressed(EWWOAction::Sprint) && !bSliding && !Weapon->IsAiming() && !Pressed(EWWOAction::Fire))
	{
		bSprinting = true;
		bCrouchToggled = false;
	}
	if (Input.Size() < 0.1f || Weapon->IsAiming() || Pressed(EWWOAction::Fire))
	{
		bSprinting = false;
	}
}

bool AWWOCharacter::HasSprintMomentum() const
{
	return bSprinting || TakeoffSpeed > M("walk_speed", WalkSpeed) * 1.05f;
}

void AWWOCharacter::UpdateCrouchAndSlide(const FVector& WishDir, float DeltaSeconds)
{
	const bool bCrouchHeld = Pressed(EWWOAction::Crouch);
	const bool bCrouchStarted = bCrouchHeld && !bCrouchWasHeld;
	bCrouchWasHeld = bCrouchHeld;

	// Tapping crouch while sprinting on the ground starts a slide. From a sprint jump,
	// crouch in the air queues a slide for landing, and crouch just after landing slides.
	SinceLanded += DeltaSeconds;
	if (bCrouchStarted)
	{
		const bool bCanSlide = M("slide_speed", SlideSpeed) > M("crouch_speed", CrouchSpeed);
		const bool bJustLanded = SinceLanded <= LandSlideWindow && bLandedSprinting;
		if (bCanSlide && IsOnFloor() && (bSprinting || bJustLanded) && !bSliding)
		{
			StartSlide(WishDir);
		}
		else if (bCanSlide && !IsOnFloor() && HasSprintMomentum())
		{
			bSlideQueued = !bSlideQueued; // tap again in the air to cancel
		}
		else if (!bSliding)
		{
			bCrouchToggled = !bCrouchToggled;
		}
	}
	if (bSlideQueued && !IsOnFloor()) return; // stay standing in the air; the slide starts on landing

	if (bSliding || bCrouchToggled)
	{
		bCrouching = true;
		bSprinting = false;
	}
	else
	{
		bCrouching = false; // Unreal keeps you crouched under a low ceiling until you can stand
	}
	if (bCrouching && !bIsCrouched) Crouch();
	else if (!bCrouching && bIsCrouched) UnCrouch();
}

void AWWOCharacter::StartSlide(const FVector& WishDir)
{
	bSliding = true;
	bSlideQueued = false;
	const FVector Moving(GetVelocity().X, GetVelocity().Y, 0.f);
	SlideDir = !WishDir.IsNearlyZero() ? WishDir : (Moving.Size() > 50.f ? Moving.GetSafeNormal() : GetActorForwardVector());
	SlideSpeedNow = M("slide_speed", SlideSpeed);
	bCrouching = true;
	bSprinting = false;
}

void AWWOCharacter::Launch(const FVector& NewVelocityCm)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->Velocity = NewVelocityCm;
	Move->SetMovementMode(MOVE_Falling);
	TakeoffSpeed = FVector2D(NewVelocityCm.X, NewVelocityCm.Y).Size() / WWO::M;
	LaunchTime = 0.15f;
	bSliding = false;
	bSprinting = false;
}

float AWWOCharacter::CurrentGravityZ() const
{
	const FVector V = GetCharacterMovement()->Velocity;
	float G = V.Z > 0.f ? AscentGravity : DescentGravity;
	if (JumpsUsed > 0 && FMath::Abs(V.Z) < ApexSpeed * WWO::M)
	{
		G *= 1.f - ApexHangClamped(); // float a moment at the top of the jump
	}
	return -G * WWO::M;
}

void AWWOCharacter::UpdateAir(float DeltaSeconds)
{
	LaunchTime = FMath::Max(LaunchTime - DeltaSeconds, 0.f);
	if (IsOnFloor() && LaunchTime <= 0.f)
	{
		JumpsUsed = 0;
		TakeoffSpeed = 0.f;
		AirTime = 0.f;
		return;
	}
	AirTime += DeltaSeconds;
	if (JumpsUsed == 0 && AirTime > CoyoteTime)
	{
		JumpsUsed = 1; // walked off a ledge and missed the coyote window: only the air jump is left
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
	if (!JustPressed(EWWOAction::Jump) || JumpsUsed >= MaxJumps + Abilities->ExtraJumps()) return;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const float Height = M("jump_height", JumpsUsed == 0 ? JumpHeight1 : JumpHeight2);
	Move->Velocity.Z = LaunchSpeed(Height) * WWO::M;
	Move->SetMovementMode(MOVE_Falling);
	JumpsUsed += 1;
	float AirSpeed = FVector2D(Move->Velocity.X, Move->Velocity.Y).Size() / WWO::M;
	if (JumpsUsed > 1 && !WishDir.IsNearlyZero())
	{
		// The air jump lets you change direction: aim your momentum where you're pushing.
		AirSpeed = FMath::Max(AirSpeed, M("walk_speed", WalkSpeed));
		Move->Velocity.X = WishDir.X * AirSpeed * WWO::M;
		Move->Velocity.Y = WishDir.Y * AirSpeed * WWO::M;
	}
	TakeoffSpeed = FMath::Max(TakeoffSpeed, AirSpeed);
	bSliding = false;
}

void AWWOCharacter::ApplyHorizontal(const FVector& WishDir, float DeltaSeconds)
{
	const FVector V = GetCharacterMovement()->Velocity / WWO::M; // m/s
	if (bSliding)
	{
		// Constant slow-down chosen so the slide covers exactly SlideDistance.
		const float Start = M("slide_speed", SlideSpeed);
		const float End = M("crouch_speed", CrouchSpeed);
		const float Decel = (Start * Start - End * End) / (2.f * M("slide_distance", SlideDistance));
		SlideSpeedNow = WWO::MoveToward(SlideSpeedNow, End, Decel * DeltaSeconds);
		WishVelocity = SlideDir * SlideSpeedNow * WWO::M;
		if (SlideSpeedNow <= End) bSliding = false;
		return;
	}

	float Speed = M("walk_speed", WalkSpeed);
	if (bIsCrouched || bCrouching) Speed = M("crouch_speed", CrouchSpeed);
	else if (bSprinting) Speed = M("sprint_speed", SprintSpeed);
	Speed *= Abilities->SpeedMultiplier() * Weapon->MoveMultiplier() * SlowMultiplier();
	if (!IsOnFloor() || LaunchTime > 0.f)
	{
		// In the air you keep your momentum and can only steer it gradually.
		Speed = FMath::Max(Speed, TakeoffSpeed);
		FVector2D H(V.X, V.Y);
		if (!WishDir.IsNearlyZero())
		{
			H = WWO::MoveToward2D(H, FVector2D(WishDir.X, WishDir.Y) * Speed, M("air_control", AirControl) * DeltaSeconds);
		}
		WishVelocity = FVector(H.X, H.Y, 0.f) * WWO::M;
		return;
	}
	if (!WishDir.IsNearlyZero())
	{
		WishVelocity = WishDir * Speed * WWO::M;
	}
	else
	{
		WishVelocity = FVector(WWO::MoveToward(V.X, 0.f, Speed), WWO::MoveToward(V.Y, 0.f, Speed), 0.f) * WWO::M;
	}
}

void AWWOCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	const float FallSpeed = FMath::Max(-GetCharacterMovement()->Velocity.Z / WWO::M, LastFallSpeed);
	LastFallSpeed = 0.f;
	SinceLanded = 0.f;
	bLandedSprinting = bSprinting || TakeoffSpeed > M("walk_speed", WalkSpeed) * 1.05f;
	if (bSlideQueued)
	{
		StartSlide(WishDirection(MoveInput()));
	}
	LandKick = -FMath::Clamp(FallSpeed * LandingDipPerSpeed, 0.f, LandingDipMax) * LandingDip;
}

void AWWOCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	CameraBaseZ += ScaledHalfHeightAdjust; // the capsule dropped: keep the eye where it was, then ease down
}

void AWWOCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	CameraBaseZ -= ScaledHalfHeightAdjust;
}

void AWWOCharacter::UpdateCamera(float DeltaSeconds)
{
	// Eye height eases between standing and crouched.
	const float Target = bIsCrouched ? CrouchCameraZ : StandCameraZ;
	CameraBaseZ = FMath::Lerp(CameraBaseZ, Target, FMath::Min(CameraSmoothing * DeltaSeconds, 1.f));

	// Landing dip: the camera drops quickly toward the kick while the kick eases back.
	LandOffset = FMath::Lerp(LandOffset, LandKick, FMath::Min(25.f * DeltaSeconds, 1.f));
	LandKick = FMath::Lerp(LandKick, 0.f, FMath::Min(8.f * DeltaSeconds, 1.f));

	// Head bob: up/down once per step, side to side once per two steps.
	const float Speed = FVector2D(GetVelocity().X, GetVelocity().Y).Size() / WWO::M;
	const bool bWalking = IsOnFloor() && !bSliding && !bMantling && Speed > 0.5f;
	SprintBlend = WWO::MoveToward(SprintBlend, bSprinting ? 1.f : 0.f, 3.f * DeltaSeconds);
	const float Stride = FMath::Max(FMath::Lerp(WalkStride, SprintStride, SprintBlend), 0.1f);
	if (bWalking) BobTime += Speed / Stride * UE_PI * DeltaSeconds; // one step = half a sine wave
	BobWeight = WWO::MoveToward(BobWeight, bWalking ? 1.f : 0.f, 4.f * DeltaSeconds);
	const float Amount = BobIntensity * BobWeight * (1.f - 0.75f * Weapon->AdsAmount); // steadier when aimed
	const float Up = FMath::Lerp(WalkBobUp, SprintBobUp, SprintBlend) * Amount;
	const float Side = FMath::Lerp(WalkBobSide, SprintBobSide, SprintBlend) * Amount;
	if (!bUlting)
	{
		Camera->SetRelativeLocation(FVector(0.f, FMath::Cos(BobTime * 0.5f) * Side * WWO::M,
			CameraBaseZ + (LandOffset + FMath::Abs(FMath::Sin(BobTime)) * Up) * WWO::M));
		Camera->SetRelativeRotation(FRotator(FMath::RadiansToDegrees(Pitch), 0.f, Weapon->CameraRoll()));
	}
}

// --- Lunge ---------------------------------------------------------------------------------

void AWWOCharacter::StartLunge(AActor* Target)
{
	bLunging = true;
	LungeTarget = Target;
	LungeTime = 0.f;
	LungeLastFlat = TNumericLimits<float>::Max();
	bSprinting = false;
	bSliding = false;
	GetCharacterMovement()->SetMovementMode(MOVE_Flying);
}

void AWWOCharacter::ApplyLunge(float DeltaSeconds)
{
	const IWWOTargetable* Target = Cast<IWWOTargetable>(LungeTarget.Get());
	if (!Target)
	{
		bLunging = false;
		LungeVelocity = FVector::ZeroVector;
		GetCharacterMovement()->SetMovementMode(MOVE_Falling);
		return;
	}
	LungeTime += DeltaSeconds;
	// Aim for where the target's "player-height center" would be: a flat dash on level
	// ground, pulled up or down toward a raised or lowered target.
	const FVector Goal = Target->FeetLocation() + FVector(0, 0, 1.f * WWO::M);
	FVector To = Goal - GetActorLocation();
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
		bLunging = false;
		LungeVelocity = FVector::ZeroVector;
		GetCharacterMovement()->Velocity = FVector::ZeroVector; // stop dead; gravity takes over
		GetCharacterMovement()->SetMovementMode(MOVE_Falling);
		return;
	}
	LungeVelocity = bArrived ? FVector::ZeroVector : To.GetSafeNormal() * Weapon->LungeSpeed * WWO::M;
	TurnToward(Parts[0].Position, Weapon->LungeAim, DeltaSeconds); // upper body, so you arrive looking at it
}

// --- Mantle --------------------------------------------------------------------------------

bool AWWOCharacter::TryMantle(const FVector2D& Input)
{
	// Mantle when airborne, pushing forward, facing a wall with a flat top in reach and
	// room to stand up there.
	if (MantleReach <= 0.f || IsOnFloor() || bSliding || bLunging || Input.Y < 0.5f) return false;
	UWorld* World = GetWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOMantle), false, this);
	FVector Forward = GetActorForwardVector();
	Forward.Z = 0.f;
	Forward.Normalize();
	const float Feet = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	// 1. A wall right in front of you at chest height.
	const FVector Chest = GetActorLocation() + FVector(0, 0, 20.f);
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

	bMantling = true;
	MantleT = 0.f;
	MantleFrom = GetActorLocation();
	MantleTo = Stand;
	bCrouchToggled = false;
	bCrouching = false;
	bSprinting = false;
	if (bIsCrouched) UnCrouch();
	LungeVelocity = FVector::ZeroVector;
	GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	return true;
}

void AWWOCharacter::UpdateMantle(float DeltaSeconds)
{
	// Rise up along the wall first, then roll forward over the edge.
	MantleT += DeltaSeconds / FMath::Max(MantleTime, 0.05f);
	const float T = FMath::Min(MantleT, 1.f);
	const float Up = WWO::SmoothStep(0.f, 0.55f, T);
	const float Over = WWO::SmoothStep(0.45f, 1.f, T);
	SetActorLocation(FVector(FMath::Lerp(MantleFrom.X, MantleTo.X, Over), FMath::Lerp(MantleFrom.Y, MantleTo.Y, Over),
		FMath::Lerp(MantleFrom.Z, MantleTo.Z, Up)), false, nullptr, ETeleportType::TeleportPhysics);
	LungeVelocity = FVector::ZeroVector;
	WishVelocity = FVector::ZeroVector;
	if (T >= 1.f)
	{
		bMantling = false;
		JumpsUsed = 0;
		TakeoffSpeed = 0.f;
		GetCharacterMovement()->Velocity = FVector::ZeroVector;
		GetCharacterMovement()->SetMovementMode(MOVE_Falling);
		LandKick = -FMath::Clamp(4.f * LandingDipPerSpeed, 0.f, LandingDipMax) * LandingDip; // a small dip over the top
	}
}

// --- Status --------------------------------------------------------------------------------

void AWWOCharacter::ApplySlow(float Amount, float Seconds)
{
	SlowAmount = FMath::Max(FMath::Clamp(Amount, 0.f, 0.95f), SlowLeft > 0.f ? SlowAmount : 0.f);
	SlowLeft = FMath::Max(SlowLeft, Seconds);
}

bool AWWOCharacter::IsInvisible() const
{
	return VeilLeft > 0.f && AWWOSporeFog::Contains(GetWorld(), GetActorLocation());
}

float AWWOCharacter::VeilMeleeMultiplier() const
{
	return IsInvisible() ? Ult->VeilMeleeMult : 1.f;
}

float AWWOCharacter::EntrenchRechargeMultiplier() const
{
	return EntrenchLeft > 0.f ? Ult->EntrenchGrenadeMult : 1.f;
}

void AWWOCharacter::ClearStatusEffects()
{
	SlowLeft = 0.f;
	VeilLeft = 0.f;
	EntrenchLeft = 0.f;
	bLunging = false;
	bMantling = false;
	bSliding = false;
	bSprinting = false;
	ResetRecoil();
}

void AWWOCharacter::ResetInputEdges()
{
	// The button that closed a menu (circle is also crouch) shouldn't count as a fresh press.
	bCrouchWasHeld = Pressed(EWWOAction::Crouch);
}

// --- Recoil -----------------------------------------------------------------------------------

void AWWOCharacter::ResetRecoil()
{
	KickLeft = KickYawLeft = 0.f;
	bRecovering = false;
}

void AWWOCharacter::AddRecoil(float PitchRad, float YawRad, float KickTime, float Recovery, float InRecoveryTime, float InRecoveryCurve)
{
	// With a kick time, the view rises over that time, then falls back by Recovery of the
	// kick. Shooting again before it settles stacks from where you are.
	if (KickTime <= 0.f && Recovery <= 0.f)
	{
		ApplyRecoil(PitchRad, YawRad);
		return;
	}
	KickLeft += PitchRad;
	KickYawLeft += YawRad;
	KickRate = KickLeft / FMath::Max(KickTime, 0.001f);
	const float Peak = Pitch + KickLeft;
	RecoilRest = Peak - PitchRad * FMath::Clamp(Recovery, 0.f, 1.f); // only this shot's kick falls back
	RecoveryTime = FMath::Max(InRecoveryTime, 0.01f);
	bRecovering = Recovery > 0.f;
	RecoveryCurve = InRecoveryCurve;
	RecoveryAge = 0.f;
	RecoveryDrop = Peak - RecoilRest;
	RecoveryLeft = RecoveryDrop;
}

float AWWOCharacter::TimedLeft(float Age) const
{
	const float Progress = FMath::Clamp(Age / RecoveryTime, 0.f, 1.f);
	return RecoveryDrop * FMath::Pow(1.f - Progress, RecoveryCurve);
}

void AWWOCharacter::UpdateRecoil(float DeltaSeconds)
{
	if (KickLeft > 0.f)
	{
		const float Step = FMath::Min(KickLeft, KickRate * DeltaSeconds);
		const float Fraction = Step / KickLeft;
		const float YawStep = KickYawLeft * Fraction;
		KickLeft -= Step;
		KickYawLeft -= YawStep;
		ApplyRecoil(Step, YawStep);
		return;
	}
	if (!bRecovering) return;
	const float YawNow = Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw;
	if (Pitch <= RecoilRest + 0.0002f)
	{
		bRecovering = false; // settled, or you already pulled it down yourself
		return;
	}
	if (RecoveryCurve > 0.f)
	{
		// Timed: what's left falls as (1 - progress)^curve, landing on time. Moves by the
		// change each frame, so your own aiming adds on top.
		const float Before = TimedLeft(RecoveryAge);
		RecoveryAge += DeltaSeconds;
		const float After = TimedLeft(RecoveryAge);
		SetViewRotation(FMath::Max(Pitch - (Before - After), RecoilRest), YawNow);
		if (After <= 0.f) bRecovering = false;
		return;
	}
	// Free: what's left of the kick eases out exponentially.
	const float Before = RecoveryLeft;
	RecoveryLeft *= FMath::Exp(-DeltaSeconds * 3.f / RecoveryTime);
	SetViewRotation(FMath::Max(Pitch - (Before - RecoveryLeft), RecoilRest), YawNow);
	if (RecoveryLeft <= 0.0002f) bRecovering = false;
}
