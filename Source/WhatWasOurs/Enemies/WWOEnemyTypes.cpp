#include "Enemies/WWOEnemyTypes.h"
#include "WWOIds.h"
#include "Net/UnrealNetwork.h"
#include "Game/WWOGameState.h"
#include "Enemies/WWOEnemyProjectile.h"
#include "Characters/WWOBodyComponent.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
#include "FX/WWOFX.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "Engine/World.h"

namespace
{
	FVector BonePos(const USkeletalMeshComponent* Mesh, FName Bone) { return Mesh->GetSocketLocation(Bone); }
}

// =================================================================================================
// Soldier
// =================================================================================================

double AWWOSoldier::NextBurstTime = 0.0;

AWWOSoldier::AWWOSoldier(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	GetCapsuleComponent()->InitCapsuleSize(30.f, 90.f);
	BodyHitbox->InitCapsuleSize(30.f, 75.f);
	BodyHitbox->SetRelativeLocation(FVector(0, 0, -15.f)); // 1.5 m tall from the feet
	Body->BodyColor = FLinearColor(0.55f, 0.15f, 0.13f);
	Body->JointColor = FLinearColor(0.16f, 0.16f, 0.18f);
	Body->ModelKey = "soldier"; // Config/CharacterModels.json
}

UWWOEnemyData* AWWOSoldier::Data() const { return D(); }

UWWOSoldierData* AWWOSoldier::D() const
{
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	return L ? L->EnemyDataAs<UWWOSoldierData>(WWOIds::Enemy::Soldier) : nullptr;
}

void AWWOSoldier::Setup()
{
	const USkeletalMeshComponent* Mesh = GetMesh();
	PinCrit("head", BonePos(Mesh, "head") + FVector(0, 0, 10.f) + GetActorForwardVector() * 2.f, FVector(12.f, 11.f, 13.5f));
	// A gray-box pistol in the right hand. Its muzzle glows during the windup.
	Gun = NewObject<USceneComponent>(this, TEXT("Gun"));
	Gun->SetupAttachment(GetRootComponent());
	Gun->SetUsingAbsoluteRotation(true);
	Gun->RegisterComponent();
	const FLinearColor GunColor(0.12f, 0.12f, 0.13f), GlowColor(1.f, 0.35f, 0.1f);
	auto Box = [this](const FVector& SizeM, const FVector& PosCm, const FLinearColor& Color, float GlowAmount) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* C = WWOFX::AddShape(this, Gun, WWOFX::Cube(), TEXT("M_WWO_Solid"), Color, GlowAmount);
		C->SetRelativeScale3D(SizeM);
		C->SetRelativeLocation(PosCm);
		return C;
	};
	Box(FVector(0.26f, 0.05f, 0.08f), FVector(9.f, 0, 0), GunColor, 0.f);   // barrel/body (x forward)
	Box(FVector(0.06f, 0.045f, 0.12f), FVector(0, 0, -8.f), GunColor, 0.f); // grip
	UStaticMeshComponent* Muzzle = Box(FVector(0.03f, 0.06f, 0.06f), FVector(23.f, 0, 0), GlowColor, 0.4f);
	GlowMat = WWOFX::MID(Muzzle);
	MuzzleLight = WWOFX::AddPointLight(this, Gun, GlowColor, 0.f, 200.f, FVector(26.f, 0, 0));
	if (const UWWOSoldierData* S = D()) Cooldown = WWO::FRandRange(0.5f, S->BurstCooldown);
	StrafeDir = WWO::FRand() < 0.5f ? 1.f : -1.f;
}

void AWWOSoldier::OnReset()
{
	CancelAttack();
	if (const UWWOSoldierData* S = D()) Cooldown = WWO::FRandRange(0.5f, S->BurstCooldown);
	Cover = nullptr;
	if (Gun) Gun->SetVisibility(true, true);
}

void AWWOSoldier::OnHit(bool bCritical)
{
	// A quick flinch on the upper body on top of everything; the legs keep walking.
	Body->PlayUpperShot(bCritical ? "Hit_Head" : "Hit_Chest", 0.05f, 0.2f);
}

void AWWOSoldier::OnDeath()
{
	CancelAttack();
	Body->ClearLayers(); // the animation tree stops; the death plays on its own
	if (Gun) Gun->SetVisibility(false, true);
	if (MuzzleLight) MuzzleLight->SetIntensity(0.f);
}

void AWWOSoldier::MoveBlockedBy(const FHitResult& Impact)
{
	Super::MoveBlockedBy(Impact);
	if (FMath::Abs(Impact.ImpactNormal.Z) < 0.5f) bBlocked = true; // bumped into a wall
}

void AWWOSoldier::AfterMove(float DeltaSeconds)
{
	if (bBlocked) StrafeDir = -StrafeDir; // strafe the other way
	bBlocked = false;
}

FString AWWOSoldier::DebugState() const
{
	if (!bAlerted) return TEXT("idle");
	const FString Attack = Windup >= 0.f ? TEXT("+windup") : (ShotsLeft > 0 ? TEXT("+burst") : TEXT(""));
	if (Cover.IsValid() && Health <= MaxHealth * D()->CoverHealth) return TEXT("cover") + Attack;
	return (bSees ? TEXT("engage") : TEXT("seek")) + Attack;
}

FVector AWWOSoldier::MuzzleLocation() const
{
	return Gun ? Gun->GetComponentTransform().TransformPosition(FVector(26.f, 0, 0)) : GetActorLocation();
}

FVector AWWOSoldier::Think(float DeltaSeconds)
{
	const UWWOSoldierData* S = D();
	const FVector ToTarget = FlatToTarget();
	const float Distance = ToTarget.Size() / WWO::M;
	bSees = CanSeeTarget(CritBox->GetComponentLocation());
	if (!bAlerted && bSees && Distance <= S->SightRange) bAlerted = true;
	if (!bAlerted) return FVector::ZeroVector;
	TurnToward(ToTarget, DeltaSeconds);
	UpdateAttack(DeltaSeconds, bSees);
	return ChooseMove(ToTarget, Distance, DeltaSeconds) * S->WalkSpeed;
}

FVector AWWOSoldier::ChooseMove(const FVector& ToTarget, float Distance, float DeltaSeconds)
{
	const UWWOSoldierData* S = D();
	const FVector Toward = ToTarget.GetSafeNormal();
	// Low on health: get behind cover if there's any nearby.
	if (Health <= MaxHealth * S->CoverHealth)
	{
		CoverCheck -= DeltaSeconds;
		if (CoverCheck <= 0.f)
		{
			CoverCheck = 0.5f;
			Cover = NearestCover();
		}
		if (Cover.IsValid())
		{
			const FVector Spot = CoverSpot(Cover.Get());
			const FVector ToSpot = FVector(Spot.X - GetActorLocation().X, Spot.Y - GetActorLocation().Y, 0.f);
			return ToSpot.Size() > 40.f ? PathDirection(Spot, DeltaSeconds) : FVector::ZeroVector;
		}
	}
	// Too close: back off. With a wall right behind it, Godot keeps backing into the wall;
	// here it strafes instead (below), so it doesn't stand pinned there.
	if (Distance < S->MinRange && !WallAhead(-Toward) && !LedgeAhead(-Toward)) return -Toward; // nor off a ledge
	if (Distance >= S->MinRange && (Distance > S->MaxRange || !bSees)) return PathDirection(TargetActor->GetActorLocation(), DeltaSeconds); // walk around walls to get a shot
	StrafeLeft -= DeltaSeconds;
	if (StrafeLeft <= 0.f)
	{
		StrafeLeft = S->StrafeTime * WWO::FRandRange(0.6f, 1.4f);
		StrafeDir = -StrafeDir;
	}
	FVector Strafe = FVector::CrossProduct(Toward, FVector::UpVector) * StrafeDir;
	if (LedgeAhead(Strafe))
	{
		// The deck ends that way: go the other way, or stand if it ends there too.
		StrafeDir = -StrafeDir;
		Strafe = -Strafe;
		if (LedgeAhead(Strafe)) return FVector::ZeroVector;
	}
	return Strafe;
}

AActor* AWWOSoldier::NearestCover() const
{
	AActor* Best = nullptr;
	float BestDistance = 25.f * WWO::M; // ignore cover farther than this
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (!It->ActorHasTag("Cover")) continue;
		const float D = FVector::Dist(GetActorLocation(), It->GetActorLocation());
		if (D < BestDistance)
		{
			Best = *It;
			BestDistance = D;
		}
	}
	return Best;
}

FVector AWWOSoldier::CoverSpot(const AActor* CoverActor) const
{
	// Just behind the cover, on the side away from the target.
	FVector Origin, Extent;
	CoverActor->GetActorBounds(true, Origin, Extent);
	FVector Away = Origin - TargetActor->GetActorLocation();
	Away.Z = 0.f;
	const float HalfWidth = FMath::Max(Extent.X, Extent.Y);
	FVector Spot = Origin + Away.GetSafeNormal() * (HalfWidth + 70.f);
	Spot.Z = GetActorLocation().Z;
	return Spot;
}

void AWWOSoldier::UpdateAttack(float DeltaSeconds, bool bCanSee)
{
	const UWWOSoldierData* S = D();
	if (ShotsLeft > 0) // mid-burst
	{
		ShotTimer -= DeltaSeconds;
		if (ShotTimer <= 0.f)
		{
			Fire();
			ShotsLeft -= 1;
			ShotTimer = S->BurstInterval;
			if (ShotsLeft == 0) Cooldown = S->BurstCooldown * WWO::FRandRange(0.85f, 1.15f);
		}
		return;
	}
	if (Windup >= 0.f)
	{
		Windup += DeltaSeconds;
		if (Windup >= S->WindupTime)
		{
			Windup = -1.f;
			ShotsLeft = FMath::Max(FMath::RoundToInt(S->BurstCount), 1);
			ShotTimer = 0.f;
		}
		return;
	}
	Cooldown -= DeltaSeconds;
	if (Cooldown > 0.f || !bCanSee) return;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < NextBurstTime && NextBurstTime - Now < 10.0) return; // another soldier just started a burst
	NextBurstTime = Now + S->GroupSpacing;
	Windup = 0.f;
}

void AWWOSoldier::Fire()
{
	const UWWOSoldierData* S = D();
	const FVector From = MuzzleLocation();
	const FVector Dir = WWO::RandomInCone((TargetAimPoint() - From).GetSafeNormal(), FMath::DegreesToRadians(S->Spread)); // no leading: strafing dodges
	AWWOEnemyProjectile::Fire(GetWorld(), From, Dir * S->ProjectileSpeed * WWO::M, S->ProjectileDamage);
}

void AWWOSoldier::Animate(float DeltaSeconds)
{
	const UWWOSoldierData* S = D();
	if (!S || !Gun) return;
	// Windup tell: the muzzle glow ramps up, then stays bright through the burst.
	float GlowTarget = 0.f;
	if (Windup >= 0.f) GlowTarget = FMath::Clamp(Windup / FMath::Max(S->WindupTime, 0.01f), 0.f, 1.f);
	else if (ShotsLeft > 0) GlowTarget = 1.f;
	Glow = GlowTarget < Glow ? WWO::MoveToward(Glow, GlowTarget, 8.f * DeltaSeconds) : GlowTarget;
	if (GlowMat) GlowMat->SetScalarParameterValue("Glow", FMath::Lerp(0.4f, 8.f, Glow));
	if (MuzzleLight) MuzzleLight->SetIntensity(Glow * 2.5f * 1500.f);
	// The gun sits in the right hand. In combat it points at the target; otherwise it points
	// where the soldier faces, tipped down.
	AimBlend = WWO::MoveToward(AimBlend, bAlerted ? 1.f : 0.f, 4.f * DeltaSeconds);
	// Animation: aim when in combat (upper body), idle or walk by speed (soldier.gd's tree).
	const float Speed = GetVelocity().Size2D() / WWO::M;
	Body->SetMix("Idle", "Walk", FMath::Clamp(Speed / 1.f, 0.f, 1.f), FMath::Max(Speed / 1.6f, 0.5f));
	Body->SetUpperBody("Pistol_Aim_Neutral", AimBlend);
	NetGlow = (uint8)FMath::RoundToInt(GlowTarget * 255.f);
	bNetAlerted = bAlerted;
	const FVector AimAt = bAlerted && TargetActor.IsValid() ? TargetAimPoint() : FVector::ZeroVector;
	PlaceGun(AimBlend, bAlerted && TargetActor.IsValid() ? &AimAt : nullptr);
}

void AWWOSoldier::PlaceGun(float Aim, const FVector* AimAt)
{
	// In the right hand, pointing where the soldier faces and tipped down; in a fight it comes up
	// onto the target (a joiner's copy has no target: level along its facing).
	const FVector Hand = GetMesh()->GetSocketLocation("hand_r");
	Gun->SetWorldLocation(Hand);
	FVector AimPoint = Hand + GetActorForwardVector() * 100.f - FVector(0, 0, 60.f);
	const FVector Raised = AimAt ? *AimAt : Hand + GetActorForwardVector() * 100.f;
	AimPoint = FMath::Lerp(AimPoint, Raised, Aim);
	if (!Hand.Equals(AimPoint)) Gun->SetWorldRotation((AimPoint - Hand).Rotation());
}

void AWWOSoldier::AnimateRemote(float DeltaSeconds)
{
	if (!Gun) return;
	// The host's copy does this in Animate and OnDeath; here the same looks from what it replicates.
	Gun->SetVisibility(!bDead, true);
	const float GlowTarget = bDead ? 0.f : NetGlow / 255.f;
	Glow = GlowTarget < Glow ? WWO::MoveToward(Glow, GlowTarget, 8.f * DeltaSeconds) : GlowTarget;
	if (GlowMat) GlowMat->SetScalarParameterValue("Glow", FMath::Lerp(0.4f, 8.f, Glow));
	if (MuzzleLight) MuzzleLight->SetIntensity(Glow * 2.5f * 1500.f);
	AimBlend = WWO::MoveToward(AimBlend, bNetAlerted && !bDead ? 1.f : 0.f, 4.f * DeltaSeconds);
	PlaceGun(AimBlend, nullptr);
}

void AWWOSoldier::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWWOSoldier, NetGlow);
	DOREPLIFETIME(AWWOSoldier, bNetAlerted);
}

// =================================================================================================
// Swarm
// =================================================================================================

AWWOSwarm::AWWOSwarm(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	ModelScale = 0.95f;
	GetCapsuleComponent()->InitCapsuleSize(32.f, 82.f);
	BodyHitbox->InitCapsuleSize(32.f, 65.f);
	BodyHitbox->SetRelativeLocation(FVector(0, 0, -14.f));
	Body->BodyColor = FLinearColor(0.62f, 0.68f, 0.2f);
	Body->JointColor = FLinearColor(0.2f, 0.15f, 0.1f);
	Body->ModelKey = "swarm";
	Tags.Add("Swarm");
}

UWWOEnemyData* AWWOSwarm::Data() const { return D(); }

UWWOSwarmData* AWWOSwarm::D() const
{
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	return L ? L->EnemyDataAs<UWWOSwarmData>(WWOIds::Enemy::Swarm) : nullptr;
}

void AWWOSwarm::Setup()
{
	Body->SetHunch(55.f, 45.f); // HunchModifier: bent forward at the spine, head lifted to look ahead
	PinCrit("head", BonePos(GetMesh(), "head") + FVector(0, 0, 10.f * ModelScale) + GetActorForwardVector() * 2.f, FVector(12.f, 11.f, 13.f));
	if (const UWWOSwarmData* S = D()) LeapCooldown = WWO::FRandRange(0.f, S->LeapCooldown);
}

FString AWWOSwarm::DebugState() const
{
	if (!bAlerted) return TEXT("idle");
	if (bLeaping) return TEXT("leap");
	const FVector To = FlatToTarget();
	return To.Size() / WWO::M <= D()->BiteRange ? TEXT("bite") : TEXT("rush");
}

void AWWOSwarm::OnReset()
{
	bLeaping = false;
	BiteTimer = 0.f;
}

FVector AWWOSwarm::Think(float DeltaSeconds)
{
	const UWWOSwarmData* S = D();
	const FVector ToTarget = FlatToTarget();
	const float Distance = ToTarget.Size() / WWO::M;
	if (!bAlerted && Distance <= S->SightRange && CanSeeTarget(CritBox->GetComponentLocation())) bAlerted = true;
	if (!bAlerted) return FVector::ZeroVector;
	LeapCooldown -= DeltaSeconds;
	BiteTimer -= DeltaSeconds;
	TurnToward(ToTarget, DeltaSeconds);
	const FVector Toward = ToTarget.GetSafeNormal();
	if (bLeaping)
	{
		// In the air: hurts once if it reaches you, ends when it lands.
		LeapAge += DeltaSeconds;
		if (!bLeapHit && FVector::Dist(GetActorLocation(), TargetActor->GetActorLocation()) < 120.f)
		{
			bLeapHit = true;
			DamageTarget(S->LeapDamage, true); // bites and leaps are melee
		}
		return FVector::ZeroVector;
	}
	// On the level above or below (a platform, a deck): flat distance says "in reach" through the floor.
	const bool bSameLevel = HeightToTarget() <= 120.f;
	if (Distance <= S->BiteRange && bSameLevel)
	{
		if (BiteTimer <= 0.f)
		{
			BiteTimer = S->BiteInterval;
			DamageTarget(S->BiteDamage, true);
			Body->Play("Punch_Jab", 1.6f, 0.05f);
		}
		return FVector::ZeroVector; // stand and bite
	}
	if (Distance <= S->LeapRange && bSameLevel && LeapCooldown <= 0.f && GetCharacterMovement()->IsMovingOnGround())
	{
		Leap(Toward);
		return FVector::ZeroVector;
	}
	return (PathDirection(TargetActor->GetActorLocation(), DeltaSeconds) + SpreadPush()).GetSafeNormal() * S->WalkSpeed;
}

void AWWOSwarm::Leap(const FVector& Toward)
{
	const UWWOSwarmData* S = D();
	bLeaping = true;
	bLeapHit = false;
	LeapAge = 0.f;
	LeapCooldown = S->LeapCooldown;
	EnemyMovement()->bSteering = false; // keep the launch velocity until it lands
	const float Up = FMath::Sqrt(2.f * 20.f * S->LeapHeight);
	GetCharacterMovement()->Velocity = (Toward * S->LeapSpeed + FVector(0, 0, Up)) * WWO::M;
	GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	Body->Play("Jump", 1.f, 0.05f);
}

void AWWOSwarm::Animate(float DeltaSeconds)
{
	if (bLeaping) return;
	if (Body->CurrentAnim() == "Punch_Jab") return; // let the bite finish
	const float Speed = GetVelocity().Size2D() / WWO::M;
	constexpr float RunAnimSpeed = 6.5f; // m/s the Sprint animation looks right at, at full size
	if (Speed > 0.5f) Body->Play("Sprint_Unarmed", FMath::Clamp(Speed / (RunAnimSpeed * ModelScale), 0.6f, 2.5f)); // (Sprint itself carries a gun)
	else Body->Play("Idle_Unarmed");
}

void AWWOSwarm::AfterMove(float DeltaSeconds)
{
	if (bLeaping && LeapAge > 0.1f && GetCharacterMovement()->IsMovingOnGround())
	{
		bLeaping = false;
		EnemyMovement()->bSteering = true;
	}
}

FVector AWWOSwarm::SpreadPush() const
{
	// A nudge away from nearby swarm, so the pack fans out.
	const UWWOSwarmData* S = D();
	FVector Push = FVector::ZeroVector;
	for (AWWOEnemy* Other : UWWOWorldSubsystem::Get(this)->LivingEnemies())
	{
		if (Other == this || !Cast<AWWOSwarm>(Other)) continue;
		FVector Away = GetActorLocation() - Other->GetActorLocation();
		Away.Z = 0.f;
		const float Dm = Away.Size() / WWO::M;
		if (Dm > 0.01f && Dm < S->SpreadDistance) Push += Away.GetSafeNormal() * (1.f - Dm / S->SpreadDistance);
	}
	return Push * 1.5f;
}

// =================================================================================================
// Tank
// =================================================================================================

namespace
{
	const FLinearColor ArmorColor(0.18f, 0.16f, 0.14f);
	const FLinearColor WeakColor(1.f, 0.45f, 0.1f);
	const FLinearColor ShieldColor(0.35f, 0.75f, 1.f);
	const FLinearColor ShellColor(1.f, 0.5f, 0.15f);
}

AWWOTank::AWWOTank(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	ModelScale = 1.55f;
	GetCapsuleComponent()->InitCapsuleSize(45.f, 140.f);
	BodyHitbox->InitCapsuleSize(42.f, 130.f);
	BodyHitbox->SetRelativeLocation(FVector(0, 0, -10.f));
	Body->BodyColor = FLinearColor(0.3f, 0.33f, 0.22f);
	Body->JointColor = FLinearColor(0.12f, 0.12f, 0.14f);
	Body->ModelKey = "tank";
}

UWWOEnemyData* AWWOTank::Data() const { return D(); }

UWWOTankData* AWWOTank::D() const
{
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	return L ? L->EnemyDataAs<UWWOTankData>(WWOIds::Enemy::Tank) : nullptr;
}

void AWWOTank::Setup()
{
	const USkeletalMeshComponent* Mesh = GetMesh();
	const FVector Back = -GetActorForwardVector();
	const FVector Chest = BonePos(Mesh, "spine_04");
	const float S = ModelScale;
	// The weak spot: a glowing power pack between the shoulder blades, sticking out of the
	// back so shots from behind reach it.
	const FVector WeakCenter = Chest + Back * 27.f * S;
	PinCrit("spine_04", WeakCenter, FVector(9.5f, 17.f, 15.5f));
	Body->AttachBox("spine_04", WeakCenter, FVector(0.12f, 0.22f, 0.2f) * S, WeakColor, 3.f);
	Body->AttachBox("spine_04", Chest + Back * 18.f * S, FVector(0.06f, 0.28f, 0.26f) * S, ArmorColor);
	// Armor plates.
	const FVector Head = BonePos(Mesh, "head");
	Body->AttachBox("head", Head + FVector(0, 0, 10.f * S), FVector(0.27f, 0.24f, 0.12f) * S, ArmorColor);
	Body->AttachBox("spine_04", Chest - Back * 14.f * S, FVector(0.07f, 0.36f, 0.3f) * S, ArmorColor);
	for (const TCHAR* Side : { TEXT("upperarm_l"), TEXT("upperarm_r") })
	{
		const FVector Shoulder = BonePos(Mesh, Side);
		Body->AttachBox(Side, Shoulder + FVector(0, 0, 1.f * S), FVector(0.19f, 0.16f, 0.07f) * S, ArmorColor);
	}
	// The energy shield bubble.
	Bubble = WWOFX::AddSoftSphere(this, GetRootComponent(), ShieldColor, 0.06f, 0.5f, false, true);
	Bubble->SetRelativeScale3D(FVector(0.75f, 0.75f, 1.15f) * ModelScale * WWO::M * 2.f / 100.f);
	Bubble->SetRelativeLocation(FVector(0, 0, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 140.f));
	if (HasAuthority()) ResetShield(); // a joiner's copy keeps the shield the host sent
}

void AWWOTank::ResetShield()
{
	MaxShield = D() ? D()->Shield : 0.f;
	Shield = MaxShield;
	SinceHit = 99.f;
}

float AWWOTank::Absorb(float Damage, bool bCritical, const FVector& Point)
{
	SinceHit = 0.f;
	if (Shield <= 0.f) return Damage;
	const float Taken = FMath::Min(Damage, Shield);
	Shield -= Taken;
	BubbleFlash = 1.f;
	UWWOWorldSubsystem::Get(this)->SpawnDamageNumber(Taken, false, Point, ShieldColor);
	if (Shield <= 0.f) ShieldBreak();
	return Damage - Taken;
}

void AWWOTank::ShieldBreak()
{
	// A quick bright pop as the bubble shatters.
	if (AWWOFXActor* FX = AWWOFXActor::Spawn(GetWorld(), Bubble->GetComponentLocation(), 0.25f))
	{
		UStaticMeshComponent* Pop = WWOFX::AddSoftSphere(FX, FX->GetRootComponent(), ShieldColor, 0.6f, 0.5f, false, true);
		const FVector Scale = Bubble->GetComponentScale();
		FX->ScaleTween(Pop, Scale, Scale * 1.3f, 0.25f);
		FX->ParamTween(WWOFX::MID(Pop), "Alpha", 0.6f, 0.f, 0.25f);
	}
}

FString AWWOTank::DebugState() const
{
	if (!bAlerted) return TEXT("idle");
	if (SlamTime >= 0.f) return SlamTime < D()->SlamWindup ? TEXT("slam-windup") : TEXT("slam-recover");
	return Lobbing > 0.f ? TEXT("lob") : TEXT("advance");
}

bool AWWOTank::IsCriticalComponent(const UPrimitiveComponent* Component) const
{
	return Shield <= 0.f && Super::IsCriticalComponent(Component); // no crits through the shield
}

void AWWOTank::OnReset()
{
	ResetShield();
	SlamTime = -1.f;
	Lobbing = 0.f;
	LobTimer = 1.5f;
	if (Bubble) Bubble->SetVisibility(true);
}

void AWWOTank::OnDeath()
{
	SlamTime = -1.f;
	if (Bubble) Bubble->SetVisibility(false);
}

FVector AWWOTank::Think(float DeltaSeconds)
{
	const UWWOTankData* T = D();
	SinceHit += DeltaSeconds;
	if (MaxShield > 0.f && SinceHit >= T->ShieldRegenDelay && Shield < MaxShield)
	{
		Shield = FMath::Min(Shield + T->ShieldRegenRate * DeltaSeconds, MaxShield);
	}
	const FVector ToTarget = FlatToTarget();
	const float Distance = ToTarget.Size() / WWO::M;
	const FVector Head = GetMesh()->GetSocketLocation("head");
	if (!bAlerted && Distance <= T->SightRange && CanSeeTarget(Head)) bAlerted = true;
	if (!bAlerted) return FVector::ZeroVector;
	SlamCooldown -= DeltaSeconds;
	LobTimer -= DeltaSeconds;
	Lobbing = FMath::Max(Lobbing - DeltaSeconds, 0.f);
	if (SlamTime >= 0.f)
	{
		SlamTime += DeltaSeconds;
		if (!bSlamLanded && SlamTime >= T->SlamWindup)
		{
			bSlamLanded = true;
			Slam();
		}
		if (SlamTime >= T->SlamWindup + T->SlamRecover)
		{
			SlamTime = -1.f;
			SlamCooldown = T->SlamCooldown;
		}
		return FVector::ZeroVector; // planted while slamming and recovering
	}
	TurnToward(ToTarget, DeltaSeconds);
	if (Distance <= T->SlamRange && SlamCooldown <= 0.f && HeightToTarget() <= 150.f && CanSeeTarget(Head))
	{
		SlamTime = 0.f;
		bSlamLanded = false;
		Body->Play("Spell_Simple_Enter", 0.6f, 0.15f); // arms up: the warning
		return FVector::ZeroVector;
	}
	if (Distance > T->SlamRange && LobTimer <= 0.f && CanSeeTarget(Head))
	{
		LobTimer = T->LobInterval;
		Lobbing = 0.5f;
		Body->Play("Spell_Simple_Shoot", 1.f, 0.1f);
		Lob();
	}
	return PathDirection(TargetActor->GetActorLocation(), DeltaSeconds) * T->WalkSpeed; // never retreats
}

void AWWOTank::Slam()
{
	const UWWOTankData* T = D();
	Body->Play("Jump_Land", 1.4f, 0.05f);
	AWWOGameState::Shockwave(GetWorld(), FeetLocation() + FVector(0, 0, 10.f), FLinearColor(1.f, 0.7f, 0.3f, 0.8f), T->SlamRadius, 0.45f); // everyone sees it
	// Hits every player (online: all of them, dead or not) and living AI teammates in range, by their
	// centre on the ground and at most 1.5 m above or below (not through a deck). The slam counts as melee.
	FWWOAreaQuery Q;
	Q.Center = GetActorLocation();
	Q.Radius = T->SlamRadius * WWO::M;
	Q.bFlat = true;
	Q.MaxHeight = 150.f;
	Q.bHeightInclusive = true;
	Q.Measure = EWWOAreaMeasure::Centre;
	Q.Targets = EWWOAreaTargets::None;
	Q.Players = EWWOAreaPlayers::All;
	Q.bTeammates = true;
	UWWOWorldSubsystem::Get(this)->ForEachTargetInRadius(Q, [this, T](const FWWOAreaHit& Hit)
	{
		if (AWWOCharacter* P = HurtFriendly(Hit.Actor, T->SlamDamage, GetActorLocation(), true)) P->ApplySlow(T->SlowAmount, T->SlowTime);
		return true;
	});
}

void AWWOTank::Lob()
{
	// An explosive shell in an arc that lands where the target is standing now.
	const UWWOTankData* T = D();
	const FVector Hand = GetMesh()->GetSocketLocation("hand_r") + FVector(0, 0, 30.f);
	const FVector Target = TargetActor->GetActorLocation() - FVector(0, 0, 90.f); // at the feet
	const float Time = FMath::Max(T->LobFlightTime, 0.3f);
	const float Gravity = 20.f * WWO::M;
	const FVector Velocity = (Target - Hand) / Time + FVector(0, 0, 0.5f * Gravity * Time);
	AWWOEnemyProjectile::Fire(GetWorld(), Hand, Velocity, T->LobDamage, Time + 2.f, Gravity, T->LobRadius, ShellColor, 0.2f);
}

void AWWOTank::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWWOTank, Shield);
	DOREPLIFETIME(AWWOTank, MaxShield);
}

void AWWOTank::OnRep_Shield(float Before)
{
	if (Shield < Before) BubbleFlash = 1.f;                 // hit
	if (Before > 0.f && Shield <= 0.f) ShieldBreak();       // the pop
}

void AWWOTank::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority()) UpdateBubble(DeltaSeconds); // the host's runs in Animate
}

void AWWOTank::UpdateBubble(float DeltaSeconds)
{
	if (!Bubble) return;
	Bubble->SetVisibility(Shield > 0.f);
	BubbleFlash = WWO::MoveToward(BubbleFlash, 0.f, 4.f * DeltaSeconds);
	if (UMaterialInstanceDynamic* M = WWOFX::MID(Bubble)) M->SetScalarParameterValue("Alpha", FMath::Lerp(0.06f, 0.35f, BubbleFlash));
}

void AWWOTank::Animate(float DeltaSeconds)
{
	if (!Bubble) return;
	UpdateBubble(DeltaSeconds);
	if (SlamTime >= 0.f || Lobbing > 0.f) return; // the slam and throw poses play out
	const float Speed = GetVelocity().Size2D() / WWO::M;
	constexpr float WalkAnimSpeed = 1.6f; // m/s the Walk animation looks right at, at full size
	if (Speed > 0.3f) Body->Play("Walk_Unarmed", FMath::Clamp(Speed / (WalkAnimSpeed * ModelScale), 0.5f, 2.f));
	else Body->Play("Idle_Unarmed");
}
