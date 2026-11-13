#include "World/WWOTarget.h"
#include "Player/WWOProf.h"
#include "FX/WWOFX.h"
#include "Systems/WWOWorldSubsystem.h"
#include "WWOTypes.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"

namespace
{
	const FLinearColor BodyColor(0.2f, 0.85f, 0.95f);
	const FLinearColor HeadColor(1.f, 0.55f, 0.1f);
}

AWWOTarget::AWWOTarget()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));

	BodyBox = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	BodyBox->SetupAttachment(Root);
	BodyBox->SetBoxExtent(FVector(25.f, 40.f, 70.f)); // 0.8 x 1.4 x 0.5 m
	BodyBox->SetRelativeLocation(FVector(0, 0, 70.f));
	HeadBox = CreateDefaultSubobject<UBoxComponent>(TEXT("Head"));
	HeadBox->SetupAttachment(Root);
	HeadBox->SetBoxExtent(FVector(16.f));
	HeadBox->SetRelativeLocation(FVector(0, 0, 158.f));
	for (UBoxComponent* Box : { BodyBox.Get(), HeadBox.Get() })
	{
		Box->SetCollisionProfileName(TEXT("BlockAll"));
	}

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(BodyBox);
	BodyMesh->SetRelativeScale3D(FVector(0.5f, 0.8f, 1.4f));
	HeadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeadMesh"));
	HeadMesh->SetupAttachment(HeadBox);
	HeadMesh->SetRelativeScale3D(FVector(0.32f));
	for (UStaticMeshComponent* Mesh : { BodyMesh.Get(), HeadMesh.Get() })
	{
		if (Cube.Succeeded()) Mesh->SetStaticMesh(Cube.Object);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	Tags.Add("Target");
	bReplicates = true;
	SetReplicateMovement(true); // strafing runs on the host
}

void AWWOTarget::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWWOTarget, Health);
	DOREPLIFETIME(AWWOTarget, MaxHealth);
	DOREPLIFETIME(AWWOTarget, bDead);
}

void AWWOTarget::OnRep_Dead()
{
	if (bDead) StartDeath();
	else ShowAlive();
}

void AWWOTarget::BeginPlay()
{
	Super::BeginPlay();
	Start = GetActorLocation();
	BodyMesh->SetMaterial(0, WWOFX::Material(TEXT("M_WWO_Solid")));
	HeadMesh->SetMaterial(0, WWOFX::Material(TEXT("M_WWO_Solid")));
	BodyMat = BodyMesh->CreateAndSetMaterialInstanceDynamic(0);
	HeadMat = HeadMesh->CreateAndSetMaterialInstanceDynamic(0);
	if (UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(this))
	{
		WS->RegisterTarget(this);
		if (HasAuthority()) MaxHealth = Health = WS->TargetHealth; // a joiner's copy keeps the host's values
	}
}

void AWWOTarget::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(this)) WS->UnregisterTarget(this);
	Super::EndPlay(Reason);
}

bool AWWOTarget::IsCriticalComponent(const UPrimitiveComponent* Component) const
{
	return Component == HeadBox.Get();
}

TArray<FWWOAimPart> AWWOTarget::GetAimParts() const
{
	// Head first.
	return { { HeadBox->GetComponentLocation(), 17.f, true }, { BodyBox->GetComponentLocation(), 40.f, false } };
}

bool AWWOTarget::TakeHit(float Damage, bool bCritical, const FVector& Point, FLinearColor Color)
{
	if (bDead) return false;
	if (bCritical) HeadFlash = 1.f;
	else BodyFlash = 1.f;
	UWWOWorldSubsystem::Get(this)->SpawnDamageNumber(Damage, bCritical, Point, Color);
	if (!HasAuthority()) return false; // a joiner's hit: looks only, the host owns the health
	Health -= Damage;
	if (Health > 0.f) return false;
	bDead = true;
	Health = 0.f;
	StartDeath();
	const float Respawn = UWWOWorldSubsystem::Get(this)->RespawnTime;
	RespawnLeft = Respawn > 0.f ? Respawn : -1.f;
	return true;
}

void AWWOTarget::StartDeath()
{
	SetCollision(false);
	DeathAge = 0.f;
}

void AWWOTarget::Revive(float NewMaxHealth)
{
	if (!HasAuthority()) return; // the host revives it; that replicates
	MaxHealth = NewMaxHealth;
	Health = MaxHealth;
	bDead = false;
	RespawnLeft = -1.f;
	ShowAlive();
}

void AWWOTarget::ShowAlive()
{
	SetCollision(true);
	DeathAge = -1.f;
	SetActorHiddenInGame(false);
	BodyMesh->SetRelativeScale3D(FVector(0.5f, 0.8f, 1.4f));
	HeadMesh->SetRelativeScale3D(FVector(0.32f));
}

void AWWOTarget::SetCollision(bool bOn)
{
	BodyBox->SetCollisionEnabled(bOn ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	HeadBox->SetCollisionEnabled(bOn ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
}

void AWWOTarget::Tick(float DeltaSeconds)
{
	WWO_PROF("Target");
	Super::Tick(DeltaSeconds);
	if (StrafeDistance > 0.f && !bDead && HasAuthority())
	{
		const float Half = StrafeDistance * 0.5f;
		Time += DeltaSeconds;
		SetActorLocation(Start + GetActorRightVector() * FMath::Sin(Time * StrafeSpeed / Half) * Half * WWO::M);
	}
	if (DeathAge >= 0.f)
	{
		// Shrink away (ease-in back), then hide.
		DeathAge += DeltaSeconds;
		const float T = FMath::Clamp(DeathAge / 0.25f, 0.f, 1.f);
		const float C1 = 1.70158f, C3 = C1 + 1.f;
		const float E = C3 * T * T * T - C1 * T * T;
		const float S = FMath::Max(1.f - E * 0.99f, 0.01f);
		BodyMesh->SetRelativeScale3D(FVector(0.5f, 0.8f, 1.4f) * S);
		HeadMesh->SetRelativeScale3D(FVector(0.32f) * S);
		if (T >= 1.f) SetActorHiddenInGame(true);
	}
	if (HasAuthority() && RespawnLeft > 0.f && (RespawnLeft -= DeltaSeconds) <= 0.f && bDead) Revive(MaxHealth);
	BodyFlash = WWO::MoveToward(BodyFlash, 0.f, 6.f * DeltaSeconds);
	HeadFlash = WWO::MoveToward(HeadFlash, 0.f, 6.f * DeltaSeconds);
	if (BodyMat) BodyMat->SetVectorParameterValue("Color", FMath::Lerp(BodyColor, FLinearColor::White, BodyFlash));
	if (HeadMat) HeadMat->SetVectorParameterValue("Color", FMath::Lerp(HeadColor, FLinearColor::White, HeadFlash));
}
