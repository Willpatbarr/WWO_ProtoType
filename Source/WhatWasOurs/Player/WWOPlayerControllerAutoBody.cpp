// -WWOAutoTest: the body animation steps (also alone with -WWOAutoTestBody). On a floor built
// high above the arena (no crates, no enemies), a row of mannequins holds the body states the
// port fills in from the Godot animations: crouch idle and walk, the slide, the Soldier's aim
// with a hit flinch, the death and the knockback death, the Swarm's hunched run, the ult's
// charge and cast and the Tank's slam warning. One screenshot of the row (30_body_row), one close
// up of each (31_body_*), then your own body in third person crouched, crouch walking and sliding
// (32_player_*), driven by the real inputs, and last the first-person arms in slow motion: the
// grenade throw and the punch (33_fp_*). Logs `AutoTest body:` with the clip each one plays.

#include "Player/WWOPlayerController.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOWeaponComponent.h"
#include "Characters/WWOBodyComponent.h"
#include "FX/WWOFX.h"
#include "WWOTypes.h"
#include "WhatWasOurs.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	struct FBodyPose
	{
		const TCHAR* Name;
		FName Anim;
		float Speed;
		FLinearColor Body, Joint;
		float Scale;
		float Lead; // one-off clips start this long before each screenshot (0: started once, held)
	};
	const FLinearColor PlayerBlue(0.3f, 0.42f, 0.55f), Joint(0.15f, 0.16f, 0.18f);
	const FLinearColor SoldierRed(0.55f, 0.15f, 0.13f), SwarmGreen(0.62f, 0.68f, 0.2f), TankOlive(0.3f, 0.33f, 0.22f);
	const FBodyPose Poses[] = {
		{ TEXT("crouch_idle"), "Crouch_Idle", 1.f, PlayerBlue, Joint, 1.f, 0.f },
		{ TEXT("crouch_walk"), "Crouch_Fwd", 1.f, PlayerBlue, Joint, 1.f, 0.f },
		{ TEXT("slide"), "Slide", 1.f, PlayerBlue, Joint, 1.f, 0.f },
		{ TEXT("soldier_aim"), NAME_None, 1.f, SoldierRed, Joint, 1.f, 0.f },
		{ TEXT("soldier_flinch"), NAME_None, 1.f, SoldierRed, Joint, 1.f, 0.12f },
		{ TEXT("death"), "Death01", 1.f, SoldierRed, Joint, 1.f, 0.f },
		{ TEXT("knockback_death"), "Hit_Knockback", 1.f, SoldierRed, Joint, 1.f, 0.f },
		{ TEXT("swarm_run"), "Sprint", 1.f, SwarmGreen, FLinearColor(0.2f, 0.15f, 0.1f), 0.95f, 0.f },
		{ TEXT("ult_charge"), "Spell_Simple_Idle", 1.f, PlayerBlue, Joint, 1.f, 0.f },
		{ TEXT("ult_cast"), "Spell_Simple_Shoot", 1.f, PlayerBlue, Joint, 1.f, 0.2f },
		{ TEXT("tank_slam_warning"), "Spell_Simple_Enter", 0.6f, TankOlive, FLinearColor(0.12f, 0.12f, 0.14f), 1.55f, 0.f },
	};
	constexpr int32 NumPoses = UE_ARRAY_COUNT(Poses);
	constexpr float Spacing = 160.f; // cm between the mannequins
	constexpr float RowShotAt = 3.f, CloseShotEvery = 0.8f;
}

bool AWWOPlayerController::UpdateAutoBodyTest(AWWOCharacter* P, float DeltaTime)
{
	UWorld* World = GetWorld();
	const float Before = AutoBodyClock;
	AutoBodyClock += DeltaTime;
	auto Crossed = [&](float T) { return Before < T && AutoBodyClock >= T; };
	auto PoseAt = [&](int32 i) { return AutoBodyOrigin + FVector(0.f, (i - (NumPoses - 1) * 0.5f) * Spacing, 0.f); };
	auto Look = [&](const FVector& From, const FVector& At, float Fov = 70.f)
	{
		if (!AutoBodyCamera) return;
		AutoBodyCamera->SetActorLocationAndRotation(From, (At - From).Rotation());
		AutoBodyCamera->GetCameraComponent()->SetFieldOfView(Fov);
	};
	auto Restart = [&](int32 i)
	{
		const FBodyPose& Pose = Poses[i];
		UWWOBodyComponent* Body = AutoBodies.IsValidIndex(i) && AutoBodies[i] ? AutoBodies[i]->FindComponentByClass<UWWOBodyComponent>() : nullptr;
		if (!Body) return;
		if (Pose.Anim.IsNone()) Body->PlayUpperShot("Hit_Chest", 0.05f, 0.2f); // the Soldier: aiming, flinches (Lead > 0 only)
		else Body->Play(Pose.Anim, Pose.Speed, 0.f);
	};

	if (AutoBodyStep == 0)
	{
		// The studio: a 60 m floor 40 m above the player (the arena's sky and sun light it).
		P->Health->GodMode = 1.f;
		AutoBodyOrigin = P->GetActorLocation() + FVector(0.f, 0.f, 4000.f);
		AActor* Floor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(AutoBodyOrigin));
		UStaticMeshComponent* Slab = WWOFX::AddShape(Floor, nullptr, WWOFX::Cube(), TEXT("M_WWO_Solid"), FLinearColor(0.42f, 0.42f, 0.4f));
		Floor->SetRootComponent(Slab);
		Slab->SetWorldLocation(AutoBodyOrigin - FVector(0.f, 0.f, 50.f));
		Slab->SetWorldScale3D(FVector(60.f, 60.f, 1.f));
		Slab->SetCollisionProfileName(TEXT("BlockAll"));
		AutoBodyProps.Add(Floor);
		for (int32 i = 0; i < NumPoses; ++i)
		{
			const FBodyPose& Pose = Poses[i];
			AActor* A = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FRotator(0.f, 180.f, 0.f), PoseAt(i))); // facing the camera
			USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(A, TEXT("Mesh"));
			A->SetRootComponent(Mesh);
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->RegisterComponent();
			Mesh->SetWorldLocationAndRotation(PoseAt(i), FRotator(0.f, 180.f - 90.f, 0.f)); // the mannequin faces +Y
			UWWOBodyComponent* Body = NewObject<UWWOBodyComponent>(A, TEXT("Body"));
			Body->MeshOverride = Mesh;
			Body->BodyColor = Pose.Body;
			Body->JointColor = Pose.Joint;
			Body->ModelScale = Pose.Scale;
			// The character model each pose belongs to (Config/CharacterModels.json): the enemy's, or
			// one flora model per player pose so all three show.
			const FString PoseName(Pose.Name);
			static const FName PlayerModels[] = { "voltvine", "ironbark", "veilspore" };
			Body->ModelKey = PoseName.StartsWith(TEXT("soldier")) || PoseName.Contains(TEXT("death")) ? FName("soldier")
				: PoseName.StartsWith(TEXT("swarm")) ? FName("swarm") : PoseName.StartsWith(TEXT("tank")) ? FName("tank") : PlayerModels[i % 3];
			Body->RegisterComponent(); // begins play: Manny, the anim instance, Idle
			if (FCString::Strcmp(Pose.Name, TEXT("slide")) == 0)
			{
				Body->Play("Slide_Start", 1.6f, 0.08f); // as the player does: drop into it, then hold it
				Body->Queue("Slide");
			}
			else if (Pose.Anim.IsNone())
			{
				Body->SetMix("Idle", "Walk", 0.f, 1.f);
				Body->SetUpperBody("Pistol_Aim_Neutral", 1.f);
			}
			else
			{
				Body->Play(Pose.Anim, Pose.Speed, 0.f);
			}
			if (FCString::Strcmp(Pose.Name, TEXT("swarm_run")) == 0) Body->SetHunch(55.f, 45.f);
			AutoBodies.Add(A);
		}
		AutoBodyCamera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform(AutoBodyOrigin));
		AutoBodyCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		Look(AutoBodyOrigin + FVector(-1150.f, 0.f, 260.f), AutoBodyOrigin + FVector(0.f, 0.f, 80.f), 75.f);
		SetViewTarget(AutoBodyCamera);
		// You wait at the far end of the floor, out of the row's way.
		P->TeleportTo(AutoBodyOrigin + FVector(1500.f, -2000.f, P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), FRotator::ZeroRotator);
		P->GetCharacterMovement()->StopMovementImmediately();
		AutoBodyStep = 1;
		return false;
	}

	// The row, then a close up of each.
	const int32 Closeups = NumPoses;
	for (int32 i = -1; i < Closeups; ++i)
	{
		const float ShotAt = RowShotAt + (i + 1) * CloseShotEvery;
		for (int32 k = 0; k < NumPoses; ++k)
		{
			if (Poses[k].Lead > 0.f && Crossed(ShotAt - Poses[k].Lead)) Restart(k);
		}
		if (Crossed(ShotAt - 0.05f))
		{
			if (i < 0) Look(AutoBodyOrigin + FVector(-1150.f, 0.f, 260.f), AutoBodyOrigin + FVector(0.f, 0.f, 80.f), 75.f);
			else
			{
				const bool bLow = FCString::Strcmp(Poses[i].Name, TEXT("slide")) == 0 || FCString::Strncmp(Poses[i].Name, TEXT("death"), 5) == 0
					|| FCString::Strcmp(Poses[i].Name, TEXT("knockback_death")) == 0;
				const float S = Poses[i].Scale;
				const FVector At = PoseAt(i) + FVector(0.f, 0.f, (bLow ? 30.f : 95.f) * S);
				Look(At + FVector(-260.f, -170.f, bLow ? 120.f : 40.f) * S, At, 55.f);
			}
		}
		if (Crossed(ShotAt))
		{
			if (i < 0)
			{
				Shot(TEXT("30_body_row"));
				for (int32 k = 0; k < NumPoses; ++k)
				{
					const UWWOBodyComponent* Body = AutoBodies.IsValidIndex(k) && AutoBodies[k] ? AutoBodies[k]->FindComponentByClass<UWWOBodyComponent>() : nullptr;
					UE_LOG(LogWWO, Display, TEXT("AutoTest body: %s plays %s at %.2f s"), Poses[k].Name, Body ? *Body->CurrentAnim().ToString() : TEXT("-"), Body ? Body->CurrentAnimTime() : 0.f);
				}
			}
			else Shot(*FString::Printf(TEXT("31_body_%02d_%s"), i, Poses[i].Name));
		}
	}

	// Your own body, third person, on the floor: crouch, crouch walk, then a sprint into a slide.
	const float PlayerAt = RowShotAt + (Closeups + 1) * CloseShotEvery + 0.5f;
	auto LookAtPlayer = [&](float Side)
	{
		const FVector Feet = P->GetActorLocation() - FVector(0.f, 0.f, P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		const FVector Fwd = P->GetActorForwardVector(), Right = P->GetActorRightVector();
		Look(Feet + Fwd * 260.f + Right * Side * 200.f + FVector(0.f, 0.f, 130.f), Feet + FVector(0.f, 0.f, 60.f), 60.f);
	};
	auto LogPlayer = [&](const TCHAR* What)
	{
		UE_LOG(LogWWO, Display, TEXT("AutoTest body: player %s plays %s (crouched %d sliding %d, %.2f m/s)"), What, *P->Body->CurrentAnim().ToString(),
			P->bIsCrouched, P->bSliding, P->GetVelocity().Size2D() / WWO::M);
	};
	if (Crossed(PlayerAt))
	{
		for (AActor* A : AutoBodies) if (A) A->SetActorHiddenInGame(true);
		P->Body->SetShadowOnly(false); // the whole body shows, as in the ult's third person
		P->Weapon->SetVisibility(false, true);
		P->SetViewRotation(0.f, 90.f); // facing +Y, down the empty half of the floor
		Held[(int32)EWWOAction::Crouch] = true;
	}
	if (Crossed(PlayerAt + 0.1f)) Held[(int32)EWWOAction::Crouch] = false;
	if (Crossed(PlayerAt + 0.95f)) LookAtPlayer(1.f);
	if (Crossed(PlayerAt + 1.f)) { LogPlayer(TEXT("crouched")); Shot(TEXT("32_player_crouch")); MoveValue = FVector2D(0.f, 1.f); }
	if (Crossed(PlayerAt + 1.75f)) LookAtPlayer(1.f);
	if (Crossed(PlayerAt + 1.8f)) { LogPlayer(TEXT("crouch walking")); Shot(TEXT("32_player_crouch_walk")); JustPressed[(int32)EWWOAction::Sprint] = true; }
	if (Crossed(PlayerAt + 2.6f)) { LogPlayer(TEXT("sprinting")); Held[(int32)EWWOAction::Crouch] = true; }
	if (Crossed(PlayerAt + 2.7f)) Held[(int32)EWWOAction::Crouch] = false;
	if (Crossed(PlayerAt + 3.05f)) LookAtPlayer(1.f);
	if (Crossed(PlayerAt + 3.1f)) { LogPlayer(TEXT("sliding")); Shot(TEXT("32_player_slide")); }
	if (Crossed(PlayerAt + 3.35f)) LookAtPlayer(-1.f);
	if (Crossed(PlayerAt + 3.4f)) { LogPlayer(TEXT("sliding")); Shot(TEXT("32_player_slide_late")); MoveValue = FVector2D::ZeroVector; }
	if (Crossed(PlayerAt + 4.5f))
	{
		P->Body->SetShadowOnly(true);
		P->Weapon->SetVisibility(true, true);
		SetViewTarget(P);
		P->SetViewRotation(0.f, 0.f);
	}
	// The first-person arms in slow motion (game time at 1/20, this clock is real time): a grenade
	// throw (the left arm), then a punch at nothing (the right), a shot every 0.05 s of game time.
	constexpr float Slow = 0.05f;
	const float ArmsAt = PlayerAt + 5.f, PunchAt = ArmsAt + 10.f, ArmsEnd = PunchAt + 12.f;
	if (Crossed(ArmsAt))
	{
		UGameplayStatics::SetGlobalTimeDilation(World, Slow);
		P->Weapon->StartThrowPose(FLinearColor(0.3f, 0.6f, 1.f));
	}
	if (Crossed(PunchAt)) JustPressed[(int32)EWWOAction::Melee] = true;
	for (int32 k = 1; k <= 9; ++k)
	{
		if (Crossed(ArmsAt + k)) Shot(*FString::Printf(TEXT("33_fp_throw_%02d"), k * 5));
		if (Crossed(PunchAt + k)) Shot(*FString::Printf(TEXT("33_fp_punch_%02d"), k * 5));
	}
	if (Crossed(ArmsEnd)) UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
	if (AutoBodyClock >= ArmsEnd)
	{
		for (AActor* A : AutoBodies) if (A) A->Destroy();
		for (AActor* A : AutoBodyProps) if (A) A->Destroy();
		if (AutoBodyCamera) AutoBodyCamera->Destroy();
		AutoBodies.Reset();
		AutoBodyProps.Reset();
		AutoBodyCamera = nullptr;
		P->TeleportTo(AutoBodyOrigin - FVector(0.f, 0.f, 4000.f - 5.f), P->GetActorRotation());
		P->Health->GodMode = 0.f;
		return true;
	}
	return false;
}
