#pragma once

// The anim instance of a character model (Config/CharacterModels.json): it copies the pose of the
// body's mannequin, which stays as an invisible animation driver, through an IK Retargeter
// (FAnimNode_RetargetPoseFromMesh run straight from the proxy, no AnimBP). So every animation,
// layer and hunch the mannequin plays shows on the model. Modeled on IKRig's editor-only
// UIKRetargetAnimInstance.

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "WWOModelAnimInstance.generated.h"

class UIKRetargeter;

USTRUCT()
struct FWWOModelAnimProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FWWOModelAnimProxy() = default;
	FWWOModelAnimProxy(UAnimInstance* InAnimInstance, FAnimNode_RetargetPoseFromMesh* InNode)
		: FAnimInstanceProxy(InAnimInstance), RetargetNode(InNode) {}

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void CacheBones() override;
	virtual bool Evaluate(FPoseContext& Output) override;
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;
	virtual FAnimNode_Base* GetCustomRootNode() override { return RetargetNode; }
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override { OutNodes.Add(RetargetNode); }

	FAnimNode_RetargetPoseFromMesh* RetargetNode = nullptr;
};

UCLASS(Transient, NotBlueprintable)
class WHATWASOURS_API UWWOModelAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// Follow Source (the body's mannequin) through Retargeter (RTG_Manny_<key>).
	void Configure(UIKRetargeter* Retargeter, USkeletalMeshComponent* Source);

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FWWOModelAnimProxy(this, &RetargetNode); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete static_cast<FWWOModelAnimProxy*>(InProxy); }

	UPROPERTY(Transient) FAnimNode_RetargetPoseFromMesh RetargetNode;
};
