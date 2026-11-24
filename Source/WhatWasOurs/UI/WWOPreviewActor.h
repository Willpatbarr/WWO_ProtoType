#pragma once

// The character menu's preview: a mannequin far below the level that follows the loadout, with its
// own scene capture, backdrop and lights. UWWOCharacterMenu spawns it, shows its render target and
// turns it (UpdateTurn); the capture runs only while the menu is open (SetPreviewLive).

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WWOPreviewActor.generated.h"

class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UDirectionalLightComponent;
class UWWOBodyComponent;

// The mannequin the character menu shows, far away from the level, with its own camera.
UCLASS()
class WHATWASOURS_API AWWOPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	AWWOPreviewActor();
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneCaptureComponent2D> Capture;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UWWOBodyComponent> Body;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> Target;
	UPROPERTY() TObjectPtr<USceneComponent> Turntable;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Backdrop;

	// The preview's area in the Godot layout: the Loadout tab's middle column's shape (tall), so the
	// render fills that column's height with no band under it (the Flora tab's wider column shows it
	// with the page's shade either side, which its backdrop's edges match).
	static inline const FVector2D Size = FVector2D(540.f, 780.f);
	// The capture camera: 4 m out and 1.25 m up (chest height on the robot, which is taller than the mannequin this was
	// first framed for), looking a little down at its middle through a long lens (CameraHalfFov tall each way). From
	// waist height and close, the robot was seen from below and looked as if it leaned. It still fills about 90% of the
	// preview image's height, with room above for a VeilSpore hood and below for the floor shadow.
	static inline const FVector CameraAt = FVector(400.f, 0.f, 125.f);
	static constexpr float CameraPitch = -3.9f;
	static constexpr float CameraHalfFov = 14.75f;
	// The backdrop's color, so it comes out as the panel's (0.1, 0.11, 0.13) after the capture's
	// tonemapping, at BackdropBias; at another exposure it's scaled to match (BackdropFor).
	// ExposureBias is the gun preview's (AWWOGunPreviewActor); the character uses CharacterExposure.
	static inline const FLinearColor BackdropColor = FLinearColor(0.0366f, 0.0381f, 0.0417f) * 0.92f;
	static constexpr float BackdropBias = 5.5f;
	static constexpr float ExposureBias = 1.f;
	static FLinearColor BackdropFor(float Bias) { return BackdropColor * FMath::Pow(2.f, (BackdropBias - Bias) / 2.2f); }
	// The studio (the concept sheets' look): a soft key from the front, upper left, the only light
	// that casts shadows; a low cool fill from the right; two rims from behind that cut the silhouette
	// out of the backdrop; a faint top light and a fainter bounce from below. All directional, all on
	// lighting channel 2, which only the preview body and its grafts are on (the level sits far above
	// and keeps its own sun). They are switched on only while the menu shows the preview (SetLive):
	// every visible directional light costs a full-screen pass in the main view too.
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> KeyLight;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> FillLight;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> RimLeft;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> RimRight;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> TopLight;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> BounceLight;
	// A soft shadow on the floor under the feet (M_WWO_PreviewShadow).
	UPROPERTY() TObjectPtr<UStaticMeshComponent> FloorShadow;

	// The character's exposure: about the arenas' (Tools/build_maps.py keeps auto exposure near EV 1),
	// so the flora glow that is tuned for play reads as glow here and does not clip to white.
	static constexpr float CharacterExposure = -1.f;
	// The character backdrop's colour against BackdropFor's: the bloom's veil lifts the render a
	// little, so this brings its edges back to the page's shade (measured on the menu screenshots).
	static constexpr float BackdropTrim = 0.94f;
	// Supersampling of the render target against the preview's on-screen size at 1080p (with the
	// capture's own temporal AA on top).
	static constexpr float Supersample = 1.6f;

	// The loop the preview character stands in (UWWOBodyComponent::Play).
	static inline const FName IdleAnim = FName("Idle_Unarmed"); // hands empty: the armed idle aims a gun it does not hold here

	// Capture and lights on (menu open) or off.
	void SetLive(bool bLive);
};
