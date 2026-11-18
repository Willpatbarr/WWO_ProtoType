#include "UI/WWOHUDWidget.h"
#include "UI/WWOPainter.h"
#include "Player/WWOProf.h"
#include "Game/WWOGameState.h"
#include "Game/WWOPlayerState.h"
#include "GameFramework/PlayerState.h"
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
#include "World/WWOBreachLever.h"
#include "EngineUtils.h"
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
	// One red for the radar's lit sections and enemies' health bars. Triangles (FWWOPainter::Tri) come
	// out darker on screen than boxes given the same colour, so each has the value that lands on the
	// same shade (about 198, 57, 52 of 255).
	const FLinearColor FoeRed(0.89f, 0.508f, 0.487f);      // for Tri
	const FLinearColor FoeRedBox(0.775f, 0.225f, 0.205f);    // for Box
	const FLinearColor ReticleOnFriend(0.25f, 1.f, 0.35f, 0.95f); // over someone you can't hurt (a teammate)
	const FLinearColor CritColor(1.f, 0.85f, 0.2f);
	const FLinearColor KillColor(1.f, 0.25f, 0.2f);
	const FLinearColor ShieldColor(0.55f, 0.8f, 1.f);
	const FLinearColor HealthColor(0.95f, 0.95f, 0.95f);
	const FLinearColor HealthLowColor(1.f, 0.3f, 0.25f);
	constexpr float LowAmmo = 0.25f;

	void ChargeBox(const FWWOPainter& P, FVector2D Pos, FVector2D BoxSize, const FLinearColor& Color, float Charge)
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

// What every element painter shares, worked out once per paint by NativePaint.
struct FWWOHUDFrame
{
	const FWWOPainter& P;
	AWWOCharacter* Pl = nullptr;
	APlayerController* PC = nullptr;
	UWWOWeaponComponent* Gun = nullptr;
	const UWWOWeaponData* W = nullptr;
	const UWWOLoadoutSubsystem* Loadout = nullptr;
	UWWOWorldSubsystem* WS = nullptr;
	float S = 1.f;           // pixels per layout pixel (P.S)
	FVector2D Size;          // the layout's size (P.Size)
	FVector2D Center;        // its middle
	float Aimed = 0.f;       // the gun's AdsAmount
	float DropPx = 0.f;      // how far below the middle the reticle sits (none in the ult's third person)
	FVector2D AimAt;         // the reticle: where shots go

	// A world point on the HUD, in layout pixels (false when it can't be projected).
	bool ToScreen(const FVector& World, FVector2D& Out2D) const
	{
		FVector2D Screen;
		if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, World, Screen, true)) return false;
		Out2D = Screen / S;
		return true;
	}
};

int32 UWWOHUDWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling,
	FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const
{
	WWO_PROF("HUDWidget.NativePaint");
	const int32 Result = Super::NativePaint(Args, Geo, Culling, Out, LayerId, Style, bParentEnabled);
	AWWOCharacter* Pl = Player();
	APlayerController* PC = GetOwningPlayer();
	if (!Pl || !PC) return Result;
	const FVector2D Local = Geo.GetLocalSize();
	const float S = WWOHUD::Scale(Local);
	const FWWOPainter P{ Out, Geo, LayerId + 1, S, Local / S };
	const FVector2D Size = P.Size;
	const FVector2D Center = Size * 0.5f;
	UWWOWeaponComponent* Gun = Pl->Weapon;
	const float DropPx = Pl->Ult->bActive ? 0.f : Size.Y * Pl->ReticleDrop; // the low reticle (none in the ult's third person)
	const FWWOHUDFrame F{ P, Pl, PC, Gun, Gun->GetWeaponData(), UWWOLoadoutSubsystem::Get(Pl), UWWOWorldSubsystem::Get(Pl), S, Size, Center,
		Gun->AdsAmount, DropPx, Center + FVector2D(0.f, DropPx) };

	// In z-order: each draws over the ones before it.
	PaintScreenEffects(F);
	PaintWorldLabels(F);
	PaintReticle(F);
	PaintAssistDebug(F);
	PaintHealthBars(F);
	PaintMateChevrons(F);
	PaintKillPopups(F);
	PaintKillFeed(F);
	PaintRadar(F);
	PaintAmmo(F);
	PaintAbilities(F);
	PaintVitals(F);
	PaintDamageSpikes(F);
	PaintOverlays(F);
	PaintWaveLine(F);
	PaintBreachEnergy(F);
	PaintFps(F);
	return Result + 3;
}

// --- The element painters --------------------------------------------------------------------

void UWWOHUDWidget::PaintScreenEffects(const FWWOHUDFrame& F) const
{
	const FWWOPainter& P = F.P;
	AWWOCharacter* Pl = F.Pl;
	const UWWOWeaponData* W = F.W;
	const FVector2D Size = F.Size;
	const float Aimed = F.Aimed;
	const float DropPx = F.DropPx;
	// Low health: red creeps in from the edges, further and stronger the less you have left
	// (under everything else on the HUD). Faint with the shield half gone, heavy near death.
	if (HurtShown > 0.01f && Pl->HealthVignette > 0.f)
	{
		const float Amount = FMath::Pow(HurtShown, 1.6f);
		P.Vignette(FLinearColor(0.75f, 0.03f, 0.02f, FMath::Clamp(0.9f * Amount * Pl->HealthVignette, 0.f, 0.95f)), FMath::Lerp(0.95f, 0.3f, Amount));
	}
	// Static Charge charged and waiting for a punch: a faint blue at the edges of the screen, and
	// thin lightning that keeps arcing along them.
	if (StaticShown > 0.01f)
	{
		const double Real = FPlatformTime::Seconds();
		const FLinearColor Blue(0.3f, 0.62f, 1.f);
		P.Vignette(WWO::WithAlpha(Blue, (0.3f + 0.04f * FMath::Sin(Real * 5.0)) * StaticShown), 0.62f);
		// The arcs: a new set every 0.09 s, each slot lit a little under half the time. An arc runs
		// along one edge, close to it, jagged.
		constexpr int32 Slots = 7;
		const int64 Step = int64(Real / 0.09);
		for (int32 Slot = 0; Slot < Slots; ++Slot)
		{
			FRandomStream Rand(int32((Step * 31 + Slot * 7919) & 0x7fffffff));
			if (Rand.FRand() > 0.42f) continue;
			const int32 Side = Rand.RandRange(0, 3); // top, bottom, left, right
			const bool bFlat = Side < 2;
			const float Long = bFlat ? Size.X : Size.Y;
			const float Length = Rand.FRandRange(110.f, 300.f);
			const float From = Rand.FRandRange(0.f, Long - Length);
			const int32 Points = Rand.RandRange(7, 12);
			const float Bright = Rand.FRandRange(0.35f, 0.7f) * StaticShown;
			FVector2D Last = FVector2D::ZeroVector;
			for (int32 i = 0; i <= Points; ++i)
			{
				const float Along = From + Length * i / Points + (i > 0 && i < Points ? Rand.FRandRange(-8.f, 8.f) : 0.f);
				// How far in from the edge: its ends touch the edge, its middle wanders in.
				const float In = (i == 0 || i == Points) ? 2.f : Rand.FRandRange(4.f, 30.f);
				FVector2D At;
				if (Side == 0) At = FVector2D(Along, In);
				else if (Side == 1) At = FVector2D(Along, Size.Y - In);
				else if (Side == 2) At = FVector2D(In, Along);
				else At = FVector2D(Size.X - In, Along);
				if (i > 0)
				{
					P.Line(Last, At, WWO::WithAlpha(Blue, 0.16f * Bright), 6.f);                           // its glow
					P.Line(Last, At, FLinearColor(0.8f, 0.92f, 1.f, Bright), 1.6f);                      // the bolt
				}
				Last = At;
			}
		}
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
}

void UWWOHUDWidget::PaintWorldLabels(const FWWOHUDFrame& F) const
{
	const FWWOPainter& P = F.P;
	AWWOCharacter* Pl = F.Pl;
	UWWOWorldSubsystem* WS = F.WS;
	const FVector2D Size = F.Size;
	// Floating damage numbers and words: Godot's fixed-size Label3D (font 36, crits 48, pixel
	// size 0.0006, outline 8). Fixed size keeps it at depth 1 m, so it's the same size at any
	// distance but still scales with the camera FOV (bigger when zoomed in). Centered on its point.
	{
		const float PxPerFontPx = 0.0006f * Size.X / (2.f * FMath::Tan(FMath::DegreesToRadians(Pl->Camera->FieldOfView) * 0.5f));
		for (const FWWOFloatingText& T : WS->FloatingTexts())
		{
			FVector2D At;
			const FVector World = T.Start + FVector(0, T.Jitter, 60.f * FMath::Min(T.Age / FWWOFloatingText::RiseTime, 1.f));
			if (!F.ToScreen(World, At)) continue;
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
		if (!F.ToScreen(Ally->GetActorLocation() + FVector(0, 0, 125.f), At)) continue;
		const FLinearColor C = Ally->IsDown() ? FLinearColor(1, 0.4f, 0.35f) : (Ally->Shield > 0.f ? ShieldColor : FLinearColor(1, 0.9f, 0.9f));
		P.Text(Ally->StatusText(), At, 15.f, C, 0.5f);
	}
}

void UWWOHUDWidget::PaintReticle(const FWWOHUDFrame& F) const
{
	const FWWOPainter& P = F.P;
	AWWOCharacter* Pl = F.Pl;
	UWWOWeaponComponent* Gun = F.Gun;
	const UWWOWeaponData* W = F.W;
	const FVector2D Size = F.Size;
	const FVector2D Center = F.Center;
	const float Aimed = F.Aimed;
	const FVector2D AimAt = F.AimAt;
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
}

void UWWOHUDWidget::PaintAssistDebug(const FWWOHUDFrame& F) const
{
	const FWWOPainter& P = F.P;
	AWWOCharacter* Pl = F.Pl;
	UWWOWeaponComponent* Gun = F.Gun;
	const UWWOWeaponData* W = F.W;
	const FVector2D Size = F.Size;
	const FVector2D AimAt = F.AimAt;
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
		if (Assist->FrictionPart.IsValid() && F.ToScreen(Assist->FrictionPart.Position, At)) P.Circle(At, 6.f, FLinearColor(1.f, 0.85f, 0.2f, 0.9f), 2.f, 16);
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
}

void UWWOHUDWidget::PaintHealthBars(const FWWOHUDFrame& F) const
{
	const FWWOPainter& P = F.P;
	AWWOCharacter* Pl = F.Pl;
	UWWOWeaponComponent* Gun = F.Gun;
	UWWOWorldSubsystem* WS = F.WS;
	// Health bars: the target under the reticle, and every other visible enemy whose middle is
	// within HealthBarRadius of the reticle on screen. A thin shield bar sits right on top of the
	// health bar (no gap, no border), and the pair floats a little clear of the head.
	auto HealthBar = [&](const IWWOTargetable* Target, const FLinearColor& BarHealthColor = FoeRedBox, const FLinearColor& BarShieldColor = FLinearColor(0.9f, 0.9f, 0.92f), const FString& Name = FString()) // an enemy's shield: a very light grey
	{
		FVector2D Top;
		if (!F.ToScreen(Target->FeetLocation() + FVector(0, 0, Target->GetBodyHeight()), Top)) return;
		constexpr float Wide = 126.f, HealthTall = 7.f, ShieldTall = 3.f, Lift = 16.f;
		const bool bShield = Target->GetMaxShield() > 0.f;
		const FVector2D Pos = Top - FVector2D(Wide * 0.5f, HealthTall + Lift); // the health bar's top left
		const FVector2D TopLeft = Pos - FVector2D(0.f, bShield ? ShieldTall : 0.f);
		const float Fraction = FMath::Clamp(Target->GetHealth() / FMath::Max(Target->GetMaxHealth(), 1.f), 0.f, 1.f);
		// No border: just a faint dark track the size of the bars, so the part that is gone still reads.
		P.Box(TopLeft, FVector2D(Wide, HealthTall + (bShield ? ShieldTall : 0.f)), FLinearColor(0, 0, 0, 0.35f));
		P.Box(Pos, FVector2D(Wide * Fraction, HealthTall), BarHealthColor);
		if (bShield) P.Box(TopLeft, FVector2D(Wide * FMath::Clamp(Target->GetShield() / Target->GetMaxShield(), 0.f, 1.f), ShieldTall), BarShieldColor);
		// (No health and shield numbers over the bars any more.) A player's gamer tag, or a bot's "bot3", sits on them.
		if (!Name.IsEmpty()) P.Text(Name, TopLeft + FVector2D(Wide * 0.5f, -21), 15.f, FLinearColor(1.f, 1.f, 1.f, 0.92f), 0.5f, 3.f);
	};
	// The name on a player's bars: their player state's (the gamer tag they joined with; bots are bot1, bot2...).
	auto TagOf = [](const AActor* Actor) -> FString
	{
		const APawn* Pawn = Cast<APawn>(Actor);
		const APlayerState* PS = Pawn ? Pawn->GetPlayerState() : nullptr;
		return PS ? PS->GetPlayerName() : FString();
	};
	// Bars over the enemies you have marked, and over your teammates in sight: who that is, is
	// worked out in game code each frame (UWWOWeaponComponent::UpdateMarks), not here. Teammates'
	// bars are near-white (light grey health, a see-through white shield) so the only red bars are
	// the other team's.
	if (!Pl->Ult->bActive)
	{
		for (AActor* Actor : WS->LivingTargets(Pl))
		{
			const IWWOTargetable* Target = Cast<IWWOTargetable>(Actor);
			if (Target && !Target->IsDead() && Gun->Marked.Contains(Actor)) HealthBar(Target, FoeRedBox, FLinearColor(0.9f, 0.9f, 0.92f), TagOf(Actor));
		}
		if (UWWOWorldSubsystem::IsPvP(Pl))
		{
			for (AWWOCharacter* Mate : WS->GetPlayers())
			{
				if (Mate == Pl || Mate->Health->bDead || UWWOWorldSubsystem::CanHurt(Pl, Mate)) continue;
				if (Gun->MatesInSight.Contains(Mate)) HealthBar(Mate, FLinearColor(0.82f, 0.83f, 0.85f), FLinearColor(1.f, 1.f, 1.f, 0.6f), TagOf(Mate));
			}
		}
	}
}

void UWWOHUDWidget::PaintMateChevrons(const FWWOHUDFrame& F) const
{
	const FWWOPainter& P = F.P;
	AWWOCharacter* Pl = F.Pl;
	UWWOWeaponComponent* Gun = F.Gun;
	UWWOWorldSubsystem* WS = F.WS;
	// PvP: an open chevron over each teammate's head, kept (fainter) through walls so you know where
	// your side is. The other team has no marker: the radar says roughly where they are.
	if (UWWOWorldSubsystem::IsPvP(Pl))
	{
		const int32 MyTeam = UWWOWorldSubsystem::TeamOf(Pl);
		for (AWWOCharacter* Other : WS->GetPlayers())
		{
			if (Other == Pl || Other->Health->bDead || UWWOWorldSubsystem::TeamOf(Other) != MyTeam || MyTeam < 0) continue;
			FVector2D At;
			if (!F.ToScreen(Other->FeetLocation() + FVector(0, 0, Other->GetBodyHeight()), At)) continue;
			At.Y -= 66.f; // clear of the bars and the name over them
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
}

void UWWOHUDWidget::PaintKillPopups(const FWWOHUDFrame& F) const
{
	const FWWOPainter& P = F.P;
	AWWOCharacter* Pl = F.Pl;
	const FVector2D AimAt = F.AimAt;
	// Your own kills, to the right of the reticle for 2.5 s: "Kill +1", "Precision kill +2" (the
	// number is what it added to your team's score). The newest is level with the reticle, older ones above it.
	if (const AWWOGameState* Mine = Pl->GetWorld()->GetGameState<AWWOGameState>())
	{
		constexpr float Shown = 2.5f, Fade = 0.5f, Px = 22.f, LineTall = 30.f, Right = 150.f;
		const double KillNow = FPlatformTime::Seconds();
		float Y = AimAt.Y;
		for (int32 Index = Mine->KillFeed.Num() - 1; Index >= 0; --Index)
		{
			const AWWOGameState::FKillLine& Line = Mine->KillFeed[Index];
			const float Age = float(KillNow - Line.At);
			if (Age > Shown) break;
			if (!Line.bMine) continue;
			const bool bPrecision = Line.How == AWWOGameState::KillPrecision;
			const float Alpha = FMath::Clamp((Shown - Age) / Fade, 0.f, 1.f);
			P.Text(FString::Printf(TEXT("%s +%d"), bPrecision ? TEXT("Precision kill") : TEXT("Kill"), Line.Points), FVector2D(AimAt.X + Right, Y), Px,
				WWO::WithAlpha(bPrecision ? UWWOWorldSubsystem::CritColor() : FLinearColor::White, Alpha), 0.f, 5.f, 0.75f * Alpha, 0.5f);
			Y -= LineTall;
		}
	}
}

void UWWOHUDWidget::PaintKillFeed(const FWWOHUDFrame& F) const
{
	const FWWOPainter& P = F.P;
	AWWOCharacter* Pl = F.Pl;
	// The kill feed (bottom left, over the ability, grenade and ult boxes): "killer > killee", each
	// name in its team's color, the newest at the bottom, each line for 6 s and fading over its last one.
	if (const AWWOGameState* Feed = Pl->GetWorld()->GetGameState<AWWOGameState>())
	{
		constexpr float Shown = 6.f, Fade = 1.f, Px = 16.f, LineTall = 23.f;
		const double FeedNow = FPlatformTime::Seconds();
		const FSlateFontInfo FeedFont = P.Font(Px, 4.f);
		// Built upward from the newest line, which sits just above the boxes (their top is 126 px off the bottom).
		float Y = P.Size.Y - 126.f - 14.f - LineTall;
		for (int32 Index = Feed->KillFeed.Num() - 1; Index >= 0; --Index)
		{
			const AWWOGameState::FKillLine& Line = Feed->KillFeed[Index];
			const float Age = float(FeedNow - Line.At);
			if (Age > Shown) break; // older ones are older still
			const float Alpha = FMath::Clamp((Shown - Age) / Fade, 0.f, 1.f);
			auto TeamTint = [&](int32 Team) { return WWO::WithAlpha(Team >= 0 ? AWWOPlayerState::TeamColor(Team) : FLinearColor::White, Alpha); };
			float X = 40.f;
			P.Text(Line.Killer, FVector2D(X, Y), Px, TeamTint(Line.KillerTeam), 0.f, 4.f);
			X += P.Measure(Line.Killer, FeedFont).X;
			const FString Arrow = TEXT("  >  ");
			P.Text(Arrow, FVector2D(X, Y), Px, FLinearColor(1.f, 1.f, 1.f, 0.75f * Alpha), 0.f, 4.f);
			X += P.Measure(Arrow, FeedFont).X;
			P.Text(Line.Victim, FVector2D(X, Y), Px, TeamTint(Line.VictimTeam), 0.f, 4.f);
			Y -= LineTall;
		}
	}
}

void UWWOHUDWidget::PaintRadar(const FWWOHUDFrame& F) const
{
	const FWWOPainter& P = F.P;
	AWWOCharacter* Pl = F.Pl;
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
		// Teammates, in your own blue: a dot where they are when in range, else an arrow on the rim
		// pointing out their way.
		TArray<FVector2D> Mates;
		RadarMates(Pl, Mates);
		for (const FVector2D& Mate : Mates)
		{
			const float A = FMath::DegreesToRadians(Mate.X);
			const FVector2D Out2(FMath::Sin(A), -FMath::Cos(A)), Side(-Out2.Y, Out2.X);
			if (Mate.Y <= 1.f)
			{
				const FVector2D At = Mid + Out2 * FMath::Max(Mate.Y * R, Hub * 1.2f); // (clear of your own triangle)
				constexpr int32 Sides = 10;
				const float Dot = FMath::Max(R * 0.055f, 2.5f);
				for (int32 i = 0; i < Sides; ++i)
				{
					const float B0 = UE_TWO_PI * i / Sides, B1 = UE_TWO_PI * (i + 1) / Sides;
					P.Tri(At, At + FVector2D(FMath::Cos(B0), FMath::Sin(B0)) * Dot, At + FVector2D(FMath::Cos(B1), FMath::Sin(B1)) * Dot, You);
				}
			}
			else
			{
				const float Long = R * 0.17f, Wide = R * 0.09f;
				P.Tri(Mid + Out2 * (R + 2.f + Long), Mid + Out2 * (R + 2.f) + Side * Wide, Mid + Out2 * (R + 2.f) - Side * Wide, You);
			}
		}
		// You: a small triangle in the middle, pointing the way you look (it never moves).
		P.Tri(Mid + FVector2D(0.f, -Hub * 0.8f), Mid + FVector2D(Hub * 0.5f, Hub * 0.55f), Mid + FVector2D(-Hub * 0.5f, Hub * 0.55f), You);
	}
}

void UWWOHUDWidget::PaintAmmo(const FWWOHUDFrame& F) const
{
	const FWWOPainter& P = F.P;
	UWWOWeaponComponent* Gun = F.Gun;
	const UWWOWeaponData* W = F.W;
	const FVector2D Size = F.Size;
	const FVector2D Center = F.Center;
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
}

void UWWOHUDWidget::PaintAbilities(const FWWOHUDFrame& F) const
{
	const FWWOPainter& P = F.P;
	AWWOCharacter* Pl = F.Pl;
	const UWWOLoadoutSubsystem* Loadout = F.Loadout;
	const FVector2D Size = F.Size;
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
}

void UWWOHUDWidget::PaintVitals(const FWWOHUDFrame& F) const
{
	const FWWOPainter& P = F.P;
	AWWOCharacter* Pl = F.Pl;
	const FVector2D Center = F.Center;
	// Vitals (top center, under the match line): shield bar over health bar. Each bar's width is
	// its max against the default 100, so Durability points make them longer.
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

	// What's on you right now: a row of chips under the vitals (NativeTick lists them), each with
	// its name, the seconds left where it has a timer, and a bar that drains with it.
	// A chip's bar drains against the longest time it has shown (Of 0 = not known up front; ChipPeak
	// keeps the longest, NativeTick updates it).
	const FVector2D ChipSize(128.f, 26.f);
	const float Gap = 8.f;
	float X = Center.X - (Chips.Num() * ChipSize.X + FMath::Max(Chips.Num() - 1, 0) * Gap) * 0.5f;
	for (const FWWOStatusChip& Chip : Chips)
	{
		const FString Key = Chip.Label.Left(6);
		const float Of = Chip.Of > 0.f ? Chip.Of : ChipPeak.FindRef(Key);
		const FVector2D At(X, Origin.Y + 40.f);
		P.Box(At, ChipSize, FLinearColor(0, 0, 0, 0.55f));
		P.Box(At, FVector2D(4.f, ChipSize.Y), Chip.Color);
		if (Chip.Left > 0.f && Of > 0.f) P.Box(At + FVector2D(4.f, ChipSize.Y - 3.f), FVector2D((ChipSize.X - 4.f) * FMath::Clamp(Chip.Left / Of, 0.f, 1.f), 3.f), Chip.Color);
		const FString Text = Chip.Left > 0.f ? FString::Printf(TEXT("%s  %.1f"), *Chip.Label, Chip.Left) : Chip.Label;
		P.Text(Text, At + FVector2D(ChipSize.X * 0.5f + 2.f, 3.f), 14.f, Chip.Color, 0.5f);
		X += ChipSize.X + Gap;
	}
}

void UWWOHUDWidget::PaintDamageSpikes(const FWWOHUDFrame& F) const
{
	const FWWOPainter& P = F.P;
	AWWOCharacter* Pl = F.Pl;
	const FVector2D Center = F.Center;
	const UWWOHealthComponent* H = Pl->Health;
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
}

void UWWOHUDWidget::PaintOverlays(const FWWOHUDFrame& F) const
{
	const FWWOPainter& P = F.P;
	AWWOCharacter* Pl = F.Pl;
	APlayerController* PC = F.PC;
	const FVector2D Size = F.Size;
	const FVector2D Center = F.Center;
	const UWWOHealthComponent* H = Pl->Health;
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

void UWWOHUDWidget::PaintWaveLine(const FWWOHUDFrame& F) const
{
	const FWWOPainter& P = F.P;
	AWWOCharacter* Pl = F.Pl;
	const FVector2D Center = F.Center;
	// The wave line (top center), from the host's game state (the game mode only exists there).
	if (const AWWOGameState* GS = Pl->GetWorld()->GetGameState<AWWOGameState>())
	{
		if (!GS->StatusLine.IsEmpty()) P.Text(GS->StatusLine, FVector2D(Center.X, 24.f), 28.f, GS->StatusColor, 0.5f, 8.f, 0.85f);
	}
}

void UWWOHUDWidget::PaintBreachEnergy(const FWWOHUDFrame& F) const
{
	// Two bars under the match line, your team's first (left): each team's pool, a tick where the hub
	// opens (the threshold), the number. A bar is full at twice the threshold.
	const AWWOGameState* GS = F.Pl->GetWorld()->GetGameState<AWWOGameState>();
	if (!GS || GS->BreachEnergy.Num() < 2) return;
	const FWWOPainter& P = F.P;
	const int32 Mine = FMath::Clamp(UWWOWorldSubsystem::TeamOf(F.Pl), 0, 1);
	const float Threshold = FMath::Max(GS->BreachThreshold, 1.f);
	const FVector2D BarSize(220.f, 12.f);
	const float Gap = 40.f, Top = 128.f;
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		const int32 Team = Slot == 0 ? Mine : 1 - Mine;
		const float Energy = GS->BreachEnergy[Team];
		const FLinearColor Color = Team == 1 ? FLinearColor(1.f, 0.68f, 0.2f) : FLinearColor(0.35f, 0.62f, 1.f); // AWWOBreachPortal::TeamColor, sRGB
		const FVector2D Pos(F.Center.X + (Slot == 0 ? -Gap * 0.5f - BarSize.X : Gap * 0.5f), Top);
		P.Box(Pos, BarSize, FLinearColor(0.1f, 0.1f, 0.12f, 0.7f));
		const float Fill = FMath::Clamp(Energy / (Threshold * 2.f), 0.f, 1.f);
		P.Box(Pos, FVector2D(BarSize.X * Fill, BarSize.Y), Energy >= Threshold ? Color : WWO::Darkened(Color, 0.45f));
		const float TickX = Pos.X + BarSize.X * 0.5f;
		P.Box(FVector2D(TickX - 1.f, Pos.Y - 3.f), FVector2D(2.f, BarSize.Y + 6.f), FLinearColor(1, 1, 1, 0.9f));
		P.Outline(Pos, BarSize, FLinearColor(1, 1, 1, 0.35f), 1.f);
		// The next breach's pool, grey and thin under it, while a door opened mid-breach has frozen the pool.
		const bool bFrozen = GS->BreachFrozen.IsValidIndex(Team) && GS->BreachFrozen[Team];
		const float Next = GS->BreachNext.IsValidIndex(Team) ? GS->BreachNext[Team] : 0.f;
		if (bFrozen || Next > 0.f)
		{
			const FVector2D NextPos(Pos.X, Pos.Y + BarSize.Y + 2.f);
			P.Box(NextPos, FVector2D(BarSize.X, 5.f), FLinearColor(0.1f, 0.1f, 0.12f, 0.6f));
			P.Box(NextPos, FVector2D(BarSize.X * FMath::Clamp(Next / (Threshold * 2.f), 0.f, 1.f), 5.f), FLinearColor(0.62f, 0.64f, 0.68f, 0.9f));
		}
		const FString Label = FString::Printf(TEXT("%s%s  %.0f"), Slot == 0 ? TEXT("YOUR BREACH") : TEXT("THEIRS"),
			Energy >= Threshold ? TEXT(" READY") : TEXT(""), Energy);
		P.Text(Label, FVector2D(Pos.X + (Slot == 0 ? BarSize.X : 0.f), Pos.Y + BarSize.Y + 9.f), 16.f, Color, Slot == 0 ? 1.f : 0.f, 5.f, 0.8f);
		// The team's boss, once Door3 is open: its health, and its shield or how long the window has left.
		const float BossLeft = GS->BossHealth.IsValidIndex(Team) ? GS->BossHealth[Team] : -1.f;
		if (BossLeft >= 0.f)
		{
			const float Window = GS->BossWindow.IsValidIndex(Team) ? GS->BossWindow[Team] : 0.f;
			const FVector2D BossPos(Pos.X, Pos.Y + BarSize.Y + 32.f);
			P.Box(BossPos, FVector2D(BarSize.X, 8.f), FLinearColor(0.1f, 0.1f, 0.12f, 0.7f));
			P.Box(BossPos, FVector2D(BarSize.X * FMath::Clamp(BossLeft, 0.f, 1.f), 8.f), Window > 0.f ? FLinearColor(1.f, 0.3f, 0.25f) : FLinearColor(0.55f, 0.3f, 0.3f));
			const FString BossLine = Window > 0.f ? FString::Printf(TEXT("BOSS %.0f%%  WINDOW %.0fs"), BossLeft * 100.f, FMath::CeilToFloat(Window))
				: FString::Printf(TEXT("BOSS %.0f%%  SHIELDED"), BossLeft * 100.f);
			P.Text(BossLine, FVector2D(BossPos.X + (Slot == 0 ? BarSize.X : 0.f), BossPos.Y + 10.f), 15.f, FLinearColor(1.f, 0.6f, 0.55f), Slot == 0 ? 1.f : 0.f, 5.f, 0.8f);
		}
	}
	// At one of your team's levers: what holding the key does, or why it won't, and how far a pull has got.
	const AWWOBreachLever* Near = nullptr;
	for (TActorIterator<AWWOBreachLever> It(F.Pl->GetWorld()); It; ++It)
	{
		if (It->Team == Mine && It->InReach(F.Pl->GetActorLocation())) Near = *It;
	}
	if (!Near) return;
	const int32 Opened = GS->DoorsOpen.IsValidIndex(Mine) ? GS->DoorsOpen[Mine] : 0;
	const AWWOPlayerController* Keys = Cast<AWWOPlayerController>(F.PC);
	const FString Key = Keys ? Keys->BindHint("revive") : TEXT("Revive");
	FString Line;
	if (Near->bPulled) Line = FString::Printf(TEXT("Door %d is open"), (int32)Near->Door);
	else if (Near->bLive) Line = FString::Printf(TEXT("Hold %s to open Door %d"), *Key, (int32)Near->Door);
	else if (Near->Door > Opened + 1) Line = FString::Printf(TEXT("Door %d: open Door %d first"), (int32)Near->Door, Opened + 1);
	else if (GS->BreachFrozen.IsValidIndex(Mine) && GS->BreachFrozen[Mine]) Line = TEXT("One door per breach: breach again for the next");
	else Line = FString::Printf(TEXT("Door %d: breach from Room %d to unlock this lever"), (int32)Near->Door, (int32)Near->Door);
	P.Text(Line, F.Center + FVector2D(0, 90), 26.f, FLinearColor::White, 0.5f);
	if (Near->Progress > 0.f)
	{
		const FVector2D BarPos(F.Center.X - 110.f, F.Center.Y + 125.f);
		P.Box(BarPos, FVector2D(220.f, 10.f), FLinearColor(0.1f, 0.1f, 0.12f, 0.7f));
		P.Box(BarPos, FVector2D(220.f * FMath::Clamp(Near->Progress, 0.f, 1.f), 10.f), FLinearColor::White);
	}
}

void UWWOHUDWidget::PaintFps(const FWWOHUDFrame& F) const
{
	const FWWOPainter& P = F.P;
	AWWOCharacter* Pl = F.Pl;
	// Frame rate (top left), big: green at 60+, yellow at 30+, red below. wwo.ShowFPS 0 hides it.
	if (CVarWWOShowFPS.GetValueOnGameThread() && FpsShown > 0.f)
	{
		const FLinearColor C = FpsShown >= 59.f ? FLinearColor(0.45f, 1.f, 0.45f) : FpsShown >= 30.f ? FLinearColor(1.f, 0.85f, 0.3f) : FLinearColor(1.f, 0.35f, 0.3f);
		const float Top = Pl->RadarSize > 1.f ? 24.f + Pl->RadarSize * 2.f + 10.f : 14.f; // under the radar
		P.Text(FString::Printf(TEXT("%d FPS"), FMath::RoundToInt(FpsShown)), FVector2D(24.f, Top), 26.f, C, 0.f, 6.f, 0.85f);
		P.Text(FString::Printf(TEXT("%.1f ms"), 1000.f / FpsShown), FVector2D(24.f, Top + 32.f), 16.f, FLinearColor(1.f, 1.f, 1.f, 0.85f), 0.f, 4.f, 0.85f);
	}
}
