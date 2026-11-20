// wwo.ShellAnchors 0|1|2: draws the graft anchors of every robot wearing shell parts, every frame (a
// development aid, not in shipping builds). 1: each anchor as a point with a short line out of the surface
// (and a shorter one along its 'up'), named within 6 m of the camera; the bare skeleton's seams in magenta.
// 2: the paths too, as lines, with the step to their seam in magenta. The bodies don't tick, so one world
// delegate draws them all, and only while it's on.

#include "Characters/WWOBodyComponent.h"

#if !UE_BUILD_SHIPPING

#include "Characters/WWOShellAnchors.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"

namespace
{
	int32 Mode = 0;
	FDelegateHandle PostTickHandle, TickStartHandle;
	constexpr float LabelRange = 600.f; // cm
	// Head, Core, Arms, Legs; the seams.
	const FColor SlotColors[UWWOBodyComponent::NumShellSlots] = { FColor(80, 220, 255), FColor(255, 220, 60), FColor(110, 240, 110), FColor(255, 150, 50) };
	const FColor SeamColor(255, 60, 220);

	void DrawBody(UWorld* World, const UWWOBodyComponent* Body, const FVector* Camera)
	{
		for (int32 Slot = 0; Slot <= UWWOBodyComponent::NumShellSlots; ++Slot)
		{
			const FWWOShellAnchorSet* Set = Body->WornAnchors(Slot);
			if (!Set) continue;
			const bool bSeam = Slot == UWWOBodyComponent::NumShellSlots;
			const FColor Color = bSeam ? SeamColor : SlotColors[Slot];
			for (const TPair<FName, FWWOShellPoint>& It : Set->Anchors)
			{
				const FTransform T = Body->PointWorld(It.Value);
				const FVector At = T.GetLocation(), Out = T.GetUnitAxis(EAxis::X), Up = T.GetUnitAxis(EAxis::Z);
				DrawDebugPoint(World, At, bSeam ? 9.f : 7.f, Color, false, 0.f, SDPG_Foreground);
				DrawDebugLine(World, At, At + Out * 6.f, Color, false, 0.f, SDPG_Foreground, 0.35f);
				DrawDebugLine(World, At, At + Up * 2.5f, FColor::White, false, 0.f, SDPG_Foreground, 0.2f);
				if (Camera && FVector::Dist(*Camera, At) < LabelRange) DrawDebugString(World, At + Out * 7.f, It.Key.ToString(), nullptr, Color, 0.f, true, 0.7f);
			}
			if (Mode < 2 || bSeam) continue;
			for (const TPair<FName, FWWOShellPath>& It : Set->Paths)
			{
				TArray<FVector> Points;
				if (!Body->PathWorld(It.Key, Points, false) || Points.Num() < 2) continue;
				for (int32 i = 0; i + 1 < Points.Num(); ++i) DrawDebugLine(World, Points[i], Points[i + 1], Color, false, 0.f, SDPG_Foreground, 0.25f);
				FTransform Seam;
				if (!It.Value.FromSeam.IsNone() && Body->FindAnchor(It.Value.FromSeam, Seam)) DrawDebugLine(World, Seam.GetLocation(), Points[0], SeamColor, false, 0.f, SDPG_Foreground, 0.25f);
				if (!It.Value.ToSeam.IsNone() && Body->FindAnchor(It.Value.ToSeam, Seam)) DrawDebugLine(World, Points.Last(), Seam.GetLocation(), SeamColor, false, 0.f, SDPG_Foreground, 0.25f);
			}
		}
	}

	void Draw(UWorld* World)
	{
		if (!World || !World->IsGameWorld() || Mode <= 0) return;
		FVector Camera;
		const APlayerController* PC = World->GetFirstPlayerController();
		const bool bCamera = PC && PC->PlayerCameraManager;
		if (bCamera) Camera = PC->PlayerCameraManager->GetCameraLocation();
		for (TObjectIterator<UWWOBodyComponent> It; It; ++It)
		{
			if (It->GetWorld() == World && It->HasShells()) DrawBody(World, *It, bCamera ? &Camera : nullptr);
		}
	}

	void OnChanged(IConsoleVariable*)
	{
		const bool bOn = Mode > 0;
		if (bOn && !PostTickHandle.IsValid())
		{
			// After the actors (the bones are this frame's); a paused world (the character menu) skips that, so
			// its frames draw at the start of the tick instead.
			PostTickHandle = FWorldDelegates::OnWorldPostActorTick.AddLambda([](UWorld* World, ELevelTick, float) { Draw(World); });
			TickStartHandle = FWorldDelegates::OnWorldTickStart.AddLambda([](UWorld* World, ELevelTick, float) { if (World && World->IsPaused()) Draw(World); });
		}
		else if (!bOn && PostTickHandle.IsValid())
		{
			FWorldDelegates::OnWorldPostActorTick.Remove(PostTickHandle);
			FWorldDelegates::OnWorldTickStart.Remove(TickStartHandle);
			PostTickHandle.Reset();
			TickStartHandle.Reset();
		}
	}

	FAutoConsoleVariableRef ShellAnchorsVar(TEXT("wwo.ShellAnchors"), Mode,
		TEXT("Draw the graft anchors of every robot wearing shell parts: 0 off, 1 anchors (named within 6 m; seams magenta), 2 anchors and paths."),
		FConsoleVariableDelegate::CreateStatic(&OnChanged));
}

#endif
