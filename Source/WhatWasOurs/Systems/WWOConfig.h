#pragma once

// The game's JSON files: tuning data in Config/ (staged into packaged builds, see WhatWasOurs.Build.cs)
// and each player's own files in Saved/WWO/. Every loader goes through here; what an object's
// fields mean stays with the system that reads them.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace WWOConfig
{
	// Full path of a file in the project's Config folder, e.g. Path(TEXT("Sounds.json")).
	FString Path(const TCHAR* Name);
	// Full path of one of this machine's files, e.g. SavedPath(TEXT("Keybinds.json")) is Saved/WWO/Keybinds.json.
	FString SavedPath(const TCHAR* Name);

	// Reads a file and parses it as one JSON object. Null when the file is missing or isn't a valid
	// JSON object; bOutFileRead (optional) tells the two apart: true when the file itself was read.
	TSharedPtr<FJsonObject> Load(const FString& FilePath, bool* bOutFileRead = nullptr);

	// Config/<Name>, read and parsed once a run (null is remembered too): edits apply on the next
	// start. Everyone asking for the same name shares one object, so treat it as read-only unless
	// the change is meant for every reader. Game thread only.
	TSharedPtr<FJsonObject> LoadCached(const TCHAR* Name);

	// The player's own file, or with none on disk the one the build shipped with (Config/<ShippedName>).
	// A saved file that exists but isn't valid JSON gives null: it does not fall back to the shipped one.
	// bOutMine (optional): whether the player's own file was the one read.
	TSharedPtr<FJsonObject> LoadSavedElseShipped(const FString& SavedFilePath, const TCHAR* ShippedName, bool* bOutMine = nullptr);

	// Writes an object to a file as JSON. False when the file could not be written.
	bool Save(const FString& FilePath, const TSharedRef<FJsonObject>& Json);

	// A 3-number array field (or 2 numbers and a zero, or one number for all three), else Default.
	FVector Vec(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, const FVector& Default = FVector::ZeroVector);

	// The object's "color" field: a palette name (dark, steel, orange, gold, olive, white, cyan, green,
	// red, yellow, purple) or three numbers, sRGB. Steel when there is none.
	FLinearColor Color(const TSharedPtr<FJsonObject>& O);
}
