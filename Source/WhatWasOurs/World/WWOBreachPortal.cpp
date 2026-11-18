// A breach portal's doorway (World/WWOBreachPortal.h).

#include "World/WWOBreachPortal.h"
#include "FX/WWOFX.h"
#include "WWOTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr float Post = 0.3f;    // m: the frame's posts and lintel
	constexpr float PanelAlpha = 0.3f;
	constexpr float FrameGlow = 2.f;
}

AWWOBreachPortal::AWWOBreachPortal()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true; // a handful in the level; joiners need every one to see where they lead
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AWWOBreachPortal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWWOBreachPortal, Team);
	DOREPLIFETIME(AWWOBreachPortal, bHub);
}

FLinearColor AWWOBreachPortal::TeamColor(int32 InTeam)
{
	// The spawn floors' colours in Tools/build_maps.py (FOOTRACE_COLORS), brighter.
	return InTeam == 1 ? WWO::Lin(FLinearColor(1.f, 0.68f, 0.2f)) : WWO::Lin(FLinearColor(0.35f, 0.62f, 1.f));
}

void AWWOBreachPortal::BeginPlay()
{
	Super::BeginPlay();
	Build();
}

void AWWOBreachPortal::Build()
{
	USceneComponent* Root = GetRootComponent();
	const FLinearColor Color = TeamColor(Team);
	// Two posts and a lintel round the opening, standing on the floor (the actor's origin).
	const float HalfW = (Width + Post) * 0.5f * WWO::M;
	for (const float Side : { -1.f, 1.f })
	{
		UStaticMeshComponent* P = WWOFX::AddShape(this, Root, WWOFX::Cube(), TEXT("M_WWO_Solid"), Color, FrameGlow);
		P->SetRelativeLocation(FVector(0.f, Side * HalfW, Height * 0.5f * WWO::M));
		P->SetRelativeScale3D(FVector(Post, Post, Height));
		Frame.Add(P);
	}
	UStaticMeshComponent* Lintel = WWOFX::AddShape(this, Root, WWOFX::Cube(), TEXT("M_WWO_Solid"), Color, FrameGlow);
	Lintel->SetRelativeLocation(FVector(0.f, 0.f, (Height + Post * 0.5f) * WWO::M));
	Lintel->SetRelativeScale3D(FVector(Post, Width + Post * 2.f, Post));
	Frame.Add(Lintel);
	// The opening itself: a thin see-through sheet in the team's colour.
	FLinearColor Sheet = Color;
	Sheet.A = PanelAlpha;
	Panel = WWOFX::AddShape(this, Root, WWOFX::Cube(), TEXT("M_WWO_Unlit"), Sheet);
	Panel->SetCastShadow(false);
	Panel->SetRelativeLocation(FVector(0.f, 0.f, Height * 0.5f * WWO::M));
	Panel->SetRelativeScale3D(FVector(0.04f, Width, Height));
	if (UMaterialInstanceDynamic* M = WWOFX::MID(Panel)) M->SetScalarParameterValue("Alpha", PanelAlpha);
}

void AWWOBreachPortal::Recolor()
{
	const FLinearColor Color = TeamColor(Team);
	for (UStaticMeshComponent* C : Frame)
	{
		if (UMaterialInstanceDynamic* M = C ? WWOFX::MID(C) : nullptr) M->SetVectorParameterValue("Color", Color);
	}
	if (UMaterialInstanceDynamic* M = Panel ? WWOFX::MID(Panel) : nullptr)
	{
		FLinearColor Sheet = Color;
		Sheet.A = PanelAlpha;
		M->SetVectorParameterValue("Color", Sheet);
	}
}

void AWWOBreachPortal::OnRep_Look()
{
	Recolor();
}

FTransform AWWOBreachPortal::ArrivalSpot(float HalfHeight) const
{
	const FVector Forward = FRotator(0.f, GetActorRotation().Yaw, 0.f).Vector();
	const FVector At = GetActorLocation() + Forward * ArriveAhead * WWO::M + FVector(0.f, 0.f, HalfHeight + 5.f);
	return FTransform(Forward.Rotation(), At);
}

bool AWWOBreachPortal::Contains(const FVector& Feet) const
{
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(Feet);
	return FMath::Abs(Local.X) <= Depth * WWO::M && FMath::Abs(Local.Y) <= Width * 0.5f * WWO::M
		&& Local.Z >= -0.5f * WWO::M && Local.Z <= Height * WWO::M;
}
