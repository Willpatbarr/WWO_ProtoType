// UWWOWeaponComponent: the trigger, bursts and the shot (UpdateFiring -> FireRound -> Shoot), and where
// your own shot's trail starts.

#include "Player/WWOWeaponComponent.h"
#include "WhatWasOurs.h"
#include "Player/WWOWeaponPrivate.h"
#include "Player/WWOCharacter.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Engine/World.h"
#include "Systems/WWOSound.h"
#include "Player/WWOAimAssistComponent.h"
#include "FX/WWOFX.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"

using namespace WWOWeapon; // the helpers these files share (WWOWeaponPrivate.h)

namespace
{
	constexpr float MuzzleFlashTime = 0.05f;
	const FLinearColor ShotColor(1.f, 0.8f, 0.45f); // trails and the flash's light
}

FVector UWWOWeaponComponent::TrailStart() const
{
	// Where a trail has to start in the world to look like it leaves the first-person gun's muzzle:
	// the gun draws with its own field of view, so its muzzle isn't where it appears to be.
	const AWWOCharacter* P = GetPlayer();
	const UCameraComponent* Camera = P->Camera;
	const FTransform View = Camera->GetComponentTransform();
	constexpr float Depth = 120.f; // cm in front of the eye
	const UStaticMeshComponent* Core = Part(FlashCore);
	const FVector Seen = Core ? View.InverseTransformPositionNoScale(Core->GetComponentLocation()) : FVector::ZeroVector;
	if (Seen.X < 1.f) return View.TransformPositionNoScale(FVector(Depth, 12.f, -10.f));
	const float Ratio = FMath::Tan(FMath::DegreesToRadians(Camera->FieldOfView) * 0.5f)
		/ FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(Camera->FirstPersonFieldOfView, 1.f, 170.f)) * 0.5f);
	return View.TransformPositionNoScale(FVector(Depth, Seen.Y / Seen.X * Ratio * Depth, Seen.Z / Seen.X * Ratio * Depth));
}

bool UWWOWeaponComponent::WantsToFire() const
{
	// Semi-auto weapons need a fresh trigger pull for every shot.
	return Weapon->IsSemiAuto() ? JustPressed((int32)EWWOAction::Fire) : Pressed((int32)EWWOAction::Fire);
}

void UWWOWeaponComponent::UpdateFiring(float DeltaSeconds)
{
	AWWOCharacter* P = GetPlayer();
	// The gun model's kick: full at the shot, held, then eased back (UWWOWeaponData::KickHold / KickReturn).
	KickAge += DeltaSeconds;
	{
		const float Back = FMath::Clamp((KickAge - FMath::Max(Weapon->KickHold, 0.f)) / FMath::Max(Weapon->KickReturn, 0.01f), 0.f, 1.f);
		Kick = 1.f - Back * Back * (3.f - 2.f * Back); // slow off the top, slow onto rest
	}
	FlashTime -= DeltaSeconds;
	bFlashVisible = FlashTime > 0.f; // decided before this frame's shot, like Godot (a new flash shows from the next frame)

	FireCooldown -= DeltaSeconds;
	// A semi-auto pull is remembered for a moment: one that comes just before the gun can fire
	// again (or during a burst) fires as soon as it can, instead of being lost.
	if (Weapon->IsSemiAuto() && JustPressed((int32)EWWOAction::Fire)) FireBuffer = FMath::Max(P->FireBufferTime, 0.f);
	else FireBuffer = FMath::Max(FireBuffer - DeltaSeconds, 0.f);
	// A burst under way finishes on its own, trigger held or not (a reload, a punch or a swap cuts
	// it short). FireCooldown keeps counting meanwhile: it times burst start to burst start.
	if (BurstLeft > 0)
	{
		if (bReloading || bMeleeing || IsSwapping())
		{
			EndBurst();
			return;
		}
		BurstCooldown -= DeltaSeconds;
		while (BurstLeft > 0 && BurstCooldown <= 0.f)
		{
			if (Ammo <= 0) break;
			FireRound();
			--BurstLeft;
			BurstCooldown += 60.f / FMath::Max(Weapon->BurstRate, 1.f);
		}
		if (BurstLeft == 0 || Ammo <= 0) EndBurst();
		if (Ammo <= 0) StartReload();
		return;
	}
	// Handling: the gun needs a moment to come up after a sprint or mantle. A trigger pull
	// meanwhile fires as soon as it's up.
	if (P->MoveState.bSprinting || P->IsMantling()) ReadyLeft = M("ready_time", ReadyTime);
	if (ReadyLeft > 0.f)
	{
		if (!(P->MoveState.bSprinting || P->IsMantling())) ReadyLeft -= DeltaSeconds;
		if (Weapon->IsSemiAuto() && JustPressed((int32)EWWOAction::Fire)) bReadyShotQueued = true;
		FireCooldown = FMath::Max(FireCooldown, 0.f); // don't bank shots while waiting
		return;
	}
	const bool bWants = WantsToFire() || ((bReadyShotQueued || FireBuffer > 0.f) && Weapon->IsSemiAuto());
	bReadyShotQueued = false;
	if (!bWants || bReloading || bMeleeing || IsSwapping())
	{
		FireCooldown = FMath::Max(FireCooldown, 0.f);
		return;
	}
	if (Weapon->IsSemiAuto() && FireCooldown > 0.f) return; // pulled the trigger before the gun was ready
	while (FireCooldown <= 0.f)
	{
		if (Ammo <= 0)
		{
			StartReload(); // pulling the trigger on empty reloads
			return;
		}
		const int32 Rounds = Weapon->BurstCount();
		FireBuffer = 0.f; // the remembered pull is spent
		if (Rounds > 1) BeginBurst(Rounds);
		FireRound();
		FireCooldown += 60.f / FMath::Max(Weapon->FireRate, 1.f);
		if (Rounds > 1)
		{
			// The rest of the burst follows at the burst's own rate (see the top of this function).
			BurstLeft = Rounds - 1;
			BurstCooldown = 60.f / FMath::Max(Weapon->BurstRate, 1.f);
			FireCooldown = FMath::Max(FireCooldown, 0.f); // one burst per frame, however long the frame
			break;
		}
		if (Weapon->IsSemiAuto()) break;
	}
	if (Ammo <= 0)
	{
		if (BurstLeft > 0) EndBurst();
		StartReload(); // ran dry: reload automatically
	}
}

void UWWOWeaponComponent::FireRound()
{
	Shoot();
	Ammo -= 1;
	if (PerkState.RefundRounds > 0) Ammo = FMath::Min(Ammo + PerkState.RefundRounds, MagazineSize()); // Marksman: a crit's round comes back
	PerkState.RefundRounds = 0;
}

void UWWOWeaponComponent::BeginBurst(int32 Rounds)
{
	BurstSize = Rounds;
	PerkState.BurstHits = 0;
	PerkState.bBurstBoosted = PerkState.bFullBurst; // Full Burst: earned by the last burst, spent on this one
	PerkState.bFullBurst = false;
}

void UWWOWeaponComponent::EndBurst()
{
	// The perks that care whether every round of a finished burst hit (Full Burst).
	const bool bEveryRoundHit = BurstLeft == 0 && BurstSize > 1 && PerkState.BurstHits >= BurstSize;
	PerkState.bFullBurst = false;
	if (const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this))
	{
		for (const FWWOPerk* Perk : WWOPerk::Equipped(*L)) if (Perk->OnBurstEnd) Perk->OnBurstEnd(PerkState, bEveryRoundHit);
	}
	BurstLeft = 0;
	PerkState.bBurstBoosted = false;
}

void UWWOWeaponComponent::Shoot()
{
	AWWOCharacter* P = GetPlayer();
	Kick = 1.f;
	KickAge = 0.f;
	FlashTime = MuzzleFlashTime;
	const int32 PelletCount = FMath::Max(1, FMath::RoundToInt(Weapon->Pellets));
	// Hip-fire spread shrinks to nothing as you aim in; pellets scatter around that aim.
	const FVector Aim = WWO::RandomInCone(P->AimForward(), FMath::DegreesToRadians(Weapon->HipSpread) * (1.f - AdsAmount));
	struct FTotal { float Damage = 0.f; bool bCritical = false; FVector Point; };
	TMap<AActor*, FTotal> Totals; // pellets add up into one hit per target
	TArray<FVector_NetQuantize> Ends; // every pellet's impact, for the other players' screens
	// Perks that power up just this shot, and the ones that depend on what each pellet hits.
	float ShotScale = 1.f, CritBonus = 0.f;
	FWWOPerkList TargetPerks;
	if (const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this))
	{
		const FWWOPerkView View = PerkView();
		for (const FWWOPerk* Perk : WWOPerk::Equipped(*L))
		{
			if (Perk->ShotOn && Perk->ShotOn(View))
			{
				ShotScale *= Perk->ShotScale;
				CritBonus += Perk->ShotCrit;
			}
			if (Perk->TargetOn) TargetPerks.Add(Perk);
		}
	}
	PerkState.SinceShot = 0.f;
	for (int32 i = 0; i < PelletCount; ++i)
	{
		const FVector Dir = PelletCount > 1 ? WWO::RandomInCone(Aim, FMath::DegreesToRadians(Weapon->PelletSpread)) : Aim;
		FHitResult Hit;
		bool bHit = CastRayTo(P->EyeLocation() + Dir * Weapon->FireRange * WWO::M, Hit);
		if (PelletCount == 1 && (!bHit || !Cast<IWWOTargetable>(Hit.GetActor())))
		{
			FHitResult Magnetized;
			if (P->AimAssist->Magnetize(Magnetized)) // near-miss? bend it onto the target
			{
				Hit = Magnetized;
				bHit = true;
			}
		}
		if (!bHit)
		{
			Ends.Add(P->EyeLocation() + Dir * Weapon->FireRange * WWO::M); // a miss still flies through the air
			continue;
		}
		AWWOFXActor::ImpactMark(GetWorld(), Hit.ImpactPoint);
		Ends.Add(Hit.ImpactPoint);
		IWWOTargetable* Target = Cast<IWWOTargetable>(Hit.GetActor());
		if (!Target || Target->IsDead() || !UWWOWorldSubsystem::CanHurt(P, Hit.GetActor())) continue; // PvP: not your team
		const bool bCritical = Target->IsCriticalHit(Hit, P->EyeLocation());
		if (P->IsLocalHuman()) AWWOFXActor::HitSpark(GetWorld(), Hit.ImpactPoint, -Dir, bCritical); // a bot's: ShowShotFromBody
		FTotal& Entry = Totals.FindOrAdd(Hit.GetActor());
		if (Entry.Damage == 0.f) Entry.Point = Hit.ImpactPoint;
		// Long Shot: far targets; Point Blank: close ones; Finisher: ones under half their health and shield.
		float TargetScale = 1.f;
		const float HitDist = FVector::Dist(P->EyeLocation(), Hit.ImpactPoint) / WWO::M;
		for (const FWWOPerk* Perk : TargetPerks) if (Perk->TargetOn(HitDist, *Target)) TargetScale *= Perk->TargetScale;
		Entry.Damage += Weapon->Damage * ShotScale * TargetScale * (bCritical ? Weapon->CritMultiplier + CritBonus : 1.f);
		Entry.bCritical |= bCritical;
	}
	// The shot through the air, on this machine. Yours leaves from where your gun's muzzle shows on
	// screen (thinner, so it doesn't cover what you're aiming at) with the flash on the gun itself;
	// a bot's is drawn from its body like any other player's.
	if (P->IsLocalHuman())
	{
		UWWOSound::PlayFlat(this, UWWOSound::HasEvent(this, FName(*(TEXT("fire_") + Weapon->Id.ToString()))) ? FName(*(TEXT("fire_") + Weapon->Id.ToString())) : FName("fire"));
		const FVector Muzzle = TrailStart();
		for (const FVector_NetQuantize& End : Ends) AWWOFXActor::BulletTrail(GetWorld(), Muzzle, End, ShotColor, 0.6f);
		// No two flashes alike: the cross rolls and every flare changes size a little.
		const float Roll = FMath::FRandRange(0.f, 360.f);
		for (const FName& Name : { FlashTongue, FlashCrossA, FlashCrossB })
		{
			if (UStaticMeshComponent* Flare = Part(Name))
			{
				if (!FlareSizes.Contains(Name)) FlareSizes.Add(Name, Flare->GetRelativeScale3D());
				Flare->SetRelativeRotation(FRotator(0.f, 0.f, Roll));
				Flare->SetRelativeScale3D(FlareSizes[Name] * FMath::FRandRange(0.75f, 1.25f));
			}
		}
		if (AWWOFXActor* Light = AWWOFXActor::Spawn(GetWorld(), P->EyeLocation() + P->AimForward() * 80.f, 0.07f))
		{
			Light->LightTween(Light->AddLight(ShotColor, 7000.f, 500.f), 7000.f, 0.f, 0.07f, EWWOEase::QuadIn);
		}
	}
	else P->ShowShotFromBody(P->EyeLocation(), Ends, false);

	bool bAnyCritical = false, bAnyKilled = false;
	int32 Kills = 0;
	AActor* FirstHit = nullptr;
	// Online, a joiner's hits are shown here at once (TakeHit only flashes and numbers on a joiner)
	// and sent to the host, which checks and applies them; a kill is predicted from the health the
	// host last sent. The host's own shot applies directly. Either way the others see it.
	const ENetMode Mode = GetWorld()->GetNetMode();
	const bool bJoiner = Mode == NM_Client;
	FWWODamageBy By(P); // the host's own hits are credited to it (PvP)
	FWWOGunShot NetShot;
	NetShot.WeaponId = Weapon->Id;
	if (const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this))
	{
		for (const FName& Slot : UWWOLoadoutSubsystem::ModSlots())
		{
			if (const UWWOModData* Mod = L->GetMod(Slot)) NetShot.Mods.Add(Mod->Id);
		}
	}
	NetShot.Origin = P->EyeLocation();
	NetShot.Ends = Ends;
	{
		TGuardValue<bool> Seen(UWWOWorldSubsystem::bAttackerSawHit, true); // this shot marks its hits once, below
		for (const auto& Pair : Totals)
		{
			if (IWWOTargetable* Target = Cast<IWWOTargetable>(Pair.Key))
			{
				const float Before = Target->GetHealth() + Target->GetShield();
				bool bKilled = Target->TakeHit(Pair.Value.Damage, Pair.Value.bCritical, Pair.Value.Point);
				if (bJoiner)
				{
					bKilled = Before > 0.f && Before - Pair.Value.Damage <= 0.f;
					NetShot.Hits.Add({ Pair.Key, Pair.Value.Damage, Pair.Value.bCritical, Pair.Value.Point });
				}
				bAnyKilled |= bKilled;
				Kills += bKilled ? 1 : 0;
				bAnyCritical |= Pair.Value.bCritical;
				if (!FirstHit) FirstHit = Pair.Key;
			}
		}
	}
	if (bJoiner) P->ServerGunShot(NetShot);
	else if (Mode == NM_ListenServer) P->MulticastGunShot(NetShot.Origin, NetShot.Ends);
	// A joiner's kill perks wait for the host's word (OnKillsConfirmed); its kill marker doesn't.
	bLastShotPredictedKill = bAnyKilled;
	OnShotResult(FirstHit, Totals.Num(), bJoiner ? 0 : Kills, bAnyCritical);
	if (Totals.Num() > 0) OnHitConfirmed.Broadcast(bAnyCritical, bAnyKilled);
	if (Totals.Num() > 0 && P->IsLocalHuman()) UWWOSound::PlayFlat(this, bAnyKilled ? "kill" : (bAnyCritical ? "hit_crit" : "hit"));
	// Recoil kicks the actual view, so you have to pull down to stay on target.
	const bool bSynced = Weapon->RecoilRecoverySync >= 0.5f;
	float RecoveryTime = Weapon->RecoilRecoveryTime;
	if (bSynced) RecoveryTime = 60.f / FMath::Max(Weapon->FireRate, 1.f) - Weapon->RecoilKickTime + Weapon->RecoilRecoveryDelay;
	const bool bShaped = Weapon->RecoilShape >= 0.5f; // the measured rise and fall, over the rise and fall-back times
	if (bShaped) RecoveryTime = Weapon->RecoilRecoveryTime;
	const float Share = BurstLeft > 0 ? FMath::Max(Weapon->BurstRecoilLater, 0.f) : 1.f; // (BurstLeft is only set once a burst's first round is away)
	P->AddRecoil(FMath::DegreesToRadians(Weapon->RecoilUp * Share), FMath::DegreesToRadians(WWO::FRandRange(-Weapon->RecoilSide, Weapon->RecoilSide) * Share),
		Weapon->RecoilKickTime, Weapon->RecoilRecovery, RecoveryTime, bShaped ? -1.f : (bSynced ? Weapon->RecoilRecoveryCurve : 0.f));
	Rumble(Weapon->RumbleWeak, Weapon->RumbleStrong, Weapon->RumbleTime);
	StartPunch(Weapon->PunchFov, Weapon->PunchRoll * (WWO::FRand() < 0.5f ? 1.f : -1.f), Weapon->PunchTime);
}
