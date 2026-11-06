#include "Characters/WWOModelAnimInstance.h"
#include "Retargeter/IKRetargeter.h"
#include "Components/SkeletalMeshComponent.h"

void FWWOModelAnimProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);
	FAnimationInitializeContext Context(this);
	RetargetNode->Initialize_AnyThread(Context);
}

void FWWOModelAnimProxy::CacheBones()
{
	if (bBoneCachesInvalidated)
	{
		FAnimationCacheBonesContext Context(this);
		RetargetNode->CacheBones_AnyThread(Context);
		bBoneCachesInvalidated = false;
	}
}

bool FWWOModelAnimProxy::Evaluate(FPoseContext& Output)
{
	RetargetNode->Evaluate_AnyThread(Output);
	return true;
}

void FWWOModelAnimProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	RetargetNode->Update_AnyThread(InContext);
}

void UWWOModelAnimInstance::Configure(UIKRetargeter* Retargeter, USkeletalMeshComponent* Source)
{
	RetargetNode.IKRetargeterAsset = Retargeter;
	RetargetNode.RetargetFrom = ERetargetSourceMode::CustomSkeletalMeshComponent;
	RetargetNode.SourceMeshComponent = Source;
	RetargetNode.bSuppressWarnings = true;
	if (FIKRetargetProcessor* Processor = RetargetNode.GetRetargetProcessor()) Processor->SetNeedsInitialized();
}
