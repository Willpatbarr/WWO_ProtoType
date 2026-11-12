#include "Player/WWOCharacter.h"
#include "Systems/WWOSound.h"
#include "FX/WWOFX.h"
#include "Player/WWOBotController.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Game/WWOPlayerState.h"
#include "Components/SphereComponent.h"
#include "Flora/WWOFloraEffects.h"
#include "WhatWasOurs.h"
#include "Data/WWOData.h"
#include "Net/UnrealNetwork.h"
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
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "AnimationRuntime.h"
#include "Engine/SkeletalMesh.h"

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
	constexpr float HeadHitRadius = 15.f;     // PvP head hitbox (cm), centered on the skull
	constexpr float HeadHitUp = 10.f;         // skull center above the head bone, as on the enemies
	constexpr float HeadHitForward = 2.f;
	constexpr float HeadShotThrough = 60.f;   // a shot that lands this close in front of the head still counts
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
	SetupVisuals(IsLocallyControlled() && IsPlayerControlled());
	// Online, a joiner's damage happens on the host (which charges its copy's Static Charge); the
	// joiner's own copy charges from the damage it's told about, so both agree on when it's ready.
	Health->OnDamaged.AddWeakLambda(this, [this](float Amount) { if (!HasAuthority()) Abilities->OnDamageTaken(Amount); });
	// Taking damage jolts your own view and controller a little, more for a bigger hit. (A punch, not
	// recoil: your aim stays where it was.)
	Health->OnDamaged.AddWeakLambda(this, [this](float Amount)
	{
		if (IsLocallyControlled() && IsPlayerControlled())
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
	if (bLocalVisuals != (IsLocallyControlled() && IsPlayerControlled())) UE_LOG(LogWWO, Display, TEXT("Net visuals: %s fixed up by its player state"), *GetName());
	RefreshLocalVisuals(); // a joiner's controller often arrives before its player state
}

void AWWOCharacter::RefreshLocalVisuals()
{
	// A bot is run here but isn't anyone's view: it shows its whole body like another player.
	const bool bMine = IsLocallyControlled() && IsPlayerControlled();
	if (bMine != bLocalVisuals) SetupVisuals(bMine);
}

void AWWOCharacter::UpdateTeamTint()
{
	// PvP, on each machine: when the one playing here marks a player (the reticle comes over them,
	// or they enter the marker range while aimed in; the HUD decides, UWWOWeaponComponent::Marked),
	// that player's outline flashes once: a strong outline over a faint tint, fading out. Red for
	// the other team, blue for theirs, whichever team that is. Nobody is tinted otherwise.
	static constexpr float Fill = 0.1f, Rim = 1.7f; // the outline saturates well in from the edge
	static constexpr float FlashHold = 0.25f, FlashFade = 0.55f; // seconds at full strength, then fading
	static const FLinearColor Foe(1.f, 0.1f, 0.06f), Mate(0.2f, 0.5f, 1.f);
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const AWWOCharacter* Watcher = PC ? Cast<AWWOCharacter>(PC->GetPawn()) : nullptr;
	const double* Since = Watcher && Watcher != this ? Watcher->Weapon->Marked.Find(this) : nullptr;
	const float Age = Since ? (float)(GetWorld()->GetTimeSeconds() - *Since) : 99.f;
	const float Strength = 1.f - FMath::Clamp((Age - FlashHold) / FlashFade, 0.f, 1.f);
	if (bLocalVisuals || Health->bDead || !UWWOWorldSubsystem::IsPvP(this) || Strength <= 0.f)
	{
		Body->SetTeamTint(FLinearColor::Black, 0.f, 0.f);
		return;
	}
	Body->SetTeamTint(UWWOWorldSubsystem::CanHurt(Watcher, this) ? Foe : Mate, Fill * Strength, Rim * Strength);
}

void AWWOCharacter::SetupVisuals(bool bLocal)
{
	// Yours: the first-person gun and arms, and only your body's shadow (with your flora's grafts and
	// frame part colors). Someone else's (online): their whole body on their flora's model, no gun.
	bLocalVisuals = bLocal;
	if (GetNetMode() != NM_Standalone) UE_LOG(LogWWO, Display, TEXT("Net visuals: %s set up as %s"), *GetName(), bLocal ? TEXT("mine") : TEXT("someone else's"));
	Weapon->SetFirstPersonHidden(!bLocal);
	if (bLocal)
	{
		// A joiner's own character can be set up as someone else's for a moment before it's known to
		// be theirs: drop the copy of the loadout it was given then, so it reads their real one.
		if (!HasAuthority() && NetLoadout)
		{
			NetLoadout = nullptr;
			if (HasActorBegunPlay()) Weapon->RebindLoadout();
		}
		Body->FollowLoadout(true);
	}
	else
	{
		Body->SetShadowOnly(false);
		if (!NetFlora.IsNone()) Body->SetModel(NetFlora);
		ShownLook.Reset();
		ShowLook(); // their grafts, if what they have on is known yet (else when it arrives: OnRep_NetLook)
	}
}

void AWWOCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWWOCharacter, NetFlora);
	DOREPLIFETIME(AWWOCharacter, NetLook);
	DOREPLIFETIME(AWWOCharacter, NetFists);
}

void AWWOCharacter::OnRep_NetFlora()
{
	if (!bLocalVisuals && !NetFlora.IsNone()) Body->SetModel(NetFlora);
}

void AWWOCharacter::OnRep_NetLook()
{
	// A joiner keeps a copy of everyone else's loadout (as the host does for joiners and bots), so
	// whatever belongs to that character reads what they have on, not what you do.
	if (IsLocallyControlled() && IsPlayerControlled()) return; // your own comes from your own loadout
	if (!HasAuthority())
	{
		if (!NetLoadout) NetLoadout = UWWOLoadoutSubsystem::NewCopy(UWWOLoadoutSubsystem::Get(GetWorld()));
		if (NetLoadout) NetLoadout->ApplyNet(NetLook);
	}
	if (HasActorBegunPlay() && !bLocalVisuals) ShowLook();
}

void AWWOCharacter::OnRep_NetFists()
{
	// Someone else's fists, as the host says (your own are lit from your own ability state).
	if (IsLocallyControlled() && IsPlayerControlled()) return;
	const UWWOLoadoutSubsystem* Data = UWWOLoadoutSubsystem::Get(GetWorld());
	const UWWOAbilityData* Lit = nullptr;
	if (Data && !NetFists.IsNone()) for (const UWWOAbilityData* A : Data->Abilities) if (A && A->Id == NetFists) Lit = A;
	Body->SetHandGlow(Lit ? Lit->Color : FLinearColor::Black, Lit ? 0.85f : 0.f);
}

void AWWOCharacter::ShowLook()
{
	// Rebuilt only when something you can see changed (not for a swapped gun or its mods).
	FString Look = NetLook.Flora.ToString() + TEXT("|") + NetLook.Grenade.ToString();
	for (const FName& Id : NetLook.Abilities) Look += TEXT("|") + Id.ToString();
	for (const FName& Id : NetLook.FrameParts) Look += TEXT("|") + Id.ToString();
	if (NetLook.Flora.IsNone() || Look == ShownLook) return;
	ShownLook = Look;
	Body->FollowLoadout(false); // their flora's grafts and their shell's colors, from their loadout
	UE_LOG(LogWWO, Display, TEXT("Net visuals: %s shows %s"), *GetName(), *Look);
}

void AWWOCharacter::ServerSetFlora_Implementation(FName Flora)
{
	NetFlora = Flora;
	OnRep_NetFlora(); // the host shows it too
}

void AWWOCharacter::ServerSetLoadout_Implementation(const FWWONetLoadout& Loadout)
{
	if (!NetLoadout) NetLoadout = UWWOLoadoutSubsystem::NewCopy(UWWOLoadoutSubsystem::Get(GetWorld()));
	if (NetLoadout) NetLoadout->ApplyNet(Loadout);
	// Every machine gets it straight away (a joiner standing still sends no moves for the per-frame check to ride on).
	NetLook = Loadout;
	OnRep_NetLook();
	UE_LOG(LogWWO, Display, TEXT("Net: %s's loadout: flora %s, grenade %s, abilities %s, %d traits, weapon %s with %d mods"), *GetName(),
		*Loadout.Flora.ToString(), *Loadout.Grenade.ToString(), *FString::JoinBy(Loadout.Abilities, TEXT("+"), [](const FName& N) { return N.ToString(); }),
		Loadout.Traits.Num(), *Loadout.Weapon.ToString(), Loadout.ModIds.Num());
}

int32 AWWOCharacter::NetAbilitiesUsed = 0;
int32 AWWOCharacter::NetUltsCast = 0;

void AWWOCharacter::TickStatusTimers(float DeltaSeconds)
{
	SlowLeft = FMath::Max(SlowLeft - DeltaSeconds, 0.f);
	VeilLeft = FMath::Max(VeilLeft - DeltaSeconds, 0.f);
	EntrenchLeft = FMath::Max(EntrenchLeft - DeltaSeconds, 0.f);
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

void AWWOCharacter::ServerUseAbility_Implementation(int32 Slot)
{
	++NetAbilitiesUsed;
	Abilities->HostUseAbility(Slot);
}

void AWWOCharacter::ServerActivateUlt_Implementation()
{
	if (Ult->Charge >= 0.95f) Ult->Fill(); // a little slack: the two charges run a frame or two apart
	const bool bCast = Ult->ActivateUlt();
	NetUltsCast += bCast ? 1 : 0;
	UE_LOG(LogWWO, Display, TEXT("NetUlt from %s: %s"), *GetName(), bCast ? TEXT("cast") : TEXT("refused"));
}

void AWWOCharacter::ClientDamageFrom_Implementation(FVector_NetQuantize From)
{
	Health->AddDamageMark(From);
}

void AWWOCharacter::ClientApplySlow_Implementation(float Amount, float Seconds)
{
	ApplySlow(Amount, Seconds); // the host slowed you: your moves must slow too, or they disagree
}

void AWWOCharacter::MulticastEntrenchRoots_Implementation(float Seconds)
{
	if (HasAuthority() || IsLocallyControlled()) return; // the host's and the player's own already show them
	AWWOEntrenchRoots::Spawn(GetWorld(), this, Seconds);
}

void AWWOCharacter::ServerSetRevive_Implementation(bool bHolding)
{
	Health->bWantsRevive = bHolding;
}

void AWWOCharacter::UpdateReviveInput()
{
	if (GetNetMode() == NM_Standalone) return;
	const bool bHolding = !Health->bDead && Pressed(EWWOAction::Revive) && Health->FindReviveTarget();
	if (HasAuthority()) Health->bWantsRevive = bHolding;
	else if (bHolding != bSentRevive)
	{
		bSentRevive = bHolding;
		ServerSetRevive(bHolding);
	}
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
	if (bNetStats) return Base; // the host's copy of someone else: their values came already modified
	const UWWOLoadoutSubsystem* Loadout = GetLoadout();
	return Loadout ? Loadout->Apply(Stat, Base) : Base;
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
	return PC && !PC->IsMenuOpen() && PC->IsHeld(Action);
}

bool AWWOCharacter::JustPressed(EWWOAction Action) const
{
	if (const AWWOBotController* Bot = GetBot()) return Bot->WasJustPressed(Action);
	const AWWOPlayerController* PC = GetWWOController();
	return PC && !PC->IsMenuOpen() && PC->WasJustPressed(Action);
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

FVector AWWOCharacter::EyeLocation() const
{
	return Camera->GetComponentLocation();
}

// --- PvP target --------------------------------------------------------------------------------

void AWWOCharacter::UpdateHitboxes()
{
	const bool bOn = UWWOWorldSubsystem::IsPvP(this) && !Health->bDead;
	if (bOn != bHitboxesOn)
	{
		bHitboxesOn = bOn;
		const ECollisionEnabled::Type Mode = bOn ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision;
		BodyHitbox->SetCollisionEnabled(Mode);
		HeadHitbox->SetCollisionEnabled(Mode);
		// The movement capsule stops taking shots, so the hitboxes (and the head) get them.
		GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, bOn ? ECR_Ignore : ECR_Block);
	}
	if (bOn) BodyHitbox->SetCapsuleHalfHeight(FMath::Max(GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - 20.f, 35.f));
}

TArray<FWWOAimPart> AWWOCharacter::GetAimParts() const
{
	TArray<FWWOAimPart> Parts;
	Parts.Add({ HeadHitbox->GetComponentLocation(), HeadHitbox->GetUnscaledSphereRadius(), true });
	Parts.Add({ BodyHitbox->GetComponentLocation(), BodyHitbox->GetUnscaledCapsuleRadius(), false });
	return Parts;
}

bool AWWOCharacter::IsCriticalComponent(const UPrimitiveComponent* Component) const
{
	return Component == HeadHitbox;
}

bool AWWOCharacter::IsCriticalHit(const FHitResult& Hit, const FVector& From) const
{
	// The body hitbox is a loose capsule around the whole character, so a shot aimed at the head
	// often meets it first (a head leaning into a run, a shot from above or behind). What counts is
	// whether the shot's line goes through the head.
	if (Hit.GetComponent() == HeadHitbox) return true;
	if (!HeadHitbox->IsCollisionEnabled()) return false;
	const FVector Dir = (Hit.ImpactPoint - From).GetSafeNormal();
	if (Dir.IsNearlyZero()) return false;
	FHitResult Through;
	return HeadHitbox->LineTraceComponent(Through, Hit.ImpactPoint - Dir, Hit.ImpactPoint + Dir * HeadShotThrough,
		FCollisionQueryParams(SCENE_QUERY_STAT(WWOHeadShot), false));
}

void AWWOCharacter::PinHeadHitbox()
{
	// The head hitbox rides the body's head bone, like the enemies' crit boxes, so it is where the
	// head is drawn in every pose (running lean, crouch, slide, hit reactions) and not at the eye
	// point on the capsule's axis. Placed from the reference pose, so it doesn't depend on the pose
	// the body happens to be in right now.
	static const FName HeadBone("head");
	USkeletalMeshComponent* BodyMesh = GetMesh();
	const USkeletalMesh* Asset = BodyMesh ? BodyMesh->GetSkeletalMeshAsset() : nullptr;
	const int32 Bone = Asset ? Asset->GetRefSkeleton().FindBoneIndex(HeadBone) : INDEX_NONE;
	if (Bone == INDEX_NONE)
	{
		UE_LOG(LogWWO, Warning, TEXT("%s: no head bone, the head hitbox stays at the eyes"), *GetName());
		return;
	}
	// The mesh faces +Y in its own space (it's turned -90 on the capsule) with Z up.
	const FTransform Ref = FAnimationRuntime::GetComponentSpaceTransformRefPose(Asset->GetRefSkeleton(), Bone);
	const FVector Local = Ref.InverseTransformPosition(Ref.GetLocation() + FVector(0, HeadHitForward, HeadHitUp));
	HeadHitbox->AttachToComponent(BodyMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, HeadBone);
	HeadHitbox->SetRelativeLocation(Local);
}

void AWWOCharacter::ProbeHeadShots(int32& Shots, int32& Crits, int32& OldCrits, float& HeadOffEyes) const
{
	if (!bHitboxesOn) return;
	const FVector Head = GetMesh()->GetSocketLocation("head") + FVector(0, 0, HeadHitUp) + GetActorForwardVector() * HeadHitForward;
	const FVector Eyes = Camera->GetComponentLocation();
	HeadOffEyes = FMath::Max(HeadOffEyes, FVector::Dist(Head, Eyes));
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOHeadProbe), false);
	for (int32 Side = 0; Side < 8; ++Side)
	{
		for (const float Up : { 0.f, 20.f })
		{
			const FVector Away = FRotator(Up, GetActorRotation().Yaw + Side * 45.f, 0.f).Vector();
			const FVector From = Head + Away * 1500.f;
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Away).GetSafeNormal();
			for (const FVector2D& Off : { FVector2D(0, 0), FVector2D(6, 0), FVector2D(-6, 0), FVector2D(0, 6), FVector2D(0, -6) })
			{
				const FVector At = Head + Right * Off.X + FVector::UpVector * Off.Y;
				const FVector To = From + (At - From).GetSafeNormal() * 2000.f;
				FHitResult BodyHit, HeadHit;
				const bool bBody = BodyHitbox->LineTraceComponent(BodyHit, From, To, Params);
				const bool bHead = HeadHitbox->LineTraceComponent(HeadHit, From, To, Params);
				++Shots;
				if (bBody || bHead)
				{
					const FHitResult& First = bHead && (!bBody || HeadHit.Distance <= BodyHit.Distance) ? HeadHit : BodyHit;
					Crits += IsCriticalHit(First, From) ? 1 : 0;
				}
				// Before: a 16 cm sphere at the eyes, and only if the shot met it before the body.
				const FVector Dir = (To - From).GetSafeNormal();
				const float Along = FVector::DotProduct(Eyes - From, Dir);
				const float OffSq = FVector::DistSquared(From + Dir * Along, Eyes);
				if (OffSq <= 256.f && (!bBody || Along - FMath::Sqrt(256.f - OffSq) <= BodyHit.Distance)) ++OldCrits;
			}
		}
	}
}

bool AWWOCharacter::TakeHit(float Damage, bool bCritical, const FVector& Point, FLinearColor Color)
{
	if (Health->bDead) return false;
	UWWOWorldSubsystem::Get(this)->SpawnDamageNumber(Damage, bCritical, Point, Color);
	if (!HasAuthority()) return false; // a joiner's hit: the number now; the host applies it (health replicates)
	Health->LastAttacker = UWWOWorldSubsystem::CurrentAttacker;
	const AActor* By = UWWOWorldSubsystem::CurrentAttacker.Get();
	Health->DamageCameFrom(By && By != this ? By->GetActorLocation() : Point); // whoever did it, else where it landed
	Health->TakeDamage(Damage);
	return Health->bDead;
}

bool AWWOCharacter::IsDead() const { return Health->bDead; }
float AWWOCharacter::GetHealth() const { return Health->Health; }
float AWWOCharacter::GetMaxHealth() const { return Health->MaxHealth(); }
float AWWOCharacter::GetShield() const { return Health->Shield; }
float AWWOCharacter::GetMaxShield() const { return Health->MaxShield(); }

FVector AWWOCharacter::FeetLocation() const
{
	return GetActorLocation() - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
}

FVector AWWOCharacter::AimForward() const
{
	// The camera looks up by the reticle drop; the aim is that much below its center.
	if (AppliedReticleDrop <= 0.f || bUlting) return Camera->GetForwardVector();
	const FQuat Down = FRotator(-FMath::RadiansToDegrees(AppliedReticleDrop), 0.f, 0.f).Quaternion();
	return (Camera->GetComponentQuat() * Down).GetForwardVector();
}

float AWWOCharacter::ReticleAngleForFov(float HorizontalFovDeg) const
{
	// A point Drop of the screen height below center is atan(2 * Drop * tan(vertical half FOV))
	// below the view axis; the FOV is horizontal, so the vertical one comes from the aspect.
	if (bUlting || ReticleDrop <= 0.f) return 0.f;
	FVector2D Viewport(16.f, 9.f);
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(Viewport);
	const float TanHalfV = FMath::Tan(FMath::DegreesToRadians(HorizontalFovDeg) * 0.5f) * Viewport.Y / FMath::Max(Viewport.X, 1.f);
	return FMath::Atan(2.f * ReticleDrop * TanHalfV);
}

float AWWOCharacter::ReticleDropAngle() const
{
	// For the world camera's FOV, so the reticle keeps its place on screen as ADS zooms in.
	return ReticleAngleForFov(Camera->FieldOfView);
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
	// Godot sets camera.rotation.x directly, so a shot's kick shows (and aims the next shot)
	// this frame instead of at the next UpdateCamera.
	if (!bUlting)
	{
		const FRotator CameraRotation = Camera->GetRelativeRotation();
		Camera->SetRelativeRotation(FRotator(FMath::RadiansToDegrees(Pitch + AppliedReticleDrop), CameraRotation.Yaw, CameraRotation.Roll));
	}
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

void AWWOCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateHitboxes(); // PvP hitboxes on or off, on every machine
	UpdateTeamTint();
	// The movement component has already run PrepareMovement and moved (it ticks first);
	// this runs the rest in Godot's order: camera (after the move), gun, abilities, grenades, ult.
	if (!IsLocallyControlled())
	{
		// Someone else's character: the host runs their health and the world side of their kit;
		// everyone shows their body.
		if (HasAuthority()) TickRemoteOnHost(DeltaSeconds);
		return;
	}
	if (PreparedFrame != GFrameCounter) PrepareMovement(DeltaSeconds); // movement didn't tick this frame
	UpdateReviveInput();
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

void AWWOCharacter::UpdateModel()
{
	// Pick the body animation from what you're doing. Playback speeds up or slows to match the
	// speed (m/s) each animation looks right at.
	constexpr float WalkAnimSpeed = 1.6f, JogAnimSpeed = 4.5f, SprintAnimSpeed = 7.f, CrouchAnimSpeed = 1.5f;
	const float Speed = GetCharacterMovement()->Velocity.Size2D() / WWO::M;
	if (Health->bDead)
	{
		// Down (online: until a teammate revives you). Once: replaying a finished clip starts it over.
		if (!bPosedDown) Body->Play("Death01", 1.f, 0.1f);
		bPosedDown = true;
		return;
	}
	bPosedDown = false;
	if (bMantling)
	{
		Body->Play("Jump_Start");
	}
	else if (bSliding)
	{
		// Drop into the slide (Slide_Start), then hold the sliding pose (Slide, looping).
		const FName Now = Body->CurrentAnim();
		if (Now != "Slide_Start" && Now != "Slide")
		{
			Body->Play("Slide_Start", 1.6f, 0.08f);
			Body->Queue("Slide");
		}
	}
	else if (!IsOnFloor()) Body->Play("Jump");
	else if (bCrouching)
	{
		if (Speed > 0.3f) Body->Play("Crouch_Fwd", FMath::Clamp(Speed / CrouchAnimSpeed, 0.6f, 2.f));
		else Body->Play("Crouch_Idle");
	}
	else if (Speed < 0.3f) Body->Play("Idle");
	else if (bSprinting) Body->Play("Sprint", FMath::Clamp(Speed / SprintAnimSpeed, 0.6f, 2.f));
	else if (Speed > 3.f) Body->Play("Jog_Fwd", FMath::Clamp(Speed / JogAnimSpeed, 0.6f, 2.f));
	else Body->Play("Walk", FMath::Clamp(Speed / WalkAnimSpeed, 0.6f, 2.f));
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
		return;
	}
	if (MoveIn.Pressed & MoveLaunch) ApplyLaunch(MoveIn.Push);
	if (MoveIn.Held & MoveFrozen)
	{
		if (bLungeFlight) EndLungeFlight();
		bSprinting = false;
		bSliding = false;
		UpdateAir(DeltaSeconds);
		WishVelocity = FVector::ZeroVector;
		return;
	}
	const FVector2D Input = MoveIn.Move;
	const FVector WishDir = WishDirection(Input);

	if (bMantling || TryMantle(Input))
	{
		if (bLungeFlight) EndLungeFlight();
		UpdateMantle(DeltaSeconds);
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
		bLungeFlight = true;
		LungeVelocity = MoveIn.Push;
		if (Move->MovementMode != MOVE_Flying) Move->SetMovementMode(MOVE_Flying);
	}
	else if (bLungeFlight) EndLungeFlight();
	if (!IsOnFloor()) LastFallSpeed = -Move->Velocity.Z / WWO::M;
}

FWWOMoveState AWWOCharacter::GetMoveState() const
{
	FWWOMoveState S;
	S.bSprinting = bSprinting; S.bCrouching = bCrouching; S.bSliding = bSliding; S.bCrouchToggled = bCrouchToggled;
	S.bSlideQueued = bSlideQueued; S.bCrouchWasHeld = bCrouchWasHeld; S.bLandedSprinting = bLandedSprinting; S.bMantling = bMantling;
	S.JumpsUsed = (uint8)FMath::Clamp(JumpsUsed, 0, 255);
	S.bLungeFlight = bLungeFlight;
	S.TakeoffSpeed = TakeoffSpeed; S.SlideSpeedNow = SlideSpeedNow; S.LaunchTime = LaunchTime; S.AirTime = AirTime;
	S.SinceLanded = SinceLanded; S.MantleT = MantleT;
	S.SlideDir = SlideDir; S.MantleFrom = MantleFrom; S.MantleTo = MantleTo;
	return S;
}

void AWWOCharacter::SetMoveState(const FWWOMoveState& S)
{
	bSprinting = S.bSprinting; bCrouching = S.bCrouching; bSliding = S.bSliding; bCrouchToggled = S.bCrouchToggled;
	bSlideQueued = S.bSlideQueued; bCrouchWasHeld = S.bCrouchWasHeld; bLandedSprinting = S.bLandedSprinting; bMantling = S.bMantling;
	JumpsUsed = S.JumpsUsed;
	bLungeFlight = S.bLungeFlight;
	TakeoffSpeed = S.TakeoffSpeed; SlideSpeedNow = S.SlideSpeedNow; LaunchTime = S.LaunchTime; AirTime = S.AirTime;
	SinceLanded = S.SinceLanded; MantleT = S.MantleT;
	SlideDir = S.SlideDir; MantleFrom = S.MantleFrom; MantleTo = S.MantleTo;
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
	Ar << JumpsUsed << TakeoffSpeed << SlideSpeedNow << LaunchTime << AirTime << SinceLanded << MantleT;
	Ar << SlideDir << MantleFrom << MantleTo;
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
	return FString::Printf(TEXT("sprint %d crouch %d slide %d toggled %d queued %d held %d landsprint %d mantle %d jumps %d takeoff %.2f slidespeed %.2f air %.2f landed %.2f"),
		bSprinting, bCrouching, bSliding, bCrouchToggled, bSlideQueued, bCrouchWasHeld, bLandedSprinting, bMantling, JumpsUsed, TakeoffSpeed, SlideSpeedNow, AirTime, SinceLanded);
}

TArray<float> AWWOCharacter::LocalMoveStats() const
{
	// The order ServerSetMoveStats reads them in.
	return {
		M("walk_speed", WalkSpeed), M("sprint_speed", SprintSpeed), M("crouch_speed", CrouchSpeed), M("slide_speed", SlideSpeed),
		M("slide_distance", SlideDistance), M("jump_height", JumpHeight1), M("jump_height", JumpHeight2), M("air_control", AirControl),
		AscentGravity, DescentGravity, ApexHang, CoyoteTime, MantleReach, MantleTime,
	};
}

TArray<float> AWWOCharacter::BaseMoveStats() const
{
	return { WalkSpeed, SprintSpeed, CrouchSpeed, SlideSpeed, SlideDistance, JumpHeight1, JumpHeight2, AirControl,
		AscentGravity, DescentGravity, ApexHang, CoyoteTime, MantleReach, MantleTime };
}

void AWWOCharacter::ApplyMoveBase(const TArray<float>& V)
{
	if (V.Num() < 14) return;
	WalkSpeed = V[0]; SprintSpeed = V[1]; CrouchSpeed = V[2]; SlideSpeed = V[3];
	SlideDistance = V[4]; JumpHeight1 = V[5]; JumpHeight2 = V[6]; AirControl = V[7];
	AscentGravity = V[8]; DescentGravity = V[9]; ApexHang = V[10]; CoyoteTime = V[11]; MantleReach = V[12]; MantleTime = V[13];
}

void AWWOCharacter::ClientSetMoveBase_Implementation(const TArray<float>& V)
{
	ApplyMoveBase(V); // M() then adds this player's own loadout, as the host's copy does
}

void AWWOCharacter::ServerSetMoveStats_Implementation(const TArray<float>& V)
{
	if (UWWOWorldSubsystem::IsPvP(this)) return; // PvP: the host's settings and the player's loadout, not their word
	if (V.Num() < 14) return;
	WalkSpeed = V[0]; SprintSpeed = V[1]; CrouchSpeed = V[2]; SlideSpeed = V[3];
	SlideDistance = V[4]; JumpHeight1 = V[5]; JumpHeight2 = V[6]; AirControl = V[7];
	AscentGravity = V[8]; DescentGravity = V[9]; ApexHang = V[10]; CoyoteTime = V[11]; MantleReach = V[12]; MantleTime = V[13];
	bNetStats = true; // M() now returns these as they are
}

void AWWOCharacter::UpdateSprint(const FVector2D& Input)
{
	// Press once to sprint; it stays on until you stop moving or crouch. Aiming and firing
	// stop a sprint and block starting one. Sprinting from a crouch stands you up.
	const bool bAiming = MoveIn.Held & MoveAim, bFiring = MoveIn.Held & MoveFire;
	if ((MoveIn.Pressed & MoveSprint) && !bSliding && !bAiming && !bFiring)
	{
		bSprinting = true;
		bCrouchToggled = false;
	}
	if (Input.Size() < 0.1f || bAiming || bFiring)
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
	const bool bCrouchHeld = MoveIn.Held & MoveCrouch;
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
	// Goes out with the next move (MoveLaunch), so the host launches you in the same move.
	PendingLaunch = NewVelocityCm;
	bLaunchPending = true;
}

void AWWOCharacter::ApplyLaunch(const FVector& NewVelocityCm)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->Velocity = NewVelocityCm;
	Move->SetMovementMode(MOVE_Falling);
	TakeoffSpeed = FVector2D(NewVelocityCm.X, NewVelocityCm.Y).Size() / WWO::M;
	LaunchTime = 0.15f;
	bSliding = false;
	bSprinting = false;
}

void AWWOCharacter::EndLungeFlight()
{
	// The lunge is over: stop dead and let gravity take over (this move's stick too, like Godot's velocity = ZERO).
	bLungeFlight = false;
	LungeVelocity = FVector::ZeroVector;
	GetCharacterMovement()->Velocity = FVector::ZeroVector;
	WishVelocity = FVector::ZeroVector;
	GetCharacterMovement()->SetMovementMode(MOVE_Falling);
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
	if (!(MoveIn.Pressed & MoveJump) || JumpsUsed >= MaxJumps + MoveIn.ExtraJumps) return;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const float Height = M("jump_height", JumpsUsed == 0 ? JumpHeight1 : JumpHeight2);
	Move->Velocity.Z = LaunchSpeed(Height) * WWO::M;
	Move->SetMovementMode(MOVE_Falling);
	JumpsUsed += 1;
	UWWOSound::PlayFor(this, "jump");
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

void AWWOCharacter::ApplyHorizontal(const FVector& WishDir, bool bOnFloor, float DeltaSeconds)
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
	Speed *= MoveIn.SpeedScale; // abilities x aiming x slow, from the move input
	if (!bOnFloor || LaunchTime > 0.f)
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
	SinceLanded = 0.f;
	bLandedSprinting = bSprinting || TakeoffSpeed > M("walk_speed", WalkSpeed) * 1.05f;
	if (bSlideQueued)
	{
		StartSlide(WishDirection(MoveIn.Move));
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
	// Godot's ult/entrench branch returns before any camera update, so the eye, dip and bob
	// hold still while you're frozen or rooted.
	const bool bFrozen = bUlting || EntrenchLeft > 0.f;
	if (bFrozen) DeltaSeconds = 0.f;
	// Eye height eases between standing and crouched.
	const float Target = bIsCrouched ? CrouchCameraZ : StandCameraZ;
	CameraBaseZ = FMath::Lerp(CameraBaseZ, Target, FMath::Min(CameraSmoothing * DeltaSeconds, 1.f));

	// Landing dip: the camera drops quickly toward the kick while the kick eases back.
	LandOffset = FMath::Lerp(LandOffset, LandKick, FMath::Min(25.f * DeltaSeconds, 1.f));
	LandKick = FMath::Lerp(LandKick, 0.f, FMath::Min(8.f * DeltaSeconds, 1.f));

	// Head bob: up/down once per step, side to side once per two steps.
	const float Speed = FVector2D(GetVelocity().X, GetVelocity().Y).Size() / WWO::M;
	// Godot skips the head bob while mantling, so the bob holds where it was until you're up.
	const float BobDelta = bMantling ? 0.f : DeltaSeconds;
	const bool bWalking = IsOnFloor() && !bSliding && Speed > 0.5f;
	SprintBlend = WWO::MoveToward(SprintBlend, bSprinting ? 1.f : 0.f, 3.f * BobDelta);
	const float Stride = FMath::Max(FMath::Lerp(WalkStride, SprintStride, SprintBlend), 0.1f);
	const int32 StepBefore = FMath::FloorToInt(BobTime / UE_PI);
	if (bWalking) BobTime += Speed / Stride * UE_PI * BobDelta; // one step = half a sine wave
	if (FMath::FloorToInt(BobTime / UE_PI) != StepBefore && !bCrouching) UWWOSound::PlayFor(this, "footstep", bSprinting ? 1.f : 0.75f); // a foot comes down (yours flat, anyone else's from where they are)
	BobWeight = WWO::MoveToward(BobWeight, bWalking ? 1.f : 0.f, 4.f * BobDelta);
	const float Amount = BobIntensity * BobWeight * (1.f - 0.75f * Weapon->AdsAmount); // steadier when aimed
	const float Up = FMath::Lerp(WalkBobUp, SprintBobUp, SprintBlend) * Amount;
	const float Side = FMath::Lerp(WalkBobSide, SprintBobSide, SprintBlend) * Amount;
	if (!bUlting)
	{
		Camera->SetRelativeLocation(FVector(0.f, FMath::Cos(BobTime * 0.5f) * Side * WWO::M,
			CameraBaseZ + (LandOffset + FMath::Abs(FMath::Sin(BobTime)) * Up) * WWO::M));
		AppliedReticleDrop = ReticleDropAngle();
		Camera->SetRelativeRotation(FRotator(FMath::RadiansToDegrees(Pitch + AppliedReticleDrop), 0.f, Weapon->CameraRoll()));
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
	bSliding = false; // the next move flies it (MoveLunge)
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
	if (MantleReach <= 0.f || IsOnFloor() || bSliding || bLunging || Input.Y < 0.5f) return false;
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

	bMantling = true;
	MantleT = 0.f;
	bCrouchToggled = false;
	bCrouching = false;
	bSprinting = false;
	if (bIsCrouched) UnCrouch(); // grows from the feet, so the capsule center moves up
	MantleFrom = GetActorLocation();
	MantleTo = Stand;
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
		Land(4.f); // a small landing dip as you come over the top (and a queued slide starts)
	}
}

// --- Status --------------------------------------------------------------------------------

void AWWOCharacter::ApplySlow(float Amount, float Seconds)
{
	SlowAmount = FMath::Max(FMath::Clamp(Amount, 0.f, 0.95f), SlowLeft > 0.f ? SlowAmount : 0.f);
	SlowLeft = FMath::Max(SlowLeft, Seconds);
	if (HasAuthority() && !IsLocallyControlled() && GetNetMode() != NM_Standalone) ClientApplySlow(Amount, Seconds);
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
	AimAssist->NotifyShot(); // hidden assist: the after-shot window opens
	if (KickTime <= 0.f && Recovery <= 0.f)
	{
		ApplyRecoil(PitchRad, YawRad);
		AimAssist->SteerRecoil(FMath::Sqrt(PitchRad * PitchRad + YawRad * YawRad), true); // hides in the instant kick
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
		AimAssist->SteerRecoil(FMath::Sqrt(Step * Step + YawStep * YawStep), true); // hides in the kick
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
		AimAssist->SteerRecoil(Before - After, false); // hidden assist: the fall-back bends toward the target
		if (After <= 0.f) bRecovering = false;
		return;
	}
	// Free: what's left of the kick eases out exponentially.
	const float Before = RecoveryLeft;
	RecoveryLeft *= FMath::Exp(-DeltaSeconds * 3.f / RecoveryTime);
	SetViewRotation(FMath::Max(Pitch - (Before - RecoveryLeft), RecoilRest), YawNow);
	AimAssist->SteerRecoil(Before - RecoveryLeft, false);
	if (RecoveryLeft <= 0.0002f) bRecovering = false;
}
