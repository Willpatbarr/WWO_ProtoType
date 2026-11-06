#pragma once

// Character screen (Godot: character_menu.gd). Touchpad, Create or I opens and closes it.
// Left: three loadout tiles (Flora, Weapons, Frame Parts); picking one expands it.
// - Flora (layout "B"): rows for Flora, Abilities (grenade + two abilities) and Traits.
//   Clicking a slot opens a picker row; picking an ability in the other slot swaps them.
// - Weapons: the equipped weapon and a 2-column picker with stats.
// - Frame Parts: a row per slot and the net effect.
// Right: the character, rendered by a scene capture of a mannequin that follows the loadout,
// framed like Godot's preview camera (45 degree vertical FOV, 4.4 m away, 1.1 m above the feet).
// Circle / Esc closes a picker, then the tile, then the menu. Pauses the game.

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/Actor.h"
#include "WWOCharacterMenu.generated.h"

class UVerticalBox;
class UScrollBox;
class UWidgetSwitcher;
class UTextBlock;
class UButton;
class UImage;
class UWWOSettingsMenu;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UWWOBodyComponent;

// The mannequin the character menu shows, far away from the level, with its own camera.
UCLASS()
class WHATWASOURS_API AWWOPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	AWWOPreviewActor();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneCaptureComponent2D> Capture;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UWWOBodyComponent> Body;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> Target;
	UPROPERTY() TObjectPtr<USceneComponent> Turntable;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Backdrop;

	// The preview's area in the Godot layout: what's left of the menu row beside the tiles.
	static inline const FVector2D Size = FVector2D(592.f, 693.f);
	// The backdrop's color, so it comes out as the panel's (0.1, 0.11, 0.13) after the capture's
	// tonemapping, at BackdropBias; at another exposure it's scaled to match (BackdropFor).
	static inline const FLinearColor BackdropColor = FLinearColor(0.0366f, 0.0381f, 0.0417f);
	static constexpr float BackdropBias = 5.5f;
	static constexpr float ExposureBias = 1.f;
	static FLinearColor BackdropFor(float Bias) { return BackdropColor * FMath::Pow(2.f, (BackdropBias - Bias) / 2.2f); }
	// Godot's preview lights: a DirectionalLight3D (energy 1, no shadows) and an ambient color of
	// (0.5, 0.5, 0.55), here a key light and a fill from the camera. They light only lighting
	// channel 2, which only the preview body and its grafts are on (the level sits above them and
	// would shade the level's own sun).
	UPROPERTY() TObjectPtr<class UDirectionalLightComponent> KeyLight;
	UPROPERTY() TObjectPtr<class UDirectionalLightComponent> FillLight;
};

UCLASS()
class WHATWASOURS_API UWWOCharacterMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	bool IsOpen() const { return bOpen; }
	void Open();
	void Close();
	void Back();
	FString DebugState() const; // for the autotest
	void AutoTestShow(const FString& Tile, const FString& PickerName); // for the autotest
	void AutoTestScroll(float Offset);
	AWWOPreviewActor* GetPreviewActor() const { return PreviewActor; } // for the autotest

private:
	bool bOpen = false;
	bool bBuilt = false;
	FString Expanded;  // "", "Flora", "Weapons" or "Frame Parts"
	FString Picker;    // "", "flora", "grenade", "ability0", "ability1" or "traits"
	UPROPERTY() TObjectPtr<UWidgetSwitcher> TileSwitcher;
	UPROPERTY() TObjectPtr<UTextBlock> ExpandedTitle;
	UPROPERTY() TObjectPtr<UScrollBox> ExpandedScroll;
	UPROPERTY() TObjectPtr<UVerticalBox> Content;
	UPROPERTY() TObjectPtr<UWidget> ModRowsStart; // the mod slots label (autotest scrolls to it)
	UPROPERTY() TObjectPtr<UImage> Preview;
	UPROPERTY() TObjectPtr<AWWOPreviewActor> PreviewActor;
	UPROPERTY() TObjectPtr<UWWOSettingsMenu> Styler; // borrows the settings menu's widget helpers

	void Build();
	void Expand(const FString& Tile);
	void Collapse();
	void Rebuild();
	void Refresh();
	void TogglePicker(const FString& Name);
	void AddRowLabel(const FString& Text);
	void AddContent(UWidget* Widget);
	UWidget* Slot(const FString& Text, FVector2D Size, const FLinearColor& Color, float FontPx, TFunction<void()> Fn,
		const FLinearColor* Border = nullptr, float BorderWidth = 2.f, const FString& Tooltip = FString(), bool bWrap = false);
	void BuildFloraRows();
	void BuildWeaponRows();
	void BuildModRows();
	void BuildFrameRows();
};
