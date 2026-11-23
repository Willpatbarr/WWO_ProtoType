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
class UWWODawnLotusComponent;
class AWWOPlayerController;
class UWWOLoadoutSubsystem;
class UWWOWeaponData;

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

// The movement rules' own state: the character holds one (AWWOCharacter::MoveState) and
// SimulateMove reads and writes it directly. Saved with each move (rewound when moves combine) and
// sent back with a correction so a replay starts from the host's. A new field here also needs a
// line in Serialize (the wire) and in ToString (the correction logs); nothing else.
struct FWWOMoveState
{
	bool bSprinting = false;
	bool bCrouching = false;      // wants to be crouched (Unreal keeps you crouched under a low ceiling)
	bool bSliding = false;
	bool bCrouchToggled = false;
	bool bSlideQueued = false;    // crouch tapped in the air: slide on landing
	bool bCrouchWasHeld = false;  // crouch button last move (edge detection inside the simulation)
	bool bLandedSprinting = false;
	bool bMantling = false;
	bool bLungeFlight = false;    // moving as a lunge this move (both machines, from MoveLunge)
	int32 JumpsUsed = 0;          // a byte on the wire
	float TakeoffSpeed = 0.f;     // m/s, keeps sprint/slide momentum while airborne
	float SlideSpeedNow = 0.f;    // m/s
	float SlideLockout = 0.f;     // s left before a slide you broke out of early would have ended: no new ground slide until then
	float LaunchTime = 0.f;
	float AirTime = 0.f;
	float SinceLanded = 99.f;
	float MantleT = 0.f;
	FVector SlideDir = FVector::ForwardVector;
	FVector2D GroundVel = FVector2D::ZeroVector; // m/s, the horizontal velocity the rules asked for last move (ground momentum eases from it)
	FVector MantleFrom = FVector::ZeroVector;
	FVector MantleTo = FVector::ZeroVector;
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
// Damage: per hit at the very most. Rate: shots per s. Range: cm. Burst: rounds per trigger pull.
// Round: one round's modded damage. Pellets: per shot. Always: the perks the host can't check
// (Overpressure, Final Round, Focus, Full Burst, Slideshot) at their best. Crit: the crit
// multiplier with its perks. Perks: the gun's perks (entries of WWOPerk::Table(); CheckGunHit
// counts the checkable ones per hit, ServerGunShot runs their arcs); SinceShot is how long before
// this shot that gun last fired (Cold Bore).
struct FWWOGunLimits
{
	float Damage = 0.f, Rate = 0.f, Range = 0.f, Round = 0.f, Always = 1.f, Crit = 1.f, SinceShot = 100.f;
	int32 Burst = 1, Pellets = 1;
	TArray<const struct FWWOPerk*, TInlineAllocator<6>> Perks;
};

// A player. In PvP also a target for the other team (IWWOTargetable): shots hit its body and head
// hitboxes (on only in PvP), damage goes to its health on the host.
UCLASS()
class WHATWASOURS_API AWWOCharacter : public ACharacter, public IWWOTargetable
{
	GENERATED_BODY()

	// --- Core: lifetime, the per-frame driver, controllers and input (WWOCharacter.cpp) ---
public:
	AWWOCharacter(const FObjectInitializer& ObjectInitializer);
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void NotifyControllerChanged() override;
	virtual void OnRep_PlayerState() override;
	bool bUlting = false;           // the ult is playing: no moving or looking
	AWWOPlayerController* GetWWOController() const;
	class AWWOBotController* GetBot() const; // a bot player (PvP stress tests), else null
	// This machine's own human player: controlled here, and not a bot.
	bool IsLocalHuman() const { return IsLocallyControlled() && IsPlayerControlled(); }
	// Online, the host's copy of a character controlled on another machine (a joiner's).
	bool IsHostCopyOfRemote() const { return HasAuthority() && !IsLocallyControlled() && GetNetMode() != NM_Standalone; }
	UWWOLoadoutSubsystem* GetLoadout() const;
private:
	bool bWeaponSawDead = false; // the weapon has been told of the death (or the respawn) it last saw
	bool Pressed(EWWOAction Action) const;
	bool JustPressed(EWWOAction Action) const;
	void TickRemoteOnHost(float DeltaSeconds); // someone else's character, on the host

	// --- Components ---
public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UWWOWeaponComponent> Weapon;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UWWOAimAssistComponent> AimAssist;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UWWOHealthComponent> Health;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UWWOAbilityComponent> Abilities;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UWWOGrenadeComponent> Grenades;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UWWOUltComponent> Ult;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UWWOBodyComponent> Body; // grafts and part colors on the body mesh
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UWWODawnLotusComponent> DawnLotus; // Nectar, Reveille's boost (Flora/WWODawnLotus.h)

	// --- Tunables (Settings menu). Settings rows and saved settings find these by property name: don't rename them. ---
	// Camera and controls.
public:
	UPROPERTY(EditAnywhere, Category = "Camera") float MouseSensitivity = 2.f; // x0.001 radians per pixel
	// Destiny-style low reticle: where shots go sits this fraction of the screen height below center,
	// at the hip and aimed in (the camera looks up by the matching angle; the aim doesn't move). The
	// gun lines its sight up there when aimed and the scope centers on it. Not from Godot.
	UPROPERTY(EditAnywhere, Category = "Camera") float ReticleDrop = 0.06f;
	UPROPERTY(EditAnywhere, Category = "Camera") float StickSensitivity = 4.f; // radians per second at full tilt
	// Forgiving trigger pulls. TriggerReset: how far a controller trigger has to come back up (share
	// of its travel) before pulling it in again counts as a new pull; it never has to be let go all
	// the way. FireBufferTime: a pull this long (s) before a semi-auto gun can fire still fires, the
	// moment it can.
	UPROPERTY(EditAnywhere, Category = "Camera") float TriggerReset = 0.15f;
	UPROPERTY(EditAnywhere, Category = "Camera") float FireBufferTime = 0.2f;
	UPROPERTY(EditAnywhere, Category = "Camera") float BobIntensity = 1.5f;
	UPROPERTY(EditAnywhere, Category = "Camera") float AdsBob = 0.5f; // how much of the head bob is left when aimed in (0-1)
	UPROPERTY(EditAnywhere, Category = "Camera") float LandingDip = 1.f;
	UPROPERTY(EditAnywhere, Category = "Camera") float WalkStride = 2.3f;
	UPROPERTY(EditAnywhere, Category = "Camera") float SprintStride = 4.2f;
	// Movement (sent to the host through MoveStatTable, WWOCharacterMove.cpp).
	UPROPERTY(EditAnywhere, Category = "Walk") float WalkSpeed = 5.f;
	UPROPERTY(EditAnywhere, Category = "Walk") float SprintSpeed = 10.f;
	// Ground momentum: how fast your velocity moves toward where the stick points, and to a stop
	// when you let go (m/s each second; high = instant, as it was before momentum).
	UPROPERTY(EditAnywhere, Category = "Walk") float GroundAccel = 80.f;
	UPROPERTY(EditAnywhere, Category = "Walk") float GroundDecel = 80.f;
	UPROPERTY(EditAnywhere, Category = "Crouch") float CrouchSpeed = 2.5f;
	UPROPERTY(EditAnywhere, Category = "Crouch") float SlideSpeed = 22.f;   // speed at the start of a slide
	UPROPERTY(EditAnywhere, Category = "Crouch") float SlideDistance = 9.5f; // meters before slowing to crouch speed
	UPROPERTY(EditAnywhere, Category = "Jump") float JumpHeight1 = 2.f;
	UPROPERTY(EditAnywhere, Category = "Jump") float JumpHeight2 = 2.3f;
	UPROPERTY(EditAnywhere, Category = "Jump") float AscentGravity = 20.f;
	UPROPERTY(EditAnywhere, Category = "Jump") float DescentGravity = 32.f;
	UPROPERTY(EditAnywhere, Category = "Jump") float AirControl = 5.f;
	UPROPERTY(EditAnywhere, Category = "Jump") float ApexHang = 0.5f;
	UPROPERTY(EditAnywhere, Category = "Jump") float CoyoteTime = 0.12f;
	UPROPERTY(EditAnywhere, Category = "Jump") float MantleReach = 2.f;
	UPROPERTY(EditAnywhere, Category = "Jump") float MantleTime = 0.35f;
	// HUD and audio. Not the character's own: they sit here because the Settings menu and saved
	// settings bind to them on the player; the HUD (UWWOHUDWidget) and UWWOSound read them.
	// Enemy readability (HUD settings): the tint over the enemy under the reticle (0 = off), how near
	// the reticle (HUD px at 1600 x 900) an enemy's health bar shows, and how far to the side of the
	// hit damage numbers appear (alternating left and right) so they don't cover the target.
	UPROPERTY(EditAnywhere, Category = "HUD") float TargetHighlight = 0.12f;
	UPROPERTY(EditAnywhere, Category = "HUD") float HealthBarRadius = 140.f;
	UPROPERTY(EditAnywhere, Category = "HUD") float HealthBarAdsScale = 1.75f; // the marker range grows by this while aimed in
	UPROPERTY(EditAnywhere, Category = "HUD") float DamageNumberOffset = 55.f;
	// The radar (UWWOHUDWidget::Radar): a ring of 8 sections around you, each filled in from its rim
	// by the nearest enemy that way. RadarRange (m) is its reach, RadarSteps how many fill steps there
	// are, and inside RadarFullRange (m, about melee distance) a section is full. An enemy higher or
	// lower than you fills it fainter: down to RadarMinAlpha at RadarHeightFade (m) of difference.
	UPROPERTY(EditAnywhere, Category = "HUD") float RadarRange = 30.f;
	UPROPERTY(EditAnywhere, Category = "HUD") float RadarFullRange = 4.f;
	UPROPERTY(EditAnywhere, Category = "HUD") float RadarSteps = 4.f;
	UPROPERTY(EditAnywhere, Category = "HUD") float RadarHeightFade = 6.f;
	UPROPERTY(EditAnywhere, Category = "HUD") float RadarMinAlpha = 0.2f;
	UPROPERTY(EditAnywhere, Category = "HUD") float RadarSize = 70.f; // px radius at 1600 x 900 (0 = no radar)
	// The red vignette that grows around the screen as your health and shield drop (0 = off).
	UPROPERTY(EditAnywhere, Category = "HUD") float HealthVignette = 1.f;
	UPROPERTY(EditAnywhere, Category = "Audio") float SoundVolume = 1.f; // every sound effect you hear, times this (0 = silent)

	// --- Movement: PrepareMovement, the move input, the movement rules, lunge, launch, mantle, the move stats (WWOCharacterMove.cpp) ---
public:
	FWWOMoveState MoveState;        // the movement rules' state (sprinting, sliding, jumps...): see FWWOMoveState
	FVector WishVelocity = FVector::ZeroVector;  // cm/s, read by the movement component
	FVector LungeVelocity = FVector::ZeroVector; // cm/s, while lunging or mantling
	bool IsLunging() const { return bLunging; }
	bool IsInLungeFlight() const { return MoveState.bLungeFlight; } // the movement side of a lunge (from the move input)
	bool IsMantling() const { return MoveState.bMantling; }
	bool IsOnFloor() const;
	float CurrentGravityZ() const; // cm/s^2
	virtual void Landed(const FHitResult& Hit) override;
	virtual void BaseChange() override;
	void StartLunge(AActor* Target);
	void Launch(const FVector& NewVelocityCm); // Spore Dash: keeps the horizontal speed until landing
	void ResetInputEdges();

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
	FWWOMoveState GetMoveState() const { return MoveState; }
	void SetMoveState(const FWWOMoveState& S) { MoveState = S; }
	// Your movement values (after loadout modifiers and your Settings sliders), sent to the host once
	// and whenever they change, so it simulates you with them.
	UFUNCTION(Server, Reliable) void ServerSetMoveStats(const TArray<float>& Values);
	// PvP: everyone moves by the host's movement settings (their loadouts still count): the host
	// sends its base values, and doesn't take a joiner's word for them.
	UFUNCTION(Client, Reliable) void ClientSetMoveBase(const TArray<float>& Values);
	TArray<float> BaseMoveStats() const; // the unmodified values, same order as LocalMoveStats
	void ApplyMoveBase(const TArray<float>& Values);
	TArray<float> SentMoveBase; // host: what this joiner was last sent
private:
	bool bLunging = false;     // your machine steering a lunge at its target (it sends the velocity)
	bool bLaunchPending = false;
	FVector PendingLaunch = FVector::ZeroVector;
	void EndLungeFlight();
	void ApplyLaunch(const FVector& NewVelocityCm);
	TWeakObjectPtr<AActor> LungeTarget;
	float LungeTime = 0.f;
	float LungeLastFlat = TNumericLimits<float>::Max();
	float LastFallSpeed = 0.f; // m/s
	uint64 PreparedFrame = 0; // GFrameCounter of the last PrepareMovement
	bool bNetStats = false;   // the host's copy of someone else: the movement values are theirs, already modified
	TArray<float> SentMoveStats;
	float MoveStatsCheck = 0.f;
	TArray<float> LocalMoveStats() const;
	void CaptureMoveInput();
	void TickMovement(float DeltaSeconds);
	FVector2D MoveInput() const;
	FVector WishDirection(const FVector2D& Input) const;
	void UpdateSprint(const FVector2D& Input);
	void UpdateCrouchAndSlide(const FVector& WishDir, float DeltaSeconds);
	bool HasSprintMomentum() const;
	void StartSlide(const FVector& WishDir);
	void BreakSlide();           // end a slide early (into a sprint or a crouch); sets the slide lockout
	float SlideDecel() const;    // m/s each second, so a full slide covers exactly SlideDistance
	void UpdateAir(float DeltaSeconds);
	void HandleJump(const FVector& WishDir);
	void ApplyHorizontal(const FVector& WishDir, bool bOnFloor, float DeltaSeconds);
	void Land(float FallSpeedM); // Godot's _land: landing dip, land-slide window, queued slide
	void ApplyLunge(float DeltaSeconds);
	float ApexHangClamped() const { return FMath::Clamp(ApexHang, 0.f, 0.9f); }
	float LaunchSpeed(float HeightM) const; // m/s
	bool TryMantle(const FVector2D& Input);
	void UpdateMantle(float DeltaSeconds);

	// --- View: looking, the camera (eye height, landing dip, head bob), the low reticle (WWOCharacterView.cpp) ---
public:
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	float BobTime = 0.f;
	float BobWeight = 0.f;
	float LandOffset = 0.f;         // meters, current landing dip
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
	void TurnToward(const FVector& Point, float Strength, float DeltaSeconds);
	void SetViewRotation(float PitchRad, float YawDeg);
private:
	float AppliedReticleDrop = 0.f; // radians, set with the camera each frame
	float ReticleDropAngle() const; // what ReticleDrop needs now (FOV, aiming)
	float Pitch = 0.f;
	float CameraBaseZ = 60.f; // cm above the capsule center
	float SprintBlend = 0.f;
	float LandKick = 0.f;
	void UpdateCamera(float DeltaSeconds);

	// --- Recoil: the kick, the rise and the fall back (WWOCharacterRecoil.cpp; matched to footage, pure moves only) ---
public:
	void ApplyRecoil(float PitchRad, float YawRad); // kicks the view directly
	void AddRecoil(float PitchRad, float YawRad, float KickTime, float Recovery, float RecoveryTime, float RecoveryCurve = 0.f);
	bool IsRecoiling() const { return KickLeft > 0.f || bRecovering; }
	void ResetRecoil();
private:
	// Recoil (radians)
	float KickLeft = 0.f, KickYawLeft = 0.f, KickRate = 0.f;
	bool bKickShaped = false; // the rise follows RiseShape, not a steady rate
	float KickShapeTotal = 0.f, KickShapeTime = 0.1f, KickShapeAge = 0.f;
	float RecoilRest = 0.f;
	bool bRecovering = false;
	float RecoveryTime = 0.12f, RecoveryCurve = 0.f, RecoveryAge = 0.f, RecoveryDrop = 0.f, RecoveryLeft = 0.f;
	void UpdateRecoil(float DeltaSeconds);
	float TimedLeft(float Age) const;

	// --- Combat: a PvP target (hitboxes, headshots, ApplyHit) and status effects (WWOCharacterCombat.cpp) ---
public:
	// IWWOTargetable (PvP)
	virtual TArray<FWWOAimPart> GetAimParts() const override;
	virtual bool IsCriticalComponent(const UPrimitiveComponent* Component) const override;
	virtual bool IsCriticalHit(const FHitResult& Hit, const FVector& From) const override;
	virtual bool ApplyHit(float Damage, bool bCritical, const FVector& Point, FLinearColor Color) override;
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
	void PinHeadHitbox(); // onto the body's head bone, so it follows the animations
	// Status effects.
	float SlowLeft = 0.f;
	float SlowAmount = 0.f;
	float VeilLeft = 0.f;
	float EntrenchLeft = 0.f;
	bool IsInvisible() const;
	float SlowMultiplier() const { return SlowLeft > 0.f ? 1.f - SlowAmount : 1.f; }
	float VeilMeleeMultiplier() const;
	float EntrenchRechargeMultiplier() const;
	void ApplySlow(float Amount, float Seconds);
	void ClearStatusEffects();
	// PvP: a Blinding Grenade whites out a player's screen for a while (the HUD draws it).
	void ApplyBlind(float Seconds);
	UFUNCTION(Client, Reliable) void ClientApplyBlind(float Seconds);
	float BlindLeft = 0.f, BlindTotal = 1.f;
	UFUNCTION(Client, Reliable) void ClientApplySlow(float Amount, float Seconds);
	UFUNCTION(Client, Unreliable) void ClientDamageFrom(FVector_NetQuantize From); // the host: you were hit from there (HUD marker)
	UFUNCTION(NetMulticast, Reliable) void MulticastEntrenchRoots(float Seconds);
private:
	void TickStatusTimers(float DeltaSeconds);

	// --- Look: what other machines see of this body (WWOCharacterLook.cpp) ---
public:
	void RefreshLocalVisuals();
	// Online: your flora, so everyone else sees your character model (Config/CharacterModels.json).
	UPROPERTY(ReplicatedUsing = OnRep_NetFlora) FName NetFlora;
	UFUNCTION() void OnRep_NetFlora();
	// What this player has on, sent by the host to every machine, so their body can show their
	// flora's grafts and shell colors wherever they are seen (bots and joiners alike).
	UPROPERTY(ReplicatedUsing = OnRep_NetLook) FWWONetLoadout NetLook;
	UFUNCTION() void OnRep_NetLook();
	// The ability lighting this player's fists (Thornfist up, Static Charge armed), sent by the host
	// so every machine shows the glow, not only the host's and the player's own.
	UPROPERTY(ReplicatedUsing = OnRep_NetFists) FName NetFists;
	UFUNCTION() void OnRep_NetFists();
	void ShowLook(); // someone else's body: grafts and colors from NetLook
	FString ShownLook; // the parts of NetLook the body was last built from
	UFUNCTION(Server, Reliable) void ServerSetFlora(FName Flora);
	// Online: a joiner's loadout, kept on the host (UWWOLoadoutSubsystem::Get finds it for anything
	// this character owns). Null for your own character and everyone's on a joiner.
	UPROPERTY() TObjectPtr<UWWOLoadoutSubsystem> NetLoadout;
	UFUNCTION(Server, Reliable) void ServerSetLoadout(const FWWONetLoadout& Loadout);
	FWWONetLoadout SentLoadout;
	UFUNCTION(Server, Unreliable) void ServerSetPose(uint8 Raise, int8 Look);
	// What others see of your upper body (see SendPose): how far the gun is up, 0-1, and the look's
	// pitch in degrees. Yours on your machine; a joiner's on the host as it last sent them.
	float PoseRaise = 0.f, PosePitch = 0.f;
private:
	float ShownRaise = 0.f, ShownPitch = 0.f, PoseSendLeft = 0.f, RemoteStride = 0.f;
	uint8 SentRaise = 0;
	int8 SentLook = 0;
	float LocalRaise() const;
	void SendPose(float DeltaSeconds);
	void RemoteFootsteps(float DeltaSeconds);
	bool bLocalVisuals = false; // set up as your own character (first-person gun, shadow-only body)
	void SetupVisuals(bool bLocal);
	void UpdateTeamTint(); // PvP: a player you mark flashes a strong outline over a faint tint (red foes, blue teammates)
	void UpdateModel(); // the body animation from what you're doing (Godot: _update_model)
	bool bPosedDown = false; // the death clip has been started for this downing (it holds its last frame)

	// --- Net: what replicates, the online kit and revive (WWOCharacterNet.cpp) ---
public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// Online: you're holding Revive next to a downed teammate (the host runs the revive).
	UFUNCTION(Server, Reliable) void ServerSetRevive(bool bHolding);
	// Online kit: a joiner's ability or ult (its own machine runs it at once; the host runs the
	// world side), the host's slow on a joiner, and roots everyone else sees.
	UFUNCTION(Server, Reliable) void ServerUseAbility(int32 Slot, FVector_NetQuantizeNormal Aim);
	// The host said no to something this machine already did (What: an EWWOKit, Player/WWOKitRefusal.h;
	// Slot: the ability's): the charge comes back and what it started here stops.
	UFUNCTION(Client, Reliable) void ClientKitRefused(uint8 What, int32 Slot);
	FVector NetAim = FVector::ForwardVector; // host: where a joiner was looking when it last used an ability
	UFUNCTION(Server, Reliable) void ServerActivateUlt();
	UFUNCTION(Server, Reliable) void ServerUltAim(FVector_NetQuantizeNormal Aim); // sent just before it: where a joiner's ult points (NetAim)
	// Holding the Revive key anywhere, alive: the host's view of it, for things you hold a key at
	// (FootRace's levers). Set on the host from its own input or a joiner's ServerSetUse.
	bool bHoldingUse = false;
	UFUNCTION(Server, Reliable) void ServerSetUse(bool bHolding);
private:
	bool bSentRevive = false;
	bool bSentUse = false;
	void UpdateReviveInput();
	void UpdateUseInput();

	// --- Gun net: a joiner's shots and punches, checked and applied by the host (WWOCharacterGunNet.cpp) ---
public:
	// Online gunfire (see FWWOGunShot).
	UFUNCTION(Server, Reliable) void ServerGunShot(const FWWOGunShot& Shot);
	// A shot by this character as seen from outside (a bot's, another player's): the muzzle flash at
	// its gun hand, a trail to each end, and (bMarks) a mark where each one landed.
	void ShowShotFromBody(const FVector& From, TConstArrayView<FVector_NetQuantize> Ends, bool bMarks);
	UFUNCTION(NetMulticast, Unreliable) void MulticastGunShot(FVector_NetQuantize From, const TArray<FVector_NetQuantize>& Ends);

	// Online melee: a joiner's swing (so the host's copy of its body punches, which everyone else
	// sees) and, if it landed, what it hit. The host checks the hit, applies it and the knockback.
	UFUNCTION(Server, Reliable) void ServerMeleeSwing();
	UFUNCTION(Server, Reliable) void ServerMeleeHit(AActor* Target, float Damage, FVector_NetQuantize Origin, FVector_NetQuantize Point);
	void PlaySwingOnBody(); // the punch on the full body (others see it online)

	// The host's word on what a joiner's shot or punch killed (kill perks, the kill marker).
	UFUNCTION(Client, Reliable) void ClientKillCredit(int32 Kills, bool bMelee);
	// A hit by something this joiner left in the world (its grenade, pool...): the hit marker.
	UFUNCTION(Client, Unreliable) void ClientHitMarker(bool bKilled);
	// Online: a joiner's grenade leaves its hand (the host builds and throws it).
	UFUNCTION(Server, Reliable) void ServerThrowGrenade(FVector_NetQuantize Hand, FVector_NetQuantizeNormal Dir);
	double LastSwingTime = -100.0;  // host: this player's last melee swing (a hit, or a lunge, needs one)
	double LastLaunchTime = -100.0; // host: the last launch (Spore Dash) it let this player's moves make
	float LungeOnCredit = 0.f;      // host: seconds of lunging moves taken before their swing arrived
private:
	// Host: per gun (by id), the shots this player may fire right now (refills at that gun's own
	// rate, so naming a fast gun doesn't pay for a slow one's shots) and when it last fired.
	struct FShotBudget { float Shots = 3.f; double Time = 0.0; double LastShot = -1000.0; };
	TMap<FName, FShotBudget> ShotBudgets;
	// The most a gun with these mods can do (Why says what's wrong with a setup that can't exist).
	static bool GunLimits(const UWWOLoadoutSubsystem* L, const UWWOWeaponData* Gun, const TArray<FName>& ModIds, FWWOGunLimits& Out, FString& Why);
	bool CheckGunHit(const FWWOGunShot& Shot, const FWWOGunHit& Hit, const struct FWWOGunLimits& Limits, FString& Why) const;
	bool CheckMeleeHit(AActor* Target, float Damage, const FVector& Origin, const FVector& Point, FString& Why);
	bool CheckReach(AActor* Target, const FVector& Origin, const FVector& Point, FString& Why) const; // on the target, in sight
	double LastMeleeHitTime = -100.0; // host: when this player's last melee hit was applied

	// The regression scenarios (-WWORegress, Player/WWORegress.h) read and reset private state between runs; no gameplay code uses this.
	friend class FWWORegress;
};
