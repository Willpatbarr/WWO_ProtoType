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
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "Fonts/CompositeFont.h"
#include "Misc/Paths.h"

namespace WWOUI
{
	FLinearColor Accent(const UObject* WorldContext)
	{
		const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(WorldContext);
		return L && L->GetFlora() ? WWO::Lightened(L->GetFlora()->Color, 0.15f) : DefaultAccent;
	}

	namespace
	{
		// Each weight's face, read from its file once and kept (null: no file for it).
		TSharedPtr<const FCompositeFont> LoadFace(EWeight Weight)
		{
			static const TCHAR* const Names[] = { TEXT("Light"), TEXT("Regular"), TEXT("Medium"), TEXT("SemiBold"), TEXT("Bold") };
			static TSharedPtr<const FCompositeFont> Faces[UE_ARRAY_COUNT(Names)];
			static bool bTried[UE_ARRAY_COUNT(Names)] = {};
			const int32 Index = (int32)Weight;
			if (!bTried[Index])
			{
				bTried[Index] = true;
				const FString File = FPaths::ProjectContentDir() / FString::Printf(TEXT("WWO/Fonts/Poppins-%s.ttf"), Names[Index]);
				if (FPaths::FileExists(File))
				{
					TSharedRef<FCompositeFont> Poppins = MakeShared<FCompositeFont>(FName(Names[Index]), File, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
					// A character Poppins hasn't got (a check mark, other scripts) comes from the engine's own fallbacks.
					const TSharedRef<const FCompositeFont> Engine = FCoreStyle::GetDefaultFont();
					Poppins->FallbackTypeface = Engine->FallbackTypeface;
					Poppins->SubTypefaces = Engine->SubTypefaces;
					Faces[Index] = Poppins;
				}
			}
			return Faces[Index];
		}

		// The face for Weight, or the nearest weight that has a file (the lighter side first: Light ships).
		TSharedPtr<const FCompositeFont> Face(EWeight Weight)
		{
			const int32 Want = (int32)Weight;
			for (int32 Step = 0; Step <= 4; ++Step)
			{
				for (const int32 Try : { Want - Step, Want + Step })
				{
					if (Try < 0 || Try > 4) continue;
					if (TSharedPtr<const FCompositeFont> Found = LoadFace((EWeight)Try)) return Found;
				}
			}
			return nullptr;
		}
	}

	FSlateFontInfo FontAt(float Points, EWeight Weight)
	{
		const TSharedPtr<const FCompositeFont> Poppins = Face(Weight);
		if (!Poppins.IsValid()) return FCoreStyle::GetDefaultFontStyle("Regular", FMath::Max(1.f, Points)); // no font files at all
		const FName Typeface = Poppins->DefaultTypeface.Fonts.Num() > 0 ? Poppins->DefaultTypeface.Fonts[0].Name : NAME_None;
		return FSlateFontInfo(Poppins, FMath::Max(1.f, Points), Typeface);
	}

	FSlateFontInfo Font(float Px, EWeight Weight)
	{
		// Godot font sizes are pixels; Slate's are points at 96 dpi (0.75 pt per px).
		return FontAt(Px * 0.75f * K, Weight);
	}

	void SetWeight(UTextBlock* T, EWeight Weight)
	{
		if (!T) return;
		const FSlateFontInfo Old = T->GetFont();
		FSlateFontInfo F = FontAt(Old.Size, Weight);
		F.LetterSpacing = Old.LetterSpacing;
		T->SetFont(F);
	}

	void SetTip(UWidgetTree* Tree, UWidget* Widget, const FString& Tip)
	{
		if (!Widget || Tip.IsEmpty()) return;
		UTextBlock* Words = Text(Tree, Tip, Type::Small, TextPrimary, true);
		Words->SetWrapTextAt(360.f * K);
		UBorder* Box = Tree->ConstructWidget<UBorder>();
		Box->SetBrush(RoundBrush(FLinearColor(0.07f, 0.075f, 0.09f, 0.97f), Corner, PanelEdge, BoxEdge));
		Box->SetPadding(FMargin(3.f * Grid * K, 2.f * Grid * K));
		Box->SetContent(Words);
		Widget->SetToolTip(Box);
	}

	UScrollBox* ScrollBox(UWidgetTree* Tree)
	{
		UScrollBox* Box = Tree->ConstructWidget<UScrollBox>();
		FScrollBarStyle Bar = Box->GetWidgetBarStyle();
		const FSlateBrush None = RoundBrush(FLinearColor(0, 0, 0, 0), 0.f);
		Bar.SetVerticalBackgroundImage(None).SetHorizontalBackgroundImage(None).SetVerticalTopSlotImage(None).SetVerticalBottomSlotImage(None)
			.SetHorizontalTopSlotImage(None).SetHorizontalBottomSlotImage(None);
		Bar.SetNormalThumbImage(RoundBrush(FLinearColor(1, 1, 1, 0.22f), 2.f));
		Bar.SetHoveredThumbImage(RoundBrush(FLinearColor(1, 1, 1, 0.4f), 2.f));
		Bar.SetDraggedThumbImage(RoundBrush(FLinearColor(1, 1, 1, 0.55f), 2.f));
		Box->SetWidgetBarStyle(Bar);
		Box->SetScrollbarThickness(FVector2D(4.f * K));
		return Box;
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

UTexture2D* SymbolTexture(FName Kind, FName Id, bool bBadge)
{
	// T_<Kind>_<Name>[_Badge]: the id in title case (Tools/import_symbols.py), the floras' as they're written.
	static const TMap<FName, FString> FloraNames = { { "voltvine", TEXT("VoltVine") }, { "veilspore", TEXT("VeilSpore") }, { "ironbark", TEXT("IronBark") } };
	FString Name;
	if (Kind == "Flora")
	{
		const FString* Found = FloraNames.Find(Id);
		if (!Found) return nullptr;
		Name = *Found;
	}
	else
	{
		TArray<FString> Parts;
		Id.ToString().ParseIntoArray(Parts, TEXT("_"));
		for (FString& Part : Parts) Name += Part.Left(1).ToUpper() + Part.Mid(1);
	}
	const FString Asset = FString::Printf(TEXT("T_%s_%s%s"), *Kind.ToString(), *Name, bBadge ? TEXT("_Badge") : TEXT(""));
	const FString Path = FString::Printf(TEXT("/Game/WWO/Textures/UI/%s.%s"), *Asset, *Asset);
	if (UTexture2D* Loaded = FindObject<UTexture2D>(nullptr, *Path)) return Loaded;
	return LoadObject<UTexture2D>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

UWidget* MakeSymbol(UWidgetTree* Tree, FName Kind, FName Id, FVector2D Size, bool bBadge)
{
	UTexture2D* Texture = SymbolTexture(Kind, Id, bBadge);
	if (!Texture) return nullptr;
	UImage* Image = Tree->ConstructWidget<UImage>();
	FSlateBrush Brush;
	Brush.SetResourceObject(Texture);
	Brush.ImageSize = Size;
	Brush.DrawAs = ESlateBrushDrawType::Image;
	Image->SetBrush(Brush);
	Image->SetVisibility(ESlateVisibility::HitTestInvisible);
	return Image;
}

UWidget* FillWithSymbol(UWidgetTree* Tree, UTexture2D* Texture)
{
	UImage* Image = Tree->ConstructWidget<UImage>();
	FSlateBrush Brush;
	Brush.SetResourceObject(Texture);
	Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
	Brush.DrawAs = ESlateBrushDrawType::Image;
	Image->SetBrush(Brush);
	Image->SetVisibility(ESlateVisibility::HitTestInvisible);
	UScaleBox* Fit = Tree->ConstructWidget<UScaleBox>();
	Fit->SetStretch(EStretch::ScaleToFill);
	Fit->SetVisibility(ESlateVisibility::HitTestInvisible);
	Fit->AddChild(Image);
	return Fit;
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
		FSlateFontInfo F = Font(FontPx, EWeight::Medium); // spaced capitals read a step heavier
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
		const FSlateBrush BoxBrush = RoundBrush(CardColor, Corner, CardEdge, 1.5f);
		BoxStyle.SetBackgroundImageNormal(BoxBrush).SetBackgroundImageHovered(BoxBrush).SetBackgroundImageReadOnly(BoxBrush);
		BoxStyle.SetBackgroundImageFocused(RoundBrush(CardColor, Corner, AccentColor, 1.5f));
		BoxStyle.SetBackgroundColor(FLinearColor::White).SetForegroundColor(FLinearColor::White).SetFocusedForegroundColor(FLinearColor::White);
		BoxStyle.TextStyle.SetFont(Font(FontPx)).SetColorAndOpacity(FSlateColor(FLinearColor::White));
		BoxStyle.SetPadding(FMargin(PaddingPx.Left * K, PaddingPx.Top * K, PaddingPx.Right * K, PaddingPx.Bottom * K));
		Box->WidgetStyle = BoxStyle;
	}

	UWidget* HeaderName(UWidgetTree* Tree, const FString& Title, bool bLogo)
	{
		UHorizontalBox* Name = Tree->ConstructWidget<UHorizontalBox>();
		UTextBlock* Mark = Spaced(Tree, TEXT("//"), 26.f, DefaultAccent, 0);
		Name->AddChildToHorizontalBox(bLogo ? MakeLogo(Tree, false, FVector2D(30.f, 30.f) * K, Mark) : Mark)->SetVerticalAlignment(VAlign_Center);
		UHorizontalBoxSlot* TitleSlot = Name->AddChildToHorizontalBox(Spaced(Tree, Title, 17.f, FLinearColor::White, 420));
		TitleSlot->SetVerticalAlignment(VAlign_Center);
		TitleSlot->SetPadding(FMargin(18.f * K, 0, 0, 0));
		return Name;
	}

	FTabStrip TabStrip(UWidgetTree* Tree, const TArray<FString>& Names, TFunctionRef<UWidget*(UWidget* Content, int32 Index)> Wrap)
	{
		FTabStrip Strip;
		Strip.Box = Tree->ConstructWidget<UHorizontalBox>();
		for (int32 i = 0; i < Names.Num(); ++i)
		{
			UWidget* Bar = nullptr;
			if (i > 0)
			{
				Bar = Spaced(Tree, TEXT("|"), 15.f, TabBar, 0);
				UHorizontalBoxSlot* BarSlot = Strip.Box->AddChildToHorizontalBox(Bar);
				BarSlot->SetVerticalAlignment(VAlign_Center);
				BarSlot->SetPadding(FMargin(26.f * K, 0));
			}
			UVerticalBox* Tab = Tree->ConstructWidget<UVerticalBox>();
			UTextBlock* Label = Spaced(Tree, Names[i], 17.f, FLinearColor::White, 180);
			Tab->AddChildToVerticalBox(Label)->SetPadding(FMargin(8.f * K, 6.f * K, 4.f * K, 6.f * K));
			UBorder* Line = Tree->ConstructWidget<UBorder>();
			USizeBox* LineBox = Tree->ConstructWidget<USizeBox>();
			LineBox->SetHeightOverride(3.f * K);
			LineBox->AddChild(Line);
			Tab->AddChildToVerticalBox(LineBox);
			UWidget* Clicked = Wrap(Tab, i);
			Strip.Box->AddChildToHorizontalBox(Clicked)->SetVerticalAlignment(VAlign_Center);
			Strip.Labels.Add(Label);
			Strip.Lines.Add(Line);
			Strip.Tabs.Add(Clicked);
			Strip.Bars.Add(Bar);
		}
		return Strip;
	}

	void SetActiveTab(const TArray<TObjectPtr<UTextBlock>>& Labels, const TArray<TObjectPtr<UBorder>>& Lines, int32 Active, const FLinearColor& AccentColor)
	{
		for (int32 i = 0; i < Labels.Num(); ++i)
		{
			Labels[i]->SetColorAndOpacity(FSlateColor(WWO::Lin(i == Active ? FLinearColor::White : TabDim)));
			if (Lines.IsValidIndex(i)) Lines[i]->SetBrushColor(WWO::Lin(i == Active ? AccentColor : FLinearColor(0, 0, 0, 0)));
		}
	}

	void AddPageHeader(UWidgetTree* Tree, UVerticalBox* Column, UWidget* Name, UWidget* Middle, UWidget* Right, EHorizontalAlignment RightAlign)
	{
		UHorizontalBox* Header = Tree->ConstructWidget<UHorizontalBox>();
		UHorizontalBoxSlot* NameSlot = Header->AddChildToHorizontalBox(Name);
		NameSlot->SetSize(Fill(1.f));
		NameSlot->SetVerticalAlignment(VAlign_Center);
		Header->AddChildToHorizontalBox(Middle)->SetVerticalAlignment(VAlign_Center);
		UHorizontalBoxSlot* RightSlot = Header->AddChildToHorizontalBox(Right);
		RightSlot->SetSize(Fill(1.f));
		RightSlot->SetHorizontalAlignment(RightAlign);
		RightSlot->SetVerticalAlignment(VAlign_Center);
		Column->AddChildToVerticalBox(Header);
		Column->AddChildToVerticalBox(Rule(Tree, RuleColor))->SetPadding(FMargin(0, 4.f * K, 0, 0));
	}

	FButtonLook FilledLook(const FLinearColor& Color, const FMargin& Padding, bool bLeftAlign)
	{
		FButtonLook Look;
		Look.Normal = RoundBrush(Color, Corner);
		Look.Hovered = RoundBrush(Color + FLinearColor(0.08f, 0.08f, 0.08f, 0.f), Corner);
		Look.Pressed = RoundBrush(Color + FLinearColor(0.15f, 0.15f, 0.15f, 0.f), Corner);
		Look.Padding = Padding;
		Look.HAlign = bLeftAlign ? HAlign_Left : HAlign_Center;
		Look.VAlign = VAlign_Center;
		return Look;
	}

	FButtonLook CardLook(const FLinearColor& Fill, const FLinearColor& Edge, float EdgeWidth, const FMargin& Padding, EVerticalAlignment VAlign)
	{
		FButtonLook Look;
		const bool bPlain = Fill.A <= 0.f;
		Look.Normal = RoundBrush(Fill, Corner, Edge, EdgeWidth);
		Look.Hovered = RoundBrush(bPlain ? FLinearColor(1, 1, 1, 0.06f) : WWO::Lightened(Fill, 0.08f), Corner, bPlain ? Edge : HoverEdge, EdgeWidth);
		Look.Pressed = RoundBrush(bPlain ? FLinearColor(1, 1, 1, 0.1f) : WWO::Lightened(Fill, 0.14f), Corner, bPlain ? Edge : FLinearColor::White, EdgeWidth);
		Look.Padding = Padding;
		Look.VAlign = VAlign;
		return Look;
	}

	FButtonLook PlainLook()
	{
		return CardLook(FLinearColor(0, 0, 0, 0), FLinearColor(0, 0, 0, 0), 0.f, FMargin(0.f));
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
