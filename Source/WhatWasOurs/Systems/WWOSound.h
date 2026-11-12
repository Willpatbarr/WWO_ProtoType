#pragma once

// Sound effects. The game plays an event by name; Config/Sounds.json says which sounds each
// event picks from, how loud, at what pitch and how far it carries. An event that isn't in the
// file (or lists no sounds) is silent, so a hook can go in before its sound exists.
//
//   UWWOSound::Play(this, "explosion", Where);     // in the world: quieter with distance
//   UWWOSound::PlayFlat(this, "hit");              // for whoever is playing here, at full volume
//   UWWOSound::PlayFor(Character, "reload_start"); // a character's own noise: flat for you,
//                                                  // in the world when it's someone else
//
// Everything runs on the machine that hears it: nothing is sent over the network, so play an
// event wherever its cause is seen (the hooks sit next to the matching visual effects).
// The player's volume is AWWOCharacter::SoundVolume (Settings, Audio).

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WWOSound.generated.h"

class USoundBase;
class USoundAttenuation;

UCLASS()
class WHATWASOURS_API UWWOSound : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static void Play(const UObject* Context, FName Event, const FVector& At, float Volume = 1.f);
	static void PlayFlat(const UObject* Context, FName Event, float Volume = 1.f);
	static void PlayFor(const AActor* Who, FName Event, float Volume = 1.f);
	static bool HasEvent(const UObject* Context, FName Event); // is it in Sounds.json (a weapon with its own shot sound)?

	// How many times each event has played since the game started, for the test logs.
	static TMap<FName, int32> Played;
	static FString Summary();

private:
	struct FEvent
	{
		TArray<USoundBase*> Sounds;
		float Volume = 1.f, PitchMin = 1.f, PitchMax = 1.f;
		float RangeM = 30.f;
		double LastAt = -1.0; // real time it last played (the same event can't stack within a few ms)
	};
	TMap<FName, FEvent> Events;
	bool bLoaded = false;
	UPROPERTY() TArray<TObjectPtr<USoundBase>> Loaded; // keeps the sounds from being collected
	UPROPERTY() TMap<int32, TObjectPtr<USoundAttenuation>> Falloffs; // one per range (m)

	void Load();
public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override; // loads here, so the first shot of a match doesn't hitch
private:
	void PlayEvent(FName Event, const FVector* At, float Volume);
	float PlayerVolume() const;
	USoundAttenuation* FalloffFor(float RangeM);
};
