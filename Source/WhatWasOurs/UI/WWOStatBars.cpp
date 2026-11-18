#include "UI/WWOStatBars.h"
#include "UI/WWOSettingsMenu.h"
#include "UI/WWOUI.h"
#include "WWOTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"

int32 UWWOStatBars::NativePaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling,
	FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const int32 Layer = Super::NativePaint(Args, Geo, Culling, Out, LayerId, Style, bParentEnabled);
	constexpr float K = WWOUI::K;
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("GenericWhiteBox");
	auto Box = [&](float X, float Y, float W, float H, const FLinearColor& C, int32 L)
	{
		if (W <= 0.f) return;
		FSlateDrawElement::MakeBox(Out, L, Geo.ToPaintGeometry(FVector2f(W * K, H * K), FSlateLayoutTransform(FVector2f(X * K, Y * K))),
			White, ESlateDrawEffect::None, WWO::Lin(C));
	};
	// The track: rounded ends, as the HUD's bars.
	auto Track = [&](float X, float Y, float W, float H, const FLinearColor& C, int32 L)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(H, H, H, H) * 0.5f * K, FSlateColor(FLinearColor::Transparent), 0.f);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		FSlateDrawElement::MakeBox(Out, L, Geo.ToPaintGeometry(FVector2f(W * K, H * K), FSlateLayoutTransform(FVector2f(X * K, Y * K))),
			&Brush, ESlateDrawEffect::None, WWO::Lin(C));
	};
	auto Text = [&](const FString& S, float X, float Y, float Px, const FLinearColor& C, float AlignX)
	{
		const FSlateFontInfo F = WWOUI::Font(Px, AlignX > 0.5f ? WWOUI::EWeight::Medium : WWOUI::EWeight::Regular);
		const FVector2D M = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(S, F);
		FSlateDrawElement::MakeText(Out, Layer + 2, Geo.ToPaintGeometry(FVector2f(M), FSlateLayoutTransform(FVector2f(X * K - M.X * AlignX, Y * K - M.Y * 0.5f))),
			S, F, ESlateDrawEffect::None, WWO::Lin(C));
	};

	// As wide as the menu makes it (Width when it has no size yet): labels, the bar, the value.
	const float Wide = Geo.GetLocalSize().X > 1.f ? Geo.GetLocalSize().X / K : Width;
	const float BarX = 138.f, BarW = Wide - BarX - 84.f, BarH = 8.f;
	const FLinearColor TrackColor(1.f, 1.f, 1.f, 0.08f), Bar(0.82f, 0.84f, 0.88f);
	const FLinearColor Gain(0.35f, 0.95f, 0.45f), Loss(1.f, 0.35f, 0.3f);
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		const FWWOStatBar& R = Rows[i];
		const float Y = i * RowHeight;
		const float Mid = Y + RowHeight * 0.5f;
		Text(R.Label, 0.f, Mid, WWOUI::Type::Body, WWOUI::TextSecondary, 0.f);
		Track(BarX, Mid - BarH * 0.5f, BarW, BarH, TrackColor, Layer); // under the bars
		const float Now = FMath::Clamp(R.Fill, 0.f, 1.f) * BarW;
		const float Then = FMath::Clamp(R.PreviewFill, 0.f, 1.f) * BarW;
		const bool bChange = R.bPreview && FMath::Abs(Then - Now) > 0.5f;
		if (bChange && Then > Now)
		{
			Box(BarX, Mid - BarH * 0.5f, Now, BarH, Bar, Layer + 1);
			Box(BarX + Now, Mid - BarH * 0.5f, Then - Now, BarH, Gain, Layer + 1);
		}
		else if (bChange)
		{
			Box(BarX, Mid - BarH * 0.5f, Then, BarH, Bar, Layer + 1);
			Box(BarX + Then, Mid - BarH * 0.5f, Now - Then, BarH, Loss, Layer + 1);
		}
		else
		{
			Box(BarX, Mid - BarH * 0.5f, Now, BarH, Bar, Layer + 1);
		}
		Box(BarX + BarW * 0.5f - 1.f, Mid - BarH * 0.5f - 3.f, 2.f, BarH + 6.f, FLinearColor(1, 1, 1, 0.35f), Layer + 2); // the bare frame
		const bool bBetter = Then > Now;
		Text(bChange ? R.PreviewValue : R.Value, Wide, Mid, WWOUI::Type::Body, bChange ? (bBetter ? Gain : Loss) : WWOUI::TextPrimary, 1.f);
	}
	return Layer + 2;
}
