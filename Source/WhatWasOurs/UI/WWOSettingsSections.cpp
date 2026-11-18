#include "UI/WWOSettingsMenu.h"
#include "UI/WWOSettingsMenuPrivate.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOAimAssistComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOGrenadeComponent.h"
#include "Player/WWOUltComponent.h"
#include "Game/WWOGameMode.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Data/WWOData.h"
#include "Net/WWONetSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"
#include "Engine/World.h"
#include "WWOIds.h"

using namespace WWOSettingsUI;

namespace
{
	FWWOSettingRow Header(const FString& Text)
	{
		FWWOSettingRow R;
		R.Header = Text;
		return R;
	}

	// A slider bound to a float UPROPERTY by name (the Godot rows bound by property name too).
	FWWOSettingRow Row(UObject* Obj, FName Prop, const FString& Label, float Min, float Max, float Step, TFunction<FString(float)> Show = nullptr)
	{
		FWWOSettingRow R;
		R.Id = Prop;
		R.Label = Label;
		R.Min = Min;
		R.Max = Max;
		R.Step = Step;
		R.Show = Show;
		FFloatProperty* Property = Obj ? FindFProperty<FFloatProperty>(Obj->GetClass(), Prop) : nullptr;
		if (!Property)
		{
			R.Label += TEXT(" (missing)");
			R.Get = [] { return 0.f; };
			R.Set = [](float) {};
			return R;
		}
		TWeakObjectPtr<UObject> Weak = Obj;
		R.Get = [Weak, Property] { return Weak.IsValid() ? Property->GetPropertyValue_InContainer(Weak.Get()) : 0.f; };
		R.Set = [Weak, Property](float V) { if (Weak.IsValid()) Property->SetPropertyValue_InContainer(Weak.Get(), V); };
		return R;
	}

	// Which settings are a player's own, by page: the rows named, or with none named the whole page.
	// Everything else is game tuning (movement, damage, enemies, items...), the host's online.
	const TMap<FString, TArray<FName>>& OwnSettings()
	{
		static const TMap<FString, TArray<FName>> Own = {
			{ TEXT("Camera"), { "HipFov", "ReticleDrop", "AdsDotSize", "AdsGunScale", "AdsLevel", "MouseSensitivity", "StickSensitivity", "TriggerReset", "FireBufferTime", "RumbleScale", "PunchScale", "BlastShake", "HitFlinch", "BobIntensity", "AdsBob" } },
			{ TEXT("Audio"), {} },
			{ TEXT("HUD"), {} },
		};
		return Own;
	}

	// The enemy, ally and target pages' button.
	FWWOMenuButton ResetTargetsButton(UWWOWorldSubsystem* WS)
	{
		TWeakObjectPtr<UWWOWorldSubsystem> WeakWS = WS;
		return { TEXT("Reset targets and enemies"), [WeakWS] { if (WeakWS.IsValid()) WeakWS->ResetTargets(); } };
	}

	TFunction<FString(float)> Percent() { return [](float V) { return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(V * 100.f)); }; }
	TFunction<FString(float)> Times(int32 Digits = 2) { return [Digits](float V) { return Digits == 1 ? FString::Printf(TEXT("x%.1f"), V) : FString::Printf(TEXT("x%.2f"), V); }; }
	TFunction<FString(float)> SignedPercent() { return [](float V) { return FString::Printf(TEXT("%+d%%"), FMath::RoundToInt(V * 100.f)); }; }

}

// --- What's in the menu -----------------------------------------------------------------------

void UWWOSettingsMenu::RegisterSections(AWWOCharacter* P)
{
	// The order sections are registered in is the order of the pages, of the search results and of
	// the saved file's sections: a new section goes where it should show, an old one stays put.
	// (Which tab a page sits under is Prepare's TabLayout.)
	RegisterMovementAndViewSections(P); // Walk / Sprint, Camera, Audio, HUD, Jump, Crouch / Slide
	RegisterCombatSections(P);          // Melee, Grenades, Player, Targets, Ult
	RegisterEnemySections(P);           // Soldier, Swarm, Tank
	RegisterMatchSections(P);           // Maps, Multiplayer, PvP, FootRace, Bots, Waves, Allies
	RegisterAimAssistSection(P);        // Aim Assist
	// Specific sections: one per item.
	RegisterWeaponSections(P);
	RegisterShellSections(P);
	RegisterAbilitySections(P);
	RegisterTraitSections(P);
	RegisterGrenadeSections(P);
}

void UWWOSettingsMenu::AddGlobal(const FString& Title, TArray<FWWOSettingRow> Rows, TArray<FWWOMenuButton> Buttons)
{
	FWWOSettingSection S;
	S.Key = TEXT("global/") + Title;
	S.Title = Title;
	S.Rows = MoveTemp(Rows);
	S.Buttons = MoveTemp(Buttons);
	GlobalKeys.Add(S.Key);
	Sections.Add(MoveTemp(S));
}

void UWWOSettingsMenu::RegisterMovementAndViewSections(AWWOCharacter* P)
{
	UWWOWeaponComponent* G = P->Weapon;
	AddGlobal(TEXT("Walk / Sprint"), {
		Row(P, "WalkSpeed", TEXT("Walk speed"), 1.f, 15.f, 0.5f),
		Row(P, "SprintSpeed", TEXT("Sprint speed"), 1.f, 25.f, 0.5f),
	});
	AddGlobal(TEXT("Camera"), {
		Row(G, "HipFov", TEXT("Field of view (horizontal)"), 55.f, 120.f, 1.f),
		Row(P, "ReticleDrop", TEXT("Reticle below center (fraction of screen height)"), 0.f, 0.2f, 0.005f),
		Row(G, "AdsDotSize", TEXT("Aimed-in dot size (px)"), 0.5f, 6.f, 0.1f),
		Row(G, "AdsGunScale", TEXT("Gun and sight size when aimed, all weapons (1 = same as at the hip)"), 0.3f, 3.f, 0.05f),
		Row(G, "AdsLevel", TEXT("Auto-level the gun's top when aimed (only guns whose top sits below the sight; per gun: ADS gun angle)"), 0.f, 1.f, 0.05f),
		Row(P, "MouseSensitivity", TEXT("Mouse sensitivity"), 0.1f, 10.f, 0.1f),
		Row(P, "StickSensitivity", TEXT("Stick sensitivity"), 0.5f, 10.f, 0.1f),
		Row(P, "TriggerReset", TEXT("Trigger: how far it must come back up to fire again (share of its travel)"), 0.03f, 0.6f, 0.01f),
		Row(P, "FireBufferTime", TEXT("Semi-auto: a pull this early still fires (s; 0 = off)"), 0.f, 0.5f, 0.01f),
		Row(G, "RumbleScale", TEXT("Controller rumble strength (0 = off)"), 0.f, 2.f, 0.05f),
		Row(G, "PunchScale", TEXT("Camera punch strength (0 = off)"), 0.f, 2.f, 0.05f),
		Row(G, "BlastShake", TEXT("Shake from explosions near you (0 = off)"), 0.f, 2.f, 0.05f),
		Row(G, "HitFlinch", TEXT("Jolt when you take damage (0 = off; never moves your aim)"), 0.f, 2.f, 0.05f),
		Row(P, "BobIntensity", TEXT("Head bob intensity"), 0.f, 3.f, 0.1f),
		Row(P, "AdsBob", TEXT("Head bob left when aimed in (0-1)"), 0.f, 1.f, 0.05f),
		Row(P, "WalkStride", TEXT("Walk stride length (m)"), 0.5f, 5.f, 0.1f),
		Row(P, "SprintStride", TEXT("Sprint stride length (m)"), 0.5f, 6.f, 0.1f),
		Row(P, "LandingDip", TEXT("Landing dip"), 0.f, 3.f, 0.1f),
	});
	AddGlobal(TEXT("Audio"), {
		Row(P, "SoundVolume", TEXT("Sound effects volume (0 = silent)"), 0.f, 1.5f, 0.05f),
	});
	AddGlobal(TEXT("HUD"), {
		Header(TEXT("Enemy readability")),
		Row(P, "TargetHighlight", TEXT("Tint on the enemy under the reticle (0 = off)"), 0.f, 0.6f, 0.01f),
		Row(P, "HealthBarRadius", TEXT("Show health bars of enemies this near the reticle (px; 0 = only the one under it)"), 0.f, 500.f, 5.f),
		Row(P, "HealthBarAdsScale", TEXT("Marker range while aiming in (times the range above)"), 1.f, 4.f, 0.05f),
		Row(P, "DamageNumberOffset", TEXT("Damage numbers this far to the side of the hit (px)"), 0.f, 200.f, 5.f),
		Header(TEXT("Radar")),
		Row(P, "RadarSize", TEXT("Radar size (px radius; 0 = off)"), 0.f, 140.f, 2.f),
		Row(P, "RadarRange", TEXT("Radar range (m)"), 5.f, 80.f, 1.f),
		Row(P, "RadarFullRange", TEXT("Section fills completely inside (m; about melee distance)"), 1.f, 10.f, 0.25f),
		Row(P, "RadarSteps", TEXT("Fill steps between the rim and full"), 2.f, 8.f, 1.f),
		Row(P, "RadarHeightFade", TEXT("Height difference at which a section is faintest (m)"), 1.f, 20.f, 0.5f),
		Row(P, "RadarMinAlpha", TEXT("Faintest a section gets (0-1)"), 0.05f, 1.f, 0.05f),
		Header(TEXT("Your health")),
		Row(P, "HealthVignette", TEXT("Red vignette as your health drops (strength; 0 = off)"), 0.f, 2.f, 0.05f),
	});
	AddGlobal(TEXT("Jump"), {
		Row(P, "JumpHeight1", TEXT("Jump 1 height (m)"), 0.2f, 5.f, 0.1f),
		Row(P, "JumpHeight2", TEXT("Jump 2 height (m)"), 0.f, 5.f, 0.1f),
		Row(P, "AscentGravity", TEXT("Jump ascent speed"), 2.f, 80.f, 1.f),
		Row(P, "DescentGravity", TEXT("Jump descent speed"), 2.f, 80.f, 1.f),
		Row(P, "AirControl", TEXT("Air control"), 0.f, 40.f, 0.5f),
		Row(P, "ApexHang", TEXT("Apex hang"), 0.f, 0.9f, 0.05f),
		Row(P, "CoyoteTime", TEXT("Coyote time (s)"), 0.f, 0.3f, 0.01f),
		Row(P, "MantleReach", TEXT("Mantle reach (m, 0 = off)"), 0.f, 3.5f, 0.1f),
		Row(P, "MantleTime", TEXT("Mantle time (s)"), 0.1f, 1.f, 0.05f),
	});
	AddGlobal(TEXT("Crouch / Slide"), {
		Row(P, "CrouchSpeed", TEXT("Crouch walk speed"), 0.5f, 10.f, 0.5f),
		Row(P, "SlideSpeed", TEXT("Slide speed"), 1.f, 30.f, 0.5f),
		Row(P, "SlideDistance", TEXT("Slide distance (m)"), 1.f, 20.f, 0.5f),
	});
}

void UWWOSettingsMenu::RegisterCombatSections(AWWOCharacter* P)
{
	UWWOWeaponComponent* G = P->Weapon;
	UWWOGrenadeComponent* Th = P->Grenades;
	UWWOHealthComponent* H = P->Health;
	UWWOUltComponent* U = P->Ult;
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(P);
	UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(P);
	TWeakObjectPtr<UWWOWorldSubsystem> WeakWS = WS;
	const FWWOMenuButton ResetTargets = ResetTargetsButton(WS);
	AddGlobal(TEXT("Melee"), {
		Row(G, "MeleeTime", TEXT("Melee time (s, lower = faster)"), 0.2f, 2.f, 0.05f),
		Row(G, "MeleeReach", TEXT("Melee reach (m)"), 0.5f, 6.f, 0.1f),
		Row(G, "MeleeDamage", TEXT("Melee damage"), 1.f, 300.f, 1.f),
		Row(G, "LungeRange", TEXT("Lunge range (m)"), 0.f, 10.f, 0.1f),
		Row(G, "LungeAngle", TEXT("Lunge angle (degrees)"), 0.f, 60.f, 1.f),
		Row(G, "LungeSpeed", TEXT("Lunge speed (m/s)"), 5.f, 40.f, 0.5f),
		Row(G, "LungeVertical", TEXT("Lunge vertical pull"), 0.f, 1.f, 0.05f),
		Row(G, "LungeAim", TEXT("Lunge camera pull"), 0.f, 1.f, 0.05f),
		Header(TEXT("Impact feel (when a melee lands)")),
		Row(G, "MeleeHitstop", TEXT("Hit-stop freeze (s)"), 0.f, 0.3f, 0.005f),
		Row(G, "MeleeJolt", TEXT("Camera jolt up (degrees)"), 0.f, 8.f, 0.1f),
		Row(G, "MeleePunchFov", TEXT("Camera punch FOV (degrees, - = zoom in)"), -15.f, 15.f, 0.25f),
		Row(G, "MeleePunchRoll", TEXT("Camera punch roll (degrees)"), -8.f, 8.f, 0.1f),
		Row(G, "MeleePunchTime", TEXT("Camera punch settle time (s)"), 0.02f, 0.8f, 0.01f),
		Row(G, "MeleeRumbleStrong", TEXT("Rumble: heavy thump (big motor)"), 0.f, 1.f, 0.05f),
		Row(G, "MeleeRumbleWeak", TEXT("Rumble: sharp buzz (small motor)"), 0.f, 1.f, 0.05f),
		Row(G, "MeleeRumbleTime", TEXT("Rumble length (s)"), 0.f, 0.6f, 0.01f),
		Row(G, "MeleeKnockback", TEXT("Knockback (m/s)"), 0.f, 30.f, 0.5f),
		Row(G, "MeleeKillBoost", TEXT("Kills hit harder by (except knockback)"), 1.f, 3.f, 0.05f, Times()),
		Row(G, "MeleeKillKnockback", TEXT("Killing blow knockback"), 1.f, 4.f, 0.05f, Times()),
		// (The old single arm's rows, turn / lift / push / FOV / hold / pull-back, went with it: the
		// punch is the first-person hands' hook now, UWWOWeaponComponent::UpdateHands.)
		Header(TEXT("First-person arm")),
		Row(G, "HandsHitHold", TEXT("Fist stays on a hit (s)"), 0.f, 0.3f, 0.01f),
	});
	AddGlobal(TEXT("Grenades"), {
		Row(Th, "ThrowDistance", TEXT("Throw distance (m)"), 3.f, 40.f, 0.5f),
		Row(Th, "RechargeTime", TEXT("Recharge time (s)"), 0.5f, 60.f, 0.5f),
		Row(Th, "DetonationTime", TEXT("Detonation time (s)"), 0.1f, 5.f, 0.05f),
		Header(TEXT("Throw animation (left arm)")),
		Row(G, "ThrowRelease", TEXT("Grenade leaves your hand after (s)"), 0.f, 0.6f, 0.01f),
		Row(G, "ThrowFollow", TEXT("Follow-through (s)"), 0.05f, 0.6f, 0.01f),
	});
	TWeakObjectPtr<UWWOHealthComponent> WeakH = H;
	AddGlobal(TEXT("Player"), {
		Header(TEXT("Health and shields")),
		Row(H, "BaseHealth", TEXT("Health"), 10.f, 500.f, 5.f),
		Row(H, "BaseShield", TEXT("Shields"), 0.f, 500.f, 5.f),
		Row(H, "RechargeDelay", TEXT("Recovery delay (s)"), 0.5f, 15.f, 0.25f),
		Row(H, "HealthRegen", TEXT("Health regen (per s)"), 5.f, 300.f, 5.f),
		Row(H, "ShieldRegen", TEXT("Shield regen (per s)"), 5.f, 300.f, 5.f),
		Row(H, "RespawnTime", TEXT("Respawn time (s)"), 0.f, 10.f, 0.25f),
		Header(TEXT("Revive (online: down until a teammate revives you)")),
		Row(H, "ReviveTime", TEXT("Hold to revive (s)"), 0.25f, 10.f, 0.25f),
		Row(H, "ReviveRange", TEXT("Revive reach (m)"), 0.5f, 6.f, 0.1f),
		Row(H, "ReviveHealth", TEXT("Revived with health"), 0.05f, 1.f, 0.05f, Percent()),
		Row(H, "GodMode", TEXT("God mode (0/1)"), 0.f, 1.f, 1.f),
		Header(TEXT("Weapon handling")),
		Row(G, "SwapTime", TEXT("Weapon swap time (s)"), 0.05f, 1.5f, 0.01f),
		Row(G, "ReadyTime", TEXT("Ready time after sprint (s)"), 0.f, 0.6f, 0.01f),
		Header(TEXT("Shell")),
		Row(L, "PointValue", TEXT("Each group point changes stats by"), 0.f, 0.25f, 0.01f, Percent()),
	}, {
		{ TEXT("Take 30 damage"), [WeakH] { if (WeakH.IsValid()) WeakH->TakeDamage(30.f); } },
		{ TEXT("Die (test respawn)"), [WeakH] { if (WeakH.IsValid()) WeakH->Kill(); } },
	});
	{
		FWWOSettingRow TargetHealthRow = Row(WS, "TargetHealth", TEXT("Target health"), 10.f, 2000.f, 10.f);
		TargetHealthRow.Set = [WeakWS](float V) { if (WeakWS.IsValid()) WeakWS->SetTargetHealth(V); }; // new health applies right away
		AddGlobal(TEXT("Targets"), { TargetHealthRow, Row(WS, "RespawnTime", TEXT("Auto respawn (s, 0 = off)"), 0.f, 10.f, 0.5f) }, { ResetTargets });
	}
	TWeakObjectPtr<UWWOUltComponent> WeakU = U;
	AddGlobal(TEXT("Ult"), {
		Header(TEXT("Ult (F / L1+R1). VoltVine: massive grenade blast")),
		Row(U, "ChargeTime", TEXT("Charge time (s)"), 1.f, 600.f, 1.f),
		Row(U, "Damage", TEXT("Damage (center)"), 0.f, 5000.f, 10.f),
		Row(U, "BlastRadius", TEXT("Blast radius (m)"), 1.f, 40.f, 0.5f),
		Header(TEXT("Animation and camera")),
		Row(U, "WindupTime", TEXT("Windup before the blast (s)"), 0.2f, 4.f, 0.05f),
		Row(U, "HoldTime", TEXT("Watch the blast for (s)"), 0.f, 4.f, 0.05f),
		Row(U, "CameraBlend", TEXT("Camera glide time (s)"), 0.05f, 2.f, 0.05f),
		Row(U, "CameraDistance", TEXT("Camera distance behind (m)"), 1.f, 12.f, 0.25f),
		Row(U, "CameraHeight", TEXT("Camera height (m)"), -1.f, 6.f, 0.25f),
		Row(U, "CameraSide", TEXT("Camera over-shoulder (m)"), -3.f, 3.f, 0.1f),
		Header(TEXT("Veil (VeilSpore ult)")),
		Row(U, "VeilTime", TEXT("Veiled for (s)"), 1.f, 60.f, 0.5f),
		Row(U, "VeilFogRadius", TEXT("Fog radius (m)"), 1.f, 20.f, 0.25f),
		Row(U, "VeilFogTime", TEXT("Fog time (s)"), 1.f, 60.f, 0.5f),
		Row(U, "VeilMeleeMult", TEXT("Melee damage while veiled"), 1.f, 6.f, 0.1f, Times(1)),
		Row(U, "VeilCameraBlend", TEXT("Camera flash time (s)"), 0.05f, 1.f, 0.01f),
		Row(U, "VeilHold", TEXT("Time in third person (s)"), 0.f, 2.f, 0.05f),
		Header(TEXT("Entrench (IronBark ult)")),
		Row(U, "EntrenchTime", TEXT("Entrenched for (s)"), 1.f, 60.f, 0.5f),
		Row(U, "EntrenchGrenadeMult", TEXT("Grenade recharge speed"), 1.f, 20.f, 0.5f, Times(1)),
	}, { { TEXT("Fill ult"), [WeakU] { if (WeakU.IsValid()) WeakU->Fill(); } } });
}

void UWWOSettingsMenu::RegisterEnemySections(AWWOCharacter* P)
{
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(P);
	const FWWOMenuButton ResetTargets = ResetTargetsButton(UWWOWorldSubsystem::Get(P));
	UWWOSoldierData* SD = L->EnemyDataAs<UWWOSoldierData>(WWOIds::Enemy::Soldier);
	AddGlobal(TEXT("Soldier"), {
		Header(TEXT("Enemy: Soldier (shared by all soldiers)")),
		Row(SD, "AiEnabled", TEXT("AI on (0/1)"), 0.f, 1.f, 1.f),
		Row(SD, "MaxHealth", TEXT("Health"), 10.f, 1000.f, 5.f),
		Row(SD, "WalkSpeed", TEXT("Walk speed (m/s)"), 0.5f, 10.f, 0.25f),
		Row(SD, "MinRange", TEXT("Backs off inside (m)"), 0.f, 50.f, 0.5f),
		Row(SD, "MaxRange", TEXT("Moves in beyond (m)"), 1.f, 80.f, 0.5f),
		Row(SD, "SightRange", TEXT("Notices you within (m)"), 5.f, 120.f, 1.f),
		Row(SD, "StrafeTime", TEXT("Strafe switch time (s)"), 0.3f, 6.f, 0.1f),
		Row(SD, "TurnSpeed", TEXT("Turn speed"), 1.f, 30.f, 0.5f),
		Header(TEXT("Shooting")),
		Row(SD, "WindupTime", TEXT("Windup glow (s)"), 0.f, 2.f, 0.05f),
		Row(SD, "BurstCount", TEXT("Shots per burst"), 1.f, 10.f, 1.f),
		Row(SD, "BurstInterval", TEXT("Time between shots (s)"), 0.03f, 1.f, 0.01f),
		Row(SD, "BurstCooldown", TEXT("Time between bursts (s)"), 0.2f, 8.f, 0.1f),
		Row(SD, "GroupSpacing", TEXT("Group burst spacing (s)"), 0.f, 3.f, 0.05f),
		Row(SD, "ProjectileDamage", TEXT("Damage per shot"), 0.f, 100.f, 1.f),
		Row(SD, "ProjectileSpeed", TEXT("Shot speed (m/s)"), 5.f, 150.f, 1.f),
		Row(SD, "Spread", TEXT("Aim error (degrees)"), 0.f, 10.f, 0.1f),
		Header(TEXT("Cover")),
		Row(SD, "CoverHealth", TEXT("Seeks cover below health"), 0.f, 1.f, 0.05f, Percent()),
	}, { ResetTargets });
	UWWOSwarmData* SW = L->EnemyDataAs<UWWOSwarmData>(WWOIds::Enemy::Swarm);
	AddGlobal(TEXT("Swarm"), {
		Header(TEXT("Enemy: Swarm (shared by all swarm)")),
		Row(SW, "AiEnabled", TEXT("AI on (0/1)"), 0.f, 1.f, 1.f),
		Row(SW, "MaxHealth", TEXT("Health"), 5.f, 500.f, 5.f),
		Row(SW, "WalkSpeed", TEXT("Run speed (m/s)"), 1.f, 20.f, 0.25f),
		Row(SW, "SightRange", TEXT("Notices you within (m)"), 5.f, 120.f, 1.f),
		Row(SW, "TurnSpeed", TEXT("Turn speed"), 1.f, 30.f, 0.5f),
		Row(SW, "SpreadDistance", TEXT("Pack spacing (m)"), 0.f, 5.f, 0.1f),
		Header(TEXT("Leap")),
		Row(SW, "LeapRange", TEXT("Leaps from (m)"), 0.f, 12.f, 0.25f),
		Row(SW, "LeapSpeed", TEXT("Leap speed (m/s)"), 2.f, 30.f, 0.5f),
		Row(SW, "LeapHeight", TEXT("Leap height (m)"), 0.f, 4.f, 0.1f),
		Row(SW, "LeapDamage", TEXT("Leap damage"), 0.f, 100.f, 1.f),
		Row(SW, "LeapCooldown", TEXT("Time between leaps (s)"), 0.2f, 10.f, 0.1f),
		Header(TEXT("Bite")),
		Row(SW, "BiteRange", TEXT("Bites within (m)"), 0.5f, 4.f, 0.05f),
		Row(SW, "BiteDamage", TEXT("Bite damage"), 0.f, 100.f, 1.f),
		Row(SW, "BiteInterval", TEXT("Time between bites (s)"), 0.1f, 4.f, 0.05f),
	}, { ResetTargets });
	UWWOTankData* TK = L->EnemyDataAs<UWWOTankData>(WWOIds::Enemy::Tank);
	AddGlobal(TEXT("Tank"), {
		Header(TEXT("Enemy: Tank")),
		Row(TK, "AiEnabled", TEXT("AI on (0/1)"), 0.f, 1.f, 1.f),
		Row(TK, "MaxHealth", TEXT("Health"), 50.f, 5000.f, 25.f),
		Row(TK, "WalkSpeed", TEXT("Walk speed (m/s)"), 0.5f, 10.f, 0.25f),
		Row(TK, "SightRange", TEXT("Notices you within (m)"), 5.f, 120.f, 1.f),
		Row(TK, "TurnSpeed", TEXT("Turn speed"), 0.2f, 20.f, 0.1f),
		Header(TEXT("Shield (applies on reset)")),
		Row(TK, "Shield", TEXT("Shield (0 = none)"), 0.f, 2000.f, 10.f),
		Row(TK, "ShieldRegenDelay", TEXT("Refill delay (s)"), 0.5f, 30.f, 0.5f),
		Row(TK, "ShieldRegenRate", TEXT("Refill rate (per s)"), 5.f, 500.f, 5.f),
		Header(TEXT("Slam")),
		Row(TK, "SlamRange", TEXT("Slams when you're within (m)"), 1.f, 10.f, 0.25f),
		Row(TK, "SlamRadius", TEXT("Slam radius (m)"), 1.f, 12.f, 0.25f),
		Row(TK, "SlamDamage", TEXT("Slam damage"), 0.f, 200.f, 1.f),
		Row(TK, "SlamWindup", TEXT("Arms-up warning (s)"), 0.1f, 3.f, 0.05f),
		Row(TK, "SlamRecover", TEXT("Stands still after (s)"), 0.f, 5.f, 0.1f),
		Row(TK, "SlamCooldown", TEXT("Time between slams (s)"), 0.5f, 15.f, 0.25f),
		Row(TK, "SlowAmount", TEXT("Slow amount"), 0.f, 0.95f, 0.05f, [](float V) { return FString::Printf(TEXT("%d%% slower"), FMath::RoundToInt(V * 100.f)); }),
		Row(TK, "SlowTime", TEXT("Slow time (s)"), 0.f, 10.f, 0.25f),
		Header(TEXT("Lobbed shot")),
		Row(TK, "LobDamage", TEXT("Damage (center)"), 0.f, 200.f, 1.f),
		Row(TK, "LobRadius", TEXT("Splash radius (m)"), 0.5f, 10.f, 0.25f),
		Row(TK, "LobInterval", TEXT("Time between shots (s)"), 0.5f, 15.f, 0.25f),
		Row(TK, "LobFlightTime", TEXT("Time in the air (s)"), 0.3f, 4.f, 0.05f),
	}, { ResetTargets });
}

void UWWOSettingsMenu::RegisterMatchSections(AWWOCharacter* P)
{
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(P);
	AWWOGameMode* Mode = P->GetWorld()->GetAuthGameMode<AWWOGameMode>();
	const FWWOMenuButton ResetTargets = ResetTargetsButton(UWWOWorldSubsystem::Get(P));
	TWeakObjectPtr<UWorld> WeakWorld = P->GetWorld();
	auto MapButton = [WeakWorld](const TCHAR* Text, const TCHAR* Level) -> FWWOMenuButton
	{
		const FName LevelName(Level);
		return { FString(Text), [WeakWorld, LevelName] { if (WeakWorld.IsValid()) AWWOGameMode::LoadMap(WeakWorld.Get(), LevelName); } };
	};
	// The same table the launch screen offers (UWWONetSubsystem::Maps): a new map goes there once.
	TArray<FWWOMenuButton> MapButtons;
	for (const FWWOMapInfo& Info : UWWONetSubsystem::Maps())
	{
		MapButtons.Add(MapButton(*FString::Printf(TEXT("%s (%s)"), *Info.Title, *Info.Blurb), *Info.Level.ToString()));
	}
	AddGlobal(TEXT("Maps"), { Header(TEXT("Switch maps (restarts the run on that map)")) }, MapButtons);
	// LAN play (UWWONetSubsystem). Hosting reopens this map for others to join; joining loads the host's.
	auto Net = [WeakWorld]() { return WeakWorld.IsValid() ? UWWONetSubsystem::Get(WeakWorld.Get()) : nullptr; };
	FWWOSettingRow NetStatus;
	NetStatus.Header = TEXT("Status");
	NetStatus.LiveHeader = [WeakWorld, Net]() { UWWONetSubsystem* N = Net(); return N && WeakWorld.IsValid() ? N->DescribeStatus(WeakWorld.Get()) : FString(); };
	NetStatus.bPref = true;
	AddGlobal(TEXT("Multiplayer"), {
		NetStatus,
		Header(TEXT("The title screen starts and joins games. The host and join buttons here are shortcuts for testing.")),
	}, {
		{ TEXT("Host a LAN game (this map)"), [WeakWorld, Net] { if (UWWONetSubsystem* N = Net(); N && WeakWorld.IsValid()) N->Host(FName(*UGameplayStatics::GetCurrentLevelName(WeakWorld.Get()))); } },
		{ TEXT("Host a PvP game (team deathmatch, this map)"), [WeakWorld, Net] { if (UWWONetSubsystem* N = Net(); N && WeakWorld.IsValid()) N->Host(FName(*UGameplayStatics::GetCurrentLevelName(WeakWorld.Get())), true); } },
		{ TEXT("Quit to main menu"), [Net] { if (UWWONetSubsystem* N = Net()) N->LeaveToTitle(); }, true },
		{ TEXT("Switch team (PvP)"), [WeakWorld] { if (WeakWorld.IsValid()) if (AWWOPlayerController* PC = Cast<AWWOPlayerController>(WeakWorld->GetFirstPlayerController())) PC->ServerSwitchTeam(); }, true },
		{ TEXT("Find and join a LAN game"), [Net] { if (UWWONetSubsystem* N = Net()) N->FindAndJoin(); } },
		{ TEXT("Join a game on this machine (testing)"), [Net] { if (UWWONetSubsystem* N = Net()) N->JoinAddress(TEXT("127.0.0.1")); } },
	});
	TWeakObjectPtr<AWWOGameMode> WeakMode = Mode;
	AddGlobal(TEXT("PvP"), {
		Header(TEXT("Team deathmatch (the host's settings count; 0 turns a limit off)")),
		Row(Mode, "KillTarget", TEXT("Team kills to win"), 0.f, 200.f, 1.f),
		Row(Mode, "TimeLimit", TEXT("Time limit (minutes)"), 0.f, 60.f, 1.f),
		Row(Mode, "PvPRespawnTime", TEXT("Respawn delay (s)"), 0.5f, 15.f, 0.5f),
	});
	AddGlobal(TEXT("FootRace"), {
		Header(TEXT("Breach energy (FootRace; the host's settings count)")),
		Row(Mode, "BreachThreshold", TEXT("Energy needed to enter the hub"), 0.f, 500.f, 5.f),
		Row(Mode, "EnergyPerEnemy", TEXT("Energy per AI enemy killed"), 0.f, 100.f, 1.f),
		Row(Mode, "EnergyPerPlayerKill", TEXT("Energy per enemy player killed"), 0.f, 200.f, 1.f),
		Row(Mode, "BreachDrain", TEXT("Drain per player in the hub (per second)"), 0.f, 50.f, 0.5f),
		Row(Mode, "BreachDeathLoss", TEXT("Share of the pool lost when a breacher dies"), 0.f, 1.f, 0.05f),
		Row(Mode, "LeverHoldTime", TEXT("Time to pull a door lever (s)"), 0.f, 5.f, 0.1f),
		Header(TEXT("Boss")),
		Row(Mode, "BossHealth", TEXT("Boss health (applies to the next boss)"), 100.f, 20000.f, 100.f),
		Row(Mode, "BossWindow", TEXT("First damage window (s; each next is a third longer)"), 1.f, 60.f, 0.5f),
		Header(TEXT("Lane rooms")),
		Row(Mode, "RoomEnemies", TEXT("Enemies in a team's live room"), 0.f, 30.f, 1.f),
		Row(Mode, "RoomSpawnStagger", TEXT("Time between spawns while a room fills (s)"), 0.f, 3.f, 0.05f),
		Row(Mode, "RoomRefillTime", TEXT("Time between spawns once it has filled (s)"), 0.f, 20.f, 0.5f),
	});
	AddGlobal(TEXT("Bots"), {
		Header(TEXT("Bot players in PvP (the host's settings count)")),
		Row(Mode, "Bots", TEXT("Bots in the match (raising it adds them now)"), 0.f, 11.f, 1.f),
		Row(Mode, "BotHealth", TEXT("Bot health (yours is on the Player page)"), 10.f, 500.f, 5.f),
		Row(Mode, "BotShield", TEXT("Bot shields"), 0.f, 500.f, 5.f),
		Row(Mode, "BotReaction", TEXT("Reaction time (s)"), 0.f, 1.5f, 0.05f),
		Row(Mode, "BotAimError", TEXT("Aim error (degrees; 0 = perfect)"), 0.f, 8.f, 0.1f),
		Row(Mode, "BotTurnSpeed", TEXT("Turn speed (degrees per second)"), 60.f, 720.f, 10.f),
		Row(Mode, "BotViewAngle", TEXT("View cone (degrees)"), 30.f, 360.f, 5.f),
		Row(Mode, "BotSightRange", TEXT("Sight range (m)"), 10.f, 150.f, 5.f),
		Row(Mode, "BotMemory", TEXT("Hunts a lost enemy for (s)"), 0.f, 20.f, 0.5f),
		Row(Mode, "BotAggression", TEXT("Aggression (0 = backs off early, 1 = never)"), 0.f, 1.f, 0.05f),
		Row(Mode, "BotKitUse", TEXT("Grenade, ability and ult use (0 = never)"), 0.f, 1.f, 0.05f),
		Header(TEXT("Teamwork")),
		Row(Mode, "BotStickToPlayer", TEXT("Share of your team's bots that escort you (the rest pair up)"), 0.f, 1.f, 0.05f),
		Row(Mode, "BotCalloutRange", TEXT("Callout range (m; teammates this near come to help; 0 = off)"), 0.f, 100.f, 5.f),
	});
	AddGlobal(TEXT("Waves"), {
		Header(TEXT("Arena waves (changes to 'on' and the wave list apply on Restart run)")),
		Row(Mode, "Enabled", TEXT("Waves on (0/1; 0 = sandbox)"), 0.f, 1.f, 1.f),
		Row(Mode, "WavesToWin", TEXT("Waves to win"), 1.f, 30.f, 1.f),
		Row(Mode, "StartDelay", TEXT("Time before wave 1 (s)"), 0.f, 30.f, 0.5f),
		Row(Mode, "BreakTime", TEXT("Break between waves (s)"), 0.f, 60.f, 0.5f),
		Row(Mode, "SizeScale", TEXT("Wave size"), 0.25f, 4.f, 0.05f, Times()),
		Row(Mode, "SpawnStagger", TEXT("Time between spawns (s)"), 0.f, 3.f, 0.05f),
	}, {
		{ TEXT("Restart run"), [WeakMode, WeakWorld]
			{
				// In someone else's game there's no game mode here: LoadMap says only the host can.
				if (WeakMode.IsValid()) WeakMode->Restart();
				else if (WeakWorld.IsValid()) AWWOGameMode::LoadMap(WeakWorld.Get(), FName(*UGameplayStatics::GetCurrentLevelName(WeakWorld.Get())));
			} },
		{ TEXT("Skip wave (kill all)"), [WeakMode] { if (WeakMode.IsValid()) WeakMode->SkipWave(); } },
	});
	UWWOAllyData* AL = L->Catalog.AllyData;
	AddGlobal(TEXT("Allies"), {
		Header(TEXT("AI teammates (for testing team abilities like Shield Share)")),
		Row(AL, "AiEnabled", TEXT("AI on (0/1)"), 0.f, 1.f, 1.f),
		Row(AL, "MaxHealth", TEXT("Health"), 10.f, 500.f, 5.f),
		Row(AL, "MaxShield", TEXT("Shields"), 0.f, 500.f, 5.f),
		Row(AL, "RechargeDelay", TEXT("Recovery delay (s)"), 0.5f, 15.f, 0.25f),
		Row(AL, "ShieldRegen", TEXT("Shield regen (per s)"), 1.f, 300.f, 5.f),
		Row(AL, "MoveSpeed", TEXT("Move speed (m/s)"), 1.f, 15.f, 0.25f),
		Row(AL, "FollowMin", TEXT("Stays at least (m) from you"), 0.f, 10.f, 0.25f),
		Row(AL, "FollowMax", TEXT("Walks back past (m)"), 1.f, 30.f, 0.5f),
		Header(TEXT("Shooting")),
		Row(AL, "Damage", TEXT("Damage per shot"), 0.f, 100.f, 1.f),
		Row(AL, "FireInterval", TEXT("Time between shots (s)"), 0.05f, 3.f, 0.05f),
		Row(AL, "Spread", TEXT("Aim error (degrees)"), 0.f, 15.f, 0.25f),
		Row(AL, "SightRange", TEXT("Shoots enemies within (m)"), 5.f, 120.f, 1.f),
		Row(AL, "RespawnTime", TEXT("Down for (s)"), 1.f, 60.f, 1.f),
	}, { ResetTargets });
}

void UWWOSettingsMenu::RegisterAimAssistSection(AWWOCharacter* P)
{
	UWWOAimAssistComponent* AA = P->AimAssist;
	AddGlobal(TEXT("Aim Assist"), {
		Row(AA, "MouseAssist", TEXT("Mouse assist (0 = off)"), 0.f, 1.f, 0.05f),
		Row(AA, "ShowDebug", TEXT("Show assist cones (0/1)"), 0.f, 1.f, 1.f),
		Header(TEXT("Hidden assist (only while a shot's recoil plays out; per-weapon amounts in each weapon)")),
		Row(AA, "HiddenAssist", TEXT("Hidden assist on (0/1)"), 0.f, 1.f, 1.f),
		Row(AA, "ShowHiddenAssist", TEXT("Show hidden assist markers (0/1)"), 0.f, 1.f, 1.f),
	});
}

// --- One section per item ------------------------------------------------------------------------

void UWWOSettingsMenu::RegisterWeaponSections(AWWOCharacter* P)
{
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(P);
	for (UWWOWeaponData* W : L->Catalog.Weapons)
	{
		FWWOSettingSection S;
		S.Key = TEXT("weapon/") + W->Id.ToString();
		S.Title = W->DisplayName;
		S.Rows = {
			Header(W->Archetype), Header(TEXT("Aim down sights")),
			Row(W, "Zoom", TEXT("ADS zoom (magnification)"), 1.f, 10.f, 0.1f),
			Row(W, "AdsTime", TEXT("ADS time (s)"), 0.05f, 1.f, 0.01f),
			Row(W, "AdsSensitivity", TEXT("ADS sensitivity multiplier"), 0.1f, 2.f, 0.05f),
			Row(W, "AdsMoveSpeed", TEXT("ADS move speed multiplier"), 0.1f, 1.f, 0.05f),
			Row(W, "AdsTilt", TEXT("ADS gun angle (degrees; + tips the muzzle down, - up)"), -15.f, 15.f, 0.25f),
			Row(W, "AdsVignette", TEXT("ADS vignette"), 0.f, 1.f, 0.05f),
			Row(W, "ScopeOverlay", TEXT("Scope overlay (0/1)"), 0.f, 1.f, 1.f),
			Row(W, "SightHeight", TEXT("Sight dot height above gun (cm)"), -6.f, 6.f, 0.1f),
			Header(TEXT("Shooting")),
			Row(W, "SemiAuto", TEXT("Semi-auto (0 = hold to fire)"), 0.f, 1.f, 1.f),
			Row(W, "FireRate", TEXT("Fire rate (rounds/min; bursts/min for a burst gun)"), 20.f, 1200.f, 5.f),
			Row(W, "Burst", TEXT("Rounds per trigger pull (1 = no burst)"), 1.f, 6.f, 1.f),
			Row(W, "BurstRate", TEXT("Fire rate inside a burst (rounds/min)"), 200.f, 1500.f, 10.f),
			Row(W, "Damage", W->Pellets > 1.f ? TEXT("Damage (per pellet)") : TEXT("Damage"), 1.f, 400.f, 1.f),
			Row(W, "CritMultiplier", TEXT("Critical hit multiplier"), 1.f, 4.f, 0.05f),
			Row(W, "FireRange", TEXT("Range (m)"), 5.f, 400.f, 5.f),
			Row(W, "Pellets", TEXT("Pellets per shot"), 1.f, 20.f, 1.f),
			Row(W, "PelletSpread", TEXT("Pellet spread (degrees)"), 0.f, 15.f, 0.1f),
			Row(W, "HipSpread", TEXT("Hip-fire spread (degrees)"), 0.f, 10.f, 0.1f),
			Row(W, "RecoilUp", TEXT("Recoil up (degrees/shot)"), 0.f, 8.f, 0.05f),
			Row(W, "RecoilSide", TEXT("Recoil side (degrees/shot)"), 0.f, 3.f, 0.05f),
			Row(W, "RecoilKickTime", TEXT("Recoil rise time (s, 0 = instant)"), 0.f, 0.3f, 0.005f),
			Row(W, "RecoilRecovery", TEXT("Recoil falls back (0-1)"), 0.f, 1.f, 0.05f),
			Row(W, "RecoilRecoveryTime", TEXT("Recoil fall-back speed (s, when not synced)"), 0.02f, 1.f, 0.01f),
			Row(W, "RecoilRecoverySync", TEXT("Fall-back ends when next shot is ready (0/1)"), 0.f, 1.f, 1.f),
			Row(W, "RecoilRecoveryCurve", TEXT("Synced fall-back curve (1 = steady, higher = fast then slow)"), 1.f, 5.f, 0.1f),
			Row(W, "RecoilRecoveryDelay", TEXT("Synced fall-back lands after shot is ready by (s)"), -0.15f, 0.15f, 0.005f),
			Row(W, "VerticalKick", TEXT("Vertical kick (gun tilts up; 1 = about 3.4 degrees)"), 0.f, 20.f, 0.1f),
			Row(W, "HorizontalKick", TEXT("Horizontal kick (+ right, - left)"), -5.f, 5.f, 0.1f),
			Row(W, "KickHold", TEXT("Kick holds at the top for (s)"), 0.f, 0.4f, 0.005f),
			Row(W, "KickReturn", TEXT("Kick comes back over (s)"), 0.02f, 0.8f, 0.01f),
			Row(W, "AdsKick", TEXT("Kick left when aimed in (0-1)"), 0.f, 1.f, 0.01f),
			Row(W, "KickPush", TEXT("Kick pushes the gun back (cm)"), 0.f, 15.f, 0.1f),
			Row(W, "KickPivotDown", TEXT("Kick turns about a point below the sight by (cm)"), 0.f, 20.f, 0.1f),
			Row(W, "KickPivotAhead", TEXT("...and ahead of the sight by (cm)"), -10.f, 20.f, 0.1f),
			Row(W, "KickSide", TEXT("Kick shifts the gun right (cm)"), -5.f, 5.f, 0.05f),
			Row(W, "RecoilShape", TEXT("Recoil follows the measured curves (0/1)"), 0.f, 1.f, 1.f),
			Row(W, "BurstRecoilLater", TEXT("Burst: later rounds kick this much of the first"), 0.f, 1.5f, 0.05f),
			Row(W, "MagazineSize", TEXT("Magazine size"), 1.f, 100.f, 1.f),
			Row(W, "ReloadTime", TEXT("Reload time (s)"), 0.3f, 6.f, 0.05f),
			Header(TEXT("Rumble")),
			Row(W, "RumbleStrong", TEXT("Rumble: heavy thump (big motor)"), 0.f, 1.f, 0.05f),
			Row(W, "RumbleWeak", TEXT("Rumble: sharp buzz (small motor)"), 0.f, 1.f, 0.05f),
			Row(W, "RumbleTime", TEXT("Rumble length (s)"), 0.f, 0.5f, 0.005f),
			Header(TEXT("Camera punch")),
			Row(W, "PunchFov", TEXT("Punch: FOV widens by (degrees)"), 0.f, 10.f, 0.1f),
			Row(W, "PunchRoll", TEXT("Punch: camera roll (degrees)"), 0.f, 5.f, 0.05f),
			Row(W, "PunchTime", TEXT("Punch settle time (s)"), 0.02f, 0.5f, 0.005f),
			Header(TEXT("Handling")),
			Row(W, "ViewmodelFov", TEXT("Gun FOV"), 40.f, 100.f, 1.f),
			Row(W, "Sway", TEXT("Gun sway"), 0.f, 3.f, 0.1f),
			Row(W, "GunBob", TEXT("Gun bob"), 0.f, 3.f, 0.1f),
			Row(W, "AdsGunBob", TEXT("Gun bob left when aimed in (0-1)"), 0.f, 1.f, 0.05f),
			Row(W, "StrafeShift", TEXT("Strafe: gun shifts sideways (cm)"), -3.f, 3.f, 0.05f),
			Row(W, "StrafeRoll", TEXT("Strafe: gun rolls (degrees)"), -8.f, 8.f, 0.1f),
			Row(W, "AdsMark", TEXT("Aimed-in mark (0 none, 1 dot, 2 iron-sight marks)"), 0.f, 2.f, 1.f),
			Row(W, "SlideTilt", TEXT("Slide gun tilt (degrees)"), 0.f, 60.f, 1.f),
			Header(TEXT("Aim assist")),
			Row(W, "AimAssistance", TEXT("Aim assistance stat"), 0.f, 100.f, 1.f),
			Row(W, "CritMagnetism", TEXT("Head magnetism (fraction of cone)"), 0.f, 1.f, 0.05f),
			Row(W, "MagnetismAngle", TEXT("Magnetism cone (degrees)"), 0.f, 10.f, 0.1f),
			Row(W, "FrictionStrength", TEXT("Friction strength"), 0.f, 0.9f, 0.05f),
			Row(W, "FrictionAngle", TEXT("Friction cone (degrees)"), 0.f, 15.f, 0.1f),
			Row(W, "Adhesion", TEXT("Adhesion strength"), 0.f, 1.f, 0.05f),
			Row(W, "AssistRange", TEXT("Assist range (m)"), 5.f, 300.f, 1.f),
			Row(W, "AdsBoost", TEXT("ADS assist boost"), 1.f, 3.f, 0.05f),
			Header(TEXT("Hidden assist (while this gun's recoil plays out)")),
			Row(W, "KickSteer", TEXT("Kick steer (the kick drifts sideways toward the target; 0 = off)"), 0.f, 3.f, 0.05f),
			Row(W, "RecoverySteer", TEXT("Fall-back steer (the recoil falls back toward the target; 0 = off)"), 0.f, 3.f, 0.05f),
			Row(W, "FollowupMagnetism", TEXT("Follow-up shot magnetism (x cone; 1 = off)"), 1.f, 4.f, 0.05f),
			Row(W, "KickFriction", TEXT("Friction during recoil (x strength; 1 = off)"), 1.f, 4.f, 0.05f),
			Row(W, "KickAdhesion", TEXT("Adhesion added during recoil (0 = off)"), 0.f, 1.f, 0.05f),
		};
		Sections.Add(MoveTemp(S));
	}
}

void UWWOSettingsMenu::RegisterShellSections(AWWOCharacter* P)
{
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(P);
	for (UWWOFramePartData* Part : L->Catalog.FrameParts)
	{
		FWWOSettingSection S;
		S.Key = TEXT("frame/") + Part->Id.ToString();
		S.Title = Part->DisplayName;
		S.Rows.Add(Header(Capitalize(Part->Slot.ToString()) + TEXT(" part · group points")));
		for (const FName& Group : UWWOLoadoutSubsystem::GroupOrder())
		{
			TArray<FString> Stats;
			for (const FName& Stat : UWWOLoadoutSubsystem::StatsInGroup(Group)) Stats.Add(UWWOLoadoutSubsystem::StatName(Stat).ToLower());
			S.Rows.Add(Header(UWWOLoadoutSubsystem::GroupTitle(Group) + TEXT(": ") + FString::Join(Stats, TEXT(", "))));
			S.Rows.Add(Row(Part, FName(*UWWOLoadoutSubsystem::GroupTitle(Group)), UWWOLoadoutSubsystem::GroupTitle(Group) + TEXT(" points"), -5.f, 5.f, 1.f,
				[](float V) { return FString::Printf(TEXT("%+d"), FMath::RoundToInt(V)); }));
		}
		Sections.Add(MoveTemp(S));
	}
}

void UWWOSettingsMenu::RegisterAbilitySections(AWWOCharacter* P)
{
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(P);
	for (UWWOAbilityData* A : L->Catalog.Abilities)
	{
		FWWOSettingSection S;
		S.Key = TEXT("ability/") + A->Id.ToString();
		S.Title = A->DisplayName;
		S.Rows.Add(Header(KindName(A->Kind) + TEXT(" · ") + A->Description));
		const FName Id = A->Id;
		if (Id == WWOIds::Ability::Overgrowth) S.Rows.Append({ Row(A, "SpeedMult", TEXT("Speed multiplier"), 1.f, 2.f, 0.05f), Row(A, "ExtraJumps", TEXT("Extra jumps"), 0.f, 3.f, 1.f) });
		if (Id == WWOIds::Ability::Thornfist) S.Rows.Append({
			Row(A, "MeleeDamageMult", TEXT("Melee damage multiplier"), 1.f, 5.f, 0.05f), Row(A, "Duration", TEXT("Duration (s)"), 1.f, 30.f, 0.5f),
			Row(A, "Cooldown", TEXT("Recharge time (s)"), 1.f, 120.f, 1.f), Header(TEXT("Electric discharge (when a boosted melee lands)")),
			Row(A, "DischargeArcs", TEXT("Lightning arcs"), 0.f, 16.f, 1.f), Row(A, "DischargeRadius", TEXT("Arc reach (m)"), 0.2f, 4.f, 0.05f),
			Row(A, "DischargeTime", TEXT("Crackle time (s)"), 0.05f, 1.5f, 0.05f) });
		if (Id == WWOIds::Ability::StaticCharge) S.Rows.Append({
			Row(A, "ChargeDamage", TEXT("Damage taken to fill it"), 10.f, 1000.f, 10.f), Row(A, "ChainDamage", TEXT("Damage to the enemy you punch"), 0.f, 1000.f, 5.f),
			Row(A, "ChainJumps", TEXT("Jumps to more enemies"), 0.f, 20.f, 1.f), Row(A, "ChainRange", TEXT("Jump range (m)"), 1.f, 30.f, 0.5f),
			Row(A, "ChainFalloff", TEXT("Each jump does (of the last)"), 0.3f, 1.f, 0.05f, Percent()), Row(A, "ChainDelay", TEXT("Time between jumps (s)"), 0.f, 0.4f, 0.01f) });
		if (Id == WWOIds::Ability::RootBombs) S.Rows.Append({
			Row(A, "Cooldown", TEXT("Recharge time (s)"), 1.f, 120.f, 1.f), Row(A, "BombDamage", TEXT("Damage per bomb (center)"), 0.f, 1000.f, 5.f),
			Row(A, "BombRadius", TEXT("Blast radius (m)"), 0.5f, 12.f, 0.25f), Row(A, "BombSpeed", TEXT("Throw speed (m/s)"), 5.f, 60.f, 1.f) });
		if (Id == WWOIds::Ability::ShieldShare) S.Rows.Add(Row(A, "ShieldAmount", TEXT("Shield given per explosion"), 0.f, 300.f, 5.f));
		if (Id == WWOIds::Ability::ExtraGrenade) S.Rows.Add(Row(A, "ExtraCharges", TEXT("Extra grenade charges"), 0.f, 3.f, 1.f));
		if (Id == WWOIds::Ability::MeleeGuard) S.Rows.Append({ Row(A, "GuardReduction", TEXT("Melee damage cut by"), 0.f, 1.f, 0.05f, Percent()),
			Row(A, "Duration", TEXT("Lasts after a melee hit (s)"), 0.5f, 20.f, 0.5f) });
		if (Id == WWOIds::Ability::SporeFogDamage) S.Rows.Add(Row(A, "FogDps", TEXT("Fog damage per second"), 0.f, 100.f, 1.f));
		if (Id == WWOIds::Ability::SporeDash) S.Rows.Append({
			Row(A, "Cooldown", TEXT("Recharge time (s)"), 1.f, 120.f, 1.f), Row(A, "DashSpeed", TEXT("Dash speed backwards (m/s)"), 2.f, 40.f, 0.5f),
			Row(A, "DashUp", TEXT("Dash lift (m/s)"), 0.f, 15.f, 0.25f), Row(A, "DashFogRadius", TEXT("Fog radius (m)"), 0.5f, 12.f, 0.25f),
			Row(A, "DashFogTime", TEXT("Fog time (s)"), 0.5f, 30.f, 0.5f) });
		if (Id == WWOIds::Ability::SporePatches) S.Rows.Append({
			Row(A, "PatchMax", TEXT("Patches per grenade"), 1.f, 10.f, 1.f), Row(A, "PatchRadius", TEXT("Fog radius per patch (m)"), 0.5f, 8.f, 0.25f),
			Row(A, "PatchTime", TEXT("Patch time (s)"), 0.5f, 30.f, 0.5f) });
		if (Id == WWOIds::Ability::Seedburst) S.Rows.Append({
			Row(A, "SubCount", TEXT("Seed count"), 1.f, 8.f, 1.f), Row(A, "SubScale", TEXT("Seed size (blast and damage)"), 0.1f, 1.f, 0.05f),
			Row(A, "SubSpread", TEXT("Seed spread (m/s)"), 1.f, 15.f, 0.5f), Row(A, "SubFuse", TEXT("Seed fuse (s)"), 0.1f, 3.f, 0.05f) });
		Sections.Add(MoveTemp(S));
	}
}

void UWWOSettingsMenu::RegisterTraitSections(AWWOCharacter* P)
{
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(P);
	for (UWWOTraitData* T : L->Catalog.Traits)
	{
		FWWOSettingSection S;
		S.Key = TEXT("trait/") + T->Id.ToString();
		S.Title = T->DisplayName;
		S.Rows.Add(Header(T->Description));
		TWeakObjectPtr<UWWOTraitData> WeakT = T;
		for (int32 i = 0; i < T->Mods.Num(); ++i)
		{
			FWWOSettingRow R;
			R.Id = FName(*FString::Printf(TEXT("mod%d"), i)); // every mod edits "value", so each gets its own id
			R.Label = UWWOLoadoutSubsystem::StatName(T->Mods[i].Stat) + TEXT(" change");
			R.Min = -0.9f;
			R.Max = 2.f;
			R.Step = 0.01f;
			R.Show = SignedPercent();
			R.Get = [WeakT, i] { return WeakT.IsValid() && WeakT->Mods.IsValidIndex(i) ? WeakT->Mods[i].Value : 0.f; };
			R.Set = [WeakT, i](float V) { if (WeakT.IsValid() && WeakT->Mods.IsValidIndex(i)) WeakT->Mods[i].Value = V; };
			S.Rows.Add(R);
		}
		if (!T->Effect.IsNone() && T->Effect != "no_radar") S.Rows.Add(Row(T, "Amount", TEXT("Effect strength"), 0.f, 3.f, 0.05f, SignedPercent()));
		Sections.Add(MoveTemp(S));
	}
}

void UWWOSettingsMenu::RegisterGrenadeSections(AWWOCharacter* P)
{
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(P);
	UWWOGrenadeComponent* Th = P->Grenades;
	for (UWWOGrenadeData* Gr : L->Catalog.Grenades)
	{
		FWWOSettingSection S;
		S.Key = TEXT("grenade/") + Gr->Id.ToString();
		S.Title = Gr->DisplayName;
		TWeakObjectPtr<UWWOGrenadeComponent> WeakTh = Th;
		S.Rows = {
			Header(TEXT("Blast")),
			Row(Gr, "Damage", TEXT("Damage (center)"), 1.f, 500.f, 1.f),
			Row(Gr, "BlastRadius", TEXT("Blast radius (m)"), 0.5f, 12.f, 0.1f),
			Header(TEXT("Adjustments on top of the global grenade settings")),
			Row(Gr, "ThrowDistanceMult", TEXT("Throw distance"), 0.25f, 3.f, 0.05f, [WeakTh](float V) { return FString::Printf(TEXT("x%.2f = %.1f m"), V, WeakTh.IsValid() ? WeakTh->ThrowDistance * V : 0.f); }),
			Row(Gr, "RechargeMult", TEXT("Recharge time"), 0.25f, 3.f, 0.05f, [WeakTh](float V) { return FString::Printf(TEXT("x%.2f = %.1f s"), V, WeakTh.IsValid() ? WeakTh->RechargeTime * V : 0.f); }),
			Row(Gr, "FuseMult", TEXT("Detonation time"), 0.25f, 3.f, 0.05f, [WeakTh](float V) { return FString::Printf(TEXT("x%.2f = %.2f s"), V, WeakTh.IsValid() ? WeakTh->DetonationTime * V : 0.f); }),
			Row(Gr, "Bounce", TEXT("Bounciness"), 0.f, 1.f, 0.05f),
		};
		switch (Gr->Kind)
		{
		case EWWOGrenadeKind::Pool:
			S.Rows.Append({ Header(TEXT("Lingering pool (radius = blast radius)")), Row(Gr, "PoolDps", TEXT("Pool damage per second"), 0.f, 200.f, 1.f),
				Row(Gr, "PoolTime", TEXT("Pool time (s)"), 0.5f, 20.f, 0.5f) });
			break;
		case EWWOGrenadeKind::Wall:
			S.Rows.Append({ Header(TEXT("Wall")), Row(Gr, "WallWidth", TEXT("Wall width (m)"), 1.f, 12.f, 0.25f),
				Row(Gr, "WallHeight", TEXT("Wall height (m)"), 0.5f, 6.f, 0.1f), Row(Gr, "WallTime", TEXT("Wall lasts (s)"), 1.f, 60.f, 0.5f) });
			break;
		case EWWOGrenadeKind::Blind:
			S.Rows.Append({ Header(TEXT("Blinding")), Row(Gr, "BlindTime", TEXT("Blinds enemies for (s)"), 0.5f, 15.f, 0.25f) });
			break;
		case EWWOGrenadeKind::Tripmine:
			S.Rows.Append({ Header(TEXT("Slowing tripmine")), Row(Gr, "ArmTime", TEXT("Arms after (s)"), 0.f, 5.f, 0.05f),
				Row(Gr, "TriggerRadius", TEXT("Goes off within (m)"), 0.5f, 8.f, 0.25f), Row(Gr, "MineLife", TEXT("Lasts (s)"), 1.f, 120.f, 1.f),
				Row(Gr, "SlowAmount", TEXT("Slows enemies by"), 0.f, 0.95f, 0.05f, Percent()), Row(Gr, "SlowTime", TEXT("Slow time (s)"), 0.f, 15.f, 0.25f) });
			break;
		case EWWOGrenadeKind::Spike:
			S.Rows.Append({ Header(TEXT("Poison spike (damage = the direct hit)")), Row(Gr, "PoisonDps", TEXT("Poison damage per second"), 0.f, 200.f, 1.f),
				Row(Gr, "PoisonTime", TEXT("Poison time (s)"), 0.5f, 20.f, 0.5f), Row(Gr, "SpikeLife", TEXT("Bounces around for (s)"), 1.f, 20.f, 0.5f),
				Row(Gr, "SeekRadius", TEXT("Seeks targets within (m)"), 0.f, 20.f, 0.5f), Row(Gr, "SeekStrength", TEXT("Seek turn rate (degrees/s)"), 0.f, 720.f, 10.f) });
			break;
		default:
			break;
		}
		Sections.Add(MoveTemp(S));
	}
}

void UWWOSettingsMenu::MarkPreferences()
{
	for (FWWOSettingSection& S : Sections)
	{
		const TArray<FName>* Ids = S.Key.StartsWith(TEXT("global/")) ? OwnSettings().Find(S.Title) : nullptr;
		if (!Ids) continue;
		for (FWWOSettingRow& R : S.Rows)
		{
			if (R.Header.IsEmpty() && (Ids->Num() == 0 || Ids->Contains(R.Id))) R.bPref = true;
		}
	}
}

bool UWWOSettingsMenu::SectionAllowed(const FWWOSettingSection& S) const
{
	if (!bRestricted) return true;
	for (const FWWOSettingRow& R : S.Rows) if (R.Header.IsEmpty() && R.bPref) return true;
	for (const FWWOMenuButton& B : S.Buttons) if (B.bPref) return true;
	return false;
}
