#pragma once

// Data assets: one per Godot resource type (WeaponData, GrenadeData, AbilityData,
// FramePartData, FloraData, TraitData, and the enemy and ally tuning). Field names match the
// Godot ones in PascalCase (hip_position -> HipPosition), so Tools/create_assets.py can copy
// the values straight across. Units are meters / seconds / degrees, as in Godot.
// All of them are primary assets, found by the Asset Manager (see DefaultGame.ini).

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WWOTypes.h"
#include "Dom/JsonObject.h"
#include "WWOData.generated.h"

UCLASS(Abstract, BlueprintType)
class WHATWASOURS_API UWWOItemData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") FString DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item", meta = (MultiLine = true)) FString Description;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") float Order = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") bool bUnlocked = true;
};

// One weapon and every stat. Positions are in the Godot camera convention (meters,
// x right, y up, z back) because the viewmodel pose math is kept in that convention.
UCLASS(BlueprintType)
class WHATWASOURS_API UWWOWeaponData : public UWWOItemData
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId("WWOWeapon", GetFName()); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon") FString Archetype;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon") FVector HipPosition = FVector(0.15, -0.12, -0.4);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon") FVector AdsPosition = FVector(0.0, 0.0, -0.34);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ADS") float Zoom = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ADS") float AdsTime = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ADS") float AdsSensitivity = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ADS") float AdsMoveSpeed = 0.65f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ADS") float AdsVignette = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ADS") float ScopeOverlay = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float SemiAuto = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float FireRate = 600.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float Damage = 20.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float CritMultiplier = 1.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float FireRange = 120.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float Pellets = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float PelletSpread = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float HipSpread = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float RecoilUp = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float RecoilSide = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float RecoilKickTime = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float RecoilRecovery = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float RecoilRecoveryTime = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float RecoilRecoverySync = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float RecoilRecoveryCurve = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float RecoilRecoveryDelay = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float GunKick = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float MagazineSize = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float ReloadTime = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rumble") float RumbleStrong = 0.3f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rumble") float RumbleWeak = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rumble") float RumbleTime = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera punch") float PunchFov = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera punch") float PunchRoll = 0.3f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera punch") float PunchTime = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handling") float ViewmodelFov = 65.f;
	// How far (cm) the sight dot sits above where the gun model puts it: the gun and its mod parts
	// move down by this much, so aiming lines the dot up higher off the gun. Not from Godot.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handling") float SightHeight = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handling") float Sway = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handling") float GunBob = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handling") float SlideTilt = 11.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim assist") float AimAssistance = 70.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim assist") float MagnetismAngle = 2.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim assist") float CritMagnetism = 0.4f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim assist") float FrictionStrength = 0.45f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim assist") float FrictionAngle = 3.7f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim assist") float Adhesion = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim assist") float AssistRange = 40.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim assist") float AdsBoost = 1.4f;

	bool IsSemiAuto() const { return SemiAuto >= 0.5f; }
};

UCLASS(BlueprintType)
class WHATWASOURS_API UWWOGrenadeData : public UWWOItemData
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId("WWOGrenade", GetFName()); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade") FLinearColor Color = FLinearColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade") FName Flora = "voltvine";
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade") EWWOGrenadeKind Kind = EWWOGrenadeKind::Explosive;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blast") float Damage = 120.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blast") float BlastRadius = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blast") float ThrowDistanceMult = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blast") float RechargeMult = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blast") float FuseMult = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blast") float Bounce = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lingering pool") float PoolDps = 25.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lingering pool") float PoolTime = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Poison spike") float PoisonDps = 20.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Poison spike") float PoisonTime = 5.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Poison spike") float SpikeLife = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Poison spike") float SeekRadius = 7.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Poison spike") float SeekStrength = 160.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slowing tripmine") float ArmTime = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slowing tripmine") float TriggerRadius = 2.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slowing tripmine") float MineLife = 25.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slowing tripmine") float SlowAmount = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slowing tripmine") float SlowTime = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall") float WallWidth = 5.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall") float WallHeight = 2.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall") float WallTime = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blinding") float BlindTime = 3.f;
};

// One flora ability. The effect itself is picked by Id (see UWWOAbilityComponent).
UCLASS(BlueprintType)
class WHATWASOURS_API UWWOAbilityData : public UWWOItemData
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId("WWOAbility", GetFName()); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability") FLinearColor Color = FLinearColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability") EWWOAbilityKind Kind = EWWOAbilityKind::Passive;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability") FName Flora = "voltvine";

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overgrowth") float SpeedMult = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overgrowth") float ExtraJumps = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thornfist") float MeleeDamageMult = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thornfist") float Duration = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thornfist") float Cooldown = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thornfist") float DischargeArcs = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thornfist") float DischargeRadius = 1.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thornfist") float DischargeTime = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Charge") float ChargeDamage = 250.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Charge") float ChainDamage = 120.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Charge") float ChainJumps = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Charge") float ChainRange = 8.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Charge") float ChainFalloff = 0.85f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Charge") float ChainDelay = 0.07f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Seedburst") float SubCount = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Seedburst") float SubScale = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Seedburst") float SubSpread = 5.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Seedburst") float SubFuse = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spore Fog Damage") float FogDps = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spore Dash") float DashSpeed = 13.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spore Dash") float DashUp = 5.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spore Dash") float DashFogRadius = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spore Dash") float DashFogTime = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spore Patches") float PatchRadius = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spore Patches") float PatchTime = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spore Patches") float PatchMax = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Guard") float GuardReduction = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Root Bombs") float BombDamage = 150.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Root Bombs") float BombRadius = 3.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Root Bombs") float BombSpeed = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield Share") float ShieldAmount = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Extra Grenade") float ExtraCharges = 1.f;
};

// A weapon mod (draft): fits one slot on one frame (the weapon with Id == Frame). Changes stats
// by percentages (PercentAdd modifiers) and may carry a perk, picked by Perk (see
// UWWOWeaponComponent). Loaded from Config/WeaponMods.json, not an asset, while the design settles.
UCLASS(BlueprintType)
class WHATWASOURS_API UWWOModData : public UWWOItemData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mod") FName Frame = "rifle";
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mod") FName Slot; // barrel, magazine, stock, sight, chambering, grip
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mod") int32 Rarity = 1; // 1 to 5
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mod") TArray<FWWOStatModifier> Mods;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mod") FName Perk; // focus, scavenger, brace, hunter, static, adrenal
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mod") FString PerkText;

	FString Summary() const; // "+25% Range, +15% Side recoil" and the perk on its own line

	// How it looks on the gun: simple shapes at the slot's anchor (Config/WeaponMods.json "parts",
	// read by UWWOWeaponComponent::BuildModel).
	TArray<TSharedPtr<FJsonObject>> VisualParts;
};

// Frame parts give points in the stat groups (see UWWOLoadoutSubsystem::Groups).
UCLASS(BlueprintType)
class WHATWASOURS_API UWWOFramePartData : public UWWOItemData
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId("WWOFramePart", GetFName()); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frame part") FName Slot = "legs"; // head, arms, legs, core
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frame part") float Agility = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frame part") float Aerial = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frame part") float Dexterity = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frame part") float Durability = 0.f;

	float Points(FName Group) const;
	FName SpikeGroup() const; // the group this part gives the most points to, or None
	FLinearColor BodyColor(const FLinearColor& Base) const;
	FString Summary() const;

	static FLinearColor GroupColor(FName Group);
};

// A flora (a Destiny-style subclass).
UCLASS(BlueprintType)
class WHATWASOURS_API UWWOFloraData : public UWWOItemData
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId("WWOFlora", GetFName()); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flora") FLinearColor Color = FLinearColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flora") EWWOUltKind Ult = EWWOUltKind::Blast;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flora") FString UltName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flora") FLinearColor GraftColor = FLinearColor(0, 0, 0, 0);
};

// A flora trait: stat changes (Mods) and/or a named effect (Effect + Amount).
UCLASS(BlueprintType)
class WHATWASOURS_API UWWOTraitData : public UWWOItemData
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId("WWOTrait", GetFName()); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trait") FName Flora = "voltvine";
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trait") TArray<FWWOStatModifier> Mods;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trait") FName Effect; // fog_longer, more_bounce, poison_damage, ...
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trait") float Amount = 0.f;
};

// Tuning every enemy type has.
UCLASS(Abstract, BlueprintType)
class WHATWASOURS_API UWWOEnemyData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy") float AiEnabled = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy") float MaxHealth = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy") float WalkSpeed = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy") float SightRange = 35.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy") float TurnSpeed = 8.f;
};

UCLASS(BlueprintType)
class WHATWASOURS_API UWWOSoldierData : public UWWOEnemyData
{
	GENERATED_BODY()

public:
	UWWOSoldierData() { MaxHealth = 325.f; WalkSpeed = 4.f; SightRange = 35.f; TurnSpeed = 8.f; }
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId("WWOEnemy", GetFName()); }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soldier") float MinRange = 15.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soldier") float MaxRange = 25.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soldier") float StrafeTime = 1.8f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float WindupTime = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float BurstCount = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float BurstInterval = 0.09f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float BurstCooldown = 1.4f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float GroupSpacing = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float ProjectileDamage = 10.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float ProjectileSpeed = 53.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting") float Spread = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") float CoverHealth = 0.35f;
};

UCLASS(BlueprintType)
class WHATWASOURS_API UWWOSwarmData : public UWWOEnemyData
{
	GENERATED_BODY()

public:
	UWWOSwarmData() { MaxHealth = 75.f; WalkSpeed = 7.5f; SightRange = 30.f; TurnSpeed = 12.f; }
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId("WWOEnemy", GetFName()); }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm") float LeapRange = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm") float LeapSpeed = 11.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm") float LeapHeight = 1.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm") float LeapDamage = 15.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm") float LeapCooldown = 2.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm") float BiteRange = 1.4f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm") float BiteDamage = 10.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm") float BiteInterval = 0.8f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swarm") float SpreadDistance = 1.6f;
};

UCLASS(BlueprintType)
class WHATWASOURS_API UWWOTankData : public UWWOEnemyData
{
	GENERATED_BODY()

public:
	UWWOTankData() { MaxHealth = 1200.f; WalkSpeed = 3.f; SightRange = 40.f; TurnSpeed = 1.5f; }
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId("WWOEnemy", GetFName()); }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield") float Shield = 300.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield") float ShieldRegenDelay = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield") float ShieldRegenRate = 75.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slam") float SlamRange = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slam") float SlamRadius = 4.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slam") float SlamDamage = 40.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slam") float SlamWindup = 0.7f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slam") float SlamRecover = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slam") float SlamCooldown = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slam") float SlowAmount = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slam") float SlowTime = 2.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobbed shot") float LobDamage = 35.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobbed shot") float LobRadius = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobbed shot") float LobInterval = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobbed shot") float LobFlightTime = 1.2f;
};

UCLASS(BlueprintType)
class WHATWASOURS_API UWWOAllyData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId("WWOAlly", GetFName()); }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally") float AiEnabled = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally") float MaxHealth = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally") float MaxShield = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally") float RechargeDelay = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally") float ShieldRegen = 40.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally") float HealthRegen = 25.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally") float MoveSpeed = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally") float FollowMin = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally") float FollowMax = 7.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally") float Damage = 10.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally") float FireInterval = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally") float Spread = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally") float SightRange = 35.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally") float RespawnTime = 8.f;
};
