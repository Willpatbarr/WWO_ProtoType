#include "Systems/WWOSound.h"
#include "Player/WWOCharacter.h"
#include "WhatWasOurs.h"
#include "WWOTypes.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"

TMap<FName, int32> UWWOSound::Played;

void UWWOSound::Load()
{
	bLoaded = true;
	FString Text;
	TSharedPtr<FJsonObject> Json;
	if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectConfigDir() / TEXT("Sounds.json")))
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
	{
		UE_LOG(LogWWO, Warning, TEXT("Sounds: Config/Sounds.json is missing or isn't valid JSON; the game is silent"));
		return;
	}
	const TSharedPtr<FJsonObject>* List = nullptr;
	if (!Json->TryGetObjectField(TEXT("events"), List)) return;
	int32 Missing = 0;
	for (const auto& Pair : (*List)->Values)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (!Pair.Value->TryGetObject(O)) continue;
		FEvent E;
		const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
		if ((*O)->TryGetArrayField(TEXT("sounds"), Names))
		{
			for (const TSharedPtr<FJsonValue>& Name : *Names)
			{
				const FString Asset = Name->AsString();
				if (USoundBase* Sound = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/WWO/Audio/%s.%s"), *Asset, *Asset)))
				{
					E.Sounds.Add(Sound);
					Loaded.Add(Sound);
				}
				else ++Missing;
			}
		}
		double Number = 1.0;
		if ((*O)->TryGetNumberField(TEXT("volume"), Number)) E.Volume = (float)Number;
		if ((*O)->TryGetNumberField(TEXT("range"), Number)) E.RangeM = (float)Number;
		const TArray<TSharedPtr<FJsonValue>>* Pitch = nullptr;
		if ((*O)->TryGetArrayField(TEXT("pitch"), Pitch) && Pitch->Num() >= 2)
		{
			E.PitchMin = (float)(*Pitch)[0]->AsNumber();
			E.PitchMax = (float)(*Pitch)[1]->AsNumber();
		}
		Events.Add(FName(*Pair.Key), E);
	}
	UE_LOG(LogWWO, Display, TEXT("Sounds: %d events, %d sounds loaded, %d named in Sounds.json but not found"), Events.Num(), Loaded.Num(), Missing);
}

float UWWOSound::PlayerVolume() const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const AWWOCharacter* Me = PC ? Cast<AWWOCharacter>(PC->GetPawn()) : nullptr;
	return Me ? FMath::Max(Me->SoundVolume, 0.f) : 1.f;
}

USoundAttenuation* UWWOSound::FalloffFor(float RangeM)
{
	// Full volume close by (a tenth of the range), fading to nothing at the range.
	const int32 Key = FMath::Max(FMath::RoundToInt(RangeM), 1);
	if (const TObjectPtr<USoundAttenuation>* Found = Falloffs.Find(Key)) return Found->Get();
	USoundAttenuation* Falloff = NewObject<USoundAttenuation>(this);
	Falloff->Attenuation.bAttenuate = true;
	Falloff->Attenuation.bSpatialize = true;
	Falloff->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	Falloff->Attenuation.AttenuationShapeExtents = FVector(Key * 0.1f * WWO::M);
	Falloff->Attenuation.FalloffDistance = Key * 0.9f * WWO::M;
	Falloffs.Add(Key, Falloff);
	return Falloff;
}

void UWWOSound::PlayEvent(FName Event, const FVector* At, float Volume)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer || !GEngine || !GEngine->UseSound()) return;
	if (!bLoaded) Load();
	FEvent* E = Events.Find(Event);
	if (!E) return;
	// The same event twice within a few ms (a shotgun's pellets landing, a dozen bots firing on
	// one frame) plays once.
	const double Now = FPlatformTime::Seconds();
	if (Now - E->LastAt < 0.025) return;
	E->LastAt = Now;
	++Played.FindOrAdd(Event);
	if (E->Sounds.Num() == 0) return;
	const float Loud = E->Volume * Volume * PlayerVolume();
	if (Loud <= 0.001f) return;
	USoundBase* Sound = E->Sounds[FMath::RandHelper(E->Sounds.Num())];
	const float Pitch = FMath::FRandRange(E->PitchMin, E->PitchMax);
	// A sound with no range is only ever for the player here; the menus' own play while paused.
	if (At && E->RangeM > 0.f) UGameplayStatics::PlaySoundAtLocation(World, Sound, *At, FRotator::ZeroRotator, Loud, Pitch, 0.f, FalloffFor(E->RangeM));
	else UGameplayStatics::PlaySound2D(World, Sound, Loud, Pitch, 0.f, nullptr, nullptr, Event.ToString().StartsWith(TEXT("ui_")));
}

void UWWOSound::Play(const UObject* Context, FName Event, const FVector& At, float Volume)
{
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	if (UWWOSound* Sounds = World ? World->GetSubsystem<UWWOSound>() : nullptr) Sounds->PlayEvent(Event, &At, Volume);
}

void UWWOSound::PlayFlat(const UObject* Context, FName Event, float Volume)
{
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	if (UWWOSound* Sounds = World ? World->GetSubsystem<UWWOSound>() : nullptr) Sounds->PlayEvent(Event, nullptr, Volume);
}

void UWWOSound::PlayFor(const AActor* Who, FName Event, float Volume)
{
	if (!Who) return;
	const APawn* Pawn = Cast<APawn>(Who);
	if (Pawn && Pawn->IsLocallyControlled() && Pawn->IsPlayerControlled()) PlayFlat(Who, Event, Volume);
	else Play(Who, Event, Who->GetActorLocation(), Volume);
}

bool UWWOSound::HasEvent(const UObject* Context, FName Event)
{
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	UWWOSound* Sounds = World ? World->GetSubsystem<UWWOSound>() : nullptr;
	if (!Sounds) return false;
	if (!Sounds->bLoaded) Sounds->Load();
	return Sounds->Events.Contains(Event);
}

FString UWWOSound::Summary()
{
	TArray<FString> Parts;
	int32 Total = 0;
	TArray<FName> Names;
	Played.GetKeys(Names);
	Names.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	for (const FName& Name : Names)
	{
		Parts.Add(FString::Printf(TEXT("%s %d"), *Name.ToString(), Played[Name]));
		Total += Played[Name];
	}
	return FString::Printf(TEXT("%d played (%s)"), Total, *FString::Join(Parts, TEXT(", ")));
}
