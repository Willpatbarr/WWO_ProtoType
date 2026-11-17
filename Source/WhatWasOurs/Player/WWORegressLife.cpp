// Regression scenarios: the life of each lingering world effect (row 4.11). The spore pool, spore fog,
// spore patch, bark wall and Entrench roots, each made with short, chosen timings and followed tick by
// tick: when it hurts a victim standing in it (the first tick, the last one before it goes, none after),
// the values its looks are set to around each edge of its life (grow-in, fade, end, tail), its own
// switches (collision, cover, hidden, emitting, Contains / Blocks) and the tick it is gone. The .joiner
// cases give the effect a joiner's role (not the authority) to show what a joiner's copy does at the
// end: the host destroys, a joiner's copy waits to be told.

#include "Player/WWORegress.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOAbilityComponent.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Flora/WWOFloraEffects.h"
#include "Flora/WWOSporeFog.h"
#include "FX/WWOFX.h"
#include "World/WWOTarget.h"
#include "Enemies/WWOEnemy.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "WWOIds.h"

namespace
{
	struct FLifeState
	{
		TWeakObjectPtr<AActor> Thing;
		TWeakObjectPtr<AActor> Victim;
		float LastHealth = 0.f;
		FString Hits;  // "tick:amount" for each tick the victim lost health
		int32 GoneTick = -1;
		TArray<TPair<FString, FString>> Samples; // value name -> "tick:value ..."
		TArray<TPair<FString, FString>> Flags;   // switch name -> "tick:0|1 ..." (each change)
		TMap<FString, int32> FlagWas;
		TSet<TWeakObjectPtr<AActor>> Bolts;
		int32 BoltsEarly = 0, BoltsLate = 0;
	};

	FString& Line(TArray<TPair<FString, FString>>& Lines, const FString& Name)
	{
		for (TPair<FString, FString>& L : Lines) if (L.Key == Name) return L.Value;
		return Lines.Add_GetRef({ Name, FString() }).Value;
	}
	void Append(FString& S, const FString& Item) { if (!S.IsEmpty()) S += TEXT(" "); S += Item; }

	// Sampled on the first ticks, every 10th tick, and two ticks either side of each edge of its life.
	bool IsSampleTick(int32 Tick, const TArray<float>& Edges)
	{
		if (Tick <= 3 || Tick % 10 == 0) return true;
		for (const float E : Edges) if (FMath::Abs(Tick - FWWORegress::Ticks(E)) <= 2) return true;
		return false;
	}

	float AlphaOf(const UMaterialInstanceDynamic* M)
	{
		float Value = -1.f;
		if (M) M->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Alpha")), Value);
		return Value;
	}
	float AlphaOf(const UPrimitiveComponent* C) { return AlphaOf(C ? Cast<UMaterialInstanceDynamic>(C->GetMaterial(0)) : nullptr); }
}

void FWWORegress::AddLifeScenarios(TArray<FWWORegressScenario>& Out, const UWWOLoadoutSubsystem&)
{
	// What each case makes, where its victim stands, and what it reads off the effect each tick.
	using FValues = TArray<TPair<FString, float>>;
	using FSwitches = TArray<TPair<FString, bool>>;
	struct FLifeCase
	{
		FString Name;
		bool bJoiner = false;
		float RunFor = 3.f;          // seconds the scenario follows it
		TArray<float> Edges;         // its life's edges, in seconds from the spawn
		FName VictimKind;            // "dummy", "soldier" or none
		bool bFogDamage = false;     // Spore Fog Damage equipped
		bool bPatch = false;         // the Spore Patches HUD count once it is gone
		bool bVoltVine = false;      // count the pool's lightning arcs, before and in its last 0.4 s
		float ArcsStopAt = 0.f;
		TFunction<AActor*(FWWORegress&, const FVector&)> Make;
		TFunction<void(FWWORegress&, AActor*, FValues&, FSwitches&)> Read;
	};
	TArray<FLifeCase> Cases;

	// Spore pool: grows in 0.25 s, fades over its last 0.5 s, ends at its duration (2.025 s: a damage tick
	// every 0.5 s lands on the last tick before it ends). Damage each 0.5 s from the first tick, host only.
	auto ReadPool = [](FWWORegress&, AActor* A, FValues& V, FSwitches& F)
	{
		AWWOSporePool* Pool = Cast<AWWOSporePool>(A);
		V.Add({ TEXT("disc_scale"), Pool->Disc ? (float)Pool->Disc->GetRelativeScale3D().X : -1.f });
		V.Add({ TEXT("disc_alpha"), AlphaOf(Pool->Mat.Get()) });
		V.Add({ TEXT("haze_scale_z"), Pool->Haze ? (float)Pool->Haze->GetRelativeScale3D().Z : -1.f });
		V.Add({ TEXT("haze_alpha"), AlphaOf(Pool->Haze.Get()) });
		V.Add({ TEXT("glow"), Pool->Glow ? Pool->Glow->Intensity : -1.f });
		F.Add({ TEXT("hidden"), Pool->IsHidden() });
		F.Add({ TEXT("bubbling"), Pool->Bubbles.IsValid() && Pool->Bubbles->bEmitting });
	};
	for (const bool bJoiner : { false, true })
	{
		FLifeCase C;
		C.Name = bJoiner ? TEXT("pool.joiner") : TEXT("pool");
		C.bJoiner = bJoiner;
		C.Edges = { 0.25f, 1.525f, 2.025f };
		C.VictimKind = "dummy";
		C.Make = [](FWWORegress& R, const FVector& At) -> AActor* { return AWWOSporePool::Spawn(R.World(), At, 3.f, 25.f, 2.025f, FLinearColor(0.5f, 0.8f, 0.3f), "veilspore", R.Player()); };
		C.Read = ReadPool;
		Cases.Add(C);
	}
	{
		FLifeCase C;
		C.Name = TEXT("pool.voltvine");
		C.Edges = { 2.025f };
		C.VictimKind = "dummy";
		C.bVoltVine = true;
		C.ArcsStopAt = 2.025f - 0.4f;
		C.Make = [](FWWORegress& R, const FVector& At) -> AActor* { return AWWOSporePool::Spawn(R.World(), At, 3.f, 25.f, 2.025f, FLinearColor(0.3f, 0.6f, 1.f), WWOIds::Flora::VoltVine, R.Player()); };
		C.Read = [](FWWORegress&, AActor* A, FValues&, FSwitches& F) { F.Add({ TEXT("hidden"), A->IsHidden() }); };
		Cases.Add(C);
	}

	// Spore fog (thick): strength rises over 0.6 s and falls over its last 0.6 s, ends at 1.7 s. Spore Fog
	// Damage each 0.5 s while its strength is at least 0.3, host only; Contains above 0.3, Blocks from 0.5.
	auto ReadFog = [](FWWORegress& R, AActor* A, FValues& V, FSwitches& F)
	{
		AWWOSporeFog* Fog = Cast<AWWOSporeFog>(A);
		const FVector C = Fog->GetActorLocation();
		const FVector Side = FRotationMatrix(FRotator(0.f, R.HomeYaw(), 0.f)).GetUnitAxis(EAxis::Y) * 1000.f;
		V.Add({ TEXT("strength"), Fog->Strength() });
		V.Add({ TEXT("alpha"), AlphaOf(Fog->Material.Get()) });
		V.Add({ TEXT("edge_alpha"), AlphaOf(Fog->Edge.Get()) });
		F.Add({ TEXT("contains"), AWWOSporeFog::Contains(R.World(), C) });
		F.Add({ TEXT("blocks"), AWWOSporeFog::Blocks(R.World(), C - Side, C + Side) });
		F.Add({ TEXT("motes"), Fog->Motes.IsValid() && Fog->Motes->bEmitting });
		F.Add({ TEXT("smoke"), Fog->Smoke.IsValid() && Fog->Smoke->bEmitting });
	};
	for (const bool bJoiner : { false, true })
	{
		FLifeCase C;
		C.Name = bJoiner ? TEXT("fog.joiner") : TEXT("fog");
		C.bJoiner = bJoiner;
		C.Edges = { 0.18f, 0.6f, 1.1f, 1.52f, 1.7f };
		C.VictimKind = "soldier";
		C.bFogDamage = true;
		C.Make = [](FWWORegress& R, const FVector& At) -> AActor* { return AWWOSporeFog::SpawnFog(R.World(), At, 4.f, 1.7f, true, R.Player()); };
		C.Read = ReadFog;
		Cases.Add(C);
	}

	// Spore patch: grows in 0.25 s, holds its duration (1 s), shrinks over 0.3 s after it, then the host
	// destroys it. Its own fog has the same duration.
	auto ReadPatch = [](FWWORegress& R, AActor* A, FValues& V, FSwitches&)
	{
		V.Add({ TEXT("scale"), (float)A->GetActorScale3D().X });
		V.Add({ TEXT("live_count"), (float)R.Player()->Abilities->LiveCount(WWOIds::Ability::SporePatches) });
		int32 Fogs = 0;
		for (TActorIterator<AWWOSporeFog> It(R.World()); It; ++It) if (!It->IsActorBeingDestroyed()) ++Fogs;
		V.Add({ TEXT("fogs"), (float)Fogs });
	};
	for (const bool bJoiner : { false, true })
	{
		FLifeCase C;
		C.Name = bJoiner ? TEXT("patch.joiner") : TEXT("patch");
		C.bJoiner = bJoiner;
		C.RunFor = 2.f;
		C.Edges = { 0.25f, 1.f, 1.3f };
		C.bPatch = true;
		C.Make = [](FWWORegress& R, const FVector& At) -> AActor* { return AWWOSporePatch::Spawn(R.World(), At, 2.f, 1.f, R.Player()); };
		C.Read = ReadPatch;
		Cases.Add(C);
	}

	// Bark wall: rises over 0.25 s, stands its duration (1 s), then loses its collision and cover and sinks
	// over 0.4 s on a clock started again from 0; the host destroys it.
	auto ReadWall = [](FWWORegress&, AActor* A, FValues& V, FSwitches& F)
	{
		AWWOBarkWall* Wall = Cast<AWWOBarkWall>(A);
		V.Add({ TEXT("rise"), (float)Wall->Visual->GetRelativeScale3D().Z });
		V.Add({ TEXT("rise_z"), (float)Wall->Visual->GetRelativeLocation().Z });
		F.Add({ TEXT("collision"), Wall->Box->IsCollisionEnabled() });
		F.Add({ TEXT("cover"), Wall->ActorHasTag("Cover") });
	};
	for (const bool bJoiner : { false, true })
	{
		FLifeCase C;
		C.Name = bJoiner ? TEXT("wall.joiner") : TEXT("wall");
		C.bJoiner = bJoiner;
		C.RunFor = 2.f;
		C.Edges = { 0.25f, 1.f, 1.4f };
		C.Make = [](FWWORegress& R, const FVector& At) -> AActor* { return AWWOBarkWall::Raise(R.World(), At, R.Home(), 3.f, 2.6f, 1.f, R.Player()); };
		C.Read = ReadWall;
		Cases.Add(C);
	}

	// Entrench roots (this machine's looks only, not replicated): rise over 0.2 s, stand their duration
	// (1 s), sink over 0.3 s on a clock started again from 0, then destroy themselves.
	{
		FLifeCase C;
		C.Name = TEXT("roots");
		C.RunFor = 2.f;
		C.Edges = { 0.2f, 1.f, 1.3f };
		C.Make = [](FWWORegress& R, const FVector&) -> AActor* { return AWWOEntrenchRoots::Spawn(R.World(), R.Player(), 1.f); };
		C.Read = [](FWWORegress&, AActor* A, FValues& V, FSwitches&)
		{
			AWWOEntrenchRoots* Roots = Cast<AWWOEntrenchRoots>(A);
			V.Add({ TEXT("scale_z"), (float)Roots->GetActorRelativeScale3D().Z });
			V.Add({ TEXT("ring_alpha"), AlphaOf(Roots->RingMat.Get()) });
		};
		Cases.Add(C);
	}

	for (const FLifeCase& Case : Cases)
	{
		FWWORegressScenario S;
		S.Name = TEXT("life.") + Case.Name;
		S.MaxTicks = Ticks(Case.RunFor + 2.f);
		TSharedRef<FLifeState> St = MakeShared<FLifeState>();
		S.Setup = [St, Case](FWWORegress& R)
		{
			*St = FLifeState();
			if (Case.bFogDamage)
			{
				R.EquipFlora("veilspore");
				R.EquipAbility(0, "spore_fog_damage");
			}
			if (Case.VictimKind == FName("dummy")) St->Victim = R.SpawnDummy(R.Ahead(10.f, 1.f), 5000.f);
			else if (!Case.VictimKind.IsNone()) St->Victim = R.SpawnEnemy(Case.VictimKind, R.Ahead(10.f, 1.f));
		};
		S.Step = [St, Case](FWWORegress& R, int32 Tick)
		{
			if (Tick == 0)
			{
				St->LastHealth = FWWORegress::HealthOf(St->Victim.Get());
				St->Thing = Case.Make(R, R.Ahead(10.f));
				if (St->Thing.IsValid() && Case.bJoiner) St->Thing->SetRole(ROLE_SimulatedProxy); // a joiner's copy
				R.RecordBool(TEXT("made"), St->Thing.IsValid());
			}
			if (const AActor* V = St->Victim.Get())
			{
				const float Now = FWWORegress::HealthOf(V);
				if (Now < St->LastHealth) Append(St->Hits, FString::Printf(TEXT("%d:%.9g"), Tick, St->LastHealth - Now));
				St->LastHealth = Now;
			}
			if (Case.bVoltVine)
			{
				for (TActorIterator<AWWOLightningBolt> It(R.World()); It; ++It)
				{
					if (St->Bolts.Contains(*It)) continue;
					St->Bolts.Add(*It);
					(Tick <= Ticks(Case.ArcsStopAt) + 2 ? St->BoltsEarly : St->BoltsLate) += 1; // seen a tick or two after it is made
				}
			}
			AActor* A = St->Thing.Get();
			if (!A && St->GoneTick < 0) St->GoneTick = Tick;
			if (A)
			{
				FValues Values;
				FSwitches Switches;
				Case.Read(R, A, Values, Switches);
				if (IsSampleTick(Tick, Case.Edges))
				{
					for (const TPair<FString, float>& P : Values) Append(Line(St->Samples, P.Key), FString::Printf(TEXT("%d:%.9g"), Tick, P.Value));
				}
				for (const TPair<FString, bool>& P : Switches)
				{
					const int32 Now = P.Value ? 1 : 0;
					const int32* Was = St->FlagWas.Find(P.Key);
					if (!Was || *Was != Now) Append(Line(St->Flags, P.Key), FString::Printf(TEXT("%d:%d"), Tick, Now));
					St->FlagWas.Add(P.Key, Now);
				}
			}
			if (Tick < Ticks(Case.RunFor)) return false;
			R.Record(TEXT("gone_tick"), St->GoneTick);
			R.Record(TEXT("hits"), St->Hits.IsEmpty() ? FString(TEXT("none")) : St->Hits);
			for (const TPair<FString, FString>& P : St->Samples) R.Record(TEXT("at.") + P.Key, P.Value);
			for (const TPair<FString, FString>& P : St->Flags) R.Record(TEXT("switch.") + P.Key, P.Value);
			if (Case.bPatch) R.Record(TEXT("live_count_end"), R.Player()->Abilities->LiveCount(WWOIds::Ability::SporePatches));
			if (Case.bVoltVine)
			{
				R.RecordBool(TEXT("arcs_before"), St->BoltsEarly > 0);
				R.Record(TEXT("arcs_last_0.4s"), St->BoltsLate);
			}
			if (A && Case.bJoiner) A->SetRole(ROLE_Authority); // so the teardown can destroy it
			return true;
		};
		Out.Add(S);
	}
}
