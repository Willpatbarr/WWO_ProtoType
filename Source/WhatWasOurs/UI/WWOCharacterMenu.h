#pragma once

// Character screen (Godot: character_menu.gd). Touchpad, Create or I opens and closes it.
// It fills the window. A header carries four tabs, Loadout, Flora, Weapon, Shell (the frame
// parts; bumpers or Q / E cycle them):
// - Loadout: the frame's four stat bars and the build list on the left, the character in the
//   middle, and on the right the flora, grenade, abilities, traits, weapon and frame parts as
//   cards. Clicking a card opens the tab that edits it.
// - Flora, Weapon, Shell: the editors below, beside the character (Weapon takes the full width).
// The character stands still; the right stick, A / D or dragging it with the mouse turns it.
// The editors:
// - Flora (layout "B"): rows for Flora, Abilities (grenade + two abilities) and Traits.
//   Clicking a slot opens a picker row; picking an ability in the other slot swaps them.
// - Weapons: one screen, no scrolling: the weapons, the frame's stat bars and perks, its mod
//   slots, and the saved configs (the hovered slot's mods take their place).
// - Frame Parts: one screen too: a row per slot and the net effect.
// Right: the character, rendered by a scene capture of a mannequin that follows the loadout,
// framed like Godot's preview camera (45 degree vertical FOV, 4.4 m away, 1.1 m above the feet).
// Circle / Esc closes a picker, then the tile, then the menu. Pauses the game.

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/Actor.h"
#include "WWOCharacterMenu.generated.h"

class UVerticalBox;
class UHorizontalBox;
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
	UPROPERTY() TObjectPtr<class UDirectionalLightComponent> KeyLight;
	UPROPERTY() TObjectPtr<class UDirectionalLightComponent> FillLight;
};

UCLASS()
class WHATWASOURS_API UWWOCharacterMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	bool IsOpen() const { return bOpen; }
	void Open();
	void Close();
	void Back();
	void CycleTab(int32 Step); // bumpers / Q and E: Loadout, Frame, Flora, Weapon
	FString DebugState() const; // for the autotest
	void AutoTestShow(const FString& Tile, const FString& PickerName); // for the autotest
	void AutoTestScroll(float Offset);
	float ScrollOverflow() const; // how far the open tab's page could scroll (0: it all fits), for the autotest
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
	UPROPERTY() TObjectPtr<UWidget> Preview; // the character beside the editors
	UPROPERTY() TObjectPtr<AWWOPreviewActor> PreviewActor;
	UPROPERTY() TObjectPtr<UWWOSettingsMenu> Styler; // borrows the settings menu's widget helpers

	void Build();
	// The full-window layout: a header with the four tabs over a switcher. Loadout is the overview
	// (stats and the build list, the character, what's equipped); the other tabs are the editors.
	void BuildOverview();
	void SetTab(int32 Index);
	void UpdateTabs();
	int32 TabIndex() const;
	UPROPERTY() TObjectPtr<UHorizontalBox> Overview;
	UPROPERTY() TObjectPtr<UWidget> OverviewPreview; // the character on the Loadout tab
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> TabLabels;
	UPROPERTY() TArray<TObjectPtr<class UBorder>> TabLines;
	UPROPERTY() TArray<TObjectPtr<UObject>> CardHandlers; // the overview cards' click forwarders
	UPROPERTY() TArray<TObjectPtr<UObject>> HeaderHandlers; // the four tabs' (built once; BuildOverview empties CardHandlers)
	void SetPreviewLive(bool bLive); // the character preview renders only while the menu is open
	// Turning the character: the right stick, A and D, or dragging it with the mouse.
	bool bMouseWasDown = false, bDragging = false;
	FVector2D LastCursor = FVector2D::ZeroVector;
	void UpdateTurn(float DeltaTime);
	// Loadout tab: the tiles that offer other choices when hovered (the flora, grenade, abilities,
	// traits and the two weapon slots), and the row of choices that's open.
	TArray<TPair<TWeakObjectPtr<UWidget>, FString>> HoverTiles;
	FString HoverPicker;
	float HoverIdle = 0.f;
	UPROPERTY() TObjectPtr<UWidget> HoverPopup;
	void UpdateHoverPicker(float DeltaTime);
	void OpenHoverPicker(const FString& Name, UWidget* Tile);
	void CloseHoverPicker();
	// The Loadout tab's four stat bars, kept so a hovered shell part can show on them.
	struct FStatBar
	{
		FName Group;
		TObjectPtr<class UBorder> Have, Shift, Rest; // what you have, the hovered part's piece, the empty rest
		TObjectPtr<UTextBlock> Value, Change;
	};
	TArray<FStatBar> StatBars4;
	TArray<TPair<TWeakObjectPtr<UWidget>, TWeakObjectPtr<class UWWOFramePartData>>> HoverParts; // the open row's shell parts
	TWeakObjectPtr<class UWWOFramePartData> PreviewPart;
	bool bPreviewEquipped = false;
	void UpdateStatPreview(const class UWWOFramePartData* Part, bool bEquipped);
	// The Build notes' rows and the item each comes from; the hovered item's rows are outlined.
	TArray<TPair<TObjectPtr<class UBorder>, FString>> NoteRows;
	TArray<TPair<TWeakObjectPtr<UWidget>, FString>> HoverNames; // the open row's choices, by item name
	FString OutlinedSource;
	void UpdateNoteOutline(const FString& Source);
	void Expand(const FString& Tile);
	void Collapse();
	void Rebuild();
	void Refresh();
	void TogglePicker(const FString& Name);
	void AddRowLabel(const FString& Text);
	UWidget* Card(UWidget* Inside, TFunction<void()> Fn, const FLinearColor& Fill, const FLinearColor& Edge, float EdgeWidth, const FMargin& Padding);
	void AddContent(UWidget* Widget);
	UWidget* Slot(const FString& Text, FVector2D Size, const FLinearColor& Color, float FontPx, TFunction<void()> Fn,
		const FLinearColor* Border = nullptr, float BorderWidth = 2.f, const FString& Tooltip = FString(), bool bWrap = false);
	void BuildFloraRows();
	void BuildWeaponRows();
	void BuildModRows(UHorizontalBox* Columns);
	// Weapon mods (draft): the open slot's popup and the hover preview on the stat bars.
	static constexpr float ModSlotHeight = 82.f; // Godot pixels per slot, padding included
	// The Weapon tab's four columns (Godot pixels; with their gaps they fit the 1464 the panel has
	// at 16:9): weapons, stat bars and perks, mod slots, configs or the open slot's mods.
	static constexpr float WeaponColumn = 290.f;
	static constexpr float StatsColumn = 430.f;
	static constexpr float SlotColumn = 196.f;
	static constexpr float SideColumn = 440.f;
	static constexpr float ConfigListHeight = 540.f; // the config list scrolls inside this, the page never
	FName OpenSlot;
	float PopupIdle = 0.f;
	FName AutoTestPreview; // set by the autotest: a mod id to preview as if hovered
	UPROPERTY() TObjectPtr<class UWWOStatBars> StatBars;
	UPROPERTY() TObjectPtr<UWidget> PopupPanel;
	TArray<TPair<TWeakObjectPtr<UButton>, FName>> SlotButtons;
	TArray<TPair<TWeakObjectPtr<UButton>, TWeakObjectPtr<class UWWOModData>>> PopupButtons;
	void OpenModSlot(FName ModSlot);
	void UpdateModHover(float DeltaTime);
	void UpdateStatBars(const class UWWOModData* PreviewMod, FName PreviewSlot, bool bPreview);
	void BuildFrameRows();
};
