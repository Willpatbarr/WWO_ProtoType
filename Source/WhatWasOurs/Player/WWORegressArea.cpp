// Regression scenarios: the edges of every area effect and aim cone (row 4.6). For each way of measuring
// "within R of this point" (feet, actor centre, chest, nearest part's surface, body centre, last part,
// flat with a height limit), one victim put just inside and one just outside the radius (by 3 cm, or 5 cm
// in height), measured the way the effect measures, and where the effect needs a line of sight, one
// victim behind a bark wall. Each victim's measured distance is recorded with what happened to it, so the
// golden file shows the scenario really sits on the edge. The aim assist's cone is swept in small steps.

#include "Player/WWORegress.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOAimAssistComponent.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "Weapons/WWOGrenade.h"
#include "Flora/WWOFloraEffects.h"
#include "Flora/WWOSporeFog.h"
#include "Enemies/WWOEnemy.h"
#include "Enemies/WWOEnemyTypes.h"
#include "Enemies/WWOEnemyProjectile.h"
#include "Allies/WWOAlly.h"
#include "World/WWOTarget.h"
#include "Data/WWOData.h"
#include "GameFramework/ProjectileMovementComponent.h"

namespace
{
	// How an effect measures a victim's distance from its centre (cm). These copy the gameplay code on
	// purpose: they are the test's own statement of where each effect measures to.
	using FMeasure = float (*)(const AActor* A, const FVector& C);
	FVector FeetOf(const AActor* A)
	{
		const IWWOTargetable* T = Cast<IWWOTargetable>(A);
		return T ? T->FeetLocation() : A->GetActorLocation();
	}
	float ToFeet(const AActor* A, const FVector& C) { return FVector::Dist(FeetOf(A), C); }
	float ToCentre(const AActor* A, const FVector& C) { return FVector::Dist(A->GetActorLocation(), C); }
	float ToChest(const AActor* A, const FVector& C) { return FVector::Dist(FeetOf(A) + FVector(0, 0, 80.f), C); }
	float ToFlatFeet(const AActor* A, const FVector& C) { return FVector2D(FeetOf(A) - C).Size(); }
	float ToFlatCentre(const AActor* A, const FVector& C) { return FVector2D(A->GetActorLocation() - C).Size(); }
	float ToBodyCenter(const AActor* A, const FVector& C) { return FVector::Dist(Cast<IWWOTargetable>(A)->BodyCenter(), C); }
	float ToLastPart(const AActor* A, const FVector& C) { return FVector::Dist(Cast<IWWOTargetable>(A)->GetAimParts().Last().Position, C); }
	float ToSurface(const AActor* A, const FVector& C) // the nearest part's surface, never below 0
	{
		float Best = TNumericLimits<float>::Max();
		for (const FWWOAimPart& Part : Cast<IWWOTargetable>(A)->GetAimParts()) Best = FMath::Min(Best, FMath::Max(FVector::Dist(Part.Position, C) - Part.Radius, 0.f));
		return Best;
	}
	float ToSurfaceSigned(const AActor* A, const FVector& C) // the melee's: the nearest part's surface, may go below 0
	{
		float Best = TNumericLimits<float>::Max();
		for (const FWWOAimPart& Part : Cast<IWWOTargetable>(A)->GetAimParts()) Best = FMath::Min(Best, FVector::Dist(Part.Position, C) - Part.Radius);
		return Best;
	}

	// Slides A along the floor, along Dir, until Measure(A, C) is WantCm (secant steps; to 0.01 cm).
	void PutAt(AActor* A, const FVector& C, const FVector& Dir, float WantCm, FMeasure Measure)
	{
		if (!A) return;
		for (int32 i = 0; i < 12; ++i)
		{
			const FVector Loc = A->GetActorLocation();
			const float D0 = Measure(A, C);
			if (FMath::Abs(WantCm - D0) < 0.01f) return;
			A->SetActorLocation(Loc + Dir, false, nullptr, ETeleportType::TeleportPhysics);
			const float Slope = Measure(A, C) - D0;
			const float Step = FMath::Abs(Slope) > 0.05f ? (WantCm - D0) / Slope : WantCm - D0;
			A->SetActorLocation(Loc + Dir * Step, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	struct FEdgeVictim
	{
		FString Role;
		TWeakObjectPtr<AActor> Actor;
		FMeasure Measure = nullptr;
		float Before = 0.f;
	};
	struct FEdgeState
	{
		FVector Point = FVector::ZeroVector;
		TArray<FEdgeVictim> Victims;
		TWeakObjectPtr<AActor> Thing; // the grenade, projectile or tank the scenario sets off
	};

	FVector SideOf(const FWWORegress& R) { return FRotationMatrix(FRotator(0.f, R.HomeYaw(), 0.f)).GetUnitAxis(EAxis::Y); }
	FVector FwdOf(const FWWORegress& R) { return FRotator(0.f, R.HomeYaw(), 0.f).Vector(); }

	// A victim spawned WantCm from C along Dir (as measured), then slid onto the exact distance.
	AActor* AddVictim(FWWORegress& R, FEdgeState& St, const FString& Role, FName Kind, const FVector& C, const FVector& Dir, float WantCm, FMeasure Measure, float Lift = 0.f)
	{
		FVector At = C + Dir * WantCm;
		At.Z = R.Home().Z;
		AActor* A = nullptr;
		if (Kind == "dummy") A = R.SpawnDummy(At, 1000.f);
		else if (Kind == "foe") A = R.SpawnBot(1, At);
		else if (Kind == "mate") A = R.SpawnBot(0, At);
		else if (Kind == "ally") A = R.SpawnAlly(At);
		else if (Kind == "player") { R.PlaceAt(At, R.HomeYaw()); A = R.Player(); }
		else A = R.SpawnEnemy(Kind, At);
		if (A && Lift != 0.f) A->SetActorLocation(A->GetActorLocation() + FVector(0, 0, Lift), false, nullptr, ETeleportType::TeleportPhysics);
		PutAt(A, C, Dir, WantCm, Measure);
		St.Victims.Add({ Role, A, Measure });
		return A;
	}

	void MarkBefore(FEdgeState& St)
	{
		for (FEdgeVictim& V : St.Victims) V.Before = FWWORegress::HealthOf(V.Actor.Get());
	}

	// Each victim's measured distance (to 0.1 cm), what it lost, and its status.
	void RecordEdge(FWWORegress& R, const FEdgeState& St, const FVector& C)
	{
		for (const FEdgeVictim& V : St.Victims)
		{
			const AActor* A = V.Actor.Get();
			if (!A) { R.Record(V.Role + TEXT(".gone"), 1); continue; }
			R.Record(V.Role + TEXT(".measure_cm"), FString::Printf(TEXT("%.1f"), V.Measure(A, C)));
			R.Record(V.Role + TEXT(".taken"), V.Before - FWWORegress::HealthOf(A));
			if (const AWWOEnemy* E = Cast<AWWOEnemy>(A))
			{
				R.Record(V.Role + TEXT(".slowed"), E->SlowLeft > 0.f ? 1 : 0);
				R.Record(V.Role + TEXT(".blinded"), E->BlindLeft > 0.f ? 1 : 0);
			}
			if (const AWWOCharacter* Ch = Cast<AWWOCharacter>(A))
			{
				R.Record(V.Role + TEXT(".slowed"), Ch->SlowLeft > 0.f ? 1 : 0);
				R.Record(V.Role + TEXT(".blinded"), Ch->BlindLeft > 0.f ? 1 : 0);
				R.Record(V.Role + TEXT(".shield"), Ch->Health->Shield);
			}
			if (const AWWOAlly* Al = Cast<AWWOAlly>(A)) R.Record(V.Role + TEXT(".shield"), Al->Shield);
		}
	}

	// A bark wall standing across the line from C to At, halfway, and whether it blocks that line.
	void WallBetween(FWWORegress& R, const FVector& C, const FVector& At)
	{
		AWWOBarkWall::Raise(R.World(), (C + At) * 0.5f, C, 3.f, 2.6f, 30.f, R.Player());
	}
	FString LineTo(FWWORegress& R, const FVector& From, const AActor* A, const AActor* Skip = nullptr)
	{
		if (!A) return TEXT("none");
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WWORegressArea), false, R.Player());
		if (Skip) Params.AddIgnoredActor(Skip);
		const IWWOTargetable* T = Cast<IWWOTargetable>(A);
		const FVector To = T ? T->BodyCenter() : A->GetActorLocation();
		if (!R.World()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params)) return TEXT("clear");
		return Hit.GetActor() == A ? TEXT("victim") : (Cast<AWWOBarkWall>(Hit.GetActor()) ? TEXT("wall") : TEXT("other"));
	}

	const UWWOGrenadeData* GrenadeOfKind(const UWWOLoadoutSubsystem& L, EWWOGrenadeKind Kind)
	{
		for (const UWWOGrenadeData* G : L.Grenades) if (G && G->Kind == Kind) return G;
		return nullptr;
	}
	FWWOGrenadeSpec SpecOf(const UWWOGrenadeData* G)
	{
		FWWOGrenadeSpec Spec;
		Spec.Color = G->Color;
		Spec.Damage = G->Damage;
		Spec.BlastRadius = G->BlastRadius;
		Spec.Data = const_cast<UWWOGrenadeData*>(G);
		return Spec;
	}
}

void FWWORegress::AddAreaScenarios(TArray<FWWORegressScenario>& Out, const UWWOLoadoutSubsystem& L)
{
	const UWWOGrenadeData* Explosive = GrenadeOfKind(L, EWWOGrenadeKind::Explosive);
	const UWWOGrenadeData* Blind = GrenadeOfKind(L, EWWOGrenadeKind::Blind);
	const UWWOGrenadeData* Mine = GrenadeOfKind(L, EWWOGrenadeKind::Tripmine);
	const UWWOGrenadeData* Spike = GrenadeOfKind(L, EWWOGrenadeKind::Spike);
	auto Add = [&Out](const FString& Name, bool bPvP, float Seconds, TFunction<void(FWWORegress&, FEdgeState&)> Setup,
		TFunction<bool(FWWORegress&, FEdgeState&, int32)> Step)
	{
		FWWORegressScenario S;
		S.Name = Name;
		S.bPvP = bPvP;
		S.MaxTicks = Ticks(Seconds);
		TSharedRef<FEdgeState> St = MakeShared<FEdgeState>();
		S.Setup = [St, Setup](FWWORegress& R) { *St = FEdgeState(); Setup(R, *St); };
		S.Step = [St, Step](FWWORegress& R, int32 Tick) { return Step(R, *St, Tick); };
		Out.Add(S);
	};

	// --- A grenade's blast (AWWOGrenade::Explode): nearest part's surface <= R, walls block ---
	// Dummies: one 3 cm inside, one 3 cm outside, one well inside behind a wall. Set off at an exact point
	// 30 cm above the floor (ExplodeAt), the player's.
	if (Explosive)
	{
		Add(TEXT("area.blast.dummies"), false, 3.f, [Explosive](FWWORegress& R, FEdgeState& St)
		{
			St.Point = R.Ahead(10.f) + FVector(0, 0, 30.f);
			const float Rc = Explosive->BlastRadius * WWO::M;
			AddVictim(R, St, TEXT("in"), "dummy", St.Point, SideOf(R), Rc - 3.f, ToSurface);
			AddVictim(R, St, TEXT("out"), "dummy", St.Point, -SideOf(R), Rc + 3.f, ToSurface);
			AActor* Cover = AddVictim(R, St, TEXT("cover"), "dummy", St.Point, FwdOf(R), Rc * 0.5f, ToSurface);
			WallBetween(R, St.Point, Cover->GetActorLocation());
		}, [Explosive](FWWORegress& R, FEdgeState& St, int32 Tick)
		{
			if (Tick == 0)
			{
				MarkBefore(St);
				R.Record(TEXT("cover_line"), LineTo(R, St.Point + FVector(0, 0, 5.f), St.Victims[2].Actor.Get()));
				AWWOGrenade::ExplodeAt(R.World(), SpecOf(Explosive), St.Point, R.Player());
			}
			if (Tick < 5) return false;
			RecordEdge(R, St, St.Point);
			R.Record(TEXT("hits_marked"), UWWOWorldSubsystem::HitsMarked - R.ShotsMarkedSince);
			return true;
		});
	}

	// --- Blind / slow on a blast (AWWOGrenade::BlindOrSlow): R + 0.5 m, enemies by their feet, the
	// other team's players by their actor centre ---
	if (Blind)
	{
		for (const bool bPvP : { false, true })
		{
			Add(FString::Printf(TEXT("area.blind.%s"), bPvP ? TEXT("foes") : TEXT("enemies")), bPvP, 3.f, [Blind, bPvP](FWWORegress& R, FEdgeState& St)
			{
				St.Point = R.Ahead(10.f) + FVector(0, 0, 30.f);
				const float Reach = (Blind->BlastRadius + 0.5f) * WWO::M;
				const FName Kind = bPvP ? FName("foe") : FName("soldier");
				const FMeasure M = bPvP ? ToCentre : ToFeet;
				AddVictim(R, St, TEXT("in"), Kind, St.Point, SideOf(R), Reach - 3.f, M);
				AddVictim(R, St, TEXT("out"), Kind, St.Point, -SideOf(R), Reach + 3.f, M);
			}, [Blind](FWWORegress& R, FEdgeState& St, int32 Tick)
			{
				if (Tick == 0)
				{
					MarkBefore(St);
					AWWOGrenade::ExplodeAt(R.World(), SpecOf(Blind), St.Point, R.Player());
				}
				if (Tick < 5) return false;
				RecordEdge(R, St, St.Point);
				return true;
			});
		}
	}

	// --- The Snare Mine's trigger (AWWOGrenade::UpdateMine): feet <= Trigger + 0.5 m, the first one found
	// sets it off. Planted (Stick) at an exact point 15 cm above the floor; one victim each ---
	if (Mine)
	{
		for (const FName Kind : { FName("dummy"), FName("foe") })
		{
			for (const bool bIn : { true, false })
			{
				const bool bPvP = Kind == "foe";
				Add(FString::Printf(TEXT("area.mine.%s_%s"), *Kind.ToString(), bIn ? TEXT("in") : TEXT("out")), bPvP, 4.f, [Mine, Kind, bIn](FWWORegress& R, FEdgeState& St)
				{
					St.Point = R.Ahead(10.f) + FVector(0, 0, 15.f);
					const float Trigger = (Mine->TriggerRadius + 0.5f) * WWO::M;
					AddVictim(R, St, bIn ? TEXT("in") : TEXT("out"), Kind, St.Point, SideOf(R), Trigger + (bIn ? -3.f : 3.f), ToFeet);
				}, [Mine](FWWORegress& R, FEdgeState& St, int32 Tick)
				{
					if (Tick == 0)
					{
						MarkBefore(St);
						AWWOGrenade* G = AWWOGrenade::Throw(R.World(), SpecOf(Mine), St.Point, FVector::ZeroVector, R.Player());
						if (G) G->Stick();
						St.Thing = G;
					}
					if (Tick < Ticks(Mine->ArmTime + 1.f)) return false;
					const AWWOGrenade* G = Cast<AWWOGrenade>(St.Thing.Get());
					R.RecordBool(TEXT("went_off"), !G || G->bSpent);
					RecordEdge(R, St, St.Point);
					return true;
				});
			}
		}
	}

	// --- Shield Share on a blast (AWWOGrenade::ShareShields): R + 0.5 m by the actor centre; AI teammates,
	// and online the player's own team. Their shields emptied first ---
	if (Explosive)
	{
		for (const bool bPvP : { false, true })
		{
			Add(FString::Printf(TEXT("area.shield_share.%s"), bPvP ? TEXT("mates") : TEXT("allies")), bPvP, 3.f, [Explosive, bPvP](FWWORegress& R, FEdgeState& St)
			{
				R.EquipFlora("ironbark");
				R.EquipAbility(0, "shield_share");
				St.Point = R.Ahead(10.f) + FVector(0, 0, 30.f);
				const float Reach = (Explosive->BlastRadius + 0.5f) * WWO::M;
				const FName Kind = bPvP ? FName("mate") : FName("ally");
				AddVictim(R, St, TEXT("in"), Kind, St.Point, SideOf(R), Reach - 3.f, ToCentre);
				AddVictim(R, St, TEXT("out"), Kind, St.Point, -SideOf(R), Reach + 3.f, ToCentre);
			}, [Explosive](FWWORegress& R, FEdgeState& St, int32 Tick)
			{
				if (Tick == 0)
				{
					for (FEdgeVictim& V : St.Victims)
					{
						if (AWWOAlly* Al = Cast<AWWOAlly>(V.Actor.Get())) Al->Shield = 0.f;
						if (AWWOCharacter* Ch = Cast<AWWOCharacter>(V.Actor.Get())) Ch->Health->Shield = 0.f;
					}
					MarkBefore(St);
					AWWOGrenade::ExplodeAt(R.World(), SpecOf(Explosive), St.Point, R.Player());
				}
				if (Tick < 3) return false;
				RecordEdge(R, St, St.Point);
				return true;
			});
		}
	}

	// --- A spore pool's damage (AWWOSporePool::Tick): feet within R flat, and less than 1.5 m above or below.
	// Dummies 3 cm inside and outside, and two 1 m out lifted 1.45 m and 1.55 m; the other team's players ---
	for (const bool bPvP : { false, true })
	{
		Add(FString::Printf(TEXT("area.pool.%s"), bPvP ? TEXT("foes") : TEXT("dummies")), bPvP, 4.f, [bPvP](FWWORegress& R, FEdgeState& St)
		{
			St.Point = R.Ahead(10.f);
			const float Rc = 3.f * WWO::M;
			const FName Kind = bPvP ? FName("foe") : FName("dummy");
			AddVictim(R, St, TEXT("in"), Kind, St.Point, SideOf(R), Rc - 3.f, ToFlatFeet);
			AddVictim(R, St, TEXT("out"), Kind, St.Point, -SideOf(R), Rc + 3.f, ToFlatFeet);
			if (!bPvP)
			{
				AddVictim(R, St, TEXT("low"), "dummy", St.Point, FwdOf(R), 100.f, ToFlatFeet, 145.f);
				AddVictim(R, St, TEXT("high"), "dummy", St.Point, -FwdOf(R), 100.f, ToFlatFeet, 155.f);
			}
		}, [](FWWORegress& R, FEdgeState& St, int32 Tick)
		{
			if (Tick == 0)
			{
				MarkBefore(St);
				St.Thing = AWWOSporePool::Spawn(R.World(), St.Point, 3.f, 25.f, 5.f, FLinearColor(0.5f, 0.8f, 0.3f), "veilspore", R.Player());
			}
			if (Tick < Ticks(2.f)) return false;
			if (const AActor* Pool = St.Thing.Get()) R.RecordVec(TEXT("pool_at"), Pool->GetActorLocation());
			RecordEdge(R, St, St.Point);
			for (const FEdgeVictim& V : St.Victims)
			{
				if (const AActor* A = V.Actor.Get()) R.Record(V.Role + TEXT(".height_cm"), FString::Printf(TEXT("%.1f"), FeetOf(A).Z - St.Point.Z));
			}
			return true;
		});
	}

	// --- Spore Fog Damage (AWWOSporeFog::Tick): the chest (feet + 0.8 m) within R; rank-and-file enemies,
	// the other team's players ---
	for (const bool bPvP : { false, true })
	{
		Add(FString::Printf(TEXT("area.fog.%s"), bPvP ? TEXT("foes") : TEXT("enemies")), bPvP, 5.f, [bPvP](FWWORegress& R, FEdgeState& St)
		{
			R.EquipFlora("veilspore");
			R.EquipAbility(0, "spore_fog_damage");
			St.Point = R.Ahead(10.f);
			const float Rc = 4.f * WWO::M;
			const FName Kind = bPvP ? FName("foe") : FName("soldier");
			AddVictim(R, St, TEXT("in"), Kind, St.Point, SideOf(R), Rc - 3.f, ToChest);
			AddVictim(R, St, TEXT("out"), Kind, St.Point, -SideOf(R), Rc + 3.f, ToChest);
		}, [](FWWORegress& R, FEdgeState& St, int32 Tick)
		{
			if (Tick == 0)
			{
				MarkBefore(St);
				St.Thing = AWWOSporeFog::SpawnFog(R.World(), St.Point, 4.f, 5.f, false, R.Player());
			}
			if (Tick < Ticks(3.f)) return false;
			RecordEdge(R, St, St.Point);
			return true;
		});
	}

	// --- Chain lightning's next jump (AWWOChainLightning::NextTarget): the nearest body centre closer than
	// the range, with a clear line from the last one. From a dummy: one 3 cm inside the range with a nearer
	// one behind a wall; or one 3 cm outside ---
	for (const bool bIn : { true, false })
	{
		Add(FString::Printf(TEXT("area.chain.%s"), bIn ? TEXT("in") : TEXT("out")), false, 3.f, [bIn](FWWORegress& R, FEdgeState& St)
		{
			AActor* First = R.SpawnDummy(R.Ahead(10.f), 5000.f);
			St.Thing = First;
			St.Point = Cast<IWWOTargetable>(First)->BodyCenter();
			const float Range = 6.f * WWO::M;
			AddVictim(R, St, bIn ? TEXT("in") : TEXT("out"), "dummy", St.Point, SideOf(R), Range + (bIn ? -3.f : 3.f), ToBodyCenter);
			if (bIn)
			{
				AActor* Cover = AddVictim(R, St, TEXT("cover"), "dummy", St.Point, FwdOf(R), Range * 0.5f, ToBodyCenter);
				WallBetween(R, First->GetActorLocation(), Cover->GetActorLocation());
			}
		}, [](FWWORegress& R, FEdgeState& St, int32 Tick)
		{
			if (Tick == 0)
			{
				MarkBefore(St);
				if (St.Victims.Num() > 1) R.Record(TEXT("cover_line"), LineTo(R, St.Point, St.Victims[1].Actor.Get(), St.Thing.Get()));
				AWWOChainLightning::SpawnFrom(R.World(), St.Thing.Get(), R.Player(), FLinearColor(0.45f, 0.75f, 1.f), 40.f, 6.f, 0.85f, 2, 0.07f);
			}
			if (Tick < Ticks(1.5f)) return false;
			RecordEdge(R, St, St.Point);
			return true;
		});
	}

	// --- The Poison Spike's seeking (AWWOGrenade::Seek): the nearest body (last aim part) closer than the
	// seek radius. Flying level and straight (no gravity) past one dummy to its side ---
	if (Spike)
	{
		for (const bool bIn : { true, false })
		{
			Add(FString::Printf(TEXT("area.seek.%s"), bIn ? TEXT("in") : TEXT("out")), false, 2.f, [Spike, bIn](FWWORegress& R, FEdgeState& St)
			{
				St.Point = R.Ahead(10.f) + FVector(0, 0, 60.f);
				AddVictim(R, St, bIn ? TEXT("in") : TEXT("out"), "dummy", St.Point, SideOf(R), Spike->SeekRadius * WWO::M + (bIn ? -3.f : 3.f), ToLastPart);
			}, [Spike](FWWORegress& R, FEdgeState& St, int32 Tick)
			{
				if (Tick == 0)
				{
					MarkBefore(St);
					AWWOGrenade* Spiked = AWWOGrenade::Throw(R.World(), SpecOf(Spike), St.Point, FwdOf(R) * 300.f, R.Player());
					if (Spiked) Spiked->Movement->ProjectileGravityScale = 0.f;
					St.Thing = Spiked;
				}
				const AWWOGrenade* G = Cast<AWWOGrenade>(St.Thing.Get());
				if (Tick == 10 || Tick == 40)
				{
					const FString At = FString::FromInt(Tick);
					R.RecordBool(TEXT("alive_") + At, G && !G->bSpent);
					if (G) R.Record(TEXT("sideways_") + At, FString::Printf(TEXT("%.3f"), FVector::DotProduct(G->Movement->Velocity, SideOf(R))));
				}
				if (Tick < 40) return false;
				RecordEdge(R, St, St.Point);
				return true;
			});
		}
	}

	// --- An enemy shell's splash (AWWOEnemyProjectile::Splash): every player's actor centre within R + 0.5 m,
	// living AI teammates likewise. The player on one side, an ally on the other ---
	for (const bool bPlayerIn : { true, false })
	{
		Add(FString::Printf(TEXT("area.splash.player_%s"), bPlayerIn ? TEXT("in") : TEXT("out")), false, 2.f, [bPlayerIn](FWWORegress& R, FEdgeState& St)
		{
			St.Point = R.Ahead(10.f) + FVector(0, 0, 20.f);
			const float Reach = 2.f * WWO::M + 50.f;
			AddVictim(R, St, TEXT("player"), "player", St.Point, SideOf(R), Reach + (bPlayerIn ? -3.f : 3.f), ToCentre);
			AddVictim(R, St, TEXT("ally"), "ally", St.Point, -SideOf(R), Reach + (bPlayerIn ? 3.f : -3.f), ToCentre);
			R.Tune(R.Player()->Health, "RechargeDelay", 100.f);
		}, [](FWWORegress& R, FEdgeState& St, int32 Tick)
		{
			if (Tick == 0)
			{
				MarkBefore(St);
				AWWOEnemyProjectile* Shell = AWWOEnemyProjectile::Fire(R.World(), St.Point + FVector(0, 0, 400.f), FVector::ZeroVector, 30.f, 10.f, 0.f, 2.f);
				if (Shell) Shell->Splash(St.Point);
				St.Thing = Shell;
			}
			if (Tick < 3) return false;
			RecordEdge(R, St, St.Point);
			return true;
		});
	}

	// --- The Tank's slam (AWWOTank::Slam): actor centres within R flat and at most 1.5 m apart in height;
	// every player and living AI teammates. The player on one side, an ally on the other ---
	for (const bool bPlayerIn : { true, false })
	{
		Add(FString::Printf(TEXT("area.slam.player_%s"), bPlayerIn ? TEXT("in") : TEXT("out")), false, 2.f, [bPlayerIn](FWWORegress& R, FEdgeState& St)
		{
			AWWOEnemy* Tank = R.SpawnEnemy("tank", R.Ahead(10.f));
			St.Thing = Tank;
			St.Point = Tank ? Tank->GetActorLocation() : R.Ahead(10.f);
			const UWWOTankData* T = R.Loadout()->EnemyDataAs<UWWOTankData>("tank");
			const float Rc = (T ? T->SlamRadius : 4.5f) * WWO::M;
			AddVictim(R, St, TEXT("player"), "player", St.Point, SideOf(R), Rc + (bPlayerIn ? -3.f : 3.f), ToFlatCentre);
			AddVictim(R, St, TEXT("ally"), "ally", St.Point, -SideOf(R), Rc + (bPlayerIn ? 3.f : -3.f), ToFlatCentre);
			R.Tune(R.Player()->Health, "RechargeDelay", 100.f);
		}, [](FWWORegress& R, FEdgeState& St, int32 Tick)
		{
			AWWOTank* Tank = Cast<AWWOTank>(St.Thing.Get());
			if (Tick == 0)
			{
				MarkBefore(St);
				if (Tank)
				{
					St.Point = Tank->GetActorLocation();
					Tank->Slam();
				}
			}
			if (Tick < 3) return false;
			RecordEdge(R, St, St.Point);
			for (const FEdgeVictim& V : St.Victims)
			{
				if (const AActor* A = V.Actor.Get()) R.Record(V.Role + TEXT(".height_cm"), FString::Printf(TEXT("%.1f"), A->GetActorLocation().Z - St.Point.Z));
			}
			return true;
		});
	}

	// --- The punch (UWWOWeaponComponent::FindLungeTarget, MeleeStrike): the nearest part's surface from the
	// eye, within the reach (a hit) or the lunge range (a lunge first), with a clear line. A dummy straight
	// ahead 3 cm inside and outside each, and one at 2 m behind a wall ---
	struct FMeleeEdge { const TCHAR* Name; bool bLunge; float Off; bool bCover; };
	for (const FMeleeEdge& E : { FMeleeEdge{ TEXT("reach_in"), false, -3.f, false }, FMeleeEdge{ TEXT("reach_out"), false, 3.f, false },
		FMeleeEdge{ TEXT("lunge_in"), true, -3.f, false }, FMeleeEdge{ TEXT("lunge_out"), true, 3.f, false }, FMeleeEdge{ TEXT("cover"), false, 0.f, true } })
	{
		Add(FString::Printf(TEXT("area.melee.%s"), E.Name), false, 4.f, [E](FWWORegress& R, FEdgeState& St)
		{
			const AWWOCharacter* P = R.Player();
			St.Point = P->EyeLocation();
			const float Want = E.bCover ? 200.f : (E.bLunge ? P->Weapon->LungeRange : P->Weapon->MeleeReach) * WWO::M + E.Off;
			AActor* A = AddVictim(R, St, TEXT("target"), "dummy", St.Point, FwdOf(R), Want, ToSurfaceSigned);
			if (E.bCover) WallBetween(R, R.Home(), A->GetActorLocation());
		}, [](FWWORegress& R, FEdgeState& St, int32 Tick)
		{
			AWWOCharacter* P = R.Player();
			const AActor* T = St.Victims[0].Actor.Get();
			if (Tick == 0 && T) R.AimAt(Cast<IWWOTargetable>(T)->BodyCenter());
			if (Tick == 3)
			{
				St.Point = P->EyeLocation();
				R.Record(TEXT("measure_at_press_cm"), FString::Printf(TEXT("%.1f"), ToSurfaceSigned(T, St.Point)));
				MarkBefore(St);
				R.Press(EWWOAction::Melee);
			}
			if (Tick < 4 + Ticks(1.5f)) return false;
			R.Record(TEXT("dealt"), St.Victims[0].Before - FWWORegress::HealthOf(T));
			R.Record(TEXT("lunge_m"), FString::Printf(TEXT("%.2f"), (float)FVector2D(P->GetActorLocation() - R.Home()).Size() / 100.f));
			R.Record(TEXT("confirms"), R.Confirms);
			return true;
		});
	}

	// --- The aim assist's cone (UWWOAimAssistComponent::FindPart): the part nearest the reticle inside the
	// cone, in line of sight. The rifle, assist on as for a controller; the aim swept across a dummy at
	// 10 m (body, then head), and once at a dummy behind a wall ---
	for (const bool bCover : { false, true })
	{
		Add(FString::Printf(TEXT("area.assist.%s"), bCover ? TEXT("cover") : TEXT("sweep")), false, 4.f, [bCover](FWWORegress& R, FEdgeState& St)
		{
			R.EquipWeapon("rifle");
			R.Tune(R.Player()->AimAssist, "MouseAssist", 1.f);
			AActor* A = R.SpawnDummy(R.Ahead(10.f), 1000.f);
			St.Victims.Add({ TEXT("dummy"), A, ToCentre });
			if (bCover) WallBetween(R, R.Home(), A->GetActorLocation());
		}, [](FWWORegress& R, FEdgeState& St, int32 Tick)
		{
			const AActor* A = St.Victims[0].Actor.Get();
			const IWWOTargetable* T = Cast<IWWOTargetable>(A);
			if (!T) return true;
			const TArray<FWWOAimPart> Parts = T->GetAimParts();
			// Offsets across the body, then across the head (m, sideways at 10 m: 0.1 m is about 0.57 degrees).
			static const float Offsets[] = { 0.f, 0.2f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 1.f, 1.2f };
			const int32 Count = UE_ARRAY_COUNT(Offsets);
			const int32 Step = Tick / 2;
			if (Step >= Count * 2) return true;
			const FVector Aim = (Step < Count ? Parts.Last().Position : Parts[0].Position) + SideOf(R) * Offsets[Step % Count] * WWO::M;
			if (Tick % 2 == 0) { R.AimAt(Aim); return false; }
			const UWWOWeaponData* W = R.Player()->Weapon->GetWeaponData();
			const FWWOAssistPart Found = W ? R.Player()->AimAssist->FindPart(W->MagnetismAngle, W->CritMagnetism) : FWWOAssistPart();
			const FWWOAssistPart Wide = W ? R.Player()->AimAssist->FindPart(W->FrictionAngle) : FWWOAssistPart();
			const FString Key = FString::Printf(TEXT("%s_%.1f"), Step < Count ? TEXT("body") : TEXT("head"), Offsets[Step % Count]);
			auto Line = [](const FWWOAssistPart& F)
			{
				return F.IsValid() ? FString::Printf(TEXT("%s angle %.3f cone %.3f"), F.bCritical ? TEXT("head") : TEXT("body"),
					FMath::RadiansToDegrees(F.Angle), FMath::RadiansToDegrees(F.Cone)) : FString(TEXT("none"));
			};
			R.Record(Key + TEXT(".magnet"), Line(Found));
			R.Record(Key + TEXT(".friction"), Line(Wide));
			return false;
		});
	}
}
