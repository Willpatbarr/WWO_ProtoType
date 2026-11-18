// -WWOAutoTestFootRace (on L_FootRaceTest, alone: the player is on team 0): FootRace's portals, breach
// energy, doors and levers, and lane rooms. The player is put straight into a doorway's opening (as
// walking into it would leave them), and holds the Revive key at a lever through ScriptHold.
// - with no energy, a room doorway refuses; an enemy kill earns EnergyPerEnemy;
// - Room1's doorway to the team's hub doorway and back; the hub drains the pool only while in it;
// - the other team's hub doorway does nothing; running out sends you back; a death costs a share;
// - Door1 blocks the doorway; its lever goes live on a breach from Room1 and opens it; that freezes the
//   pool, a hub kill splits between the pools, Lever2 waits; leaving lands in Room2 and swaps the pools;
//   a breach from Room2 opens Door2; leaving lands in Room3;
// - lane rooms: both Room1s fill at the start, Room3 fills (with its Tank) once stood in, a lane's
//   enemies leave the other team's players alone, a kill is refilled after RoomRefillTime;
// - the boss: Door3 brings it up shielded (hits do nothing), a window (opened as wwo.BossWindow does)
//   lets hits land and ends on time, the next window is a third longer, killing it wins the race.
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
#include "Systems/WWOWorldSubsystem.h"
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
		Check(Door1 && Door2 && F->DoorOf(Team, 3) && F->DoorOf(1 - Team, 1) && Lever1 && Lever2 && F->LeverOf(Team, 3) && F->LeverOf(1 - Team, 1),
			TEXT("three doors and three levers per team"));
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
	}
	// Room1 -> hub -> Room1, the pool draining only while in the hub.
	if (Crossed(7.f)) { F->SetEnergy(Team, Rich); StepInto(P, Room1); }
	if (Crossed(7.5f))
	{
		Check(At(P, Hub) && F->IsBreaching(P), FString::Printf(TEXT("Room1's doorway leads to the team's hub doorway (at %s)"), *Where()));
		Check(Lever1->bLive && !Lever2->bLive, TEXT("a breach from Room1 makes Lever1 live, not Lever2"));
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
	if (Crossed(24.f + GM->LeverHoldTime * 0.5f))
	{
		Check(!Door1->IsOpen() && Lever1->Progress > 0.2f, FString::Printf(TEXT("holding the key pulls the lever over time (%.2f)"), Lever1->Progress));
		Shot(TEXT("88_footrace_lever"));
	}
	if (Crossed(24.f + Hold + 0.2f))
	{
		Check(Door1->IsOpen() && F->DoorsOpen(Team) == 1 && Lever1->bPulled, TEXT("the pull opens Door1"));
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
	// The lane rooms.
	const int32 Want = FMath::RoundToInt(GM->RoomEnemies);
	const float Rooms = 30.f + Hold + 1.5f + Want * GM->RoomSpawnStagger + 1.f; // Room3 has had time to fill
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
	if (Crossed(Rooms + 1.f + GM->RoomRefillTime))
	{
		Check(F->AliveIn(Team, "Room3") == Want, FString::Printf(TEXT("and Room3 refills to %d within %.0f s (%d)"), Want, GM->RoomRefillTime, F->AliveIn(Team, "Room3")));
		const FTransform Spot = Room3->ArrivalSpot(P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		P->TeleportTo(Spot.GetLocation(), Spot.Rotator());
		PC->SetControlRotation(Spot.Rotator());
		P->SetViewRotation(0.f, Spot.Rotator().Yaw);
	}
	if (Crossed(Rooms + 1.5f + GM->RoomRefillTime)) Shot(TEXT("88_footrace_room_enemies"));
	// The boss. Door3 opened straight (the levers are tested above), with a short window to wait out.
	const float B = Rooms + 2.f + GM->RoomRefillTime;
	AWWOGameMode* Mutable = Cast<AWWOGameMode>(World->GetAuthGameMode());
	AWWOBoss* Boss = F->BossOf(Team);
	auto Hit = [P](AWWOBoss* Target, float Amount)
	{
		FWWODamageBy By(P);
		Target->ApplyHit(Amount, false, Target->GetActorLocation(), FLinearColor::White);
	};
	const float Window = 2.f;
	if (Crossed(B))
	{
		Mutable->BossWindow = Window;
		F->OpenDoor(Team, 3);
	}
	if (Crossed(B + 0.5f))
	{
		Check(F->DoorsOpen(Team) == 3 && Boss != nullptr, TEXT("opening Door3 brings the team's boss up"));
		Check(F->BossOf(1 - Team) == nullptr, TEXT("and not the other team's"));
		if (Boss)
		{
			Check(Boss->bShielded && FMath::IsNearlyEqual(Boss->GetMaxHealth(), GM->BossHealth), FString::Printf(TEXT("shielded, with %.0f health (%.0f)"), GM->BossHealth, Boss->GetMaxHealth()));
			Hit(Boss, 500.f);
		}
	}
	if (Boss && Crossed(B + 0.7f)) Check(Boss->GetHealth() == Boss->GetMaxHealth(), FString::Printf(TEXT("a hit on the shield does nothing (%.0f)"), Boss->GetHealth()));
	if (Boss && Crossed(B + 1.f))
	{
		F->OpenBossWindow(Team);
		Check(!Boss->bShielded && F->BossCycle(Team) == 1 && FMath::IsNearlyEqual(F->BossWindowLeft(Team), Window, 0.05f),
			FString::Printf(TEXT("a window drops the shield for %.1f s (%.2f)"), Window, F->BossWindowLeft(Team)));
		Hit(Boss, 500.f);
	}
	if (Boss && Crossed(B + 1.2f)) Check(FMath::IsNearlyEqual(Boss->GetHealth(), GM->BossHealth - 500.f, 1.f), FString::Printf(TEXT("hits land in the window (%.0f)"), Boss->GetHealth()));
	if (Boss && Crossed(B + 1.f + Window + 0.3f)) Check(Boss->bShielded && F->BossWindowLeft(Team) == 0.f, TEXT("the shield is back when the window ends"));
	if (Boss && Crossed(B + 3.6f))
	{
		F->OpenBossWindow(Team);
		Check(F->BossCycle(Team) == 2 && FMath::IsNearlyEqual(F->BossWindowLeft(Team), Window * 4.f / 3.f, 0.05f),
			FString::Printf(TEXT("the second window is a third longer (%.2f s)"), F->BossWindowLeft(Team)));
		Check(PC->GetWorld()->GetGameState<AWWOGameState>()->BossWindow.IsValidIndex(Team), TEXT("the boss shows on the game state"));
		// A look at it: from the boss room's doorway.
		if (AWWOBreachPortal* BossDoorway = F->RoomPortal(Team, "Boss"))
		{
			const FTransform Spot = BossDoorway->ArrivalSpot(P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			const FRotator Facing = (Boss->GetActorLocation() - Spot.GetLocation()).Rotation();
			P->TeleportTo(Spot.GetLocation(), FRotator(0.f, Facing.Yaw, 0.f), false, true);
			PC->SetControlRotation(FRotator(0.f, Facing.Yaw, 0.f));
			P->SetViewRotation(0.f, Facing.Yaw);
		}
	}
	if (Boss && Crossed(B + 4.f)) Shot(TEXT("88_footrace_boss"));
	if (Boss && Crossed(B + 4.2f)) Hit(Boss, GM->BossHealth * 2.f);
	if (Crossed(B + 4.6f))
	{
		Check(F->IsOver() && F->Winner == Team, FString::Printf(TEXT("killing the boss wins the race for its team (winner %d)"), F->Winner));
		const AWWOGameState* GS = PC->GetWorld()->GetGameState<AWWOGameState>();
		Check(GS && GS->bMatchOver && GS->MatchWinner == Team, TEXT("and every machine is told"));
		Shot(TEXT("88_footrace_win"));
	}
	if (Crossed(B + 5.f)) WWOTest::Exit();
}
