#include "UI/WWOSettingsMenu.h"
#include "Systems/WWOConfig.h"
#include "WWOSwitches.h"
#include "Dom/JsonObject.h"

namespace
{
	FString SavePath() { return WWOConfig::SavedPath(TEXT("SavedSettings.json")); }

}

// --- Values, defaults, saving ----------------------------------------------------------------

FWWOSettingSection* UWWOSettingsMenu::Find(const FString& Key)
{
	return Sections.FindByPredicate([&Key](const FWWOSettingSection& S) { return S.Key == Key; });
}

const FWWOSettingSection* UWWOSettingsMenu::Find(const FString& Key) const
{
	return Sections.FindByPredicate([&Key](const FWWOSettingSection& S) { return S.Key == Key; });
}

const FWWOSettingRow* UWWOSettingsMenu::RowFor(const FString& Key, FName Id) const
{
	const FWWOSettingSection* S = Find(Key);
	return S ? S->Rows.FindByPredicate([Id](const FWWOSettingRow& R) { return R.Header.IsEmpty() && R.Id == Id; }) : nullptr;
}

float UWWOSettingsMenu::GetValue(const FString& Key, FName Id) const
{
	const FWWOSettingRow* R = RowFor(Key, Id);
	return R ? R->Get() : 0.f;
}

void UWWOSettingsMenu::SetValue(const FString& Key, FName Id, float Value)
{
	if (const FWWOSettingRow* R = RowFor(Key, Id))
	{
		R->Set(Value);
		SyncSliders();
	}
}

void UWWOSettingsMenu::RecordDefaults()
{
	for (const FWWOSettingSection& S : Sections)
	{
		TMap<FName, float>& Values = Defaults.FindOrAdd(S.Key);
		for (const FWWOSettingRow& R : S.Rows)
		{
			if (R.Header.IsEmpty()) Values.Add(R.Id, R.Get());
		}
	}
}

void UWWOSettingsMenu::ResetSection(const FString& Key)
{
	if (const TMap<FName, float>* Values = Defaults.Find(Key))
	{
		for (const auto& Pair : *Values)
		{
			if (const FWWOSettingRow* R = RowFor(Key, Pair.Key); R && Allowed(*R)) R->Set(Pair.Value);
		}
	}
	SyncSliders();
}

void UWWOSettingsMenu::ResetAll()
{
	for (const FWWOSettingSection& S : Sections) ResetSection(S.Key);
}

bool UWWOSettingsMenu::SaveDefaults()
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	if (bRestricted)
	{
		// In someone else's game only your own preferences are saved, into the file as it is: the
		// tuning in memory may be the host's (movement in PvP), and it isn't yours to keep. With no
		// file of your own yet, it starts as the build's shipped values (LoadSaved stops falling back
		// to those once your file exists).
		if (const TSharedPtr<FJsonObject> Parsed = WWOConfig::LoadSavedElseShipped(SavePath(), TEXT("ShippedSettings.json")); Parsed.IsValid()) Root = Parsed;
	}
	for (const FWWOSettingSection& S : Sections)
	{
		if (!SectionAllowed(S)) continue;
		const TSharedPtr<FJsonObject>* Existing = nullptr;
		TSharedRef<FJsonObject> Values = bRestricted && Root->TryGetObjectField(S.Key, Existing) ? (*Existing).ToSharedRef() : MakeShared<FJsonObject>();
		TMap<FName, float>& Recorded = Defaults.FindOrAdd(S.Key);
		for (const FWWOSettingRow& R : S.Rows)
		{
			if (!R.Header.IsEmpty() || !Allowed(R)) continue;
			const float V = R.Get();
			Values->SetNumberField(R.Id.ToString(), V);
			Recorded.Add(R.Id, V); // Reset now returns here
		}
		Root->SetObjectField(S.Key, Values);
	}
	return WWOConfig::Save(SavePath(), Root.ToSharedRef());
}

void UWWOSettingsMenu::LoadSaved()
{
	// Your saved values, or in a fresh packaged build the ones it shipped with (Config/ShippedSettings.json).
	bool bMine = false;
	// The regression scenarios (-WWORegress) start from the shipped values whatever this machine saved.
	const TSharedPtr<FJsonObject> Root = WWOSwitches::IsRegress() ? WWOConfig::Load(WWOConfig::Path(TEXT("ShippedSettings.json")))
		: WWOConfig::LoadSavedElseShipped(SavePath(), TEXT("ShippedSettings.json"), &bMine);
	if (!Root.IsValid()) return;
	for (const auto& SectionPair : Root->Values)
	{
		const TSharedPtr<FJsonObject>* Values = nullptr;
		if (!SectionPair.Value->TryGetObject(Values)) continue;
		const FString SectionKey(*SectionPair.Key);
		for (const auto& ValuePair : (*Values)->Values)
		{
			FName Id(*ValuePair.Key);
			// "Gun kick" was split into vertical and horizontal: an old save's value becomes the vertical one.
			if (Id == "GunKick" && !(*Values)->HasField(TEXT("VerticalKick"))) Id = "VerticalKick";
			if (const FWWOSettingRow* R = RowFor(SectionKey, Id))
			{
				const float V = (float)ValuePair.Value->AsNumber();
				// Target health resets every target when set: in someone else's game they are the host's to reset.
				if (bRestricted && Id == "TargetHealth") continue;
				// God mode never ships on, whatever the packaging machine had saved.
				if (!bMine && Id == "GodMode") continue;
				R->Set(V);
				Defaults.FindOrAdd(SectionKey).Add(Id, V);
			}
		}
	}
}
