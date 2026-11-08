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
#include "Engine/NetSerialization.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOTargetable.h"
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

// Online movement (see AWWOCharacter::SimulateMove). The input the movement simulation runs on:
// captured from your controller each frame, sent to the host with each move, and replayed by your
// own machine after a correction, so the host and you run exactly the same movement rules.
enum EWWOMoveBit : uint8
{
	MoveJump = 1 << 0,
	MoveSprint = 1 << 1,
	MoveCrouch = 1 << 2,
	MoveFire = 1 << 3,
	MoveAim = 1 << 4,
	MoveFrozen = 1 << 5,     // the ult or Entrench: no moving
	MoveLunge = 1 << 6,      // Held: lunging this move, at Push (the velocity your machine aimed it with)
	MoveLaunch = 1 << 7,     // Pressed: launched this move (Spore Dash), Push is the new velocity
};

struct FWWOMoveInput
{
	FVector2D Move = FVector2D::ZeroVector; // stick: x = right, y = forward
	uint8 Held = 0;                         // EWWOMoveBit
	uint8 Pressed = 0;                      // went down this frame
	float SpeedScale = 1.f;                 // abilities x aiming x slow
	uint8 ExtraJumps = 0;                   // from abilities
	FVector Push = FVector::ZeroVector;     // cm/s, with MoveLunge or MoveLaunch (the host caps it)

	bool HasPush() const { return (Held & MoveLunge) || (Pressed & MoveLaunch); }
	bool operator==(const FWWOMoveInput& O) const
	{
		return Move.Equals(O.Move, 0.01f) && Held == O.Held && Pressed == O.Pressed && FMath::IsNearlyEqual(SpeedScale, O.SpeedScale, 0.01f) && ExtraJumps == O.ExtraJumps
			&& Push.Equals(O.Push, 1.f);
	}
	void Serialize(FArchive& Ar);
	void Quantize(); // round to what Serialize sends, so your machine simulates the same input as the host
};

// The movement rules' own state, sent back with a correction so a replay starts from the host's.
struct FWWOMoveState
{
	bool bSprinting = false, bCrouching = false, bSliding = false, bCrouchToggled = false, bSlideQueued = false;
	bool bCrouchWasHeld = false, bLandedSprinting = false, bMantling = false, bLungeFlight = false;
	uint8 JumpsUsed = 0;
	float TakeoffSpeed = 0.f, SlideSpeedNow = 0.f, LaunchTime = 0.f, AirTime = 0.f, SinceLanded = 99.f, MantleT = 0.f;
	FVector SlideDir = FVector::ForwardVector, MantleFrom = FVector::ZeroVector, MantleTo = FVector::ZeroVector;
	void Serialize(FArchive& Ar);
	FString ToString() const; // for logs
};

// Online gunfire. A joiner's shot is shown on its own screen at once (impacts, flash, number, hit
// marker), and sent to the host as what it hit; the host checks each hit (see ServerGunShot) and
// applies the damage. Everyone else sees the shot's impacts (MulticastGunShot).
USTRUCT()
struct FWWOGunHit
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<AActor> Target;
	UPROPERTY() float Damage = 0.f;       // all of this shot's pellets on that target
	UPROPERTY() bool bCritical = false;
	UPROPERTY() FVector_NetQuantize Point; // where it was hit
};

USTRUCT()
struct FWWOGunShot
{
	GENERATED_BODY()
	UPROPERTY() FName WeaponId;
	UPROPERTY() TArray<FName> Mods;               // the mods on it (the host checks the shot against this setup)
	UPROPERTY() FVector_NetQuantize Origin;       // the shooter's eye
	UPROPERTY() TArray<FWWOGunHit> Hits;          // one per target hit
	UPROPERTY() TArray<FVector_NetQuantize> Ends; // every pellet's impact (walls too), for the looks
};

// The most a gun setup can do, for the host checking a joiner's shot (WWOCharacterGunNet.cpp).
struct FWWOGunLimits { float Damage = 0.f, Rate = 0.f, Range = 0.f; }; // per hit, shots per s, cm

// A player. In PvP also a target for the other team (IWWOTargetable): shots hit its body and head
// hitboxes (on only in PvP), damage goes to its health on the host.
UCLASS()
class WHATWASOURS_API AWWOCharacter : public ACharacter, public IWWOTargetable
{
	GENERATED_BODY()

public:
	AWWOCharacter(const FObjectInitializer& ObjectInitializer);

	// IWWOTargetable (PvP)
	virtual TArray<FWWOAimPart> GetAimParts() const override;
	virtual bool IsCriticalComponent(const UPrimitiveComponent* Component) const override;
	virtual bool TakeHit(float Damage, bool bCritical, const FVector& Point, FLinearColor Color = FLinearColor(0, 0, 0, 0)) override;
	virtual bool IsDead() const override;
	virtual float GetHealth() const override;
	virtual float GetMaxHealth() const override;
	virtual float GetShield() const override;
	virtual float GetMaxShield() const override;
	virtual float GetBodyHeight() const override { return 200.f; }
	virtual FVector FeetLocation() const override;
	UPROPERTY() TObjectPtr<class UCapsuleComponent> BodyHitbox;
	UPROPERTY() TObjectPtr<class USphereComponent> HeadHitbox;
	void UpdateHitboxes(); // on only in PvP (co-op traces pass through players as before)
	bool bHitboxesOn = false;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void NotifyControllerChanged() override;
	virtual void OnRep_PlayerState() override;
	void RefreshLocalVisuals();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Landed(const FHitResult& Hit) override;
	virtual void BaseChange() override;
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
	// Destiny-style low reticle: where shots go sits this fraction of the screen height below center,
	// at the hip and aimed in (the camera looks up by the matching angle; the aim doesn't move). The
	// gun lines its sight up there when aimed and the scope centers on it. Not from Godot.
	UPROPERTY(EditAnywhere, Category = "Camera") float ReticleDrop = 0.06f;
	// Enemy readability (HUD settings): the tint over the enemy under the reticle (0 = off), how near
	// the reticle (HUD px at 1600 x 900) an enemy's health bar shows, and how far to the side of the
	// hit damage numbers appear (alternating left and right) so they don't cover the target.
	UPROPERTY(EditAnywhere, Category = "HUD") float TargetHighlight = 0.12f;
	UPROPERTY(EditAnywhere, Category = "HUD") float HealthBarRadius = 140.f;
	UPROPERTY(EditAnywhere, Category = "HUD") float DamageNumberOffset = 55.f;
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
	bool IsInLungeFlight() const { return bLungeFlight; } // the movement side of a lunge (from the move input)
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
	FVector AimForward() const; // where shots go: the reticle, ReticleDrop below the camera's center
	// The angle (radians) the camera currently looks above the aim for ReticleDrop.
	float GetReticleDropAngle() const { return AppliedReticleDrop; }
	// The angle (radians) below the view axis of the reticle's point on screen, for a horizontal FOV
	// (the world camera's, or the gun's first-person one).
	float ReticleAngleForFov(float HorizontalFovDeg) const;

	void Look(FVector2D AmountRad); // x = right, y = up; scaled by ADS
	void ApplyRecoil(float PitchRad, float YawRad); // kicks the view directly
	void AddRecoil(float PitchRad, float YawRad, float KickTime, float Recovery, float RecoveryTime, float RecoveryCurve = 0.f);
	void TurnToward(const FVector& Point, float Strength, float DeltaSeconds);
	void SetViewRotation(float PitchRad, float YawDeg);
	bool IsRecoiling() const { return KickLeft > 0.f || bRecovering; }

	void StartLunge(AActor* Target);
	void Launch(const FVector& NewVelocityCm); // Spore Dash: keeps the horizontal speed until landing
	void ApplySlow(float Amount, float Seconds);
	void ResetInputEdges();
	void ClearStatusEffects();
	void ResetRecoil();

	float M(FName Stat, float Base) const; // a stat after frame part and trait modifiers

	// This frame's local work before moving (health, looking, recoil, capturing the move input); the
	// movement component calls it just before it moves, on the machine that controls you.
	void PrepareMovement(float DeltaSeconds);

	// --- Online movement ---
	// The movement rules for one move (sets WishVelocity), from MoveIn only. The movement component
	// runs it for every move: yours as you play, the host's copy of you as your moves arrive, and
	// your replays after a correction.
	FWWOMoveInput MoveIn;
	void SimulateMove(float DeltaSeconds);
	FWWOMoveState GetMoveState() const;
	void SetMoveState(const FWWOMoveState& State);
	// Your movement values (after loadout modifiers and your Settings sliders), sent to the host once
	// and whenever they change, so it simulates you with them.
	UFUNCTION(Server, Reliable) void ServerSetMoveStats(const TArray<float>& Values);
	// PvP: everyone moves by the host's movement settings (their loadouts still count): the host
	// sends its base values, and doesn't take a joiner's word for them.
	UFUNCTION(Client, Reliable) void ClientSetMoveBase(const TArray<float>& Values);
	TArray<float> BaseMoveStats() const; // the unmodified values, same order as LocalMoveStats
	void ApplyMoveBase(const TArray<float>& Values);
	TArray<float> SentMoveBase; // host: what this joiner was last sent

	// Online: your flora, so everyone else sees your character model (Config/CharacterModels.json).
	UPROPERTY(ReplicatedUsing = OnRep_NetFlora) FName NetFlora;
	UFUNCTION() void OnRep_NetFlora();
	UFUNCTION(Server, Reliable) void ServerSetFlora(FName Flora);
	// Online: a joiner's loadout, kept on the host (UWWOLoadoutSubsystem::Get finds it for anything
	// this character owns). Null for your own character and everyone's on a joiner.
	UPROPERTY() TObjectPtr<UWWOLoadoutSubsystem> NetLoadout;
	UFUNCTION(Server, Reliable) void ServerSetLoadout(const FWWONetLoadout& Loadout);
	FWWONetLoadout SentLoadout;
	// Online: you're holding Revive next to a downed teammate (the host runs the revive).
	UFUNCTION(Server, Reliable) void ServerSetRevive(bool bHolding);

	// Online gunfire (see FWWOGunShot).
	UFUNCTION(Server, Reliable) void ServerGunShot(const FWWOGunShot& Shot);
	UFUNCTION(NetMulticast, Unreliable) void MulticastGunShot(FVector_NetQuantize From, const TArray<FVector_NetQuantize>& Ends);
	// The host's tally of joiners' shots, for -WWOAutoTestNet.
	static int32 NetShotsTaken, NetHitsApplied, NetHitsRejected;
	static int32 NetShotsSeen; // shots from someone else shown on this machine

	// Online melee: a joiner's swing (so the host's copy of its body punches, which everyone else
	// sees) and, if it landed, what it hit. The host checks the hit, applies it and the knockback.
	UFUNCTION(Server, Reliable) void ServerMeleeSwing();
	UFUNCTION(Server, Reliable) void ServerMeleeHit(AActor* Target, float Damage, FVector_NetQuantize Origin, FVector_NetQuantize Point);
	void PlaySwingOnBody(); // the punch on the full body (others see it online)
	static int32 NetMeleeSwings, NetMeleeApplied, NetMeleeRejected; // the host's tally of joiners' melee

	// The host's word on what a joiner's shot or punch killed (kill perks, the kill marker).
	UFUNCTION(Client, Reliable) void ClientKillCredit(int32 Kills, bool bMelee);
	// A hit by something this joiner left in the world (its grenade, pool...): the hit marker.
	UFUNCTION(Client, Unreliable) void ClientHitMarker(bool bKilled);
	// Online: a joiner's grenade leaves its hand (the host builds and throws it).
	UFUNCTION(Server, Reliable) void ServerThrowGrenade(FVector_NetQuantize Hand, FVector_NetQuantizeNormal Dir);
	// Online kit: a joiner's ability or ult (its own machine runs it at once; the host runs the
	// world side), the host's slow on a joiner, and roots everyone else sees.
	UFUNCTION(Server, Reliable) void ServerUseAbility(int32 Slot);
	UFUNCTION(Server, Reliable) void ServerActivateUlt();
	UFUNCTION(Client, Reliable) void ClientApplySlow(float Amount, float Seconds);
	UFUNCTION(NetMulticast, Reliable) void MulticastEntrenchRoots(float Seconds);
	static int32 NetAbilitiesUsed, NetUltsCast; // host: joiners' abilities and ults it ran
	static int32 NetKillsCredited; // joiner: kills the host confirmed; host: kills it credited to joiners

	AWWOPlayerController* GetWWOController() const;
	class AWWOBotController* GetBot() const; // a bot player (PvP stress tests), else null
	UWWOLoadoutSubsystem* GetLoadout() const;

private:
	float ShotBudget = 3.f;      // host: shots this player may fire right now (refills at the gun's rate)
	double ShotBudgetTime = 0.0; // when it was last refilled
	bool CheckGunHit(const FWWOGunShot& Shot, const FWWOGunHit& Hit, const struct FWWOGunLimits& Limits, FString& Why) const;
	bool CheckMeleeHit(AActor* Target, float Damage, const FVector& Origin, const FVector& Point, FString& Why);
	bool CheckReach(AActor* Target, const FVector& Origin, const FVector& Point, FString& Why) const; // on the target, in sight
	double LastMeleeHitTime = -100.0; // host: when this player's last melee hit was applied
	float AppliedReticleDrop = 0.f; // radians, set with the camera each frame
	float ReticleDropAngle() const; // what ReticleDrop needs now (FOV, aiming)

	float Pitch = 0.f;
	bool bLunging = false;     // your machine steering a lunge at its target (it sends the velocity)
	bool bLungeFlight = false; // moving as a lunge this move (both machines, from MoveLunge)
	bool bLaunchPending = false;
	FVector PendingLaunch = FVector::ZeroVector;
	void EndLungeFlight();
	void ApplyLaunch(const FVector& NewVelocityCm);
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
	bool bNetStats = false;   // the host's copy of someone else: the movement values are theirs, already modified
	bool bLocalVisuals = false; // set up as your own character (first-person gun, shadow-only body)
	void SetupVisuals(bool bLocal);
	TArray<float> SentMoveStats;
	float MoveStatsCheck = 0.f;
	TArray<float> LocalMoveStats() const;
	void CaptureMoveInput();

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
	void ApplyHorizontal(const FVector& WishDir, bool bOnFloor, float DeltaSeconds);
	void Land(float FallSpeedM); // Godot's _land: landing dip, land-slide window, queued slide
	void ApplyLunge(float DeltaSeconds);
	void UpdateCamera(float DeltaSeconds);
	void UpdateModel(); // the body animation from what you're doing (Godot: _update_model)
	void UpdateRecoil(float DeltaSeconds);
	float TimedLeft(float Age) const;
	float ApexHangClamped() const { return FMath::Clamp(ApexHang, 0.f, 0.9f); }
	float LaunchSpeed(float HeightM) const; // m/s
	bool TryMantle(const FVector2D& Input);
	void UpdateMantle(float DeltaSeconds);
	bool Pressed(EWWOAction Action) const;
	bool bSentRevive = false;
	bool bPosedDown = false; // the death clip has been started for this downing (it holds its last frame)
	void UpdateReviveInput();
	void TickStatusTimers(float DeltaSeconds);
	void TickRemoteOnHost(float DeltaSeconds); // someone else's character, on the host
	bool JustPressed(EWWOAction Action) const;
};
