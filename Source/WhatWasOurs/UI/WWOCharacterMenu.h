#pragma once

// Character screen (Godot: character_menu.gd). Touchpad / I opens and closes it.
// Left: three loadout tiles (Flora, Weapons, Frame Parts); picking one expands it.
// - Flora (layout "B"): rows for Flora, Abilities (grenade + two abilities) and Traits.
//   Clicking a slot opens a picker row; picking an ability in the other slot swaps them.
// - Weapons: the equipped weapon and a 2-column picker with stats.
// - Frame Parts: a row per slot and the net effect.
// Right: the character, rendered by a scene capture of a mannequin that follows the loadout.
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
};

UCLASS()
class WHATWASOURS_API UWWOCharacterMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;

	bool IsOpen() const { return bOpen; }
	void Open();
	void Close();
	void Back();

private:
	bool bOpen = false;
	bool bBuilt = false;
	FString Expanded;  // "", "Flora", "Weapons" or "Frame Parts"
	FString Picker;    // "", "flora", "grenade", "ability0", "ability1" or "traits"
	UPROPERTY() TObjectPtr<UWidgetSwitcher> TileSwitcher;
	UPROPERTY() TObjectPtr<UTextBlock> ExpandedTitle;
	UPROPERTY() TObjectPtr<UScrollBox> ExpandedScroll;
	UPROPERTY() TObjectPtr<UVerticalBox> Content;
	UPROPERTY() TObjectPtr<UImage> Preview;
	UPROPERTY() TObjectPtr<AWWOPreviewActor> PreviewActor;
	UPROPERTY() TObjectPtr<UWWOSettingsMenu> Styler; // borrows the settings menu's widget helpers

	void Build();
	void Expand(const FString& Tile);
	void Collapse();
	void Rebuild();
	void TogglePicker(const FString& Name);
	void AddRowLabel(const FString& Text);
	UButton* Slot(const FString& Text, FVector2D Size, const FLinearColor& Color, const FLinearColor& Border, float BorderWidth, float FontPx, TFunction<void()> Fn);
	void BuildFloraRows();
	void BuildWeaponRows();
	void BuildFrameRows();
};
