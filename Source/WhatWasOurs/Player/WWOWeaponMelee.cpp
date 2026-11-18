// UWWOWeaponComponent: melee: the swing, the lunge target, the strike and what a landed punch sets off.

#include "Player/WWOWeaponComponent.h"
#include "WhatWasOurs.h"
#include "Player/WWOWeaponPrivate.h"
#include "Player/WWOCharacter.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Engine/World.h"
#include "Systems/WWOSound.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOAbilityComponent.h"
#include "Flora/WWOFloraEffects.h"
#include "FX/WWOFX.h"
#include "Game/WWOGameState.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Systems/WWOTargetable.h"

using namespace WWOWeapon; // the helpers these files share (WWOWeaponPrivate.h)

namespace
{
	constexpr float MeleeLungeHold = 0.3f; // the swing waits here (wound up) until the lunge arrives
	constexpr float MeleeAngle = 35.f;     // degrees off center a melee can still connect
}

void UWWOWeaponComponent::UpdateMelee(float DeltaSeconds)
{
	AWWOCharacter* P = GetPlayer();
	AWWOPlayerController* PC = P->GetWWOController();
	const bool bMeleePressed = JustPressed((int32)EWWOAction::Melee) || (PC && PC->ConsumeChord(EWWOChord::Melee)); // V, or R1 alone
	if (bMeleePressed && !bMeleeing)
	{
		bMeleeing = true;
		MeleeProgress = 0.f;
		bMeleeLanded = false;
		bMeleeConnected = false; // (the swing's whoosh is a miss's sound: MeleeStrike plays it when nothing was hit)
		CancelReload(); // meleeing cancels a reload, like Destiny
		P->MoveState.bSprinting = false;
		AActor* Target = FindLungeTarget();
		MeleeTarget = Target;
		// Online, the punch shows on your full body for everyone else (the host's copy replicates it).
		const ENetMode Mode = GetNetMode();
		if (Mode == NM_Client) P->ServerMeleeSwing();
		if (Mode != NM_Standalone) P->PlaySwingOnBody();
		if (Target && P->EntrenchLeft <= 0.f) P->StartLunge(Target); // rooted by Entrench: punch in place
	}
	if (!bMeleeing) return;
	// The swing plays alongside the lunge; if the lunge is still travelling when the swing
	// reaches the hit, it waits just short of it and lands the moment you arrive.
	MeleeProgress += DeltaSeconds / FMath::Max(M("melee_time", MeleeTime), 0.05f);
	if (P->IsLunging()) MeleeProgress = FMath::Min(MeleeProgress, MeleeImpactAt - 0.01f);
	if (MeleeProgress >= MeleeImpactAt && !bMeleeLanded)
	{
		bMeleeLanded = true;
		MeleeStrike();
	}
	if (MeleeProgress >= 1.f)
	{
		bMeleeing = false;
		MeleeProgress = 0.f;
	}
}

AActor* UWWOWeaponComponent::FindLungeTarget() const
{
	// The target nearest your crosshair inside the lunge range and angle, or one the swing
	// would hit anyway.
	const AWWOCharacter* P = GetPlayer();
	const FVector Origin = P->EyeLocation();
	const FVector Forward = P->AimForward();
	// Scored by angle + 2 x distance (m, to the part's surface); a part no nearer than the best score is passed over.
	const FWWOAimPick Best = UWWOWorldSubsystem::Get(this)->BestAimPart(this, [&](const AActor*, const FWWOAimPart& Part, float BestScore, float& Score)
	{
		const FVector To = Part.Position - Origin;
		const float Distance = (To.Size() - Part.Radius) / WWO::M;
		if (Distance >= BestScore) return false;
		const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward, To.GetSafeNormal()), -1.f, 1.f)));
		const bool bInCone = Distance <= LungeRange && Angle <= LungeAngle;
		const bool bWillHit = Distance <= MeleeReach && Angle <= MeleeAngle;
		if (!(bInCone || bWillHit)) return false;
		Score = Angle + Distance * 2.f;
		return true;
	}, [this](const FVector& To, FHitResult& Hit) { return CastRayTo(To, Hit); });
	return Best.Actor;
}

void UWWOWeaponComponent::MeleeStrike()
{
	// Hit the closest target part within reach and inside the melee angle.
	AWWOCharacter* P = GetPlayer();
	const FVector Origin = P->EyeLocation();
	const FVector Forward = P->AimForward();
	const IWWOTargetable* Locked = Cast<IWWOTargetable>(MeleeTarget.Get());
	// Scored by distance (m, to the part's surface).
	const FWWOAimPick Pick = UWWOWorldSubsystem::Get(this)->BestAimPart(this, [&](const AActor* Actor, const FWWOAimPart& Part, float BestDistance, float& Score)
	{
		const FVector To = Part.Position - Origin;
		const float Distance = (To.Size() - Part.Radius) / WWO::M;
		if (Distance > MeleeReach || Distance >= BestDistance) return false;
		if (Locked && !Locked->IsDead() && Actor != MeleeTarget.Get()) return false; // the swing lands on the enemy you lunged at
		const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward, To.GetSafeNormal()), -1.f, 1.f)));
		if (Angle > MeleeAngle) return false;
		Score = Distance;
		return true;
	}, [this](const FVector& To, FHitResult& Hit) { return CastRayTo(To, Hit); });
	AActor* Best = Pick.Actor;
	FVector BestPoint = Pick.IsValid() ? FVector(Pick.Hit.ImpactPoint) : FVector::ZeroVector;
	if (!Best && Locked && !Locked->IsDead())
	{
		// The lunge got you there: hit it even if it shifted a little off center.
		const FWWOAimPart Near = Locked->GetAimParts().Last();
		if ((FVector::Dist(Origin, Near.Position) - Near.Radius) / WWO::M <= MeleeReach + 0.5f)
		{
			Best = MeleeTarget.Get();
			BestPoint = Near.Position;
		}
	}
	if (!Best)
	{
		UWWOSound::PlayFor(GetOwner(), "melee_swing"); // a miss: the whoosh, as the fist passes the reticle
		return;
	}
	IWWOTargetable* Target = Cast<IWWOTargetable>(Best);
	FWWODamageBy By(P);
	const float Boosted = M("melee_damage", MeleeDamage) * P->Abilities->MeleeMultiplier() * P->VeilMeleeMultiplier();
	bool bKilled;
	{
		TGuardValue<bool> Seen(UWWOWorldSubsystem::bAttackerSawHit, true); // this punch marks its hit once, below
		if (GetNetMode() == NM_Client)
		{
			// A joiner shows the hit now (flash, number) and predicts the kill from the health the host
			// last sent; the host applies it and the knockback.
			const float Before = Target->GetHealth() + Target->GetShield();
			Target->TakeHit(Boosted, false, BestPoint);
			bKilled = Before > 0.f && Before - Boosted <= 0.f;
			P->ServerMeleeHit(Best, Boosted, Origin, BestPoint);
		}
		else bKilled = Target->TakeHit(Boosted, false, BestPoint); // melee doesn't crit
	}
	OnHitConfirmed.Broadcast(false, bKilled);
	Kick = 1.f;
	KickAge = 0.f;
	AWWOFXActor::HitSpark(GetWorld(), BestPoint, (Origin - BestPoint).GetSafeNormal(), bKilled); // sparks off the hit; the gold flare on a kill
	UWWOSound::Play(this, "melee_hit", BestPoint);
	if (bKilled && P->IsLocalHuman()) UWWOSound::PlayFlat(this, "kill");
	MeleeImpact(Best, bKilled, BestPoint);
}

void UWWOWeaponComponent::MeleeImpact(AActor* TargetActor, bool bKilled, const FVector& Point)
{
	bMeleeConnected = true;
	// Everything that makes a landed melee feel heavy. Kills hit harder.
	AWWOCharacter* P = GetPlayer();
	const float Boost = bKilled ? MeleeKillBoost : 1.f;
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	const UWWOFloraData* Flora = Loadout ? Loadout->GetFlora() : nullptr;
	const FLinearColor Color = Flora && Flora->GraftColor.A > 0.f ? Flora->GraftColor : FLinearColor(0.25f, 0.55f, 1.f);
	if (GetNetMode() == NM_Client)
	{
		// A joiner shows its own discharge now; the host runs the rest (ServerMeleeHit).
		if (const UWWOAbilityData* Thornfist = P->Abilities->ThornfistActive())
		{
			if (Thornfist->DischargeArcs >= 1.f)
			{
				AWWODischarge::Spawn(GetWorld(), TargetActor, Point, (Point - P->EyeLocation()).GetSafeNormal(), Color,
					FMath::RoundToInt(Thornfist->DischargeArcs), Thornfist->DischargeRadius, Thornfist->DischargeTime);
			}
		}
		P->Abilities->OnMeleeLanded();
		P->Abilities->ConsumeStaticCharge(); // used up here too (the HUD); the host sends the chain
	}
	else MeleeKitEffects(TargetActor, Point);
	P->AddRecoil(FMath::DegreesToRadians(MeleeJolt * Boost), 0.f, 0.02f, 1.f, 0.12f); // the view snaps up and settles
	StartPunch(MeleePunchFov * Boost, MeleePunchRoll * Boost, MeleePunchTime);
	Rumble(MeleeRumbleWeak * Boost, MeleeRumbleStrong * Boost, MeleeRumbleTime * Boost);
	if (GetNetMode() != NM_Client) MeleeKnockBack(TargetActor, bKilled); // a joiner's is the host's (it moves the target)
	if (AWWOPlayerController* PC = P->GetWWOController()) PC->Hitstop(MeleeHitstop * Boost);
}

void UWWOWeaponComponent::MeleeKitEffects(AActor* TargetActor, const FVector& Point)
{
	AWWOCharacter* P = GetPlayer();
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	const UWWOFloraData* Flora = Loadout ? Loadout->GetFlora() : nullptr;
	const FLinearColor Color = Flora && Flora->GraftColor.A > 0.f ? Flora->GraftColor : FLinearColor(0.25f, 0.55f, 1.f);
	P->Abilities->OnMeleeLanded(); // Melee Guard
	if (const UWWOAbilityData* Thornfist = P->Abilities->ThornfistActive())
	{
		if (Thornfist->DischargeArcs >= 1.f)
		{
			AWWOGameState::Discharge(GetWorld(), TargetActor, Point, (Point - P->EyeLocation()).GetSafeNormal(), Color,
				FMath::RoundToInt(Thornfist->DischargeArcs), Thornfist->DischargeRadius, Thornfist->DischargeTime, P);
		}
	}
	if (const UWWOAbilityData* StaticCharge = P->Abilities->ConsumeStaticCharge())
	{
		AWWOChainLightning::Spawn(GetWorld(), StaticCharge, TargetActor, P, Color);
		AWWOGameState::Sound(GetWorld(), "static_discharge", Point); // the deep crack of it going off, for everyone near
		AWWOGameState::Sound(GetWorld(), "static_discharge_boom", Point); // and a rolling strike under it, so it lands heavy
	}
}

void UWWOWeaponComponent::MeleeKnockBack(AActor* TargetActor, bool bKilled) const
{
	IWWOTargetable* Target = Cast<IWWOTargetable>(TargetActor);
	if (!Target) return;
	FVector Away = TargetActor->GetActorLocation() - GetPlayer()->GetActorLocation();
	Away.Z = 0.f;
	Target->KnockBack(Away.GetSafeNormal() * MeleeKnockback * (bKilled ? MeleeKillKnockback : 1.f) * WWO::M);
}
