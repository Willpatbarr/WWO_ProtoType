// AWWOCharacter: as a PvP target (hitboxes, the head pinned to the head bone, headshots, ApplyHit,
// health for IWWOTargetable) and the status effects on it (slow, blind, veil, entrench) with their RPCs.

#include "Player/WWOCharacter.h"
#include "WhatWasOurs.h"
#include "Player/WWOCharacterPrivate.h"
#include "WWOTypes.h"
#include "Engine/World.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Components/SphereComponent.h"
#include "Flora/WWOFloraEffects.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOUltComponent.h"
#include "Flora/WWOSporeFog.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "AnimationRuntime.h"
#include "Engine/SkeletalMesh.h"

using namespace WWOCharacter; // the constants these files share (WWOCharacterPrivate.h)

namespace
{
	constexpr float HeadHitUp = 10.f;         // skull center above the head bone, as on the enemies
	constexpr float HeadHitForward = 2.f;
	constexpr float HeadShotThrough = 60.f;   // a shot that lands this close in front of the head still counts
}

void AWWOCharacter::TickStatusTimers(float DeltaSeconds)
{
	SlowLeft = FMath::Max(SlowLeft - DeltaSeconds, 0.f);
	VeilLeft = FMath::Max(VeilLeft - DeltaSeconds, 0.f);
	EntrenchLeft = FMath::Max(EntrenchLeft - DeltaSeconds, 0.f);
	BlindLeft = FMath::Max(BlindLeft - DeltaSeconds, 0.f);
}

void AWWOCharacter::ApplyBlind(float Seconds)
{
	if (Seconds <= 0.f) return;
	BlindLeft = FMath::Max(BlindLeft, Seconds);
	BlindTotal = FMath::Max(BlindLeft, 0.1f);
	if (IsHostCopyOfRemote()) ClientApplyBlind(Seconds);
}

void AWWOCharacter::ClientApplyBlind_Implementation(float Seconds)
{
	ApplyBlind(Seconds);
}

void AWWOCharacter::ClientDamageFrom_Implementation(FVector_NetQuantize From)
{
	Health->AddDamageMark(From);
}

void AWWOCharacter::ClientApplySlow_Implementation(float Amount, float Seconds)
{
	ApplySlow(Amount, Seconds); // the host slowed you: your moves must slow too, or they disagree
}

void AWWOCharacter::MulticastEntrenchRoots_Implementation(float Seconds)
{
	if (HasAuthority() || IsLocallyControlled()) return; // the host's and the player's own already show them
	AWWOEntrenchRoots::Spawn(GetWorld(), this, Seconds);
}

// --- PvP target --------------------------------------------------------------------------------

void AWWOCharacter::UpdateHitboxes()
{
	const bool bOn = UWWOWorldSubsystem::IsPvP(this) && !Health->bDead;
	if (bOn != bHitboxesOn)
	{
		bHitboxesOn = bOn;
		const ECollisionEnabled::Type Mode = bOn ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision;
		BodyHitbox->SetCollisionEnabled(Mode);
		HeadHitbox->SetCollisionEnabled(Mode);
		// The movement capsule never takes shots (the Pawn profile ignores that channel): alive in
		// PvP the hitboxes and the head get them, and dead nothing does.
		GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	}
	if (bOn) BodyHitbox->SetCapsuleHalfHeight(FMath::Max(GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - 20.f, 35.f));
}

TArray<FWWOAimPart> AWWOCharacter::GetAimParts() const
{
	TArray<FWWOAimPart> Parts;
	Parts.Add({ HeadHitbox->GetComponentLocation(), HeadHitbox->GetUnscaledSphereRadius(), true });
	Parts.Add({ BodyHitbox->GetComponentLocation(), BodyHitbox->GetUnscaledCapsuleRadius(), false });
	return Parts;
}

bool AWWOCharacter::IsCriticalComponent(const UPrimitiveComponent* Component) const
{
	return Component == HeadHitbox;
}

bool AWWOCharacter::IsCriticalHit(const FHitResult& Hit, const FVector& From) const
{
	// The body hitbox is a loose capsule around the whole character, so a shot aimed at the head
	// often meets it first (a head leaning into a run, a shot from above or behind). What counts is
	// whether the shot's line goes through the head.
	if (Hit.GetComponent() == HeadHitbox) return true;
	if (!HeadHitbox->IsCollisionEnabled()) return false;
	const FVector Dir = (Hit.ImpactPoint - From).GetSafeNormal();
	if (Dir.IsNearlyZero()) return false;
	FHitResult Through;
	return HeadHitbox->LineTraceComponent(Through, Hit.ImpactPoint - Dir, Hit.ImpactPoint + Dir * HeadShotThrough,
		FCollisionQueryParams(SCENE_QUERY_STAT(WWOHeadShot), false));
}

void AWWOCharacter::PinHeadHitbox()
{
	// The head hitbox rides the body's head bone, like the enemies' crit boxes, so it is where the
	// head is drawn in every pose (running lean, crouch, slide, hit reactions) and not at the eye
	// point on the capsule's axis. Placed from the reference pose, so it doesn't depend on the pose
	// the body happens to be in right now.
	static const FName HeadBone("head");
	USkeletalMeshComponent* BodyMesh = GetMesh();
	const USkeletalMesh* Asset = BodyMesh ? BodyMesh->GetSkeletalMeshAsset() : nullptr;
	const int32 Bone = Asset ? Asset->GetRefSkeleton().FindBoneIndex(HeadBone) : INDEX_NONE;
	if (Bone == INDEX_NONE)
	{
		UE_LOG(LogWWO, Warning, TEXT("%s: no head bone, the head hitbox stays at the eyes"), *GetName());
		return;
	}
	// The mesh faces +Y in its own space (it's turned -90 on the capsule) with Z up.
	const FTransform Ref = FAnimationRuntime::GetComponentSpaceTransformRefPose(Asset->GetRefSkeleton(), Bone);
	const FVector Local = Ref.InverseTransformPosition(Ref.GetLocation() + FVector(0, HeadHitForward, HeadHitUp));
	HeadHitbox->AttachToComponent(BodyMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, HeadBone);
	HeadHitbox->SetRelativeLocation(Local);
}

void AWWOCharacter::ProbeHeadShots(int32& Shots, int32& Crits, int32& OldCrits, float& HeadOffEyes) const
{
	if (!bHitboxesOn) return;
	const FVector Head = GetMesh()->GetSocketLocation("head") + FVector(0, 0, HeadHitUp) + GetActorForwardVector() * HeadHitForward;
	const FVector Eyes = Camera->GetComponentLocation();
	HeadOffEyes = FMath::Max(HeadOffEyes, FVector::Dist(Head, Eyes));
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOHeadProbe), false);
	for (int32 Side = 0; Side < 8; ++Side)
	{
		for (const float Up : { 0.f, 20.f })
		{
			const FVector Away = FRotator(Up, GetActorRotation().Yaw + Side * 45.f, 0.f).Vector();
			const FVector From = Head + Away * 1500.f;
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Away).GetSafeNormal();
			for (const FVector2D& Off : { FVector2D(0, 0), FVector2D(6, 0), FVector2D(-6, 0), FVector2D(0, 6), FVector2D(0, -6) })
			{
				const FVector At = Head + Right * Off.X + FVector::UpVector * Off.Y;
				const FVector To = From + (At - From).GetSafeNormal() * 2000.f;
				FHitResult BodyHit, HeadHit;
				const bool bBody = BodyHitbox->LineTraceComponent(BodyHit, From, To, Params);
				const bool bHead = HeadHitbox->LineTraceComponent(HeadHit, From, To, Params);
				++Shots;
				if (bBody || bHead)
				{
					const FHitResult& First = bHead && (!bBody || HeadHit.Distance <= BodyHit.Distance) ? HeadHit : BodyHit;
					Crits += IsCriticalHit(First, From) ? 1 : 0;
				}
				// Before: a 16 cm sphere at the eyes, and only if the shot met it before the body.
				const FVector Dir = (To - From).GetSafeNormal();
				const float Along = FVector::DotProduct(Eyes - From, Dir);
				const float OffSq = FVector::DistSquared(From + Dir * Along, Eyes);
				if (OffSq <= 256.f && (!bBody || Along - FMath::Sqrt(256.f - OffSq) <= BodyHit.Distance)) ++OldCrits;
			}
		}
	}
}

bool AWWOCharacter::ApplyHit(float Damage, bool bCritical, const FVector& Point, FLinearColor Color)
{
	if (Health->bDead) return false;
	UWWOWorldSubsystem::Get(this)->SpawnDamageNumber(Damage, bCritical, Point, Color);
	if (!HasAuthority()) return false; // a joiner's hit: the number now; the host applies it (health replicates)
	if (UWWOWorldSubsystem::CurrentAttacker.IsValid())
	{
		Health->LastAttacker = UWWOWorldSubsystem::CurrentAttacker; // damage from nobody doesn't erase who hit you last
		Health->LastAttackTime = GetWorld()->GetTimeSeconds();
		Health->bLastAttackCritical = bCritical;
	}
	const AActor* By = UWWOWorldSubsystem::CurrentAttacker.Get();
	Health->DamageCameFrom(By && By != this ? By->GetActorLocation() : Point); // whoever did it, else where it landed
	Health->TakeDamage(Damage);
	return Health->bDead;
}

bool AWWOCharacter::IsDead() const { return Health->bDead; }
float AWWOCharacter::GetHealth() const { return Health->Health; }
float AWWOCharacter::GetMaxHealth() const { return Health->MaxHealth(); }
float AWWOCharacter::GetShield() const { return Health->Shield; }
float AWWOCharacter::GetMaxShield() const { return Health->MaxShield(); }

FVector AWWOCharacter::FeetLocation() const
{
	return GetActorLocation() - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
}

// --- Status --------------------------------------------------------------------------------

void AWWOCharacter::ApplySlow(float Amount, float Seconds)
{
	SlowAmount = FMath::Max(FMath::Clamp(Amount, 0.f, 0.95f), SlowLeft > 0.f ? SlowAmount : 0.f);
	SlowLeft = FMath::Max(SlowLeft, Seconds);
	if (IsHostCopyOfRemote()) ClientApplySlow(Amount, Seconds);
}

bool AWWOCharacter::IsInvisible() const
{
	return VeilLeft > 0.f && AWWOSporeFog::Contains(GetWorld(), GetActorLocation());
}

float AWWOCharacter::VeilMeleeMultiplier() const
{
	return IsInvisible() ? Ult->VeilMeleeMult : 1.f;
}

float AWWOCharacter::EntrenchRechargeMultiplier() const
{
	return EntrenchLeft > 0.f ? Ult->EntrenchGrenadeMult : 1.f;
}

void AWWOCharacter::ClearStatusEffects()
{
	SlowLeft = 0.f;
	VeilLeft = 0.f;
	EntrenchLeft = 0.f;
	bLunging = false;
	MoveState.bMantling = false;
	MoveState.bSliding = false;
	MoveState.bSprinting = false;
	ResetRecoil();
}
