#include "Systems/WWOWorldSubsystem.h"
#include "WhatWasOurs.h"
#include "Player/WWOProf.h"
#include "EngineUtils.h"
#include "Enemies/WWOEnemy.h"
#include "Allies/WWOAlly.h"
#include "Flora/WWOSporeFog.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOWeaponComponent.h"
#include "World/WWOTarget.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Game/WWOGameState.h"
#include "Game/WWOPlayerState.h"
#include "Player/WWOHealthComponent.h"

UWWOWorldSubsystem* UWWOWorldSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UWWOWorldSubsystem>() : nullptr;
}

void UWWOWorldSubsystem::RegisterTarget(AActor* Actor)
{
	Targets.AddUnique(Actor);
}

void UWWOWorldSubsystem::UnregisterTarget(AActor* Actor)
{
	Targets.Remove(Actor);
}

void UWWOWorldSubsystem::RegisterTeammate(AWWOAlly* Ally)
{
	Teammates.AddUnique(Ally);
}

void UWWOWorldSubsystem::UnregisterTeammate(AWWOAlly* Ally)
{
	Teammates.Remove(Ally);
}

void UWWOWorldSubsystem::RegisterFog(AWWOSporeFog* Fog)
{
	SporeFogs.AddUnique(Fog);
}

void UWWOWorldSubsystem::UnregisterFog(AWWOSporeFog* Fog)
{
	SporeFogs.Remove(Fog);
}

TArray<AActor*> UWWOWorldSubsystem::AllTargets() const
{
	TArray<AActor*> Out;
	for (const TWeakObjectPtr<AActor>& T : Targets)
	{
		if (T.IsValid()) Out.Add(T.Get());
	}
	return Out;
}

TArray<AActor*> UWWOWorldSubsystem::LivingTargets(const UObject* For) const
{
	TArray<AActor*> Out;
	for (const TWeakObjectPtr<AActor>& T : Targets)
	{
		const IWWOTargetable* Target = Cast<IWWOTargetable>(T.Get());
		if (Target && !Target->IsDead()) Out.Add(T.Get());
	}
	if (For && IsPvP(this))
	{
		for (AWWOCharacter* P : GetPlayers())
		{
			if (!P->Health->bDead && CanHurt(For, P)) Out.Add(P);
		}
	}
	return Out;
}

// --- Area queries ------------------------------------------------------------------------------

namespace
{
	// A query's victims, gathered one group at a time just before that group is visited (as the loops
	// it replaces gathered LivingTargets / LivingEnemies / GetPlayers / LivingTeammates), so a victim
	// dying or going away mid-visit changes nothing. Held on the stack: no heap use up to 64.
	using FAreaList = TArray<AActor*, TInlineAllocator<64>>;

	struct FAreaMeasured
	{
		FVector Point = FVector::ZeroVector;
		double Distance = 0.0;
		float PartRadius = 0.f;
	};

	// The distance each area effect measured, computed the way it computed it.
	FAreaMeasured MeasureTo(const AActor* Actor, const IWWOTargetable* Target, EWWOAreaMeasure Measure, const FWWOAreaQuery& Q)
	{
		FAreaMeasured Out;
		if (!Target) Measure = EWWOAreaMeasure::Centre;
		switch (Measure)
		{
		case EWWOAreaMeasure::Feet: Out.Point = Target->FeetLocation(); break;
		case EWWOAreaMeasure::Chest: Out.Point = Target->FeetLocation() + FVector(0, 0, 80.f); break;
		case EWWOAreaMeasure::BodyCenter: Out.Point = Target->BodyCenter(); break;
		case EWWOAreaMeasure::LastPart:
		{
			const FWWOAimPart Body = Target->GetAimParts().Last();
			Out.Point = Body.Position;
			Out.PartRadius = Body.Radius;
			break;
		}
		case EWWOAreaMeasure::NearestPart:
		{
			// The closest part of it to the centre, by its surface (kept in a float, as the blast did).
			FWWOAimPart BestPart;
			float BestDistance = TNumericLimits<float>::Max();
			for (const FWWOAimPart& Part : Target->GetAimParts())
			{
				const float D = FMath::Max(FVector::Dist(Part.Position, Q.Center) - Part.Radius, 0.f);
				if (D < BestDistance)
				{
					BestDistance = D;
					BestPart = Part;
				}
			}
			Out.Point = BestPart.Position;
			Out.PartRadius = BestPart.Radius;
			Out.Distance = BestDistance;
			return Out;
		}
		default: Out.Point = Actor->GetActorLocation(); break;
		}
		Out.Distance = Q.bFlat ? FVector2D(Out.Point.X - Q.Center.X, Out.Point.Y - Q.Center.Y).Size() : FVector::Dist(Out.Point, Q.Center);
		return Out;
	}

	bool Within(const FWWOAreaQuery& Q, const FAreaMeasured& M)
	{
		const double D = Q.bFloatDistance ? (double)(float)M.Distance : M.Distance;
		if (!(Q.bInclusive ? D <= Q.Radius : D < Q.Radius)) return false;
		if (!Q.bFlat) return true;
		const double Height = FMath::Abs(M.Point.Z - Q.Center.Z);
		return Q.bHeightInclusive ? Height <= Q.MaxHeight : Height < Q.MaxHeight;
	}

	bool InSight(const UWorld* World, const FWWOAreaQuery& Q, const AActor* Actor, const FVector& Point)
	{
		if (!Q.SightParams) return true;
		FHitResult Block;
		return !(World->LineTraceSingleByChannel(Block, Q.SightFrom, Point, ECC_Visibility, *Q.SightParams) && Block.GetActor() != Actor);
	}
}

// Every victim the query includes, group by group in the query's order, with how to measure it.
// Visit(Actor, Target, Measure) returns false to stop.
template <typename FVisit>
static void WalkArea(const UWWOWorldSubsystem& WS, const TArray<TWeakObjectPtr<AActor>>& Targets, const TArray<TWeakObjectPtr<AWWOAlly>>& Teammates,
	const FWWOAreaQuery& Q, FVisit&& Visit)
{
	const EWWOAreaMeasure PlayersMeasure = Q.PlayersMeasure == EWWOAreaMeasure::Same ? Q.Measure : Q.PlayersMeasure;
	auto VisitTeammates = [&]() -> bool
	{
		if (!Q.bTeammates) return true;
		FAreaList List;
		for (const TWeakObjectPtr<AWWOAlly>& A : Teammates) if (A.IsValid() && !A->IsDown()) List.Add(A.Get());
		for (AActor* A : List) if (!Visit(A, nullptr, EWWOAreaMeasure::Centre)) return false;
		return true;
	};
	if (Q.bTeammatesFirst && !VisitTeammates()) return;
	if (Q.Targets != EWWOAreaTargets::None)
	{
		FAreaList List;
		for (const TWeakObjectPtr<AActor>& T : Targets)
		{
			if (Q.Targets == EWWOAreaTargets::Enemies)
			{
				const AWWOEnemy* Enemy = Cast<AWWOEnemy>(T.Get());
				if (Enemy && !Enemy->IsDead()) List.Add(T.Get());
			}
			else if (const IWWOTargetable* Target = Cast<IWWOTargetable>(T.Get()); Target && !Target->IsDead()) List.Add(T.Get());
		}
		for (AActor* A : List) if (!Visit(A, Cast<IWWOTargetable>(A), Q.Measure)) return;
	}
	if (Q.Players != EWWOAreaPlayers::None && (Q.Players != EWWOAreaPlayers::FoesOf || UWWOWorldSubsystem::IsPvP(&WS)))
	{
		FAreaList List;
		for (TActorIterator<AWWOCharacter> It(WS.GetWorld()); It; ++It) List.Add(*It);
		const AWWOCharacter* Own = Q.Players == EWWOAreaPlayers::FriendsOf ? UWWOWorldSubsystem::PlayerOf(Q.For) : nullptr;
		for (AActor* A : List)
		{
			AWWOCharacter* P = static_cast<AWWOCharacter*>(A);
			// Who counts is decided as each one comes up, as the GetPlayers loops did.
			if (Q.Players == EWWOAreaPlayers::FoesOf && (P->Health->bDead || !UWWOWorldSubsystem::CanHurt(Q.For, P))) continue;
			if (Q.Players == EWWOAreaPlayers::FriendsOf && (P == Own || P->Health->bDead || UWWOWorldSubsystem::CanHurt(Q.For, P))) continue;
			if (!Visit(A, static_cast<IWWOTargetable*>(P), PlayersMeasure)) return;
		}
	}
	if (!Q.bTeammatesFirst) VisitTeammates();
}

void UWWOWorldSubsystem::ForEachTargetInRadius(const FWWOAreaQuery& Q, TFunctionRef<bool(const FWWOAreaHit&)> Visit) const
{
	const UWorld* World = GetWorld();
	WalkArea(*this, Targets, Teammates, Q, [&](AActor* Actor, IWWOTargetable* Target, EWWOAreaMeasure Measure)
	{
		const FAreaMeasured M = MeasureTo(Actor, Target, Measure, Q);
		if (!Within(Q, M) || !InSight(World, Q, Actor, M.Point)) return true;
		FWWOAreaHit Hit;
		Hit.Actor = Actor;
		Hit.Target = Target;
		Hit.Point = M.Point;
		Hit.Distance = (float)M.Distance;
		Hit.PartRadius = M.PartRadius;
		return Visit(Hit);
	});
}

bool UWWOWorldSubsystem::NearestTargetInRadius(const FWWOAreaQuery& Q, TFunctionRef<bool(const AActor*)> Admit, FWWOAreaHit& Out) const
{
	const UWorld* World = GetWorld();
	bool bFound = false;
	double Best = 0.0;
	WalkArea(*this, Targets, Teammates, Q, [&](AActor* Actor, IWWOTargetable* Target, EWWOAreaMeasure Measure)
	{
		if (!Admit(Actor)) return true;
		const FAreaMeasured M = MeasureTo(Actor, Target, Measure, Q);
		const double D = Q.bFloatDistance ? (double)(float)M.Distance : M.Distance;
		if (!Within(Q, M) || (bFound && !(D < Best))) return true;
		if (!InSight(World, Q, Actor, M.Point)) return true; // traced only when it would be the nearest
		bFound = true;
		Best = D;
		Out.Actor = Actor;
		Out.Target = Target;
		Out.Point = M.Point;
		Out.Distance = (float)M.Distance;
		Out.PartRadius = M.PartRadius;
		return true;
	});
	return bFound;
}

FWWOAimPick UWWOWorldSubsystem::BestAimPart(const UObject* For, TFunctionRef<bool(const AActor*, const FWWOAimPart&, float, float&)> Score,
	TFunctionRef<bool(const FVector&, FHitResult&)> Sight) const
{
	// LivingTargets(For), gathered once before any part is looked at.
	FAreaList List;
	for (const TWeakObjectPtr<AActor>& T : Targets)
	{
		const IWWOTargetable* Target = Cast<IWWOTargetable>(T.Get());
		if (Target && !Target->IsDead()) List.Add(T.Get());
	}
	if (For && IsPvP(this))
	{
		for (TActorIterator<AWWOCharacter> It(GetWorld()); It; ++It)
		{
			if (!It->Health->bDead && CanHurt(For, *It)) List.Add(*It);
		}
	}
	FWWOAimPick Best;
	for (AActor* Actor : List)
	{
		const IWWOTargetable* Target = Cast<IWWOTargetable>(Actor);
		for (const FWWOAimPart& Part : Target->GetAimParts())
		{
			float S = 0.f;
			if (!Score(Actor, Part, Best.Score, S) || S >= Best.Score) continue;
			FHitResult Check;
			if (!Sight(Part.Position, Check) || Check.GetActor() != Actor) continue;
			Best.Actor = Actor;
			Best.Part = Part;
			Best.Hit = Check;
			Best.Score = S;
		}
	}
	return Best;
}

TWeakObjectPtr<AActor> UWWOWorldSubsystem::CurrentAttacker;

bool UWWOWorldSubsystem::bAttackerSawHit = false;
int32 UWWOWorldSubsystem::HitsMarked = 0;

void UWWOWorldSubsystem::ConfirmHit(AActor* Victim, bool bCritical, bool bKilled)
{
	AWWOCharacter* By = Cast<AWWOCharacter>(CurrentAttacker.Get());
	if (!By || By == Victim || !By->IsPlayerControlled()) return; // bots and allies have no reticle
	if (By->IsLocallyControlled())
	{
		++HitsMarked;
		if (!bAttackerSawHit) By->Weapon->OnHitConfirmed.Broadcast(bCritical, bKilled); // else the gun or fist marks it
	}
	else if (By->HasAuthority() && !bAttackerSawHit) By->ClientHitMarker(bKilled);
}

FWWODamageBy::FWWODamageBy(const UObject* Attacker)
{
	Previous = UWWOWorldSubsystem::CurrentAttacker;
	AWWOCharacter* P = UWWOWorldSubsystem::PlayerOf(Attacker);
	UWWOWorldSubsystem::CurrentAttacker = P ? static_cast<AActor*>(P) : const_cast<AActor*>(Cast<AActor>(Attacker));
}

FWWODamageBy::~FWWODamageBy()
{
	UWWOWorldSubsystem::CurrentAttacker = Previous;
}

bool UWWOWorldSubsystem::IsPvP(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const AWWOGameState* GS = World ? World->GetGameState<AWWOGameState>() : nullptr;
	return GS && GS->bPvP;
}

int32 UWWOWorldSubsystem::TeamOf(const AActor* Player)
{
	const APawn* Pawn = Cast<APawn>(Player);
	const AWWOPlayerState* PS = Pawn ? Pawn->GetPlayerState<AWWOPlayerState>() : nullptr;
	return PS ? PS->Team : -1;
}

bool UWWOWorldSubsystem::CanHurt(const UObject* Attacker, const AActor* Victim)
{
	// An AI teammate is on every player's side in co-op. PvP has none (the game mode removes them) and
	// they have no team, so there it stays as it was.
	if (Cast<AWWOAlly>(Victim)) return IsPvP(Victim) || !PlayerOf(Attacker);
	if (!Cast<AWWOCharacter>(Victim)) return true; // enemies, dummies
	if (!IsPvP(Victim)) return false;              // co-op: players never hurt each other
	const AWWOCharacter* By = PlayerOf(Attacker);
	if (!By || By == Victim) return false;
	const int32 A = TeamOf(By), B = TeamOf(Victim);
	return A >= 0 && B >= 0 && A != B;
}

AWWOCharacter* UWWOWorldSubsystem::PlayerOf(const UObject* Context)
{
	for (int32 i = 0; i < 5 && Context; ++i)
	{
		if (const AWWOCharacter* C = Cast<AWWOCharacter>(Context)) return const_cast<AWWOCharacter*>(C);
		if (const UActorComponent* Comp = Cast<UActorComponent>(Context)) Context = Comp->GetOwner();
		else if (const AActor* A = Cast<AActor>(Context)) Context = A->GetOwner() ? static_cast<const UObject*>(A->GetOwner()) : A->GetInstigator();
		else return nullptr;
	}
	return nullptr;
}

TArray<AWWOEnemy*> UWWOWorldSubsystem::LivingEnemies() const
{
	TArray<AWWOEnemy*> Out;
	for (const TWeakObjectPtr<AActor>& T : Targets)
	{
		AWWOEnemy* Enemy = Cast<AWWOEnemy>(T.Get());
		if (Enemy && !Enemy->IsDead()) Out.Add(Enemy);
	}
	return Out;
}

TArray<AWWOAlly*> UWWOWorldSubsystem::LivingTeammates() const
{
	TArray<AWWOAlly*> Out;
	for (const TWeakObjectPtr<AWWOAlly>& A : Teammates)
	{
		if (A.IsValid() && !A->IsDown()) Out.Add(A.Get());
	}
	return Out;
}

TArray<AWWOAlly*> UWWOWorldSubsystem::AllTeammates() const
{
	TArray<AWWOAlly*> Out;
	for (const TWeakObjectPtr<AWWOAlly>& A : Teammates)
	{
		if (A.IsValid()) Out.Add(A.Get());
	}
	return Out;
}

AWWOCharacter* UWWOWorldSubsystem::GetPlayer() const
{
	return Cast<AWWOCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0));
}

TArray<AWWOCharacter*> UWWOWorldSubsystem::GetPlayers() const
{
	TArray<AWWOCharacter*> Out;
	for (TActorIterator<AWWOCharacter> It(GetWorld()); It; ++It) Out.Add(*It);
	return Out;
}

void UWWOWorldSubsystem::ResetTargets()
{
	for (AActor* Actor : AllTargets())
	{
		if (AWWOTarget* Dummy = Cast<AWWOTarget>(Actor))
		{
			Dummy->Revive(TargetHealth);
		}
		else if (IWWOTargetable* Target = Cast<IWWOTargetable>(Actor))
		{
			Target->ResetTarget();
		}
	}
	for (AWWOAlly* Ally : AllTeammates())
	{
		Ally->ResetAlly();
	}
}

void UWWOWorldSubsystem::SetTargetHealth(float Value)
{
	TargetHealth = Value;
	ResetTargets(); // new health applies right away
}

void UWWOWorldSubsystem::SpawnDamageNumber(float Damage, bool bCritical, const FVector& Point, FLinearColor Color)
{
	// Only your own damage shows as numbers: not a teammate's, a bot's or an enemy player's (the
	// host works out everybody's hits, so it would see them all). Damage with nobody behind it
	// (CurrentAttacker unset) still shows.
	if (const AActor* By = CurrentAttacker.Get(); By && By != GetPlayer())
	{
		UE_LOG(LogWWO, VeryVerbose, TEXT("Damage number hidden: %s's"), *By->GetName());
		return;
	}
	UE_LOG(LogWWO, VeryVerbose, TEXT("Damage number shown: %s's"), *GetNameSafe(CurrentAttacker.Get()));
	ShowText(FString::FromInt(FMath::RoundToInt(Damage)), bCritical, Point, Color);
}

void UWWOWorldSubsystem::ShowText(const FString& Text, bool bCritical, const FVector& Point, FLinearColor Color)
{
	FWWOFloatingText Entry;
	Entry.Text = Text;
	Entry.Start = Point + FVector(0, 0, 20.f);
	Entry.Color = Color.A > 0.f ? Color : (bCritical ? CritColor() : FLinearColor::White);
	Entry.bCritical = bCritical;
	Entry.Jitter = FMath::FRandRange(-15.f, 15.f); // along world Y (Godot x)
	Texts.Add(Entry);
}

void UWWOWorldSubsystem::Tick(float DeltaTime)
{
	WWO_PROF("WorldSubsystem");
	for (FWWOFloatingText& T : Texts) T.Age += DeltaTime;
	// Godot's tween: rises for 0.7 s, fades from 0.25 s over 0.7 s, then frees itself.
	Texts.RemoveAll([](const FWWOFloatingText& T) { return T.Age >= FWWOFloatingText::Lifetime; });
}
