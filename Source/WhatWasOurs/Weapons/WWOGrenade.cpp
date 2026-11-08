#include "Weapons/WWOGrenade.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOAbilityComponent.h"
#include "Enemies/WWOEnemy.h"
#include "Allies/WWOAlly.h"
#include "Flora/WWOFloraEffects.h"
#include "FX/WWOFX.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "WhatWasOurs.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

AWWOGrenade::AWWOGrenade()
{
	PrimaryActorTick.bCanEverTick = true;
	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(Radius * WWO::M);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore); // shots and sight lines pass through
	Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Collision->SetCanEverAffectNavigation(false);
	SetRootComponent(Collision);

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	Movement->InitialSpeed = 0.f;
	Movement->MaxSpeed = 0.f; // no limit
	Movement->bShouldBounce = true;
	Movement->Bounciness = 0.35f;
	Movement->Friction = 0.f; // OnBounce applies Godot's rigid-body contact friction (with spin) instead
	Movement->ProjectileGravityScale = 1.f; // 9.8 m/s^2, like Godot's rigid bodies
	Movement->BounceVelocityStopSimulatingThreshold = 30.f;

	bReplicates = true;
	SetReplicateMovement(true); // joiners see the host's flight
}

void AWWOGrenade::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AWWOGrenade, Look, COND_InitialOnly);
	DOREPLIFETIME(AWWOGrenade, bStuck);
}

int32 AWWOGrenade::SeenRemote = 0;

void AWWOGrenade::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority()) return; // the host's is set up by Throw
	++SeenRemote;
	// Someone else's machine runs it: just the looks, moved by the host.
	Movement->StopMovementImmediately();
	Movement->Deactivate();
	Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Collision->SetSphereRadius(Look.Size);
	BuildLooks();
	if (bStuck) AddHalo();
}

void AWWOGrenade::OnRep_Stuck()
{
	if (bStuck && !Halo) AddHalo();
}

AWWOGrenade* AWWOGrenade::Throw(UWorld* World, const FWWOGrenadeSpec& InSpec, const FVector& At, const FVector& VelocityCm, AActor* InThrower)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	Params.Owner = InThrower; // whose loadout it answers to (UWWOLoadoutSubsystem::Get)
	AWWOGrenade* G = World->SpawnActor<AWWOGrenade>(At, FRotator::ZeroRotator, Params);
	if (!G) return nullptr;
	G->Spec = InSpec;
	G->Movement->Velocity = VelocityCm;
	G->FinishSpawning(FTransform(At));
	G->Setup(InThrower);
	return G;
}

void AWWOGrenade::ExplodeAt(UWorld* World, const FWWOGrenadeSpec& InSpec, const FVector& At, AActor* Instigator)
{
	FWWOGrenadeSpec S = InSpec;
	S.bVisible = false;
	if (AWWOGrenade* G = Throw(World, S, At, FVector::ZeroVector, Instigator))
	{
		G->Movement->StopMovementImmediately();
		G->Movement->ProjectileGravityScale = 0.f;
		G->Explode();
	}
}

void AWWOGrenade::Setup(AActor* InThrower)
{
	Thrower = InThrower;
	ReleasedAt = GetActorLocation();
	if (InThrower) Collision->IgnoreActorWhenMoving(InThrower, true); // don't bounce off yourself on the way out
	const float Size = Radius * (Spec.bIsSeed ? 0.65f : 1.f) * Spec.SizeScale * WWO::M;
	Collision->SetSphereRadius(Size);
	if (Spec.Data) Spec.Flavor = Spec.Data->Flora;
	Look.Color = Spec.Color;
	Look.Size = Size;
	Look.BlastRadius = Spec.BlastRadius;
	Look.Flavor = Spec.Flavor;
	Look.bVisible = Spec.bVisible;
	Look.bBlind = Spec.Data && Spec.Data->Kind == EWWOGrenadeKind::Blind;
	Movement->Bounciness = FMath::Clamp((Spec.Data ? Spec.Data->Bounce : 0.35f) + UWWOLoadoutSubsystem::Bonus(this, "more_bounce"), 0.f, 1.f);
	Movement->OnProjectileBounce.AddDynamic(this, &AWWOGrenade::OnBounce);

	if (Spec.Seedburst || Spec.bIsSeed)
	{
		AWWOCharacter* P = Cast<AWWOCharacter>(InThrower);
		if (P && P->IsLocallyControlled())
		{
			P->Abilities->LiveSeeds++; // keeps the Seedburst HUD lit
			bCounted = true;
		}
	}
	BuildLooks();
	if (Spec.SporePatches) PatchesLeft = FMath::RoundToInt(Spec.SporePatches->PatchMax);
	if (Spec.Data && Spec.Data->Kind == EWWOGrenadeKind::Tripmine)
	{
		LifeLimit = Spec.Data->MineLife; // no fuse: it waits for an enemy
		bFuseIsFizzle = true;
	}
	else if (Spec.Data && Spec.Data->Kind == EWWOGrenadeKind::Spike)
	{
		LifeLimit = Spec.Data->SpikeLife; // no fuse: it goes off when it touches a target
		bFuseIsFizzle = true;
	}
	else
	{
		LifeLimit = Spec.Fuse;
	}
}

void AWWOGrenade::BuildLooks()
{
	const float Size = Look.Size;
	if (Look.bVisible)
	{
		Ball = WWOFX::AddShape(this, Collision, WWOFX::Sphere(), TEXT("M_WWO_Solid"), Look.Color, 3.f);
		Ball->SetRelativeScale3D(FVector(Size / 50.f));
		Ball->SetCastShadow(false);
		Light = NewObject<UPointLightComponent>(this);
		Light->SetupAttachment(Collision);
		Light->SetLightColor(Look.Color);
		Light->SetIntensityUnits(ELightUnits::Unitless);
		Light->SetIntensity(1.5f * 1500.f);
		Light->SetAttenuationRadius(250.f);
		Light->SetCastShadows(false);
		Light->RegisterComponent();
		if (AWWOParticles* T = AWWOParticles::Burst(GetWorld(), GetActorLocation(), WWO::Lightened(Look.Color, 0.2f), 28, 0.25f, 0.f, 0.15f,
			Size / WWO::M * 1.4f, FVector::ZeroVector, EWWOParticleStyle::Glow, false))
		{
			T->bOneShot = false;
			T->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
			T->Start();
			Trail = T;
		}
	}
	else
	{
		SetActorHiddenInGame(true);
		Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void AWWOGrenade::EndPlay(const EEndPlayReason::Type Reason)
{
	if (bCounted)
	{
		if (UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(this))
		{
			if (AWWOCharacter* P = WS->GetPlayer()) P->Abilities->LiveSeeds = FMath::Max(P->Abilities->LiveSeeds - 1, 0);
		}
	}
	if (Trail.IsValid()) Trail->StopAndFade();
	Super::EndPlay(Reason);
}

void AWWOGrenade::IgnoreActor(AActor* Other)
{
	Collision->IgnoreActorWhenMoving(Other, true);
}

void AWWOGrenade::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		if (Trail.IsValid()) Trail->SetActorLocation(GetActorLocation());
		return; // the host runs it
	}
	if (bSpent) return;
	Age += DeltaSeconds;
	PatchCooldown -= DeltaSeconds;
	if (Trail.IsValid()) Trail->SetActorLocation(GetActorLocation());
	if (!bStuck)
	{
		// Godot's default rigid-body damping (0.1 per second, linear and angular).
		const float Damp = FMath::Max(1.f - LinearDamp * DeltaSeconds, 0.f);
		Movement->Velocity *= Damp;
		Spin *= Damp;
	}
	if (Spec.Data && Spec.Data->Kind == EWWOGrenadeKind::Spike) Seek(DeltaSeconds);
	if (Spec.Data && Spec.Data->Kind == EWWOGrenadeKind::Tripmine && bStuck) UpdateMine(DeltaSeconds);
	if (bSpent) return;
	if (Age >= LifeLimit)
	{
		if (bFuseIsFizzle) Fizzle();
		else Explode();
	}
}

void AWWOGrenade::Seek(float DeltaSeconds)
{
	// The Poison Spike curves toward the nearest target in range, harder the closer it gets.
	AActor* Best = nullptr;
	float BestDistance = Spec.Data->SeekRadius * WWO::M;
	float BestRadius = 0.f;
	FVector BestPos;
	for (AActor* Actor : UWWOWorldSubsystem::Get(this)->LivingTargets())
	{
		const FWWOAimPart Body = Cast<IWWOTargetable>(Actor)->GetAimParts().Last();
		const float Distance = FVector::Dist(GetActorLocation(), Body.Position);
		if (Distance < BestDistance)
		{
			Best = Actor;
			BestDistance = Distance;
			BestRadius = Body.Radius;
			BestPos = Body.Position;
		}
	}
	if (!Best) return;
	if (BestDistance <= BestRadius + Proximity * WWO::M)
	{
		SpikeHit(Best); // close enough: counts as touching it
		return;
	}
	const FVector To = (BestPos - GetActorLocation()).GetSafeNormal();
	const float Speed = Movement->Velocity.Size();
	if (Speed < 50.f) return;
	const FVector Heading = Movement->Velocity / Speed;
	const float Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(Heading, To), -1.f, 1.f));
	if (Angle < 0.001f) return;
	const float Turn = FMath::DegreesToRadians(Spec.Data->SeekStrength) * (1.f + (1.f - BestDistance / (Spec.Data->SeekRadius * WWO::M))) * DeltaSeconds;
	// Godot's Vector3.slerp: turn the heading along the great circle toward the target.
	const FVector Axis = FVector::CrossProduct(Heading, To).GetSafeNormal();
	if (Axis.IsNearlyZero()) return;
	Movement->Velocity = Heading.RotateAngleAxisRad(Angle * FMath::Min(Turn / Angle, 1.f), Axis) * Speed;
}

void AWWOGrenade::ApplyContactFriction(const FHitResult& Hit, const FVector& ImpactVelocity)
{
	// Godot's rigid-body contact: the bounce keeps the tangential speed (Movement->Friction is 0),
	// then friction (0.8, the grenade's against a default surface) pushes against the contact
	// point's slip, up to mu * the normal impulse, and spins the ball. Once it's rolling there's
	// no slip, so it keeps rolling (only the damping slows it), like the Godot ball.
	const FVector N = Hit.ImpactNormal.GetSafeNormal();
	const float Vn = FVector::DotProduct(ImpactVelocity, N);
	if (Vn >= 0.f || N.IsNearlyZero()) return;
	const float R = Collision->GetUnscaledSphereRadius();
	const FVector V = Movement->Velocity;
	FVector Slip = V - R * FVector::CrossProduct(Spin, N); // the contact point's velocity (r = -R n)...
	Slip -= FVector::DotProduct(Slip, N) * N; // ...along the surface
	const float SlipSpeed = Slip.Size();
	if (SlipSpeed < KINDA_SMALL_NUMBER) return;
	const float Normal = -Vn * (1.f + Movement->Bounciness); // normal impulse per unit mass
	const FVector J = -Slip / SlipSpeed * FMath::Min(ContactFriction * Normal, SlipSpeed * 2.f / 7.f); // a solid ball stops slipping at 2/7
	Movement->Velocity = V + J;
	Spin -= 5.f / (2.f * R) * FVector::CrossProduct(N, J);
}

void AWWOGrenade::OnBounce(const FHitResult& Hit, const FVector& ImpactVelocity)
{
	if (bSpent) return;
	AActor* Other = Hit.GetActor();
	if (!bStuck) ApplyContactFriction(Hit, ImpactVelocity);
	if (!bBounced && Spec.Data)
	{
		bBounced = true;
		UE_LOG(LogWWO, Log, TEXT("Grenade %s: first touched %s %.1f m from the hand after %.2f s, %.1f m/s in, %.1f m/s out"), *Spec.Data->Id.ToString(),
			Other ? *Other->GetName() : TEXT("-"), FVector::Dist(GetActorLocation(), ReleasedAt) / WWO::M, Age, ImpactVelocity.Size() / WWO::M, Movement->Velocity.Size() / WWO::M);
	}
	IWWOTargetable* Target = Cast<IWWOTargetable>(Other);
	if (Spec.Data && Spec.Data->Kind == EWWOGrenadeKind::Spike && Target && !Target->IsDead())
	{
		SpikeHit(Other);
		return;
	}
	if (Spec.Data && Spec.Data->Kind == EWWOGrenadeKind::Tripmine && !bStuck)
	{
		Stick();
		return;
	}
	if (Spec.SporePatches && PatchesLeft > 0 && PatchCooldown <= 0.f && !Target)
	{
		PatchesLeft -= 1;
		PatchCooldown = 0.3f;
		AWWOSporePatch::Spawn(GetWorld(), GetActorLocation() - FVector(0, 0, Radius * WWO::M), Spec.SporePatches->PatchRadius, Spec.SporePatches->PatchTime, Thrower.Get());
	}
	if (Spec.bExplodeOnImpact && !Cast<AWWOGrenade>(Other))
	{
		Explode();
	}
}

void AWWOGrenade::Stick()
{
	// The Snare Mine plants itself where it first lands.
	bStuck = true;
	Movement->StopMovementImmediately();
	Movement->ProjectileGravityScale = 0.f;
	Movement->Deactivate();
	ArmLeft = Spec.Data->ArmTime;
	UE_LOG(LogWWO, Log, TEXT("Grenade %s: stuck at %s after %.2f s"), *Spec.Data->Id.ToString(), *GetActorLocation().ToCompactString(), Age);
	if (Trail.IsValid()) Trail->StopAndFade();
	AddHalo();
}

void AWWOGrenade::AddHalo()
{
	Halo = WWOFX::AddSoftSphere(this, Collision, Look.Color, 0.f, 2.f);
	Halo->SetRelativeScale3D(FVector(40.f / 50.f));
	if (!HasAuthority())
	{
		if (Trail.IsValid()) Trail->StopAndFade();
		WWOFX::MID(Halo)->SetScalarParameterValue("Alpha", 0.6f); // planted (the blink is the host's)
	}
}

void AWWOGrenade::Blast(const FVector& Center)
{
	SpawnBlast(Center, Spec.BlastRadius);
	if (IsOnline()) MulticastBlast(Center, Spec.BlastRadius);
}

void AWWOGrenade::MulticastBlast_Implementation(FVector_NetQuantize Center, float RadiusM)
{
	if (HasAuthority()) return; // shown already
	if (Trail.IsValid()) Trail->StopAndFade();
	SetActorHiddenInGame(true);
	SpawnBlast(Center, RadiusM);
}

void AWWOGrenade::Finish()
{
	if (!IsOnline())
	{
		Destroy();
		return;
	}
	// Hidden for a moment, so the blast reaches everyone before the grenade goes away.
	SetActorHiddenInGame(true);
	Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Movement->StopMovementImmediately();
	Movement->Deactivate();
	if (Trail.IsValid()) Trail->StopAndFade();
	SetLifeSpan(0.5f);
}

void AWWOGrenade::UpdateMine(float DeltaSeconds)
{
	if (ArmLeft > 0.f)
	{
		ArmLeft -= DeltaSeconds;
		if (Light) Light->SetIntensity(0.3f * 1500.f);
		return;
	}
	const float Blink = 0.5f + 0.5f * FMath::Sin(GetWorld()->GetTimeSeconds() * 15.f);
	if (Light) Light->SetIntensity(2.f * Blink * 1500.f); // armed: blinks
	if (Halo) WWOFX::MID(Halo)->SetScalarParameterValue("Alpha", 0.25f + 0.6f * Blink);
	for (AActor* Actor : UWWOWorldSubsystem::Get(this)->LivingTargets())
	{
		if (FVector::Dist(Cast<IWWOTargetable>(Actor)->FeetLocation(), GetActorLocation()) <= (Spec.Data->TriggerRadius + 0.5f) * WWO::M)
		{
			Explode();
			return;
		}
	}
}

void AWWOGrenade::Confirm(bool bKilled)
{
	AWWOCharacter* P = Cast<AWWOCharacter>(Thrower.Get());
	if (!P) return;
	if (P->IsLocallyControlled()) P->Weapon->OnHitConfirmed.Broadcast(false, bKilled);
	else P->ClientHitMarker(bKilled); // a joiner's grenade: tell its thrower
}

void AWWOGrenade::SpikeHit(AActor* TargetActor)
{
	// Direct contact: a hit, then poison.
	bSpent = true;
	const FVector Point = GetActorLocation();
	IWWOTargetable* Target = Cast<IWWOTargetable>(TargetActor);
	const bool bKilled = Target->TakeHit(Spec.Damage, false, Point);
	Confirm(bKilled);
	UE_LOG(LogWWO, Log, TEXT("Grenade %s: spiked %s for %.0f after %.2f s (killed %d), poison %.0f/s for %.1f s"), *Spec.Data->Id.ToString(),
		*TargetActor->GetName(), Spec.Damage, Age, bKilled, Spec.Data->PoisonDps, Spec.Data->PoisonTime);
	if (!bKilled)
	{
		const float Potency = 1.f + UWWOLoadoutSubsystem::Bonus(this, "poison_damage"); // Stronger Poison
		UWWODamageOverTime::Apply(TargetActor, Spec.Data->PoisonDps * Potency, Spec.Data->PoisonTime);
	}
	Spec.BlastRadius = 0.8f;
	Blast(Point);
	Finish();
}

void AWWOGrenade::Fizzle()
{
	if (bSpent) return;
	bSpent = true;
	UE_LOG(LogWWO, Log, TEXT("Grenade %s: fizzled after %.2f s"), Spec.Data ? *Spec.Data->Id.ToString() : TEXT("?"), Age);
	Spec.BlastRadius = 0.6f;
	Blast(GetActorLocation());
	Finish();
}

void AWWOGrenade::Explode()
{
	if (bSpent) return;
	bSpent = true;
	UWorld* World = GetWorld();
	const FVector Center = GetActorLocation();
	Blast(Center);
	int32 HitCount = 0;
	float DealtTotal = 0.f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOBlast), false, this);
	if (Thrower.IsValid()) Params.AddIgnoredActor(Thrower.Get());
	for (AActor* Actor : UWWOWorldSubsystem::Get(this)->LivingTargets())
	{
		IWWOTargetable* Target = Cast<IWWOTargetable>(Actor);
		// Closest part of the target to the blast.
		FWWOAimPart BestPart;
		float BestDistance = TNumericLimits<float>::Max();
		for (const FWWOAimPart& Part : Target->GetAimParts())
		{
			const float D = FMath::Max(FVector::Dist(Part.Position, Center) - Part.Radius, 0.f);
			if (D < BestDistance)
			{
				BestDistance = D;
				BestPart = Part;
			}
		}
		if (BestDistance > Spec.BlastRadius * WWO::M) continue;
		// Walls block the blast.
		FHitResult Check;
		if (World->LineTraceSingleByChannel(Check, Center + FVector(0, 0, 5.f), BestPart.Position, ECC_Visibility, Params) && Check.GetActor() != Actor)
		{
			continue;
		}
		const float Dealt = Spec.Damage * FMath::Lerp(1.f, EdgeDamage, BestDistance / (Spec.BlastRadius * WWO::M));
		Confirm(Target->TakeHit(Dealt, false, BestPart.Position));
		HitCount++;
		DealtTotal += Dealt;
	}
	UE_LOG(LogWWO, Log, TEXT("Grenade %s: exploded at %s after %.2f s (radius %.1f m), hit %d for %.0f in all"),
		Spec.Data ? *Spec.Data->Id.ToString() : (Spec.bIsSeed ? TEXT("seed") : TEXT("blast")), *Center.ToCompactString(), Age, Spec.BlastRadius, HitCount, DealtTotal);
	if (Spec.Seedburst) Burst(Center);
	ShareShields(Center);
	if (Spec.Data)
	{
		const UWWOGrenadeData* D = Spec.Data;
		const float Reach = (Spec.BlastRadius + 0.5f) * WWO::M;
		if (D->Kind == EWWOGrenadeKind::Wall)
		{
			const AWWOBarkWall* Wall = AWWOBarkWall::Raise(World, Center, Spec.ThrownFrom, D->WallWidth, D->WallHeight,
				D->WallTime * (1.f + UWWOLoadoutSubsystem::Bonus(this, "wall_longer")), Thrower.Get()); // Longer Walls
			if (Wall) UE_LOG(LogWWO, Log, TEXT("Grenade %s: raised a %.1f x %.1f m wall at %s facing yaw %.0f"), *D->Id.ToString(), D->WallWidth, D->WallHeight,
				*Wall->GetActorLocation().ToCompactString(), Wall->GetActorRotation().Yaw);
		}
		if (D->Kind == EWWOGrenadeKind::Blind || D->Kind == EWWOGrenadeKind::Tripmine)
		{
			int32 Affected = 0;
			const float SlowTime = D->SlowTime * (1.f + UWWOLoadoutSubsystem::Bonus(this, "slow_longer")); // Longer Snare
			for (AWWOEnemy* Enemy : UWWOWorldSubsystem::Get(this)->LivingEnemies())
			{
				if (FVector::Dist(Enemy->FeetLocation(), Center) > Reach) continue;
				if (D->Kind == EWWOGrenadeKind::Blind) Enemy->ApplyBlind(D->BlindTime);
				else Enemy->ApplySlow(D->SlowAmount, SlowTime);
				Affected++;
			}
			UE_LOG(LogWWO, Log, TEXT("Grenade %s: %s %d enemies"), *D->Id.ToString(), D->Kind == EWWOGrenadeKind::Blind ? TEXT("blinded") : TEXT("slowed"), Affected);
		}
		if (D->Kind == EWWOGrenadeKind::Pool)
		{
			AWWOSporePool::Spawn(World, Center - FVector(0, 0, Radius * WWO::M), Spec.BlastRadius,
				D->PoolDps * (1.f + UWWOLoadoutSubsystem::Bonus(this, "poison_damage")), D->PoolTime, Spec.Color, Spec.Flavor, Thrower.Get());
			UE_LOG(LogWWO, Log, TEXT("Grenade %s: left a %.1f m pool (%.0f/s for %.1f s)"), *D->Id.ToString(), Spec.BlastRadius, D->PoolDps, D->PoolTime);
		}
	}
	Finish();
}

void AWWOGrenade::Burst(const FVector& Center)
{
	// Seedburst: scatter smaller seed grenades outward from the blast.
	const int32 Count = FMath::RoundToInt(Spec.Seedburst->SubCount);
	for (int32 i = 0; i < Count; ++i)
	{
		FWWOGrenadeSpec Seed;
		Seed.bIsSeed = true;
		Seed.Color = WWO::Lightened(Spec.Color, 0.25f);
		Seed.Damage = Spec.Damage * Spec.Seedburst->SubScale;
		Seed.BlastRadius = Spec.BlastRadius * Spec.Seedburst->SubScale;
		Seed.Fuse = Spec.Seedburst->SubFuse;
		Seed.Flavor = Spec.Flavor;
		const float Angle = UE_TWO_PI * i / FMath::Max(Count, 1) + FMath::FRandRange(-0.3f, 0.3f); // spread evenly around
		const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
		const float Spread = Spec.Seedburst->SubSpread;
		Throw(GetWorld(), Seed, Center + FVector(0, 0, 30.f), (Out * Spread + FVector(0, 0, Spread * 0.8f)) * WWO::M, Thrower.Get());
	}
}

void AWWOGrenade::ShareShields(const FVector& Center)
{
	// Shield Share (IronBark): explosions refill the shields of teammates caught in them.
	AWWOCharacter* P = Cast<AWWOCharacter>(Thrower.Get());
	const UWWOAbilityData* Share = P ? P->Abilities->Equipped("shield_share") : nullptr;
	if (!Share) return;
	for (AWWOAlly* Mate : UWWOWorldSubsystem::Get(this)->LivingTeammates())
	{
		if (FVector::Dist(Mate->GetActorLocation(), Center) <= (Spec.BlastRadius + 0.5f) * WWO::M) Mate->RestoreShield(Share->ShieldAmount);
	}
}

void AWWOGrenade::SpawnBlast(const FVector& Center, float RadiusM)
{
	// A soft glowing ball that pops out to the blast size and fades, a white-hot core, a
	// light flash, sparks, a ground shockwave, and a little extra in the flora's style.
	UWorld* World = GetWorld();
	const float R = RadiusM;
	const FLinearColor Color = Look.Color;
	const float S = R * WWO::M / 50.f; // sphere mesh is 50 cm in radius
	if (AWWOFXActor* FX = AWWOFXActor::Spawn(World, Center, 0.5f))
	{
		UStaticMeshComponent* Blast = WWOFX::AddSoftSphere(FX, FX->GetRootComponent(), Color, 0.55f, 1.f, false, false, 0.35f);
		FX->ScaleTween(Blast, FVector(0.1f * S / R), FVector(S), 0.18f, EWWOEase::ExpoOut);
		FX->ParamTween(WWOFX::MID(Blast), "Alpha", 0.55f, 0.f, 0.4f, EWWOEase::QuadOut);
		UStaticMeshComponent* Core = WWOFX::AddSoftSphere(FX, FX->GetRootComponent(), FMath::Lerp(Color, FLinearColor::White, 0.6f), 0.6f, 1.5f, true, false, 0.8f);
		FX->ScaleTween(Core, FVector(S * 0.1f), FVector(S * 0.3f), 0.12f, EWWOEase::QuadOut);
		FX->ParamTween(WWOFX::MID(Core), "Alpha", 0.6f, 0.f, 0.16f);
		const bool bBlind = Look.bBlind;
		UPointLightComponent* Flash = FX->AddLight(FMath::Lerp(Color, FLinearColor::White, 0.25f), 0.f, R * 2.2f * WWO::M);
		FX->LightTween(Flash, (bBlind ? 8.f : 6.f) * 3000.f, 0.f, 0.45f, EWWOEase::QuadOut);
	}
	// Sparks, more for bigger blasts.
	AWWOParticles::Burst(World, Center, FMath::Lerp(Color, FLinearColor::White, 0.3f), FMath::Clamp(FMath::RoundToInt(R * 9.f), 6, 70),
		0.55f, R * 1.2f, R * 3.f, FMath::Clamp(R * 0.025f, 0.04f, 0.2f), FVector(0, 0, -9.8f));
	FVector Ground;
	const bool bGround = WWOFX::GroundBelow(World, Center, FMath::Max(R * 0.5f, 0.6f) * WWO::M, Ground, this);
	if (bGround && R >= 1.f) AWWOFXActor::Shockwave(World, Ground, Color, R * 1.2f, 0.4f);
	if (Look.Flavor == "voltvine")
	{
		// A few bolts crackle out from the middle of the blast.
		for (int32 i = 0; i < FMath::Clamp(FMath::RoundToInt(R * 1.2f), 2, 8); ++i)
		{
			const FVector Dir = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-0.3f, 1.f)).GetSafeNormal();
			AWWOLightningBolt::Spawn(World, Center, Center + Dir * R * WWO::M * FMath::FRandRange(0.35f, 0.65f), Color, FMath::FRandRange(0.2f, 0.32f));
		}
	}
	else if (Look.Flavor == "veilspore")
	{
		AWWOParticles::Burst(World, Center, WWO::WithAlpha(WWO::Darkened(Color, 0.1f), 0.55f), FMath::Clamp(FMath::RoundToInt(R * 5.f), 6, 45),
			1.6f, R * 0.4f, R, R * 0.4f, FVector(0, 0, 0.4f), EWWOParticleStyle::Smoke);
	}
	else if (Look.Flavor == "ironbark")
	{
		if (AWWOParticles* Chunks = AWWOParticles::Burst(World, Center, FLinearColor(0.32f, 0.2f, 0.11f), FMath::Clamp(FMath::RoundToInt(R * 3.f), 4, 30),
			0.9f, R * 0.8f, R * 2.f, FMath::Clamp(R * 0.04f, 0.07f, 0.25f), FVector(0, 0, -14.f), EWWOParticleStyle::Chunk, false))
		{
			Chunks->Direction = FVector::UpVector;
			Chunks->SpreadDegrees = 70.f;
			Chunks->Start();
		}
		if (bGround)
		{
			AWWOParticles::Burst(World, Ground + FVector(0, 0, 20.f), FLinearColor(0.55f, 0.45f, 0.35f, 0.4f), FMath::Clamp(FMath::RoundToInt(R * 3.f), 4, 30),
				1.1f, R * 0.5f, R * 1.1f, R * 0.4f, FVector(0, 0, 0.3f), EWWOParticleStyle::Smoke);
		}
	}
}
