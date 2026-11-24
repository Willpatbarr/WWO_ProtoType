#include "Characters/WWOBodyAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimNodeBase.h"
#include "BonePose.h"

namespace
{
	// soldier.gd's LOWER_BODY (bone name starts), plus the mannequin's ankle helpers and IK bones.
	const TCHAR* LowerBody[] = { TEXT("root"), TEXT("pelvis"), TEXT("thigh"), TEXT("calf"), TEXT("foot"), TEXT("ball"), TEXT("ankle"), TEXT("ik_") };
	// HunchModifier bends spine_01..03 and lifts neck_01 and Head (the UAL rig); the mannequin
	// spreads the same angles over its five spine bones and its two neck bones and head.
	const TCHAR* Spine[] = { TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03"), TEXT("spine_04"), TEXT("spine_05") };
	const TCHAR* Neck[] = { TEXT("neck_01"), TEXT("neck_02"), TEXT("head") };

	float ClipLength(const UAnimSequence* Seq) { return Seq ? Seq->GetPlayLength() : 0.f; }
}

// --- Instance (game thread) ------------------------------------------------------------------

FAnimInstanceProxy* UWWOBodyAnimInstance::CreateAnimInstanceProxy()
{
	return new FWWOBodyAnimProxy(this);
}

void UWWOBodyAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FWWOBodyAnimProxy*>(InProxy);
}

void UWWOBodyAnimInstance::Play(UAnimSequence* Seq, bool bLoop, float InSpeed, float Blend)
{
	Speed = InSpeed;
	if (!Seq || Seq == GetCurrent()) return;
	Queued = nullptr;
	const float Rate = Blend > 0.f ? 1.f / Blend : 0.f;
	for (FWWOBodyClip& C : Clips)
	{
		C.FadeRate = -Rate;
		if (Rate <= 0.f) C.Weight = 0.f;
	}
	FWWOBodyClip& New = Clips.AddDefaulted_GetRef();
	New.Seq = Seq;
	New.bLoop = bLoop;
	New.Time = 0.f;
	New.Weight = Rate > 0.f && Clips.Num() > 1 ? 0.f : 1.f;
	New.FadeRate = Rate > 0.f ? Rate : 0.f;
	Clips.RemoveAll([](const FWWOBodyClip& C) { return C.Weight <= 0.f && C.FadeRate <= 0.f; });
}

void UWWOBodyAnimInstance::Queue(UAnimSequence* Seq, bool bLoop)
{
	Queued = Seq;
	bQueuedLoop = bLoop;
}

UAnimSequence* UWWOBodyAnimInstance::GetCurrent() const
{
	if (Clips.Num() == 0) return nullptr;
	const FWWOBodyClip& C = Clips.Last();
	return (C.bLoop || C.Time < ClipLength(C.Seq)) ? C.Seq.Get() : nullptr;
}

float UWWOBodyAnimInstance::GetCurrentTime() const
{
	return Clips.Num() ? Clips.Last().Time : 0.f;
}

float UWWOBodyAnimInstance::GetCurrentLength() const
{
	return Clips.Num() ? ClipLength(Clips.Last().Seq) : 0.f;
}

void UWWOBodyAnimInstance::Stop()
{
	bMix = false;
	Clips.Reset();
	Queued = nullptr;
	Shot = FWWOBodyClip();
}

void UWWOBodyAnimInstance::SetMix(UAnimSequence* A, UAnimSequence* B, float Alpha, float SpeedB)
{
	for (FWWOBodyClip* C : { &MixA, &MixB })
	{
		UAnimSequence* Seq = C == &MixA ? A : B;
		if (bMix && C->Seq == Seq) continue;
		*C = FWWOBodyClip();
		C->Seq = Seq;
		C->Weight = 1.f;
		C->bLoop = true;
	}
	bMix = true;
	MixAlpha = FMath::Clamp(Alpha, 0.f, 1.f);
	MixSpeedB = SpeedB;
}

void UWWOBodyAnimInstance::ClearLayers()
{
	if (bMix)
	{
		Clips.Reset();
		Queued = nullptr;
		MixA.Weight = 1.f - MixAlpha;
		MixB.Weight = MixAlpha;
		Clips.Add(MixA);
		Clips.Add(MixB);
		bMix = false;
	}
	Upper = FWWOBodyClip();
	Shot = FWWOBodyClip();
}

void UWWOBodyAnimInstance::SetUpperBody(UAnimSequence* Seq, float Weight)
{
	if (Upper.Seq != Seq) Upper.Time = 0.f;
	Upper.Seq = Seq;
	Upper.bLoop = true;
	Upper.Weight = FMath::Clamp(Weight, 0.f, 1.f);
}

void UWWOBodyAnimInstance::PlayUpperShot(UAnimSequence* Seq, float FadeIn, float FadeOut)
{
	Shot.Seq = Seq;
	Shot.Time = 0.f;
	Shot.bLoop = false;
	ShotFadeIn = FadeIn;
	ShotFadeOut = FadeOut;
}

void UWWOBodyAnimInstance::Advance(float DeltaSeconds)
{
	const float Step = DeltaSeconds * Speed; // speed_scale moves every clip, fading ones too
	for (FWWOBodyClip& C : Clips)
	{
		const float Length = ClipLength(C.Seq);
		C.Time += Step;
		if (C.bLoop && Length > 0.f) C.Time = FMath::Fmod(C.Time, Length);
		else C.Time = FMath::Min(C.Time, Length);
		C.Weight = FMath::Clamp(C.Weight + C.FadeRate * DeltaSeconds, 0.f, 1.f);
	}
	Clips.RemoveAll([](const FWWOBodyClip& C) { return C.Weight <= 0.f && C.FadeRate <= 0.f; });
	if (Queued && Clips.Num() && !GetCurrent())
	{
		UAnimSequence* Next = Queued;
		Queued = nullptr;
		Play(Next, bQueuedLoop, Speed, 0.f);
	}
	if (bMix)
	{
		for (FWWOBodyClip* C : { &MixA, &MixB })
		{
			const float Length = ClipLength(C->Seq);
			C->Time = Length > 0.f ? FMath::Fmod(C->Time + DeltaSeconds * (C == &MixB ? MixSpeedB : 1.f), Length) : 0.f;
		}
	}
	if (Upper.Seq)
	{
		const float Length = ClipLength(Upper.Seq);
		Upper.Time = Length > 0.f ? FMath::Fmod(Upper.Time + DeltaSeconds, Length) : 0.f;
	}
	if (Shot.Seq)
	{
		Shot.Time += DeltaSeconds;
		const float Length = ClipLength(Shot.Seq);
		if (Shot.Time >= Length) Shot = FWWOBodyClip();
		else Shot.Weight = FMath::Min3(1.f, Shot.Time / FMath::Max(ShotFadeIn, 1e-3f), (Length - Shot.Time) / FMath::Max(ShotFadeOut, 1e-3f));
	}
}

void UWWOBodyAnimInstance::Snapshot(FWWOBodyAnimProxy& Proxy) const
{
	Proxy.Base.Reset();
	if (bMix)
	{
		Proxy.Base.Add({ MixA.Seq.Get(), MixA.Time, 1.f - MixAlpha, true });
		Proxy.Base.Add({ MixB.Seq.Get(), MixB.Time, MixAlpha, true });
	}
	else
	{
		for (const FWWOBodyClip& C : Clips) Proxy.Base.Add({ C.Seq.Get(), C.Time, C.Weight, C.bLoop });
	}
	Proxy.Upper = { Upper.Seq.Get(), Upper.Time, Upper.Seq ? Upper.Weight : 0.f, true };
	Proxy.Shot = { Shot.Seq.Get(), Shot.Time, Shot.Seq ? Shot.Weight : 0.f, false };
	Proxy.HunchSpine = HunchSpine + BaseSpine;
	Proxy.HunchHead = HunchHead + BaseHead;
}

// --- Proxy (worker thread) -------------------------------------------------------------------

void FWWOBodyAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (UWWOBodyAnimInstance* Body = Cast<UWWOBodyAnimInstance>(InAnimInstance))
	{
		Body->Advance(DeltaSeconds);
		Body->Snapshot(*this);
	}
}

void FWWOBodyAnimProxy::CacheMasks(const FBoneContainer& Bones)
{
	if (CachedSerial == Bones.GetSerialNumber() && CachedBones == Bones.GetCompactPoseNumBones()) return;
	CachedSerial = Bones.GetSerialNumber();
	CachedBones = Bones.GetCompactPoseNumBones();
	const FReferenceSkeleton& Ref = Bones.GetReferenceSkeleton();
	UpperMask.Init(false, CachedBones);
	for (int32 i = 0; i < CachedBones; ++i)
	{
		const FString Name = Ref.GetBoneName(Bones.MakeMeshPoseIndex(FCompactPoseBoneIndex(i)).GetInt()).ToString();
		bool bLower = false;
		for (const TCHAR* Start : LowerBody) bLower |= Name.StartsWith(Start);
		UpperMask[i] = !bLower && Name != TEXT("center_of_mass") && Name != TEXT("interaction");
	}
	auto Find = [&](const TCHAR* Name) -> int32
	{
		const int32 Mesh = Ref.FindBoneIndex(Name);
		return Mesh == INDEX_NONE ? INDEX_NONE : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Mesh)).GetInt();
	};
	SpineBones.Reset();
	HeadBones.Reset();
	for (const TCHAR* N : Spine) if (const int32 I = Find(N); I != INDEX_NONE) SpineBones.Add(I);
	for (const TCHAR* N : Neck) if (const int32 I = Find(N); I != INDEX_NONE) HeadBones.Add(I);
}

void FWWOBodyAnimProxy::Sample(const FClip& Clip, FPoseContext& Out)
{
	FAnimationPoseData Data(Out);
	Clip.Seq->GetAnimationPose(Data, FAnimExtractContext(double(Clip.Time), false, FDeltaTimeRecord(), Clip.bLoop));
}

bool FWWOBodyAnimProxy::Evaluate(FPoseContext& Output)
{
	FCompactPose& Pose = Output.Pose;
	CacheMasks(Pose.GetBoneContainer());

	// Base: the crossfading clips, weighted.
	float Total = 0.f;
	FPoseContext Temp(this);
	for (const FClip& C : Base)
	{
		if (!C.Seq || C.Weight <= 0.f) continue;
		if (Total <= 0.f)
		{
			Sample(C, Output);
			Total = C.Weight;
			continue;
		}
		Sample(C, Temp);
		Total += C.Weight;
		const float Alpha = C.Weight / Total;
		for (const FCompactPoseBoneIndex I : Pose.ForEachBoneIndex()) Pose[I].BlendWith(Temp.Pose[I], Alpha);
	}
	if (Total <= 0.f) Output.ResetToRefPose();

	// Upper body: the overlay, then the one-shot on top.
	for (const FClip* C : { &Upper, &Shot })
	{
		if (!C->Seq || C->Weight <= 0.f) continue;
		Sample(*C, Temp);
		for (const FCompactPoseBoneIndex I : Pose.ForEachBoneIndex())
		{
			if (UpperMask.IsValidIndex(I.GetInt()) && UpperMask[I.GetInt()]) Pose[I].BlendWith(Temp.Pose[I], C->Weight);
		}
	}

	// Hunch: each bone turns about the body's left-right axis at its own joint (the bend in
	// HunchModifier, around the skeleton's X axis), so the bends add up down the chain.
	if ((HunchSpine != 0.f && SpineBones.Num()) || (HunchHead != 0.f && HeadBones.Num()))
	{
		FCSPose<FCompactPose> CS;
		CS.InitPose(Pose);
		auto Bend = [&CS](int32 Index, float Degrees)
		{
			const FCompactPoseBoneIndex B(Index);
			FTransform T = CS.GetComponentSpaceTransform(B);
			// Mesh space faces +Y with +X to the left: turning about -X tips the top forward.
			T.SetRotation(FQuat(FVector(-1.f, 0.f, 0.f), FMath::DegreesToRadians(Degrees)) * T.GetRotation());
			const FBoneTransform One(B, T);
			CS.SafeSetCSBoneTransforms(TConstArrayView<FBoneTransform>(&One, 1));
		};
		for (const int32 I : SpineBones) Bend(I, HunchSpine / SpineBones.Num());
		for (const int32 I : HeadBones) Bend(I, -HunchHead / HeadBones.Num());
		FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CS), Pose);
	}
	return true;
}
