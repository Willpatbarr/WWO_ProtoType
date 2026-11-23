// -WWOAutoTestFootRace (on L_FootRaceTest, alone: the player is on team 0): FootRace's portals, breach
// energy, doors and levers, and lane rooms. The player is put straight into a doorway's opening (as
// walking into it would leave them), and holds the Revive key at a lever through ScriptHold.
// - with no energy, a room doorway refuses; an enemy kill earns EnergyPerEnemy;
// - Room1's doorway to the team's hub doorway and back; the hub drains the pool only while in it;
// - the other team's hub doorway does nothing; running out sends you back; a death costs a share;
// - Door1 blocks the doorway; its lever goes live on a breach from Room1 and opens it; that freezes the
//   pool, a hub kill splits between the pools, Lever2 waits; leaving lands in Room2 and swaps the pools;
//   a breach from Room2 opens Door2; leaving lands in Room3;
// - lane rooms: both Room1s fill at the start, Room3 fills (with its Tank) once stood in, each enemy
//   comes out at one of the room's wall markers and they spread over most of the markers, a lane's
//   enemies leave the other team's players alone, a kill is refilled after LaneRoomRefill;
// - hijacking (two parked bots, a foe and a mate): the other team's hub doorway refuses before a kill;
//   killing a breacher opens it to the killer's whole team (the feed says so); going in lands in the other
//   team's furthest opened room, ends the breach, costs and drains nothing; a hijacker's kill and the kill of
//   a hijacker earn the bonus; a death ends a hijack; time up brings a hijacker home; the doorway closes;
// - the boss: Door3 and Door4 don't bring it (opened straight), the last door (Door5) brings it up on its
//   platform (seen from the floor in front), shielded (hits do nothing); CoreDelay later its core appears in the hub
//   with a wave in the boss room; the core carried out through the hub doorway opens a window, hits land,
//   the window ends on time; the next core is dropped by a carrier who dies; the other team delivering it
//   heals the boss (never past full); the next window is a third longer; killing the boss wins the race.
// Screenshots 88_footrace_*. Logs `AutoTest footrace: ... ok|FAIL`.

#include "Player/WWOAutoTest.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOTestRun.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
#include "Game/WWOGameMode.h"
#include "Game/WWOFootRaceRules.h"
#include "Game/WWOPlayerState.h"
#include "Enemies/WWOEnemy.h"
#include "Enemies/WWOEnemyKinds.h"
#include "Enemies/WWOEnemyTypes.h"
#include "Game/WWOGameState.h"
#include "World/WWOBreachPortal.h"
#include "World/WWOBreachDoor.h"
#include "World/WWOBreachLever.h"
#include "World/WWOBossCore.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Player/WWOBotController.h"
#include "WhatWasOurs.h"
#include "WWOSwitches.h"
#include "WWOIds.h"
#include "WWOTypes.h"
#include "Components/CapsuleComponent.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Weapons/WWOGrenade.h"
#include "Player/WWOWeaponComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Flora/WWOFloraEffects.h"
#include "Data/WWOData.h"
#include "Systems/WWOLoadoutSubsystem.h"

namespace
{
	constexpr float Near = 1.5f * WWO::M; // how close to an arrival spot counts as "arrived there"
	float EnergyMark = 0.f;               // a pool when a step began, for the step that checks it
	float NextMark = 0.f;
	// Room3's enemies as each is first seen (the tick it spawns): how far from the nearest of the room's
	// EnemySpawn markers, and which marker that is.
	TMap<TWeakObjectPtr<AWWOEnemy>, TPair<float, int32>> Room3Seen;
	TWeakObjectPtr<AWWOCharacter> HijackFoe, HijackMate; // the hijack steps' two parked bots

	// Puts the body's feet in the middle of a doorway's opening.
	void StepInto(AWWOCharacter* P, const AWWOBreachPortal* Portal)
	{
		const float Half = P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		// No overlap check: the lane's enemies crowd the doorways, and a refused teleport isn't what this tests.
		P->TeleportTo(Portal->GetActorLocation() + FVector(0, 0, Half + 5.f), Portal->GetActorRotation(), false, true);
	}

	bool At(const AWWOCharacter* P, const AWWOBreachPortal* Portal)
	{
		if (!Portal) return false;
		const FTransform Spot = Portal->ArrivalSpot(P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		return FVector::Dist2D(P->GetActorLocation(), Spot.GetLocation()) < Near;
	}

	// Stands the player in front of a lever, facing it.
	void StandAt(AWWOPlayerController* PC, AWWOCharacter* P, const AWWOBreachLever* Lever)
	{
		const FVector Ahead = FRotator(0.f, Lever->GetActorRotation().Yaw, 0.f).Vector();
		const FRotator Facing(0.f, Lever->GetActorRotation().Yaw + 180.f, 0.f);
		P->TeleportTo(Lever->GetActorLocation() + Ahead * 1.3f * WWO::M + FVector(0, 0, P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), Facing, false, true);
		PC->SetControlRotation(Facing);
		P->SetViewRotation(0.f, Facing.Yaw);
	}

	// Does a shot across the door's doorway hit the door?
	bool Blocks(UWorld* World, const AWWOBreachDoor* Door)
	{
		const FVector Across = FRotator(0.f, Door->GetActorRotation().Yaw, 0.f).Vector() * 2.f * WWO::M;
		const FVector Mid = Door->GetActorLocation();
		FHitResult Hit;
		return World->LineTraceSingleByChannel(Hit, Mid - Across, Mid + Across, ECC_Visibility) && Hit.GetActor() == Door;
	}
}

void FWWOAutoTest::UpdateAutoFootRaceTest(float DeltaTime)
{
	AWWOCharacter* P = PC->GetWWOCharacter();
	const AWWOGameMode* GM = PC->GetWorld()->GetAuthGameMode<AWWOGameMode>();
	FWWOFootRaceRules* F = GM ? GM->FootRace() : nullptr;
	if (!P || !GM) return;
	UWorld* World = PC->GetWorld();
	const float Before = AutoFootRaceClock;
	AutoFootRaceClock += DeltaTime;
	const float T = AutoFootRaceClock;
	auto Crossed = [&](float At) { return WWOTest::Crossed(Before, T, At); };
	auto Check = [](bool bOk, const FString& What) { return WWOTest::Check(TEXT("footrace"), bOk, What); };
	auto Where = [P]() { return P->GetActorLocation().ToCompactString(); };

	if (Crossed(1.f) && !Check(F != nullptr, TEXT("the map runs FootRace's rules"))) { WWOTest::Exit(); return; }
	if (!F) return;
	if (T < 19.f || T >= 21.f) P->Health->GodMode = 1.f; // but for the death step
	const AWWOPlayerState* PS = PC->GetPlayerState<AWWOPlayerState>();
	const int32 Team = PS ? PS->Team : -1;
	AWWOBreachPortal* Room1 = F->RoomPortal(Team, "Room1");
	AWWOBreachPortal* Room2 = F->RoomPortal(Team, "Room2");
	AWWOBreachPortal* Room3 = F->RoomPortal(Team, "Room3");
	AWWOBreachPortal* Hub = F->HubPortal(Team);
	AWWOBreachPortal* OtherHub = F->HubPortal(1 - Team);
	AWWOBreachDoor* Door1 = F->DoorOf(Team, 1);
	AWWOBreachDoor* Door2 = F->DoorOf(Team, 2);
	AWWOBreachLever* Lever1 = F->LeverOf(Team, 1);
	AWWOBreachLever* Lever2 = F->LeverOf(Team, 2);
	const float Rich = GM->BreachThreshold * 2.f; // plenty, for the steps about the doorways themselves
	// The two lever pulls hold the Revive key for LeverHoldTime and a little more.
	const float Hold = GM->LeverHoldTime + 0.4f;
	PC->ScriptHold(EWWOAction::Revive, (T >= 24.f && T < 24.f + Hold) || (T >= 30.f && T < 30.f + Hold));

	if (Crossed(2.f))
	{
		Check(Team == 0, FString::Printf(TEXT("alone, the player is on team 0 (team %d)"), Team));
		Check(Room1 && Room2 && Room3 && F->RoomPortal(Team, "Boss") && Hub && OtherHub,
			TEXT("a doorway in each of the team's rooms, and one per team in the hub"));
		int32 DoorsFound = 0, LeversFound = 0;
		for (int32 T2 = 0; T2 < 2; ++T2)
		{
			for (int32 D = 1; D <= FWWOFootRaceRules::DoorCount; ++D)
			{
				DoorsFound += F->DoorOf(T2, D) != nullptr;
				LeversFound += F->LeverOf(T2, D) != nullptr;
			}
		}
		Check(DoorsFound == 2 * FWWOFootRaceRules::DoorCount && LeversFound == 2 * FWWOFootRaceRules::DoorCount,
			FString::Printf(TEXT("%d doors and %d levers per team (%d doors, %d levers)"), FWWOFootRaceRules::DoorCount, FWWOFootRaceRules::DoorCount, DoorsFound, LeversFound));
		bool bRooms = true;
		for (int32 I = 1; I <= FWWOFootRaceRules::BossRoom; ++I) bRooms &= F->RoomPortal(Team, FWWOFootRaceRules::RoomName(I)) != nullptr && !F->RoomCenter(Team, I).IsZero();
		Check(bRooms, FString::Printf(TEXT("Room1..Room%d and the boss room, each with its doorway"), FWWOFootRaceRules::DoorCount));
		Check(F->DoorsOpen(0) == 0 && F->DoorsOpen(1) == 0 && Door1 && !Door1->IsOpen(), TEXT("every door starts shut"));
		bool bInSpawn = false;
		for (TActorIterator<ATargetPoint> It(World); It; ++It)
		{
			if (It->ActorHasTag("TeamSpawn") && It->ActorHasTag(Team == 1 ? FName("Lane1") : FName("Lane0"))
				&& FVector::Dist2D(It->GetActorLocation(), P->GetActorLocation()) < 1.f * WWO::M) bInSpawn = true;
		}
		Check(bInSpawn, FString::Printf(TEXT("placed on one of its team's spawn spots (at %s)"), *Where()));
		Check(F->Energy(0) == 0.f && F->Energy(1) == 0.f, TEXT("both teams start with no breach energy"));
	}
	if (!Room1 || !Room2 || !Room3 || !Hub || !OtherHub || !Door1 || !Door2 || !Lever1 || !Lever2) return;
	// A look at Room1's doorway from in front of it.
	if (Crossed(2.5f))
	{
		const FTransform Spot = Room1->ArrivalSpot(P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		const FRotator Back(0.f, Spot.Rotator().Yaw + 180.f, 0.f);
		P->TeleportTo(Spot.GetLocation() + Spot.Rotator().Vector() * 3.f * WWO::M, Back);
		PC->SetControlRotation(Back);
		P->SetViewRotation(0.f, Back.Yaw);
	}
	if (Crossed(4.f)) Shot(TEXT("88_footrace_room_doorway"));
	// No energy: the doorway refuses.
	if (Crossed(4.5f)) StepInto(P, Room1);
	if (Crossed(5.5f)) Check(!At(P, Hub) && !F->IsBreaching(P), FString::Printf(TEXT("with no energy, Room1's doorway refuses (at %s)"), *Where()));
	// An enemy kill earns energy.
	if (Crossed(6.f))
	{
		const FWWOEnemyKind& Kind = WWOEnemyKinds::FindOrSwarm(WWOIds::Enemy::Swarm);
		const FVector Spot = Room1->ArrivalSpot(100.f).GetLocation() + Room1->GetActorForwardVector() * 6.f * WWO::M;
		AWWOEnemy* E = World->SpawnActor<AWWOEnemy>(Kind.Class, Spot, FRotator::ZeroRotator, WWO::AlwaysSpawn());
		if (Check(E != nullptr, TEXT("an enemy to kill")))
		{
			E->bRespawns = false;
			AutoFootRaceEnemy = E;
		}
	}
	// A moment later (the rules notice a new enemy on their next tick), the player kills it.
	if (Crossed(6.2f) && AutoFootRaceEnemy.IsValid())
	{
		FWWODamageBy By(P);
		AutoFootRaceEnemy->ApplyHit(100000.f, false, AutoFootRaceEnemy->GetActorLocation(), FLinearColor::White);
	}
	if (Crossed(6.5f))
	{
		Check(FMath::IsNearlyEqual(F->Energy(Team), GM->EnergyPerEnemy, 0.01f),
			FString::Printf(TEXT("killing an enemy earns %.0f energy (%.1f)"), GM->EnergyPerEnemy, F->Energy(Team)));
		Check(F->Energy(1 - Team) == 0.f, TEXT("and nothing for the other team"));
		const AWWOGameState* GS = World->GetGameState<AWWOGameState>();
		Check(GS && GS->KillFeed.Num() == 0, FString::Printf(TEXT("an AI kill puts nothing in the kill feed (%d lines)"), GS ? GS->KillFeed.Num() : -1));
	}
	// Room1 -> hub -> Room1, the pool draining only while in the hub.
	if (Crossed(7.f)) { F->SetEnergy(Team, Rich); StepInto(P, Room1); }
	if (Crossed(7.5f))
	{
		Check(At(P, Hub) && F->IsBreaching(P), FString::Printf(TEXT("Room1's doorway leads to the team's hub doorway (at %s)"), *Where()));
		Check(Lever1->bLive && !Lever2->bLive, TEXT("a breach from Room1 makes Lever1 live, not Lever2"));
		const AWWOGameState* GS = World->GetGameState<AWWOGameState>();
		const bool bFeed = GS && GS->KillFeed.Num() > 0 && GS->KillFeed.Last().How == AWWOGameState::KillEvent && GS->KillFeed.Last().Victim == TEXT("breached")
			&& PS && GS->KillFeed.Last().Killer == PS->GetPlayerName();
		Check(bFeed, TEXT("the breach shows in the kill feed: \"<player> breached\""));
		Check(GS && GS->BreachStart.IsValidIndex(Team) && FMath::IsNearlyEqual(GS->BreachStart[Team], Rich, 1.f),
			TEXT("the race track's breach bar knows the pool the breach began on"));
		EnergyMark = F->Energy(Team);
	}
	if (Crossed(8.5f))
	{
		const float Drained = EnergyMark - F->Energy(Team);
		Check(Drained > GM->BreachDrain * 0.7f && Drained < GM->BreachDrain * 1.3f,
			FString::Printf(TEXT("one breacher drains about %.0f a second (%.1f in 1 s)"), GM->BreachDrain, Drained));
		Shot(TEXT("88_footrace_hub_arrival"));
	}
	if (Crossed(9.f)) StepInto(P, Hub);
	if (Crossed(10.f))
	{
		Check(At(P, Room1) && !F->IsBreaching(P), FString::Printf(TEXT("the hub doorway leads back to Room1, the furthest room open (at %s)"), *Where()));
		EnergyMark = F->Energy(Team);
	}
	if (Crossed(11.f))
	{
		Check(F->Energy(Team) == EnergyMark, TEXT("out of the hub, nothing drains"));
		const AWWOGameState* Track = World->GetGameState<AWWOGameState>();
		Check(Track && Track->BreachStart.IsValidIndex(Team) && Track->BreachStart[Team] == 0.f, TEXT("and the race track's breach bar is gone"));
		Check(!Lever1->bLive, TEXT("with the breach over, Lever1 is not live"));
		Check(Blocks(World, Door1), TEXT("a shut door blocks its doorway"));
	}
	// The other team's hub doorway.
	if (Crossed(11.5f))
	{
		StepInto(P, OtherHub);
		AutoFootRaceBefore = P->GetActorLocation();
	}
	if (Crossed(12.5f))
	{
		Check(FVector::Dist2D(P->GetActorLocation(), AutoFootRaceBefore) < Near,
			FString::Printf(TEXT("the other team's hub doorway does nothing (at %s)"), *Where()));
		Shot(TEXT("88_footrace_other_doorway"));
	}
	// Running out in the hub sends you back.
	if (Crossed(13.f)) { F->SetEnergy(Team, Rich); StepInto(P, Room1); }
	if (Crossed(14.f)) { Check(F->IsBreaching(P), TEXT("in the hub again")); F->SetEnergy(Team, GM->BreachDrain * 0.3f); }
	if (Crossed(15.f))
	{
		Check(At(P, Room1) && !F->IsBreaching(P), FString::Printf(TEXT("out of energy, the breacher is sent back to Room1 (at %s)"), *Where()));
		Check(F->Energy(Team) == 0.f, FString::Printf(TEXT("and the pool is empty (%.1f)"), F->Energy(Team)));
	}
	// Dying in the hub costs a share of the pool.
	if (Crossed(16.f)) { F->SetEnergy(Team, Rich); StepInto(P, Room1); }
	if (Crossed(19.f))
	{
		P->Health->GodMode = 0.f;
		EnergyMark = F->Energy(Team);
		Check(F->IsBreaching(P), TEXT("breaching before the death"));
		P->Health->Kill();
	}
	if (Crossed(19.3f))
	{
		const float Want = EnergyMark * (1.f - GM->BreachDeathLoss);
		Check(FMath::Abs(F->Energy(Team) - Want) < 1.f,
			FString::Printf(TEXT("a breacher's death costs %.0f%% of the pool (%.1f -> %.1f, want %.1f)"), GM->BreachDeathLoss * 100.f, EnergyMark, F->Energy(Team), Want));
		Check(!F->IsBreaching(P), TEXT("and the dead player is no longer breaching"));
	}
	// Levers (respawned by now). Door1: a breach from Room1, hold the key at Lever1.
	if (Crossed(23.f)) { F->SetEnergy(Team, Rich); StepInto(P, Room1); }
	if (Crossed(23.5f))
	{
		Check(F->IsBreaching(P) && Lever1->bLive, TEXT("breaching from Room1 again: Lever1 is live"));
		StandAt(PC, P, Lever1);
	}
	// Frame times: the worst frame in a quiet stretch before, and around Door1 opening (a hitch there was jarring).
	static float WorstBefore = 0.f, WorstAtDoor = 0.f;
	if (T >= 20.5f && T < 23.f) WorstBefore = FMath::Max(WorstBefore, DeltaTime);
	// (to 0.6 s after: the screenshot of the popup comes after that, and a screenshot's frame is a slow one)
	if (T >= 24.f + GM->LeverHoldTime - 0.2f && T < 24.f + GM->LeverHoldTime + 0.6f) WorstAtDoor = FMath::Max(WorstAtDoor, DeltaTime);
	if (Crossed(24.f + GM->LeverHoldTime + 0.6f))
	{
		UE_LOG(LogWWO, Display, TEXT("FootRace frames: worst %.1f ms before, %.1f ms around Door1 opening"), WorstBefore * 1000.f, WorstAtDoor * 1000.f);
		Check(WorstAtDoor < FMath::Max(WorstBefore * 2.f, 0.05f), FString::Printf(TEXT("no hitch when a door opens (worst frame %.1f ms, %.1f ms before)"), WorstAtDoor * 1000.f, WorstBefore * 1000.f));
	}
	if (Crossed(24.f + GM->LeverHoldTime * 0.5f))
	{
		Check(!Door1->IsOpen() && Lever1->Progress > 0.2f, FString::Printf(TEXT("holding the key pulls the lever over time (%.2f)"), Lever1->Progress));
		Shot(TEXT("88_footrace_lever"));
	}
	if (Crossed(24.f + Hold + 0.2f))
	{
		Check(Door1->IsOpen() && F->DoorsOpen(Team) == 1 && Lever1->bPulled, TEXT("the pull opens Door1"));
		const AWWOGameState* Feed = World->GetGameState<AWWOGameState>();
		Check(Feed && Feed->KillFeed.Num() > 0 && Feed->KillFeed.Last().Victim == TEXT("opened Door 1") && PS && Feed->KillFeed.Last().Killer == PS->GetPlayerName(),
			FString::Printf(TEXT("the door shows in the kill feed: \"<player> opened Door 1\" (%s)"), Feed && Feed->KillFeed.Num() ? *Feed->KillFeed.Last().Victim : TEXT("none")));
		Check(!Blocks(World, Door1), TEXT("an open door no longer blocks its doorway"));
		Check(F->IsFrozen(Team), TEXT("opening a door mid-breach freezes the breach's pool"));
		Check(!Lever2->bLive, TEXT("one door per breach: Lever2 is not live"));
		// A kill by the breacher, in the hub: half to each pool.
		EnergyMark = F->Energy(Team);
		NextMark = F->NextEnergy(Team);
		TArray<AWWOEnemy*> InRoom1 = F->EnemiesIn(Team, "Room1");
		if (Check(InRoom1.Num() > 0, TEXT("an enemy left in Room1 to kill")))
		{
			FWWODamageBy By(P);
			InRoom1.Last()->ApplyHit(100000.f, false, InRoom1.Last()->GetActorLocation(), FLinearColor::White);
		}
	}
	if (Crossed(24.f + Hold + 0.35f)) Shot(TEXT("88_footrace_door_opened")); // "Next door opened!" by the reticle
	if (Crossed(24.f + Hold + 0.5f))
	{
		Check(FMath::IsNearlyEqual(F->NextEnergy(Team), NextMark + GM->EnergyPerEnemy * 0.5f, 0.01f),
			FString::Printf(TEXT("a kill in the hub puts half in the next pool (%.1f -> %.1f)"), NextMark, F->NextEnergy(Team)));
		const float Drained = GM->BreachDrain * 0.3f;
		Check(FMath::Abs(F->Energy(Team) - (EnergyMark + GM->EnergyPerEnemy * 0.5f - Drained)) < GM->BreachDrain * 0.15f,
			FString::Printf(TEXT("and half in the frozen pool (%.1f -> %.1f)"), EnergyMark, F->Energy(Team)));
		NextMark = F->NextEnergy(Team);
		StepInto(P, Hub);
	}
	if (Crossed(24.f + Hold + 1.5f))
	{
		Check(At(P, Room2) && !F->IsBreaching(P), FString::Printf(TEXT("leaving the hub lands in Room2, the furthest room open (at %s)"), *Where()));
		Check(!F->IsFrozen(Team) && FMath::IsNearlyEqual(F->Energy(Team), NextMark, 0.01f) && F->NextEnergy(Team) == 0.f,
			FString::Printf(TEXT("the breach over, the next pool became the pool (%.1f, next %.1f)"), F->Energy(Team), F->NextEnergy(Team)));
	}
	// Door2: a breach from Room2.
	if (Crossed(29.f)) { F->SetEnergy(Team, Rich); StepInto(P, Room2); }
	if (Crossed(29.5f))
	{
		Check(Lever2->bLive, TEXT("breaching from Room2: Lever2 is live"));
		StandAt(PC, P, Lever2);
	}
	if (Crossed(30.f + Hold + 0.2f)) Check(Door2->IsOpen() && F->DoorsOpen(Team) == 2, TEXT("the pull opens Door2"));
	if (Crossed(30.f + Hold + 0.5f)) StepInto(P, Hub);
	if (Crossed(30.f + Hold + 1.5f)) Check(At(P, Room3), FString::Printf(TEXT("leaving the hub lands in Room3 (at %s)"), *Where()));
	// Room3's first spawn is its Tank: its model is loaded at the start, so no stall when it comes.
	static float WorstRoom3 = 0.f;
	if (T >= 30.f + Hold + 0.5f && T < 30.f + Hold + 3.5f) WorstRoom3 = FMath::Max(WorstRoom3, DeltaTime);
	if (Crossed(30.f + Hold + 3.5f))
		Check(WorstRoom3 < 0.1f, FString::Printf(TEXT("no stall as Room3 fills, its Tank first (worst frame %.1f ms)"), WorstRoom3 * 1000.f));
	// The lane rooms.
	const int32 Want = FMath::RoundToInt(GM->LaneRoomEnemies);
	const float Rooms = 30.f + Hold + 1.5f + Want * GM->LaneRoomStagger + 1.f; // Room3 has had time to fill
	// Where Room3's enemies come out, each as first seen (the tick it spawned: it hasn't walked yet).
	if (T >= 30.f + Hold + 0.5f && T < Rooms)
	{
		TArray<FVector> Markers;
		for (TActorIterator<ATargetPoint> It(World); It; ++It)
		{
			if (It->ActorHasTag("EnemySpawn") && It->ActorHasTag("Room3") && It->ActorHasTag(Team == 1 ? FName("Lane1") : FName("Lane0"))) Markers.Add(It->GetActorLocation());
		}
		for (AWWOEnemy* E : F->EnemiesIn(Team, "Room3"))
		{
			if (Room3Seen.Contains(E) || Markers.IsEmpty()) continue;
			int32 Best = 0;
			for (int32 I = 1; I < Markers.Num(); ++I)
				if (FVector::Dist2D(Markers[I], E->GetActorLocation()) < FVector::Dist2D(Markers[Best], E->GetActorLocation())) Best = I;
			Room3Seen.Add(E, { (float)FVector::Dist2D(Markers[Best], E->GetActorLocation()) / WWO::M, Best });
		}
	}
	if (Crossed(Rooms))
	{
		Check(F->AliveIn(1 - Team, "Room1") == Want, FString::Printf(TEXT("the other lane's Room1 filled to %d at the start (%d)"), Want, F->AliveIn(1 - Team, "Room1")));
		Check(F->FurthestRoom(Team) == 3, FString::Printf(TEXT("the team's furthest room is Room3 (%d)"), F->FurthestRoom(Team)));
		Check(F->AliveIn(Team, "Room3") == Want, FString::Printf(TEXT("Room3 filled to %d (%d)"), Want, F->AliveIn(Team, "Room3")));
		bool bTank = false;
		for (const AWWOEnemy* E : F->EnemiesIn(Team, "Room3"))
		{
			const FWWOEnemyKind* Kind = WWOEnemyKinds::Of(E);
			if (Kind && Kind->Id == WWOIds::Enemy::Tank) bTank = true;
		}
		Check(bTank, TEXT("Room3 has its Tank"));
		float Farthest = 0.f;
		TSet<int32> Used;
		for (const TPair<TWeakObjectPtr<AWWOEnemy>, TPair<float, int32>>& Seen : Room3Seen)
		{
			Farthest = FMath::Max(Farthest, Seen.Value.Key);
			Used.Add(Seen.Value.Value);
		}
		Check(Room3Seen.Num() >= Want && Farthest < 1.5f,
			FString::Printf(TEXT("Room3's enemies come out at its wall markers (%d seen, the farthest %.2f m from one)"), Room3Seen.Num(), Farthest));
		Check(Used.Num() >= 8, FString::Printf(TEXT("spread over the room's markers, not one spot (%d markers used)"), Used.Num()));
		int32 Lanes = 0, Unset = 0, AfterMe = 0;
		for (TActorIterator<AWWOEnemy> It(World); It; ++It)
		{
			if (It->IsDead()) continue;
			++Lanes;
			if (It->Lane < 0) ++Unset;
			if (It->Lane != Team && It->GetTargetActor() == P) ++AfterMe;
		}
		Check(Unset == 0, FString::Printf(TEXT("every enemy guards a lane (%d of %d don't)"), Unset, Lanes));
		Check(AfterMe == 0, FString::Printf(TEXT("the other lane's enemies leave this team's player alone (%d after them)"), AfterMe));
		TArray<AWWOEnemy*> InRoom3 = F->EnemiesIn(Team, "Room3");
		if (InRoom3.Num() > 0)
		{
			FWWODamageBy By(P);
			AWWOEnemy* Victim = InRoom3.Last(); // not the Tank (the first spawned), whose shield takes hits
			Victim->ApplyHit(100000.f, false, Victim->GetActorLocation(), FLinearColor::White);
		}
	}
	if (Crossed(Rooms + 0.5f)) Check(F->AliveIn(Team, "Room3") == Want - 1, FString::Printf(TEXT("a kill in Room3 leaves %d (%d)"), Want - 1, F->AliveIn(Team, "Room3")));
	if (Crossed(Rooms + 1.f + GM->LaneRoomRefill))
	{
		Check(F->AliveIn(Team, "Room3") == Want, FString::Printf(TEXT("and Room3 refills to %d within %.1f s (%d)"), Want, GM->LaneRoomRefill, F->AliveIn(Team, "Room3")));
		const FTransform Spot = Room3->ArrivalSpot(P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		P->TeleportTo(Spot.GetLocation(), Spot.Rotator());
		PC->SetControlRotation(Spot.Rotator());
		P->SetViewRotation(0.f, Spot.Rotator().Yaw);
	}
	if (Crossed(Rooms + 1.5f + GM->LaneRoomRefill)) Shot(TEXT("88_footrace_room_enemies"));
	// Hijacking, with two bots (parked: they stand where they are put and do nothing): Foe on team 1,
	// Mate on team 0. Short times: the doorway open 6 s after the kill, 4 s in the enemy lane.
	AWWOGameMode* Mutable = Cast<AWWOGameMode>(World->GetAuthGameMode());
	const float H = Rooms + 2.f + GM->LaneRoomRefill;
	AWWOCharacter* Foe = HijackFoe.Get();
	AWWOCharacter* Mate = HijackMate.Get();
	AWWOPlayerState* MyState = PC->GetPlayerState<AWWOPlayerState>();
	auto KillBy = [World](AWWOCharacter* Victim, AWWOCharacter* Killer)
	{
		Victim->Health->GodMode = 0.f;
		Victim->Health->LastAttacker = Killer;
		Victim->Health->LastAttackTime = World->GetTimeSeconds();
		Victim->Health->Kill();
	};
	auto HijackFeed = [World]() { const AWWOGameState* GS = World->GetGameState<AWWOGameState>(); return GS && GS->KillFeed.Num() ? GS->KillFeed.Last() : AWWOGameState::FKillLine(); };
	if (Crossed(H))
	{
		Mutable->HijackOpenTime = 6.f;
		Mutable->HijackStayTime = 4.f;
		Mutable->Roster.BotsWanted = 2;
	}
	if (Crossed(H + 1.f))
	{
		for (TActorIterator<AWWOBotController> It(World); It; ++It)
		{
			It->bParked = true;
			AWWOCharacter* Body = Cast<AWWOCharacter>(It->GetPawn());
			if (!Body) continue;
			if (!HijackFoe.IsValid()) { HijackFoe = Body; Mutable->SetTeam(It->GetPlayerState<AWWOPlayerState>(), 1 - Team); }
			else { HijackMate = Body; It->bMayHijack = true; Mutable->SetTeam(It->GetPlayerState<AWWOPlayerState>(), Team); }
		}
		Check(HijackFoe.IsValid() && HijackMate.IsValid(), TEXT("hijacking: two bots to play it with, one on each team"));
	}
	if (Foe && Mate)
	{
		// The foe breaches; before any kill the other team's hub doorway refuses.
		if (Crossed(H + 2.f)) { F->SetEnergy(1 - Team, Rich); StepInto(Foe, F->RoomPortal(1 - Team, "Room1")); StepInto(P, OtherHub); AutoFootRaceBefore = P->GetActorLocation(); }
		if (Crossed(H + 3.f))
		{
			Check(F->IsBreaching(Foe), TEXT("hijacking: the other team's player is breaching"));
			Check(!F->CanHijack(Team) && !F->IsHijacking(P) && FVector::Dist2D(P->GetActorLocation(), AutoFootRaceBefore) < Near,
				FString::Printf(TEXT("before any kill, the other team's hub doorway refuses (at %s)"), *Where()));
			// Off the doorway first (it would take the player as soon as it opens), then the kill.
			const FTransform Spot = Room1->ArrivalSpot(P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			P->TeleportTo(Spot.GetLocation(), Spot.Rotator(), false, true);
			EnergyMark = F->Energy(Team);
			KillBy(Foe, P);
		}
		if (Crossed(H + 3.3f))
		{
			const AWWOGameState* GS = World->GetGameState<AWWOGameState>();
			Check(F->CanHijack(Team) && !F->CanHijack(1 - Team) && GS && GS->HijackUntil.IsValidIndex(Team) && GS->HijackUntil[Team] > GS->GetServerWorldTimeSeconds(),
				TEXT("killing a breacher opens their hub doorway to the killer's team (and every machine is told)"));
			Check(FMath::IsNearlyEqual(F->Energy(Team), EnergyMark + GM->EnergyPerPlayerKill, 0.5f),
				FString::Printf(TEXT("that kill earns the normal %.0f (%.1f -> %.1f)"), GM->EnergyPerPlayerKill, EnergyMark, F->Energy(Team)));
			const AWWOGameState::FKillLine Line = HijackFeed();
			Check(Line.How == AWWOGameState::KillEvent && Line.Victim == TEXT("can hijack") && Line.Killer == AWWOPlayerState::TeamName(Team),
				FString::Printf(TEXT("the kill feed says \"%s can hijack\" (%s %s)"), *AWWOPlayerState::TeamName(Team), *Line.Killer, *Line.Victim));
			// In through the hub, then into their hub doorway.
			F->SetEnergy(Team, Rich);
			StepInto(P, Room1);
		}
		if (Crossed(H + 4.f))
		{
			Check(F->IsBreaching(P), TEXT("hijacking: breaching first"));
			StepInto(P, OtherHub);
		}
		if (Crossed(H + 4.5f))
		{
			const AWWOBreachPortal* Landing = F->RoomPortal(1 - Team, FWWOFootRaceRules::RoomName(F->OpenedRoom(1 - Team)));
			Check(F->IsHijacking(P) && !F->IsBreaching(P) && At(P, Landing),
				FString::Printf(TEXT("going in lands in front of the other team's furthest opened room's doorway (%s) and ends the breach (at %s)"),
					Landing ? *Landing->Room.ToString() : TEXT("none"), *Where()));
			Check(MyState && MyState->HijackUntil > 0.f, TEXT("and every machine is told who is hijacking, and until when"));
			const AWWOGameState::FKillLine Line = HijackFeed();
			Check(Line.How == AWWOGameState::KillEvent && Line.Victim == TEXT("hijacked"), FString::Printf(TEXT("the kill feed says \"<player> hijacked\" (%s)"), *Line.Victim));
			EnergyMark = F->Energy(Team);
			StepInto(Mate, OtherHub); // a teammate who wasn't breaching, through the same doorway
		}
		if (Crossed(H + 5.5f))
		{
			Check(F->Energy(Team) == EnergyMark, FString::Printf(TEXT("hijacking costs nothing and drains nothing (%.1f -> %.1f)"), EnergyMark, F->Energy(Team)));
			Check(F->IsHijacking(Mate), TEXT("the doorway is open to the killer's whole team: a teammate goes in too"));
			Shot(TEXT("88_footrace_hijack"));
		}
		// The defender (respawned by now) kills one hijacker: the bonus for the defenders, and that hijack is over.
		if (Crossed(H + 6.5f))
		{
			Check(!Foe->Health->bDead, TEXT("hijacking: the foe is back"));
			NextMark = F->Energy(1 - Team);
			KillBy(Mate, Foe);
		}
		if (Crossed(H + 6.7f))
		{
			const AWWOPlayerState* MateState = Mate->GetPlayerState<AWWOPlayerState>();
			Check(FMath::IsNearlyEqual(F->Energy(1 - Team) - NextMark, GM->EnergyPerPlayerKill + GM->HijackKillBonus, 0.5f),
				FString::Printf(TEXT("killing a hijacker earns the defenders %.0f + %.0f (%.1f)"), GM->EnergyPerPlayerKill, GM->HijackKillBonus, F->Energy(1 - Team) - NextMark));
			Check(!F->IsHijacking(Mate) && MateState && MateState->HijackUntil == 0.f, TEXT("a hijacker's death ends its hijack"));
			// The other hijacker kills the defender: the bonus for the hijackers.
			EnergyMark = F->Energy(Team);
			KillBy(Foe, P);
		}
		if (Crossed(H + 6.9f))
		{
			Check(FMath::IsNearlyEqual(F->Energy(Team) - EnergyMark, GM->EnergyPerPlayerKill + GM->HijackKillBonus, 0.5f),
				FString::Printf(TEXT("a hijacker's kill earns %.0f + %.0f (%.1f)"), GM->EnergyPerPlayerKill, GM->HijackKillBonus, F->Energy(Team) - EnergyMark));
		}
		// Time's up: home to the team's own furthest opened room.
		if (Crossed(H + 8.5f))
		{
			const AWWOBreachPortal* Home = F->RoomPortal(Team, FWWOFootRaceRules::RoomName(F->OpenedRoom(Team)));
			Check(!F->IsHijacking(P) && MyState && MyState->HijackUntil == 0.f && At(P, Home),
				FString::Printf(TEXT("after HijackStayTime the hijacker is brought home to its team's furthest opened room (%s; at %s)"),
					Home ? *Home->Room.ToString() : TEXT("none"), *Where()));
			Check(F->CanHijack(Team), TEXT("the doorway is still open before HijackOpenTime"));
		}
		if (Crossed(H + 9.5f))
		{
			const AWWOGameState* GS = World->GetGameState<AWWOGameState>();
			Check(!F->CanHijack(Team) && GS && GS->HijackUntil.IsValidIndex(Team) && GS->HijackUntil[Team] == 0.f,
				TEXT("HijackOpenTime after the kill, the doorway closes again"));
			StepInto(P, OtherHub);
			AutoFootRaceBefore = P->GetActorLocation();
		}
		if (Crossed(H + 10.f))
		{
			Check(!F->IsHijacking(P) && FVector::Dist2D(P->GetActorLocation(), AutoFootRaceBefore) < Near, TEXT("and refuses again"));
			Mutable->HijackOpenTime = 30.f;
			Mutable->HijackStayTime = 30.f;
		}
	}
	// The boss and its cores. Door3 to the last door opened straight (the levers are tested above), with short times.
	const float B = H + 11.f;
	AWWOBoss* Boss = F->BossOf(Team);
	AWWOBossCore* Core = F->CoreOf(Team);
	AWWOBreachPortal* BossDoorway = F->RoomPortal(Team, "Boss");
	auto Hit = [P](AWWOBoss* Target, float Amount)
	{
		FWWODamageBy By(P);
		Target->ApplyHit(Amount, false, Target->GetActorLocation(), FLinearColor::White);
	};
	auto StandOn = [P](const AWWOBossCore* C)
	{
		P->TeleportTo(C->GetActorLocation() + FVector(0, 0, 10.f), P->GetActorRotation(), false, true);
	};
	const float Window = 2.f;
	const int32 Wave = FMath::RoundToInt(GM->BossRoomWave);
	if (Crossed(B))
	{
		Mutable->BossWindow = Window;
		Mutable->CoreDelay = 1.f;
		bool bEarly = false;
		for (int32 D = 3; D < FWWOFootRaceRules::DoorCount; ++D)
		{
			F->OpenDoor(Team, D);
			bEarly |= F->BossOf(Team) != nullptr;
		}
		Check(!bEarly && F->DoorsOpen(Team) == FWWOFootRaceRules::DoorCount - 1, FString::Printf(TEXT("Door3 to Door%d don't bring the boss"), FWWOFootRaceRules::DoorCount - 1));
		F->OpenDoor(Team, FWWOFootRaceRules::DoorCount);
	}
	if (Crossed(B + 0.5f))
	{
		Check(F->DoorsOpen(Team) == FWWOFootRaceRules::DoorCount && Boss != nullptr,
			FString::Printf(TEXT("opening the last door (Door%d) brings the team's boss up"), FWWOFootRaceRules::DoorCount));
		Check(F->BossOf(1 - Team) == nullptr, TEXT("and not the other team's"));
		Check(F->AliveIn(Team, "Boss") == Wave, FString::Printf(TEXT("the boss room fills to %d as soon as the boss is up, before any core (%d)"), Wave, F->AliveIn(Team, "Boss")));
		if (Boss)
		{
			Check(Boss->bShielded && FMath::IsNearlyEqual(Boss->GetMaxHealth(), GM->BossMaxHealth), FString::Printf(TEXT("shielded, with %.0f health (%.0f)"), GM->BossMaxHealth, Boss->GetMaxHealth()));
			// On its platform (the boss room's floor is at its RoomArea's bottom), and in sight from the floor in front of it.
			const float Floor = F->RoomCenter(Team, FWWOFootRaceRules::BossRoom).Z;
			const float Feet = Boss->GetActorLocation().Z - Boss->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			Check(Feet - Floor > 1.2f * WWO::M, FString::Printf(TEXT("the boss stands on its platform (feet %.2f m above the floor)"), (Feet - Floor) / WWO::M));
			const FVector Ahead = FRotator(0.f, Boss->GetActorRotation().Yaw, 0.f).Vector();
			const FVector From = FVector(Boss->GetActorLocation().X, Boss->GetActorLocation().Y, Floor + 1.7f * WWO::M) + Ahead * 18.f * WWO::M;
			FHitResult Sight;
			const bool bBlocked = World->LineTraceSingleByChannel(Sight, From, Boss->GetActorLocation(), ECC_Visibility, FCollisionQueryParams(SCENE_QUERY_STAT(WWOTestBossSight), false, P));
			Check(!bBlocked || Sight.GetActor() == Boss, FString::Printf(TEXT("and can be shot from the floor 18 m in front (%s)"), bBlocked && Sight.GetActor() ? *Sight.GetActor()->GetName() : TEXT("clear")));
			Hit(Boss, 500.f);
		}
	}
	// The boss room keeps its enemies with no core out, refilling at BossRoomRefill; and no stall as it fills.
	static float WorstBossFill = 0.f;
	if (T >= B && T < B + 1.f) WorstBossFill = FMath::Max(WorstBossFill, DeltaTime);
	if (Crossed(B + 1.f))
	{
		UE_LOG(LogWWO, Display, TEXT("FootRace frames: worst %.1f ms as the boss and its room's %d enemies came"), WorstBossFill * 1000.f, Wave);
		Check(WorstBossFill < 0.1f, FString::Printf(TEXT("no stall as the boss room fills (worst frame %.1f ms)"), WorstBossFill * 1000.f));
	}
	if (Crossed(B + 2.9f))
	{
		Check(F->AliveIn(Team, "Boss") >= Wave, FString::Printf(TEXT("with the core delivered and none out, the boss room still holds %d (%d)"), Wave, F->AliveIn(Team, "Boss")));
		TArray<AWWOEnemy*> InBoss = F->EnemiesIn(Team, "Boss");
		FWWODamageBy By(P);
		for (int32 I = 0; I < 3 && I < InBoss.Num(); ++I) InBoss[I]->ApplyHit(100000.f, false, InBoss[I]->GetActorLocation(), FLinearColor::White);
	}
	if (Crossed(B + 3.f + GM->BossRoomRefill * 3.f + 0.4f))
		Check(F->AliveIn(Team, "Boss") >= Wave, FString::Printf(TEXT("three killed are back within %.1f s (%d)"), GM->BossRoomRefill * 3.f + 0.4f, F->AliveIn(Team, "Boss")));
	if (Boss && Crossed(B + 0.7f)) Check(Boss->GetHealth() == Boss->GetMaxHealth(), FString::Printf(TEXT("a hit on the shield does nothing (%.0f)"), Boss->GetHealth()));
	// The first core: CoreDelay after the boss, with its wave.
	if (Crossed(B + 1.6f))
	{
		Check(Core != nullptr && !Core->Carrier && !Core->bDropped, TEXT("a core for the team appears in the hub CoreDelay after the boss"));
		Check(F->AliveIn(Team, "Boss") == Wave, FString::Printf(TEXT("with a wave of %d in the boss room (%d)"), Wave, F->AliveIn(Team, "Boss")));
		Check(F->CoreOf(1 - Team) == nullptr, TEXT("and no core for the other team"));
		F->SetEnergy(Team, Rich);
		if (BossDoorway) StepInto(P, BossDoorway);
	}
	if (Core && Crossed(B + 2.2f)) { Check(F->IsBreaching(P), TEXT("breaching from the boss room")); StandOn(Core); }
	if (Core && Crossed(B + 2.5f))
	{
		Check(Core->Carrier == P, TEXT("walking onto the core picks it up"));
		Shot(TEXT("88_footrace_core"));
		StepInto(P, Hub);
	}
	if (Boss && Crossed(B + 2.8f))
	{
		Check(!Boss->bShielded && F->BossCycle(Team) == 1 && F->CoreOf(Team) == nullptr,
			FString::Printf(TEXT("carried out through the hub doorway, the core drops the shield for window 1 (%.2f s)"), F->BossWindowLeft(Team)));
		Check(BossDoorway && At(P, BossDoorway), FString::Printf(TEXT("and the carrier lands in the boss room (at %s)"), *Where()));
		Hit(Boss, 500.f);
	}
	if (Boss && Crossed(B + 3.f)) Check(FMath::IsNearlyEqual(Boss->GetHealth(), GM->BossMaxHealth - 500.f, 1.f), FString::Printf(TEXT("hits land in the window (%.0f)"), Boss->GetHealth()));
	if (Boss && Crossed(B + 2.8f + Window + 0.3f)) Check(Boss->bShielded && F->BossWindowLeft(Team) == 0.f, TEXT("the shield is back when the window ends"));
	// The next core: dropped by a carrier who dies, then lying where they fell.
	const float C2 = B + 2.8f + Window + 1.6f;
	if (Crossed(C2))
	{
		Check(Core != nullptr, TEXT("the next core comes CoreDelay after the shield is back"));
		F->SetEnergy(Team, Rich);
		if (BossDoorway) StepInto(P, BossDoorway);
	}
	if (Core && Crossed(C2 + 0.5f)) StandOn(Core);
	if (Core && Crossed(C2 + 0.8f))
	{
		Check(Core->Carrier == P, TEXT("picked up again"));
		AutoFootRaceBefore = P->GetActorLocation();
		P->Health->GodMode = 0.f;
		P->Health->Kill();
	}
	if (Core && Crossed(C2 + 1.1f))
	{
		Check(!Core->Carrier && Core->bDropped && FVector::Dist2D(Core->GetActorLocation(), AutoFootRaceBefore) < 1.f * WWO::M,
			TEXT("a carrier who dies drops the core where they fell"));
		const AWWOGameState* GS = World->GetGameState<AWWOGameState>();
		Check(GS && GS->CoreState.IsValidIndex(Team) && GS->CoreState[Team] == 3, TEXT("and every machine is told it lies dropped"));
	}
	// The other team delivering this team's core: a heal, up to full.
	if (Boss && Crossed(C2 + 1.3f))
	{
		EnergyMark = Boss->GetHealth();
		F->DeliverCore(Team, 1 - Team);
		Check(FMath::IsNearlyEqual(Boss->GetHealth(), FMath::Min(EnergyMark + GM->CoreHeal, Boss->GetMaxHealth()), 1.f),
			FString::Printf(TEXT("the other team's delivery heals the boss %.0f (%.0f -> %.0f)"), GM->CoreHeal, EnergyMark, Boss->GetHealth()));
		F->DeliverCore(Team, 1 - Team);
		F->DeliverCore(Team, 1 - Team);
		Check(Boss->GetHealth() == Boss->GetMaxHealth(), FString::Printf(TEXT("never past full (%.0f)"), Boss->GetHealth()));
	}
	// Killing it in a window wins the race.
	if (Boss && Crossed(C2 + 1.5f))
	{
		F->OpenBossWindow(Team);
		Check(F->BossCycle(Team) == 2 && FMath::IsNearlyEqual(F->BossWindowLeft(Team), Window * 4.f / 3.f, 0.05f),
			FString::Printf(TEXT("the second window is a third longer (%.2f s)"), F->BossWindowLeft(Team)));
	}
	// Stealing a core: the other team's boss and its core; the player takes that core from its spot, carries it
	// out through its own hub doorway (nothing happens: it keeps it), then into their hub doorway: in as a
	// hijacker, their boss healed, the core spent.
	const float K = C2 + 4.f;
	AWWOBoss* TheirBoss = F->BossOf(1 - Team);
	AWWOBossCore* TheirCore = F->CoreOf(1 - Team);
	auto LastEvent = [World]() { const AWWOGameState* GS = World->GetGameState<AWWOGameState>(); return GS && GS->KillFeed.Num() ? GS->KillFeed.Last().Victim : FString(); };
	if (Crossed(K)) for (int32 D = 1; D <= FWWOFootRaceRules::DoorCount; ++D) F->OpenDoor(1 - Team, D);
	if (Crossed(K + 1.2f))
	{
		Check(TheirBoss && TheirCore && !TheirCore->Carrier, TEXT("stealing: the other team's boss is up and its core is in the hub"));
		if (TheirBoss)
		{
			F->OpenBossWindow(1 - Team);
			Hit(TheirBoss, 1000.f);
		}
		F->SetEnergy(Team, Rich);
		StepInto(P, Room1);
	}
	if (TheirCore && Crossed(K + 1.6f)) StandOn(TheirCore);
	if (TheirCore && Crossed(K + 1.8f))
	{
		Check(TheirCore->Carrier == P, TEXT("an enemy core can be taken straight from its spot"));
		Check(LastEvent() == FString::Printf(TEXT("stole %s's core"), *AWWOPlayerState::TeamName(1 - Team)), FString::Printf(TEXT("the kill feed says so (%s)"), *LastEvent()));
		EnergyMark = TheirBoss ? TheirBoss->GetHealth() : 0.f;
		StepInto(P, Hub);
	}
	if (TheirCore && Crossed(K + 2.3f))
	{
		Check(TheirCore->Carrier == P && !F->IsBreaching(P) && TheirBoss && TheirBoss->GetHealth() == EnergyMark,
			FString::Printf(TEXT("out through your own hub doorway an enemy core heals nothing: you keep carrying it (%.0f)"), TheirBoss ? TheirBoss->GetHealth() : -1.f));
	}
	if (TheirCore && Crossed(K + 2.6f)) { F->SetEnergy(Team, Rich); StepInto(P, Room1); } // (after the doorway's cooldown)
	if (TheirCore && Crossed(K + 3.f))
	{
		Check(F->IsBreaching(P) && TheirCore->Carrier == P, TEXT("back in the hub with it"));
		StepInto(P, OtherHub);
	}
	if (Crossed(K + 3.7f) && TheirBoss)
	{
		const AWWOBreachPortal* Landing = F->RoomPortal(1 - Team, FWWOFootRaceRules::RoomName(F->OpenedRoom(1 - Team)));
		Check(F->CoreOf(1 - Team) == nullptr && FMath::IsNearlyEqual(TheirBoss->GetHealth(), FMath::Min(EnergyMark + GM->CoreHeal, TheirBoss->GetMaxHealth()), 1.f),
			FString::Printf(TEXT("carried into their hub doorway it heals their boss %.0f and is spent (%.0f -> %.0f)"), GM->CoreHeal, EnergyMark, TheirBoss->GetHealth()));
		Check(F->IsHijacking(P) && At(P, Landing), FString::Printf(TEXT("and the thief goes in as a hijacker, at their furthest opened room (at %s)"), *Where()));
		Check(LastEvent() == FString::Printf(TEXT("healed %s's boss"), *AWWOPlayerState::TeamName(1 - Team)), FString::Printf(TEXT("the kill feed says so (%s)"), *LastEvent()));
	}
	if (Boss && Crossed(K + 4.f))
	{
		// A look at it from the boss room's doorway, then the kill.
		if (BossDoorway)
		{
			const FTransform Spot = BossDoorway->ArrivalSpot(P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			const FRotator Facing(0.f, (Boss->GetActorLocation() - Spot.GetLocation()).Rotation().Yaw, 0.f);
			P->TeleportTo(Spot.GetLocation(), Facing, false, true);
			PC->SetControlRotation(Facing);
			P->SetViewRotation(0.f, Facing.Yaw);
		}
	}
	if (Boss && Crossed(K + 4.4f))
	{
		F->OpenBossWindow(Team); // the second window has run out by now: a third, for the kill
		Shot(TEXT("88_footrace_boss"));
	}
	if (Boss && Crossed(K + 4.6f)) Hit(Boss, GM->BossMaxHealth * 2.f);
	if (Crossed(K + 5.f))
	{
		Check(F->IsOver() && F->Winner == Team, FString::Printf(TEXT("killing the boss wins the race for its team (winner %d)"), F->Winner));
		const AWWOGameState* GS = World->GetGameState<AWWOGameState>();
		Check(GS && GS->bMatchOver && GS->MatchWinner == Team, TEXT("and every machine is told"));
		Check(F->CoreOf(Team) == nullptr, TEXT("the cores go with the match"));
		Shot(TEXT("88_footrace_win"));
	}
	if (Crossed(K + 5.4f)) WWOTest::Exit();
}

// -WWOAutoTestFootRaceBots (on L_FootRaceTest with -WWOBots=7: 4 v 4): the bots play FootRace while the
// player stands in its spawn room out of the way. Every 10 s a `FootRaceBots:` line logs both teams'
// energy, doors, bosses and cores and what the bots are doing; the match must end in a win inside the
// time limit (-WWOFootRaceBotsTime=S, 1500 by default), with bots having breached, pulled levers and
// carried cores. Logs `AutoTest footracebots: ... ok|FAIL`.
void FWWOAutoTest::UpdateAutoFootRaceBotsTest(float DeltaTime)
{
	AWWOCharacter* P = PC->GetWWOCharacter();
	UWorld* World = PC->GetWorld();
	const AWWOGameMode* GM = World->GetAuthGameMode<AWWOGameMode>();
	FWWOFootRaceRules* F = GM ? GM->FootRace() : nullptr;
	if (!P || !F) return;
	auto Check = [](bool bOk, const FString& What) { return WWOTest::Check(TEXT("footracebots"), bOk, What); };
	static float Limit = -1.f;
	if (Limit < 0.f)
	{
		FString Value;
		Limit = WWOSwitches::Value(TEXT("WWOFootRaceBotsTime="), Value) ? FCString::Atof(*Value) : 1500.f;
	}
	P->Health->GodMode = 1.f;
	// The player stays parked where it was placed: the window takes a person's keys and mouse, and the
	// match is the bots' (a run once had a person carry a core).
	static FVector Parked = FVector::ZeroVector;
	if (AutoBotsRaceClock > 1.f)
	{
		if (Parked.IsZero()) Parked = P->GetActorLocation();
		else if (FVector::Dist(P->GetActorLocation(), Parked) > 50.f) P->TeleportTo(Parked, P->GetActorRotation(), false, true);
	}
	AutoBotsRaceClock += DeltaTime;
	int32 Bots = 0;
	TMap<FString, int32> Jobs;
	for (TActorIterator<AWWOBotController> It(World); It; ++It)
	{
		++Bots;
		const AWWOCharacter* Body = Cast<AWWOCharacter>(It->GetPawn());
		Jobs.FindOrAdd(!Body || Body->Health->bDead ? FString(TEXT("dead")) : It->StateIndex() == 4 ? It->JobName() : It->DebugState().Left(8))++;
	}
	// Frame times over each 10 s (the log line's "frame avg/worst"; the first seconds load the map).
	static double FrameSum = 0.0;
	static int32 Frames = 0;
	static float FrameWorst = 0.f;
	FrameSum += DeltaTime;
	++Frames;
	FrameWorst = FMath::Max(FrameWorst, DeltaTime);
	if ((AutoBotsRaceLog -= DeltaTime) <= 0.f)
	{
		AutoBotsRaceLog = 10.f;
		UE_LOG(LogWWO, Display, TEXT("FootRaceBots frames: avg %.1f ms, worst %.1f ms over %d frames; %d enemies alive"),
			Frames ? FrameSum / Frames * 1000.0 : 0.0, FrameWorst * 1000.f, Frames, UWWOWorldSubsystem::Get(World)->LivingEnemies().Num());
		FrameSum = 0.0;
		Frames = 0;
		FrameWorst = 0.f;
		FString JobLine;
		for (const TPair<FString, int32>& J : Jobs) JobLine += FString::Printf(TEXT(" %s %d,"), *J.Key, J.Value);
		auto BossText = [F](int32 Team)
		{
			const AWWOBoss* Boss = F->BossOf(Team);
			return Boss ? FString::Printf(TEXT("%.0f%%%s"), Boss->GetHealth() / FMath::Max(Boss->GetMaxHealth(), 1.f) * 100.f, Boss->bShielded ? TEXT(" sh") : TEXT(" open")) : FString(TEXT("-"));
		};
		UE_LOG(LogWWO, Display, TEXT("FootRaceBots: %.0fs bots %d | energy %.0f/%.0f next %.0f/%.0f | doors %d/%d | room %d/%d | boss %s / %s | cores %d/%d | breaches %d levers %d cores %d stuck %d |%s"),
			AutoBotsRaceClock, Bots, F->Energy(0), F->Energy(1), F->NextEnergy(0), F->NextEnergy(1), F->DoorsOpen(0), F->DoorsOpen(1),
			F->FurthestRoom(0), F->FurthestRoom(1), *BossText(0), *BossText(1), F->CoreOf(0) != nullptr, F->CoreOf(1) != nullptr,
			AWWOBotController::Breaches, AWWOBotController::LeverPulls, AWWOBotController::CoresCarried, AWWOBotController::StuckJumps, *JobLine);
	}
	static float ExitAt = -1.f;
	if (ExitAt >= 0.f)
	{
		if (AutoBotsRaceClock >= ExitAt) WWOTest::Exit(); // a second after the last screenshot (exiting on its frame can stall on a shader)
		return;
	}
	if (F->IsOver() || AutoBotsRaceClock >= Limit)
	{
		const int32 Want = AWWOGameMode::FootRaceTeamSize(World->GetMapName()) * 2 - 1;
		Check(Bots == Want, FString::Printf(TEXT("%d bots joined, full teams with the player (%d)"), Want, Bots));
		Check(AWWOBotController::Breaches >= 4, FString::Printf(TEXT("bots breached (%d)"), AWWOBotController::Breaches));
		Check(AWWOBotController::LeverPulls >= 3, FString::Printf(TEXT("bots pulled levers (%d)"), AWWOBotController::LeverPulls));
		Check(F->DoorsOpen(0) >= 1 && F->DoorsOpen(1) >= 1, FString::Printf(TEXT("both teams opened doors (%d, %d)"), F->DoorsOpen(0), F->DoorsOpen(1)));
		Check(AWWOBotController::CoresCarried >= 1, FString::Printf(TEXT("bots carried boss cores (%d)"), AWWOBotController::CoresCarried));
		Check(F->IsOver(), FString::Printf(TEXT("a team won inside %.0f s (winner %d, %.0f s)"), Limit, F->Winner, AutoBotsRaceClock));
		Shot(TEXT("89_footracebots_end"));
		ExitAt = AutoBotsRaceClock + 1.f;
	}
}

// -WWOAutoTestSurfaces (on L_FootRaceTest, alone): the kit that needs a surface, on the dressed map, at seven
// spots in lane 0 (Room2, which has no enemies at the start; the boss room, no boss yet; the hub): the open
// floor, beside a cover piece (one dropped beside it, one thrown at its face), on top of one, at the foot of a
// side wall, on the boss platform and its ramp, and in the hub. At each: a pool grenade dropped from 1.5 m, a
// grenade with Spore Patches dropped from 3 m (it bounces, sprouting patches) and a tripmine. Each pool and
// patch must lie on the surface under it (within 10 cm of where a downward trace meets it), each mine stuck to
// a surface; one that lands on top of cover settles on the floor below it (WWOFX::SettleOnGround), and Spore
// Patches sprout only where the grenade touches down (not off the cover's face). Screenshots 87_surfaces_<spot>. Logs `AutoTest surfaces: ... ok|FAIL` and a `Surfaces:` line per
// effect (its height over the surface found below it).
namespace
{
	struct FSurfaceSpot { FString Name; FVector At; FVector Throw = FVector::ZeroVector; };
	TArray<FSurfaceSpot> SurfaceSpots;
	TWeakObjectPtr<ACameraActor> SurfaceCamera;
	TArray<TWeakObjectPtr<AWWOGrenade>> SurfaceMines; // this spot's tripmine
	int32 SurfaceMinesStuck = 0, SurfaceMinesOff = 0;

	// The first surface a downward trace meets from Above (what the game's own ground traces answer).
	bool SurfaceBelow(UWorld* World, const FVector& Above, float Reach, FVector& Out)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOTestSurface), false);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (Cast<AWWOCharacter>(*It) || Cast<AWWOGrenade>(*It) || Cast<AWWOEnemy>(*It)) Params.AddIgnoredActor(*It);
			// A trace that starts inside a cover box meets it where it starts: look through the one it starts in.
			FVector O, E;
			if (It->ActorHasTag("Cover") && (It->GetActorBounds(true, O, E), FBox(O - E, O + E).IsInside(Above))) Params.AddIgnoredActor(*It);
		}
		if (!World->LineTraceSingleByChannel(Hit, Above, Above - FVector(0, 0, Reach), ECC_Visibility, Params)) return false;
		Out = Hit.ImpactPoint;
		return true;
	}
}

void FWWOAutoTest::UpdateAutoSurfacesTest(float DeltaTime)
{
	AWWOCharacter* P = PC->GetWWOCharacter();
	UWorld* World = PC->GetWorld();
	const AWWOGameMode* GM = World->GetAuthGameMode<AWWOGameMode>();
	FWWOFootRaceRules* F = GM ? GM->FootRace() : nullptr;
	if (!P) return;
	const float Before = AutoSurfacesClock;
	AutoSurfacesClock += DeltaTime;
	const float T = AutoSurfacesClock;
	auto Crossed = [&](float At) { return WWOTest::Crossed(Before, T, At); };
	auto Check = [](bool bOk, const FString& What) { return WWOTest::Check(TEXT("surfaces"), bOk, What); };
	P->Health->GodMode = 1.f;
	if (Crossed(1.f) && !Check(F != nullptr, TEXT("the map runs FootRace's rules"))) { WWOTest::Exit(); return; }
	if (!F) return;
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(P);
	auto GrenadeOf = [L](EWWOGrenadeKind Kind) -> UWWOGrenadeData*
	{
		for (UWWOGrenadeData* G : L->Catalog.Grenades) if (G && G->Kind == Kind) return G;
		return nullptr;
	};
	auto SpecOf = [](UWWOGrenadeData* G)
	{
		FWWOGrenadeSpec Spec;
		Spec.Color = G->Color;
		Spec.Damage = G->Damage;
		Spec.BlastRadius = G->BlastRadius;
		Spec.Data = G;
		return Spec;
	};
	UWWOAbilityData* Patches = nullptr;
	for (UWWOAbilityData* A : L->Catalog.Abilities) if (A && A->Id == WWOIds::Ability::SporePatches) Patches = A;
	const float Step = 7.f;
	if (Crossed(2.f))
	{
		// The spots, from the level's markers and actors (lane 0; Unreal X is across the lane, Y along it).
		SurfaceSpots.Reset();
		const FVector Room2 = F->RoomCenter(0, 2);
		const FVector Open = Room2 + FVector(0.f, -15.5f * WWO::M, 0.f);
		SurfaceSpots.Add({ TEXT("floor"), Open });
		const AActor* Cover = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!It->ActorHasTag("Cover") || F->RoomAt(0, It->GetActorLocation()) != 2) continue;
			if (!Cover || FVector::Dist2D(It->GetActorLocation(), Open) < FVector::Dist2D(Cover->GetActorLocation(), Open)) Cover = *It;
		}
		if (Check(Cover != nullptr, TEXT("a cover piece in Room2")))
		{
			FVector O, E;
			Cover->GetActorBounds(true, O, E);
			const FVector Side = O + FVector(0.f, E.Y + 1.2f * WWO::M, 0.f);
			SurfaceSpots.Add({ TEXT("beside_cover"), FVector(Side.X, Side.Y, Room2.Z),
				FVector(0.f, -9.f * WWO::M, 0.f) }); // also one thrown straight at its face from there
			SurfaceSpots.Add({ TEXT("on_cover"), FVector(O.X, O.Y, O.Z + E.Z) });
		}
		SurfaceSpots.Add({ TEXT("wall_foot"), Open + FVector(-18.9f * WWO::M, 0.f, 0.f) });
		for (TActorIterator<ATargetPoint> It(World); It; ++It)
		{
			if (It->ActorHasTag("BossSpawn") && It->ActorHasTag("Lane0"))
			{
				SurfaceSpots.Add({ TEXT("platform"), It->GetActorLocation() + FVector(2.f * WWO::M, 0.f, 0.f) });
				SurfaceSpots.Add({ TEXT("ramp"), It->GetActorLocation() + FVector(0.f, -11.f * WWO::M, 0.f) });
			}
			if (It->ActorHasTag("CoreSpawn")) SurfaceSpots.Add({ TEXT("hub"), It->GetActorLocation() + FVector(8.f * WWO::M, 8.f * WWO::M, 0.f) });
		}
		Check(SurfaceSpots.Num() == 7 && Patches && GrenadeOf(EWWOGrenadeKind::Pool) && GrenadeOf(EWWOGrenadeKind::Tripmine),
			FString::Printf(TEXT("seven spots, and a pool grenade, a tripmine and Spore Patches to try (%d spots)"), SurfaceSpots.Num()));
	}
	for (int32 I = 0; I < SurfaceSpots.Num(); ++I)
	{
		const FSurfaceSpot& S = SurfaceSpots[I];
		const float T0 = 3.f + I * Step;
		FVector Top;
		const bool bTop = SurfaceBelow(World, S.At + FVector(0, 0, 8.f * WWO::M), 12.f * WWO::M, Top);
		// The view: a camera 11 m back and 4 m up, looking at the spot (the player stays in its spawn room, so no
		// room fills because of it).
		if (Crossed(T0) && bTop)
		{
			if (!SurfaceCamera.IsValid())
			{
				ACameraActor* Cam = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity);
				Cam->GetCameraComponent()->bConstrainAspectRatio = false;
				Cam->GetCameraComponent()->SetFieldOfView(80.f);
				SurfaceCamera = Cam;
			}
			const FVector Eye = Top + FVector(-8.f * WWO::M, -8.f * WWO::M, 4.f * WWO::M);
			SurfaceCamera->SetActorLocationAndRotation(Eye, (Top - Eye).Rotation());
			PC->SetViewTarget(SurfaceCamera.Get());
		}
		if (Crossed(T0))
		{
			if (!Check(bTop, FString::Printf(TEXT("%s: a surface under the spot"), *S.Name))) continue;
			if (UWWOGrenadeData* Pool = GrenadeOf(EWWOGrenadeKind::Pool))
				AWWOGrenade::Throw(World, SpecOf(Pool), Top + FVector(0, 0, 1.5f * WWO::M), FVector::ZeroVector, P);
			SurfaceMines.Reset();
			if (UWWOGrenadeData* Mine = GrenadeOf(EWWOGrenadeKind::Tripmine))
				SurfaceMines.Add(AWWOGrenade::Throw(World, SpecOf(Mine), Top + FVector(1.5f * WWO::M, 1.5f * WWO::M, 1.5f * WWO::M), FVector::ZeroVector, P));
			if (UWWOGrenadeData* Boom = GrenadeOf(EWWOGrenadeKind::Explosive); Boom && Patches)
			{
				FWWOGrenadeSpec Spec = SpecOf(Boom);
				Spec.SporePatches = Patches;
				Spec.Fuse = 3.f;
				AWWOGrenade::Throw(World, Spec, Top + FVector(1.5f * WWO::M, -1.5f * WWO::M, 3.f * WWO::M), FVector::ZeroVector, P);
				if (!S.Throw.IsZero()) AWWOGrenade::Throw(World, Spec, Top + FVector(0, 0, 1.2f * WWO::M), S.Throw, P); // at the cover's face
			}
		}
		// The mine: stuck to a surface once it lands (checked early: a mine near an enemy goes off).
		if (Crossed(T0 + 0.75f))
		{
			SurfaceMinesStuck = SurfaceMinesOff = 0;
			for (const TWeakObjectPtr<AWWOGrenade>& G : SurfaceMines)
			{
				if (!G.IsValid() || !G->IsStuck()) continue;
				++SurfaceMinesStuck;
				const FVector C = G->GetActorLocation();
				const bool bOn = World->SweepTestByChannel(C, C - FVector(0, 0, 1.f), FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(40.f));
				UE_LOG(LogWWO, Display, TEXT("Surfaces: %s mine stuck at %s, %s"), *S.Name, *C.ToCompactString(), bOn ? TEXT("touching a surface") : TEXT("touching nothing"));
				if (!bOn) ++SurfaceMinesOff;
			}
		}
		if (Crossed(T0 + 4.f)) Shot(*FString::Printf(TEXT("87_surfaces_%s"), *S.Name));
		if (Crossed(T0 + 4.5f) && bTop)
		{
			// Every pool and patch near the spot lies on the surface under it; every mine is stuck to something.
			int32 Pools = 0, PatchesSeen = 0, Mines = 0, Off = 0;
			float Worst = 0.f;
			auto Measure = [&](const AActor* A, const TCHAR* What)
			{
				FVector Under;
				const bool bUnder = SurfaceBelow(World, A->GetActorLocation() + FVector(0, 0, 1.f * WWO::M), 20.f * WWO::M, Under);
				const float Gap = bUnder ? (A->GetActorLocation().Z - Under.Z) / WWO::M : 99.f;
				UE_LOG(LogWWO, Display, TEXT("Surfaces: %s %s at %s: %.2f m over the surface below it"), *S.Name, What, *A->GetActorLocation().ToCompactString(), Gap);
				Worst = FMath::Max(Worst, FMath::Abs(Gap));
				if (FMath::Abs(Gap) > 0.1f) ++Off;
				// Landed on top of cover: it runs off to the floor (WWOFX::SettleOnGround), not a disc in the air.
				if (S.Name == TEXT("on_cover") && FMath::Abs(A->GetActorLocation().Z - F->RoomCenter(0, 2).Z) > 0.1f * WWO::M)
				{
					UE_LOG(LogWWO, Display, TEXT("Surfaces: %s %s is %.2f m above the room's floor"), *S.Name, What, (A->GetActorLocation().Z - F->RoomCenter(0, 2).Z) / WWO::M);
					++Off;
				}
			};
			const float Reach = 6.f * WWO::M;
			for (TActorIterator<AWWOSporePool> It(World); It; ++It) if (FVector::Dist2D(It->GetActorLocation(), Top) < Reach && !It->IsHidden()) { ++Pools; Measure(*It, TEXT("pool")); }
			for (TActorIterator<AWWOSporePatch> It(World); It; ++It) if (FVector::Dist2D(It->GetActorLocation(), Top) < Reach + FMath::Abs(S.Throw.Y)) { ++PatchesSeen; Measure(*It, TEXT("patch")); }
			Mines = SurfaceMinesStuck;
			Off += SurfaceMinesOff;
			Check(Pools >= 1 && PatchesSeen >= 1 && Mines >= 1 && Off == 0,
				FString::Printf(TEXT("%s: %d pool(s), %d patch(es), %d mine(s), all on a surface (%d off, the worst %.2f m)"), *S.Name, Pools, PatchesSeen, Mines, Off, Worst));
		}
	}
	if (SurfaceSpots.Num() > 0 && Crossed(3.f + SurfaceSpots.Num() * Step + 0.5f)) WWOTest::Exit();
}
