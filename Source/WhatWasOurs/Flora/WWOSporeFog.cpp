#include "Flora/WWOSporeFog.h"
#include "Net/UnrealNetwork.h"
#include "Enemies/WWOEnemy.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOAbilityComponent.h"
#include "FX/WWOFX.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"

namespace
{
	constexpr float FogTick = 0.5f; // seconds between damage ticks
	constexpr float Fade = 0.6f;    // seconds to fade in and out
}

AWWOSporeFog::AWWOSporeFog()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	bReplicates = true;
}

void AWWOSporeFog::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AWWOSporeFog, Radius, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AWWOSporeFog, Duration, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AWWOSporeFog, bBlocksSight, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AWWOSporeFog, Opacity, COND_InitialOnly);
}

AWWOSporeFog* AWWOSporeFog::SpawnFog(UWorld* World, const FVector& At, float RadiusM, float Seconds, bool bThick, AActor* InOwner)
{
	FActorSpawnParameters Params;
	Params.Owner = InOwner;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	AWWOSporeFog* Fog = World->SpawnActor<AWWOSporeFog>(At, FRotator::ZeroRotator, Params);
	if (!Fog) return nullptr;
	Fog->Radius = RadiusM;
	Fog->Duration = Seconds * (1.f + UWWOLoadoutSubsystem::Bonus(InOwner ? static_cast<const UObject*>(InOwner) : World, "fog_longer")); // Longer Fog trait (the owner's)
	Fog->bBlocksSight = bThick;
	Fog->Opacity = bThick ? 0.32f : 0.22f;
	Fog->FinishSpawning(FTransform(At));
	return Fog;
}

void AWWOSporeFog::BeginPlay()
{
	Super::BeginPlay();
	if (UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(this)) WS->RegisterFog(this);
	// Godot's offsets, converted (x, y, z) -> (-z, x, y).
	const FVector Offsets[] = { FVector::ZeroVector, FVector(-0.2f, 0.35f, 0.1f), FVector(0.25f, -0.3f, 0.05f), FVector(0.3f, 0.1f, 0.3f), FVector(-0.3f, -0.2f, 0.25f) };
	for (const FVector& Offset : Offsets)
	{
		UStaticMeshComponent* Puff = WWOFX::AddSoftSphere(this, GetRootComponent(), Color(), 0.f, 0.9f, false);
		if (!Material) Material = WWOFX::MID(Puff);
		else Puff->SetMaterial(0, Material);
		Puff->SetRelativeLocation(Offset * Radius * WWO::M);
		const FVector Size = FVector(1.f, 1.f, 0.7f) * Radius * WWO::M / 50.f * FMath::FRandRange(0.7f, 0.85f);
		Puff->SetRelativeScale3D(Size);
		Puffs.Add(Puff);
		Sizes.Add(Size);
	}
	// Where it ends, drawn on the floor under it, so you know when you're inside.
	FVector Floor;
	if (WWOFX::GroundBelow(GetWorld(), GetActorLocation(), Radius * WWO::M + 200.f, Floor, this))
	{
		Edge = WWOFX::AddGroundDisc(this, GetRootComponent(), WWO::Lightened(Color(), 0.25f), 0.05f, 0.08f, 0.5f);
		Edge->SetWorldLocation(Floor + FVector(0, 0, 3.f));
		const float S = Radius * WWO::M / 50.f;
		Edge->SetWorldScale3D(FVector(S, S, 1.f));
	}
	// Wisps of it curling upward.
	if (AWWOParticles* Wisps = AWWOParticles::Burst(GetWorld(), GetActorLocation() - FVector(0, 0, Radius * 30.f), WWO::WithAlpha(WWO::Lightened(Color(), 0.2f), 0.3f),
		FMath::Clamp(FMath::RoundToInt(Radius * 3.f), 4, 16), 3.f, 0.1f, 0.35f, Radius * 0.22f, FVector(0, 0, 0.25f), EWWOParticleStyle::Smoke, false))
	{
		Wisps->bOneShot = false;
		Wisps->Shape = EWWOEmitShape::Sphere;
		Wisps->ShapeRadius = Radius * 0.7f;
		Wisps->Start();
		Smoke = Wisps;
	}
	if (AWWOParticles* P = AWWOParticles::Burst(GetWorld(), GetActorLocation(), WWO::WithAlpha(WWO::Lightened(Color(), 0.45f), 0.9f),
		FMath::RoundToInt(Radius * 12.f), 2.5f, 0.05f, 0.3f, 0.07f, FVector(0, 0, 0.05f), EWWOParticleStyle::Glow, false))
	{
		P->bOneShot = false;
		P->Shape = EWWOEmitShape::Sphere;
		P->ShapeRadius = Radius * 0.8f;
		P->Start();
		Motes = P;
	}
}

void AWWOSporeFog::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(this)) WS->UnregisterFog(this);
	if (Motes.IsValid()) Motes->StopAndFade();
	if (Smoke.IsValid()) Smoke->StopAndFade();
	Super::EndPlay(Reason);
}

float AWWOSporeFog::Strength() const
{
	return FMath::Clamp(FMath::Min(Age / Fade, (Duration - Age) / Fade), 0.f, 1.f);
}

bool AWWOSporeFog::Contains(const UWorld* World, const FVector& Point)
{
	const UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(World);
	if (!WS) return false;
	for (const TWeakObjectPtr<AWWOSporeFog>& Fog : WS->Fogs())
	{
		if (Fog.IsValid() && Fog->Strength() > 0.3f && FVector::Dist(Point, Fog->GetActorLocation()) <= Fog->Radius * WWO::M) return true;
	}
	return false;
}

bool AWWOSporeFog::Blocks(const UWorld* World, const FVector& From, const FVector& To)
{
	const UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(World);
	if (!WS) return false;
	for (const TWeakObjectPtr<AWWOSporeFog>& Fog : WS->Fogs())
	{
		if (!Fog.IsValid() || !Fog->bBlocksSight || Fog->Strength() < 0.5f) continue;
		const FVector Closest = FMath::ClosestPointOnSegment(Fog->GetActorLocation(), From, To);
		if (FVector::Dist(Closest, Fog->GetActorLocation()) <= Fog->Radius * WWO::M * 0.9f) return true;
	}
	return false;
}

void AWWOSporeFog::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (Age >= Duration)
	{
		if (HasAuthority()) Destroy(); // a joiner's copy goes when the host's does
		return;
	}
	if (Material) Material->SetScalarParameterValue("Alpha", Opacity * 2.2f * Strength()); // the soft edges thin it out
	if (Duration - Age < Fade * 2.f && Motes.IsValid()) Motes->bEmitting = false;
	if (Duration - Age < Fade * 2.f && Smoke.IsValid()) Smoke->bEmitting = false;
	if (Edge) if (UMaterialInstanceDynamic* EdgeMat = WWOFX::MID(Edge)) EdgeMat->SetScalarParameterValue("Alpha", 0.75f * Strength());
	for (int32 i = 0; i < Puffs.Num(); ++i) // a slow churn, each puff gently swelling and shrinking
	{
		Puffs[i]->AddRelativeRotation(FRotator(0.f, FMath::RadiansToDegrees(DeltaSeconds * (0.2f + 0.1f * i)), 0.f));
		Puffs[i]->SetRelativeScale3D(Sizes[i] * (1.f + 0.05f * FMath::Sin(Age * 1.3f + i * 1.7f)));
	}
	// Spore Fog Damage: hurts the rank and file (Swarm and Soldiers) inside it.
	if (!HasAuthority()) return; // the damage is the host's
	TickLeft -= DeltaSeconds;
	if (TickLeft > 0.f) return;
	TickLeft = FogTick;
	const AWWOCharacter* Player = GetOwner() ? Cast<AWWOCharacter>(GetOwner()) : UWWOWorldSubsystem::Get(this)->GetPlayer(); // whose Spore Fog Damage
	const UWWOAbilityData* Ability = Player ? Player->Abilities->Equipped("spore_fog_damage") : nullptr;
	if (!Ability || Strength() < 0.3f) return;
	for (AWWOEnemy* Enemy : UWWOWorldSubsystem::Get(this)->LivingEnemies())
	{
		if (!Enemy->IsRankAndFile()) continue;
		const FVector Chest = Enemy->FeetLocation() + FVector(0, 0, 80.f);
		if (FVector::Dist(Chest, GetActorLocation()) <= Radius * WWO::M)
		{
			const float Dps = Ability->FogDps * (1.f + UWWOLoadoutSubsystem::Bonus(this, "poison_damage")); // Stronger Poison
			Enemy->TakeHit(Dps * FogTick, false, Chest, WWO::Lightened(Color(), 0.3f));
		}
	}
}
