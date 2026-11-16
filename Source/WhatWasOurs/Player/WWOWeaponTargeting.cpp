// UWWOWeaponComponent: what the reticle is on (the target, its highlight, the sight dot's colour), who is
// marked and which teammates are in sight (UpdateMarks), and where the gun's sight is seen on screen.

#include "Player/WWOWeaponComponent.h"
#include "WhatWasOurs.h"
#include "Player/WWOWeaponPrivate.h"
#include "Player/WWOCharacter.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Engine/World.h"
#include "Characters/WWOBodyComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOUltComponent.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "UI/WWOHUDLayout.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "SceneView.h"

using namespace WWOWeapon; // the helpers these files share (WWOWeaponPrivate.h)

namespace
{
	const FLinearColor SightColor(0.95f, 0.95f, 0.95f);
	const FLinearColor SightOnTargetColor(1.f, 0.1f, 0.08f);
}

bool UWWOWeaponComponent::SightOnScreen(FVector2D& OutAt, float& OutRoll) const
{
	const AWWOCharacter* P = GetPlayer();
	if (!P || !P->Camera || !ModelRoot || bFirstPersonHidden) return false;
	// The gun draws with its own field of view (TrailStart has the same sum the other way round).
	const FTransform View = P->Camera->GetComponentTransform();
	const float HalfTan = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(P->Camera->FirstPersonFieldOfView, 1.f, 170.f)) * 0.5f);
	auto Seen = [&](const FVector& World, FVector2D& Out) -> bool
	{
		const FVector V = View.InverseTransformPositionNoScale(World);
		if (V.X < 0.1f) return false;
		Out = FVector2D(V.Y, -V.Z) / (V.X * HalfTan);
		return true;
	};
	const FVector Sight = ModelRoot->GetComponentLocation();
	FVector2D Above;
	if (!Seen(Sight, OutAt) || !Seen(Sight + ModelRoot->GetUpVector() * 2.f, Above)) return false;
	const FVector2D Up = Above - OutAt;
	OutRoll = FMath::Atan2(Up.X, -Up.Y);
	return true;
}

void UWWOWeaponComponent::UpdateReticleTarget()
{
	const AWWOCharacter* P = GetPlayer();
	FHitResult Hit;
	ReticleTarget = nullptr;
	if (CastRayTo(P->EyeLocation() + P->AimForward() * Weapon->FireRange * WWO::M, Hit))
	{
		const IWWOTargetable* Target = Cast<IWWOTargetable>(Hit.GetActor());
		if (Target && !Target->IsDead()) ReticleTarget = Hit.GetActor();
	}
	bTargetUnderReticle = ReticleTarget.IsValid();
	// The faint tint over the enemy under the reticle (moves with it, off when nothing's there).
	AActor* Now = P->TargetHighlight > 0.f ? ReticleTarget.Get() : nullptr;
	if (Cast<AWWOCharacter>(Now)) Now = nullptr; // players flash an outline when marked instead (AWWOCharacter::UpdateTeamTint)
	if (Now != Highlighted.Get())
	{
		if (UWWOBodyComponent* Old = Highlighted.IsValid() ? Highlighted->FindComponentByClass<UWWOBodyComponent>() : nullptr) Old->SetHighlight(0.f);
		Highlighted = Now;
	}
	if (UWWOBodyComponent* Body = Now ? Now->FindComponentByClass<UWWOBodyComponent>() : nullptr) Body->SetHighlight(P->TargetHighlight);
	if (UStaticMeshComponent* Dot = Part("SightDot"))
	{
		if (UMaterialInstanceDynamic* Mat = Cast<UMaterialInstanceDynamic>(Dot->GetMaterial(0)))
		{
			Mat->SetVectorParameterValue("Color", bTargetUnderReticle ? SightOnTargetColor : SightColor);
		}
	}
}

void UWWOWeaponComponent::UpdateMarks(APlayerController* PC)
{
	// Who is marked: a target is picked up when the reticle comes over it (at the hip) or as soon as
	// it is inside the marker range (aimed in: within HealthBarRadius of the reticle on screen, and
	// in sight), and dropped when it leaves the marker range. Enemies show their bars while marked.
	// A PvP player's outline flashes once at the moment they're picked up (Marked holds when;
	// AWWOCharacter::UpdateTeamTint plays it). Teammates' bars show whenever they're in sight
	// (MatesInSight). Nobody is marked in the ult's third person.
	AWWOCharacter* Pl = GetPlayer();
	UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(Pl);
	const ULocalPlayer* LP = PC ? PC->GetLocalPlayer() : nullptr;
	TMap<TWeakObjectPtr<const AActor>, double> StillMarked;
	MatesInSight.Reset();
	if (!WS || !LP || !LP->ViewportClient || Pl->Ult->bActive)
	{
		Marked = MoveTemp(StillMarked);
		return;
	}
	// The marker range is in HUD pixels, measured from the HUD's reticle: the HUD's 1600 x 900 layout
	// over this player's part of the screen (UWWOHUDWidget::NativePaint), worked out the same way here.
	// The HUD widget is as big as the player's view rectangle in whole pixels (the one the engine
	// lays the player's widgets out in, SGameLayerManager), in Slate units.
	FSceneViewProjectionData Projection;
	if (!LP->ViewportClient->Viewport || !LP->GetProjectionData(LP->ViewportClient->Viewport, Projection))
	{
		Marked = MoveTemp(StillMarked);
		return;
	}
	const FIntRect ViewRect = Projection.GetConstrainedViewRect();
	const FVector2D Local = FVector2D(ViewRect.Width(), ViewRect.Height()) / FMath::Max(UWidgetLayoutLibrary::GetViewportScale(PC), 0.01f);
	const float S = WWOHUD::Scale(Local);
	if (S <= 0.f)
	{
		Marked = MoveTemp(StillMarked);
		return;
	}
	const FVector2D Size = Local / S;
	const float DropPx = Size.Y * Pl->ReticleDrop; // the low reticle
	const FVector2D AimAt = Size * 0.5f + FVector2D(0.f, DropPx);
	auto ToScreen = [PC, S](const FVector& World, FVector2D& Out2D) -> bool
	{
		FVector2D Screen;
		if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, World, Screen, true)) return false;
		Out2D = Screen / S;
		return true;
	};
	const AActor* Under = ReticleTarget.Get();
	const bool bAimedIn = AdsAmount > 0.5f;
	const double MarkNow = Pl->GetWorld()->GetTimeSeconds();
	// The range widens as you aim in (eased with the aim, so it doesn't jump).
	const float MarkRange = Pl->HealthBarRadius * FMath::Lerp(1.f, FMath::Max(Pl->HealthBarAdsScale, 1.f), AdsAmount);
	auto Track = [&](const AActor* Actor, const IWWOTargetable* Target, bool& bInSight) -> bool
	{
		const FVector Middle = Target->FeetLocation() + FVector(0, 0, Target->GetBodyHeight() * 0.5f);
		FVector2D At;
		FHitResult Hit;
		bInSight = Actor == Under || !CastRayTo(Middle, Hit) || Hit.GetActor() == Actor;
		const bool bInRange = Actor == Under
			|| (MarkRange > 0.f && bInSight && ToScreen(Middle, At) && FVector2D::Distance(At, AimAt) <= MarkRange);
		const double* Since = Marked.Find(Actor);
		if (Since ? bInRange : (Actor == Under || (bAimedIn && bInRange)))
		{
			StillMarked.Add(Actor, Since ? *Since : MarkNow);
			return true;
		}
		return false;
	};
	for (AActor* Actor : WS->LivingTargets(Pl))
	{
		const IWWOTargetable* Target = Cast<IWWOTargetable>(Actor);
		bool bInSight = false;
		if (Target && !Target->IsDead()) Track(Actor, Target, bInSight);
	}
	if (UWWOWorldSubsystem::IsPvP(Pl))
	{
		for (AWWOCharacter* Mate : WS->GetPlayers())
		{
			if (Mate == Pl || Mate->Health->bDead || UWWOWorldSubsystem::CanHurt(Pl, Mate)) continue;
			bool bInSight = false;
			Track(Mate, Mate, bInSight);
			if (bInSight) MatesInSight.Add(Mate);
		}
	}
	Marked = MoveTemp(StillMarked);
}
