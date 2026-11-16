#pragma once

// Immediate-mode drawing in the Godot 1600 x 900 layout, scaled to the real size: what the HUD
// paints with (UWWOHUDWidget, UI/WWOHUDPaint.cpp). Build one per paint with the widget's
// geometry, the layer, the scale and the layout size (WWOHUD::Scale, UI/WWOHUDLayout.h, gives the scale).

#include "CoreMinimal.h"
#include "WWOTypes.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "UI/WWOHUDLayout.h"

// Paints in the Godot 1600 x 900 layout, scaled to the real size.
struct FWWOPainter
{
	FSlateWindowElementList& Out;
	const FGeometry& Geo;
	int32 Layer;
	float S;          // pixels per Godot pixel
	FVector2D Size;   // local size in Godot pixels
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("GenericWhiteBox");

	void Box(FVector2D Pos, FVector2D BoxSize, const FLinearColor& Color) const
	{
		if (Color.A <= 0.001f) return;
		FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(FVector2f(BoxSize * S), FSlateLayoutTransform(FVector2f(Pos * S))), White,
			ESlateDrawEffect::None, WWO::Lin(Color));
	}
	void Outline(FVector2D Pos, FVector2D BoxSize, const FLinearColor& Color, float Width) const
	{
		Box(Pos, FVector2D(BoxSize.X, Width), Color);
		Box(Pos + FVector2D(0, BoxSize.Y - Width), FVector2D(BoxSize.X, Width), Color);
		Box(Pos, FVector2D(Width, BoxSize.Y), Color);
		Box(Pos + FVector2D(BoxSize.X - Width, 0), FVector2D(Width, BoxSize.Y), Color);
	}
	void Line(FVector2D A, FVector2D B, const FLinearColor& Color, float Width) const
	{
		TArray<FVector2f> Points = { FVector2f(A * S), FVector2f(B * S) };
		FSlateDrawElement::MakeLines(Out, Layer, Geo.ToPaintGeometry(), Points, ESlateDrawEffect::None, WWO::Lin(Color), true, Width * S);
	}
	// A filled triangle (Slate has no call for one: it goes in as custom vertices on the white brush).
	void Tri(FVector2D A, FVector2D B, FVector2D C, const FLinearColor& Color) const
	{
		if (Color.A <= 0.001f || !FSlateApplication::IsInitialized()) return;
		const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*White);
		const FColor Packed = WWO::Lin(Color).ToFColor(false);
		const FSlateRenderTransform& To = Geo.GetAccumulatedRenderTransform();
		TArray<FSlateVertex> Verts;
		for (const FVector2D& V : { A, B, C }) Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(To, FVector2f(V * S), FVector2f(0.5f, 0.5f), Packed));
		FSlateDrawElement::MakeCustomVerts(Out, Layer, Handle, Verts, { 0, 1, 2 }, nullptr, 0, 0);
	}
	// A vignette: Color over the whole screen, clear inside the ellipse that is Inner of the way
	// from the center to the edges and rising smoothly to Color's alpha at the corners. A grid of
	// triangles with the fade in the vertex colors, so it needs no material.
	void Vignette(const FLinearColor& Color, float Inner) const
	{
		if (Color.A <= 0.003f || !FSlateApplication::IsInitialized()) return;
		constexpr int32 Nx = 32, Ny = 18;
		const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*White);
		const FSlateRenderTransform& To = Geo.GetAccumulatedRenderTransform();
		TArray<FSlateVertex> Verts;
		TArray<SlateIndex> Indices;
		Verts.Reserve((Nx + 1) * (Ny + 1));
		Indices.Reserve(Nx * Ny * 6);
		for (int32 y = 0; y <= Ny; ++y)
		{
			for (int32 x = 0; x <= Nx; ++x)
			{
				const FVector2D UV(float(x) / Nx, float(y) / Ny);
				// 0 at the center, 1 at the middle of an edge, about 1.41 in a corner.
				const float D = FVector2D((UV.X - 0.5f) * 2.f, (UV.Y - 0.5f) * 2.f).Size();
				const float Fade = WWO::SmoothStep(Inner, 1.42f, D);
				FLinearColor C = Color;
				C.A *= Fade;
				Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(To, FVector2f(UV * Size * S), FVector2f(0.5f, 0.5f), WWO::Lin(C).ToFColor(false)));
			}
		}
		for (int32 y = 0; y < Ny; ++y)
		{
			for (int32 x = 0; x < Nx; ++x)
			{
				const SlateIndex A = SlateIndex(y * (Nx + 1) + x), B = A + 1, C2 = SlateIndex(A + Nx + 1), D2 = C2 + 1;
				Indices.Append({ A, B, C2, B, D2, C2 });
			}
		}
		FSlateDrawElement::MakeCustomVerts(Out, Layer - 1, Handle, Verts, Indices, nullptr, 0, 0); // a layer down: under the rest of the HUD
	}
	void Circle(FVector2D Center, float Radius, const FLinearColor& Color, float Width, int32 Segments = 48) const
	{
		TArray<FVector2f> Points;
		for (int32 i = 0; i <= Segments; ++i)
		{
			const float A = UE_TWO_PI * i / Segments;
			Points.Add(FVector2f((Center + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius) * S));
		}
		FSlateDrawElement::MakeLines(Out, Layer, Geo.ToPaintGeometry(), Points, ESlateDrawEffect::None, WWO::Lin(Color), true, Width * S);
	}
	void Dot(FVector2D Center, float Radius, const FLinearColor& Color) const
	{
		for (float R = Radius; R > 0.f; R -= 0.75f) Circle(Center, R, Color, 1.5f, 16);
	}
	// Godot draws every label in its default font, Open Sans SemiBold: Roboto Medium is the
	// engine's closest weight. Sizes are Godot pixels; Slate's are points at 96 dpi (0.75 pt/px).
	FSlateFontInfo Font(float Px, float Outline = 6.f, float OutlineAlpha = 0.75f) const
	{
		FSlateFontInfo F = FCoreStyle::GetDefaultFontStyle("Medium", FMath::Max(1.f, Px * 0.75f * S));
		F.OutlineSettings.OutlineSize = FMath::RoundToInt(Outline * 0.5f * S);
		F.OutlineSettings.OutlineColor = FLinearColor(0, 0, 0, OutlineAlpha);
		return F;
	}
	FVector2D Measure(const FString& Text, const FSlateFontInfo& F) const
	{
		return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, F) / S;
	}
	// Align: 0 = left / top, 0.5 = center, 1 = right / bottom of Pos. The outline fades with
	// the text, like a Godot modulate (Slate doesn't tint outlines on its own).
	void Text(const FString& Str, FVector2D Pos, float Px, const FLinearColor& Color, float AlignX = 0.f, float Outline = 6.f,
		float OutlineAlpha = 0.75f, float AlignY = 0.f) const
	{
		if (Str.IsEmpty() || Color.A <= 0.001f) return;
		const FSlateFontInfo F = Font(Px, Outline, OutlineAlpha * Color.A);
		const FVector2D M = Measure(Str, F);
		const FVector2D At = Pos - FVector2D(M.X * AlignX, M.Y * AlignY);
		FSlateDrawElement::MakeText(Out, Layer + 1, Geo.ToPaintGeometry(FVector2f(M * S), FSlateLayoutTransform(FVector2f(At * S))), Str, F,
			ESlateDrawEffect::None, WWO::Lin(Color));
	}
	void Material(UMaterialInstanceDynamic* Mat, FVector2D Offset = FVector2D::ZeroVector) const
	{
		if (!Mat) return;
		FSlateBrush Brush;
		Brush.SetResourceObject(Mat);
		Brush.ImageSize = FVector2D(Size * S);
		FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(FVector2f(Size * S), FSlateLayoutTransform(FVector2f(Offset * S))), &Brush,
			ESlateDrawEffect::None, FLinearColor::White);
	}
};
