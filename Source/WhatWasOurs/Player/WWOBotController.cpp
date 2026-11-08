#include "Player/WWOBotController.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
#include "Systems/WWOWorldSubsystem.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"
#include "Engine/World.h"

AWWOBotController::AWWOBotController()
{
	bWantsPlayerState = true; // a team and a score like any player
	PrimaryActorTick.bCanEverTick = true;
}

void AWWOBotController::PickGoal()
{
	// A random spawn spot to wander to.
	TArray<FVector> Spots;
	for (TActorIterator<ATargetPoint> It(GetWorld()); It; ++It) if (It->ActorHasTag("EnemySpawn")) Spots.Add(It->GetActorLocation());
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It) Spots.Add(It->GetActorLocation());
	Goal = Spots.Num() > 0 ? Spots[FMath::RandRange(0, Spots.Num() - 1)] : FVector::ZeroVector;
}

void AWWOBotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Presses last one frame (the character reads them during its own tick).
	for (bool& J : Just) J = false;
	AWWOCharacter* Me = Cast<AWWOCharacter>(GetPawn());
	if (!Me || Me->Health->bDead)
	{
		for (bool& H : Held) H = false;
		Move = FVector2D::ZeroVector;
		return;
	}
	Clock += DeltaSeconds;
	const FVector Eye = Me->EyeLocation();
	// Every so often: the nearest player of the other team in sight.
	if ((ThinkLeft -= DeltaSeconds) <= 0.f)
	{
		ThinkLeft = 0.4f;
		Foe = nullptr;
		float Best = 45.f * WWO::M;
		for (AActor* A : UWWOWorldSubsystem::Get(this)->LivingTargets(Me))
		{
			const AWWOCharacter* Other = Cast<AWWOCharacter>(A);
			if (!Other) continue;
			const float D = FVector::Dist(Other->GetActorLocation(), Me->GetActorLocation());
			if (D >= Best) continue;
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOBotSight), false, Me);
			if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, Other->EyeLocation(), ECC_Visibility, Params) && Hit.GetActor() != Other) continue;
			Best = D;
			Foe = A;
		}
		if (Goal.IsZero() || FVector::Dist2D(Goal, Me->GetActorLocation()) < 3.f * WWO::M) PickGoal();
	}
	auto Face = [Me](const FVector& Dir, float Noise)
	{
		const FVector D = Dir.GetSafeNormal();
		Me->SetViewRotation(FMath::Asin(D.Z) + FMath::DegreesToRadians(FMath::FRandRange(-Noise, Noise)), D.Rotation().Yaw + FMath::FRandRange(-Noise, Noise));
	};
	Held[(int32)EWWOAction::Fire] = false;
	if (const AWWOCharacter* Target = Cast<AWWOCharacter>(Foe.Get()); Target && !Target->Health->bDead)
	{
		// Fight: aim in at the chest, strafe, keep some distance, fire on a semi-auto rhythm.
		const FVector To = Target->GetAimParts().Last().Position - Eye;
		Face(To, 2.5f);
		Held[(int32)EWWOAction::Aim] = true;
		Held[(int32)EWWOAction::Sprint] = false;
		if ((StrafeLeft -= DeltaSeconds) <= 0.f) { StrafeLeft = FMath::FRandRange(0.6f, 1.6f); StrafeDir = FMath::RandBool() ? 1.f : -1.f; }
		const float Dist = To.Size() / WWO::M;
		Move = FVector2D(StrafeDir, Dist > 14.f ? 1.f : (Dist < 5.f ? -1.f : 0.f));
		if (Clock >= NextShot)
		{
			NextShot = Clock + FMath::FRandRange(0.3f, 0.6f);
			Press(EWWOAction::Fire);
		}
	}
	else
	{
		// Wander: run to the goal, jumping now and then (and to get unstuck).
		Held[(int32)EWWOAction::Aim] = false;
		FVector To = Goal - Me->GetActorLocation();
		To.Z = 0.f;
		Face(To, 0.f);
		Move = FVector2D(0.f, 1.f);
		if (!Held[(int32)EWWOAction::Sprint]) Press(EWWOAction::Sprint);
		const float Moved = FVector::Dist2D(LastSpot, Me->GetActorLocation());
		LastSpot = Me->GetActorLocation();
		Stuck = Moved < 50.f * DeltaSeconds ? Stuck + DeltaSeconds : 0.f;
		if (Stuck > 1.f) { PickGoal(); Stuck = 0.f; Press(EWWOAction::Jump); }
	}
	if (Clock >= NextJump)
	{
		NextJump = Clock + FMath::FRandRange(2.f, 6.f);
		Press(EWWOAction::Jump);
	}
	if (Just[(int32)EWWOAction::Jump]) Held[(int32)EWWOAction::Jump] = true;
	else Held[(int32)EWWOAction::Jump] = false;
}
