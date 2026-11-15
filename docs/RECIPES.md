# Recipes

What the code requires today to add each kind of thing. Paths are relative to `Source/WhatWasOurs/`
unless they start with `Config/`, `Content/` or `Tools/`. Where a recipe has many steps because
behaviour is keyed by id in several files, `CODE_REVIEW_2026-11.md` has the planned fix.

## A weapon

1. A `UWWOWeaponData` asset in `Content/WWO/Data` (primary asset type `WWOWeapon`): unique `Id`, `Order`,
   `bUnlocked`. `Tools/create_burst_rifle.py` shows how to copy an existing one.
2. An entry in `Config/WeaponModels.json`: mesh, scale, rotation, offset from the sight point, muzzle,
   an anchor per mod slot, the `hand` grip. Without one the gun falls back to a box model.
3. Optional mods in `Config/WeaponMods.json` with `"frame": "<Id>"`. A weapon is a frame as soon as the
   file has mods for it.
4. A `fire_<Id>` event in `Config/Sounds.json` (else it plays `fire`).
5. Its Settings section appears by itself (`weapon/<Id>`).
6. Id branches to check: the box-model fallback (`ModelParts`, `Player/WWOWeaponComponent.cpp`), the
   pistol hand pose (`bHandsPistol`, same file; `Aim_Pistol` in `Player/WWOCharacter.cpp`), and
   iron-sight marks (`Data/WWOData.h`).
7. Test: `-WWOAutoTestGuns` and `-WWOAutoTestFrames` on `L_TestRange`.

## A weapon perk

1. Its numbers in `Weapons/WWOPerks.h`.
2. Its effect in `UWWOWeaponComponent`: `Shoot` (scales the shot), `RefreshStats` (stat changes while
   active) or `OnShotResult` (triggers on hits and kills).
3. Its HUD counter in `PerkStatus`.
4. The host's mirror in `Player/WWOCharacterGunNet.cpp` (`GunLimits`, `CheckGunHit`), or the host will
   cap a joiner's damage below what the perk allows.
5. A mod carrying it in `Config/WeaponMods.json`. Perks are keyed by name, so any frame can carry one.

## An ability or a grenade

1. A `UWWOAbilityData` or `UWWOGrenadeData` asset with its `Flora` set.
2. Grenade: pick an `EWWOGrenadeKind` (`WWOTypes.h`). A new kind needs branches in
   `Weapons/WWOGrenade.cpp`: `Setup`, `Tick`, `OnBounce`, `Explode`.
3. Ability: behaviour is wired by id. `Equipped("<id>")` in `Player/WWOAbilityComponent.cpp` (dispatch,
   `IsEffectActive`), and wherever else it acts (grep an existing id such as `static_charge` to see the
   places).
4. The look on the body: a branch in `Characters/WWOBodyGrafts.cpp` (`F.Has("<id>")`).
5. Text: a line in `UWWOCharacterMenu::BuildOverview` (the Loadout tab's Build notes), and per-ability
   rows in `UWWOSettingsMenu::RegisterSections`.
6. Bots: a hint in `AWWOBotController::UseKit` if they should use it.
7. An `ability` / `grenade_throw` sound plays by default.
8. Test: `-WWOAutoTest` sets off every ability and throws every grenade.

## An enemy type

1. An `AWWOEnemy` subclass in `Enemies/WWOEnemyTypes.*` overriding the hooks it needs (`Setup`, `Think`,
   `Animate`, `AnimateRemote`, `OnHit`, `OnDeath`...), and a `UWWOEnemyData` subclass with its asset.
2. A data pointer and cast in `UWWOLoadoutSubsystem::LoadAllData` and `NewCopy`.
3. The name-to-class branch in `AWWOGameMode::SpawnEnemy`, and rows in `Waves()`.
4. A `Config/CharacterModels.json` key if it has its own model; set `Body->ModelKey` before `BeginPlay`.
5. Spawn points: `ATargetPoint`s tagged `EnemySpawn` plus the kind's name.
6. What joiners see goes in `AnimateRemote`, from replicated state.
7. Test: `-WWOWaveTest`.

## A map

1. The level: one `PlayerStart`, `ATargetPoint`s tagged `EnemySpawn` (add `PvEOnly` to keep one out of
   PvP respawns), a `NavMeshBoundsVolume`, optionally a `TargetPoint` tagged `KillZ`.
   Generated maps are built by scripts in `Tools/` from a geometry JSON; change the script, not the map.
2. A row in `UWWONetSubsystem::Maps()` (`Net/WWONetSubsystem.cpp`): level, title, blurb, PvP, PvE, team
   size. The launch screen and the Maps page read this table.
3. `+MapsToCook` in `Config/DefaultGame.ini`.
4. Test: `-WWOAutoTestMaps` on the map, then a bot match with `-WWOPerfTest`.

## A settings slider

1. A `UPROPERTY(EditAnywhere) float` on the class that uses the value.
2. One `Row(Obj, "PropertyName", label, min, max, step, Show)` line in the right block of
   `UWWOSettingsMenu::RegisterSections`. Saving, reset and search then work by themselves.
3. If joiners should have it (a player's own preference), add its id to `OwnSettings()`.
4. A new section also needs its title in the `Layout` table in `Prepare`, or it lands under Player.
5. To show it in the firing tester, add the id to `GunGroups` / `CameraIds` in `UI/WWOFiringTester.cpp`.
6. If the shipped default should change, update `Config/ShippedSettings.json`.

## A HUD element

1. A block in `UWWOHUDWidget::NativePaint` drawing through `P.Box` / `Line` / `Text` / `Tri`, in
   1600 x 900 units. Paint order is z-order.
2. Animated values are members eased in `NativeTick`.
3. Tunables are float properties on `AWWOCharacter`, exposed as settings rows under HUD.
4. Read game state; don't change it from paint.

## A menu

1. A `UUserWidget` built in code, with `Open` / `Close` / `Back` / `IsOpen`, `IsFocusable = false` on
   every button, the OS cursor hidden and the ring cursor painted in `NativePaint`.
2. In `AWWOPlayerController`: create it, `AddToViewport` with a z-order, and add it to `IsMenuOpen` /
   `IsOtherMenuOpen`, `HandleMenuPress`, `CloseMenus` and the scoreboard's close list.
3. A text box must hand keyboard focus back to the viewport when done.

## An input action

1. A value in `EWWOAction` (`WWOTypes.h`).
2. A row in `AWWOPlayerController::BindRows()` if players can rebind it, or a `Map()` line in `MapKeys`
   for a fixed key. Add it to the menu list in `BuildInput` if it must work while paused.
3. Gameplay polls it with `IsHeld` / `WasJustPressed`. Give `AWWOBotController` the same action if bots
   should use it.
4. Update `Config/ShippedKeybinds.json` if the shipped default should include it.

## A sound

1. A `.wav` in `Tools/audio_src`, then `Tools/import_sounds.py`.
2. An event in `Config/Sounds.json`: sounds, volume, pitch range, distance (0 = flat only).
3. A hook beside the matching visual effect: `UWWOSound::Play` / `PlayFlat` / `PlayFor`. An event missing
   from the file is silent, so the hook can go in first.

## A scripted test

1. A new `Player/WWOPlayerControllerAuto<Name>.cpp` with an `UpdateAuto<Name>Test`.
2. Its state and a `bAuto<Name>` flag in `WWOPlayerController.h`, a switch read in `BeginPlay` with
   `WWOSwitches::Has(TEXT("WWOAutoTest<Name>"))`, a branch in `Tick`.
3. Checks go through `WWOTest::Check(TEXT("<name>"), bOk, What)` (it logs `AutoTest <name>: ... ok` or
   `... FAIL`); timed steps through `WWOTest::Crossed(Before, Now, T)`; end with `WWOTest::Exit`.
4. Screenshots through `Shot("<NN>_<name>")`, with a number prefix no other test uses.
5. Change the loadout freely: the run's loadout guard (`WWOTest::GuardLoadout`, already called for every
   `-WWOAutoTest...` switch) puts `Saved/SaveGames/Loadout.sav` back when the game closes. Don't write
   your own backup. If the switch has no `WWOAutoTest` in it, call `WWOTest::GuardLoadout()` yourself first.
6. Add the switch to the comment table in `WWOSwitches.h` and to the table in `TESTING.md`.

## A JSON config file

1. The file in `Config/`, and its name in the list in `WhatWasOurs.Build.cs` so packaged builds stage it.
2. Read it with `WWOConfig::Load(WWOConfig::Path(TEXT("<Name>.json")))`, or `WWOConfig::LoadCached` if
   it is asked for often and a restart to pick up edits is fine. A player's own file with a shipped
   default: `WWOConfig::LoadSavedElseShipped` and `WWOConfig::Save` with `WWOConfig::SavedPath`.
3. Parse the object's fields in the system that owns them; use `WWOConfig::Vec` / `Color` for vectors
   and colours.
