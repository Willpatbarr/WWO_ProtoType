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
