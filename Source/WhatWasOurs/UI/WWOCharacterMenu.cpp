#include "UI/WWOCharacterMenu.h"
#include "UI/WWOCharacterMenuPrivate.h"
#include "Systems/WWOSound.h"
#include "UI/WWOUI.h"
#include "Player/WWOPlayerController.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "WWOTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/ScaleBox.h"
#include "UI/WWOMenuCursor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"

using namespace WWOCharacterMenuUI;

namespace
{
	const TCHAR* const TabNames[] = { TEXT("LOADOUT"), TEXT("FLORA"), TEXT("WEAPON"), TEXT("SHELL") }; // the shell is the frame parts
	const TCHAR* const TabTiles[] = { TEXT(""), TEXT("Flora"), TEXT("Weapons"), TEXT("Frame Parts") };
	const FVector PreviewSpot(0.f, 0.f, -50000.f); // far below the level
}

// --- Menu ------------------------------------------------------------------------------------

void UWWOCharacterMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	Build();
}

int32 UWWOCharacterMenu::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!bOpen) return Layer;
	WWOUI::PaintCursor(this, AllottedGeometry, OutDrawElements, Layer + 1);
	return Layer + 1;
}

void UWWOCharacterMenu::AddRowLabel(const FString& Text)
{
	// A section's name: small spaced capitals, like the Loadout tab's labels.
	UTextBlock* Label = WWOUI::Spaced(WidgetTree, Text, 13.f, FLinearColor(1, 1, 1, 0.6f), 220);
	Content->AddChildToVerticalBox(Label)->SetPadding(FMargin(0, Content->GetChildrenCount() > 0 ? 22.f * K : 0.f, 0, 2.f * K));
}

void UWWOCharacterMenu::AddContent(UWidget* Widget)
{
	Content->AddChildToVerticalBox(Widget)->SetPadding(FMargin(0, Content->GetChildrenCount() > 0 ? 12.f * K : 0.f, 0, 0));
}

UWidget* UWWOCharacterMenu::Slot(const FString& Text, FVector2D Size, const FLinearColor& Color, float FontPx, TFunction<void()> Fn,
	const FLinearColor* Border, float BorderWidth, const FString& Tooltip, bool bWrap)
{
	// Godot's _make_slot: a button at least Size, centered text (font 20 unless set), 8 px corners,
	// a 2 px border in the color lightened 35% (or _set_slot_border's), lighter when hovered/pressed.
	const FLinearColor Edge = Border ? *Border : WWO::Lightened(Color, 0.35f);
	const float Width = Border ? BorderWidth : 2.f;
	WWOUI::FButtonLook Look = WWOUI::FilledLook(Color, FMargin(Width + 4.f, Width));
	Look.Normal = WWOUI::RoundBrush(Color, 5.f, Edge, Width); // the menu's 5 px corners
	Look.Hovered = WWOUI::RoundBrush(WWO::Lightened(Color, 0.12f), 5.f, FLinearColor::White, Width);
	Look.Pressed = WWOUI::RoundBrush(WWO::Lightened(Color, 0.2f), 5.f, FLinearColor::White, Width);
	UButton* B = WWOUI::TextButton(WidgetTree, Text, FontPx, Look);
	WWOUI::KeepPruned(SlotHandlers, SlotPruneAt, WWOUI::OnClick(B, this, MoveTemp(Fn)));
	if (!Tooltip.IsEmpty()) B->SetToolTipText(FText::FromString(Tooltip));
	USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
	Box->SetMinDesiredWidth(Size.X * K);
	Box->SetMinDesiredHeight(Size.Y * K);
	if (bWrap)
	{
		// Godot's autowrap: the button keeps its minimum width and the text wraps inside it.
		Box->SetWidthOverride(Size.X * K);
		if (UTextBlock* Label = Cast<UTextBlock>(B->GetChildAt(0))) Label->SetWrapTextAt((Size.X - 2.f * (Width + 4.f)) * K);
	}
	Box->AddChild(B);
	return Box;
}

void UWWOCharacterMenu::Build()
{
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Root;
	SetCursor(EMouseCursor::None); // the menu draws Godot's ring cursor instead (NativePaint)
}

void UWWOCharacterMenu::Open()
{
	if (!bBuilt)
	{
		bBuilt = true;
		UWidgetTree* Tree = WidgetTree;
		UOverlay* Root = Cast<UOverlay>(WidgetTree->RootWidget);
		TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
		// The whole window, in the preview backdrop's shade so the character has no visible frame.
		UBorder* Page = Tree->ConstructWidget<UBorder>();
		Page->SetBrushColor(WWO::Lin(WWOUI::PageColor));
		Page->SetPadding(FMargin(40.f * K, 12.f * K, 40.f * K, 28.f * K));
		UOverlaySlot* PageSlot = Root->AddChildToOverlay(Page);
		PageSlot->SetHorizontalAlignment(HAlign_Fill);
		PageSlot->SetVerticalAlignment(VAlign_Fill);
		UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>();
		Page->SetContent(Column);

		// Header: the game's name, the tabs, the keys.
		TArray<FString> TabTitles;
		for (const TCHAR* Title : TabNames) TabTitles.Add(Title);
		WWOUI::FTabStrip Tabs = WWOUI::TabStrip(Tree, TabTitles, [this, WeakThis](UWidget* Tab, int32 i) -> UWidget*
		{
			return Card(Tab, [WeakThis, i] { if (WeakThis.IsValid()) WeakThis->SetTab(i); }, FLinearColor(0, 0, 0, 0), FLinearColor(0, 0, 0, 0), 0.f, FMargin(0));
		});
		for (int32 i = 0; i < Tabs.Labels.Num(); ++i)
		{
			TabLabels.Add(Tabs.Labels[i]);
			TabLines.Add(Tabs.Lines[i]);
		}
		UTextBlock* Keys = WWOUI::Spaced(WidgetTree, TEXT("Q / E  TABS   ·   A / D  OR DRAG  TURN   ·   ESC  BACK"), 11.f, WWOUI::Faint, 120);
		Keys->SetJustification(ETextJustify::Right);
		WWOUI::AddPageHeader(Tree, Column, WWOUI::HeaderName(Tree, TEXT("WHAT WAS OURS"), true), Tabs.Box, Keys);

		// The character, shown on every tab (one render target, two places it can sit).
		PreviewActor = GetWorld()->SpawnActor<AWWOPreviewActor>(PreviewSpot, FRotator::ZeroRotator, WWO::AlwaysSpawn());
		auto MakePreview = [this, Tree]() -> UWidget*
		{
			UImage* Image = Tree->ConstructWidget<UImage>();
			if (PreviewActor && PreviewActor->Target)
			{
				FSlateBrush Brush;
				Brush.SetResourceObject(PreviewActor->Target);
				Brush.ImageSize = AWWOPreviewActor::Size * K;
				Image->SetBrush(Brush);
			}
			UScaleBox* Fit = Tree->ConstructWidget<UScaleBox>(); // keeps its shape, as big as fits
			Fit->SetStretch(EStretch::ScaleToFit);
			Fit->AddChild(Image);
			return Fit;
		};

		TileSwitcher = Tree->ConstructWidget<UWidgetSwitcher>();
		Column->AddChildToVerticalBox(TileSwitcher)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		// Loadout: three columns, filled by BuildOverview (the middle one is the character).
		Overview = Tree->ConstructWidget<UHorizontalBox>();
		OverviewPreview = MakePreview();
		TileSwitcher->AddChild(Overview);

		// The editors: the open tab's rows beside the character.
		UHorizontalBox* Editor = Tree->ConstructWidget<UHorizontalBox>();
		UBorder* ExpandedPanel = Tree->ConstructWidget<UBorder>();
		ExpandedPanel->SetBrush(WWOUI::RoundBrush(WWOUI::PanelColor, 6.f, WWOUI::PanelEdge, 1.f));
		ExpandedPanel->SetPadding(FMargin(28.f * K));
		ExpandedScroll = Tree->ConstructWidget<UScrollBox>();
		Content = Tree->ConstructWidget<UVerticalBox>();
		ExpandedScroll->AddChild(Content);
		ExpandedPanel->SetContent(ExpandedScroll);
		UHorizontalBoxSlot* PanelSlot = Editor->AddChildToHorizontalBox(ExpandedPanel);
		PanelSlot->SetSize(WWOUI::Fill(1.6f));
		PanelSlot->SetPadding(FMargin(0, 24.f * K, 0, 0));
		Preview = MakePreview();
		UHorizontalBoxSlot* PreviewSlot = Editor->AddChildToHorizontalBox(Preview);
		PreviewSlot->SetSize(WWOUI::Fill(1.f));
		PreviewSlot->SetHorizontalAlignment(HAlign_Fill);
		PreviewSlot->SetVerticalAlignment(VAlign_Fill);
		TileSwitcher->AddChild(Editor);
		HeaderHandlers = CardHandlers; // the tabs were made with Card: keep theirs past BuildOverview's reset
	}
	SetPreviewLive(true);
	if (PreviewActor) PreviewActor->Turntable->SetRelativeRotation(FRotator::ZeroRotator); // facing you
	BuildOverview();
	UWWOSound::PlayFlat(this, "ui_open");
	bOpen = true;
	Expanded.Reset();
	Picker.Reset();
	TileSwitcher->SetActiveWidgetIndex(0);
	UpdateTabs();
	SetVisibility(ESlateVisibility::Visible);
}

void UWWOCharacterMenu::Close()
{
	CloseHoverPicker();
	if (bOpen) UWWOSound::PlayFlat(this, "ui_close");
	bOpen = false;
	SetVisibility(ESlateVisibility::Collapsed);
	SetPreviewLive(false);
	if (UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this)) L->FlushSave();
}

void UWWOCharacterMenu::SetPreviewLive(bool bLive)
{
	// A scene capture left on renders the preview every frame of play, menu or no menu.
	if (!PreviewActor || !PreviewActor->Capture) return;
	PreviewActor->Capture->bCaptureEveryFrame = bLive;
	PreviewActor->Capture->SetVisibility(bLive);
	PreviewActor->SetActorTickEnabled(bLive);
}

void UWWOCharacterMenu::Back()
{
	if (!OpenSlot.IsNone())
	{
		OpenSlot = NAME_None;
		Refresh();
	}
	else if (!Picker.IsEmpty())
	{
		Picker.Reset();
		Refresh();
	}
	else if (!Expanded.IsEmpty())
	{
		Collapse();
	}
	else if (AWWOPlayerController* PC = Cast<AWWOPlayerController>(GetOwningPlayer()))
	{
		PC->ToggleCharacterMenu();
	}
}

void UWWOCharacterMenu::Expand(const FString& Tile)
{
	// The Weapons tile takes the whole width (it covers the character preview for now).
	if (Preview) Preview->SetVisibility(Tile == TEXT("Weapons") ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	OpenSlot = NAME_None;
	Expanded = Tile;
	Picker.Reset();
	Rebuild();
	ExpandedScroll->ScrollToStart();
	TileSwitcher->SetActiveWidgetIndex(1);
	UpdateTabs();
}

void UWWOCharacterMenu::Collapse()
{
	OpenSlot = NAME_None;
	Expanded.Reset();
	Picker.Reset();
	BuildOverview(); // what was changed in an editor shows on the cards
	TileSwitcher->SetActiveWidgetIndex(0);
	UpdateTabs();
}

void UWWOCharacterMenu::TogglePicker(const FString& Name)
{
	Picker = Picker == Name ? FString() : Name;
	Refresh();
}

void UWWOCharacterMenu::Refresh()
{
	// Rebuild the expanded tile in place, keeping the scroll position (Godot's _refresh).
	const float Scroll = ExpandedScroll->GetScrollOffset();
	Rebuild();
	ExpandedScroll->SetScrollOffset(Scroll);
}

void UWWOCharacterMenu::Rebuild()
{
	Content->ClearChildren();
	ModRowsStart = nullptr;
	if (Expanded == TEXT("Flora")) BuildFloraRows();
	else if (Expanded == TEXT("Weapons")) BuildWeaponRows();
	else if (Expanded == TEXT("Frame Parts")) BuildFrameRows();
}

UWidget* UWWOCharacterMenu::Card(UWidget* Inside, TFunction<void()> Fn, const FLinearColor& Fill, const FLinearColor& Edge, float EdgeWidth, const FMargin& Padding)
{
	// A bordered box you can click (or, with no Fn, just a box).
	UWidgetTree* Tree = WidgetTree;
	if (!Fn)
	{
		UBorder* Box = Tree->ConstructWidget<UBorder>();
		Box->SetBrush(WWOUI::RoundBrush(Fill, 5.f, Edge, EdgeWidth));
		Box->SetPadding(Padding * K);
		Box->SetContent(Inside);
		return Box;
	}
	UButton* B = WWOUI::Button(Tree, Inside, WWOUI::CardLook(Fill, Edge, EdgeWidth, Padding));
	CardHandlers.Add(WWOUI::OnClick(B, this, MoveTemp(Fn)));
	return B;
}

int32 UWWOCharacterMenu::TabIndex() const
{
	for (int32 i = 0; i < UE_ARRAY_COUNT(TabTiles); ++i) if (Expanded == TabTiles[i]) return i;
	return 0;
}

void UWWOCharacterMenu::SetTab(int32 Index)
{
	Index = (Index % UE_ARRAY_COUNT(TabTiles) + UE_ARRAY_COUNT(TabTiles)) % UE_ARRAY_COUNT(TabTiles);
	if (Index == TabIndex()) return;
	UWWOSound::PlayFlat(this, "ui_tab");
	if (Index == 0) Collapse();
	else Expand(TabTiles[Index]);
}

void UWWOCharacterMenu::CycleTab(int32 Step)
{
	SetTab(TabIndex() + Step);
}

void UWWOCharacterMenu::UpdateTabs()
{
	// The open tab is white over a line in the flora's color; the others are dim.
	WWOUI::SetActiveTab(TabLabels, TabLines, TabIndex(), WWOUI::Accent(this));
}

void UWWOCharacterMenu::UpdateTurn(float DeltaTime)
{
	// The character stands still until you turn it: the right stick (the left one moves the menu
	// cursor), A and D, or holding the mouse (or Cross) on it and dragging.
	if (!bOpen || !PreviewActor) return;
	constexpr float StickRate = 200.f, KeyRate = 160.f, DragRate = 0.45f; // degrees a second, and per pixel
	float Turn = 0.f;
	const float Stick = FWWOMenuCursor::RightStickX;
	if (FMath::Abs(Stick) > FWWOMenuCursor::Deadzone) Turn += Stick * StickRate * DeltaTime;
	if (const APlayerController* PC = GetOwningPlayer())
	{
		if (PC->IsInputKeyDown(EKeys::D)) Turn += KeyRate * DeltaTime;
		if (PC->IsInputKeyDown(EKeys::A)) Turn -= KeyRate * DeltaTime;
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication& App = FSlateApplication::Get();
		const bool bDown = App.GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
		const FVector2D Cursor = App.GetCursorPos();
		const UWidget* Shown = Expanded.IsEmpty() ? OverviewPreview.Get() : Preview.Get();
		if (bDown && !bMouseWasDown) bDragging = Shown && Shown->IsVisible() && Shown->GetCachedGeometry().IsUnderLocation(Cursor);
		if (!bDown) bDragging = false;
		if (bDragging) Turn += (Cursor.X - LastCursor.X) * DragRate;
		bMouseWasDown = bDown;
		LastCursor = Cursor;
	}
	// Seen from the camera, turning right is a negative yaw here.
	if (Turn != 0.f) PreviewActor->Turntable->AddRelativeRotation(FRotator(0.f, -Turn, 0.f));
}

void UWWOCharacterMenu::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	UpdateTurn(InDeltaTime);
	UpdateHoverPicker(InDeltaTime);
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bOpen && Expanded == TEXT("Weapons")) UpdateModHover(InDeltaTime);
}

// --- Autotest hooks -------------------------------------------------------------------------

FString UWWOCharacterMenu::DebugState() const
{
	return FString::Printf(TEXT("expanded '%s' picker '%s'"), *Expanded, *Picker);
}

void UWWOCharacterMenu::AutoTestShow(const FString& Tile, const FString& PickerName)
{
	// "mod_<slot>" opens that slot's popup; "mod_<slot>:<mod id>" also previews that mod.
	// An empty Tile is the Loadout tab: the overview, which the menu shows by collapsing to it. With a
	// PickerName there (flora, grenade, ability0...) the cursor goes onto that tile, so its hover row of
	// choices opens (UpdateHoverPicker) as it does for someone resting the mouse on it.
	if (Tile.IsEmpty())
	{
		if (!Expanded.IsEmpty() || PickerName.IsEmpty()) Collapse();
		if (PickerName.IsEmpty() && FSlateApplication::IsInitialized()) FSlateApplication::Get().SetCursorPos(FVector2D::ZeroVector); // off every tile
		for (const TPair<TWeakObjectPtr<UWidget>, FString>& Hover : HoverTiles)
		{
			if (PickerName.IsEmpty() || Hover.Value != PickerName || !Hover.Key.IsValid() || !FSlateApplication::IsInitialized()) continue;
			const FGeometry& Geo = Hover.Key->GetCachedGeometry();
			FSlateApplication::Get().SetCursorPos(Geo.LocalToAbsolute(Geo.GetLocalSize() * 0.5f));
		}
		return;
	}
	if (Expanded != Tile) Expand(Tile);
	FString Name = PickerName, ModId;
	PickerName.Split(TEXT(":"), &Name, &ModId);
	AutoTestPreview = ModId.IsEmpty() ? NAME_None : FName(*ModId);
	if (Name.StartsWith(TEXT("mod_")))
	{
		OpenSlot = FName(*Name.Mid(4));
		Picker.Reset();
	}
	else
	{
		OpenSlot = NAME_None;
		Picker = Name;
	}
	Refresh();
}

float UWWOCharacterMenu::ScrollOverflow() const
{
	// The Loadout tab (nothing expanded) has no scrolling list: the last tab's scroll box is stale.
	return ExpandedScroll && !Expanded.IsEmpty() ? ExpandedScroll->GetScrollOffsetOfEnd() : 0.f;
}

void UWWOCharacterMenu::AutoTestScroll(float Offset)
{
	// A negative offset scrolls the mod slots to the top.
	if (Offset < 0.f && ModRowsStart) ExpandedScroll->ScrollWidgetIntoView(ModRowsStart, false, EDescendantScrollDestination::TopOrLeft);
	else ExpandedScroll->SetScrollOffset(FMath::Min(Offset, ExpandedScroll->GetScrollOffsetOfEnd()));
}
