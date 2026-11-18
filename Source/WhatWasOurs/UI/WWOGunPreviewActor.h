#pragma once

// The character menu's gun on the Weapon tab: the selected weapon's own model (WWOWeapon::BuildShowcase:
// the first-person mesh, its kept and extra parts, the equipped mods' parts) far below the level, lit by
// a key, a rim and a fill light, seen three-quarters from its right side by its own scene capture and
// framed to fit. It sways slowly while shown. UWWOCharacterMenu spawns it, shows its render target and
// calls ShowGun when the weapon or its mods change; the capture runs only while the Weapon tab is open
// (SetLive). Its lights reach only lighting channel 1, which only its parts are on (the character
// preview uses channel 2, the level channel 0).

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WWOGunPreviewActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UDirectionalLightComponent;
class UWWOWeaponData;
class UWWOLoadoutSubsystem;

UCLASS()
class WHATWASOURS_API AWWOGunPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	AWWOGunPreviewActor();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// The render's shape in the Godot layout (the Weapon tab's gun panel); the target is twice that
	// many 1080p pixels, for clean edges when it's scaled down.
	static inline const FVector2D Size = FVector2D(340.f, 204.f);

	// Builds Weapon's model (with Loadout's mods when it is the loadout's weapon) and frames it.
	void ShowGun(const UWWOWeaponData* Weapon, const UWWOLoadoutSubsystem* Loadout);
	// The capture and the sway run only while the gun is on screen.
	void SetLive(bool bLive);
	FName ShownId() const { return Shown; }
	// Picture mode (the HUD's weapon pictures): call once, right after spawning. The gun is seen flat
	// from its right side with nothing behind it, into a Shape-sized target whose alpha is the gun's
	// cut-out (scene colour, inverse opacity: M_WWO_HUDGun shows it). It does not sway or capture every
	// frame: each ShowGun takes the picture over the next few frames (so textures and shaders that
	// are still loading have arrived) and then the lights and the tick go off again.
	void MakePicture(const FVector2D& Shape);

	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> Target;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneCaptureComponent2D> Capture;

private:
	UPROPERTY() TObjectPtr<USceneComponent> Turntable; // turns (the sway)
	UPROPERTY() TObjectPtr<USceneComponent> Pivot;     // under it: moves the model's middle onto the turntable's axis
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Backdrop;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> KeyLight;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> RimLight;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> FillLight;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	FName Shown;
	float Clock = 0.f;
	// The three-quarter view: the gun turned this far toward the camera, and the camera's pitch down.
	static constexpr float SwayRate = 0.45f, CameraPitch = -12.f;
	float BaseYaw = -28.f, SwayYaw = 14.f, Pitch = CameraPitch;
	FVector2D Shape = Size;     // the picture's shape (Frame fits the gun to it)
	bool bPicture = false;
	int32 PictureFrames = 0;    // picture mode: captures still to take
	void SetLights(bool bOn);
	void Frame();
};
