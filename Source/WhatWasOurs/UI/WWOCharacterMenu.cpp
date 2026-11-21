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
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/Spacer.h"
#include "Components/ScaleBox.h"
#include "UI/WWOMenuCursor.h"
#include "UI/WWOGunPreviewActor.h"
#include "UI/WWOPartPictures.h"
#include "Materials/MaterialInterface.h"
#include "Characters/WWOBodyComponent.h"
#include "Data/WWOData.h"
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
	const FVector GunSpot(0.f, 6000.f, -50000.f);  // the gun's, beside it
	const FVector PartsSpot(0.f, -6000.f, -50000.f); // the shell part pictures' stations, on the other side
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
	UTextBlock* Label = WWOUI::Spaced(WidgetTree, Text, WWOUI::Type::Caption, WWOUI::TextSecondary, 220);
	Content->AddChildToVerticalBox(Label)->SetPadding(FMargin(0, Content->GetChildrenCount() > 0 ? 24.f * K : 0.f, 0, 0));
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
	Look.Normal = WWOUI::RoundBrush(Color, WWOUI::Corner, Edge, Width); // the menu's 5 px corners
	Look.Hovered = WWOUI::RoundBrush(WWO::Lightened(Color, 0.12f), WWOUI::Corner, FLinearColor::White, Width);
	Look.Pressed = WWOUI::RoundBrush(WWO::Lightened(Color, 0.2f), WWOUI::Corner, FLinearColor::White, Width);
	UButton* B = WWOUI::TextButton(WidgetTree, Text, FontPx, Look);
	WWOUI::KeepPruned(SlotHandlers, SlotPruneAt, WWOUI::OnClick(B, this, MoveTemp(Fn)));
	WWOUI::SetTip(WidgetTree, B, Tooltip);
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

UWidget* UWWOCharacterMenu::ItemCard(const FItemCard& Spec, TFunction<void()> Fn)
{
	UWidgetTree* Tree = WidgetTree;
	const bool bNeutral = Spec.Color.A <= 0.f;
	UTexture2D* Badge = Spec.Kind.IsNone() ? nullptr : WWOUI::SymbolTexture(Spec.Kind, Spec.Id);
	// Every coloured card is the same depth of its colour: the shade at its badge's edge.
	const FLinearColor Fill = bNeutral ? WWOUI::CardColor : WWOUI::BadgeEdge(Spec.Color);
	const FLinearColor Edge = bNeutral ? (Spec.bOn ? OnEdge : FLinearColor(1, 1, 1, 0.2f))
		: (Spec.bOn ? WWO::Lightened(Spec.Color, 0.35f) : WWO::WithAlpha(WWO::Lightened(Spec.Color, 0.1f), 0.7f));
	const float EdgeWidth = Spec.bOn ? WWOUI::BoxEdgeOn : WWOUI::BoxEdge;
	WWOUI::FButtonLook Look;
	Look.Normal = WWOUI::RoundBrush(Fill, WWOUI::Corner, Edge, EdgeWidth);
	// Hovered and pressed the edge goes white; a badge card keeps its fill, so the badge stays seamless.
	Look.Hovered = WWOUI::RoundBrush(Badge ? Fill : WWO::Lightened(Fill, 0.06f), WWOUI::Corner, WWOUI::HoverEdge, EdgeWidth);
	Look.Pressed = WWOUI::RoundBrush(Badge ? Fill : WWO::Lightened(Fill, 0.12f), WWOUI::Corner, FLinearColor::White, EdgeWidth);
	Look.Padding = FMargin(Badge || Spec.Picture ? 8.f : 14.f, 8.f, 12.f, 8.f);
	Look.HAlign = HAlign_Fill;
	Look.VAlign = VAlign_Center;

	UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();
	if (Spec.Picture)
	{
		USizeBox* Box = Tree->ConstructWidget<USizeBox>();
		Box->SetWidthOverride(Spec.PictureSize.X * K);
		Box->SetHeightOverride(Spec.PictureSize.Y * K);
		Box->AddChild(PictureWidget(Spec.Picture, Spec.PictureSize));
		UHorizontalBoxSlot* PictureSlot = Row->AddChildToHorizontalBox(Box);
		PictureSlot->SetVerticalAlignment(VAlign_Center);
		PictureSlot->SetPadding(FMargin(0, 0, 8.f * K, 0));
	}
	else if (Badge)
	{
		// The bare symbol (white on clear) on the card's own fill, which is the badge's edge shade: the
		// badge's square would show as a faint seam against the rounded card.
		// Its fine lines lose alpha at card size, so it is drawn four times over itself to read as bright
		// as on the badge (alpha builds up: a 30% line comes out at 76%).
		const FVector2D SymbolSize = FVector2D(Spec.Badge, Spec.Badge) * K;
		UWidget* Symbol = nullptr;
		if (UWidget* Once = WWOUI::MakeSymbol(Tree, Spec.Kind, Spec.Id, SymbolSize, false))
		{
			UOverlay* Stack = Tree->ConstructWidget<UOverlay>();
			Stack->SetVisibility(ESlateVisibility::HitTestInvisible);
			Stack->AddChildToOverlay(Once);
			for (int32 i = 0; i < 3; ++i) Stack->AddChildToOverlay(WWOUI::MakeSymbol(Tree, Spec.Kind, Spec.Id, SymbolSize, false));
			Symbol = Stack;
		}
		if (!Symbol) Symbol = WWOUI::MakeSymbol(Tree, Spec.Kind, Spec.Id, SymbolSize);
		UHorizontalBoxSlot* BadgeSlot = Row->AddChildToHorizontalBox(Symbol);
		BadgeSlot->SetVerticalAlignment(VAlign_Center);
		BadgeSlot->SetPadding(FMargin(0, 0, 10.f * K, 0));
	}
	UVerticalBox* Words = Tree->ConstructWidget<UVerticalBox>();
	const ETextJustify::Type Justify = Spec.bCenter && !Badge ? ETextJustify::Center : ETextJustify::Left;
	UTextBlock* Title = WWOUI::Text(Tree, Spec.Title, WWOUI::Type::Body, WWOUI::TextPrimary, true);
	WWOUI::SetWeight(Title, WWOUI::EWeight::Medium);
	Title->SetJustification(Justify);
	Words->AddChildToVerticalBox(Title);
	if (!Spec.Sub.IsEmpty())
	{
		UTextBlock* Sub = WWOUI::Text(Tree, Spec.Sub, WWOUI::Type::Small, WWOUI::TextSecondary, true);
		Sub->SetJustification(Justify);
		Words->AddChildToVerticalBox(Sub)->SetPadding(FMargin(0, 2.f * K, 0, 0));
	}
	UHorizontalBoxSlot* WordsSlot = Row->AddChildToHorizontalBox(Words);
	WordsSlot->SetSize(WWOUI::Fill(1.f));
	WordsSlot->SetVerticalAlignment(VAlign_Center);

	UWidget* Inside = Row;
	if (Spec.bOn && Spec.bMark)
	{
		// The chosen one's mark: a dot in its edge colour in the top right corner.
		UBorder* Dot = Tree->ConstructWidget<UBorder>();
		Dot->SetBrush(WWOUI::RoundBrush(Edge, 4.f));
		USizeBox* DotBox = Tree->ConstructWidget<USizeBox>();
		DotBox->SetWidthOverride(8.f * K);
		DotBox->SetHeightOverride(8.f * K);
		DotBox->AddChild(Dot);
		UOverlay* Both = Tree->ConstructWidget<UOverlay>();
		UOverlaySlot* RowSlot = Both->AddChildToOverlay(Row);
		RowSlot->SetHorizontalAlignment(HAlign_Fill);
		RowSlot->SetVerticalAlignment(VAlign_Center);
		UOverlaySlot* DotSlot = Both->AddChildToOverlay(DotBox);
		DotSlot->SetHorizontalAlignment(HAlign_Right);
		DotSlot->SetVerticalAlignment(VAlign_Top);
		DotSlot->SetPadding(FMargin(0, -2.f * K, -5.f * K, 0));
		Inside = Both;
	}
	UButton* B = WWOUI::Button(Tree, Inside, Look);
	WWOUI::KeepPruned(SlotHandlers, SlotPruneAt, WWOUI::OnClick(B, this, MoveTemp(Fn)));
	WWOUI::SetTip(Tree, B, Spec.Tip);
	USizeBox* Box = Tree->ConstructWidget<USizeBox>();
	Box->SetMinDesiredHeight(Spec.MinHeight * K);
	Box->AddChild(B);
	return Box;
}

void UWWOCharacterMenu::AddCardGrid(const TArray<UWidget*>& Cards, int32 Columns)
{
	// Equal columns across the page, CardGap apart, the outer cards' edges on the page's edges (the
	// grid's own padding is pulled back out); every card in a row as tall as the tallest.
	UUniformGridPanel* Grid = WidgetTree->ConstructWidget<UUniformGridPanel>();
	Grid->SetSlotPadding(FMargin(CardGap * 0.5f * K));
	for (int32 i = 0; i < Cards.Num(); ++i)
	{
		if (!Cards[i]) continue;
		UUniformGridSlot* Cell = Grid->AddChildToUniformGrid(Cards[i], i / Columns, i % Columns);
		Cell->SetHorizontalAlignment(HAlign_Fill);
		Cell->SetVerticalAlignment(VAlign_Fill);
	}
	// Columns with no card still take their share, so a short row's cards are as wide as a full row's.
	for (int32 Col = Cards.Num(); Col < Columns; ++Col) Grid->AddChildToUniformGrid(WidgetTree->ConstructWidget<USpacer>(), 0, Col);
	const float Half = CardGap * 0.5f * K;
	Content->AddChildToVerticalBox(Grid)->SetPadding(FMargin(-Half, 8.f * K - Half, -Half, -Half));
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
		UTextBlock* Keys = WWOUI::Spaced(WidgetTree, TEXT("Q / E  TABS   ·   A / D  OR DRAG  TURN   ·   ESC  BACK"), WWOUI::Type::Caption, WWOUI::Faint, 120);
		Keys->SetJustification(ETextJustify::Right);
		WWOUI::AddPageHeader(Tree, Column, WWOUI::HeaderName(Tree, TEXT("WHAT WAS OURS"), true), Tabs.Box, Keys);

		// The character, shown on every tab (one render target, two places it can sit).
		PreviewActor = GetWorld()->SpawnActor<AWWOPreviewActor>(PreviewSpot, FRotator::ZeroRotator, WWO::AlwaysSpawn());
		GunPreview = GetWorld()->SpawnActor<AWWOGunPreviewActor>(GunSpot, FRotator::ZeroRotator, WWO::AlwaysSpawn());
		PartPictures = GetWorld()->SpawnActor<AWWOPartPictures>(PartsSpot, FRotator::ZeroRotator, WWO::AlwaysSpawn());
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
		ExpandedPanel->SetBrush(WWOUI::RoundBrush(WWOUI::PanelColor, WWOUI::Corner, WWOUI::PanelEdge, 1.f));
		ExpandedPanel->SetPadding(FMargin(28.f * K));
		ExpandedScroll = WWOUI::ScrollBox(Tree);
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
		PreviewSlot->SetPadding(FMargin(0, 24.f * K, 0, 0)); // level with the panel's top, clear of the header's line
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
	if (GunPreview) GunPreview->SetLive(false); // opens on the Loadout tab
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
	PreviewActor->SetLive(bLive); // its lights too
	if (GunPreview) GunPreview->SetLive(bLive && Expanded == TEXT("Weapons"));
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
	if (GunPreview) GunPreview->SetLive(Tile == TEXT("Weapons"));
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
	if (GunPreview) GunPreview->SetLive(false);
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
		Box->SetBrush(WWOUI::RoundBrush(Fill, WWOUI::Corner, Edge, EdgeWidth));
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
	if (bOpen && Expanded == TEXT("Weapons")) UpdateModPreview();
	if (bOpen && PartPictures) PartPictures->Step(); // (lit by the preview's studio, which is on while the menu is open)
}

FReply UWWOCharacterMenu::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Cards are buttons and keep their presses, so this is a press on the page around them (a mouse
	// click or the controller's Cross, which FWWOMenuCursor sends as one). Left unhandled as before.
	const FReply Reply = Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	if (bOpen && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton) CloseOnClickAway(InMouseEvent.GetScreenSpacePosition());
	return Reply;
}

void UWWOCharacterMenu::CloseOnClickAway(const FVector2D& ScreenPos)
{
	if (Expanded == TEXT("Weapons") && !OpenSlot.IsNone())
	{
		if (PopupPanel && PopupPanel->GetCachedGeometry().IsUnderLocation(ScreenPos)) return;
		OpenSlot = NAME_None;
		Refresh();
	}
	else if (Expanded == TEXT("Flora") && !Picker.IsEmpty())
	{
		if (Preview && Preview->GetCachedGeometry().IsUnderLocation(ScreenPos)) return; // dragging turns the character
		Picker.Reset();
		Refresh();
	}
}

UMaterialInterface* UWWOCharacterMenu::PartPicture(const UWWOFramePartData* Part, FName FrameSlot, bool bSmall) const
{
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	const int32 Slot = UWWOBodyComponent::ShellSlotOf(FrameSlot);
	if (!PartPictures || !L || Slot == INDEX_NONE) return nullptr;
	FColor Paint[3];
	for (int32 Role = 0; Role < 3; ++Role) Paint[Role] = L->GetShellColor(Slot, Role);
	return PartPictures->Picture(Part, Slot, Paint, bSmall);
}

UWidget* UWWOCharacterMenu::PictureWidget(UMaterialInterface* Mat, FVector2D Size)
{
	UImage* Image = WidgetTree->ConstructWidget<UImage>();
	FSlateBrush Brush;
	Brush.SetResourceObject(Mat);
	Brush.ImageSize = Size * K;
	Image->SetBrush(Brush);
	Image->SetVisibility(ESlateVisibility::HitTestInvisible);
	UScaleBox* Fit = WidgetTree->ConstructWidget<UScaleBox>();
	Fit->SetStretch(EStretch::ScaleToFit);
	Fit->SetVisibility(ESlateVisibility::HitTestInvisible);
	Fit->AddChild(Image);
	return Fit;
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

namespace
{
	// A widget's rectangle in absolute (desktop) space, as last laid out.
	FSlateRect AbsoluteRect(const UWidget* W)
	{
		const FGeometry& G = W->GetCachedGeometry();
		return FSlateRect::FromPointAndExtent(G.GetAbsolutePosition(), G.GetAbsoluteSize());
	}
}

TArray<FString> UWWOCharacterMenu::AutoTestHoverTiles() const
{
	TArray<FString> Names;
	for (const TPair<TWeakObjectPtr<UWidget>, FString>& Tile : HoverTiles) if (Tile.Key.IsValid()) Names.Add(Tile.Value);
	return Names;
}

bool UWWOCharacterMenu::AutoTestTileRect(const FString& Name, FSlateRect& Out) const
{
	for (const TPair<TWeakObjectPtr<UWidget>, FString>& Tile : HoverTiles)
	{
		if (Tile.Value == Name && Tile.Key.IsValid()) { Out = AbsoluteRect(Tile.Key.Get()); return true; }
	}
	return false;
}

bool UWWOCharacterMenu::AutoTestHoverRects(FSlateRect& OutPanel, TArray<FSlateRect>& OutChoices) const
{
	OutChoices.Reset();
	if (!HoverPopup) return false;
	OutPanel = AbsoluteRect(HoverPopup);
	for (const TPair<TWeakObjectPtr<UWidget>, FString>& Choice : HoverNames) if (Choice.Key.IsValid()) OutChoices.Add(AbsoluteRect(Choice.Key.Get()));
	return true;
}

bool UWWOCharacterMenu::AutoTestModRects(FSlateRect& OutPanel, TArray<FSlateRect>& OutSlots) const
{
	OutSlots.Reset();
	for (const auto& Pair : SlotButtons) if (Pair.Key.IsValid()) OutSlots.Add(AbsoluteRect(Pair.Key.Get()));
	if (!PopupPanel) return false;
	OutPanel = AbsoluteRect(PopupPanel);
	return true;
}

bool UWWOCharacterMenu::AutoTestConfigListRect(FSlateRect& Out) const
{
	if (!ConfigListBox) return false;
	Out = AbsoluteRect(ConfigListBox);
	return true;
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
