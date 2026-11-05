#pragma once

// The equipped weapon (Godot: gun.gd). Modeled on Destiny 2:
// - Zoom is a magnification (it divides tan(half FOV)), FOV is horizontal.
// - The gun draws with its own fixed FOV (Unreal's First Person Rendering), so it doesn't
//   grow when you zoom and never clips into walls.
// - Hitscan shooting with pellets and spread, semi/auto fire, kick-and-recover recoil,
//   reload animation, melee with lunge, grenade throw arm, and every gun pose (sprint,
//   slide, air lag, mantle, throw, swap, reload, melee).
// The pose math keeps the Godot camera convention (meters, x right, y up, z back) and is
// converted to Unreal space once per frame (see ToUnreal).

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "WWOWeaponComponent.generated.h"

class AWWOCharacter;
class UWWOWeaponData;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
struct FHitResult;

DECLARE_MULTICAST_DELEGATE_TwoParams(FWWOHitConfirmed, bool /*bCritical*/, bool /*bKilled*/);

// A gray-box first-person arm (the punching right arm or the throwing left arm).
USTRUCT()
struct FWWOFPArm
{
	GENERATED_BODY()

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Upper;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Fore;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Fist;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Ball; // the grenade in the throwing hand
	bool bLeft = false;
};

UCLASS(ClassGroup = (WWO), meta = (BlueprintSpawnableComponent))
class WHATWASOURS_API UWWOWeaponComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWWOWeaponComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void UpdateWeapon(float DeltaSeconds); // called by the character each frame

	FWWOHitConfirmed OnHitConfirmed;

	// --- Global values (Settings menu) ---
	UPROPERTY(EditAnywhere, Category = "Camera") float HipFov = 100.f;
	UPROPERTY(EditAnywhere, Category = "Camera") float RumbleScale = 1.f;
	UPROPERTY(EditAnywhere, Category = "Camera") float PunchScale = 1.f;

	UPROPERTY(EditAnywhere, Category = "Melee") float MeleeTime = 0.45f;
	UPROPERTY(EditAnywhere, Category = "Melee") float MeleeReach = 2.5f;
	UPROPERTY(EditAnywhere, Category = "Melee") float MeleeDamage = 80.f;
	UPROPERTY(EditAnywhere, Category = "Melee") float LungeRange = 3.7f;
	UPROPERTY(EditAnywhere, Category = "Melee") float LungeAngle = 20.f;
	UPROPERTY(EditAnywhere, Category = "Melee") float LungeSpeed = 12.f;
	UPROPERTY(EditAnywhere, Category = "Melee") float LungeVertical = 1.f;
	UPROPERTY(EditAnywhere, Category = "Melee") float LungeAim = 0.5f;
	UPROPERTY(EditAnywhere, Category = "Melee") float MeleeHitstop = 0.015f;
	UPROPERTY(EditAnywhere, Category = "Melee") float MeleeJolt = 2.f;
	UPROPERTY(EditAnywhere, Category = "Melee") float MeleePunchFov = -4.f;
	UPROPERTY(EditAnywhere, Category = "Melee") float MeleePunchRoll = 2.5f;
	UPROPERTY(EditAnywhere, Category = "Melee") float MeleePunchTime = 0.25f;
	UPROPERTY(EditAnywhere, Category = "Melee") float MeleeRumbleStrong = 0.75f;
	UPROPERTY(EditAnywhere, Category = "Melee") float MeleeRumbleWeak = 0.7f;
	UPROPERTY(EditAnywhere, Category = "Melee") float MeleeRumbleTime = 0.11f;
	UPROPERTY(EditAnywhere, Category = "Melee") float MeleeKnockback = 6.f;
	UPROPERTY(EditAnywhere, Category = "Melee") float MeleeKillBoost = 1.4f;
	UPROPERTY(EditAnywhere, Category = "Melee") float MeleeKillKnockback = 2.f;
	UPROPERTY(EditAnywhere, Category = "Melee") float MeleeArmYaw = 16.f;
	UPROPERTY(EditAnywhere, Category = "Melee") float MeleeArmPitch = 8.f;
	UPROPERTY(EditAnywhere, Category = "Melee") float MeleeArmPush = 0.05f;
	UPROPERTY(EditAnywhere, Category = "Melee") float MeleeArmFov = 90.f;
	UPROPERTY(EditAnywhere, Category = "Melee") float MeleeArmHold = 0.01f;
	UPROPERTY(EditAnywhere, Category = "Melee") float MeleeArmRetract = 0.2f;

	UPROPERTY(EditAnywhere, Category = "Throw") float ThrowRelease = 0.22f;
	UPROPERTY(EditAnywhere, Category = "Throw") float ThrowFollow = 0.2f;
	UPROPERTY(EditAnywhere, Category = "Throw") float ThrowArmYaw = -12.f;
	UPROPERTY(EditAnywhere, Category = "Throw") float ThrowArmPitch = 8.f;
	UPROPERTY(EditAnywhere, Category = "Throw") float ThrowArmPush = 0.f;

	UPROPERTY(EditAnywhere, Category = "Handling") float SwapTime = 0.35f;
	UPROPERTY(EditAnywhere, Category = "Handling") float ReadyTime = 0.07f;

	// --- State (HUD) ---
	UWWOWeaponData* GetWeaponData() const { return Weapon; }
	float AdsAmount = 0.f;
	int32 Ammo = 30;
	bool bReloading = false;
	float ReloadProgress = 0.f;
	bool bMeleeing = false;
	float MeleeProgress = 0.f;
	bool bTargetUnderReticle = false;
	TWeakObjectPtr<AActor> ReticleTarget;

	void Equip(UWWOWeaponData* NewWeapon);
	bool IsAiming() const;
	bool IsSwapping() const;
	float LookMultiplier() const;
	float MoveMultiplier() const;
	void AddLook(const FVector2D& Amount) { LookDelta += Amount; }
	float CameraRoll() const; // degrees, from the camera punch
	void StartReload();
	void CancelReload();
	void StartThrowPose(const FLinearColor& GrenadeColor);
	bool IsWindingThrow() const { return ThrowClock < ThrowRelease; }
	void StartPunch(float FovDegrees, float RollDegrees, float Seconds);
	void Rumble(float Weak, float Strong, float Seconds);
	int32 MagazineSize() const;

	// A line from the eyes (ignores the player). Returns true on a hit.
	bool CastRayTo(const FVector& To, FHitResult& OutHit) const;
	AWWOCharacter* GetPlayer() const;

private:
	UPROPERTY() TObjectPtr<UWWOWeaponData> Weapon;
	UPROPERTY() TObjectPtr<USceneComponent> ModelRoot;
	UPROPERTY() TMap<FName, TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY() FWWOFPArm MeleeArm;
	UPROPERTY() FWWOFPArm ThrowArm;
	FVector MagRest = FVector::ZeroVector; // Godot space
	FDelegateHandle LoadoutHandle;

	float AdsProgress = 0.f;
	FVector2D LookDelta = FVector2D::ZeroVector;
	FVector2D SwayValue = FVector2D::ZeroVector;
	float SprintPose = 0.f, SlidePose = 0.f, AirLag = 0.f, MantlePose = 0.f;
	float ThrowTime = 0.f;
	float ThrowClock = 1e9f;
	FLinearColor ThrowColor = FLinearColor::White;
	float SwapLeft = 0.f, SwapLength = 0.35f;
	float ReadyLeft = 0.f;
	bool bReadyShotQueued = false;
	float FireCooldown = 0.f;
	float Kick = 0.f;
	float FlashTime = 0.f;
	float PunchAge = 1e9f, PunchFovValue = 0.f, PunchRollValue = 0.f, PunchLength = 0.1f;
	bool bMeleeLanded = false;
	TWeakObjectPtr<AActor> MeleeTarget;

	float M(FName Stat, float Base) const;
	bool Pressed(int32 Action) const;
	bool JustPressed(int32 Action) const;

	void BuildModel();
	void BuildArms();
	void BuildArm(FWWOFPArm& Arm, bool bLeft);
	void UpdateArmColor();
	void UpdateMelee(float DeltaSeconds);
	AActor* FindLungeTarget() const;
	void MeleeStrike();
	void MeleeImpact(AActor* Target, bool bKilled, const FVector& Point);
	void UpdateReload(float DeltaSeconds);
	void UpdateAds(float DeltaSeconds);
	void UpdateFiring(float DeltaSeconds);
	bool WantsToFire() const;
	void Shoot();
	void UpdateFov();
	void UpdateReticleTarget();
	void UpdatePose(float DeltaSeconds);
	void PlaceArm(FWWOFPArm& Arm, float AnimT, float Yaw, float PitchDeg, float Push, float Drop, bool bVisible);
	float MeleeAnimTime(float Progress) const;
	float MeleeRetracting(float Progress) const;
	float ThrowAnimTime(float Clock) const;
	float ThrowPoseLength() const { return ThrowRelease + ThrowFollow + 0.1f; }
	float PunchAmount() const;
	UStaticMeshComponent* Part(FName Name) const;
};
