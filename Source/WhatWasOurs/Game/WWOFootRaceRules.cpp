// FootRace (Game/WWOFootRaceRules.h).

#include "Game/WWOFootRaceRules.h"
#include "Game/WWOGameMode.h"
#include "Game/WWOGameState.h"
#include "Game/WWOPlayerState.h"
#include "Game/WWOTeamDeathmatch.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
#include "Enemies/WWOEnemy.h"
#include "Enemies/WWOEnemyKinds.h"
#include "Enemies/WWOEnemyTypes.h"
#include "Characters/WWOBodyComponent.h"
#include "WWOIds.h"
#include "Allies/WWOAlly.h"
#include "World/WWOBreachPortal.h"
#include "World/WWOBreachDoor.h"
#include "World/WWOBreachLever.h"
#include "World/WWOBossCore.h"
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
		GS->BreachNext = { 0.f, 0.f };
		GS->BreachFrozen = { false, false };
		GS->DoorsOpen = { 0, 0 };
		GS->BreachStart = { 0.f, 0.f };
		GS->BossHealth = { -1.f, -1.f };
		GS->BossWindow = { 0.f, 0.f };
		GS->CoreState = { 0, 0 };
		GS->BreachThreshold = Mode.BreachThreshold;
	}
	// Every enemy's model, loaded now with the map: the first Tank (Room3) or boss would otherwise stall
	// the game for most of a second while its model loads mid-match.
	// And the clips they play (Enemies/WWOEnemyTypes.cpp's Body->Play names): the Tank's walk and casts
	// are first needed in Room3. All kept referenced (Preloaded) so nothing unloads them before then.
	TArray<UObject*> Loaded;
	for (const FWWOEnemyKind& Kind : WWOEnemyKinds::All())
	{
		if (const AWWOEnemy* Default = Kind.Class ? Kind.Class->GetDefaultObject<AWWOEnemy>() : nullptr) UWWOBodyComponent::Preload(Default->Body->ModelKey, Loaded);
	}
	UWWOBodyComponent::Preload(AWWOBoss::StaticClass()->GetDefaultObject<AWWOBoss>()->Body->ModelKey, Loaded);
	static const FName EnemyClips[] = { "Death01", "Hit_Knockback", "Idle", "Idle_Unarmed", "Jump", "Jump_Land", "Punch_Jab",
		"Spell_Simple_Enter", "Spell_Simple_Shoot", "Sprint_Unarmed", "Walk_Unarmed" };
	UWWOBodyComponent::PreloadAnims(EnemyClips, Loaded);
	// And the effect materials (the Tank's shield bubble is the first M_WWO_SoftAlpha in the match: a quarter
	// of a second to load in Room3).
	for (const TCHAR* Name : { TEXT("M_WWO_SoftAlpha"), TEXT("M_WWO_SoftAdd"), TEXT("M_WWO_Unlit"), TEXT("M_WWO_Ground"), TEXT("M_WWO_Solid") })
	{
		if (UMaterialInterface* M = WWOFX::Material(Name)) Loaded.Add(M);
	}
	for (UObject* Object : Loaded) Preloaded.Emplace(Object);
	// Loaded isn't enough for the Tank: the first one set up still cost a quarter of a second (its model's
	// first set-up, done once a run). One of each enemy kind and the boss, set up now, out of sight under
	// the map and gone a moment later, takes that cost while the match loads.
	{
		TArray<UClass*> Warm;
		for (const FWWOEnemyKind& Kind : WWOEnemyKinds::All()) if (Kind.Class) Warm.Add(Kind.Class);
		Warm.Add(AWWOBoss::StaticClass());
		for (int32 I = 0; I < Warm.Num(); ++I)
		{
			const FTransform Where(FVector(I * 600.f, 0.f, -20000.f));
			if (AWWOEnemy* E = World->SpawnActorDeferred<AWWOEnemy>(Warm[I], Where, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
			{
				E->bRespawns = false;
				E->FinishSpawning(Where);
				E->SetActorTickEnabled(false);
				E->SetLifeSpan(0.5f);
			}
		}
	}
	SpawnPortals();
	SpawnDoorsAndLevers();
	FindRooms();
	State = EWWOWaveState::Match;
}

void FWWOFootRaceRules::SpawnPortals()
{
	UWorld* World = Mode.GetWorld();
	int32 RoomCount = 0, Hubs = 0;
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
		(bHub ? Hubs : RoomCount)++;
	}
	UE_LOG(LogWWO, Display, TEXT("FootRace: %d room portals, %d hub portals"), RoomCount, Hubs);
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
	EndBreachesDone();
	UpdateLevers(DeltaSeconds);
	UpdateRooms(DeltaSeconds);
	UpdateBosses(DeltaSeconds);
	UpdateCores(DeltaSeconds);
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
				if (!P->bHub && IsFrozen(PS->Team))
				{
					UE_LOG(LogWWO, Display, TEXT("FootRace: %s (team %d) can't breach yet: the team's last breach is still in the hub"), *Body->GetName(), PS->Team);
					break;
				}
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
	if (Breachers(Team) == 0)
	{
		// A breach begins: the HUD's race track measures its bar against the pool it began on.
		if (AWWOGameState* GS = Mode.GetGameState<AWWOGameState>())
		{
			if (GS->BreachStart.Num() < 2) GS->BreachStart.Init(0.f, 2);
			GS->BreachStart[Team] = FMath::Max(Energy(Team), 1.f);
		}
	}
	InHub.Add(Body);
	for (int32 I = 1; I <= 4; ++I) if (RoomName(I) == Portal->Room) EnteredFrom[Team].Add(I);
	Teleport(Body, To->ArrivalSpot(Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
	UE_LOG(LogWWO, Display, TEXT("FootRace: %s (team %d) through the %s doorway into the hub (%.0f energy)"), *Body->GetName(), Team,
		*Portal->Room.ToString(), Energy(Team));
	if (AWWOGameState* GS = Mode.GetGameState<AWWOGameState>())
	{
		const AWWOPlayerState* PS = Body->GetPlayerState<AWWOPlayerState>();
		GS->ReportEvent(PS ? PS->GetPlayerName() : Body->GetName(), Team, TEXT("breached"));
	}
}

void FWWOFootRaceRules::LeaveHub(AWWOCharacter* Body, int32 Team)
{
	InHub.Remove(Body);
	// Carrying a core out through the hub doorway delivers it.
	for (int32 CoreTeam = 0; CoreTeam < 2; ++CoreTeam)
	{
		if (Cores[CoreTeam].IsValid() && Cores[CoreTeam]->Carrier == Body)
		{
			RemoveCore(CoreTeam);
			DeliverCore(CoreTeam, Team);
		}
	}
	AWWOBreachPortal* To = RoomPortal(Team, RoomName(OpenedRoom(Team)));
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
	for (TActorIterator<AWWOEnemy> It(World); It; ++It) BindEnemy(*It); // any spawned by something else (tests)
}

void FWWOFootRaceRules::BindEnemy(AWWOEnemy* Enemy)
{
	if (!Enemy || EnemiesBound.Contains(Enemy)) return;
	EnemiesBound.Add(Enemy);
	EnemyBindings.Add({ Enemy, Enemy->OnDied.AddWeakLambda(&Mode, [this](AWWOEnemy* E) { OnEnemyDied(E); }) });
}

FName FWWOFootRaceRules::RoomName(int32 Index)
{
	static const FName Names[] = { "Spawn", "Room1", "Room2", "Room3", "Boss" };
	return Index >= 0 && Index < UE_ARRAY_COUNT(Names) ? Names[Index] : NAME_None;
}

FName FWWOFootRaceRules::RoomKind(int32 Index, int32 Spawned)
{
	using namespace WWOIds::Enemy;
	if (Index == 3 && Spawned == 0) return Tank; // Room3 opens with its Tank
	static const FName Room1[] = { Swarm, Swarm, Soldier };
	static const FName Room2[] = { Soldier, Swarm, Soldier };
	static const FName Room3[] = { Soldier, Soldier, Swarm };
	const FName* Pattern = Index == 1 ? Room1 : Index == 2 ? Room2 : Room3;
	return Pattern[Spawned % 3];
}

void FWWOFootRaceRules::FindRooms()
{
	TMap<TPair<int32, int32>, int32> Found; // (team, index) -> Rooms slot
	for (TActorIterator<ATargetPoint> It(Mode.GetWorld()); It; ++It)
	{
		if (!It->ActorHasTag("RoomArea")) continue;
		const int32 Team = It->ActorHasTag(LaneTag(1)) ? 1 : It->ActorHasTag(LaneTag(0)) ? 0 : -1;
		int32 Index = -1;
		for (int32 I = 0; I < 5; ++I) if (It->ActorHasTag(RoomName(I))) Index = I;
		if (Team < 0 || Index < 0) continue;
		FRoom Room;
		Room.Team = Team;
		Room.Index = Index;
		// The marker's scale is the room's size in m (Tools/footrace_layout.py).
		const FVector Half = It->GetActorScale3D() * 0.5f * WWO::M;
		Room.Area = FBox(It->GetActorLocation() - Half, It->GetActorLocation() + Half);
		Found.Add({ Team, Index }, Rooms.Add(Room));
	}
	for (TActorIterator<ATargetPoint> It(Mode.GetWorld()); It; ++It)
	{
		if (!It->ActorHasTag("EnemySpawn")) continue;
		const int32 Team = It->ActorHasTag(LaneTag(1)) ? 1 : It->ActorHasTag(LaneTag(0)) ? 0 : -1;
		for (int32 I = 1; I <= 4; ++I)
		{
			if (const int32* Slot = Found.Find({ Team, I }); Slot && It->ActorHasTag(RoomName(I))) Rooms[*Slot].Spawns.Add(It->GetActorLocation());
		}
	}
	UE_LOG(LogWWO, Display, TEXT("FootRace: %d lane rooms"), Rooms.Num());
}

int32 FWWOFootRaceRules::RoomAt(int32 Team, const FVector& Where) const
{
	for (const FRoom& Room : Rooms)
	{
		if (Room.Team == Team && Room.Area.IsInsideOrOn(Where)) return Room.Index;
	}
	return -1;
}

FVector FWWOFootRaceRules::RoomCenter(int32 Team, int32 Index) const
{
	for (const FRoom& Room : Rooms)
	{
		if (Room.Team == Team && Room.Index == Index) return FVector(Room.Area.GetCenter().X, Room.Area.GetCenter().Y, Room.Area.Min.Z);
	}
	return FVector::ZeroVector;
}

TArray<AWWOEnemy*> FWWOFootRaceRules::EnemiesIn(int32 Team, FName Name) const
{
	TArray<AWWOEnemy*> Out;
	for (const FRoom& Room : Rooms)
	{
		if (Room.Team != Team || RoomName(Room.Index) != Name) continue;
		for (const TWeakObjectPtr<AWWOEnemy>& E : Room.Alive) if (E.IsValid() && !E->IsDead()) Out.Add(E.Get());
	}
	return Out;
}

void FWWOFootRaceRules::UpdateRooms(float DeltaSeconds)
{
	// A team's furthest room: the furthest any of its living players stands in.
	for (AWWOCharacter* Body : UWWOWorldSubsystem::Get(&Mode)->GetPlayers())
	{
		const int32 Team = UWWOWorldSubsystem::TeamOf(Body);
		if (Team < 0 || Team > 1 || Body->Health->bDead) continue;
		const int32 Index = RoomAt(Team, Body->GetActorLocation());
		if (Index > Furthest[Team])
		{
			Furthest[Team] = Index;
			UE_LOG(LogWWO, Display, TEXT("FootRace: team %d reached %s"), Team, *RoomName(Index).ToString());
		}
	}
	for (FRoom& Room : Rooms)
	{
		Room.Alive.RemoveAll([](const TWeakObjectPtr<AWWOEnemy>& E) { return !E.IsValid() || E->IsDead(); });
		// Only the live room spawns. The boss room only while its team's core is out (a wave comes with the
		// core, SpawnBossWave, and refills until it is delivered), so the team can always earn the breach to
		// fetch it, and nothing piles up while the boss fight goes on.
		const bool bBoss = Room.Index == 4;
		if (Room.Index != Furthest[Room.Team] || Room.Spawns.IsEmpty() || (bBoss && !Cores[Room.Team].IsValid())) continue;
		const int32 Want = FMath::RoundToInt(bBoss ? Mode.BossWave : Mode.RoomEnemies);
		if (Room.Alive.Num() >= Want)
		{
			Room.bFilled = true;
			Room.SpawnLeft = Mode.RoomRefillTime; // a death is refilled RoomRefillTime after it, not at once
			continue;
		}
		Room.SpawnLeft -= DeltaSeconds;
		if (Room.SpawnLeft > 0.f) continue;
		SpawnInRoom(Room);
		Room.SpawnLeft = Room.bFilled ? Mode.RoomRefillTime : Mode.RoomSpawnStagger;
	}
}

void FWWOFootRaceRules::SpawnInRoom(FRoom& Room)
{
	const FVector& Marker = Room.Spawns[FMath::Min(Room.Spawns.Num() - 1, (int32)(WWO::FRand() * Room.Spawns.Num()))];
	FTransform Where;
	AWWOEnemy* Enemy = WWOEnemyKinds::SpawnDeferred(Mode.GetWorld(), RoomKind(Room.Index, Room.Spawned), Marker, Where);
	if (!Enemy) return;
	Enemy->bRespawns = false;
	Enemy->Lane = (int8)Room.Team;
	Enemy->FinishSpawning(Where);
	Enemy->bAlerted = true; // the room's enemies come at the team
	Enemy->OnDied.AddLambda([](AWWOEnemy* Dead) { Dead->SetLifeSpan(4.f); }); // the corpse goes, as the waves' do
	BindEnemy(Enemy); // now, so a kill this same tick still counts
	Room.Alive.Add(Enemy);
	++Room.Spawned;
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
	AWWOPlayerState* VictimPS = Victim->GetPlayerState<AWWOPlayerState>();
	if (VictimPS) ++VictimPS->Deaths;
	if (KillerTeam >= 0 && KillerTeam != VictimTeam)
	{
		CreditKill(KillerTeam, Mode.EnergyPerPlayerKill, Killer, TEXT("a player kill"));
		// The kill feed (the lane's AI kills stay out of it): killer > killee, as team deathmatch shows it.
		AWWOPlayerState* KillerPS = Killer->GetPlayerState<AWWOPlayerState>();
		const bool bPrecision = Victim->Health->bLastAttackCritical && Mode.GetWorld()->GetTimeSeconds() - Victim->Health->LastAttackTime <= 0.05;
		if (KillerPS) ++KillerPS->Kills;
		if (KillerPS && bPrecision) ++KillerPS->PrecisionKills;
		if (AWWOGameState* GS = Mode.GetGameState<AWWOGameState>(); GS && KillerPS)
		{
			GS->ReportKill(KillerPS, KillerPS->GetPlayerName(), KillerTeam, VictimPS ? VictimPS->GetPlayerName() : Victim->GetName(), VictimTeam,
				bPrecision ? AWWOGameState::KillPrecision : FName(), 1);
		}
	}
}

void FWWOFootRaceRules::OnEnemyDied(AWWOEnemy* Enemy)
{
	const AWWOCharacter* Killer = Enemy ? Enemy->LastHitBy.Get() : nullptr;
	if (Killer && !Killer->Health) Killer = nullptr;
	const int32 Team = Killer ? UWWOWorldSubsystem::TeamOf(Killer) : -1;
	if (Team >= 0) CreditKill(Team, Mode.EnergyPerEnemy, Killer, TEXT("an enemy kill"));
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
	if (State == EWWOWaveState::MatchOver)
	{
		OutColor = FLinearColor(0.6f, 1.f, 0.65f);
		return AWWOPlayerState::TeamName(Winner) + TEXT(" WINS THE RACE");
	}
	return TEXT("FOOTRACE");
}

// --- Doors, levers and the two pools ---------------------------------------------------------------

void FWWOFootRaceRules::SpawnDoorsAndLevers()
{
	UWorld* World = Mode.GetWorld();
	auto DoorNumber = [](const AActor* Marker)
	{
		for (int32 D = 1; D <= 3; ++D) if (Marker->ActorHasTag(FName(*FString::Printf(TEXT("Door%d"), D)))) return D;
		return 0;
	};
	for (TActorIterator<ATargetPoint> It(World); It; ++It)
	{
		const bool bDoor = It->ActorHasTag("DoorSlot");
		const bool bLever = It->ActorHasTag("LeverSlot");
		if (!bDoor && !bLever) continue;
		const int32 Team = It->ActorHasTag(LaneTag(1)) ? 1 : It->ActorHasTag(LaneTag(0)) ? 0 : -1;
		const int32 Door = DoorNumber(*It);
		if (Team < 0 || Door == 0) continue;
		const FRotator Facing(0.f, It->GetActorRotation().Yaw, 0.f);
		if (bDoor)
		{
			// The slab stands in the doorway, its middle at half the doorway's height (the marker is on the floor).
			const FTransform Where(Facing, It->GetActorLocation() + FVector(0, 0, AWWOBreachDoor::Height * 0.5f * WWO::M),
				FVector(AWWOBreachDoor::Thickness, AWWOBreachDoor::Width, AWWOBreachDoor::Height));
			AWWOBreachDoor* D = World->SpawnActorDeferred<AWWOBreachDoor>(AWWOBreachDoor::StaticClass(), Where, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!D) continue;
			D->Team = (int8)Team;
			D->Door = Door;
			D->FinishSpawning(Where);
			Doors.Add(D);
		}
		else
		{
			FVector At = It->GetActorLocation();
			FVector Ground;
			if (WWOFX::GroundBelow(World, At + FVector(0, 0, 100.f), 400.f, Ground)) At = Ground;
			AWWOBreachLever* L = World->SpawnActorDeferred<AWWOBreachLever>(AWWOBreachLever::StaticClass(), FTransform(Facing, At), nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!L) continue;
			L->Team = (int8)Team;
			L->Door = (int8)Door;
			L->FinishSpawning(FTransform(Facing, At));
			Levers.Add(L);
		}
	}
	UE_LOG(LogWWO, Display, TEXT("FootRace: %d doors, %d levers"), Doors.Num(), Levers.Num());
}

int32 FWWOFootRaceRules::DoorsOpen(int32 Team) const
{
	const AWWOGameState* GS = Mode.GetGameState<AWWOGameState>();
	return GS && GS->DoorsOpen.IsValidIndex(Team) ? GS->DoorsOpen[Team] : 0;
}

AWWOBreachDoor* FWWOFootRaceRules::DoorOf(int32 Team, int32 Door) const
{
	for (const TWeakObjectPtr<AWWOBreachDoor>& D : Doors) if (D.IsValid() && D->Team == Team && D->Door == Door) return D.Get();
	return nullptr;
}

AWWOBreachLever* FWWOFootRaceRules::LeverOf(int32 Team, int32 Door) const
{
	for (const TWeakObjectPtr<AWWOBreachLever>& L : Levers) if (L.IsValid() && L->Team == Team && L->Door == Door) return L.Get();
	return nullptr;
}

bool FWWOFootRaceRules::IsLeverLive(int32 Team, int32 Door) const
{
	if (Team < 0 || Team > 1) return false;
	return Door == DoorsOpen(Team) + 1 && Door <= 3 && !IsFrozen(Team) && Breachers(Team) > 0 && EnteredFrom[Team].Contains(Door);
}

void FWWOFootRaceRules::OpenDoor(int32 Team, int32 Door, const AWWOCharacter* By)
{
	AWWOGameState* GS = Mode.GetGameState<AWWOGameState>();
	if (!GS || Team < 0 || Team > 1 || Door != DoorsOpen(Team) + 1) return;
	if (GS->DoorsOpen.Num() < 2) GS->DoorsOpen.SetNum(2);
	GS->DoorsOpen[Team] = Door;
	if (AWWOBreachDoor* D = DoorOf(Team, Door)) D->Open();
	if (AWWOBreachLever* L = LeverOf(Team, Door))
	{
		L->bPulled = true;
		L->bLive = false;
		L->Progress = 0.f;
	}
	// The breach that opened it goes on draining the pool it is on; what the team earns now is for the next one.
	const bool bFreeze = Breachers(Team) > 0;
	if (bFreeze)
	{
		if (GS->BreachFrozen.Num() < 2) GS->BreachFrozen.SetNum(2);
		GS->BreachFrozen[Team] = true;
	}
	UE_LOG(LogWWO, Display, TEXT("FootRace: team %d opened Door %d%s"), Team, Door, bFreeze ? TEXT(" (its breach's pool is frozen)") : TEXT(""));
	const AWWOPlayerState* ByState = By ? By->GetPlayerState<AWWOPlayerState>() : nullptr;
	GS->ReportEvent(ByState ? ByState->GetPlayerName() : AWWOPlayerState::TeamName(Team), Team, FString::Printf(TEXT("opened Door %d"), Door));
	if (Door == 3) SpawnBoss(Team);
}

void FWWOFootRaceRules::UpdateLevers(float DeltaSeconds)
{
	for (const TWeakObjectPtr<AWWOBreachLever>& Weak : Levers)
	{
		AWWOBreachLever* L = Weak.Get();
		if (!L || L->bPulled) continue;
		L->bLive = IsLeverLive(L->Team, L->Door);
		bool bHeld = false;
		const AWWOCharacter* Puller = nullptr;
		if (L->bLive)
		{
			for (const TWeakObjectPtr<AWWOCharacter>& B : InHub)
			{
				if (B.IsValid() && !B->Health->bDead && B->bHoldingUse && UWWOWorldSubsystem::TeamOf(B.Get()) == L->Team && L->InReach(B->GetActorLocation()))
				{
					bHeld = true;
					Puller = B.Get();
					break;
				}
			}
		}
		// Let go (or out of reach) and the handle springs back.
		L->Progress = bHeld ? L->Progress + DeltaSeconds / FMath::Max(Mode.LeverHoldTime, 0.01f) : 0.f;
		if (L->Progress >= 1.f) OpenDoor(L->Team, L->Door, Puller);
	}
}

void FWWOFootRaceRules::EndBreachesDone()
{
	for (int32 Team = 0; Team < 2; ++Team)
	{
		if (Breachers(Team) > 0) continue;
		EnteredFrom[Team].Reset();
		if (AWWOGameState* GS = Mode.GetGameState<AWWOGameState>(); GS && GS->BreachStart.IsValidIndex(Team)) GS->BreachStart[Team] = 0.f;
		if (!IsFrozen(Team)) continue;
		// The breach that opened the door is over: the rest of its pool goes, the next pool takes its place.
		AWWOGameState* GS = Mode.GetGameState<AWWOGameState>();
		const float Left = Energy(Team);
		SetEnergy(Team, NextEnergy(Team));
		SetNext(Team, 0.f);
		GS->BreachFrozen[Team] = false;
		UE_LOG(LogWWO, Display, TEXT("FootRace: team %d's breach is over: %.0f left over thrown away, the next pool (%.0f) is the pool"), Team, Left, Energy(Team));
	}
}

float FWWOFootRaceRules::NextEnergy(int32 Team) const
{
	const AWWOGameState* GS = Mode.GetGameState<AWWOGameState>();
	return GS && GS->BreachNext.IsValidIndex(Team) ? GS->BreachNext[Team] : 0.f;
}

void FWWOFootRaceRules::SetNext(int32 Team, float Value)
{
	AWWOGameState* GS = Mode.GetGameState<AWWOGameState>();
	if (!GS || Team < 0 || Team > 1) return;
	if (GS->BreachNext.Num() < 2) GS->BreachNext.SetNum(2);
	GS->BreachNext[Team] = FMath::Max(0.f, Value);
}

bool FWWOFootRaceRules::IsFrozen(int32 Team) const
{
	const AWWOGameState* GS = Mode.GetGameState<AWWOGameState>();
	return GS && GS->BreachFrozen.IsValidIndex(Team) && GS->BreachFrozen[Team];
}

void FWWOFootRaceRules::CreditKill(int32 Team, float Amount, const AWWOCharacter* Killer, const TCHAR* Why)
{
	if (Team < 0 || Team > 1) return;
	if (!IsFrozen(Team))
	{
		AddEnergy(Team, Amount, Why);
		return;
	}
	// Frozen: a kill made in the hub stretches the breach a little and helps the next one; any other goes to the next.
	const float ToPool = Killer && IsBreaching(Killer) ? Amount * 0.5f : 0.f;
	if (ToPool > 0.f) SetEnergy(Team, Energy(Team) + ToPool);
	SetNext(Team, NextEnergy(Team) + Amount - ToPool);
	UE_LOG(LogWWO, Display, TEXT("FootRace: team %d %+.0f energy (%s; frozen: %.0f to the pool, %.0f to the next), now %.0f + %.0f next"),
		Team, Amount, Why, ToPool, Amount - ToPool, Energy(Team), NextEnergy(Team));
}

// --- The bosses ------------------------------------------------------------------------------------

float FWWOFootRaceRules::WindowLength(int32 Cycle) const
{
	return Mode.BossWindow * (1.f + (FMath::Max(Cycle, 1) - 1) / 3.f);
}

void FWWOFootRaceRules::SpawnBoss(int32 Team)
{
	if (Team < 0 || Team > 1 || Bosses[Team].IsValid()) return;
	UWorld* World = Mode.GetWorld();
	const ATargetPoint* Marker = nullptr;
	for (TActorIterator<ATargetPoint> It(World); It; ++It)
	{
		if (It->ActorHasTag("BossSpawn") && It->ActorHasTag(LaneTag(Team))) Marker = *It;
	}
	if (!Marker) return;
	const float Half = AWWOBoss::StaticClass()->GetDefaultObject<AWWOBoss>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FTransform Where(FRotator(0.f, Marker->GetActorRotation().Yaw, 0.f), Marker->GetActorLocation() + FVector(0, 0, Half + 5.f));
	AWWOBoss* Boss = World->SpawnActorDeferred<AWWOBoss>(AWWOBoss::StaticClass(), Where, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Boss) return;
	Boss->BossHealth = Mode.BossHealth;
	Boss->bRespawns = false;
	Boss->Lane = (int8)Team;
	Boss->FinishSpawning(Where);
	Bosses[Team] = Boss;
	Cycles[Team] = 0;
	WindowLeft[Team] = 0.f;
	CoreLeft[Team] = Mode.CoreDelay;
	BindEnemy(Boss);
	EnemyBindings.Add({ Boss, Boss->OnDied.AddWeakLambda(&Mode, [this, Team](AWWOEnemy*) { EndMatch(Team); }) });
	UE_LOG(LogWWO, Display, TEXT("FootRace: team %d's boss is up (%.0f health, shielded)"), Team, Mode.BossHealth);
}

void FWWOFootRaceRules::OpenBossWindow(int32 Team)
{
	AWWOBoss* Boss = BossOf(Team);
	if (!Boss || Boss->IsDead() || !Boss->bShielded || State != EWWOWaveState::Match) return;
	++Cycles[Team];
	WindowLeft[Team] = WindowLength(Cycles[Team]);
	Boss->SetShielded(false);
	UE_LOG(LogWWO, Display, TEXT("FootRace: team %d's boss shield is down: window %d, %.1f s"), Team, Cycles[Team], WindowLeft[Team]);
}

void FWWOFootRaceRules::UpdateBosses(float DeltaSeconds)
{
	AWWOGameState* GS = Mode.GetGameState<AWWOGameState>();
	for (int32 Team = 0; Team < 2; ++Team)
	{
		AWWOBoss* Boss = BossOf(Team);
		if (Boss && !Boss->IsDead() && WindowLeft[Team] > 0.f)
		{
			WindowLeft[Team] = FMath::Max(0.f, WindowLeft[Team] - DeltaSeconds);
			if (WindowLeft[Team] == 0.f)
			{
				Boss->SetShielded(true);
				UE_LOG(LogWWO, Display, TEXT("FootRace: team %d's boss shield is back up after window %d"), Team, Cycles[Team]);
			}
		}
		if (!GS) continue;
		if (GS->BossHealth.Num() < 2) GS->BossHealth.Init(-1.f, 2);
		if (GS->BossWindow.Num() < 2) GS->BossWindow.Init(0.f, 2);
		GS->BossHealth[Team] = Boss ? (Boss->IsDead() ? 0.f : Boss->GetHealthFraction()) : -1.f;
		GS->BossWindow[Team] = WindowLeft[Team];
	}
}

void FWWOFootRaceRules::EndMatch(int32 WinningTeam)
{
	if (State == EWWOWaveState::MatchOver) return;
	State = EWWOWaveState::MatchOver;
	Winner = WinningTeam;
	if (AWWOGameState* GS = Mode.GetGameState<AWWOGameState>())
	{
		GS->bMatchOver = true;
		GS->MatchWinner = (int8)WinningTeam;
	}
	if (Mode.bTestRun) Mode.RestartLeft = 6.f;
	RemoveCore(0);
	RemoveCore(1);
	UE_LOG(LogWWO, Display, TEXT("FootRace: team %d killed its boss and wins"), WinningTeam);
}

// --- Boss cores ------------------------------------------------------------------------------------

void FWWOFootRaceRules::UpdateCores(float DeltaSeconds)
{
	UWorld* World = Mode.GetWorld();
	AWWOGameState* GS = Mode.GetGameState<AWWOGameState>();
	for (int32 Team = 0; Team < 2; ++Team)
	{
		AWWOBoss* Boss = BossOf(Team);
		AWWOBossCore* Core = CoreOf(Team);
		if (!Boss || Boss->IsDead() || State != EWWOWaveState::Match)
		{
			RemoveCore(Team);
		}
		else if (!Core)
		{
			// The next core: CoreDelay s after the shield is up (counting starts when it is).
			if (Boss->bShielded && WindowLeft[Team] <= 0.f)
			{
				if (CoreLeft[Team] < 0.f) CoreLeft[Team] = Mode.CoreDelay;
				CoreLeft[Team] -= DeltaSeconds;
				if (CoreLeft[Team] <= 0.f)
				{
					CoreLeft[Team] = -1.f;
					SpawnCore(Team);
					SpawnBossWave(Team);
				}
			}
		}
		else if (Core->Carrier)
		{
			// A carrier who dies (or goes) drops it where they fell.
			AWWOCharacter* Carrier = Core->Carrier;
			if (!IsValid(Carrier) || Carrier->Health->bDead)
			{
				const FVector Where = IsValid(Carrier) ? Carrier->GetActorLocation() : Core->GetActorLocation();
				Core->Drop(Where, Mode.CoreDropTime);
				UE_LOG(LogWWO, Display, TEXT("FootRace: team %d's core dropped"), Team);
			}
		}
		else
		{
			if (Core->bDropped && (Core->DroppedLeft -= DeltaSeconds) <= 0.f)
			{
				Core->GoHome();
				UE_LOG(LogWWO, Display, TEXT("FootRace: team %d's dropped core went back to the hub"), Team);
			}
			// A fresh core is its team's to take; a dropped one is anyone's.
			for (AWWOCharacter* Body : UWWOWorldSubsystem::Get(World)->GetPlayers())
			{
				const int32 BodyTeam = UWWOWorldSubsystem::TeamOf(Body);
				if (!Body || Body->Health->bDead || BodyTeam < 0 || (!Core->bDropped && BodyTeam != Team)) continue;
				if (!Core->InReach(Body->GetActorLocation())) continue;
				Core->PickUp(Body);
				UE_LOG(LogWWO, Display, TEXT("FootRace: %s (team %d) picked up team %d's core"), *Body->GetName(), BodyTeam, Team);
				break;
			}
		}
		if (GS)
		{
			if (GS->CoreState.Num() < 2) GS->CoreState.Init(0, 2);
			Core = CoreOf(Team);
			GS->CoreState[Team] = !Core ? 0 : Core->Carrier ? 2 : Core->bDropped ? 3 : 1;
		}
	}
}

void FWWOFootRaceRules::SpawnCore(int32 Team)
{
	UWorld* World = Mode.GetWorld();
	const ATargetPoint* Marker = nullptr;
	for (TActorIterator<ATargetPoint> It(World); It; ++It) if (It->ActorHasTag("CoreSpawn")) Marker = *It;
	if (!Marker) return;
	// On the team's side of the marker: toward its hub doorway.
	FVector At = Marker->GetActorLocation();
	if (const AWWOBreachPortal* Doorway = HubPortal(Team)) At += (Doorway->GetActorLocation() - At).GetSafeNormal2D() * 4.f * WWO::M;
	FVector Ground = At;
	WWOFX::GroundBelow(World, At + FVector(0, 0, 100.f), 400.f, Ground);
	At = Ground + FVector(0, 0, 1.1f * WWO::M);
	AWWOBossCore* Core = World->SpawnActorDeferred<AWWOBossCore>(AWWOBossCore::StaticClass(), FTransform(At), nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Core) return;
	Core->Team = (int8)Team;
	Core->Home = At;
	Core->FinishSpawning(FTransform(At));
	Cores[Team] = Core;
	UE_LOG(LogWWO, Display, TEXT("FootRace: team %d's boss core is in the hub"), Team);
}

void FWWOFootRaceRules::SpawnBossWave(int32 Team)
{
	const int32 Count = FMath::RoundToInt(Mode.BossWave);
	for (FRoom& Room : Rooms)
	{
		if (Room.Team != Team || Room.Index != 4 || Room.Spawns.IsEmpty()) continue;
		for (int32 I = 0; I < Count; ++I) SpawnInRoom(Room);
		Room.bFilled = true; // from now on it refills at RoomRefillTime while the core is out
		Room.SpawnLeft = Mode.RoomRefillTime;
		UE_LOG(LogWWO, Display, TEXT("FootRace: a wave of %d in team %d's boss room"), Count, Team);
	}
}

void FWWOFootRaceRules::RemoveCore(int32 Team)
{
	if (AWWOBossCore* Core = CoreOf(Team)) Core->Destroy();
	Cores[Team] = nullptr;
}

void FWWOFootRaceRules::DeliverCore(int32 CoreTeam, int32 CarrierTeam)
{
	AWWOBoss* Boss = BossOf(CoreTeam);
	if (!Boss || Boss->IsDead()) return;
	if (CoreTeam == CarrierTeam)
	{
		UE_LOG(LogWWO, Display, TEXT("FootRace: team %d delivered its core"), CoreTeam);
		OpenBossWindow(CoreTeam);
		return;
	}
	const float Before = Boss->GetHealth();
	Boss->Heal(Mode.CoreHeal);
	UE_LOG(LogWWO, Display, TEXT("FootRace: team %d delivered team %d's core: its boss heals %.0f (%.0f -> %.0f)"), CarrierTeam, CoreTeam,
		Boss->GetHealth() - Before, Before, Boss->GetHealth());
}
