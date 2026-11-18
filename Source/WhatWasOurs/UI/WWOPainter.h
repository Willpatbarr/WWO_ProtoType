#pragma once

// Immediate-mode drawing in the Godot 1600 x 900 layout, scaled to the real size: what the HUD
// paints with (UWWOHUDWidget, UI/WWOHUDPaint.cpp). Build one per paint with the widget's
// geometry, the layer, the scale and the layout size (WWOHUD::Scale, UI/WWOHUDLayout.h, gives the scale).

#include "CoreMinimal.h"
#include "WWOTypes.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "UI/WWOHUDLayout.h"
#include "UI/WWOUI.h"

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
	// A ring: Sides straight pieces round Center, Width wide (all in Godot pixels).
	void Ring(FVector2D Center, float Radius, const FLinearColor& Color, float Width, int32 Sides = 28) const
	{
		if (Color.A <= 0.001f) return;
		TArray<FVector2f> Points;
		for (int32 I = 0; I <= Sides; ++I)
		{
			const float A = UE_TWO_PI * I / Sides;
			Points.Add(FVector2f((Center + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius) * S));
		}
		FSlateDrawElement::MakeLines(Out, Layer, Geo.ToPaintGeometry(), Points, ESlateDrawEffect::None, WWO::Lin(Color), true, Width * S);
	}
	// A filled disc, as a fan of triangles in one draw (Tri's colour note applies).
	void Disc(FVector2D Center, float Radius, const FLinearColor& Color, int32 Sides = 28) const
	{
		if (Color.A <= 0.001f || !FSlateApplication::IsInitialized()) return;
		const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*White);
		const FColor Packed = WWO::Lin(Color).ToFColor(false);
		const FSlateRenderTransform& To = Geo.GetAccumulatedRenderTransform();
		TArray<FSlateVertex> Verts;
		TArray<SlateIndex> Indices;
		Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(To, FVector2f(Center * S), FVector2f(0.5f, 0.5f), Packed));
		for (int32 I = 0; I <= Sides; ++I)
		{
			const float A = UE_TWO_PI * I / Sides;
			const FVector2D V = Center + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius;
			Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(To, FVector2f(V * S), FVector2f(0.5f, 0.5f), Packed));
			if (I > 0) Indices.Append({ 0, (SlateIndex)I, (SlateIndex)(I + 1) });
		}
		FSlateDrawElement::MakeCustomVerts(Out, Layer, Handle, Verts, Indices, nullptr, 0, 0);
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
	// Poppins (WWOUI::FontAt), no outline. Sizes are Godot pixels (WWOUI::Type); Slate's are points at
	// 96 dpi (0.75 pt/px).
	FSlateFontInfo Font(float Px, WWOUI::EWeight Weight = WWOUI::EWeight::Medium) const
	{
		FSlateFontInfo F = WWOUI::HudFontAt(Px * 0.75f * S, Weight); // words in Poppins Light, digits in Weight
		F.LetterSpacing = CapsSpacing; // capitals set a little apart
		return F;
	}
	// Every word on the HUD is in capitals (digits are unaffected): Text and Measure both take the
	// string as written and work on its capitals, so callers lay out with the same widths they draw.
	static constexpr int32 CapsSpacing = 40; // thousandths of an em
	FVector2D Measure(const FString& Text, const FSlateFontInfo& F) const
	{
		return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text.ToUpper(), F) / S;
	}
	// Text at Pos. Align: 0 = left / top, 0.5 = center, 1 = right / bottom of Pos. Shadow (0..1) is a
	// soft drop shadow under it, for text over the world (0: none, for text on a panel); it fades
	// with the text. Returns the text's size.
	FVector2D Text(const FString& Written, FVector2D Pos, float Px, const FLinearColor& Color, float AlignX = 0.f, float AlignY = 0.f,
		WWOUI::EWeight Weight = WWOUI::EWeight::Medium, float Shadow = 1.f) const
	{
		if (Written.IsEmpty() || Color.A <= 0.001f) return FVector2D::ZeroVector;
		const FString Str = Written.ToUpper();
		const FSlateFontInfo F = Font(Px, Weight);
		const FVector2D M = Measure(Str, F);
		const FVector2D At = Pos - FVector2D(M.X * AlignX, M.Y * AlignY);
		if (Shadow > 0.f)
		{
			// A soft shadow: a few faint copies a little down, spread sideways, so it reads as a blur
			// under the letters rather than a hard edge round them. Grows a touch with the text.
			const float Drop = FMath::Clamp(Px * 0.06f, 1.f, 2.5f);
			const FLinearColor Dark(0.f, 0.f, 0.f, 0.3f * Shadow * Color.A); // Poppins Light is thin: a little more under it
			for (const FVector2D Off : { FVector2D(0.f, Drop), FVector2D(-Drop * 0.6f, Drop * 0.8f), FVector2D(Drop * 0.6f, Drop * 0.8f), FVector2D(0.f, Drop * 1.7f) })
			{
				FSlateDrawElement::MakeText(Out, Layer + 1, Geo.ToPaintGeometry(FVector2f(M * S), FSlateLayoutTransform(FVector2f((At + Off) * S))), Str, F,
					ESlateDrawEffect::None, Dark);
			}
		}
		FSlateDrawElement::MakeText(Out, Layer + 2, Geo.ToPaintGeometry(FVector2f(M * S), FSlateLayoutTransform(FVector2f(At * S))), Str, F,
			ESlateDrawEffect::None, WWO::Lin(Color));
		return M;
	}
	// A rounded panel (WWOUI's corner unless given): Fill inside an optional Edge, Width wide.
	void Panel(FVector2D Pos, FVector2D BoxSize, const FLinearColor& Fill, const FLinearColor& Edge = FLinearColor(0, 0, 0, 0), float Width = 0.f,
		float Radius = WWOUI::Corner) const
	{
		if (Fill.A <= 0.001f && (Edge.A <= 0.001f || Width <= 0.f)) return;
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(FLinearColor::White);
		const float R = FMath::Min(Radius, FMath::Min(BoxSize.X, BoxSize.Y) * 0.5f) * S;
		Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(R, R, R, R), FSlateColor(WWO::Lin(Edge)), Width * S);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(FVector2f(BoxSize * S), FSlateLayoutTransform(FVector2f(Pos * S))), &Brush,
			ESlateDrawEffect::None, WWO::Lin(Fill));
	}
	// A rounded panel with its own radius at each corner (top left, top right, bottom right, bottom
	// left; each kept to half the box's short side): a part of a rounded box, such as its top.
	void PanelCorners(FVector2D Pos, FVector2D BoxSize, const FLinearColor& Fill, const FVector4& Radii) const
	{
		if (Fill.A <= 0.001f || BoxSize.X <= 0.f || BoxSize.Y <= 0.f) return;
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(FLinearColor::White);
		const float Most = FMath::Min(BoxSize.X, BoxSize.Y) * 0.5f;
		const FVector4 R(FMath::Min((float)Radii.X, Most) * S, FMath::Min((float)Radii.Y, Most) * S, FMath::Min((float)Radii.Z, Most) * S, FMath::Min((float)Radii.W, Most) * S);
		Brush.OutlineSettings = FSlateBrushOutlineSettings(R, FSlateColor(FLinearColor(0, 0, 0, 0)), 0.f);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(FVector2f(BoxSize * S), FSlateLayoutTransform(FVector2f(Pos * S))), &Brush,
			ESlateDrawEffect::None, WWO::Lin(Fill));
	}
	// A texture drawn as a rounded box (WWOUI's corner unless given): the HUD's badges, rounded like
	// every other box. Tint multiplies it (white: as it is).
	void RoundImage(UTexture2D* Texture, FVector2D Pos, FVector2D BoxSize, const FLinearColor& Tint = FLinearColor::White, float Radius = WWOUI::Corner) const
	{
		if (!Texture || Tint.A <= 0.001f) return;
		FSlateBrush Brush;
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = FVector2D(BoxSize * S);
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		const float R = FMath::Min(Radius, FMath::Min(BoxSize.X, BoxSize.Y) * 0.5f) * S;
		Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(R, R, R, R), FSlateColor(FLinearColor(0, 0, 0, 0)), 0.f);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(FVector2f(BoxSize * S), FSlateLayoutTransform(FVector2f(Pos * S))), &Brush,
			ESlateDrawEffect::None, Tint);
	}
	// A texture (a UI symbol, say) stretched over the box; Tint multiplies it (white: as it is).
	void Image(UTexture2D* Texture, FVector2D Pos, FVector2D BoxSize, const FLinearColor& Tint = FLinearColor::White) const
	{
		if (!Texture || Tint.A <= 0.001f) return;
		FSlateBrush Brush;
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = FVector2D(BoxSize * S);
		FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(FVector2f(BoxSize * S), FSlateLayoutTransform(FVector2f(Pos * S))), &Brush,
			ESlateDrawEffect::None, Tint);
	}
	// A UI material drawn over a box (the HUD's weapon pictures).
	void MaterialBox(UMaterialInstanceDynamic* Mat, FVector2D Pos, FVector2D BoxSize) const
	{
		if (!Mat) return;
		FSlateBrush Brush;
		Brush.SetResourceObject(Mat);
		Brush.ImageSize = FVector2D(BoxSize * S);
		FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(FVector2f(BoxSize * S), FSlateLayoutTransform(FVector2f(Pos * S))), &Brush,
			ESlateDrawEffect::None, FLinearColor::White);
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
