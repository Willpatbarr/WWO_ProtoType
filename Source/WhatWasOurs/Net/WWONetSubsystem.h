#pragma once

// Local network play (LAN): one player hosts (a listen server: the host plays and runs the game for
// everyone) and others on the same network join. Uses Unreal's LAN sessions (the Null online
// subsystem), so hosts are found without typing an address.
// - Host(Map): advertises a LAN game and reopens the map as a listen server.
// - FindAndJoin(): searches the LAN and joins the first game found.
// - JoinAddress(Ip): joins a host directly (no search), e.g. "127.0.0.1" for a second copy on this Mac.
// - Leave(): back to playing alone.
// Command line (for testing): -WWOHost hosts on start; -WWOJoin finds and joins a LAN game,
// -WWOJoin=<ip> joins that address.

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Engine/EngineBaseTypes.h"
#include "WWONetSubsystem.generated.h"

class FOnlineSessionSearch;

UCLASS()
class WHATWASOURS_API UWWONetSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UWWONetSubsystem* Get(const UObject* WorldContext);

	// Wave survival: up to 6 players (PvP will allow 12).
	static constexpr int32 MaxPlayers = 6;

	void Host(FName Map);
	void FindAndJoin();
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
	TSharedPtr<FOnlineSessionSearch> Search;
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
