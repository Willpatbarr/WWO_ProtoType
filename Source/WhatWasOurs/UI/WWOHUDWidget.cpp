#include "UI/WWOHUDWidget.h"
#include "Game/WWOGameState.h"
#include "Game/WWOPlayerState.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOAimAssistComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOAbilityComponent.h"
#include "Flora/WWOSporeFog.h"
#include "Flora/WWOFloraEffects.h"
#include "Player/WWOGrenadeComponent.h"
#include "Player/WWOUltComponent.h"
#include "Allies/WWOAlly.h"
#include "Game/WWOGameMode.h"
#include "FX/WWOFX.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraComponent.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"


static TAutoConsoleVariable<int32> CVarWWOShowFPS(TEXT("wwo.ShowFPS"), 1, TEXT("Show the frame rate in the top left of the HUD (1 = on, 0 = off)."));

namespace
{
	const FLinearColor ReticleColor(1, 1, 1, 0.9f);
	const FLinearColor ReticleOnTarget(1.f, 0.25f, 0.2f, 0.95f);
	// One red for the radar's lit sections and enemies' health bars. Triangles (FPainter::Tri) come
	// out darker on screen than boxes given the same colour, so each has the value that lands on the
	// same shade (about 205, 46, 41 of 255).
	const FLinearColor FoeRed(0.92f, 0.45f, 0.41f);      // for Tri
	const FLinearColor FoeRedBox(0.8f, 0.18f, 0.16f);    // for Box
	const FLinearColor ReticleOnFriend(0.25f, 1.f, 0.35f, 0.95f); // over someone you can't hurt (a teammate)
	const FLinearColor CritColor(1.f, 0.85f, 0.2f);
	const FLinearColor KillColor(1.f, 0.25f, 0.2f);
	const FLinearColor ShieldColor(0.55f, 0.8f, 1.f);
	const FLinearColor HealthColor(0.95f, 0.95f, 0.95f);
	const FLinearColor HealthLowColor(1.f, 0.3f, 0.25f);
	constexpr float HitMarkerTime = 0.18f;
	constexpr float KillMarkerTime = 0.4f;
	constexpr float LowAmmo = 0.25f;

	// Paints in the Godot 1600 x 900 layout, scaled to the real size.
	struct FPainter
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

	void ChargeBox(const FPainter& P, FVector2D Pos, FVector2D BoxSize, const FLinearColor& Color, float Charge)
	{
		// Full color when charged; gray with the color filling up from the bottom otherwise.
		if (Charge >= 1.f)
		{
			P.Box(Pos, BoxSize, Color);
			P.Outline(Pos, BoxSize, FLinearColor(1, 1, 1, 0.85f), 3.f);
			return;
		}
		P.Box(Pos, BoxSize, FLinearColor(0.25f, 0.25f, 0.27f, 0.9f));
		const float Fill = BoxSize.Y * FMath::Clamp(Charge, 0.f, 1.f);
		P.Box(FVector2D(Pos.X, Pos.Y + BoxSize.Y - Fill), FVector2D(BoxSize.X, Fill), WWO::Darkened(Color, 0.35f));
		P.Outline(Pos, BoxSize, FLinearColor(0.6f, 0.6f, 0.6f, 0.7f), 2.f);
	}
}

AWWOCharacter* UWWOHUDWidget::Player() const
{
	const APlayerController* PC = GetOwningPlayer();
	return PC ? Cast<AWWOCharacter>(PC->GetPawn()) : nullptr;
}

void UWWOHUDWidget::Bind(AWWOCharacter* P)
{
	BoundPlayer = P;
	P->Weapon->OnHitConfirmed.AddWeakLambda(this, [this](bool bCritical, bool bKilled)
	{
		HitTime = bKilled ? KillMarkerTime : HitMarkerTime;
		bHitCritical = bCritical;
		bHitKilled = bKilled;
	});
	P->Health->OnDamaged.AddWeakLambda(this, [this](float) { DamageFlash = 0.3f; });
}

void UWWOHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	AWWOCharacter* P = Player();
	if (P && BoundPlayer.Get() != P) Bind(P);
	HitTime -= GetWorld() ? GetWorld()->GetDeltaSeconds() : InDeltaTime; // game time, like Godot's _process (slows in a hit-stop)
	DamageFlash = WWO::MoveToward(DamageFlash, 0.f, 1.2f * InDeltaTime);
	// How hurt you are, 0 (full health and shield) to 1 (nothing left): the vignette follows it,
	// quickly as damage lands and a little slower as you heal.
	float Hurt = 0.f;
	if (P && P->Health)
	{
		const float Most = P->Health->MaxHealth() + P->Health->MaxShield();
		Hurt = Most > 0.f ? 1.f - FMath::Clamp((P->Health->Health + P->Health->Shield) / Most, 0.f, 1.f) : 0.f;
	}
	HurtShown = WWO::MoveToward(HurtShown, Hurt, (Hurt > HurtShown ? 6.f : 1.5f) * InDeltaTime);
	FpsClock += FApp::GetDeltaTime(); // real time, so hit-stops and pauses don't skew it
	++FpsFrames;
	if (FpsClock >= 0.25f)
	{
		FpsShown = FpsFrames / FpsClock;
		FpsClock = 0.f;
		FpsFrames = 0;
	}
	if (!VignetteMat)
	{
		if (UMaterialInterface* M = WWOFX::Material(TEXT("M_WWO_Vignette"))) VignetteMat = UMaterialInstanceDynamic::Create(M, this);
		if (UMaterialInterface* M = WWOFX::Material(TEXT("M_WWO_Scope"))) ScopeMat = UMaterialInstanceDynamic::Create(M, this);
	}
}

int32 UWWOHUDWidget::RadarBands(float Distance, float Range, float FullRange, int32 Steps)
{
	Steps = FMath::Max(Steps, 1);
	if (Distance > Range) return 0;
	if (Distance <= FullRange || Steps == 1 || Range <= FullRange) return Steps;
	// The stretch from the range's edge in to the full range, cut into Steps - 1 equal parts.
	const float In = (Range - Distance) / (Range - FullRange); // 0 at the edge, 1 at the full range
	return FMath::Clamp(1 + FMath::FloorToInt(In * (Steps - 1)), 1, Steps - 1);
}

int32 UWWOHUDWidget::Radar(const AWWOCharacter* Viewer, TArray<float>& Out)
{
	const int32 Steps = Viewer ? FMath::Clamp(FMath::RoundToInt(Viewer->RadarSteps), 1, 12) : 1;
	Out.Init(0.f, 8 * Steps);
	const UWWOWorldSubsystem* WS = Viewer ? UWWOWorldSubsystem::Get(Viewer) : nullptr;
	if (!WS || !Viewer->Camera) return Steps;
	const float Yaw = Viewer->Camera->GetComponentRotation().Yaw;
	const FVector From = Viewer->FeetLocation();
	for (const AActor* Actor : WS->LivingTargets(Viewer)) // enemies, and in PvP the other team
	{
		const IWWOTargetable* Target = Cast<IWWOTargetable>(Actor);
		if (!Target || Actor == Viewer) continue;
		const FVector To = Target->FeetLocation() - From;
		const int32 Bands = RadarBands(FVector2D(To.X, To.Y).Size() / WWO::M, Viewer->RadarRange, Viewer->RadarFullRange, Steps);
		if (Bands <= 0) continue;
		// Which way, from the way you look: 0 ahead, 90 to the right. (Right on top of you: ahead.)
		const float Bearing = FMath::FindDeltaAngleDegrees(Yaw, FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X)));
		const int32 Section = ((FMath::RoundToInt(Bearing / 45.f) % 8) + 8) % 8;
		const float Level = FMath::Clamp(FMath::Abs(To.Z) / WWO::M / FMath::Max(Viewer->RadarHeightFade, 0.1f), 0.f, 1.f);
		const float Alpha = FMath::Lerp(1.f, FMath::Clamp(Viewer->RadarMinAlpha, 0.f, 1.f), Level);
		for (int32 Band = 0; Band < Bands; ++Band) Out[Section * Steps + Band] = FMath::Max(Out[Section * Steps + Band], Alpha);
	}
	return Steps;
}

int32 UWWOHUDWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling,
	FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const int32 Result = Super::NativePaint(Args, Geo, Culling, Out, LayerId, Style, bParentEnabled);
	AWWOCharacter* Pl = Player();
	APlayerController* PC = GetOwningPlayer();
	if (!Pl || !PC) return Result;
	// Godot's canvas_items stretch with aspect "expand": the 1600 x 900 layout scales by the
	// tighter of the two ratios, and the extra width or height is added to the canvas.
	const FVector2D Local = Geo.GetLocalSize();
	const float S = FMath::Min(Local.X / 1600.f, Local.Y / 900.f);
	FPainter P{ Out, Geo, LayerId + 1, S, Local / S };
	const FVector2D Size = P.Size;
	const FVector2D Center = Size * 0.5f;
	UWWOWeaponComponent* Gun = Pl->Weapon;
	const UWWOWeaponData* W = Gun->GetWeaponData();
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(Pl);
	UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(Pl);
	const float Aimed = Gun->AdsAmount;
	const float DropPx = Pl->Ult->bActive ? 0.f : Size.Y * Pl->ReticleDrop; // the low reticle (none in the ult's third person)
	const FVector2D AimAt = Center + FVector2D(0.f, DropPx);
	auto ToScreen = [PC, S](const FVector& World, FVector2D& Out2D) -> bool
	{
		FVector2D Screen;
		if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, World, Screen, true)) return false;
		Out2D = Screen / S;
		return true;
	};

	// Low health: red creeps in from the edges, further and stronger the less you have left
	// (under everything else on the HUD). Faint with the shield half gone, heavy near death.
	if (HurtShown > 0.01f && Pl->HealthVignette > 0.f)
	{
		const float Amount = FMath::Pow(HurtShown, 1.6f);
		P.Vignette(FLinearColor(0.75f, 0.03f, 0.02f, FMath::Clamp(0.9f * Amount * Pl->HealthVignette, 0.f, 0.95f)), FMath::Lerp(0.95f, 0.3f, Amount));
	}
	// ADS vignette and scope.
	if (W && VignetteMat && W->AdsVignette * Aimed > 0.001f)
	{
		VignetteMat->SetScalarParameterValue("Strength", W->AdsVignette * Aimed);
		P.Material(VignetteMat);
	}
	if (W && ScopeMat && W->ScopeOverlay >= 0.5f && Aimed > 0.8f)
	{
		ScopeMat->SetScalarParameterValue("Strength", WWO::SmoothStep(0.8f, 1.f, Aimed));
		ScopeMat->SetScalarParameterValue("Aspect", Size.X / Size.Y);
		// Centered on the low reticle: drawn shifted down, with the strip it leaves at the top black
		// like the rest of the scope's outside.
		P.Material(ScopeMat, FVector2D(0.f, DropPx));
		if (DropPx > 0.f) P.Box(FVector2D::ZeroVector, FVector2D(Size.X, DropPx + 1.f), FLinearColor(0, 0, 0, WWO::SmoothStep(0.8f, 1.f, Aimed)));
	}

	// Floating damage numbers and words: Godot's fixed-size Label3D (font 36, crits 48, pixel
	// size 0.0006, outline 8). Fixed size keeps it at depth 1 m, so it's the same size at any
	// distance but still scales with the camera FOV (bigger when zoomed in). Centered on its point.
	{
		const float PxPerFontPx = 0.0006f * Size.X / (2.f * FMath::Tan(FMath::DegreesToRadians(Pl->Camera->FieldOfView) * 0.5f));
		for (const FWWOFloatingText& T : WS->FloatingTexts())
		{
			FVector2D At;
			const FVector World = T.Start + FVector(0, T.Jitter, 60.f * FMath::Min(T.Age / FWWOFloatingText::RiseTime, 1.f));
			if (!ToScreen(World, At)) continue;
			// Off to the side of the hit (left or right by its jitter) so the number doesn't cover the target.
			if (!T.Follow.IsValid()) At.X += (T.Jitter >= 0.f ? 1.f : -1.f) * Pl->DamageNumberOffset;
			const float FontPx = T.bCritical ? 48.f : 36.f;
			P.Text(T.Text, At, FontPx * PxPerFontPx, WWO::WithAlpha(T.Color, T.Alpha()), 0.5f, 8.f * PxPerFontPx, 0.8f, 0.5f);
		}
	}
	// Teammate labels.
	for (const AWWOAlly* Ally : WS->AllTeammates())
	{
		FVector2D At;
		if (!ToScreen(Ally->GetActorLocation() + FVector(0, 0, 125.f), At)) continue;
		const FLinearColor C = Ally->IsDown() ? FLinearColor(1, 0.4f, 0.35f) : (Ally->Shield > 0.f ? ShieldColor : FLinearColor(1, 0.9f, 0.9f));
		P.Text(Ally->StatusText(), At, 15.f, C, 0.5f);
	}

	// Where shots go: ReticleDrop below center (Destiny-style), hip and aimed.
	// Reticle: fades out early as you aim in, red over a target. None in third person.
	// Over a teammate (someone you can't hurt) it turns green, never the on-target red.
	const AActor* OverNow = Gun->ReticleTarget.Get();
	const bool bOverFriend = OverNow && !UWWOWorldSubsystem::CanHurt(Pl, OverNow);
	const bool bOverFoe = Gun->bTargetUnderReticle && !bOverFriend;
	FLinearColor Ret = bOverFriend ? ReticleOnFriend : (bOverFoe ? ReticleOnTarget : ReticleColor);
	Ret.A *= FMath::Clamp(1.f - Aimed * 2.5f, 0.f, 1.f);
	if (Pl->Ult->bActive) Ret.A = 0.f;
	if (Ret.A > 0.f)
	{
		// The ticks open out to the gun's hip-fire spread (shots land inside them).
		const float TanHalfV = FMath::Tan(FMath::DegreesToRadians(Pl->Camera->FieldOfView) * 0.5f) * Size.Y / FMath::Max(Size.X, 1.f);
		const float Bloom = W ? FMath::Tan(FMath::DegreesToRadians(W->HipSpread * (1.f - Aimed))) / FMath::Max(TanHalfV, 0.01f) * Size.Y * 0.5f : 0.f;
		for (const FVector2D Dir : { FVector2D(0, -1), FVector2D(0, 1), FVector2D(-1, 0), FVector2D(1, 0) })
		{
			P.Line(AimAt + Dir * (7.f + Bloom), AimAt + Dir * (16.f + Bloom), Ret, 2.f);
		}
		P.Dot(AimAt, 1.5f, Ret);
	}
	// Aimed in: the aim mark, here on the HUD at the point shots go to (not on the gun, whose kick,
	// sway and strafe lean would carry it off that point). A dot for dot and holo sights, three
	// small marks for iron sights; a scope draws its own.
	if (W && W->AdsMark >= 0.5f && W->ScopeOverlay < 0.5f && !Pl->Ult->bActive)
	{
		FLinearColor Mark = bOverFriend ? ReticleOnFriend : (bOverFoe ? FLinearColor(1.f, 0.1f, 0.08f) : FLinearColor(0.95f, 0.95f, 0.95f));
		Mark.A = FMath::Clamp((Aimed - 0.55f) / 0.35f, 0.f, 1.f);
		if (Mark.A > 0.f)
		{
			if (W->AdsMark < 1.5f) P.Dot(AimAt, FMath::Max(Gun->AdsDotSize, 0.5f), Mark);
			else
			{
				// Iron-sight marks sit on the aim point, but they belong to the sight: while the gun is
				// flipped up by a shot they swing away with it (down and to the right), as in the footage.
				// The pip stays on the aim point; the two brackets are the gun's, so they are drawn where its
				// sight point is seen and turned with it: they trail in a strafe, sway, and swing off with the kick.
				FVector2D At = AimAt, Seen;
				float Roll = 0.f;
				if (Gun->SightOnScreen(Seen, Roll)) At = Center + Seen * Size.X * 0.5f;
				const FVector2D AxisX(FMath::Cos(Roll), FMath::Sin(Roll)), AxisY(-FMath::Sin(Roll), FMath::Cos(Roll));
				const float Flip = Gun->KickAmount();
				// A pip between two brackets, ( . ), in the sight's own red (green over a teammate).
				if (!bOverFriend) { const float A = Mark.A; Mark = FLinearColor(1.f, 0.3f, 0.3f, A); }
				const float Gap = 0.03f * Size.Y, Tall = 0.011f * Size.Y, Bow = 0.004f * Size.Y;
				P.Dot(AimAt, 1.4f, Mark);
				Mark.A *= 1.f - 0.5f * Flip;
				for (const float Side : { -1.f, 1.f })
				{
					const FVector2D Mid = At + AxisX * (Side * Gap);
					P.Line(Mid - AxisX * (Side * Bow) - AxisY * Tall, Mid, Mark, 1.5f);
					P.Line(Mid, Mid - AxisX * (Side * Bow) + AxisY * Tall, Mark, 1.5f);
				}
			}
		}
	}
	// Hit marker: four diagonal ticks; bigger and yellow for crits, red for kills.
	if (HitTime > 0.f)
	{
		const float MSize = bHitKilled ? 1.8f : (bHitCritical ? 1.4f : 1.f);
		const float Width = bHitKilled ? 3.5f : (bHitCritical ? 3.f : 2.f);
		FLinearColor C = bHitKilled ? KillColor : (bHitCritical ? CritColor : FLinearColor::White);
		C.A = FMath::Clamp(HitTime / (bHitKilled ? KillMarkerTime : HitMarkerTime), 0.f, 1.f);
		for (const FVector2D Dir : { FVector2D(1, 1), FVector2D(1, -1), FVector2D(-1, 1), FVector2D(-1, -1) })
		{
			const FVector2D D = Dir.GetSafeNormal();
			P.Line(AimAt + D * 9.f * MSize, AimAt + D * 19.f * MSize, C, Width);
		}
	}
	// Aim assist debug cones.
	const UWWOAimAssistComponent* Assist = Pl->AimAssist;
	if (Assist->ShowDebug >= 0.5f && W)
	{
		const float HalfFov = FMath::DegreesToRadians(Pl->Camera->FieldOfView) * 0.5f;
		auto ConePx = [&](float AngleDeg, bool bScaled)
		{
			return Size.X * 0.5f * FMath::Tan(FMath::DegreesToRadians(AngleDeg) * (bScaled ? Assist->ConeScale() : 1.f)) / FMath::Tan(HalfFov);
		};
		P.Circle(AimAt, ConePx(W->MagnetismAngle, true), FLinearColor(0.3f, 0.9f, 1.f, 0.8f), 1.5f);
		P.Circle(AimAt, ConePx(W->FrictionAngle, true), FLinearColor(1.f, 0.85f, 0.2f, 0.8f), 1.5f);
		P.Circle(AimAt, ConePx(Gun->LungeAngle, false), FLinearColor(1.f, 0.3f, 0.3f, 0.7f), 1.5f, 64);
		FVector2D At;
		if (Assist->FrictionPart.IsValid() && ToScreen(Assist->FrictionPart.Position, At)) P.Circle(At, 6.f, FLinearColor(1.f, 0.85f, 0.2f, 0.9f), 2.f, 16);
		P.Text(FString::Printf(TEXT("Aim assist: %s (strength %d%%)  range %.0f m"), Assist->UsingController() ? TEXT("controller") : TEXT("mouse"),
			FMath::RoundToInt(Assist->ConeScale() * 100.f), Assist->EffectiveRange()), AimAt + FVector2D(0, 120.f - 17.f), 18.f, FLinearColor::White, 0.5f, 0.f); // Godot: draw_string, baseline at +120, no outline
	}
	// Hidden assist markers (Settings, Aim Assist): what the after-shot assist just did, under the
	// reticle for 0.4 s each. Cyan STEER: the recoil's fall-back bent toward the target. Magenta
	// MAGNET: a shot hit only thanks to the wider follow-up cone. Yellow STICKY: boosted friction or
	// adhesion was working. A thin ring shows while the window is open.
	if (Assist->ShowHiddenAssist >= 0.5f && Assist->HiddenAssist >= 0.5f)
	{
		const double Now = Pl->GetWorld()->GetTimeSeconds();
		if (Assist->HiddenWindow() > 0.f) P.Circle(AimAt, 26.f, FLinearColor(1, 1, 1, 0.35f * Assist->HiddenWindow()), 1.f, 32);
		struct FMark { double At; const TCHAR* Label; FLinearColor Color; };
		const FMark Marks[] = {
			{ Assist->LastSteer, TEXT("STEER"), FLinearColor(0.3f, 0.9f, 1.f) },
			{ Assist->LastMagnet, TEXT("MAGNET"), FLinearColor(1.f, 0.35f, 0.9f) },
			{ Assist->LastSticky, TEXT("STICKY"), FLinearColor(1.f, 0.85f, 0.2f) },
		};
		float Y = 34.f;
		for (const FMark& M : Marks)
		{
			const float Age = (float)(Now - M.At);
			if (Age > 0.4f) continue;
			P.Text(M.Label, AimAt + FVector2D(0.f, Y), 15.f, WWO::WithAlpha(M.Color, 1.f - Age / 0.4f), 0.5f, 4.f);
			Y += 17.f;
		}
	}
	// Health bars: the target under the reticle, and every other visible enemy whose middle is
	// within HealthBarRadius of the reticle on screen. A thin shield bar sits right on top of the
	// health bar (no gap, no border), and the pair floats a little clear of the head.
	auto HealthBar = [&](const IWWOTargetable* Target, const FLinearColor& BarHealthColor = FoeRedBox, const FLinearColor& BarShieldColor = ShieldColor)
	{
		FVector2D Top;
		if (!ToScreen(Target->FeetLocation() + FVector(0, 0, Target->GetBodyHeight()), Top)) return;
		constexpr float Wide = 126.f, HealthTall = 7.f, ShieldTall = 3.f, Lift = 16.f;
		const bool bShield = Target->GetMaxShield() > 0.f;
		const FVector2D Pos = Top - FVector2D(Wide * 0.5f, HealthTall + Lift); // the health bar's top left
		const FVector2D TopLeft = Pos - FVector2D(0.f, bShield ? ShieldTall : 0.f);
		const float Fraction = FMath::Clamp(Target->GetHealth() / FMath::Max(Target->GetMaxHealth(), 1.f), 0.f, 1.f);
		// No border: just a faint dark track the size of the bars, so the part that is gone still reads.
		P.Box(TopLeft, FVector2D(Wide, HealthTall + (bShield ? ShieldTall : 0.f)), FLinearColor(0, 0, 0, 0.35f));
		P.Box(Pos, FVector2D(Wide * Fraction, HealthTall), BarHealthColor);
		FString Label = FString::FromInt(FMath::CeilToInt(Target->GetHealth()));
		if (bShield)
		{
			P.Box(TopLeft, FVector2D(Wide * FMath::Clamp(Target->GetShield() / Target->GetMaxShield(), 0.f, 1.f), ShieldTall), BarShieldColor);
			if (Target->GetShield() > 0.f) Label = FString::Printf(TEXT("%d + %d"), FMath::CeilToInt(Target->GetShield()), FMath::CeilToInt(Target->GetHealth()));
		}
		P.Text(Label, TopLeft + FVector2D(Wide * 0.5f, -21), 16.f, FLinearColor::White, 0.5f, 0.f); // Godot: draw_string, baseline 6 px up, no outline
	};
	// Who is marked: a target is picked up when the reticle comes over it (at the hip) or as soon as
	// it is inside the marker range (aimed in: within HealthBarRadius of the reticle on screen, and
	// in sight), and dropped when it leaves the marker range. Enemies show their bars while marked.
	// A PvP player's outline flashes once at the moment they're picked up (Gun->Marked holds when;
	// AWWOCharacter::UpdateTeamTint plays it). Teammates' bars show whenever they're in sight, in
	// near-white (light grey health, a see-through white shield) so the only red bars are the
	// other team's.
	const AActor* Under = Gun->ReticleTarget.Get();
	const bool bAimedIn = Gun->AdsAmount > 0.5f;
	const double MarkNow = Pl->GetWorld()->GetTimeSeconds();
	// The range widens as you aim in (eased with the aim, so it doesn't jump).
	const float MarkRange = Pl->HealthBarRadius * FMath::Lerp(1.f, FMath::Max(Pl->HealthBarAdsScale, 1.f), Gun->AdsAmount);
	TMap<TWeakObjectPtr<const AActor>, double> StillMarked;
	auto Track = [&](const AActor* Actor, const IWWOTargetable* Target, bool& bInSight) -> bool
	{
		const FVector Middle = Target->FeetLocation() + FVector(0, 0, Target->GetBodyHeight() * 0.5f);
		FVector2D At;
		FHitResult Hit;
		bInSight = Actor == Under || !Gun->CastRayTo(Middle, Hit) || Hit.GetActor() == Actor;
		const bool bInRange = Actor == Under
			|| (MarkRange > 0.f && bInSight && ToScreen(Middle, At) && FVector2D::Distance(At, AimAt) <= MarkRange);
		const double* Since = Gun->Marked.Find(Actor);
		if (Since ? bInRange : (Actor == Under || (bAimedIn && bInRange)))
		{
			StillMarked.Add(Actor, Since ? *Since : MarkNow);
			return true;
		}
		return false;
	};
	if (!Pl->Ult->bActive)
	{
		for (AActor* Actor : WS->LivingTargets(Pl))
		{
			const IWWOTargetable* Target = Cast<IWWOTargetable>(Actor);
			bool bInSight = false;
			if (Target && !Target->IsDead() && Track(Actor, Target, bInSight)) HealthBar(Target);
		}
		if (UWWOWorldSubsystem::IsPvP(Pl))
		{
			for (AWWOCharacter* Mate : WS->GetPlayers())
			{
				if (Mate == Pl || Mate->Health->bDead || UWWOWorldSubsystem::CanHurt(Pl, Mate)) continue;
				bool bInSight = false;
				Track(Mate, Mate, bInSight);
				if (bInSight) HealthBar(Mate, FLinearColor(0.82f, 0.83f, 0.85f), FLinearColor(1.f, 1.f, 1.f, 0.6f));
			}
		}
	}
	Gun->Marked = MoveTemp(StillMarked);

	// PvP: an open chevron over each teammate's head, kept (fainter) through walls so you know where
	// your side is. The other team has no marker: the radar says roughly where they are.
	if (UWWOWorldSubsystem::IsPvP(Pl))
	{
		const int32 MyTeam = UWWOWorldSubsystem::TeamOf(Pl);
		for (AWWOCharacter* Other : WS->GetPlayers())
		{
			if (Other == Pl || Other->Health->bDead || UWWOWorldSubsystem::TeamOf(Other) != MyTeam || MyTeam < 0) continue;
			FVector2D At;
			if (!ToScreen(Other->FeetLocation() + FVector(0, 0, Other->GetBodyHeight()), At)) continue;
			At.Y -= 66.f;
			FHitResult Hit;
			const bool bInSight = !Gun->CastRayTo(Other->GetAimParts()[0].Position, Hit) || Hit.GetActor() == Other;
			const FLinearColor C = WWO::WithAlpha(AWWOPlayerState::TeamColor(MyTeam), bInSight ? 1.f : 0.45f);
			const FLinearColor Back(0, 0, 0, bInSight ? 0.7f : 0.3f);
			const FVector2D L(-8.f, -6.f), M(0.f, 4.f), R(8.f, -6.f);
			P.Line(At + L, At + M, Back, 6.f);
			P.Line(At + M, At + R, Back, 6.f);
			P.Line(At + L, At + M, C, 3.f);
			P.Line(At + M, At + R, C, 3.f);
		}
	}

	// The radar (the top left corner; the frame rate goes under it).
	if (Pl->RadarSize > 1.f && !Pl->Health->bDead)
	{
		TArray<float> Lit;
		const int32 Steps = Radar(Pl, Lit);
		const float R = Pl->RadarSize, Hub = R * 0.2f;
		const FVector2D Mid(24.f + R, 24.f + R);
		const FLinearColor Base(0.5f, 0.5f, 0.5f, 0.1f), Split(0.85f, 0.85f, 0.85f, 0.3f), Rim(0.85f, 0.85f, 0.85f, 0.9f), Foe = FoeRed, You(0.68f, 0.83f, 1.f); // pale (these darken a good deal on the way to the screen)
		constexpr float Gap = 0.035f; // radians left clear each side of a section
		constexpr int32 Slices = 6;
		// A piece of a ring: between two radii, across one section (less the gaps).
		auto Arc = [&](int32 Section, float Inner, float Outer, const FLinearColor& Color)
		{
			const float From = (Section - 0.5f) * UE_PI / 4.f + Gap, To = (Section + 0.5f) * UE_PI / 4.f - Gap;
			for (int32 i = 0; i < Slices; ++i)
			{
				// Angle 0 is up the screen (the way you look) and they go clockwise.
				const float A0 = FMath::Lerp(From, To, float(i) / Slices), A1 = FMath::Lerp(From, To, float(i + 1) / Slices);
				const FVector2D D0(FMath::Sin(A0), -FMath::Cos(A0)), D1(FMath::Sin(A1), -FMath::Cos(A1));
				P.Tri(Mid + D0 * Inner, Mid + D0 * Outer, Mid + D1 * Outer, Color);
				P.Tri(Mid + D0 * Inner, Mid + D1 * Outer, Mid + D1 * Inner, Color);
			}
		};
		for (int32 Section = 0; Section < 8; ++Section)
		{
			Arc(Section, Hub, R, Base);
			for (int32 Band = 0; Band < Steps; ++Band)
			{
				const float A = Lit[Section * Steps + Band];
				if (A <= 0.f) continue;
				// Band 0 is the rim; the last band reaches the hub.
				Arc(Section, FMath::Lerp(R, Hub, float(Band + 1) / Steps), FMath::Lerp(R, Hub, float(Band) / Steps), WWO::WithAlpha(Foe, A * 0.9f));
			}
			// The line between this section and the next.
			const float A = (Section + 0.5f) * UE_PI / 4.f;
			const FVector2D D(FMath::Sin(A), -FMath::Cos(A));
			P.Line(Mid + D * Hub, Mid + D * R, Split, 1.f);
		}
		P.Circle(Mid, R, Rim, 1.f, 64);
		// You: a small triangle in the middle, pointing the way you look (it never moves).
		P.Tri(Mid + FVector2D(0.f, -Hub * 0.8f), Mid + FVector2D(Hub * 0.5f, Hub * 0.55f), Mid + FVector2D(-Hub * 0.5f, Hub * 0.55f), You);
	}

	// Ammo (bottom right): weapon name, the count (red when low), reload bar.
	if (W)
	{
		// Godot stacks these in a bottom-right VBox (4 px apart): the hidden reload bar takes no
		// room, so the labels sit 12 px lower when not reloading, and the bar spans the widest row.
		const int32 Mag = Gun->MagazineSize();
		const FVector2D Corner(Size.X - 40.f, Size.Y - 40.f);
		const FVector2D Lift(0.f, Gun->bReloading ? 0.f : -12.f);
		P.Text(W->DisplayName, Corner - FVector2D(0, 92) - Lift, 20.f, FLinearColor::White, 1.f, 6.f, 0.7f);
		if (Gun->ConfigToastLeft > 0.f)
		{
			P.Text(Gun->ConfigToast, FVector2D(Center.X, Size.Y - 150.f), 24.f, WWO::WithAlpha(FLinearColor::White, FMath::Min(Gun->ConfigToastLeft / 0.4f, 1.f)), 0.5f, 6.f, 0.7f);
		}
		// Weapon mod perks (draft): streaks, counters and timers above the weapon name.
		P.Text(Gun->PerkStatus(), Corner - FVector2D(0, 120) - Lift, 18.f, FLinearColor(0.6f, 0.85f, 1.f), 1.f, 6.f, 0.7f);
		const bool bLow = Gun->Ammo <= int32(Mag * LowAmmo);
		const FString Count = Gun->bReloading && Gun->Ammo < Mag ? TEXT("RELOADING") : FString::Printf(TEXT("%d / %d"), Gun->Ammo, Mag);
		P.Text(Count, Corner - FVector2D(0, 66) - Lift, 44.f, bLow && !Gun->bReloading ? FLinearColor(1, 0.3f, 0.25f) : FLinearColor::White, 1.f, 8.f, 0.7f);
		if (Gun->bReloading)
		{
			const float BarWidth = (float)FMath::Max3(160.0, P.Measure(Count, P.Font(44.f, 8.f)).X, P.Measure(W->DisplayName, P.Font(20.f)).X);
			P.Box(Corner - FVector2D(BarWidth, 8), FVector2D(BarWidth, 8), FLinearColor(0.15f, 0.15f, 0.17f, 0.9f));
			P.Box(Corner - FVector2D(BarWidth, 8), FVector2D(BarWidth * Gun->ReloadProgress, 8), FLinearColor(0.75f, 0.78f, 0.85f));
		}
	}

	// Grenade, abilities and ult (bottom left): a 64 px square each with its name under it.
	if (Loadout)
	{
		const float Pad = 14.f;
		FVector2D Pos(40.f, Size.Y - 40.f - 64.f - 22.f);
		const double Now = Pl->GetWorld()->GetRealTimeSeconds();
		if (const UWWOGrenadeData* G = Loadout->GetGrenade())
		{
			const float Charges = Pl->Grenades->Charge;
			ChargeBox(P, Pos, FVector2D(64, 64), G->Color, Charges >= 1.f ? 1.f : Charges);
			if (Pl->Grenades->MaxCharges() > 1.f)
			{
				// Extra Grenade: how many are ready, and the next one filling.
				const int32 Ready = FMath::FloorToInt(Charges);
				P.Text(FString::Printf(TEXT("x%d"), Ready), Pos + FVector2D(4, 2), 16.f, Ready > 0 ? FLinearColor::Black : FLinearColor::White, 0.f, 0);
				if (Ready >= 1 && Charges < Pl->Grenades->MaxCharges())
				{
					P.Box(Pos + FVector2D(0, 59), FVector2D(64 * (Charges - Ready), 5), FLinearColor(1, 1, 1, 0.8f));
				}
			}
			P.Text(G->DisplayName, Pos + FVector2D(0, 68), 16.f, FLinearColor::White, 0.f);
			Pos.X += 64 + Pad + FMath::Max(0.f, P.Measure(G->DisplayName, P.Font(16.f)).X - 64.f);
		}
		for (int32 Slot = 0; Slot < UWWOLoadoutSubsystem::AbilitySlots; ++Slot)
		{
			const UWWOAbilityData* A = Loadout->GetAbility(Slot);
			if (!A)
			{
				Pos.X += 64 + Pad; // an empty slot keeps its place in the row (Godot's empty column)
				continue;
			}
			const FVector2D Box(64, 64);
			switch (A->Kind)
			{
			case EWWOAbilityKind::Passive: // always on: halfway between ready and empty
				P.Box(Pos, Box, FMath::Lerp(A->Color, FLinearColor(0.25f, 0.25f, 0.27f), 0.5f));
				P.Outline(Pos, Box, FLinearColor(0.75f, 0.75f, 0.75f, 0.75f), 2.f);
				break;
			case EWWOAbilityKind::Active:
			case EWWOAbilityKind::Charged:
				ChargeBox(P, Pos, Box, A->Color, Pl->Abilities->Charge(A));
				if (Pl->Abilities->IsEffectActive(A))
				{
					const float Pulse = 0.6f + 0.4f * FMath::Sin(Now * 12.0);
					P.Outline(Pos - FVector2D(3, 3), Box + FVector2D(6, 6), WWO::WithAlpha(WWO::Lightened(A->Color, 0.5f), Pulse), 4.f);
				}
				break;
			case EWWOAbilityKind::Triggered:
				if (Pl->Abilities->IsEffectActive(A))
				{
					P.Box(Pos, Box, A->Color);
					P.Outline(Pos, Box, FLinearColor(1, 1, 1, 0.85f), 3.f);
				}
				else
				{
					P.Box(Pos, Box, FLinearColor(0.17f, 0.17f, 0.19f, 0.9f));
					P.Outline(Pos, Box, WWO::WithAlpha(A->Color, 0.45f), 2.f);
				}
				break;
			}
			P.Text(A->DisplayName, Pos + FVector2D(0, 68), 16.f, FLinearColor::White, 0.f);
			Pos.X += 64 + Pad + FMath::Max(0.f, P.Measure(A->DisplayName, P.Font(16.f)).X - 64.f);
		}
		// Ult: fills in the grenade's color, pulses when ready.
		const FLinearColor UltColor = Loadout->GetGrenade() ? Loadout->GetGrenade()->Color : FLinearColor(1.f, 0.6f, 0.2f);
		ChargeBox(P, Pos, FVector2D(64, 64), UltColor, Pl->Ult->Charge);
		if (Pl->Ult->IsReady() && !Pl->Ult->bActive)
		{
			P.Outline(Pos - FVector2D(3, 3), FVector2D(70, 70), FLinearColor(1, 1, 1, 0.5f + 0.5f * FMath::Sin(Now * 8.0)), 3.f);
		}
		P.Text(TEXT("Ult"), Pos + FVector2D(0, 68), 16.f, FLinearColor::White, 0.f);
	}

	// Vitals (top center, under the match line): shield bar over health bar. Each bar's width is
	// its max against the default 100, so Durability points make them longer.
	{
		const UWWOHealthComponent* H = Pl->Health;
		const FVector2D Box(620, 30);
		const FVector2D Origin(Center.X - Box.X * 0.5f, 78.f);
		const float Unit = Box.X / 150.f;
		if (H->MaxShield() > 0.f)
		{
			const float Full = FMath::Min(H->MaxShield() * Unit, Box.X);
			const FVector2D At(Origin.X + (Box.X - Full) * 0.5f, Origin.Y);
			P.Box(At, FVector2D(Full, 10), FLinearColor(0, 0, 0, 0.45f));
			P.Box(At, FVector2D(Full * H->Shield / H->MaxShield(), 10), ShieldColor);
		}
		const float FullH = FMath::Min(H->MaxHealth() * Unit, Box.X);
		const FVector2D HAt(Origin.X + (Box.X - FullH) * 0.5f, Origin.Y + 14.f);
		const float Frac = H->Health / H->MaxHealth();
		P.Box(HAt, FVector2D(FullH, 16), FLinearColor(0, 0, 0, 0.45f));
		P.Box(HAt, FVector2D(FullH * Frac, 16), Frac < 0.35f ? HealthLowColor : HealthColor);
		P.Outline(HAt, FVector2D(FullH, 16), FLinearColor(1, 1, 1, 0.5f), 1.f);

		// What's on you right now: a row of chips under the vitals, each with its name, the seconds
		// left where it has a timer, and a bar that drains with it.
		struct FChip { FString Label; FLinearColor Color; float Left; float Of; };
		TArray<FChip> Chips;
		if (!H->bDead)
		{
			if (Pl->SlowLeft > 0.f) Chips.Add({ FString::Printf(TEXT("SLOWED %d%%"), FMath::RoundToInt(Pl->SlowAmount * 100.f)), FLinearColor(0.6f, 0.85f, 1.f), Pl->SlowLeft, 0.f });
			if (const UWWODamageOverTime* Dot = Pl->FindComponentByClass<UWWODamageOverTime>()) Chips.Add({ TEXT("POISONED"), UWWODamageOverTime::Color(), Dot->TimeLeft, 0.f });
			if (AWWOSporeFog::Contains(Pl->GetWorld(), Pl->GetActorLocation())) Chips.Add({ Pl->IsInvisible() ? TEXT("VEILED") : TEXT("IN FOG"), FLinearColor(0.8f, 0.65f, 1.f), Pl->IsInvisible() ? Pl->VeilLeft : 0.f, 0.f });
			if (Pl->EntrenchLeft > 0.f) Chips.Add({ TEXT("ENTRENCHED"), FLinearColor(0.85f, 0.65f, 0.4f), Pl->EntrenchLeft, 0.f });
			if (Pl->Abilities->GuardTimeLeft() > 0.f) Chips.Add({ TEXT("GUARD"), FLinearColor(0.75f, 0.9f, 0.6f), Pl->Abilities->GuardTimeLeft(), 0.f });
		}
		// A chip's bar drains against the longest time it has shown (Of 0 = not known up front).
		TMap<FString, float> Seen;
		const FVector2D ChipSize(128.f, 26.f);
		const float Gap = 8.f;
		float X = Center.X - (Chips.Num() * ChipSize.X + FMath::Max(Chips.Num() - 1, 0) * Gap) * 0.5f;
		for (const FChip& Chip : Chips)
		{
			const FString Key = Chip.Label.Left(6);
			const float Of = Chip.Of > 0.f ? Chip.Of : FMath::Max(ChipPeak.FindRef(Key), Chip.Left);
			Seen.Add(Key, Of);
			const FVector2D At(X, Origin.Y + 40.f);
			P.Box(At, ChipSize, FLinearColor(0, 0, 0, 0.55f));
			P.Box(At, FVector2D(4.f, ChipSize.Y), Chip.Color);
			if (Chip.Left > 0.f && Of > 0.f) P.Box(At + FVector2D(4.f, ChipSize.Y - 3.f), FVector2D((ChipSize.X - 4.f) * FMath::Clamp(Chip.Left / Of, 0.f, 1.f), 3.f), Chip.Color);
			const FString Text = Chip.Left > 0.f ? FString::Printf(TEXT("%s  %.1f"), *Chip.Label, Chip.Left) : Chip.Label;
			P.Text(Text, At + FVector2D(ChipSize.X * 0.5f + 2.f, 3.f), 14.f, Chip.Color, 0.5f);
			X += ChipSize.X + Gap;
		}
		ChipPeak = MoveTemp(Seen);

		// Where damage is coming from: a faint red spike, a long four-sided pyramid whose point aims at
		// the source in the world (left and right, ahead and behind, up and down), drawn in
		// perspective as if it floated just below your line of sight, its faces shaded so it reads as
		// a solid. A point aimed away from you is someone ahead; one coming at you is someone behind.
		// It turns with you and fades over a second and a half.
		if (!H->bDead)
		{
			const double MarkTime = Pl->GetWorld()->GetTimeSeconds();
			const FTransform View = Pl->Camera->GetComponentTransform();
			const FVector Anchor(120.f, 0.f, -40.f); // in view space (x ahead, y right, z up)
			constexpr float Focal = 420.f, BaseAt = 14.f, PointAt = 64.f, HalfBase = 7.5f;
			const FVector Light = FVector(-0.45f, -0.35f, 0.82f).GetSafeNormal(); // from above, toward you
			auto Project = [&](const FVector& V) { return Center + FVector2D(V.Y, -V.Z) * (Focal / FMath::Max(V.X, 20.f)); };
			for (const UWWOHealthComponent::FDamageMark& Mark : H->DamageMarks)
			{
				const float Age = (float)(MarkTime - Mark.Time);
				if (Age > 1.5f) continue;
				const FVector Dir = View.InverseTransformVectorNoScale(Mark.From - View.GetLocation()).GetSafeNormal();
				if (Dir.IsNearlyZero()) continue;
				const float Fade = Age < 0.9f ? 1.f : 1.f - (Age - 0.9f) / 0.6f;
				FVector Side = FVector::CrossProduct(FVector::UpVector, Dir).GetSafeNormal();
				if (Side.IsNearlyZero()) Side = FVector::RightVector; // straight up or down
				const FVector Over = FVector::CrossProduct(Dir, Side);
				const FVector Base = Anchor + Dir * BaseAt, Point = Anchor + Dir * PointAt;
				const FVector Corners[4] = { Base + (Side + Over) * HalfBase, Base + (Over - Side) * HalfBase, Base - (Side + Over) * HalfBase, Base + (Side - Over) * HalfBase };
				// Only the faces turned toward the eye (at the origin) are drawn, so none show through
				// another and the opacity stays even.
				auto Face = [&](const FVector& A, const FVector& B, const FVector& C, const FVector& Inside)
				{
					FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
					const FVector Middle = (A + B + C) / 3.f;
					if (FVector::DotProduct(Normal, Middle - Inside) < 0.f) Normal = -Normal; // outward
					if (FVector::DotProduct(Normal, Middle) >= 0.f) return;                    // turned away
					const float Lit = FMath::Max(FVector::DotProduct(Normal, Light), 0.f);
					P.Tri(Project(A), Project(B), Project(C), FLinearColor(1.f, 0.12f + 0.2f * Lit, 0.08f + 0.14f * Lit, (0.24f + 0.2f * Lit) * Fade));
				};
				const FVector Inside = Anchor + Dir * (BaseAt + (PointAt - BaseAt) * 0.25f);
				for (int32 i = 0; i < 4; ++i) Face(Corners[i], Corners[(i + 1) % 4], Point, Inside);
				Face(Corners[0], Corners[1], Corners[2], Inside); // the base, seen when it points away from you
				Face(Corners[0], Corners[2], Corners[3], Inside);
			}
		}
		// Blinded (PvP, a Blinding Grenade): white, clearing over the last part of the blind time.
		if (Pl->BlindLeft > 0.f) P.Box(FVector2D::ZeroVector, Size, FLinearColor(1.f, 1.f, 1.f, 0.97f * FMath::Clamp(Pl->BlindLeft / FMath::Max(Pl->BlindTotal * 0.6f, 0.1f), 0.f, 1.f)));
		// Damage flash and death screen.
		P.Box(FVector2D::ZeroVector, Size, FLinearColor(0.8f, 0.05f, 0.02f, DamageFlash));
		auto ReviveBar = [&](float Frac, float Y)
		{
			const FVector2D BarSize(320.f, 12.f), BarAt(Center.X - BarSize.X * 0.5f, Y);
			P.Box(BarAt, BarSize, FLinearColor(0, 0, 0, 0.5f));
			P.Box(BarAt, FVector2D(BarSize.X * FMath::Clamp(Frac, 0.f, 1.f), BarSize.Y), FLinearColor(0.45f, 1.f, 0.55f));
			P.Outline(BarAt, BarSize, FLinearColor(1, 1, 1, 0.6f), 1.f);
		};
		if (H->bDead && H->IsOnline() && !UWWOWorldSubsystem::IsPvP(Pl))
		{
			// Down: a teammate can bring you back.
			P.Box(FVector2D::ZeroVector, Size, FLinearColor(0.05f, 0.f, 0.f, 0.45f));
			P.Text(TEXT("You're down"), Center - FVector2D(0, 50), 40.f, FLinearColor::White, 0.5f);
			P.Text(H->BeingRevived > 0.f ? TEXT("Being revived") : TEXT("A teammate can revive you"), Center, 30.f, FLinearColor::White, 0.5f);
			if (H->BeingRevived > 0.f) ReviveBar(H->BeingRevived, Center.Y + 40.f);
		}
		else if (H->bDead)
		{
			P.Box(FVector2D::ZeroVector, Size, FLinearColor(0.05f, 0.f, 0.f, 0.6f));
			P.Text(TEXT("You died"), Center - FVector2D(0, 50), 40.f, FLinearColor::White, 0.5f);
			P.Text(FString::Printf(TEXT("Respawning in %.1f"), FMath::Max(H->RespawnLeft, 0.f)), Center, 40.f, FLinearColor::White, 0.5f);
		}
		else if (const AWWOCharacter* Downed = H->FindReviveTarget())
		{
			// Next to a downed teammate: how to revive them, and how far along you are.
			const AWWOPlayerController* Keys = Cast<AWWOPlayerController>(PC);
			P.Text(FString::Printf(TEXT("Hold %s to revive"), Keys ? *Keys->BindHint("revive") : TEXT("Revive")), Center + FVector2D(0, 90), 26.f, FLinearColor::White, 0.5f);
			if (Downed->Health->BeingRevived > 0.f) ReviveBar(Downed->Health->BeingRevived, Center.Y + 125.f);
		}
	}

	// The wave line (top center), from the host's game state (the game mode only exists there).
	if (const AWWOGameState* GS = Pl->GetWorld()->GetGameState<AWWOGameState>())
	{
		if (!GS->StatusLine.IsEmpty()) P.Text(GS->StatusLine, FVector2D(Center.X, 24.f), 28.f, GS->StatusColor, 0.5f, 8.f, 0.85f);
	}

	// Frame rate (top left), big: green at 60+, yellow at 30+, red below. wwo.ShowFPS 0 hides it.
	if (CVarWWOShowFPS.GetValueOnGameThread() && FpsShown > 0.f)
	{
		const FLinearColor C = FpsShown >= 59.f ? FLinearColor(0.45f, 1.f, 0.45f) : FpsShown >= 30.f ? FLinearColor(1.f, 0.85f, 0.3f) : FLinearColor(1.f, 0.35f, 0.3f);
		const float Top = Pl->RadarSize > 1.f ? 24.f + Pl->RadarSize * 2.f + 10.f : 14.f; // under the radar
		P.Text(FString::Printf(TEXT("%d FPS"), FMath::RoundToInt(FpsShown)), FVector2D(24.f, Top), 26.f, C, 0.f, 6.f, 0.85f);
		P.Text(FString::Printf(TEXT("%.1f ms"), 1000.f / FpsShown), FVector2D(24.f, Top + 32.f), 16.f, FLinearColor(1.f, 1.f, 1.f, 0.85f), 0.f, 4.f, 0.85f);
	}
	return Result + 3;
}
