// The player character (see the header): construction, BeginPlay, the per-frame driver (Tick, and
// TickRemoteOnHost for someone else's character on the host), controllers and input reads. The rest
// of the class is split by concern: WWOCharacterMove (PrepareMovement, the move input and the
// movement rules: SimulateMove, sprint, crouch, slide, jump, lunge, mantle, the move stats and M()),
// WWOCharacterView (looking, the camera, the low reticle), WWOCharacterRecoil (the recoil state
// machine), WWOCharacterCombat (PvP hitboxes, ApplyHit, status effects), WWOCharacterLook (what other
// machines see of this body), WWOCharacterNet (replication, kit and revive RPCs) and
// WWOCharacterGunNet (the host's checks of a joiner's shots and punches).

#include "Player/WWOCharacter.h"
#include "WhatWasOurs.h"
#include "Player/WWOCharacterPrivate.h"
#include "WWOTypes.h"
#include "Engine/World.h"
#include "Player/WWOProf.h"
#include "Systems/WWOSound.h"
#include "FX/WWOFX.h"
#include "Player/WWOBotController.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Game/WWOPlayerState.h"
#include "Components/SphereComponent.h"
#include "Player/WWOCharacterMovement.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOAimAssistComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOAbilityComponent.h"
#include "Player/WWOGrenadeComponent.h"
#include "Player/WWOUltComponent.h"
#include "Characters/WWOBodyComponent.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"

using namespace WWOCharacter; // the constants these files share (WWOCharacterPrivate.h)

namespace
{
	constexpr float HeadHitRadius = 15.f;     // PvP head hitbox (cm), centered on the skull
}

AWWOCharacter::AWWOCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UWWOCharacterMovement>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	GetCapsuleComponent()->InitCapsuleSize(CapsuleRadius, StandHalfHeight);
	// PvP hitboxes (shots only; off until a PvP match, see UpdateHitboxes).
	BodyHitbox = CreateDefaultSubobject<UCapsuleComponent>(TEXT("BodyHitbox"));
	BodyHitbox->SetupAttachment(GetCapsuleComponent());
	BodyHitbox->InitCapsuleSize(35.f, 80.f);
	BodyHitbox->SetRelativeLocation(FVector(0, 0, -15.f));
	HeadHitbox = CreateDefaultSubobject<USphereComponent>(TEXT("HeadHitbox"));
	HeadHitbox->InitSphereRadius(HeadHitRadius);
	for (UPrimitiveComponent* Hitbox : { (UPrimitiveComponent*)BodyHitbox, (UPrimitiveComponent*)HeadHitbox })
	{
		Hitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Hitbox->SetCollisionObjectType(ECC_Pawn);
		Hitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
		Hitbox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Hitbox->SetCanEverAffectNavigation(false);
		Hitbox->SetGenerateOverlapEvents(false);
	}
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	HeadHitbox->SetupAttachment(Camera); // until BeginPlay pins it to the body's head (PinHeadHitbox)
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
	PinHeadHitbox(); // the body component has picked its mesh by now
	if (Controller) Pitch = FMath::DegreesToRadians(Controller->GetControlRotation().Pitch);
	Pitch = FMath::Clamp(FRotator::NormalizeAxis(FMath::RadiansToDegrees(Pitch)), -89.f, 89.f) * UE_PI / 180.f;
	// Someone else's until a controller on this machine takes it (NotifyControllerChanged).
	SetupVisuals(IsLocalHuman());
	// Online, a joiner's damage happens on the host (which charges its copy's Static Charge); the
	// joiner's own copy charges from the damage it's told about, so both agree on when it's ready.
	Health->OnDamaged.AddWeakLambda(this, [this](float Amount) { if (!HasAuthority()) Abilities->OnDamageTaken(Amount); });
	// Taking damage jolts your own view and controller a little, more for a bigger hit. (A punch, not
	// recoil: your aim stays where it was.)
	Health->OnDamaged.AddWeakLambda(this, [this](float Amount)
	{
		if (IsLocalHuman())
		{
			Weapon->Shake(Weapon->HitFlinch * FMath::Clamp(Amount / 60.f, 0.2f, 1.f) * 0.6f, 0.2f);
			UWWOSound::PlayFlat(this, "hurt", FMath::Clamp(Amount / 40.f, 0.4f, 1.f));
		}
	});
	// Someone else going down goes out with a burst, in their team's color in PvP (not your own
	// death: you're looking out from inside it).
	Health->OnDied.AddWeakLambda(this, [this]
	{
		UWWOSound::PlayFor(this, bLocalVisuals ? "death_own" : "death");
		if (bLocalVisuals) return;
		const AWWOPlayerState* PS = GetPlayerState<AWWOPlayerState>();
		const FLinearColor Color = PS && UWWOWorldSubsystem::IsPvP(this) ? AWWOPlayerState::TeamColor(PS->Team) : FLinearColor(0.6f, 0.85f, 1.f);
		AWWOFXActor::DeathBurst(GetWorld(), GetActorLocation(), Color);
	});
}

void AWWOCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	RefreshLocalVisuals();
}

void AWWOCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	if (bLocalVisuals != IsLocalHuman()) UE_LOG(LogWWO, Display, TEXT("Net visuals: %s fixed up by its player state"), *GetName());
	RefreshLocalVisuals(); // a joiner's controller often arrives before its player state
}

void AWWOCharacter::TickRemoteOnHost(float DeltaSeconds)
{
	if (UWWOWorldSubsystem::IsPvP(this))
	{
		// Move by the host's settings (its own character's base values) plus this player's loadout copy.
		bNetStats = false;
		if (const AWWOCharacter* Mine = UWWOWorldSubsystem::Get(this)->GetPlayer())
		{
			const TArray<float> Base = Mine->BaseMoveStats();
			if (Base != SentMoveBase)
			{
				SentMoveBase = Base;
				ApplyMoveBase(Base);
				ClientSetMoveBase(Base);
			}
		}
	}
	Health->UpdateHealth(DeltaSeconds);
	TickStatusTimers(DeltaSeconds);                 // their veil, entrench and slows run out here too
	Grenades->UpdateCharge(DeltaSeconds);           // to check their throws
	if (!Health->bDead)
	{
		Abilities->UpdateAbilities(DeltaSeconds);   // recharges (their presses arrive as ServerUseAbility)
		Ult->UpdateUlt(DeltaSeconds);               // a cast ult plays out here (and its charge)
	}
	if (!bUlting && EntrenchLeft <= 0.f) UpdateModel(); // the body replicates to everyone else
}

AWWOPlayerController* AWWOCharacter::GetWWOController() const
{
	return Cast<AWWOPlayerController>(Controller);
}

UWWOLoadoutSubsystem* AWWOCharacter::GetLoadout() const
{
	return UWWOLoadoutSubsystem::Get(this);
}

AWWOBotController* AWWOCharacter::GetBot() const
{
	return Cast<AWWOBotController>(Controller);
}

bool AWWOCharacter::Pressed(EWWOAction Action) const
{
	if (const AWWOBotController* Bot = GetBot()) return Bot->IsHeld(Action);
	// With a menu open (online, where menus don't pause) your buttons work the menu, not your character.
	const AWWOPlayerController* PC = GetWWOController();
	return PC && PC->IsHeld(Action); // (false under a menu, true for scripted input: the controller decides)
}

bool AWWOCharacter::JustPressed(EWWOAction Action) const
{
	if (const AWWOBotController* Bot = GetBot()) return Bot->WasJustPressed(Action);
	const AWWOPlayerController* PC = GetWWOController();
	return PC && PC->WasJustPressed(Action);
}

void AWWOCharacter::Tick(float DeltaSeconds)
{
	WWO_PROF("Character");
	Super::Tick(DeltaSeconds);
	UpdateHitboxes(); // PvP hitboxes on or off, on every machine
	UpdateTeamTint();
	Weapon->UpdateWorldGun(!bLocalVisuals); // the gun in the hand of a body others see whole
	// The movement component has already run PrepareMovement and moved (it ticks first);
	// this runs the rest in Godot's order: camera (after the move), gun, abilities, grenades, ult.
	if (!IsLocallyControlled())
	{
		// Someone else's character: the host runs their health and the world side of their kit;
		// everyone shows their body.
		if (HasAuthority()) TickRemoteOnHost(DeltaSeconds);
		else BlindLeft = FMath::Max(BlindLeft - DeltaSeconds, 0.f);
		RemoteFootsteps(DeltaSeconds);
		return;
	}
	SendPose(DeltaSeconds);
	if (PreparedFrame != GFrameCounter) PrepareMovement(DeltaSeconds); // movement didn't tick this frame
	UpdateReviveInput();
	if (Health->bDead != bWeaponSawDead)
	{
		// Dying stops the gun mid-burst, mid-reload or mid-punch; coming back starts every gun full.
		bWeaponSawDead = Health->bDead;
		if (bWeaponSawDead)
		{
			Weapon->Interrupt();
			Ult->Abort(false);          // no blast at the respawn spot from an ult you died in
			Grenades->CancelRelease();  // nor a grenade you had wound up
		}
		else Weapon->ResetForRespawn();
	}
	if (Health->bDead)
	{
		UpdateModel(); // lying where you fell (online the host's replicates to everyone)
		return; // no shooting or throwing
	}
	if (!bUlting && EntrenchLeft <= 0.f) UpdateModel(); // frozen or rooted: the ult drives the body
	UpdateCamera(DeltaSeconds);
	AimAssist->UpdateAdhesion();
	if (!bUlting) Weapon->UpdateWeapon(DeltaSeconds);
	Abilities->UpdateAbilities(DeltaSeconds);
	Grenades->UpdateGrenades(DeltaSeconds);
	Ult->UpdateUlt(DeltaSeconds);
}
