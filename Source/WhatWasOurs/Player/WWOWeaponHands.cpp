// UWWOWeaponComponent: hands on the gun: the player's own model's arms, solved each frame to hold the
// gun as a first-person rifle pose does, and the punch and throw clips they leave it to play.

#include "Player/WWOWeaponComponent.h"
#include "WhatWasOurs.h"
#include "Player/WWOWeaponPrivate.h"
#include "Player/WWOCharacter.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Engine/World.h"
#include "Systems/WWOConfig.h"
#include "Characters/WWOBodyComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"

using namespace WWOWeapon; // the helpers these files share (WWOWeaponPrivate.h)

namespace
{
	// The first-person rifle pose the hands copy (xidePunk First Person Rifle Animation, a Fab pack).
	// Its arms mesh hangs off the demo camera by ClipToCamera, and its rifle off hand_r by ClipRifle.
	const TCHAR* HandsPosePath = TEXT("/Game/FirstPersonRifleAnimations/Animations/AS_Rifle_Idle_Pose.AS_Rifle_Idle_Pose");
	const FTransform ClipToCamera(FRotator(0.f, -90.f, 0.f), FVector(7.f, 0.f, -162.614f));
	const FTransform ClipRifle(FRotator(-10.451844f, 167.601919f, 11.114348f), FVector(-8.663722f, 2.838672f, 0.827358f));
	const FTransform MeshToCamera(FRotator(0.f, -90.f, 0.f)); // a mannequin-style mesh (faces +y) turned to face the camera's +x

	// A clip's bone in its skeleton's mesh space at Seconds.
	FTransform ClipBone(const UAnimSequence* Anim, FName Bone, float Seconds)
	{
		const FReferenceSkeleton& Ref = Anim->GetSkeleton()->GetReferenceSkeleton();
		FTransform Out = FTransform::Identity;
		for (int32 B = Ref.FindBoneIndex(Bone); B != INDEX_NONE; B = Ref.GetParentIndex(B))
		{
			FTransform Local;
			Anim->GetBoneTransform(Local, FSkeletonPoseBoneIndex(B), FAnimExtractContext(double(Seconds)), false);
			Out = Out * Local;
		}
		return Out;
	}

	// A bone of a reference skeleton in mesh space.
	FTransform RefBone(const FReferenceSkeleton& Ref, int32 Bone)
	{
		FTransform Out = FTransform::Identity;
		for (int32 B = Bone; B != INDEX_NONE; B = Ref.GetParentIndex(B)) Out = Out * Ref.GetRefBonePose()[B];
		return Out;
	}

	// A frame for a forearm: x along it, y toward the body's outside.
	FQuat ForearmFrame(const FVector& Along, bool bLeft)
	{
		return FRotationMatrix::MakeFromXY(Along, FVector(0.f, bLeft ? -1.f : 1.f, 0.f)).ToQuat();
	}
}

void UWWOWeaponComponent::BuildHandsClip(FHandsClip& Clip, const UAnimSequence* Anim, const FTransform& ToCamera, float Start, float Key)
{
	Clip.bValid = false;
	if (!Anim || !Anim->GetSkeleton() || HandsOwner.Num() == 0) return;
	const FReferenceSkeleton& ClipRef = Anim->GetSkeleton()->GetReferenceSkeleton();
	Clip.ToCamera = ToCamera;
	Clip.Start = Start;
	Clip.Key = Key;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const bool bLeft = Side == 1;
		const int32 Lower = ClipRef.FindBoneIndex(bLeft ? "lowerarm_l" : "lowerarm_r"), Hand = ClipRef.FindBoneIndex(bLeft ? "hand_l" : "hand_r");
		if (Lower == INDEX_NONE || Hand == INDEX_NONE) return;
		const FTransform L = RefBone(ClipRef, Lower) * MeshToCamera, H = RefBone(ClipRef, Hand) * MeshToCamera;
		Clip.HandRef[Side] = H.GetRotation();
		Clip.HandAlign[Side] = ForearmFrame((H.GetLocation() - L.GetLocation()).GetSafeNormal(), bLeft) * ForearmFrame(HandsArms[Side].LowerDir, bLeft).Inverse();
	}
	Clip.bValid = true;
}

void UWWOWeaponComponent::BuildHands(USkeletalMesh* Asset)
{
	HandsAsset = Asset;
	if (!HandsPose) HandsPose = LoadObject<UAnimSequence>(nullptr, HandsPosePath);
	if (!HandsMesh)
	{
		HandsMesh = NewObject<UPoseableMeshComponent>(GetOwner());
		HandsMesh->SetupAttachment(GetAttachParent()); // the camera: its component space is camera space
		HandsMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		HandsMesh->SetCastShadow(false);
		HandsMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
		HandsMesh->SetBoundsScale(4.f);
		HandsMesh->RegisterComponent();
	}
	HandsMesh->SetSkinnedAssetAndUpdate(Asset);
	HandsRefLocal.Reset();
	HandsOwner.Reset();
	if (!Asset || !HandsPose || !HandsPose->GetSkeleton()) return;
	const AWWOCharacter* Player = GetPlayer();
	bHandsRigid = Player && Player->Body && Player->Body->GetModel() && Player->Body->GetModel()->GetSkeletalMeshAsset() == Asset && Player->Body->bRigidHands;
	const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
	const FReferenceSkeleton& ClipRef = HandsPose->GetSkeleton()->GetReferenceSkeleton();
	HandsRefLocal = Ref.GetRefBonePose();
	HandsOwner.Init(-1, Ref.GetNum());
	const FTransform ClipHandR = ClipBone(HandsPose, "hand_r", 0.f) * ClipToCamera;
	HandsClipGun = ClipRifle * ClipHandR;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const bool bLeft = Side == 1;
		const TCHAR* S = bLeft ? TEXT("_l") : TEXT("_r");
		const FName UpperName(*FString::Printf(TEXT("upperarm%s"), S)), LowerName(*FString::Printf(TEXT("lowerarm%s"), S)), HandName(*FString::Printf(TEXT("hand%s"), S));
		FHandsArm& A = HandsArms[Side];
		A.Upper = Ref.FindBoneIndex(UpperName);
		A.Lower = Ref.FindBoneIndex(LowerName);
		A.Hand = Ref.FindBoneIndex(HandName);
		if (A.Upper == INDEX_NONE || A.Lower == INDEX_NONE || A.Hand == INDEX_NONE) { HandsOwner.Reset(); return; }
		const FTransform U = RefBone(Ref, A.Upper) * MeshToCamera, L = RefBone(Ref, A.Lower) * MeshToCamera, H = RefBone(Ref, A.Hand) * MeshToCamera;
		A.UpperLength = FVector::Dist(U.GetLocation(), L.GetLocation());
		A.LowerLength = FVector::Dist(L.GetLocation(), H.GetLocation());
		A.UpperRef = U.GetRotation();
		A.LowerRef = L.GetRotation();
		A.HandRef = H.GetRotation();
		A.UpperDir = (L.GetLocation() - U.GetLocation()).GetSafeNormal();
		A.LowerDir = (H.GetLocation() - L.GetLocation()).GetSafeNormal();
		const FTransform ClipL = RefBone(ClipRef, ClipRef.FindBoneIndex(LowerName)) * MeshToCamera, ClipH = RefBone(ClipRef, ClipRef.FindBoneIndex(HandName)) * MeshToCamera;
		A.ClipHandRef = ClipH.GetRotation();
		A.HandAlign = ForearmFrame((ClipH.GetLocation() - ClipL.GetLocation()).GetSafeNormal(), bLeft) * ForearmFrame(A.LowerDir, bLeft).Inverse();
		A.ClipShoulder = ClipBone(HandsPose, UpperName, 0.f) * ClipToCamera;
		A.ClipElbow = ClipBone(HandsPose, LowerName, 0.f) * ClipToCamera;
		A.ClipHand = ClipBone(HandsPose, HandName, 0.f) * ClipToCamera;
		HandsOwner[A.Upper] = HandsOwner[A.Lower] = HandsOwner[A.Hand] = -2;
		if (bHandsRigid)
		{
			const AWWOCharacter* Owner = GetPlayer();
			const FVector Flip(bLeft ? -1.f : 1.f, 1.f, 1.f); // the left hand is the right one mirrored
			A.RigidRef = FRotationMatrix::MakeFromXZ(MeshToCamera.TransformVector(Owner->Body->HandFingers * Flip), MeshToCamera.TransformVector(Owner->Body->HandPalm * Flip)).ToQuat();
		}
	}
	// Whatever hangs off a hand (fingers) goes with it.
	for (int32 B = 0; B < Ref.GetNum(); ++B)
	{
		const int32 Parent = Ref.GetParentIndex(B);
		if (HandsOwner[B] != -1 || Parent == INDEX_NONE) continue;
		if (Parent == HandsArms[0].Hand || HandsOwner[Parent] == 0) HandsOwner[B] = 0;
		else if (Parent == HandsArms[1].Hand || HandsOwner[Parent] == 1) HandsOwner[B] = 1;
	}
	// The action clips (Fab packs: First person brawler animation pack, xidePunk First Person Grenade
	// Animations), each with where its arms hang off its demo camera; the throw's wind-up starts 0.3 s in and the grenade leaves the hand at 0.78 s.
	if (!HandsPunchAnim) HandsPunchAnim = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/FirstPersonBrawler/Character/Animations/FP_Hook_R.FP_Hook_R"));
	if (!HandsThrowAnim) HandsThrowAnim = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/FirstPersonGrenadeAnimation/Animations/AS_Player_Grenade_Throw.AS_Player_Grenade_Throw"));
	// The hook: the fist goes out wide to the right by 0.15 s, comes across the middle (its furthest
	// forward) at about 0.25 s, and carries on to the left until 0.40 s.
	// A swing that hits something stops at the middle instead of carrying through (UpdateHands).
	// It is played as the pack made it: its own reach (the fist 34 cm ahead of the eyes at most),
	// shoulder and wrist turn. (Reach, Lean and bFist can push the fist further out, lean the
	// shoulder in and square the fist to the forearm; all off here.)
	// (Key: the wrist is still 12 cm right of the middle at 0.215 s, which is when the fist itself,
	// turned in ahead of the wrist, is over the reticle.)
	BuildHandsClip(HandsPunch, HandsPunchAnim, FTransform(FRotator(0.f, -90.f, 0.f), FVector(-3.96167f, 0.f, -166.104431f)), 0.f, 0.215f);
	HandsPunch.Reach = 1.f;
	HandsPunch.Lean = 0.f;
	HandsPunch.bFist = false;
	HandsPunch.Offset = FVector(20.f, 0.f, 0.f); // the whole swing, unchanged in shape, 20 cm further from the eyes
	HandsPunch.Roll = 10.f;                      // turned so the elbow rides higher and the fist comes in lower (at 20 the forearm is level with the eyes and fills the view)
	HandsPunch.bOnReticle = true;                // and placed so the fist crosses the reticle itself: where the hit lands
	BuildHandsClip(HandsThrow, HandsThrowAnim, FTransform(FRotator(0.f, -90.f, 0.f), FVector(10.f, 0.f, -162.61403f)), 0.3f, 0.78f);
	HandsThrow.bMirror = true; // thrown with the left hand: the right one stays on the gun
	HandsThrow.Arms = 2;
	HandsPunch.Arms = 1;       // the right hand punches: the left one keeps the gun as it drops out of the way
}

void UWWOWeaponComponent::ReadHandsHolds()
{
	// This gun's grip and fore-end (Config/WeaponModels.json: hand, and the grip mod's anchor).
	// Read when the model is built (the end of BuildModel, once ModelShift is set), not every frame:
	// the config is cached for the run, so a rebuild is the only time these can change. Only
	// UpdateHands reads them, and only while the first-person gun is shown, when it has been built.
	const TSharedPtr<FJsonObject> Model = Weapon ? ModelConfig(Weapon->Id) : nullptr;
	const TSharedPtr<FJsonObject>* Anchors = nullptr;
	const FVector Offset = WWOConfig::Vec(Model, TEXT("offset")) + ModelShift;
	HandsGrip = Offset + WWOConfig::Vec(Model, TEXT("hand"), FVector(-3.f, 0.f, -4.f));
	HandsFore = Offset + (Model.IsValid() && Model->TryGetObjectField(TEXT("anchors"), Anchors) ? WWOConfig::Vec(*Anchors, TEXT("grip"), FVector(22.f, 0.f, -1.f)) : FVector(22.f, 0.f, -1.f));
	HandsMag = Offset + (Model.IsValid() && Model->TryGetObjectField(TEXT("anchors"), Anchors) ? WWOConfig::Vec(*Anchors, TEXT("magazine"), FVector(8.f, 0.f, -6.f)) : FVector(8.f, 0.f, -6.f));
	bHandsPistol = Weapon && Weapon->Id == "handcannon";
}

void UWWOWeaponComponent::UpdateHands()
{
	AWWOCharacter* P = GetPlayer();
	if (bFirstPersonHidden) // someone else's character or a bot: nobody sees first-person hands on it
	{
		if (HandsMesh) HandsMesh->SetVisibility(false);
		return;
	}
	USkeletalMesh* Want = nullptr;
	if (P && P->Body && P->Body->GetModel()) Want = P->Body->GetModel()->GetSkeletalMeshAsset();
	if (!Want) Want = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (Want != HandsAsset || !HandsMesh) BuildHands(Want);
	const bool bShow = !bFirstPersonHidden && HandsOwner.Num() > 0 && IsVisible();
	HandsMesh->SetVisibility(bShow);
	if (!bShow) return;
	const FReferenceSkeleton& Ref = HandsAsset->GetRefSkeleton();
	// The clip's rifle onto this gun: anything held against the clip's rifle moves to the same place on ours.
	const FTransform GunOurs = FTransform(HandsGunOffset) * GetRelativeTransform();
	const FTransform ClipToOurs = HandsClipGun.Inverse() * GunOurs;
	TArray<FTransform> CS;
	CS.SetNum(Ref.GetNum());
	FVector Hidden = FVector(-40.f, 0.f, -40.f); // behind and below the camera
	// The punch and the throw: the arms leave the gun (which drops out of the way by itself) and
	// play a first-person clip at its own speed, placed in time so its key moment (the fist landing,
	// the grenade leaving the hand) falls on the game's (the melee hit, the throw's release).
	{
		const float MeleeSeconds = FMath::Max(M("melee_time", MeleeTime), 0.05f);
		const float ThrowEnd = HandsThrow.bValid ? ThrowRelease + (HandsThrowAnim->GetPlayLength() - HandsThrow.Key) : 0.f;
		int32 Playing = 0;
		float Time = 0.f;
		HandsHitTime = bMeleeing && bMeleeConnected ? HandsHitTime + HandsDelta : 0.f;
		if (bMeleeing && HandsPunch.bValid && HandsHitTime <= HandsHitHold) // (after a hit's short hold the hand goes straight back to the gun)
		{
			// The swing fills the melee: up to the hit it plays from its start to its key moment, after
			// it the rest, so the melee time setting speeds the whole swing up or slows it down.
			Playing = 1;
			const float End = HandsPunchAnim->GetPlayLength() - 0.1f;
			Time = MeleeProgress < MeleeImpactAt
				? FMath::Lerp(HandsPunch.Start, HandsPunch.Key, MeleeProgress / MeleeImpactAt)
				: FMath::Lerp(HandsPunch.Key, End, (MeleeProgress - MeleeImpactAt) / (1.f - MeleeImpactAt));
			if (bMeleeConnected) Time = FMath::Min(Time, HandsPunch.Key); // it hit: the fist stops there, on the reticle
		}
		else if (HandsAction == 1 && HandsPunch.bValid && !bMeleeConnected && HandsActionTime < HandsPunchAnim->GetPlayLength() - 0.12f) { Playing = 1; Time = HandsActionTime + HandsDelta; } // a miss swings through and comes back
		else if (HandsThrow.bValid && ThrowClock < ThrowEnd - 0.15f) { Playing = 2; Time = HandsThrow.Key + (ThrowClock - ThrowRelease); }
		if (Playing != 0)
		{
			const FHandsClip& Clip = Playing == 1 ? HandsPunch : HandsThrow;
			HandsAction = Playing;
			HandsActionTime = FMath::Max(Time, Clip.Start);
		}
		HandsActionWeight = WWO::MoveToward(HandsActionWeight, Playing != 0 ? 1.f : 0.f, HandsDelta / (Playing != 0 ? 0.08f : 0.18f));
		if (HandsActionWeight <= 0.f) HandsAction = 0;
	}
	const FHandsClip* Action = HandsAction == 1 ? &HandsPunch : HandsAction == 2 ? &HandsThrow : nullptr;
	const UAnimSequence* ActionAnim = HandsAction == 1 ? HandsPunchAnim : HandsThrowAnim;
	const float ActionWeight = Action ? WWO::SmoothStep(0.f, 1.f, HandsActionWeight) : 0.f;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FHandsArm& A = HandsArms[Side];
		FTransform Hand = A.ClipHand * ClipToOurs;
		FQuat HandRotation = Hand.GetRotation() * A.ClipHandRef.Inverse() * A.HandAlign * A.HandRef; // turns as the clip's does from its reference pose
		if (bHandsRigid)
		{
			// A rigid hand lies flat against the gun: the right one along the right of the grip, the
			// left one under the fore-end (a pistol: along the left of the grip). Touch is where the
			// palm meets the gun, Fingers and Palm the way the hand points and faces, in gun space.
			FVector Touch, Fingers, Palm;
			// (The knuckles sit 2 cm short of Touch along the fingers, which then close toward the palm:
			// the right hand's around the front of the grip, the left's up around the far side of the fore-end.)
			if (Side == 0) { Touch = HandsGrip + FVector(4.f, 1.5f, -2.f); Fingers = FVector(1.f, 0.f, 0.25f); Palm = FVector(0.f, -1.f, 0.f); }
			else if (bHandsPistol) { Touch = HandsGrip + FVector(4.5f, -1.5f, -3.f); Fingers = FVector(1.f, 0.f, 0.2f); Palm = FVector(0.f, 1.f, 0.f); }
			else { Touch = HandsFore + FVector(0.f, 2.f, -1.f); Fingers = FVector(0.35f, 1.f, 0.1f); Palm = FVector(0.f, 0.f, 1.f); }
			const FTransform Gun = GetRelativeTransform();
			// The hand's middle is 11 cm from the wrist along the fingers and 2 cm behind the wrist's line; the palm's surface 3.5 cm in front of its middle.
			auto Lay = [&](FVector At, FVector Along, FVector Facing)
			{
				Along.Normalize();
				Facing = (Facing - Along * FVector::DotProduct(Facing, Along)).GetSafeNormal();
				return FTransform(Gun.GetRotation() * FRotationMatrix::MakeFromXZ(Along, Facing).ToQuat() * A.RigidRef.Inverse() * A.HandRef,
					Gun.TransformPositionNoScale(At - Facing * 1.5f - Along * 11.f));
			};
			Hand = Lay(Touch, Fingers, Palm);
			if (Side == 1 && bReloading)
			{
				// Reloading: the left hand goes to the magazine, takes it down and out as the gun's own
				// magazine drops (UpdatePose), brings it back up, and returns to its hold.
				const float RP = ReloadProgress;
				const float AtMag = WWO::SmoothStep(0.06f, 0.2f, RP) * (1.f - WWO::SmoothStep(FMath::Max(ReloadSeated, 0.6f), FMath::Min(FMath::Max(ReloadSeated, 0.6f) + 0.2f, 1.f), RP));
				const float MagOut = WWO::SmoothStep(0.2f, 0.4f, RP) * (1.f - WWO::SmoothStep(0.5f, ReloadSeated, RP));
				const FTransform OnMag = Lay(HandsMag + FVector(0.f, -1.5f, -3.f - ReloadMagDrop * 100.f * MagOut), FVector(0.3f, 0.f, 1.f), FVector(0.f, 1.f, 0.f));
				Hand.Blend(Hand, OnMag, AtMag);
			}
			HandRotation = Hand.GetRotation();
		}
		FVector Shoulder = A.ClipShoulder.GetLocation();
		FVector ElbowHint = A.ClipElbow.GetLocation() + (Hand.GetLocation() - A.ClipHand.GetLocation()) * 0.5f;
		if (Action && (Action->Arms & (1 << Side)))
		{
			// (Mirrored: this arm plays the clip's other arm, flipped left for right across the view.)
			const int32 From = Action->bMirror ? 1 - Side : Side;
			const bool bLeft = From == 1;
			auto Flip = [&](const FVector& V) { return Action->bMirror ? FVector(V.X, -V.Y, V.Z) : V; };
			auto FlipQ = [&](const FQuat& Q) { return Action->bMirror ? FQuat(-Q.X, Q.Y, -Q.Z, Q.W) : Q; };
			const FTransform ClipHandRaw = ClipBone(ActionAnim, bLeft ? "hand_l" : "hand_r", HandsActionTime) * Action->ToCamera;
			FTransform ClipHand(FlipQ(ClipHandRaw.GetRotation()), Flip(ClipHandRaw.GetLocation()));
			FVector ClipShoulder = Flip((ClipBone(ActionAnim, bLeft ? "upperarm_l" : "upperarm_r", HandsActionTime) * Action->ToCamera).GetLocation());
			FVector ClipElbow = Flip((ClipBone(ActionAnim, bLeft ? "lowerarm_l" : "lowerarm_r", HandsActionTime) * Action->ToCamera).GetLocation());
			// The arm turned as one piece about the view's forward axis (nothing in the swing itself changes).
			const float RollRadians = FMath::DegreesToRadians(Action->Roll);
			const FQuat Turn = FQuat::FindBetweenNormals(FVector(0.f, 1.f, 0.f), FVector(0.f, FMath::Cos(RollRadians), FMath::Sin(RollRadians)));
			ClipHand = FTransform(Turn * ClipHand.GetRotation(), Turn.RotateVector(ClipHand.GetLocation()));
			ClipShoulder = Turn.RotateVector(ClipShoulder);
			ClipElbow = Turn.RotateVector(ClipElbow);
			FVector Move = Action->Offset;
			if (Action->bOnReticle && P && Weapon)
			{
				// Where the knuckles are at the key moment, against the reticle (which sits below the
				// middle of the view: the same angle the gun points down by, UpdatePose).
				const FVector Knuckles = Turn.RotateVector(Flip((ClipBone(ActionAnim, bLeft ? "middle_01_l" : "middle_01_r", Action->Key) * Action->ToCamera).GetLocation())) + Action->Offset;
				const float Drop = P->ReticleAngleForFov(Weapon->ViewmodelFov);
				Move += FVector(0.f, -Knuckles.Y, -Knuckles.X * FMath::Tan(Drop) - Knuckles.Z);
			}
			ClipHand.AddToTranslation(Move);
			ClipShoulder += Move;
			ClipElbow += Move;
			if (Action->Reach != 1.f)
			{
				const float Rest = (ClipBone(ActionAnim, bLeft ? "hand_l" : "hand_r", 0.f) * Action->ToCamera).GetLocation().X;
				FVector At = ClipHand.GetLocation();
				const float Was = At.X;
				At.X = FMath::Max(Rest + (At.X - Rest) * Action->Reach, 6.f); // (never pulled back into your face)
				ClipHand.SetLocation(At);
				const float Leaning = FMath::Max(At.X - Was, 0.f) * Action->Lean; // forward only: the shoulder doesn't pull back with the wind-up
				ClipShoulder.X += Leaning;
				ClipElbow.X += Leaning;
			}
			const FQuat ClipRotation = ClipHand.GetRotation() * FlipQ(Action->HandRef[From]).Inverse() * Action->HandAlign[Side] * A.HandRef;
			Hand.SetLocation(FMath::Lerp(Hand.GetLocation(), ClipHand.GetLocation(), ActionWeight));
			HandRotation = FQuat::Slerp(HandRotation, ClipRotation, ActionWeight);
			Shoulder = FMath::Lerp(Shoulder, ClipShoulder, ActionWeight);
			ElbowHint = FMath::Lerp(ElbowHint, ClipElbow, ActionWeight);
		}
		// Two-bone reach: the elbow goes where the clip's does, as near as the model's own arm lengths allow.
		FVector To = Hand.GetLocation() - Shoulder;
		const float Reach = FMath::Clamp(To.Size(), FMath::Abs(A.UpperLength - A.LowerLength) + 1.f, A.UpperLength + A.LowerLength - 0.5f);
		const FVector Axis = To.GetSafeNormal();
		const FVector Wrist = Shoulder + Axis * Reach;
		const float Along = (A.UpperLength * A.UpperLength - A.LowerLength * A.LowerLength + Reach * Reach) / (2.f * Reach);
		const float Out = FMath::Sqrt(FMath::Max(A.UpperLength * A.UpperLength - Along * Along, 0.f));
		const FVector Hint = ElbowHint - Shoulder;
		FVector Pole = (Hint - Axis * FVector::DotProduct(Hint, Axis)).GetSafeNormal();
		if (Pole.IsNearlyZero()) Pole = FVector(0.f, Side == 1 ? -1.f : 1.f, -1.f).GetSafeNormal();
		const FVector Elbow = Shoulder + Axis * Along + Pole * Out;
		CS[A.Upper] = FTransform(FQuat::FindBetweenNormals(A.UpperDir, (Elbow - Shoulder).GetSafeNormal()) * A.UpperRef, Shoulder);
		CS[A.Lower] = FTransform(FQuat::FindBetweenNormals(A.LowerDir, (Wrist - Elbow).GetSafeNormal()) * A.LowerRef, Elbow);
		if (Action && Action->bFist && bHandsRigid && (Action->Arms & (1 << Side)))
		{
			// The fist in line with the forearm, the back of the hand up and a little outward.
			const FVector Forearm = (Wrist - Elbow).GetSafeNormal();
			FVector PalmWay = FVector(0.f, Side == 1 ? 0.35f : -0.35f, -1.f);
			PalmWay = (PalmWay - Forearm * FVector::DotProduct(PalmWay, Forearm)).GetSafeNormal();
			const FQuat Fist = FRotationMatrix::MakeFromXZ(Forearm, PalmWay).ToQuat() * A.RigidRef.Inverse() * A.HandRef;
			HandRotation = FQuat::Slerp(HandRotation, Fist, ActionWeight);
		}
		CS[A.Hand] = FTransform(HandRotation, Wrist);
	}
	Hidden = CS[HandsArms[0].Upper].GetLocation();
	// Fingers: closed around the gun, a fist for the punch, around the grenade and then open as it leaves.
	FQuat Curl[2] = { FQuat::Identity, FQuat::Identity }, ThumbCurl[2] = { FQuat::Identity, FQuat::Identity };
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const bool bActing = Action && (Action->Arms & (1 << Side));
		float Hold = Side == 0 ? 0.55f : 0.45f;
		float Acting = HandsAction == 1 ? 1.f : (ThrowClock < ThrowRelease ? 0.5f : 0.f);
		const float Target = bActing ? FMath::Lerp(Hold, Acting, ActionWeight) : Hold;
		HandsCurl[Side] = WWO::MoveToward(HandsCurl[Side], Target, HandsDelta * 8.f);
		if (!bHandsRigid) continue;
		const FQuat FromRef = CS[HandsArms[Side].Hand].GetRotation() * HandsArms[Side].HandRef.Inverse();
		const FVector Along = FromRef.RotateVector(HandsArms[Side].RigidRef.GetAxisX()), Facing = FromRef.RotateVector(HandsArms[Side].RigidRef.GetAxisZ());
		auto Toward = [&](float Degrees) { const float R = FMath::DegreesToRadians(Degrees); return FQuat::FindBetweenNormals(Along, (Along * FMath::Cos(R) + Facing * FMath::Sin(R)).GetSafeNormal()); };
		Curl[Side] = Toward(40.f * HandsCurl[Side]);      // per joint
		ThumbCurl[Side] = Toward(18.f * HandsCurl[Side]);
	}
	for (int32 B = 0; B < Ref.GetNum(); ++B) // parents first
	{
		if (HandsOwner[B] == -1) CS[B] = FTransform(FQuat::Identity, Hidden, FVector(0.001f));
		else if (HandsOwner[B] >= 0)
		{
			CS[B] = HandsRefLocal[B] * CS[Ref.GetParentIndex(B)];
			const bool bThumb = Ref.GetBoneName(B).ToString().StartsWith(TEXT("thumb"));
			CS[B].SetRotation((bThumb ? ThumbCurl : Curl)[HandsOwner[B]] * CS[B].GetRotation());
		}
		HandsMesh->SetBoneTransformByName(Ref.GetBoneName(B), CS[B], EBoneSpaces::ComponentSpace);
	}
}
