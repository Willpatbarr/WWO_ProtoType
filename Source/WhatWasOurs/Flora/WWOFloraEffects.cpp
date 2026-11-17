#include "Flora/WWOFloraEffects.h"
#include "Player/WWOProf.h"
#include "Components/PointLightComponent.h"
#include "Flora/WWOSporeFog.h"
#include "Game/WWOGameState.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOAbilityComponent.h"
#include "FX/WWOFX.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "WWOIds.h"
#include "Player/WWOTestTally.h"

namespace
{
	const FLinearColor BarkColor(0.42f, 0.3f, 0.19f);
}

// --- Spore pool -------------------------------------------------------------------------------

AWWOSporePool::AWWOSporePool()
	: Life(0.25f, 0.f, false) // grows in over 0.25 s, fades over the last 0.5 s of its duration (Tick), no tail
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	bReplicates = true;
}

void AWWOSporePool::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AWWOSporePool, Radius, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AWWOSporePool, Duration, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AWWOSporePool, Color, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AWWOSporePool, Flavor, COND_InitialOnly);
}

void AWWOSporePool::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority()) FWWOTestTally::Count(&FWWOTestTally::PoolsSeen);
	const FVector At = GetActorLocation();
	Disc = WWOFX::AddGroundDisc(this, GetRootComponent(), Color, 0.1f, 0.55f, 0.8f);
	Disc->SetRelativeLocation(FVector(0, 0, 4.f));
	Mat = WWOFX::MID(Disc);
	// A low haze of its color over the surface, and its light on what stands in it.
	Haze = WWOFX::AddSoftSphere(this, GetRootComponent(), Color, 0.22f, 1.1f, true, false, 0.1f);
	Haze->SetRelativeLocation(FVector(0, 0, 12.f));
	Glow = WWOFX::AddPointLight(this, GetRootComponent(), Color, 2200.f, Radius * 1.6f * WWO::M, FVector(0, 0, 60.f));
	if (AWWOParticles* B = AWWOParticles::Burst(GetWorld(), At, WWO::WithAlpha(WWO::Lightened(Color, 0.3f), 0.8f), FMath::RoundToInt(Radius * 10.f),
		1.2f, 0.3f, 0.9f, 0.08f, FVector(0, 0, 0.6f), EWWOParticleStyle::Glow, false))
	{
		B->bOneShot = false;
		B->Direction = FVector::UpVector;
		B->SpreadDegrees = 15.f;
		B->Shape = EWWOEmitShape::Ring;
		B->ShapeRadius = Radius * 0.9f;
		B->Start();
		Bubbles = B;
	}
}

AWWOSporePool* AWWOSporePool::Spawn(UWorld* World, const FVector& At, float RadiusM, float InDps, float InDuration, const FLinearColor& InColor, FName InFlavor, AActor* InThrower)
{
	AWWOSporePool* Pool = World->SpawnActor<AWWOSporePool>(At, FRotator::ZeroRotator, WWO::AlwaysSpawnDeferred(InThrower));
	if (!Pool) return nullptr;
	Pool->Radius = RadiusM;
	Pool->Dps = InDps;
	Pool->Duration = InDuration;
	Pool->Color = InColor;
	Pool->Flavor = InFlavor;
	Pool->Thrower = InThrower;
	Pool->FinishSpawning(FTransform(At)); // BeginPlay builds the looks
	return Pool;
}

void AWWOSporePool::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Bubbles.IsValid()) Bubbles->StopAndFade();
	Super::EndPlay(Reason);
}

void AWWOSporePool::Tick(float DeltaSeconds)
{
	WWO_PROF("SporePool");
	Super::Tick(DeltaSeconds);
	if (Life.Advance(DeltaSeconds, Duration) == EWWOLifeStep::Over)
	{
		if (!FWWOEffectLife::End(*this)) SetActorHiddenInGame(true); // a joiner's copy hides until the host's goes
		return;
	}
	// Grows in quickly, shrinks and fades at the end.
	const float Grow = Life.GrowIn();
	const float FadeOut = Life.LeftOf(Duration, 0.5f);
	const float Scale = Radius * WWO::M / 50.f * FMath::Lerp(0.2f, 1.f, Grow) * FadeOut;
	Disc->SetRelativeScale3D(FVector(Scale, Scale, 1.f));
	if (Mat) Mat->SetScalarParameterValue("Alpha", FadeOut);
	if (Haze)
	{
		// Breathes a little, and goes with the pool.
		const float Breath = 1.f + 0.06f * FMath::Sin(Life.Age * 2.3f);
		Haze->SetRelativeScale3D(FVector(Scale * 0.95f * Breath, Scale * 0.95f * Breath, Scale * 0.22f));
		if (UMaterialInstanceDynamic* HazeMat = WWOFX::MID(Haze)) HazeMat->SetScalarParameterValue("Alpha", 0.22f * FadeOut);
	}
	if (Glow) Glow->SetIntensity(2200.f * FadeOut * (0.85f + 0.15f * FMath::Sin(Life.Age * 5.f)));
	if (FadeOut < 1.f && Bubbles.IsValid()) Bubbles->bEmitting = false;
	if (Flavor == WWOIds::Flora::VoltVine && Life.Age < Duration - 0.4f)
	{
		// The VoltVine pool crackles with little arcs along the surface.
		NextArc -= DeltaSeconds;
		if (NextArc <= 0.f)
		{
			NextArc = FMath::FRandRange(0.08f, 0.22f);
			const float A = FMath::FRand() * UE_TWO_PI;
			const float B = A + FMath::FRandRange(0.6f, 1.8f) * (FMath::FRand() < 0.5f ? 1.f : -1.f);
			const FVector C = GetActorLocation() + FVector(0, 0, 10.f);
			const float R = Radius * WWO::M;
			AWWOLightningBolt::Spawn(GetWorld(), C + FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * R * FMath::FRandRange(0.1f, 0.85f),
				C + FVector(FMath::Cos(B), FMath::Sin(B), 0.f) * R * FMath::FRandRange(0.3f, 0.9f), Color, FMath::FRandRange(0.1f, 0.18f));
		}
	}
	if (!HasAuthority()) return; // the damage is the host's
	TickLeft -= DeltaSeconds;
	if (TickLeft > 0.f) return;
	TickLeft += 0.5f; // not reset: a late frame doesn't cost damage
	FWWODamageBy By(this);
	// Targets (and in PvP the thrower's foes) standing in it: feet within the radius on the ground, and
	// less than 1.5 m above or below it.
	FWWOAreaQuery Q;
	Q.Center = GetActorLocation();
	Q.Radius = Radius * WWO::M;
	Q.bFlat = true;
	Q.MaxHeight = 150.f;
	Q.Measure = EWWOAreaMeasure::Feet;
	Q.For = this;
	UWWOWorldSubsystem::Get(this)->ForEachTargetInRadius(Q, [this](const FWWOAreaHit& Hit)
	{
		Hit.Target->TakeHit(Dps * 0.5f, false, Hit.Point + FVector(0, 0, 30.f), Color);
		return true;
	});
}

// --- Spore patch ------------------------------------------------------------------------------

AWWOSporePatch::AWWOSporePatch()
	: Life(0.25f, 0.3f, false) // grows in over 0.25 s, holds its duration, shrinks over 0.3 s after it
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	bReplicates = true;
}

void AWWOSporePatch::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AWWOSporePatch, Radius, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AWWOSporePatch, Duration, COND_InitialOnly);
}

AWWOSporePatch* AWWOSporePatch::Spawn(UWorld* World, const FVector& At, float RadiusM, float InDuration, AActor* InOwner)
{
	AWWOSporePatch* Patch = World->SpawnActor<AWWOSporePatch>(At, FRotator::ZeroRotator, WWO::AlwaysSpawnDeferred(InOwner));
	if (!Patch) return nullptr;
	Patch->Radius = RadiusM;
	Patch->Duration = InDuration;
	Patch->FinishSpawning(FTransform(At)); // BeginPlay builds the looks
	AWWOSporeFog::SpawnFog(World, At + FVector(0, 0, 40.f), RadiusM, InDuration, false, InOwner);
	return Patch;
}

void AWWOSporePatch::BeginPlay()
{
	Super::BeginPlay();
	const FVector At = GetActorLocation();
	// A little mushroom: a pale stem and a glowing purple cap.
	UStaticMeshComponent* Stem = WWOFX::AddShape(this, GetRootComponent(), WWOFX::Cylinder(), TEXT("M_WWO_Solid"), FLinearColor(0.85f, 0.82f, 0.7f));
	Stem->SetRelativeLocation(FVector(0, 0, 12.5f));
	Stem->SetRelativeScale3D(FVector(0.12f, 0.12f, 0.25f));
	UStaticMeshComponent* Cap = WWOFX::AddShape(this, GetRootComponent(), WWOFX::Sphere(), TEXT("M_WWO_Solid"), AWWOSporeFog::Color(), 1.5f);
	Cap->SetRelativeLocation(FVector(0, 0, 25.f));
	Cap->SetRelativeScale3D(FVector(0.32f, 0.32f, 0.16f));
	SetActorScale3D(FVector(0.01f));
	AWWOParticles::Burst(GetWorld(), At + FVector(0, 0, 30.f), WWO::WithAlpha(WWO::Lightened(AWWOSporeFog::Color(), 0.2f), 0.5f), 10, 0.9f, 0.8f, 2.f, 0.5f,
		FVector(0, 0, 0.3f), EWWOParticleStyle::Smoke);
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	if (P && P->IsLocallyControlled())
	{
		P->Abilities->LiveCount(WWOIds::Ability::SporePatches)++; // keeps the Spore Patches HUD box lit
		bCounted = true;
	}
}

void AWWOSporePatch::EndPlay(const EEndPlayReason::Type Reason)
{
	if (bCounted)
	{
		if (AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner()))
		{
			int32& Live = P->Abilities->LiveCount(WWOIds::Ability::SporePatches);
			Live = FMath::Max(Live - 1, 0);
		}
	}
	Super::EndPlay(Reason);
}

void AWWOSporePatch::Tick(float DeltaSeconds)
{
	WWO_PROF("SporePatch");
	Super::Tick(DeltaSeconds);
	const EWWOLifeStep Step = Life.Advance(DeltaSeconds, Duration);
	const float In = Life.GrowIn();
	const float Out = FMath::Clamp(Life.TailLeft(Duration), 0.01f, 1.f);
	SetActorScale3D(FVector(FMath::Max(In * Out, 0.01f)));
	if (Step == EWWOLifeStep::Over) FWWOEffectLife::End(*this); // a joiner's copy stays, shrunk, until the host's goes
}

// --- Poison -----------------------------------------------------------------------------------

UWWODamageOverTime::UWWODamageOverTime()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UWWODamageOverTime::Apply(AActor* Target, float DamagePerSecond, float Seconds)
{
	if (!Target) return;
	if (UWWODamageOverTime* Existing = Target->FindComponentByClass<UWWODamageOverTime>())
	{
		Existing->Dps = FMath::Max(Existing->Dps, DamagePerSecond);
		Existing->TimeLeft = FMath::Max(Existing->TimeLeft, Seconds);
		if (UWWOWorldSubsystem::CurrentAttacker.IsValid()) Existing->By = UWWOWorldSubsystem::CurrentAttacker;
		return;
	}
	UWWODamageOverTime* Poison = NewObject<UWWODamageOverTime>(Target);
	Poison->By = UWWOWorldSubsystem::CurrentAttacker;
	Poison->Dps = DamagePerSecond;
	Poison->TimeLeft = Seconds;
	Poison->RegisterComponent();
	// Purple drops falling off the poisoned target.
	if (AWWOParticles* D = AWWOParticles::Burst(Target->GetWorld(), Target->GetActorLocation(), Color(), 22, 0.9f, 0.1f, 0.5f, 0.13f,
		FVector(0, 0, -3.f), EWWOParticleStyle::Glow, false))
	{
		D->bOneShot = false;
		D->Shape = EWWOEmitShape::Sphere;
		D->ShapeRadius = 0.35f;
		D->AttachToActor(Target, FAttachmentTransformRules::KeepWorldTransform);
		D->Start();
		Poison->Drips = D;
	}
}

void UWWODamageOverTime::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	WWO_PROF("DamageOverTime");
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	IWWOTargetable* Target = Cast<IWWOTargetable>(GetOwner());
	if (!Target || Target->IsDead())
	{
		DestroyComponent();
		return;
	}
	if (Drips.IsValid()) Drips->SetActorLocation(Target->FeetLocation() + FVector(0, 0, 100.f));
	TimeLeft -= DeltaTime;
	TickLeft -= DeltaTime;
	if (TickLeft <= 0.f)
	{
		TickLeft += 0.5f; // not reset: a late frame doesn't cost damage
		FWWODamageBy Credit(By.Get());
		Target->TakeHit(Dps * 0.5f, false, Target->FeetLocation() + FVector(0, 0, 100.f), Color());
	}
	if (TimeLeft <= 0.f) DestroyComponent();
}

void UWWODamageOverTime::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (Drips.IsValid())
	{
		Drips->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Drips->StopAndFade(); // let the last drops fall, then go
	}
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

// --- Bark wall --------------------------------------------------------------------------------

AWWOBarkWall::AWWOBarkWall()
	: Life(0.25f, 0.4f, true) // rises over 0.25 s, stands its duration, sinks over 0.4 s on its own clock
{
	PrimaryActorTick.bCanEverTick = true;
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	Box->SetCollisionProfileName(TEXT("BlockAll"));
	SetRootComponent(Box);
	Visual = CreateDefaultSubobject<USceneComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Box);
	Tags.Add("Cover");
	bReplicates = true; // solid: everyone needs it where the host has it, or moves disagree
}

void AWWOBarkWall::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AWWOBarkWall, Width, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AWWOBarkWall, Height, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AWWOBarkWall, Duration, COND_InitialOnly);
}

void AWWOBarkWall::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority()) Build(); // the host's is built in Raise; this one arrives already standing in place
}

AWWOBarkWall* AWWOBarkWall::Raise(UWorld* World, const FVector& Center, const FVector& FacingFrom, float WidthM, float HeightM, float Seconds, AActor* InOwner)
{
	// On the ground under the blast, facing the thrower.
	FVector Ground = Center;
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Center + FVector(0, 0, 50.f), Center - FVector(0, 0, 600.f), ECC_Visibility)) Ground = Hit.ImpactPoint;
	FVector Toward = FacingFrom - Ground;
	Toward.Z = 0.f;
	const FRotator Facing = Toward.Size() > 10.f ? Toward.Rotation() : FRotator::ZeroRotator;
	AWWOBarkWall* Wall = World->SpawnActor<AWWOBarkWall>(Ground, Facing, WWO::AlwaysSpawnDeferred(InOwner));
	if (!Wall) return nullptr;
	Wall->Width = WidthM;
	Wall->Height = HeightM;
	Wall->Duration = Seconds;
	Wall->FinishSpawning(FTransform(Facing, Ground));
	Wall->Build();
	return Wall;
}

void AWWOBarkWall::Build()
{
	// The wall faces the thrower: its thin side (x) points at them.
	const FVector Extent(0.25f * WWO::M, Width * 0.5f * WWO::M, Height * 0.5f * WWO::M);
	Box->SetBoxExtent(Extent);
	if (HasAuthority()) Box->AddLocalOffset(FVector(0, 0, Extent.Z)); // a joiner's copy arrives already raised
	Visual->SetRelativeLocation(FVector::ZeroVector);
	UStaticMeshComponent* Slab = WWOFX::AddShape(this, Visual, WWOFX::Cube(), TEXT("M_WWO_Solid"), BarkColor);
	Slab->SetRelativeScale3D(FVector(0.5f, Width, Height));
	for (int32 i = 0; i < 5; ++i)
	{
		const float H = Height * FMath::FRandRange(0.85f, 1.05f);
		UStaticMeshComponent* Ridge = WWOFX::AddShape(this, Visual, WWOFX::Cube(), TEXT("M_WWO_Solid"), WWO::Darkened(BarkColor, 0.3f));
		Ridge->SetRelativeScale3D(FVector(0.62f, 0.18f, H));
		Ridge->SetRelativeLocation(FVector(0.f, FMath::Lerp(-Width * 0.4f, Width * 0.4f, i / 4.f) * WWO::M, (H - Height) * 0.5f * WWO::M));
	}
	Visual->SetRelativeScale3D(FVector(1.f, 1.f, 0.01f));
	Dust(1.f);
	if (AWWOParticles* Chips = AWWOParticles::Burst(GetWorld(), GetActorLocation() - FVector(0, 0, Height * 0.5f * WWO::M - 20.f), WWO::Lightened(BarkColor, 0.1f),
		18, 0.9f, 2.f, 5.f, 0.09f, FVector(0, 0, -14.f), EWWOParticleStyle::Chunk, false))
	{
		Chips->Shape = EWWOEmitShape::Box;
		Chips->BoxExtent = FVector(0.3f, Width * 0.5f, 0.1f);
		Chips->SetActorRotation(GetActorRotation());
		Chips->SpreadDegrees = 50.f;
		Chips->Start();
	}
}

void AWWOBarkWall::Dust(float Amount)
{
	if (AWWOParticles* D = AWWOParticles::Burst(GetWorld(), GetActorLocation() - FVector(0, 0, Height * 0.5f * WWO::M - 20.f), FLinearColor(0.55f, 0.47f, 0.38f, 0.45f),
		FMath::RoundToInt(Width * 6.f * Amount), 1.2f, 0.4f, 1.5f, 0.9f, FVector(0, 0, 0.25f), EWWOParticleStyle::Smoke, false))
	{
		D->Shape = EWWOEmitShape::Box;
		D->BoxExtent = FVector(0.4f, Width * 0.55f, 0.1f);
		D->SetActorRotation(GetActorRotation());
		D->Start();
	}
}

void AWWOBarkWall::Tick(float DeltaSeconds)
{
	WWO_PROF("BarkWall");
	Super::Tick(DeltaSeconds);
	const EWWOLifeStep Step = Life.Advance(DeltaSeconds, Duration);
	// The slab is centered on the collision box: grow it up from the ground.
	auto SetRise = [this](float R)
	{
		R = FMath::Max(R, 0.01f);
		Visual->SetRelativeScale3D(FVector(1.f, 1.f, R));
		Visual->SetRelativeLocation(FVector(0, 0, -(1.f - R) * Height * 0.5f * WWO::M));
	};
	if (Step == EWWOLifeStep::Holding || Step == EWWOLifeStep::Leaving)
	{
		SetRise(Life.GrowIn());
		if (Step == EWWOLifeStep::Leaving)
		{
			// No longer solid or cover from the tick it starts to sink.
			Tags.Remove("Cover");
			Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Dust(0.6f);
		}
		return;
	}
	SetRise(Life.TailLeft(Duration));
	if (Step == EWWOLifeStep::Over) FWWOEffectLife::End(*this); // a joiner's copy stays, sunk, until the host's goes
}

// --- Entrench roots ---------------------------------------------------------------------------

AWWOEntrenchRoots::AWWOEntrenchRoots()
	: Life(0.2f, 0.3f, true) // rise over 0.2 s, stand their duration, sink over 0.3 s on their own clock
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

AWWOEntrenchRoots* AWWOEntrenchRoots::Spawn(UWorld* World, AActor* Player, float InDuration)
{
	const FVector Feet = Player->GetActorLocation() - FVector(0, 0, 100.f);
	AWWOEntrenchRoots* Roots = World->SpawnActor<AWWOEntrenchRoots>(Feet, FRotator::ZeroRotator, WWO::AlwaysSpawnDeferred());
	if (!Roots) return nullptr;
	Roots->Duration = InDuration;
	Roots->FinishSpawning(FTransform(Feet));
	Roots->AttachToActor(Player, FAttachmentTransformRules::KeepWorldTransform);
	// A ring of roots leaning outward, and a glowing ring on the ground.
	for (int32 i = 0; i < 9; ++i)
	{
		const float Angle = UE_TWO_PI * i / 9.f + FMath::FRandRange(-0.15f, 0.15f);
		const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
		const float H = FMath::FRandRange(0.9f, 1.4f);
		UStaticMeshComponent* Root = WWOFX::AddShape(Roots, Roots->GetRootComponent(), WWOFX::Cone(), TEXT("M_WWO_Solid"), BarkColor);
		Root->SetRelativeScale3D(FVector(0.24f, 0.24f, H));
		const FRotator Lean = FRotationMatrix::MakeFromZ(FVector::UpVector * FMath::Cos(0.45f) + Out * FMath::Sin(0.45f)).Rotator(); // 0.45 rad outward
		Root->SetRelativeLocationAndRotation(Out * 75.f + FVector(0, 0, H * 40.f), Lean);
	}
	UStaticMeshComponent* Ring = WWOFX::AddGroundDisc(Roots, Roots->GetRootComponent(), FLinearColor(0.95f, 0.65f, 0.3f), 0.15f, 0.25f, 0.6f);
	Ring->SetRelativeLocation(FVector(0, 0, 4.f));
	Ring->SetRelativeScale3D(FVector(110.f / 50.f, 110.f / 50.f, 1.f));
	Roots->RingMat = WWOFX::MID(Ring);
	Roots->SetActorRelativeScale3D(FVector(1.f, 1.f, 0.01f));
	Roots->BurstDirt();
	return Roots;
}

void AWWOEntrenchRoots::BurstDirt()
{
	const FVector At = GetActorLocation() + FVector(0, 0, 10.f);
	if (AWWOParticles* Chips = AWWOParticles::Burst(GetWorld(), At, WWO::Lightened(BarkColor, 0.1f), 22, 0.8f, 2.5f, 5.f, 0.08f, FVector(0, 0, -14.f), EWWOParticleStyle::Chunk, false))
	{
		Chips->SpreadDegrees = 60.f;
		Chips->Shape = EWWOEmitShape::Ring;
		Chips->ShapeRadius = 0.8f;
		Chips->Start();
	}
	AWWOParticles::Burst(GetWorld(), At, FLinearColor(0.55f, 0.47f, 0.38f, 0.45f), 16, 1.1f, 0.8f, 2.f, 0.8f, FVector(0, 0, 0.25f), EWWOParticleStyle::Smoke);
	AWWOFXActor::Shockwave(GetWorld(), GetActorLocation(), FLinearColor(0.95f, 0.65f, 0.3f), 2.5f, 0.35f);
}

void AWWOEntrenchRoots::Tick(float DeltaSeconds)
{
	WWO_PROF("EntrenchRoots");
	Super::Tick(DeltaSeconds);
	const EWWOLifeStep Step = Life.Advance(DeltaSeconds, Duration);
	RingAge += DeltaSeconds;
	if (RingMat) RingMat->SetScalarParameterValue("Alpha", 0.7f - 0.2f * FMath::Cos(RingAge * UE_PI / 0.8f)); // a slow pulse: 0.5 -> 0.9 -> 0.5 every 1.6 s
	if (Step == EWWOLifeStep::Holding || Step == EWWOLifeStep::Leaving)
	{
		SetActorRelativeScale3D(FVector(1.f, 1.f, FMath::Max(Life.GrowIn(), 0.01f)));
		if (Step == EWWOLifeStep::Leaving) BurstDirt();
		return;
	}
	SetActorRelativeScale3D(FVector(1.f, 1.f, FMath::Max(Life.TailLeft(Duration), 0.01f)));
	if (Step == EWWOLifeStep::Over) FWWOEffectLife::End(*this, true); // looks only, on each machine: not replicated
}

// --- Chain lightning ------------------------------------------------------------------------

AWWOChainLightning::AWWOChainLightning()
{
	PrimaryActorTick.bCanEverTick = true;
}

AWWOChainLightning* AWWOChainLightning::Spawn(UWorld* World, const UWWOAbilityData* Ability, AActor* First, AActor* InPlayer, const FLinearColor& InColor)
{
	AWWOChainLightning* Chain = World->SpawnActor<AWWOChainLightning>(First->GetActorLocation(), FRotator::ZeroRotator, WWO::AlwaysSpawnDeferred());
	if (!Chain) return nullptr;
	Chain->FinishSpawning(FTransform(First->GetActorLocation()));
	Chain->Player = InPlayer;
	Chain->Color = InColor;
	Chain->Damage = Ability->ChainDamage;
	Chain->Range = Ability->ChainRange;
	Chain->Falloff = Ability->ChainFalloff;
	Chain->Delay = FMath::Max(Ability->ChainDelay, 0.f);
	Chain->JumpsLeft = FMath::RoundToInt(Ability->ChainJumps);
	Chain->Hit.Add(First);
	Chain->Current = First;
	Chain->Burst(First, 14, 2.2f, 0.55f); // the big one where the punch landed
	Chain->DamageTarget(First, Chain->Damage);
	Chain->Wait = Chain->Delay;
	return Chain;
}

AWWOChainLightning* AWWOChainLightning::SpawnFrom(UWorld* World, AActor* First, AActor* InPlayer, const FLinearColor& InColor,
	float InDamage, float InRange, float InFalloff, int32 Jumps, float InDelay)
{
	if (!World || !First) return nullptr;
	AWWOChainLightning* Chain = World->SpawnActor<AWWOChainLightning>(First->GetActorLocation(), FRotator::ZeroRotator, WWO::AlwaysSpawnDeferred());
	if (!Chain) return nullptr;
	Chain->FinishSpawning(FTransform(First->GetActorLocation()));
	Chain->Player = InPlayer;
	Chain->Color = InColor;
	Chain->Damage = InDamage;
	Chain->Range = InRange;
	Chain->Falloff = InFalloff;
	Chain->Delay = FMath::Max(InDelay, 0.f);
	Chain->JumpsLeft = Jumps;
	Chain->Hit.Add(First);
	Chain->Current = First;
	Chain->Burst(First, 6, 1.2f, 0.35f); // a small crackle where it starts
	Chain->Wait = Chain->Delay;
	return Chain;
}

AActor* AWWOChainLightning::NextTarget() const
{
	// The nearest living target (and in PvP the player's foes) not hit yet, its body centre closer than the
	// range, with a clear line from the last one.
	const FVector Origin = Current.IsValid() ? AWWOLightningBolt::CenterOf(Current.Get()) : GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOChain), false);
	if (Current.IsValid()) Params.AddIgnoredActor(Current.Get());
	if (Player.IsValid()) Params.AddIgnoredActor(Player.Get());
	FWWOAreaQuery Q;
	Q.Center = Origin;
	Q.Radius = Range * WWO::M;
	Q.bInclusive = false;
	Q.bFloatDistance = true;
	Q.Measure = EWWOAreaMeasure::BodyCenter;
	Q.For = Player.Get();
	Q.SightParams = &Params;
	Q.SightFrom = Origin;
	FWWOAreaHit Next;
	const bool bFound = UWWOWorldSubsystem::Get(this)->NearestTargetInRadius(Q, [this](const AActor* Actor) { return !Hit.Contains(Actor); }, Next);
	return bFound ? Next.Actor : nullptr;
}

void AWWOChainLightning::Burst(AActor* Target, int32 Arcs, float Reach, float Time)
{
	if (!Target) return;
	const FVector Impact = AWWOLightningBolt::CenterOf(Target);
	const AWWOCharacter* P = Cast<AWWOCharacter>(Player.Get());
	const FVector Away = P ? (Impact - P->EyeLocation()).GetSafeNormal() : FVector::ForwardVector; // from the camera, like Godot
	AWWOGameState::Discharge(GetWorld(), Target, Impact, Away, Color, Arcs, Reach, Time); // everyone sees it
}

void AWWOChainLightning::DamageTarget(AActor* Target, float Amount)
{
	IWWOTargetable* T = Cast<IWWOTargetable>(Target);
	FWWODamageBy By(Player.Get());
	if (T && !T->IsDead() && UWWOWorldSubsystem::CanHurt(Player.Get(), Target)) T->TakeHit(Amount, false, AWWOLightningBolt::CenterOf(Target));
}

void AWWOChainLightning::Tick(float DeltaSeconds)
{
	WWO_PROF("ChainLightning");
	Super::Tick(DeltaSeconds);
	Wait -= DeltaSeconds;
	if (Wait > 0.f) return;
	Wait = Delay;
	AActor* Next = JumpsLeft > 0 ? NextTarget() : nullptr;
	if (!Next)
	{
		Destroy();
		return;
	}
	JumpsLeft -= 1;
	AWWOGameState::Bolt(GetWorld(), Current.IsValid() ? AWWOLightningBolt::CenterOf(Current.Get()) : GetActorLocation(),
		AWWOLightningBolt::CenterOf(Next), Color, 0.4f, Current.Get(), Next);
	Damage *= Falloff;
	Hit.Add(Next);
	Burst(Next, 5, 0.9f, 0.35f);
	DamageTarget(Next, Damage);
	AWWOCharacter* P = Cast<AWWOCharacter>(Player.Get());
	if (P && P->IsLocallyControlled())
	{
		if (AWWOPlayerController* PC = P->GetWWOController()) PC->Rumble(0.6f, 0.35f, 0.06f); // a sharp buzz with each jump
	}
	Current = Next;
}
