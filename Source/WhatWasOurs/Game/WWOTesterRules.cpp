// The firing tester's rules (Game/WWOTesterRules.h). Moved from AWWOGameMode (its Begin branch and
// UpdateTester) as they were.

#include "Game/WWOTesterRules.h"
#include "Game/WWOGameMode.h"
#include "Player/WWOCharacter.h"
#include "Enemies/WWOEnemy.h"
#include "Allies/WWOAlly.h"
#include "World/WWOTarget.h"
#include "Systems/WWOWorldSubsystem.h"
#include "WWOTypes.h"
#include "WhatWasOurs.h"
#include "EngineUtils.h"

void FWWOTesterRules::Begin()
{
	// Nothing to fight. The map's own dummies go too; one is put in front of the player once they
	// are in (Update).
	UWorld* World = Mode.GetWorld();
	for (TActorIterator<AWWOEnemy> It(World); It; ++It) It->Destroy();
	for (TActorIterator<AWWOAlly> It(World); It; ++It) It->Destroy();
	for (TActorIterator<AWWOTarget> It(World); It; ++It) It->Destroy();
	State = EWWOWaveState::Idle;
}

void FWWOTesterRules::Update(float DeltaSeconds)
{
	// One dummy straight ahead of where the player starts, far enough to read the kick against.
	UWorld* World = Mode.GetWorld();
	AWWOCharacter* P = UWWOWorldSubsystem::Get(World)->GetPlayer();
	if (Target.IsValid() || !P || !Mode.bStarted) return;
	if (!bHomeSet)
	{
		bHomeSet = true;
		Home = P->GetActorTransform();
	}
	const FVector Forward = FRotator(0.f, Home.Rotator().Yaw, 0.f).Vector();
	FVector At = Home.GetLocation() + Forward * TargetDistance * WWO::M;
	// Stand it on the floor under that point.
	FHitResult Floor;
	if (World->LineTraceSingleByChannel(Floor, At + FVector(0, 0, 300.f), At - FVector(0, 0, 1000.f), ECC_Visibility)) At = Floor.ImpactPoint;
	Target = World->SpawnActor<AWWOTarget>(AWWOTarget::StaticClass(), At, FRotator(0.f, Home.Rotator().Yaw + 180.f, 0.f), WWO::AlwaysSpawn());
	UE_LOG(LogWWO, Display, TEXT("Tester: target dummy %.0f m ahead of the start (%s)"), TargetDistance, Target.IsValid() ? TEXT("placed") : TEXT("FAILED"));
}
