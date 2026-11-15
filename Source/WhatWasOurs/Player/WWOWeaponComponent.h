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
class UPoseableMeshComponent;
class UAnimSequence;
class UMaterialInstanceDynamic;
struct FHitResult;

DECLARE_MULTICAST_DELEGATE_TwoParams(FWWOHitConfirmed, bool /*bCritical*/, bool /*bKilled*/);

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
	UPROPERTY(EditAnywhere, Category = "Feel") float BlastShake = 1.f; // how hard a nearby explosion shakes the view and the controller (0 = off)
	UPROPERTY(EditAnywhere, Category = "Feel") float HitFlinch = 1.f;  // how hard taking damage jolts the view and the controller (0 = off); it doesn't move your aim
	// A jolt from outside (an explosion nearby, a hit taken): a camera punch and a rumble, by Strength.
	void Shake(float Strength, float Seconds);
	// How big every gun's model is when aimed down sights, against its hip size (1 = the same). It
	// scales around the sight point, so the sight stays on the reticle. Not from Godot.
	UPROPERTY(EditAnywhere, Category = "Camera") float AdsGunScale = 1.f;
	UPROPERTY(EditAnywhere, Category = "Camera") float AdsDotSize = 2.f; // px: the aimed-in dot the HUD draws at the aim point
	// Aimed in, the gun tips down just enough that you look exactly along its top instead of down
	// onto it: the top shows as a level edge under the sight, not a ramp running up to it
	// (1 = fully level, 0 = the gun points straight at the reticle as at the hip). Not from Godot.
	UPROPERTY(EditAnywhere, Category = "Camera") float AdsLevel = 1.f;

	// Where the pose clip's rifle (its origin is at the grip) sits on this gun, whose origin is its sight point (cm: forward, right, up).
	FVector DebugGunOffset = FVector::ZeroVector; // autotest: moves the gun (Godot space, m) to bring the hands into view
	FVector DebugGunRotation = FVector::ZeroVector; // and turns it (radians: pitch, yaw, roll)
	UPROPERTY(EditAnywhere, Category = "Hands") FVector HandsGunOffset = FVector(-10.f, 0.f, -13.f);
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

	UPROPERTY(EditAnywhere, Category = "Throw") float ThrowRelease = 0.22f;
	UPROPERTY(EditAnywhere, Category = "Throw") float ThrowFollow = 0.2f;

	UPROPERTY(EditAnywhere, Category = "Handling") float SwapTime = 0.35f;
	UPROPERTY(EditAnywhere, Category = "Handling") float ReadyTime = 0.07f;

	// --- State (HUD) ---
	// The equipped weapon's stats with its mods and active perks applied (a copy, rebuilt each
	// frame from the asset, so Settings sliders still tune the asset live).
	UWWOWeaponData* GetWeaponData() const { return Weapon; }
	UWWOWeaponData* GetBaseWeaponData() const { return BaseWeapon; }
	FString PerkStatus() const;
	FString ConfigToast; // "Config 2: Long Barrel, ..." for a moment after a quick swap
	float ConfigToastLeft = 0.f; // active perks for the HUD, e.g. "Focus 3/5 · Static 7/10"
	float AdsAmount = 0.f;
	float SecondsSinceShot() const { return SinceShot; }
	int32 Ammo = 30;
	bool bReloading = false;
	float ReloadProgress = 0.f;
	bool bMeleeing = false;
	float MeleeProgress = 0.f;
	bool bTargetUnderReticle = false;
	TWeakObjectPtr<AActor> ReticleTarget;

	void Equip(UWWOWeaponData* NewWeapon);
	// The gun other players see in this character's right hand (its weapon's mesh from
	// Config/WeaponModels.json, gripped at the model's "hand" point): lowered at rest, and up along
	// the look as the body's aim pose comes in. Called for every character, every frame.
	void UpdateWorldGun(bool bShow);
	float WorldGunRaise = 1.f; // the gun other players see: 1 up and aimed, 0 carried (UpdateWorldGun)
	bool bForceWorldGun = false; // the pose test: show it on your own body too
	// Stops what the gun was in the middle of (a burst, a reload, a punch): when its owner dies or
	// starts an ult, so none of it carries on by itself afterwards.
	void Interrupt();
	// A fresh life: every gun full and its perks as on first pickup.
	void ResetForRespawn();
	void RebindLoadout(); // follow the loadout this character uses now (see UWWOLoadoutSubsystem::Get)
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
	float GetThrowClock() const { return ThrowClock; }
	void StartPunch(float FovDegrees, float RollDegrees, float Seconds);
	void Rumble(float Weak, float Strong, float Seconds);
	int32 MagazineSize() const;
	// Someone else's character (online): no first-person gun or arms (they're drawn for its owner).
	void SetFirstPersonHidden(bool bHidden);
	bool bFirstPersonHidden = false; // someone else's character online: no first-person gun or arms

	// A line from the eyes (ignores the player). Returns true on a hit.
	bool CastRayTo(const FVector& To, FHitResult& OutHit) const;
	AWWOCharacter* GetPlayer() const;

	// Kills the host confirmed (online, a joiner's kill perks wait for these; see ClientKillCredit).
	void OnKillsConfirmed(int32 Kills, bool bMelee);

	// Melee, for the host checking a joiner's punch (see AWWOCharacter::ServerMeleeHit).
	float MeleeSwingTime() const { return M("melee_time", MeleeTime); }
	void MeleeKnockBack(AActor* Target, bool bKilled) const; // shove it away from the player (host only online)
	// The kit a landed punch sets off: Melee Guard, Thornfist's discharge, an armed Static Charge's
	// chain. The host runs it for every punch (a joiner's too); a joiner only shows its own discharge.
	void MeleeKitEffects(AActor* Target, const FVector& Point);
	int32 HostStaticHits = 0; // host: a joiner's hits toward the Static perk's arc
	FName HostStaticGun;      // ...with this gun

private:
	UPROPERTY() TObjectPtr<UWWOWeaponData> BaseWeapon; // the asset
	UPROPERTY() TObjectPtr<UWWOWeaponData> Weapon;     // its modded copy
	FString ModSignature; // the equipped mods, to refill the magazine when they change
	int32 HitStreak = 0;  // Focus: hits in a row
	int32 StaticHits = 0; // Static: hits toward the next arc
	float AdrenalLeft = 0.f; // Adrenal: seconds of faster reloads left
	bool bOverpressure = true; // Overpressure: the next shot is the first since a reload
	float AimHeld = 0.f;       // Patience: seconds fully aimed in
	float HammerLeft = 0.f;    // Hammer: seconds of faster fire left
	float SinceShot = 100.f;   // Cold Bore: seconds since the last shot
	int32 RefundRounds = 0;    // Marksman: rounds to put back after this shot (a crit)
	// Burst fire (UWWOWeaponData::Burst): the rounds still to come in the burst under way, the
	// time to the next one, and how many of its rounds have hit (Full Burst).
	int32 BurstLeft = 0, BurstSize = 0, BurstHits = 0;
	float BurstCooldown = 0.f;
	bool bFullBurst = false;    // Full Burst: the last burst landed every round
	bool bBurstBoosted = false; // ...so this one hits harder
	// What each gun was left with when you swapped away from it, by weapon id: swapping back is
	// not a reload and doesn't re-arm Overpressure or Cold Bore.
	struct FGunState { int32 Ammo = 0; bool bOverpressure = true; double LastShot = -1000.0; };
	TMap<FName, FGunState> GunStates;
	void StoreGunState();
	void BeginBurst(int32 Rounds);
	void EndBurst();
	void FireRound(); // one round: the shot, the ammo, a refunded round
	void RefreshStats();
	bool IsAnchored() const;
	void OnShotResult(AActor* FirstHit, int32 HitCount, int32 Kills, bool bCritical);
	void ApplyKillPerks(int32 Kills); // Hammer, Scavenger, Adrenal
	bool bLastShotPredictedKill = false; // joiner: the kill marker already showed for the last shot
	UPROPERTY() TObjectPtr<USceneComponent> WorldGunRoot;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> WorldGun;
	FName WorldGunId;
	UPROPERTY() TObjectPtr<USceneComponent> ModelRoot;
	UPROPERTY() TMap<FName, TObjectPtr<UStaticMeshComponent>> Parts;
	TSet<FName> AdsHiddenParts; // mod parts hidden while aiming
public:
	// The targets you have marked (picked up under the reticle, or inside the marker range while
	// aimed in; dropped on leaving the range) and the world time each was picked up. The HUD keeps
	// it as it draws; AWWOCharacter::UpdateTeamTint flashes a player's outline from that time.
	mutable TMap<TWeakObjectPtr<const AActor>, double> Marked;
private:
	TMap<FName, FVector> FlareSizes; // the muzzle flash flares' built sizes (each shot varies them)
	FVector TrailStart() const;      // where your own shot's trail starts, to look like it leaves the muzzle
	TWeakObjectPtr<AActor> Highlighted; // the enemy tinted for being under the reticle
	float BuiltSightHeight = 0.f; // the sight height the model was built with
	float TopBelowSight = 0.f;    // cm from the sight line down to the gun's top, as built (0: no mesh model)
	FVector ModelShift = FVector::ZeroVector; // from it: every part but the sight dot
	// Hands on the gun: the player's own character model (the mannequin without one), only its arms
	// shown, each solved every frame to reach where a first-person rifle pose holds the gun.
	UPROPERTY() TObjectPtr<UPoseableMeshComponent> HandsMesh;
	UPROPERTY() TObjectPtr<USkeletalMesh> HandsAsset;
	UPROPERTY() TObjectPtr<UAnimSequence> HandsPose;
	struct FHandsArm
	{
		int32 Upper = INDEX_NONE, Lower = INDEX_NONE, Hand = INDEX_NONE;
		float UpperLength = 0.f, LowerLength = 0.f;
		FQuat UpperRef, LowerRef, HandRef;   // reference pose, camera space
		FVector UpperDir, LowerDir;          // reference pose bone directions, camera space
		FQuat HandAlign;                     // the model's reference forearm frame onto the clip skeleton's
		FTransform ClipShoulder, ClipElbow, ClipHand; // the pose clip, camera space
		FQuat ClipHandRef;                   // the clip skeleton's reference hand, camera space
		FQuat RigidRef;                      // a rigid hand's frame in the reference pose (x fingers, z out of the palm), camera space
	};
	FHandsArm HandsArms[2]; // right, left
	// A first-person action clip the arms leave the gun to play (the punch, the grenade throw).
	struct FHandsClip
	{
		FTransform ToCamera;      // its arms mesh on its demo camera
		float Key = 0.f;          // seconds into it: the fist lands, the grenade leaves the hand
		float Start = 0.f;        // nothing before this is played
		FQuat HandRef[2];         // its skeleton's reference hands, camera space
		FQuat HandAlign[2];       // the model's reference forearm frame onto its skeleton's
		bool bMirror = false;     // played left for right (the throw is authored right-handed; the right hand keeps the gun)
		FVector Offset = FVector::ZeroVector; // the whole arm (shoulder, elbow and hand alike) moved by this, camera space cm: forward, right, up
		float Roll = 0.f;         // the whole arm turned as one piece about the view's forward axis, degrees: + lifts its right side (the right elbow up, the fist down)
		bool bOnReticle = false;  // the whole arm is also moved sideways and up or down so the knuckles are on the reticle at the key moment
		float Lean = 0.f;         // how much of the reach added by Reach the shoulder goes with (leaning into it), 0 to 1
		bool bFist = false;       // the hand is held in line with the forearm, back of the hand up (a rigid-handed model's punch)
		float Reach = 1.f;        // how far the hand travels forward and back from where the clip starts, against the clip's own travel
		int32 Arms = 3;           // which arms play it: bit 0 the right, bit 1 the left; the others stay on the gun
		bool bValid = false;
	};
	UPROPERTY() TObjectPtr<UAnimSequence> HandsPunchAnim;
	UPROPERTY() TObjectPtr<UAnimSequence> HandsThrowAnim;
	FHandsClip HandsPunch, HandsThrow;
	void BuildHandsClip(FHandsClip& Clip, const UAnimSequence* Anim, const FTransform& ToCamera, float Start, float Key);
	int32 HandsAction = 0;      // 0 none, 1 punch, 2 throw: the clip being played or faded out
	float HandsActionTime = 0.f;
	float HandsActionWeight = 0.f;
	float HandsDelta = 0.f;
	float HandsHitTime = 0.f; // seconds since this swing hit
	UPROPERTY(EditAnywhere, Category = "Hands") float HandsHitHold = 0.06f; // how long the fist stays where it hit before pulling back (s)
	float HandsCurl[2] = { 0.f, 0.f }; // how far each hand's fingers are closed past the model's own resting curl (0 to 1)
	TArray<FTransform> HandsRefLocal;
	TArray<int32> HandsOwner; // per bone: 0 / 1 = hangs off that arm's hand, -1 = hidden, -2 = an arm bone
	FTransform HandsClipGun; // the pose clip's rifle, camera space
	bool bHandsRigid = false;
	FVector HandsGrip, HandsFore, HandsMag; // this gun's grip, the underside of its fore-end and its magazine, gun space (cm)
	bool bHandsPistol = false;
	void BuildHands(USkeletalMesh* Asset);
	void UpdateHands();
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ThrowBall; // the grenade shown in the throwing hand
	FVector MagRest = FVector::ZeroVector; // Godot space
	FDelegateHandle LoadoutHandle;
	TWeakObjectPtr<class UWWOLoadoutSubsystem> BoundLoadout;

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
	float FireBuffer = 0.f; // s left in which a semi-auto pull that came too early still fires (AWWOCharacter::FireBufferTime)
	float FireCooldown = 0.f;
	public:
	// Where the first-person gun's sight point is seen on screen: X and Y from the screen's centre in
	// half screen widths (+ right, + down), and how far the gun is seen rolled (radians, + clockwise).
	// The HUD draws the iron-sight brackets there, so they ride on the gun. False: no gun to see.
	bool SightOnScreen(FVector2D& OutAt, float& OutRoll) const;
	float KickAmount() const { return Kick; } // 0-1, for the HUD's iron-sight marks
	private:
	float Kick = 0.f;       // the gun model's kick now, 1 at the shot (looks only)
	float KickAge = 1000.f; // s since the shot that started it
	float StrafeLean = 0.f; // -1 to 1: how hard you are moving left or right, eased
	float FlashTime = 0.f;
	bool bFlashVisible = false;
	float PunchAge = 1e9f, PunchFovValue = 0.f, PunchRollValue = 0.f, PunchLength = 0.1f;
	bool bMeleeLanded = false;
	bool bMeleeConnected = false; // this swing hit something: the first-person hook stops there instead of swinging through
	TWeakObjectPtr<AActor> MeleeTarget;

	float M(FName Stat, float Base) const;
	bool Pressed(int32 Action) const;
	bool JustPressed(int32 Action) const;

	void BuildModel();
	void BuildThrowBall();
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
	float ThrowPoseLength() const { return ThrowRelease + ThrowFollow + 0.1f; }
	float PunchAmount() const;
	UStaticMeshComponent* Part(FName Name) const;
};
