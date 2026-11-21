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
// - Weapons: one screen, no scrolling: the weapons with a render of the selected gun under them
//   (AWWOGunPreviewActor), the frame's stat bars and perks, its mod slots, and the saved configs
//   (the hovered slot's mods take their place).
// - Frame Parts: one screen too: a row per slot (each part's card with its picture, AWWOPartPictures), the shell colours (which part, which colour, a row of swatches)
//   and the net effect.
// Right: the character, rendered by a scene capture of a mannequin that follows the loadout,
// framed like Godot's preview camera (45 degree vertical FOV, 4.4 m away, 1.1 m above the feet).
// Circle / Esc closes a picker, then the tile, then the menu. Pauses the game.

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/WWOPreviewActor.h"
#include "WWOCharacterMenu.generated.h"

class UVerticalBox;
class UHorizontalBox;
class UScrollBox;
class UWidgetSwitcher;
class UTextBlock;
class UButton;
class UImage;
namespace WWOCharacterMenuUI { struct FItemCard; }

// The "hover opens it, leaving closes it a moment later" timer both of the menu's hover popups use
// (the Loadout tab's row of choices, the Weapon tab's mod list). The caller decides when one opens;
// this says when an open one closes: after CloseAfter seconds with the cursor on neither the
// thing that opened it nor the popup.
struct FWWOHoverPopup
{
	explicit FWWOHoverPopup(float InCloseAfter) : CloseAfter(InCloseAfter) {}
	void Reset() { Idle = 0.f; }
	// Called each frame. While it's closed, or the cursor is on the anchor or the popup, the wait starts over.
	bool ShouldClose(bool bIsOpen, bool bOverAnchorOrPopup, float DeltaTime)
	{
		if (!bIsOpen || bOverAnchorOrPopup)
		{
			Idle = 0.f;
			return false;
		}
		Idle += DeltaTime;
		return Idle > CloseAfter;
	}

private:
	float CloseAfter = 0.f;
	float Idle = 0.f;
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
	// For the hover test (Player/WWOAutoTestHover.cpp), in Slate's absolute (desktop) space:
	TArray<FString> AutoTestHoverTiles() const;                          // the Loadout tab's hover tiles, by name
	bool AutoTestTileRect(const FString& Name, FSlateRect& Out) const;   // one of them
	const FString& AutoTestHoverRow() const { return HoverPicker; }      // the open row of choices ("" if none)
	bool AutoTestHoverRects(FSlateRect& OutPanel, TArray<FSlateRect>& OutChoices) const; // the open row and each choice in it
	bool AutoTestHoverScrolls() const { return HoverScroll != nullptr; } // the row was too many to fit and scrolls
	bool AutoTestModRects(FSlateRect& OutPanel, TArray<FSlateRect>& OutSlots) const;     // the Weapon tab's open slot mods and the slots
	bool AutoTestConfigListRect(FSlateRect& Out) const;                  // the Weapon tab's config list

private:
	bool bOpen = false;
	bool bBuilt = false;
	FString Expanded;  // "", "Flora", "Weapons" or "Frame Parts"
	FString Picker;    // "", "flora", "grenade", "ability0", "ability1" or "traits"
	UPROPERTY() TObjectPtr<UWidgetSwitcher> TileSwitcher;
	UPROPERTY() TObjectPtr<UScrollBox> ExpandedScroll;
	UPROPERTY() TObjectPtr<UVerticalBox> Content;
	UPROPERTY() TObjectPtr<UWidget> ModRowsStart; // the mod slots label (autotest scrolls to it)
	UPROPERTY() TObjectPtr<UWidget> Preview; // the character beside the editors
	UPROPERTY() TObjectPtr<AWWOPreviewActor> PreviewActor;
	UPROPERTY() TObjectPtr<class AWWOGunPreviewActor> GunPreview; // the selected gun on the Weapon tab
	FString GunKey; // the weapon and mods it was last built with
	// The shell part pictures (the Loadout tab's part cards, the Shell tab's part cards), taken while the menu is open.
	UPROPERTY() TObjectPtr<class AWWOPartPictures> PartPictures;
	// Part's picture (in FrameSlot) in the loadout's colours for that slot; null if there is none to show.
	class UMaterialInterface* PartPicture(const class UWWOFramePartData* Part, FName FrameSlot, bool bSmall = false) const; // bSmall: the Shell tab's
	// A picture as a widget, at least Size (Godot pixels) and scaled to fit what it's given, its shape kept.
	UWidget* PictureWidget(class UMaterialInterface* Mat, FVector2D Size);

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
	UPROPERTY() TArray<TObjectPtr<UObject>> SlotHandlers; // the editors' slot buttons (Slot), dropped as they leave the page
	int32 SlotPruneAt = 512;
	void SetPreviewLive(bool bLive); // the character preview renders only while the menu is open
	// Turning the character: the right stick, A and D, or dragging it with the mouse.
	bool bMouseWasDown = false, bDragging = false;
	FVector2D LastCursor = FVector2D::ZeroVector;
	void UpdateTurn(float DeltaTime);
	// Loadout tab: the tiles that offer other choices when hovered (the flora, grenade, abilities,
	// traits and the two weapon slots), and the row of choices that's open.
	TArray<TPair<TWeakObjectPtr<UWidget>, FString>> HoverTiles;
	FString HoverPicker;
	FWWOHoverPopup HoverClose{ 0.25f }; // the row closes this long after the cursor leaves it and its tile
	UPROPERTY() TObjectPtr<UWidget> HoverPopup;
	UPROPERTY() TObjectPtr<UScrollBox> HoverScroll; // the open row's scroll box, when it has more rows than fit
	UPROPERTY() TObjectPtr<UWidget> ConfigListBox; // the Weapon tab's config list (for the hover test)
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
	// The editors' card (WWOCharacterMenuPrivate.h, FItemCard) and a row of them on a grid of Columns
	// equal columns, added to the page; Fn is the click.
	UWidget* ItemCard(const WWOCharacterMenuUI::FItemCard& Spec, TFunction<void()> Fn);
	void AddCardGrid(const TArray<UWidget*>& Cards, int32 Columns);
	void BuildFloraRows();
	void BuildWeaponRows();
	void BuildModRows(UHorizontalBox* Columns);
	// Weapon mods (draft): the open slot's popup and the hover preview on the stat bars.
	static constexpr float ModSlotHeight = 82.f; // Godot pixels per slot, padding included
	// The Weapon tab's four columns (Godot pixels; with their gaps they fit the 1464 the panel has
	// at 16:9): weapons, stat bars and perks, mod slots, configs or the open slot's mods.
	static constexpr float WeaponColumn = 340.f;
	static constexpr float StatsColumn = 380.f;
	static constexpr float SlotColumn = 196.f;
	static constexpr float SideColumn = 440.f;
	static constexpr float ConfigListHeight = 540.f; // the config list scrolls inside this, the page never
	FName OpenSlot;
	FWWOHoverPopup ModClose{ 0.35f }; // the open slot's mods close this long after the cursor leaves them and the slots
	FName AutoTestPreview; // set by the autotest: a mod id to preview as if hovered
	UPROPERTY() TObjectPtr<class UWWOStatBars> StatBars;
	UPROPERTY() TObjectPtr<UWidget> PopupPanel;
	TArray<TPair<TWeakObjectPtr<UButton>, FName>> SlotButtons;
	TArray<TPair<TWeakObjectPtr<UButton>, TWeakObjectPtr<class UWWOModData>>> PopupButtons;
	void OpenModSlot(FName ModSlot);
	void UpdateModHover(float DeltaTime);
	void UpdateStatBars(const class UWWOModData* PreviewMod, FName PreviewSlot, bool bPreview);
	void BuildFrameRows();
	// The Shell tab's colour picker (under the parts): which slot it paints (INDEX_NONE: all four) and which of the
	// slot's colours (FWWOEquipped::EShellRole), then a row of swatches and Default.
	int32 PaintTarget = INDEX_NONE;
	int32 PaintRole = 0;
	void BuildColourRows(class UWWOLoadoutSubsystem* L, TFunctionRef<void(const FString&)> Label); // Label: the tab's section name
};
