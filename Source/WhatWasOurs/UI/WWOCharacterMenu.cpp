#include "UI/WWOCharacterMenu.h"
#include "UI/WWOSettingsMenu.h"
#include "UI/WWOStatBars.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOPlayerController.h"
#include "Characters/WWOBodyComponent.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WrapBox.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "FX/WWOFX.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Misc/App.h"

namespace
{
	FSlateChildSize WWOFillSize(float Value)
	{
		FSlateChildSize Size(ESlateSizeRule::Fill);
		Size.Value = Value;
		return Size;
	}

	constexpr float K = UWWOSettingsMenu::K;
	const FVector PreviewSpot(0.f, 0.f, -50000.f); // far below the level
}

// --- Preview -------------------------------------------------------------------------------

AWWOPreviewActor::AWWOPreviewActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true; // keeps turning while the menu has the game paused
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Turntable = CreateDefaultSubobject<USceneComponent>(TEXT("Turntable"));
	Turntable->SetupAttachment(GetRootComponent());
	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Turntable);
	Mesh->SetRelativeRotation(FRotator(0, -90.f, 0));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->PrimaryComponentTick.bTickEvenWhenPaused = true;
	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(GetRootComponent());
	// Godot's preview camera: 4.4 m in front, 1.1 m above the feet, level, 45 degrees tall (Godot
	// FOVs are vertical; Unreal's are horizontal, so convert for the preview's shape).
	Capture->SetRelativeLocationAndRotation(FVector(440.f, 0.f, 110.f), FRotator(0.f, 180.f, 0.f));
	Capture->FOVAngle = FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(22.5f)) * Size.X / Size.Y));
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->bCaptureEveryFrame = true;
	Capture->PrimaryComponentTick.bTickEvenWhenPaused = true;
	// Godot's preview has its own world and a transparent background (the panel shows through):
	// no sky or fog here, and a flat backdrop in the panel's color behind the character.
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetCloud(false);
	Capture->ShowFlags.SetGrain(false);
	// Fixed exposure, so the backdrop comes out the same shade every time (auto exposure would
	// brighten it toward mid-gray).
	Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Capture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Capture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Capture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	Capture->PostProcessSettings.bOverride_AutoExposureBias = true;
	Capture->PostProcessSettings.AutoExposureBias = ExposureBias;
	Capture->PostProcessBlendWeight = 1.f;
	Backdrop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Backdrop"));
	Backdrop->SetupAttachment(GetRootComponent());
	Backdrop->SetRelativeLocationAndRotation(FVector(-400.f, 0.f, 100.f), FRotator(90.f, 0.f, 0.f));
	Backdrop->SetRelativeScale3D(FVector(30.f));
	Backdrop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Backdrop->CastShadow = false;
	Body = CreateDefaultSubobject<UWWOBodyComponent>(TEXT("Body"));
	Body->BodyColor = FLinearColor(0.3f, 0.42f, 0.55f);
	Mesh->LightingChannels.bChannel0 = false; // the grafts copy the body's channels
	Mesh->LightingChannels.bChannel2 = true;
	// Godot's light: rotation (-35, 30, 0) degrees, seen from the preview camera (Godot +Z, toward
	// the camera, is +X here; Godot +X, the camera's right, is -Y).
	auto MakeLight = [this](const TCHAR* Name, const FRotator& Rotation, float Lux)
	{
		UDirectionalLightComponent* Light = CreateDefaultSubobject<UDirectionalLightComponent>(Name);
		Light->SetupAttachment(GetRootComponent());
		Light->SetRelativeRotation(Rotation);
		Light->Intensity = Lux;
		Light->CastShadows = false;
		Light->bAtmosphereSunLight = false;
		Light->LightingChannels.bChannel0 = false;
		Light->LightingChannels.bChannel2 = true;
		return Light;
	};
	KeyLight = MakeLight(TEXT("KeyLight"), FRotator(-35.f, 150.f, 0.f), UE_PI);
	FillLight = MakeLight(TEXT("FillLight"), FRotator(-10.f, 180.f, 0.f), UE_PI * 0.5f);
}

void AWWOPreviewActor::BeginPlay()
{
	Body->MeshOverride = Mesh;
	Super::BeginPlay();
	Target = NewObject<UTextureRenderTarget2D>(this);
	Target->RenderTargetFormat = RTF_RGBA8;
	Target->ClearColor = FLinearColor(0.07f, 0.08f, 0.1f, 1.f);
	Target->InitAutoFormat(FMath::RoundToInt(Size.X * 1.2f), FMath::RoundToInt(Size.Y * 1.2f)); // 1080p pixels
	if (UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
	{
		Backdrop->SetStaticMesh(Plane);
		if (UMaterialInterface* M = WWOFX::Material(TEXT("M_WWO_Unlit")))
		{
			UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(M, this);
			Mat->SetVectorParameterValue("Color", BackdropFor(ExposureBias));
			Backdrop->SetMaterial(0, Mat);
		}
	}
	Capture->TextureTarget = Target;
	Capture->ShowOnlyActors.Add(this);
	Body->FollowLoadout(false); // flora grafts follow the loadout
}

void AWWOPreviewActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Godot: rotate_y(0.6 * delta), counterclockwise seen from above (negative yaw here). Real
	// time, since the menu has the game paused.
	Turntable->AddRelativeRotation(FRotator(0.f, -FMath::RadiansToDegrees(0.6f) * FApp::GetDeltaTime(), 0.f));
}

// --- Menu ------------------------------------------------------------------------------------

void UWWOCharacterMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	Build();
}

int32 UWWOCharacterMenu::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!bOpen) return Layer;
	UWWOSettingsMenu::PaintCursor(this, AllottedGeometry, OutDrawElements, Layer + 1);
	return Layer + 1;
}

void UWWOCharacterMenu::AddRowLabel(const FString& Text)
{
	// Godot: a 22 px label at 60%, 12 px below the row above (the content VBox's separation).
	UTextBlock* Label = Styler->MakeText(Text, 22.f, FLinearColor(1, 1, 1, 0.6f));
	Content->AddChildToVerticalBox(Label)->SetPadding(FMargin(0, Content->GetChildrenCount() > 0 ? 12.f * K : 0.f, 0, 0));
}

void UWWOCharacterMenu::AddContent(UWidget* Widget)
{
	Content->AddChildToVerticalBox(Widget)->SetPadding(FMargin(0, Content->GetChildrenCount() > 0 ? 12.f * K : 0.f, 0, 0));
}

UWidget* UWWOCharacterMenu::Slot(const FString& Text, FVector2D Size, const FLinearColor& Color, float FontPx, TFunction<void()> Fn,
	const FLinearColor* Border, float BorderWidth, const FString& Tooltip, bool bWrap)
{
	// Godot's _make_slot: a button at least Size, centered text (font 20 unless set), 8 px corners,
	// a 2 px border in the color lightened 35% (or _set_slot_border's), lighter when hovered/pressed.
	const FLinearColor Edge = Border ? *Border : WWO::Lightened(Color, 0.35f);
	const float Width = Border ? BorderWidth : 2.f;
	UButton* B = Styler->MakeButton(this, Text, MoveTemp(Fn), FontPx, Color, false, FMargin(Width + 4.f, Width), false);
	FButtonStyle Style = B->GetStyle();
	Style.SetNormal(UWWOSettingsMenu::RoundBrush(Color, 8.f, Edge, Width));
	Style.SetHovered(UWWOSettingsMenu::RoundBrush(WWO::Lightened(Color, 0.12f), 8.f, Edge, Width));
	Style.SetPressed(UWWOSettingsMenu::RoundBrush(WWO::Lightened(Color, 0.2f), 8.f, Edge, Width));
	B->SetStyle(Style);
	if (!Tooltip.IsEmpty()) B->SetToolTipText(FText::FromString(Tooltip));
	USizeBox* Box = Styler->WidgetTree->ConstructWidget<USizeBox>();
	Box->SetMinDesiredWidth(Size.X * K);
	Box->SetMinDesiredHeight(Size.Y * K);
	if (bWrap)
	{
		// Godot's autowrap: the button keeps its minimum width and the text wraps inside it.
		Box->SetWidthOverride(Size.X * K);
		if (UTextBlock* Label = Cast<UTextBlock>(B->GetChildAt(0))) Label->SetWrapTextAt((Size.X - 2.f * (Width + 4.f)) * K);
	}
	Box->AddChild(B);
	return Box;
}

void UWWOCharacterMenu::Build()
{
	// A spare settings menu, never shown: only its MakeText / MakeButton helpers are used.
	Styler = CreateWidget<UWWOSettingsMenu>(GetOwningPlayer(), UWWOSettingsMenu::StaticClass());
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Root;
	SetCursor(EMouseCursor::None); // the menu draws Godot's ring cursor instead (NativePaint)
}

void UWWOCharacterMenu::Open()
{
	if (!bBuilt)
	{
		bBuilt = true;
		// Build into the styler's tree, then show its root in this menu.
		UWidgetTree* Tree = Styler->WidgetTree;
		UOverlay* Root = Cast<UOverlay>(WidgetTree->RootWidget);
		UBorder* Backdrop = Tree->ConstructWidget<UBorder>();
		Backdrop->SetBrushColor(FLinearColor(0, 0, 0, 0.7f));
		UOverlaySlot* BackSlot = Root->AddChildToOverlay(Backdrop);
		BackSlot->SetHorizontalAlignment(HAlign_Fill);
		BackSlot->SetVerticalAlignment(VAlign_Fill);

		USizeBox* PanelBox = Tree->ConstructWidget<USizeBox>();
		PanelBox->SetWidthOverride(1500.f * K);
		PanelBox->SetHeightOverride(820.f * K);
		UOverlaySlot* PanelSlot = Root->AddChildToOverlay(PanelBox);
		PanelSlot->SetHorizontalAlignment(HAlign_Center);
		PanelSlot->SetVerticalAlignment(VAlign_Center);
		UBorder* Panel = Tree->ConstructWidget<UBorder>();
		Panel->SetBrush(UWWOSettingsMenu::RoundBrush(FLinearColor(0.1f, 0.11f, 0.13f), 12.f));
		Panel->SetPadding(FMargin(28.f * K));
		PanelBox->AddChild(Panel);
		UVerticalBox* Page = Tree->ConstructWidget<UVerticalBox>();
		Panel->SetContent(Page);

		UHorizontalBox* Header = Tree->ConstructWidget<UHorizontalBox>();
		Header->AddChildToHorizontalBox(Styler->MakeText(TEXT("Character"), 40.f))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Header->AddChildToHorizontalBox(Styler->MakeText(TEXT("Touchpad / Create / I to close  ·  ◯ / Esc to go back"), 20.f, FLinearColor(1, 1, 1, 0.6f)))->SetVerticalAlignment(VAlign_Center);
		Page->AddChildToVerticalBox(Header)->SetPadding(FMargin(0, 0, 0, 16.f * K));

		UHorizontalBox* Body = Tree->ConstructWidget<UHorizontalBox>();
		Page->AddChildToVerticalBox(Body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		// Left: the three tiles, or one expanded tile. Tiles: 40 px, left-aligned, 36 px padding.
		TileSwitcher = Tree->ConstructWidget<UWidgetSwitcher>();
		UVerticalBox* Tiles = Tree->ConstructWidget<UVerticalBox>();
		TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
		for (const FString Tile : { FString(TEXT("Flora")), FString(TEXT("Weapons")), FString(TEXT("Frame Parts")) })
		{
			UButton* B = Styler->MakeButton(this, Tile, [WeakThis, Tile] { if (WeakThis.IsValid()) WeakThis->Expand(Tile); }, 40.f,
				FLinearColor(0.18f, 0.19f, 0.23f), true, FMargin(36.f, 0.f));
			FButtonStyle Style = B->GetStyle(); // Godot's tile shades
			Style.SetHovered(UWWOSettingsMenu::RoundBrush(FLinearColor(0.26f, 0.28f, 0.33f), 8.f));
			Style.SetPressed(UWWOSettingsMenu::RoundBrush(FLinearColor(0.32f, 0.34f, 0.4f), 8.f));
			B->SetStyle(Style);
			UVerticalBoxSlot* TileSlot = Tiles->AddChildToVerticalBox(B);
			TileSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			TileSlot->SetPadding(FMargin(0, Tiles->GetChildrenCount() > 1 ? 16.f * K : 0.f, 0, 0));
		}
		TileSwitcher->AddChild(Tiles);
		UBorder* ExpandedPanel = Tree->ConstructWidget<UBorder>();
		ExpandedPanel->SetBrush(UWWOSettingsMenu::RoundBrush(FLinearColor(0.18f, 0.19f, 0.23f), 8.f));
		ExpandedPanel->SetPadding(FMargin(28.f * K));
		UVerticalBox* ExpandedPage = Tree->ConstructWidget<UVerticalBox>();
		UHorizontalBox* Bar = Tree->ConstructWidget<UHorizontalBox>();
		ExpandedTitle = Styler->MakeText(TEXT(""), 40.f);
		Bar->AddChildToHorizontalBox(ExpandedTitle)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		// The menu theme's default font is 28 px.
		Bar->AddChildToHorizontalBox(Styler->MakeButton(this, TEXT("Collapse ✕"), [WeakThis] { if (WeakThis.IsValid()) WeakThis->Collapse(); }, 28.f))
			->SetVerticalAlignment(VAlign_Center);
		ExpandedPage->AddChildToVerticalBox(Bar)->SetPadding(FMargin(0, 0, 0, 20.f * K));
		ExpandedScroll = Tree->ConstructWidget<UScrollBox>();
		Content = Tree->ConstructWidget<UVerticalBox>();
		ExpandedScroll->AddChild(Content);
		ExpandedPage->AddChildToVerticalBox(ExpandedScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ExpandedPanel->SetContent(ExpandedPage);
		TileSwitcher->AddChild(ExpandedPanel);
		UHorizontalBoxSlot* TileArea = Body->AddChildToHorizontalBox(TileSwitcher);
		TileArea->SetSize(WWOFillSize(1.4f));
		TileArea->SetPadding(FMargin(0, 0, 24.f * K, 0));

		// Right: the character preview, filling the rest of the row like Godot's SubViewportContainer.
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		PreviewActor = GetWorld()->SpawnActor<AWWOPreviewActor>(PreviewSpot, FRotator::ZeroRotator, Params);
		Preview = Tree->ConstructWidget<UImage>();
		if (PreviewActor && PreviewActor->Target)
		{
			FSlateBrush Brush;
			Brush.SetResourceObject(PreviewActor->Target);
			Brush.ImageSize = AWWOPreviewActor::Size * K;
			Preview->SetBrush(Brush);
		}
		UHorizontalBoxSlot* PreviewSlot = Body->AddChildToHorizontalBox(Preview);
		PreviewSlot->SetSize(WWOFillSize(1.f));
		PreviewSlot->SetHorizontalAlignment(HAlign_Fill);
		PreviewSlot->SetVerticalAlignment(VAlign_Fill);
	}
	bOpen = true;
	Expanded.Reset();
	Picker.Reset();
	TileSwitcher->SetActiveWidgetIndex(0);
	SetVisibility(ESlateVisibility::Visible);
}

void UWWOCharacterMenu::Close()
{
	bOpen = false;
	SetVisibility(ESlateVisibility::Collapsed);
}

void UWWOCharacterMenu::Back()
{
	if (!OpenSlot.IsNone())
	{
		OpenSlot = NAME_None;
		Refresh();
	}
	else if (!Picker.IsEmpty())
	{
		Picker.Reset();
		Refresh();
	}
	else if (!Expanded.IsEmpty())
	{
		Collapse();
	}
	else if (AWWOPlayerController* PC = Cast<AWWOPlayerController>(GetOwningPlayer()))
	{
		PC->ToggleCharacterMenu();
	}
}

void UWWOCharacterMenu::Expand(const FString& Tile)
{
	// The Weapons tile takes the whole width (it covers the character preview for now).
	if (Preview) Preview->SetVisibility(Tile == TEXT("Weapons") ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	OpenSlot = NAME_None;
	Expanded = Tile;
	Picker.Reset();
	ExpandedTitle->SetText(FText::FromString(Tile));
	Rebuild();
	ExpandedScroll->ScrollToStart();
	TileSwitcher->SetActiveWidgetIndex(1);
}

void UWWOCharacterMenu::Collapse()
{
	if (Preview) Preview->SetVisibility(ESlateVisibility::Visible);
	OpenSlot = NAME_None;
	Expanded.Reset();
	Picker.Reset();
	TileSwitcher->SetActiveWidgetIndex(0);
}

void UWWOCharacterMenu::TogglePicker(const FString& Name)
{
	Picker = Picker == Name ? FString() : Name;
	Refresh();
}

void UWWOCharacterMenu::Refresh()
{
	// Rebuild the expanded tile in place, keeping the scroll position (Godot's _refresh).
	const float Scroll = ExpandedScroll->GetScrollOffset();
	Rebuild();
	ExpandedScroll->SetScrollOffset(Scroll);
}

void UWWOCharacterMenu::Rebuild()
{
	Content->ClearChildren();
	ModRowsStart = nullptr;
	if (Expanded == TEXT("Flora")) BuildFloraRows();
	else if (Expanded == TEXT("Weapons")) BuildWeaponRows();
	else if (Expanded == TEXT("Frame Parts")) BuildFrameRows();
}

namespace
{
	FString KindText(EWWOAbilityKind Kind)
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

void UWWOCharacterMenu::BuildFloraRows()
{
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return;
	UWidgetTree* Tree = Styler->WidgetTree;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
	auto Row = [Tree]() -> UHorizontalBox* { return Tree->ConstructWidget<UHorizontalBox>(); };
	auto AddToRow = [](UHorizontalBox* R, UWidget* W) { R->AddChildToHorizontalBox(W)->SetPadding(FMargin(R->GetChildrenCount() > 1 ? 12.f * K : 0.f, 0, 0, 0)); };
	auto Flow = [Tree]() -> UWrapBox*
	{
		UWrapBox* W = Tree->ConstructWidget<UWrapBox>();
		W->SetInnerSlotPadding(FVector2D(10.f * K, 10.f * K));
		return W;
	};
	auto Hint = [this](const FString& Text)
	{
		AddContent(Styler->MakeText(Text, 18.f, FLinearColor(1, 1, 1, 0.6f), true));
	};
	auto Pick = [WeakThis](TFunction<void()> Equip)
	{
		return [WeakThis, Equip] { Equip(); if (WeakThis.IsValid()) { WeakThis->Picker.Reset(); WeakThis->Refresh(); } };
	};

	// Flora
	AddRowLabel(TEXT("Flora"));
	UWWOFloraData* Flora = L->GetFlora();
	const FLinearColor FloraColor = Flora ? Flora->Color : FLinearColor(0.4f, 0.85f, 0.3f);
	FString FloraText = TEXT("Flora\n") + (Flora ? Flora->DisplayName : FString(TEXT("None")));
	if (Flora && !Flora->UltName.IsEmpty()) FloraText += TEXT("\nUlt: ") + Flora->UltName;
	{
		UHorizontalBox* R = Row();
		AddToRow(R, Slot(FloraText, FVector2D(300, 120), WWO::Darkened(FloraColor, 0.6f), 20.f,
			[WeakThis] { if (WeakThis.IsValid()) WeakThis->TogglePicker(TEXT("flora")); }, &FloraColor, 3.f));
		AddContent(R);
	}
	if (Picker == TEXT("flora"))
	{
		AddRowLabel(TEXT("Choose a flora (switches your grenade, abilities and ult)"));
		UHorizontalBox* R = Row();
		for (UWWOFloraData* Option : L->Floras)
		{
			if (!Option->bUnlocked) continue;
			const bool bOn = Option == Flora;
			const FLinearColor Edge = bOn ? WWO::Lightened(Option->Color, 0.3f) : Option->Color;
			AddToRow(R, Slot(Option->DisplayName + (bOn ? TEXT("  ✓") : TEXT("")) + TEXT("\nUlt: ") + Option->UltName, FVector2D(240, 90),
				WWO::Darkened(Option->Color, 0.5f), 20.f, Pick([WL, Option] { if (WL.IsValid()) WL->EquipFlora(Option); }), &Edge, bOn ? 5.f : 2.f, Option->Description));
		}
		AddContent(R);
		if (Flora) Hint(Flora->Description);
	}

	// Abilities: grenade, ability 1, ability 2.
	AddRowLabel(TEXT("Abilities"));
	{
		UHorizontalBox* R = Row();
		if (const UWWOGrenadeData* G = L->GetGrenade())
		{
			AddToRow(R, Slot(TEXT("Grenade\n") + G->DisplayName, FVector2D(220, 110), WWO::Darkened(G->Color, 0.6f), 20.f,
				[WeakThis] { if (WeakThis.IsValid()) WeakThis->TogglePicker(TEXT("grenade")); }, &G->Color, 3.f));
		}
		for (int32 SlotIndex = 0; SlotIndex < UWWOLoadoutSubsystem::AbilitySlots; ++SlotIndex)
		{
			const UWWOAbilityData* A = L->GetAbility(SlotIndex);
			const FString Title = FString::Printf(TEXT("Ability %d"), SlotIndex + 1);
			if (!A)
			{
				AddToRow(R, Slot(Title, FVector2D(220, 110), FLinearColor(0.24f, 0.25f, 0.3f), 20.f, [] {})); // an empty slot does nothing
				continue;
			}
			const FString Name = FString::Printf(TEXT("ability%d"), SlotIndex);
			AddToRow(R, Slot(Title + TEXT("\n") + A->DisplayName, FVector2D(220, 110), WWO::Darkened(A->Color, 0.6f), 20.f,
				[WeakThis, Name] { if (WeakThis.IsValid()) WeakThis->TogglePicker(Name); }, &A->Color, 3.f));
		}
		AddContent(R);
	}
	if (Picker == TEXT("grenade"))
	{
		AddRowLabel(TEXT("Choose a grenade"));
		UHorizontalBox* R = Row();
		for (UWWOGrenadeData* Option : L->GrenadesForFlora())
		{
			if (!Option->bUnlocked) continue;
			const bool bOn = Option == L->GetGrenade();
			const FString Stats = FString::Printf(TEXT("%s · %d dmg · %s m"), *StaticEnum<EWWOGrenadeKind>()->GetNameStringByValue((int64)Option->Kind),
				FMath::RoundToInt(Option->Damage), *FString::SanitizeFloat(Option->BlastRadius));
			const FLinearColor Edge = bOn ? WWO::Lightened(Option->Color, 0.3f) : Option->Color;
			AddToRow(R, Slot(Option->DisplayName + (bOn ? TEXT("  ✓") : TEXT("")) + TEXT("\n") + Stats, FVector2D(200, 90), WWO::Darkened(Option->Color, 0.45f), 20.f,
				Pick([WL, Option] { if (WL.IsValid()) WL->EquipGrenade(Option); }), &Edge, bOn ? 5.f : 2.f));
		}
		AddContent(R);
	}
	else if (Picker.StartsWith(TEXT("ability")))
	{
		const int32 SlotIndex = FCString::Atoi(*Picker.Mid(7));
		AddRowLabel(FString::Printf(TEXT("Choose ability %d"), SlotIndex + 1));
		UHorizontalBox* R = Row();
		TArray<FString> Lines;
		for (UWWOAbilityData* Option : L->AbilitiesForFlora())
		{
			Lines.Add(FString::Printf(TEXT("%s: %s"), *Option->DisplayName, *Option->Description));
			if (!Option->bUnlocked) continue;
			const int32 In = L->AbilitySlotOf(Option);
			const FString Tag = In == SlotIndex ? TEXT("  ✓") : (In != INDEX_NONE ? FString::Printf(TEXT("  (slot %d)"), In + 1) : FString());
			const FLinearColor Edge = In == SlotIndex ? WWO::Lightened(Option->Color, 0.3f) : Option->Color;
			AddToRow(R, Slot(Option->DisplayName + Tag + TEXT("\n") + KindText(Option->Kind), FVector2D(200, 90), WWO::Darkened(Option->Color, 0.5f), 20.f,
				Pick([WL, Option, SlotIndex] { if (WL.IsValid()) WL->EquipAbility(SlotIndex, Option); }), &Edge, In == SlotIndex ? 5.f : 2.f, Option->Description));
		}
		AddContent(R);
		Hint(FString::Join(Lines, TEXT("\n")));
	}

	// Traits: the slots (equipped ones first), then the picker.
	const TArray<UWWOTraitData*> Equipped = L->EquippedTraits();
	AddRowLabel(FString::Printf(TEXT("Traits (%d of %d)"), Equipped.Num(), UWWOLoadoutSubsystem::TraitSlots));
	UWrapBox* Slots = Flow();
	const FLinearColor TraitEdge(0.55f, 0.7f, 1.f);
	for (int32 i = 0; i < UWWOLoadoutSubsystem::TraitSlots; ++i)
	{
		const bool bHas = i < Equipped.Num();
		Slots->AddChild(Slot(bHas ? Equipped[i]->DisplayName : TEXT("Empty"), FVector2D(170, 90), bHas ? FLinearColor(0.16f, 0.22f, 0.34f) : FLinearColor(0.2f, 0.21f, 0.25f), 16.f,
			[WeakThis] { if (WeakThis.IsValid()) WeakThis->TogglePicker(TEXT("traits")); }, bHas ? &TraitEdge : nullptr, 2.f, bHas ? Equipped[i]->Description : FString()));
	}
	AddContent(Slots);
	if (Picker == TEXT("traits"))
	{
		AddRowLabel(FString::Printf(TEXT("Choose traits (click to equip / unequip, up to %d)"), UWWOLoadoutSubsystem::TraitSlots));
		UWrapBox* R = Flow();
		const bool bFull = Equipped.Num() >= UWWOLoadoutSubsystem::TraitSlots;
		const FLinearColor OffEdge(0.4f, 0.42f, 0.5f);
		for (UWWOTraitData* Option : L->TraitsForFlora())
		{
			if (!Option->bUnlocked) continue;
			const bool bOn = L->IsTraitEquipped(Option);
			UWidget* B = Slot(Option->DisplayName + (bOn ? TEXT("  ✓") : TEXT("")) + TEXT("\n") + Option->Description, FVector2D(250, 110),
				bOn ? FLinearColor(0.16f, 0.22f, 0.34f) : FLinearColor(0.2f, 0.21f, 0.26f), 14.f,
				[WeakThis, WL, Option] { if (WL.IsValid()) WL->ToggleTrait(Option); if (WeakThis.IsValid()) WeakThis->Refresh(); },
				bOn ? &TraitEdge : &OffEdge, bOn ? 4.f : 1.f, FString(), true);
			if (bFull && !bOn) B->SetRenderOpacity(0.5f); // every slot is taken
			R->AddChild(B);
		}
		AddContent(R);
	}
}

void UWWOCharacterMenu::BuildWeaponRows()
{
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L || !L->GetWeapon()) return;
	UWidgetTree* Tree = Styler->WidgetTree;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
	AddRowLabel(TEXT("Equipped"));
	const FLinearColor CurrentEdge(0.85f, 0.85f, 0.9f);
	{
		UHorizontalBox* R = Tree->ConstructWidget<UHorizontalBox>();
		R->AddChildToHorizontalBox(Slot(L->GetWeapon()->DisplayName + TEXT("\n") + L->GetWeapon()->Archetype, FVector2D(320, 110), FLinearColor(0.24f, 0.25f, 0.3f), 20.f,
			[] {}, &CurrentEdge, 3.f));
		AddContent(R);
	}
	AddRowLabel(TEXT("Choose a weapon"));
	UUniformGridPanel* Grid = Tree->ConstructWidget<UUniformGridPanel>();
	const FLinearColor OnEdge(0.9f, 0.9f, 0.95f);
	int32 Index = 0;
	for (UWWOWeaponData* Option : L->Weapons)
	{
		if (!Option->bUnlocked) continue;
		const bool bOn = Option == L->GetWeapon();
		const FString Fire = Option->IsSemiAuto() ? TEXT("semi") : TEXT("auto");
		const FString Dmg = Option->Pellets > 1.f ? FString::Printf(TEXT("%d×%d dmg"), FMath::RoundToInt(Option->Pellets), FMath::RoundToInt(Option->Damage))
			: FString::Printf(TEXT("%d dmg"), FMath::RoundToInt(Option->Damage));
		const FString Stats = FString::Printf(TEXT("%d rpm %s · %s · %d rds"), FMath::RoundToInt(Option->FireRate), *Fire, *Dmg, FMath::RoundToInt(Option->MagazineSize));
		UWidget* B = Slot(Option->DisplayName + (bOn ? TEXT("  ✓") : TEXT("")) + TEXT("\n") + Stats, FVector2D(330, 90), FLinearColor(0.2f, 0.21f, 0.26f), 20.f,
			[WeakThis, WL, Option] { if (WL.IsValid()) WL->EquipWeapon(Option); if (WeakThis.IsValid()) WeakThis->Refresh(); }, bOn ? &OnEdge : nullptr, 4.f);
		// Godot's GridContainer: 2 columns, 12 px apart.
		UUniformGridSlot* GridSlot = Grid->AddChildToUniformGrid(B, Index / 4, Index % 4); // the tile is full width
		GridSlot->SetHorizontalAlignment(HAlign_Fill);
		GridSlot->SetVerticalAlignment(VAlign_Fill);
		++Index;
	}
	Grid->SetSlotPadding(FMargin(0.f, 0.f, 12.f * K, 12.f * K));
	UHorizontalBox* R = Tree->ConstructWidget<UHorizontalBox>();
	R->AddChildToHorizontalBox(Grid);
	AddContent(R);
	BuildModRows();
}

namespace
{
	// Rarity colors (draft): common gray up to legendary gold.
	FLinearColor RarityColor(int32 Rarity)
	{
		switch (Rarity)
		{
		case 2: return FLinearColor(0.35f, 0.75f, 0.4f);
		case 3: return FLinearColor(0.35f, 0.6f, 1.f);
		case 4: return FLinearColor(0.7f, 0.45f, 1.f);
		case 5: return FLinearColor(1.f, 0.75f, 0.3f);
		default: return FLinearColor(0.75f, 0.75f, 0.78f);
		}
	}
}

namespace
{
	// One stat bar: the mod stat it reads, whether lower is better, and how to show the value.
	struct FWWOBarDef
	{
		const TCHAR* Label;
		FName Stat;
		bool bLowerBetter;
		bool bThroughLoadout; // a time the loadout's Apply() scales (shell parts, traits, mods)
		int32 Decimals;
		const TCHAR* Suffix;
	};

	const TArray<FWWOBarDef>& BarDefs()
	{
		static const TArray<FWWOBarDef> Defs = {
			{ TEXT("Damage"), "damage", false, false, 1, TEXT("") },
			{ TEXT("Fire rate"), "fire_rate", false, false, 0, TEXT(" rpm") },
			{ TEXT("Range"), "range", false, false, 0, TEXT(" m") },
			{ TEXT("Magazine"), "magazine_size", false, false, 0, TEXT("") },
			{ TEXT("Reload speed"), "reload_time", true, true, 2, TEXT(" s") },
			{ TEXT("Aim-in speed"), "ads_time", true, true, 2, TEXT(" s") },
			{ TEXT("Handling"), "swap_time", true, true, 2, TEXT(" s") },
			{ TEXT("Stability"), "recoil_up", true, false, 2, TEXT("°") },
			{ TEXT("Side stability"), "recoil_side", true, false, 2, TEXT("°") },
			{ TEXT("Zoom"), "zoom", false, false, 2, TEXT("x") },
			{ TEXT("Steadiness"), "sway", true, false, 2, TEXT("") },
			{ TEXT("Aim move speed"), "ads_move_speed", false, false, 0, TEXT("%") },
		};
		return Defs;
	}

	float SumPercent(const TMap<FName, UWWOModData*>& Set, FName Stat)
	{
		float Total = 0.f;
		for (const auto& Pair : Set)
		{
			if (!Pair.Value) continue;
			for (const FWWOStatModifier& M : Pair.Value->Mods)
			{
				if (M.Stat == Stat && M.Op == EWWOStatOp::PercentAdd) Total += M.Value;
			}
		}
		return Total;
	}
}

void UWWOCharacterMenu::BuildModRows()
{
	// Draft weapon mods: stat bars, every slot at once, and a popup of a slot's mods while the
	// cursor is over it (see UpdateModHover). Then saved configs.
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	SlotButtons.Reset();
	PopupButtons.Reset();
	if (!L || !L->HasMods(L->GetWeapon())) return;
	UWidgetTree* Tree = Styler->WidgetTree;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
	const FLinearColor OnEdge(0.9f, 0.9f, 0.95f);

	AddRowLabel(L->GetWeapon()->DisplayName + TEXT(" frame: stats and mod slots (hover a slot for its mods)"));
	ModRowsStart = Content->GetChildAt(Content->GetChildrenCount() - 1);
	UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();

	// Stat bars.
	StatBars = Tree->ConstructWidget<UWWOStatBars>(UWWOStatBars::StaticClass());
	USizeBox* BarsBox = Tree->ConstructWidget<USizeBox>();
	BarsBox->SetWidthOverride(UWWOStatBars::Width * K);
	BarsBox->SetHeightOverride(UWWOStatBars::RowHeight * BarDefs().Num() * K);
	BarsBox->AddChild(StatBars);
	Row->AddChildToHorizontalBox(BarsBox)->SetVerticalAlignment(VAlign_Top);
	UpdateStatBars(nullptr, NAME_None, false);

	// Every slot, one column.
	UVerticalBox* Slots = Tree->ConstructWidget<UVerticalBox>();
	for (const FName& ModSlot : UWWOLoadoutSubsystem::ModSlots())
	{
		const UWWOModData* Mod = L->GetMod(ModSlot);
		const FLinearColor Edge = Mod ? RarityColor(Mod->Rarity) : FLinearColor(0.4f, 0.42f, 0.5f);
		const bool bOpenSlot = OpenSlot == ModSlot;
		UWidget* W = Slot(UWWOLoadoutSubsystem::ModSlotTitle(ModSlot) + TEXT("\n") + (Mod ? Mod->DisplayName : FString(TEXT("Empty"))),
			FVector2D(210, ModSlotHeight - 8.f), Mod ? WWO::Darkened(Edge, 0.65f) : FLinearColor(0.2f, 0.21f, 0.25f), 17.f,
			[WeakThis, ModSlot] { if (WeakThis.IsValid()) WeakThis->OpenModSlot(ModSlot); }, bOpenSlot ? &OnEdge : &Edge, bOpenSlot ? 5.f : 2.f);
		if (USizeBox* Box = Cast<USizeBox>(W)) SlotButtons.Add({ Cast<UButton>(Box->GetChildAt(0)), ModSlot });
		Slots->AddChildToVerticalBox(W)->SetPadding(FMargin(0, 0, 0, 8.f * K));
	}
	Row->AddChildToHorizontalBox(Slots)->SetPadding(FMargin(28.f * K, 0, 0, 0));

	// The popup: the open slot's mods, lined up with the slot.
	PopupPanel = nullptr;
	if (!OpenSlot.IsNone())
	{
		const int32 SlotIndex = UWWOLoadoutSubsystem::ModSlots().IndexOfByKey(OpenSlot);
		UBorder* Panel = Tree->ConstructWidget<UBorder>();
		Panel->SetBrush(UWWOSettingsMenu::RoundBrush(FLinearColor(0.12f, 0.13f, 0.16f), 8.f, FLinearColor(0.55f, 0.58f, 0.66f), 2.f));
		Panel->SetPadding(FMargin(12.f * K));
		UVerticalBox* List = Tree->ConstructWidget<UVerticalBox>();
		List->AddChildToVerticalBox(Styler->MakeText(UWWOLoadoutSubsystem::ModSlotTitle(OpenSlot) + TEXT(" mods"), 20.f, FLinearColor(1, 1, 1, 0.7f)))
			->SetPadding(FMargin(0, 0, 0, 8.f * K));
		const UWWOModData* Current = L->GetMod(OpenSlot);
		const FName ThisSlot = OpenSlot;
		TArray<UWWOModData*> Options = L->ModsFor(OpenSlot);
		Options.Insert(nullptr, 0); // Empty
		for (UWWOModData* Mod : Options)
		{
			const bool bOn = Mod == Current;
			const FLinearColor Base = Mod ? RarityColor(Mod->Rarity) : FLinearColor(0.4f, 0.42f, 0.5f);
			const FString Title = (Mod ? Mod->DisplayName : FString(TEXT("Empty"))) + (bOn ? TEXT("   ✓ equipped") : TEXT(""));
			const FString Body = Mod ? Mod->Summary() : FString(TEXT("No changes"));
			UWidget* W = Slot(Title + TEXT("\n") + Body, FVector2D(470, 84), Mod ? WWO::Darkened(Base, 0.7f) : FLinearColor(0.2f, 0.21f, 0.25f), 15.f,
				[WeakThis, WL, ThisSlot, Mod] { if (WL.IsValid()) WL->EquipMod(ThisSlot, Mod); if (WeakThis.IsValid()) WeakThis->Refresh(); },
				bOn ? &OnEdge : &Base, bOn ? 5.f : 2.f, FString(), true);
			if (USizeBox* Box = Cast<USizeBox>(W)) PopupButtons.Add({ Cast<UButton>(Box->GetChildAt(0)), Mod });
			List->AddChildToVerticalBox(W)->SetPadding(FMargin(0, 0, 0, 8.f * K));
		}
		Panel->SetContent(List);
		PopupPanel = Panel;
		UHorizontalBoxSlot* PopupSlot = Row->AddChildToHorizontalBox(Panel);
		// Beside the slot, a row up, but never past the bottom of the slot column (3 options fit from row 1.6).
		PopupSlot->SetPadding(FMargin(16.f * K, FMath::Clamp(SlotIndex - 1.f, 0.f, 1.6f) * ModSlotHeight * K, 0, 0));
		PopupSlot->SetVerticalAlignment(VAlign_Top);
	}
	AddContent(Row);

	// Perks of the equipped mods.
	TArray<FString> Perks;
	for (const FName& ModSlot : UWWOLoadoutSubsystem::ModSlots())
	{
		const UWWOModData* Mod = L->GetMod(ModSlot);
		if (Mod && !Mod->PerkText.IsEmpty()) Perks.Add(TEXT("Perk · ") + Mod->PerkText);
	}
	AddContent(Styler->MakeText(Perks.Num() > 0 ? FString::Join(Perks, TEXT("\n")) : TEXT("No perks equipped"), 17.f, FLinearColor(0.6f, 0.85f, 1.f), true));

	// Saved configs: save the current mods, or apply / overwrite / delete a saved one.
	AddRowLabel(TEXT("Configs  (in game: D-pad Down or B switches to the next one)"));
	AddContent(Slot(TEXT("Save current as new config"), FVector2D(320, 56), FLinearColor(0.2f, 0.3f, 0.24f), 18.f,
		[WeakThis, WL] { if (WL.IsValid()) WL->SaveConfig(INDEX_NONE); if (WeakThis.IsValid()) WeakThis->Refresh(); }));
	const TArray<FWWOModConfig>& Configs = L->GetConfigs();
	for (int32 i = 0; i < Configs.Num(); ++i)
	{
		UHorizontalBox* CRow = Tree->ConstructWidget<UHorizontalBox>();
		auto Add = [CRow](UWidget* W) { CRow->AddChildToHorizontalBox(W)->SetPadding(FMargin(CRow->GetChildrenCount() > 1 ? 10.f * K : 0.f, 0, 0, 0)); };
		const bool bActive = L->GetActiveConfig() == i;
		Add(Slot(Configs[i].Name + (bActive ? TEXT("  (last applied)") : TEXT("")) + TEXT("\n") + UWWOLoadoutSubsystem::ConfigSummary(Configs[i], L), FVector2D(560, 70),
			FLinearColor(0.2f, 0.21f, 0.26f), 15.f, [WeakThis, WL, i] { if (WL.IsValid()) WL->ApplyConfig(i); if (WeakThis.IsValid()) WeakThis->Refresh(); },
			bActive ? &OnEdge : nullptr, 2.f, TEXT("Apply"), true));
		Add(Slot(TEXT("Apply"), FVector2D(110, 70), FLinearColor(0.2f, 0.3f, 0.24f), 18.f,
			[WeakThis, WL, i] { if (WL.IsValid()) WL->ApplyConfig(i); if (WeakThis.IsValid()) WeakThis->Refresh(); }));
		Add(Slot(TEXT("Overwrite"), FVector2D(130, 70), FLinearColor(0.3f, 0.27f, 0.18f), 18.f,
			[WeakThis, WL, i] { if (WL.IsValid()) WL->SaveConfig(i); if (WeakThis.IsValid()) WeakThis->Refresh(); }));
		Add(Slot(TEXT("Delete"), FVector2D(110, 70), FLinearColor(0.32f, 0.17f, 0.17f), 18.f,
			[WeakThis, WL, i] { if (WL.IsValid()) WL->DeleteConfig(i); if (WeakThis.IsValid()) WeakThis->Refresh(); }));
		AddContent(CRow);
	}
}

void UWWOCharacterMenu::OpenModSlot(FName ModSlot)
{
	if (OpenSlot == ModSlot) return;
	OpenSlot = ModSlot;
	PopupIdle = 0.f;
	Refresh();
}

void UWWOCharacterMenu::UpdateStatBars(const UWWOModData* PreviewMod, FName PreviewSlot, bool bPreview)
{
	// Values with the equipped mods, and with PreviewMod in PreviewSlot instead (null = empty).
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	const UWWOWeaponData* W = L ? L->GetWeapon() : nullptr;
	if (!StatBars || !W) return;
	TMap<FName, UWWOModData*> Now;
	for (const FName& ModSlot : UWWOLoadoutSubsystem::ModSlots()) Now.Add(ModSlot, L->GetMod(ModSlot));
	TMap<FName, UWWOModData*> Then = Now;
	if (bPreview) Then.Add(PreviewSlot, const_cast<UWWOModData*>(PreviewMod));
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwningPlayerPawn());
	const float SwapBase = P && P->Weapon ? P->Weapon->SwapTime : 0.35f;

	auto Base = [W, SwapBase](FName Stat) -> float
	{
		if (Stat == "damage") return W->Damage;
		if (Stat == "fire_rate") return W->FireRate;
		if (Stat == "range") return W->FireRange;
		if (Stat == "magazine_size") return W->MagazineSize;
		if (Stat == "reload_time") return W->ReloadTime;
		if (Stat == "ads_time") return W->AdsTime;
		if (Stat == "swap_time") return SwapBase;
		if (Stat == "recoil_up") return W->RecoilUp;
		if (Stat == "recoil_side") return W->RecoilSide;
		if (Stat == "zoom") return W->Zoom;
		if (Stat == "sway") return W->Sway;
		if (Stat == "ads_move_speed") return W->AdsMoveSpeed;
		return 1.f;
	};
	auto Value = [L, &Base](const FWWOBarDef& D, const TMap<FName, UWWOModData*>& Set) -> float
	{
		const float B = Base(D.Stat);
		const float Pct = SumPercent(Set, D.Stat);
		if (D.Stat == "zoom") return 1.f + (B - 1.f) * FMath::Max(1.f + Pct, 0.f);
		if (D.Stat == "magazine_size") return FMath::Max(1.f, FMath::RoundToFloat(B * FMath::Max(1.f + Pct, 0.f)));
		if (D.bThroughLoadout)
		{
			// Apply() = base * (1 + shell parts + traits + equipped mods): swap the mods' share.
			return FMath::Max(L->Apply(D.Stat, B) + B * (Pct - L->ModPercent(D.Stat)), 0.f);
		}
		return B * FMath::Max(1.f + Pct, 0.f);
	};
	const TMap<FName, UWWOModData*> NoMods;
	StatBars->Rows.Reset();
	for (const FWWOBarDef& D : BarDefs())
	{
		const float Bare = FMath::Max(Value(D, NoMods), 0.0001f);
		auto Fill = [&D, Bare](float V) { return 0.5f * (D.bLowerBetter ? Bare / FMath::Max(V, 0.0001f) : V / Bare); };
		auto Shown = [&D](float V) { return FString::Printf(TEXT("%.*f%s"), D.Decimals, D.Stat == FName("ads_move_speed") ? V * 100.f : V, D.Suffix); };
		const float A = Value(D, Now), B = Value(D, Then);
		FWWOStatBar R;
		R.Label = D.Label;
		R.Fill = Fill(A);
		R.PreviewFill = Fill(B);
		R.Value = Shown(A);
		R.PreviewValue = Shown(B);
		R.bPreview = bPreview;
		StatBars->Rows.Add(R);
	}
}

void UWWOCharacterMenu::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bOpen && Expanded == TEXT("Weapons")) UpdateModHover(InDeltaTime);
}

void UWWOCharacterMenu::UpdateModHover(float DeltaTime)
{
	// Hovering a slot opens its popup; the popup stays while the cursor is on it or a slot, and
	// closes a moment after it leaves both. Hovering a mod in the popup previews it on the bars.
	bool bOnSlot = false;
	for (const auto& Pair : SlotButtons)
	{
		if (Pair.Key.IsValid() && Pair.Key->IsHovered() && AutoTestPreview.IsNone())
		{
			bOnSlot = true;
			if (Pair.Value != OpenSlot)
			{
				OpenModSlot(Pair.Value);
				return; // rebuilt
			}
		}
	}
	const bool bOnPopup = PopupPanel && PopupPanel->IsHovered();
	if (!OpenSlot.IsNone() && !bOnSlot && !bOnPopup && AutoTestPreview.IsNone())
	{
		PopupIdle += DeltaTime;
		if (PopupIdle > 0.35f)
		{
			OpenSlot = NAME_None;
			Refresh();
			return;
		}
	}
	else
	{
		PopupIdle = 0.f;
	}
	const UWWOModData* Hovered = nullptr;
	bool bPreview = false;
	for (const auto& Pair : PopupButtons)
	{
		if (Pair.Key.IsValid() && Pair.Key->IsHovered())
		{
			Hovered = Pair.Value.Get();
			bPreview = true;
		}
	}
	if (!AutoTestPreview.IsNone())
	{
		// The autotest previews a mod by id ("empty" for an empty slot).
		for (const auto& Pair : PopupButtons)
		{
			if ((Pair.Value.IsValid() && Pair.Value->Id == AutoTestPreview) || (!Pair.Value.IsValid() && AutoTestPreview == "empty"))
			{
				Hovered = Pair.Value.Get();
				bPreview = true;
			}
		}
	}
	UpdateStatBars(Hovered, OpenSlot, bPreview);
}

void UWWOCharacterMenu::BuildFrameRows()
{
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return;
	UWidgetTree* Tree = Styler->WidgetTree;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
	const FLinearColor OnEdge(0.9f, 0.9f, 0.95f);
	for (const FName& FrameSlot : UWWOLoadoutSubsystem::FrameSlots())
	{
		FString Title = FrameSlot.ToString();
		Title = Title.Left(1).ToUpper() + Title.Mid(1);
		AddRowLabel(Title);
		UWrapBox* Row = Tree->ConstructWidget<UWrapBox>();
		Row->SetInnerSlotPadding(FVector2D(10.f * K, 10.f * K));
		for (UWWOFramePartData* Part : L->FramePartsFor(FrameSlot))
		{
			if (!Part->bUnlocked) continue;
			const bool bOn = Part == L->GetFramePart(FrameSlot);
			Row->AddChild(Slot(Part->DisplayName + (bOn ? TEXT("  ✓") : TEXT("")) + TEXT("\n") + Part->Summary(), FVector2D(200, 120), FLinearColor(0.2f, 0.22f, 0.27f), 15.f,
				[WeakThis, WL, Part] { if (WL.IsValid()) WL->EquipFramePart(Part); if (WeakThis.IsValid()) WeakThis->Refresh(); }, bOn ? &OnEdge : nullptr, 4.f));
		}
		AddContent(Row);
	}
	AddRowLabel(TEXT("Net effect"));
	const TArray<FString> Lines = L->GroupSummary();
	AddContent(Styler->MakeText(Lines.Num() > 0 ? FString::Join(Lines, TEXT("\n")) : TEXT("No changes (all Standard parts)"), 18.f));
}

// --- Autotest hooks -------------------------------------------------------------------------

FString UWWOCharacterMenu::DebugState() const
{
	return FString::Printf(TEXT("expanded '%s' picker '%s'"), *Expanded, *Picker);
}

void UWWOCharacterMenu::AutoTestShow(const FString& Tile, const FString& PickerName)
{
	// "mod_<slot>" opens that slot's popup; "mod_<slot>:<mod id>" also previews that mod.
	if (Expanded != Tile) Expand(Tile);
	FString Name = PickerName, ModId;
	PickerName.Split(TEXT(":"), &Name, &ModId);
	AutoTestPreview = ModId.IsEmpty() ? NAME_None : FName(*ModId);
	if (Name.StartsWith(TEXT("mod_")))
	{
		OpenSlot = FName(*Name.Mid(4));
		Picker.Reset();
	}
	else
	{
		OpenSlot = NAME_None;
		Picker = Name;
	}
	Refresh();
}

void UWWOCharacterMenu::AutoTestScroll(float Offset)
{
	// A negative offset scrolls the mod slots to the top.
	if (Offset < 0.f && ModRowsStart) ExpandedScroll->ScrollWidgetIntoView(ModRowsStart, false, EDescendantScrollDestination::TopOrLeft);
	else ExpandedScroll->SetScrollOffset(FMath::Min(Offset, ExpandedScroll->GetScrollOffsetOfEnd()));
}
