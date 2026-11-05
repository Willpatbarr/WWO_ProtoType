#include "UI/WWOCharacterMenu.h"
#include "UI/WWOSettingsMenu.h"
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
#include "Components/SkeletalMeshComponent.h"
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
	Capture->SetRelativeLocationAndRotation(FVector(440.f, 0.f, 100.f), FRotator(0.f, 180.f, 0.f)); // in front, at chest height
	Capture->FOVAngle = 45.f;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->bCaptureEveryFrame = true;
	Capture->PrimaryComponentTick.bTickEvenWhenPaused = true;
	Body = CreateDefaultSubobject<UWWOBodyComponent>(TEXT("Body"));
	Body->BodyColor = FLinearColor(0.3f, 0.42f, 0.55f);
}

void AWWOPreviewActor::BeginPlay()
{
	Body->MeshOverride = Mesh;
	Super::BeginPlay();
	Target = NewObject<UTextureRenderTarget2D>(this);
	Target->RenderTargetFormat = RTF_RGBA8;
	Target->ClearColor = FLinearColor(0.07f, 0.08f, 0.1f, 1.f);
	Target->InitAutoFormat(600, 820);
	Capture->TextureTarget = Target;
	Capture->ShowOnlyActors.Add(this);
	Body->FollowLoadout(false); // flora grafts follow the loadout
}

void AWWOPreviewActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Turntable->AddRelativeRotation(FRotator(0.f, 30.f * FApp::GetDeltaTime(), 0.f)); // real time (the game is paused)
}

// --- Menu ------------------------------------------------------------------------------------

void UWWOCharacterMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	Build();
}

void UWWOCharacterMenu::AddRowLabel(const FString& Text)
{
	UTextBlock* Label = Styler->MakeText(Text, 22.f, FLinearColor(1, 1, 1, 0.7f), true);
	Content->AddChildToVerticalBox(Label)->SetPadding(FMargin(0, 8.f * K, 0, 2.f * K));
}

UButton* UWWOCharacterMenu::Slot(const FString& Text, FVector2D Size, const FLinearColor& Color, const FLinearColor& Border, float BorderWidth, float FontPx, TFunction<void()> Fn)
{
	UButton* B = Styler->MakeButton(this, Text, MoveTemp(Fn), FontPx, Color, true);
	if (BorderWidth > 0.f)
	{
		FButtonStyle Style = B->GetStyle();
		Style.SetNormal(UWWOSettingsMenu::RoundBrush(Color, 8.f, Border, BorderWidth));
		Style.SetHovered(UWWOSettingsMenu::RoundBrush(Color + FLinearColor(0.08f, 0.08f, 0.08f, 0.f), 8.f, Border, BorderWidth));
		Style.SetPressed(UWWOSettingsMenu::RoundBrush(Color + FLinearColor(0.15f, 0.15f, 0.15f, 0.f), 8.f, Border, BorderWidth));
		B->SetStyle(Style);
	}
	B->SetToolTipText(FText());
	return B;
}

void UWWOCharacterMenu::Build()
{
	// A spare settings menu, never shown: only its MakeText / MakeButton helpers are used.
	Styler = CreateWidget<UWWOSettingsMenu>(GetOwningPlayer(), UWWOSettingsMenu::StaticClass());
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Root;
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
		Header->AddChildToHorizontalBox(Styler->MakeText(TEXT("Character"), 40.f, FLinearColor::White, true))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Header->AddChildToHorizontalBox(Styler->MakeText(TEXT("Touchpad / I to close  ·  ◯ / Esc to go back"), 20.f, FLinearColor(1, 1, 1, 0.6f)))->SetVerticalAlignment(VAlign_Center);
		Page->AddChildToVerticalBox(Header)->SetPadding(FMargin(0, 0, 0, 16.f * K));

		UHorizontalBox* Body = Tree->ConstructWidget<UHorizontalBox>();
		Page->AddChildToVerticalBox(Body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		// Left: the three tiles, or one expanded tile.
		TileSwitcher = Tree->ConstructWidget<UWidgetSwitcher>();
		UVerticalBox* Tiles = Tree->ConstructWidget<UVerticalBox>();
		TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
		for (const FString Tile : { FString(TEXT("Flora")), FString(TEXT("Weapons")), FString(TEXT("Frame Parts")) })
		{
			UButton* B = Styler->MakeButton(this, Tile, [WeakThis, Tile] { if (WeakThis.IsValid()) WeakThis->Expand(Tile); }, 40.f, FLinearColor(0.18f, 0.19f, 0.23f), true);
			UVerticalBoxSlot* TileSlot = Tiles->AddChildToVerticalBox(B);
			TileSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			TileSlot->SetPadding(FMargin(0, 0, 0, 16.f * K));
		}
		TileSwitcher->AddChild(Tiles);
		UBorder* ExpandedPanel = Tree->ConstructWidget<UBorder>();
		ExpandedPanel->SetBrush(UWWOSettingsMenu::RoundBrush(FLinearColor(0.18f, 0.19f, 0.23f), 8.f));
		ExpandedPanel->SetPadding(FMargin(28.f * K));
		UVerticalBox* ExpandedPage = Tree->ConstructWidget<UVerticalBox>();
		UHorizontalBox* Bar = Tree->ConstructWidget<UHorizontalBox>();
		ExpandedTitle = Styler->MakeText(TEXT(""), 40.f, FLinearColor::White, true);
		Bar->AddChildToHorizontalBox(ExpandedTitle)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Bar->AddChildToHorizontalBox(Styler->MakeButton(this, TEXT("Collapse ✕"), [WeakThis] { if (WeakThis.IsValid()) WeakThis->Collapse(); }, 24.f));
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

		// Right: the character preview.
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		PreviewActor = GetWorld()->SpawnActor<AWWOPreviewActor>(PreviewSpot, FRotator::ZeroRotator, Params);
		Preview = Tree->ConstructWidget<UImage>();
		if (PreviewActor && PreviewActor->Target)
		{
			FSlateBrush Brush;
			Brush.SetResourceObject(PreviewActor->Target);
			Brush.ImageSize = FVector2D(600.f, 820.f);
			Preview->SetBrush(Brush);
		}
		UHorizontalBoxSlot* PreviewSlot = Body->AddChildToHorizontalBox(Preview);
		PreviewSlot->SetSize(WWOFillSize(1.f));
		PreviewSlot->SetHorizontalAlignment(HAlign_Center);
		PreviewSlot->SetVerticalAlignment(VAlign_Center);
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
	if (!Picker.IsEmpty())
	{
		Picker.Reset();
		Rebuild();
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
	Expanded = Tile;
	Picker.Reset();
	ExpandedTitle->SetText(FText::FromString(Tile));
	Rebuild();
	ExpandedScroll->ScrollToStart();
	TileSwitcher->SetActiveWidgetIndex(1);
}

void UWWOCharacterMenu::Collapse()
{
	Expanded.Reset();
	Picker.Reset();
	TileSwitcher->SetActiveWidgetIndex(0);
}

void UWWOCharacterMenu::TogglePicker(const FString& Name)
{
	Picker = Picker == Name ? FString() : Name;
	Rebuild();
}

void UWWOCharacterMenu::Rebuild()
{
	Content->ClearChildren();
	if (Expanded == TEXT("Flora")) BuildFloraRows();
	else if (Expanded == TEXT("Weapons")) BuildWeaponRows();
	else if (Expanded == TEXT("Frame Parts")) BuildFrameRows();
}

void UWWOCharacterMenu::BuildFloraRows()
{
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return;
	UWidgetTree* Tree = Styler->WidgetTree;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
	auto Sized = [Tree](UWidget* W, FVector2D Size) -> USizeBox*
	{
		USizeBox* Box = Tree->ConstructWidget<USizeBox>();
		Box->SetWidthOverride(Size.X * K);
		Box->SetHeightOverride(Size.Y * K);
		Box->AddChild(W);
		return Box;
	};
	auto Wrap = [Tree]() -> UWrapBox*
	{
		UWrapBox* W = Tree->ConstructWidget<UWrapBox>();
		W->SetInnerSlotPadding(FVector2D(10.f * K, 10.f * K));
		return W;
	};

	// Flora
	AddRowLabel(TEXT("Flora"));
	const UWWOFloraData* Flora = L->GetFlora();
	const FLinearColor FloraColor = Flora ? Flora->Color : FLinearColor(0.4f, 0.85f, 0.3f);
	FString FloraText = TEXT("Flora\n") + (Flora ? Flora->DisplayName : FString(TEXT("None")));
	if (Flora && !Flora->UltName.IsEmpty()) FloraText += TEXT("\nUlt: ") + Flora->UltName;
	Content->AddChildToVerticalBox(Sized(Slot(FloraText, FVector2D(300, 120), WWO::Darkened(FloraColor, 0.6f), FloraColor, 3.f, 24.f,
		[WeakThis] { if (WeakThis.IsValid()) WeakThis->TogglePicker(TEXT("flora")); }), FVector2D(300, 120)))->SetHorizontalAlignment(HAlign_Left);
	if (Picker == TEXT("flora"))
	{
		AddRowLabel(TEXT("Choose a flora (switches your grenade, abilities and ult)"));
		UWrapBox* Row = Wrap();
		for (UWWOFloraData* Option : L->Floras)
		{
			if (!Option->bUnlocked) continue;
			const bool bOn = Option == Flora;
			Row->AddChild(Sized(Slot(Option->DisplayName + (bOn ? TEXT("  ✓") : TEXT("")) + TEXT("\nUlt: ") + Option->UltName, FVector2D(240, 90),
				WWO::Darkened(Option->Color, 0.5f), bOn ? WWO::Lightened(Option->Color, 0.3f) : Option->Color, bOn ? 5.f : 2.f, 22.f,
				[WeakThis, WL, Option] { if (WL.IsValid()) WL->EquipFlora(Option); if (WeakThis.IsValid()) { WeakThis->Picker.Reset(); WeakThis->Rebuild(); } }), FVector2D(240, 90)));
		}
		Content->AddChildToVerticalBox(Row);
		if (Flora) Content->AddChildToVerticalBox(Styler->MakeText(Flora->Description, 18.f, FLinearColor(1, 1, 1, 0.6f), false, true));
	}

	// Abilities: grenade, ability 1, ability 2.
	AddRowLabel(TEXT("Abilities"));
	UHorizontalBox* Abilities = Tree->ConstructWidget<UHorizontalBox>();
	if (const UWWOGrenadeData* G = L->GetGrenade())
	{
		Abilities->AddChildToHorizontalBox(Sized(Slot(TEXT("Grenade\n") + G->DisplayName, FVector2D(220, 110), WWO::Darkened(G->Color, 0.6f), G->Color, 3.f, 22.f,
			[WeakThis] { if (WeakThis.IsValid()) WeakThis->TogglePicker(TEXT("grenade")); }), FVector2D(220, 110)))->SetPadding(FMargin(0, 0, 12.f * K, 0));
	}
	for (int32 SlotIndex = 0; SlotIndex < UWWOLoadoutSubsystem::AbilitySlots; ++SlotIndex)
	{
		const UWWOAbilityData* A = L->GetAbility(SlotIndex);
		const FLinearColor C = A ? A->Color : FLinearColor(0.3f, 0.3f, 0.3f);
		const FString Name = FString::Printf(TEXT("ability%d"), SlotIndex);
		Abilities->AddChildToHorizontalBox(Sized(Slot(FString::Printf(TEXT("Ability %d\n"), SlotIndex + 1) + (A ? A->DisplayName : FString(TEXT("Empty"))), FVector2D(220, 110),
			WWO::Darkened(C, 0.6f), C, 3.f, 22.f, [WeakThis, Name] { if (WeakThis.IsValid()) WeakThis->TogglePicker(Name); }), FVector2D(220, 110)))->SetPadding(FMargin(0, 0, 12.f * K, 0));
	}
	Content->AddChildToVerticalBox(Abilities);
	if (Picker == TEXT("grenade"))
	{
		AddRowLabel(TEXT("Choose a grenade"));
		UWrapBox* Row = Wrap();
		for (UWWOGrenadeData* Option : L->GrenadesForFlora())
		{
			if (!Option->bUnlocked) continue;
			const bool bOn = Option == L->GetGrenade();
			Row->AddChild(Sized(Slot(Option->DisplayName + (bOn ? TEXT("  ✓") : TEXT("")), FVector2D(220, 80), WWO::Darkened(Option->Color, 0.55f), Option->Color,
				bOn ? 5.f : 2.f, 20.f, [WeakThis, WL, Option] { if (WL.IsValid()) WL->EquipGrenade(Option); if (WeakThis.IsValid()) { WeakThis->Picker.Reset(); WeakThis->Rebuild(); } }),
				FVector2D(220, 80)));
		}
		Content->AddChildToVerticalBox(Row);
	}
	else if (Picker.StartsWith(TEXT("ability")))
	{
		const int32 SlotIndex = FCString::Atoi(*Picker.Mid(7));
		AddRowLabel(FString::Printf(TEXT("Choose ability %d (picking the other slot's ability swaps them)"), SlotIndex + 1));
		UWrapBox* Row = Wrap();
		for (UWWOAbilityData* Option : L->AbilitiesForFlora())
		{
			if (!Option->bUnlocked) continue;
			const int32 In = L->AbilitySlotOf(Option);
			const FString Mark = In == SlotIndex ? TEXT("  ✓") : (In != INDEX_NONE ? FString::Printf(TEXT("  (slot %d)"), In + 1) : FString());
			Row->AddChild(Sized(Slot(Option->DisplayName + Mark + TEXT("\n") + Option->Description, FVector2D(260, 110), WWO::Darkened(Option->Color, 0.55f), Option->Color,
				In == SlotIndex ? 5.f : 2.f, 15.f, [WeakThis, WL, Option, SlotIndex] { if (WL.IsValid()) WL->EquipAbility(SlotIndex, Option); if (WeakThis.IsValid()) { WeakThis->Picker.Reset(); WeakThis->Rebuild(); } }),
				FVector2D(260, 110)));
		}
		Content->AddChildToVerticalBox(Row);
	}

	// Traits: the equipped ones, then the picker.
	const TArray<UWWOTraitData*> Equipped = L->EquippedTraits();
	AddRowLabel(FString::Printf(TEXT("Traits (%d of %d)"), Equipped.Num(), UWWOLoadoutSubsystem::TraitSlots));
	UWrapBox* Slots = Wrap();
	for (int32 i = 0; i < UWWOLoadoutSubsystem::TraitSlots; ++i)
	{
		const bool bHas = i < Equipped.Num();
		Slots->AddChild(Sized(Slot(bHas ? Equipped[i]->DisplayName : TEXT("Empty"), FVector2D(170, 90), bHas ? FLinearColor(0.16f, 0.22f, 0.34f) : FLinearColor(0.2f, 0.21f, 0.25f),
			FLinearColor(0.55f, 0.7f, 1.f), bHas ? 2.f : 0.f, 16.f, [WeakThis] { if (WeakThis.IsValid()) WeakThis->TogglePicker(TEXT("traits")); }), FVector2D(170, 90)));
	}
	Content->AddChildToVerticalBox(Slots);
	if (Picker == TEXT("traits"))
	{
		AddRowLabel(FString::Printf(TEXT("Choose traits (click to equip / unequip, up to %d)"), UWWOLoadoutSubsystem::TraitSlots));
		UWrapBox* Row = Wrap();
		const bool bFull = Equipped.Num() >= UWWOLoadoutSubsystem::TraitSlots;
		for (UWWOTraitData* Option : L->TraitsForFlora())
		{
			if (!Option->bUnlocked) continue;
			const bool bOn = L->IsTraitEquipped(Option);
			UButton* B = Slot(Option->DisplayName + (bOn ? TEXT("  ✓") : TEXT("")) + TEXT("\n") + Option->Description, FVector2D(250, 110),
				bOn ? FLinearColor(0.16f, 0.22f, 0.34f) : FLinearColor(0.2f, 0.21f, 0.26f), bOn ? FLinearColor(0.55f, 0.7f, 1.f) : FLinearColor(0.4f, 0.42f, 0.5f), bOn ? 4.f : 1.f, 14.f,
				[WeakThis, WL, Option] { if (WL.IsValid()) WL->ToggleTrait(Option); if (WeakThis.IsValid()) WeakThis->Rebuild(); });
			if (bFull && !bOn) B->SetRenderOpacity(0.5f); // every slot is taken
			Row->AddChild(Sized(B, FVector2D(250, 110)));
		}
		Content->AddChildToVerticalBox(Row);
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
	USizeBox* Current = Tree->ConstructWidget<USizeBox>();
	Current->SetWidthOverride(320.f * K);
	Current->SetHeightOverride(110.f * K);
	Current->AddChild(Slot(L->GetWeapon()->DisplayName + TEXT("\n") + L->GetWeapon()->Archetype, FVector2D(320, 110), FLinearColor(0.24f, 0.25f, 0.3f),
		FLinearColor(0.85f, 0.85f, 0.9f), 3.f, 24.f, [] {}));
	Content->AddChildToVerticalBox(Current)->SetHorizontalAlignment(HAlign_Left);
	AddRowLabel(TEXT("Choose a weapon"));
	UUniformGridPanel* Grid = Tree->ConstructWidget<UUniformGridPanel>();
	Grid->SetSlotPadding(FMargin(6.f * K));
	int32 Index = 0;
	for (UWWOWeaponData* Option : L->Weapons)
	{
		if (!Option->bUnlocked) continue;
		const bool bOn = Option == L->GetWeapon();
		const FString Fire = Option->IsSemiAuto() ? TEXT("semi") : TEXT("auto");
		const FString Dmg = Option->Pellets > 1.f ? FString::Printf(TEXT("%dx%d dmg"), FMath::RoundToInt(Option->Pellets), FMath::RoundToInt(Option->Damage))
			: FString::Printf(TEXT("%d dmg"), FMath::RoundToInt(Option->Damage));
		const FString Stats = FString::Printf(TEXT("%d rpm %s · %s · %d rds"), FMath::RoundToInt(Option->FireRate), *Fire, *Dmg, FMath::RoundToInt(Option->MagazineSize));
		USizeBox* Box = Tree->ConstructWidget<USizeBox>();
		Box->SetWidthOverride(330.f * K);
		Box->SetHeightOverride(90.f * K);
		Box->AddChild(Slot(Option->DisplayName + (bOn ? TEXT("  ✓") : TEXT("")) + TEXT("\n") + Stats, FVector2D(330, 90), FLinearColor(0.2f, 0.21f, 0.26f),
			FLinearColor(0.9f, 0.9f, 0.95f), bOn ? 4.f : 0.f, 20.f, [WeakThis, WL, Option] { if (WL.IsValid()) WL->EquipWeapon(Option); if (WeakThis.IsValid()) WeakThis->Rebuild(); }));
		UUniformGridSlot* GridSlot = Grid->AddChildToUniformGrid(Box, Index / 2, Index % 2);
		GridSlot->SetHorizontalAlignment(HAlign_Left);
		++Index;
	}
	Content->AddChildToVerticalBox(Grid)->SetHorizontalAlignment(HAlign_Left);
}

void UWWOCharacterMenu::BuildFrameRows()
{
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return;
	UWidgetTree* Tree = Styler->WidgetTree;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
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
			USizeBox* Box = Tree->ConstructWidget<USizeBox>();
			Box->SetWidthOverride(200.f * K);
			Box->SetHeightOverride(120.f * K);
			Box->AddChild(Slot(Part->DisplayName + (bOn ? TEXT("  ✓") : TEXT("")) + TEXT("\n") + Part->Summary(), FVector2D(200, 120), FLinearColor(0.2f, 0.22f, 0.27f),
				FLinearColor(0.9f, 0.9f, 0.95f), bOn ? 4.f : 0.f, 15.f, [WeakThis, WL, Part] { if (WL.IsValid()) WL->EquipFramePart(Part); if (WeakThis.IsValid()) WeakThis->Rebuild(); }));
			Row->AddChild(Box);
		}
		Content->AddChildToVerticalBox(Row);
	}
	AddRowLabel(TEXT("Net effect"));
	const TArray<FString> Lines = L->GroupSummary();
	Content->AddChildToVerticalBox(Styler->MakeText(Lines.Num() > 0 ? FString::Join(Lines, TEXT("\n")) : TEXT("No changes (all Standard parts)"), 18.f));
}
