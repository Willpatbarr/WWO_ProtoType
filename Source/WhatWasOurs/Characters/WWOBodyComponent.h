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
class UStaticMeshComponent;
class UPrimitiveComponent;
class UStaticMesh;
class FWWOGraftBuilder;
struct FWWOGraftFrame;
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
