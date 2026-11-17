// Regression scenarios: melee (reach, lunge, knockback, Thornfist, Static Charge, Melee Guard), the
// damage pipeline (shield then health, damage numbers, hit markers, who may hurt whom, kill credit,
// shots at players' hitboxes) and the enemies' basics (health, the Tank's shield, attacks, KillZ).

#include "Player/WWORegress.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOAbilityComponent.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "Enemies/WWOEnemy.h"
#include "Enemies/WWOEnemyTypes.h"
#include "Enemies/WWOEnemyProjectile.h"
#include "Allies/WWOAlly.h"
#include "World/WWOTarget.h"
#include "Data/WWOData.h"
#include "EngineUtils.h"
#include "Camera/CameraComponent.h"

namespace
{
	struct FCombatState
	{
		TWeakObjectPtr<AActor> Target;
		TArray<TWeakObjectPtr<AActor>> Others;
		float Before = 0.f, OthersBefore = 0.f, PeakSpeed = 0.f;
		FVector From = FVector::ZeroVector;
		int32 Mark = -1;
	};

	float OthersHealth(const FCombatState& St)
	{
		float Sum = 0.f;
		for (const TWeakObjectPtr<AActor>& A : St.Others) Sum += FWWORegress::HealthOf(A.Get());
		return Sum;
	}

	AActor* SpawnTarget(FWWORegress& R, FName Kind, const FVector& At, float DummyHealth = 1000.f)
	{
		if (Kind == "dummy") return R.SpawnDummy(At, DummyHealth);
		if (Kind == "foe") return R.SpawnBot(1, At);
		if (Kind == "mate") return R.SpawnBot(0, At);
		if (Kind == "ally") return R.SpawnAlly(At);
		return R.SpawnEnemy(Kind, At);
	}

	FVector BodyOf(const AActor* A)
	{
		const IWWOTargetable* T = Cast<IWWOTargetable>(A);
		return T ? T->BodyCenter() : (A ? A->GetActorLocation() : FVector::ZeroVector);
	}

	FString Who(const FWWORegress& R, const AActor* A)
	{
		if (!A) return TEXT("none");
		if (A == R.Player()) return TEXT("player");
		if (Cast<AWWOCharacter>(A)) return TEXT("bot");
		return A->GetClass()->GetName();
	}
}

void FWWORegress::AddCombatScenarios(TArray<FWWORegressScenario>& Out, const UWWOLoadoutSubsystem& L)
{
	// --- (g) Melee: a punch at each kind of target from inside reach, inside lunge range, and beyond ---
	// Kit variants, from inside reach: Thornfist up (its discharge, with two dummies beside the
	// target), Static Charge armed (its chain, with two more targets in range), Melee Guard.
	struct FMeleeKit { const TCHAR* Name; FName Flora; FName Ability; };
	const TArray<FMeleeKit> Kits = { { TEXT("plain"), NAME_None, NAME_None }, { TEXT("thornfist"), "voltvine", "thornfist" },
		{ TEXT("static_charge"), "voltvine", "static_charge" }, { TEXT("melee_guard"), "voltvine", "melee_guard" } };
	for (const FMeleeKit& Kit : Kits)
	{
		for (const FName Kind : { FName("dummy"), FName("soldier"), FName("swarm"), FName("foe"), FName("mate") })
		{
			for (const float Range : { 1.5f, 3.2f, 5.f })
			{
				if (!Kit.Ability.IsNone() && Range != 1.5f) continue;
				FWWORegressScenario S;
				S.Name = FString::Printf(TEXT("melee.%s.%s.%.1fm"), Kit.Name, *Kind.ToString(), Range);
				S.bPvP = Kind == "foe" || Kind == "mate";
				S.MaxTicks = Ticks(4.f);
				TSharedRef<FCombatState> St = MakeShared<FCombatState>();
				const FName Flora = Kit.Flora, Ability = Kit.Ability;
				S.Setup = [St, Kind, Range, Flora, Ability](FWWORegress& R)
				{
					*St = FCombatState();
					if (!Flora.IsNone()) R.EquipFlora(Flora);
					if (!Ability.IsNone()) R.EquipAbility(0, Ability);
					St->Target = SpawnTarget(R, Kind, R.Ahead(Range));
					if (Ability == "thornfist")
					{
						St->Others.Add(R.SpawnDummy(R.Ahead(Range + 0.6f, 0.8f), 1000.f));
						St->Others.Add(R.SpawnDummy(R.Ahead(Range + 0.6f, -0.8f), 1000.f));
					}
					if (Ability == "static_charge")
					{
						const FName Other = Kind == "foe" || Kind == "mate" ? Kind : (Kind == "dummy" ? FName("dummy") : FName("soldier"));
						St->Others.Add(SpawnTarget(R, Other, R.Ahead(Range + 3.f, 2.f)));
						St->Others.Add(SpawnTarget(R, Other, R.Ahead(Range + 5.f, -2.f)));
					}
				};
				S.Step = [St, Ability](FWWORegress& R, int32 Tick)
				{
					AWWOCharacter* P = R.Player();
					AActor* T = St->Target.Get();
					UWWOAbilityComponent* K = P->Abilities;
					if (Tick == 0)
					{
						if (T) R.AimAt(BodyOf(T));
						if (Ability == "thornfist") R.Press(EWWOAction::Ability1);
						// Charged the game's way, by taking its charge damage; it arms itself the next tick.
						if (Ability == "static_charge") if (UWWOAbilityData* A = K->Equipped("static_charge")) K->OnDamageTaken(A->ChargeDamage);
					}
					if (Tick == 3)
					{
						R.Record(TEXT("melee_mult"), K->MeleeMultiplier());
						if (UWWOAbilityData* A = K->Equipped("static_charge")) R.RecordBool(TEXT("armed"), K->FindState(A->Id) && K->FindState(A->Id)->bArmed);
						St->Before = FWWORegress::HealthOf(T);
						St->OthersBefore = OthersHealth(*St);
						St->From = P->GetActorLocation();
						R.Press(EWWOAction::Melee);
					}
					if (Tick > 4 && T) St->PeakSpeed = FMath::Max(St->PeakSpeed, (float)T->GetVelocity().Size2D());
					if (Tick == 4 + Ticks(1.5f))
					{
						const FVector Moved = P->GetActorLocation() - St->From;
						R.Record(TEXT("dealt"), St->Before - FWWORegress::HealthOf(T));
						R.RecordBool(TEXT("killed"), T && Cast<IWWOTargetable>(T)->IsDead());
						R.Record(TEXT("lunge_m"), (float)Moved.Size2D() / 100.f);
						R.Record(TEXT("knockback_peak"), St->PeakSpeed);
						if (T) R.RecordVec(TEXT("target_at"), T->GetActorLocation());
						R.Record(TEXT("confirms"), R.Confirms);
						R.Record(TEXT("hits_marked"), UWWOWorldSubsystem::HitsMarked - R.ShotsMarkedSince);
						if (St->Others.Num() > 0) R.Record(TEXT("others_dealt"), St->OthersBefore - OthersHealth(*St));
						R.Record(TEXT("guard_left"), K->GuardTimeLeft());
						R.Record(TEXT("incoming_melee_mult"), K->IncomingMeleeMultiplier());
						if (UWWOAbilityData* A = K->Equipped("static_charge")) R.RecordBool(TEXT("armed_after"), K->FindState(A->Id) && K->FindState(A->Id)->bArmed);
						return true;
					}
					return false;
				};
				Out.Add(S);
			}
		}
	}

	// --- (h) The damage pipeline ---
	// Shield first, then health: a player-made hit of each size on an other-team player.
	for (const float Amount : { 50.f, 100.f, 150.f, 250.f })
	{
		FWWORegressScenario S;
		S.Name = FString::Printf(TEXT("damage.split.%.0f"), Amount);
		S.bPvP = true;
		TSharedRef<FCombatState> St = MakeShared<FCombatState>();
		S.Setup = [St](FWWORegress& R) { *St = FCombatState(); St->Target = R.SpawnBot(1, R.Ahead(10.f)); };
		S.Step = [St, Amount](FWWORegress& R, int32 Tick)
		{
			AWWOCharacter* Foe = Cast<AWWOCharacter>(St->Target.Get());
			if (!Foe) return true;
			if (Tick == 0)
			{
				R.NotePlayerShot();
				FWWODamageBy By(R.Player());
				R.RecordBool(TEXT("killed_now"), Foe->TakeHit(Amount, false, BodyOf(Foe)));
				R.Record(TEXT("texts_shown"), UWWOWorldSubsystem::Get(R.World())->FloatingTexts().Num() - R.TextsAtStart);
				R.Record(TEXT("hits_marked"), UWWOWorldSubsystem::HitsMarked - R.ShotsMarkedSince);
			}
			if (Tick == 2)
			{
				R.Record(TEXT("shield"), Foe->Health->Shield);
				R.Record(TEXT("health"), Foe->Health->Health);
				R.RecordBool(TEXT("dead"), Foe->Health->bDead);
				R.Record(TEXT("credit"), Who(R, Foe->Health->LastAttacker.Get()));
				return true;
			}
			return false;
		};
		Out.Add(S);
	}
	// Damage numbers and the hit marker by who dealt a 40-point hit on a dummy.
	for (const TCHAR* By : { TEXT("player"), TEXT("bot"), TEXT("nobody"), TEXT("enemy") })
	{
		FWWORegressScenario S;
		S.Name = FString::Printf(TEXT("damage.numbers.by_%s"), By);
		S.bPvP = FString(By) == TEXT("bot");
		TSharedRef<FCombatState> St = MakeShared<FCombatState>();
		const FString ByName = By;
		S.Setup = [St, ByName](FWWORegress& R)
		{
			*St = FCombatState();
			St->Target = R.SpawnDummy(R.Ahead(10.f), 1000.f);
			if (ByName == TEXT("bot")) St->Others.Add(R.SpawnBot(1, R.Ahead(5.f, 3.f)));
			if (ByName == TEXT("enemy")) St->Others.Add(R.SpawnEnemy("soldier", R.Ahead(5.f, 3.f)));
		};
		S.Step = [St, ByName](FWWORegress& R, int32 Tick)
		{
			AActor* T = St->Target.Get();
			if (!T) return true;
			R.NotePlayerShot();
			const float Before = FWWORegress::HealthOf(T);
			{
				const UObject* Attacker = ByName == TEXT("player") ? (UObject*)R.Player() : (St->Others.Num() > 0 ? (UObject*)St->Others[0].Get() : nullptr);
				FWWODamageBy Scope(Attacker);
				Cast<IWWOTargetable>(T)->TakeHit(40.f, false, BodyOf(T));
			}
			R.Record(TEXT("dealt"), Before - FWWORegress::HealthOf(T));
			R.Record(TEXT("texts_shown"), UWWOWorldSubsystem::Get(R.World())->FloatingTexts().Num() - R.TextsAtStart);
			R.Record(TEXT("hits_marked"), UWWOWorldSubsystem::HitsMarked - R.ShotsMarkedSince);
			R.Record(TEXT("confirms"), R.Confirms);
			return true;
		};
		Out.Add(S);
	}
	// Who may hurt whom, PvE and PvP: CanHurt for every pair (and for an actor the player owns, as a
	// grenade is), and who is in the player's targets list.
	for (const bool bPvP : { false, true })
	{
		FWWORegressScenario S;
		S.Name = FString::Printf(TEXT("damage.can_hurt.%s"), bPvP ? TEXT("pvp") : TEXT("pve"));
		S.bPvP = bPvP;
		TSharedRef<FCombatState> St = MakeShared<FCombatState>();
		S.Setup = [St](FWWORegress& R)
		{
			*St = FCombatState();
			St->Others.Add(R.SpawnBot(0, R.Ahead(6.f, 3.f)));
			St->Others.Add(R.SpawnBot(1, R.Ahead(6.f, -3.f)));
			St->Others.Add(R.SpawnEnemy("soldier", R.Ahead(10.f)));
			St->Others.Add(R.SpawnAlly(R.Ahead(4.f, 4.f)));
			St->Others.Add(R.SpawnDummy(R.Ahead(12.f, 4.f), 1000.f));
		};
		S.Step = [St](FWWORegress& R, int32 Tick)
		{
			if (Tick < 2) return false;
			AWWOCharacter* Me = R.Player();
			AActor* Kit = R.World()->SpawnActor<AActor>(AActor::StaticClass(), FTransform(R.Ahead(1.f)), WWO::AlwaysSpawn(Me));
			const TArray<TPair<FString, AActor*>> Attackers = { { TEXT("player"), Me }, { TEXT("kit"), Kit }, { TEXT("mate"), St->Others[0].Get() },
				{ TEXT("foe"), St->Others[1].Get() }, { TEXT("enemy"), St->Others[2].Get() } };
			const TArray<TPair<FString, AActor*>> Victims = { { TEXT("player"), Me }, { TEXT("mate"), St->Others[0].Get() }, { TEXT("foe"), St->Others[1].Get() },
				{ TEXT("enemy"), St->Others[2].Get() }, { TEXT("ally"), St->Others[3].Get() }, { TEXT("dummy"), St->Others[4].Get() } };
			for (const auto& A : Attackers)
			{
				for (const auto& V : Victims) R.RecordBool(TEXT("can.") + A.Key + TEXT(">") + V.Key, UWWOWorldSubsystem::CanHurt(A.Value, V.Value));
			}
			TArray<FString> Listed;
			for (AActor* T : UWWOWorldSubsystem::Get(R.World())->LivingTargets(Me)) Listed.Add(Who(R, T));
			Listed.Sort();
			R.Record(TEXT("targets"), FString::Join(Listed, TEXT("|")));
			if (Kit) Kit->Destroy();
			return true;
		};
		Out.Add(S);
	}
	// Kill credit: the player hits an other-team player, then it dies to nobody's damage 1 s or 11 s later.
	for (const float Later : { 1.f, 11.f })
	{
		FWWORegressScenario S;
		S.Name = FString::Printf(TEXT("damage.kill_credit.%.0fs"), Later);
		S.bPvP = true;
		S.MaxTicks = Ticks(Later + 2.f);
		TSharedRef<FCombatState> St = MakeShared<FCombatState>();
		S.Setup = [St](FWWORegress& R) { *St = FCombatState(); St->Target = R.SpawnBot(1, R.Ahead(10.f)); };
		S.Step = [St, Later](FWWORegress& R, int32 Tick)
		{
			AWWOCharacter* Foe = Cast<AWWOCharacter>(St->Target.Get());
			if (!Foe) return true;
			if (Tick == 0)
			{
				FWWODamageBy By(R.Player());
				Foe->TakeHit(150.f, true, BodyOf(Foe));
			}
			if (Tick == Ticks(Later))
			{
				Foe->TakeHit(500.f, false, BodyOf(Foe)); // nobody's
				const double Age = R.World()->GetTimeSeconds() - Foe->Health->LastAttackTime;
				R.RecordBool(TEXT("dead"), Foe->Health->bDead);
				R.Record(TEXT("last_attacker"), Who(R, Foe->Health->LastAttacker.Get()));
				R.Record(TEXT("last_hit_age"), FString::Printf(TEXT("%.3f"), Age));
				R.RecordBool(TEXT("last_hit_critical"), Foe->Health->bLastAttackCritical);
				R.RecordBool(TEXT("credit_by_10s_rule"), Foe->Health->LastAttacker.Get() == R.Player() && Age <= 10.0);
				return true;
			}
			return false;
		};
		Out.Add(S);
	}
	// A gun at players' hitboxes: the rifle and the hand cannon at an other-team player's body and
	// head, and at a same-team player (no friendly fire).
	for (const FName Gun : { FName("rifle"), FName("handcannon") })
	{
		for (const FName Kind : { FName("foe"), FName("mate") })
		{
			for (const bool bHead : { false, true })
			{
				FWWORegressScenario S;
				S.Name = FString::Printf(TEXT("damage.gun_at_player.%s.%s.%s"), *Gun.ToString(), *Kind.ToString(), bHead ? TEXT("head") : TEXT("body"));
				S.bPvP = true;
				TSharedRef<FCombatState> St = MakeShared<FCombatState>();
				S.Setup = [St, Gun, Kind](FWWORegress& R)
				{
					*St = FCombatState();
					R.EquipWeapon(Gun);
					St->Target = SpawnTarget(R, Kind, R.Ahead(10.f));
				};
				S.Step = [St, bHead](FWWORegress& R, int32 Tick)
				{
					AWWOCharacter* T = Cast<AWWOCharacter>(St->Target.Get());
					if (!T) return true;
					if (Tick == 0)
					{
						FVector Aim = T->BodyCenter();
						for (const FWWOAimPart& Part : T->GetAimParts()) if (Part.bCritical == bHead) { Aim = Part.Position; break; }
						R.AimAt(Aim);
						R.RecordBool(TEXT("hitboxes_on"), T->bHitboxesOn);
						R.RecordVec(TEXT("aim_at"), Aim);
						St->Before = FWWORegress::HealthOf(T);
						R.Press(EWWOAction::Fire);
						R.Hold(EWWOAction::Fire, true);
					}
					if (Tick == 1) R.Hold(EWWOAction::Fire, false);
					if (Tick == 20)
					{
						R.Record(TEXT("dealt"), St->Before - FWWORegress::HealthOf(T));
						R.Record(TEXT("shield"), T->Health->Shield);
						R.Record(TEXT("health"), T->Health->Health);
						R.Record(TEXT("crits"), R.ConfirmCrits);
						R.Record(TEXT("confirms"), R.Confirms);
						R.Record(TEXT("credit"), Who(R, T->Health->LastAttacker.Get()));
						return true;
					}
					return false;
				};
				Out.Add(S);
			}
		}
	}

	// --- (i) Enemies ---
	// Each type as it spawns: health, shield, its aim parts.
	for (const FName Kind : { FName("soldier"), FName("swarm"), FName("tank") })
	{
		FWWORegressScenario S;
		S.Name = FString::Printf(TEXT("enemy.%s.stats"), *Kind.ToString());
		TSharedRef<FCombatState> St = MakeShared<FCombatState>();
		S.Setup = [St, Kind](FWWORegress& R) { *St = FCombatState(); St->Target = R.SpawnEnemy(Kind, R.Ahead(10.f)); };
		S.Step = [St](FWWORegress& R, int32 Tick)
		{
			AWWOEnemy* E = Cast<AWWOEnemy>(St->Target.Get());
			if (!E) return true;
			R.Record(TEXT("health"), E->GetHealth());
			R.Record(TEXT("max_health"), E->GetMaxHealth());
			R.Record(TEXT("shield"), E->GetShield());
			R.Record(TEXT("max_shield"), E->GetMaxShield());
			R.Record(TEXT("rank_and_file"), E->IsRankAndFile() ? 1 : 0);
			const TArray<FWWOAimPart> Parts = E->GetAimParts();
			R.Record(TEXT("parts"), Parts.Num());
			for (int32 i = 0; i < Parts.Num(); ++i)
			{
				R.Record(FString::Printf(TEXT("part%d.radius"), i), Parts[i].Radius);
				R.Record(FString::Printf(TEXT("part%d.crit"), i), Parts[i].bCritical ? 1 : 0);
				R.RecordVec(FString::Printf(TEXT("part%d.at"), i), Parts[i].Position);
			}
			return true;
		};
		Out.Add(S);
	}
	// The Tank's shield: a player-made hit of three sizes, then 3 s and 8 s later (its regen).
	for (const float Amount : { 100.f, 300.f, 450.f })
	{
		FWWORegressScenario S;
		S.Name = FString::Printf(TEXT("enemy.tank.shield.%.0f"), Amount);
		S.MaxTicks = Ticks(10.f);
		TSharedRef<FCombatState> St = MakeShared<FCombatState>();
		S.Setup = [St](FWWORegress& R) { *St = FCombatState(); St->Target = R.SpawnEnemy("tank", R.Ahead(10.f)); };
		S.Step = [St, Amount](FWWORegress& R, int32 Tick)
		{
			AWWOEnemy* E = Cast<AWWOEnemy>(St->Target.Get());
			if (!E) return true;
			if (Tick == 0)
			{
				FWWODamageBy By(R.Player());
				E->TakeHit(Amount, false, E->BodyCenter());
				R.Record(TEXT("shield"), E->GetShield());
				R.Record(TEXT("health"), E->GetHealth());
			}
			if (Tick == Ticks(3.f)) R.Record(TEXT("shield_3s"), E->GetShield());
			if (Tick == Ticks(8.f))
			{
				R.Record(TEXT("shield_8s"), E->GetShield());
				R.Record(TEXT("health_8s"), E->GetHealth());
				return true;
			}
			return false;
		};
		Out.Add(S);
	}
	// Attacks, AI on (the one family where enemies act): the player standing still with no regen, a
	// Soldier at 18 m, a Swarm at 1.5 m, a Tank at 3 m. What the player takes in 6 s.
	for (const FName Kind : { FName("soldier"), FName("swarm"), FName("tank") })
	{
		FWWORegressScenario S;
		S.Name = FString::Printf(TEXT("enemy.%s.attacks"), *Kind.ToString());
		S.MaxTicks = Ticks(8.f);
		TSharedRef<FCombatState> St = MakeShared<FCombatState>();
		S.Setup = [St, Kind](FWWORegress& R)
		{
			*St = FCombatState();
			const float Range = Kind == "soldier" ? 18.f : (Kind == "swarm" ? 1.5f : 3.f);
			St->Target = R.SpawnEnemy(Kind, R.Ahead(Range)); // inert through the settle ticks; on from tick 0
			R.Tune(R.Player()->Health, "RechargeDelay", 100.f);
		};
		S.Step = [St](FWWORegress& R, int32 Tick)
		{
			AWWOCharacter* P = R.Player();
			AWWOEnemy* E = Cast<AWWOEnemy>(St->Target.Get());
			if (Tick == 0)
			{
				St->Before = FWWORegress::HealthOf(P);
				UWWOLoadoutSubsystem* L = R.Loadout();
				UWWOEnemyData* Data = Cast<AWWOSoldier>(E) ? (UWWOEnemyData*)L->SoldierData : (Cast<AWWOTank>(E) ? (UWWOEnemyData*)L->TankData : (UWWOEnemyData*)L->SwarmData);
				R.Tune(Data, "AiEnabled", 1.f);
			}
			if (Tick == Ticks(1.f)) R.Record(TEXT("taken_1s"), St->Before - FWWORegress::HealthOf(P));
			if (Tick == Ticks(3.f)) R.Record(TEXT("taken_3s"), St->Before - FWWORegress::HealthOf(P));
			if (Tick == Ticks(6.f))
			{
				R.Record(TEXT("taken_6s"), St->Before - FWWORegress::HealthOf(P));
				R.Record(TEXT("player_shield"), P->Health->Shield);
				R.Record(TEXT("player_health"), P->Health->Health);
				R.Record(TEXT("player_slow_left"), P->SlowLeft);
				R.Record(TEXT("damage_marks"), P->Health->DamageMarks.Num());
				if (E)
				{
					R.RecordVec(TEXT("enemy_at"), E->GetActorLocation());
					R.Record(TEXT("enemy_state"), E->DebugState().Replace(TEXT(" "), TEXT("_")));
				}
				int32 Shots = 0;
				for (TActorIterator<AWWOEnemyProjectile> It(R.World()); It; ++It) ++Shots;
				R.Record(TEXT("projectiles_in_air"), Shots);
				return true;
			}
			return false;
		};
		Out.Add(S);
	}
	// Below KillZ: an inert Soldier put past the floor's edge falls and dies.
	{
		FWWORegressScenario S;
		S.Name = TEXT("enemy.soldier.killz");
		S.MaxTicks = Ticks(8.f);
		TSharedRef<FCombatState> St = MakeShared<FCombatState>();
		S.Setup = [St](FWWORegress& R) { *St = FCombatState(); St->Target = R.SpawnEnemy("soldier", R.Ahead(-22.f)); };
		S.Step = [St](FWWORegress& R, int32 Tick)
		{
			AWWOEnemy* E = Cast<AWWOEnemy>(St->Target.Get());
			if (E && E->IsDead() && St->Mark < 0) St->Mark = Tick;
			if (Tick == Ticks(6.f))
			{
				R.Record(TEXT("died_tick"), St->Mark);
				R.RecordBool(TEXT("dead"), E && E->IsDead());
				R.Record(TEXT("kill_height"), UWWOHealthComponent::MapKillHeight(R.World()));
				return true;
			}
			return false;
		};
		Out.Add(S);
	}
	// An AI teammate hit by an enemy (shield first, then down).
	for (const float Amount : { 60.f, 150.f, 250.f })
	{
		FWWORegressScenario S;
		S.Name = FString::Printf(TEXT("ally.hit.%.0f"), Amount);
		TSharedRef<FCombatState> St = MakeShared<FCombatState>();
		S.Setup = [St](FWWORegress& R) { *St = FCombatState(); St->Target = R.SpawnAlly(R.Ahead(6.f)); };
		S.Step = [St, Amount](FWWORegress& R, int32 Tick)
		{
			AWWOAlly* A = Cast<AWWOAlly>(St->Target.Get());
			if (!A) return true;
			if (Tick == 0)
			{
				R.Record(TEXT("health_start"), A->Health);
				R.Record(TEXT("shield_start"), A->Shield);
				A->TakeAllyDamage(Amount);
			}
			if (Tick == 2)
			{
				R.Record(TEXT("health"), A->Health);
				R.Record(TEXT("shield"), A->Shield);
				R.RecordBool(TEXT("down"), A->IsDown());
				R.Record(TEXT("down_left"), A->DownLeft);
				return true;
			}
			return false;
		};
		Out.Add(S);
	}

	// --- What the view decides: the low reticle's angle (from the viewport's shape), the target under
	// the reticle, what is marked (health bars, the outline flash) and which teammates are in sight.
	// Worked out from the screen, so this is the scenario most likely to differ without rendering.
	for (const bool bAds : { false, true })
	{
		FWWORegressScenario S;
		S.Name = FString::Printf(TEXT("view.reticle_and_marks.%s"), bAds ? TEXT("ads") : TEXT("hip"));
		S.bPvP = true;
		// Without rendering the viewport has no size: no reticle drop, and nothing is marked (marking
		// works in HUD pixels on the screen).
		S.Modes = EWWORegressModes::PerMode;
		S.ModeReason = TEXT("screen-space: the reticle drop and marking need the viewport");
		TSharedRef<FCombatState> St = MakeShared<FCombatState>();
		S.Setup = [St](FWWORegress& R)
		{
			*St = FCombatState();
			St->Target = R.SpawnBot(1, R.Ahead(12.f));
			St->Others.Add(R.SpawnBot(0, R.Ahead(8.f, 4.f)));
			St->Others.Add(R.SpawnEnemy("soldier", R.Ahead(20.f, -1.5f)));
		};
		S.Step = [St, bAds](FWWORegress& R, int32 Tick)
		{
			AWWOCharacter* P = R.Player();
			if (Tick == 0)
			{
				if (AActor* T = St->Target.Get()) R.AimAt(BodyOf(T));
				if (bAds) R.Hold(EWWOAction::Aim, true);
			}
			if (Tick < 40) return false;
			const UWWOWeaponComponent* G = P->Weapon;
			R.Record(TEXT("reticle_drop_deg"), FMath::RadiansToDegrees(P->GetReticleDropAngle()));
			R.Record(TEXT("fov"), P->Camera ? P->Camera->FieldOfView : 0.f);
			R.Record(TEXT("under_reticle"), Who(R, G->ReticleTarget.Get()));
			R.RecordBool(TEXT("target_under_reticle"), G->bTargetUnderReticle);
			TArray<FString> Marked;
			for (const auto& Pair : G->Marked) Marked.Add(Who(R, Pair.Key.Get()));
			Marked.Sort();
			R.Record(TEXT("marked"), Marked.Num() > 0 ? FString::Join(Marked, TEXT("|")) : FString(TEXT("-")));
			R.Record(TEXT("mates_in_sight"), G->MatesInSight.Num());
			FVector2D Sight;
			float Roll = 0.f;
			R.RecordBool(TEXT("sight_on_screen"), G->SightOnScreen(Sight, Roll));
			R.Hold(EWWOAction::Aim, false);
			return true;
		};
		Out.Add(S);
	}
}
