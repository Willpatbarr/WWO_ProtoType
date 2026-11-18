#pragma once

// The menus' shared style: palette, fonts, brushes and the small widget builders every menu uses
// (settings, character, launch, scoreboard, firing tester). Each builder takes the UWidgetTree to
// build into, so a menu builds its widgets in its own tree and needs no other menu to do it.
// - Sizes are Godot pixels (the prototype's 1600 x 900 layout) and are scaled by K here; fonts are
//   Godot pixels too (Font).
// - Colours are sRGB (WWO::Lin where they meet Slate, which these builders do).
// - Buttons: the cursor drives every menu, not focus, so every button is IsFocusable = false. A
//   button's click goes through a UWWOClickHandler (OnClick), which plays ui_click first. Something
//   must keep that handler alive (the delegate holds it weakly): the menu keeps it in a UPROPERTY
//   array, for good (built once), until it rebuilds, or pruned as its button leaves (KeepPruned).

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Styling/SlateBrush.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Margin.h"
#include "Types/SlateEnums.h"
#include "Components/SlateWrapperTypes.h"
#include "WWOUI.generated.h"

class UWidget;
class UWidgetTree;
class UTexture2D;
class UUserWidget;
class UTextBlock;
class UButton;
class UBorder;
class UHorizontalBox;
class UVerticalBox;
class UEditableTextBox;
class UScrollBox;
class FSlateWindowElementList;
struct FGeometry;

// Small UObjects that forward UMG's dynamic delegates to C++ lambdas.
UCLASS()
class UWWOClickHandler : public UObject
{
	GENERATED_BODY()
public:
	TFunction<void()> Fn;
	TWeakObjectPtr<class UWidget> For; // the button it serves (when that's off the page, it can go)
	UFUNCTION() void Handle() { if (Fn) Fn(); }
};

UCLASS()
class UWWOSliderHandler : public UObject
{
	GENERATED_BODY()
public:
	TFunction<void(float)> Fn;
	TWeakObjectPtr<class UWidget> For;
	UFUNCTION() void Handle(float Value) { if (Fn) Fn(Value); }
};

namespace WWOUI
{
	constexpr float K = 1.2f; // Godot's 1600 x 900 sizes -> Unreal's 1080p reference

	// --- Palette (sRGB) ---------------------------------------------------------------------------
	// Where two menus used the same name for different values, both are kept (named apart) so each
	// menu still looks exactly as it did; see docs/CODE_REVIEW_2026-11.md, 3.1, for the list.
	inline const FLinearColor PageColor(0.1f, 0.11f, 0.13f);       // every menu's page (the character preview's backdrop matches it)
	inline const FLinearColor PanelColor(0.13f, 0.14f, 0.165f);    // a raised panel on the page (the settings page, the character editors)
	inline const FLinearColor PanelEdge(1.f, 1.f, 1.f, 0.18f);
	inline const FLinearColor CardColor(0.115f, 0.125f, 0.148f);   // a card, a key, a text box, a side button
	inline const FLinearColor CardHovered(0.17f, 0.18f, 0.21f);    // the settings menu's side and key buttons
	inline const FLinearColor CardPressed(0.22f, 0.23f, 0.26f);
	inline const FLinearColor CardEdge(1.f, 1.f, 1.f, 0.45f);      // settings, launch, scoreboard
	inline const FLinearColor CardEdgeStrong(1.f, 1.f, 1.f, 0.55f); // the character menu's cards
	inline const FLinearColor HoverEdge(1.f, 1.f, 1.f, 0.9f);      // a card's edge under the cursor
	inline const FLinearColor ButtonColor(0.2f, 0.21f, 0.25f);     // a filled text button (settings, firing tester)
	inline const FLinearColor RuleColor(1.f, 1.f, 1.f, 0.3f);      // the line under a page header
	inline const FLinearColor TabDim(1.f, 1.f, 1.f, 0.6f);         // a tab or page that isn't the open one
	inline const FLinearColor TabBar(1.f, 1.f, 1.f, 0.25f);        // the "|" between tabs
	inline const FLinearColor Dim(1.f, 1.f, 1.f, 0.6f);            // secondary text (launch, scoreboard, firing tester)
	inline const FLinearColor DimBright(1.f, 1.f, 1.f, 0.62f);     // the character menu's secondary text
	inline const FLinearColor Faint(1.f, 1.f, 1.f, 0.4f);          // key hints (launch, character, settings)
	inline const FLinearColor Fainter(1.f, 1.f, 1.f, 0.35f);       // the scoreboard's faint text
	inline const FLinearColor DefaultAccent(0.72f, 0.5f, 1.f);     // the accent with no flora (also the "//" mark)

	// The accent: the equipped flora's colour lightened 15%, or DefaultAccent.
	FLinearColor Accent(const UObject* WorldContext);

	// --- Type: every word the player sees is set in Poppins ----------------------------------------
	// The faces are read at run time from Content/WWO/Fonts/Poppins-<Weight>.ttf (SIL Open Font
	// License, staged with the game). A weight with no file falls back to the nearest one there is
	// (Light is always shipped), and only with no Poppins file at all to the engine's font.
	enum class EWeight : uint8 { Light, Regular, Medium, SemiBold, Bold };
	// The type scale, in Godot pixels: the HUD and the menus size their text from these.
	namespace Type
	{
		constexpr float Micro = 10.f;    // the smallest print: a source beside a note, the detail under a card's name in capitals
		constexpr float Caption = 12.f;  // key hints, spaced-capital labels over a section
		constexpr float Small = 13.f;    // secondary lines, notes, card sub-lines
		constexpr float Body = 15.f;     // ordinary text, card titles, HUD names
		constexpr float Title = 18.f;    // a card's or row's main words, header tabs
		constexpr float Heading = 24.f;  // the HUD's match line, overlay messages
		constexpr float Display = 44.f;  // the ammo count, PLAY, big numbers
	}
	// Text colours: words, secondary words, hints.
	inline const FLinearColor TextPrimary(1.f, 1.f, 1.f, 0.96f);
	inline const FLinearColor TextSecondary(1.f, 1.f, 1.f, 0.62f);
	inline const FLinearColor TextFaint(1.f, 1.f, 1.f, 0.4f);
	// Poppins at Points (Slate points, already scaled), in Weight. No outline: text that sits over the
	// world gets a soft shadow or a backing panel from whoever draws it (FWWOPainter::Text).
	FSlateFontInfo FontAt(float Points, EWeight Weight = EWeight::Regular);
	// The HUD's font: words always Poppins Light, digits Barlow in Weight (FWWOPainter::Font uses it).
	FSlateFontInfo HudFontAt(float Points, EWeight Weight = EWeight::Regular);
	// Poppins at Px Godot pixels in the menus' scale (K).
	FSlateFontInfo Font(float Px, EWeight Weight = EWeight::Regular);
	// Sets a text block's weight, keeping its size and letter spacing.
	void SetWeight(UTextBlock* T, EWeight Weight);
	// A one-line text that may be longer than the Width (layout units) it has: cut off at its own edge
	// with "...". A centred one that is too long is set from the left, where the cut can show.
	void Ellipsis(UTextBlock* T, float Width);

	// --- Boxes: one corner, one edge, one grid -----------------------------------------------------
	constexpr float Corner = 6.f;      // every rounded box's corner radius (Godot pixels)
	constexpr float BoxEdge = 1.5f;     // a box's ordinary edge
	constexpr float BoxEdgeOn = 2.5f;   // the edge of the chosen card or the open slot
	constexpr float Grid = 4.f;        // spacing and padding are multiples of this
	// A rounded box (Radius and OutlineWidth in Godot pixels).
	FSlateBrush RoundBrush(const FLinearColor& Color, float Radius = Corner, const FLinearColor& Outline = FLinearColor(0, 0, 0, 0), float OutlineWidth = 0.f);
	// A hover tip in the menus' style (a dark card, Poppins), in place of the engine's default tooltip.
	void SetTip(UWidgetTree* Tree, UWidget* Widget, const FString& Tip);
	// A scroll box with the menus' scroll bar: a thin, faint, rounded thumb on no track.
	UScrollBox* ScrollBox(UWidgetTree* Tree);
	// The game's logo as a widget (Tools/import_ui_logos.py): the "WHAT WAS OURS" wordmark (bWordmark)
	// or the round leaf mark, drawn Size big (the wordmark's texture is 8 : 1 and the letters fill
	// its width; the mark is square). Keeps, if given, is put under it unseen: the text the logo
	// stands in for, so everything around stays exactly where it was.
	UWidget* MakeLogo(UWidgetTree* Tree, bool bWordmark, FVector2D Size, UWidget* Keeps = nullptr);
	// The UI symbols (Tools/make_symbols.py, import_symbols.py) by kind ("Flora", "Grenade") and item id
	// (voltvine; volt_grenade): the badge (white symbol on a dark shade of the item's colour) or the bare
	// symbol (white on clear). Null for an item with no symbol.
	UTexture2D* SymbolTexture(FName Kind, FName Id, bool bBadge = true);
	UWidget* MakeSymbol(UWidgetTree* Tree, FName Kind, FName Id, FVector2D Size, bool bBadge = true);
	// A symbol filling whatever holds it (cropped to its shape if that isn't square).
	UWidget* FillWithSymbol(UWidgetTree* Tree, UTexture2D* Texture);
	// The colour at a badge's edge (Tools/make_symbols.py's EDGE_SHADE): a card round a badge filled with it shows no seam.
	inline FLinearColor BadgeEdge(const FLinearColor& ItemColor) { return FLinearColor(ItemColor.R * 0.2f, ItemColor.G * 0.2f, ItemColor.B * 0.2f); }
	inline UTexture2D* FloraSymbolTexture(FName FloraId, bool bBadge) { return SymbolTexture("Flora", FloraId, bBadge); }
	inline UWidget* MakeFloraSymbol(UWidgetTree* Tree, FName FloraId, FVector2D Size, bool bBadge = true) { return MakeSymbol(Tree, "Flora", FloraId, Size, bBadge); }
	// Godot's ring cursor, drawn over an open menu (see FWWOMenuCursor).
	void PaintCursor(const UUserWidget* Menu, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer);

	// --- Small builders -----------------------------------------------------------------------------
	UTextBlock* Text(UWidgetTree* Tree, const FString& Str, float FontPx, const FLinearColor& Color = FLinearColor::White, bool bWrap = false);
	// Capitals with wide letter spacing: the menus' label style.
	UTextBlock* Spaced(UWidgetTree* Tree, const FString& Str, float FontPx, const FLinearColor& Color, int32 Spacing, bool bWrap = false);
	// A thin line across, Thickness slate units tall (not scaled).
	UWidget* Rule(UWidgetTree* Tree, const FLinearColor& Color, float Thickness = 1.5f);
	// A thin line down, 1.5 units wide; Length (Godot pixels) > 0 fixes its height.
	UWidget* RuleDown(UWidgetTree* Tree, const FLinearColor& Color, float Length = 0.f);
	// At least MinWidth x MinHeight (Godot pixels; 0 leaves that side alone).
	UWidget* Sized(UWidgetTree* Tree, UWidget* Inside, float MinWidth, float MinHeight);
	// Exactly Width wide (Godot pixels).
	UWidget* Wide(UWidgetTree* Tree, UWidget* Inside, float Width);
	// A box or panel slot's share of the room left (FSlateChildSize Fill).
	FSlateChildSize Fill(float Value = 1.f);
	// The text box style (the settings search box, the launch screen's gamer tag): a card with the
	// card edge, the accent's edge while typing in it.
	void StyleTextBox(UEditableTextBox* Box, float FontPx, const FMargin& PaddingPx, const FLinearColor& AccentColor);

	// --- Page header and tabs ----------------------------------------------------------------------
	// The left of a page header: the "//" mark (the round logo over it, with bLogo) and Title in spaced capitals.
	UWidget* HeaderName(UWidgetTree* Tree, const FString& Title, bool bLogo);
	// A row of tabs: each a spaced label over a 3 px line, with a "|" between them. Wrap puts a tab's
	// content in what gets clicked (a button whose click handler the menu keeps) or returns it as it is.
	struct FTabStrip
	{
		UHorizontalBox* Box = nullptr;
		TArray<UTextBlock*> Labels;
		TArray<UBorder*> Lines;
		TArray<UWidget*> Tabs; // each tab as Wrap returned it
		TArray<UWidget*> Bars; // the "|" before each tab (null for the first)
	};
	FTabStrip TabStrip(UWidgetTree* Tree, const TArray<FString>& Names, TFunctionRef<UWidget*(UWidget* Content, int32 Index)> Wrap);
	// The open tab white over a line in the accent; the others dim, with no line.
	void SetActiveTab(const TArray<TObjectPtr<UTextBlock>>& Labels, const TArray<TObjectPtr<UBorder>>& Lines, int32 Active, const FLinearColor& AccentColor);
	// A page header (Name, then Middle, then Right filling the rest with RightAlign) and the rule under
	// it, added to the page's Column.
	void AddPageHeader(UWidgetTree* Tree, UVerticalBox* Column, UWidget* Name, UWidget* Middle, UWidget* Right, EHorizontalAlignment RightAlign = HAlign_Fill);

	// --- Buttons ------------------------------------------------------------------------------------
	struct FButtonLook
	{
		FSlateBrush Normal, Hovered, Pressed;
		FMargin Padding = FMargin(0.f);        // around the content, Godot pixels
		EHorizontalAlignment HAlign = HAlign_Fill;
		EVerticalAlignment VAlign = VAlign_Fill;
	};
	// A filled button, lighter when hovered (+0.08) and pressed (+0.15): the settings menu's.
	FButtonLook FilledLook(const FLinearColor& Color, const FMargin& Padding, bool bLeftAlign = false);
	// A card: Fill inside an Edge EdgeWidth wide, 5 px corners. Hovered and pressed it lightens and
	// its edge goes white; a see-through one (Fill.A == 0) gets only a faint wash and keeps its edge.
	FButtonLook CardLook(const FLinearColor& Fill, const FLinearColor& Edge, float EdgeWidth, const FMargin& Padding, EVerticalAlignment VAlign = VAlign_Fill);
	// No box of its own (a tab, a sidebar line): only a faint wash when hovered (CardLook with nothing).
	FButtonLook PlainLook();
	// The same brush in every state (a button that can't be pressed).
	FButtonLook StillLook(const FSlateBrush& Brush, const FMargin& Padding);

	// A button around Content in that look, IsFocusable off. Nothing happens on a click until OnClick.
	UButton* Button(UWidgetTree* Tree, UWidget* Content, const FButtonLook& Look);
	// A button with a line of text (centered, or left with bLeftAlign); the text doesn't wrap unless
	// bWrap (Godot buttons don't: the text sets their width).
	UButton* TextButton(UWidgetTree* Tree, const FString& Label, float FontPx, const FButtonLook& Look, bool bWrap = false);
	// Clicking the button plays ui_click, then Fn. Returns the handler: the caller keeps it alive.
	UWWOClickHandler* OnClick(UButton* B, UObject* Owner, TFunction<void()> Fn);
	// Adds Handler to Handlers, and now and then drops those (click or slider) whose widget is no
	// longer under a menu's root: for pages rebuilt often, each time with new buttons.
	void KeepPruned(TArray<TObjectPtr<UObject>>& Handlers, int32& PruneAt, UObject* Handler);
}
