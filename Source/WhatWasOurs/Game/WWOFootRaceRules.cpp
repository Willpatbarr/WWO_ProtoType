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
	constexpr double KillCreditWindow = 10.0; // s: as team deathmatch, a hit longer ago than this is nobody's kill
}

FWWOFootRaceRules::~FWWOFootRaceRules()
{
	for (const TPair<TWeakObjectPtr<UWWOHealthComponent>, FDelegateHandle>& B : PlayerBindings)
	{
		if (UWWOHealthComponent* H = B.Key.Get()) H->OnDied.Remove(B.Value);
	}
	for (const TPair<TWeakObjectPtr<AWWOEnemy>, FDelegateHandle>& B : EnemyBindings)
	{
		if (AWWOEnemy* E = B.Key.Get()) E->OnDied.Remove(B.Value);
	}
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
		GS->BreachEnergy = { 0.f, 0.f };
		GS->BreachThreshold = Mode.BreachThreshold;
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
	if (AWWOGameState* GS = Mode.GetGameState<AWWOGameState>()) GS->BreachThreshold = Mode.BreachThreshold; // a Settings change shows at once
	BindDeaths();
	Drain(DeltaSeconds);
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
				LastThrough.Add(Body, Now); // a refused step waits the cooldown too (one log line, not one a tick)
				if (!P->bHub && Energy(PS->Team) < Mode.BreachThreshold)
				{
					UE_LOG(LogWWO, Display, TEXT("FootRace: %s (team %d) can't breach yet: %.0f of %.0f energy"), *Body->GetName(), PS->Team,
						Energy(PS->Team), Mode.BreachThreshold);
					break;
				}
				GoThrough(Body, PS->Team, P.Get());
				break;
			}
		}
	}
}

void FWWOFootRaceRules::GoThrough(AWWOCharacter* Body, int32 Team, AWWOBreachPortal* Portal)
{
	if (Portal->bHub)
	{
		LeaveHub(Body, Team);
		return;
	}
	AWWOBreachPortal* To = HubPortal(Team);
	if (!To) return;
	CameFrom.Add(Body, Portal);
	InHub.Add(Body);
	Teleport(Body, To->ArrivalSpot(Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
	UE_LOG(LogWWO, Display, TEXT("FootRace: %s (team %d) through the %s doorway into the hub (%.0f energy)"), *Body->GetName(), Team,
		*Portal->Room.ToString(), Energy(Team));
}

void FWWOFootRaceRules::LeaveHub(AWWOCharacter* Body, int32 Team)
{
	InHub.Remove(Body);
	const TWeakObjectPtr<AWWOBreachPortal>* From = CameFrom.Find(Body);
	AWWOBreachPortal* To = From && From->IsValid() ? From->Get() : RoomPortal(Team, "Room1");
	if (!To) return;
	Teleport(Body, To->ArrivalSpot(Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
	LastThrough.Add(Body, Mode.GetWorld()->GetTimeSeconds());
	UE_LOG(LogWWO, Display, TEXT("FootRace: %s (team %d) back from the hub to the %s doorway"), *Body->GetName(), Team, *To->Room.ToString());
}

float FWWOFootRaceRules::Energy(int32 Team) const
{
	const AWWOGameState* GS = Mode.GetGameState<AWWOGameState>();
	return GS && GS->BreachEnergy.IsValidIndex(Team) ? GS->BreachEnergy[Team] : 0.f;
}

void FWWOFootRaceRules::SetEnergy(int32 Team, float Value)
{
	AWWOGameState* GS = Mode.GetGameState<AWWOGameState>();
	if (!GS || Team < 0 || Team > 1) return;
	if (GS->BreachEnergy.Num() < 2) GS->BreachEnergy.SetNum(2);
	GS->BreachEnergy[Team] = FMath::Max(0.f, Value);
}

void FWWOFootRaceRules::AddEnergy(int32 Team, float Amount, const TCHAR* Why)
{
	if (Team < 0 || Team > 1 || Amount == 0.f) return;
	SetEnergy(Team, Energy(Team) + Amount);
	UE_LOG(LogWWO, Display, TEXT("FootRace: team %d %+.0f energy (%s), now %.0f"), Team, Amount, Why, Energy(Team));
}

int32 FWWOFootRaceRules::Breachers(int32 Team) const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AWWOCharacter>& B : InHub)
	{
		if (B.IsValid() && !B->Health->bDead && UWWOWorldSubsystem::TeamOf(B.Get()) == Team) ++Count;
	}
	return Count;
}

void FWWOFootRaceRules::Drain(float DeltaSeconds)
{
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const int32 Count = Breachers(Team);
		if (Count == 0) continue;
		SetEnergy(Team, Energy(Team) - Mode.BreachDrain * Count * DeltaSeconds);
		if (Energy(Team) > 0.f) continue;
		// Out of energy: every breacher of the team goes back to the room they came from.
		TArray<AWWOCharacter*> Out;
		for (const TWeakObjectPtr<AWWOCharacter>& B : InHub)
		{
			if (B.IsValid() && !B->Health->bDead && UWWOWorldSubsystem::TeamOf(B.Get()) == Team) Out.Add(B.Get());
		}
		UE_LOG(LogWWO, Display, TEXT("FootRace: team %d is out of breach energy: %d back to their rooms"), Team, Out.Num());
		for (AWWOCharacter* Body : Out) LeaveHub(Body, Team);
	}
}

void FWWOFootRaceRules::BindDeaths()
{
	UWorld* World = Mode.GetWorld();
	for (AWWOCharacter* P : UWWOWorldSubsystem::Get(World)->GetPlayers())
	{
		if (!P || !P->Health || PlayersBound.Contains(P)) continue;
		PlayersBound.Add(P);
		TWeakObjectPtr<AWWOCharacter> Weak = P;
		PlayerBindings.Add({ P->Health, P->Health->OnDied.AddWeakLambda(&Mode, [this, Weak] { if (Weak.IsValid()) OnPlayerDied(Weak.Get()); }) });
	}
	for (TActorIterator<AWWOEnemy> It(World); It; ++It)
	{
		if (EnemiesBound.Contains(*It)) continue;
		EnemiesBound.Add(*It);
		EnemyBindings.Add({ *It, It->OnDied.AddWeakLambda(&Mode, [this](AWWOEnemy* E) { OnEnemyDied(E); }) });
	}
}

void FWWOFootRaceRules::OnPlayerDied(AWWOCharacter* Victim)
{
	const int32 VictimTeam = UWWOWorldSubsystem::TeamOf(Victim);
	if (InHub.Remove(Victim) > 0)
	{
		const float Lost = Energy(VictimTeam) * Mode.BreachDeathLoss;
		AddEnergy(VictimTeam, -Lost, TEXT("a breacher died"));
	}
	// The kill, credited as team deathmatch credits it: whoever hit them last, lately, on the other team.
	const bool bRecent = Mode.GetWorld()->GetTimeSeconds() - Victim->Health->LastAttackTime <= KillCreditWindow;
	AWWOCharacter* Killer = bRecent ? Cast<AWWOCharacter>(Victim->Health->LastAttacker.Get()) : nullptr;
	const int32 KillerTeam = Killer ? UWWOWorldSubsystem::TeamOf(Killer) : -1;
	if (KillerTeam >= 0 && KillerTeam != VictimTeam) AddEnergy(KillerTeam, Mode.EnergyPerPlayerKill, TEXT("a player kill"));
}

void FWWOFootRaceRules::OnEnemyDied(AWWOEnemy* Enemy)
{
	const AWWOCharacter* Killer = Enemy ? Enemy->LastHitBy.Get() : nullptr;
	const int32 Team = Killer ? UWWOWorldSubsystem::TeamOf(Killer) : -1;
	if (Team >= 0) AddEnergy(Team, Mode.EnergyPerEnemy, TEXT("an enemy kill"));
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
