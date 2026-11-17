#include "Enemies/WWOEnemy.h"
#include "Player/WWOProf.h"
#include "WhatWasOurs.h"
#include "Systems/WWOSound.h"
#include "FX/WWOFX.h"
#include "Game/WWOGameState.h"
#include "Net/UnrealNetwork.h"
#include "Characters/WWOBodyComponent.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
#include "Allies/WWOAlly.h"
#include "Flora/WWOSporeFog.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "AIController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "NavigationSystem.h"
#include "Engine/World.h"

namespace
{
	constexpr float CorpseTime = 3.f; // seconds the body stays before disappearing
	constexpr float EnemyGravity = 20.f; // m/s^2, as in Godot

	// Paths again every 0.3 s or when the goal has moved 1.5 m; a path point within 60 cm (flat, 60
	// itself included) is reached.
	const FWWOPathRules EnemyPath = []
	{
		FWWOPathRules R;
		R.RepathAfter = 0.3f;
		R.RepathMoved = 150.f;
		R.Reach = 60.f;
		R.bReachInclusive = true;
		return R;
	}();
}

UWWOEnemyMovement::UWWOEnemyMovement()
{
	GravityScale = EnemyGravity / 9.8f;
	MaxWalkSpeed = 3000.f;
	MaxAcceleration = 100000.f;
	BrakingDecelerationWalking = 0.f;
	GroundFriction = 0.f;
	bOrientRotationToMovement = false;
	MaxStepHeight = 30.f;
	SetWalkableFloorAngle(46.f);
	bUseFlatBaseForFloorChecks = true;
}

void UWWOEnemyMovement::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (!bSteering) return; // a leap or a knockback carries on by itself
	Velocity.X = WWO::MoveToward(Velocity.X, Wish.X, Accel * DeltaTime);
	Velocity.Y = WWO::MoveToward(Velocity.Y, Wish.Y, Accel * DeltaTime);
}

AWWOEnemy::AWWOEnemy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UWWOEnemyMovement>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationYaw = false;
	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// The movement capsule blocks the world and pawns but not shots: the hitboxes take those.
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	BodyHitbox = CreateDefaultSubobject<UCapsuleComponent>(TEXT("BodyHitbox"));
	BodyHitbox->SetupAttachment(Capsule);
	CritBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CritBox"));
	CritBox->SetupAttachment(Capsule);
	for (UPrimitiveComponent* Hitbox : { (UPrimitiveComponent*)BodyHitbox, (UPrimitiveComponent*)CritBox })
	{
		Hitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Hitbox->SetCollisionObjectType(ECC_Pawn);
		Hitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
		Hitbox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Hitbox->SetCanEverAffectNavigation(false);
	}

	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body = CreateDefaultSubobject<UWWOBodyComponent>(TEXT("Body"));
	Tags.Add("Enemy");
	// Online: the host runs the AI; everyone else gets the position, health and animation.
	bReplicates = true;
	SetReplicatingMovement(true);
}

void AWWOEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWWOEnemy, Health);
	DOREPLIFETIME(AWWOEnemy, MaxHealth);
	DOREPLIFETIME(AWWOEnemy, bDead);
}

UWWOEnemyMovement* AWWOEnemy::EnemyMovement() const
{
	return Cast<UWWOEnemyMovement>(GetCharacterMovement());
}

FVector AWWOEnemy::GetWish() const
{
	const UWWOEnemyMovement* Move = EnemyMovement();
	return Move && Move->bSteering ? Move->Wish : FVector::ZeroVector;
}

void AWWOEnemy::BeginPlay()
{
	const float Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	GetMesh()->SetRelativeLocationAndRotation(FVector(0, 0, -Half), FRotator(0, -90.f, 0));
	Body->ModelScale = ModelScale;
	Super::BeginPlay();
	SpawnTransform = GetActorTransform();
	if (UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(this)) WS->RegisterTarget(this);
	// A joiner's copy keeps what the host sent (an enemy already hurt when it first shows up).
	if (HasAuthority())
	{
		MaxHealth = Data() ? Data()->MaxHealth : 100.f;
		Health = MaxHealth;
	}
	Setup();
}

void AWWOEnemy::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(this)) WS->UnregisterTarget(this);
	Super::EndPlay(Reason);
}

FVector AWWOEnemy::FeetLocation() const
{
	return GetActorLocation() - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
}

// --- Target interface ---------------------------------------------------------------------

TArray<FWWOAimPart> AWWOEnemy::GetAimParts() const
{
	TArray<FWWOAimPart> Parts;
	if (CritBox->IsCollisionEnabled())
	{
		Parts.Add({ CritBox->GetComponentLocation(), CritRadius(), true });
	}
	Parts.Add({ FeetLocation() + FVector(0, 0, BodyHeight() * 0.5f), BodyRadius(), false });
	return Parts;
}

bool AWWOEnemy::IsCriticalComponent(const UPrimitiveComponent* Component) const
{
	return Component == CritBox;
}

bool AWWOEnemy::ApplyHit(float Damage, bool bCritical, const FVector& Point, FLinearColor Color)
{
	if (bDead) return false;
	if (!HasAuthority())
	{
		// A joiner's hit: the flash and number now; the host decides the damage (it replicates).
		Flash = 1.f;
		UWWOWorldSubsystem::Get(this)->SpawnDamageNumber(Damage, bCritical, Point, Color);
		return false;
	}
	bAlerted = true; // getting shot from out of sight range still wakes it up
	if (AWWOCharacter* By = UWWOWorldSubsystem::PlayerOf(UWWOWorldSubsystem::CurrentAttacker.Get())) LastHitBy = By;
	Flash = 1.f;
	const float Through = Absorb(Damage, bCritical, Point); // shields take their share first
	if (Through > 0.f)
	{
		UWWOWorldSubsystem::Get(this)->SpawnDamageNumber(Through, bCritical, Point, Color);
		Health -= Through;
	}
	if (Health <= 0.f)
	{
		Die();
		return true;
	}
	OnHit(bCritical);
	return false;
}

void AWWOEnemy::ResetTarget()
{
	// Back to its spawn point at full health, unaware of you.
	SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
	GetCharacterMovement()->StopMovementImmediately();
	EnemyMovement()->bSteering = true;
	MaxHealth = Data() ? Data()->MaxHealth : 100.f;
	Health = MaxHealth;
	bAlerted = false;
	SlowLeft = 0.f;
	BlindLeft = 0.f;
	CorpseLeft = -1.f;
	RespawnLeft = -1.f;
	if (bDead)
	{
		bDead = false;
		SetCollision(true);
	}
	SetActorHiddenInGame(false);
	OnReset();
	Body->Play("Idle", 1.f, 0.f);
}

void AWWOEnemy::Die()
{
	bDead = true;
	Health = 0.f;
	SetCollision(false);
	OnDeath();
	Body->Play("Death01", 1.f, 0.1f);
	OnDied.Broadcast(this);
	CorpseLeft = CorpseTime;
	const UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(this);
	RespawnLeft = (bRespawns && WS && WS->RespawnTime > 0.f) ? FMath::Max(WS->RespawnTime, CorpseTime) : -1.f;
}

void AWWOEnemy::OnRep_Dead()
{
	SetCollision(!bDead);
}

void AWWOEnemy::SetCollision(bool bOn)
{
	BodyHitbox->SetCollisionEnabled(bOn ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	CritBox->SetCollisionEnabled(bOn ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	// A corpse still rests on the floor, but you walk (and shoot) through it.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, bOn ? ECR_Block : ECR_Ignore);
}

void AWWOEnemy::KnockBack(const FVector& Push)
{
	// A shove that its own movement then works against. Bigger enemies move less.
	const float Heft = FMath::Pow(FMath::Max(ModelScale, 0.1f), 3.f);
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->Velocity.X += Push.X / Heft;
	Move->Velocity.Y += Push.Y / Heft;
	if (bDead && Push.Size() > 100.f)
	{
		// A killing blow knocks it clean off its feet, falling away from the hit.
		const FVector Away = FVector(Push.X, Push.Y, 0.f).GetSafeNormal();
		if (!Away.IsNearlyZero()) SetActorRotation(FRotator(0.f, (-Away).Rotation().Yaw, 0.f));
		Body->Play("Hit_Knockback", 1.f, 0.05f); // thrown onto its back instead of the usual collapse
	}
}

void AWWOEnemy::ApplyBlind(float Seconds)
{
	BlindLeft = FMath::Max(BlindLeft, Seconds);
	AWWOGameState::Text(GetWorld(), TEXT("BLINDED"), FeetLocation() + FVector(0, 0, BodyHeight() + 30.f), FLinearColor(1.f, 0.95f, 0.6f)); // everyone sees it
}

void AWWOEnemy::ApplySlow(float Amount, float Seconds)
{
	SlowAmount = FMath::Max(FMath::Clamp(Amount, 0.f, 0.95f), SlowLeft > 0.f ? SlowAmount : 0.f);
	SlowLeft = FMath::Max(SlowLeft, Seconds);
	AWWOGameState::Text(GetWorld(), TEXT("SLOWED"), FeetLocation() + FVector(0, 0, BodyHeight() + 30.f), FLinearColor(0.5f, 0.65f, 1.f));
}

// --- Each frame ----------------------------------------------------------------------------

bool AWWOEnemy::TargetIsInvisible() const
{
	const AWWOCharacter* P = Cast<AWWOCharacter>(TargetActor.Get());
	return P && P->IsInvisible();
}

void AWWOEnemy::Tick(float DeltaSeconds)
{
	WWO_PROF("Enemy");
	Super::Tick(DeltaSeconds);
	if (bDead != bDeathShown)
	{
		// On every machine, the moment it dies (bDead replicates): it goes out with a burst.
		bDeathShown = bDead;
		if (bDead) UWWOSound::Play(this, "enemy_death", GetActorLocation());
		if (bDead) AWWOFXActor::DeathBurst(GetWorld(), BodyCenter(), FLinearColor(1.f, 0.55f, 0.2f), GetBodyHeight() / 100.f * 0.8f);
	}
	if (!HasAuthority())
	{
		// Online, not the host: it moves and animates as the host's copy does; only the hit flash fades here.
		Flash = WWO::MoveToward(Flash, 0.f, 6.f * DeltaSeconds);
		Body->SetFlash(Flash);
		AnimateRemote(DeltaSeconds);
		return;
	}
	UWWOEnemyMovement* Move = EnemyMovement();
	// Off the map (walked or knocked off a ledge): it dies like a player would, so a wave isn't left
	// waiting on something falling forever.
	if (!bDead && GetActorLocation().Z < UWWOHealthComponent::MapKillHeight(GetWorld()))
	{
		UE_LOG(LogWWO, Display, TEXT("%s fell off the map"), *GetName());
		Die();
	}
	if (bDead)
	{
		Move->bSteering = true;
		Move->Wish = FVector::ZeroVector; // a knocked-back body slides to a stop
		if (CorpseLeft > 0.f && (CorpseLeft -= DeltaSeconds) <= 0.f) SetActorHiddenInGame(true);
		if (RespawnLeft > 0.f && (RespawnLeft -= DeltaSeconds) <= 0.f) ResetTarget();
		return;
	}
	Retarget(DeltaSeconds);
	FVector Wish = FVector::ZeroVector;
	if (TargetIsInvisible()) bAlerted = false; // veiled in spore fog: it loses track of you
	BlindLeft = FMath::Max(BlindLeft - DeltaSeconds, 0.f);
	bool bTargetAlive = TargetActor.IsValid();
	if (const AWWOCharacter* P = Cast<AWWOCharacter>(TargetActor.Get())) bTargetAlive = !P->Health->bDead;
	if (const AWWOAlly* A = Cast<AWWOAlly>(TargetActor.Get())) bTargetAlive = !A->IsDown();
	if (BlindLeft > 0.f)
	{
		StandDown(); // blinded: stands there, can't see or attack
	}
	else if (Data() && Data()->AiEnabled >= 0.5f && bTargetAlive && !TargetIsInvisible())
	{
		Wish = Think(DeltaSeconds);
	}
	else
	{
		StandDown(); // AI off, or the player is dead
	}
	Wish = Unstick(Wish, DeltaSeconds);
	SlowLeft = FMath::Max(SlowLeft - DeltaSeconds, 0.f);
	if (SlowLeft > 0.f) Wish *= 1.f - SlowAmount;
	Move->Wish = FVector(Wish.X, Wish.Y, 0.f) * WWO::M;
	AfterMove(DeltaSeconds);
	Flash = WWO::MoveToward(Flash, 0.f, 6.f * DeltaSeconds);
	Body->SetFlash(Flash);
	Animate(DeltaSeconds);
}

FVector AWWOEnemy::Unstick(const FVector& Wish, float DeltaSeconds)
{
	// Not in Godot: if it has kept trying to walk the same way for most of a second and isn't
	// getting anywhere (wedged on a corner, in a gap between crates, behind another enemy), it
	// steps off to one side for a moment, then carries on. Each try goes the other way.
	const float WishSize = Wish.Size2D();
	if (UnstickLeft > 0.f)
	{
		UnstickLeft -= DeltaSeconds;
		return WishSize > 0.5f ? UnstickDir * WishSize : Wish;
	}
	const FVector Dir = Wish.GetSafeNormal2D();
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	const bool bSameWay = FVector::DotProduct(Dir, LastWishDir) > 0.5f; // not flipping back and forth at a range edge
	LastWishDir = Dir;
	if (WishSize > 1.f && bSameWay && EnemyMovement()->bSteering && Move->IsMovingOnGround() && Move->Velocity.Size2D() / WWO::M < WishSize * 0.15f)
	{
		StuckFor += DeltaSeconds;
		if (StuckFor >= 0.75f)
		{
			StuckFor = 0.f;
			UnstickSide = -UnstickSide;
			UnstickDir = FVector::CrossProduct(Dir, FVector::UpVector) * UnstickSide;
			UnstickLeft = 0.6f;
		}
	}
	else
	{
		StuckFor = 0.f;
	}
	return Wish;
}

void AWWOEnemy::Retarget(float DeltaSeconds)
{
	// Enemies go after whoever's closest: a player (any of them, online) or an AI teammate.
	RetargetLeft -= DeltaSeconds;
	const AWWOAlly* CurrentAlly = Cast<AWWOAlly>(TargetActor.Get());
	const AWWOCharacter* CurrentPlayer = Cast<AWWOCharacter>(TargetActor.Get());
	const bool bTargetDown = (CurrentAlly && CurrentAlly->IsDown()) || (CurrentPlayer && CurrentPlayer->Health->bDead);
	if (TargetActor.IsValid() && RetargetLeft > 0.f && !bTargetDown && !TargetIsInvisible()) return; // a veiled target: look for someone else now
	RetargetLeft = 0.5f;
	UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(this);
	AActor* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (AWWOCharacter* Player : WS->GetPlayers())
	{
		if (Player->IsInvisible() || Player->Health->bDead) continue; // veiled: teammates are fair game, you aren't
		const float D = FVector::Dist(GetActorLocation(), Player->GetActorLocation());
		if (D < BestDistance)
		{
			Best = Player;
			BestDistance = D;
		}
	}
	if (!Best) Best = WS->GetPlayer(); // everyone down or veiled: keep facing someone
	for (AWWOAlly* Mate : WS->LivingTeammates())
	{
		const float D = FVector::Dist(GetActorLocation(), Mate->GetActorLocation());
		if (D < BestDistance)
		{
			Best = Mate;
			BestDistance = D;
		}
	}
	TargetActor = Best;
}

AWWOCharacter* AWWOEnemy::HurtFriendly(AActor* Victim, float Amount, const FVector& CameFrom, bool bMelee)
{
	if (AWWOCharacter* P = Cast<AWWOCharacter>(Victim))
	{
		P->Health->DamageCameFrom(CameFrom);
		P->Health->TakeDamage(Amount, bMelee);
		return P;
	}
	if (AWWOAlly* A = Cast<AWWOAlly>(Victim)) A->TakeAllyDamage(Amount, bMelee);
	return nullptr;
}

void AWWOEnemy::DamageTarget(float Amount, bool bMelee)
{
	HurtFriendly(TargetActor.Get(), Amount, GetActorLocation(), bMelee);
}

FVector AWWOEnemy::FlatToTarget() const
{
	if (!TargetActor.IsValid()) return FVector::ZeroVector;
	FVector To = TargetActor->GetActorLocation() - GetActorLocation();
	To.Z = 0.f;
	return To;
}

FVector AWWOEnemy::TargetAimPoint() const
{
	return TargetActor.IsValid() ? TargetActor->GetActorLocation() + FVector(0, 0, 30.f) : FVector::ZeroVector; // chest height
}

FCollisionQueryParams AWWOEnemy::IgnoreEnemies() const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOEnemySight), false, this);
	for (AWWOEnemy* Enemy : UWWOWorldSubsystem::Get(this)->LivingEnemies()) Params.AddIgnoredActor(Enemy);
	return Params;
}

bool AWWOEnemy::CanSeeTarget(const FVector& From) const
{
	if (!TargetActor.IsValid()) return false;
	const FVector To = TargetAimPoint();
	if (AWWOSporeFog::Blocks(GetWorld(), From, To)) return false; // thick spore fog in the way
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, IgnoreEnemies())) return true; // nothing in the way
	return Hit.GetActor() == TargetActor.Get() || Cast<AWWOAlly>(Hit.GetActor()) || Cast<AWWOCharacter>(Hit.GetActor());
}

void AWWOEnemy::TurnToward(const FVector& Direction, float DeltaSeconds, float Speed)
{
	if (Direction.Size2D() < 1.f) return;
	const float Rate = Speed < 0.f ? (Data() ? Data()->TurnSpeed : 8.f) : Speed;
	const float Target = Direction.Rotation().Yaw; // a float target: float maths
	SetActorRotation(FRotator(0.f, WWONav::TurnYaw(GetActorRotation().Yaw, Target, Rate, DeltaSeconds), 0.f));
}

FVector AWWOEnemy::PathDirection(const FVector& Goal, float DeltaSeconds)
{
	// Which way to walk to reach Goal, around walls, using the level's navmesh. With no
	// navmesh it's a straight line.
	FVector Straight = Goal - GetActorLocation();
	Straight.Z = 0.f;
	Path.Update(this, FeetLocation(), Goal, GetActorLocation(), DeltaSeconds, EnemyPath);
	const TArray<FVector>& Points = Path.Points;
	if (Points.Num() < 2)
	{
		// Godot heads straight for the goal from here. When the goal can't be reached on the
		// navmesh (you're up on a platform it has no way onto) and a wall or rail is right in
		// front, it would just press into it, so it stands at the closest spot instead.
		if (Path.bPartial && WallAhead(Straight)) return FVector::ZeroVector;
		return Straight.GetSafeNormal();
	}
	FVector Step = Points[0] - GetActorLocation();
	Step.Z = 0.f;
	Step = Step.Size() > 1.f ? Step.GetSafeNormal() : Straight.GetSafeNormal();
	// Something that isn't in the navmesh (a training target, a bark wall) can sit right on the
	// path. Walking square into it would stop it dead, so it sidesteps along its face instead
	// (keeping to one side until the way is clear).
	FVector Normal;
	if (WallAhead(Step, &Normal) && FVector::DotProduct(Step, -Normal) > 0.7f)
	{
		FVector Along = Step - Normal * FVector::DotProduct(Step, Normal);
		if (Along.Size() < 0.2f) Along = FVector::CrossProduct(Normal, FVector::UpVector) * SidestepSide;
		SidestepSide = FVector::DotProduct(Along, FVector::CrossProduct(Normal, FVector::UpVector)) >= 0.f ? 1.f : -1.f;
		return Along.GetSafeNormal();
	}
	return Step;
}

FString AWWOEnemy::PathDebug() const
{
	FString Out = FString::Printf(TEXT("path %d points%s"), Path.Points.Num(), Path.bPartial ? TEXT(" (partial)") : TEXT(""));
	for (const FVector& Point : Path.Points) Out += TEXT(" ") + Point.ToCompactString();
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (Path.Points.Num() == 0 && Nav)
	{
		// Why there's no path.
		FNavLocation Near;
		Out += FString::Printf(TEXT(" (start %s, goal %s %s)"), Nav->ProjectPointToNavigation(FeetLocation(), Near) ? TEXT("on navmesh") : TEXT("OFF navmesh"),
			*Path.Goal.ToCompactString(), Nav->ProjectPointToNavigation(Path.Goal, Near) ? TEXT("on navmesh") : TEXT("OFF navmesh"));
	}
	return Out;
}

bool AWWOEnemy::LedgeAhead(const FVector& Direction) const
{
	// A step that way (its radius + 70 cm), is there floor within a short drop (150 cm below its feet)?
	// Level geometry only.
	const FVector Step = GetActorLocation() + Direction.GetSafeNormal2D() * (GetCapsuleComponent()->GetScaledCapsuleRadius() + 70.f);
	const float Feet = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	return WWONav::LedgeAhead(GetWorld(), FWWOProbe(), Step, Feet + 150.f, FCollisionQueryParams(SCENE_QUERY_STAT(WWOEnemyLedge), false, this));
}

float AWWOEnemy::HeightToTarget() const
{
	if (!TargetActor.IsValid()) return 0.f;
	const IWWOTargetable* T = Cast<IWWOTargetable>(TargetActor.Get());
	const float TheirFeet = T ? T->FeetLocation().Z : TargetActor->GetActorLocation().Z - 90.f;
	return FMath::Abs(TheirFeet - FeetLocation().Z);
}

bool AWWOEnemy::WallAhead(const FVector& Direction, FVector* OutNormal) const
{
	// Level geometry (not people) within a step of its capsule along Direction: its capsule 15 cm
	// shorter, from 10 cm up (clear of the floor), swept 30 cm; the first hit (for its normal).
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	FWWOProbe Probe;
	Probe.Radius = Capsule->GetScaledCapsuleRadius();
	Probe.HalfHeight = Capsule->GetScaledCapsuleHalfHeight() - 15.f;
	return WWONav::WallAhead(GetWorld(), Probe, GetActorLocation() + FVector(0, 0, 10.f), Direction.GetSafeNormal2D() * 30.f,
		FCollisionQueryParams(SCENE_QUERY_STAT(WWOEnemyWall), false, this), OutNormal);
}

void AWWOEnemy::PinCrit(FName Bone, const FVector& WorldPoint, const FVector& HalfExtentCm)
{
	// Keeps the crit box at WorldPoint relative to Bone (measured now), so it follows that
	// body part through the animations.
	CritBox->SetBoxExtent(HalfExtentCm);
	CritBox->SetWorldLocationAndRotation(WorldPoint, GetActorRotation());
	CritBox->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepWorldTransform, Bone);
}
