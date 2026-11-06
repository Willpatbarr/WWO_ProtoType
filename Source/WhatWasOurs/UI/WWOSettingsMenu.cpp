#include "UI/WWOSettingsMenu.h"
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
#include "Styling/CoreStyle.h"
#include "UI/WWOMenuCursor.h"
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
	H->Fn = MoveTemp(Fn);
	B->OnClicked.AddDynamic(H, &UWWOClickHandler::Handle);
	Handlers.Add(H);
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
		Row(P, "MouseSensitivity", TEXT("Mouse sensitivity"), 0.1f, 10.f, 0.1f),
		Row(P, "StickSensitivity", TEXT("Stick sensitivity"), 0.5f, 10.f, 0.1f),
		Row(G, "RumbleScale", TEXT("Controller rumble strength (0 = off)"), 0.f, 2.f, 0.05f),
		Row(G, "PunchScale", TEXT("Camera punch strength (0 = off)"), 0.f, 2.f, 0.05f),
		Row(P, "BobIntensity", TEXT("Head bob intensity"), 0.f, 3.f, 0.1f),
		Row(P, "WalkStride", TEXT("Walk stride length (m)"), 0.5f, 5.f, 0.1f),
		Row(P, "SprintStride", TEXT("Sprint stride length (m)"), 0.5f, 6.f, 0.1f),
		Row(P, "LandingDip", TEXT("Landing dip"), 0.f, 3.f, 0.1f),
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
		Row(H, "GodMode", TEXT("God mode (0/1)"), 0.f, 1.f, 1.f),
		Header(TEXT("Weapon handling")),
		Row(G, "SwapTime", TEXT("Weapon swap time (s)"), 0.05f, 1.5f, 0.01f),
		Row(G, "ReadyTime", TEXT("Ready time after sprint (s)"), 0.f, 0.6f, 0.01f),
		Header(TEXT("Frame parts")),
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
		return { Text, [WeakWorld, LevelName] { if (WeakWorld.IsValid()) AWWOGameMode::LoadMap(WeakWorld.Get(), LevelName); } };
	};
	AddGlobal(TEXT("Maps"), { Header(TEXT("Switch maps (restarts the run on that map)")) }, {
		MapButton(TEXT("Small arena"), TEXT("L_Arena")),
		MapButton(TEXT("Large arena (two levels)"), TEXT("L_ArenaLarge")),
		MapButton(TEXT("Test range (gray box)"), TEXT("L_TestRange")),
	});
	TWeakObjectPtr<AWWOGameMode> WeakMode = Mode;
	AddGlobal(TEXT("Waves"), {
		Header(TEXT("Arena waves (changes to 'on' and the wave list apply on Restart run)")),
		Row(Mode, "Enabled", TEXT("Waves on (0/1; 0 = sandbox)"), 0.f, 1.f, 1.f),
		Row(Mode, "WavesToWin", TEXT("Waves to win"), 1.f, 30.f, 1.f),
		Row(Mode, "StartDelay", TEXT("Time before wave 1 (s)"), 0.f, 30.f, 0.5f),
		Row(Mode, "BreakTime", TEXT("Break between waves (s)"), 0.f, 60.f, 0.5f),
		Row(Mode, "SizeScale", TEXT("Wave size"), 0.25f, 4.f, 0.05f, Times()),
		Row(Mode, "SpawnStagger", TEXT("Time between spawns (s)"), 0.f, 3.f, 0.05f),
	}, {
		{ TEXT("Restart run"), [WeakMode] { if (WeakMode.IsValid()) WeakMode->Restart(); } },
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
			Row(W, "AdsVignette", TEXT("ADS vignette"), 0.f, 1.f, 0.05f),
			Row(W, "ScopeOverlay", TEXT("Scope overlay (0/1)"), 0.f, 1.f, 1.f),
			Row(W, "SightHeight", TEXT("Sight dot height above gun (cm)"), -6.f, 6.f, 0.1f),
			Header(TEXT("Shooting")),
			Row(W, "SemiAuto", TEXT("Semi-auto (0 = hold to fire)"), 0.f, 1.f, 1.f),
			Row(W, "FireRate", TEXT("Fire rate (rounds/min)"), 20.f, 1200.f, 5.f),
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
			if (const FWWOSettingRow* R = RowFor(Key, Pair.Key)) R->Set(Pair.Value);
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
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	for (const FWWOSettingSection& S : Sections)
	{
		TSharedRef<FJsonObject> Values = MakeShared<FJsonObject>();
		TMap<FName, float>& Recorded = Defaults.FindOrAdd(S.Key);
		for (const FWWOSettingRow& R : S.Rows)
		{
			if (!R.Header.IsEmpty()) continue;
			const float V = R.Get();
			Values->SetNumberField(R.Id.ToString(), V);
			Recorded.Add(R.Id, V); // Reset now returns here
		}
		Root->SetObjectField(S.Key, Values);
	}
	FString Text;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
	return FFileHelper::SaveStringToFile(Text, *SavePath());
}

void UWWOSettingsMenu::LoadSaved()
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *SavePath())) return;
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
				// Target health resets every target when set; apply it quietly here.
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
	const UScrollBox* Scroll = GlobalSwitcher ? Cast<UScrollBox>(GlobalSwitcher->GetActiveWidget()) : nullptr;
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

void UWWOSettingsMenu::StyleTab(UButton* Tab, bool bSelected)
{
	// Godot's tab styles from the settings theme: 4 px corners, 9 x 6 px padding.
	const float Shade = bSelected ? 0.24f : 0.13f;
	const FLinearColor Color(Shade, Shade + 0.01f, Shade + 0.04f);
	FButtonStyle Style = Tab->GetStyle();
	Style.SetNormal(RoundBrush(Color, 4.f));
	Style.SetHovered(RoundBrush(bSelected ? Color : FLinearColor(0.18f, 0.19f, 0.22f), 4.f));
	Style.SetPressed(RoundBrush(bSelected ? Color : FLinearColor(0.18f, 0.19f, 0.22f), 4.f));
	Tab->SetStyle(Style);
}

UWidget* UWWOSettingsMenu::MakeTabs(const TArray<FString>& Titles, UWidgetSwitcher* Switcher, TArray<TObjectPtr<UButton>>* OutButtons)
{
	// Godot's TabContainer tab bar (font 15 in this menu's theme). Godot keeps one row and adds
	// scroll arrows when the tabs don't fit; here they wrap, so every tab stays one click away.
	UWrapBox* Bar = WidgetTree->ConstructWidget<UWrapBox>();
	Bar->SetInnerSlotPadding(FVector2D(4.f * K, 4.f * K));
	TSharedRef<TArray<UButton*>> Buttons = MakeShared<TArray<UButton*>>();
	for (int32 i = 0; i < Titles.Num(); ++i)
	{
		TWeakObjectPtr<UWidgetSwitcher> WeakSwitcher = Switcher;
		TWeakObjectPtr<UWWOSettingsMenu> WeakThis = this;
		const bool bGlobal = OutButtons != nullptr;
		UButton* B = MakeButton(this, Titles[i], [WeakSwitcher, i, Buttons, WeakThis, bGlobal]
		{
			if (!WeakSwitcher.IsValid()) return;
			if (bGlobal && WeakThis.IsValid())
			{
				WeakThis->SelectGlobalTab(i);
				return;
			}
			WeakSwitcher->SetActiveWidgetIndex(i);
			for (int32 j = 0; j < Buttons->Num(); ++j) StyleTab((*Buttons)[j], j == i);
		}, 15.f, FLinearColor(0.13f, 0.14f, 0.17f), false, FMargin(9.f, 6.f));
		StyleTab(B, i == 0);
		Buttons->Add(B);
		if (OutButtons) OutButtons->Add(B);
		Bar->AddChild(B);
	}
	return Bar;
}

void UWWOSettingsMenu::SelectGlobalTab(int32 Index)
{
	GlobalTab = (Index + GlobalKeys.Num()) % FMath::Max(GlobalKeys.Num(), 1);
	if (GlobalSwitcher) GlobalSwitcher->SetActiveWidgetIndex(GlobalTab);
	for (int32 j = 0; j < GlobalTabButtons.Num(); ++j) StyleTab(GlobalTabButtons[j], j == GlobalTab);
}

void UWWOSettingsMenu::CycleGlobalTab(int32 Step)
{
	SelectGlobalTab(GlobalTab + Step);
}

FString UWWOSettingsMenu::ValueText(const FWWOSettingRow& Row, float V)
{
	if (Row.Show) return Row.Show(V);
	return Row.Step < 0.1f ? FString::Printf(TEXT("%.2f"), V) : FString::Printf(TEXT("%.1f"), V);
}

UWidget* UWWOSettingsMenu::MakeRow(const FString& Key, int32 RowIndex)
{
	// Godot: [label, at least 270 px] 12 [slider, fills, 28 px tall] 12 [value, at least 80 px or
	// 170 for custom readouts, right-aligned], all in the theme's 22 px font.
	const FWWOSettingRow& Row = Find(Key)->Rows[RowIndex];
	UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>();
	USizeBox* LabelBox = WidgetTree->ConstructWidget<USizeBox>();
	LabelBox->SetMinDesiredWidth(270.f * K);
	LabelBox->AddChild(MakeText(Row.Label, 22.f));
	Line->AddChildToHorizontalBox(LabelBox)->SetVerticalAlignment(VAlign_Center);

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
	UHorizontalBoxSlot* BoxSlot = Line->AddChildToHorizontalBox(SliderBox);
	BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	BoxSlot->SetVerticalAlignment(VAlign_Center);
	BoxSlot->SetPadding(FMargin(12.f * K, 0.f));

	USizeBox* ValueBox = WidgetTree->ConstructWidget<USizeBox>();
	ValueBox->SetMinDesiredWidth((Row.Show ? 170.f : 80.f) * K);
	UTextBlock* Value = MakeText(ValueText(Row, Row.Get()), 22.f);
	Value->SetJustification(ETextJustify::Right);
	ValueBox->AddChild(Value);
	Line->AddChildToHorizontalBox(ValueBox)->SetVerticalAlignment(VAlign_Center);

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
	Handlers.Add(H);
	Live.Add({ Slider, Value, Fill, Key, RowIndex });
	return Line;
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
	int32 Count = 0;
	UVerticalBox* Group = nullptr;
	for (int32 i = 0; i < S->Rows.Num(); ++i, ++Count)
	{
		const FWWOSettingRow& R = S->Rows[i];
		if (R.Header.IsEmpty())
		{
			UWidget* Row = MakeRow(Key, i);
			if (Group) Group->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 8.f * K, 0.f, 0.f));
			else Add(Row, Count == 0, false);
			continue;
		}
		const bool bHasRows = S->Rows.IsValidIndex(i + 1) && S->Rows[i + 1].Header.IsEmpty();
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
		RegisterSections(P);
		RecordDefaults();
		LoadSaved();
		UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(P);
		UOverlay* Root = Cast<UOverlay>(WidgetTree->RootWidget);

		UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>();
		Backdrop->SetBrushColor(FLinearColor(0, 0, 0, 0.6f)); // dims the game behind the menu
		UOverlaySlot* BackSlot = Root->AddChildToOverlay(Backdrop);
		BackSlot->SetHorizontalAlignment(HAlign_Fill);
		BackSlot->SetVerticalAlignment(VAlign_Fill);

		USizeBox* PanelBox = WidgetTree->ConstructWidget<USizeBox>();
		PanelBox->SetWidthOverride(1560.f * K);
		PanelBox->SetHeightOverride(860.f * K);
		UOverlaySlot* PanelSlot = Root->AddChildToOverlay(PanelBox);
		PanelSlot->SetHorizontalAlignment(HAlign_Center);
		PanelSlot->SetVerticalAlignment(VAlign_Center);
		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetBrush(RoundBrush(FLinearColor(0.1f, 0.11f, 0.13f), 12.f));
		Panel->SetPadding(FMargin(20.f * K));
		PanelBox->AddChild(Panel);
		UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>();
		Panel->SetContent(Page);

		UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
		Header->AddChildToHorizontalBox(MakeText(TEXT("Settings"), 32.f))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Header->AddChildToHorizontalBox(MakeText(TEXT("◯ / Esc to go back  ·  L1/R1 or Q/E: Global tabs"), 18.f, FLinearColor(1, 1, 1, 0.6f)))->SetVerticalAlignment(VAlign_Center);
		Page->AddChildToVerticalBox(Header)->SetPadding(FMargin(0, 0, 0, 12.f * K));

		UHorizontalBox* Panes = WidgetTree->ConstructWidget<UHorizontalBox>();
		Page->AddChildToVerticalBox(Panes)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		// Global pane.
		UVerticalBox* GlobalPane = WidgetTree->ConstructWidget<UVerticalBox>();
		GlobalPane->AddChildToVerticalBox(MakeText(TEXT("Global"), 26.f));
		GlobalSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>();
		TArray<FString> GlobalTitles;
		for (const FString& Key : GlobalKeys)
		{
			GlobalTitles.Add(Find(Key)->Title);
			UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
			BuildRows(Scroll, Key);
			GlobalSwitcher->AddChild(Scroll);
		}
		GlobalPane->AddChildToVerticalBox(MakeTabs(GlobalTitles, GlobalSwitcher, &GlobalTabButtons))->SetPadding(FMargin(0, 8.f * K, 0, 0));
		UBorder* GlobalBack = WidgetTree->ConstructWidget<UBorder>();
		GlobalBack->SetBrush(RoundBrush(FLinearColor(0.13f, 0.14f, 0.17f), 6.f));
		GlobalBack->SetPadding(FMargin(0.f));
		GlobalBack->SetContent(GlobalSwitcher);
		GlobalPane->AddChildToVerticalBox(GlobalBack)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TWeakObjectPtr<UWWOSettingsMenu> WeakThis = this;
		UVerticalBoxSlot* ResetTabSlot = GlobalPane->AddChildToVerticalBox(MakeButton(this, TEXT("Reset this tab"), [WeakThis]
		{
			if (WeakThis.IsValid()) WeakThis->ResetSection(WeakThis->GlobalKeys[WeakThis->GlobalTab]);
		}, 22.f));
		ResetTabSlot->SetHorizontalAlignment(HAlign_Left);
		ResetTabSlot->SetPadding(FMargin(0, 8.f * K, 0, 0));
		UHorizontalBoxSlot* GlobalSlot = Panes->AddChildToHorizontalBox(GlobalPane);
		GlobalSlot->SetSize(WWOFillSize(1.15f));
		GlobalSlot->SetPadding(FMargin(0, 0, 20.f * K, 0));

		// Specific pane.
		UVerticalBox* SpecificPane = WidgetTree->ConstructWidget<UVerticalBox>();
		SpecificPane->AddChildToVerticalBox(MakeText(TEXT("Specific"), 26.f));
		UWidgetSwitcher* SpecificSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>();
		TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
		SpecificSwitcher->AddChild(MakeBrowser([WL]
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
		}));
		// Flora: sub-tabs for grenades, abilities and traits.
		UVerticalBox* FloraPage = WidgetTree->ConstructWidget<UVerticalBox>();
		UWidgetSwitcher* FloraSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>();
		FloraSwitcher->AddChild(MakeBrowser([WL]
		{
			TArray<FWWOBrowserItem> Items;
			if (WL.IsValid()) for (UWWOGrenadeData* G : WL->Grenades) Items.Add({ TEXT("grenade/") + G->Id.ToString(), G->DisplayName, G == WL->GetGrenade() ? TEXT("Equipped") : TEXT("") });
			return Items;
		}));
		FloraSwitcher->AddChild(MakeBrowser([WL]
		{
			TArray<FWWOBrowserItem> Items;
			if (!WL.IsValid()) return Items;
			for (UWWOAbilityData* A : WL->Abilities)
			{
				const int32 Slot = WL->AbilitySlotOf(A);
				Items.Add({ TEXT("ability/") + A->Id.ToString(), A->DisplayName, KindName(A->Kind) + (Slot != INDEX_NONE ? FString::Printf(TEXT(" · equipped in slot %d"), Slot + 1) : FString()) });
			}
			return Items;
		}));
		FloraSwitcher->AddChild(MakeBrowser([WL]
		{
			TArray<FWWOBrowserItem> Items;
			if (!WL.IsValid()) return Items;
			for (UWWOTraitData* T : WL->Traits)
			{
				Items.Add({ TEXT("trait/") + T->Id.ToString(), T->DisplayName, Capitalize(T->Flora.ToString()) + (WL->IsTraitEquipped(T) ? TEXT(" · equipped") : TEXT("")) });
			}
			return Items;
		}));
		FloraPage->AddChildToVerticalBox(MakeTabs({ TEXT("Grenades"), TEXT("Abilities"), TEXT("Traits") }, FloraSwitcher, nullptr));
		FloraPage->AddChildToVerticalBox(FloraSwitcher)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		SpecificSwitcher->AddChild(FloraPage);
		SpecificSwitcher->AddChild(MakeBrowser([WL]
		{
			TArray<FWWOBrowserItem> Items;
			if (WL.IsValid()) for (UWWOWeaponData* W : WL->Weapons) Items.Add({ TEXT("weapon/") + W->Id.ToString(), W->DisplayName, W->Archetype + (W == WL->GetWeapon() ? TEXT(" · equipped") : TEXT("")) });
			return Items;
		}));
		SpecificSwitcher->AddChild(MakeBrowser([WL]
		{
			TArray<FWWOBrowserItem> Items;
			if (!WL.IsValid()) return Items;
			for (UWWOFramePartData* Part : WL->FrameParts)
			{
				Items.Add({ TEXT("frame/") + Part->Id.ToString(), Part->DisplayName, Capitalize(Part->Slot.ToString()) + (Part == WL->GetFramePart(Part->Slot) ? TEXT(" · equipped") : TEXT("")) });
			}
			return Items;
		}));
		SpecificPane->AddChildToVerticalBox(MakeTabs({ TEXT("Equipped"), TEXT("Flora"), TEXT("Weapons"), TEXT("Frame Parts") }, SpecificSwitcher, nullptr))->SetPadding(FMargin(0, 8.f * K, 0, 0));
		UBorder* SpecificBack = WidgetTree->ConstructWidget<UBorder>();
		SpecificBack->SetBrush(RoundBrush(FLinearColor(0.13f, 0.14f, 0.17f), 6.f));
		SpecificBack->SetPadding(FMargin(0.f));
		SpecificBack->SetContent(SpecificSwitcher);
		SpecificPane->AddChildToVerticalBox(SpecificBack)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Panes->AddChildToHorizontalBox(SpecificPane)->SetSize(WWOFillSize(1.f));

		// Bottom buttons.
		UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>();
		Buttons->AddChildToHorizontalBox(MakeButton(this, TEXT("Reset all"), [WeakThis] { if (WeakThis.IsValid()) WeakThis->ResetAll(); }, 22.f))->SetPadding(FMargin(0, 0, 12.f * K, 0));
		SaveButton = MakeButton(this, TEXT("Save as defaults"), [WeakThis]
		{
			if (!WeakThis.IsValid()) return;
			const bool bSaved = WeakThis->SaveDefaults();
			if (UTextBlock* Label = Cast<UTextBlock>(WeakThis->SaveButton->GetChildAt(0)))
			{
				Label->SetText(FText::FromString(bSaved ? TEXT("Saved ✓") : TEXT("Save failed")));
			}
			WeakThis->SaveRevertAt = FPlatformTime::Seconds() + 1.5; // Godot's 1.5 s timer (real time: the game is paused)
		}, 22.f);
		Buttons->AddChildToHorizontalBox(SaveButton)->SetPadding(FMargin(0, 0, 12.f * K, 0));
		TWeakObjectPtr<APlayerController> WeakPC = GetOwningPlayer();
		Buttons->AddChildToHorizontalBox(MakeButton(this, TEXT("Close"), [WeakPC]
		{
			if (AWWOPlayerController* PC = Cast<AWWOPlayerController>(WeakPC.Get())) PC->ToggleSettingsMenu();
		}, 22.f));
		Page->AddChildToVerticalBox(Buttons)->SetPadding(FMargin(0, 12.f * K, 0, 0));
		SelectGlobalTab(0);
	}
}

void UWWOSettingsMenu::Open()
{
	Prepare();
	bOpen = true;
	SetVisibility(ESlateVisibility::Visible);
	// Item lists can change (a different grenade equipped), so refresh any showing.
	for (int32 i = 0; i < Browsers.Num(); ++i)
	{
		if (Browsers[i].Switcher->GetActiveWidgetIndex() == 0) ShowList(i);
	}
	SaveRevertAt = 0.0;
	if (SaveButton)
	{
		if (UTextBlock* Label = Cast<UTextBlock>(SaveButton->GetChildAt(0))) Label->SetText(FText::FromString(TEXT("Save as defaults")));
	}
	SyncSliders();
}

void UWWOSettingsMenu::Close()
{
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
