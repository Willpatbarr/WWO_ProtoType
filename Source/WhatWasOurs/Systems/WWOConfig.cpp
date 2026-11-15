#include "Systems/WWOConfig.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FString WWOConfig::Path(const TCHAR* Name)
{
	return FPaths::ProjectConfigDir() / Name;
}

FString WWOConfig::SavedPath(const TCHAR* Name)
{
	return FPaths::ProjectSavedDir() / TEXT("WWO") / Name;
}

TSharedPtr<FJsonObject> WWOConfig::Load(const FString& FilePath, bool* bOutFileRead)
{
	FString Text;
	const bool bRead = FFileHelper::LoadFileToString(Text, *FilePath);
	if (bOutFileRead) *bOutFileRead = bRead;
	TSharedPtr<FJsonObject> Json;
	if (!bRead || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()) return nullptr;
	return Json;
}

TSharedPtr<FJsonObject> WWOConfig::LoadCached(const TCHAR* Name)
{
	static TMap<FString, TSharedPtr<FJsonObject>> Cache;
	const FString Key(Name);
	if (const TSharedPtr<FJsonObject>* Found = Cache.Find(Key)) return *Found;
	return Cache.Add(Key, Load(Path(Name)));
}

TSharedPtr<FJsonObject> WWOConfig::LoadSavedElseShipped(const FString& SavedFilePath, const TCHAR* ShippedName, bool* bOutMine)
{
	bool bMine = false;
	TSharedPtr<FJsonObject> Json = Load(SavedFilePath, &bMine);
	if (bOutMine) *bOutMine = bMine;
	return bMine ? Json : Load(Path(ShippedName));
}

bool WWOConfig::Save(const FString& FilePath, const TSharedRef<FJsonObject>& Json)
{
	FString Text;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
	return FFileHelper::SaveStringToFile(Text, *FilePath);
}

FVector WWOConfig::Vec(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, const FVector& Default)
{
	const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
	if (O.IsValid() && O->TryGetArrayField(Field, A) && A->Num() >= 3) return FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
	if (O.IsValid() && O->TryGetArrayField(Field, A) && A->Num() == 2) return FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), 0.0);
	double N = 0.0;
	if (O.IsValid() && O->TryGetNumberField(Field, N)) return FVector(N);
	return Default;
}

FLinearColor WWOConfig::Color(const TSharedPtr<FJsonObject>& O)
{
	FString Name;
	if (O.IsValid() && O->TryGetStringField(TEXT("color"), Name))
	{
		static const TMap<FString, FLinearColor> Named = {
			{ TEXT("dark"), FLinearColor(0.12f, 0.12f, 0.13f) }, { TEXT("steel"), FLinearColor(0.22f, 0.24f, 0.28f) },
			{ TEXT("orange"), FLinearColor(0.95f, 0.5f, 0.1f) }, { TEXT("gold"), FLinearColor(0.78f, 0.6f, 0.25f) },
			{ TEXT("olive"), FLinearColor(0.22f, 0.27f, 0.2f) }, { TEXT("white"), FLinearColor(0.95f, 0.95f, 0.95f) },
			{ TEXT("cyan"), FLinearColor(0.2f, 0.8f, 1.f) }, { TEXT("green"), FLinearColor(0.3f, 1.f, 0.4f) },
			{ TEXT("red"), FLinearColor(1.f, 0.2f, 0.15f) }, { TEXT("yellow"), FLinearColor(1.f, 0.8f, 0.2f) },
			{ TEXT("purple"), FLinearColor(0.6f, 0.3f, 1.f) },
		};
		if (const FLinearColor* C = Named.Find(Name.ToLower())) return *C;
	}
	const FVector V = Vec(O, TEXT("color"), FVector(0.22, 0.24, 0.28));
	return FLinearColor(V.X, V.Y, V.Z);
}
