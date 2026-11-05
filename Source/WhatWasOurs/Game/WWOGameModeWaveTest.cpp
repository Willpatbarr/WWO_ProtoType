// The -WWOWaveTest run for AWWOGameMode (enemy, ally and wave behavior): plays all the waves with
// god mode on, moving you around the level, cuts each fight short after a while, and logs what
// every enemy and teammate is doing, flagging anyone stuck, stuck in the air, or not facing its
// target. Ends with a "WaveTest done" summary line and quits.

#include "Game/WWOGameMode.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
#include "Enemies/WWOEnemy.h"
#include "Enemies/WWOEnemyTypes.h"
#include "Enemies/WWOEnemyProjectile.h"
#include "Allies/WWOAlly.h"
#include "Systems/WWOWorldSubsystem.h"
#include "WhatWasOurs.h"
#include "WWOTypes.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "UnrealClient.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "EngineUtils.h"
#include "Components/PrimitiveComponent.h"

void AWWOGameMode::UpdateWaveTest(float DeltaSeconds)
{
	UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(this);
	AWWOCharacter* P = WS->GetPlayer();
	if (!P) return;
	P->Health->GodMode = 1.f;
	if (WaveTestHome.IsZero()) WaveTestHome = P->GetActorLocation();
	if ((!bWaveTestNavChecked || (Wave == 3 && WaveTestFight > 20.f && WaveTestFight - DeltaSeconds <= 20.f)) && State == EWWOWaveState::Spawning && WaveTestFight > 5.f)
	{
		// Can every spawn point reach you on the navmesh?
		bWaveTestNavChecked = true;
		// Anything movable that the navmesh is built around (it rebuilds around these at runtime).
		TMap<FString, int32> Relevant;
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			TInlineComponentArray<UPrimitiveComponent*> Prims(*It);
			for (const UPrimitiveComponent* Prim : Prims)
			{
				if (Prim->IsNavigationRelevant() && Prim->Mobility != EComponentMobility::Static)
				{
					Relevant.FindOrAdd(It->GetClass()->GetName() + TEXT(".") + Prim->GetName())++;
				}
			}
		}
		for (const TPair<FString, int32>& Pair : Relevant) UE_LOG(LogWWO, Display, TEXT("WaveTest nav: movable nav-relevant %s x%d"), *Pair.Key, Pair.Value);
		for (const AActor* Point : SpawnPoints())
		{
			const UNavigationPath* NavPath = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), Point->GetActorLocation(), P->GetActorLocation());
			UE_LOG(LogWWO, Display, TEXT("WaveTest nav: %s -> player: %s, %d points, length %.1f m%s"), *Point->GetActorLabel(),
				NavPath && NavPath->IsValid() ? TEXT("ok") : TEXT("NO PATH"), NavPath ? NavPath->PathPoints.Num() : 0,
				NavPath && NavPath->IsValid() ? NavPath->GetPathLength() / WWO::M : 0.f, NavPath && NavPath->IsPartial() ? TEXT(" PARTIAL") : TEXT(""));
		}
		// A map of the navmesh, 1 m cells, north up: '.' reaches you, 'x' is cut off, ' ' no navmesh.
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		if (const ARecastNavMesh* Recast = Nav ? Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance()) : nullptr)
		{
			UE_LOG(LogWWO, Display, TEXT("WaveTest nav: navmesh agent radius %.0f height %.0f step %.0f slope %.0f cell %.0f x %.0f"), Recast->AgentRadius,
				Recast->AgentHeight, Recast->GetAgentMaxStepHeight(ENavigationDataResolution::Default), Recast->AgentMaxSlope,
				Recast->GetCellSize(ENavigationDataResolution::Default), Recast->GetCellHeight(ENavigationDataResolution::Default));
		}
		FNavLocation Goal;
		if (Nav && Nav->ProjectPointToNavigation(P->GetActorLocation(), Goal, FVector(50.f, 50.f, 200.f)))
		{
			for (const float Height : { 0.f, 200.f, 400.f })
			{
				UE_LOG(LogWWO, Display, TEXT("WaveTest navmap at %.0f m:"), Height / WWO::M);
				for (float X = 3000.f; X >= -3000.f; X -= 100.f)
				{
					FString Row;
					bool bAny = false;
					for (float Y = -4800.f; Y <= 4800.f; Y += 100.f)
					{
						FNavLocation Here;
						TCHAR C = ' ';
						if (Nav->ProjectPointToNavigation(FVector(X, Y, Height + 10.f), Here, FVector(45.f, 45.f, 40.f)))
						{
							bAny = true;
							C = 'x';
							const UNavigationPath* Test = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), Here.Location, Goal.Location);
							if (Test && Test->IsValid() && !Test->IsPartial()) C = '.';
						}
						else
						{
							for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
							{
								if (FVector::Dist2D(It->GetActorLocation(), FVector(X, Y, 0.f)) < 100.f && FMath::Abs(It->GetActorLocation().Z - Height) < 200.f) C = 'o';
							}
						}
						Row.AppendChar(C);
					}
					if (bAny) UE_LOG(LogWWO, Display, TEXT("WaveTest navmap %+05.0f |%s|"), X, *Row);
				}
			}
		}
	}
	const FString Shots = FPaths::ProjectSavedDir() / TEXT("AutoTest") / TEXT("wave_");
	if (State == EWWOWaveState::Won)
	{
		UE_LOG(LogWWO, Display, TEXT("WaveTest done: cleared %d waves; %d stuck, %d blocked by others, %d holding range, %d not facing, %d overlaps; %d enemy shots hit the player"),
			Wave, WaveTestStuck, WaveTestBlocked, WaveTestHolding, WaveTestTurned, WaveTestOverlaps, AWWOEnemyProjectile::PlayerHits);
		FScreenshotRequest::RequestScreenshot(Shots + TEXT("won.png"), true, false);
		bWaveTest = false;
		FTimerHandle Quit;
		GetWorldTimerManager().SetTimer(Quit, []() { FPlatformMisc::RequestExit(false); }, 1.f, false);
		return;
	}
	if (State != EWWOWaveState::Spawning)
	{
		WaveTestFight = 0.f;
		WaveTestTracks.Reset();
		return;
	}
	// Let each wave fight for a while (the allies shoot back), then cut it short.
	const float FightTime = 28.f;
	const float Before = WaveTestFight;
	WaveTestFight += DeltaSeconds;
	// Move yourself somewhere new at the start of each wave and halfway through it, so the
	// enemies re-path and the teammates follow you through the level.
	if ((Before == 0.f || (Before < 14.f && WaveTestFight >= 14.f)) && Wave > 1)
	{
		const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		FNavLocation To;
		if (Nav && Nav->GetRandomReachablePointInRadius(WaveTestHome, 2500.f, To))
		{
			P->TeleportTo(To.Location + FVector(0, 0, P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), P->GetActorRotation());
			UE_LOG(LogWWO, Display, TEXT("WaveTest: moved the player to %s"), *P->GetActorLocation().ToCompactString());
		}
	}
	if (Before < 12.f && WaveTestFight >= 12.f)
	{
		FScreenshotRequest::RequestScreenshot(Shots + FString::Printf(TEXT("%d.png"), Wave), true, false);
	}
	if (Queue.Num() == 0 && WaveTestFight >= FightTime)
	{
		UE_LOG(LogWWO, Display, TEXT("WaveTest: wave %d fought %.0f s, skipping the %d left"), Wave, WaveTestFight, Alive.Num());
		SkipWave();
		return;
	}
	// Every 0.25 s: is anyone stuck (wants to move, isn't) or not facing its target?
	WaveTestSample -= DeltaSeconds;
	if (WaveTestSample > 0.f) return;
	const float Step = 0.25f;
	WaveTestSample = Step;
	const TArray<AWWOEnemy*> Living = WS->LivingEnemies();
	TArray<ACharacter*> Everyone;
	Everyone.Add(P);
	for (AWWOAlly* A : WS->LivingTeammates()) Everyone.Add(A);
	for (AWWOEnemy* E : Living) Everyone.Add(E);
	for (ACharacter* C : Everyone)
	{
		if (C == P) continue;
		AWWOEnemy* E = Cast<AWWOEnemy>(C);
		const UWWOEnemyMovement* Move = Cast<UWWOEnemyMovement>(C->GetCharacterMovement());
		const FVector Wish = E ? E->GetWish() : (Move && Move->bSteering ? Move->Wish : FVector::ZeroVector);
		const FString What = E ? E->DebugState() : TEXT("ally");
		FWaveTestTrack& T = WaveTestTracks.FindOrAdd(C);
		const FVector Pos = C->GetActorLocation();
		const float Moved = FVector::Dist2D(Pos, T.Pos);
		const bool bWants = Wish.Size2D() > 100.f && C->GetCharacterMovement()->IsMovingOnGround();
		const bool bStill = bWants && Moved < Wish.Size2D() * Step * 0.15f;
		// A wish that keeps flipping back and forth is a decision at a threshold (a Soldier
		// behind a wall right at its 15 m minimum range backs off, then walks back in, as in
		// Godot), not something in the way.
		T.bFlipped = bStill && (T.bFlipped || FVector::DotProduct(Wish.GetSafeNormal2D(), T.Wish.GetSafeNormal2D()) < -0.5f);
		T.Still = bStill ? T.Still + Step : 0.f;
		T.Pos = Pos;
		T.Wish = Wish;
		if (!bStill) T.bStuckLogged = false;
		if (T.Still >= 2.f && !T.bStuckLogged)
		{
			T.bStuckLogged = true;
			// Pushing into someone (you, a teammate, another enemy) is blocked, not stuck.
			const AActor* Blocker = nullptr;
			for (ACharacter* O : Everyone)
			{
				if (O == C) continue;
				const FVector ToO = O->GetActorLocation() - Pos;
				const float Reach = C->GetCapsuleComponent()->GetScaledCapsuleRadius() + O->GetCapsuleComponent()->GetScaledCapsuleRadius() + 30.f;
				if (ToO.Size2D() < Reach && FVector::DotProduct(ToO.GetSafeNormal2D(), Wish.GetSafeNormal2D()) > 0.2f) Blocker = O;
			}
			if (Blocker)
			{
				++WaveTestBlocked;
				UE_LOG(LogWWO, Display, TEXT("WaveTest blocked: %s (%s) pushing into %s"), *C->GetName(), *What, *Blocker->GetName());
			}
			else if (T.bFlipped)
			{
				++WaveTestHolding;
				UE_LOG(LogWWO, Display, TEXT("WaveTest holding: %s (%s) at %s, %.1f m from its target, can't see it"), *C->GetName(), *What,
					*Pos.ToCompactString(), E && E->GetTargetActor() ? FVector::Dist2D(E->GetTargetActor()->GetActorLocation(), Pos) / WWO::M : 0.f);
			}
			else
			{
				// What's in the way: sweep its capsule a little along where it wants to go.
				FHitResult Wall;
				const UCapsuleComponent* Cap = C->GetCapsuleComponent();
				FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOWaveTest), false, C);
				GetWorld()->SweepSingleByChannel(Wall, Pos, Pos + Wish.GetSafeNormal2D() * 40.f, FQuat::Identity, ECC_Pawn,
					FCollisionShape::MakeCapsule(Cap->GetScaledCapsuleRadius(), Cap->GetScaledCapsuleHalfHeight() - 5.f), Params);
				++WaveTestStuck;
				UE_LOG(LogWWO, Warning, TEXT("WaveTest STUCK: %s (%s) at %s wants %.1f m/s toward %s, moved %.0f cm in the last %.2f s, %s in the way; %s"),
					*C->GetName(), *What, *Pos.ToCompactString(), Wish.Size2D() / WWO::M, *Wish.GetSafeNormal2D().ToCompactString(), Moved, Step,
					Wall.GetActor() ? *Wall.GetActor()->GetActorLabel() : TEXT("nothing"), E ? *E->PathDebug() : TEXT(""));
			}
		}
		// Stuck in the air (on someone's head, or wedged on a ledge).
		T.Air = C->GetCharacterMovement()->IsFalling() ? T.Air + Step : 0.f;
		if (T.Air >= 3.f && !T.bAirLogged)
		{
			T.bAirLogged = true;
			++WaveTestStuck;
			UE_LOG(LogWWO, Warning, TEXT("WaveTest STUCK IN THE AIR: %s (%s) at %s for %.1f s"), *C->GetName(), *What, *Pos.ToCompactString(), T.Air);
		}
		if (T.Air == 0.f) T.bAirLogged = false;
		// Facing: the Tank turns slowly on purpose and plants itself to slam, so give it more slack.
		const AActor* Target = E ? E->GetTargetActor() : nullptr;
		if (Target && !What.StartsWith(TEXT("leap")) && !What.StartsWith(TEXT("slam")))
		{
			const bool bTank = Cast<AWWOTank>(E) != nullptr;
			const FVector To = (Target->GetActorLocation() - Pos).GetSafeNormal2D();
			const float Off = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(To, C->GetActorForwardVector().GetSafeNormal2D()), -1.f, 1.f)));
			T.Off = Off > (bTank ? 60.f : 30.f) ? T.Off + Step : 0.f;
			if (T.Off >= 1.5f && !T.bTurnLogged)
			{
				T.bTurnLogged = true;
				++WaveTestTurned;
				UE_LOG(LogWWO, Warning, TEXT("WaveTest NOT FACING: %s (%s) %.0f deg off its target %s"), *C->GetName(), *What, Off, *Target->GetName());
			}
			if (T.Off == 0.f) T.bTurnLogged = false;
		}
	}
	// Overlaps: two living enemies inside each other (bunched past their capsules).
	for (int32 i = 0; i < Living.Num(); ++i)
	{
		for (int32 j = i + 1; j < Living.Num(); ++j)
		{
			const float Gap = FVector::Dist2D(Living[i]->GetActorLocation(), Living[j]->GetActorLocation());
			const float Sum = Living[i]->GetCapsuleComponent()->GetScaledCapsuleRadius() + Living[j]->GetCapsuleComponent()->GetScaledCapsuleRadius();
			const float Height = FMath::Abs(Living[i]->GetActorLocation().Z - Living[j]->GetActorLocation().Z);
			if (Gap < Sum * 0.8f && Height < 100.f)
			{
				++WaveTestOverlaps;
				UE_LOG(LogWWO, Warning, TEXT("WaveTest OVERLAP: %s and %s %.0f cm apart"), *Living[i]->GetName(), *Living[j]->GetName(), Gap);
			}
		}
	}
	// Every 3 s: a line per enemy.
	WaveTestLog -= Step;
	if (WaveTestLog > 0.f) return;
	WaveTestLog = 3.f;
	UE_LOG(LogWWO, Display, TEXT("WaveTest: wave %d at %.0f s, %d alive, %d queued; player %s health %.0f; enemy shots hit the player %d times"), Wave, WaveTestFight,
		Alive.Num(), Queue.Num(), *P->GetActorLocation().ToCompactString(), P->Health->Health, AWWOEnemyProjectile::PlayerHits);
	for (AWWOAlly* A : WS->AllTeammates())
	{
		UE_LOG(LogWWO, Display, TEXT("WaveTest   %s %s at %s speed %.1f"), *A->GetName(), *A->StatusText(), *A->GetActorLocation().ToCompactString(),
			A->GetVelocity().Size2D() / WWO::M);
	}
	for (AWWOEnemy* E : Living)
	{
		const AActor* Target = E->GetTargetActor();
		UE_LOG(LogWWO, Display, TEXT("WaveTest   %s %s at %s speed %.1f wish %.1f -> %s %.1f m, health %.0f"), *E->GetName(), *E->DebugState(),
			*E->GetActorLocation().ToCompactString(), E->GetVelocity().Size2D() / WWO::M, E->GetWish().Size2D() / WWO::M,
			Target ? *Target->GetName() : TEXT("none"), Target ? FVector::Dist2D(Target->GetActorLocation(), E->GetActorLocation()) / WWO::M : 0.f, E->GetHealth());
	}
}
