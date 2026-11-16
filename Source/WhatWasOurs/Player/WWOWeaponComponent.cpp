// The weapon component (see the header): construction, the per-frame driver (UpdateWeapon), equip and
// swap, reload and ADS state, ammo and input. The rest of the class is split by concern:
// WWOWeaponFiring (the shot), WWOWeaponPerks (stats and perks), WWOWeaponMelee, WWOWeaponModel
// (the first- and third-person gun models), WWOWeaponView (pose, FOV, punch, rumble),
// WWOWeaponHands (the first-person arms) and WWOWeaponTargeting (reticle target, marks).

#include "Player/WWOWeaponComponent.h"
#include "WhatWasOurs.h"
#include "Player/WWOWeaponPrivate.h"
#include "Player/WWOCharacter.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Engine/World.h"
#include "Systems/WWOSound.h"
#include "Player/WWOBotController.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOPlayerController.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Components/StaticMeshComponent.h"

using namespace WWOWeapon; // the helpers these files share (WWOWeaponPrivate.h)

UWWOWeaponComponent::UWWOWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // the character updates it each frame
}

AWWOCharacter* UWWOWeaponComponent::GetPlayer() const
{
	return Cast<AWWOCharacter>(GetOwner());
}

float UWWOWeaponComponent::M(FName Stat, float Base) const
{
	const AWWOCharacter* P = GetPlayer();
	return P ? P->M(Stat, Base) : Base;
}

bool UWWOWeaponComponent::Pressed(int32 Action) const
{
	if (const AWWOBotController* Bot = GetPlayer() ? GetPlayer()->GetBot() : nullptr) return Bot->IsHeld((EWWOAction)Action);
	const AWWOPlayerController* PC = GetPlayer() ? GetPlayer()->GetWWOController() : nullptr;
	return PC && PC->IsHeld((EWWOAction)Action);
}

bool UWWOWeaponComponent::JustPressed(int32 Action) const
{
	if (const AWWOBotController* Bot = GetPlayer() ? GetPlayer()->GetBot() : nullptr) return Bot->WasJustPressed((EWWOAction)Action);
	const AWWOPlayerController* PC = GetPlayer() ? GetPlayer()->GetWWOController() : nullptr;
	return PC && PC->WasJustPressed((EWWOAction)Action);
}

void UWWOWeaponComponent::BeginPlay()
{
	Super::BeginPlay();
	ModelRoot = NewObject<USceneComponent>(GetOwner(), TEXT("WeaponModel"));
	ModelRoot->SetupAttachment(this);
	ModelRoot->RegisterComponent();
	BuildThrowBall();
	RebindLoadout();
}

void UWWOWeaponComponent::RebindLoadout()
{
	// Follow the loadout this character uses now (a bot gets its own after it spawns).
	if (BoundLoadout.IsValid()) BoundLoadout->OnChanged.Remove(LoadoutHandle);
	BoundLoadout = UWWOLoadoutSubsystem::Get(this);
	if (UWWOLoadoutSubsystem* Loadout = BoundLoadout.Get())
	{
		Equip(Loadout->GetWeapon());
		LoadoutHandle = Loadout->OnChanged.AddWeakLambda(this, [this, Loadout]()
		{
			if (Loadout->GetWeapon() != BaseWeapon) Equip(Loadout->GetWeapon());
			else
			{
				// New mods: a fresh, full magazine (it may have grown), like re-equipping.
				FString Signature;
				for (const FName& Slot : UWWOLoadoutSubsystem::ModSlots())
				{
					if (const UWWOModData* Mod = Loadout->GetMod(Slot)) Signature += Mod->Id.ToString() + TEXT(",");
				}
				if (Signature != ModSignature)
				{
					// Other mods on the same gun (your other slot's config of it, or a change in the menu):
					// a full magazine stays full at its new size, a part-used one keeps its rounds, perks
					// keep their state, and it takes a swap like any other change of weapon.
					const bool bWasFull = Ammo >= MagazineSize();
					ModSignature = Signature;
					RefreshStats();
					BuildModel(); // the mods' parts
					Ammo = bWasFull ? MagazineSize() : FMath::Min(Ammo, MagazineSize());
					HitStreak = StaticHits = 0;
					BurstLeft = 0;
					bFullBurst = bBurstBoosted = false;
					CancelReload();
					SwapLength = FMath::Max(M("swap_time", SwapTime), 0.05f);
					SwapLeft = SwapLength;
				}
			}
		});
	}
}

void UWWOWeaponComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (BoundLoadout.IsValid()) BoundLoadout->OnChanged.Remove(LoadoutHandle);
	Super::EndPlay(Reason);
}

void UWWOWeaponComponent::SetFirstPersonHidden(bool bHidden)
{
	const bool bWas = bFirstPersonHidden;
	bFirstPersonHidden = bHidden;
	if (bWas != bHidden && Weapon && ModelRoot) BuildModel(); // built only while shown
	if (ModelRoot) ModelRoot->SetVisibility(!bHidden, true);
	if (ThrowBall) ThrowBall->SetVisibility(!bHidden); // UpdatePose sets it again each frame
}

int32 UWWOWeaponComponent::MagazineSize() const
{
	return Weapon ? FMath::Max(1, (int32)Weapon->MagazineSize) : 1;
}

void UWWOWeaponComponent::Equip(UWWOWeaponData* NewWeapon)
{
	// Swap in the weapon's model and start it with a full magazine.
	if (!NewWeapon) return;
	StoreGunState(); // the gun you're putting away keeps its rounds
	BaseWeapon = NewWeapon;
	Weapon = DuplicateObject<UWWOWeaponData>(NewWeapon, this);
	HitStreak = StaticHits = 0;
	bOverpressure = true;
	AimHeld = 0.f;
	SinceShot = 100.f;
	bMeleeLanded = false;
	RefundRounds = 0;
	BurstLeft = BurstSize = BurstHits = 0;
	bFullBurst = bBurstBoosted = false;
	ModSignature.Reset();
	if (const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this))
	{
		for (const FName& Slot : UWWOLoadoutSubsystem::ModSlots())
		{
			if (const UWWOModData* Mod = L->GetMod(Slot)) ModSignature += Mod->Id.ToString() + TEXT(",");
		}
	}
	RefreshStats();
	BuildModel();
	Ammo = MagazineSize();
	// Back to a gun you carried this life: as you left it (a first pickup is full, perks ready).
	if (const FGunState* Kept = GunStates.Find(NewWeapon->Id))
	{
		Ammo = FMath::Clamp(Kept->Ammo, 0, MagazineSize());
		bOverpressure = Kept->bOverpressure;
		SinceShot = GetWorld() ? float(GetWorld()->GetTimeSeconds() - Kept->LastShot) : 100.f;
	}
	CancelReload();
	AdsProgress = 0.f;
	FireCooldown = 0.f;
	SwapLength = FMath::Max(M("swap_time", SwapTime), 0.05f);
	SwapLeft = SwapLength;
	UWWOSound::PlayFor(GetOwner(), "weapon_swap");
}

void UWWOWeaponComponent::StoreGunState()
{
	if (!BaseWeapon || !GetWorld()) return;
	FGunState& State = GunStates.FindOrAdd(BaseWeapon->Id);
	State.Ammo = Ammo;
	State.bOverpressure = bOverpressure;
	State.LastShot = GetWorld()->GetTimeSeconds() - SinceShot;
}

void UWWOWeaponComponent::Interrupt()
{
	BurstLeft = 0;
	bBurstBoosted = false;
	bReadyShotQueued = false;
	CancelReload();
	bMeleeing = false;
	MeleeProgress = 0.f;
	bMeleeLanded = false;
	MeleeTarget = nullptr;
}

void UWWOWeaponComponent::ResetForRespawn()
{
	Interrupt();
	GunStates.Reset();
	HitStreak = StaticHits = 0;
	AdrenalLeft = HammerLeft = 0.f;
	bFullBurst = false;
	bOverpressure = true;
	SinceShot = 100.f;
	AimHeld = 0.f;
	FireCooldown = 0.f;
	if (Weapon) Ammo = MagazineSize();
}

bool UWWOWeaponComponent::IsSwapping() const
{
	return SwapLeft > SwapLength * 0.4f; // can fire once the gun is mostly back up
}

bool UWWOWeaponComponent::IsAiming() const
{
	return Pressed((int32)EWWOAction::Aim) && !bReloading && !bMeleeing && !IsSwapping();
}

float UWWOWeaponComponent::LookMultiplier() const
{
	return Weapon ? FMath::Lerp(1.f, Weapon->AdsSensitivity / Weapon->Zoom, AdsAmount) : 1.f;
}

float UWWOWeaponComponent::MoveMultiplier() const
{
	return Weapon ? FMath::Lerp(1.f, Weapon->AdsMoveSpeed, AdsAmount) : 1.f;
}

bool UWWOWeaponComponent::CastRayTo(const FVector& To, FHitResult& OutHit) const
{
	const AWWOCharacter* P = GetPlayer();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOShot), false, P);
	return GetWorld()->LineTraceSingleByChannel(OutHit, P->EyeLocation(), To, ECC_Visibility, Params);
}

void UWWOWeaponComponent::StartReload()
{
	if (bReloading || Ammo >= MagazineSize()) return;
	bReloading = true;
	ReloadProgress = 0.f;
	GetPlayer()->MoveState.bSprinting = false; // reloading stops a sprint
	UWWOSound::PlayFor(GetOwner(), "reload_start");
}

void UWWOWeaponComponent::CancelReload()
{
	bReloading = false;
	ReloadProgress = 0.f;
}

void UWWOWeaponComponent::UpdateWeapon(float DeltaSeconds)
{
	if (!Weapon) return;
	AdrenalLeft = FMath::Max(AdrenalLeft - DeltaSeconds, 0.f);
	HammerLeft = FMath::Max(HammerLeft - DeltaSeconds, 0.f);
	SinceShot += DeltaSeconds;
	RefreshStats();
	if (!FMath::IsNearlyEqual(Weapon->SightHeight, BuiltSightHeight)) BuildModel(); // the slider moved
	UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	// Swapping weapons goes to your other weapon slot's saved config. With nothing in that slot it
	// steps through every weapon, as before there were slots.
	if (Loadout && JustPressed((int32)EWWOAction::WeaponNext)) { if (!Loadout->SwapWeaponSlot()) Loadout->CycleWeapon(1); }
	else if (Loadout && JustPressed((int32)EWWOAction::WeaponPrev)) { if (!Loadout->SwapWeaponSlot()) Loadout->CycleWeapon(-1); }
	ConfigToastLeft = FMath::Max(ConfigToastLeft - DeltaSeconds, 0.f);
	if (Loadout && JustPressed((int32)EWWOAction::NextConfig))
	{
		// You carry two saved configs, one per weapon slot: this goes to the other one and names it
		// (the same swap as the weapon swap key, which says nothing).
		const bool bSwapped = Loadout->SwapWeaponSlot();
		const int32 Index = Loadout->GetSlotConfig(Loadout->GetActiveSlot());
		ConfigToast = bSwapped && Index != INDEX_NONE ? Loadout->GetConfigs()[Index].Name + TEXT(": ") + UWWOLoadoutSubsystem::ConfigSummary(Loadout->GetConfigs()[Index], Loadout)
			: FString(TEXT("Your other weapon slot is empty (pick a config for it in the character menu)"));
		ConfigToastLeft = 2.5f;
	}
	UpdateMelee(DeltaSeconds);
	UpdateReload(DeltaSeconds);
	UpdateAds(DeltaSeconds);
	UpdateFiring(DeltaSeconds);
	PunchAge += DeltaSeconds;
	UpdateFov();
	UpdateReticleTarget();
	UpdatePose(DeltaSeconds);
}

void UWWOWeaponComponent::UpdateReload(float DeltaSeconds)
{
	Ammo = FMath::Min(Ammo, MagazineSize()); // in case the slider shrank the magazine
	// Square is Revive too: next to a downed teammate it revives instead.
	if (JustPressed((int32)EWWOAction::Reload) && !GetPlayer()->Health->FindReviveTarget()) StartReload();
	if (bReloading && GetPlayer()->MoveState.bSprinting) CancelReload(); // sprinting cancels a reload, like Destiny
	if (!bReloading) return;
	const float Adrenal = AdrenalLeft > 0.f ? 1.4f : 1.f; // Adrenal: reloads 40% faster after a kill
	ReloadProgress += DeltaSeconds * Adrenal / FMath::Max(M("reload_time", Weapon->ReloadTime), 0.05f);
	if (ReloadProgress >= ReloadSeated && Ammo < MagazineSize())
	{
		Ammo = MagazineSize(); // the new magazine is in
		UWWOSound::PlayFor(GetOwner(), "reload_done");
		bOverpressure = true;
	}
	if (ReloadProgress >= 1.f) CancelReload();
}

void UWWOWeaponComponent::UpdateAds(float DeltaSeconds)
{
	const float Target = IsAiming() ? 1.f : 0.f;
	AdsProgress = WWO::MoveToward(AdsProgress, Target, DeltaSeconds / FMath::Max(M("ads_time", Weapon->AdsTime), 0.01f));
	AdsAmount = WWO::SmoothStep(0.f, 1.f, AdsProgress);
	AimHeld = AdsProgress >= 1.f ? AimHeld + DeltaSeconds : 0.f; // Patience
}
