#include "UI/WWOUI.h"
#include "UI/WWOMenuCursor.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOSound.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"

namespace WWOUI
{
	FLinearColor Accent(const UObject* WorldContext)
	{
		const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(WorldContext);
		return L && L->GetFlora() ? WWO::Lightened(L->GetFlora()->Color, 0.15f) : DefaultAccent;
	}

	FSlateFontInfo Font(float Px)
	{
		// Godot font sizes are pixels; Slate's are points at 96 dpi (0.75 pt per px).
		return FCoreStyle::GetDefaultFontStyle("Medium", Px * 0.75f * K);
	}

	FSlateBrush RoundBrush(const FLinearColor& Color, float Radius, const FLinearColor& Outline, float OutlineWidth)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(WWO::Lin(Color));
		Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(Radius, Radius, Radius, Radius) * K, FSlateColor(WWO::Lin(Outline)), OutlineWidth * K);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		return Brush;
	}

	UWidget* MakeLogo(UWidgetTree* Tree, bool bWordmark, FVector2D Size, UWidget* Keeps)
	{
		UOverlay* Both = Tree->ConstructWidget<UOverlay>();
		if (Keeps)
		{
			Keeps->SetVisibility(ESlateVisibility::Hidden); // takes its room, draws nothing
			Both->AddChildToOverlay(Keeps);
		}
		UImage* Image = Tree->ConstructWidget<UImage>();
		if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, bWordmark ? TEXT("/Game/WWO/Textures/UI/T_LogoTitle.T_LogoTitle") : TEXT("/Game/WWO/Textures/UI/T_LogoMark.T_LogoMark")))
		{
			FSlateBrush Brush;
			Brush.SetResourceObject(Texture);
			Brush.ImageSize = Size;
			Brush.DrawAs = ESlateBrushDrawType::Image;
			Image->SetBrush(Brush);
		}
		else if (Keeps) Keeps->SetVisibility(ESlateVisibility::HitTestInvisible); // no texture: the text it replaced
		Image->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* ImageSlot = Both->AddChildToOverlay(Image);
		ImageSlot->SetHorizontalAlignment(HAlign_Center);
		ImageSlot->SetVerticalAlignment(VAlign_Center);
		// The wordmark's texture is clear above and below the letters (40 of its 256 rows each way):
		// that part doesn't count for layout, so the text it replaced still decides how tall this is.
		if (bWordmark && Keeps) ImageSlot->SetPadding(FMargin(0.f, -Size.Y * 40.f / 256.f));
		return Both;
	}

	void PaintCursor(const UUserWidget* Menu, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer)
	{
		FWWOMenuCursor::PaintRing(Geometry, Menu->GetTickSpaceGeometry(), Out, Layer, K);
	}

	UTextBlock* Text(UWidgetTree* Tree, const FString& Str, float FontPx, const FLinearColor& Color, bool bWrap)
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Str));
		T->SetFont(Font(FontPx));
		T->SetColorAndOpacity(FSlateColor(WWO::Lin(Color)));
		T->SetAutoWrapText(bWrap);
		return T;
	}

	UTextBlock* Spaced(UWidgetTree* Tree, const FString& Str, float FontPx, const FLinearColor& Color, int32 Spacing, bool bWrap)
	{
		UTextBlock* T = WWOUI::Text(Tree, Str.ToUpper(), FontPx, Color, bWrap);
		FSlateFontInfo F = T->GetFont();
		F.LetterSpacing = Spacing;
		T->SetFont(F);
		return T;
	}

	UWidget* Rule(UWidgetTree* Tree, const FLinearColor& Color, float Thickness)
	{
		UBorder* Line = Tree->ConstructWidget<UBorder>();
		Line->SetBrushColor(WWO::Lin(Color));
		USizeBox* Box = Tree->ConstructWidget<USizeBox>();
		Box->SetHeightOverride(Thickness);
		Box->AddChild(Line);
		return Box;
	}

	UWidget* RuleDown(UWidgetTree* Tree, const FLinearColor& Color, float Length)
	{
		UBorder* Line = Tree->ConstructWidget<UBorder>();
		Line->SetBrushColor(WWO::Lin(Color));
		USizeBox* Box = Tree->ConstructWidget<USizeBox>();
		Box->SetWidthOverride(1.5f);
		if (Length > 0.f) Box->SetHeightOverride(Length * K);
		Box->AddChild(Line);
		return Box;
	}

	UWidget* Sized(UWidgetTree* Tree, UWidget* Inside, float MinWidth, float MinHeight)
	{
		USizeBox* Box = Tree->ConstructWidget<USizeBox>();
		if (MinWidth > 0.f) Box->SetMinDesiredWidth(MinWidth * K);
		if (MinHeight > 0.f) Box->SetMinDesiredHeight(MinHeight * K);
		Box->AddChild(Inside);
		return Box;
	}

	UWidget* Wide(UWidgetTree* Tree, UWidget* Inside, float Width)
	{
		USizeBox* Box = Tree->ConstructWidget<USizeBox>();
		Box->SetWidthOverride(Width * K);
		Box->AddChild(Inside);
		return Box;
	}

	FSlateChildSize Fill(float Value)
	{
		FSlateChildSize Size(ESlateSizeRule::Fill);
		Size.Value = Value;
		return Size;
	}

	void StyleTextBox(UEditableTextBox* Box, float FontPx, const FMargin& PaddingPx, const FLinearColor& AccentColor)
	{
		FEditableTextBoxStyle BoxStyle = Box->WidgetStyle;
		const FSlateBrush BoxBrush = RoundBrush(CardColor, 5.f, CardEdge, 1.5f);
		BoxStyle.SetBackgroundImageNormal(BoxBrush).SetBackgroundImageHovered(BoxBrush).SetBackgroundImageReadOnly(BoxBrush);
		BoxStyle.SetBackgroundImageFocused(RoundBrush(CardColor, 5.f, AccentColor, 1.5f));
		BoxStyle.SetBackgroundColor(FLinearColor::White).SetForegroundColor(FLinearColor::White).SetFocusedForegroundColor(FLinearColor::White);
		BoxStyle.TextStyle.SetFont(Font(FontPx)).SetColorAndOpacity(FSlateColor(FLinearColor::White));
		BoxStyle.SetPadding(FMargin(PaddingPx.Left * K, PaddingPx.Top * K, PaddingPx.Right * K, PaddingPx.Bottom * K));
		Box->WidgetStyle = BoxStyle;
	}

	FButtonLook FilledLook(const FLinearColor& Color, const FMargin& Padding, bool bLeftAlign)
	{
		FButtonLook Look;
		Look.Normal = RoundBrush(Color, 8.f);
		Look.Hovered = RoundBrush(Color + FLinearColor(0.08f, 0.08f, 0.08f, 0.f), 8.f);
		Look.Pressed = RoundBrush(Color + FLinearColor(0.15f, 0.15f, 0.15f, 0.f), 8.f);
		Look.Padding = Padding;
		Look.HAlign = bLeftAlign ? HAlign_Left : HAlign_Center;
		Look.VAlign = VAlign_Center;
		return Look;
	}

	FButtonLook CardLook(const FLinearColor& Fill, const FLinearColor& Edge, float EdgeWidth, const FMargin& Padding, EVerticalAlignment VAlign)
	{
		FButtonLook Look;
		const bool bPlain = Fill.A <= 0.f;
		Look.Normal = RoundBrush(Fill, 5.f, Edge, EdgeWidth);
		Look.Hovered = RoundBrush(bPlain ? FLinearColor(1, 1, 1, 0.06f) : WWO::Lightened(Fill, 0.08f), 5.f, bPlain ? Edge : HoverEdge, EdgeWidth);
		Look.Pressed = RoundBrush(bPlain ? FLinearColor(1, 1, 1, 0.1f) : WWO::Lightened(Fill, 0.14f), 5.f, bPlain ? Edge : FLinearColor::White, EdgeWidth);
		Look.Padding = Padding;
		Look.VAlign = VAlign;
		return Look;
	}

	FButtonLook StillLook(const FSlateBrush& Brush, const FMargin& Padding)
	{
		FButtonLook Look;
		Look.Normal = Look.Hovered = Look.Pressed = Brush;
		Look.Padding = Padding;
		return Look;
	}

	UButton* Button(UWidgetTree* Tree, UWidget* Content, const FButtonLook& Look)
	{
		UButton* B = Tree->ConstructWidget<UButton>();
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		B->IsFocusable = false; // the cursor drives everything, not focus
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		FButtonStyle Style;
		Style.SetNormal(Look.Normal);
		Style.SetHovered(Look.Hovered);
		Style.SetPressed(Look.Pressed);
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		B->SetStyle(Style);
		UButtonSlot* Slot = Cast<UButtonSlot>(B->AddChild(Content));
		Slot->SetPadding(Look.Padding * K);
		Slot->SetHorizontalAlignment(Look.HAlign);
		Slot->SetVerticalAlignment(Look.VAlign);
		return B;
	}

	UButton* TextButton(UWidgetTree* Tree, const FString& Label, float FontPx, const FButtonLook& Look, bool bWrap)
	{
		UTextBlock* T = WWOUI::Text(Tree, Label, FontPx, FLinearColor::White, bWrap);
		T->SetJustification(Look.HAlign == HAlign_Left ? ETextJustify::Left : ETextJustify::Center);
		return Button(Tree, T, Look);
	}

	UWWOClickHandler* OnClick(UButton* B, UObject* Owner, TFunction<void()> Fn)
	{
		UWWOClickHandler* H = NewObject<UWWOClickHandler>(Owner);
		H->Fn = [Fn = MoveTemp(Fn), Weak = TWeakObjectPtr<UObject>(Owner)] { if (Weak.IsValid()) UWWOSound::PlayFlat(Weak.Get(), "ui_click"); if (Fn) Fn(); };
		B->OnClicked.AddDynamic(H, &UWWOClickHandler::Handle);
		H->For = B;
		return H;
	}

	void KeepPruned(TArray<TObjectPtr<UObject>>& Handlers, int32& PruneAt, UObject* Handler)
	{
		// Lists, pages and search results are rebuilt often, each time with new buttons and sliders and
		// a handler apiece. A handler whose widget is no longer under a menu's root is done with.
		Handlers.Add(Handler);
		if (Handlers.Num() < PruneAt) return;
		auto OnPage = [](const UWidget* W)
		{
			if (!W) return false;
			const UWidget* Top = W;
			while (Top->GetParent()) Top = Top->GetParent();
			const UWidgetTree* Tree = Cast<UWidgetTree>(Top->GetOuter());
			return Tree && Tree->RootWidget == Top;
		};
		Handlers.RemoveAll([&OnPage](const TObjectPtr<UObject>& H)
		{
			if (const UWWOClickHandler* Click = Cast<UWWOClickHandler>(H)) return !OnPage(Click->For.Get());
			if (const UWWOSliderHandler* Slide = Cast<UWWOSliderHandler>(H)) return !OnPage(Slide->For.Get());
			return false;
		});
		PruneAt = Handlers.Num() * 2 + 256;
	}
}
