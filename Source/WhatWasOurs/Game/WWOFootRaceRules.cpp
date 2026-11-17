// FootRace (Game/WWOFootRaceRules.h).

#include "Game/WWOFootRaceRules.h"
#include "Game/WWOGameMode.h"
#include "Game/WWOGameState.h"
#include "Game/WWOPlayerState.h"
#include "Game/WWOTeamDeathmatch.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
#include "Enemies/WWOEnemy.h"
#include "Allies/WWOAlly.h"
#include "World/WWOBreachPortal.h"
#include "FX/WWOFX.h"
#include "Systems/WWOWorldSubsystem.h"
#include "WWOTypes.h"
#include "WhatWasOurs.h"
#include "Components/CapsuleComponent.h"
#include "Engine/TargetPoint.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"

namespace
{
	FName LaneTag(int32 Team) { return FName(*FString::Printf(TEXT("Lane%d"), Team)); }

	// The room tag on a PortalAnchor (Room1..Room3, Boss).
	FName RoomOf(const AActor* Marker)
	{
		for (const FName& Tag : Marker->Tags)
		{
			const FString S = Tag.ToString();
			if (S.StartsWith(TEXT("Room")) || S == TEXT("Boss")) return Tag;
		}
		return NAME_None;
	}

	constexpr float BackToWall = 1.2f; // m: a doorway stands this far behind its marker, near the wall at its back
}

void FWWOFootRaceRules::Begin()
{
	UWorld* World = Mode.GetWorld();
	for (TActorIterator<AWWOEnemy> It(World); It; ++It) It->Destroy();
	for (TActorIterator<AWWOAlly> It(World); It; ++It) It->Destroy();
	if (AWWOGameState* GS = Mode.GetGameState<AWWOGameState>())
	{
		GS->bPvP = true; // the teams hurt each other; respawns go through PvPSpawnFor (SpawnSpot)
		GS->TeamScores = { 0, 0 };
		GS->bMatchOver = false;
		GS->MatchWinner = -1;
	}
	SpawnPortals();
	State = EWWOWaveState::Match;
}

void FWWOFootRaceRules::SpawnPortals()
{
	UWorld* World = Mode.GetWorld();
	int32 Rooms = 0, Hubs = 0;
	for (TActorIterator<ATargetPoint> It(World); It; ++It)
	{
		const bool bRoom = It->ActorHasTag("PortalAnchor");
		const bool bHub = It->ActorHasTag("HubPortal");
		if (!bRoom && !bHub) continue;
		const int32 Team = It->ActorHasTag(LaneTag(1)) ? 1 : It->ActorHasTag(LaneTag(0)) ? 0 : -1;
		if (Team < 0) continue;
		const FRotator Facing(0.f, It->GetActorRotation().Yaw, 0.f);
		FVector At = It->GetActorLocation() - Facing.Vector() * BackToWall * WWO::M;
		FVector Ground;
		if (WWOFX::GroundBelow(World, At + FVector(0, 0, 100.f), 400.f, Ground)) At = Ground;
		AWWOBreachPortal* Portal = World->SpawnActorDeferred<AWWOBreachPortal>(AWWOBreachPortal::StaticClass(), FTransform(Facing, At),
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Portal) continue;
		Portal->Team = (int8)Team;
		Portal->bHub = bHub;
		Portal->Room = bHub ? NAME_None : RoomOf(*It);
		Portal->FinishSpawning(FTransform(Facing, At));
		Portals.Add(Portal);
		(bHub ? Hubs : Rooms)++;
	}
	UE_LOG(LogWWO, Display, TEXT("FootRace: %d room portals, %d hub portals"), Rooms, Hubs);
}

void FWWOFootRaceRules::OnPlayerJoined(APlayerController* NewPlayer)
{
	FWWOTeamDeathmatch::BalanceTeam(Mode.GameState, NewPlayer);
}

FTransform FWWOFootRaceRules::SpawnSpot(const AWWOCharacter* Player) const
{
	const int32 Team = FMath::Max(0, UWWOWorldSubsystem::TeamOf(Player));
	TArray<const ATargetPoint*> Spots;
	for (TActorIterator<ATargetPoint> It(Mode.GetWorld()); It; ++It)
	{
		if (It->ActorHasTag("TeamSpawn") && It->ActorHasTag(LaneTag(Team))) Spots.Add(*It);
	}
	if (Spots.IsEmpty()) return Mode.FindPlayerStart(nullptr) ? Mode.FindPlayerStart(nullptr)->GetActorTransform() : FTransform::Identity;
	const ATargetPoint* Spot = Spots[FMath::Min(Spots.Num() - 1, (int32)(WWO::FRand() * Spots.Num()))];
	return FTransform(FRotator(0.f, Spot->GetActorRotation().Yaw, 0.f), Spot->GetActorLocation() + FVector(0, 0, 100.f));
}

AWWOBreachPortal* FWWOFootRaceRules::RoomPortal(int32 Team, FName Room) const
{
	for (const TWeakObjectPtr<AWWOBreachPortal>& P : Portals)
	{
		if (P.IsValid() && !P->bHub && P->Team == Team && P->Room == Room) return P.Get();
	}
	return nullptr;
}

AWWOBreachPortal* FWWOFootRaceRules::HubPortal(int32 Team) const
{
	for (const TWeakObjectPtr<AWWOBreachPortal>& P : Portals)
	{
		if (P.IsValid() && P->bHub && P->Team == Team) return P.Get();
	}
	return nullptr;
}

void FWWOFootRaceRules::Update(float DeltaSeconds)
{
	UWorld* World = Mode.GetWorld();
	const double Now = World->GetTimeSeconds();
	for (AWWOCharacter* Body : UWWOWorldSubsystem::Get(World)->GetPlayers())
	{
		if (!Body || !Body->Health) continue;
		AController* Controller = Body->GetController();
		AWWOPlayerState* PS = Controller ? Controller->GetPlayerState<AWWOPlayerState>() : nullptr;
		if (!PS) continue;
		if (PS->Team < 0 || PS->Team > 1) FWWOTeamDeathmatch::BalanceTeam(Mode.GameState, Controller);
		if (Body->Health->bDead) continue;
		// A body's first placing: its team's spawn room (the level's one Player Start is lane 0's).
		if (!Placed.Contains(Body))
		{
			Placed.Add(Body);
			Teleport(Body, SpawnSpot(Body));
			LastThrough.Add(Body, Now);
			continue;
		}
		if (const double* Last = LastThrough.Find(Body); Last && Now - *Last < PortalCooldown) continue;
		const FVector Feet = Body->GetActorLocation() - FVector(0, 0, Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		for (const TWeakObjectPtr<AWWOBreachPortal>& P : Portals)
		{
			if (P.IsValid() && P->Team == PS->Team && P->Contains(Feet))
			{
				GoThrough(Body, PS->Team, P.Get());
				LastThrough.Add(Body, Now);
				break;
			}
		}
	}
}

void FWWOFootRaceRules::GoThrough(AWWOCharacter* Body, int32 Team, AWWOBreachPortal* Portal)
{
	AWWOBreachPortal* To = nullptr;
	if (!Portal->bHub)
	{
		To = HubPortal(Team);
		CameFrom.Add(Body, Portal);
	}
	else
	{
		const TWeakObjectPtr<AWWOBreachPortal>* From = CameFrom.Find(Body);
		To = From && From->IsValid() ? From->Get() : RoomPortal(Team, "Room1");
	}
	if (!To) return;
	Teleport(Body, To->ArrivalSpot(Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
	UE_LOG(LogWWO, Display, TEXT("FootRace: %s (team %d) through the %s doorway to the %s doorway"), *Body->GetName(), Team,
		Portal->bHub ? TEXT("hub") : *Portal->Room.ToString(), To->bHub ? TEXT("hub") : *To->Room.ToString());
}

void FWWOFootRaceRules::Teleport(AWWOCharacter* Body, const FTransform& Spot)
{
	// As team deathmatch moves a player (FWWOTeamDeathmatch::SwitchTeam): the body, the yaw it
	// respawns facing, and the view, on the host and the owning machine.
	const FRotator Facing = Spot.Rotator();
	Body->TeleportTo(Spot.GetLocation(), Facing);
	Body->Health->SpawnYaw = Facing.Yaw;
	if (APlayerController* PC = Cast<APlayerController>(Body->GetController())) PC->ClientSetRotation(Facing);
	Body->SetViewRotation(0.f, Facing.Yaw);
}

FString FWWOFootRaceRules::StatusText(FLinearColor& OutColor) const
{
	OutColor = FLinearColor::White;
	return TEXT("FOOTRACE");
}
