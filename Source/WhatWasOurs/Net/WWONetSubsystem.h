#pragma once

// Local network play (LAN): one player hosts (a listen server: the host plays and runs the game for
// everyone) and others on the same network join. Uses Unreal's LAN sessions (the Null online
// subsystem), so hosts are found without typing an address.
// - Host(Map, bPvP, Bots): advertises a LAN game and reopens the map as a listen server.
// - PlayLocal(Map, bPvP, Bots): the same match alone, not advertised.
// - FindGames() fills Games with the LAN's open games (the launch screen lists them);
//   JoinGame(Index) joins one. FindAndJoin() searches and joins the first game found.
// - LeaveToTitle(): ends or leaves the game and goes back to the launch screen.
// The launch screen (UWWOLaunchMenu) shows when the game starts (WantsTitle), not on test runs.
// - JoinAddress(Ip): joins a host directly (no search), e.g. "127.0.0.1" for a second copy on this Mac.
// - Leave(): back to playing alone.
// Command line (for testing): -WWOHost hosts on start; -WWOJoin finds and joins a LAN game,
// -WWOJoin=<ip> joins that address.

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Engine/EngineBaseTypes.h"
#include "WWONetSubsystem.generated.h"

class FOnlineSessionSearch;

// A map the launch screen offers, and which modes it suits.
struct FWWOMapInfo
{
	FName Level;
	FString Title, Blurb;
	bool bPvP = false, bPvE = false;
	int32 TeamSize = 4; // PvP players per team (a local PvP match fills both teams with bots)
};

// An open game found on the LAN.
struct FWWOGameInfo
{
	FString Host, MapTitle;
	FName Map;
	bool bPvP = false;
	int32 Players = 1, MaxPlayers = 6, Ping = 0;
};

UCLASS()
class WHATWASOURS_API UWWONetSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UWWONetSubsystem* Get(const UObject* WorldContext);

	// Wave survival: up to 6 players (PvP will allow 12).
	static constexpr int32 MaxPlayers = 6;     // co-op waves
	static constexpr int32 MaxPvPPlayers = 12; // team deathmatch

	// bPvP: team deathmatch instead of waves. Bots: bot players in a PvP match (-1 = the host's Bots setting).
	void Host(FName Map, bool bPvP = false, int32 Bots = -1);
	void PlayTester(); // the firing tester: the Test Range alone, one dummy, no enemies (AWWOGameMode::bTester)
	void PlayLocal(FName Map, bool bPvP = false, int32 Bots = -1); // alone (PvP: against bots)
	void FindAndJoin();
	// The launch screen's list: FindGames searches (bSearching until it's done), Games is what the
	// last search found, GamesSerial goes up each time it changes.
	void FindGames();
	void JoinGame(int32 Index);
	TArray<FWWOGameInfo> Games;
	bool bSearching = false;
	int32 GamesSerial = 0;
	void LeaveToTitle();
	// Whether this map load should open on the launch screen: the first one of a normal run (not a
	// test run, not Play-in-Editor, not with a map or -WWOHost / -WWOJoin on the command line;
	// -WWOTitle forces it), and whenever a game was left for the title or the connection was lost.
	bool WantsTitle(const UObject* WorldContext);
	// Host: keeps the advertised game's map, mode and player count current (the game mode calls it).
	void PublishMatch(const UObject* WorldContext);

	static const TArray<FWWOMapInfo>& Maps();
	static const FWWOMapInfo* FindMap(FName Level);
	static FString MapTitle(FName Level);
	static FString MatchOptions(bool bListen, bool bPvP, int32 Bots); // the travel URL's options

	// The gamer tag: the name other players see above your health bar. Asked for on the launch
	// screen before hosting or joining, kept in Saved/WWO/GamerTag.txt, and sent to the host as the
	// travel URL's Tag option, which the game mode names the player from as they log in.
	static constexpr int32 MaxTagLength = 16;
	static FString CleanTag(const FString& Typed); // letters, digits, _ and - only, at most MaxTagLength
	const FString& GetGamerTag() const { return GamerTag; }
	void SetGamerTag(const FString& Typed, bool bSave = true);
	FString WithName(const FString& Options) const; // Options plus ?Tag=<tag> when there is one (AWWOGameMode::InitNewPlayer reads it)
	void JoinAddress(const FString& Address);
	void Leave();

	// What it's doing, for the menu ("Searching...", "No LAN games found", "Lost the connection"...).
	FString Status;
	bool IsOnline(const UObject* WorldContext) const;
	// The menu's status line: hosting / joined / alone, players in the match, this Mac's address, and Status.
	FString DescribeStatus(const UObject* WorldContext) const;
	static FString LocalAddress(); // this Mac's LAN address, for others to join by

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// The command line's -WWOHost / -WWOJoin, done once per run when the first local player starts.
	void HandleCommandLine(const UObject* WorldContext);

private:
	FName PendingMap;
	bool bPendingPvP = false;
	int32 PendingBots = -1;
	FString HostOptions() const { return WithName(MatchOptions(true, bPendingPvP, PendingBots)); }
	FString GamerTag;
	bool bTitleDecided = false; // the first map load's choice has been made
	bool bShowTitle = false;    // the next map load opens on the launch screen
	bool bJoinFirst = false;    // FindAndJoin: join the first game the search finds
	TSharedPtr<FOnlineSessionSearch> Search;
	// The finished search Games was built from, entry for entry: JoinGame joins from this, never
	// from Search, which a newer search may already be refilling in another order.
	TArray<FOnlineSessionSearchResult> FoundResults;
	bool IsHosting() const;
	void CancelSearch(); // a game is starting: the list's search has nothing more to say
	FDelegateHandle CreateHandle, DestroyHandle, FindHandle, JoinHandle;
	bool bCommandLineDone = false;
	FDelegateHandle NetFailureHandle, TravelFailureHandle;
	void OnNetworkFailure(UWorld* World, class UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);
	void OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error);

	IOnlineSessionPtr Sessions() const;
	void CreateSession();
	void OnDestroyed(FName Name, bool bOk);
	void OnCreated(FName Name, bool bOk);
	void OnFound(bool bOk);
	void OnJoined(FName Name, EOnJoinSessionCompleteResult::Type Result);
};
