#pragma once

// The scripted test runs (-WWOAutoTest and its narrower switches, -WWOPerfTest, a joiner's -WWOStress
// log): all their state and code, off the player controller. See docs/TESTING.md.
// - AWWOPlayerController::BeginPlay makes one (MakeIfAsked) only when the command line asks for a
//   test; with no such switch there is no driver and the controller runs nothing from here.
// - The controller calls Tick once a frame from its own Tick (which runs while paused, unlike
//   PlayerTick, where JustPressed is cleared: tests set input from here).
// - It is a friend of the controller: tests write its input arrays and open its menus directly (PC->).
// - The driver lives exactly as long as its controller, so state that must survive a map load is
//   file-scope static in the test's own .cpp.
// - Each test's steps are in their own file: Player/WWOAutoTest<Name>.cpp, WWOPerfTest.cpp.

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "WWOTypes.h"

class AWWOPlayerController;
class AWWOCharacter;
class AActor;

// Which test the command line asks for, read once when the controller begins play.
struct FWWOAutoTestModes
{
	bool bAutoTest = false;           // -WWOAutoTest: the full scripted play-through, or one of the parts below
	bool bAutoTestMenusOnly = false;  // -WWOAutoTestMenus: just the controller-only menu steps
	bool bAutoTestBodyOnly = false;   // -WWOAutoTestBody: just the body animation steps
	bool bAutoTestGraftsOnly = false; // -WWOAutoTestGrafts: just the flora graft steps
	bool bAutoTestMapsOnly = false;   // -WWOAutoTestMaps: just the map views
	bool bAutoTestModsOnly = false;   // -WWOAutoTestMods (or Guns, Hidden, Frames): just the weapon mod steps
	bool bAutoTestGunsOnly = false;   // -WWOAutoTestGuns: gun model screenshots (in the mods test)
	bool bAutoTestHiddenOnly = false; // -WWOAutoTestHidden: the hidden aim assist, off then on (in the mods test)
	bool bAutoTestTitle = false;      // -WWOAutoTestTitle / TitleJoin / TitleJoinFail
	bool bAutoTestTabs = false;       // -WWOAutoTestTabs / Vignette / Pose
	bool bAutoTestTester = false;     // -WWOAutoTestTester
	bool bAutoTestNet = false;        // -WWOAutoTestNet
	bool bAutoTestPvP = false;        // -WWOAutoTestPvP
	bool bAutoTestFootRace = false;   // -WWOAutoTestFootRace
	bool bPerfTest = false;           // -WWOPerfTest
	bool bJoinerStress = false;       // -WWOStress on a joiner: only logs its frame times
	bool bRegress = false;            // -WWORegress: the regression scenarios (Player/WWORegress.h)

	static FWWOAutoTestModes Read(const AWWOPlayerController& Controller);
	bool Any() const;
};

class FWWOAutoTest : public FGCObject, private FWWOAutoTestModes
{
public:
	// The driver for this command line, or none when it asks for no test. Also starts the FAIL count
	// and the loadout guard of a -WWOAutoTest... run.
	static TSharedPtr<FWWOAutoTest> MakeIfAsked(AWWOPlayerController& Controller);
	FWWOAutoTest(AWWOPlayerController& Controller, const FWWOAutoTestModes& Modes);

	void Tick(float DeltaTime); // real seconds: the one update for the mode the switches picked

	// The actors a test spawned and holds (as the controller's UPROPERTYs did).
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FWWOAutoTest"); }

private:
	AWWOPlayerController* PC = nullptr; // the controller this drives (it owns the driver)
	TSharedPtr<class FWWORegress> Regress; // -WWORegress: the scenario runner (its own file)

	// -WWOAutoTest: a scripted play-through that saves screenshots (for checking the port).
	// Map steps (WWOAutoTestMaps.cpp): the level from fixed cameras.
	int32 AutoMapIndex = -1;
	float AutoMapClock = 0.f;
	TObjectPtr<class ACameraActor> AutoMapCamera = nullptr; // referenced: AddReferencedObjects
	bool UpdateAutoMapTest(class AWWOCharacter* P, float DeltaTime);
	// Weapon mod steps (WWOAutoTestMods.cpp): draft mods, configs, perks.
	TWeakObjectPtr<AActor> AutoHiddenTarget;
	float AutoHiddenOff = 0.f;
	int32 AutoGunIdx = 0;
	int32 AutoGunStage = 0;
	FVector2D AutoGunKick = FVector2D::ZeroVector; // the hand cannon's kick, put back after the diagonal shot
	int32 AutoModStep = 0;
	float AutoModClock = 0.f;
	int32 AutoModConfig = INDEX_NONE;
	TArray<float> AutoModBase;
	float AutoModBaseReload = 0.f, AutoModBaseSwap = 0.f;
	bool UpdateAutoModTest(class AWWOCharacter* P, float DeltaTime);
	// -WWOAutoTestTitle / -WWOAutoTestTitleJoin (WWOAutoTestTitle.cpp): the launch screen's
	// flow, across the map loads it causes.
	void UpdateAutoTitleTest(float DeltaTime);
	// -WWOAutoTestTabs (WWOAutoTestTabs.cpp): the Weapon and Shell tabs fit on one screen.
	void UpdateAutoTabsTest(float DeltaTime);
	void UpdateAutoTesterTest(float DeltaTime); // -WWOAutoTestTester: the firing tester
	// -WWOAutoTestFootRace (WWOAutoTestFootRace.cpp): FootRace's breach portals, alone on L_FootRaceTest.
	float AutoFootRaceClock = 0.f;
	FVector AutoFootRaceBefore = FVector::ZeroVector;
	TWeakObjectPtr<class AWWOEnemy> AutoFootRaceEnemy;
	void UpdateAutoFootRaceTest(float DeltaTime);
	// -WWOAutoTestNet (WWOAutoTestNet.cpp): online play, run on the host and the joiner.
	float AutoNetWait = 0.f;   // seconds since start
	float AutoNetClock = 0.f; // seconds since both players were in and settled (negative while settling)
	float AutoNetNextShot = 0.f;
	int32 AutoNetShots = 0;
	int32 AutoNetStep = -1;
	int32 AutoNetTotalCorrections = 0;
	bool bAutoNetSawOther = false, bAutoNetMantleJumped = false, bAutoNetSawMantle = false;
	FVector AutoNetCrate = FVector::ZeroVector;
	FBox AutoNetCrateBox = FBox(ForceInit);
	int32 AutoNetDamageEvents = 0;
	bool bAutoNetOwnBodyShown = false;
	TWeakObjectPtr<AActor> AutoNetShootTarget; // the dummy this side shoots at
	TWeakObjectPtr<AActor> AutoNetMeleeTarget; // joiner: the dummy it punches
	float AutoNetMeleeHealth = 0.f;
	float AutoNetReviveSeen = 0.f;
	FTransform AutoNetJoinerStart = FTransform(FVector::ZeroVector); // host: where the joiner began
	TWeakObjectPtr<AActor> AutoNetGrenadeTarget; // joiner: the dummy it threw at
	float AutoNetGrenadeHealth = 0.f; // joiner: the most of the host's revive bar it saw
	float AutoNetShootHealth = 0.f;
	int32 AutoNetShotPulls = 0; // joiner: OnDamaged fired from the host's replicated health
	bool bAutoNetBound = false;
	// -WWOAutoTestPvP (WWOAutoTestPvP.cpp): team deathmatch, host (-WWOHostPvP) and joiner.
	float AutoPvPHostHealth = 0.f;
	int32 AutoPvPRejected = 0;
	void UpdateAutoPvPTest(float DeltaTime);
	float StressClock = 0.f, StressFrames = 0.f, StressWorst = 0.f, StressNext = 5.f;
	int32 StressCorrections = 0;
	void LogJoinerStress(float DeltaTime); // -WWOStress on a joiner
	void UpdateAutoNetTest(float DeltaTime);
	// -WWOPerfTest (WWOPerfTest.cpp): frame and GPU time at each map view, logged, then quit.
	bool bPerfProfiled = false;
	float PerfStartClock = 0.f;
	bool UpdatePerfSweep(class AWWOCharacter* P, float DeltaTime); // -WWOPerfSweep: true while it is running
	TArray<FVector2D> PerfSums; // per view: (frame ms, GPU ms) summed
	TArray<int32> PerfFrames;
	void UpdatePerfTest(class AWWOCharacter* P, float DeltaTime);
	// Graft steps (WWOAutoTestGrafts.cpp): loadouts shown in the character preview.
	float AutoGraftClock = 0.f;
	float AutoGraftCaptureFov = 0.f;
	TObjectPtr<class UWWOFloraData> AutoGraftFlora = nullptr;
	TObjectPtr<class UWWOGrenadeData> AutoGraftGrenade = nullptr;
	TArray<TObjectPtr<class UWWOAbilityData>> AutoGraftAbilities;
	TArray<TObjectPtr<class UWWOFramePartData>> AutoGraftFrame;
	bool UpdateAutoGraftTest(class AWWOCharacter* P, float DeltaTime);
	// Body steps (WWOAutoTestBody.cpp): a row of mannequins in each body state.
	int32 AutoBodyStep = 0;
	float AutoBodyClock = 0.f;
	FVector AutoBodyOrigin = FVector::ZeroVector;
	TArray<TObjectPtr<AActor>> AutoBodies; // referenced, with the next two
	TArray<TObjectPtr<AActor>> AutoBodyProps;
	TObjectPtr<class ACameraActor> AutoBodyCamera = nullptr;
	bool UpdateAutoBodyTest(class AWWOCharacter* P, float DeltaTime);
	// Menu steps (WWOAutoTestMenu.cpp): the menus driven by injected DualSense input.
	int32 AutoMenuStep = 0;
	float AutoMenuClock = 0.f;
	int32 AutoMenuFails = 0;
	float AutoMenuValue = 0.f;
	bool UpdateAutoMenuTest(class AWWOCharacter* P, float DeltaTime);
	float AutoTestClock = 0.f;
	int32 AutoTestStep = 0;
	FVector AutoSlideFrom = FVector::ZeroVector; // ground movement steps: where the slide started
	bool bAutoTrackSlide = false;
	// Jump steps: one jump's arc (feet height above takeoff), then the landing dip.
	bool bAutoTrackJump = false;
	bool bAutoJumpLanded = false;
	bool bAutoSawMantle = false;
	float AutoJumpTime = 0.f, AutoJumpFromZ = 0.f, AutoJumpPeak = 0.f, AutoJumpPeakTime = 0.f, AutoJumpLandTime = 0.f, AutoJumpDip = 0.f;
	FVector AutoCrateAt = FVector::ZeroVector;
	float AutoLookYaw = 0.f, AutoLookPitch = 0.f, AutoLookFrom = 0.f; // stick look step
	float AutoGunPitch = 0.f; // gun steps
	int32 AutoGunAmmo = 0;
	double AutoGunTime = 0.0;
	// Grenade steps: each grenade kind thrown at a training target in turn (the wall last).
	TArray<TObjectPtr<class UWWOGrenadeData>> AutoGrenades;
	TObjectPtr<class UWWOGrenadeData> AutoGrenadeBefore = nullptr; // equipped again afterwards
	TWeakObjectPtr<AActor> AutoTarget; // the training target
	TWeakObjectPtr<AActor> AutoGrenadeTarget; // this grenade's (an enemy for blind and slow)
	int32 AutoGrenadeIndex = -1;
	float AutoPhase = 0.f;    // seconds into this grenade (or melee) step
	float AutoGoneAt = -1.f;  // when the thrown grenade went off (-1: still out)
	float AutoTargetHealth = 0.f;
	bool bAutoGrenadeOut = false, bAutoShotBlast = false, bAutoShotArmed = false, bAutoLongThrow = false;
	// Melee steps: a lunge onto the training target, then a punch on the nearest enemy.
	int32 AutoMeleeIndex = -1;
	FVector AutoMeleeFrom = FVector::ZeroVector;
	float AutoLungeTime = 0.f;
	bool bAutoSawLunge = false, bAutoMeleeLogged = false, bAutoShotPunch = false;
	// Ability and ult steps (WWOAutoTestAbility.cpp): every ability equipped and
	// set off in turn, then each flora's ult; the loadout is put back afterwards.
	TArray<TObjectPtr<class UWWOAbilityData>> AutoAbilities;
	TArray<TObjectPtr<class UWWOFloraData>> AutoFloras;
	TObjectPtr<class UWWOFloraData> AutoFloraBefore = nullptr;
	TObjectPtr<class UWWOGrenadeData> AutoAbilityGrenadeBefore = nullptr;
	TArray<TObjectPtr<class UWWOAbilityData>> AutoAbilitiesBefore;
	TArray<TObjectPtr<class UWWOTraitData>> AutoTraitsBefore;
	TMap<TWeakObjectPtr<AActor>, float> AutoHealths; // living targets' health before a hit
	TWeakObjectPtr<AActor> AutoAbilityTarget;
	int32 AutoAbilityIndex = -1;
	float AutoAbilityPhase = -1.f;
	float AutoAbilityValue = 0.f;
	FVector AutoAbilityFrom = FVector::ZeroVector;
	bool bAutoAbilityShot = false, bAutoAbilityFlag = false;
	bool UpdateAutoAbilityTest(class AWWOCharacter* P, float DeltaTime);
	bool UpdateAutoGrenadeTest(class AWWOCharacter* P, float DeltaTime);
	bool UpdateAutoMeleeTest(class AWWOCharacter* P, float DeltaTime);
	bool PlaceForAutoTest(class AWWOCharacter* P, AActor* Target, float DistanceM, float YawOffset, bool bMove = true);
	void UpdateAutoTest(float DeltaTime);
	void Shot(const TCHAR* Name);
};
