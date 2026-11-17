// Regression scenarios: match flow, the rules the game mode runs. The waves (Begin, the countdown, a wave
// spawned and cleared, the break, the next wave starting; won; everyone down; jumping and skipping), team
// deathmatch (Begin, kill credit and score, the kill feed, the kill limit, the time limit, friendly fire,
// respawn timer and spot, switching sides), the firing tester (what it clears and the dummy it puts down),
// the bot roster (filling to N with the teams and kits each gets, a bot making room, a leaver's place) and
// every status line. The level (L_TestRange) runs the waves, idle (no enemy spawn points): a scenario
// switches the rules it needs on and puts the game mode back at its end (M::Enter / M::Leave). Events are
// driven directly (a hit, a kill, a call); where the flow crosses ticks (waves, the limits, a respawn) the
// game mode's own Tick runs it, with the AI off and bots parked.

#include "Player/WWORegress.h"
#include "WhatWasOurs.h"
#include "Game/WWOGameMode.h"
#include "Game/WWOGameState.h"
#include "Game/WWOPlayerState.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOBotController.h"
#include "Player/WWOPlayerController.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "Enemies/WWOEnemy.h"
#include "Allies/WWOAlly.h"
#include "World/WWOTarget.h"
#include "Data/WWOData.h"
#include "Engine/TargetPoint.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	bool NavBusy(UWorld* World)
	{
		UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		return Nav && Nav->IsNavigationBuildInProgress();
	}

	// Spawn points a scenario put down (engine actors: the teardown only removes this game's own).
	TArray<TWeakObjectPtr<ATargetPoint>> GPoints;

	void AddPoint(FWWORegress& R, const FVector& At, const TArray<FName>& Tags = {})
	{
		ATargetPoint* Point = R.World()->SpawnActor<ATargetPoint>(ATargetPoint::StaticClass(), At, FRotator::ZeroRotator, WWO::AlwaysSpawn());
		if (!Point) { R.Record(TEXT("spawn_failed"), TEXT("point")); return; }
		Point->Tags.Add("EnemySpawn");
		Point->Tags.Append(Tags);
		GPoints.Add(Point);
	}

	void RemovePoints()
	{
		for (const TWeakObjectPtr<ATargetPoint>& P : GPoints) if (P.IsValid()) P->Destroy();
		GPoints.Reset();
	}

	// Four PvP spawn spots in the test space (enemy spawn points count for PvP unless tagged PvEOnly).
	void AddPvPPoints(FWWORegress& R)
	{
		AddPoint(R, R.Ahead(15.f, -10.f));
		AddPoint(R, R.Ahead(15.f, 10.f));
		AddPoint(R, R.Ahead(30.f, -6.f));
		AddPoint(R, R.Ahead(30.f, 6.f), { FName("PvEOnly") }); // the waves only: never a PvP spot
	}

	FString Deg(float D) { FString T = FString::Printf(TEXT("%.4f"), D); return T == TEXT("-0.0000") ? FString(TEXT("0.0000")) : T; }

	FString Joined(const TArray<FName>& Names)
	{
		return Names.Num() == 0 ? FString(TEXT("-")) : FString::JoinBy(Names, TEXT(","), [](const FName& N) { return N.ToString(); });
	}

	FString ColorText(const FLinearColor& C) { return FString::Printf(TEXT("%.9g,%.9g,%.9g,%.9g"), C.R, C.G, C.B, C.A); }

	bool Alive(const AActor* A) { return IsValid(A) && !A->IsActorBeingDestroyed(); }

	enum class ERules : uint8 { Waves, Deathmatch, Tester };

	// What a scenario keeps between its ticks.
	struct FMatchRun
	{
		bool bEntered = false;
		int32 Start = -1;  // the tick the rules were switched on
		int32 Events = 0;  // e00, e01, ... recorded so far
		int32 Mark = -1;   // a scenario's own tick marker
		int32 LastState = -1, LastWave = -1, LastQueue = -1, LastAlive = -1;
		bool bKilled = false;
		TArray<TWeakObjectPtr<AActor>> Others;
		// The game mode as it was (put back by M::Leave).
		bool bPvP = false, bTester = false, bTesterHomeSet = false;
		EWWOWaveState State = EWWOWaveState::Idle;
		int32 Wave = 0, Winner = -1;
		FWWOBotRoster Roster;
		float Timer = 0.f, RestartLeft = -1.f, MatchLeft = 0.f, MatchElapsed = 0.f;
		TArray<FName> Queue;
		TArray<TWeakObjectPtr<AWWOEnemy>> AliveList;
		TSet<TWeakObjectPtr<AWWOCharacter>> PvPBound;
		TSet<TWeakObjectPtr<AWWOPlayerState>> TeamGiven;
		FTransform TesterHome;
		TWeakObjectPtr<AWWOTarget> TesterTarget;
		bool bGSPvP = false, bMatchOver = false;
		TArray<int32> TeamScores;
		int8 MatchWinner = -1;
		int32 Feed = 0, PvPKills = 0, PvPMatchesEnded = 0;
	};

	void RecordGS(FWWORegress& R, const FString& Prefix)
	{
		const AWWOGameState* GS = R.World()->GetGameState<AWWOGameState>();
		if (!GS) { R.Record(Prefix + TEXT("gs"), TEXT("none")); return; }
		R.Record(Prefix + TEXT("gs.status"), GS->StatusLine);
		R.Record(Prefix + TEXT("gs.status_color"), ColorText(GS->StatusColor));
		R.RecordBool(Prefix + TEXT("gs.pvp"), GS->bPvP);
		R.Record(Prefix + TEXT("gs.scores"), FString::JoinBy(GS->TeamScores, TEXT(","), [](int32 V) { return FString::FromInt(V); }));
		R.RecordBool(Prefix + TEXT("gs.match_over"), GS->bMatchOver);
		R.Record(Prefix + TEXT("gs.match_winner"), (int32)GS->MatchWinner);
	}

	void RecordPS(FWWORegress& R, const FString& Key, const AActor* Who)
	{
		const APawn* Pawn = Cast<APawn>(Who);
		const AWWOPlayerState* PS = Pawn ? Pawn->GetPlayerState<AWWOPlayerState>() : nullptr;
		if (!PS) { R.Record(Key, TEXT("none")); return; }
		R.Record(Key, FString::Printf(TEXT("team %d kills %d precision %d deaths %d"), PS->Team, PS->Kills, PS->PrecisionKills, PS->Deaths));
	}

	// Names as recorded: the player's own (the computer's name, or a gamer tag) is "player".
	FString NameOf(FWWORegress& R, const FString& Name)
	{
		const AWWOPlayerState* Mine = R.Player() ? R.Player()->GetPlayerState<AWWOPlayerState>() : nullptr;
		return Mine && Name == Mine->GetPlayerName() ? FString(TEXT("player")) : Name;
	}

	void RecordFeed(FWWORegress& R, const FString& Key, int32 From)
	{
		const AWWOGameState* GS = R.World()->GetGameState<AWWOGameState>();
		if (!GS) return;
		R.Record(Key + TEXT(".count"), GS->KillFeed.Num() - From);
		for (int32 i = From; i < GS->KillFeed.Num(); ++i)
		{
			const AWWOGameState::FKillLine& L = GS->KillFeed[i];
			R.Record(FString::Printf(TEXT("%s.%d"), *Key, i - From), FString::Printf(TEXT("%s(%d) > %s(%d) how %s points %d mine %d"),
				*NameOf(R, L.Killer), L.KillerTeam, *NameOf(R, L.Victim), L.VictimTeam, *L.How.ToString(), L.Points, L.bMine ? 1 : 0));
		}
	}

	FString TeamsOf(FWWORegress& R)
	{
		TArray<FString> Parts;
		for (const AWWOCharacter* C : UWWOWorldSubsystem::Get(R.World())->GetPlayers())
		{
			const AWWOPlayerState* PS = C->GetPlayerState<AWWOPlayerState>();
			Parts.Add(FString::Printf(TEXT("%s:%d"), PS ? *NameOf(R, PS->GetPlayerName()) : TEXT("?"), PS ? PS->Team : -9));
		}
		return FString::Join(Parts, TEXT(","));
	}

	void SetTeamOf(AActor* Who, int32 Team)
	{
		const APawn* Pawn = Cast<APawn>(Who);
		if (AWWOPlayerState* PS = Pawn ? Pawn->GetPlayerState<AWWOPlayerState>() : nullptr) PS->Team = Team;
	}

	// A player-made killing hit (the attacker scope gives the kill credit).
	void KillBy(AWWOCharacter* Attacker, AWWOCharacter* Victim, bool bCritical)
	{
		if (!Victim) return;
		FWWODamageBy By(Attacker);
		Victim->TakeHit(99999.f, bCritical, Victim->GetActorLocation());
	}
}

void FWWORegress::AddMatchScenarios(TArray<FWWORegressScenario>& Out, const UWWOLoadoutSubsystem& L)
{
	// Everything that reaches into the game mode's rules: one place, so the scenarios read the same
	// whichever object holds them. (A local class of this member function shares FWWORegress's access.)
	struct M
	{
		static AWWOGameMode* GM(FWWORegress& R) { return R.World()->GetAuthGameMode<AWWOGameMode>(); }

		static void Snap(AWWOGameMode* G, FMatchRun& S)
		{
			S.bPvP = G->bPvP;
			S.bTester = G->bTester;
			S.bTesterHomeSet = G->bTesterHomeSet;
			S.TesterHome = G->TesterHome;
			S.TesterTarget = G->TesterTarget;
			S.State = G->State;
			S.Wave = G->Wave;
			S.Timer = G->Timer;
			S.RestartLeft = G->RestartLeft;
			S.Queue = G->Queue;
			S.AliveList = G->Alive;
			S.MatchLeft = G->MatchLeft;
			S.MatchElapsed = G->MatchElapsed;
			S.Winner = G->Winner;
			S.PvPBound = G->PvPBound;
			S.TeamGiven = G->TeamGiven;
			S.Roster = G->Roster;
			S.PvPKills = AWWOGameMode::PvPKills;
			S.PvPMatchesEnded = AWWOGameMode::PvPMatchesEnded;
			if (const AWWOGameState* GS = G->GetGameState<AWWOGameState>())
			{
				S.bGSPvP = GS->bPvP;
				S.TeamScores = GS->TeamScores;
				S.bMatchOver = GS->bMatchOver;
				S.MatchWinner = GS->MatchWinner;
				S.Feed = GS->KillFeed.Num();
			}
		}

		// Switches the rules on (the game mode's Begin under them), the player already placed on a team.
		static void Enter(FWWORegress& R, FMatchRun& S, ERules Rules)
		{
			AWWOGameMode* G = GM(R);
			Snap(G, S);
			S.bEntered = true;
			G->bPvP = Rules == ERules::Deathmatch;
			G->bTester = Rules == ERules::Tester;
			if (Rules == ERules::Deathmatch)
			{
				// The player is never bound or rebalanced by the match (its death binding would outlive the scenario).
				AWWOCharacter* P = R.Player();
				G->PvPBound.Add(P);
				G->TeamGiven.Add(P->GetPlayerState<AWWOPlayerState>());
				G->Roster.OptionBots = 0; // the roster adds no bots of its own unless a scenario asks
			}
			G->Begin();
		}

		static void Leave(FWWORegress& R, FMatchRun& S)
		{
			if (!S.bEntered) return;
			S.bEntered = false;
			AWWOGameMode* G = GM(R);
			G->bPvP = S.bPvP;
			G->bTester = S.bTester;
			G->bTesterHomeSet = S.bTesterHomeSet;
			G->TesterHome = S.TesterHome;
			G->TesterTarget = S.TesterTarget;
			G->State = S.State;
			G->Wave = S.Wave;
			G->Timer = S.Timer;
			G->RestartLeft = S.RestartLeft;
			G->Queue = S.Queue;
			G->Alive = S.AliveList;
			G->MatchLeft = S.MatchLeft;
			G->MatchElapsed = S.MatchElapsed;
			G->Winner = S.Winner;
			G->PvPBound = S.PvPBound;
			G->TeamGiven = S.TeamGiven;
			G->Roster = S.Roster;
			AWWOGameMode::PvPKills = S.PvPKills;
			AWWOGameMode::PvPMatchesEnded = S.PvPMatchesEnded;
			if (AWWOGameState* GS = G->GetGameState<AWWOGameState>())
			{
				GS->bPvP = S.bGSPvP;
				GS->TeamScores = S.TeamScores;
				GS->bMatchOver = S.bMatchOver;
				GS->MatchWinner = S.MatchWinner;
				GS->KillFeed.SetNum(FMath::Min(GS->KillFeed.Num(), S.Feed));
			}
		}

		// The end of a scenario: the rules put back, the scenario's spawn points gone.
		static void Done(FWWORegress& R, FMatchRun& S)
		{
			Leave(R, S);
			RemovePoints();
		}

		// --- Reading the rules ---
		static int32 State(AWWOGameMode* G) { return (int32)G->State; }
		static int32 Wave(AWWOGameMode* G) { return G->Wave; }
		static float Timer(AWWOGameMode* G) { return G->Timer; }
		static float RestartLeft(AWWOGameMode* G) { return G->RestartLeft; }
		static void NoRestart(AWWOGameMode* G) { G->RestartLeft = -1.f; } // a lost round would reload the level
		static const TArray<FName>& Queue(AWWOGameMode* G) { return G->Queue; }
		static TArray<AWWOEnemy*> AliveEnemies(AWWOGameMode* G)
		{
			TArray<AWWOEnemy*> Out;
			for (const TWeakObjectPtr<AWWOEnemy>& E : G->Alive) Out.Add(E.Get());
			return Out;
		}
		static float MatchLeft(AWWOGameMode* G) { return G->MatchLeft; }
		static int32 Winner(AWWOGameMode* G) { return G->Winner; }
		static void PvPDeath(AWWOGameMode* G, AWWOCharacter* Victim) { G->OnPvPDeath(Victim); }
		static FString Status(AWWOGameMode* G, FLinearColor& Color) { return G->StatusText(Color); }
		// The status line for a state, with the fields a line reads set for it (put back by Leave).
		static FString StatusFor(AWWOGameMode* G, EWWOWaveState St, int32 Wave, float Timer, int32 Left, float MatchLeft, int32 Winner, bool bTestRun, FLinearColor& Color)
		{
			G->State = St;
			G->Wave = Wave;
			G->Timer = Timer;
			G->Queue.Reset();
			G->Alive.Reset();
			for (int32 i = 0; i < Left; ++i) G->Queue.Add("swarm");
			G->MatchLeft = MatchLeft;
			G->Winner = Winner;
			const bool bWasTest = G->bTestRun;
			G->bTestRun = bTestRun;
			const FString Line = G->StatusText(Color);
			G->bTestRun = bWasTest;
			return Line;
		}
		static void SkipWave(AWWOGameMode* G) { G->SkipWave(); }
		static void JumpToWave(AWWOGameMode* G, int32 N) { G->JumpToWave(N); }
		// --- The roster ---
		static void SetOptionBots(AWWOGameMode* G, int32 N) { G->Roster.OptionBots = N; }
		static int32 BotsSpawned(AWWOGameMode* G) { return G->Roster.BotsSpawned; }
		static int32 BotsExtra(AWWOGameMode* G) { return G->Roster.BotsExtra; }
		static void PersonLeft(AWWOGameMode* G) { G->Roster.PersonLeft(); } // what Logout does for a person leaving mid-match
		static void UpdateBots(AWWOGameMode* G) { G->Roster.Update(*G); } // as the game mode's PvP tick runs it (no stress harness here)
		static void RetireBotFor(AWWOGameMode* G, AController* Joiner) { G->Roster.RetireFor(*G, Joiner); }
		// --- The tester ---
		static void UpdateTester(AWWOGameMode* G) { G->UpdateTester(); }
		static AWWOTarget* TesterTarget(AWWOGameMode* G) { return G->TesterTarget.Get(); }
		static FTransform TesterHome(AWWOGameMode* G) { return G->TesterHome; }
		static bool TesterHomeSet(AWWOGameMode* G) { return G->bTesterHomeSet; }
	};

	// One line per change of the waves' state, wave, queue or living count.
	auto WaveEvent = [](FWWORegress& R, FMatchRun& S, int32 T, bool bForce)
	{
		AWWOGameMode* G = M::GM(R);
		const int32 St = M::State(G), Wv = M::Wave(G), Q = M::Queue(G).Num(), A = M::AliveEnemies(G).Num();
		if (!bForce && St == S.LastState && Wv == S.LastWave && Q == S.LastQueue && A == S.LastAlive) return;
		S.LastState = St; S.LastWave = Wv; S.LastQueue = Q; S.LastAlive = A;
		const AWWOGameState* GS = R.World()->GetGameState<AWWOGameState>();
		R.Record(FString::Printf(TEXT("e%02d"), S.Events++), FString::Printf(TEXT("t%d state %d wave %d queue %d alive %d timer %.9g restart %.9g | %s"),
			T, St, Wv, Q, A, M::Timer(G), M::RestartLeft(G), GS ? *GS->StatusLine : TEXT("-")));
	};

	auto RecordEnemies = [](FWWORegress& R, const FString& Prefix)
	{
		const TArray<AWWOEnemy*> List = M::AliveEnemies(M::GM(R));
		for (int32 i = 0; i < List.Num(); ++i)
		{
			const AWWOEnemy* E = List[i];
			const FString K = FString::Printf(TEXT("%s%d"), *Prefix, i);
			if (!E) { R.Record(K, TEXT("gone")); continue; }
			R.Record(K + TEXT(".class"), E->GetClass()->GetName());
			R.RecordVec(K + TEXT(".at"), E->GetActorLocation());
			R.Record(K + TEXT(".yaw"), Deg(E->GetActorRotation().Yaw));
		}
	};

	auto Inert = [](FWWORegress& R)
	{
		for (const auto& Pair : R.Loadout()->Catalog.EnemyData)
		{
			UWWOEnemyData* D = Pair.Value;
			if (D && D->AiEnabled >= 0.5f) R.Tune(D, "AiEnabled", 0.f);
		}
	};

	// =============================================================================================
	// Waves
	// =============================================================================================

	// Begin: off (Enabled 0), no spawn points, and with points (the hand-placed enemies go, allies stay).
	{
		FWWORegressScenario S;
		S.Name = TEXT("match.wave.begin");
		TSharedRef<FMatchRun> Run = MakeShared<FMatchRun>();
		S.Setup = [Run, Inert](FWWORegress& R)
		{
			*Run = FMatchRun();
			Inert(R);
			Run->Others.Add(R.SpawnEnemy("swarm", R.Ahead(12.f)));
			Run->Others.Add(R.SpawnAlly(R.Ahead(8.f, 4.f)));
		};
		S.Step = [Run](FWWORegress& R, int32 Tick)
		{
			AWWOGameMode* G = M::GM(R);
			auto Case = [&](const TCHAR* Name)
			{
				M::Enter(R, *Run, ERules::Waves);
				R.Record(FString::Printf(TEXT("%s.state"), Name), M::State(G));
				R.Record(FString::Printf(TEXT("%s.wave"), Name), M::Wave(G));
				R.Record(FString::Printf(TEXT("%s.timer"), Name), M::Timer(G));
				R.RecordBool(FString::Printf(TEXT("%s.enemy_left"), Name), Alive(Run->Others[0].Get()));
				R.RecordBool(FString::Printf(TEXT("%s.ally_left"), Name), Alive(Run->Others[1].Get()));
				M::Leave(R, *Run);
			};
			R.Tune(G, "Enabled", 1.f);
			Case(TEXT("no_points"));
			AddPoint(R, R.Ahead(20.f));
			R.Tune(G, "Enabled", 0.f);
			Case(TEXT("off"));
			AddPoint(R, R.Ahead(20.f));
			R.Tune(G, "Enabled", 1.f);
			R.Tune(G, "StartDelay", 4.5f);
			Case(TEXT("on"));
			RemovePoints();
			return true;
		};
		Out.Add(S);
	}

	// A wave's whole round: the countdown, wave 1 spawned one by one, every enemy killed, the break, wave 2 starting.
	auto WaveRound = [WaveEvent, RecordEnemies, Inert](const FString& Name, float WavesToWin)
	{
		FWWORegressScenario S;
		S.Name = Name;
		S.MaxTicks = 1500;
		TSharedRef<FMatchRun> Run = MakeShared<FMatchRun>();
		S.Setup = [Run, Inert, WavesToWin](FWWORegress& R)
		{
			*Run = FMatchRun();
			Inert(R);
			AWWOGameMode* G = M::GM(R);
			R.Tune(G, "Enabled", 1.f);
			R.Tune(G, "StartDelay", 0.5f);
			R.Tune(G, "BreakTime", 1.f);
			R.Tune(G, "SizeScale", 0.25f); // wave 1: 2 Swarms
			R.Tune(G, "WavesToWin", WavesToWin);
			AddPoint(R, R.Ahead(20.f, -6.f));
			AddPoint(R, R.Ahead(22.f, 6.f));
		};
		S.Step = [Run, WaveEvent, RecordEnemies](FWWORegress& R, int32 Tick)
		{
			AWWOGameMode* G = M::GM(R);
			if (Run->Start < 0)
			{
				if (NavBusy(R.World()) && Tick < 600) return false; // the spawn checks the navmesh
				Run->Start = Tick;
				M::Enter(R, *Run, ERules::Waves);
				WaveEvent(R, *Run, 0, true);
				return false;
			}
			const int32 T = Tick - Run->Start;
			WaveEvent(R, *Run, T, false);
			const TArray<AWWOEnemy*> Living = M::AliveEnemies(G);
			if (!Run->bKilled && M::State(G) == (int32)EWWOWaveState::Spawning && M::Queue(G).Num() == 0 && Living.Num() == 2)
			{
				Run->bKilled = true;
				RecordEnemies(R, TEXT("w1."));
				R.Record(TEXT("kill_tick"), T);
				M::SkipWave(G); // the last ones killed
				WaveEvent(R, *Run, T, true);
			}
			const bool bNext = M::State(G) == (int32)EWWOWaveState::Spawning && M::Wave(G) == 2;
			const bool bWon = M::State(G) == (int32)EWWOWaveState::Won;
			if (bWon && Run->Mark < 0) { Run->Mark = T; return false; } // one more tick: the status line
			if (bNext || (bWon && T > Run->Mark) || T > 1200)
			{
				if (bNext) R.Record(TEXT("w2.queue"), Joined(M::Queue(G)));
				R.Record(TEXT("end_tick"), T);
				RecordGS(R, TEXT("end."));
				R.Record(TEXT("rand_next"), WWO::FRand());
				M::Done(R, *Run);
				return true;
			}
			return false;
		};
		return S;
	};
	Out.Add(WaveRound(TEXT("match.wave.round"), 8.f));
	Out.Add(WaveRound(TEXT("match.wave.won"), 1.f));

	// Everyone down: the player alone (lost at once), then with a bot and an ally up (lost only when the last player goes).
	for (const bool bCompany : { false, true })
	{
		FWWORegressScenario S;
		S.Name = bCompany ? TEXT("match.wave.last_down") : TEXT("match.wave.lost");
		TSharedRef<FMatchRun> Run = MakeShared<FMatchRun>();
		S.Setup = [Run, Inert, bCompany](FWWORegress& R)
		{
			*Run = FMatchRun();
			Inert(R);
			R.Tune(M::GM(R), "Enabled", 1.f);
			AddPoint(R, R.Ahead(20.f));
			if (bCompany)
			{
				Run->Others.Add(R.SpawnBot(0, R.Ahead(5.f, 3.f)));
				Run->Others.Add(R.SpawnAlly(R.Ahead(5.f, -3.f)));
			}
		};
		S.Step = [Run, WaveEvent, bCompany](FWWORegress& R, int32 Tick)
		{
			AWWOGameMode* G = M::GM(R);
			if (Tick == 0) { M::Enter(R, *Run, ERules::Waves); WaveEvent(R, *Run, Tick, true); return false; }
			auto After = [&](const TCHAR* What)
			{
				R.Record(FString::Printf(TEXT("%s.state"), What), M::State(G));
				R.Record(FString::Printf(TEXT("%s.restart"), What), M::RestartLeft(G));
				M::NoRestart(G);
			};
			AWWOCharacter* Bot = bCompany ? Cast<AWWOCharacter>(Run->Others[0].Get()) : nullptr;
			AWWOAlly* Ally = bCompany ? Cast<AWWOAlly>(Run->Others[1].Get()) : nullptr;
			if (Tick == 5) { R.Player()->Health->Kill(); After(TEXT("player_down")); }
			if (Tick == 6 && Ally) { Ally->TakeAllyDamage(99999.f); After(TEXT("ally_down")); }
			if (Tick == 7 && Bot) { Bot->Health->Kill(); After(TEXT("bot_down")); }
			if (Tick == 9)
			{
				WaveEvent(R, *Run, Tick, true);
				RecordGS(R, TEXT("end."));
				M::Done(R, *Run);
				return true;
			}
			return false;
		};
		Out.Add(S);
	}

	// Jumping to a wave (the console's wwo.Wave), skipping one (wwo.SkipWave), and both while idle.
	{
		FWWORegressScenario S;
		S.Name = TEXT("match.wave.jump");
		TSharedRef<FMatchRun> Run = MakeShared<FMatchRun>();
		S.Setup = [Run](FWWORegress& R) { *Run = FMatchRun(); R.Tune(M::GM(R), "Enabled", 1.f); AddPoint(R, R.Ahead(20.f)); };
		S.Step = [Run](FWWORegress& R, int32 Tick)
		{
			AWWOGameMode* G = M::GM(R);
			auto Note = [&](const TCHAR* What)
			{
				R.Record(FString::Printf(TEXT("%s"), What), FString::Printf(TEXT("state %d wave %d timer %.9g queue %s"), M::State(G), M::Wave(G), M::Timer(G), *Joined(M::Queue(G))));
			};
			Note(TEXT("idle"));
			M::JumpToWave(G, 3);
			Note(TEXT("idle_jump"));
			M::Enter(R, *Run, ERules::Waves);
			Note(TEXT("begun"));
			M::JumpToWave(G, 0);
			Note(TEXT("jump0"));
			M::JumpToWave(G, 5);
			Note(TEXT("jump5"));
			M::JumpToWave(G, 7);
			Note(TEXT("jump7"));
			M::SkipWave(G);
			Note(TEXT("skipped"));
			R.Record(TEXT("rand_next"), WWO::FRand());
			M::Done(R, *Run);
			return true;
		};
		Out.Add(S);
	}

	// Every status line the game mode writes (both modes; a test run's PvP lines say "restarting").
	{
		FWWORegressScenario S;
		S.Name = TEXT("match.status");
		TSharedRef<FMatchRun> Run = MakeShared<FMatchRun>();
		S.Setup = [Run](FWWORegress& R) { *Run = FMatchRun(); };
		S.Step = [Run](FWWORegress& R, int32 Tick)
		{
			AWWOGameMode* G = M::GM(R);
			R.Tune(G, "WavesToWin", 6.f);
			R.Tune(G, "KillTarget", 25.f);
			R.Tune(G, "TimeLimit", 7.f);
			M::Enter(R, *Run, ERules::Waves);
			if (AWWOGameState* GS = G->GetGameState<AWWOGameState>()) GS->TeamScores = { 3, 5 };
			struct FCase { const TCHAR* Name; EWWOWaveState St; int32 Wave; float Timer; int32 Left; float MatchLeft; int32 Winner; bool bTest; };
			const FCase Cases[] = {
				{ TEXT("idle"), EWWOWaveState::Idle, 0, 0.f, 0, 0.f, -1, false },
				{ TEXT("get_ready"), EWWOWaveState::Countdown, 0, 5.2f, 0, 0.f, -1, false },
				{ TEXT("countdown"), EWWOWaveState::Countdown, 2, 0.4f, 0, 0.f, -1, false },
				{ TEXT("spawning"), EWWOWaveState::Spawning, 3, 0.f, 7, 0.f, -1, false },
				{ TEXT("won"), EWWOWaveState::Won, 6, 0.f, 0, 0.f, -1, false },
				{ TEXT("lost"), EWWOWaveState::Lost, 4, 0.f, 0, 0.f, -1, false },
				{ TEXT("match"), EWWOWaveState::Match, 0, 0.f, 0, 61.3f, -1, false },
				{ TEXT("match_low"), EWWOWaveState::Match, 0, 0.f, 0, -2.f, -1, false },
				{ TEXT("over_blue"), EWWOWaveState::MatchOver, 0, 0.f, 0, 0.f, 0, false },
				{ TEXT("over_orange_test"), EWWOWaveState::MatchOver, 0, 0.f, 0, 0.f, 1, true },
				{ TEXT("over_draw"), EWWOWaveState::MatchOver, 0, 0.f, 0, 0.f, -1, false },
				{ TEXT("over_draw_test"), EWWOWaveState::MatchOver, 0, 0.f, 0, 0.f, -1, true },
			};
			for (const FCase& C : Cases)
			{
				FLinearColor Color;
				const FString Line = M::StatusFor(G, C.St, C.Wave, C.Timer, C.Left, C.MatchLeft, C.Winner, C.bTest, Color);
				R.Record(FString::Printf(TEXT("%s"), C.Name), Line);
				R.Record(FString::Printf(TEXT("%s.color"), C.Name), ColorText(Color));
			}
			// The limits off: no "first to", no clock.
			R.Tune(G, "KillTarget", 0.f);
			R.Tune(G, "TimeLimit", 0.f);
			FLinearColor Color;
			R.Record(TEXT("match_no_limits"), M::StatusFor(G, EWWOWaveState::Match, 0, 0.f, 0, 30.f, -1, false, Color));
			M::Done(R, *Run);
			return true;
		};
		Out.Add(S);
	}

	// =============================================================================================
	// Team deathmatch
	// =============================================================================================

	// Begin: enemies and allies go, the game state's match fields reset, the clock set.
	{
		FWWORegressScenario S;
		S.Name = TEXT("match.tdm.begin");
		S.bPvP = true;
		TSharedRef<FMatchRun> Run = MakeShared<FMatchRun>();
		S.Setup = [Run, Inert](FWWORegress& R)
		{
			*Run = FMatchRun();
			Inert(R);
			Run->Others.Add(R.SpawnEnemy("swarm", R.Ahead(12.f)));
			Run->Others.Add(R.SpawnAlly(R.Ahead(8.f, 4.f)));
			Run->Others.Add(R.SpawnDummy(R.Ahead(10.f, -4.f)));
		};
		S.Step = [Run](FWWORegress& R, int32 Tick)
		{
			AWWOGameMode* G = M::GM(R);
			if (Tick == 0)
			{
				R.Tune(G, "TimeLimit", 4.f);
				M::Snap(G, *Run); // as it was, before the stale fields below
				Run->Mark = 1;
				if (AWWOGameState* GS = G->GetGameState<AWWOGameState>()) { GS->TeamScores = { 9, 9, 9 }; GS->bMatchOver = true; GS->MatchWinner = 1; }
				const FMatchRun Before = *Run;
				M::Enter(R, *Run, ERules::Deathmatch);
				Run->bGSPvP = Before.bGSPvP;
				Run->TeamScores = Before.TeamScores;
				Run->bMatchOver = Before.bMatchOver;
				Run->MatchWinner = Before.MatchWinner;
				R.Record(TEXT("state"), M::State(G));
				R.Record(TEXT("match_left"), M::MatchLeft(G));
				R.Record(TEXT("winner"), M::Winner(G));
				R.RecordBool(TEXT("enemy_left"), Alive(Run->Others[0].Get()));
				R.RecordBool(TEXT("ally_left"), Alive(Run->Others[1].Get()));
				R.RecordBool(TEXT("dummy_left"), Alive(Run->Others[2].Get()));
				RecordGS(R, TEXT("begun."));
				return false;
			}
			// One tick of the match: the clock and the status line.
			R.Record(TEXT("t1.match_left"), M::MatchLeft(G));
			RecordGS(R, TEXT("t1."));
			M::Done(R, *Run);
			return true;
		};
		Out.Add(S);
	}

	// A kill: the credit, the score, the kill feed, the victim's respawn timer, then where it comes back.
	for (const bool bPrecision : { false, true })
	{
		FWWORegressScenario S;
		S.Name = bPrecision ? TEXT("match.tdm.kill_precision") : TEXT("match.tdm.kill");
		S.bPvP = true;
		S.MaxTicks = 400;
		TSharedRef<FMatchRun> Run = MakeShared<FMatchRun>();
		S.Setup = [Run](FWWORegress& R)
		{
			*Run = FMatchRun();
			AddPvPPoints(R);
			Run->Others.Add(R.SpawnBot(1, R.Ahead(10.f)));
			Run->Others.Add(R.SpawnBot(1, R.Ahead(14.f, -9.f))); // a foe standing near one spot
		};
		S.Step = [Run, bPrecision](FWWORegress& R, int32 Tick)
		{
			AWWOGameMode* G = M::GM(R);
			AWWOCharacter* Victim = Cast<AWWOCharacter>(Run->Others[0].Get());
			if (!Victim || Tick > 390) { R.Record(TEXT("no_respawn"), 1); M::Done(R, *Run); return true; }
			if (Tick == 0)
			{
				M::Enter(R, *Run, ERules::Deathmatch);
				KillBy(R.Player(), Victim, bPrecision);
				M::PvPDeath(G, Victim);
				RecordPS(R, TEXT("killer"), R.Player());
				RecordPS(R, TEXT("victim"), Victim);
				RecordFeed(R, TEXT("feed"), Run->Feed);
				R.Record(TEXT("pvp_kills"), AWWOGameMode::PvPKills - Run->PvPKills);
				R.Record(TEXT("state"), M::State(G));
				R.Record(TEXT("respawn_left"), Victim->Health->RespawnLeft);
				RecordGS(R, TEXT("kill."));
				// A second death call for the same body (already dead): counted again, as before.
				M::PvPDeath(G, Victim);
				RecordPS(R, TEXT("again.victim"), Victim);
				R.Record(TEXT("again.pvp_kills"), AWWOGameMode::PvPKills - Run->PvPKills);
				M::Leave(R, *Run);
				return false;
			}
			if (!Victim->Health->bDead)
			{
				R.Record(TEXT("respawn_tick"), Tick);
				R.RecordVec(TEXT("respawn_at"), Victim->GetActorLocation());
				R.Record(TEXT("respawn_yaw"), Deg(Victim->GetActorRotation().Yaw));
				R.Record(TEXT("respawn_health"), Victim->Health->Health + Victim->Health->Shield);
				R.Record(TEXT("rand_next"), WWO::FRand());
				M::Done(R, *Run);
				return true;
			}
			return false;
		};
		Out.Add(S);
	}

	// No kill credit: a teammate's kill, an other-team hit more than 10 s ago, nobody's hit; and one 9.5 s ago that counts.
	{
		FWWORegressScenario S;
		S.Name = TEXT("match.tdm.no_credit");
		S.bPvP = true;
		TSharedRef<FMatchRun> Run = MakeShared<FMatchRun>();
		S.Setup = [Run](FWWORegress& R)
		{
			*Run = FMatchRun();
			Run->Others.Add(R.SpawnBot(0, R.Ahead(8.f, -6.f)));
			Run->Others.Add(R.SpawnBot(1, R.Ahead(8.f, -2.f)));
			Run->Others.Add(R.SpawnBot(1, R.Ahead(8.f, 2.f)));
			Run->Others.Add(R.SpawnBot(1, R.Ahead(8.f, 6.f)));
		};
		S.Step = [Run](FWWORegress& R, int32 Tick)
		{
			AWWOGameMode* G = M::GM(R);
			M::Enter(R, *Run, ERules::Deathmatch);
			const double Now = R.World()->GetTimeSeconds();
			const TCHAR* Names[] = { TEXT("teammate"), TEXT("stale"), TEXT("nobody"), TEXT("recent") };
			const double Ago[] = { 0.0, 10.5, 0.0, 9.5 };
			for (int32 i = 0; i < 4; ++i)
			{
				AWWOCharacter* V = Cast<AWWOCharacter>(Run->Others[i].Get());
				if (!V) { R.Record(Names[i], TEXT("missing")); continue; }
				V->Health->LastAttacker = i == 2 ? nullptr : R.Player();
				V->Health->LastAttackTime = Now - Ago[i];
				V->Health->bLastAttackCritical = true;
				V->Health->Kill();
				M::PvPDeath(G, V);
				RecordPS(R, FString::Printf(TEXT("%s.victim"), Names[i]), V);
				RecordPS(R, FString::Printf(TEXT("%s.player"), Names[i]), R.Player());
			}
			RecordFeed(R, TEXT("feed"), Run->Feed);
			RecordGS(R, TEXT("end."));
			M::Done(R, *Run);
			return true;
		};
		Out.Add(S);
	}

	// The kill limit: three kills 30 ticks apart, the third ends the match. The match's own tick binds the
	// bots' deaths and gives them teams; the bots' health follows the Bots settings.
	{
		FWWORegressScenario S;
		S.Name = TEXT("match.tdm.kill_limit");
		S.bPvP = true;
		TSharedRef<FMatchRun> Run = MakeShared<FMatchRun>();
		S.Setup = [Run](FWWORegress& R)
		{
			*Run = FMatchRun();
			R.Tune(M::GM(R), "KillTarget", 4.f); // 1 + 2 (the precision kill) + 1
			R.Tune(M::GM(R), "BotHealth", 120.f);
			for (int32 i = 0; i < 3; ++i) Run->Others.Add(R.SpawnBot(1, R.Ahead(10.f, (i - 1) * 4.f)));
		};
		S.Step = [Run](FWWORegress& R, int32 Tick)
		{
			AWWOGameMode* G = M::GM(R);
			if (Tick == 0) { M::Enter(R, *Run, ERules::Deathmatch); return false; }
			if (Tick == 1)
			{
				R.Record(TEXT("balanced"), TeamsOf(R));
				for (int32 i = 0; i < 3; ++i)
				{
					const AWWOCharacter* B = Cast<AWWOCharacter>(Run->Others[i].Get());
					if (B) R.Record(FString::Printf(TEXT("bot%d.health"), i), FString::Printf(TEXT("%.9g/%.9g base %.9g/%.9g"), B->Health->Health, B->Health->Shield, B->Health->BaseHealth, B->Health->BaseShield));
				}
				for (const TWeakObjectPtr<AActor>& B : Run->Others) SetTeamOf(B.Get(), 1); // all on the other team
			}
			for (int32 i = 0; i < 3; ++i)
			{
				if (Tick == 2 + i * 30)
				{
					KillBy(R.Player(), Cast<AWWOCharacter>(Run->Others[i].Get()), i == 1);
					const FString K = FString::Printf(TEXT("kill%d."), i);
					R.Record(K + TEXT("state"), M::State(G));
					R.Record(K + TEXT("winner"), M::Winner(G));
					RecordGS(R, K);
				}
			}
			if (Tick == 64)
			{
				RecordPS(R, TEXT("player"), R.Player());
				RecordFeed(R, TEXT("feed"), Run->Feed);
				R.Record(TEXT("restart"), M::RestartLeft(G));
				R.Record(TEXT("matches_ended"), AWWOGameMode::PvPMatchesEnded - Run->PvPMatchesEnded);
				RecordGS(R, TEXT("end."));
				M::Done(R, *Run);
				return true;
			}
			return false;
		};
		Out.Add(S);
	}

	// The time limit (3 s): the clock as the status line shows it, then the match over by the most kills (or a draw).
	for (const bool bKill : { true, false })
	{
		FWWORegressScenario S;
		S.Name = bKill ? TEXT("match.tdm.time_limit") : TEXT("match.tdm.time_draw");
		S.bPvP = true;
		S.MaxTicks = 400;
		TSharedRef<FMatchRun> Run = MakeShared<FMatchRun>();
		S.Setup = [Run](FWWORegress& R)
		{
			*Run = FMatchRun();
			R.Tune(M::GM(R), "TimeLimit", 0.05f);
			Run->Others.Add(R.SpawnBot(1, R.Ahead(10.f)));
		};
		S.Step = [Run, bKill](FWWORegress& R, int32 Tick)
		{
			AWWOGameMode* G = M::GM(R);
			if (Tick == 0) { M::Enter(R, *Run, ERules::Deathmatch); return false; }
			if (Tick == 1) SetTeamOf(Run->Others[0].Get(), 1);
			if (Tick == 10 && bKill) KillBy(R.Player(), Cast<AWWOCharacter>(Run->Others[0].Get()), false);
			if (Tick % 60 == 0)
			{
				const AWWOGameState* GS = R.World()->GetGameState<AWWOGameState>();
				R.Record(FString::Printf(TEXT("t%03d"), Tick), FString::Printf(TEXT("left %.9g | %s"), M::MatchLeft(G), GS ? *GS->StatusLine : TEXT("-")));
			}
			if (M::State(G) == (int32)EWWOWaveState::MatchOver && Run->Mark < 0)
			{
				Run->Mark = Tick;
				R.Record(TEXT("over_tick"), Tick);
				R.Record(TEXT("winner"), M::Winner(G));
				R.Record(TEXT("match_left"), M::MatchLeft(G));
				return false;
			}
			if (Run->Mark >= 0 || Tick > 380)
			{
				RecordGS(R, TEXT("end."));
				M::Done(R, *Run);
				return true;
			}
			return false;
		};
		Out.Add(S);
	}

	// Where a player comes back: the spot farthest from the nearest foe, one of the best three, none taken, facing the middle.
	{
		FWWORegressScenario S;
		S.Name = TEXT("match.tdm.spawn_spot");
		S.bPvP = true;
		TSharedRef<FMatchRun> Run = MakeShared<FMatchRun>();
		S.Setup = [Run](FWWORegress& R)
		{
			*Run = FMatchRun();
			AddPvPPoints(R);
			Run->Others.Add(R.SpawnBot(1, R.Ahead(15.f, -10.f) + FVector(0, 0, 0.f))); // on a spot: taken
			Run->Others.Add(R.SpawnBot(1, R.Ahead(28.f, -4.f)));
			Run->Others.Add(R.SpawnBot(0, R.Ahead(15.f, 9.f))); // a teammate near a spot: still taken
		};
		S.Step = [Run](FWWORegress& R, int32 Tick)
		{
			AWWOGameMode* G = M::GM(R);
			for (int32 i = 0; i < 5; ++i)
			{
				const FTransform Spot = G->PvPSpawnFor(R.Player());
				R.RecordVec(FString::Printf(TEXT("pick%d.at"), i), Spot.GetLocation());
				R.Record(FString::Printf(TEXT("pick%d.yaw"), i), Deg(Spot.Rotator().Yaw));
			}
			// No foes standing: any spot, at random.
			for (const TWeakObjectPtr<AActor>& B : Run->Others) if (AWWOCharacter* C = Cast<AWWOCharacter>(B.Get())) C->Health->Kill();
			for (int32 i = 0; i < 3; ++i)
			{
				const FTransform Spot = G->PvPSpawnFor(R.Player());
				R.RecordVec(FString::Printf(TEXT("alone%d.at"), i), Spot.GetLocation());
			}
			R.Record(TEXT("rand_next"), WWO::FRand());
			RemovePoints();
			return true;
		};
		Out.Add(S);
	}

	// Switching sides: the teams balanced in turn, a switch onto the smaller side (moved to a spawn spot,
	// health refilled), one again at once (too soon), and one that would leave the other side bigger.
	{
		FWWORegressScenario S;
		S.Name = TEXT("match.tdm.switch");
		S.bPvP = true;
		TSharedRef<FMatchRun> Run = MakeShared<FMatchRun>();
		S.Setup = [Run](FWWORegress& R)
		{
			*Run = FMatchRun();
			AddPvPPoints(R);
			Run->Others.Add(R.SpawnBot(0, R.Ahead(20.f, -3.f)));
			Run->Others.Add(R.SpawnBot(0, R.Ahead(20.f, 3.f)));
		};
		S.Step = [Run](FWWORegress& R, int32 Tick)
		{
			AWWOGameMode* G = M::GM(R);
			AWWOCharacter* P = R.Player();
			AWWOPlayerState* PS = P->GetPlayerState<AWWOPlayerState>();
			const double SwitchBefore = PS ? PS->LastSwitch : 0.0;
			const float SpawnYawBefore = P->Health->SpawnYaw;
			M::Enter(R, *Run, ERules::Deathmatch);
			SetTeamOf(P, -1);
			for (const TWeakObjectPtr<AActor>& B : Run->Others) SetTeamOf(B.Get(), -1);
			G->BalanceTeam(R.PC);
			for (const TWeakObjectPtr<AActor>& B : Run->Others) if (APawn* Pawn = Cast<APawn>(B.Get())) G->BalanceTeam(Pawn->GetController());
			R.Record(TEXT("balanced"), TeamsOf(R));
			P->Health->Health = 10.f;
			G->SwitchTeam(R.PC);
			R.Record(TEXT("switched"), TeamsOf(R));
			R.RecordVec(TEXT("switched.at"), P->GetActorLocation());
			R.Record(TEXT("switched.yaw"), Deg(P->GetActorRotation().Yaw));
			R.Record(TEXT("switched.health"), P->Health->Health);
			G->SwitchTeam(R.PC);
			R.Record(TEXT("too_soon"), TeamsOf(R));
			if (const APawn* Pawn = Cast<APawn>(Run->Others[1].Get())) G->SwitchTeam(Pawn->GetController());
			R.Record(TEXT("bigger"), TeamsOf(R));
			R.Record(TEXT("rand_next"), WWO::FRand());
			if (PS) PS->LastSwitch = SwitchBefore;
			P->Health->SpawnYaw = SpawnYawBefore;
			M::Done(R, *Run);
			// Outside a match: nothing.
			G->SwitchTeam(R.PC);
			R.Record(TEXT("no_match"), TeamsOf(R));
			return true;
		};
		Out.Add(S);
	}

	// =============================================================================================
	// The firing tester
	// =============================================================================================

	{
		FWWORegressScenario S;
		S.Name = TEXT("match.tester.begin");
		TSharedRef<FMatchRun> Run = MakeShared<FMatchRun>();
		S.Setup = [Run, Inert](FWWORegress& R)
		{
			*Run = FMatchRun();
			Inert(R);
			Run->Others.Add(R.SpawnEnemy("swarm", R.Ahead(12.f)));
			Run->Others.Add(R.SpawnAlly(R.Ahead(8.f, 4.f)));
			Run->Others.Add(R.SpawnDummy(R.Ahead(10.f, -4.f)));
		};
		S.Step = [Run](FWWORegress& R, int32 Tick)
		{
			AWWOGameMode* G = M::GM(R);
			M::Enter(R, *Run, ERules::Tester);
			R.Record(TEXT("state"), M::State(G));
			R.RecordBool(TEXT("enemy_left"), Alive(Run->Others[0].Get()));
			R.RecordBool(TEXT("ally_left"), Alive(Run->Others[1].Get()));
			R.RecordBool(TEXT("dummy_left"), Alive(Run->Others[2].Get()));
			FLinearColor Color;
			R.Record(TEXT("status"), M::Status(G, Color));
			M::Done(R, *Run);
			return true;
		};
		Out.Add(S);
	}

	// The dummy: 20 m ahead of where the player first stood, on the floor, facing back; put down again
	// at the same spot when it is gone, wherever the player is by then.
	{
		FWWORegressScenario S;
		S.Name = TEXT("match.tester.target");
		TSharedRef<FMatchRun> Run = MakeShared<FMatchRun>();
		S.Setup = [Run](FWWORegress& R) { *Run = FMatchRun(); };
		S.Step = [Run](FWWORegress& R, int32 Tick)
		{
			AWWOGameMode* G = M::GM(R);
			M::Enter(R, *Run, ERules::Tester);
			R.Face(R.HomeYaw() + 30.f);
			M::UpdateTester(G);
			AWWOTarget* First = M::TesterTarget(G);
			R.RecordBool(TEXT("placed"), First != nullptr);
			if (First)
			{
				R.RecordVec(TEXT("target.at"), First->GetActorLocation());
				R.Record(TEXT("target.yaw"), Deg(First->GetActorRotation().Yaw));
			}
			R.RecordBool(TEXT("home_set"), M::TesterHomeSet(G));
			R.RecordVec(TEXT("home.at"), M::TesterHome(G).GetLocation());
			R.Record(TEXT("home.yaw"), Deg(M::TesterHome(G).Rotator().Yaw));
			M::UpdateTester(G);
			R.RecordBool(TEXT("kept"), M::TesterTarget(G) == First);
			if (First) First->Destroy();
			R.PlaceAt(R.Ahead(4.f, 3.f), 0.f);
			M::UpdateTester(G);
			AWWOTarget* Second = M::TesterTarget(G);
			R.RecordBool(TEXT("replaced"), Second != nullptr && Second != First);
			if (Second) R.RecordVec(TEXT("again.at"), Second->GetActorLocation());
			int32 Dummies = 0;
			for (TActorIterator<AWWOTarget> It(R.World()); It; ++It) Dummies += Alive(*It) ? 1 : 0;
			R.Record(TEXT("dummies"), Dummies);
			M::Done(R, *Run);
			return true;
		};
		Out.Add(S);
	}

	// =============================================================================================
	// The bot roster (bots pick their kit with FMath's generator, which drawing also draws on: no-draw only)
	// =============================================================================================

	auto NewBots = [](FWWORegress& R, TSet<AWWOBotController*>& Known, bool bAdopt = true) -> TArray<AWWOBotController*>
	{
		TArray<AWWOBotController*> Fresh;
		for (TActorIterator<AWWOBotController> It(R.World()); It; ++It)
		{
			if (Known.Contains(*It) || !Alive(*It)) continue;
			Known.Add(*It);
			if (!bAdopt) continue;
			Fresh.Add(*It);
			It->bParked = true;
			R.SpawnedControllers.Add(*It); // the teardown removes them
		}
		return Fresh;
	};
	auto RecordBot = [](FWWORegress& R, const FString& K, AWWOBotController* Bot)
	{
		const AWWOPlayerState* PS = Bot->GetPlayerState<AWWOPlayerState>();
		const APawn* Body = Bot->GetPawn();
		R.Record(K, FString::Printf(TEXT("%s team %d"), PS ? *PS->GetPlayerName() : TEXT("?"), PS ? PS->Team : -9));
		R.Record(K + TEXT(".kit"), FString::Printf(TEXT("%s %s %s guns %s"), *Bot->Kit.Flora.ToString(), *Bot->Kit.Grenade.ToString(), *Joined(Bot->Kit.Abilities), *Joined(Bot->Guns)));
		if (Body)
		{
			R.RecordVec(K + TEXT(".at"), Body->GetActorLocation());
			R.Record(K + TEXT(".yaw"), Deg(Body->GetActorRotation().Yaw));
		}
	};

	// Filling a match of N players: one bot a call (the launch screen's count), each on the smaller team.
	for (const int32 Players : { 2, 3, 6, 12 })
	{
		FWWORegressScenario S;
		S.Name = FString::Printf(TEXT("match.roster.fill.%d"), Players);
		S.bPvP = true;
		S.Modes = EWWORegressModes::NoRenderOnly;
		S.ModeReason = TEXT("a bot's kit and timers draw on FMath's generator, which drawing also draws on");
		TSharedRef<FMatchRun> Run = MakeShared<FMatchRun>();
		S.Setup = [Run](FWWORegress& R) { *Run = FMatchRun(); AddPvPPoints(R); };
		S.Step = [Run, Players, NewBots, RecordBot](FWWORegress& R, int32 Tick)
		{
			AWWOGameMode* G = M::GM(R);
			TSet<AWWOBotController*> Known;
			NewBots(R, Known, false);
			M::Enter(R, *Run, ERules::Deathmatch);
			M::SetOptionBots(G, Players - 1);
			int32 N = 0;
			for (int32 i = 0; i < Players; ++i) // one call more than it needs: the last adds nobody
			{
				M::UpdateBots(G);
				for (AWWOBotController* Bot : NewBots(R, Known)) RecordBot(R, FString::Printf(TEXT("bot%02d"), N++), Bot);
			}
			R.Record(TEXT("spawned"), M::BotsSpawned(G));
			R.Record(TEXT("teams"), TeamsOf(R));
			R.Record(TEXT("rand_next"), WWO::FRand());
			M::Done(R, *Run);
			return true;
		};
		Out.Add(S);
	}

	// A person joining: a bot from the bigger team makes room; a person leaving: a bot takes the place.
	{
		FWWORegressScenario S;
		S.Name = TEXT("match.roster.replace");
		S.bPvP = true;
		S.Modes = EWWORegressModes::NoRenderOnly;
		S.ModeReason = TEXT("a bot's kit and timers draw on FMath's generator, which drawing also draws on");
		TSharedRef<FMatchRun> Run = MakeShared<FMatchRun>();
		S.Setup = [Run](FWWORegress& R) { *Run = FMatchRun(); AddPvPPoints(R); };
		S.Step = [Run, NewBots, RecordBot](FWWORegress& R, int32 Tick)
		{
			AWWOGameMode* G = M::GM(R);
			TSet<AWWOBotController*> Known;
			NewBots(R, Known, false);
			M::Enter(R, *Run, ERules::Deathmatch);
			M::RetireBotFor(G, R.PC); // no bots yet: nothing
			R.Record(TEXT("empty.counts"), FString::Printf(TEXT("spawned %d extra %d"), M::BotsSpawned(G), M::BotsExtra(G)));
			M::SetOptionBots(G, 3);
			for (int32 i = 0; i < 3; ++i) { M::UpdateBots(G); NewBots(R, Known); }
			R.Record(TEXT("full"), TeamsOf(R));
			M::RetireBotFor(G, R.PC);
			R.Record(TEXT("retired"), TeamsOf(R));
			R.Record(TEXT("retired.counts"), FString::Printf(TEXT("spawned %d extra %d"), M::BotsSpawned(G), M::BotsExtra(G)));
			M::UpdateBots(G);
			R.Record(TEXT("after_retire.new"), NewBots(R, Known).Num());
			M::PersonLeft(G);
			M::PersonLeft(G);
			M::UpdateBots(G);
			M::UpdateBots(G);
			int32 N = 0;
			for (AWWOBotController* Bot : NewBots(R, Known)) RecordBot(R, FString::Printf(TEXT("filled%d"), N++), Bot);
			M::UpdateBots(G);
			R.Record(TEXT("left.counts"), FString::Printf(TEXT("spawned %d extra %d new %d"), M::BotsSpawned(G), M::BotsExtra(G), NewBots(R, Known).Num()));
			R.Record(TEXT("teams"), TeamsOf(R));
			M::Done(R, *Run);
			return true;
		};
		Out.Add(S);
	}
}
