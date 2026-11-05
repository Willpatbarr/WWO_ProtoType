// Map steps of the autotest (-WWOAutoTestMaps, and the end of -WWOAutoTest): the level seen from a
// handful of fixed cameras (spawn, overviews, platforms and their ramps, halls), one screenshot
// each, for checking the arena look and lighting. Views are written in Godot tile coordinates
// like build_maps.py: tile (x, z) at height y, 4 m tiles centered on the map.

#include "Player/WWOPlayerController.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
#include "WWOTypes.h"
#include "WhatWasOurs.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"

namespace
{
	struct FWWOMapView
	{
		const TCHAR* Name;
		FVector From; // Godot tile x, tile z, height in meters
		FVector At;
	};

	// The small arena (16 x 12 tiles).
	const FWWOMapView ArenaViews[] = {
		{TEXT("spawn"), {7.5, 9.6, 1.7}, {7.5, 5.0, 1.2}},
		{TEXT("overview"), {13.5, 9.5, 9.0}, {6.0, 4.0, 0.0}},
		{TEXT("west_platform"), {6.2, 5.6, 2.2}, {3.0, 3.5, 1.4}},
		{TEXT("east_platform"), {9.0, 4.2, 2.2}, {12.5, 6.8, 1.4}},
		{TEXT("north_hall"), {1.0, 1.8, 2.0}, {12.0, 0.8, 2.0}},
		{TEXT("west_corridor"), {0.5, 9.2, 1.7}, {0.5, 3.0, 1.4}},
		{TEXT("from_platform"), {12.5, 7.4, 3.7}, {6.0, 4.0, 0.5}},
		{TEXT("top"), {7.5, 15.0, 26.0}, {7.5, 5.0, 0.0}},
	};
	// The large arena (24 x 16 tiles).
	const FWWOMapView ArenaLargeViews[] = {
		{TEXT("spawn"), {11.5, 13.4, 1.7}, {11.5, 8.0, 2.0}},
		{TEXT("overview"), {17.5, 12.6, 9.0}, {9.0, 6.0, 1.0}},
		{TEXT("tower"), {15.5, 11.4, 2.2}, {11.5, 9.0, 3.0}},
		{TEXT("west_stairs"), {9.0, 11.0, 2.2}, {6.0, 8.5, 2.5}},
		{TEXT("west_wing"), {4.6, 14.4, 5.7}, {4.6, 2.0, 5.0}},
		{TEXT("north_hall"), {6.5, 2.0, 2.0}, {17.0, 2.0, 2.0}},
		{TEXT("east_hallway"), {19.0, 15.2, 1.7}, {19.0, 0.0, 1.5}},
		{TEXT("top"), {11.5, 23.0, 36.0}, {11.5, 7.0, 0.0}},
	};
	// The test range: positions are Godot meters (x, z, y), no tile grid.
	const FWWOMapView TestRangeViews[] = {
		{TEXT("spawn"), {0.0, 3.0, 1.7}, {0.0, -20.0, 1.0}},
		{TEXT("overview"), {22.0, 18.0, 14.0}, {0.0, -15.0, 0.0}},
		{TEXT("soldiers"), {14.0, -18.0, 3.0}, {27.0, -31.0, 1.0}},
	};
}

bool AWWOPlayerController::UpdateAutoMapTest(AWWOCharacter* P, float DeltaTime)
{
	UWorld* World = GetWorld();
	const FString Map = World->GetMapName();
	const bool bLarge = Map.Contains(TEXT("ArenaLarge"));
	const bool bRange = Map.Contains(TEXT("TestRange"));
	const FWWOMapView* Views = bLarge ? ArenaLargeViews : bRange ? TestRangeViews : ArenaViews;
	const int32 Count = bLarge ? UE_ARRAY_COUNT(ArenaLargeViews) : bRange ? UE_ARRAY_COUNT(TestRangeViews) : UE_ARRAY_COUNT(ArenaViews);
	const FString MapTag = bLarge ? TEXT("large") : bRange ? TEXT("range") : TEXT("arena");
	const FVector2D Size = bLarge ? FVector2D(24.0, 16.0) : FVector2D(16.0, 12.0);
	auto ToWorld = [bRange, Size](const FVector& V)
	{
		// Godot (x, y, z) = Unreal (-z, x, y) * 100; tiles are 4 m, centered on the map.
		const double X = bRange ? V.X : (V.X - (Size.X - 1.0) * 0.5) * 4.0;
		const double Z = bRange ? V.Y : (V.Y - (Size.Y - 1.0) * 0.5) * 4.0;
		return FVector(-Z, X, V.Z) * WWO::M;
	};
	const float Hold = 2.5f; // seconds per view: the sky light capture and auto exposure settle
	if (AutoMapIndex < 0)
	{
		AutoMapIndex = 0;
		AutoMapClock = 0.f;
		P->Health->GodMode = 1.f;
		AutoMapCamera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity);
		AutoMapCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		AutoMapCamera->GetCameraComponent()->SetFieldOfView(85.f);
		SetViewTarget(AutoMapCamera);
	}
	if (AutoMapIndex >= Count)
	{
		if (AutoMapCamera) AutoMapCamera->Destroy();
		AutoMapCamera = nullptr;
		SetViewTarget(P);
		AutoMapIndex = -1;
		P->Health->GodMode = 0.f;
		UE_LOG(LogWWO, Display, TEXT("AutoTest maps: %d views of %s"), Count, *Map);
		return true;
	}
	const FWWOMapView& View = Views[AutoMapIndex];
	if (AutoMapClock == 0.f)
	{
		const FVector From = ToWorld(View.From), At = ToWorld(View.At);
		AutoMapCamera->SetActorLocationAndRotation(From, (At - From).Rotation());
	}
	AutoMapClock += DeltaTime;
	if (AutoMapClock >= Hold)
	{
		Shot(*FString::Printf(TEXT("36_map_%s_%d_%s"), *MapTag, AutoMapIndex, View.Name));
		++AutoMapIndex;
		AutoMapClock = 0.f;
	}
	return false;
}
