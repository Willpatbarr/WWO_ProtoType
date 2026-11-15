#include "UI/WWOCharacterMenu.h"
#include "Systems/WWOSound.h"
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
#include "Components/ScaleBox.h"
#include "UI/WWOMenuCursor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
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
	const FLinearColor PageColor(0.1f, 0.11f, 0.13f); // what the preview's backdrop comes out as
	const FLinearColor CardColor(0.115f, 0.125f, 0.148f);
	const FLinearColor CardEdge(1.f, 1.f, 1.f, 0.55f);
	const FLinearColor TraitFill(0.16f, 0.22f, 0.34f), TraitEdge(0.55f, 0.7f, 1.f);       // an equipped trait, as on the Flora tab
	const FLinearColor TraitOffFill(0.2f, 0.21f, 0.26f), TraitOffEdge(0.4f, 0.42f, 0.5f); // one that isn't
	const TCHAR* const TabNames[] = { TEXT("LOADOUT"), TEXT("FLORA"), TEXT("WEAPON"), TEXT("SHELL") }; // the shell is the frame parts
	FString KindText(EWWOAbilityKind Kind);
	const TCHAR* const TabTiles[] = { TEXT(""), TEXT("Flora"), TEXT("Weapons"), TEXT("Frame Parts") };

	// Text in capitals with wide letter spacing (the mockup's label style).
	UTextBlock* Spaced(UWWOSettingsMenu* Styler, const FString& Text, float FontPx, const FLinearColor& Color, int32 Spacing, bool bWrap = false)
	{
		UTextBlock* T = Styler->MakeText(Text.ToUpper(), FontPx, Color, bWrap);
		FSlateFontInfo F = T->GetFont();
		F.LetterSpacing = Spacing;
		T->SetFont(F);
		return T;
	}

	// A thin line across (or, with bUpright, down).
	UWidget* Rule(UWidgetTree* Tree, const FLinearColor& Color, bool bUpright = false, float Length = 0.f)
	{
		UBorder* Line = Tree->ConstructWidget<UBorder>();
		Line->SetBrushColor(WWO::Lin(Color));
		USizeBox* Box = Tree->ConstructWidget<USizeBox>();
		if (bUpright) { Box->SetWidthOverride(1.5f); if (Length > 0.f) Box->SetHeightOverride(Length * K); }
		else Box->SetHeightOverride(1.5f);
		Box->AddChild(Line);
		return Box;
	}

	// Up to two initials, standing in for an icon until there is art.
	FString Initials(const FString& Name)
	{
		TArray<FString> Words;
		Name.ParseIntoArray(Words, TEXT(" "));
		FString Out;
		for (const FString& Word : Words) if (Out.Len() < 2 && !Word.IsEmpty()) Out += Word.Left(1).ToUpper();
		return Out;
	}
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
	// (Now nearer, 2.75 m and 0.98 m up, so the character fills the menu's tall middle column.)
	Capture->SetRelativeLocationAndRotation(FVector(275.f, 0.f, 98.f), FRotator(0.f, 180.f, 0.f));
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
	// Nothing that shades the flat backdrop unevenly: it has to match the page around it.
	Capture->PostProcessSettings.bOverride_VignetteIntensity = true;
	Capture->PostProcessSettings.VignetteIntensity = 0.f;
	Capture->PostProcessSettings.bOverride_BloomIntensity = true;
	Capture->PostProcessSettings.BloomIntensity = 0.f;
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
	Super::Tick(DeltaSeconds); // it stands still; the menu turns it (UWWOCharacterMenu::UpdateTurn)
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
	// A section's name: small spaced capitals, like the Loadout tab's labels.
	UTextBlock* Label = Spaced(Styler, Text, 13.f, FLinearColor(1, 1, 1, 0.6f), 220);
	Content->AddChildToVerticalBox(Label)->SetPadding(FMargin(0, Content->GetChildrenCount() > 0 ? 22.f * K : 0.f, 0, 2.f * K));
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
	Style.SetNormal(UWWOSettingsMenu::RoundBrush(Color, 5.f, Edge, Width)); // the menu's 5 px corners
	Style.SetHovered(UWWOSettingsMenu::RoundBrush(WWO::Lightened(Color, 0.12f), 5.f, FLinearColor::White, Width));
	Style.SetPressed(UWWOSettingsMenu::RoundBrush(WWO::Lightened(Color, 0.2f), 5.f, FLinearColor::White, Width));
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
		TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
		// The whole window, in the preview backdrop's shade so the character has no visible frame.
		UBorder* Page = Tree->ConstructWidget<UBorder>();
		Page->SetBrushColor(WWO::Lin(PageColor));
		Page->SetPadding(FMargin(40.f * K, 12.f * K, 40.f * K, 28.f * K));
		UOverlaySlot* PageSlot = Root->AddChildToOverlay(Page);
		PageSlot->SetHorizontalAlignment(HAlign_Fill);
		PageSlot->SetVerticalAlignment(VAlign_Fill);
		UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>();
		Page->SetContent(Column);

		// Header: the game's name, the tabs, the keys.
		UHorizontalBox* Header = Tree->ConstructWidget<UHorizontalBox>();
		UHorizontalBox* Name = Tree->ConstructWidget<UHorizontalBox>();
		Name->AddChildToHorizontalBox(UWWOSettingsMenu::MakeLogo(Tree, false, FVector2D(30.f, 30.f) * K, Spaced(Styler, TEXT("//"), 26.f, FLinearColor(0.72f, 0.5f, 1.f), 0)))->SetVerticalAlignment(VAlign_Center);
		UHorizontalBoxSlot* NameText = Name->AddChildToHorizontalBox(Spaced(Styler, TEXT("WHAT WAS OURS"), 17.f, FLinearColor::White, 420));
		NameText->SetVerticalAlignment(VAlign_Center);
		NameText->SetPadding(FMargin(18.f * K, 0, 0, 0));
		UHorizontalBoxSlot* NameSlot = Header->AddChildToHorizontalBox(Name);
		NameSlot->SetSize(WWOFillSize(1.f));
		NameSlot->SetVerticalAlignment(VAlign_Center);
		UHorizontalBox* Tabs = Tree->ConstructWidget<UHorizontalBox>();
		for (int32 i = 0; i < UE_ARRAY_COUNT(TabNames); ++i)
		{
			if (i > 0)
			{
				UHorizontalBoxSlot* Bar = Tabs->AddChildToHorizontalBox(Spaced(Styler, TEXT("|"), 15.f, FLinearColor(1, 1, 1, 0.25f), 0));
				Bar->SetVerticalAlignment(VAlign_Center);
				Bar->SetPadding(FMargin(26.f * K, 0));
			}
			UVerticalBox* Tab = Tree->ConstructWidget<UVerticalBox>();
			UTextBlock* Label = Spaced(Styler, TabNames[i], 17.f, FLinearColor::White, 180);
			Tab->AddChildToVerticalBox(Label)->SetPadding(FMargin(8.f * K, 6.f * K, 4.f * K, 6.f * K));
			UBorder* Line = Tree->ConstructWidget<UBorder>();
			USizeBox* LineBox = Tree->ConstructWidget<USizeBox>();
			LineBox->SetHeightOverride(3.f * K);
			LineBox->AddChild(Line);
			Tab->AddChildToVerticalBox(LineBox);
			TabLabels.Add(Label);
			TabLines.Add(Line);
			Tabs->AddChildToHorizontalBox(Card(Tab, [WeakThis, i] { if (WeakThis.IsValid()) WeakThis->SetTab(i); }, FLinearColor(0, 0, 0, 0), FLinearColor(0, 0, 0, 0), 0.f, FMargin(0)))
				->SetVerticalAlignment(VAlign_Center);
		}
		UHorizontalBoxSlot* TabsSlot = Header->AddChildToHorizontalBox(Tabs);
		TabsSlot->SetVerticalAlignment(VAlign_Center);
		UTextBlock* Keys = Spaced(Styler, TEXT("Q / E  TABS   ·   A / D  OR DRAG  TURN   ·   ESC  BACK"), 11.f, FLinearColor(1, 1, 1, 0.4f), 120);
		Keys->SetJustification(ETextJustify::Right);
		UHorizontalBoxSlot* KeysSlot = Header->AddChildToHorizontalBox(Keys);
		KeysSlot->SetSize(WWOFillSize(1.f));
		KeysSlot->SetVerticalAlignment(VAlign_Center);
		Column->AddChildToVerticalBox(Header);
		Column->AddChildToVerticalBox(Rule(Tree, FLinearColor(1, 1, 1, 0.3f)))->SetPadding(FMargin(0, 4.f * K, 0, 0));

		// The character, shown on every tab (one render target, two places it can sit).
		PreviewActor = GetWorld()->SpawnActor<AWWOPreviewActor>(PreviewSpot, FRotator::ZeroRotator, WWO::AlwaysSpawn());
		auto MakePreview = [this, Tree]() -> UWidget*
		{
			UImage* Image = Tree->ConstructWidget<UImage>();
			if (PreviewActor && PreviewActor->Target)
			{
				FSlateBrush Brush;
				Brush.SetResourceObject(PreviewActor->Target);
				Brush.ImageSize = AWWOPreviewActor::Size * K;
				Image->SetBrush(Brush);
			}
			UScaleBox* Fit = Tree->ConstructWidget<UScaleBox>(); // keeps its shape, as big as fits
			Fit->SetStretch(EStretch::ScaleToFit);
			Fit->AddChild(Image);
			return Fit;
		};

		TileSwitcher = Tree->ConstructWidget<UWidgetSwitcher>();
		Column->AddChildToVerticalBox(TileSwitcher)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		// Loadout: three columns, filled by BuildOverview (the middle one is the character).
		Overview = Tree->ConstructWidget<UHorizontalBox>();
		OverviewPreview = MakePreview();
		TileSwitcher->AddChild(Overview);

		// The editors: the open tab's rows beside the character.
		UHorizontalBox* Editor = Tree->ConstructWidget<UHorizontalBox>();
		UBorder* ExpandedPanel = Tree->ConstructWidget<UBorder>();
		ExpandedPanel->SetBrush(UWWOSettingsMenu::RoundBrush(FLinearColor(0.13f, 0.14f, 0.165f), 6.f, FLinearColor(1, 1, 1, 0.18f), 1.f));
		ExpandedPanel->SetPadding(FMargin(28.f * K));
		ExpandedTitle = Styler->MakeText(TEXT(""), 40.f); // the tabs say where you are; kept for the code that sets it
		ExpandedScroll = Tree->ConstructWidget<UScrollBox>();
		Content = Tree->ConstructWidget<UVerticalBox>();
		ExpandedScroll->AddChild(Content);
		ExpandedPanel->SetContent(ExpandedScroll);
		UHorizontalBoxSlot* PanelSlot = Editor->AddChildToHorizontalBox(ExpandedPanel);
		PanelSlot->SetSize(WWOFillSize(1.6f));
		PanelSlot->SetPadding(FMargin(0, 24.f * K, 0, 0));
		Preview = MakePreview();
		UHorizontalBoxSlot* PreviewSlot = Editor->AddChildToHorizontalBox(Preview);
		PreviewSlot->SetSize(WWOFillSize(1.f));
		PreviewSlot->SetHorizontalAlignment(HAlign_Fill);
		PreviewSlot->SetVerticalAlignment(VAlign_Fill);
		TileSwitcher->AddChild(Editor);
		HeaderHandlers = CardHandlers; // the tabs were made with Card: keep theirs past BuildOverview's reset
	}
	SetPreviewLive(true);
	if (PreviewActor) PreviewActor->Turntable->SetRelativeRotation(FRotator::ZeroRotator); // facing you
	BuildOverview();
	UWWOSound::PlayFlat(this, "ui_open");
	bOpen = true;
	Expanded.Reset();
	Picker.Reset();
	TileSwitcher->SetActiveWidgetIndex(0);
	UpdateTabs();
	SetVisibility(ESlateVisibility::Visible);
}

void UWWOCharacterMenu::Close()
{
	CloseHoverPicker();
	if (bOpen) UWWOSound::PlayFlat(this, "ui_close");
	bOpen = false;
	SetVisibility(ESlateVisibility::Collapsed);
	SetPreviewLive(false);
	if (UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this)) L->FlushSave();
}

void UWWOCharacterMenu::SetPreviewLive(bool bLive)
{
	// A scene capture left on renders the preview every frame of play, menu or no menu.
	if (!PreviewActor || !PreviewActor->Capture) return;
	PreviewActor->Capture->bCaptureEveryFrame = bLive;
	PreviewActor->Capture->SetVisibility(bLive);
	PreviewActor->SetActorTickEnabled(bLive);
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
	if (Preview) Preview->SetVisibility(Tile == TEXT("Weapons") ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	OpenSlot = NAME_None;
	Expanded = Tile;
	Picker.Reset();
	ExpandedTitle->SetText(FText::FromString(Tile));
	Rebuild();
	ExpandedScroll->ScrollToStart();
	TileSwitcher->SetActiveWidgetIndex(1);
	UpdateTabs();
}

void UWWOCharacterMenu::Collapse()
{
	OpenSlot = NAME_None;
	Expanded.Reset();
	Picker.Reset();
	BuildOverview(); // what was changed in an editor shows on the cards
	TileSwitcher->SetActiveWidgetIndex(0);
	UpdateTabs();
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

UWidget* UWWOCharacterMenu::Card(UWidget* Inside, TFunction<void()> Fn, const FLinearColor& Fill, const FLinearColor& Edge, float EdgeWidth, const FMargin& Padding)
{
	// A bordered box you can click (or, with no Fn, just a box).
	UWidgetTree* Tree = Styler->WidgetTree;
	if (!Fn)
	{
		UBorder* Box = Tree->ConstructWidget<UBorder>();
		Box->SetBrush(UWWOSettingsMenu::RoundBrush(Fill, 5.f, Edge, EdgeWidth));
		Box->SetPadding(Padding * K);
		Box->SetContent(Inside);
		return Box;
	}
	UButton* B = Tree->ConstructWidget<UButton>();
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	B->IsFocusable = false; // the cursor drives everything, not focus
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	FButtonStyle Style;
	const bool bPlain = Fill.A <= 0.f;
	Style.SetNormal(UWWOSettingsMenu::RoundBrush(Fill, 5.f, Edge, EdgeWidth));
	Style.SetHovered(UWWOSettingsMenu::RoundBrush(bPlain ? FLinearColor(1, 1, 1, 0.06f) : WWO::Lightened(Fill, 0.08f), 5.f, bPlain ? Edge : FLinearColor(1, 1, 1, 0.9f), EdgeWidth));
	Style.SetPressed(UWWOSettingsMenu::RoundBrush(bPlain ? FLinearColor(1, 1, 1, 0.1f) : WWO::Lightened(Fill, 0.14f), 5.f, bPlain ? Edge : FLinearColor::White, EdgeWidth));
	Style.SetNormalPadding(FMargin(0));
	Style.SetPressedPadding(FMargin(0));
	B->SetStyle(Style);
	UButtonSlot* Slot = Cast<UButtonSlot>(B->AddChild(Inside));
	Slot->SetPadding(Padding * K);
	Slot->SetHorizontalAlignment(HAlign_Fill);
	Slot->SetVerticalAlignment(VAlign_Fill);
	UWWOClickHandler* H = NewObject<UWWOClickHandler>(this);
	H->Fn = [Fn = MoveTemp(Fn), Weak = TWeakObjectPtr<UWWOCharacterMenu>(this)] { if (Weak.IsValid()) UWWOSound::PlayFlat(Weak.Get(), "ui_click"); if (Fn) Fn(); };
	B->OnClicked.AddDynamic(H, &UWWOClickHandler::Handle);
	CardHandlers.Add(H);
	return B;
}

int32 UWWOCharacterMenu::TabIndex() const
{
	for (int32 i = 0; i < UE_ARRAY_COUNT(TabTiles); ++i) if (Expanded == TabTiles[i]) return i;
	return 0;
}

void UWWOCharacterMenu::SetTab(int32 Index)
{
	Index = (Index % UE_ARRAY_COUNT(TabTiles) + UE_ARRAY_COUNT(TabTiles)) % UE_ARRAY_COUNT(TabTiles);
	if (Index == TabIndex()) return;
	UWWOSound::PlayFlat(this, "ui_tab");
	if (Index == 0) Collapse();
	else Expand(TabTiles[Index]);
}

void UWWOCharacterMenu::CycleTab(int32 Step)
{
	SetTab(TabIndex() + Step);
}

void UWWOCharacterMenu::UpdateTabs()
{
	// The open tab is white over a line in the flora's color; the others are dim.
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	const FLinearColor Accent = L && L->GetFlora() ? WWO::Lightened(L->GetFlora()->Color, 0.15f) : FLinearColor(0.72f, 0.5f, 1.f);
	const int32 Now = TabIndex();
	for (int32 i = 0; i < TabLabels.Num(); ++i)
	{
		TabLabels[i]->SetColorAndOpacity(FSlateColor(WWO::Lin(i == Now ? FLinearColor::White : FLinearColor(1, 1, 1, 0.6f))));
		TabLines[i]->SetBrushColor(WWO::Lin(i == Now ? Accent : FLinearColor(0, 0, 0, 0)));
	}
}

void UWWOCharacterMenu::BuildOverview()
{
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L || !Overview) return;
	UWidgetTree* Tree = Styler->WidgetTree;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	CardHandlers.Reset();
	HoverTiles.Reset();
	CloseHoverPicker();
	Overview->ClearChildren();
	const UWWOFloraData* Flora = L->GetFlora();
	const FLinearColor Accent = Flora ? WWO::Lightened(Flora->Color, 0.15f) : FLinearColor(0.72f, 0.5f, 1.f);
	const FLinearColor Dim(1, 1, 1, 0.62f);
	auto Open = [WeakThis](const TCHAR* Tile) { return [WeakThis, Tile] { if (WeakThis.IsValid()) WeakThis->Expand(Tile); }; };
	// An icon's stand-in: the item's initials in a bordered square.
	auto Icon = [&](const FString& Name, float Side, float FontPx, const FLinearColor& Edge) -> UWidget*
	{
		UTextBlock* Text = Spaced(Styler, Initials(Name), FontPx, Edge, 40);
		Text->SetJustification(ETextJustify::Center);
		UBorder* Box = Tree->ConstructWidget<UBorder>();
		Box->SetBrush(UWWOSettingsMenu::RoundBrush(CardColor, 5.f, Edge, 1.5f));
		Box->SetHorizontalAlignment(HAlign_Center);
		Box->SetVerticalAlignment(VAlign_Center);
		Box->SetContent(Text);
		USizeBox* Size = Tree->ConstructWidget<USizeBox>();
		Size->SetWidthOverride(Side * K);
		Size->SetHeightOverride(Side * K);
		Size->AddChild(Box);
		return Size;
	};

	// --- Left: the frame's stats, then everything in the build with what it does.
	UVerticalBox* Left = Tree->ConstructWidget<UVerticalBox>();
	static const TCHAR* const Groups[] = { TEXT("agility"), TEXT("dexterity"), TEXT("aerial"), TEXT("durability") };
	StatBars4.Reset();
	NoteRows.Reset();
	OutlinedSource.Reset();
	for (const TCHAR* Group : Groups)
	{
		// White for what you have, a colored piece for what a hovered part adds or takes
		// (UpdateStatPreview), dark for the rest.
		FStatBar Bar;
		Bar.Group = Group;
		UHorizontalBox* Title = Tree->ConstructWidget<UHorizontalBox>();
		Title->AddChildToHorizontalBox(Spaced(Styler, Group, 18.f, FLinearColor::White, 260))->SetSize(WWOFillSize(1.f));
		Bar.Change = Spaced(Styler, TEXT(""), 14.f, Dim, 60);
		UHorizontalBoxSlot* ChangeSlot = Title->AddChildToHorizontalBox(Bar.Change);
		ChangeSlot->SetVerticalAlignment(VAlign_Bottom);
		ChangeSlot->SetPadding(FMargin(0, 0, 10.f * K, 0));
		Bar.Value = Spaced(Styler, TEXT(""), 14.f, Dim, 60);
		Title->AddChildToHorizontalBox(Bar.Value)->SetVerticalAlignment(VAlign_Bottom);
		Left->AddChildToVerticalBox(Title)->SetPadding(FMargin(0, Left->GetChildrenCount() > 1 ? 22.f * K : 0.f, 0, 8.f * K));
		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();
		Bar.Have = Tree->ConstructWidget<UBorder>();
		Bar.Have->SetBrushColor(WWO::Lin(FLinearColor(0.93f, 0.93f, 0.95f)));
		Bar.Shift = Tree->ConstructWidget<UBorder>();
		Bar.Rest = Tree->ConstructWidget<UBorder>();
		Bar.Rest->SetBrushColor(WWO::Lin(FLinearColor(0.24f, 0.25f, 0.28f)));
		Row->AddChildToHorizontalBox(Bar.Have);
		Row->AddChildToHorizontalBox(Bar.Shift);
		Row->AddChildToHorizontalBox(Bar.Rest);
		USizeBox* BarBox = Tree->ConstructWidget<USizeBox>();
		BarBox->SetHeightOverride(11.f * K);
		BarBox->AddChild(Row);
		Left->AddChildToVerticalBox(BarBox);
		StatBars4.Add(Bar);
	}
	PreviewPart = nullptr;
	UpdateStatPreview(nullptr, false);
	Left->AddChildToVerticalBox(Rule(Tree, FLinearColor(1, 1, 1, 0.3f)))->SetPadding(FMargin(-24.f * K, 38.f * K, 0, 0));
	UHorizontalBox* BuildTitle = Tree->ConstructWidget<UHorizontalBox>();
	BuildTitle->AddChildToHorizontalBox(Spaced(Styler, TEXT("//"), 20.f, Accent, 0))->SetVerticalAlignment(VAlign_Center);
	BuildTitle->AddChildToHorizontalBox(Spaced(Styler, TEXT("BUILD"), 18.f, FLinearColor::White, 380))->SetPadding(FMargin(16.f * K, 0, 0, 0));
	Left->AddChildToVerticalBox(BuildTitle)->SetPadding(FMargin(-18.f * K, 16.f * K, 0, 16.f * K));
	Left->AddChildToVerticalBox(Rule(Tree, FLinearColor(1, 1, 1, 0.3f)))->SetPadding(FMargin(-24.f * K, 0, 0, 12.f * K));
	// Notes on the build: what it does to things, and what it does for you, each with where it
	// comes from. (What each item is belongs to the Flora tab.)
	UScrollBox* List = Tree->ConstructWidget<UScrollBox>();
	auto Pad = [](UWidget* W, const FMargin& M) { if (UScrollBoxSlot* S = Cast<UScrollBoxSlot>(W->Slot)) S->SetPadding(M); };
	auto Heading = [&](const FString& Text)
	{
		UTextBlock* T = Spaced(Styler, Text, 12.f, Accent, 300);
		List->AddChild(T);
		Pad(T, FMargin(0, List->GetChildrenCount() > 1 ? 18.f * K : 2.f * K, 0, 6.f * K));
	};
	auto Note = [&](const FString& What, const FString& From)
	{
		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();
		UHorizontalBoxSlot* WhatSlot = Row->AddChildToHorizontalBox(Spaced(Styler, What, 13.f, FLinearColor::White, 90, true));
		WhatSlot->SetSize(WWOFillSize(1.f));
		WhatSlot->SetVerticalAlignment(VAlign_Center);
		UTextBlock* Source = Spaced(Styler, From, 10.f, Dim, 120);
		Source->SetJustification(ETextJustify::Right);
		UHorizontalBoxSlot* SourceSlot = Row->AddChildToHorizontalBox(Source);
		SourceSlot->SetVerticalAlignment(VAlign_Center);
		SourceSlot->SetPadding(FMargin(12.f * K, 0, 4.f * K, 0));
		// In a frame that lights up while the cursor is on the item the note comes from
		// (UpdateNoteOutline). The frame is always there, so nothing moves when it does.
		UBorder* Frame = Tree->ConstructWidget<UBorder>();
		Frame->SetBrush(UWWOSettingsMenu::RoundBrush(FLinearColor(0, 0, 0, 0), 4.f));
		Frame->SetPadding(FMargin(8.f * K, 4.f * K, 4.f * K, 4.f * K));
		Frame->SetContent(Row);
		List->AddChild(Frame);
		Pad(Frame, FMargin(-8.f * K, 1.f * K, 0, 1.f * K));
		if (!From.IsEmpty()) NoteRows.Add({ Frame, From });
	};
	auto Num = [](float V) { return FString::SanitizeFloat(FMath::RoundToFloat(V * 10.f) / 10.f, 0); };
	auto Pct = [](float V) { return FString::Printf(TEXT("%+d%%"), FMath::RoundToInt(V * 100.f)); };
	const TArray<UWWOTraitData*> BuildTraits = L->EquippedTraits();
	Heading(TEXT("Effects you apply"));
	int32 Effects = 0;
	if (const UWWOGrenadeData* G = L->GetGrenade())
	{
		FString What;
		switch (G->Kind)
		{
		case EWWOGrenadeKind::Pool: What = FString::Printf(TEXT("Damage pool · %s/s for %s s"), *Num(G->PoolDps), *Num(G->PoolTime)); break;
		case EWWOGrenadeKind::Spike: What = FString::Printf(TEXT("Poison · %s/s for %s s"), *Num(G->PoisonDps), *Num(G->PoisonTime)); break;
		case EWWOGrenadeKind::Tripmine: What = FString::Printf(TEXT("Slow · %d%% for %s s"), FMath::RoundToInt(G->SlowAmount * 100.f), *Num(G->SlowTime)); break;
		case EWWOGrenadeKind::Wall: What = FString::Printf(TEXT("Wall · %s s"), *Num(G->WallTime)); break;
		case EWWOGrenadeKind::Blind: What = FString::Printf(TEXT("Blind · %s s"), *Num(G->BlindTime)); break;
		default: What = FString::Printf(TEXT("Blast · %s dmg, %s m"), *Num(G->Damage), *Num(G->BlastRadius)); break;
		}
		Note(What, G->DisplayName);
		++Effects;
	}
	for (int32 i = 0; i < UWWOLoadoutSubsystem::AbilitySlots; ++i)
	{
		const UWWOAbilityData* A = L->GetAbility(i);
		if (!A) continue;
		FString What;
		if (A->Id == "static_charge") What = FString::Printf(TEXT("Chain lightning · %s dmg, %s jumps"), *Num(A->ChainDamage), *Num(A->ChainJumps));
		else if (A->Id == "seedburst") What = FString::Printf(TEXT("Seed grenades · %s more blasts"), *Num(A->SubCount));
		else if (A->Id == "spore_fog_damage") What = FString::Printf(TEXT("Fog damage · %s/s"), *Num(A->FogDps));
		else if (A->Id == "spore_dash") What = FString::Printf(TEXT("Hiding fog · %s s"), *Num(A->DashFogTime));
		else if (A->Id == "spore_patches") What = FString::Printf(TEXT("Fog patches · %s s each"), *Num(A->PatchTime));
		else if (A->Id == "root_bombs") What = FString::Printf(TEXT("Root bombs · %s dmg, %s m"), *Num(A->BombDamage), *Num(A->BombRadius));
		else if (A->Id == "shield_share") What = FString::Printf(TEXT("Shield teammates · +%s"), *Num(A->ShieldAmount));
		if (!What.IsEmpty()) { Note(What, A->DisplayName); ++Effects; }
	}
	if (Flora && !Flora->UltName.IsEmpty()) { Note(TEXT("Ult · ") + Flora->UltName, Flora->DisplayName); ++Effects; }
	// Traits that change those effects rather than your own stats.
	static const TMap<FName, FString> EffectNames = {
		{ "fog_longer", TEXT("fog time") }, { "wall_longer", TEXT("wall time") }, { "slow_longer", TEXT("slow time") },
		{ "more_bounce", TEXT("grenade bounce") }, { "blast_bigger", TEXT("blast size") }, { "poison_damage", TEXT("poison, pool and fog damage") },
	};
	for (const UWWOTraitData* T : BuildTraits)
	{
		if (T->Effect.IsNone()) continue;
		const FString* Named = EffectNames.Find(T->Effect);
		Note(Named ? Pct(T->Amount) + TEXT(" ") + *Named : T->Description, T->DisplayName);
		++Effects;
	}
	if (Effects == 0) Note(TEXT("None"), TEXT(""));
	Heading(TEXT("Buffs on you"));
	int32 Buffs = 0;
	for (int32 i = 0; i < UWWOLoadoutSubsystem::AbilitySlots; ++i)
	{
		const UWWOAbilityData* A = L->GetAbility(i);
		if (!A) continue;
		auto Buff = [&](const FString& What) { Note(What, A->DisplayName); ++Buffs; };
		if (A->Id == "overgrowth")
		{
			if (!FMath::IsNearlyEqual(A->SpeedMult, 1.f)) Buff(Pct(A->SpeedMult - 1.f) + TEXT(" move speed"));
			if (A->ExtraJumps > 0.f) Buff(FString::Printf(TEXT("+%s jump"), *Num(A->ExtraJumps)));
		}
		else if (A->Id == "thornfist") Buff(FString::Printf(TEXT("Melee damage x%s for %s s"), *Num(A->MeleeDamageMult), *Num(A->Duration)));
		else if (A->Id == "melee_guard") Buff(Pct(-A->GuardReduction) + TEXT(" melee damage taken after a melee hit"));
		else if (A->Id == "extra_grenade") Buff(FString::Printf(TEXT("+%s grenade charge"), *Num(A->ExtraCharges)));
	}
	for (const UWWOTraitData* T : BuildTraits)
	{
		for (const FWWOStatModifier& Mod : T->Mods)
		{
			Note((Mod.Op == EWWOStatOp::Flat ? FString::Printf(TEXT("%+g"), Mod.Value) : Pct(Mod.Op == EWWOStatOp::Multiply ? Mod.Value - 1.f : Mod.Value))
				+ TEXT(" ") + UWWOLoadoutSubsystem::StatName(Mod.Stat), T->DisplayName);
			++Buffs;
		}
	}
	if (Buffs == 0) Note(TEXT("None"), TEXT(""));
	Left->AddChildToVerticalBox(List)->SetSize(WWOFillSize(1.f));
	UHorizontalBoxSlot* LeftSlot = Overview->AddChildToHorizontalBox(Left);
	LeftSlot->SetSize(WWOFillSize(0.31f));
	LeftSlot->SetPadding(FMargin(44.f * K, 72.f * K, 30.f * K, 0));

	// --- Middle: the character, between two faint lines.
	UHorizontalBoxSlot* LineA = Overview->AddChildToHorizontalBox(Rule(Tree, FLinearColor(1, 1, 1, 0.1f), true));
	LineA->SetPadding(FMargin(0, 40.f * K, 0, 0));
	UHorizontalBoxSlot* Middle = Overview->AddChildToHorizontalBox(OverviewPreview);
	Middle->SetSize(WWOFillSize(0.38f));
	Middle->SetHorizontalAlignment(HAlign_Fill);
	Middle->SetVerticalAlignment(VAlign_Fill);
	UHorizontalBoxSlot* LineB = Overview->AddChildToHorizontalBox(Rule(Tree, FLinearColor(1, 1, 1, 0.1f), true));
	LineB->SetPadding(FMargin(0, 40.f * K, 0, 0));

	// --- Right: what's equipped, as cards. Each opens the tab that changes it.
	UVerticalBox* Right = Tree->ConstructWidget<UVerticalBox>();
	// The flora, with its grenade and abilities over its traits.
	UHorizontalBox* Top = Tree->ConstructWidget<UHorizontalBox>();
	{
		UVerticalBox* In = Tree->ConstructWidget<UVerticalBox>();
		UTextBlock* Big = Spaced(Styler, Initials(Flora ? Flora->DisplayName : TEXT("?")), 52.f, Accent, 0);
		Big->SetJustification(ETextJustify::Center);
		UVerticalBoxSlot* BigSlot = In->AddChildToVerticalBox(Big);
		BigSlot->SetSize(WWOFillSize(1.f));
		BigSlot->SetVerticalAlignment(VAlign_Center);
		UTextBlock* Label = Spaced(Styler, Flora ? Flora->DisplayName : TEXT("No flora"), 13.f, FLinearColor::White, 200);
		Label->SetJustification(ETextJustify::Center);
		In->AddChildToVerticalBox(Label);
		USizeBox* Size = Tree->ConstructWidget<USizeBox>();
		Size->SetWidthOverride(172.f * K);
		UWidget* FloraCard = Card(In, Open(TEXT("Flora")), CardColor, Accent, 2.f, FMargin(8.f, 10.f));
		HoverTiles.Add({ FloraCard, FString(TEXT("flora")) });
		Size->AddChild(FloraCard);
		Top->AddChildToHorizontalBox(Size);
	}
	UVerticalBox* Small = Tree->ConstructWidget<UVerticalBox>();
	auto Tile = [&](UHorizontalBox* Row, const FString& Name, const FLinearColor& Fill, const FLinearColor& Edge, const FString& PickerName)
	{
		UTextBlock* Text = Spaced(Styler, Name.IsEmpty() ? FString(TEXT("")) : Initials(Name), 20.f, FLinearColor(1, 1, 1, 0.92f), 40);
		Text->SetJustification(ETextJustify::Center);
		UVerticalBox* In = Tree->ConstructWidget<UVerticalBox>();
		UVerticalBoxSlot* TextSlot = In->AddChildToVerticalBox(Text);
		TextSlot->SetSize(WWOFillSize(1.f));
		TextSlot->SetVerticalAlignment(VAlign_Center);
		UWidget* Box = Card(In, Open(TEXT("Flora")), Name.IsEmpty() ? CardColor : Fill, Name.IsEmpty() ? FLinearColor(1, 1, 1, 0.2f) : Edge, Name.IsEmpty() ? 1.5f : 2.f, FMargin(2.f));
		HoverTiles.Add({ Box, PickerName }); // hovering it offers the other choices (UpdateHoverPicker)
		UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Box);
		S->SetSize(WWOFillSize(1.f));
		S->SetPadding(FMargin(Row->GetChildrenCount() > 1 ? 10.f * K : 0.f, 0, 0, 0));
	};
	UHorizontalBox* Kit = Tree->ConstructWidget<UHorizontalBox>();
	// The same colors as on the Flora tab: each item's own, darkened, inside an edge of it; traits blue.
	{
		const UWWOGrenadeData* G = L->GetGrenade();
		Tile(Kit, G ? G->DisplayName : FString(), G ? WWO::Darkened(G->Color, 0.6f) : CardColor, G ? G->Color : CardEdge, TEXT("grenade"));
	}
	for (int32 i = 0; i < 2; ++i)
	{
		const UWWOAbilityData* A = L->GetAbility(i);
		Tile(Kit, A ? A->DisplayName : FString(), A ? WWO::Darkened(A->Color, 0.6f) : CardColor, A ? A->Color : CardEdge, FString::Printf(TEXT("ability%d"), i));
	}
	UHorizontalBox* TraitRow = Tree->ConstructWidget<UHorizontalBox>();
	const TArray<UWWOTraitData*> Traits = L->EquippedTraits();
	for (int32 i = 0; i < FMath::Max(Traits.Num(), 4); ++i)
	{
		Tile(TraitRow, Traits.IsValidIndex(i) ? Traits[i]->DisplayName : FString(), TraitFill, TraitEdge, FString::Printf(TEXT("trait%d"), i));
	}
	Small->AddChildToVerticalBox(Kit)->SetSize(WWOFillSize(1.15f));
	UVerticalBoxSlot* TraitSlot = Small->AddChildToVerticalBox(TraitRow);
	TraitSlot->SetSize(WWOFillSize(1.f));
	TraitSlot->SetPadding(FMargin(0, 10.f * K, 0, 0));
	UHorizontalBoxSlot* SmallSlot = Top->AddChildToHorizontalBox(Small);
	SmallSlot->SetSize(WWOFillSize(1.f));
	SmallSlot->SetPadding(FMargin(14.f * K, 0, 0, 0));
	USizeBox* TopBox = Tree->ConstructWidget<USizeBox>();
	TopBox->SetHeightOverride(156.f * K);
	TopBox->AddChild(Top);
	Right->AddChildToVerticalBox(TopBox);
	// Your two weapons: each bar is a weapon slot holding a saved config. Hovering one offers the
	// saved configs; clicking it opens the Weapon tab.
	for (int32 WeaponSlot = 0; WeaponSlot < UWWOLoadoutSubsystem::WeaponSlotCount; ++WeaponSlot)
	{
		const int32 ConfigIndex = L->GetSlotConfig(WeaponSlot);
		const bool bInHand = WeaponSlot == L->GetActiveSlot();
		FString Name = TEXT("Empty · hover to choose");
		if (ConfigIndex != INDEX_NONE)
		{
			const FWWOModConfig& Config = L->GetConfigs()[ConfigIndex];
			const UWWOWeaponData* Gun = nullptr;
			for (const UWWOWeaponData* W : L->Weapons) if (W->Id == Config.Weapon) Gun = W;
			Name = (Gun ? Gun->DisplayName : Config.Weapon.ToString()) + TEXT("  ·  ") + Config.Name;
		}
		else if (bInHand && L->GetWeapon()) Name = L->GetWeapon()->DisplayName + TEXT("  ·  not saved");
		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();
		UBorder* Mark = Tree->ConstructWidget<UBorder>();
		Mark->SetBrushColor(WWO::Lin(bInHand ? Accent : FLinearColor(1, 1, 1, 0.3f)));
		USizeBox* MarkBox = Tree->ConstructWidget<USizeBox>();
		MarkBox->SetWidthOverride(3.f * K);
		MarkBox->AddChild(Mark);
		Row->AddChildToHorizontalBox(MarkBox)->SetPadding(FMargin(0, 6.f * K));
		UVerticalBox* Words = Tree->ConstructWidget<UVerticalBox>();
		Words->AddChildToVerticalBox(Spaced(Styler, FString::Printf(TEXT("Weapon %d%s"), WeaponSlot + 1, bInHand ? TEXT("  ·  in hand") : TEXT("")), 10.f, Dim, 240));
		Words->AddChildToVerticalBox(Spaced(Styler, Name, 16.f, FLinearColor::White, 180))->SetPadding(FMargin(0, 6.f * K, 0, 0));
		UHorizontalBoxSlot* WordsSlot = Row->AddChildToHorizontalBox(Words);
		WordsSlot->SetPadding(FMargin(18.f * K, 0, 0, 0));
		WordsSlot->SetVerticalAlignment(VAlign_Center);
		UWidget* Bar = Card(Row, Open(TEXT("Weapons")), CardColor, CardEdge, 1.5f, FMargin(16.f, 10.f));
		HoverTiles.Add({ Bar, FString::Printf(TEXT("weapon%d"), WeaponSlot) });
		Right->AddChildToVerticalBox(Bar)->SetPadding(FMargin(0, 26.f * K, 0, 0));
	}
	// The four frame parts.
	UUniformGridPanel* Parts = Tree->ConstructWidget<UUniformGridPanel>();
	Parts->SetSlotPadding(FMargin(7.f * K));
	static const TCHAR* const PartSlots[] = { TEXT("head"), TEXT("arms"), TEXT("core"), TEXT("legs") };
	for (int32 i = 0; i < 4; ++i)
	{
		const UWWOFramePartData* Part = L->GetFramePart(PartSlots[i]);
		UOverlay* In = Tree->ConstructWidget<UOverlay>();
		UOverlaySlot* Tick = In->AddChildToOverlay(Spaced(Styler, TEXT("/"), 16.f, Accent, 0));
		Tick->SetHorizontalAlignment(HAlign_Right);
		Tick->SetVerticalAlignment(VAlign_Top);
		UVerticalBox* Words = Tree->ConstructWidget<UVerticalBox>();
		Words->AddChildToVerticalBox(Spaced(Styler, PartSlots[i], 12.f, Dim, 220));
		Words->AddChildToVerticalBox(Spaced(Styler, Part ? Part->DisplayName : TEXT("Standard"), 14.f, FLinearColor::White, 160, true))->SetPadding(FMargin(0, 6.f * K, 0, 0));
		if (Part) Words->AddChildToVerticalBox(Spaced(Styler, Part->Summary().Replace(TEXT("\n"), TEXT(" ")), 9.5f, Dim, 40, true))->SetPadding(FMargin(0, 6.f * K, 0, 0));
		UOverlaySlot* WordsSlot = In->AddChildToOverlay(Words);
		WordsSlot->SetHorizontalAlignment(HAlign_Fill);
		WordsSlot->SetVerticalAlignment(VAlign_Bottom);
		UWidget* PartCard = Card(In, Open(TEXT("Frame Parts")), CardColor, CardEdge, 1.5f, FMargin(18.f, 12.f));
		HoverTiles.Add({ PartCard, FString(TEXT("part_")) + PartSlots[i] });
		UUniformGridSlot* Cell = Parts->AddChildToUniformGrid(PartCard, i / 2, i % 2);
		Cell->SetHorizontalAlignment(HAlign_Fill);
		Cell->SetVerticalAlignment(VAlign_Fill);
	}
	UVerticalBoxSlot* PartsSlot = Right->AddChildToVerticalBox(Parts);
	PartsSlot->SetSize(WWOFillSize(1.f));
	PartsSlot->SetPadding(FMargin(-7.f * K, 20.f * K, -7.f * K, -7.f * K));
	UHorizontalBoxSlot* RightSlot = Overview->AddChildToHorizontalBox(Right);
	RightSlot->SetSize(WWOFillSize(0.31f));
	RightSlot->SetPadding(FMargin(36.f * K, 40.f * K, 16.f * K, 16.f * K));
}

void UWWOCharacterMenu::UpdateStatPreview(const UWWOFramePartData* Part, bool bEquipped)
{
	// The four stat bars, and what the shell part under the cursor means for them.
	// - A part you could equip: green for what it would add, red for what it would take away.
	// - The part you have on: yellow for what it is adding, faint red for what it is costing you.
	PreviewPart = const_cast<UWWOFramePartData*>(Part);
	bPreviewEquipped = bEquipped;
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return;
	// No points is a quarter of the bar; eight fills it (two a part).
	auto Fill = [](float Points) { return FMath::Clamp(0.25f + Points * 0.75f / 8.f, 0.04f, 1.f); };
	static const FLinearColor Green(0.35f, 0.9f, 0.4f), Red(1.f, 0.25f, 0.2f), Yellow(1.f, 0.82f, 0.2f), FaintRed(1.f, 0.25f, 0.2f, 0.35f);
	const FLinearColor Dim(1, 1, 1, 0.62f);
	for (const FStatBar& Bar : StatBars4)
	{
		if (!Bar.Have || !Bar.Shift || !Bar.Rest) continue;
		float Now = 0.f;
		for (const FName& FrameSlot : UWWOLoadoutSubsystem::FrameSlots())
		{
			if (const UWWOFramePartData* On = L->GetFramePart(FrameSlot)) Now += On->Points(Bar.Group);
		}
		float White = Fill(Now), Piece = 0.f, Delta = 0.f;
		FLinearColor Color(0, 0, 0, 0);
		if (Part && bEquipped)
		{
			// What it's doing for you now: the bar without it, and the difference.
			Delta = Part->Points(Bar.Group);
			const float Without = Fill(Now - Delta);
			if (Delta > 0.f) { White = Without; Piece = Fill(Now) - Without; Color = Yellow; }
			else if (Delta < 0.f) { Piece = Without - Fill(Now); Color = FaintRed; }
		}
		else if (Part)
		{
			// What swapping to it would do.
			const UWWOFramePartData* On = L->GetFramePart(Part->Slot);
			Delta = Part->Points(Bar.Group) - (On ? On->Points(Bar.Group) : 0.f);
			const float After = Fill(Now + Delta);
			if (Delta > 0.f) { Piece = After - Fill(Now); Color = Green; }
			else if (Delta < 0.f) { White = After; Piece = Fill(Now) - After; Color = Red; }
		}
		auto Size = [](UBorder* Box, float Share)
		{
			if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Box->Slot)) S->SetSize(WWOFillSize(FMath::Max(Share, 0.0001f)));
			Box->SetVisibility(Share > 0.0005f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		};
		Size(Bar.Have, White);
		Size(Bar.Shift, Piece);
		Size(Bar.Rest, 1.f - White - Piece);
		Bar.Shift->SetBrushColor(WWO::Lin(Color));
		if (Bar.Value) Bar.Value->SetText(FText::FromString(FString::Printf(TEXT("%+d"), FMath::RoundToInt(Now))));
		if (Bar.Change)
		{
			const bool bShow = Part && FMath::Abs(Delta) >= 0.01f;
			Bar.Change->SetText(FText::FromString(bShow ? FString::Printf(TEXT("%+d"), FMath::RoundToInt(Delta)) + (bEquipped ? TEXT(" FROM THIS") : TEXT("")) : FString()));
			Bar.Change->SetColorAndOpacity(FSlateColor(WWO::Lin(bEquipped ? (Delta > 0.f ? Yellow : Red) : (Delta > 0.f ? Green : Red))));
		}
	}
}

void UWWOCharacterMenu::UpdateNoteOutline(const FString& Source)
{
	OutlinedSource = Source;
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	const FLinearColor Accent = L && L->GetFlora() ? WWO::Lightened(L->GetFlora()->Color, 0.15f) : FLinearColor(0.72f, 0.5f, 1.f);
	for (const TPair<TObjectPtr<UBorder>, FString>& Row : NoteRows)
	{
		const bool bOn = !Source.IsEmpty() && Row.Value == Source;
		if (Row.Key) Row.Key->SetBrush(UWWOSettingsMenu::RoundBrush(bOn ? WWO::WithAlpha(Accent, 0.1f) : FLinearColor(0, 0, 0, 0), 4.f, bOn ? Accent : FLinearColor(0, 0, 0, 0), bOn ? 1.5f : 0.f));
	}
}

void UWWOCharacterMenu::CloseHoverPicker()
{
	HoverPicker.Reset();
	HoverIdle = 0.f;
	HoverParts.Reset();
	HoverNames.Reset();
	if (HoverPopup) HoverPopup->RemoveFromParent();
	HoverPopup = nullptr;
}

void UWWOCharacterMenu::UpdateHoverPicker(float DeltaTime)
{
	// On the Loadout tab, resting the cursor on the flora, the grenade, an ability, a trait or a
	// weapon slot drops a row of the other choices under it (icons only: the tabs explain them).
	// It stays while the cursor is on the tile or the row, and goes a moment after it leaves both.
	if (!bOpen || !Expanded.IsEmpty() || !FSlateApplication::IsInitialized())
	{
		if (HoverPopup) CloseHoverPicker();
		if (PreviewPart.IsValid()) UpdateStatPreview(nullptr, false);
		if (!OutlinedSource.IsEmpty()) UpdateNoteOutline(FString());
		return;
	}
	const FVector2D Cursor = FSlateApplication::Get().GetCursorPos();
	FString Over;
	UWidget* OverTile = nullptr;
	for (const TPair<TWeakObjectPtr<UWidget>, FString>& Tile : HoverTiles)
	{
		if (Tile.Key.IsValid() && Tile.Key->GetCachedGeometry().IsUnderLocation(Cursor)) { Over = Tile.Value; OverTile = Tile.Key.Get(); }
	}
	const bool bOnPopup = HoverPopup && HoverPopup->GetCachedGeometry().IsUnderLocation(Cursor);
	// The shell part under the cursor, for the stat bars: one of the choices in the open row, else
	// the part on the card being hovered.
	const UWWOFramePartData* Part = nullptr;
	for (const TPair<TWeakObjectPtr<UWidget>, TWeakObjectPtr<UWWOFramePartData>>& Choice : HoverParts)
	{
		if (Choice.Key.IsValid() && Choice.Key->GetCachedGeometry().IsUnderLocation(Cursor)) Part = Choice.Value.Get();
	}
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	if (!Part && !bOnPopup && Over.StartsWith(TEXT("part_")) && Loadout) Part = Loadout->GetFramePart(FName(*Over.Mid(5)));
	const bool bEquipped = Part && Loadout && Loadout->GetFramePart(Part->Slot) == Part;
	if (Part != PreviewPart.Get() || bEquipped != bPreviewEquipped) UpdateStatPreview(Part, bEquipped);
	// The item under the cursor, for the Build notes: a choice in the open row, else what's on the
	// tile being hovered. Its notes (the rows it is the source of) get an outline.
	FString Source;
	for (const TPair<TWeakObjectPtr<UWidget>, FString>& Choice : HoverNames)
	{
		if (Choice.Key.IsValid() && Choice.Key->GetCachedGeometry().IsUnderLocation(Cursor)) Source = Choice.Value;
	}
	if (Source.IsEmpty() && !bOnPopup && Loadout)
	{
		const TArray<UWWOTraitData*> Traits = Loadout->EquippedTraits();
		if (Over == TEXT("flora") && Loadout->GetFlora()) Source = Loadout->GetFlora()->DisplayName;
		else if (Over == TEXT("grenade") && Loadout->GetGrenade()) Source = Loadout->GetGrenade()->DisplayName;
		else if (Over.StartsWith(TEXT("ability")) && Loadout->GetAbility(FCString::Atoi(*Over.Mid(7)))) Source = Loadout->GetAbility(FCString::Atoi(*Over.Mid(7)))->DisplayName;
		else if (Over.StartsWith(TEXT("trait")) && Traits.IsValidIndex(FCString::Atoi(*Over.Mid(5)))) Source = Traits[FCString::Atoi(*Over.Mid(5))]->DisplayName;
	}
	if (Source != OutlinedSource) UpdateNoteOutline(Source);
	if (!Over.IsEmpty() && !bOnPopup && Over != HoverPicker) OpenHoverPicker(Over, OverTile);
	else if (!Over.IsEmpty() || bOnPopup) HoverIdle = 0.f;
	else if (HoverPopup && (HoverIdle += DeltaTime) > 0.25f) CloseHoverPicker();
}

void UWWOCharacterMenu::OpenHoverPicker(const FString& Name, UWidget* Tile)
{
	CloseHoverPicker();
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	UOverlay* Root = Cast<UOverlay>(WidgetTree->RootWidget);
	if (!L || !Root || !Tile) return;
	UWidgetTree* Tree = Styler->WidgetTree;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
	const FLinearColor Accent = L->GetFlora() ? WWO::Lightened(L->GetFlora()->Color, 0.15f) : FLinearColor(0.72f, 0.5f, 1.f);
	UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();
	// One choice: a square of initials (wider, with a second line, for a weapon config).
	auto Option = [&](const FString& Label, const FString& Under, const FString& Tip, bool bOn, const FLinearColor& Fill, const FLinearColor& Edge, TFunction<void()> Pick) -> UWidget*
	{
		UVerticalBox* In = Tree->ConstructWidget<UVerticalBox>();
		UTextBlock* Text = Spaced(Styler, Label, Under.IsEmpty() ? 18.f : 13.f, bOn ? FLinearColor::White : FLinearColor(1, 1, 1, 0.85f), 40);
		Text->SetJustification(ETextJustify::Center);
		UVerticalBoxSlot* TextSlot = In->AddChildToVerticalBox(Text);
		TextSlot->SetSize(WWOFillSize(1.f));
		TextSlot->SetVerticalAlignment(VAlign_Center);
		if (!Under.IsEmpty())
		{
			UTextBlock* Second = Spaced(Styler, Under, 9.5f, FLinearColor(1, 1, 1, 0.6f), 60);
			Second->SetJustification(ETextJustify::Center);
			In->AddChildToVerticalBox(Second);
		}
		UWidget* Box = Card(In, [WeakThis, Pick] { Pick(); if (WeakThis.IsValid()) WeakThis->BuildOverview(); }, Fill, Edge, bOn ? 4.f : 2.f, FMargin(4.f));
		Box->SetToolTipText(FText::FromString(Tip));
		HoverNames.Add({ Box, Tip }); // its notes in the Build section light up while it's hovered
		USizeBox* Size = Tree->ConstructWidget<USizeBox>();
		Size->SetWidthOverride((Under.IsEmpty() ? 56.f : 132.f) * K);
		Size->SetHeightOverride(56.f * K);
		Size->AddChild(Box);
		Row->AddChildToHorizontalBox(Size)->SetPadding(FMargin(Row->GetChildrenCount() > 1 ? 8.f * K : 0.f, 0, 0, 0));
		return Box;
	};
	// An item's own color darkened inside an edge of it, lighter and thicker for the equipped one.
	auto In = [](const FLinearColor& C) { return WWO::Darkened(C, 0.5f); };
	auto Rim = [](const FLinearColor& C, bool bOn) { return bOn ? WWO::Lightened(C, 0.3f) : C; };
	if (Name == TEXT("flora"))
	{
		for (UWWOFloraData* F : L->Floras)
		{
			if (F->bUnlocked) Option(Initials(F->DisplayName), FString(), F->DisplayName, F == L->GetFlora(), In(F->Color), Rim(F->Color, F == L->GetFlora()), [WL, F] { if (WL.IsValid()) WL->EquipFlora(F); });
		}
	}
	else if (Name == TEXT("grenade"))
	{
		for (UWWOGrenadeData* G : L->GrenadesForFlora())
		{
			if (G->bUnlocked) Option(Initials(G->DisplayName), FString(), G->DisplayName, G == L->GetGrenade(), In(G->Color), Rim(G->Color, G == L->GetGrenade()), [WL, G] { if (WL.IsValid()) WL->EquipGrenade(G); });
		}
	}
	else if (Name.StartsWith(TEXT("ability")))
	{
		const int32 AbilitySlot = FCString::Atoi(*Name.Mid(7));
		for (UWWOAbilityData* A : L->AbilitiesForFlora())
		{
			// One already in the other slot swaps places with this one.
			if (A->bUnlocked) Option(Initials(A->DisplayName), FString(), A->DisplayName, L->AbilitySlotOf(A) == AbilitySlot, In(A->Color), Rim(A->Color, L->AbilitySlotOf(A) == AbilitySlot),
				[WL, A, AbilitySlot] { if (WL.IsValid()) WL->EquipAbility(AbilitySlot, A); });
		}
	}
	else if (Name.StartsWith(TEXT("trait")))
	{
		const int32 TraitSlot = FCString::Atoi(*Name.Mid(5));
		const TArray<UWWOTraitData*> Equipped = L->EquippedTraits();
		UWWOTraitData* InSlot = Equipped.IsValidIndex(TraitSlot) ? Equipped[TraitSlot] : nullptr;
		for (UWWOTraitData* T : L->TraitsForFlora())
		{
			if (!T->bUnlocked) continue;
			const bool bOn = L->IsTraitEquipped(T);
			// An equipped one comes off; any other takes this slot's place.
			Option(Initials(T->DisplayName), FString(), T->DisplayName, bOn, bOn ? TraitFill : TraitOffFill, bOn ? TraitEdge : TraitOffEdge, [WL, T, InSlot, bOn]
			{
				if (!WL.IsValid()) return;
				if (!bOn && InSlot) WL->ToggleTrait(InSlot);
				WL->ToggleTrait(T);
			});
		}
	}
	else if (Name.StartsWith(TEXT("weapon")))
	{
		const int32 WeaponSlot = FCString::Atoi(*Name.Mid(6));
		const TArray<FWWOModConfig>& Configs = L->GetConfigs();
		for (int32 i = 0; i < Configs.Num(); ++i)
		{
			const UWWOWeaponData* Gun = nullptr;
			for (const UWWOWeaponData* W : L->Weapons) if (W->Id == Configs[i].Weapon) Gun = W;
			const FString GunName = Gun ? Gun->DisplayName : Configs[i].Weapon.ToString();
			Option(GunName, Configs[i].Name, GunName + TEXT(" · ") + Configs[i].Name + TEXT("\n") + UWWOLoadoutSubsystem::ConfigSummary(Configs[i], L),
				L->GetSlotConfig(WeaponSlot) == i, CardColor, L->GetSlotConfig(WeaponSlot) == i ? FLinearColor::White : CardEdge, [WL, WeaponSlot, i] { if (WL.IsValid()) WL->SetSlotConfig(WeaponSlot, i); });
		}
		if (Configs.Num() == 0)
		{
			UHorizontalBoxSlot* None = Row->AddChildToHorizontalBox(Spaced(Styler, TEXT("No saved configs yet · save one on the Weapon tab"), 12.f, FLinearColor(1, 1, 1, 0.7f), 100));
			None->SetVerticalAlignment(VAlign_Center);
			None->SetPadding(FMargin(8.f * K, 14.f * K));
		}
	}
	else if (Name.StartsWith(TEXT("part_")))
	{
		// The shell's parts for this slot. Resting on one shows what it would do to the stat bars.
		const FName PartSlot(*Name.Mid(5));
		for (UWWOFramePartData* Part : L->FramePartsFor(PartSlot))
		{
			if (!Part->bUnlocked) continue;
			const bool bOn = Part == L->GetFramePart(PartSlot);
			UWidget* Box = Option(Part->DisplayName, Part->Summary().Replace(TEXT("\n"), TEXT("  ")), Part->DisplayName, bOn, CardColor, bOn ? FLinearColor::White : CardEdge,
				[WL, Part] { if (WL.IsValid()) WL->EquipFramePart(Part); });
			HoverParts.Add({ Box, Part });
		}
	}
	if (Row->GetChildrenCount() == 0) return;
	UBorder* Panel = Tree->ConstructWidget<UBorder>();
	Panel->SetBrush(UWWOSettingsMenu::RoundBrush(FLinearColor(0.07f, 0.075f, 0.09f, 0.98f), 6.f, WWO::WithAlpha(Accent, 0.9f), 1.5f));
	Panel->SetPadding(FMargin(10.f * K));
	Panel->SetContent(Row);
	// Just under the tile, its left edge on the tile's (pulled in from the window's right edge when
	// the row would run past it; the last layout pass's width is not known yet, so by a count).
	const FGeometry& RootGeo = Root->GetCachedGeometry();
	const FGeometry& TileGeo = Tile->GetCachedGeometry();
	const FVector2D At = RootGeo.AbsoluteToLocal(TileGeo.GetAbsolutePositionAtCoordinates(FVector2D(0.f, 1.f)));
	float Width = 20.f * K;
	const bool bWide = Name.StartsWith(TEXT("weapon")) || Name.StartsWith(TEXT("part_"));
	for (int32 i = 0; i < Row->GetChildrenCount(); ++i) Width += (bWide ? 140.f : 64.f) * K;
	if (Name.StartsWith(TEXT("weapon")) && L->GetConfigs().Num() == 0) Width = 520.f * K;
	const float Left = FMath::Clamp((float)At.X, 8.f, FMath::Max((float)RootGeo.GetLocalSize().X - Width - 8.f, 8.f));
	UOverlaySlot* PopupSlot = Root->AddChildToOverlay(Panel);
	PopupSlot->SetHorizontalAlignment(HAlign_Left);
	// Under the tile, or over it when the tile is low in the window (the bottom row of the shell).
	const float RootHeight = (float)RootGeo.GetLocalSize().Y;
	if (At.Y > RootHeight * 0.8f)
	{
		const FVector2D Top = RootGeo.AbsoluteToLocal(TileGeo.GetAbsolutePositionAtCoordinates(FVector2D(0.f, 0.f)));
		PopupSlot->SetVerticalAlignment(VAlign_Bottom);
		PopupSlot->SetPadding(FMargin(Left, 0, 0, RootHeight - (float)Top.Y + 4.f));
	}
	else
	{
		PopupSlot->SetVerticalAlignment(VAlign_Top);
		PopupSlot->SetPadding(FMargin(Left, At.Y + 4.f, 0, 0));
	}
	HoverPopup = Panel;
	HoverPicker = Name;
	HoverIdle = 0.f;
}

void UWWOCharacterMenu::UpdateTurn(float DeltaTime)
{
	// The character stands still until you turn it: the right stick (the left one moves the menu
	// cursor), A and D, or holding the mouse (or Cross) on it and dragging.
	if (!bOpen || !PreviewActor) return;
	constexpr float StickRate = 200.f, KeyRate = 160.f, DragRate = 0.45f; // degrees a second, and per pixel
	float Turn = 0.f;
	const float Stick = FWWOMenuCursor::RightStickX;
	if (FMath::Abs(Stick) > FWWOMenuCursor::Deadzone) Turn += Stick * StickRate * DeltaTime;
	if (const APlayerController* PC = GetOwningPlayer())
	{
		if (PC->IsInputKeyDown(EKeys::D)) Turn += KeyRate * DeltaTime;
		if (PC->IsInputKeyDown(EKeys::A)) Turn -= KeyRate * DeltaTime;
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication& App = FSlateApplication::Get();
		const bool bDown = App.GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
		const FVector2D Cursor = App.GetCursorPos();
		const UWidget* Shown = Expanded.IsEmpty() ? OverviewPreview.Get() : Preview.Get();
		if (bDown && !bMouseWasDown) bDragging = Shown && Shown->IsVisible() && Shown->GetCachedGeometry().IsUnderLocation(Cursor);
		if (!bDown) bDragging = false;
		if (bDragging) Turn += (Cursor.X - LastCursor.X) * DragRate;
		bMouseWasDown = bDown;
		LastCursor = Cursor;
	}
	// Seen from the camera, turning right is a negative yaw here.
	if (Turn != 0.f) PreviewActor->Turntable->AddRelativeRotation(FRotator(0.f, -Turn, 0.f));
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
	// The whole Weapon tab on one screen, no scrolling: four columns across the full width.
	// Weapons | the frame's stat bars, perks under them | its six mod slots | saved configs, or
	// the hovered slot's mods in their place (BuildModRows fills the last three).
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L || !L->GetWeapon()) return;
	UWidgetTree* Tree = Styler->WidgetTree;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
	UHorizontalBox* Columns = Tree->ConstructWidget<UHorizontalBox>();
	ModRowsStart = Columns;

	UVerticalBox* Guns = Tree->ConstructWidget<UVerticalBox>();
	Guns->AddChildToVerticalBox(Spaced(Styler, TEXT("Weapon"), 13.f, FLinearColor(1, 1, 1, 0.6f), 220))->SetPadding(FMargin(0, 0, 0, 12.f * K));
	const FLinearColor OnEdge(0.9f, 0.9f, 0.95f);
	for (UWWOWeaponData* Option : L->Weapons)
	{
		if (!Option->bUnlocked) continue;
		const bool bOn = Option == L->GetWeapon();
		const FString Fire = Option->BurstCount() > 1 ? FString::Printf(TEXT("%d-round burst"), Option->BurstCount()) : FString(Option->IsSemiAuto() ? TEXT("semi") : TEXT("auto"));
		const FString Dmg = Option->Pellets > 1.f ? FString::Printf(TEXT("%d×%d dmg"), FMath::RoundToInt(Option->Pellets), FMath::RoundToInt(Option->Damage))
			: FString::Printf(TEXT("%d dmg"), FMath::RoundToInt(Option->Damage));
		const FString Stats = FString::Printf(TEXT("%d rpm %s · %s · %d rds"), FMath::RoundToInt(Option->RoundsPerMinute()), *Fire, *Dmg, FMath::RoundToInt(Option->MagazineSize));
		UWidget* B = Slot(Option->DisplayName + (bOn ? TEXT("  ✓") : TEXT("")) + TEXT("\n") + Stats, FVector2D(WeaponColumn, 62), bOn ? FLinearColor(0.24f, 0.25f, 0.3f) : FLinearColor(0.2f, 0.21f, 0.26f), 15.f,
			[WeakThis, WL, Option] { if (WL.IsValid()) WL->EquipWeapon(Option); if (WeakThis.IsValid()) WeakThis->Refresh(); }, bOn ? &OnEdge : nullptr, 3.f);
		Guns->AddChildToVerticalBox(B)->SetPadding(FMargin(0, 0, 0, 8.f * K));
	}
	USizeBox* GunsBox = Tree->ConstructWidget<USizeBox>();
	GunsBox->SetWidthOverride(WeaponColumn * K);
	GunsBox->AddChild(Guns);
	Columns->AddChildToHorizontalBox(GunsBox);
	BuildModRows(Columns);
	AddContent(Columns);
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

void UWWOCharacterMenu::BuildModRows(UHorizontalBox* Columns)
{
	// Draft weapon mods: the stat bars with the equipped perks under them, every slot at once, and
	// the saved configs. While the cursor is over a slot, that slot's mods take the configs' place
	// (see UpdateModHover).
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	SlotButtons.Reset();
	PopupButtons.Reset();
	PopupPanel = nullptr;
	if (!L || !L->HasMods(L->GetWeapon())) return;
	UWidgetTree* Tree = Styler->WidgetTree;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
	const FLinearColor OnEdge(0.9f, 0.9f, 0.95f);
	const FLinearColor LabelColor(1, 1, 1, 0.6f);
	auto Column = [&](float Width, float Gap) -> UVerticalBox*
	{
		UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>();
		USizeBox* Size = Tree->ConstructWidget<USizeBox>();
		Size->SetWidthOverride(Width * K);
		Size->AddChild(Box);
		Columns->AddChildToHorizontalBox(Size)->SetPadding(FMargin(Gap * K, 0, 0, 0));
		return Box;
	};
	auto Label = [&](UVerticalBox* To, const FString& Text)
	{
		const float Above = To->GetChildrenCount() > 0 ? 18.f * K : 0.f; // read before the label is added
		To->AddChildToVerticalBox(Spaced(Styler, Text, 13.f, LabelColor, 220))->SetPadding(FMargin(0, Above, 0, 12.f * K));
	};

	// Stat bars, and what the equipped mods' perks do.
	UVerticalBox* Stats = Column(StatsColumn, 24.f);
	Label(Stats, L->GetWeapon()->DisplayName + TEXT(" frame"));
	StatBars = Tree->ConstructWidget<UWWOStatBars>(UWWOStatBars::StaticClass());
	USizeBox* BarsBox = Tree->ConstructWidget<USizeBox>();
	BarsBox->SetWidthOverride(StatsColumn * K);
	BarsBox->SetHeightOverride(UWWOStatBars::RowHeight * BarDefs().Num() * K);
	BarsBox->AddChild(StatBars);
	Stats->AddChildToVerticalBox(BarsBox);
	UpdateStatBars(nullptr, NAME_None, false);
	Label(Stats, TEXT("Perks"));
	TArray<FString> Perks;
	for (const FName& ModSlot : UWWOLoadoutSubsystem::ModSlots())
	{
		const UWWOModData* Mod = L->GetMod(ModSlot);
		if (Mod && !Mod->PerkText.IsEmpty()) Perks.Add(Mod->PerkText);
	}
	UTextBlock* PerkText = Styler->MakeText(Perks.Num() > 0 ? FString::Join(Perks, TEXT("\n")) : TEXT("No perks equipped"), 14.f, FLinearColor(0.6f, 0.85f, 1.f), true);
	PerkText->SetWrapTextAt(StatsColumn * K);
	Stats->AddChildToVerticalBox(PerkText);

	// Every slot, one column.
	UVerticalBox* Slots = Column(SlotColumn, 18.f);
	Label(Slots, TEXT("Mods"));
	for (const FName& ModSlot : UWWOLoadoutSubsystem::ModSlots())
	{
		const UWWOModData* Mod = L->GetMod(ModSlot);
		const FLinearColor Edge = Mod ? RarityColor(Mod->Rarity) : FLinearColor(0.4f, 0.42f, 0.5f);
		const bool bOpenSlot = OpenSlot == ModSlot;
		UWidget* W = Slot(UWWOLoadoutSubsystem::ModSlotTitle(ModSlot) + TEXT("\n") + (Mod ? Mod->DisplayName : FString(TEXT("Empty"))),
			FVector2D(SlotColumn, ModSlotHeight - 8.f), Mod ? WWO::Darkened(Edge, 0.65f) : FLinearColor(0.2f, 0.21f, 0.25f), 16.f,
			[WeakThis, ModSlot] { if (WeakThis.IsValid()) WeakThis->OpenModSlot(ModSlot); }, bOpenSlot ? &OnEdge : &Edge, bOpenSlot ? 5.f : 2.f);
		if (USizeBox* Box = Cast<USizeBox>(W)) SlotButtons.Add({ Cast<UButton>(Box->GetChildAt(0)), ModSlot });
		Slots->AddChildToVerticalBox(W)->SetPadding(FMargin(0, 0, 0, 8.f * K));
	}

	UVerticalBox* Side = Column(SideColumn, 22.f);
	if (!OpenSlot.IsNone())
	{
		// The open slot's mods, lined up with the slot (a row up, and never past the column's foot).
		const int32 SlotIndex = UWWOLoadoutSubsystem::ModSlots().IndexOfByKey(OpenSlot);
		Label(Side, UWWOLoadoutSubsystem::ModSlotTitle(OpenSlot) + TEXT(" mods"));
		UBorder* Panel = Tree->ConstructWidget<UBorder>();
		Panel->SetBrush(UWWOSettingsMenu::RoundBrush(FLinearColor(0.12f, 0.13f, 0.16f), 8.f, FLinearColor(0.55f, 0.58f, 0.66f), 2.f));
		Panel->SetPadding(FMargin(12.f * K, 12.f * K, 12.f * K, 4.f * K));
		UVerticalBox* List = Tree->ConstructWidget<UVerticalBox>();
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
			UWidget* W = Slot(Title + TEXT("\n") + Body, FVector2D(SideColumn - 28.f, 84), Mod ? WWO::Darkened(Base, 0.7f) : FLinearColor(0.2f, 0.21f, 0.25f), 15.f,
				[WeakThis, WL, ThisSlot, Mod] { if (WL.IsValid()) WL->EquipMod(ThisSlot, Mod); if (WeakThis.IsValid()) WeakThis->Refresh(); },
				bOn ? &OnEdge : &Base, bOn ? 5.f : 2.f, FString(), true);
			if (USizeBox* Box = Cast<USizeBox>(W)) PopupButtons.Add({ Cast<UButton>(Box->GetChildAt(0)), Mod });
			List->AddChildToVerticalBox(W)->SetPadding(FMargin(0, 0, 0, 8.f * K));
		}
		Panel->SetContent(List);
		PopupPanel = Panel;
		Side->AddChildToVerticalBox(Panel)->SetPadding(FMargin(0, FMath::Clamp(SlotIndex - 1.f, 0.f, 1.6f) * ModSlotHeight * K, 0, 0));
		return;
	}

	// Saved configs: save the current mods as a new one; click one to apply it, or save over or
	// delete it. The list alone scrolls if there are more than fit (about eight).
	Label(Side, TEXT("Configs"));
	Side->AddChildToVerticalBox(Slot(TEXT("Save current as new config"), FVector2D(SideColumn, 44), FLinearColor(0.2f, 0.3f, 0.24f), 16.f,
		[WeakThis, WL] { if (WL.IsValid()) WL->SaveConfig(INDEX_NONE); if (WeakThis.IsValid()) WeakThis->Refresh(); }));
	UScrollBox* ConfigList = Tree->ConstructWidget<UScrollBox>();
	const TArray<FWWOModConfig>& Configs = L->GetConfigs();
	constexpr float SaveOverWidth = 92.f, DeleteWidth = 72.f, ButtonGap = 6.f;
	for (int32 i = 0; i < Configs.Num(); ++i)
	{
		UHorizontalBox* CRow = Tree->ConstructWidget<UHorizontalBox>();
		const bool bActive = L->GetActiveConfig() == i;
		const UWWOWeaponData* ConfigGun = nullptr;
		for (const UWWOWeaponData* W : L->Weapons) if (W && W->Id == Configs[i].Weapon) ConfigGun = W;
		const FString Name = Configs[i].Name + TEXT(" · ") + (ConfigGun ? ConfigGun->DisplayName : Configs[i].Weapon.ToString()) + (bActive ? TEXT("  ✓") : TEXT(""));
		CRow->AddChildToHorizontalBox(Slot(Name + TEXT("\n") + UWWOLoadoutSubsystem::ConfigSummary(Configs[i], L),
			FVector2D(SideColumn - SaveOverWidth - DeleteWidth - ButtonGap * 2.f - 14.f, 58), FLinearColor(0.2f, 0.21f, 0.26f), 13.f,
			[WeakThis, WL, i] { if (WL.IsValid()) WL->ApplyConfig(i); if (WeakThis.IsValid()) WeakThis->Refresh(); },
			bActive ? &OnEdge : nullptr, 2.f, TEXT("Apply"), true));
		CRow->AddChildToHorizontalBox(Slot(TEXT("Save over"), FVector2D(SaveOverWidth, 58), FLinearColor(0.3f, 0.27f, 0.18f), 13.f,
			[WeakThis, WL, i] { if (WL.IsValid()) WL->SaveConfig(i); if (WeakThis.IsValid()) WeakThis->Refresh(); }))->SetPadding(FMargin(ButtonGap * K, 0, 0, 0));
		CRow->AddChildToHorizontalBox(Slot(TEXT("Delete"), FVector2D(DeleteWidth, 58), FLinearColor(0.32f, 0.17f, 0.17f), 13.f,
			[WeakThis, WL, i] { if (WL.IsValid()) WL->DeleteConfig(i); if (WeakThis.IsValid()) WeakThis->Refresh(); }))->SetPadding(FMargin(ButtonGap * K, 0, 0, 0));
		if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(ConfigList->AddChild(CRow))) RowSlot->SetPadding(FMargin(0, 0, 0, 8.f * K));
	}
	USizeBox* ListBox = Tree->ConstructWidget<USizeBox>();
	ListBox->SetMaxDesiredHeight(ConfigListHeight * K);
	ListBox->AddChild(ConfigList);
	Side->AddChildToVerticalBox(ListBox)->SetPadding(FMargin(0, 10.f * K, 0, 0));
	Side->AddChildToVerticalBox(Spaced(Styler, FString::Printf(TEXT("In game: %s swaps to your other weapon slot's config"),
		Cast<AWWOPlayerController>(GetOwningPlayer()) ? *Cast<AWWOPlayerController>(GetOwningPlayer())->BindHint("next_config") : TEXT("the swap key")), 10.f, FLinearColor(1, 1, 1, 0.4f), 80, true))->SetPadding(FMargin(0, 6.f * K, 0, 0));
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
		if (Stat == "fire_rate") return W->RoundsPerMinute();
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
	UpdateTurn(InDeltaTime);
	UpdateHoverPicker(InDeltaTime);
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
	// The whole Shell tab on one screen, no scrolling: a row per slot, its name at the left and its
	// parts sharing the rest of the width, then the net effect.
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return;
	UWidgetTree* Tree = Styler->WidgetTree;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
	const FLinearColor OnEdge(0.9f, 0.9f, 0.95f);
	for (const FName& FrameSlot : UWWOLoadoutSubsystem::FrameSlots())
	{
		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();
		USizeBox* Name = Tree->ConstructWidget<USizeBox>();
		Name->SetWidthOverride(74.f * K);
		Name->AddChild(Spaced(Styler, FrameSlot.ToString(), 13.f, FLinearColor(1, 1, 1, 0.6f), 220));
		Row->AddChildToHorizontalBox(Name)->SetVerticalAlignment(VAlign_Center);
		for (UWWOFramePartData* Part : L->FramePartsFor(FrameSlot))
		{
			if (!Part->bUnlocked) continue;
			const bool bOn = Part == L->GetFramePart(FrameSlot);
			UHorizontalBoxSlot* PartSlot = Row->AddChildToHorizontalBox(Slot(Part->DisplayName + (bOn ? TEXT("  ✓") : TEXT("")) + TEXT("\n") + Part->Summary(), FVector2D(140, 104), CardColor, 15.f,
				[WeakThis, WL, Part] { if (WL.IsValid()) WL->EquipFramePart(Part); if (WeakThis.IsValid()) WeakThis->Refresh(); }, bOn ? &OnEdge : nullptr, 4.f));
			PartSlot->SetSize(WWOFillSize(1.f));
			PartSlot->SetPadding(FMargin(10.f * K, 0, 0, 0));
		}
		AddContent(Row);
	}
	AddRowLabel(TEXT("Net effect"));
	const TArray<FString> Lines = L->GroupSummary();
	AddContent(Styler->MakeText(Lines.Num() > 0 ? FString::Join(Lines, TEXT("\n")) : TEXT("No changes (all Standard parts)"), 16.f));
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

float UWWOCharacterMenu::ScrollOverflow() const
{
	return ExpandedScroll ? ExpandedScroll->GetScrollOffsetOfEnd() : 0.f;
}

void UWWOCharacterMenu::AutoTestScroll(float Offset)
{
	// A negative offset scrolls the mod slots to the top.
	if (Offset < 0.f && ModRowsStart) ExpandedScroll->ScrollWidgetIntoView(ModRowsStart, false, EDescendantScrollDestination::TopOrLeft);
	else ExpandedScroll->SetScrollOffset(FMath::Min(Offset, ExpandedScroll->GetScrollOffsetOfEnd()));
}
