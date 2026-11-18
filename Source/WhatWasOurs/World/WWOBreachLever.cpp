// A FootRace door lever (World/WWOBreachLever.h).

#include "World/WWOBreachLever.h"
#include "World/WWOBreachPortal.h"
#include "FX/WWOFX.h"
#include "WWOTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr float BaseHeight = 1.0f;   // m
	constexpr float HandleLength = 0.9f; // m
	constexpr float UpPitch = 35.f;      // degrees the handle leans back while up
	constexpr float DownPitch = -60.f;   // and forward once pulled
	const FLinearColor BaseColor(0.18f, 0.19f, 0.21f);
}

AWWOBreachLever::AWWOBreachLever()
{
	PrimaryActorTick.bCanEverTick = true; // the handle follows Progress on every machine
	bReplicates = true;
	bAlwaysRelevant = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AWWOBreachLever::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWWOBreachLever, Team);
	DOREPLIFETIME(AWWOBreachLever, Door);
	DOREPLIFETIME(AWWOBreachLever, bLive);
	DOREPLIFETIME(AWWOBreachLever, bPulled);
	DOREPLIFETIME(AWWOBreachLever, Progress);
}

void AWWOBreachLever::BeginPlay()
{
	Super::BeginPlay();
	Build();
	Refresh();
}

void AWWOBreachLever::Build()
{
	USceneComponent* Root = GetRootComponent();
	const FLinearColor Color = AWWOBreachPortal::TeamColor(Team);
	Base = WWOFX::AddShape(this, Root, WWOFX::Cube(), TEXT("M_WWO_Solid"), BaseColor);
	Base->SetRelativeLocation(FVector(0.f, 0.f, BaseHeight * 0.5f * WWO::M));
	Base->SetRelativeScale3D(FVector(0.6f, 0.6f, BaseHeight));
	Base->SetCollisionProfileName(TEXT("BlockAll")); // something to stand at, and to hide behind a little
	Pivot = NewObject<USceneComponent>(this);
	Pivot->SetupAttachment(Root);
	Pivot->SetRelativeLocation(FVector(0.f, 0.f, BaseHeight * WWO::M));
	Pivot->RegisterComponent();
	AddInstanceComponent(Pivot);
	Handle = WWOFX::AddShape(this, Pivot, WWOFX::Cube(), TEXT("M_WWO_Solid"), Color, 1.f);
	Handle->SetRelativeLocation(FVector(0.f, 0.f, HandleLength * 0.5f * WWO::M));
	Handle->SetRelativeScale3D(FVector(0.12f, 0.12f, HandleLength));
	Lamp = WWOFX::AddShape(this, Root, WWOFX::Sphere(), TEXT("M_WWO_Solid"), Color);
	Lamp->SetRelativeScale3D(FVector(0.25f));
	Lamp->SetRelativeLocation(FVector(0.f, -0.2f * WWO::M, (BaseHeight + 0.1f) * WWO::M));
}

void AWWOBreachLever::OnRep_Look()
{
	Refresh();
}

void AWWOBreachLever::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bLive != bShownLive || bPulled != bShownPulled) Refresh();
	if (Pivot)
	{
		const float Pull = bPulled ? 1.f : FMath::Clamp(Progress, 0.f, 1.f);
		Pivot->SetRelativeRotation(FRotator(FMath::Lerp(UpPitch, DownPitch, Pull), 0.f, 0.f));
	}
}

void AWWOBreachLever::Refresh()
{
	if (!Lamp) return;
	bShownLive = bLive;
	bShownPulled = bPulled;
	const FLinearColor Color = AWWOBreachPortal::TeamColor(Team);
	// The lamp: lit in the team's colour while it can be pulled, dark otherwise; white once its door is open.
	if (UMaterialInstanceDynamic* M = WWOFX::MID(Lamp))
	{
		M->SetVectorParameterValue("Color", bPulled ? FLinearColor(0.8f, 0.8f, 0.8f) : bLive ? Color : WWO::Darkened(Color, 0.75f));
		M->SetScalarParameterValue("Glow", bLive ? 4.f : bPulled ? 1.f : 0.f);
	}
	if (UMaterialInstanceDynamic* M = WWOFX::MID(Handle))
	{
		M->SetVectorParameterValue("Color", bLive || bPulled ? Color : WWO::Darkened(Color, 0.5f));
		M->SetScalarParameterValue("Glow", bLive ? 1.5f : 0.f);
	}
}

bool AWWOBreachLever::InReach(const FVector& BodyLocation) const
{
	return FVector::Dist2D(BodyLocation, GetActorLocation()) <= Reach * WWO::M && FMath::Abs(BodyLocation.Z - GetActorLocation().Z) < 3.f * WWO::M;
}
