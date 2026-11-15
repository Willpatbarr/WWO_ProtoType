#include "Allies/WWOAlly.h"
#include "Player/WWOProf.h"
#include "Net/UnrealNetwork.h"
#include "Game/WWOGameState.h"
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
#include "Engine/World.h"

AWWOAlly::AWWOAlly(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UWWOEnemyMovement>(ACharacter::CharacterMovementComponentName))
{
	// Online: the host runs the AI; everyone else gets the position and animation.
	bReplicates = true;
	SetReplicatingMovement(true);
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
	ShowDown(true);
	Body->Play("Death01", 1.f, 0.1f);
}

void AWWOAlly::ShowDown(bool bIsDown)
{
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, bIsDown ? ECR_Ignore : ECR_Block);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, bIsDown ? ECR_Ignore : ECR_Block);
	if (Gun) Gun->SetVisibility(!bIsDown, true);
}

void AWWOAlly::OnRep_Down()
{
	ShowDown(bDown);
}

void AWWOAlly::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWWOAlly, Health);
	DOREPLIFETIME(AWWOAlly, Shield);
	DOREPLIFETIME(AWWOAlly, DownLeft);
	DOREPLIFETIME(AWWOAlly, bDown);
}

void AWWOAlly::GetUp()
{
	bDown = false;
	Health = D()->MaxHealth;
	Shield = D()->MaxShield;
	ShowDown(false);
	if (const AWWOCharacter* P = UWWOWorldSubsystem::Get(this)->GetPlayer())
	{
		// 2 m to your right, as in Godot, but not through a wall: it stops short of one.
		const FVector From = P->GetActorLocation();
		FVector Spot = From + P->GetActorRightVector() * 200.f;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOAllyGetUp), false, this);
		Params.AddIgnoredActor(P);
		FHitResult Hit;
		const UCapsuleComponent* Capsule = GetCapsuleComponent();
		if (GetWorld()->SweepSingleByChannel(Hit, From, Spot, FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight() - 10.f), Params))
		{
			Spot = Hit.Location;
		}
		TeleportTo(Spot, GetActorRotation());
	}
	GetCharacterMovement()->StopMovementImmediately();
	Body->Play("Idle", 1.f, 0.1f);
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
	WWO_PROF("Ally");
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		// Online, not the host: it moves and animates as the host's copy does; the gun stays in its hand.
		Gun->SetWorldLocationAndRotation(GetMesh()->GetSocketLocation("hand_r"), FRotator(0.f, GetActorRotation().Yaw, 0.f));
		return;
	}
	UWWOEnemyMovement* Move = Cast<UWWOEnemyMovement>(GetCharacterMovement());
	const UWWOAllyData* Data = D();
	if (bDown)
	{
		DownLeft -= DeltaSeconds;
		Move->Wish = FVector::ZeroVector;
		Move->Velocity.X = Move->Velocity.Y = 0.f; // down: drops where it stands
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
	// Jogs when moving, at a playback speed that matches how fast (Godot: ally.gd _process).
	const float Speed = GetVelocity().Size2D() / WWO::M;
	Body->Play(Speed > 0.5f ? FName("Jog_Fwd") : FName("Idle"), FMath::Clamp(Speed / 4.5f, 0.6f, 1.6f));
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
	if (Distance < Data->FollowMin)
	{
		// Give you some room. Godot backs straight off even into a wall; here it stays put
		// when a wall is right behind it.
		const FVector Away = -To.GetSafeNormal();
		const UCapsuleComponent* Capsule = GetCapsuleComponent();
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		const FVector From = GetActorLocation() + FVector(0, 0, 10.f);
		const bool bWall = GetWorld()->SweepTestByObjectType(From, From + Away * 30.f, FQuat::Identity, Objects,
			FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight() - 15.f),
			FCollisionQueryParams(SCENE_QUERY_STAT(WWOAllyBack), false, this));
		return bWall ? FVector::ZeroVector : Away * 0.5f;
	}
	if (Distance <= Data->FollowMax) return FVector::ZeroVector;
	// Walk back toward you, around walls.
	PathAge += DeltaSeconds;
	if (PathAge > 0.4f) // on its age alone: a straight two-point path trims to one point at once
	{
		bool bPartial = false;
		Path = WWONav::FindPath(this, GetActorLocation() - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), P->GetActorLocation(), bPartial);
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
	const float Yaw = Current + FMath::FindDeltaAngleDegrees(Current, Direction.Rotation().Yaw) * WWO::Damp(10.f, DeltaSeconds);
	SetActorRotation(FRotator(0.f, Yaw, 0.f));
}

AWWOEnemy* AWWOAlly::NearestVisibleEnemy() const
{
	AWWOEnemy* Best = nullptr;
	float BestDistance = D()->SightRange * WWO::M;
	const FVector Eyes = GetActorLocation() + FVector(0, 0, 60.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOAllySight), false, this);
	const AWWOCharacter* Player = UWWOWorldSubsystem::Get(this)->GetPlayer();
	for (AWWOEnemy* Enemy : UWWOWorldSubsystem::Get(this)->LivingEnemies())
	{
		const FVector Aim = Enemy->GetAimParts().Last().Position;
		const float Distance = FVector::Dist(Eyes, Aim);
		if (Distance >= BestDistance) continue;
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Eyes, Aim, ECC_Visibility, Params) || Hit.GetActor() != Enemy) continue;
		// The player's capsule ignores Visibility traces; in Godot you block a teammate's view.
		FHitResult PlayerHit;
		if (Player && Player->GetCapsuleComponent()->LineTraceComponent(PlayerHit, Eyes, Aim, FCollisionQueryParams(SCENE_QUERY_STAT(WWOAllySight), false))
			&& PlayerHit.Time < Hit.Time) continue;
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
	AWWOGameState::Tracer(GetWorld(), From, bHit ? Hit.ImpactPoint : From + Dir * Data->SightRange * WWO::M, FLinearColor(0.5f, 1.f, 0.9f, 0.8f)); // everyone sees it
	if (bHit)
	{
		FWWODamageBy By(this); // an ally's hits are its own (no damage numbers for them on your screen)
		if (AWWOEnemy* Victim = Cast<AWWOEnemy>(Hit.GetActor())) Victim->TakeHit(Data->Damage, false, Hit.ImpactPoint, FLinearColor(0.55f, 0.9f, 0.85f));
	}
}
