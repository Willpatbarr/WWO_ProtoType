// FootRace's lobby page (UI/WWOFootRaceLobby.h).

#include "UI/WWOFootRaceLobby.h"
#include "UI/WWOUI.h"
#include "Game/WWOGameMode.h"
#include "Game/WWOGameState.h"
#include "Game/WWOPlayerState.h"
#include "Player/WWOPlayerController.h"
#include "WWOTypes.h"
#include "WhatWasOurs.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"

namespace
{
	constexpr float K = WWOUI::K;
}

void UWWOFootRaceLobby::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	WidgetTree->RootWidget = WidgetTree->ConstructWidget<UOverlay>();
	SetCursor(EMouseCursor::None); // the ring cursor is drawn instead (NativePaint)
}

int32 UWWOFootRaceLobby::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!bOpen) return Layer;
	WWOUI::PaintCursor(this, AllottedGeometry, OutDrawElements, Layer + 1);
	return Layer + 1;
}

bool UWWOFootRaceLobby::IsHost() const
{
	return GetWorld() && GetWorld()->GetNetMode() != NM_Client;
}

void UWWOFootRaceLobby::Open()
{
	bOpen = true;
	SetVisibility(ESlateVisibility::Visible);
	Rebuild();
}

void UWWOFootRaceLobby::Close()
{
	bOpen = false;
	SetVisibility(ESlateVisibility::Collapsed);
}

void UWWOFootRaceLobby::JoinTeam(int32 Team)
{
	if (AWWOPlayerController* PC = Cast<AWWOPlayerController>(GetOwningPlayer())) PC->LobbyChooseTeam(Team);
}

void UWWOFootRaceLobby::StepBots(int32 Team, int32 Delta)
{
	AWWOPlayerController* PC = Cast<AWWOPlayerController>(GetOwningPlayer());
	const AWWOGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWWOGameState>() : nullptr;
	if (!PC || !GS || !IsHost()) return;
	PC->LobbyBots(Team, (GS->LobbyBots.IsValidIndex(Team) ? GS->LobbyBots[Team] : 0) + Delta);
}

bool UWWOFootRaceLobby::StartRace()
{
	AWWOPlayerController* PC = Cast<AWWOPlayerController>(GetOwningPlayer());
	return PC && IsHost() && PC->LobbyStart();
}

FString UWWOFootRaceLobby::Signature() const
{
	const AWWOGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWWOGameState>() : nullptr;
	if (!GS) return FString();
	FString Text;
	for (int32 Count : GS->LobbyBots) Text += FString::Printf(TEXT("%d,"), Count);
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (const AWWOPlayerState* P = Cast<AWWOPlayerState>(PS); P && !P->IsABot()) Text += FString::Printf(TEXT("%s:%d;"), *P->GetPlayerName(), P->Team);
	}
	return Text;
}

void UWWOFootRaceLobby::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bOpen && Signature() != Shown) Rebuild(); // someone joined, switched, or the host changed the bots
}

UButton* UWWOFootRaceLobby::Button(const FString& Title, bool bEnabled, float Width, TFunction<void()> Fn)
{
	UTextBlock* Words = WWOUI::Spaced(WidgetTree, Title, WWOUI::Type::Body, bEnabled ? FLinearColor::White : WWOUI::Fainter, 200);
	WWOUI::FButtonLook Look = bEnabled ? WWOUI::CardLook(WWOUI::CardColor, WWOUI::CardEdge, 1.5f, FMargin(14.f, 10.f))
		: WWOUI::StillLook(WWOUI::RoundBrush(WWOUI::CardColor, WWOUI::Corner, FLinearColor(1, 1, 1, 0.15f), 1.5f), FMargin(14.f, 10.f));
	Look.HAlign = HAlign_Center;
	Look.VAlign = VAlign_Center;
	UButton* B = WWOUI::Button(WidgetTree, Words, Look);
	if (bEnabled) Handlers.Add(WWOUI::OnClick(B, this, MoveTemp(Fn)));
	return B;
}

UWidget* UWWOFootRaceLobby::TeamColumn(int32 Team)
{
	UWidgetTree* Tree = WidgetTree;
	const AWWOGameState* GS = GetWorld()->GetGameState<AWWOGameState>();
	const FLinearColor TeamColor = AWWOPlayerState::TeamColor(Team);
	const APlayerController* PC = GetOwningPlayer();
	const AWWOPlayerState* Me = PC ? PC->GetPlayerState<AWWOPlayerState>() : nullptr;
	TWeakObjectPtr<UWWOFootRaceLobby> Weak(this);
	UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>();
	Column->AddChildToVerticalBox(WWOUI::Spaced(Tree, AWWOPlayerState::TeamName(Team), WWOUI::Type::Heading, TeamColor, 260));
	Column->AddChildToVerticalBox(WWOUI::Rule(Tree, WWO::WithAlpha(TeamColor, 0.8f), 2.f))->SetPadding(FMargin(0, 8.f * K, 0, 10.f * K));
	// Its people.
	Listed[Team].Reset();
	if (GS)
	{
		for (const APlayerState* PS : GS->PlayerArray)
		{
			const AWWOPlayerState* P = Cast<AWWOPlayerState>(PS);
			if (!P || P->IsABot() || P->Team != Team) continue;
			Listed[Team].Add(P->GetPlayerName());
			const bool bMe = P == Me;
			Column->AddChildToVerticalBox(WWOUI::Text(Tree, P->GetPlayerName() + (bMe ? TEXT("  (you)") : TEXT("")), WWOUI::Type::Title, bMe ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.82f)))
				->SetPadding(FMargin(0, 4.f * K));
		}
	}
	if (Listed[Team].Num() == 0) Column->AddChildToVerticalBox(WWOUI::Text(Tree, TEXT("nobody yet"), WWOUI::Type::Body, WWOUI::Fainter))->SetPadding(FMargin(0, 4.f * K));
	// Its bots: the host's stepper, or the count.
	const int32 Bots = GS && GS->LobbyBots.IsValidIndex(Team) ? GS->LobbyBots[Team] : 0;
	BotsShown[Team] = Bots;
	UHorizontalBox* BotRow = Tree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* Label = BotRow->AddChildToHorizontalBox(WWOUI::Spaced(Tree, FString::Printf(TEXT("Bots  %d"), Bots), WWOUI::Type::Title, WWOUI::TextPrimary, 160));
	Label->SetSize(WWOUI::Fill());
	Label->SetVerticalAlignment(VAlign_Center);
	if (IsHost())
	{
		BotRow->AddChildToHorizontalBox(WWOUI::Wide(Tree, Button(TEXT("-"), true, 56.f, [Weak, Team] { if (Weak.IsValid()) Weak->StepBots(Team, -1); }), 56.f))->SetPadding(FMargin(0, 0, 8.f * K, 0));
		BotRow->AddChildToHorizontalBox(WWOUI::Wide(Tree, Button(TEXT("+"), true, 56.f, [Weak, Team] { if (Weak.IsValid()) Weak->StepBots(Team, 1); }), 56.f));
	}
	Column->AddChildToVerticalBox(BotRow)->SetPadding(FMargin(0, 16.f * K, 0, 16.f * K));
	// Joining it.
	const bool bOnIt = Me && Me->Team == Team;
	Column->AddChildToVerticalBox(Button(bOnIt ? FString(TEXT("Your team")) : FString::Printf(TEXT("Join %s"), *AWWOPlayerState::TeamName(Team)), !bOnIt, 300.f,
		[Weak, Team] { if (Weak.IsValid()) Weak->JoinTeam(Team); }));
	return Column;
}

void UWWOFootRaceLobby::Rebuild()
{
	UOverlay* Root = Cast<UOverlay>(WidgetTree->RootWidget);
	if (!Root) return;
	Root->ClearChildren();
	Handlers.Reset();
	Shown = Signature();
	UWidgetTree* Tree = WidgetTree;
	UBorder* Page = Tree->ConstructWidget<UBorder>();
	Page->SetBrushColor(WWO::Lin(WWO::WithAlpha(WWOUI::PageColor, 0.94f)));
	UOverlaySlot* PageSlot = Root->AddChildToOverlay(Page);
	PageSlot->SetHorizontalAlignment(HAlign_Fill);
	PageSlot->SetVerticalAlignment(VAlign_Fill);

	UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>();
	Column->AddChildToVerticalBox(WWOUI::Spaced(Tree, TEXT("FootRace"), WWOUI::Type::Small, WWOUI::Dim, 300))->SetHorizontalAlignment(HAlign_Center);
	Column->AddChildToVerticalBox(WWOUI::Spaced(Tree, TEXT("Pick your team"), WWOUI::Type::Display, FLinearColor::White, 320))->SetHorizontalAlignment(HAlign_Center);
	UHorizontalBox* Teams = Tree->ConstructWidget<UHorizontalBox>();
	Teams->AddChildToHorizontalBox(WWOUI::Wide(Tree, TeamColumn(0), 380.f))->SetPadding(FMargin(0, 0, 40.f * K, 0));
	Teams->AddChildToHorizontalBox(WWOUI::Wide(Tree, TeamColumn(1), 380.f))->SetPadding(FMargin(40.f * K, 0, 0, 0));
	UVerticalBoxSlot* TeamsSlot = Column->AddChildToVerticalBox(Teams);
	TeamsSlot->SetHorizontalAlignment(HAlign_Center);
	TeamsSlot->SetPadding(FMargin(0, 34.f * K, 0, 38.f * K));
	TWeakObjectPtr<UWWOFootRaceLobby> Weak(this);
	if (IsHost())
	{
		Column->AddChildToVerticalBox(WWOUI::Wide(Tree, Button(TEXT("Start race"), true, 320.f, [Weak] { if (Weak.IsValid()) Weak->StartRace(); }), 320.f))
			->SetHorizontalAlignment(HAlign_Center);
	}
	else
	{
		Column->AddChildToVerticalBox(WWOUI::Spaced(Tree, TEXT("Waiting for the host to start the race"), WWOUI::Type::Caption, WWOUI::Dim, 160))
			->SetHorizontalAlignment(HAlign_Center);
	}
	UOverlaySlot* ColumnSlot = Root->AddChildToOverlay(Column);
	ColumnSlot->SetHorizontalAlignment(HAlign_Center);
	ColumnSlot->SetVerticalAlignment(VAlign_Center);
}
