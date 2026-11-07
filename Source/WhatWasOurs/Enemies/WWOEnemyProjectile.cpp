#include "Enemies/WWOEnemyProjectile.h"
#include "Enemies/WWOEnemy.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
#include "Allies/WWOAlly.h"
#include "FX/WWOFX.h"
#include "Systems/WWOWorldSubsystem.h"
#include "WWOTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

int32 AWWOEnemyProjectile::PlayerHits = 0;
int32 AWWOEnemyProjectile::SeenRemote = 0;

AWWOEnemyProjectile::AWWOEnemyProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	// Sent once at spawn (velocity and looks are fixed); each machine then flies its own copy.
	bReplicates = true;
	SetReplicateMovement(false);
	bAlwaysRelevant = true;
}

void AWWOEnemyProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AWWOEnemyProjectile, Velocity, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AWWOEnemyProjectile, Life, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AWWOEnemyProjectile, Gravity, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AWWOEnemyProjectile, SplashRadius, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AWWOEnemyProjectile, Color, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AWWOEnemyProjectile, Size, COND_InitialOnly);
}

void AWWOEnemyProjectile::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority()) return;
	// A joiner's copy: the replicated values are in by now (they arrive with the spawn).
	++SeenRemote;
	Life += 1.f; // the host's copy decides when it ends; this one only outlives it a little
	Build();
}

void AWWOEnemyProjectile::EndPlay(const EEndPlayReason::Type Reason)
{
	// A joiner's copy going because the host's hit something or ran out: show the hit here.
	if (!HasAuthority() && Reason == EEndPlayReason::Destroyed && GetWorld())
	{
		if (SplashRadius > 0.f) SplashLooks(GetActorLocation());
		else AWWOFXActor::Pop(GetWorld(), GetActorLocation(), Color, Size * 2.5f, 0.2f);
	}
	Super::EndPlay(Reason);
}

AWWOEnemyProjectile* AWWOEnemyProjectile::Fire(UWorld* World, const FVector& From, const FVector& VelocityCm, float InDamage, float InLife,
	float GravityCm, float SplashRadiusM, const FLinearColor& InColor, float SizeM)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AWWOEnemyProjectile* Shot = World->SpawnActor<AWWOEnemyProjectile>(From, FRotator::ZeroRotator, Params);
	if (!Shot) return nullptr;
	Shot->Velocity = VelocityCm;
	Shot->Damage = InDamage;
	Shot->Life = InLife;
	Shot->Gravity = GravityCm;
	Shot->SplashRadius = SplashRadiusM;
	Shot->Color = InColor;
	Shot->Size = SizeM;
	Shot->Build();
	return Shot;
}

void AWWOEnemyProjectile::Build()
{
	UStaticMeshComponent* Ball = WWOFX::AddShape(this, GetRootComponent(), WWOFX::Sphere(), TEXT("M_WWO_Unlit"), Color, 4.f);
	Ball->SetRelativeScale3D(FVector(Size * 2.f));
	Ball->SetCastShadow(false);
	if (UMaterialInstanceDynamic* M = WWOFX::MID(Ball))
	{
		M->SetScalarParameterValue("Alpha", 1.f);
		M->SetScalarParameterValue("Glow", 4.f);
	}
	UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
	Light->SetupAttachment(GetRootComponent());
	Light->SetLightColor(Color);
	Light->SetIntensityUnits(ELightUnits::Unitless);
	Light->SetIntensity(0.8f * 1500.f);
	Light->SetAttenuationRadius(150.f);
	Light->SetCastShadows(false);
	Light->RegisterComponent();
}

void AWWOEnemyProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const bool bHost = HasAuthority();
	if (!bHost && bStopped) return;
	Life -= DeltaSeconds;
	if (Life <= 0.f)
	{
		if (!bHost) { bStopped = true; return; } // the host's copy ends it
		if (SplashRadius > 0.f) Splash(GetActorLocation());
		Destroy();
		return;
	}
	Velocity.Z -= Gravity * DeltaSeconds;
	const FVector From = GetActorLocation();
	const FVector To = From + Velocity * DeltaSeconds;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOEnemyShot), false, this);
	for (AWWOEnemy* Enemy : UWWOWorldSubsystem::Get(this)->LivingEnemies()) Params.AddIgnoredActor(Enemy); // shots pass through enemies
	FHitResult Hit;
	bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params);
	// The player's capsule (Unreal's Pawn profile) ignores Visibility traces, but in Godot the
	// shot's ray hits the player's body, so test it too and keep whichever comes first.
	for (AWWOCharacter* P : UWWOWorldSubsystem::Get(this)->GetPlayers()) // every player, online
	{
		FHitResult PlayerHit;
		if (P->GetCapsuleComponent()->LineTraceComponent(PlayerHit, From, To, FCollisionQueryParams(SCENE_QUERY_STAT(WWOEnemyShot), false))
			&& (!bHit || PlayerHit.Time < Hit.Time))
		{
			Hit = PlayerHit;
			bHit = true;
		}
	}
	if (!bHit)
	{
		SetActorLocation(To);
		return;
	}
	if (!bHost)
	{
		// Looks only: stop where it hit; the host decides what it did, and EndPlay pops it.
		SetActorLocation(Hit.ImpactPoint);
		bStopped = true;
		return;
	}
	if (SplashRadius > 0.f)
	{
		Splash(Hit.ImpactPoint);
	}
	else if (AWWOCharacter* P = Cast<AWWOCharacter>(Hit.GetActor()))
	{
		if (!P->Health->bDead) ++PlayerHits;
		P->Health->TakeDamage(Damage);
	}
	else if (AWWOAlly* A = Cast<AWWOAlly>(Hit.GetActor()))
	{
		A->TakeAllyDamage(Damage);
	}
	AWWOFXActor::Pop(GetWorld(), Hit.ImpactPoint, Color, Size * 2.5f, 0.2f);
	Destroy();
}

void AWWOEnemyProjectile::Splash(const FVector& Point)
{
	UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(this);
	const float R = SplashRadius * WWO::M;
	for (AWWOCharacter* P : WS->GetPlayers())
	{
		const float D = FVector::Dist(Point, P->GetActorLocation());
		if (D <= R + 50.f) // +0.5 m: the player's body, not just its center
		{
			P->Health->TakeDamage(Damage * FMath::Lerp(1.f, 0.5f, FMath::Clamp(D / R, 0.f, 1.f)));
		}
	}
	for (AWWOAlly* Mate : WS->LivingTeammates())
	{
		const float D = FVector::Dist(Point, Mate->GetActorLocation());
		if (D <= R + 50.f) Mate->TakeAllyDamage(Damage * FMath::Lerp(1.f, 0.5f, FMath::Clamp(D / R, 0.f, 1.f)));
	}
	SplashLooks(Point);
}

void AWWOEnemyProjectile::SplashLooks(const FVector& Point)
{
	// The blast: a sphere that pops out to the splash size and fades.
	const float R = SplashRadius * WWO::M;
	if (AWWOFXActor* FX = AWWOFXActor::Spawn(GetWorld(), Point, 0.4f))
	{
		UStaticMeshComponent* Ball = WWOFX::AddSoftSphere(FX, FX->GetRootComponent(), Color, 0.5f, 0.6f, false);
		FX->ScaleTween(Ball, FVector(0.1f * R / 50.f / SplashRadius), FVector(R / 50.f), 0.15f);
		FX->ParamTween(WWOFX::MID(Ball), "Alpha", 0.5f, 0.f, 0.4f);
	}
}
