#include "Allies/WWOAlly.h"
#include "Enemies/WWOEnemy.h"
#include "Characters/WWOBodyComponent.h"
#include "Player/WWOCharacter.h"
#include "FX/WWOFX.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Engine/World.h"

AWWOAlly::AWWOAlly(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UWWOEnemyMovement>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationYaw = false;
	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	GetCapsuleComponent()->InitCapsuleSize(40.f, 100.f);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (UWWOEnemyMovement* Move = Cast<UWWOEnemyMovement>(GetCharacterMovement())) Move->Accel = 1400.f;
	Body = CreateDefaultSubobject<UWWOBodyComponent>(TEXT("Body"));
	Body->BodyColor = FLinearColor(0.25f, 0.7f, 0.65f); // friendly teal
	Body->JointColor = FLinearColor(0.12f, 0.14f, 0.16f);
	Body->AnimSet = EWWOAnimSet::Rifle;
	Tags.Add("Teammate");
}

UWWOAllyData* AWWOAlly::D() const
{
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	return L ? L->AllyData.Get() : nullptr;
}

void AWWOAlly::BeginPlay()
{
	GetMesh()->SetRelativeLocationAndRotation(FVector(0, 0, -100.f), FRotator(0, -90.f, 0));
	Super::BeginPlay();
	if (UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(this)) WS->RegisterTeammate(this);
	Health = D()->MaxHealth;
	Shield = D()->MaxShield;
	// A gray-box rifle in the right hand, pointing where it faces.
	Gun = NewObject<USceneComponent>(this, TEXT("Gun"));
	Gun->SetupAttachment(GetRootComponent());
	Gun->SetUsingAbsoluteRotation(true);
	Gun->RegisterComponent();
	UStaticMeshComponent* Barrel = WWOFX::AddShape(this, Gun, WWOFX::Cube(), TEXT("M_WWO_Solid"), FLinearColor(0.15f, 0.15f, 0.17f));
	Barrel->SetRelativeScale3D(FVector(0.45f, 0.06f, 0.09f));
	Barrel->SetRelativeLocation(FVector(15.f, 0, 0));
}

void AWWOAlly::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(this)) WS->UnregisterTeammate(this);
	Super::EndPlay(Reason);
}

void AWWOAlly::TakeAllyDamage(float Amount, bool bMelee)
{
	if (bDown || Amount <= 0.f) return;
	SinceHit = 0.f;
	const float ToShield = FMath::Min(Amount, Shield);
	Shield -= ToShield;
	Health -= Amount - ToShield;
	if (Health <= 0.f) GoDown();
}

void AWWOAlly::RestoreShield(float Amount)
{
	// Shield Share: an explosion of yours refilled this teammate's shields.
	if (bDown) return;
	const float Before = Shield;
	Shield = FMath::Min(Shield + Amount, D()->MaxShield);
	if (Shield > Before)
	{
		UWWOWorldSubsystem::Get(this)->ShowText(FString::Printf(TEXT("+%d"), FMath::RoundToInt(Shield - Before)), false,
			GetActorLocation() + FVector(0, 0, 100.f), FLinearColor(0.55f, 0.8f, 1.f));
	}
}

void AWWOAlly::GoDown()
{
	bDown = true;
	Health = 0.f;
	Shield = 0.f;
	DownLeft = D()->RespawnTime;
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Body->PlayDeath(false);
	Gun->SetVisibility(false, true);
}

void AWWOAlly::GetUp()
{
	bDown = false;
	Health = D()->MaxHealth;
	Shield = D()->MaxShield;
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Gun->SetVisibility(true, true);
	if (const AWWOCharacter* P = UWWOWorldSubsystem::Get(this)->GetPlayer())
	{
		TeleportTo(P->GetActorLocation() + P->GetActorRightVector() * 200.f, GetActorRotation());
	}
	GetCharacterMovement()->StopMovementImmediately();
	Body->Revive();
}

void AWWOAlly::ResetAlly()
{
	// The "Reset targets and enemies" button brings teammates back too.
	if (bDown) GetUp();
	Health = D()->MaxHealth;
	Shield = D()->MaxShield;
}

FString AWWOAlly::StatusText() const
{
	return bDown ? FString::Printf(TEXT("DOWN %d"), FMath::CeilToInt(DownLeft))
		: FString::Printf(TEXT("S %d  H %d"), FMath::CeilToInt(Shield), FMath::CeilToInt(Health));
}

void AWWOAlly::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWWOEnemyMovement* Move = Cast<UWWOEnemyMovement>(GetCharacterMovement());
	const UWWOAllyData* Data = D();
	if (bDown)
	{
		DownLeft -= DeltaSeconds;
		Move->Wish = FVector::ZeroVector;
		if (DownLeft <= 0.f) GetUp();
		return;
	}
	Regen(DeltaSeconds);
	FVector Wish = FVector::ZeroVector;
	AWWOEnemy* Enemy = Data->AiEnabled >= 0.5f ? NearestVisibleEnemy() : nullptr;
	if (Data->AiEnabled >= 0.5f && UWWOWorldSubsystem::Get(this)->GetPlayer()) Wish = Follow(DeltaSeconds) * Data->MoveSpeed;
	if (Enemy)
	{
		Face(Enemy->GetActorLocation() - GetActorLocation(), DeltaSeconds);
		FireCooldown -= DeltaSeconds;
		if (FireCooldown <= 0.f)
		{
			FireCooldown = Data->FireInterval * FMath::FRandRange(0.85f, 1.2f);
			Shoot(Enemy);
		}
	}
	else if (Wish.Size() > 0.1f)
	{
		Face(Wish, DeltaSeconds);
	}
	Move->Wish = Wish * WWO::M;
	// The gun sits in the right hand, pointing where it faces.
	Gun->SetWorldLocationAndRotation(GetMesh()->GetSocketLocation("hand_r"), FRotator(0.f, GetActorRotation().Yaw, 0.f));
}

void AWWOAlly::Regen(float DeltaSeconds)
{
	const UWWOAllyData* Data = D();
	SinceHit += DeltaSeconds;
	if (SinceHit < Data->RechargeDelay) return;
	if (Shield < Data->MaxShield) Shield = FMath::Min(Shield + Data->ShieldRegen * DeltaSeconds, Data->MaxShield);
	else if (Health < Data->MaxHealth) Health = FMath::Min(Health + Data->HealthRegen * DeltaSeconds, Data->MaxHealth);
}

FVector AWWOAlly::Follow(float DeltaSeconds)
{
	const UWWOAllyData* Data = D();
	const AWWOCharacter* P = UWWOWorldSubsystem::Get(this)->GetPlayer();
	FVector To = P->GetActorLocation() - GetActorLocation();
	To.Z = 0.f;
	const float Distance = To.Size() / WWO::M;
	if (Distance < Data->FollowMin) return -To.GetSafeNormal() * 0.5f; // give you some room
	if (Distance <= Data->FollowMax) return FVector::ZeroVector;
	// Walk back toward you, around walls.
	PathAge += DeltaSeconds;
	if (PathAge > 0.4f || Path.Num() < 2)
	{
		Path.Reset();
		if (UNavigationPath* NavPath = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), GetActorLocation(), P->GetActorLocation(), this))
		{
			if (NavPath->IsValid()) Path = NavPath->PathPoints;
		}
		PathAge = 0.f;
	}
	while (Path.Num() > 1 && FVector2D(Path[0].X - GetActorLocation().X, Path[0].Y - GetActorLocation().Y).Size() < 60.f) Path.RemoveAt(0);
	if (Path.Num() >= 2)
	{
		FVector Step = Path[0] - GetActorLocation();
		Step.Z = 0.f;
		if (Step.Size() > 1.f) return Step.GetSafeNormal();
	}
	return To.GetSafeNormal();
}

void AWWOAlly::Face(const FVector& Direction, float DeltaSeconds)
{
	if (Direction.Size2D() < 1.f) return;
	const float Current = GetActorRotation().Yaw;
	const float Yaw = Current + FMath::FindDeltaAngleDegrees(Current, Direction.Rotation().Yaw) * (1.f - FMath::Exp(-10.f * DeltaSeconds));
	SetActorRotation(FRotator(0.f, Yaw, 0.f));
}

AWWOEnemy* AWWOAlly::NearestVisibleEnemy() const
{
	AWWOEnemy* Best = nullptr;
	float BestDistance = D()->SightRange * WWO::M;
	const FVector Eyes = GetActorLocation() + FVector(0, 0, 60.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOAllySight), false, this);
	for (AWWOEnemy* Enemy : UWWOWorldSubsystem::Get(this)->LivingEnemies())
	{
		const FVector Aim = Enemy->GetAimParts().Last().Position;
		const float Distance = FVector::Dist(Eyes, Aim);
		if (Distance >= BestDistance) continue;
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Eyes, Aim, ECC_Visibility, Params) || Hit.GetActor() != Enemy) continue;
		Best = Enemy;
		BestDistance = Distance;
	}
	return Best;
}

void AWWOAlly::Shoot(AWWOEnemy* Enemy)
{
	const UWWOAllyData* Data = D();
	const FVector From = Gun->GetComponentTransform().TransformPosition(FVector(40.f, 0, 0));
	const FVector Aim = Enemy->GetAimParts().Last().Position;
	const FVector Dir = WWO::RandomInCone((Aim - From).GetSafeNormal(), FMath::DegreesToRadians(Data->Spread));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOAllyShot), false, this);
	FHitResult Hit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, From, From + Dir * (Data->SightRange + 5.f) * WWO::M, ECC_Visibility, Params);
	AWWOFXActor::Tracer(GetWorld(), From, bHit ? Hit.ImpactPoint : From + Dir * Data->SightRange * WWO::M, FLinearColor(0.5f, 1.f, 0.9f, 0.8f));
	if (bHit)
	{
		if (AWWOEnemy* Victim = Cast<AWWOEnemy>(Hit.GetActor())) Victim->TakeHit(Data->Damage, false, Hit.ImpactPoint, FLinearColor(0.55f, 0.9f, 0.85f));
	}
}
