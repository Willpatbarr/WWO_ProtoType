// Regression scenarios: kit. Every grenade asset set off at a fixed point beside an inert enemy, an
// other-team player, a same-team player and an AI teammate; every ability and each flora's ult; the
// flora effects (pools, fog, patches, poison, chain lightning, walls) on each kind of victim.

#include "Player/WWORegress.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOAbilityComponent.h"
#include "Player/WWOGrenadeComponent.h"
#include "Player/WWOUltComponent.h"
#include "Game/WWOGameState.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "Weapons/WWOGrenade.h"
#include "Flora/WWOFloraEffects.h"
#include "Flora/WWOSporeFog.h"
#include "Enemies/WWOEnemy.h"
#include "Allies/WWOAlly.h"
#include "World/WWOTarget.h"
#include "Data/WWOData.h"
#include "EngineUtils.h"

namespace
{
	// A status timer, to 0.1 s: which of two actors ticks first in a frame is the engine's choice (the
	// order of its tick set), and can change with what ran before, so the exact tick it was applied
	// on is not a stable number.
	FString Timer(float Seconds) { return FString::Printf(TEXT("%.1f"), Seconds); }

	// Who stands near the effect, by role.
	struct FVictim
	{
		FString Role;
		TWeakObjectPtr<AActor> Actor;
		float Start = 0.f, AtMark = 0.f;
		float Last = 0.f, Sum = 0.f; // every tick: the damage it took in all (regen and respawns don't hide it)
		bool bDied = false;
	};
	struct FKitState
	{
		TArray<FVictim> Victims;
		FVector Point = FVector::ZeroVector;
		int32 BoomTick = -1;
		int32 SeenGrenades = 0;
		TSet<TWeakObjectPtr<AActor>> Grenades;
		FVector LastGrenadeAt = FVector::ZeroVector;
		float PlayerBefore = 0.f;
		FVector PlayerFrom = FVector::ZeroVector;
	};

	// The sets of victims a kit scenario is run against.
	enum class EVictims : uint8 { Enemy, Foe, Mate, Ally };
	const TCHAR* VictimsName(EVictims V)
	{
		switch (V) { case EVictims::Enemy: return TEXT("enemy"); case EVictims::Foe: return TEXT("foe"); case EVictims::Mate: return TEXT("mate"); default: return TEXT("ally"); }
	}
	bool IsPvPSet(EVictims V) { return V == EVictims::Foe || V == EVictims::Mate; }

	// Enemies: one 1 m beside the point and one at 90% of RadiusM on the other side. Players and
	// allies: one 1.5 m beside it.
	void PlaceVictims(FWWORegress& R, FKitState& St, EVictims V, const FVector& Point, float RadiusM, float NearM = 1.5f)
	{
		const FVector Side = FRotationMatrix(FRotator(0.f, R.HomeYaw(), 0.f)).GetUnitAxis(EAxis::Y);
		const FVector Near = Point + Side * NearM * WWO::M;
		switch (V)
		{
		case EVictims::Enemy:
			St.Victims.Add({ TEXT("enemy_near"), R.SpawnEnemy("soldier", Point + Side * FMath::Min(NearM, 1.f) * WWO::M) });
			St.Victims.Add({ TEXT("enemy_edge"), R.SpawnEnemy("soldier", Point - Side * RadiusM * 0.9f * WWO::M) });
			break;
		case EVictims::Foe: St.Victims.Add({ TEXT("foe"), R.SpawnBot(1, Near) }); break;
		case EVictims::Mate: St.Victims.Add({ TEXT("mate"), R.SpawnBot(0, Near) }); break;
		case EVictims::Ally: St.Victims.Add({ TEXT("ally"), R.SpawnAlly(Near) }); break;
		}
	}

	void MarkVictims(FKitState& St, bool bStart)
	{
		for (FVictim& V : St.Victims)
		{
			(bStart ? V.Start : V.AtMark) = FWWORegress::HealthOf(V.Actor.Get());
			if (bStart) { V.Last = V.Start; V.Sum = 0.f; V.bDied = false; }
		}
	}

	// Once a tick: what each victim lost since the last tick, and whether it went down.
	void FollowVictims(FKitState& St)
	{
		for (FVictim& V : St.Victims)
		{
			const AActor* A = V.Actor.Get();
			const float Now = FWWORegress::HealthOf(A);
			if (Now < V.Last) V.Sum += V.Last - Now;
			V.Last = Now;
			const IWWOTargetable* T = Cast<IWWOTargetable>(A);
			const AWWOAlly* Al = Cast<AWWOAlly>(A);
			V.bDied |= !A || (T && T->IsDead()) || (Al && Al->IsDown());
		}
	}

	void RecordSums(FWWORegress& R, const FKitState& St)
	{
		for (const FVictim& V : St.Victims)
		{
			R.Record(V.Role + TEXT(".taken_all"), V.Sum);
			R.RecordBool(V.Role + TEXT(".went_down"), V.bDied);
		}
	}

	// What happened to each victim: damage by the mark and in all, its health and shield, whose last
	// hit it was, and (bStatus) what it has on it now.
	void RecordVictims(FWWORegress& R, const FKitState& St, bool bStatus)
	{
		for (const FVictim& V : St.Victims)
		{
			AActor* A = V.Actor.Get();
			const float Now = FWWORegress::HealthOf(A);
			R.Record(V.Role + TEXT(".first"), V.Start - V.AtMark);
			R.Record(V.Role + TEXT(".total"), V.Start - Now);
			const IWWOTargetable* T = Cast<IWWOTargetable>(A);
			const AWWOAlly* Al = Cast<AWWOAlly>(A);
			R.RecordBool(V.Role + TEXT(".dead"), !A || (T && T->IsDead()) || (Al && Al->IsDown()));
			if (const AWWOCharacter* C = Cast<AWWOCharacter>(A))
			{
				R.Record(V.Role + TEXT(".health"), C->Health->Health);
				R.Record(V.Role + TEXT(".shield"), C->Health->Shield);
				R.Record(V.Role + TEXT(".credit"), C->Health->LastAttacker.Get() == R.Player() ? TEXT("player") : (C->Health->LastAttacker.IsValid() ? TEXT("other") : TEXT("none")));
			}
			if (Al)
			{
				R.Record(V.Role + TEXT(".health"), Al->Health);
				R.Record(V.Role + TEXT(".shield"), Al->Shield);
			}
		}
		if (!bStatus) return;
		for (const FVictim& V : St.Victims)
		{
			AActor* A = V.Actor.Get();
			if (const AWWOEnemy* E = Cast<AWWOEnemy>(A))
			{
				R.Record(V.Role + TEXT(".slow"), E->SlowLeft > 0.f ? E->SlowAmount : 0.f);
				R.Record(V.Role + TEXT(".slow_left"), Timer(E->SlowLeft));
				R.Record(V.Role + TEXT(".blind_left"), Timer(E->BlindLeft));
			}
			if (const AWWOCharacter* C = Cast<AWWOCharacter>(A))
			{
				R.Record(V.Role + TEXT(".slow"), C->SlowLeft > 0.f ? C->SlowAmount : 0.f);
				R.Record(V.Role + TEXT(".slow_left"), Timer(C->SlowLeft));
				R.Record(V.Role + TEXT(".blind_left"), Timer(C->BlindLeft));
			}
			R.RecordBool(V.Role + TEXT(".poisoned"), A && A->FindComponentByClass<UWWODamageOverTime>() != nullptr);
		}
	}

	template <typename T> int32 CountOf(UWorld* W)
	{
		int32 N = 0;
		for (TActorIterator<T> It(W); It; ++It) if (!It->IsActorBeingDestroyed()) ++N;
		return N;
	}

	// Every grenade in the world (thrown in the scenario): new ones are counted; the tick the last
	// one goes is the blast.
	void TrackGrenades(FWWORegress& R, FKitState& St, int32 Tick)
	{
		bool bAny = false;
		for (TActorIterator<AWWOGrenade> It(R.World()); It; ++It)
		{
			if (It->IsActorBeingDestroyed() || It->IsHidden()) continue;
			if (!St.Grenades.Contains(*It)) { St.Grenades.Add(*It); ++St.SeenGrenades; }
			St.LastGrenadeAt = It->GetActorLocation();
			bAny = true;
		}
		if (!bAny && St.SeenGrenades > 0 && St.BoomTick < 0) St.BoomTick = Tick;
	}

	FString WallLine(FWWORegress& R)
	{
		TArray<FString> Walls;
		for (TActorIterator<AWWOBarkWall> It(R.World()); It; ++It)
		{
			const FVector D = It->GetActorLocation() - R.Home();
			Walls.Add(FString::Printf(TEXT("%.1f,%.1f,%.1f@%.1f"), D.X, D.Y, D.Z, It->GetActorRotation().Yaw));
		}
		return Walls.Num() > 0 ? FString::Join(Walls, TEXT("|")) : FString(TEXT("-"));
	}
}

void FWWORegress::AddKitScenarios(TArray<FWWORegressScenario>& Out, const UWWOLoadoutSubsystem& L)
{
	const TArray<EVictims> AllSets = { EVictims::Enemy, EVictims::Foe, EVictims::Mate, EVictims::Ally };

	// --- (d) Every grenade asset, set off 10 m ahead, beside each set of victims ---
	// The grenade comes out of the thrower's own code (UWWOGrenadeComponent::ThrowFrom builds the spec
	// from the loadout) 30 cm above the point, straight down and slow (the throw distance tuned to
	// 5 cm for the scenario), so it settles on the point and goes off as it would anywhere.
	for (const UWWOGrenadeData* G : L.Grenades)
	{
		if (!G) continue;
		const FName Id = G->Id, Flora = G->Flora;
		const float Radius = G->BlastRadius;
		// The Poison Spike lying still only counts a body within reach of it as a touch: 0.6 m from an
		// enemy, 0.45 m from a player. (At 0.6 m a player's body is just out of reach, and where the
		// spike then rolls follows the player's idle animation, whose phase depends on what ran before.)
		const float NearM = G->Kind == EWWOGrenadeKind::Spike ? 0.6f : 1.5f;
		const float NearPlayerM = G->Kind == EWWOGrenadeKind::Spike ? 0.45f : 1.5f;
		const bool bLingers = G->Kind == EWWOGrenadeKind::Pool || G->Kind == EWWOGrenadeKind::Spike;
		for (const EVictims Set : AllSets)
		{
			FWWORegressScenario S;
			S.Name = FString::Printf(TEXT("grenade.%s.%s"), *Id.ToString(), VictimsName(Set));
			S.bPvP = IsPvPSet(Set);

			S.MaxTicks = Ticks(20.f);
			TSharedRef<FKitState> St = MakeShared<FKitState>();
			S.Setup = [St, Id, Flora, Radius, Set, NearM, NearPlayerM](FWWORegress& R)
			{
				*St = FKitState();
				R.EquipFlora(Flora);
				R.EquipGrenade(Id);
				St->Point = R.Ahead(10.f);
				PlaceVictims(R, *St, Set, St->Point, Radius, IsPvPSet(Set) ? NearPlayerM : NearM);
				R.Tune(R.Player()->Grenades, "ThrowDistance", 0.05f);
			};
			S.Step = [St, bLingers](FWWORegress& R, int32 Tick)
			{
				AWWOCharacter* P = R.Player();
				if (Tick == 0)
				{
					MarkVictims(*St, true);
					P->Grenades->ThrowFrom(St->Point + FVector(0, 0, 30.f), FVector(0, 0, -1.f));
				}
				TrackGrenades(R, *St, Tick);
				FollowVictims(*St);
				if (St->BoomTick >= 0 && Tick == St->BoomTick + 2)
				{
					MarkVictims(*St, false);
					RecordVictims(R, *St, true); // status right after the blast
					R.Record(TEXT("boom_tick"), St->BoomTick);
					R.RecordVec(TEXT("boom_at"), St->LastGrenadeAt);
					R.Record(TEXT("pools"), CountOf<AWWOSporePool>(R.World()));
					R.Record(TEXT("walls"), WallLine(R));
				}
				// What lingers (a pool, poison) plays out over the next 5.5 s, the rest is over in 1 s; one
				// that hasn't gone off by 12 s (a mine nobody walks onto) is recorded as not gone off.
				const bool bDone = (St->BoomTick >= 0 && Tick >= St->BoomTick + Ticks(bLingers ? 5.5f : 1.f)) || Tick >= Ticks(12.f);
				if (!bDone) return false;
				if (St->BoomTick < 0) R.Record(TEXT("boom_tick"), -1);
				RecordSums(R, *St);
				R.Record(TEXT("grenades_seen"), St->SeenGrenades);
				R.Record(TEXT("player_hp"), FWWORegress::HealthOf(P));
				R.Record(TEXT("hits_marked"), UWWOWorldSubsystem::HitsMarked - R.ShotsMarkedSince);
				return true;
			};
			Out.Add(S);
		}
		// The real throw: from the hand, level, through the component's own press and release.
		FWWORegressScenario Throw;
		Throw.Name = FString::Printf(TEXT("grenade.%s.throw"), *Id.ToString());
		// The grenade leaves from a point fixed to the camera (UWWOGrenadeComponent::Release), and the
		// camera is tilted up by the low reticle's angle, worked out from the viewport's shape: with no
		// viewport (no rendering) that angle is 0, the hand is about 5 cm lower, and it lands elsewhere.
		Throw.Modes = EWWORegressModes::PerMode;
		Throw.ModeReason = TEXT("the hand point rides the camera, tilted by the viewport-shaped reticle drop");
		Throw.MaxTicks = Ticks(14.f);
		TSharedRef<FKitState> St = MakeShared<FKitState>();
		Throw.Setup = [St, Id, Flora](FWWORegress& R)
		{
			*St = FKitState();
			R.EquipFlora(Flora);
			R.EquipGrenade(Id);
		};
		Throw.Step = [St](FWWORegress& R, int32 Tick)
		{
			AWWOCharacter* P = R.Player();
			if (Tick == 0)
			{
				R.Record(TEXT("charges_max"), P->Grenades->MaxCharges());
				R.Record(TEXT("throw_distance"), P->Grenades->EffectiveThrowDistance());
				R.Record(TEXT("recharge"), P->Grenades->EffectiveRechargeTime());
				R.Record(TEXT("fuse"), P->Grenades->EffectiveFuse());
				R.Press(EWWOAction::Grenade);
			}
			if (Tick == 5) R.Record(TEXT("charge_after_throw"), P->Grenades->Charge);
			TrackGrenades(R, *St, Tick);
			if (St->BoomTick >= 0 || Tick >= Ticks(12.f))
			{
				R.Record(TEXT("boom_tick"), St->BoomTick);
				R.RecordVec(TEXT("boom_at"), St->LastGrenadeAt);
				R.Record(TEXT("grenades_seen"), St->SeenGrenades);
				R.Record(TEXT("charge_at_boom"), P->Grenades->Charge);
				R.Record(TEXT("walls"), WallLine(R));
				return true;
			}
			return false;
		};
		Out.Add(Throw);
	}

	// --- (e) Abilities ---
	// Each one equipped alone (with its own flora), set off by its button where it has one; a second
	// walked forward; then a grenade set off 4 m ahead (Seedburst, Spore Patches, Shield Share act on
	// it); what it did by 8 s.
	for (const UWWOAbilityData* A : L.Abilities)
	{
		if (!A) continue;
		const FName Id = A->Id, Flora = A->Flora;
		const bool bActive = A->Kind == EWWOAbilityKind::Active;
		FWWORegressScenario S;
		S.Name = FString::Printf(TEXT("ability.%s"), *Id.ToString());
		S.MaxTicks = Ticks(12.f);
		TSharedRef<FKitState> St = MakeShared<FKitState>();
		S.Setup = [St, Id, Flora](FWWORegress& R)
		{
			*St = FKitState();
			if (!Flora.IsNone() && Flora != "none") R.EquipFlora(Flora); // Seedburst belongs to no flora
			R.EquipAbility(0, Id);
			St->Victims.Add({ TEXT("enemy"), R.SpawnEnemy("soldier", R.Ahead(8.f)) });
			if (Id == "shield_share") St->Victims.Add({ TEXT("ally"), R.SpawnAlly(R.Ahead(4.f, 1.5f)) });
			R.EquipGrenade(Id == "shield_share" ? FName("iron_grenade") : FName("volt_grenade"));
			R.Tune(R.Player()->Grenades, "ThrowDistance", 0.05f);
		};
		S.Step = [St, Id, bActive](FWWORegress& R, int32 Tick)
		{
			AWWOCharacter* P = R.Player();
			UWWOAbilityComponent* K = P->Abilities;
			UWWOAbilityData* Ab = K->Equipped(Id);
			if (Tick == 0)
			{
				// An ally just hit (90 off its shield, and its own regen held off for a while): room to refill.
				if (St->Victims.Num() > 1) if (AWWOAlly* Ally = Cast<AWWOAlly>(St->Victims[1].Actor.Get())) Ally->TakeAllyDamage(90.f);
				MarkVictims(*St, true);
				St->PlayerFrom = P->GetActorLocation();
				R.RecordBool(TEXT("equipped"), Ab != nullptr);
				R.Record(TEXT("speed_mult"), K->SpeedMultiplier());
				R.Record(TEXT("extra_jumps"), K->ExtraJumps());
				R.Record(TEXT("extra_charges"), K->ExtraCharges());
				R.Record(TEXT("grenade_max"), P->Grenades->MaxCharges());
				R.Record(TEXT("charge_start"), Ab ? K->Charge(Ab) : -1.f);
				if (bActive) R.Press(EWWOAction::Ability1);
				R.Move(FVector2D(0.f, 1.f));
			}
			if (Tick == 2)
			{
				R.Record(TEXT("charge_used"), Ab ? K->Charge(Ab) : -1.f);
				R.RecordBool(TEXT("effect_on"), Ab && K->IsEffectActive(Ab));
				R.Record(TEXT("effect_left"), K->ActiveLeft.FindRef(Id));
				R.Record(TEXT("melee_mult"), K->MeleeMultiplier());
			}
			if (Tick == 60)
			{
				R.Move(FVector2D::ZeroVector);
				const FVector D = P->GetActorLocation() - St->PlayerFrom;
				R.RecordVec(TEXT("moved_1s"), R.Home() + D);
				R.Record(TEXT("fogs"), CountOf<AWWOSporeFog>(R.World()));
				P->Grenades->ThrowFrom(R.Ahead(4.f) + FVector(0, 0, 30.f), FVector(0, 0, -1.f));
			}
			FollowVictims(*St);
			if (Tick > 60) TrackGrenades(R, *St, Tick);
			if (Tick == 62) R.Record(TEXT("live_seeds"), K->LiveSeeds);
			if (St->BoomTick >= 0 && Tick == St->BoomTick + 2)
			{
				// Right after the (last) blast: patches sprouted, an ally's shield refilled.
				R.Record(TEXT("boom_tick"), St->BoomTick);
				R.Record(TEXT("patches_at_boom"), CountOf<AWWOSporePatch>(R.World()));
				if (St->Victims.Num() > 1) if (const AWWOAlly* Ally = Cast<AWWOAlly>(St->Victims[1].Actor.Get())) R.Record(TEXT("ally_shield_at_boom"), Ally->Shield);
			}
			if (Tick == Ticks(8.f))
			{
				MarkVictims(*St, false);
				RecordVictims(R, *St, false);
				RecordSums(R, *St);
				R.Record(TEXT("grenades_seen"), St->SeenGrenades);
				R.Record(TEXT("patches"), CountOf<AWWOSporePatch>(R.World()));
				R.Record(TEXT("fogs_after"), CountOf<AWWOSporeFog>(R.World()));
				R.Record(TEXT("charge_8s"), Ab ? K->Charge(Ab) : -1.f);
				return true;
			}
			return false;
		};
		Out.Add(S);
	}

	// --- (e) Each flora's ult: on inert enemies at 5 and 10 m and a dummy at 15 m; and in PvP on an
	// other-team and a same-team player at 5 m ---
	for (const UWWOFloraData* F : L.Floras)
	{
		if (!F) continue;
		const FName Flora = F->Id;
		for (const bool bPvP : { false, true })
		{
			FWWORegressScenario S;
			S.Name = FString::Printf(TEXT("ult.%s.%s"), *Flora.ToString(), bPvP ? TEXT("players") : TEXT("enemies"));
			S.bPvP = bPvP;
			S.MaxTicks = Ticks(10.f);
			TSharedRef<FKitState> St = MakeShared<FKitState>();
			S.Setup = [St, Flora, bPvP](FWWORegress& R)
			{
				*St = FKitState();
				R.EquipFlora(Flora);
				if (bPvP)
				{
					St->Victims.Add({ TEXT("foe_5m"), R.SpawnBot(1, R.Ahead(5.f)) });
					St->Victims.Add({ TEXT("mate_5m"), R.SpawnBot(0, R.Ahead(5.f, 2.f)) });
				}
				else
				{
					St->Victims.Add({ TEXT("enemy_5m"), R.SpawnEnemy("soldier", R.Ahead(5.f)) });
					St->Victims.Add({ TEXT("enemy_10m"), R.SpawnEnemy("soldier", R.Ahead(10.f)) });
					St->Victims.Add({ TEXT("dummy_15m"), R.SpawnDummy(R.Ahead(15.f), 5000.f) });
				}
			};
			S.Step = [St](FWWORegress& R, int32 Tick)
			{
				AWWOCharacter* P = R.Player();
				if (Tick == 0)
				{
					MarkVictims(*St, true);
					R.Press(EWWOAction::Ult);
				}
				FollowVictims(*St);
				if (Tick == 2)
				{
					R.RecordBool(TEXT("active"), P->Ult->bActive);
					R.Record(TEXT("charge"), P->Ult->Charge);
				}
				if (Tick == Ticks(3.f))
				{
					// While it lasts: can the player be hurt (an unattributed 30-point hit), is it hidden.
					const float Before = FWWORegress::HealthOf(P);
					P->TakeHit(30.f, false, P->GetActorLocation());
					R.Record(TEXT("hit_taken_3s"), Before - FWWORegress::HealthOf(P));
					R.Record(TEXT("veil_left"), P->VeilLeft);
					R.Record(TEXT("entrench_left"), P->EntrenchLeft);
					R.RecordBool(TEXT("invisible"), P->IsInvisible());
					R.Record(TEXT("veil_melee_mult"), P->VeilMeleeMultiplier());
					R.Record(TEXT("grenade_recharge_mult"), P->EntrenchRechargeMultiplier());
					R.Record(TEXT("fogs"), CountOf<AWWOSporeFog>(R.World()));
					R.Record(TEXT("roots"), CountOf<AWWOEntrenchRoots>(R.World()));
					R.RecordBool(TEXT("ulting"), P->bUlting);
				}
				if (Tick == Ticks(6.f))
				{
					MarkVictims(*St, false);
					RecordVictims(R, *St, false);
					RecordSums(R, *St);
					R.RecordBool(TEXT("active_6s"), P->Ult->bActive);
					R.Record(TEXT("charge_6s"), P->Ult->Charge);
					R.Record(TEXT("hits_marked"), UWWOWorldSubsystem::HitsMarked - R.ShotsMarkedSince);
					return true;
				}
				return false;
			};
			Out.Add(S);
		}
	}

	// --- (f) Flora effects, made by their own factories, owned by the player, 10 m ahead ---
	struct FEffect { FString Name; TFunction<void(FWWORegress&, const FVector&)> Make; float Seconds; };
	TArray<FEffect> Effects;
	for (const UWWOFloraData* F : L.Floras)
	{
		if (!F) continue;
		const FName Flavor = F->Id;
		Effects.Add({ FString::Printf(TEXT("pool_%s"), *Flavor.ToString()), [Flavor](FWWORegress& R, const FVector& At)
		{
			AWWOSporePool::Spawn(R.World(), At, 3.f, 25.f, 5.f, FLinearColor(0.5f, 0.8f, 0.3f), Flavor, R.Player());
		}, 6.f });
	}
	Effects.Add({ TEXT("fog_damage"), [](FWWORegress& R, const FVector& At)
	{
		AWWOSporeFog::SpawnFog(R.World(), At, 4.f, 5.f, false, R.Player()); // Spore Fog Damage is equipped (Setup)
	}, 6.f });
	Effects.Add({ TEXT("fog_plain"), [](FWWORegress& R, const FVector& At)
	{
		AWWOSporeFog::SpawnFog(R.World(), At, 4.f, 5.f, false, R.Player()); // nothing equipped: no harm
	}, 6.f });
	Effects.Add({ TEXT("patch"), [](FWWORegress& R, const FVector& At)
	{
		AWWOSporePatch::Spawn(R.World(), At, 2.f, 6.f, R.Player()); // Spore Fog Damage is equipped (Setup)
	}, 7.f });
	Effects.Add({ TEXT("poison"), [](FWWORegress& R, const FVector& At)
	{
		// The Poison Spike's poison on everyone within 4 m of the point, credited to the player.
		FWWODamageBy By(R.Player());
		for (AActor* T : UWWOWorldSubsystem::Get(R.World())->LivingTargets(R.Player()))
		{
			if (FVector::Dist(T->GetActorLocation(), At) < 400.f) AWWOGameState::Poison(T, 20.f, 5.f);
		}
	}, 6.f });
	Effects.Add({ TEXT("chain"), [](FWWORegress& R, const FVector& At)
	{
		// Static Rounds' chain: from a dummy at the point, two jumps of 40.
		AActor* First = R.SpawnDummy(At, 5000.f);
		AWWOChainLightning::SpawnFrom(R.World(), First, R.Player(), FLinearColor(0.45f, 0.75f, 1.f), 40.f, 6.f, 0.85f, 2, 0.07f);
	}, 2.f });
	Effects.Add({ TEXT("wall"), [](FWWORegress& R, const FVector& At)
	{
		AWWOBarkWall::Raise(R.World(), At - FRotator(0.f, R.HomeYaw(), 0.f).Vector() * 200.f, R.Home(), 5.f, 2.6f, 3.f, R.Player());
	}, 1.5f });
	for (const FEffect& E : Effects)
	{
		for (const EVictims Set : AllSets)
		{
			FWWORegressScenario S;
			S.Name = FString::Printf(TEXT("flora.%s.%s"), *E.Name, VictimsName(Set));
			S.bPvP = IsPvPSet(Set);
			S.MaxTicks = Ticks(E.Seconds + 2.f);
			TSharedRef<FKitState> St = MakeShared<FKitState>();
			const TFunction<void(FWWORegress&, const FVector&)> Make = E.Make;
			const float Seconds = E.Seconds;
			const bool bWall = E.Name == TEXT("wall");
			const bool bFogDamage = E.Name == TEXT("fog_damage") || E.Name == TEXT("patch");
			S.Setup = [St, Set, bFogDamage](FWWORegress& R)
			{
				*St = FKitState();
				if (bFogDamage)
				{
					R.EquipFlora("veilspore");
					R.EquipAbility(0, "spore_fog_damage");
				}
				St->Point = R.Ahead(10.f);
				PlaceVictims(R, *St, Set, St->Point, 2.5f);
			};
			S.Step = [St, Make, Seconds, bWall](FWWORegress& R, int32 Tick)
			{
				if (Tick == 0)
				{
					MarkVictims(*St, true);
					Make(R, St->Point);
				}
				FollowVictims(*St);
				if (Tick == Ticks(1.f)) MarkVictims(*St, false);
				if (Tick == Ticks(Seconds))
				{
					RecordVictims(R, *St, true);
					RecordSums(R, *St);
					R.Record(TEXT("hits_marked"), UWWOWorldSubsystem::HitsMarked - R.ShotsMarkedSince);
					if (bWall)
					{
						R.Record(TEXT("walls"), WallLine(R));
						// A line from the player's eye to the near victim: does the wall stop it.
						const AActor* Near = St->Victims.Num() > 0 ? St->Victims[0].Actor.Get() : nullptr;
						FHitResult Hit;
						const bool bHit = Near && R.Player()->Weapon->CastRayTo(Near->GetActorLocation(), Hit);
						R.Record(TEXT("blocked_by"), !bHit ? FString(TEXT("nothing")) : (Hit.GetActor() == Near ? FString(TEXT("victim")) : (Cast<AWWOBarkWall>(Hit.GetActor()) ? FString(TEXT("wall")) : FString(TEXT("other")))));
					}
					return true;
				}
				return false;
			};
			Out.Add(S);
		}
	}
}
