#pragma once

// Settings menu (Godot: settings_menu.gd): two panes side by side.
// - Global (left): movement, camera, jump, crouch/slide, melee, grenades, player, targets,
//   ult, enemies, maps, waves, allies, aim assist. Q/E or L1/R1 cycle its tabs.
// - Specific (right): Equipped, Flora (Grenades / Abilities / Traits), Weapons, Frame Parts.
//   Pick an item to open its sliders; Back (or circle / Esc) returns to the list.
// Every slider binds to a float UPROPERTY by name through reflection. "Save as defaults"
// writes every section to Saved/WWO/SavedSettings.json; the game loads it on startup and
// Reset goes back to it. Tab / Esc / Options opens it; it pauses the game.

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WWOSettingsMenu.generated.h"

class UVerticalBox;
class UScrollBox;
class UWidgetSwitcher;
class UTextBlock;
class USlider;
class UButton;
class UWrapBox;
class UPanelWidget;
class AWWOCharacter;

// Small UObjects that forward UMG's dynamic delegates to C++ lambdas.
UCLASS()
class UWWOClickHandler : public UObject
{
	GENERATED_BODY()
public:
	TFunction<void()> Fn;
	UFUNCTION() void Handle() { if (Fn) Fn(); }
};

UCLASS()
class UWWOSliderHandler : public UObject
{
	GENERATED_BODY()
public:
	TFunction<void(float)> Fn;
	UFUNCTION() void Handle(float Value) { if (Fn) Fn(Value); }
};

struct FWWOSettingRow
{
	FString Header; // set = a header line instead of a slider
	FName Id;
	FString Label;
	float Min = 0.f, Max = 1.f, Step = 0.1f;
	TFunction<float()> Get;
	TFunction<void(float)> Set;
	TFunction<FString(float)> Show;
};

struct FWWOMenuButton
{
	FString Text;
	TFunction<void()> Fn;
};

struct FWWOSettingSection
{
	FString Key;
	FString Title;
	TArray<FWWOSettingRow> Rows;
	TArray<FWWOMenuButton> Buttons;
};

struct FWWOBrowserItem
{
	FString Key, Title, Subtitle;
};

struct FWWOBrowser
{
	TFunction<TArray<FWWOBrowserItem>()> Provider;
	UWidgetSwitcher* Switcher = nullptr; // 0 = list, 1 = item
	UScrollBox* List = nullptr;
	UTextBlock* Title = nullptr;
	UScrollBox* Rows = nullptr;
	FString OpenKey;
};

UCLASS()
class WHATWASOURS_API UWWOSettingsMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;

	bool IsOpen() const { return bOpen; }
	bool IsPrepared() const { return bBuilt; }
	void Prepare();
	void Open();
	void Close();
	void Back();
	void CycleGlobalTab(int32 Step);

	float GetValue(const FString& Key, FName Id) const;
	void SetValue(const FString& Key, FName Id, float Value);
	void ResetSection(const FString& Key);
	void ResetAll();
	bool SaveDefaults();

	// Shared styling helpers (the character menu uses them too).
	static FSlateFontInfo Font(float Px, bool bBold = false);
	static FSlateBrush RoundBrush(const FLinearColor& Color, float Radius = 8.f, const FLinearColor& Outline = FLinearColor(0, 0, 0, 0), float OutlineWidth = 0.f);
	UButton* MakeButton(UObject* Outer, const FString& Text, TFunction<void()> Fn, float FontPx = 22.f, const FLinearColor& Color = FLinearColor(0.2f, 0.21f, 0.25f),
		bool bLeftAlign = false, float MinHeight = 0.f);
	UTextBlock* MakeText(const FString& Text, float FontPx, const FLinearColor& Color = FLinearColor::White, bool bBold = false, bool bWrap = false);

	static constexpr float K = 1.2f; // Godot's 1600 x 900 sizes -> Unreal's 1080p reference

private:
	bool bOpen = false;
	bool bBuilt = false;
	TArray<FWWOSettingSection> Sections;
	TArray<FString> GlobalKeys;
	TMap<FString, TMap<FName, float>> Defaults;
	struct FLiveSlider { TWeakObjectPtr<USlider> Slider; TWeakObjectPtr<UTextBlock> Value; FString Key; int32 Row; };
	TArray<FLiveSlider> Live;
	TArray<FWWOBrowser> Browsers;
	UPROPERTY() TArray<TObjectPtr<UObject>> Handlers;
	UPROPERTY() TObjectPtr<UWidgetSwitcher> GlobalSwitcher;
	UPROPERTY() TArray<TObjectPtr<UButton>> GlobalTabButtons;
	UPROPERTY() TObjectPtr<UButton> SaveButton;
	int32 GlobalTab = 0;

	void Build();
	void RegisterSections(AWWOCharacter* P);
	void RecordDefaults();
	void LoadSaved();
	FWWOSettingSection* Find(const FString& Key);
	const FWWOSettingSection* Find(const FString& Key) const;
	const FWWOSettingRow* RowFor(const FString& Key, FName Id) const;
	void BuildRows(UPanelWidget* Container, const FString& Key);
	UWidget* MakeRow(const FString& Key, int32 RowIndex);
	void SyncSliders();
	void SelectGlobalTab(int32 Index);
	UWidget* MakeBrowser(TFunction<TArray<FWWOBrowserItem>()> Provider);
	void ShowList(int32 BrowserIndex);
	void OpenItem(int32 BrowserIndex, const FString& Key);
	UWidget* MakeTabs(const TArray<FString>& Titles, UWidgetSwitcher* Switcher, TArray<TObjectPtr<UButton>>* OutButtons, float FontPx);
	static FString ValueText(const FWWOSettingRow& Row, float V);
};
