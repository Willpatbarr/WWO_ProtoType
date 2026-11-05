#include "Player/WWOUltComponent.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Characters/WWOBodyComponent.h"
#include "Weapons/WWOGrenade.h"
#include "Flora/WWOSporeFog.h"
#include "Flora/WWOFloraEffects.h"
#include "FX/WWOFX.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

namespace
{
	constexpr float CastLead = 0.2f; // the throw animation starts this long before the blast
	constexpr float OrbSize = 0.25f; // meters, fully charged
}

UWWOUltComponent::UWWOUltComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UWWOUltComponent::BeginPlay()
{
	Super::BeginPlay();
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	UltCamera = NewObject<UCameraComponent>(P, TEXT("UltCamera"));
	UltCamera->SetupAttachment(P->GetRootComponent());
	UltCamera->SetUsingAbsoluteLocation(true);
	UltCamera->SetUsingAbsoluteRotation(true);
	UltCamera->bAutoActivate = false;
	UltCamera->RegisterComponent();
	UltCamera->Deactivate();
}

bool UWWOUltComponent::CanActivate() const
{
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	return IsReady() && !bActive && !P->Health->bDead && !P->IsMantling() && !P->IsLunging();
}

bool UWWOUltComponent::ActivateUlt()
{
	if (!CanActivate()) return false;
	Charge = 0.f;
	bActive = true;
	Begin();
	return true;
}

FTransform UWWOUltComponent::FirstPersonTransform() const
{
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	return P->Camera->GetComponentTransform();
}

FTransform UWWOUltComponent::ThirdPersonTransform() const
{
	// Behind and above you, over the right shoulder, looking at your chest. Pulled in if a
	// wall is in the way.
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	const FVector Eyes = P->EyeLocation();
	FVector Wanted = Eyes - P->GetActorForwardVector() * CameraDistance * WWO::M + FVector(0, 0, CameraHeight * WWO::M)
		+ P->GetActorRightVector() * CameraSide * WWO::M;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOUltCamera), false, P);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Eyes, Wanted, ECC_Camera, Params))
	{
		Wanted = Hit.Location + (Eyes - Wanted).GetSafeNormal() * 30.f;
	}
	const FVector LookAt = P->GetActorLocation() + FVector(0, 0, 20.f);
	return FTransform((LookAt - Wanted).Rotation(), Wanted);
}

void UWWOUltComponent::Begin()
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	const UWWOFloraData* Flora = Loadout ? Loadout->GetFlora() : nullptr;
	Kind = !Flora ? EKind::Blast : (Flora->Ult == EWWOUltKind::Veil ? EKind::Veil : (Flora->Ult == EWWOUltKind::Entrench ? EKind::Entrench : EKind::Blast));
	Color = Loadout && Loadout->GetGrenade() ? Loadout->GetGrenade()->Color : FLinearColor(1.f, 0.6f, 0.2f);
	P->bUlting = true;
	UCharacterMovementComponent* Move = P->GetCharacterMovement();
	Move->Velocity = FVector(0, 0, FMath::Min(Move->Velocity.Z, 0.f));
	P->Weapon->SetVisibility(false, true);
	P->Body->SetShadowOnly(false); // the whole body (and its grafts) shows in third person
	Clock = 0.f;
	bExploded = false;
	bCast = false;
	bGlidingBack = false;
	GlideFrom = FirstPersonTransform();
	GlideTo = ThirdPersonTransform();
	UltCamera->SetWorldTransform(GlideFrom);
	UltCamera->SetFieldOfView(P->Camera->FieldOfView);
	P->Camera->Deactivate();
	UltCamera->Activate();
	if (Kind == EKind::Blast)
	{
		Blend = CameraBlend;
		CastAt = FMath::Max(WindupTime - CastLead, Blend);
		ExplodeAt = FMath::Max(WindupTime, Blend + CastLead);
		FinishAt = ExplodeAt + HoldTime;
		P->Body->PlayAction(EWWOBodyAction::UltCharge, 1.f);
		// The orb that grows between your hands.
		Orb = WWOFX::AddShape(P, nullptr, WWOFX::Sphere(), TEXT("M_WWO_Solid"), Color, 4.f);
		Orb->SetUsingAbsoluteLocation(true);
		Orb->SetUsingAbsoluteScale(true);
		Orb->SetWorldScale3D(FVector(0.01f));
		Orb->SetCastShadow(false);
		OrbLight = NewObject<UPointLightComponent>(P);
		OrbLight->SetupAttachment(Orb);
		OrbLight->SetLightColor(Color);
		OrbLight->SetIntensityUnits(ELightUnits::Unitless);
		OrbLight->SetIntensity(2.f * 1500.f);
		OrbLight->SetAttenuationRadius(300.f);
		OrbLight->SetUsingAbsoluteScale(true);
		OrbLight->RegisterComponent();
		PlaceOrb();
	}
	else
	{
		Blend = VeilCameraBlend;
		ExplodeAt = Blend; // the release
		FinishAt = Blend + VeilHold;
		P->Body->PlayAction(EWWOBodyAction::UltCast, 1.6f);
	}
}

void UWWOUltComponent::PlaceOrb()
{
	// Between the hands, a little in front of them.
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	const USkeletalMeshComponent* Mesh = P->GetMesh();
	const FVector Mid = (Mesh->GetSocketLocation("hand_l") + Mesh->GetSocketLocation("hand_r")) * 0.5f;
	Orb->SetWorldLocation(Mid + P->GetActorForwardVector() * 15.f);
}

void UWWOUltComponent::Release()
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	if (Kind == EKind::Blast)
	{
		if (Orb)
		{
			Orb->DestroyComponent();
			Orb = nullptr;
			OrbLight = nullptr;
		}
		// A grenade that goes off the moment it exists: same damage rules and blast look.
		FWWOGrenadeSpec Spec;
		Spec.Color = Color;
		Spec.Damage = Damage;
		Spec.BlastRadius = BlastRadius;
		Spec.Flavor = Loadout && Loadout->GetFlora() ? Loadout->GetFlora()->Id : NAME_None;
		AWWOGrenade::ExplodeAt(GetWorld(), Spec, P->GetActorLocation() + FVector(0, 0, 20.f), P);
	}
	else if (Kind == EKind::Veil)
	{
		AWWOSporeFog::SpawnFog(GetWorld(), P->GetActorLocation(), VeilFogRadius, VeilFogTime, true);
		P->VeilLeft = VeilTime;
	}
	else
	{
		P->EntrenchLeft = EntrenchTime;
		AWWOEntrenchRoots::Spawn(GetWorld(), P, EntrenchTime);
	}
}

void UWWOUltComponent::Finish()
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	UltCamera->Deactivate();
	P->Camera->Activate();
	P->Body->SetShadowOnly(true);
	P->Weapon->SetVisibility(true, true);
	P->bUlting = false;
	P->ResetInputEdges(); // a button held through the ult doesn't fire on release
	bActive = false;
}

void UWWOUltComponent::UpdateUlt(float DeltaSeconds)
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	if (!bActive)
	{
		if (Charge < 1.f) Charge = FMath::Min(Charge + DeltaSeconds / FMath::Max(ChargeTime, 0.1f), 1.f);
		AWWOPlayerController* PC = P->GetWWOController();
		if (PC && (PC->WasJustPressed(EWWOAction::Ult) || PC->ConsumeChord(EWWOChord::Ult))) ActivateUlt(); // F, or L1 + R1
		return;
	}
	Clock += DeltaSeconds;
	if (Orb)
	{
		PlaceOrb();
		const float Grow = FMath::Clamp(Clock / FMath::Max(WindupTime - CastLead, 0.1f), 0.f, 1.f);
		Orb->SetWorldScale3D(FVector(FMath::Max(OrbSize * WWO::M * Grow / 50.f, 0.01f)));
	}
	if (Kind == EKind::Blast && !bCast && Clock >= CastAt)
	{
		bCast = true;
		P->Body->PlayAction(EWWOBodyAction::UltCast, 1.f);
	}
	if (!bExploded && Clock >= ExplodeAt)
	{
		bExploded = true;
		Release();
	}
	// Camera: glide out, hold, glide back.
	auto Ease = [](float T) { T = FMath::Clamp(T, 0.f, 1.f); return -(FMath::Cos(UE_PI * T) - 1.f) * 0.5f; };
	if (Clock < FinishAt)
	{
		const float T = Ease(Clock / FMath::Max(Blend, 0.01f));
		UltCamera->SetWorldLocation(FMath::Lerp(GlideFrom.GetLocation(), GlideTo.GetLocation(), T));
		UltCamera->SetWorldRotation(FQuat::Slerp(GlideFrom.GetRotation(), GlideTo.GetRotation(), T));
		return;
	}
	if (!bGlidingBack)
	{
		bGlidingBack = true;
		GlideFrom = UltCamera->GetComponentTransform();
		P->Body->PlayAction(EWWOBodyAction::None, 1.f);
	}
	const float Back = (Clock - FinishAt) / FMath::Max(Blend, 0.01f);
	const FTransform To = FirstPersonTransform();
	const float T = Ease(Back);
	UltCamera->SetWorldLocation(FMath::Lerp(GlideFrom.GetLocation(), To.GetLocation(), T));
	UltCamera->SetWorldRotation(FQuat::Slerp(GlideFrom.GetRotation(), To.GetRotation(), T));
	if (Back >= 1.f) Finish();
}
