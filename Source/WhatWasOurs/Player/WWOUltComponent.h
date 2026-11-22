#pragma once

// The ult (F, or L1 + R1 together). Which one depends on the equipped flora (Godot: ult_manager.gd):
// - Blast (VoltVine): a massive grenade explosion centered on you. The camera glides out to
//   third person, your body casts (an orb in your grenade's color grows between your hands),
//   the blast goes off, then the camera glides back.
// - Veil (VeilSpore): a quick third-person flash as you release a big spore fog; for a while
//   you're invisible to enemies and hit harder in melee whenever you're inside spore fog.
// - Entrench (IronBark): the same quick flash as roots burst up around you; you're rooted (can
//   still look and shoot), can't be hurt, and your grenade recharges much faster.
// - Beam (DawnLotus, placeholder form): the orb charges between the hands, then one forward beam, its damage scaled by the Nectar it spends.
// You can't move, look, shoot or take damage while it plays. Charges over ChargeTime.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WWOTypes.h"
#include "WWOUltComponent.generated.h"

class UCameraComponent;
class UStaticMeshComponent;
class UPointLightComponent;

UCLASS(ClassGroup = (WWO))
class WHATWASOURS_API UWWOUltComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWWOUltComponent();
	virtual void BeginPlay() override;
	void UpdateUlt(float DeltaSeconds); // called by the character

	UPROPERTY(EditAnywhere, Category = "Ult") float ChargeTime = 60.f;
	UPROPERTY(EditAnywhere, Category = "Ult") float Damage = 600.f;
	UPROPERTY(EditAnywhere, Category = "Ult") float BlastRadius = 12.f;
	UPROPERTY(EditAnywhere, Category = "Ult") float WindupTime = 0.4f;
	UPROPERTY(EditAnywhere, Category = "Ult") float HoldTime = 0.4f;
	UPROPERTY(EditAnywhere, Category = "Ult") float CameraDistance = 4.f;
	UPROPERTY(EditAnywhere, Category = "Ult") float CameraHeight = 1.f;
	UPROPERTY(EditAnywhere, Category = "Ult") float CameraSide = 0.6f;
	UPROPERTY(EditAnywhere, Category = "Ult") float CameraBlend = 0.25f;
	UPROPERTY(EditAnywhere, Category = "Ult") float VeilTime = 12.f;
	UPROPERTY(EditAnywhere, Category = "Ult") float VeilFogRadius = 7.f;
	UPROPERTY(EditAnywhere, Category = "Ult") float VeilFogTime = 12.f;
	UPROPERTY(EditAnywhere, Category = "Ult") float VeilMeleeMult = 3.f;
	UPROPERTY(EditAnywhere, Category = "Ult") float VeilCameraBlend = 0.15f;
	UPROPERTY(EditAnywhere, Category = "Ult") float VeilHold = 0.35f;
	UPROPERTY(EditAnywhere, Category = "Ult") float EntrenchTime = 10.f;
	UPROPERTY(EditAnywhere, Category = "Ult") float EntrenchGrenadeMult = 5.f;

	float Charge = 1.f; // start full so it's easy to test
	bool bActive = false;
	FVector AimAtCast = FVector::ForwardVector; // on the machine controlling it: where the reticle pointed as it was cast (the Beam)

	bool IsReady() const { return Charge >= 1.f; }
	bool CanActivate() const;
	bool ActivateUlt();
	void Fill() { Charge = 1.f; }
	// Stops an ult under way (its owner died, or the host refused a joiner's): the camera and gun
	// come back and nothing goes off. bRefused also gives the charge back and ends what it started here.
	void Abort(bool bRefused);

private:
	UPROPERTY() TObjectPtr<UCameraComponent> UltCamera;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Orb;
	UPROPERTY() TObjectPtr<UPointLightComponent> OrbLight;

	EWWOUltKind Kind = EWWOUltKind::Blast; // the flora's (UWWOFloraData::Ult), set as it starts
	float Clock = 0.f;
	float Windup = 0.4f; // this cast's charge time (Blast: WindupTime; Beam: its own)
	bool IsCharged() const { return Kind == EWWOUltKind::Blast || Kind == EWWOUltKind::Beam; } // the orb between the hands, then the release
	float Blend = 0.25f;
	float ExplodeAt = 0.f;
	float CastAt = 0.f;
	float ChargeAt = 0.f; // the blast's Spell_Simple_Enter -> Spell_Simple_Idle
	float FinishAt = 0.f;
	bool bExploded = false;
	bool bCast = false;
	bool bCharging = false;
	bool bGlidingBack = false;
	FTransform GlideFrom, GlideTo;
	FLinearColor Color = FLinearColor::White;

	FTransform ThirdPersonTransform() const;
	FTransform FirstPersonTransform() const;
	void Begin();
	void Finish();
	bool bStartChecked = false; // PvP: a match starts with the ult empty (checked once the mode is known)
	void Release();
	void PlaceOrb();

	// The regression scenarios (-WWORegress, Player/WWORegress.h) read and reset private state between runs; no gameplay code uses this.
	friend class FWWORegress;
};
