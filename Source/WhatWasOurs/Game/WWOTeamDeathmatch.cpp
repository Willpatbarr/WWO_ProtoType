// Team deathmatch (Game/WWOTeamDeathmatch.h). Moved from AWWOGameMode (the PvP branches of Begin,
// PostLogin and Logout, UpdateMatch, OnPvPDeath, EndMatch, SwitchTeam, BalanceTeam, SetTeam,
// CountTeams, PvPSpawnFor and the PvP status lines) as they were. OnPvPDeath is OnDeath + OnKill,
// in the same order: the death, the credit, the score, the log, the kill feed, the limit.

#include "Game/WWOTeamDeathmatch.h"
#include "Game/WWOGameMode.h"
#include "Game/WWOGameState.h"
#include "Game/WWOPlayerState.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
#include "Enemies/WWOEnemy.h"
#include "Allies/WWOAlly.h"
#include "Systems/WWOWorldSubsystem.h"
#include "WWOTypes.h"
#include "WhatWasOurs.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "Player/WWOTestTally.h"

namespace
{
	// Adds the players and bots on each PvP team to Count, leaving out Except (someone about to be placed).
	void CountTeams(const AGameStateBase* GameState, int32 Count[2], const AWWOPlayerState* Except = nullptr)
	{
		for (const APlayerState* Other : GameState->PlayerArray)
		{
			if (const AWWOPlayerState* PS = Cast<AWWOPlayerState>(Other); PS && PS != Except && PS->Team >= 0 && PS->Team <= 1) ++Count[PS->Team];
		}
	}
}

FWWOTeamDeathmatch::~FWWOTeamDeathmatch()
{
	for (const TPair<TWeakObjectPtr<UWWOHealthComponent>, FDelegateHandle>& B : Bindings)
	{
		if (UWWOHealthComponent* H = B.Key.Get()) H->OnDied.Remove(B.Value);
	}
}

void FWWOTeamDeathmatch::Begin()
{
	// No waves, no enemies, no AI teammates.
	UWorld* World = Mode.GetWorld();
	for (TActorIterator<AWWOEnemy> It(World); It; ++It) It->Destroy();
	for (TActorIterator<AWWOAlly> It(World); It; ++It) It->Destroy();
	if (AWWOGameState* GS = Mode.GetGameState<AWWOGameState>())
	{
		GS->bPvP = true;
		GS->TeamScores = { 0, 0 };
		GS->bMatchOver = false;
		GS->MatchWinner = -1;
	}
	MatchLeft = Mode.TimeLimit * 60.f;
	MatchElapsed = 0.f;
	Winner = -1;
	State = EWWOWaveState::Match;
}

void FWWOTeamDeathmatch::OnPlayerJoined(APlayerController* NewPlayer)
{
	const bool bMidMatch = State == EWWOWaveState::Match && !NewPlayer->IsLocalController();
	if (bMidMatch) Mode.Roster.RetireFor(Mode, NewPlayer); // before the team is picked: the person takes the bot's side
	BalanceTeam(Mode.GameState, NewPlayer);
	TeamGiven.Add(NewPlayer->GetPlayerState<AWWOPlayerState>());
	// Into a match under way: at a spot away from the other team, not the map's one Player Start.
	if (AWWOCharacter* Body = bMidMatch ? Cast<AWWOCharacter>(NewPlayer->GetPawn()) : nullptr)
	{
		const FTransform Spot = SpawnSpot(Mode, Body);
		Body->TeleportTo(Spot.GetLocation(), Spot.Rotator());
		Body->Health->SpawnYaw = Spot.Rotator().Yaw;
		NewPlayer->ClientSetRotation(Spot.Rotator());
	}
}

void FWWOTeamDeathmatch::OnPlayerLeaving(AController* Exiting)
{
	// A person leaving (Quit to main menu, or a dropped connection): a bot fills their place, so the
	// teams stay the size they were. The roster spawns it and puts it on the smaller team.
	const APlayerController* Person = Cast<APlayerController>(Exiting);
	if (Person && !Person->IsLocalController() && State == EWWOWaveState::Match)
	{
		Mode.Roster.PersonLeft();
		UE_LOG(LogWWO, Display, TEXT("PvP: %s left, a bot takes the place"), *GetNameSafe(Exiting->PlayerState));
	}
}

void FWWOTeamDeathmatch::SwitchTeam(AController* Player)
{
	AWWOPlayerState* PS = Player ? Player->GetPlayerState<AWWOPlayerState>() : nullptr;
	if (!PS || State != EWWOWaveState::Match) return;
	const double Now = Mode.GetWorld()->GetTimeSeconds();
	if (Now - PS->LastSwitch < 10.0) return;
	int32 Count[2] = { 0, 0 };
	CountTeams(Mode.GameState, Count);
	const int32 To = 1 - PS->Team;
	if (Count[To] + 1 > Count[PS->Team]) return; // it would leave the other side the bigger one
	PS->LastSwitch = Now;
	SetTeam(PS, To);
	if (AWWOCharacter* Body = Cast<AWWOCharacter>(Player->GetPawn()); Body && !Body->Health->bDead)
	{
		const FTransform Spot = SpawnSpot(Mode, Body);
		Body->TeleportTo(Spot.GetLocation(), Spot.Rotator());
		Body->Health->Health = Body->Health->MaxHealth();
		Body->Health->Shield = Body->Health->MaxShield();
		Body->Health->LastAttacker = nullptr;
		Body->Health->SpawnYaw = Spot.Rotator().Yaw;
		Player->ClientSetRotation(Spot.Rotator());
		Body->SetViewRotation(0.f, Spot.Rotator().Yaw);
	}
}

void FWWOTeamDeathmatch::BalanceTeam(const AGameStateBase* GameState, AController* Player)
{
	AWWOPlayerState* Mine = Player ? Player->GetPlayerState<AWWOPlayerState>() : nullptr;
	if (!Mine || !GameState) return;
	int32 Count[2] = { 0, 0 };
	CountTeams(GameState, Count, Mine);
	SetTeam(Mine, Count[1] < Count[0] ? 1 : 0);
}

void FWWOTeamDeathmatch::SetTeam(AWWOPlayerState* PS, int32 Team)
{
	if (!PS) return;
	PS->Team = FMath::Clamp(Team, 0, 1);
	UE_LOG(LogWWO, Display, TEXT("PvP: %s is on %s"), *PS->GetPlayerName(), *AWWOPlayerState::TeamName(PS->Team));
}

void FWWOTeamDeathmatch::Update(float DeltaSeconds)
{
	// Score every player's deaths (they bind once, like the waves' death binding).
	for (AWWOCharacter* P : UWWOWorldSubsystem::Get(&Mode)->GetPlayers())
	{
		if (PvPBound.Contains(P)) continue;
		PvPBound.Add(P);
		TWeakObjectPtr<AWWOCharacter> Weak = P;
		Bindings.Add({ P->Health, P->Health->OnDied.AddWeakLambda(&Mode, [this, Weak] { if (Weak.IsValid()) OnDeath(Weak.Get()); }) });
		// A player who joined before PostLogin gave them a team (the host's own at start).
		if (AWWOPlayerState* PS = P->GetPlayerState<AWWOPlayerState>(); PS && P->GetController() && !TeamGiven.Contains(PS))
		{
			TeamGiven.Add(PS);
			BalanceTeam(Mode.GameState, P->GetController());
		}
	}
	if (State != EWWOWaveState::Match) return;
	if (Mode.CmdKillTarget >= 0.f) Mode.KillTarget = Mode.CmdKillTarget; // a test's own target, over the saved setting loaded since
	// By time played, so the limit can be turned on, off or changed mid-match.
	MatchElapsed += DeltaSeconds;
	MatchLeft = FMath::Max(Mode.TimeLimit * 60.f - MatchElapsed, 0.f);
	if (Mode.TimeLimit > 0.f && MatchElapsed >= Mode.TimeLimit * 60.f)
	{
		const AWWOGameState* GS = Mode.GetGameState<AWWOGameState>();
		const int32 Blue = GS && GS->TeamScores.Num() > 0 ? GS->TeamScores[0] : 0;
		const int32 Orange = GS && GS->TeamScores.Num() > 1 ? GS->TeamScores[1] : 0;
		EndMatch(Blue == Orange ? -1 : (Blue > Orange ? 0 : 1)); // time: the most kills
	}
}

void FWWOTeamDeathmatch::OnDeath(AWWOCharacter* Victim)
{
	if (State != EWWOWaveState::Match || !Victim) return;
	AWWOPlayerState* VictimPS = Victim->GetPlayerState<AWWOPlayerState>();
	if (VictimPS) ++VictimPS->Deaths;
	// Whoever hit them last, if that was in the last 10 s (a fall long after a fight is nobody's kill).
	const bool bRecent = Mode.GetWorld()->GetTimeSeconds() - Victim->Health->LastAttackTime <= 10.0;
	AWWOCharacter* Killer = bRecent ? Cast<AWWOCharacter>(Victim->Health->LastAttacker.Get()) : nullptr;
	AWWOPlayerState* KillerPS = Killer ? Killer->GetPlayerState<AWWOPlayerState>() : nullptr;
	if (!KillerPS || !UWWOWorldSubsystem::CanHurt(Killer, Victim))
	{
		UE_LOG(LogWWO, Display, TEXT("PvP: %s died (no kill credit)"), *GetNameSafe(Victim));
		return; // a fall or anything that isn't the other team: nobody scores
	}
	OnKill(Victim, VictimPS, KillerPS);
}

void FWWOTeamDeathmatch::OnKill(AWWOCharacter* Victim, AWWOPlayerState* VictimPS, AWWOPlayerState* KillerPS)
{
	++KillerPS->Kills;
	FWWOTestTally::Count(&FWWOTestTally::PvPKills);
	// A precision kill (the hit that killed was a head hit) scores more.
	const bool bPrecision = Victim->Health->bLastAttackCritical && Mode.GetWorld()->GetTimeSeconds() - Victim->Health->LastAttackTime <= 0.05;
	const int32 Points = bPrecision ? AWWOGameState::PrecisionKillPoints : AWWOGameState::KillPoints;
	if (bPrecision) ++KillerPS->PrecisionKills;
	AWWOGameState* GS = Mode.GetGameState<AWWOGameState>();
	if (GS)
	{
		if (GS->TeamScores.Num() < 2) GS->TeamScores.SetNum(2);
		GS->TeamScores[KillerPS->Team] += Points;
	}
	UE_LOG(LogWWO, Display, TEXT("PvP: %s (%s) killed %s; %s %d"), *KillerPS->GetPlayerName(), *AWWOPlayerState::TeamName(KillerPS->Team),
		VictimPS ? *VictimPS->GetPlayerName() : *Victim->GetName(), *AWWOPlayerState::TeamName(KillerPS->Team), GS ? GS->TeamScores[KillerPS->Team] : 0);
	// The kill feed, on everyone's screen: killer > killee.
	if (GS) GS->ReportKill(KillerPS, KillerPS->GetPlayerName(), KillerPS->Team, VictimPS ? VictimPS->GetPlayerName() : Victim->GetName(), VictimPS ? VictimPS->Team : -1,
		bPrecision ? AWWOGameState::KillPrecision : FName(), Points);
	if (GS && Mode.KillTarget >= 1.f && GS->TeamScores[KillerPS->Team] >= FMath::RoundToInt(Mode.KillTarget)) EndMatch(KillerPS->Team);
}

void FWWOTeamDeathmatch::EndMatch(int32 WinningTeam)
{
	State = EWWOWaveState::MatchOver;
	Winner = WinningTeam;
	FWWOTestTally::Count(&FWWOTestTally::PvPMatchesEnded);
	// The scoreboard comes up on every machine and stays until the host picks what's next
	// (UWWOScoreboard: next match, change map, quit). A test run restarts by itself as before.
	if (AWWOGameState* GS = Mode.GetGameState<AWWOGameState>())
	{
		GS->bMatchOver = true;
		GS->MatchWinner = (int8)WinningTeam;
	}
	if (Mode.bTestRun) Mode.RestartLeft = 6.f;
	UE_LOG(LogWWO, Display, TEXT("PvP: match over, %s"), WinningTeam < 0 ? TEXT("a draw") : *(AWWOPlayerState::TeamName(WinningTeam) + TEXT(" wins")));
}

FString FWWOTeamDeathmatch::StatusText(FLinearColor& OutColor) const
{
	OutColor = FLinearColor::White;
	const AWWOGameState* GS = Mode.GetGameState<AWWOGameState>();
	const int32 Blue = GS && GS->TeamScores.Num() > 0 ? GS->TeamScores[0] : 0;
	const int32 Orange = GS && GS->TeamScores.Num() > 1 ? GS->TeamScores[1] : 0;
	switch (State)
	{
	case EWWOWaveState::Match:
	{
		FString Line = FString::Printf(TEXT("BLUE %d  –  %d ORANGE"), Blue, Orange);
		if (Mode.KillTarget >= 1.f) Line += FString::Printf(TEXT("   ·   first to %d"), FMath::RoundToInt(Mode.KillTarget));
		if (Mode.TimeLimit > 0.f)
		{
			const int32 Secs = FMath::CeilToInt(FMath::Max(MatchLeft, 0.f));
			Line += FString::Printf(TEXT("   ·   %d:%02d"), Secs / 60, Secs % 60);
		}
		return Line;
	}
	case EWWOWaveState::MatchOver:
		OutColor = Winner < 0 ? FLinearColor(0.9f, 0.9f, 0.9f) : AWWOPlayerState::TeamColor(Winner);
		return Winner < 0 ? FString::Printf(TEXT("DRAW  %d – %d%s"), Blue, Orange, Mode.bTestRun ? TEXT("   ·   restarting") : TEXT(""))
			: FString::Printf(TEXT("%s WINS  %d – %d%s"), *AWWOPlayerState::TeamName(Winner), Blue, Orange, Mode.bTestRun ? TEXT("   ·   restarting") : TEXT(""));
	default:
		return FString();
	}
}

FTransform FWWOTeamDeathmatch::SpawnSpot(const AWWOGameMode& GM, const AWWOCharacter* Player)
{
	// The spawn spot (enemy spawn points and player starts) farthest from the nearest living
	// player of the other team. Enemy spawn points tagged PvEOnly (L_Cascade's, out on the nodes and
	// terminals) are for the waves only.
	TArray<FTransform> Spots;
	for (AActor* Point : GM.SpawnPoints())
	{
		if (!Point->ActorHasTag("PvEOnly")) Spots.Add(FTransform(Point->GetActorRotation(), Point->GetActorLocation() + FVector(0, 0, 100.f)));
	}
	for (TActorIterator<APlayerStart> It(GM.GetWorld()); It; ++It) Spots.Add(It->GetActorTransform());
	if (Spots.Num() == 0) return Player ? Player->GetActorTransform() : FTransform::Identity;
	TArray<const AWWOCharacter*> Others;
	for (const AWWOCharacter* Other : UWWOWorldSubsystem::Get(&GM)->GetPlayers())
	{
		if (Other != Player && !Other->Health->bDead && UWWOWorldSubsystem::CanHurt(Player, Other)) Others.Add(Other);
	}
	// Rank them by how far the nearest foe is, leave out any somebody is standing on, and take one
	// of the best three at random: a team that dies together doesn't all come back on one spot.
	TArray<TPair<float, int32>> Ranked;
	for (int32 i = 0; i < Spots.Num(); ++i)
	{
		bool bTaken = false;
		for (const AWWOCharacter* Any : UWWOWorldSubsystem::Get(&GM)->GetPlayers())
		{
			bTaken |= Any != Player && !Any->Health->bDead && FVector::Dist2D(Any->GetActorLocation(), Spots[i].GetLocation()) < 150.f;
		}
		if (bTaken) continue;
		float Nearest = Others.Num() > 0 ? TNumericLimits<float>::Max() : 0.f;
		for (const AWWOCharacter* Other : Others) Nearest = FMath::Min(Nearest, FVector::Dist(Other->GetActorLocation(), Spots[i].GetLocation()));
		Ranked.Add({ Nearest, i });
	}
	Ranked.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key > B.Key; });
	const int32 Best = Ranked.Num() > 0 ? Ranked[WWO::RandRange(0, FMath::Min(Ranked.Num(), Others.Num() > 0 ? 3 : Ranked.Num()) - 1)].Value
		: WWO::RandRange(0, Spots.Num() - 1);
	// Facing the middle of the map.
	FTransform Spot = Spots[Best];
	FVector ToMiddle = -Spot.GetLocation();
	ToMiddle.Z = 0.f;
	if (!ToMiddle.IsNearlyZero()) Spot.SetRotation(ToMiddle.Rotation().Quaternion());
	return Spot;
}
