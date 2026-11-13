#include "UI/WWOSettingsMenu.h"
#include "Systems/WWOSound.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOAimAssistComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOGrenadeComponent.h"
#include "Player/WWOUltComponent.h"
#include "Game/WWOGameMode.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/ProgressBar.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/EditableTextBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "UI/WWOMenuCursor.h"
#include "Net/WWONetSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformTime.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "Engine/World.h"

namespace
{
	FSlateChildSize WWOFillSize(float Value)
	{
		FSlateChildSize Size(ESlateSizeRule::Fill);
		Size.Value = Value;
		return Size;
	}

	FString SavePath() { return FPaths::ProjectSavedDir() / TEXT("WWO/SavedSettings.json"); }

	FWWOSettingRow Header(const FString& Text)
	{
		FWWOSettingRow R;
		R.Header = Text;
		return R;
	}

	// A slider bound to a float UPROPERTY by name (the Godot rows bound by property name too).
	FWWOSettingRow Row(UObject* Obj, FName Prop, const FString& Label, float Min, float Max, float Step, TFunction<FString(float)> Show = nullptr)
	{
		FWWOSettingRow R;
		R.Id = Prop;
		R.Label = Label;
		R.Min = Min;
		R.Max = Max;
		R.Step = Step;
		R.Show = Show;
		FFloatProperty* Property = Obj ? FindFProperty<FFloatProperty>(Obj->GetClass(), Prop) : nullptr;
		if (!Property)
		{
			R.Label += TEXT(" (missing)");
			R.Get = [] { return 0.f; };
			R.Set = [](float) {};
			return R;
		}
		TWeakObjectPtr<UObject> Weak = Obj;
		R.Get = [Weak, Property] { return Weak.IsValid() ? Property->GetPropertyValue_InContainer(Weak.Get()) : 0.f; };
		R.Set = [Weak, Property](float V) { if (Weak.IsValid()) Property->SetPropertyValue_InContainer(Weak.Get(), V); };
		return R;
	}

	// Which settings are a player's own, by page: the rows named, or with none named the whole page.
	// Everything else is game tuning (movement, damage, enemies, items...), the host's online.
	const TMap<FString, TArray<FName>>& OwnSettings()
	{
		static const TMap<FString, TArray<FName>> Own = {
			{ TEXT("Camera"), { "HipFov", "ReticleDrop", "AdsDotSize", "AdsGunScale", "AdsLevel", "MouseSensitivity", "StickSensitivity", "TriggerReset", "FireBufferTime", "RumbleScale", "PunchScale", "BlastShake", "HitFlinch", "BobIntensity", "AdsBob" } },
			{ TEXT("Audio"), {} },
			{ TEXT("HUD"), {} },
		};
		return Own;
	}

	TFunction<FString(float)> Percent() { return [](float V) { return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(V * 100.f)); }; }
	TFunction<FString(float)> Times(int32 Digits = 2) { return [Digits](float V) { return Digits == 1 ? FString::Printf(TEXT("x%.1f"), V) : FString::Printf(TEXT("x%.2f"), V); }; }
	TFunction<FString(float)> SignedPercent() { return [](float V) { return FString::Printf(TEXT("%+d%%"), FMath::RoundToInt(V * 100.f)); }; }

	FString Capitalize(FString S)
	{
		S.ReplaceInline(TEXT("_"), TEXT(" "));
		return S.Left(1).ToUpper() + S.Mid(1);
	}

	FString KindName(EWWOAbilityKind Kind)
	{
		switch (Kind)
		{
		case EWWOAbilityKind::Active: return TEXT("Active");
		case EWWOAbilityKind::Triggered: return TEXT("Triggered");
		case EWWOAbilityKind::Charged: return TEXT("Charged");
		default: return TEXT("Passive");
		}
	}
}

// --- Styling -----------------------------------------------------------------------------------

FSlateFontInfo UWWOSettingsMenu::Font(float Px)
{
	// Godot font sizes are pixels; Slate's are points at 96 dpi (0.75 pt per px).
	return FCoreStyle::GetDefaultFontStyle("Medium", Px * 0.75f * K);
}

void UWWOSettingsMenu::PaintCursor(const UUserWidget* Menu, const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer)
{
	FWWOMenuCursor::PaintRing(Geometry, Menu->GetTickSpaceGeometry(), Out, Layer, K);
}

FSlateBrush UWWOSettingsMenu::RoundBrush(const FLinearColor& Color, float Radius, const FLinearColor& Outline, float OutlineWidth)
{
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(WWO::Lin(Color));
	Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(Radius, Radius, Radius, Radius) * K, FSlateColor(WWO::Lin(Outline)), OutlineWidth * K);
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	return Brush;
}

UTextBlock* UWWOSettingsMenu::MakeText(const FString& Text, float FontPx, const FLinearColor& Color, bool bWrap)
{
	UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
	T->SetText(FText::FromString(Text));
	T->SetFont(Font(FontPx));
	T->SetColorAndOpacity(FSlateColor(WWO::Lin(Color)));
	T->SetAutoWrapText(bWrap);
	return T;
}

UButton* UWWOSettingsMenu::MakeButton(UObject* Outer, const FString& Text, TFunction<void()> Fn, float FontPx, const FLinearColor& Color,
	bool bLeftAlign, FMargin Padding, bool bWrap)
{
	UButton* B = WidgetTree->ConstructWidget<UButton>();
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	B->IsFocusable = false; // the cursor drives everything, not focus
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	FButtonStyle Style;
	Style.SetNormal(RoundBrush(Color, 8.f));
	Style.SetHovered(RoundBrush(Color + FLinearColor(0.08f, 0.08f, 0.08f, 0.f), 8.f));
	Style.SetPressed(RoundBrush(Color + FLinearColor(0.15f, 0.15f, 0.15f, 0.f), 8.f));
	Style.SetNormalPadding(FMargin(0.f));
	Style.SetPressedPadding(FMargin(0.f));
	B->SetStyle(Style);
	// Godot buttons don't wrap: the text sets their width. (Auto-wrap in Slate wraps at last
	// frame's width, which broke two-line list items onto three lines.)
	UTextBlock* Label = MakeText(Text, FontPx, FLinearColor::White, bWrap);
	if (bLeftAlign) Label->SetJustification(ETextJustify::Left);
	else Label->SetJustification(ETextJustify::Center);
	UButtonSlot* Slot = Cast<UButtonSlot>(B->AddChild(Label));
	Slot->SetPadding(Padding * K);
	Slot->SetHorizontalAlignment(bLeftAlign ? HAlign_Left : HAlign_Center);
	Slot->SetVerticalAlignment(VAlign_Center);
	UWWOClickHandler* H = NewObject<UWWOClickHandler>(Outer ? Outer : this);
	H->Fn = [Fn = MoveTemp(Fn), Weak = TWeakObjectPtr<UWWOSettingsMenu>(this)] { if (Weak.IsValid()) UWWOSound::PlayFlat(Weak.Get(), "ui_click"); if (Fn) Fn(); };
	B->OnClicked.AddDynamic(H, &UWWOClickHandler::Handle);
	H->For = B;
	KeepHandler(H);
	return B;
}

// --- What's in the menu -----------------------------------------------------------------------

void UWWOSettingsMenu::RegisterSections(AWWOCharacter* P)
{
	UWWOWeaponComponent* G = P->Weapon;
	UWWOAimAssistComponent* AA = P->AimAssist;
	UWWOGrenadeComponent* Th = P->Grenades;
	UWWOHealthComponent* H = P->Health;
	UWWOUltComponent* U = P->Ult;
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(P);
	UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(P);
	AWWOGameMode* Mode = P->GetWorld()->GetAuthGameMode<AWWOGameMode>();
	auto AddGlobal = [this](const FString& Title, TArray<FWWOSettingRow> Rows, TArray<FWWOMenuButton> Buttons = {})
	{
		FWWOSettingSection S;
		S.Key = TEXT("global/") + Title;
		S.Title = Title;
		S.Rows = MoveTemp(Rows);
		S.Buttons = MoveTemp(Buttons);
		GlobalKeys.Add(S.Key);
		Sections.Add(MoveTemp(S));
	};
	TWeakObjectPtr<UWWOWorldSubsystem> WeakWS = WS;
	const FWWOMenuButton ResetTargets{ TEXT("Reset targets and enemies"), [WeakWS] { if (WeakWS.IsValid()) WeakWS->ResetTargets(); } };

	AddGlobal(TEXT("Walk / Sprint"), {
		Row(P, "WalkSpeed", TEXT("Walk speed"), 1.f, 15.f, 0.5f),
		Row(P, "SprintSpeed", TEXT("Sprint speed"), 1.f, 25.f, 0.5f),
	});
	AddGlobal(TEXT("Camera"), {
		Row(G, "HipFov", TEXT("Field of view (horizontal)"), 55.f, 120.f, 1.f),
		Row(P, "ReticleDrop", TEXT("Reticle below center (fraction of screen height)"), 0.f, 0.2f, 0.005f),
		Row(G, "AdsDotSize", TEXT("Aimed-in dot size (px)"), 0.5f, 6.f, 0.1f),
		Row(G, "AdsGunScale", TEXT("Gun and sight size when aimed, all weapons (1 = same as at the hip)"), 0.3f, 3.f, 0.05f),
		Row(G, "AdsLevel", TEXT("Auto-level the gun's top when aimed (only guns whose top sits below the sight; per gun: ADS gun angle)"), 0.f, 1.f, 0.05f),
		Row(P, "MouseSensitivity", TEXT("Mouse sensitivity"), 0.1f, 10.f, 0.1f),
		Row(P, "StickSensitivity", TEXT("Stick sensitivity"), 0.5f, 10.f, 0.1f),
		Row(P, "TriggerReset", TEXT("Trigger: how far it must come back up to fire again (share of its travel)"), 0.03f, 0.6f, 0.01f),
		Row(P, "FireBufferTime", TEXT("Semi-auto: a pull this early still fires (s; 0 = off)"), 0.f, 0.5f, 0.01f),
		Row(G, "RumbleScale", TEXT("Controller rumble strength (0 = off)"), 0.f, 2.f, 0.05f),
		Row(G, "PunchScale", TEXT("Camera punch strength (0 = off)"), 0.f, 2.f, 0.05f),
		Row(G, "BlastShake", TEXT("Shake from explosions near you (0 = off)"), 0.f, 2.f, 0.05f),
		Row(G, "HitFlinch", TEXT("Jolt when you take damage (0 = off; never moves your aim)"), 0.f, 2.f, 0.05f),
		Row(P, "BobIntensity", TEXT("Head bob intensity"), 0.f, 3.f, 0.1f),
		Row(P, "AdsBob", TEXT("Head bob left when aimed in (0-1)"), 0.f, 1.f, 0.05f),
		Row(P, "WalkStride", TEXT("Walk stride length (m)"), 0.5f, 5.f, 0.1f),
		Row(P, "SprintStride", TEXT("Sprint stride length (m)"), 0.5f, 6.f, 0.1f),
		Row(P, "LandingDip", TEXT("Landing dip"), 0.f, 3.f, 0.1f),
	});
	AddGlobal(TEXT("Audio"), {
		Row(P, "SoundVolume", TEXT("Sound effects volume (0 = silent)"), 0.f, 1.5f, 0.05f),
	});
	AddGlobal(TEXT("HUD"), {
		Header(TEXT("Enemy readability")),
		Row(P, "TargetHighlight", TEXT("Tint on the enemy under the reticle (0 = off)"), 0.f, 0.6f, 0.01f),
		Row(P, "HealthBarRadius", TEXT("Show health bars of enemies this near the reticle (px; 0 = only the one under it)"), 0.f, 500.f, 5.f),
		Row(P, "HealthBarAdsScale", TEXT("Marker range while aiming in (times the range above)"), 1.f, 4.f, 0.05f),
		Row(P, "DamageNumberOffset", TEXT("Damage numbers this far to the side of the hit (px)"), 0.f, 200.f, 5.f),
		Header(TEXT("Radar")),
		Row(P, "RadarSize", TEXT("Radar size (px radius; 0 = off)"), 0.f, 140.f, 2.f),
		Row(P, "RadarRange", TEXT("Radar range (m)"), 5.f, 80.f, 1.f),
		Row(P, "RadarFullRange", TEXT("Section fills completely inside (m; about melee distance)"), 1.f, 10.f, 0.25f),
		Row(P, "RadarSteps", TEXT("Fill steps between the rim and full"), 2.f, 8.f, 1.f),
		Row(P, "RadarHeightFade", TEXT("Height difference at which a section is faintest (m)"), 1.f, 20.f, 0.5f),
		Row(P, "RadarMinAlpha", TEXT("Faintest a section gets (0-1)"), 0.05f, 1.f, 0.05f),
		Header(TEXT("Your health")),
		Row(P, "HealthVignette", TEXT("Red vignette as your health drops (strength; 0 = off)"), 0.f, 2.f, 0.05f),
	});
	AddGlobal(TEXT("Jump"), {
		Row(P, "JumpHeight1", TEXT("Jump 1 height (m)"), 0.2f, 5.f, 0.1f),
		Row(P, "JumpHeight2", TEXT("Jump 2 height (m)"), 0.f, 5.f, 0.1f),
		Row(P, "AscentGravity", TEXT("Jump ascent speed"), 2.f, 80.f, 1.f),
		Row(P, "DescentGravity", TEXT("Jump descent speed"), 2.f, 80.f, 1.f),
		Row(P, "AirControl", TEXT("Air control"), 0.f, 40.f, 0.5f),
		Row(P, "ApexHang", TEXT("Apex hang"), 0.f, 0.9f, 0.05f),
		Row(P, "CoyoteTime", TEXT("Coyote time (s)"), 0.f, 0.3f, 0.01f),
		Row(P, "MantleReach", TEXT("Mantle reach (m, 0 = off)"), 0.f, 3.5f, 0.1f),
		Row(P, "MantleTime", TEXT("Mantle time (s)"), 0.1f, 1.f, 0.05f),
	});
	AddGlobal(TEXT("Crouch / Slide"), {
		Row(P, "CrouchSpeed", TEXT("Crouch walk speed"), 0.5f, 10.f, 0.5f),
		Row(P, "SlideSpeed", TEXT("Slide speed"), 1.f, 30.f, 0.5f),
		Row(P, "SlideDistance", TEXT("Slide distance (m)"), 1.f, 20.f, 0.5f),
	});
	AddGlobal(TEXT("Melee"), {
		Row(G, "MeleeTime", TEXT("Melee time (s, lower = faster)"), 0.2f, 2.f, 0.05f),
		Row(G, "MeleeReach", TEXT("Melee reach (m)"), 0.5f, 6.f, 0.1f),
		Row(G, "MeleeDamage", TEXT("Melee damage"), 1.f, 300.f, 1.f),
		Row(G, "LungeRange", TEXT("Lunge range (m)"), 0.f, 10.f, 0.1f),
		Row(G, "LungeAngle", TEXT("Lunge angle (degrees)"), 0.f, 60.f, 1.f),
		Row(G, "LungeSpeed", TEXT("Lunge speed (m/s)"), 5.f, 40.f, 0.5f),
		Row(G, "LungeVertical", TEXT("Lunge vertical pull"), 0.f, 1.f, 0.05f),
		Row(G, "LungeAim", TEXT("Lunge camera pull"), 0.f, 1.f, 0.05f),
		Header(TEXT("Impact feel (when a melee lands)")),
		Row(G, "MeleeHitstop", TEXT("Hit-stop freeze (s)"), 0.f, 0.3f, 0.005f),
		Row(G, "MeleeJolt", TEXT("Camera jolt up (degrees)"), 0.f, 8.f, 0.1f),
		Row(G, "MeleePunchFov", TEXT("Camera punch FOV (degrees, - = zoom in)"), -15.f, 15.f, 0.25f),
		Row(G, "MeleePunchRoll", TEXT("Camera punch roll (degrees)"), -8.f, 8.f, 0.1f),
		Row(G, "MeleePunchTime", TEXT("Camera punch settle time (s)"), 0.02f, 0.8f, 0.01f),
		Row(G, "MeleeRumbleStrong", TEXT("Rumble: heavy thump (big motor)"), 0.f, 1.f, 0.05f),
		Row(G, "MeleeRumbleWeak", TEXT("Rumble: sharp buzz (small motor)"), 0.f, 1.f, 0.05f),
		Row(G, "MeleeRumbleTime", TEXT("Rumble length (s)"), 0.f, 0.6f, 0.01f),
		Row(G, "MeleeKnockback", TEXT("Knockback (m/s)"), 0.f, 30.f, 0.5f),
		Row(G, "MeleeKillBoost", TEXT("Kills hit harder by (except knockback)"), 1.f, 3.f, 0.05f, Times()),
		Row(G, "MeleeKillKnockback", TEXT("Killing blow knockback"), 1.f, 4.f, 0.05f, Times()),
		Header(TEXT("First-person arm")),
		Row(G, "MeleeArmYaw", TEXT("Turn toward center (degrees)"), -45.f, 45.f, 0.5f),
		Row(G, "MeleeArmPitch", TEXT("Lift (degrees)"), -45.f, 45.f, 0.5f),
		Row(G, "MeleeArmPush", TEXT("Push forward (m)"), -0.4f, 0.4f, 0.01f),
		Row(G, "MeleeArmFov", TEXT("Arm FOV"), 40.f, 110.f, 1.f),
		Row(G, "MeleeArmHold", TEXT("Fist stays out after the hit (s)"), 0.f, 0.3f, 0.01f),
		Row(G, "MeleeArmRetract", TEXT("Arm pull-back time (s)"), 0.05f, 0.6f, 0.01f),
	});
	AddGlobal(TEXT("Grenades"), {
		Row(Th, "ThrowDistance", TEXT("Throw distance (m)"), 3.f, 40.f, 0.5f),
		Row(Th, "RechargeTime", TEXT("Recharge time (s)"), 0.5f, 60.f, 0.5f),
		Row(Th, "DetonationTime", TEXT("Detonation time (s)"), 0.1f, 5.f, 0.05f),
		Header(TEXT("Throw animation (left arm)")),
		Row(G, "ThrowRelease", TEXT("Grenade leaves your hand after (s)"), 0.f, 0.6f, 0.01f),
		Row(G, "ThrowFollow", TEXT("Follow-through (s)"), 0.05f, 0.6f, 0.01f),
		Row(G, "ThrowArmYaw", TEXT("Turn toward center (degrees)"), -45.f, 45.f, 0.5f),
		Row(G, "ThrowArmPitch", TEXT("Lift (degrees)"), -45.f, 45.f, 0.5f),
		Row(G, "ThrowArmPush", TEXT("Push forward (m)"), -0.4f, 0.4f, 0.01f),
	});
	TWeakObjectPtr<UWWOHealthComponent> WeakH = H;
	AddGlobal(TEXT("Player"), {
		Header(TEXT("Health and shields")),
		Row(H, "BaseHealth", TEXT("Health"), 10.f, 500.f, 5.f),
		Row(H, "BaseShield", TEXT("Shields"), 0.f, 500.f, 5.f),
		Row(H, "RechargeDelay", TEXT("Recovery delay (s)"), 0.5f, 15.f, 0.25f),
		Row(H, "HealthRegen", TEXT("Health regen (per s)"), 5.f, 300.f, 5.f),
		Row(H, "ShieldRegen", TEXT("Shield regen (per s)"), 5.f, 300.f, 5.f),
		Row(H, "RespawnTime", TEXT("Respawn time (s)"), 0.f, 10.f, 0.25f),
		Header(TEXT("Revive (online: down until a teammate revives you)")),
		Row(H, "ReviveTime", TEXT("Hold to revive (s)"), 0.25f, 10.f, 0.25f),
		Row(H, "ReviveRange", TEXT("Revive reach (m)"), 0.5f, 6.f, 0.1f),
		Row(H, "ReviveHealth", TEXT("Revived with health"), 0.05f, 1.f, 0.05f, Percent()),
		Row(H, "GodMode", TEXT("God mode (0/1)"), 0.f, 1.f, 1.f),
		Header(TEXT("Weapon handling")),
		Row(G, "SwapTime", TEXT("Weapon swap time (s)"), 0.05f, 1.5f, 0.01f),
		Row(G, "ReadyTime", TEXT("Ready time after sprint (s)"), 0.f, 0.6f, 0.01f),
		Header(TEXT("Shell")),
		Row(L, "PointValue", TEXT("Each group point changes stats by"), 0.f, 0.25f, 0.01f, Percent()),
	}, {
		{ TEXT("Take 30 damage"), [WeakH] { if (WeakH.IsValid()) WeakH->TakeDamage(30.f); } },
		{ TEXT("Die (test respawn)"), [WeakH] { if (WeakH.IsValid()) WeakH->Kill(); } },
	});
	{
		FWWOSettingRow TargetHealthRow = Row(WS, "TargetHealth", TEXT("Target health"), 10.f, 2000.f, 10.f);
		TargetHealthRow.Set = [WeakWS](float V) { if (WeakWS.IsValid()) WeakWS->SetTargetHealth(V); }; // new health applies right away
		AddGlobal(TEXT("Targets"), { TargetHealthRow, Row(WS, "RespawnTime", TEXT("Auto respawn (s, 0 = off)"), 0.f, 10.f, 0.5f) }, { ResetTargets });
	}
	TWeakObjectPtr<UWWOUltComponent> WeakU = U;
	AddGlobal(TEXT("Ult"), {
		Header(TEXT("Ult (F / L1+R1). VoltVine: massive grenade blast")),
		Row(U, "ChargeTime", TEXT("Charge time (s)"), 1.f, 600.f, 1.f),
		Row(U, "Damage", TEXT("Damage (center)"), 0.f, 5000.f, 10.f),
		Row(U, "BlastRadius", TEXT("Blast radius (m)"), 1.f, 40.f, 0.5f),
		Header(TEXT("Animation and camera")),
		Row(U, "WindupTime", TEXT("Windup before the blast (s)"), 0.2f, 4.f, 0.05f),
		Row(U, "HoldTime", TEXT("Watch the blast for (s)"), 0.f, 4.f, 0.05f),
		Row(U, "CameraBlend", TEXT("Camera glide time (s)"), 0.05f, 2.f, 0.05f),
		Row(U, "CameraDistance", TEXT("Camera distance behind (m)"), 1.f, 12.f, 0.25f),
		Row(U, "CameraHeight", TEXT("Camera height (m)"), -1.f, 6.f, 0.25f),
		Row(U, "CameraSide", TEXT("Camera over-shoulder (m)"), -3.f, 3.f, 0.1f),
		Header(TEXT("Veil (VeilSpore ult)")),
		Row(U, "VeilTime", TEXT("Veiled for (s)"), 1.f, 60.f, 0.5f),
		Row(U, "VeilFogRadius", TEXT("Fog radius (m)"), 1.f, 20.f, 0.25f),
		Row(U, "VeilFogTime", TEXT("Fog time (s)"), 1.f, 60.f, 0.5f),
		Row(U, "VeilMeleeMult", TEXT("Melee damage while veiled"), 1.f, 6.f, 0.1f, Times(1)),
		Row(U, "VeilCameraBlend", TEXT("Camera flash time (s)"), 0.05f, 1.f, 0.01f),
		Row(U, "VeilHold", TEXT("Time in third person (s)"), 0.f, 2.f, 0.05f),
		Header(TEXT("Entrench (IronBark ult)")),
		Row(U, "EntrenchTime", TEXT("Entrenched for (s)"), 1.f, 60.f, 0.5f),
		Row(U, "EntrenchGrenadeMult", TEXT("Grenade recharge speed"), 1.f, 20.f, 0.5f, Times(1)),
	}, { { TEXT("Fill ult"), [WeakU] { if (WeakU.IsValid()) WeakU->Fill(); } } });
	UWWOSoldierData* SD = L->SoldierData;
	AddGlobal(TEXT("Soldier"), {
		Header(TEXT("Enemy: Soldier (shared by all soldiers)")),
		Row(SD, "AiEnabled", TEXT("AI on (0/1)"), 0.f, 1.f, 1.f),
		Row(SD, "MaxHealth", TEXT("Health"), 10.f, 1000.f, 5.f),
		Row(SD, "WalkSpeed", TEXT("Walk speed (m/s)"), 0.5f, 10.f, 0.25f),
		Row(SD, "MinRange", TEXT("Backs off inside (m)"), 0.f, 50.f, 0.5f),
		Row(SD, "MaxRange", TEXT("Moves in beyond (m)"), 1.f, 80.f, 0.5f),
		Row(SD, "SightRange", TEXT("Notices you within (m)"), 5.f, 120.f, 1.f),
		Row(SD, "StrafeTime", TEXT("Strafe switch time (s)"), 0.3f, 6.f, 0.1f),
		Row(SD, "TurnSpeed", TEXT("Turn speed"), 1.f, 30.f, 0.5f),
		Header(TEXT("Shooting")),
		Row(SD, "WindupTime", TEXT("Windup glow (s)"), 0.f, 2.f, 0.05f),
		Row(SD, "BurstCount", TEXT("Shots per burst"), 1.f, 10.f, 1.f),
		Row(SD, "BurstInterval", TEXT("Time between shots (s)"), 0.03f, 1.f, 0.01f),
		Row(SD, "BurstCooldown", TEXT("Time between bursts (s)"), 0.2f, 8.f, 0.1f),
		Row(SD, "GroupSpacing", TEXT("Group burst spacing (s)"), 0.f, 3.f, 0.05f),
		Row(SD, "ProjectileDamage", TEXT("Damage per shot"), 0.f, 100.f, 1.f),
		Row(SD, "ProjectileSpeed", TEXT("Shot speed (m/s)"), 5.f, 150.f, 1.f),
		Row(SD, "Spread", TEXT("Aim error (degrees)"), 0.f, 10.f, 0.1f),
		Header(TEXT("Cover")),
		Row(SD, "CoverHealth", TEXT("Seeks cover below health"), 0.f, 1.f, 0.05f, Percent()),
	}, { ResetTargets });
	UWWOSwarmData* SW = L->SwarmData;
	AddGlobal(TEXT("Swarm"), {
		Header(TEXT("Enemy: Swarm (shared by all swarm)")),
		Row(SW, "AiEnabled", TEXT("AI on (0/1)"), 0.f, 1.f, 1.f),
		Row(SW, "MaxHealth", TEXT("Health"), 5.f, 500.f, 5.f),
		Row(SW, "WalkSpeed", TEXT("Run speed (m/s)"), 1.f, 20.f, 0.25f),
		Row(SW, "SightRange", TEXT("Notices you within (m)"), 5.f, 120.f, 1.f),
		Row(SW, "TurnSpeed", TEXT("Turn speed"), 1.f, 30.f, 0.5f),
		Row(SW, "SpreadDistance", TEXT("Pack spacing (m)"), 0.f, 5.f, 0.1f),
		Header(TEXT("Leap")),
		Row(SW, "LeapRange", TEXT("Leaps from (m)"), 0.f, 12.f, 0.25f),
		Row(SW, "LeapSpeed", TEXT("Leap speed (m/s)"), 2.f, 30.f, 0.5f),
		Row(SW, "LeapHeight", TEXT("Leap height (m)"), 0.f, 4.f, 0.1f),
		Row(SW, "LeapDamage", TEXT("Leap damage"), 0.f, 100.f, 1.f),
		Row(SW, "LeapCooldown", TEXT("Time between leaps (s)"), 0.2f, 10.f, 0.1f),
		Header(TEXT("Bite")),
		Row(SW, "BiteRange", TEXT("Bites within (m)"), 0.5f, 4.f, 0.05f),
		Row(SW, "BiteDamage", TEXT("Bite damage"), 0.f, 100.f, 1.f),
		Row(SW, "BiteInterval", TEXT("Time between bites (s)"), 0.1f, 4.f, 0.05f),
	}, { ResetTargets });
	UWWOTankData* TK = L->TankData;
	AddGlobal(TEXT("Tank"), {
		Header(TEXT("Enemy: Tank")),
		Row(TK, "AiEnabled", TEXT("AI on (0/1)"), 0.f, 1.f, 1.f),
		Row(TK, "MaxHealth", TEXT("Health"), 50.f, 5000.f, 25.f),
		Row(TK, "WalkSpeed", TEXT("Walk speed (m/s)"), 0.5f, 10.f, 0.25f),
		Row(TK, "SightRange", TEXT("Notices you within (m)"), 5.f, 120.f, 1.f),
		Row(TK, "TurnSpeed", TEXT("Turn speed"), 0.2f, 20.f, 0.1f),
		Header(TEXT("Shield (applies on reset)")),
		Row(TK, "Shield", TEXT("Shield (0 = none)"), 0.f, 2000.f, 10.f),
		Row(TK, "ShieldRegenDelay", TEXT("Refill delay (s)"), 0.5f, 30.f, 0.5f),
		Row(TK, "ShieldRegenRate", TEXT("Refill rate (per s)"), 5.f, 500.f, 5.f),
		Header(TEXT("Slam")),
		Row(TK, "SlamRange", TEXT("Slams when you're within (m)"), 1.f, 10.f, 0.25f),
		Row(TK, "SlamRadius", TEXT("Slam radius (m)"), 1.f, 12.f, 0.25f),
		Row(TK, "SlamDamage", TEXT("Slam damage"), 0.f, 200.f, 1.f),
		Row(TK, "SlamWindup", TEXT("Arms-up warning (s)"), 0.1f, 3.f, 0.05f),
		Row(TK, "SlamRecover", TEXT("Stands still after (s)"), 0.f, 5.f, 0.1f),
		Row(TK, "SlamCooldown", TEXT("Time between slams (s)"), 0.5f, 15.f, 0.25f),
		Row(TK, "SlowAmount", TEXT("Slow amount"), 0.f, 0.95f, 0.05f, [](float V) { return FString::Printf(TEXT("%d%% slower"), FMath::RoundToInt(V * 100.f)); }),
		Row(TK, "SlowTime", TEXT("Slow time (s)"), 0.f, 10.f, 0.25f),
		Header(TEXT("Lobbed shot")),
		Row(TK, "LobDamage", TEXT("Damage (center)"), 0.f, 200.f, 1.f),
		Row(TK, "LobRadius", TEXT("Splash radius (m)"), 0.5f, 10.f, 0.25f),
		Row(TK, "LobInterval", TEXT("Time between shots (s)"), 0.5f, 15.f, 0.25f),
		Row(TK, "LobFlightTime", TEXT("Time in the air (s)"), 0.3f, 4.f, 0.05f),
	}, { ResetTargets });
	TWeakObjectPtr<UWorld> WeakWorld = P->GetWorld();
	auto MapButton = [WeakWorld](const TCHAR* Text, const TCHAR* Level) -> FWWOMenuButton
	{
		const FName LevelName(Level);
		return { FString(Text), [WeakWorld, LevelName] { if (WeakWorld.IsValid()) AWWOGameMode::LoadMap(WeakWorld.Get(), LevelName); } };
	};
	// The same table the launch screen offers (UWWONetSubsystem::Maps): a new map goes there once.
	TArray<FWWOMenuButton> MapButtons;
	for (const FWWOMapInfo& Info : UWWONetSubsystem::Maps())
	{
		MapButtons.Add(MapButton(*FString::Printf(TEXT("%s (%s)"), *Info.Title, *Info.Blurb), *Info.Level.ToString()));
	}
	AddGlobal(TEXT("Maps"), { Header(TEXT("Switch maps (restarts the run on that map)")) }, MapButtons);
	// LAN play (UWWONetSubsystem). Hosting reopens this map for others to join; joining loads the host's.
	auto Net = [WeakWorld]() { return WeakWorld.IsValid() ? UWWONetSubsystem::Get(WeakWorld.Get()) : nullptr; };
	FWWOSettingRow NetStatus;
	NetStatus.Header = TEXT("Status");
	NetStatus.LiveHeader = [WeakWorld, Net]() { UWWONetSubsystem* N = Net(); return N && WeakWorld.IsValid() ? N->DescribeStatus(WeakWorld.Get()) : FString(); };
	NetStatus.bPref = true;
	AddGlobal(TEXT("Multiplayer"), {
		NetStatus,
		Header(TEXT("The title screen starts and joins games. The host and join buttons here are shortcuts for testing.")),
	}, {
		{ TEXT("Host a LAN game (this map)"), [WeakWorld, Net] { if (UWWONetSubsystem* N = Net(); N && WeakWorld.IsValid()) N->Host(FName(*UGameplayStatics::GetCurrentLevelName(WeakWorld.Get()))); } },
		{ TEXT("Host a PvP game (team deathmatch, this map)"), [WeakWorld, Net] { if (UWWONetSubsystem* N = Net(); N && WeakWorld.IsValid()) N->Host(FName(*UGameplayStatics::GetCurrentLevelName(WeakWorld.Get())), true); } },
		{ TEXT("Quit to main menu"), [Net] { if (UWWONetSubsystem* N = Net()) N->LeaveToTitle(); }, true },
		{ TEXT("Switch team (PvP)"), [WeakWorld] { if (WeakWorld.IsValid()) if (AWWOPlayerController* PC = Cast<AWWOPlayerController>(WeakWorld->GetFirstPlayerController())) PC->ServerSwitchTeam(); }, true },
		{ TEXT("Find and join a LAN game"), [Net] { if (UWWONetSubsystem* N = Net()) N->FindAndJoin(); } },
		{ TEXT("Join a game on this machine (testing)"), [Net] { if (UWWONetSubsystem* N = Net()) N->JoinAddress(TEXT("127.0.0.1")); } },
	});
	TWeakObjectPtr<AWWOGameMode> WeakMode = Mode;
	AddGlobal(TEXT("PvP"), {
		Header(TEXT("Team deathmatch (the host's settings count; 0 turns a limit off)")),
		Row(Mode, "KillTarget", TEXT("Team kills to win"), 0.f, 200.f, 1.f),
		Row(Mode, "TimeLimit", TEXT("Time limit (minutes)"), 0.f, 60.f, 1.f),
		Row(Mode, "PvPRespawnTime", TEXT("Respawn delay (s)"), 0.5f, 15.f, 0.5f),
	});
	AddGlobal(TEXT("Bots"), {
		Header(TEXT("Bot players in PvP (the host's settings count)")),
		Row(Mode, "Bots", TEXT("Bots in the match (raising it adds them now)"), 0.f, 11.f, 1.f),
		Row(Mode, "BotHealth", TEXT("Bot health (yours is on the Player page)"), 10.f, 500.f, 5.f),
		Row(Mode, "BotShield", TEXT("Bot shields"), 0.f, 500.f, 5.f),
		Row(Mode, "BotReaction", TEXT("Reaction time (s)"), 0.f, 1.5f, 0.05f),
		Row(Mode, "BotAimError", TEXT("Aim error (degrees; 0 = perfect)"), 0.f, 8.f, 0.1f),
		Row(Mode, "BotTurnSpeed", TEXT("Turn speed (degrees per second)"), 60.f, 720.f, 10.f),
		Row(Mode, "BotViewAngle", TEXT("View cone (degrees)"), 30.f, 360.f, 5.f),
		Row(Mode, "BotSightRange", TEXT("Sight range (m)"), 10.f, 150.f, 5.f),
		Row(Mode, "BotMemory", TEXT("Hunts a lost enemy for (s)"), 0.f, 20.f, 0.5f),
		Row(Mode, "BotAggression", TEXT("Aggression (0 = backs off early, 1 = never)"), 0.f, 1.f, 0.05f),
		Row(Mode, "BotKitUse", TEXT("Grenade, ability and ult use (0 = never)"), 0.f, 1.f, 0.05f),
		Header(TEXT("Teamwork")),
		Row(Mode, "BotStickToPlayer", TEXT("Share of your team's bots that escort you (the rest pair up)"), 0.f, 1.f, 0.05f),
		Row(Mode, "BotCalloutRange", TEXT("Callout range (m; teammates this near come to help; 0 = off)"), 0.f, 100.f, 5.f),
	});
	AddGlobal(TEXT("Waves"), {
		Header(TEXT("Arena waves (changes to 'on' and the wave list apply on Restart run)")),
		Row(Mode, "Enabled", TEXT("Waves on (0/1; 0 = sandbox)"), 0.f, 1.f, 1.f),
		Row(Mode, "WavesToWin", TEXT("Waves to win"), 1.f, 30.f, 1.f),
		Row(Mode, "StartDelay", TEXT("Time before wave 1 (s)"), 0.f, 30.f, 0.5f),
		Row(Mode, "BreakTime", TEXT("Break between waves (s)"), 0.f, 60.f, 0.5f),
		Row(Mode, "SizeScale", TEXT("Wave size"), 0.25f, 4.f, 0.05f, Times()),
		Row(Mode, "SpawnStagger", TEXT("Time between spawns (s)"), 0.f, 3.f, 0.05f),
	}, {
		{ TEXT("Restart run"), [WeakMode, WeakWorld]
			{
				// In someone else's game there's no game mode here: LoadMap says only the host can.
				if (WeakMode.IsValid()) WeakMode->Restart();
				else if (WeakWorld.IsValid()) AWWOGameMode::LoadMap(WeakWorld.Get(), FName(*UGameplayStatics::GetCurrentLevelName(WeakWorld.Get())));
			} },
		{ TEXT("Skip wave (kill all)"), [WeakMode] { if (WeakMode.IsValid()) WeakMode->SkipWave(); } },
	});
	UWWOAllyData* AL = L->AllyData;
	AddGlobal(TEXT("Allies"), {
		Header(TEXT("AI teammates (for testing team abilities like Shield Share)")),
		Row(AL, "AiEnabled", TEXT("AI on (0/1)"), 0.f, 1.f, 1.f),
		Row(AL, "MaxHealth", TEXT("Health"), 10.f, 500.f, 5.f),
		Row(AL, "MaxShield", TEXT("Shields"), 0.f, 500.f, 5.f),
		Row(AL, "RechargeDelay", TEXT("Recovery delay (s)"), 0.5f, 15.f, 0.25f),
		Row(AL, "ShieldRegen", TEXT("Shield regen (per s)"), 1.f, 300.f, 5.f),
		Row(AL, "MoveSpeed", TEXT("Move speed (m/s)"), 1.f, 15.f, 0.25f),
		Row(AL, "FollowMin", TEXT("Stays at least (m) from you"), 0.f, 10.f, 0.25f),
		Row(AL, "FollowMax", TEXT("Walks back past (m)"), 1.f, 30.f, 0.5f),
		Header(TEXT("Shooting")),
		Row(AL, "Damage", TEXT("Damage per shot"), 0.f, 100.f, 1.f),
		Row(AL, "FireInterval", TEXT("Time between shots (s)"), 0.05f, 3.f, 0.05f),
		Row(AL, "Spread", TEXT("Aim error (degrees)"), 0.f, 15.f, 0.25f),
		Row(AL, "SightRange", TEXT("Shoots enemies within (m)"), 5.f, 120.f, 1.f),
		Row(AL, "RespawnTime", TEXT("Down for (s)"), 1.f, 60.f, 1.f),
	}, { ResetTargets });
	AddGlobal(TEXT("Aim Assist"), {
		Row(AA, "MouseAssist", TEXT("Mouse assist (0 = off)"), 0.f, 1.f, 0.05f),
		Row(AA, "ShowDebug", TEXT("Show assist cones (0/1)"), 0.f, 1.f, 1.f),
		Header(TEXT("Hidden assist (only while a shot's recoil plays out; per-weapon amounts in each weapon)")),
		Row(AA, "HiddenAssist", TEXT("Hidden assist on (0/1)"), 0.f, 1.f, 1.f),
		Row(AA, "ShowHiddenAssist", TEXT("Show hidden assist markers (0/1)"), 0.f, 1.f, 1.f),
	});

	// Specific sections: one per item.
	for (UWWOWeaponData* W : L->Weapons)
	{
		FWWOSettingSection S;
		S.Key = TEXT("weapon/") + W->Id.ToString();
		S.Title = W->DisplayName;
		S.Rows = {
			Header(W->Archetype), Header(TEXT("Aim down sights")),
			Row(W, "Zoom", TEXT("ADS zoom (magnification)"), 1.f, 10.f, 0.1f),
			Row(W, "AdsTime", TEXT("ADS time (s)"), 0.05f, 1.f, 0.01f),
			Row(W, "AdsSensitivity", TEXT("ADS sensitivity multiplier"), 0.1f, 2.f, 0.05f),
			Row(W, "AdsMoveSpeed", TEXT("ADS move speed multiplier"), 0.1f, 1.f, 0.05f),
			Row(W, "AdsTilt", TEXT("ADS gun angle (degrees; + tips the muzzle down, - up)"), -15.f, 15.f, 0.25f),
			Row(W, "AdsVignette", TEXT("ADS vignette"), 0.f, 1.f, 0.05f),
			Row(W, "ScopeOverlay", TEXT("Scope overlay (0/1)"), 0.f, 1.f, 1.f),
			Row(W, "SightHeight", TEXT("Sight dot height above gun (cm)"), -6.f, 6.f, 0.1f),
			Header(TEXT("Shooting")),
			Row(W, "SemiAuto", TEXT("Semi-auto (0 = hold to fire)"), 0.f, 1.f, 1.f),
			Row(W, "FireRate", TEXT("Fire rate (rounds/min; bursts/min for a burst gun)"), 20.f, 1200.f, 5.f),
			Row(W, "Burst", TEXT("Rounds per trigger pull (1 = no burst)"), 1.f, 6.f, 1.f),
			Row(W, "BurstRate", TEXT("Fire rate inside a burst (rounds/min)"), 200.f, 1500.f, 10.f),
			Row(W, "Damage", W->Pellets > 1.f ? TEXT("Damage (per pellet)") : TEXT("Damage"), 1.f, 400.f, 1.f),
			Row(W, "CritMultiplier", TEXT("Critical hit multiplier"), 1.f, 4.f, 0.05f),
			Row(W, "FireRange", TEXT("Range (m)"), 5.f, 400.f, 5.f),
			Row(W, "Pellets", TEXT("Pellets per shot"), 1.f, 20.f, 1.f),
			Row(W, "PelletSpread", TEXT("Pellet spread (degrees)"), 0.f, 15.f, 0.1f),
			Row(W, "HipSpread", TEXT("Hip-fire spread (degrees)"), 0.f, 10.f, 0.1f),
			Row(W, "RecoilUp", TEXT("Recoil up (degrees/shot)"), 0.f, 8.f, 0.05f),
			Row(W, "RecoilSide", TEXT("Recoil side (degrees/shot)"), 0.f, 3.f, 0.05f),
			Row(W, "RecoilKickTime", TEXT("Recoil rise time (s, 0 = instant)"), 0.f, 0.3f, 0.005f),
			Row(W, "RecoilRecovery", TEXT("Recoil falls back (0-1)"), 0.f, 1.f, 0.05f),
			Row(W, "RecoilRecoveryTime", TEXT("Recoil fall-back speed (s, when not synced)"), 0.02f, 1.f, 0.01f),
			Row(W, "RecoilRecoverySync", TEXT("Fall-back ends when next shot is ready (0/1)"), 0.f, 1.f, 1.f),
			Row(W, "RecoilRecoveryCurve", TEXT("Synced fall-back curve (1 = steady, higher = fast then slow)"), 1.f, 5.f, 0.1f),
			Row(W, "RecoilRecoveryDelay", TEXT("Synced fall-back lands after shot is ready by (s)"), -0.15f, 0.15f, 0.005f),
			Row(W, "VerticalKick", TEXT("Vertical kick (gun tilts up; 1 = about 3.4 degrees)"), 0.f, 20.f, 0.1f),
			Row(W, "HorizontalKick", TEXT("Horizontal kick (+ right, - left)"), -5.f, 5.f, 0.1f),
			Row(W, "KickHold", TEXT("Kick holds at the top for (s)"), 0.f, 0.4f, 0.005f),
			Row(W, "KickReturn", TEXT("Kick comes back over (s)"), 0.02f, 0.8f, 0.01f),
			Row(W, "AdsKick", TEXT("Kick left when aimed in (0-1)"), 0.f, 1.f, 0.01f),
			Row(W, "KickPush", TEXT("Kick pushes the gun back (cm)"), 0.f, 15.f, 0.1f),
			Row(W, "KickPivotDown", TEXT("Kick turns about a point below the sight by (cm)"), 0.f, 20.f, 0.1f),
			Row(W, "KickPivotAhead", TEXT("...and ahead of the sight by (cm)"), -10.f, 20.f, 0.1f),
			Row(W, "KickSide", TEXT("Kick shifts the gun right (cm)"), -5.f, 5.f, 0.05f),
			Row(W, "RecoilShape", TEXT("Recoil follows the measured curves (0/1)"), 0.f, 1.f, 1.f),
			Row(W, "BurstRecoilLater", TEXT("Burst: later rounds kick this much of the first"), 0.f, 1.5f, 0.05f),
			Row(W, "MagazineSize", TEXT("Magazine size"), 1.f, 100.f, 1.f),
			Row(W, "ReloadTime", TEXT("Reload time (s)"), 0.3f, 6.f, 0.05f),
			Header(TEXT("Rumble")),
			Row(W, "RumbleStrong", TEXT("Rumble: heavy thump (big motor)"), 0.f, 1.f, 0.05f),
			Row(W, "RumbleWeak", TEXT("Rumble: sharp buzz (small motor)"), 0.f, 1.f, 0.05f),
			Row(W, "RumbleTime", TEXT("Rumble length (s)"), 0.f, 0.5f, 0.005f),
			Header(TEXT("Camera punch")),
			Row(W, "PunchFov", TEXT("Punch: FOV widens by (degrees)"), 0.f, 10.f, 0.1f),
			Row(W, "PunchRoll", TEXT("Punch: camera roll (degrees)"), 0.f, 5.f, 0.05f),
			Row(W, "PunchTime", TEXT("Punch settle time (s)"), 0.02f, 0.5f, 0.005f),
			Header(TEXT("Handling")),
			Row(W, "ViewmodelFov", TEXT("Gun FOV"), 40.f, 100.f, 1.f),
			Row(W, "Sway", TEXT("Gun sway"), 0.f, 3.f, 0.1f),
			Row(W, "GunBob", TEXT("Gun bob"), 0.f, 3.f, 0.1f),
			Row(W, "AdsGunBob", TEXT("Gun bob left when aimed in (0-1)"), 0.f, 1.f, 0.05f),
			Row(W, "StrafeShift", TEXT("Strafe: gun shifts sideways (cm)"), -3.f, 3.f, 0.05f),
			Row(W, "StrafeRoll", TEXT("Strafe: gun rolls (degrees)"), -8.f, 8.f, 0.1f),
			Row(W, "AdsMark", TEXT("Aimed-in mark (0 none, 1 dot, 2 iron-sight marks)"), 0.f, 2.f, 1.f),
			Row(W, "SlideTilt", TEXT("Slide gun tilt (degrees)"), 0.f, 60.f, 1.f),
			Header(TEXT("Aim assist")),
			Row(W, "AimAssistance", TEXT("Aim assistance stat"), 0.f, 100.f, 1.f),
			Row(W, "CritMagnetism", TEXT("Head magnetism (fraction of cone)"), 0.f, 1.f, 0.05f),
			Row(W, "MagnetismAngle", TEXT("Magnetism cone (degrees)"), 0.f, 10.f, 0.1f),
			Row(W, "FrictionStrength", TEXT("Friction strength"), 0.f, 0.9f, 0.05f),
			Row(W, "FrictionAngle", TEXT("Friction cone (degrees)"), 0.f, 15.f, 0.1f),
			Row(W, "Adhesion", TEXT("Adhesion strength"), 0.f, 1.f, 0.05f),
			Row(W, "AssistRange", TEXT("Assist range (m)"), 5.f, 300.f, 1.f),
			Row(W, "AdsBoost", TEXT("ADS assist boost"), 1.f, 3.f, 0.05f),
			Header(TEXT("Hidden assist (while this gun's recoil plays out)")),
			Row(W, "KickSteer", TEXT("Kick steer (the kick drifts sideways toward the target; 0 = off)"), 0.f, 3.f, 0.05f),
			Row(W, "RecoverySteer", TEXT("Fall-back steer (the recoil falls back toward the target; 0 = off)"), 0.f, 3.f, 0.05f),
			Row(W, "FollowupMagnetism", TEXT("Follow-up shot magnetism (x cone; 1 = off)"), 1.f, 4.f, 0.05f),
			Row(W, "KickFriction", TEXT("Friction during recoil (x strength; 1 = off)"), 1.f, 4.f, 0.05f),
			Row(W, "KickAdhesion", TEXT("Adhesion added during recoil (0 = off)"), 0.f, 1.f, 0.05f),
		};
		Sections.Add(MoveTemp(S));
	}
	for (UWWOFramePartData* Part : L->FrameParts)
	{
		FWWOSettingSection S;
		S.Key = TEXT("frame/") + Part->Id.ToString();
		S.Title = Part->DisplayName;
		S.Rows.Add(Header(Capitalize(Part->Slot.ToString()) + TEXT(" part · group points")));
		for (const FName& Group : UWWOLoadoutSubsystem::GroupOrder())
		{
			TArray<FString> Stats;
			for (const FName& Stat : UWWOLoadoutSubsystem::StatsInGroup(Group)) Stats.Add(UWWOLoadoutSubsystem::StatName(Stat).ToLower());
			S.Rows.Add(Header(UWWOLoadoutSubsystem::GroupTitle(Group) + TEXT(": ") + FString::Join(Stats, TEXT(", "))));
			S.Rows.Add(Row(Part, FName(*UWWOLoadoutSubsystem::GroupTitle(Group)), UWWOLoadoutSubsystem::GroupTitle(Group) + TEXT(" points"), -5.f, 5.f, 1.f,
				[](float V) { return FString::Printf(TEXT("%+d"), FMath::RoundToInt(V)); }));
		}
		Sections.Add(MoveTemp(S));
	}
	for (UWWOAbilityData* A : L->Abilities)
	{
		FWWOSettingSection S;
		S.Key = TEXT("ability/") + A->Id.ToString();
		S.Title = A->DisplayName;
		S.Rows.Add(Header(KindName(A->Kind) + TEXT(" · ") + A->Description));
		const FName Id = A->Id;
		if (Id == "overgrowth") S.Rows.Append({ Row(A, "SpeedMult", TEXT("Speed multiplier"), 1.f, 2.f, 0.05f), Row(A, "ExtraJumps", TEXT("Extra jumps"), 0.f, 3.f, 1.f) });
		if (Id == "thornfist") S.Rows.Append({
			Row(A, "MeleeDamageMult", TEXT("Melee damage multiplier"), 1.f, 5.f, 0.05f), Row(A, "Duration", TEXT("Duration (s)"), 1.f, 30.f, 0.5f),
			Row(A, "Cooldown", TEXT("Recharge time (s)"), 1.f, 120.f, 1.f), Header(TEXT("Electric discharge (when a boosted melee lands)")),
			Row(A, "DischargeArcs", TEXT("Lightning arcs"), 0.f, 16.f, 1.f), Row(A, "DischargeRadius", TEXT("Arc reach (m)"), 0.2f, 4.f, 0.05f),
			Row(A, "DischargeTime", TEXT("Crackle time (s)"), 0.05f, 1.5f, 0.05f) });
		if (Id == "static_charge") S.Rows.Append({
			Row(A, "ChargeDamage", TEXT("Damage taken to fill it"), 10.f, 1000.f, 10.f), Row(A, "ChainDamage", TEXT("Damage to the enemy you punch"), 0.f, 1000.f, 5.f),
			Row(A, "ChainJumps", TEXT("Jumps to more enemies"), 0.f, 20.f, 1.f), Row(A, "ChainRange", TEXT("Jump range (m)"), 1.f, 30.f, 0.5f),
			Row(A, "ChainFalloff", TEXT("Each jump does (of the last)"), 0.3f, 1.f, 0.05f, Percent()), Row(A, "ChainDelay", TEXT("Time between jumps (s)"), 0.f, 0.4f, 0.01f) });
		if (Id == "root_bombs") S.Rows.Append({
			Row(A, "Cooldown", TEXT("Recharge time (s)"), 1.f, 120.f, 1.f), Row(A, "BombDamage", TEXT("Damage per bomb (center)"), 0.f, 1000.f, 5.f),
			Row(A, "BombRadius", TEXT("Blast radius (m)"), 0.5f, 12.f, 0.25f), Row(A, "BombSpeed", TEXT("Throw speed (m/s)"), 5.f, 60.f, 1.f) });
		if (Id == "shield_share") S.Rows.Add(Row(A, "ShieldAmount", TEXT("Shield given per explosion"), 0.f, 300.f, 5.f));
		if (Id == "extra_grenade") S.Rows.Add(Row(A, "ExtraCharges", TEXT("Extra grenade charges"), 0.f, 3.f, 1.f));
		if (Id == "melee_guard") S.Rows.Append({ Row(A, "GuardReduction", TEXT("Melee damage cut by"), 0.f, 1.f, 0.05f, Percent()),
			Row(A, "Duration", TEXT("Lasts after a melee hit (s)"), 0.5f, 20.f, 0.5f) });
		if (Id == "spore_fog_damage") S.Rows.Add(Row(A, "FogDps", TEXT("Fog damage per second"), 0.f, 100.f, 1.f));
		if (Id == "spore_dash") S.Rows.Append({
			Row(A, "Cooldown", TEXT("Recharge time (s)"), 1.f, 120.f, 1.f), Row(A, "DashSpeed", TEXT("Dash speed backwards (m/s)"), 2.f, 40.f, 0.5f),
			Row(A, "DashUp", TEXT("Dash lift (m/s)"), 0.f, 15.f, 0.25f), Row(A, "DashFogRadius", TEXT("Fog radius (m)"), 0.5f, 12.f, 0.25f),
			Row(A, "DashFogTime", TEXT("Fog time (s)"), 0.5f, 30.f, 0.5f) });
		if (Id == "spore_patches") S.Rows.Append({
			Row(A, "PatchMax", TEXT("Patches per grenade"), 1.f, 10.f, 1.f), Row(A, "PatchRadius", TEXT("Fog radius per patch (m)"), 0.5f, 8.f, 0.25f),
			Row(A, "PatchTime", TEXT("Patch time (s)"), 0.5f, 30.f, 0.5f) });
		if (Id == "seedburst") S.Rows.Append({
			Row(A, "SubCount", TEXT("Seed count"), 1.f, 8.f, 1.f), Row(A, "SubScale", TEXT("Seed size (blast and damage)"), 0.1f, 1.f, 0.05f),
			Row(A, "SubSpread", TEXT("Seed spread (m/s)"), 1.f, 15.f, 0.5f), Row(A, "SubFuse", TEXT("Seed fuse (s)"), 0.1f, 3.f, 0.05f) });
		Sections.Add(MoveTemp(S));
	}
	for (UWWOTraitData* T : L->Traits)
	{
		FWWOSettingSection S;
		S.Key = TEXT("trait/") + T->Id.ToString();
		S.Title = T->DisplayName;
		S.Rows.Add(Header(T->Description));
		TWeakObjectPtr<UWWOTraitData> WeakT = T;
		for (int32 i = 0; i < T->Mods.Num(); ++i)
		{
			FWWOSettingRow R;
			R.Id = FName(*FString::Printf(TEXT("mod%d"), i)); // every mod edits "value", so each gets its own id
			R.Label = UWWOLoadoutSubsystem::StatName(T->Mods[i].Stat) + TEXT(" change");
			R.Min = -0.9f;
			R.Max = 2.f;
			R.Step = 0.01f;
			R.Show = SignedPercent();
			R.Get = [WeakT, i] { return WeakT.IsValid() && WeakT->Mods.IsValidIndex(i) ? WeakT->Mods[i].Value : 0.f; };
			R.Set = [WeakT, i](float V) { if (WeakT.IsValid() && WeakT->Mods.IsValidIndex(i)) WeakT->Mods[i].Value = V; };
			S.Rows.Add(R);
		}
		if (!T->Effect.IsNone() && T->Effect != "no_radar") S.Rows.Add(Row(T, "Amount", TEXT("Effect strength"), 0.f, 3.f, 0.05f, SignedPercent()));
		Sections.Add(MoveTemp(S));
	}
	for (UWWOGrenadeData* Gr : L->Grenades)
	{
		FWWOSettingSection S;
		S.Key = TEXT("grenade/") + Gr->Id.ToString();
		S.Title = Gr->DisplayName;
		TWeakObjectPtr<UWWOGrenadeComponent> WeakTh = Th;
		S.Rows = {
			Header(TEXT("Blast")),
			Row(Gr, "Damage", TEXT("Damage (center)"), 1.f, 500.f, 1.f),
			Row(Gr, "BlastRadius", TEXT("Blast radius (m)"), 0.5f, 12.f, 0.1f),
			Header(TEXT("Adjustments on top of the global grenade settings")),
			Row(Gr, "ThrowDistanceMult", TEXT("Throw distance"), 0.25f, 3.f, 0.05f, [WeakTh](float V) { return FString::Printf(TEXT("x%.2f = %.1f m"), V, WeakTh.IsValid() ? WeakTh->ThrowDistance * V : 0.f); }),
			Row(Gr, "RechargeMult", TEXT("Recharge time"), 0.25f, 3.f, 0.05f, [WeakTh](float V) { return FString::Printf(TEXT("x%.2f = %.1f s"), V, WeakTh.IsValid() ? WeakTh->RechargeTime * V : 0.f); }),
			Row(Gr, "FuseMult", TEXT("Detonation time"), 0.25f, 3.f, 0.05f, [WeakTh](float V) { return FString::Printf(TEXT("x%.2f = %.2f s"), V, WeakTh.IsValid() ? WeakTh->DetonationTime * V : 0.f); }),
			Row(Gr, "Bounce", TEXT("Bounciness"), 0.f, 1.f, 0.05f),
		};
		switch (Gr->Kind)
		{
		case EWWOGrenadeKind::Pool:
			S.Rows.Append({ Header(TEXT("Lingering pool (radius = blast radius)")), Row(Gr, "PoolDps", TEXT("Pool damage per second"), 0.f, 200.f, 1.f),
				Row(Gr, "PoolTime", TEXT("Pool time (s)"), 0.5f, 20.f, 0.5f) });
			break;
		case EWWOGrenadeKind::Wall:
			S.Rows.Append({ Header(TEXT("Wall")), Row(Gr, "WallWidth", TEXT("Wall width (m)"), 1.f, 12.f, 0.25f),
				Row(Gr, "WallHeight", TEXT("Wall height (m)"), 0.5f, 6.f, 0.1f), Row(Gr, "WallTime", TEXT("Wall lasts (s)"), 1.f, 60.f, 0.5f) });
			break;
		case EWWOGrenadeKind::Blind:
			S.Rows.Append({ Header(TEXT("Blinding")), Row(Gr, "BlindTime", TEXT("Blinds enemies for (s)"), 0.5f, 15.f, 0.25f) });
			break;
		case EWWOGrenadeKind::Tripmine:
			S.Rows.Append({ Header(TEXT("Slowing tripmine")), Row(Gr, "ArmTime", TEXT("Arms after (s)"), 0.f, 5.f, 0.05f),
				Row(Gr, "TriggerRadius", TEXT("Goes off within (m)"), 0.5f, 8.f, 0.25f), Row(Gr, "MineLife", TEXT("Lasts (s)"), 1.f, 120.f, 1.f),
				Row(Gr, "SlowAmount", TEXT("Slows enemies by"), 0.f, 0.95f, 0.05f, Percent()), Row(Gr, "SlowTime", TEXT("Slow time (s)"), 0.f, 15.f, 0.25f) });
			break;
		case EWWOGrenadeKind::Spike:
			S.Rows.Append({ Header(TEXT("Poison spike (damage = the direct hit)")), Row(Gr, "PoisonDps", TEXT("Poison damage per second"), 0.f, 200.f, 1.f),
				Row(Gr, "PoisonTime", TEXT("Poison time (s)"), 0.5f, 20.f, 0.5f), Row(Gr, "SpikeLife", TEXT("Bounces around for (s)"), 1.f, 20.f, 0.5f),
				Row(Gr, "SeekRadius", TEXT("Seeks targets within (m)"), 0.f, 20.f, 0.5f), Row(Gr, "SeekStrength", TEXT("Seek turn rate (degrees/s)"), 0.f, 720.f, 10.f) });
			break;
		default:
			break;
		}
		Sections.Add(MoveTemp(S));
	}
}

// --- Values, defaults, saving ----------------------------------------------------------------

void UWWOSettingsMenu::KeepHandler(UObject* Handler)
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

void UWWOSettingsMenu::MarkPreferences()
{
	for (FWWOSettingSection& S : Sections)
	{
		const TArray<FName>* Ids = S.Key.StartsWith(TEXT("global/")) ? OwnSettings().Find(S.Title) : nullptr;
		if (!Ids) continue;
		for (FWWOSettingRow& R : S.Rows)
		{
			if (R.Header.IsEmpty() && (Ids->Num() == 0 || Ids->Contains(R.Id))) R.bPref = true;
		}
	}
}

bool UWWOSettingsMenu::SectionAllowed(const FWWOSettingSection& S) const
{
	if (!bRestricted) return true;
	for (const FWWOSettingRow& R : S.Rows) if (R.Header.IsEmpty() && R.bPref) return true;
	for (const FWWOMenuButton& B : S.Buttons) if (B.bPref) return true;
	return false;
}

int32 UWWOSettingsMenu::ShownPageCount() const
{
	int32 Count = 0;
	for (const FPage& Page : Pages) if (Page.Label.IsValid()) ++Count;
	return Count;
}

FWWOSettingSection* UWWOSettingsMenu::Find(const FString& Key)
{
	return Sections.FindByPredicate([&Key](const FWWOSettingSection& S) { return S.Key == Key; });
}

const FWWOSettingSection* UWWOSettingsMenu::Find(const FString& Key) const
{
	return Sections.FindByPredicate([&Key](const FWWOSettingSection& S) { return S.Key == Key; });
}

const FWWOSettingRow* UWWOSettingsMenu::RowFor(const FString& Key, FName Id) const
{
	const FWWOSettingSection* S = Find(Key);
	return S ? S->Rows.FindByPredicate([Id](const FWWOSettingRow& R) { return R.Header.IsEmpty() && R.Id == Id; }) : nullptr;
}

float UWWOSettingsMenu::GetValue(const FString& Key, FName Id) const
{
	const FWWOSettingRow* R = RowFor(Key, Id);
	return R ? R->Get() : 0.f;
}

void UWWOSettingsMenu::SetValue(const FString& Key, FName Id, float Value)
{
	if (const FWWOSettingRow* R = RowFor(Key, Id))
	{
		R->Set(Value);
		SyncSliders();
	}
}

void UWWOSettingsMenu::RecordDefaults()
{
	for (const FWWOSettingSection& S : Sections)
	{
		TMap<FName, float>& Values = Defaults.FindOrAdd(S.Key);
		for (const FWWOSettingRow& R : S.Rows)
		{
			if (R.Header.IsEmpty()) Values.Add(R.Id, R.Get());
		}
	}
}

void UWWOSettingsMenu::ResetSection(const FString& Key)
{
	if (const TMap<FName, float>* Values = Defaults.Find(Key))
	{
		for (const auto& Pair : *Values)
		{
			if (const FWWOSettingRow* R = RowFor(Key, Pair.Key); R && Allowed(*R)) R->Set(Pair.Value);
		}
	}
	SyncSliders();
}

void UWWOSettingsMenu::ResetAll()
{
	for (const FWWOSettingSection& S : Sections) ResetSection(S.Key);
}

bool UWWOSettingsMenu::SaveDefaults()
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	if (bRestricted)
	{
		// In someone else's game only your own preferences are saved, into the file as it is: the
		// tuning in memory may be the host's (movement in PvP), and it isn't yours to keep. With no
		// file of your own yet, it starts as the build's shipped values (LoadSaved stops falling back
		// to those once your file exists).
		FString Old;
		TSharedPtr<FJsonObject> Parsed;
		if ((FFileHelper::LoadFileToString(Old, *SavePath()) || FFileHelper::LoadFileToString(Old, *(FPaths::ProjectConfigDir() / TEXT("ShippedSettings.json"))))
			&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Old), Parsed) && Parsed.IsValid()) Root = Parsed;
	}
	for (const FWWOSettingSection& S : Sections)
	{
		if (!SectionAllowed(S)) continue;
		const TSharedPtr<FJsonObject>* Existing = nullptr;
		TSharedRef<FJsonObject> Values = bRestricted && Root->TryGetObjectField(S.Key, Existing) ? (*Existing).ToSharedRef() : MakeShared<FJsonObject>();
		TMap<FName, float>& Recorded = Defaults.FindOrAdd(S.Key);
		for (const FWWOSettingRow& R : S.Rows)
		{
			if (!R.Header.IsEmpty() || !Allowed(R)) continue;
			const float V = R.Get();
			Values->SetNumberField(R.Id.ToString(), V);
			Recorded.Add(R.Id, V); // Reset now returns here
		}
		Root->SetObjectField(S.Key, Values);
	}
	FString Text;
	FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
	return FFileHelper::SaveStringToFile(Text, *SavePath());
}

void UWWOSettingsMenu::LoadSaved()
{
	// Your saved values, or in a fresh packaged build the ones it shipped with (Config/ShippedSettings.json).
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *SavePath()) && !FFileHelper::LoadFileToString(Text, *(FPaths::ProjectConfigDir() / TEXT("ShippedSettings.json")))) return;
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) return;
	for (const auto& SectionPair : Root->Values)
	{
		const TSharedPtr<FJsonObject>* Values;
		if (!SectionPair.Value->TryGetObject(Values)) continue;
		const FString SectionKey(*SectionPair.Key);
		for (const auto& ValuePair : (*Values)->Values)
		{
			FName Id(*ValuePair.Key);
			// "Gun kick" was split into vertical and horizontal: an old save's value becomes the vertical one.
			if (Id == "GunKick" && !(*Values)->HasField(TEXT("VerticalKick"))) Id = "VerticalKick";
			if (const FWWOSettingRow* R = RowFor(SectionKey, Id))
			{
				const float V = (float)ValuePair.Value->AsNumber();
				// Target health resets every target when set: in someone else's game they are the host's to reset.
				if (bRestricted && Id == "TargetHealth") continue;
				R->Set(V);
				Defaults.FindOrAdd(SectionKey).Add(Id, V);
			}
		}
	}
}

// --- Building the UI -------------------------------------------------------------------------

void UWWOSettingsMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	Build();
}

void UWWOSettingsMenu::Build()
{
	// The panels are built once the player exists (the sliders bind to its components).
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Root;
	SetCursor(EMouseCursor::None); // the menu draws Godot's ring cursor instead (NativePaint)
}

void UWWOSettingsMenu::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bOpen)
	{
		LiveTexts.RemoveAll([](const FLiveText& L) { return !L.Text.IsValid(); });
		for (const FLiveText& L : LiveTexts) L.Text->SetText(FText::FromString(L.Get()));
	}
	if (bOpen && SearchBox && SearchBox->HasKeyboardFocus() && FSlateApplication::IsInitialized())
	{
		// Clicking anywhere else gives the keyboard back to the game (nothing else here takes focus).
		FSlateApplication& App = FSlateApplication::Get();
		if (App.GetPressedMouseButtons().Contains(EKeys::LeftMouseButton) && !SearchBox->GetCachedGeometry().IsUnderLocation(App.GetCursorPos())) App.SetAllUserFocusToGameViewport();
	}
	if (SaveRevertAt > 0.0 && FPlatformTime::Seconds() >= SaveRevertAt)
	{
		SaveRevertAt = 0.0;
		if (UTextBlock* Label = SaveButton ? Cast<UTextBlock>(SaveButton->GetChildAt(0)) : nullptr) Label->SetText(FText::FromString(TEXT("Save as defaults")));
	}
}

int32 UWWOSettingsMenu::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!bOpen) return Layer;
	PaintCursor(this, AllottedGeometry, OutDrawElements, Layer + 1);
	return Layer + 1;
}

float UWWOSettingsMenu::GlobalScrollOffset() const
{
	const UScrollBox* Scroll = GlobalSwitcher ? Cast<UScrollBox>(GlobalSwitcher->GetActiveWidget()) : nullptr; // (an item page isn't one: 0)
	return Scroll ? Scroll->GetScrollOffset() : 0.f;
}

FString UWWOSettingsMenu::OpenItemKey() const
{
	for (const FWWOBrowser& B : Browsers)
	{
		bool bShown = true;
		for (UWidget* Child = B.Switcher; Child && Child->GetParent(); Child = Child->GetParent())
		{
			if (const UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(Child->GetParent())) bShown &= Switcher->GetActiveWidget() == Child;
		}
		if (bShown && B.Switcher->GetActiveWidgetIndex() == 1) return B.OpenKey;
	}
	return FString();
}


void UWWOSettingsMenu::CycleGlobalTab(int32 Step)
{
	// The bumpers and Q / E step through the tabs (past any with no pages: a joiner's menu has fewer).
	int32 Next = TopTab;
	for (int32 Tries = 0; Tries < NumTopTabs; ++Tries)
	{
		Next = ((Next + Step) % NumTopTabs + NumTopTabs) % NumTopTabs;
		if (LastPage.IsValidIndex(Next) && LastPage[Next] != INDEX_NONE) break;
	}
	SelectTopTab(Next);
}

FString UWWOSettingsMenu::ValueText(const FWWOSettingRow& Row, float V)
{
	if (Row.Show) return Row.Show(V);
	return Row.Step < 0.1f ? FString::Printf(TEXT("%.2f"), V) : FString::Printf(TEXT("%.1f"), V);
}

UWidget* UWWOSettingsMenu::MakeRow(const FString& Key, int32 RowIndex)
{
	// Two lines, so every slider gets the panel's full width: [label, fills] [value, right-aligned]
	// in the theme's 22 px font, then the slider (28 px tall) underneath. (Godot had them on one line:
	// label 270 px, slider, value.)
	const FWWOSettingRow& Row = Find(Key)->Rows[RowIndex];
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* LabelSlot = Line->AddChildToHorizontalBox(MakeText(Row.Label, 22.f));
	LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	LabelSlot->SetVerticalAlignment(VAlign_Center);
	Root->AddChildToVerticalBox(Line);

	// The slider: Godot's theme draws a 6 px track (0.3, 0.32, 0.37) filled up to the grabber
	// (0.55, 0.6, 0.7), with the default round grabber. A progress bar under the slider draws
	// the track and the fill; the slider itself draws only the grabber.
	const float Thumb = 16.f * K;
	UOverlay* SliderBox = WidgetTree->ConstructWidget<UOverlay>();
	UProgressBar* Fill = WidgetTree->ConstructWidget<UProgressBar>();
	FProgressBarStyle BarStyle;
	BarStyle.SetBackgroundImage(RoundBrush(FLinearColor(0.3f, 0.32f, 0.37f), 3.f));
	BarStyle.SetFillImage(RoundBrush(FLinearColor(0.55f, 0.6f, 0.7f), 3.f));
	BarStyle.SetMarqueeImage(RoundBrush(FLinearColor(0.55f, 0.6f, 0.7f), 3.f));
	Fill->SetWidgetStyle(BarStyle);
	Fill->SetFillColorAndOpacity(FLinearColor::White);
	Fill->SetPercent(FMath::GetRangePct(Row.Min, Row.Max, Row.Get()));
	USizeBox* FillBox = WidgetTree->ConstructWidget<USizeBox>();
	FillBox->SetHeightOverride(6.f * K);
	FillBox->AddChild(Fill);
	UOverlaySlot* FillSlot = SliderBox->AddChildToOverlay(FillBox);
	FillSlot->SetHorizontalAlignment(HAlign_Fill);
	FillSlot->SetVerticalAlignment(VAlign_Center);
	FillSlot->SetPadding(FMargin(Thumb * 0.5f, 0.f)); // the grabber's center travels from end to end of the fill

	USlider* Slider = WidgetTree->ConstructWidget<USlider>();
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Slider->IsFocusable = false;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	Slider->SetMinValue(Row.Min);
	Slider->SetMaxValue(Row.Max);
	Slider->SetStepSize(Row.Step);
	Slider->SetValue(Row.Get());
	FSliderStyle Style = Slider->GetWidgetStyle();
	FSlateBrush NoBar;
	NoBar.DrawAs = ESlateBrushDrawType::NoDrawType;
	FSlateBrush Grabber = RoundBrush(FLinearColor(0.88f, 0.88f, 0.9f), 8.f);
	Grabber.ImageSize = FVector2D(Thumb, Thumb);
	FSlateBrush GrabberHover = RoundBrush(FLinearColor(1.f, 1.f, 1.f), 8.f);
	GrabberHover.ImageSize = Grabber.ImageSize;
	Style.SetNormalBarImage(NoBar).SetHoveredBarImage(NoBar).SetDisabledBarImage(NoBar);
	Style.SetNormalThumbImage(Grabber).SetHoveredThumbImage(GrabberHover).SetDisabledThumbImage(Grabber);
	Slider->SetWidgetStyle(Style);
	Slider->SetSliderBarColor(FLinearColor::White);
	Slider->SetSliderHandleColor(FLinearColor::White);
	USizeBox* SliderHeight = WidgetTree->ConstructWidget<USizeBox>();
	SliderHeight->SetHeightOverride(28.f * K);
	SliderHeight->AddChild(Slider);
	UOverlaySlot* SliderSlot = SliderBox->AddChildToOverlay(SliderHeight);
	SliderSlot->SetHorizontalAlignment(HAlign_Fill);
	SliderSlot->SetVerticalAlignment(VAlign_Center);
	Root->AddChildToVerticalBox(SliderBox)->SetPadding(FMargin(0.f, 2.f * K, 0.f, 0.f)); // the whole width, under the label

	USizeBox* ValueBox = WidgetTree->ConstructWidget<USizeBox>();
	ValueBox->SetMinDesiredWidth((Row.Show ? 170.f : 80.f) * K);
	UTextBlock* Value = MakeText(ValueText(Row, Row.Get()), 22.f);
	Value->SetJustification(ETextJustify::Right);
	ValueBox->AddChild(Value);
	UHorizontalBoxSlot* ValueSlot = Line->AddChildToHorizontalBox(ValueBox);
	ValueSlot->SetVerticalAlignment(VAlign_Center);
	ValueSlot->SetPadding(FMargin(12.f * K, 0.f, 0.f, 0.f));

	UWWOSliderHandler* H = NewObject<UWWOSliderHandler>(this);
	TWeakObjectPtr<UTextBlock> WeakValue = Value;
	TWeakObjectPtr<UProgressBar> WeakFill = Fill;
	const FWWOSettingRow RowCopy = Row;
	H->Fn = [RowCopy, WeakValue, WeakFill](float V)
	{
		if (RowCopy.Step > 0.f) V = RowCopy.Min + FMath::RoundToFloat((V - RowCopy.Min) / RowCopy.Step) * RowCopy.Step; // snap to the step
		V = FMath::Clamp(V, RowCopy.Min, RowCopy.Max);
		RowCopy.Set(V);
		if (WeakValue.IsValid()) WeakValue->SetText(FText::FromString(ValueText(RowCopy, V)));
		if (WeakFill.IsValid()) WeakFill->SetPercent(FMath::GetRangePct(RowCopy.Min, RowCopy.Max, V));
	};
	Slider->OnValueChanged.AddDynamic(H, &UWWOSliderHandler::Handle);
	H->For = Slider;
	KeepHandler(H);
	Live.Add({ Slider, Value, Fill, Key, RowIndex });
	return Root;
}

UWidget* UWWOSettingsMenu::MakeRowFor(const FString& Key, FName Id)
{
	const FWWOSettingSection* S = Find(Key);
	if (!S) return nullptr;
	const int32 Index = S->Rows.IndexOfByPredicate([Id](const FWWOSettingRow& Row) { return Row.Header.IsEmpty() && Row.Id == Id; });
	return Index == INDEX_NONE ? nullptr : MakeRow(Key, Index);
}

void UWWOSettingsMenu::BuildRows(UPanelWidget* Container, const FString& Key)
{
	// Godot: a 12 px margin around the rows, 8 px between them; headers in 18 px at 55%.
	FWWOSettingSection* S = Find(Key);
	if (!S) return;
	auto Add = [Container](UWidget* W, bool bFirst, bool bLeft)
	{
		const FMargin Pad(12.f * K, (bFirst ? 12.f : 8.f) * K, 12.f * K, 0.f);
		if (UScrollBoxSlot* Slot = Cast<UScrollBoxSlot>(Container->AddChild(W)))
		{
			Slot->SetPadding(Pad);
			if (bLeft) Slot->SetHorizontalAlignment(HAlign_Left);
		}
	};
	// Each header with sliders under it is a collapsible group: click it to open (+) or close (-). The first
	// group of a section starts open, the rest closed; what you open or close stays that way while
	// the game runs. A header with no sliders under it (a weapon's archetype) stays a plain label.
	// In someone else's game: only your own preferences, and the headers that still have some under them.
	TArray<int32> Kept;
	for (int32 i = 0; i < S->Rows.Num(); ++i)
	{
		const FWWOSettingRow& R = S->Rows[i];
		bool bKeep = Allowed(R);
		if (!bKeep && !R.Header.IsEmpty())
		{
			for (int32 j = i + 1; j < S->Rows.Num() && S->Rows[j].Header.IsEmpty(); ++j) bKeep |= S->Rows[j].bPref;
		}
		if (bKeep) Kept.Add(i);
	}
	int32 Count = 0;
	UVerticalBox* Group = nullptr;
	for (int32 k = 0; k < Kept.Num(); ++k, ++Count)
	{
		const int32 i = Kept[k];
		const FWWOSettingRow& R = S->Rows[i];
		if (R.Header.IsEmpty())
		{
			UWidget* Row = MakeRow(Key, i);
			if (Group) Group->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 8.f * K, 0.f, 0.f));
			else Add(Row, Count == 0, false);
			continue;
		}
		const bool bHasRows = Kept.IsValidIndex(k + 1) && S->Rows[Kept[k + 1]].Header.IsEmpty();
		if (R.LiveHeader)
		{
			UTextBlock* Text = MakeText(R.LiveHeader(), 20.f, FLinearColor(1, 1, 1, 0.9f), true);
			LiveTexts.Add({ Text, R.LiveHeader });
			Add(Text, Count == 0, false);
			Group = nullptr;
			continue;
		}
		if (!bHasRows)
		{
			Add(MakeText(R.Header, 18.f, FLinearColor(1, 1, 1, 0.55f), true), Count == 0, false);
			Group = nullptr;
			continue;
		}
		const FString GroupKey = Key + TEXT("|") + R.Header;
		const bool bFirst = Group == nullptr && !FirstGroupSeen.Contains(Key);
		FirstGroupSeen.Add(Key);
		if (bFirst && !GroupState.Contains(GroupKey)) GroupState.Add(GroupKey, true);
		const bool bGroupOpen = GroupState.FindRef(GroupKey);
		Group = WidgetTree->ConstructWidget<UVerticalBox>();
		Group->SetVisibility(bGroupOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		TWeakObjectPtr<UVerticalBox> WeakGroup = Group;
		TWeakObjectPtr<UWWOSettingsMenu> WeakThis = this;
		const FString Title = R.Header;
		TSharedRef<TWeakObjectPtr<UButton>> Self = MakeShared<TWeakObjectPtr<UButton>>();
		UButton* Toggle = MakeButton(this, (bGroupOpen ? TEXT("-  ") : TEXT("+  ")) + Title, [WeakThis, WeakGroup, GroupKey, Title, Self]
		{
			if (!WeakThis.IsValid() || !WeakGroup.IsValid()) return;
			const bool bNow = !WeakThis->GroupState.FindRef(GroupKey);
			WeakThis->GroupState.Add(GroupKey, bNow);
			WeakGroup->SetVisibility(bNow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			if (UTextBlock* Label = Self->IsValid() ? Cast<UTextBlock>(Self->Get()->GetChildAt(0)) : nullptr)
			{
				Label->SetText(FText::FromString((bNow ? TEXT("-  ") : TEXT("+  ")) + Title));
			}
		}, 18.f, FLinearColor(0.15f, 0.16f, 0.19f));
		*Self = Toggle;
		Add(Toggle, Count == 0, true);
		Add(Group, false, false);
	}
	for (const FWWOMenuButton& Button : S->Buttons)
	{
		if (bRestricted && !Button.bPref) continue;
		Add(MakeButton(this, Button.Text, Button.Fn, 22.f), Count++ == 0, true);
	}
	if (UScrollBoxSlot* Last = Container->GetChildrenCount() > 0 ? Cast<UScrollBoxSlot>(Container->GetChildAt(Container->GetChildrenCount() - 1)->Slot) : nullptr)
	{
		FMargin Pad = Last->GetPadding();
		Pad.Bottom = 12.f * K;
		Last->SetPadding(Pad);
	}
}

void UWWOSettingsMenu::SyncSliders()
{
	// Push current values back into whatever sliders are on screen (after a reset).
	Live.RemoveAll([](const FLiveSlider& L) { return !L.Slider.IsValid(); });
	for (const FLiveSlider& L : Live)
	{
		const FWWOSettingSection* S = Find(L.Key);
		if (!S || !S->Rows.IsValidIndex(L.Row)) continue;
		const FWWOSettingRow& Row = S->Rows[L.Row];
		const float V = Row.Get();
		L.Slider->SetValue(V);
		if (L.Value.IsValid()) L.Value->SetText(FText::FromString(ValueText(Row, V)));
		if (L.Fill.IsValid()) L.Fill->SetPercent(FMath::GetRangePct(Row.Min, Row.Max, V));
	}
}

// --- Item browsers (list of items <-> one item's sliders) ------------------------------------

UWidget* UWWOSettingsMenu::MakeBrowser(TFunction<TArray<FWWOBrowserItem>()> Provider)
{
	const int32 Index = Browsers.AddDefaulted();
	FWWOBrowser& B = Browsers[Index];
	B.Provider = MoveTemp(Provider);
	B.Switcher = WidgetTree->ConstructWidget<UWidgetSwitcher>();
	B.List = WidgetTree->ConstructWidget<UScrollBox>();
	B.Switcher->AddChild(B.List);
	UVerticalBox* Detail = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBox* Bar = WidgetTree->ConstructWidget<UHorizontalBox>();
	TWeakObjectPtr<UWWOSettingsMenu> WeakThis = this;
	Bar->AddChildToHorizontalBox(MakeButton(this, TEXT("‹ Back"), [WeakThis, Index] { if (WeakThis.IsValid()) WeakThis->ShowList(Index); }, 22.f));
	B.Title = MakeText(TEXT(""), 26.f);
	UHorizontalBoxSlot* TitleSlot = Bar->AddChildToHorizontalBox(B.Title);
	TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TitleSlot->SetPadding(FMargin(12.f * K, 0.f));
	TitleSlot->SetVerticalAlignment(VAlign_Center);
	Bar->AddChildToHorizontalBox(MakeButton(this, TEXT("Reset item"), [WeakThis, Index]
	{
		if (WeakThis.IsValid()) WeakThis->ResetSection(WeakThis->Browsers[Index].OpenKey);
	}, 22.f));
	Detail->AddChildToVerticalBox(Bar);
	B.Rows = WidgetTree->ConstructWidget<UScrollBox>();
	Detail->AddChildToVerticalBox(B.Rows)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	B.Switcher->AddChild(Detail);
	ShowList(Index);
	return B.Switcher;
}

void UWWOSettingsMenu::ShowList(int32 BrowserIndex)
{
	FWWOBrowser& B = Browsers[BrowserIndex];
	B.List->ClearChildren();
	TWeakObjectPtr<UWWOSettingsMenu> WeakThis = this;
	for (const FWWOBrowserItem& Item : B.Provider())
	{
		const FString Text = Item.Title + (Item.Subtitle.IsEmpty() ? FString() : TEXT("\n") + Item.Subtitle);
		const FString Key = Item.Key;
		// Godot: left-aligned, at least 72 px tall, 20 px side padding, 10 px apart, 12 px page margin.
		UButton* Button = MakeButton(this, Text, [WeakThis, BrowserIndex, Key] { if (WeakThis.IsValid()) WeakThis->OpenItem(BrowserIndex, Key); },
			22.f, FLinearColor(0.18f, 0.19f, 0.23f), true, FMargin(20.f, 5.f));
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
		Box->SetMinDesiredHeight(72.f * K);
		Box->AddChild(Button);
		const bool bFirst = B.List->GetChildrenCount() == 0;
		if (UScrollBoxSlot* Slot = Cast<UScrollBoxSlot>(B.List->AddChild(Box))) Slot->SetPadding(FMargin(12.f * K, (bFirst ? 12.f : 10.f) * K, 12.f * K, 0.f));
	}
	B.Switcher->SetActiveWidgetIndex(0);
}

void UWWOSettingsMenu::OpenItem(int32 BrowserIndex, const FString& Key)
{
	FWWOBrowser& B = Browsers[BrowserIndex];
	const FWWOSettingSection* S = Find(Key);
	if (!S) return;
	B.OpenKey = Key;
	B.Title->SetText(FText::FromString(S->Title));
	B.Rows->ClearChildren();
	BuildRows(B.Rows, Key);
	B.Rows->ScrollToStart();
	B.Switcher->SetActiveWidgetIndex(1);
}

// --- Opening, closing, navigation -------------------------------------------------------------

void UWWOSettingsMenu::Prepare()
{
	// Registers every section (applying the saved defaults) and builds the panels. Runs as
	// soon as the player exists, so saved values apply at startup, not just once opened.
	AWWOCharacter* P = GetOwningPlayer() ? Cast<AWWOCharacter>(GetOwningPlayer()->GetPawn()) : nullptr;
	if (!bBuilt && P)
	{
		bBuilt = true;
		// Someone else's game: the tuning is the host's (see the header). Your own menu, alone or hosting, has everything.
		bRestricted = P->GetNetMode() == NM_Client;
		RegisterSections(P);
		MarkPreferences();
		RecordDefaults();
		LoadSaved();
		UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(P);
		UOverlay* Root = Cast<UOverlay>(WidgetTree->RootWidget);
		TWeakObjectPtr<UWWOSettingsMenu> WeakThis = this;
		TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
		const FLinearColor Accent = L && L->GetFlora() ? WWO::Lightened(L->GetFlora()->Color, 0.15f) : FLinearColor(0.72f, 0.5f, 1.f);
		auto Rule = [this](const FLinearColor& Color) -> UWidget*
		{
			UBorder* Line = WidgetTree->ConstructWidget<UBorder>();
			Line->SetBrushColor(WWO::Lin(Color));
			USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
			Box->SetHeightOverride(1.5f);
			Box->AddChild(Line);
			return Box;
		};

		// The whole window, in the character menu's style. A little see-through, so a slider's
		// effect on the game behind (field of view, the HUD) can still be watched.
		UBorder* Page = WidgetTree->ConstructWidget<UBorder>();
		Page->SetBrushColor(WWO::Lin(FLinearColor(0.1f, 0.11f, 0.13f, 0.95f)));
		Page->SetPadding(FMargin(40.f * K, 12.f * K, 40.f * K, 28.f * K));
		UOverlaySlot* PageSlot = Root->AddChildToOverlay(Page);
		PageSlot->SetHorizontalAlignment(HAlign_Fill);
		PageSlot->SetVerticalAlignment(VAlign_Fill);
		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
		Page->SetContent(Column);

		// Header: the name, the five tabs (bumpers or Q / E), the search box.
		UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
		UHorizontalBox* Name = WidgetTree->ConstructWidget<UHorizontalBox>();
		Name->AddChildToHorizontalBox(Spaced(TEXT("//"), 26.f, FLinearColor(0.72f, 0.5f, 1.f), 0))->SetVerticalAlignment(VAlign_Center);
		UHorizontalBoxSlot* NameText = Name->AddChildToHorizontalBox(Spaced(TEXT("SETTINGS"), 17.f, FLinearColor::White, 420));
		NameText->SetVerticalAlignment(VAlign_Center);
		NameText->SetPadding(FMargin(18.f * K, 0, 0, 0));
		UHorizontalBoxSlot* NameSlot = Header->AddChildToHorizontalBox(Name);
		NameSlot->SetSize(WWOFillSize(1.f));
		NameSlot->SetVerticalAlignment(VAlign_Center);
		UHorizontalBox* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>();
		for (int32 i = 0; i < NumTopTabs; ++i)
		{
			if (i > 0)
			{
				UHorizontalBoxSlot* Bar = Tabs->AddChildToHorizontalBox(Spaced(TEXT("|"), 15.f, FLinearColor(1, 1, 1, 0.25f), 0));
				Bar->SetVerticalAlignment(VAlign_Center);
				Bar->SetPadding(FMargin(26.f * K, 0));
			}
			UVerticalBox* Tab = WidgetTree->ConstructWidget<UVerticalBox>();
			UTextBlock* Label = Spaced(TopTabNames[i], 17.f, FLinearColor::White, 180);
			Tab->AddChildToVerticalBox(Label)->SetPadding(FMargin(8.f * K, 6.f * K, 4.f * K, 6.f * K));
			UBorder* Line = WidgetTree->ConstructWidget<UBorder>();
			USizeBox* LineBox = WidgetTree->ConstructWidget<USizeBox>();
			LineBox->SetHeightOverride(3.f * K);
			LineBox->AddChild(Line);
			Tab->AddChildToVerticalBox(LineBox);
			TopLabels.Add(Label);
			TopLines.Add(Line);
			UButton* TabButton = PlainButton(Tab, [WeakThis, i] { if (WeakThis.IsValid()) WeakThis->SelectTopTab(i); });
			Tabs->AddChildToHorizontalBox(TabButton)->SetVerticalAlignment(VAlign_Center);
			TopTabWidgets.Add(TabButton);
			TopTabWidgets.Add(i > 0 ? Tabs->GetChildAt(Tabs->GetChildrenCount() - 2) : nullptr);
		}
		Header->AddChildToHorizontalBox(Tabs)->SetVerticalAlignment(VAlign_Center);
		// Search: type part of a setting's name to list every match from every tab.
		SearchBox = WidgetTree->ConstructWidget<UEditableTextBox>();
		FEditableTextBoxStyle BoxStyle = SearchBox->WidgetStyle;
		const FSlateBrush BoxBrush = RoundBrush(FLinearColor(0.115f, 0.125f, 0.148f), 5.f, FLinearColor(1, 1, 1, 0.45f), 1.5f);
		BoxStyle.SetBackgroundImageNormal(BoxBrush).SetBackgroundImageHovered(BoxBrush).SetBackgroundImageReadOnly(BoxBrush);
		BoxStyle.SetBackgroundImageFocused(RoundBrush(FLinearColor(0.115f, 0.125f, 0.148f), 5.f, Accent, 1.5f));
		BoxStyle.SetBackgroundColor(FLinearColor::White).SetForegroundColor(FLinearColor::White).SetFocusedForegroundColor(FLinearColor::White);
		BoxStyle.TextStyle.SetFont(Font(15.f)).SetColorAndOpacity(FSlateColor(FLinearColor::White));
		BoxStyle.SetPadding(FMargin(12.f * K, 6.f * K));
		SearchBox->WidgetStyle = BoxStyle;
		SearchBox->SetHintText(FText::FromString(TEXT("SEARCH SETTINGS")));
		SearchBox->OnTextChanged.AddDynamic(this, &UWWOSettingsMenu::OnSearchChanged);
		SearchBox->OnTextCommitted.AddDynamic(this, &UWWOSettingsMenu::OnSearchCommitted);
		USizeBox* SearchSize = WidgetTree->ConstructWidget<USizeBox>();
		SearchSize->SetWidthOverride(250.f * K);
		SearchSize->AddChild(SearchBox);
		UHorizontalBox* RightSide = WidgetTree->ConstructWidget<UHorizontalBox>();
		RightSide->AddChildToHorizontalBox(SearchSize)->SetVerticalAlignment(VAlign_Center);
		UHorizontalBoxSlot* RightSlot = Header->AddChildToHorizontalBox(RightSide);
		RightSlot->SetSize(WWOFillSize(1.f));
		RightSlot->SetHorizontalAlignment(HAlign_Right);
		RightSlot->SetVerticalAlignment(VAlign_Center);
		Column->AddChildToVerticalBox(Header);
		Column->AddChildToVerticalBox(Rule(FLinearColor(1, 1, 1, 0.3f)))->SetPadding(FMargin(0, 4.f * K, 0, 0));

		UHorizontalBox* Body = WidgetTree->ConstructWidget<UHorizontalBox>();
		Column->AddChildToVerticalBox(Body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		// Every page: the global sections, then the item browsers, then the search results.
		GlobalSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>();
		// Which tab each global section sits under, in the order its sidebar lists them. One that
		// isn't named here lands under Player.
		static const TArray<TArray<FString>> Layout = {
			{ TEXT("Walk / Sprint"), TEXT("Jump"), TEXT("Crouch / Slide"), TEXT("Camera"), TEXT("HUD"), TEXT("Audio"), TEXT("Player"), TEXT("Aim Assist") },
			{ TEXT("Melee"), TEXT("Grenades"), TEXT("Ult"), TEXT("Targets") },
			{ TEXT("Soldier"), TEXT("Swarm"), TEXT("Tank"), TEXT("Bots"), TEXT("Allies") },
			{ TEXT("Maps"), TEXT("Multiplayer"), TEXT("PvP"), TEXT("Waves") },
		};
		for (const FString& Key : GlobalKeys)
		{
			const FString Title = Find(Key)->Title;
			int32 Tab = 0;
			for (int32 t = 0; t < Layout.Num(); ++t) if (Layout[t].Contains(Title)) Tab = t;
			UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
			BuildRows(Scroll, Key);
			GlobalSwitcher->AddChild(Scroll);
			Pages.Add({ Title, Tab });
		}
		auto AddItems = [&](const TCHAR* Title, TFunction<TArray<FWWOBrowserItem>()> Provider)
		{
			GlobalSwitcher->AddChild(MakeBrowser(MoveTemp(Provider)));
			Pages.Add({ Title, ItemsTab });
		};
		AddItems(TEXT("Equipped"), [WL]
		{
			TArray<FWWOBrowserItem> Items;
			if (!WL.IsValid()) return Items;
			if (UWWOGrenadeData* G = WL->GetGrenade()) Items.Add({ TEXT("grenade/") + G->Id.ToString(), G->DisplayName, TEXT("Equipped grenade") });
			for (int32 Slot = 0; Slot < UWWOLoadoutSubsystem::AbilitySlots; ++Slot)
			{
				if (UWWOAbilityData* A = WL->GetAbility(Slot)) Items.Add({ TEXT("ability/") + A->Id.ToString(), A->DisplayName, FString::Printf(TEXT("Equipped ability %d"), Slot + 1) });
			}
			if (UWWOWeaponData* W = WL->GetWeapon()) Items.Add({ TEXT("weapon/") + W->Id.ToString(), W->DisplayName, TEXT("Equipped weapon") });
			for (const FName& Slot : UWWOLoadoutSubsystem::FrameSlots())
			{
				if (UWWOFramePartData* Part = WL->GetFramePart(Slot)) Items.Add({ TEXT("frame/") + Part->Id.ToString(), Part->DisplayName, TEXT("Equipped ") + Slot.ToString() });
			}
			return Items;
		});
		AddItems(TEXT("Grenade Types"), [WL]
		{
			TArray<FWWOBrowserItem> Items;
			if (WL.IsValid()) for (UWWOGrenadeData* G : WL->Grenades) Items.Add({ TEXT("grenade/") + G->Id.ToString(), G->DisplayName, G == WL->GetGrenade() ? TEXT("Equipped") : TEXT("") });
			return Items;
		});
		AddItems(TEXT("Abilities"), [WL]
		{
			TArray<FWWOBrowserItem> Items;
			if (!WL.IsValid()) return Items;
			for (UWWOAbilityData* A : WL->Abilities)
			{
				const int32 Slot = WL->AbilitySlotOf(A);
				Items.Add({ TEXT("ability/") + A->Id.ToString(), A->DisplayName, KindName(A->Kind) + (Slot != INDEX_NONE ? FString::Printf(TEXT(" · equipped in slot %d"), Slot + 1) : FString()) });
			}
			return Items;
		});
		AddItems(TEXT("Traits"), [WL]
		{
			TArray<FWWOBrowserItem> Items;
			if (!WL.IsValid()) return Items;
			for (UWWOTraitData* T : WL->Traits)
			{
				Items.Add({ TEXT("trait/") + T->Id.ToString(), T->DisplayName, Capitalize(T->Flora.ToString()) + (WL->IsTraitEquipped(T) ? TEXT(" · equipped") : TEXT("")) });
			}
			return Items;
		});
		AddItems(TEXT("Weapons"), [WL]
		{
			TArray<FWWOBrowserItem> Items;
			if (WL.IsValid()) for (UWWOWeaponData* W : WL->Weapons) Items.Add({ TEXT("weapon/") + W->Id.ToString(), W->DisplayName, W->Archetype + (W == WL->GetWeapon() ? TEXT(" · equipped") : TEXT("")) });
			return Items;
		});
		AddItems(TEXT("Shell"), [WL]
		{
			TArray<FWWOBrowserItem> Items;
			if (!WL.IsValid()) return Items;
			for (UWWOFramePartData* Part : WL->FrameParts)
			{
				Items.Add({ TEXT("frame/") + Part->Id.ToString(), Part->DisplayName, Capitalize(Part->Slot.ToString()) + (Part == WL->GetFramePart(Part->Slot) ? TEXT(" · equipped") : TEXT("")) });
			}
			return Items;
		});
		// Controls: the key bindings.
		BindsPage = GlobalSwitcher->GetChildrenCount();
		BindsList = WidgetTree->ConstructWidget<UScrollBox>();
		GlobalSwitcher->AddChild(BindsList);
		Pages.Add({ TEXT("Key Bindings"), ControlsTab });
		BuildBinds();
		SearchPage = GlobalSwitcher->GetChildrenCount();
		SearchResults = WidgetTree->ConstructWidget<UScrollBox>();
		GlobalSwitcher->AddChild(SearchResults);

		// Left: the open tab's pages, then the menu's own buttons at the bottom.
		UVerticalBox* Side = WidgetTree->ConstructWidget<UVerticalBox>();
		SideSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>();
		LastPage.Init(INDEX_NONE, NumTopTabs);
		for (int32 Tab = 0; Tab < NumTopTabs; ++Tab)
		{
			UVerticalBox* List = WidgetTree->ConstructWidget<UVerticalBox>();
			TArray<int32> Order;
			if (Layout.IsValidIndex(Tab))
			{
				for (const FString& Title : Layout[Tab]) for (int32 p = 0; p < Pages.Num(); ++p) if (Pages[p].Tab == Tab && Pages[p].Title == Title) Order.Add(p);
			}
			for (int32 p = 0; p < Pages.Num(); ++p) if (Pages[p].Tab == Tab && !Order.Contains(p)) Order.Add(p);
			for (const int32 p : Order)
			{
				// In someone else's game: only the pages with something of yours on them, and the key bindings.
				if (bRestricted && p != BindsPage && !(GlobalKeys.IsValidIndex(p) && SectionAllowed(*Find(GlobalKeys[p])))) continue;
				UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
				UBorder* Mark = WidgetTree->ConstructWidget<UBorder>();
				USizeBox* MarkBox = WidgetTree->ConstructWidget<USizeBox>();
				MarkBox->SetWidthOverride(3.f * K);
				MarkBox->AddChild(Mark);
				Row->AddChildToHorizontalBox(MarkBox)->SetPadding(FMargin(0, 3.f * K));
				UTextBlock* Label = Spaced(Pages[p].Title, 15.f, FLinearColor::White, 170);
				UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(Label);
				LabelSlot->SetPadding(FMargin(16.f * K, 9.f * K, 8.f * K, 9.f * K));
				LabelSlot->SetVerticalAlignment(VAlign_Center);
				Pages[p].Label = Label;
				Pages[p].Mark = Mark;
				List->AddChildToVerticalBox(PlainButton(Row, [WeakThis, p] { if (WeakThis.IsValid()) WeakThis->SelectPage(p); }))->SetPadding(FMargin(0, 2.f * K));
				if (LastPage[Tab] == INDEX_NONE) LastPage[Tab] = p;
			}
			SideSwitcher->AddChild(List);
		}
		Side->AddChildToVerticalBox(SideSwitcher)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		if (bRestricted)
		{
			// The tabs with nothing left on them go, with the bar beside each.
			bool bFirstShown = true;
			for (int32 Tab = 0; Tab < NumTopTabs; ++Tab)
			{
				const bool bShown = LastPage[Tab] != INDEX_NONE;
				if (UWidget* Button = TopTabWidgets[Tab * 2]) Button->SetVisibility(bShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
				if (UWidget* Bar = TopTabWidgets[Tab * 2 + 1]) Bar->SetVisibility(bShown && !bFirstShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
				bFirstShown &= !bShown;
			}
			UTextBlock* Note = Spaced(TEXT("You're in someone else's game: these are your own settings. The game's tuning is the host's."), 10.f, FLinearColor(1, 1, 1, 0.5f), 90);
			Note->SetAutoWrapText(true);
			Side->AddChildToVerticalBox(Note)->SetPadding(FMargin(2.f * K, 0, 0, 6.f * K));
		}
		auto SideButton = [&](const FString& Text, TFunction<void()> Fn) -> UButton*
		{
			UButton* B = MakeButton(this, Text, MoveTemp(Fn), 15.f, FLinearColor(0.115f, 0.125f, 0.148f), false, FMargin(10.f, 8.f));
			FButtonStyle Style = B->GetStyle();
			Style.SetNormal(RoundBrush(FLinearColor(0.115f, 0.125f, 0.148f), 5.f, FLinearColor(1, 1, 1, 0.45f), 1.5f));
			Style.SetHovered(RoundBrush(FLinearColor(0.17f, 0.18f, 0.21f), 5.f, FLinearColor(1, 1, 1, 0.9f), 1.5f));
			Style.SetPressed(RoundBrush(FLinearColor(0.22f, 0.23f, 0.26f), 5.f, FLinearColor::White, 1.5f));
			B->SetStyle(Style);
			Side->AddChildToVerticalBox(B)->SetPadding(FMargin(0, 8.f * K, 0, 0));
			return B;
		};
		SideButton(TEXT("Reset this page"), [WeakThis]
		{
			if (!WeakThis.IsValid()) return;
			// A global page resets its section; an item page, the item that's open.
			if (WeakThis->CurrentPage == WeakThis->BindsPage)
			{
				if (AWWOPlayerController* PC = Cast<AWWOPlayerController>(WeakThis->GetOwningPlayer())) PC->ResetBinds();
				WeakThis->BuildBinds();
			}
			else if (WeakThis->GlobalKeys.IsValidIndex(WeakThis->CurrentPage)) WeakThis->ResetSection(WeakThis->GlobalKeys[WeakThis->CurrentPage]);
			else if (!WeakThis->OpenItemKey().IsEmpty()) WeakThis->ResetSection(WeakThis->OpenItemKey());
		});
		SideButton(TEXT("Reset all"), [WeakThis] { if (WeakThis.IsValid()) WeakThis->ResetAll(); });
		SaveButton = SideButton(TEXT("Save as defaults"), [WeakThis]
		{
			if (!WeakThis.IsValid()) return;
			const bool bSaved = WeakThis->SaveDefaults();
			if (UTextBlock* Label = Cast<UTextBlock>(WeakThis->SaveButton->GetChildAt(0)))
			{
				Label->SetText(FText::FromString(bSaved ? TEXT("Saved ✓") : TEXT("Save failed")));
			}
			WeakThis->SaveRevertAt = FPlatformTime::Seconds() + 1.5; // Godot's 1.5 s timer (real time: the game is paused)
		});
		TWeakObjectPtr<APlayerController> WeakPC = GetOwningPlayer();
		// Out of the match, back to the launch screen: hosting, that ends it for everyone; in someone
		// else's PvP game a bot takes your place there; alone it just ends. (Not on the launch screen itself.)
		const AWWOPlayerController* OwnPC = Cast<AWWOPlayerController>(GetOwningPlayer());
		if (!OwnPC || !OwnPC->GetLaunchMenu())
		{
			SideButton(TEXT("Quit to main menu"), [WeakPC]
			{
				if (UWWONetSubsystem* N = WeakPC.IsValid() ? UWWONetSubsystem::Get(WeakPC.Get()) : nullptr) N->LeaveToTitle();
			});
		}
		SideButton(TEXT("Close"), [WeakPC]
		{
			if (AWWOPlayerController* PC = Cast<AWWOPlayerController>(WeakPC.Get())) PC->ToggleSettingsMenu();
		});
		Side->AddChildToVerticalBox(Spaced(TEXT("Q / E or bumpers: tabs   ·   Esc: back"), 10.f, FLinearColor(1, 1, 1, 0.4f), 100))->SetPadding(FMargin(2.f * K, 12.f * K, 0, 0));
		USizeBox* SideBox = WidgetTree->ConstructWidget<USizeBox>();
		SideBox->SetWidthOverride(270.f * K);
		SideBox->AddChild(Side);
		Body->AddChildToHorizontalBox(SideBox)->SetPadding(FMargin(0, 28.f * K, 28.f * K, 0));

		// Right: the open page, one wide column.
		UVerticalBox* Main = WidgetTree->ConstructWidget<UVerticalBox>();
		PageTitle = Spaced(TEXT(""), 20.f, FLinearColor::White, 300);
		Main->AddChildToVerticalBox(PageTitle)->SetPadding(FMargin(4.f * K, 26.f * K, 0, 12.f * K));
		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetBrush(RoundBrush(FLinearColor(0.13f, 0.14f, 0.165f), 6.f, FLinearColor(1, 1, 1, 0.18f), 1.f));
		Panel->SetPadding(FMargin(8.f * K));
		Panel->SetContent(GlobalSwitcher);
		Main->AddChildToVerticalBox(Panel)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Body->AddChildToHorizontalBox(Main)->SetSize(WWOFillSize(1.f));
		// The first page there is (in someone else's game the first of the ones left).
		int32 First = 0;
		for (int32 Tab = NumTopTabs - 1; Tab >= 0; --Tab) if (LastPage[Tab] != INDEX_NONE) First = LastPage[Tab];
		SelectPage(First);
	}
}

// --- Tabs, pages and search -------------------------------------------------------------------

UTextBlock* UWWOSettingsMenu::Spaced(const FString& Text, float FontPx, const FLinearColor& Color, int32 Spacing)
{
	// Capitals with wide letter spacing: the character menu's label style.
	UTextBlock* T = MakeText(Text.ToUpper(), FontPx, Color);
	FSlateFontInfo F = T->GetFont();
	F.LetterSpacing = Spacing;
	T->SetFont(F);
	return T;
}

UButton* UWWOSettingsMenu::PlainButton(UWidget* Inside, TFunction<void()> Fn)
{
	// A button with no box of its own (a tab, a sidebar line): only a faint wash when hovered.
	UButton* B = WidgetTree->ConstructWidget<UButton>();
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	B->IsFocusable = false; // the cursor drives everything, not focus
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	FButtonStyle Style;
	Style.SetNormal(RoundBrush(FLinearColor(0, 0, 0, 0), 5.f));
	Style.SetHovered(RoundBrush(FLinearColor(1, 1, 1, 0.06f), 5.f));
	Style.SetPressed(RoundBrush(FLinearColor(1, 1, 1, 0.1f), 5.f));
	Style.SetNormalPadding(FMargin(0));
	Style.SetPressedPadding(FMargin(0));
	B->SetStyle(Style);
	UButtonSlot* Slot = Cast<UButtonSlot>(B->AddChild(Inside));
	Slot->SetPadding(FMargin(0));
	Slot->SetHorizontalAlignment(HAlign_Fill);
	Slot->SetVerticalAlignment(VAlign_Fill);
	UWWOClickHandler* H = NewObject<UWWOClickHandler>(this);
	H->Fn = [Fn = MoveTemp(Fn), Weak = TWeakObjectPtr<UWWOSettingsMenu>(this)] { if (Weak.IsValid()) UWWOSound::PlayFlat(Weak.Get(), "ui_click"); if (Fn) Fn(); };
	B->OnClicked.AddDynamic(H, &UWWOClickHandler::Handle);
	H->For = B;
	KeepHandler(H);
	return B;
}

void UWWOSettingsMenu::BuildBinds()
{
	// One line per thing you do: its name, its keyboard / mouse key, its controller button. Click
	// (or Cross) a key, then press the new one; Esc leaves it as it was. A key already used on
	// another line changes places with it.
	AWWOPlayerController* PC = Cast<AWWOPlayerController>(GetOwningPlayer());
	if (!BindsList || !PC) return;
	BindsList->ClearChildren();
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	const FLinearColor Accent = L && L->GetFlora() ? WWO::Lightened(L->GetFlora()->Color, 0.15f) : FLinearColor(0.72f, 0.5f, 1.f);
	TWeakObjectPtr<UWWOSettingsMenu> WeakThis = this;
	TWeakObjectPtr<AWWOPlayerController> WeakPC = PC;
	const float KeyWidth = 300.f;
	auto Line = [&](UWidget* Name, UWidget* Kbm, UWidget* Pad, float Top)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		UHorizontalBoxSlot* NameSlot = Row->AddChildToHorizontalBox(Name);
		NameSlot->SetSize(WWOFillSize(1.f));
		NameSlot->SetVerticalAlignment(VAlign_Center);
		for (UWidget* W : { Kbm, Pad })
		{
			USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
			Box->SetWidthOverride(KeyWidth * K);
			Box->AddChild(W);
			UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Box);
			S->SetPadding(FMargin(16.f * K, 0, 0, 0));
			S->SetVerticalAlignment(VAlign_Center);
		}
		if (UScrollBoxSlot* Slot = Cast<UScrollBoxSlot>(BindsList->AddChild(Row))) Slot->SetPadding(FMargin(12.f * K, Top * K, 16.f * K, 0));
	};
	Line(Spaced(TEXT("Action"), 12.f, FLinearColor(1, 1, 1, 0.55f), 220), Spaced(TEXT("Keyboard / mouse"), 12.f, FLinearColor(1, 1, 1, 0.55f), 220),
		Spaced(TEXT("Controller"), 12.f, FLinearColor(1, 1, 1, 0.55f), 220), 12.f);
	for (const FWWOBindRow& R : AWWOPlayerController::BindRows())
	{
		auto Key = [&](bool bPad) -> UWidget*
		{
			const bool bHas = (bPad ? R.PadKey : R.KbmKey).IsValid();
			if (!bHas) return MakeText(bPad && R.MoveDir >= 0 ? TEXT("Left stick") : TEXT("—"), 18.f, FLinearColor(1, 1, 1, 0.4f));
			const bool bWaiting = PC->IsCapturing(R.Id, bPad);
			const FName Id = R.Id;
			UButton* B = MakeButton(this, bWaiting ? FString(TEXT("Press a key  ·  Esc cancels")) : AWWOPlayerController::KeyName(PC->GetBind(R.Id, bPad)), [WeakThis, WeakPC, Id, bPad]
			{
				if (!WeakThis.IsValid() || !WeakPC.IsValid()) return;
				WeakPC->BeginKeyCapture(Id, bPad, [WeakThis] { if (WeakThis.IsValid()) WeakThis->BuildBinds(); });
				WeakThis->BuildBinds(); // shows which one is waiting
			}, 18.f, FLinearColor(0.115f, 0.125f, 0.148f), false, FMargin(10.f, 7.f));
			const bool bChanged = PC->GetBind(R.Id, bPad) != (bPad ? R.PadKey : R.KbmKey);
			const FLinearColor Edge = bWaiting ? Accent : (bChanged ? FLinearColor(1.f, 0.82f, 0.2f, 0.9f) : FLinearColor(1, 1, 1, 0.45f)); // yours, not the default: gold
			FButtonStyle Style = B->GetStyle();
			Style.SetNormal(RoundBrush(bWaiting ? WWO::WithAlpha(Accent, 0.18f) : FLinearColor(0.115f, 0.125f, 0.148f), 5.f, Edge, bWaiting ? 2.f : 1.5f));
			Style.SetHovered(RoundBrush(FLinearColor(0.17f, 0.18f, 0.21f), 5.f, FLinearColor(1, 1, 1, 0.9f), 1.5f));
			Style.SetPressed(RoundBrush(FLinearColor(0.22f, 0.23f, 0.26f), 5.f, FLinearColor::White, 1.5f));
			B->SetStyle(Style);
			return B;
		};
		Line(MakeText(R.Label, 20.f), Key(false), Key(true), 8.f);
	}
	UTextBlock* Note = MakeText(TEXT("Click a key, then press the new one (Esc leaves it). A key already on another line swaps with it. Gold = changed from the default. ")
		TEXT("The menu's own keys stay as they are: Tab / Esc / Options open and close, Q / E and the bumpers change tabs, Circle goes back."), 15.f, FLinearColor(1, 1, 1, 0.5f), true);
	if (UScrollBoxSlot* Slot = Cast<UScrollBoxSlot>(BindsList->AddChild(Note))) Slot->SetPadding(FMargin(12.f * K, 22.f * K, 16.f * K, 16.f * K));
}

void UWWOSettingsMenu::SelectPage(int32 Index)
{
	// One page of the open tab: its sliders (or its list of items) fill the column on the right.
	if (!Pages.IsValidIndex(Index) || !GlobalSwitcher) return;
	CurrentPage = Index;
	TopTab = Pages[Index].Tab;
	LastPage[TopTab] = Index;
	if (GlobalKeys.IsValidIndex(Index)) GlobalTab = Index;
	if (!SearchText.IsEmpty() && SearchBox) { SearchText.Reset(); SearchBox->SetText(FText::GetEmpty()); } // picking a page leaves the search
	if (Index == BindsPage) BuildBinds(); // as the bindings are now
	GlobalSwitcher->SetActiveWidgetIndex(Index);
	if (SideSwitcher) SideSwitcher->SetActiveWidgetIndex(TopTab);
	if (PageTitle) PageTitle->SetText(FText::FromString(Pages[Index].Title.ToUpper()));
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	const FLinearColor Accent = L && L->GetFlora() ? WWO::Lightened(L->GetFlora()->Color, 0.15f) : FLinearColor(0.72f, 0.5f, 1.f);
	for (int32 i = 0; i < TopLabels.Num(); ++i)
	{
		TopLabels[i]->SetColorAndOpacity(FSlateColor(WWO::Lin(i == TopTab ? FLinearColor::White : FLinearColor(1, 1, 1, 0.6f))));
		TopLines[i]->SetBrushColor(WWO::Lin(i == TopTab ? Accent : FLinearColor(0, 0, 0, 0)));
	}
	for (int32 i = 0; i < Pages.Num(); ++i)
	{
		if (Pages[i].Label.IsValid()) Pages[i].Label->SetColorAndOpacity(FSlateColor(WWO::Lin(i == Index ? FLinearColor::White : FLinearColor(1, 1, 1, 0.6f))));
		if (Pages[i].Mark.IsValid()) Pages[i].Mark->SetBrushColor(WWO::Lin(i == Index ? Accent : FLinearColor(1, 1, 1, 0.12f)));
	}
}

void UWWOSettingsMenu::SelectTopTab(int32 Index)
{
	// A tab opens on the page it was last on.
	if (bOpen) UWWOSound::PlayFlat(this, "ui_tab");
	Index = (Index % NumTopTabs + NumTopTabs) % NumTopTabs;
	if (LastPage.IsValidIndex(Index) && LastPage[Index] != INDEX_NONE) SelectPage(LastPage[Index]);
}

bool UWWOSettingsMenu::ShowPage(const FString& Title)
{
	for (int32 i = 0; i < Pages.Num(); ++i)
	{
		// (A page a joiner's menu doesn't list has no label: it can't be opened this way either.)
		if (Pages[i].Title == Title && (!bRestricted || Pages[i].Label.IsValid())) { SelectPage(i); return true; }
	}
	return false;
}

void UWWOSettingsMenu::OnSearchChanged(const FText& Text)
{
	// Every slider whose name (or whose page's or item's name) holds what was typed, from every
	// tab, under the name of where it lives. Emptying the box goes back to the page you were on.
	const FString Query = Text.ToString().TrimStartAndEnd();
	if (Query == SearchText || !SearchResults) return;
	SearchText = Query;
	if (Query.IsEmpty())
	{
		const int32 Back = CurrentPage;
		SelectPage(Back);
		return;
	}
	SearchResults->ClearChildren();
	int32 Found = 0;
	constexpr int32 Most = 80;
	for (const FWWOSettingSection& S : Sections)
	{
		if (!SectionAllowed(S)) continue;
		const bool bWholeSection = S.Title.Contains(Query);
		bool bNamed = false;
		for (int32 i = 0; i < S.Rows.Num() && Found < Most; ++i)
		{
			const FWWOSettingRow& R = S.Rows[i];
			if (!R.Header.IsEmpty() || R.LiveHeader || !Allowed(R) || !(bWholeSection || R.Label.Contains(Query))) continue;
			if (!bNamed)
			{
				bNamed = true;
				UTextBlock* Where = Spaced(S.Title, 13.f, FLinearColor(1, 1, 1, 0.55f), 220);
				if (UScrollBoxSlot* Slot = Cast<UScrollBoxSlot>(SearchResults->AddChild(Where))) Slot->SetPadding(FMargin(12.f * K, (Found == 0 ? 12.f : 26.f) * K, 12.f * K, 0));
			}
			if (UScrollBoxSlot* Slot = Cast<UScrollBoxSlot>(SearchResults->AddChild(MakeRow(S.Key, i)))) Slot->SetPadding(FMargin(12.f * K, 8.f * K, 12.f * K, 0));
			++Found;
		}
	}
	if (Found == 0)
	{
		if (UScrollBoxSlot* Slot = Cast<UScrollBoxSlot>(SearchResults->AddChild(MakeText(TEXT("No setting has that in its name."), 20.f, FLinearColor(1, 1, 1, 0.6f))))) Slot->SetPadding(FMargin(12.f * K));
	}
	SearchResults->ScrollToStart();
	GlobalSwitcher->SetActiveWidgetIndex(SearchPage);
	if (PageTitle) PageTitle->SetText(FText::FromString(FString::Printf(TEXT("SEARCH  ·  %d%s FOUND"), Found, Found >= Most ? TEXT("+") : TEXT(""))));
}

void UWWOSettingsMenu::AutoTestSearch(const FString& Text)
{
	if (SearchBox) SearchBox->SetText(FText::FromString(Text));
	OnSearchChanged(FText::FromString(Text));
}

void UWWOSettingsMenu::OnSearchCommitted(const FText& Text, ETextCommit::Type Type)
{
	// Enter or Esc hands the keyboard back to the game, so Q / E, Tab and Esc work the menu again.
	if (Type == ETextCommit::OnEnter || Type == ETextCommit::OnCleared) FSlateApplication::Get().SetAllUserFocusToGameViewport();
}

void UWWOSettingsMenu::Open()
{
	Prepare();
	bOpen = true;
	UWWOSound::PlayFlat(this, "ui_open");
	SetVisibility(ESlateVisibility::Visible);
	// Item lists can change (a different grenade equipped), so refresh any showing.
	for (int32 i = 0; i < Browsers.Num(); ++i)
	{
		if (Browsers[i].Switcher->GetActiveWidgetIndex() == 0) ShowList(i);
	}
	if (SearchBox && !SearchText.IsEmpty()) SearchBox->SetText(FText::GetEmpty());
	BuildBinds();
	SaveRevertAt = 0.0;
	if (SaveButton)
	{
		if (UTextBlock* Label = Cast<UTextBlock>(SaveButton->GetChildAt(0))) Label->SetText(FText::FromString(TEXT("Save as defaults")));
	}
	SyncSliders();
}

void UWWOSettingsMenu::Close()
{
	if (bOpen) UWWOSound::PlayFlat(this, "ui_close");
	bOpen = false;
	SetVisibility(ESlateVisibility::Collapsed);
}

void UWWOSettingsMenu::Back()
{
	// Back out of an open item first (the one on screen), then close.
	auto IsShown = [](UWidget* W)
	{
		for (UWidget* Child = W; Child && Child->GetParent(); Child = Child->GetParent())
		{
			if (UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(Child->GetParent()))
			{
				if (Switcher->GetActiveWidget() != Child) return false;
			}
		}
		return true;
	};
	for (int32 i = 0; i < Browsers.Num(); ++i)
	{
		if (Browsers[i].Switcher->GetActiveWidgetIndex() == 1 && IsShown(Browsers[i].Switcher))
		{
			ShowList(i);
			return;
		}
	}
	if (AWWOPlayerController* PC = Cast<AWWOPlayerController>(GetOwningPlayer())) PC->ToggleSettingsMenu();
}
