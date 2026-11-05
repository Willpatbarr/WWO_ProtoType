#pragma once

// Sets up and drives a mannequin body (Godot: characters/character_model.gd). Shared by the
// player, enemies, allies and the character menu preview.
// - The Unreal mannequin (SKM_Manny_Simple) with the template locomotion AnimBP
//   (ABP_Unarmed, or the pistol / rifle AnimBPs for armed characters).
// - Colors via the mannequin material's "Paint Tint", and a white hit flash.
// - One-off actions (attacks, the ult cast) as montages on the AnimBP's DefaultSlot.
// - Death plays a death animation on its own.
// - Flora grafts: gray-box markers pinned to bones that show the equipped flora's
//   grenade and abilities (simplified versions of the Godot vine/spore/bark grafts).

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WWOBodyComponent.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UAnimSequenceBase;

UENUM()
enum class EWWOAnimSet : uint8 { Unarmed, Pistol, Rifle };

UENUM()
enum class EWWOBodyAction : uint8 { None, UltCharge, UltCast, Bite, Slam, Lob, Leap };

UCLASS(ClassGroup = (WWO))
class WHATWASOURS_API UWWOBodyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWWOBodyComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	FLinearColor BodyColor = FLinearColor(0.55f, 0.15f, 0.13f);
	FLinearColor JointColor = FLinearColor(0.16f, 0.16f, 0.18f);
	EWWOAnimSet AnimSet = EWWOAnimSet::Unarmed;
	float ModelScale = 1.f;
	bool bShadowOnly = false; // the player's own body: only its shadow shows in first person
	TWeakObjectPtr<USkeletalMeshComponent> MeshOverride; // the preview uses its own mesh

	USkeletalMeshComponent* GetMesh() const;
	void ApplyColors();
	void SetFlash(float Amount);
	void PlayAction(EWWOBodyAction Action, float Rate = 1.f);
	void PlayDeath(bool bKnockedBack = false);
	void Revive();
	void FollowLoadout(bool bShadow); // rebuild the grafts whenever the loadout changes
	void RebuildGrafts();
	// Godot's set_shadow_only: only the shadow shows in first person; off for the ult's third
	// person, where the whole body and its grafts show.
	void SetShadowOnly(bool bOn);

	// A box pinned to a bone at a world point (measured now), e.g. Tank armor plates.
	UStaticMeshComponent* AttachBox(FName Bone, const FVector& WorldCenter, const FVector& SizeM, const FLinearColor& Color, float Glow = 0.f);

	static UAnimSequenceBase* LoadAnim(const TCHAR* Path);

private:
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Grafts;
	FDelegateHandle LoadoutHandle;
	bool bSetUp = false;

	UStaticMeshComponent* AddGraft(FName Bone, const FVector& World, UStaticMesh* Mesh, const FVector& ScaleM, const FLinearColor& Color,
		float Glow = 0.f, const FRotator& WorldRot = FRotator::ZeroRotator);
	FVector BonePoint(FName Bone) const;
};
