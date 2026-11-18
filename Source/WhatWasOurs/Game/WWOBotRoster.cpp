// The bot roster of a PvP match (Game/WWOBotRoster.h). Moved from AWWOGameMode (UpdateBots,
// RetireBotFor, the bot health lines of its Tick) with the same steps in the same order.

#include "Game/WWOBotRoster.h"
#include "Game/WWOGameMode.h"
#include "Game/WWOTeamDeathmatch.h"
#include "Game/WWOPlayerState.h"
#include "Player/WWOBotController.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
#include "Systems/WWOWorldSubsystem.h"
#include "WWOSwitches.h"
#include "WhatWasOurs.h"
#include "EngineUtils.h"

int32 FWWOBotRoster::Wanted(const AWWOGameMode& GM) const
{
	// The launch screen's count when it gave one (a local match fills the map's teams exactly), else
	// the Bots setting (or -WWOBots); then one more per person who left, one fewer per person who joined.
	// (A test run takes only what its command line asks for, not the saved Bots setting.)
	const int32 Asked = OptionBots >= 0 ? OptionBots : FMath::Max(BotsWanted, GM.bTestRun ? 0 : FMath::RoundToInt(GM.Bots));
	return FMath::Clamp(Asked + BotsExtra, 0, 11);
}

void FWWOBotRoster::Update(AWWOGameMode& GM, FWWOMatchRules& Match)
{
	// One a frame, once the host's own player is in.
	const int32 Want = Wanted(GM);
	UWorld* World = GM.GetWorld();
	if (BotsSpawned >= Want || Match.State != EWWOWaveState::Match || !UWWOWorldSubsystem::Get(World)->GetPlayer()) return;
	AWWOBotController* Bot = World->SpawnActor<AWWOBotController>();
	if (!Bot) return;
	++BotsSpawned;
	if (Bot->PlayerState) Bot->PlayerState->SetPlayerName(FString::Printf(TEXT("bot%d"), BotsSpawned));
	GM.BalanceTeam(Bot);
	// -WWOBotsPeace: every bot on one team, so nobody fights and they all just roam the map (a
	// steady scene of moving characters for performance tests).
	if (WWOSwitches::Has(TEXT("WWOBotsPeace"))) GM.SetTeam(Bot->GetPlayerState<AWWOPlayerState>(), 0);
	if (FWWOTeamDeathmatch* Deathmatch = Match.AsDeathmatch()) Deathmatch->TeamGiven.Add(Bot->GetPlayerState<AWWOPlayerState>());
	GM.RestartPlayer(Bot);
	if (AWWOCharacter* Body = Cast<AWWOCharacter>(Bot->GetPawn()))
	{
		const FTransform Spot = GM.PvPSpawnFor(Body);
		Body->TeleportTo(Spot.GetLocation(), Spot.Rotator());
		// -WWOBotsLineUp: a repeatable scene for performance tests. The bots stand in rows of four in
		// front of the host, facing it, and do nothing.
		if (const AWWOCharacter* Host = UWWOWorldSubsystem::Get(World)->GetPlayer(); Host && WWOSwitches::Has(TEXT("WWOBotsLineUp")))
		{
			Bot->bParked = true;
			const int32 Index = BotsSpawned - 1;
			const FVector Forward = Host->GetActorForwardVector(), Right = Host->GetActorRightVector();
			const FVector At = Host->GetActorLocation() + Forward * (500.f + (Index / 4) * 220.f) + Right * ((Index % 4) - 1.5f) * 170.f + FVector(0, 0, 150.f);
			Body->TeleportTo(At, (-Forward).Rotation(), false, true);
			Body->SetViewRotation(0.f, (-Forward).Rotation().Yaw);
		}
	}
	UE_LOG(LogWWO, Display, TEXT("PvP: bot %d of %d joined (%s)"), BotsSpawned, Want, Bot->GetPawn() ? *Bot->GetPawn()->GetName() : TEXT("no body"));
}

void FWWOBotRoster::RetireFor(AWWOGameMode& GM, AController* Joiner)
{
	// One bot leaves, from the bigger team, so the match stays the size it was.
	int32 Count[2] = { 0, 0 };
	TArray<AWWOBotController*> BotsNow;
	for (TActorIterator<AWWOBotController> It(GM.GetWorld()); It; ++It)
	{
		BotsNow.Add(*It);
		if (const AWWOPlayerState* PS = It->GetPlayerState<AWWOPlayerState>(); PS && PS->Team >= 0 && PS->Team <= 1) ++Count[PS->Team];
	}
	if (BotsNow.Num() == 0) return;
	const int32 From = Count[1] > Count[0] ? 1 : 0;
	AWWOBotController* Leaving = BotsNow[0];
	for (AWWOBotController* Bot : BotsNow)
	{
		if (const AWWOPlayerState* PS = Bot->GetPlayerState<AWWOPlayerState>(); PS && PS->Team == From) Leaving = Bot;
	}
	UE_LOG(LogWWO, Display, TEXT("PvP: %s makes room for %s"), *GetNameSafe(Leaving->PlayerState), *GetNameSafe(Joiner));
	if (APawn* Body = Leaving->GetPawn()) Body->Destroy();
	Leaving->Destroy();
	--BotsSpawned;
	--BotsExtra;
}

void FWWOBotRoster::FollowSettings(AWWOGameMode& GM) const
{
	for (AWWOCharacter* P : UWWOWorldSubsystem::Get(&GM)->GetPlayers())
	{
		if (!P->GetBot() || (P->Health->BaseHealth == GM.BotHealth && P->Health->BaseShield == GM.BotShield)) continue;
		P->Health->BaseHealth = GM.BotHealth;
		P->Health->BaseShield = GM.BotShield;
		if (!P->Health->bDead)
		{
			P->Health->Health = P->Health->MaxHealth();
			P->Health->Shield = P->Health->MaxShield();
		}
	}
}
