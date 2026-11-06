#include "UI/WWOHUDWidget.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOAimAssistComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOAbilityComponent.h"
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
	FLinearColor Ret = Gun->bTargetUnderReticle ? ReticleOnTarget : ReticleColor;
	Ret.A *= FMath::Clamp(1.f - Aimed * 2.5f, 0.f, 1.f);
	if (Pl->Ult->bActive) Ret.A = 0.f;
	if (Ret.A > 0.f)
	{
		for (const FVector2D Dir : { FVector2D(0, -1), FVector2D(0, 1), FVector2D(-1, 0), FVector2D(1, 0) })
		{
			P.Line(AimAt + Dir * 7.f, AimAt + Dir * 16.f, Ret, 2.f);
		}
		P.Dot(AimAt, 1.5f, Ret);
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
	// Health bar over the target under the reticle (and the Tank's shield above it).
	if (const IWWOTargetable* Target = Cast<IWWOTargetable>(Gun->ReticleTarget.Get()))
	{
		FVector2D Top;
		if (ToScreen(Target->FeetLocation() + FVector(0, 0, Target->GetBodyHeight()), Top))
		{
			const FVector2D Bar(110, 10);
			FVector2D Pos = Top - FVector2D(Bar.X * 0.5f, Bar.Y);
			const float Fraction = FMath::Clamp(Target->GetHealth() / FMath::Max(Target->GetMaxHealth(), 1.f), 0.f, 1.f);
			P.Box(Pos - FVector2D(2, 2), Bar + FVector2D(4, 4), FLinearColor(0, 0, 0, 0.6f));
			P.Box(Pos, FVector2D(Bar.X * Fraction, Bar.Y), FLinearColor(0.9f, 0.2f, 0.15f));
			FString Label = FString::FromInt(FMath::CeilToInt(Target->GetHealth()));
			if (Target->GetMaxShield() > 0.f)
			{
				const FVector2D ShieldPos = Pos - FVector2D(0, Bar.Y + 4);
				P.Box(ShieldPos - FVector2D(2, 2), Bar + FVector2D(4, 4), FLinearColor(0, 0, 0, 0.6f));
				P.Box(ShieldPos, FVector2D(Bar.X * FMath::Clamp(Target->GetShield() / Target->GetMaxShield(), 0.f, 1.f), Bar.Y), ShieldColor);
				if (Target->GetShield() > 0.f) Label = FString::Printf(TEXT("%d + %d"), FMath::CeilToInt(Target->GetShield()), FMath::CeilToInt(Target->GetHealth()));
				Pos = ShieldPos;
			}
			P.Text(Label, Pos + FVector2D(Bar.X * 0.5f, -21), 16.f, FLinearColor::White, 0.5f, 0.f); // Godot: draw_string, baseline 6 px up, no outline
		}
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

	// Vitals (bottom center): shield bar over health bar. Each bar's width is its max against
	// the default 100, so Durability points make them longer.
	{
		const UWWOHealthComponent* H = Pl->Health;
		const FVector2D Box(360, 30);
		const FVector2D Origin(Center.X - Box.X * 0.5f, Size.Y - Box.Y - 50.f);
		const float Unit = Box.X / 200.f;
		if (H->MaxShield() > 0.f)
		{
			const float Full = FMath::Min(H->MaxShield() * Unit, Box.X);
			const FVector2D At(Origin.X + (Box.X - Full) * 0.5f, Origin.Y);
			P.Box(At, FVector2D(Full, 8), FLinearColor(0, 0, 0, 0.45f));
			P.Box(At, FVector2D(Full * H->Shield / H->MaxShield(), 8), ShieldColor);
		}
		const float FullH = FMath::Min(H->MaxHealth() * Unit, Box.X);
		const FVector2D HAt(Origin.X + (Box.X - FullH) * 0.5f, Origin.Y + 12.f);
		const float Frac = H->Health / H->MaxHealth();
		P.Box(HAt, FVector2D(FullH, 14), FLinearColor(0, 0, 0, 0.45f));
		P.Box(HAt, FVector2D(FullH * Frac, 14), Frac < 0.35f ? HealthLowColor : HealthColor);
		P.Outline(HAt, FVector2D(FullH, 14), FLinearColor(1, 1, 1, 0.5f), 1.f);
		// Status tags above the vitals, each on its own line (Godot's labels have fixed spots:
		// VEILED 28 px and ENTRENCHED 56 px above SLOWED, shown or not).
		const float TagY = Origin.Y - 50.f;
		auto Tag = [&](bool bOn, int32 Line, const TCHAR* Text, const FLinearColor& Color)
		{
			if (bOn) P.Text(Text, FVector2D(Center.X, TagY - 28.f * Line), 20.f, Color, 0.5f);
		};
		Tag(Pl->SlowLeft > 0.f, 0, TEXT("SLOWED"), FLinearColor(0.6f, 0.85f, 1.f));
		Tag(Pl->IsInvisible(), 1, TEXT("VEILED"), FLinearColor(0.8f, 0.65f, 1.f));
		Tag(Pl->EntrenchLeft > 0.f, 2, TEXT("ENTRENCHED"), FLinearColor(0.85f, 0.65f, 0.4f));
		// Damage flash and death screen.
		P.Box(FVector2D::ZeroVector, Size, FLinearColor(0.8f, 0.05f, 0.02f, DamageFlash));
		if (H->bDead)
		{
			P.Box(FVector2D::ZeroVector, Size, FLinearColor(0.05f, 0.f, 0.f, 0.6f));
			P.Text(TEXT("You died"), Center - FVector2D(0, 50), 40.f, FLinearColor::White, 0.5f);
			P.Text(FString::Printf(TEXT("Respawning in %.1f"), FMath::Max(H->RespawnLeft, 0.f)), Center, 40.f, FLinearColor::White, 0.5f);
		}
	}

	// The wave line (top center).
	if (const AWWOGameMode* Mode = Pl->GetWorld()->GetAuthGameMode<AWWOGameMode>())
	{
		FLinearColor C;
		const FString Line = Mode->StatusText(C);
		P.Text(Line, FVector2D(Center.X, 24.f), 28.f, C, 0.5f, 8.f, 0.85f);
	}

	// Frame rate (top left), big: green at 60+, yellow at 30+, red below. wwo.ShowFPS 0 hides it.
	if (CVarWWOShowFPS.GetValueOnGameThread() && FpsShown > 0.f)
	{
		const FLinearColor C = FpsShown >= 59.f ? FLinearColor(0.45f, 1.f, 0.45f) : FpsShown >= 30.f ? FLinearColor(1.f, 0.85f, 0.3f) : FLinearColor(1.f, 0.35f, 0.3f);
		P.Text(FString::Printf(TEXT("%d FPS"), FMath::RoundToInt(FpsShown)), FVector2D(20.f, 14.f), 56.f, C, 0.f, 10.f, 0.85f);
		P.Text(FString::Printf(TEXT("%.1f ms"), 1000.f / FpsShown), FVector2D(22.f, 84.f), 26.f, FLinearColor(1.f, 1.f, 1.f, 0.85f), 0.f, 6.f, 0.85f);
	}
	return Result + 3;
}
