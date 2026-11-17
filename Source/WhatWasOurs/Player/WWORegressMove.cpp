// Regression scenarios: how the AI moves. Each mover (the Soldier, Swarm and Tank, the AI teammate, a bot)
// walking across open floor, toward a target with a wall between, toward a target up on a platform it has
// no way onto, at a ledge; turning in place; and each of its probes (the wall ahead, the ledge ahead, the
// side check) at its edges. Positions and yaw every 30 ticks, so a change to the path following, the
// probes, the turning or the number of random draws shows (rand_next: the next draw of each stream).
// The navmesh is built at run time (RuntimeGeneration=Dynamic): scenarios that walk wait until no build
// is in progress before the AI is switched on (logged, never recorded).

#include "Player/WWORegress.h"
#include "WhatWasOurs.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOBotController.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Enemies/WWOEnemy.h"
#include "Enemies/WWOEnemyTypes.h"
#include "Allies/WWOAlly.h"
#include "FX/WWOFX.h"
#include "Data/WWOData.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/GameModeBase.h"
#include "NavigationSystem.h"
#include "Engine/World.h"

namespace
{
	struct FMoveState
	{
		TWeakObjectPtr<AActor> Mover;
		TArray<TWeakObjectPtr<AActor>> Others;
		int32 Start = -1;      // the tick the AI was switched on (the navmesh at rest)
		int32 NavBusyTicks = 0; // ticks a navmesh build was in progress after that (logged)
		float PlayerBefore = 0.f;
		int32 BotPathsFound = 0, BotPathsFailed = 0, BotStuckJumps = 0, BotRegoals = 0;
		float Edge = 0.f; // the map's south edge (Y, cm), where a probe needs it
		float Half = 0.f; // a bot's capsule half height
	};

	bool NavBusy(UWorld* World)
	{
		UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		return Nav && Nav->IsNavigationBuildInProgress();
	}

	// A solid box (an effect actor: it never shapes the navmesh, and the teardown removes it), blocking
	// everything as level geometry does. Center and size in cm.
	AActor* SpawnBlock(FWWORegress& R, const FVector& Center, const FVector& SizeCm)
	{
		AWWOFXActor* FX = AWWOFXActor::Spawn(R.World(), Center, 1e6f);
		if (!FX) { R.Record(TEXT("spawn_failed"), TEXT("block")); return nullptr; }
		UStaticMeshComponent* Box = WWOFX::AddShape(FX, FX->GetRootComponent(), WWOFX::Cube(), TEXT("M_WWO_Solid"), FLinearColor(0.3f, 0.3f, 0.32f), 0.f);
		Box->SetRelativeScale3D(SizeCm / 100.f);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		return FX;
	}

	// A box standing on the floor at Feet (its top SizeCm.Z above).
	AActor* SpawnBlockOn(FWWORegress& R, const FVector& Feet, const FVector& SizeCm)
	{
		return SpawnBlock(R, Feet + FVector(0, 0, SizeCm.Z * 0.5f), SizeCm);
	}

	FString Deg(float D) { FString T = FString::Printf(TEXT("%.4f"), D); return T == TEXT("-0.0000") ? FString(TEXT("0.0000")) : T; }
	FString Dir2(const FVector& V) { return Deg(V.X) + TEXT(",") + Deg(V.Y); }

	void Track(FWWORegress& R, const FString& Key, const AActor* A)
	{
		if (!A) return;
		R.RecordVec(Key + TEXT(".at"), A->GetActorLocation());
		R.Record(Key + TEXT(".yaw"), Deg(A->GetActorRotation().Yaw));
		if (const AWWOCharacter* C = Cast<AWWOCharacter>(A); C && C->GetBot()) R.Record(Key + TEXT(".pitch"), Deg(FMath::RadiansToDegrees(C->GetPitch())));
	}

	UWWOEnemyData* DataOf(FWWORegress& R, FName Kind)
	{
		UWWOLoadoutSubsystem* L = R.Loadout();
		return Kind == "tank" ? (UWWOEnemyData*)L->TankData : (Kind == "soldier" ? (UWWOEnemyData*)L->SoldierData : (UWWOEnemyData*)L->SwarmData);
	}

	// The map's south edge along the home line: the last Y (cm, 1 cm steps) with floor under it.
	float FindEdge(FWWORegress& R)
	{
		const FVector Home = R.Home();
		FHitResult Hit;
		float Y = Home.Y;
		for (int32 i = 0; i < 3000; ++i)
		{
			const FVector At(Home.X, Y - 1.f, Home.Z + 50.f);
			if (!R.World()->LineTraceSingleByChannel(Hit, At, At - FVector(0, 0, 100.f), ECC_Visibility)) break;
			Y -= 1.f;
		}
		return Y;
	}

	// Feet on the floor at (X, Y) of home's level, as an actor location for a capsule of half height Half.
	FVector Stand(const FWWORegress& R, float X, float Y, float Half, float FeetZ = 0.f)
	{
		return FVector(X, Y, R.Home().Z + FeetZ + Half);
	}

	// A walk: the setup places everyone (AI off), Start switches the AI on once the navmesh is at rest,
	// then position and yaw every 30 ticks for the length; End records what is left.
	struct FWalk
	{
		FString Name;
		float Seconds = 6.f;
		bool bPvP = false;
		TFunction<void(FWWORegress&, FMoveState&)> Setup;
		TFunction<void(FWWORegress&, FMoveState&)> Start;
		TFunction<void(FWWORegress&, FMoveState&, int32)> Each; // every tick of the walk (the player's script)
		TFunction<void(FWWORegress&, FMoveState&)> End;
	};

	FWWORegressScenario MakeWalk(const FWalk& W)
	{
		FWWORegressScenario S;
		S.Name = W.Name;
		S.bPvP = W.bPvP;
		S.MaxTicks = FWWORegress::Ticks(W.Seconds) + 700;
		TSharedRef<FMoveState> St = MakeShared<FMoveState>();
		S.Setup = [St, W](FWWORegress& R)
		{
			*St = FMoveState();
			R.Tune(R.Player()->Health, "RechargeDelay", 100.f);
			W.Setup(R, *St);
		};
		S.Step = [St, W](FWWORegress& R, int32 Tick)
		{
			if (St->Start < 0)
			{
				if (NavBusy(R.World()) && Tick < 600) return false;
				St->Start = Tick;
				if (Tick > 0) UE_LOG(LogWWO, Display, TEXT("Regress %s: waited %d ticks for the navmesh build"), *W.Name, Tick);
				St->PlayerBefore = FWWORegress::HealthOf(R.Player());
				St->BotPathsFound = AWWOBotController::PathsFound;
				St->BotPathsFailed = AWWOBotController::PathsFailed;
				St->BotStuckJumps = AWWOBotController::StuckJumps;
				St->BotRegoals = AWWOBotController::Regoals;
				W.Start(R, *St);
			}
			const int32 T = Tick - St->Start;
			if (NavBusy(R.World())) ++St->NavBusyTicks;
			if (W.Each) W.Each(R, *St, T);
			if (T % 30 == 0) Track(R, FString::Printf(TEXT("t%03d"), T), St->Mover.Get());
			if (T < FWWORegress::Ticks(W.Seconds)) return false;
			R.Record(TEXT("player_taken"), St->PlayerBefore - FWWORegress::HealthOf(R.Player()));
			if (W.End) W.End(R, *St);
			R.Record(TEXT("rand_next"), WWO::FRand());
			if (St->NavBusyTicks > 0) UE_LOG(LogWWO, Display, TEXT("Regress %s: a navmesh build ran for %d ticks during the walk"), *W.Name, St->NavBusyTicks);
			return true;
		};
		return S;
	}

	AWWOBotController* BotOf(const FMoveState& St)
	{
		const AWWOCharacter* C = Cast<AWWOCharacter>(St.Mover.Get());
		return C ? Cast<AWWOBotController>(C->GetController()) : nullptr;
	}
}

void FWWORegress::AddMoveScenarios(TArray<FWWORegressScenario>& Out, const UWWOLoadoutSubsystem& L)
{
	// What a walk records at its end (lambdas here: they read the private state of the classes that name FWWORegress a friend).
	auto EnemyEnd = [](FWWORegress& R, FMoveState& St)
	{
		AWWOEnemy* E = Cast<AWWOEnemy>(St.Mover.Get());
		if (!E) return;
		R.Record(TEXT("state"), E->DebugState().Replace(TEXT(" "), TEXT("_")));
		R.Record(TEXT("path_points"), E->Path.Points.Num());
		R.RecordBool(TEXT("path_partial"), E->Path.bPartial);
		R.Record(TEXT("health"), E->GetHealth());
	};

	auto AllyEnd = [](FWWORegress& R, FMoveState& St)
	{
		AWWOAlly* A = Cast<AWWOAlly>(St.Mover.Get());
		if (!A) return;
		R.Record(TEXT("path_points"), A->Path.Num());
		for (int32 i = 0; i < St.Others.Num(); ++i) R.Record(FString::Printf(TEXT("other%d.health"), i), FWWORegress::HealthOf(St.Others[i].Get()));
	};

	auto BotStart = [](FWWORegress& R, FMoveState& St)
	{
		if (AWWOBotController* B = BotOf(St))
		{
			B->bParked = false;
			B->Clock = 0.f; // its clock ran while it was parked
		}
	};

	auto BotEnd = [](FWWORegress& R, FMoveState& St)
	{
		AWWOBotController* B = BotOf(St);
		if (!B) return;
		R.Record(TEXT("state"), B->DebugState().Replace(TEXT(" "), TEXT("_")));
		R.Record(TEXT("paths_found"), AWWOBotController::PathsFound - St.BotPathsFound);
		R.Record(TEXT("paths_failed"), AWWOBotController::PathsFailed - St.BotPathsFailed);
		R.Record(TEXT("stuck_jumps"), AWWOBotController::StuckJumps - St.BotStuckJumps);
		R.Record(TEXT("regoals"), AWWOBotController::Regoals - St.BotRegoals);
		B->bParked = true;
	};

	// The DoubleJumpPlatform of the range: 4 m tall, no way up (its top is a navmesh island).
	const FVector PlatformTop(1700.f, 1000.f, 400.f);
	const FVector BesidePlatform(2900.f, 1000.f, 0.f); // 12 m off its +X side, open floor

	// --- (a) Enemies walking ---
	for (const FName Kind : { FName("soldier"), FName("swarm"), FName("tank") })
	{
		const FString K = Kind.ToString();
		auto AiOn = [Kind](FWWORegress& R, FMoveState& St)
		{
			R.Tune(DataOf(R, Kind), "AiEnabled", 1.f);
			if (AWWOEnemy* E = Cast<AWWOEnemy>(St.Mover.Get())) E->bAlerted = true; // as if it had seen the player
		};
		const float OpenM = Kind == "soldier" ? 30.f : 20.f;
		// Open floor, the player at home; from 2 s to 4 s the player walks to its right (the goal moves).
		{
			FWalk W;
			W.Name = FString::Printf(TEXT("move.%s.open"), *K);
			W.Setup = [Kind, OpenM](FWWORegress& R, FMoveState& St) { St.Mover = R.SpawnEnemy(Kind, R.Ahead(OpenM)); };
			W.Start = AiOn;
			W.Each = [](FWWORegress& R, FMoveState& St, int32 T)
			{
				if (T == Ticks(2.f)) R.Move(FVector2D(1.f, 0.f));
				if (T == Ticks(4.f)) R.Move(FVector2D::ZeroVector);
			};
			W.End = EnemyEnd;
			Out.Add(MakeWalk(W));
		}
		// A wall (3 m tall, 4 m wide; not in the navmesh, so the path runs through it) between it and the player.
		{
			FWalk W;
			W.Name = FString::Printf(TEXT("move.%s.wall"), *K);
			const float AtM = Kind == "soldier" ? 30.f : 12.f;
			W.Setup = [Kind, AtM](FWWORegress& R, FMoveState& St)
			{
				St.Mover = R.SpawnEnemy(Kind, R.Ahead(AtM));
				St.Others.Add(SpawnBlockOn(R, R.Ahead(AtM - 3.f), FVector(400.f, 50.f, 300.f)));
			};
			W.Start = AiOn;
			W.End = EnemyEnd;
			Out.Add(MakeWalk(W));
		}
		// The player up on the platform: no way up (the path is partial), 12 m off its side.
		{
			FWalk W;
			W.Name = FString::Printf(TEXT("move.%s.platform"), *K);
			W.Setup = [Kind, PlatformTop, BesidePlatform](FWWORegress& R, FMoveState& St)
			{
				R.PlaceAt(PlatformTop, 0.f);
				St.Mover = R.SpawnEnemy(Kind, BesidePlatform);
			};
			W.Start = AiOn;
			W.End = EnemyEnd;
			Out.Add(MakeWalk(W));
		}
	}
	// The Soldier backing off toward the map's edge (the player inside its MinRange), then strafing along it.
	{
		FWalk W;
		W.Name = TEXT("move.soldier.ledge");
		W.Setup = [](FWWORegress& R, FMoveState& St) { St.Mover = R.SpawnEnemy("soldier", R.Ahead(-12.f)); };
		W.Start = [](FWWORegress& R, FMoveState& St) { R.Tune(R.Loadout()->SoldierData, "AiEnabled", 1.f); };
		W.End = EnemyEnd;
		Out.Add(MakeWalk(W));
	}

	// --- (b) The AI teammate walking (it follows the player) ---
	{
		auto AiOn = [](FWWORegress& R, FMoveState& St) { R.Tune(R.Loadout()->AllyData, "AiEnabled", 1.f); };
		// Open floor from 15 m, an inert Soldier off to the side it shoots at (its random fire cooldown and spread).
		{
			FWalk W;
			W.Name = TEXT("move.ally.open");
			W.Setup = [](FWWORegress& R, FMoveState& St)
			{
				St.Mover = R.SpawnAlly(R.Ahead(15.f));
				St.Others.Add(R.SpawnEnemy("soldier", R.Ahead(15.f, 12.f)));
			};
			W.Start = AiOn;
			W.Each = [](FWWORegress& R, FMoveState& St, int32 T)
			{
				if (T == Ticks(3.f)) R.Move(FVector2D(1.f, 0.f));
				if (T == Ticks(4.5f)) R.Move(FVector2D::ZeroVector);
			};
			W.End = AllyEnd;
			Out.Add(MakeWalk(W));
		}
		{
			FWalk W;
			W.Name = TEXT("move.ally.wall");
			W.Setup = [](FWWORegress& R, FMoveState& St)
			{
				St.Mover = R.SpawnAlly(R.Ahead(15.f));
				SpawnBlockOn(R, R.Ahead(12.f), FVector(400.f, 50.f, 300.f));
			};
			W.Start = AiOn;
			W.End = AllyEnd;
			Out.Add(MakeWalk(W));
		}
		{
			FWalk W;
			W.Name = TEXT("move.ally.platform");
			W.Setup = [PlatformTop, BesidePlatform](FWWORegress& R, FMoveState& St)
			{
				R.PlaceAt(PlatformTop, 0.f);
				St.Mover = R.SpawnAlly(BesidePlatform);
			};
			W.Start = AiOn;
			W.End = AllyEnd;
			Out.Add(MakeWalk(W));
		}
		// Too close: it backs off (nothing behind it), then a wall behind it holds it.
		{
			FWalk W;
			W.Name = TEXT("move.ally.back");
			W.Seconds = 3.f;
			W.Setup = [](FWWORegress& R, FMoveState& St) { St.Mover = R.SpawnAlly(R.Ahead(1.5f)); };
			W.Start = AiOn;
			W.End = AllyEnd;
			Out.Add(MakeWalk(W));
		}
		{
			FWalk W;
			W.Name = TEXT("move.ally.back_wall");
			W.Seconds = 3.f;
			W.Setup = [](FWWORegress& R, FMoveState& St)
			{
				St.Mover = R.SpawnAlly(R.Ahead(1.5f));
				SpawnBlockOn(R, R.Ahead(2.5f), FVector(400.f, 50.f, 300.f)); // its back 25 cm from the face
			};
			W.Start = AiOn;
			W.End = AllyEnd;
			Out.Add(MakeWalk(W));
		}
	}

	// --- (c) A bot walking (its whole brain on; it draws from FMath's generator, seeded per scenario) ---
	{
		// Nobody to escort and nobody to fight (the other team, PvE): it roams to the range's only spawn spot.
		{
			FWalk W;
			W.Name = TEXT("move.bot.roam");
			W.Seconds = 8.f;
			W.Setup = [](FWWORegress& R, FMoveState& St) { St.Mover = R.SpawnBot(1, R.Ahead(3.f, 3.f), R.HomeYaw()); };
			W.Start = BotStart;
			W.End = BotEnd;
			Out.Add(MakeWalk(W));
		}
		// The player's team, every bot an escort: it closes up on the player from 15 m; the player walks off at 3 s.
		{
			FWalk W;
			W.Name = TEXT("move.bot.follow");
			W.Setup = [](FWWORegress& R, FMoveState& St)
			{
				R.Tune(R.World()->GetAuthGameMode(), "BotStickToPlayer", 1.f);
				St.Mover = R.SpawnBot(0, R.Ahead(15.f));
			};
			W.Start = BotStart;
			W.Each = [](FWWORegress& R, FMoveState& St, int32 T)
			{
				if (T == Ticks(3.f)) R.Move(FVector2D(1.f, 0.f));
				if (T == Ticks(4.5f)) R.Move(FVector2D::ZeroVector);
			};
			W.End = BotEnd;
			Out.Add(MakeWalk(W));
		}
		// Roaming (as above) with a wall square across its way, 4 m ahead: 25 m south of the spawn spot.
		{
			FWalk W;
			W.Name = TEXT("move.bot.wall");
			W.Setup = [](FWWORegress& R, FMoveState& St)
			{
				St.Mover = R.SpawnBot(1, FVector(0.f, -2500.f, R.Home().Z), 90.f);
				SpawnBlockOn(R, FVector(0.f, -2100.f, R.Home().Z), FVector(400.f, 50.f, 300.f));
			};
			W.Start = BotStart;
			W.End = BotEnd;
			Out.Add(MakeWalk(W));
		}
		{
			FWalk W;
			W.Name = TEXT("move.bot.platform");
			W.Setup = [PlatformTop, BesidePlatform](FWWORegress& R, FMoveState& St)
			{
				R.PlaceAt(PlatformTop, 0.f);
				R.Tune(R.World()->GetAuthGameMode(), "BotStickToPlayer", 1.f);
				St.Mover = R.SpawnBot(0, BesidePlatform);
			};
			W.Start = BotStart;
			W.End = BotEnd;
			Out.Add(MakeWalk(W));
		}
		// A fight (PvP, the player in god mode on the other team, 15 m off along the map's edge): it strafes
		// (random), not off the edge 1.5 m to its side (its side check), jumps now and then, fires.
		{
			FWalk W;
			W.Name = TEXT("move.bot.fight");
			W.bPvP = true;
			W.Setup = [](FWWORegress& R, FMoveState& St)
			{
				const float Edge = FindEdge(R);
				R.PlaceAt(FVector(R.Home().X, Edge + 150.f, R.Home().Z), 0.f);
				R.Tune(R.Player()->Health, "GodMode", 1.f);
				St.Mover = R.SpawnBot(1, FVector(R.Home().X + 1500.f, Edge + 150.f, R.Home().Z));
			};
			W.Start = BotStart;
			W.End = [BotEnd](FWWORegress& R, FMoveState& St)
			{
				BotEnd(R, St);
				R.Record(TEXT("fmath_next"), FMath::FRand()); // its draws: the same number, in the same order
			};
			FWWORegressScenario S = MakeWalk(W);
			// Its strafes, jumps, bursts and reactions come from FMath's generator, which a drawn game also
			// draws on (effects, other threads): repeatable only without drawing.
			S.Modes = EWWORegressModes::NoRenderOnly;
			S.ModeReason = TEXT("the bot's choices draw on FMath's generator, which drawing also draws on");
			Out.Add(S);
		}
	}

	// --- (c2) Around the range's back wall (16 m wide, in the navmesh): a path with corners, so where a
	// path point counts as reached and when it paths again show. The mover 10 m behind the wall, the player
	// 10 m in front of it, both on its middle line.
	for (const FName Kind : { FName("soldier"), FName("swarm"), FName("tank"), FName("ally"), FName("bot") })
	{
		FWalk W;
		W.Name = FString::Printf(TEXT("move.%s.corner"), *Kind.ToString());
		W.Seconds = 8.f;
		W.Setup = [Kind](FWWORegress& R, FMoveState& St)
		{
			const FVector Behind(4500.f, 0.f, R.Home().Z), Front(2500.f, 0.f, R.Home().Z);
			R.PlaceAt(Front, 0.f);
			if (Kind == "ally") St.Mover = R.SpawnAlly(Behind);
			else if (Kind == "bot")
			{
				R.Tune(R.World()->GetAuthGameMode(), "BotStickToPlayer", 1.f);
				St.Mover = R.SpawnBot(0, Behind);
			}
			else St.Mover = R.SpawnEnemy(Kind, Behind);
		};
		W.Start = [Kind, BotStart](FWWORegress& R, FMoveState& St)
		{
			if (Kind == "ally") R.Tune(R.Loadout()->AllyData, "AiEnabled", 1.f);
			else if (Kind == "bot") BotStart(R, St);
			else
			{
				R.Tune(DataOf(R, Kind), "AiEnabled", 1.f);
				if (AWWOEnemy* E = Cast<AWWOEnemy>(St.Mover.Get())) E->bAlerted = true;
			}
		};
		// Every 10 ticks here: the turns at the corners are short.
		W.Each = [](FWWORegress& R, FMoveState& St, int32 T)
		{
			if (T % 10 == 0 && T % 30 != 0) Track(R, FString::Printf(TEXT("t%03d"), T), St.Mover.Get());
		};
		if (Kind == "ally") W.End = AllyEnd;
		else if (Kind == "bot") W.End = BotEnd;
		else W.End = EnemyEnd;
		Out.Add(MakeWalk(W));
	}

	// --- (d) Turning in place: each mover's turn called directly, tick by tick ---
	for (const FName Kind : { FName("soldier"), FName("swarm"), FName("tank"), FName("ally"), FName("bot") })
	{
		FWWORegressScenario S;
		S.Name = FString::Printf(TEXT("move.%s.turn"), *Kind.ToString());
		TSharedRef<FMoveState> St = MakeShared<FMoveState>();
		S.Setup = [St, Kind](FWWORegress& R)
		{
			*St = FMoveState();
			if (Kind == "ally") St->Mover = R.SpawnAlly(R.Ahead(10.f));
			else if (Kind == "bot") St->Mover = R.SpawnBot(0, R.Ahead(10.f));
			else St->Mover = R.SpawnEnemy(Kind, R.Ahead(10.f));
		};
		S.Step = [St, Kind](FWWORegress& R, int32 Tick)
		{
			AActor* M = St->Mover.Get();
			if (!M) return true;
			if (Tick == 0) R.Record(TEXT("yaw_start"), Deg(M->GetActorRotation().Yaw));
			// 0-39: toward 150 degrees left of where it faced at the start; 40-79: 100 degrees right of that
			// (an enemy at speed 3 here, the bot slower and pitched down); 80: a direction too short to turn by.
			const float Base = 270.f; // it faces the player (yaw 270) at the start
			FVector Dir = FRotator(0.f, Base + (Tick < 40 ? 150.f : -100.f), 0.f).Vector() * 100.f;
			if (Tick >= 80) Dir = FVector(0.4f, 0.3f, 0.f);
			if (AWWOEnemy* E = Cast<AWWOEnemy>(M)) E->TurnToward(Dir, Dt, Tick < 40 || Tick >= 80 ? -1.f : 3.f);
			else if (AWWOAlly* A = Cast<AWWOAlly>(M)) A->Face(Dir, Dt);
			else if (AWWOCharacter* C = Cast<AWWOCharacter>(M))
			{
				if (AWWOBotController* B = Cast<AWWOBotController>(C->GetController()))
				{
					const FVector Look = Tick < 40 ? Dir : Dir.GetSafeNormal() + FVector(0, 0, -0.3f);
					B->TurnView(C, Tick >= 80 ? FVector::ZeroVector : Look, Tick < 40 ? 260.f : 156.f, Dt, Tick < 40 ? FVector2D::ZeroVector : FVector2D(2.f, -1.f));
				}
			}
			if (Tick % 5 == 4) Track(R, FString::Printf(TEXT("t%02d"), Tick + 1), M);
			return Tick >= 84;
		};
		Out.Add(S);
	}

	// --- (e) The probes at their edges, called directly with the mover put in place ---
	// Enemies: the wall ahead (a capsule swept 30 cm, from 10 cm up, 15 cm shorter: a tall wall at a gap of
	// 5 / 27 / 33 / 60 cm in front of the capsule, straight and at 45 degrees; curbs 22 and 28 cm tall), the
	// ledge ahead (floor within 150 cm below its feet, a step of radius + 70 cm out: the map's edge 3 cm
	// either side of that step; a box 145 and 155 cm tall to step off).
	for (const FName Kind : { FName("soldier"), FName("swarm"), FName("tank") })
	{
		FWWORegressScenario S;
		S.Name = FString::Printf(TEXT("move.%s.probes"), *Kind.ToString());
		TSharedRef<FMoveState> St = MakeShared<FMoveState>();
		S.Setup = [St, Kind](FWWORegress& R)
		{
			*St = FMoveState();
			St->Mover = R.SpawnEnemy(Kind, R.Ahead(-3.f, 12.f));
			St->Edge = FindEdge(R);
			// Lanes along X, 5 m apart, faces at home's Y + 10 m: a tall wall, the two curbs, the two boxes.
			const FVector Home = R.Home();
			const float Face = Home.Y + 1000.f;
			St->Others.Add(SpawnBlockOn(R, FVector(Home.X + 500.f, Face + 25.f, Home.Z), FVector(300.f, 50.f, 300.f)));
			St->Others.Add(SpawnBlockOn(R, FVector(Home.X + 1000.f, Face + 25.f, Home.Z), FVector(300.f, 50.f, 22.f)));
			St->Others.Add(SpawnBlockOn(R, FVector(Home.X + 1500.f, Face + 25.f, Home.Z), FVector(300.f, 50.f, 28.f)));
			St->Others.Add(SpawnBlockOn(R, FVector(Home.X + 2000.f, Face + 150.f, Home.Z), FVector(300.f, 300.f, 145.f)));
			St->Others.Add(SpawnBlockOn(R, FVector(Home.X + 2500.f, Face + 150.f, Home.Z), FVector(300.f, 300.f, 155.f)));
		};
		S.Step = [St](FWWORegress& R, int32 Tick)
		{
			AWWOEnemy* E = Cast<AWWOEnemy>(St->Mover.Get());
			if (!E) return true;
			const UCapsuleComponent* C = E->GetCapsuleComponent();
			const float Rad = C->GetScaledCapsuleRadius(), Half = C->GetScaledCapsuleHalfHeight();
			const FVector Home = R.Home();
			const float Face = Home.Y + 1000.f;
			auto Put = [&](float X, float Y, float FeetZ) { E->SetActorLocation(Stand(R, X, Y, Half, FeetZ), false, nullptr, ETeleportType::TeleportPhysics); };
			const FVector North(0, 1, 0), Slant = FVector(1, 1, 0).GetSafeNormal();
			for (const float Gap : { 5.f, 27.f, 33.f, 60.f })
			{
				Put(Home.X + 500.f, Face - Rad - Gap, 0.f);
				FVector Normal = FVector::ZeroVector;
				const bool bWall = E->WallAhead(North, &Normal);
				R.Record(FString::Printf(TEXT("wall.%.0f"), Gap), FString::Printf(TEXT("%d %s"), bWall ? 1 : 0, *Dir2(Normal)));
				R.RecordBool(FString::Printf(TEXT("wall.%.0f.nonormal"), Gap), E->WallAhead(North));
				Normal = FVector::ZeroVector;
				const bool bSlant = E->WallAhead(Slant * 50.f, &Normal);
				R.Record(FString::Printf(TEXT("wall_slant.%.0f"), Gap), FString::Printf(TEXT("%d %s"), bSlant ? 1 : 0, *Dir2(Normal)));
			}
			for (int32 i = 0; i < 2; ++i)
			{
				Put(Home.X + 1000.f + i * 500.f, Face - Rad - 5.f, 0.f);
				R.RecordBool(FString::Printf(TEXT("curb%d.wall"), i == 0 ? 22 : 28), E->WallAhead(North));
			}
			// The map's edge, to the south: a step of radius + 70 cm.
			for (const float D : { -3.f, 3.f })
			{
				Put(Home.X, St->Edge + Rad + 70.f + D, 0.f);
				R.RecordBool(FString::Printf(TEXT("edge%+.0f.ledge"), D), E->LedgeAhead(-North));
				R.RecordBool(FString::Printf(TEXT("edge%+.0f.ledge_short"), D), E->LedgeAhead(-North * 0.01f));
			}
			// Off the side of a box 145 / 155 cm tall (standing 20 cm in from its -X side, stepping -X).
			for (int32 i = 0; i < 2; ++i)
			{
				const float BoxX = Home.X + 2000.f + i * 500.f;
				Put(BoxX - 150.f + 20.f, Face + 150.f, i == 0 ? 145.f : 155.f);
				R.RecordBool(FString::Printf(TEXT("box%d.ledge"), i == 0 ? 145 : 155), E->LedgeAhead(FVector(-1, 0, 0)));
				R.RecordBool(FString::Printf(TEXT("box%d.ledge_on_top"), i == 0 ? 145 : 155), E->LedgeAhead(FVector(1, 0, 0)));
			}
			R.Record(TEXT("edge_y"), Rel(St->Edge - Home.Y));
			return true;
		};
		Out.Add(S);
	}
	// The ally backing off (a capsule swept 30 cm behind it, the player 2 m in front): the wall behind at
	// 27 / 33 cm from its capsule; then between FollowMin and FollowMax it stands.
	{
		FWWORegressScenario S;
		S.Name = TEXT("move.ally.probes");
		TSharedRef<FMoveState> St = MakeShared<FMoveState>();
		S.Setup = [St](FWWORegress& R)
		{
			*St = FMoveState();
			St->Mover = R.SpawnAlly(R.Ahead(-3.f, 12.f));
			const FVector Home = R.Home();
			St->Others.Add(SpawnBlockOn(R, FVector(Home.X + 500.f, Home.Y + 1000.f + 25.f, Home.Z), FVector(300.f, 50.f, 300.f)));
		};
		S.Step = [St](FWWORegress& R, int32 Tick)
		{
			AWWOAlly* A = Cast<AWWOAlly>(St->Mover.Get());
			if (!A) return true;
			const UCapsuleComponent* C = A->GetCapsuleComponent();
			const float Rad = C->GetScaledCapsuleRadius(), Half = C->GetScaledCapsuleHalfHeight();
			const FVector Home = R.Home();
			const float Face = Home.Y + 1000.f;
			for (const float Gap : { 27.f, 33.f })
			{
				const float Y = Face - Rad - Gap;
				A->SetActorLocation(Stand(R, Home.X + 500.f, Y, Half), false, nullptr, ETeleportType::TeleportPhysics);
				R.PlaceAt(FVector(Home.X + 500.f, Y - 200.f, Home.Z), 90.f);
				R.Record(FString::Printf(TEXT("back.%.0f"), Gap), Dir2(A->Follow(Dt)));
			}
			A->SetActorLocation(Stand(R, Home.X + 500.f, Home.Y + 500.f, Half), false, nullptr, ETeleportType::TeleportPhysics);
			R.PlaceAt(FVector(Home.X + 500.f, Home.Y, Home.Z), 90.f);
			R.Record(TEXT("between"), Dir2(A->Follow(Dt)));
			R.Record(TEXT("path_points"), A->Path.Num());
			return true;
		};
		Out.Add(S);
	}
	// The bot's side check (a line 130 cm out for a wall; floor within 350 cm below a point 150 cm out):
	// a wall 127 / 133 cm from its centre; the map's edge 3 cm either side of 150 cm out; a box whose drop
	// is 5 cm inside and outside the reach below its feet.
	{
		FWWORegressScenario S;
		S.Name = TEXT("move.bot.probes");
		TSharedRef<FMoveState> St = MakeShared<FMoveState>();
		S.Setup = [St](FWWORegress& R)
		{
			*St = FMoveState();
			AWWOCharacter* Bot = R.SpawnBot(0, R.Ahead(-3.f, 12.f));
			St->Mover = Bot;
			St->Edge = FindEdge(R);
			St->Half = Bot ? Bot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
			const FVector Home = R.Home();
			St->Others.Add(SpawnBlockOn(R, FVector(Home.X + 500.f, Home.Y + 1000.f + 25.f, Home.Z), FVector(300.f, 50.f, 300.f)));
			const float Reach = 350.f - St->Half - 0.5f; // the line starts at its centre, 0.5 cm above a standing height
			St->Others.Add(SpawnBlockOn(R, FVector(Home.X + 1000.f, Home.Y + 1150.f, Home.Z), FVector(400.f, 300.f, Reach - 5.f)));
			St->Others.Add(SpawnBlockOn(R, FVector(Home.X + 2000.f, Home.Y + 1150.f, Home.Z), FVector(400.f, 300.f, Reach + 5.f)));
			R.Record(TEXT("reach_cm"), Deg(Reach));
		};
		S.Step = [St](FWWORegress& R, int32 Tick)
		{
			AWWOCharacter* Me = Cast<AWWOCharacter>(St->Mover.Get());
			AWWOBotController* B = BotOf(*St);
			if (!Me || !B) return true;
			const FVector Home = R.Home();
			auto Put = [&](float X, float Y, float FeetZ) { Me->SetActorLocation(Stand(R, X, Y, St->Half + 0.5f, FeetZ), false, nullptr, ETeleportType::TeleportPhysics); };
			const FVector North(0, 1, 0);
			for (const float D : { 127.f, 133.f })
			{
				Put(Home.X + 500.f, Home.Y + 1000.f - D, 0.f);
				R.RecordBool(FString::Printf(TEXT("wall%.0f.clear"), D), B->SideIsClear(Me, North));
			}
			for (const float D : { -3.f, 3.f })
			{
				Put(Home.X, St->Edge + 150.f + D, 0.f);
				R.RecordBool(FString::Printf(TEXT("edge%+.0f.clear"), D), B->SideIsClear(Me, -North));
			}
			for (int32 i = 0; i < 2; ++i)
			{
				const float BoxX = Home.X + 1000.f + i * 1000.f;
				const float Top = 350.f - St->Half - 0.5f + (i == 0 ? -5.f : 5.f); // the box's height
				Put(BoxX - 200.f + 20.f, Home.Y + 1150.f, Top);
				R.RecordBool(FString::Printf(TEXT("box%d.clear"), i), B->SideIsClear(Me, FVector(-1, 0, 0)));
			}
			return true;
		};
		Out.Add(S);
	}
}
