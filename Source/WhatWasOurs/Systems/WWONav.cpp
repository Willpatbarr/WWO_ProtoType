#include "Systems/WWONav.h"
#include "WWOTypes.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Engine/World.h"

TArray<FVector> WWONav::FindPath(AActor* Querier, const FVector& Start, const FVector& End, bool& bOutPartial)
{
	bOutPartial = false;
	UWorld* World = Querier ? Querier->GetWorld() : nullptr;
	const UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!Nav) return {};
	auto Snap = [Nav](const FVector& Point)
	{
		FNavLocation Out;
		if (Nav->ProjectPointToNavigation(Point, Out)) return Point;
		return Nav->ProjectPointToNavigation(Point, Out, FVector(400.f, 400.f, 250.f)) ? Out.Location : Point;
	};
	const UNavigationPath* NavPath = UNavigationSystemV1::FindPathToLocationSynchronously(World, Snap(Start), Snap(End), Querier);
	if (!NavPath || !NavPath->IsValid()) return {};
	bOutPartial = NavPath->IsPartial();
	return NavPath->PathPoints;
}

namespace
{
	FCollisionObjectQueryParams LevelGeometry()
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		return Objects;
	}
}

bool WWONav::WallAhead(const UWorld* World, const FWWOProbe& Probe, const FVector& From, const FVector& Step,
	const FCollisionQueryParams& Params, FVector* OutNormal)
{
	const FVector To = From + Step;
	FHitResult Hit;
	bool bHit = false;
	if (Probe.Radius <= 0.f)
	{
		bHit = Probe.bByChannel ? World->LineTraceSingleByChannel(Hit, From, To, Probe.Channel, Params)
			: World->LineTraceSingleByObjectType(Hit, From, To, LevelGeometry(), Params);
	}
	else
	{
		const FCollisionShape Capsule = FCollisionShape::MakeCapsule(Probe.Radius, Probe.HalfHeight);
		if (Probe.bAnyHit)
		{
			return Probe.bByChannel ? World->SweepTestByChannel(From, To, FQuat::Identity, Probe.Channel, Capsule, Params)
				: World->SweepTestByObjectType(From, To, FQuat::Identity, LevelGeometry(), Capsule, Params);
		}
		bHit = Probe.bByChannel ? World->SweepSingleByChannel(Hit, From, To, FQuat::Identity, Probe.Channel, Capsule, Params)
			: World->SweepSingleByObjectType(Hit, From, To, FQuat::Identity, LevelGeometry(), Capsule, Params);
	}
	if (bHit && OutNormal) *OutNormal = FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f).GetSafeNormal();
	return bHit;
}

bool WWONav::LedgeAhead(const UWorld* World, const FWWOProbe& Probe, const FVector& Point, float Drop, const FCollisionQueryParams& Params)
{
	FHitResult Hit;
	const FVector Down = Point - FVector(0, 0, Drop);
	return !(Probe.bByChannel ? World->LineTraceSingleByChannel(Hit, Point, Down, Probe.Channel, Params)
		: World->LineTraceSingleByObjectType(Hit, Point, Down, LevelGeometry(), Params));
}

bool FWWOPathFollower::Update(AActor* Querier, const FVector& From, const FVector& To, const FVector& Here, float DeltaSeconds, const FWWOPathRules& Rules)
{
	Age += DeltaSeconds;
	const bool bDue = Age > Rules.RepathAfter || (Rules.RepathMoved >= 0.f
		&& (Rules.bMovedSquared ? FVector::DistSquared(To, Goal) > FMath::Square(Rules.RepathMoved) : FVector::Dist(To, Goal) > Rules.RepathMoved));
	if (bDue)
	{
		Points = WWONav::FindPath(Querier, From, To, bPartial);
		Age = 0.f;
		Goal = To;
	}
	while (Points.Num() > 1)
	{
		const double Away = Rules.bDist2D ? FVector::Dist2D(Points[0], Here) : FVector2D(Points[0].X - Here.X, Points[0].Y - Here.Y).Size();
		if (Rules.bReachInclusive ? Away > Rules.Reach : Away >= Rules.Reach) break;
		Points.RemoveAt(0); // reached that point
	}
	return bDue;
}
