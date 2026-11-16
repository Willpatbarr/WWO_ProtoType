#pragma once

// The firing tester: a bench for how a gun looks and kicks. Opened from the launch screen's
// "Firing tester" choice (UWWONetSubsystem::PlayTester -> AWWOGameMode::bTester): the Test Range
// with one target dummy and no enemies.
// - Left: the gun and config in hand, and the sliders that shape its motion (the same values the
//   Settings menu holds, so "Save as defaults" keeps them).
// - Right: the player's own view, the real game drawn in a 16:9 window (the local player's view
//   rectangle is shrunk to it while this is up).
// - Under the window: TAKE CONTROL hands you the player (mouse and keys or controller); the
//   Settings key / button (Tab, Esc, Options) brings the cursor back. ADS, ADS + FIRE, HIP FIRE and
//   STRAFE play that action without you taking control (AWWOPlayerController's scripted input).

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WWOFiringTester.generated.h"

class UWWOSettingsMenu;
class UScrollBox;
class UTextBlock;
class UButton;
class UBorder;
class UCanvasPanel;

UCLASS()
class WHATWASOURS_API UWWOFiringTester : public UUserWidget
{
	GENERATED_BODY()

public:
	enum class EAct : uint8 { None, Ads, AdsFire, HipFire };

	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	bool IsOpen() const { return bOpen; }
	bool HasControl() const { return bControl; }
	bool WantsCursor() const { return bOpen && !bControl; } // the pane has the cursor: your keys work it, not the player
	void Open();
	void SetControl(bool bOn);
	void Play(EAct What);       // one of the scripted actions, from the start
	void SetStrafe(bool bOn);
	bool IsStrafing() const { return bStrafe; }
	void ResetPlayer();         // back to the start, facing the dummy
	EAct Playing() const { return Act; }

	// The window: where the player's view is drawn, as fractions of the screen.
	static constexpr float WinX = 0.355f;
	static constexpr float WinY = 0.05f;
	static constexpr float WinW = 0.63f;
	static constexpr float WinH = 0.63f;
	static constexpr float PaneW = 0.34f;

private:
	bool bOpen = false, bControl = false, bBuilt = false, bStrafe = false;
	EAct Act = EAct::None;
	float ActClock = 0.f, NextPull = 0.f, StrafeClock = 0.f;
	FName ShownWeapon;
	int32 ShownConfig = -2;
	FDelegateHandle LoadoutHandle;
	UPROPERTY() TObjectPtr<UScrollBox> Rows;
	UPROPERTY() TObjectPtr<UTextBlock> WeaponLabel;
	UPROPERTY() TObjectPtr<UTextBlock> ConfigLabel;
	UPROPERTY() TObjectPtr<UTextBlock> HintLabel;
	UPROPERTY() TObjectPtr<UTextBlock> StrafeLabel;
	UPROPERTY() TObjectPtr<UTextBlock> ControlLabel;
	UPROPERTY() TArray<TObjectPtr<UObject>> Handlers; // the click handlers of the pane's buttons (built once)

	UWWOSettingsMenu* Menu() const; // the player's real settings menu: its slider rows (MakeRowFor) and SaveDefaults are used here
	void Build();
	void BuildRows();
	void SetViewRect(bool bWindow) const;
	void UpdateScript(float DeltaTime);
	void StopScript();
	void StepWeapon(int32 Step);
	void StepConfig(int32 Step);
};
