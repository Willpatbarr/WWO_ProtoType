// -WWOAutoTestFootRace (on L_FootRaceTest, alone: the player is on team 0): the breach portals and
// breach energy. The player is put straight into a doorway's opening (as walking into it would leave
// them) and the rules must carry them through, or refuse:
// - with no energy, a room doorway refuses; an enemy kill earns EnergyPerEnemy;
// - Room1's doorway to the team's hub doorway and back, Room3's to the hub and back to Room3;
// - the hub drains the pool while the player is in it, and not once they are out;
// - the other team's hub doorway does nothing;
// - running out of energy in the hub sends the player back to their room;
// - dying in the hub costs BreachDeathLoss of the pool.
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
#include "World/WWOBreachPortal.h"
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
	float EnergyMark = 0.f;               // the pool when a step began, for the step that checks it

	// Puts the body's feet in the middle of a doorway's opening.
	void StepInto(AWWOCharacter* P, const AWWOBreachPortal* Portal)
	{
		const float Half = P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		P->TeleportTo(Portal->GetActorLocation() + FVector(0, 0, Half + 5.f), Portal->GetActorRotation());
	}

	bool At(const AWWOCharacter* P, const AWWOBreachPortal* Portal)
	{
		if (!Portal) return false;
		const FTransform Spot = Portal->ArrivalSpot(P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		return FVector::Dist2D(P->GetActorLocation(), Spot.GetLocation()) < Near;
	}
}

void FWWOAutoTest::UpdateAutoFootRaceTest(float DeltaTime)
{
	AWWOCharacter* P = PC->GetWWOCharacter();
	const AWWOGameMode* GM = PC->GetWorld()->GetAuthGameMode<AWWOGameMode>();
	FWWOFootRaceRules* F = GM ? GM->FootRace() : nullptr;
	if (!P || !GM) return;
	const float Before = AutoFootRaceClock;
	AutoFootRaceClock += DeltaTime;
	auto Crossed = [&](float T) { return WWOTest::Crossed(Before, AutoFootRaceClock, T); };
	auto Check = [](bool bOk, const FString& What) { return WWOTest::Check(TEXT("footrace"), bOk, What); };
	auto Where = [P]() { return P->GetActorLocation().ToCompactString(); };

	if (Crossed(1.f) && !Check(F != nullptr, TEXT("the map runs FootRace's rules"))) { WWOTest::Exit(); return; }
	if (!F) return;
	if (AutoFootRaceClock < 22.f) P->Health->GodMode = 1.f; // until the death step
	const AWWOPlayerState* PS = PC->GetPlayerState<AWWOPlayerState>();
	const int32 Team = PS ? PS->Team : -1;
	AWWOBreachPortal* Room1 = F->RoomPortal(Team, "Room1");
	AWWOBreachPortal* Room3 = F->RoomPortal(Team, "Room3");
	AWWOBreachPortal* Hub = F->HubPortal(Team);
	AWWOBreachPortal* OtherHub = F->HubPortal(1 - Team);
	const float Rich = GM->BreachThreshold * 2.f; // plenty, for the steps about the doorways themselves

	if (Crossed(2.f))
	{
		Check(Team == 0, FString::Printf(TEXT("alone, the player is on team 0 (team %d)"), Team));
		Check(Room1 && Room3 && F->RoomPortal(Team, "Room2") && F->RoomPortal(Team, "Boss") && Hub && OtherHub,
			TEXT("a doorway in each of the team's rooms, and one per team in the hub"));
		bool bInSpawn = false;
		for (TActorIterator<ATargetPoint> It(PC->GetWorld()); It; ++It)
		{
			if (It->ActorHasTag("TeamSpawn") && It->ActorHasTag(Team == 1 ? FName("Lane1") : FName("Lane0"))
				&& FVector::Dist2D(It->GetActorLocation(), P->GetActorLocation()) < 1.f * WWO::M) bInSpawn = true;
		}
		Check(bInSpawn, FString::Printf(TEXT("placed on one of its team's spawn spots (at %s)"), *Where()));
		Check(F->Energy(0) == 0.f && F->Energy(1) == 0.f, TEXT("both teams start with no breach energy"));
	}
	if (!Room1 || !Room3 || !Hub || !OtherHub) return;
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
		const FVector At = Room1->ArrivalSpot(100.f).GetLocation() + Room1->GetActorForwardVector() * 6.f * WWO::M;
		AWWOEnemy* E = PC->GetWorld()->SpawnActor<AWWOEnemy>(Kind.Class, At, FRotator::ZeroRotator, WWO::AlwaysSpawn());
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
		Check(At(P, Room1) && !F->IsBreaching(P), FString::Printf(TEXT("the hub doorway leads back to Room1 (at %s)"), *Where()));
		EnergyMark = F->Energy(Team);
	}
	if (Crossed(11.f)) Check(F->Energy(Team) == EnergyMark, TEXT("out of the hub, nothing drains"));
	// Room3 -> hub -> Room3.
	if (Crossed(11.5f)) StepInto(P, Room3);
	if (Crossed(12.5f)) Check(At(P, Hub), FString::Printf(TEXT("Room3's doorway leads to the hub too (at %s)"), *Where()));
	if (Crossed(13.f)) StepInto(P, Hub);
	if (Crossed(14.f)) Check(At(P, Room3), FString::Printf(TEXT("back through the hub doorway lands in Room3, the room it came from (at %s)"), *Where()));
	// The other team's hub doorway.
	if (Crossed(14.5f))
	{
		StepInto(P, OtherHub);
		AutoFootRaceBefore = P->GetActorLocation();
	}
	if (Crossed(15.5f))
	{
		Check(FVector::Dist2D(P->GetActorLocation(), AutoFootRaceBefore) < Near,
			FString::Printf(TEXT("the other team's hub doorway does nothing (at %s)"), *Where()));
		Shot(TEXT("88_footrace_other_doorway"));
	}
	// Running out in the hub sends you back.
	if (Crossed(16.f)) { F->SetEnergy(Team, Rich); StepInto(P, Room1); }
	if (Crossed(17.f)) { Check(F->IsBreaching(P), TEXT("in the hub again")); F->SetEnergy(Team, GM->BreachDrain * 0.3f); }
	if (Crossed(18.f))
	{
		Check(At(P, Room1) && !F->IsBreaching(P), FString::Printf(TEXT("out of energy, the breacher is sent back to Room1 (at %s)"), *Where()));
		Check(F->Energy(Team) == 0.f, FString::Printf(TEXT("and the pool is empty (%.1f)"), F->Energy(Team)));
	}
	// Dying in the hub costs a share of the pool.
	if (Crossed(19.f)) { F->SetEnergy(Team, Rich); StepInto(P, Room1); }
	if (Crossed(22.f))
	{
		P->Health->GodMode = 0.f;
		EnergyMark = F->Energy(Team);
		Check(F->IsBreaching(P), TEXT("breaching before the death"));
		P->Health->Kill();
	}
	if (Crossed(22.3f))
	{
		const float Want = EnergyMark * (1.f - GM->BreachDeathLoss);
		Check(FMath::Abs(F->Energy(Team) - Want) < 1.f,
			FString::Printf(TEXT("a breacher's death costs %.0f%% of the pool (%.1f -> %.1f, want %.1f)"), GM->BreachDeathLoss * 100.f, EnergyMark, F->Energy(Team), Want));
		Check(!F->IsBreaching(P), TEXT("and the dead player is no longer breaching"));
	}
	if (Crossed(23.f)) WWOTest::Exit();
}
