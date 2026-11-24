#pragma once

// The body's animation player (Godot: the AnimationPlayer / AnimationTree in character_model.gd,
// soldier.gd and HunchModifier). A native AnimInstance, no AnimBlueprint:
// - Base: one clip at a time with a crossfade and a playback speed, like AnimationPlayer.play().
//   A non-looping clip holds its last frame; Queue() starts the next one when it ends.
// - Upper body (everything above the pelvis and legs, as soldier.gd's filter): an optional
//   overlay clip blended in by weight (the Soldier's pistol aim), and a one-shot on top of it
//   with fade in / out (the Soldier's hit flinch).
// - Hunch: bends the spine forward and lifts the neck and head back up on top of whatever
//   plays (the Swarm's hunched run).
// Clips are the Godot animations baked onto the mannequin (Tools/bake_ual_anims.py).

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "WWOBodyAnimInstance.generated.h"

class UAnimSequence;

USTRUCT()
struct FWWOBodyClip
{
	GENERATED_BODY()

	UPROPERTY() TObjectPtr<UAnimSequence> Seq = nullptr;
	float Time = 0.f;
	float Weight = 0.f;
	float FadeRate = 0.f; // weight per second: + fading in, - fading out
	bool bLoop = false;
};

struct FWWOBodyAnimProxy : public FAnimInstanceProxy
{
	FWWOBodyAnimProxy() = default;
	explicit FWWOBodyAnimProxy(UAnimInstance* InAnimInstance) : FAnimInstanceProxy(InAnimInstance) {}

	struct FClip
	{
		const UAnimSequence* Seq = nullptr;
		float Time = 0.f;
		float Weight = 0.f;
		bool bLoop = false;
	};
	TArray<FClip> Base;
	FClip Upper, Shot;
	float HunchSpine = 0.f, HunchHead = 0.f;

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	uint16 CachedSerial = 0;
	int32 CachedBones = -1;
	TArray<bool> UpperMask; // by compact bone index
	TArray<int32> SpineBones, HeadBones; // compact indices, parents first

	void CacheMasks(const FBoneContainer& Bones);
	static void Sample(const FClip& Clip, FPoseContext& Out);
};

UCLASS(Transient, NotBlueprintable)
class WHATWASOURS_API UWWOBodyAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// AnimationPlayer.play(name, blend) + speed_scale: a different clip crossfades in over Blend
	// seconds; the same clip only takes the new speed.
	void Play(UAnimSequence* Seq, bool bLoop, float Speed = 1.f, float Blend = 0.2f);
	void Queue(UAnimSequence* Seq, bool bLoop); // plays when the current clip ends
	UAnimSequence* GetCurrent() const; // what's playing (null once a one-off clip has ended)
	bool IsPlaying() const { return GetCurrent() != nullptr; }
	float GetCurrentTime() const;
	float GetCurrentLength() const;
	void Stop(); // holds the pose (Godot's anim.stop() is never used; this is for Revive)

	// A Blend2 of two looping clips instead of the single clip (the Soldier's idle / walk by
	// speed, AnimationTree "move"): B at its own speed, A at normal speed.
	void SetMix(UAnimSequence* A, UAnimSequence* B, float Alpha, float SpeedB);
	// The AnimationTree switched off (the Soldier dying): the mix becomes ordinary clips that
	// the next Play() crossfades from, and the upper-body layers stop.
	void ClearLayers();

	// Upper body overlay (Blend2 with a bone filter) and a one-shot on top (OneShot).
	void SetUpperBody(UAnimSequence* Seq, float Weight);
	void PlayUpperShot(UAnimSequence* Seq, float FadeIn, float FadeOut);

	// Degrees spread over the spine bones and over the neck and head (HunchModifier).
	float HunchSpine = 0.f;
	float HunchHead = 0.f;
	// The model's own standing correction, added to the hunch (CharacterModels.json 'upright': the robot's torso tips
	// forward on the mannequin's clips, so its spine is bent back by this and its head forward again to stay level).
	float BaseSpine = 0.f;
	float BaseHead = 0.f;
	float GetUpperWeight() const { return Upper.Weight; }

	void Advance(float DeltaSeconds); // run by the proxy before each update
	void Snapshot(FWWOBodyAnimProxy& Proxy) const;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	UPROPERTY() TArray<FWWOBodyClip> Clips; // the last one is the current clip
	UPROPERTY() TObjectPtr<UAnimSequence> Queued = nullptr;
	bool bQueuedLoop = false;
	float Speed = 1.f;
	UPROPERTY() FWWOBodyClip MixA;
	UPROPERTY() FWWOBodyClip MixB;
	bool bMix = false;
	float MixAlpha = 0.f, MixSpeedB = 1.f;
	UPROPERTY() FWWOBodyClip Upper;
	UPROPERTY() FWWOBodyClip Shot;
	float ShotFadeIn = 0.05f, ShotFadeOut = 0.2f;
};
