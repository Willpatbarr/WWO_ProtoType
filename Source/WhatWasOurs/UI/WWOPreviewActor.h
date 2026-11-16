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

	// The preview's area in the Godot layout: what's left of the menu row beside the tiles.
	static inline const FVector2D Size = FVector2D(600.f, 780.f);
	// The backdrop's color, so it comes out as the panel's (0.1, 0.11, 0.13) after the capture's
	// tonemapping, at BackdropBias; at another exposure it's scaled to match (BackdropFor).
	static inline const FLinearColor BackdropColor = FLinearColor(0.0366f, 0.0381f, 0.0417f) * 0.92f;
	static constexpr float BackdropBias = 5.5f;
	static constexpr float ExposureBias = 1.f;
	static FLinearColor BackdropFor(float Bias) { return BackdropColor * FMath::Pow(2.f, (BackdropBias - Bias) / 2.2f); }
	// Godot's preview lights: a DirectionalLight3D (energy 1, no shadows) and an ambient color of
	// (0.5, 0.5, 0.55), here a key light and a fill from the camera. They light only lighting
	// channel 2, which only the preview body and its grafts are on (the level sits above them and
	// would shade the level's own sun).
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> KeyLight;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> FillLight;
};
