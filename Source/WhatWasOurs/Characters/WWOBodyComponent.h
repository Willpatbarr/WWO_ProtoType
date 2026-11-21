#pragma once

// Sets up and drives a mannequin body (Godot: characters/character_model.gd). Shared by the
// player, enemies, allies and the character menu preview.
// - The Unreal mannequin (SKM_Manny_Simple) played by UWWOBodyAnimInstance: the Godot body
//   animations, baked onto the mannequin (/Game/WWO/Anims/UAL, Tools/bake_ual_anims.py), called
//   by their Godot names. Play() is CharacterModel.play(): one clip with a crossfade and a speed.
//   Each owner picks its clip every frame from what it's doing, as its Godot script does.
// - Colors: the mannequin's paint tint, per region (head, arms, core, legs) like Godot's
//   region_color.gdshader (M_WWO_Body), and a white hit flash. A body following the loadout
//   colors each region with its equipped frame part (show_frame_parts).
// - Flora grafts (show_flora): VoltVine's vines, leaves and crystal pods, IronBark's bark plates
//   and seed pods, VeilSpore's mycelium and shelf mushrooms, built as procedural meshes by
//   FWWOGraftBuilder, pinned to the same bones as in Godot (WWOBodyGrafts.cpp), placed from the
//   standing pose (Idle's first frame) as in Godot.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WWOBodyComponent.generated.h"

class USkeletalMeshComponent;
class USkeletalMesh;
class UStaticMeshComponent;
class UPrimitiveComponent;
class UStaticMesh;
class FWWOGraftBuilder;
struct FWWOGraftFrame;
struct FWWOShellPoint;
struct FWWOShellPath;
struct FWWOShellAnchorSet;
struct FWWOVineJoints;
struct FWWOAnchorGraftResult;
class UMaterialInstanceDynamic;
class UAnimSequence;
class UWWOBodyAnimInstance;

// Online: the host's animation commands for a body, replicated so every other machine plays the
// same (enemies' and allies' AI and other players' movement only run on the host). Quantized:
// speeds and weights to 1/50.
USTRUCT()
struct FWWOBodyNet
{
	GENERATED_BODY()
	UPROPERTY() FName Anim;
	UPROPERTY() uint8 Speed = 50;
	UPROPERTY() FName Queued;
	UPROPERTY() FName Upper;
	UPROPERTY() uint8 UpperWeight = 0;
	UPROPERTY() FName MixA;
	UPROPERTY() FName MixB;
	UPROPERTY() uint8 MixAlpha = 0;
	UPROPERTY() uint8 MixSpeedB = 50;
	UPROPERTY() int8 HunchSpine = 0;
	UPROPERTY() int8 HunchHead = 0;
	UPROPERTY() FName Shot;
	UPROPERTY() uint8 ShotSerial = 0;
	UPROPERTY() uint8 ClearSerial = 0;
};

UCLASS(ClassGroup = (WWO))
class WHATWASOURS_API UWWOBodyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWWOBodyComponent();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	FLinearColor BodyColor = FLinearColor(0.55f, 0.15f, 0.13f);
	FLinearColor JointColor = FLinearColor(0.16f, 0.16f, 0.18f);
	float ModelScale = 1.f;
	bool bShadowOnly = false; // the player's own body: only its shadow shows in first person
	TWeakObjectPtr<USkeletalMeshComponent> MeshOverride; // the preview uses its own mesh
	// A character model (Config/CharacterModels.json) to show instead of the mannequin: set before
	// BeginPlay (the enemies), or picked from the loadout's flora by FollowLoadout.
	FName ModelKey;

	// Region colors (head, arms, core, legs; Godot's REGIONS order). Until set, every region is BodyColor.
	static constexpr int32 NumRegions = 4;
	FLinearColor RegionColors[NumRegions];
	bool bRegionColors = false;

	USkeletalMeshComponent* GetMesh() const;
	void ApplyColors();
	void SetFlash(float Amount);

	// CharacterModel.play(name, speed, blend): a different animation crossfades in, the same one
	// only takes the new speed. Names are the Godot ones ("Idle", "Crouch_Fwd", "Death01"...).
	void Play(FName Anim, float Speed = 1.f, float Blend = 0.2f);
	void Queue(FName Anim); // AnimationPlayer.queue: plays when the current one ends
	FName CurrentAnim() const; // None once a one-off animation has ended (it holds its last frame)
	float CurrentAnimTime() const;
	float CurrentAnimLength() const;
	float AnimLength(FName Anim) const;
	// Snaps the skeleton into an animation frame now (for measuring where to put things).
	void ForcePose(FName Anim, float At = 0.f);
	// The Soldier's upper-body layers: the pistol aim by weight, and a hit flinch on top.
	void SetUpperBody(FName Anim, float Weight);
	void PlayUpperShot(FName Anim, float FadeIn = 0.05f, float FadeOut = 0.2f);
	// The idle / walk Blend2 of the Soldier's tree, and switching the tree off (it died).
	void SetMix(FName A, FName B, float Alpha, float SpeedB);
	void ClearLayers();
	// The Swarm's hunch (HunchModifier): degrees over the spine, and lifted back over the neck and head.
	void SetHunch(float SpineDegrees, float HeadDegrees);
	// As posed now, on any machine (a player's gun in hand follows these): how far the upper-body
	// layer is in, 0-1, and the spine's bend in degrees.
	float UpperWeightNow() const;
	float HunchSpineNow() const;
	UWWOBodyAnimInstance* GetAnimInstance() const;
	void FollowLoadout(bool bShadow); // rebuild the grafts whenever the loadout changes
	void RebuildGrafts();
	// Godot's set_shadow_only: only the shadow shows in first person; off for the ult's third
	// person, where the whole body and its grafts show.
	void SetShadowOnly(bool bOn);

	// A box pinned to a bone at a world point (measured now), e.g. Tank armor plates.
	UStaticMeshComponent* AttachBox(FName Bone, const FVector& WorldCenter, const FVector& SizeM, const FLinearColor& Color, float Glow = 0.f);

	// Shows a character model (a key in Config/CharacterModels.json) instead of the mannequin, which
	// stays as the invisible animation driver the model copies its pose from. None: the mannequin.
	void SetModel(FName Key);
	// Loads a model's mesh and retargeter and finishes compiling the mesh now, so the first body to wear
	// it mid-match doesn't stall the game while it loads (FootRace's later rooms and boss: a 0.7 s freeze).
	// What it loaded goes in Out: the caller keeps those referenced until they're used (or GC unloads them).
	static void Preload(FName Key, TArray<UObject*>& Out);
	// The same for animation clips by name (as FindAnim looks them up).
	static void PreloadAnims(TConstArrayView<FName> Names, TArray<UObject*>& Out);
	USkeletalMeshComponent* GetModel() const { return Model; }
	// Shell parts (a model with "shells" in Config/CharacterModels.json, the robot frame): the model is
	// the robot's bare skeleton (joints, struts, hands: SK_RobotSkeleton) and a head, core, arms and legs
	// mesh play on its pose, each from any shell (/Game/WWO/Characters/RobotFrame/Shells/SK_Shell<n>_<Slot>,
	// Tools/import_shell_parts.py; shell 0 is the base robot's own armour). Whatever a part leaves
	// uncovered shows the skeleton.
	enum EShellSlot { ShellHead, ShellCore, ShellArms, ShellLegs, NumShellSlots };
	int32 ShellPick[NumShellSlots] = { 0, 0, 0, 0 };
	bool SetShell(int32 Head, int32 Core, int32 Arms, int32 Legs); // true: the parts were rebuilt
	// Each slot's colours (FWWOEquipped::ShellColors: primary, secondary, glow; alpha 0 keeps the part's own), from the loadout
	// (ShellFromLoadout). They're painted on every part this body draws (PaintShellPart), and by the first-person hands on
	// the arms part over them, which repaint when ShellPaintSerial changes.
	static constexpr int32 NumShellRoles = 3;
	FColor ShellPaint[NumShellSlots][NumShellRoles] = {};
	uint32 ShellPaintSerial = 0;
	void SetShellPaint(const FColor (&Paint)[NumShellSlots][NumShellRoles]);
	// Puts a slot's colours on a part (any skinned mesh showing a SK_Shell<n>_<Slot>): dynamic instances of its own materials
	// with the colours swapped in (primary on Primary / PrimaryLight / Armour, secondary on Secondary / Trim, glow on Glow /
	// Visor; Frame and Joint keep theirs); a default colour puts the part's own material back.
	void PaintShellPart(USkeletalMeshComponent* Part, int32 Slot) const;
	// Each slot wears the look of the frame part equipped in it (Config/CharacterModels.json "shell_looks":
	// frame part id -> shell number; a part not listed there looks like the base robot).
	void ShellFromLoadout(const class UWWOLoadoutSubsystem* Loadout);
	// The mesh a slot shows now (the debug override, else this body's pick); null without shells.
	USkeletalMesh* ShellMesh(int32 Slot) const;
	bool HasShells() const { return bShells && Model != nullptr; }
	// Debug: every body's shell, whatever it picked (wwo.Shell, -WWOShell=); -1 leaves a slot alone.
	static int32 ShellOverride[NumShellSlots];
	static void SetShellOverride(UWorld* World, const int32 Picks[NumShellSlots]);
	// Graft anchors (Characters/WWOShellAnchors.h, WWOBodyAnchors.cpp), while HasShells(): the named spots and
	// lines of the parts worn now (they follow ShellMesh, so wwo.Shell and a part swap move them), and the
	// bare skeleton's seams. An anchor name is looked up in each slot's worn part, then the seams.
	const FWWOShellPoint* FindAnchorPoint(FName Name, bool* bOutSeam = nullptr) const;
	// Where it is now (X out of the surface, Z its 'up'), and its size in cm. False if no worn part has it.
	bool FindAnchor(FName Name, FTransform& OutWorld, float* OutSize = nullptr) const;
	const FWWOShellPath* FindPath(FName Name) const;
	// A path's points where they are now. bBridgeSeams: a path that starts or ends at a seam begins or
	// ends on that seam's point (so a Core path and the Arms path after it meet); a closed one ends where it began.
	bool PathWorld(FName Name, TArray<FVector>& Out, bool bBridgeSeams = true) const;
	FTransform PointWorld(const FWWOShellPoint& Point) const; // an anchor or path point, now
	// The anchor set of the part worn in a slot (NumShellSlots: the skeleton's seams); null without one.
	const FWWOShellAnchorSet* WornAnchors(int32 Slot) const;
	// Anchor grafts: the flora's pieces placed from Config/GraftRecipes.json on the worn parts' anchors, instead
	// of the fitted model grafts. Per model "anchor_grafts" in Config/CharacterModels.json, or for every robot
	// with wwo.AnchorGrafts 0|1 (-1: per model) / -WWOAnchorGrafts. Rebuilds every body following its loadout.
	static void RebuildAllGrafts(UWorld* World);
	// The model's hands are rigid (Config/CharacterModels.json hand_fingers / hand_palm, right hand, mesh space).
	bool bRigidHands = false;
	FVector HandFingers = FVector::ZeroVector, HandPalm = FVector::ZeroVector;
	// A faint tint over the visible body (the model, else the mannequin): the enemy under the
	// reticle. 0 takes it off.
	void SetHighlight(float Alpha);
	// PvP: a tint over the visible body while a player is near the watcher's reticle: Fill faintly
	// all over, Rim strongly around the outline. Both 0 takes it off. It replaces the reticle highlight.
	void SetTeamTint(const FLinearColor& Color, float Fill, float Rim);
	// A soft glow around both hands (an ability that charges the fists); Alpha 0 takes it off.
	void SetHandGlow(const FLinearColor& Color, float Alpha);
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> HandGlows;

	UAnimSequence* FindAnim(FName Anim) const;
	static bool IsLooping(FName Anim);

private:
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
	UPROPERTY() mutable TMap<FName, TObjectPtr<UAnimSequence>> Anims;
	mutable TMap<FName, FTransform> IdlePose; // component space, Idle's first frame
	UPROPERTY() TArray<TObjectPtr<UPrimitiveComponent>> Grafts; // the flora grafts, rebuilt with the loadout
	UPROPERTY() TArray<TObjectPtr<UPrimitiveComponent>> Pinned; // AttachBox's (kept)
	FDelegateHandle LoadoutHandle;
	bool bSetUp = false;
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> Model; // the character model, if any
	// Online: the host's commands (see FWWOBodyNet). Not sent to the player who owns this body (a
	// player's own machine animates their body itself).
	UPROPERTY(ReplicatedUsing = OnRep_Net) FWWOBodyNet Net;
	FWWOBodyNet Applied; // what this machine has played of Net
	UFUNCTION() void OnRep_Net();
	bool Replicating() const; // the host, online: record commands into Net
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HighlightMat; // M_WWO_Highlight, made on first use
	float HighlightAlpha = 0.f;                 // the reticle highlight (SetHighlight)
	FLinearColor TeamTint = FLinearColor::Black; // SetTeamTint
	float TeamTintAlpha = 0.f, TeamTintRim = 0.f;
	void RefreshOverlay(); // the overlay material on the visible body, from the two above
	FName ActiveModel;
	bool bFollowsLoadout = false;
	bool bModelGrafts = false;  // keep the flora grafts on the model
public:
	bool bFullDetailGrafts = false; // the grafts never drop detail with distance (the menu's preview)
private:
	void UpdateGraftDetail();     // sets the grafts' level of detail from the camera's distance (on a timer)
	FTimerHandle GraftDetailTimer;
	int32 GraftDetail = 0;        // 0 full, 1 middle, 2 lowest
	bool bModelPinned = false;  // keep AttachBox's plain boxes on the model (glowing ones always stay)
	TSet<TObjectPtr<UPrimitiveComponent>> GlowingPinned;
	void UpdateModelExtras(); // shows or hides the mannequin, grafts and boxes for the model
	void ApplyShadowOnly(UPrimitiveComponent* C) const;
	void ApplyModelView(); // whole for everyone else; from the hide bone down in your own first-person view
	FName FpHideBone;
	TArray<FName> FpHideBones;
	float FpBodyBack = 0.f;
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> FpBody; // your own legs and feet in first person
	UPROPERTY() TObjectPtr<class UIKRetargeter> ModelRetargeter;
	bool bShells = false;
	UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> ShellParts;   // on the model
	UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> FpShellParts; // on your own first-person legs
	TArray<int32> ShellPartSlots, FpShellPartSlots; // the slot of each of those
	void RebuildShell();
	USkeletalMeshComponent* AddShellPart(USkeletalMeshComponent* Leader, USkeletalMesh* Asset);
	FName WornParts[NumShellSlots]; // the part each slot shows (RebuildShell), whose anchors FindAnchor reads
	// Anchor grafts (WWOBodyAnchors.cpp): what RebuildGrafts chose, kept so a part swap (RebuildShell) can
	// place them again on the new part's anchors.
	bool bModelAnchorGrafts = false; // the model's "anchor_grafts"
	bool bAnchorGraftsLive = false;
	FName AnchorFlora, AnchorGrenade;
	TArray<FName> AnchorIds;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> AnchorParts; // (also in Grafts)
	TSharedPtr<FWWOVineJoints> VineJoints; // VoltVine's vines where they pass between bones, posed every frame (WWOVineGraft.h)
	bool UseAnchorGrafts(FName FloraId) const;
	// Asks for them (built off the game thread, or shared with a robot wearing the same: FWWOAnchorGraftCache); the ones
	// shown stay until ApplyAnchorGrafts puts the new ones on. ClearAnchorGrafts takes them off and drops a pending ask.
	void BuildAnchorGrafts();
	void ClearAnchorGrafts();
	void DestroyAnchorParts();
	friend class FWWOAnchorGraftCache;
	void ApplyAnchorGrafts(const FWWOAnchorGraftResult& Result, int32 Request, const FString& How);
	int32 AnchorRequest = 0;   // the latest ask; a build that arrives for an older one is dropped
	double AnchorAskedMs = 0.0; // (the log: what asking cost this frame)
	// Their detail level (0 closest .. 2 farthest): the robot's own LOD, which the shell parts follow too, checked after
	// every pose update (UpdateAnchorLevel); the menu's preview always 0; wwo.AnchorGraftLevel forces one.
	int32 AnchorLevel = 0, AnchorLevelSwitches = 0;
	int32 AnchorLevelNow() const;
	void UpdateAnchorLevel();
	TWeakObjectPtr<USkeletalMeshComponent> AnchorLodModel;
	FDelegateHandle AnchorLodHandle;
	// The robot's own LOD (SK_RobotSkeleton's, which its shell parts follow as their leader, and its anchor grafts' level):
	// picked here from its screen size with hysteresis and forced on it (UpdateRobotLod, after every pose update), so near a
	// switch distance it can't flip back within RobotLodHold seconds or a RobotLodMargin of the switch's screen size.
	int32 RobotLod = 0;
	double RobotLodAt = 0.0; // when it last changed (FPlatformTime::Seconds)
	FDelegateHandle RobotLodHandle;
	void UpdateRobotLod();
	void StopRobotLod();

	FLinearColor Region(int32 Index) const { return bRegionColors ? RegionColors[Index] : BodyColor; }
	void SetTints(const FLinearColor& Toward, float Amount);
	// Pins a component to a bone at a world transform in the standing pose; it then follows the bone.
	void Pin(UPrimitiveComponent* C, FName Bone, const FTransform& World, bool bTrack);
	UStaticMeshComponent* AddShape(FName Bone, const FVector& World, UStaticMesh* Mesh, const FVector& ScaleM, const FLinearColor& Color,
		float Glow, bool bTrack);
	FVector BonePoint(FName Bone) const; // in the standing pose, world
	FTransform IdleBone(FName Bone) const; // world

	// Flora grafts (WWOBodyGrafts.cpp). Bones are named as in Godot (the UAL skeleton).
	static FName UnrealBone(FName GodotBone);
	FVector GodotBonePoint(FName GodotBone) const { return BonePoint(UnrealBone(GodotBone)); }
	void AttachBuilt(FName GodotBone, const FWWOGraftBuilder& Builder);
	void AttachBall(FName GodotBone, const FVector& At, float RadiusM, const FLinearColor& Color);
	void AttachModel(FName GodotBone, const TCHAR* Mesh, const FVector& At, float AngleRad, float Scale, const FWWOGraftFrame& F);
	void VoltGrafts(const FWWOGraftFrame& F, const FLinearColor& PodColor);
	// A flora on a model that has its own grafts (the robot frame: SK_VV_* for VoltVine, SK_VS_* for
	// VeilSpore in its Grafts folder, built round its body, Tools/import_robot_grafts.py): those,
	// playing the model's pose. False when the model has none for this flora (the procedural grafts
	// are used instead).
	bool ModelGrafts(FName FloraId, const FWWOGraftFrame& F);
	USkeletalMeshComponent* AddModelGraft(const FString& Name);
	void SporeGrafts(const FWWOGraftFrame& F);
	void BarkGrafts(const FWWOGraftFrame& F);
};
