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

namespace
{
	constexpr float Near = 1.5f * WWO::M; // how close to an arrival spot counts as "arrived there"
	float EnergyMark = 0.f;               // a pool when a step began, for the step that checks it
	float NextMark = 0.f;
	// Room3's enemies as each is first seen (the tick it spawns): how far from the nearest of the room's
	// EnemySpawn markers, and which marker that is.
	TMap<TWeakObjectPtr<AWWOEnemy>, TPair<float, int32>> Room3Seen;

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
	// The boss and its cores. Door3 to the last door opened straight (the levers are tested above), with short times.
	const float B = Rooms + 2.f + GM->LaneRoomRefill;
	AWWOGameMode* Mutable = Cast<AWWOGameMode>(World->GetAuthGameMode());
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
		Check(F->AliveIn(Team, "Boss") == 0, TEXT("the boss room has no enemies before its first core"));
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
	if (Boss && Crossed(C2 + 4.f))
	{
		// A look at it from the boss room's doorway (respawned by now), then the kill.
		if (BossDoorway)
		{
			const FTransform Spot = BossDoorway->ArrivalSpot(P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			const FRotator Facing(0.f, (Boss->GetActorLocation() - Spot.GetLocation()).Rotation().Yaw, 0.f);
			P->TeleportTo(Spot.GetLocation(), Facing, false, true);
			PC->SetControlRotation(Facing);
			P->SetViewRotation(0.f, Facing.Yaw);
		}
	}
	if (Boss && Crossed(C2 + 4.4f))
	{
		F->OpenBossWindow(Team); // the second window has run out by now: a third, for the kill
		Shot(TEXT("88_footrace_boss"));
	}
	if (Boss && Crossed(C2 + 4.6f)) Hit(Boss, GM->BossMaxHealth * 2.f);
	if (Crossed(C2 + 5.f))
	{
		Check(F->IsOver() && F->Winner == Team, FString::Printf(TEXT("killing the boss wins the race for its team (winner %d)"), F->Winner));
		const AWWOGameState* GS = World->GetGameState<AWWOGameState>();
		Check(GS && GS->bMatchOver && GS->MatchWinner == Team, TEXT("and every machine is told"));
		Check(F->CoreOf(Team) == nullptr, TEXT("the cores go with the match"));
		Shot(TEXT("88_footrace_win"));
	}
	if (Crossed(C2 + 5.4f)) WWOTest::Exit();
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
	if ((AutoBotsRaceLog -= DeltaTime) <= 0.f)
	{
		AutoBotsRaceLog = 10.f;
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
