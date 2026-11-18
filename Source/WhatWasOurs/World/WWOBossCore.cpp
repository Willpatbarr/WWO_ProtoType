// A FootRace boss core (World/WWOBossCore.h).

#include "World/WWOBossCore.h"
#include "World/WWOBreachPortal.h"
#include "Player/WWOCharacter.h"
#include "FX/WWOFX.h"
#include "WWOTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr float BallSize = 0.55f;  // m across
	constexpr float FloatHeight = 1.1f; // m above the floor while lying
	constexpr float AboveHead = 0.7f;   // m above the carrier's head
}

AWWOBossCore::AWWOBossCore()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(true);
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AWWOBossCore::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWWOBossCore, Team);
	DOREPLIFETIME(AWWOBossCore, Carrier);
	DOREPLIFETIME(AWWOBossCore, bDropped);
}

void AWWOBossCore::BeginPlay()
{
	Super::BeginPlay();
	Rest = GetActorLocation();
	const FLinearColor Color = AWWOBreachPortal::TeamColor(Team);
	Ball = WWOFX::AddShape(this, GetRootComponent(), WWOFX::Sphere(), TEXT("M_WWO_Solid"), Color, 4.f);
	Ball->SetRelativeScale3D(FVector(BallSize));
	Ball->SetCastShadow(false);
	Glow = WWOFX::AddSoftSphere(this, GetRootComponent(), Color, 0.5f, 1.4f);
	Glow->SetRelativeScale3D(FVector(BallSize * 2.6f));
}

bool AWWOBossCore::InReach(const FVector& BodyLocation) const
{
	return !Carrier && FVector::Dist2D(BodyLocation, Rest) <= PickupReach * WWO::M && FMath::Abs(BodyLocation.Z - Rest.Z) < 2.5f * WWO::M;
}

void AWWOBossCore::PickUp(AWWOCharacter* By)
{
	Carrier = By;
	bDropped = false;
	DroppedLeft = 0.f;
}

void AWWOBossCore::Drop(const FVector& Where, float Seconds)
{
	Carrier = nullptr;
	bDropped = true;
	DroppedLeft = Seconds;
	FVector Ground = Where;
	WWOFX::GroundBelow(GetWorld(), Where + FVector(0, 0, 50.f), 600.f, Ground);
	Rest = Ground + FVector(0, 0, FloatHeight * WWO::M);
	SetActorLocation(Rest);
}

void AWWOBossCore::GoHome()
{
	Carrier = nullptr;
	bDropped = false;
	DroppedLeft = 0.f;
	Rest = Home;
	SetActorLocation(Rest);
}

void AWWOBossCore::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	if (Carrier)
	{
		// Over the carrier's head, on every machine (Carrier replicates).
		const float Half = Carrier->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		SetActorLocation(Carrier->GetActorLocation() + FVector(0, 0, Half + AboveHead * WWO::M));
		return;
	}
	// Lying: a slow bob, so it reads as something to pick up.
	if (Ball) Ball->SetRelativeLocation(FVector(0, 0, FMath::Sin(Clock * 2.5f) * 12.f));
	if (Glow) Glow->SetRelativeLocation(Ball ? Ball->GetRelativeLocation() : FVector::ZeroVector);
	if (HasAuthority() && !GetActorLocation().Equals(Rest)) SetActorLocation(Rest); // back down from a carrier's head (joiners follow by replicated movement)
}
