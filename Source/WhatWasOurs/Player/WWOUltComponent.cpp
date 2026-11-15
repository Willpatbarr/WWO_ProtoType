#include "Player/WWOUltComponent.h"
#include "WWOSwitches.h"
#include "Systems/WWOWorldSubsystem.h"
#include "GameFramework/GameStateBase.h"
#include "Systems/WWOSound.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOBotController.h"
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
	UWWOSound::PlayFor(P, "ult");
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	const UWWOFloraData* Flora = Loadout ? Loadout->GetFlora() : nullptr;
	Kind = !Flora ? EKind::Blast : (Flora->Ult == EWWOUltKind::Veil ? EKind::Veil : (Flora->Ult == EWWOUltKind::Entrench ? EKind::Entrench : EKind::Blast));
	Color = Loadout && Loadout->GetGrenade() ? Loadout->GetGrenade()->Color : FLinearColor(1.f, 0.6f, 0.2f);
	P->bUlting = true;
	P->Weapon->Interrupt(); // the gun isn't updated during an ult: nothing half-done waits for its end
	UCharacterMovementComponent* Move = P->GetCharacterMovement();
	Move->Velocity = FVector(0, 0, FMath::Min(Move->Velocity.Z, 0.f));
	const bool bMine = P->IsLocallyControlled() && !P->GetBot(); // the camera and first-person gun are only the player's own
	if (bMine) P->Weapon->SetVisibility(false, true);
	if (bMine) P->Body->SetShadowOnly(false); // the whole body (and its grafts) shows in third person
	Clock = 0.f;
	bExploded = false;
	bCast = false;
	bCharging = false;
	bGlidingBack = false;
	GlideFrom = FirstPersonTransform();
	GlideTo = ThirdPersonTransform();
	UltCamera->SetWorldTransform(GlideFrom);
	UltCamera->SetFieldOfView(P->Camera->FieldOfView);
	if (bMine)
	{
		P->Camera->Deactivate();
		UltCamera->Activate();
	}
	if (Kind == EKind::Blast)
	{
		Blend = CameraBlend;
		CastAt = FMath::Max(WindupTime - CastLead, Blend);
		ExplodeAt = FMath::Max(WindupTime, Blend + CastLead);
		FinishAt = ExplodeAt + HoldTime;
		// Godot: Spell_Simple_Enter as the camera glides out, then Spell_Simple_Idle once the
		// enter has played out (or right before the cast), then Spell_Simple_Shoot.
		P->Body->Play("Spell_Simple_Enter", 1.f, 0.1f);
		const float EnterLeft = FMath::Max(P->Body->AnimLength("Spell_Simple_Enter") - Blend, 0.f);
		ChargeAt = Blend + FMath::Min(EnterLeft, FMath::Max(WindupTime - Blend - CastLead, 0.f));
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
		OrbLight->SetCastShadows(false); // like every other effect light: a shadowed point light redraws shadow maps around it
		OrbLight->RegisterComponent();
		PlaceOrb();
	}
	else
	{
		Blend = VeilCameraBlend;
		ExplodeAt = Blend; // the release
		FinishAt = Blend + VeilHold;
		P->Body->Play("Spell_Simple_Shoot", 1.6f, 0.05f);
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
	// Online the host's copy does the world part (blast, fog, roots); a joiner's own copy only
	// starts its veil or entrench (they change how it moves and what hits it).
	const bool bHost = P->HasAuthority();
	if (Kind == EKind::Blast)
	{
		if (Orb)
		{
			// The light goes with the orb (destroying the orb alone left its light behind, lit and
			// casting shadows, after every Blast ult).
			if (OrbLight) OrbLight->DestroyComponent();
			Orb->DestroyComponent();
			Orb = nullptr;
			OrbLight = nullptr;
		}
		if (!bHost) return;
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
		if (bHost) AWWOSporeFog::SpawnFog(GetWorld(), P->GetActorLocation(), VeilFogRadius, VeilFogTime, true, P);
		P->VeilLeft = VeilTime;
	}
	else
	{
		P->EntrenchLeft = EntrenchTime;
		AWWOEntrenchRoots::Spawn(GetWorld(), P, EntrenchTime); // on this machine (looks only)
		if (bHost && GetNetMode() == NM_ListenServer) P->MulticastEntrenchRoots(EntrenchTime); // and on the others
	}
}

void UWWOUltComponent::Abort(bool bRefused)
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	if (bActive)
	{
		if (Orb)
		{
			if (OrbLight) OrbLight->DestroyComponent();
			Orb->DestroyComponent();
			Orb = nullptr;
			OrbLight = nullptr;
		}
		Finish();
	}
	if (bRefused)
	{
		Charge = 1.f;
		P->VeilLeft = 0.f;
		P->EntrenchLeft = 0.f;
	}
}

void UWWOUltComponent::Finish()
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	if (P->IsLocallyControlled() && !P->GetBot()) // a bot has no camera or first-person gun
	{
		UltCamera->Deactivate();
		P->Camera->Activate();
		P->Body->SetShadowOnly(true);
		P->Weapon->SetVisibility(true, true);
	}
	P->bUlting = false;
	P->ResetInputEdges(); // a button held through the ult doesn't fire on release
	bActive = false;
}

void UWWOUltComponent::UpdateUlt(float DeltaSeconds)
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	if (!bStartChecked && GetWorld()->GetGameState())
	{
		// Alone and in co-op the ult starts full (handy for trying things); a PvP match starts everyone empty.
		bStartChecked = true;
		if (UWWOWorldSubsystem::IsPvP(this) && !WWOSwitches::HasAutoTestText()) Charge = 0.f;
	}
	if (!bActive)
	{
		if (Charge < 1.f) Charge = FMath::Min(Charge + DeltaSeconds / FMath::Max(ChargeTime, 0.1f), 1.f);
		AWWOPlayerController* PC = P->GetWWOController();
		const AWWOBotController* Bot = P->GetBot(); // a bot presses its own button
		const bool bPressed = Bot ? Bot->WasJustPressed(EWWOAction::Ult)
			: (PC && P->IsLocallyControlled() && (PC->WasJustPressed(EWWOAction::Ult) || PC->ConsumeChord(EWWOChord::Ult))); // F, or L1 + R1
		if (bPressed && ActivateUlt())
		{
			if (GetNetMode() == NM_Client) P->ServerActivateUlt(); // the host runs it too (the blast, fog or roots are its)
		}
		return;
	}
	Clock += DeltaSeconds;
	if (Orb)
	{
		PlaceOrb();
		const float Grow = FMath::Clamp(Clock / FMath::Max(WindupTime - CastLead, 0.1f), 0.f, 1.f);
		Orb->SetWorldScale3D(FVector(FMath::Max(OrbSize * WWO::M * Grow / 50.f, 0.01f)));
	}
	if (Kind == EKind::Blast && !bCharging && !bCast && Clock >= ChargeAt && ChargeAt < CastAt)
	{
		bCharging = true;
		P->Body->Play("Spell_Simple_Idle", 1.f, 0.1f);
	}
	if (Kind == EKind::Blast && !bCast && Clock >= CastAt)
	{
		bCast = true;
		P->Body->Play("Spell_Simple_Shoot", 1.f, 0.1f);
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
		P->Body->Play("Idle", 1.f, Kind == EKind::Blast ? 0.3f : 0.2f);
	}
	const float Back = (Clock - FinishAt) / FMath::Max(Blend, 0.01f);
	const FTransform To = FirstPersonTransform();
	const float T = Ease(Back);
	UltCamera->SetWorldLocation(FMath::Lerp(GlideFrom.GetLocation(), To.GetLocation(), T));
	UltCamera->SetWorldRotation(FQuat::Slerp(GlideFrom.GetRotation(), To.GetRotation(), T));
	if (Back >= 1.f) Finish();
}
