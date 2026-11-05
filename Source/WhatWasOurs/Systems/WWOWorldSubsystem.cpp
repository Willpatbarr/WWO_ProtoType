#include "Systems/WWOWorldSubsystem.h"
#include "Enemies/WWOEnemy.h"
#include "Allies/WWOAlly.h"
#include "Flora/WWOSporeFog.h"
#include "Player/WWOCharacter.h"
#include "World/WWOTarget.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

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

TArray<AActor*> UWWOWorldSubsystem::LivingTargets() const
{
	TArray<AActor*> Out;
	for (const TWeakObjectPtr<AActor>& T : Targets)
	{
		const IWWOTargetable* Target = Cast<IWWOTargetable>(T.Get());
		if (Target && !Target->IsDead()) Out.Add(T.Get());
	}
	return Out;
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
	for (FWWOFloatingText& T : Texts) T.Age += DeltaTime;
	// Godot's tween: rises for 0.7 s, fades from 0.25 s over 0.7 s, then frees itself.
	Texts.RemoveAll([](const FWWOFloatingText& T) { return T.Age >= FWWOFloatingText::Lifetime; });
}
