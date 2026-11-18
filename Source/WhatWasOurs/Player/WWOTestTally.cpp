// FWWOTestTally (WWOTestTally.h): the one tally, the gun lift and the head-shot probe. Not in a
// shipping build (the header's empty inlines stand in; Get still returns a tally, never written).

#include "Player/WWOTestTally.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOCharacterPrivate.h"
#include "Player/WWOWeaponComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SkeletalMeshComponent.h"

FWWOTestTally& FWWOTestTally::Get()
{
	static FWWOTestTally Tally;
	return Tally;
}

#if !UE_BUILD_SHIPPING

void FWWOTestTally::LiftGun(const UWWOWeaponComponent* Gun, const FVector& Offset, const FVector& Rotation)
{
	FWWOTestTally& T = Get();
	T.LiftedGun = Gun;
	T.LiftOffset = Offset;
	T.LiftRotation = Rotation;
}

void FWWOTestTally::AddGunLift(const UWWOWeaponComponent* Gun, FVector& Pos, FVector& Rot)
{
	const FWWOTestTally& T = Get();
	const bool bLifted = Gun && T.LiftedGun.Get() == Gun;
	Pos += bLifted ? T.LiftOffset : FVector::ZeroVector; // (a zero added as before, so the sum's bits don't change)
	Rot += bLifted ? T.LiftRotation : FVector::ZeroVector;
}

void FWWOTestTally::ProbeHeadShots(const AWWOCharacter& Player, int32& Shots, int32& Crits, int32& OldCrits, float& HeadOffEyes)
{
	using namespace WWOCharacter;
	if (!Player.bHitboxesOn) return;
	const FVector Head = Player.GetMesh()->GetSocketLocation("head") + FVector(0, 0, HeadHitUp) + Player.GetActorForwardVector() * HeadHitForward;
	const FVector Eyes = Player.Camera->GetComponentLocation();
	HeadOffEyes = FMath::Max(HeadOffEyes, FVector::Dist(Head, Eyes));
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOHeadProbe), false);
	for (int32 Side = 0; Side < 8; ++Side)
	{
		for (const float Up : { 0.f, 20.f })
		{
			const FVector Away = FRotator(Up, Player.GetActorRotation().Yaw + Side * 45.f, 0.f).Vector();
			const FVector From = Head + Away * 1500.f;
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Away).GetSafeNormal();
			for (const FVector2D& Off : { FVector2D(0, 0), FVector2D(6, 0), FVector2D(-6, 0), FVector2D(0, 6), FVector2D(0, -6) })
			{
				const FVector At = Head + Right * Off.X + FVector::UpVector * Off.Y;
				const FVector To = From + (At - From).GetSafeNormal() * 2000.f;
				FHitResult BodyHit, HeadHit;
				const bool bBody = Player.BodyHitbox->LineTraceComponent(BodyHit, From, To, Params);
				const bool bHead = Player.HeadHitbox->LineTraceComponent(HeadHit, From, To, Params);
				++Shots;
				if (bBody || bHead)
				{
					const FHitResult& First = bHead && (!bBody || HeadHit.Distance <= BodyHit.Distance) ? HeadHit : BodyHit;
					Crits += Player.IsCriticalHit(First, From) ? 1 : 0;
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

#endif
