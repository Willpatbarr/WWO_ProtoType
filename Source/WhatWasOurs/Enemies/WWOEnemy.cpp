#include "Enemies/WWOEnemy.h"
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
#include "NavigationPath.h"
#include "Engine/World.h"

namespace
{
	constexpr float CorpseTime = 3.f; // seconds the body stays before disappearing
	constexpr float EnemyGravity = 20.f; // m/s^2, as in Godot
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
}

UWWOEnemyMovement* AWWOEnemy::EnemyMovement() const
{
	return Cast<UWWOEnemyMovement>(GetCharacterMovement());
}

void AWWOEnemy::BeginPlay()
{
	const float Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	GetMesh()->SetRelativeLocationAndRotation(FVector(0, 0, -Half), FRotator(0, -90.f, 0));
	Body->ModelScale = ModelScale;
	Super::BeginPlay();
	SpawnTransform = GetActorTransform();
	if (UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(this)) WS->RegisterTarget(this);
	MaxHealth = Data() ? Data()->MaxHealth : 100.f;
	Health = MaxHealth;
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

bool AWWOEnemy::TakeHit(float Damage, bool bCritical, const FVector& Point, FLinearColor Color)
{
	if (bDead) return false;
	bAlerted = true; // getting shot from out of sight range still wakes it up
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
		Body->Revive();
	}
	SetActorHiddenInGame(false);
	OnReset();
}

void AWWOEnemy::Die()
{
	bDead = true;
	Health = 0.f;
	SetCollision(false);
	OnDeath();
	Body->PlayDeath(false);
	OnDied.Broadcast(this);
	CorpseLeft = CorpseTime;
	const UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(this);
	RespawnLeft = (bRespawns && WS && WS->RespawnTime > 0.f) ? FMath::Max(WS->RespawnTime, CorpseTime) : -1.f;
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
		Body->PlayDeath(true);
	}
}

void AWWOEnemy::ApplyBlind(float Seconds)
{
	BlindLeft = FMath::Max(BlindLeft, Seconds);
	UWWOWorldSubsystem::Get(this)->ShowText(TEXT("BLINDED"), false, FeetLocation() + FVector(0, 0, BodyHeight() + 30.f), FLinearColor(1.f, 0.95f, 0.6f));
}

void AWWOEnemy::ApplySlow(float Amount, float Seconds)
{
	SlowAmount = FMath::Max(FMath::Clamp(Amount, 0.f, 0.95f), SlowLeft > 0.f ? SlowAmount : 0.f);
	SlowLeft = FMath::Max(SlowLeft, Seconds);
	UWWOWorldSubsystem::Get(this)->ShowText(TEXT("SLOWED"), false, FeetLocation() + FVector(0, 0, BodyHeight() + 30.f), FLinearColor(0.5f, 0.65f, 1.f));
}

// --- Each frame ----------------------------------------------------------------------------

bool AWWOEnemy::TargetIsInvisible() const
{
	const AWWOCharacter* P = Cast<AWWOCharacter>(TargetActor.Get());
	return P && P->IsInvisible();
}

void AWWOEnemy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWWOEnemyMovement* Move = EnemyMovement();
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
	SlowLeft = FMath::Max(SlowLeft - DeltaSeconds, 0.f);
	if (SlowLeft > 0.f) Wish *= 1.f - SlowAmount;
	Move->Wish = FVector(Wish.X, Wish.Y, 0.f) * WWO::M;
	AfterMove(DeltaSeconds);
	Flash = WWO::MoveToward(Flash, 0.f, 6.f * DeltaSeconds);
	Body->SetFlash(Flash);
	Animate(DeltaSeconds);
}

void AWWOEnemy::Retarget(float DeltaSeconds)
{
	// Enemies go after whoever's closest: you or an AI teammate.
	RetargetLeft -= DeltaSeconds;
	const AWWOAlly* CurrentAlly = Cast<AWWOAlly>(TargetActor.Get());
	if (TargetActor.IsValid() && RetargetLeft > 0.f && !(CurrentAlly && CurrentAlly->IsDown())) return;
	RetargetLeft = 0.5f;
	UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(this);
	AWWOCharacter* Player = WS->GetPlayer();
	AActor* Best = Player;
	float BestDistance = Player ? FVector::Dist(GetActorLocation(), Player->GetActorLocation()) : TNumericLimits<float>::Max();
	if (Player && Player->IsInvisible()) BestDistance = TNumericLimits<float>::Max(); // veiled: teammates are fair game, you aren't
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

void AWWOEnemy::DamageTarget(float Amount, bool bMelee)
{
	if (AWWOCharacter* P = Cast<AWWOCharacter>(TargetActor.Get())) P->Health->TakeDamage(Amount, bMelee);
	else if (AWWOAlly* A = Cast<AWWOAlly>(TargetActor.Get())) A->TakeAllyDamage(Amount, bMelee);
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
	const float Current = GetActorRotation().Yaw;
	const float Target = Direction.Rotation().Yaw;
	const float Yaw = Current + FMath::FindDeltaAngleDegrees(Current, Target) * (1.f - FMath::Exp(-Rate * DeltaSeconds));
	SetActorRotation(FRotator(0.f, Yaw, 0.f));
}

FVector AWWOEnemy::PathDirection(const FVector& Goal, float DeltaSeconds)
{
	// Which way to walk to reach Goal, around walls, using the level's navmesh. With no
	// navmesh it's a straight line.
	PathAge += DeltaSeconds;
	FVector Straight = Goal - GetActorLocation();
	Straight.Z = 0.f;
	if (PathAge > 0.3f || FVector::Dist(Goal, PathGoal) > 150.f)
	{
		Path.Reset();
		if (UNavigationPath* NavPath = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), FeetLocation(), Goal, this))
		{
			if (NavPath->IsValid()) Path = NavPath->PathPoints;
		}
		PathAge = 0.f;
		PathGoal = Goal;
	}
	while (Path.Num() > 1)
	{
		const FVector ToNext = Path[0] - GetActorLocation();
		if (FVector2D(ToNext.X, ToNext.Y).Size() > 60.f) break;
		Path.RemoveAt(0); // reached that point
	}
	if (Path.Num() < 2) return Straight.GetSafeNormal();
	FVector Step = Path[0] - GetActorLocation();
	Step.Z = 0.f;
	return Step.Size() > 1.f ? Step.GetSafeNormal() : Straight.GetSafeNormal();
}

void AWWOEnemy::PinCrit(FName Bone, const FVector& WorldPoint, const FVector& HalfExtentCm)
{
	// Keeps the crit box at WorldPoint relative to Bone (measured now), so it follows that
	// body part through the animations.
	CritBox->SetBoxExtent(HalfExtentCm);
	CritBox->SetWorldLocationAndRotation(WorldPoint, GetActorRotation());
	CritBox->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepWorldTransform, Bone);
}
