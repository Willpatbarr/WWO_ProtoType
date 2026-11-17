// -WWOAutoTestFootRace (on L_FootRaceTest, alone: the player is on team 0): the breach portals.
// The player is put straight into a doorway's opening (as walking into it would leave them) and the
// rules must carry them through: Room1's doorway to the team's hub doorway and back, Room3's doorway
// to the hub and back to Room3 (not Room1), and the other team's hub doorway does nothing. Screenshots
// 88_footrace_*. Logs `AutoTest footrace: ... ok|FAIL`.

#include "Player/WWOAutoTest.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOTestRun.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
#include "Game/WWOGameMode.h"
#include "Game/WWOFootRaceRules.h"
#include "Game/WWOPlayerState.h"
#include "World/WWOBreachPortal.h"
#include "WWOTypes.h"
#include "Components/CapsuleComponent.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	constexpr float Near = 1.5f * WWO::M; // how close to an arrival spot counts as "arrived there"

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
	const FWWOFootRaceRules* F = GM ? GM->FootRace() : nullptr;
	if (!P || !GM) return;
	const float Before = AutoFootRaceClock;
	AutoFootRaceClock += DeltaTime;
	auto Crossed = [&](float T) { return WWOTest::Crossed(Before, AutoFootRaceClock, T); };
	auto Check = [](bool bOk, const FString& What) { return WWOTest::Check(TEXT("footrace"), bOk, What); };
	auto Where = [P]() { return P->GetActorLocation().ToCompactString(); };

	if (Crossed(1.f) && !Check(F != nullptr, TEXT("the map runs FootRace's rules"))) { WWOTest::Exit(); return; }
	if (!F) return;
	P->Health->GodMode = 1.f;
	const AWWOPlayerState* PS = PC->GetPlayerState<AWWOPlayerState>();
	const int32 Team = PS ? PS->Team : -1;
	AWWOBreachPortal* Room1 = F->RoomPortal(Team, "Room1");
	AWWOBreachPortal* Room3 = F->RoomPortal(Team, "Room3");
	AWWOBreachPortal* Hub = F->HubPortal(Team);
	AWWOBreachPortal* OtherHub = F->HubPortal(1 - Team);

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
	if (Crossed(4.5f)) StepInto(P, Room1);
	if (Crossed(5.5f))
	{
		Check(At(P, Hub), FString::Printf(TEXT("Room1's doorway leads to the team's hub doorway (at %s)"), *Where()));
		Shot(TEXT("88_footrace_hub_arrival"));
	}
	if (Crossed(6.5f)) StepInto(P, Hub);
	if (Crossed(7.5f)) Check(At(P, Room1), FString::Printf(TEXT("the hub doorway leads back to Room1 (at %s)"), *Where()));
	if (Crossed(8.5f)) StepInto(P, Room3);
	if (Crossed(9.5f)) Check(At(P, Hub), FString::Printf(TEXT("Room3's doorway leads to the hub too (at %s)"), *Where()));
	if (Crossed(10.5f)) StepInto(P, Hub);
	if (Crossed(11.5f)) Check(At(P, Room3), FString::Printf(TEXT("back through the hub doorway lands in Room3, the room it came from (at %s)"), *Where()));
	if (Crossed(12.5f))
	{
		StepInto(P, OtherHub);
		AutoFootRaceBefore = P->GetActorLocation();
	}
	if (Crossed(13.5f))
	{
		Check(FVector::Dist2D(P->GetActorLocation(), AutoFootRaceBefore) < Near,
			FString::Printf(TEXT("the other team's hub doorway does nothing (at %s)"), *Where()));
		Shot(TEXT("88_footrace_other_doorway"));
	}
	if (Crossed(14.5f)) WWOTest::Exit();
}
