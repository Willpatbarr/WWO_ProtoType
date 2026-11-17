// A FootRace lane door (World/WWOBreachDoor.h).

#include "World/WWOBreachDoor.h"
#include "FX/WWOFX.h"
#include "WWOTypes.h"
#include "World/WWOBreachPortal.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

AWWOBreachDoor::AWWOBreachDoor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	// The slab is made here (not in BeginPlay) so it is in the level from the first frame, for the
	// navmesh and for collision, on every machine.
	Slab = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Slab"));
	SetRootComponent(Slab);
	Slab->SetStaticMesh(WWOFX::Cube());
	Slab->SetMaterial(0, WWOFX::Material(TEXT("M_WWO_Solid")));
	Slab->SetCollisionProfileName(TEXT("BlockAll"));
	Slab->SetCanEverAffectNavigation(true);
}

void AWWOBreachDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWWOBreachDoor, Team);
	DOREPLIFETIME(AWWOBreachDoor, bOpen);
}

void AWWOBreachDoor::BeginPlay()
{
	Super::BeginPlay();
	// The team's colour, dark: a wall in the lane, not something to walk into (the portals glow).
	if (UMaterialInstanceDynamic* M = Slab->CreateAndSetMaterialInstanceDynamic(0))
	{
		M->SetVectorParameterValue("Color", WWO::Darkened(AWWOBreachPortal::TeamColor(Team), 0.55f));
		M->SetScalarParameterValue("Glow", 0.f);
	}
	if (bOpen) ShowOpen();
}

void AWWOBreachDoor::Open()
{
	if (bOpen) return;
	bOpen = true;
	ShowOpen();
}

void AWWOBreachDoor::OnRep_Open()
{
	if (bOpen) ShowOpen();
}

void AWWOBreachDoor::ShowOpen()
{
	Slab->SetVisibility(false);
	Slab->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Slab->SetCanEverAffectNavigation(false); // the navmesh rebuilds through the doorway (it is built at run time)
}
