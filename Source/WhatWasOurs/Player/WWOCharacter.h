#pragma once

// The player (Godot: player.gd). First-person movement: walk, latching sprint, crouch
// toggle, tap-to-slide (exact slide distance), double/triple jump with exact heights,
// separate ascent/descent gravity, apex hang, coyote time, air momentum and control,
// landing dip, stride-based head bob, mantling, melee lunge, kick-and-recover recoil.
// Every tunable is in meters / seconds like the Godot sliders.
//
// Components (Godot sibling nodes): Weapon (gun.gd), AimAssist, Health (player_health.gd),
// Abilities (ability_manager.gd), Grenades (grenade_thrower.gd), Ult (ult_manager.gd).

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "WWOTypes.h"
#include "WWOCharacter.generated.h"

class UCameraComponent;
class UWWOWeaponComponent;
class UWWOAimAssistComponent;
class UWWOHealthComponent;
class UWWOAbilityComponent;
class UWWOGrenadeComponent;
class UWWOUltComponent;
class UWWOBodyComponent;
class AWWOPlayerController;
class UWWOLoadoutSubsystem;

UCLASS()
class WHATWASOURS_API AWWOCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AWWOCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Landed(const FHitResult& Hit) override;
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	// --- Components ---
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UWWOWeaponComponent> Weapon;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UWWOAimAssistComponent> AimAssist;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UWWOHealthComponent> Health;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UWWOAbilityComponent> Abilities;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UWWOGrenadeComponent> Grenades;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UWWOUltComponent> Ult;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UWWOBodyComponent> Body; // grafts and part colors on the body mesh

	// --- Tunables (Settings menu) ---
	UPROPERTY(EditAnywhere, Category = "Camera") float MouseSensitivity = 2.f; // x0.001 radians per pixel
	UPROPERTY(EditAnywhere, Category = "Camera") float StickSensitivity = 4.f; // radians per second at full tilt
	UPROPERTY(EditAnywhere, Category = "Walk") float WalkSpeed = 5.f;
	UPROPERTY(EditAnywhere, Category = "Walk") float SprintSpeed = 10.f;
	UPROPERTY(EditAnywhere, Category = "Crouch") float CrouchSpeed = 2.5f;
	UPROPERTY(EditAnywhere, Category = "Crouch") float SlideSpeed = 22.f;   // speed at the start of a slide
	UPROPERTY(EditAnywhere, Category = "Crouch") float SlideDistance = 9.5f; // meters before slowing to crouch speed
	UPROPERTY(EditAnywhere, Category = "Jump") float JumpHeight1 = 2.f;
	UPROPERTY(EditAnywhere, Category = "Jump") float JumpHeight2 = 2.3f;
	UPROPERTY(EditAnywhere, Category = "Jump") float AscentGravity = 20.f;
	UPROPERTY(EditAnywhere, Category = "Jump") float DescentGravity = 32.f;
	UPROPERTY(EditAnywhere, Category = "Camera") float BobIntensity = 1.5f;
	UPROPERTY(EditAnywhere, Category = "Jump") float AirControl = 5.f;
	UPROPERTY(EditAnywhere, Category = "Jump") float ApexHang = 0.5f;
	UPROPERTY(EditAnywhere, Category = "Jump") float CoyoteTime = 0.12f;
	UPROPERTY(EditAnywhere, Category = "Camera") float LandingDip = 1.f;
	UPROPERTY(EditAnywhere, Category = "Jump") float MantleReach = 2.f;
	UPROPERTY(EditAnywhere, Category = "Jump") float MantleTime = 0.35f;
	UPROPERTY(EditAnywhere, Category = "Camera") float WalkStride = 2.3f;
	UPROPERTY(EditAnywhere, Category = "Camera") float SprintStride = 4.2f;

	// --- State (read by the gun, HUD and abilities) ---
	bool bSprinting = false;
	bool bCrouching = false;
	bool bSliding = false;
	bool bUlting = false;           // the ult is playing: no moving or looking
	int32 JumpsUsed = 0;
	float TakeoffSpeed = 0.f;       // m/s, keeps sprint/slide momentum while airborne
	float SlowLeft = 0.f;
	float SlowAmount = 0.f;
	float VeilLeft = 0.f;
	float EntrenchLeft = 0.f;
	float BobTime = 0.f;
	float BobWeight = 0.f;
	float LandOffset = 0.f;         // meters, current landing dip
	FVector WishVelocity = FVector::ZeroVector;  // cm/s, read by the movement component
	FVector LungeVelocity = FVector::ZeroVector; // cm/s, while lunging or mantling

	bool IsLunging() const { return bLunging; }
	bool IsMantling() const { return bMantling; }
	bool IsOnFloor() const;
	bool IsInvisible() const;
	float SlowMultiplier() const { return SlowLeft > 0.f ? 1.f - SlowAmount : 1.f; }
	float VeilMeleeMultiplier() const;
	float EntrenchRechargeMultiplier() const;
	float CurrentGravityZ() const; // cm/s^2

	// Camera pitch (radians, + = up) and the eye point / look direction.
	float GetPitch() const { return Pitch; }
	FVector EyeLocation() const;
	FVector AimForward() const;

	void Look(FVector2D AmountRad); // x = right, y = up; scaled by ADS
	void ApplyRecoil(float PitchRad, float YawRad); // kicks the view directly
	void AddRecoil(float PitchRad, float YawRad, float KickTime, float Recovery, float RecoveryTime, float RecoveryCurve = 0.f);
	void TurnToward(const FVector& Point, float Strength, float DeltaSeconds);
	void SetViewRotation(float PitchRad, float YawDeg);

	void StartLunge(AActor* Target);
	void Launch(const FVector& NewVelocityCm); // Spore Dash: keeps the horizontal speed until landing
	void ApplySlow(float Amount, float Seconds);
	void ResetInputEdges();
	void ClearStatusEffects();
	void ResetRecoil();

	float M(FName Stat, float Base) const; // a stat after frame part and trait modifiers

	// Health and this frame's movement rules (sets WishVelocity); the movement component calls
	// it just before it moves.
	void PrepareMovement(float DeltaSeconds);

	AWWOPlayerController* GetWWOController() const;
	UWWOLoadoutSubsystem* GetLoadout() const;

private:
	float Pitch = 0.f;
	bool bLunging = false;
	bool bMantling = false;
	TWeakObjectPtr<AActor> LungeTarget;
	float LungeTime = 0.f;
	float LungeLastFlat = TNumericLimits<float>::Max();
	FVector MantleFrom, MantleTo;
	float MantleT = 0.f;
	FVector SlideDir = FVector::ForwardVector;
	float SlideSpeedNow = 0.f; // m/s
	float LaunchTime = 0.f;
	bool bCrouchWasHeld = false;
	bool bCrouchToggled = false;
	bool bSlideQueued = false;
	float SinceLanded = 99.f;
	bool bLandedSprinting = false;
	float CameraBaseZ = 60.f; // cm above the capsule center
	float SprintBlend = 0.f;
	float AirTime = 0.f;
	float LandKick = 0.f;
	float LastFallSpeed = 0.f; // m/s
	uint64 PreparedFrame = 0; // GFrameCounter of the last PrepareMovement

	// Recoil (radians)
	float KickLeft = 0.f, KickYawLeft = 0.f, KickRate = 0.f;
	float RecoilRest = 0.f;
	bool bRecovering = false;
	float RecoveryTime = 0.12f, RecoveryCurve = 0.f, RecoveryAge = 0.f, RecoveryDrop = 0.f, RecoveryLeft = 0.f;

	void TickMovement(float DeltaSeconds);
	FVector2D MoveInput() const;
	FVector WishDirection(const FVector2D& Input) const;
	void UpdateSprint(const FVector2D& Input);
	void UpdateCrouchAndSlide(const FVector& WishDir, float DeltaSeconds);
	bool HasSprintMomentum() const;
	void StartSlide(const FVector& WishDir);
	void UpdateAir(float DeltaSeconds);
	void HandleJump(const FVector& WishDir);
	void ApplyHorizontal(const FVector& WishDir, float DeltaSeconds);
	void ApplyLunge(float DeltaSeconds);
	void UpdateCamera(float DeltaSeconds);
	void UpdateRecoil(float DeltaSeconds);
	float TimedLeft(float Age) const;
	float ApexHangClamped() const { return FMath::Clamp(ApexHang, 0.f, 0.9f); }
	float LaunchSpeed(float HeightM) const; // m/s
	bool TryMantle(const FVector2D& Input);
	void UpdateMantle(float DeltaSeconds);
	bool Pressed(EWWOAction Action) const;
	bool JustPressed(EWWOAction Action) const;
};
