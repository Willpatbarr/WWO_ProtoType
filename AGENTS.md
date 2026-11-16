# What Was Ours: agent guide

First-person shooter in Unreal Engine 5.8, all C++ (no Blueprint logic, UMG built in code). PvE waves
and LAN PvP up to 6v6 on a listen server. Developed on an Apple Silicon Mac, also packaged for Windows.

This file is the only doc loaded into every session. Keep it short: put detail in `docs/` and link it here.

## Read before you change something

| Working on | Read |
|---|---|
| Anything, first time | `docs/ARCHITECTURE.md` (10 minutes, the whole map) |
| Writing or moving code | `docs/CONVENTIONS.md` |
| Adding a weapon, ability, enemy, map, setting, HUD element, menu or test | `docs/RECIPES.md` |
| Running or adding tests, command-line switches | `docs/TESTING.md` |
| Multiplayer, replication, host checks | `NETWORKING_HANDOFF.md` |
| Frame rate, rendering settings | `docs/PERFORMANCE.md` |
| Planned refactors and known structural debt | `docs/CODE_REVIEW_2026-11.md` |
| Why a feature is the way it is (dated notes per feature) | `HANDOFF.md` (history: search it, don't read it whole) |

## Build and run

`UE` is the engine folder (on Will's Mac: `/Volumes/UnrealSSD/Epic Games/UE_5.8`), `PROJ` the full path
to `WhatWasOurs.uproject`.

- Close a running game first, always: `pkill -f "WhatWasOurs.uproject.*-game"`
- Build: `"$UE/Engine/Build/BatchFiles/Mac/Build.sh" WhatWasOursEditor Mac Development -project="$PROJ"`
- Run a map: `"$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$PROJ" /Game/WWO/Maps/L_Arena -game -windowed -ResX=1600 -ResY=900`
- Scripted test run: add `-WWOAutoTest` (or a narrower switch, see `docs/TESTING.md`) and `-abslog=<file>`.
  It ends with `AutoTest result: PASS` or `FAIL (n)` and exit code 1 on failure. Screenshots: `Saved/AutoTest/`.
- PvP bot match: `L_Arboretum -game -windowed -WWOHostPvP -WWOBots=11 -WWOStress -WWOStressTime=90`
- Windows: `build_windows.bat`, `Tools\package_win.bat`. Mac package: `Tools/package_mac.sh`.

## Where code lives (`Source/WhatWasOurs/`)

| Folder | Holds |
|---|---|
| `Player/` | Character, movement, weapon/health/ability/ult/grenade/aim-assist components, player and bot controllers, the autotest driver (`WWOAutoTest*.cpp`, `WWOTestRun`) |
| `Game/` | Game mode (waves, PvP, bots), game state (replicated match state, effect relay), player state |
| `Systems/` | Loadout subsystem (catalog, equipped kit, stat maths), world subsystem (targets, teams, damage credit), sound |
| `Weapons/`, `Flora/`, `FX/` | Grenades and perk numbers; flora effect actors; hand-built effects (no Niagara) |
| `Enemies/`, `Allies/` | PvE AI (Soldier, Swarm, Tank), AI teammates |
| `Characters/` | Body component, native anim instances, procedural flora grafts |
| `UI/` | HUD (immediate-mode paint), settings, character, launch, scoreboard, firing tester |
| `Net/` | Sessions, travel, the map table |
| `Data/` | Data asset classes |

Tuning data: data assets in `Content/WWO/Data`, JSON in `Config/` (`WeaponMods`, `WeaponModels`,
`CharacterModels`, `Sounds`, `ShippedSettings`, `ShippedKeybinds`, `DefaultLoadout`). Asset and map
build scripts: `Tools/`.

## Rules that are easy to break

1. **Units.** Tunables are metres, seconds, degrees. Multiply by `WWO::M` where a value meets the engine.
   Colours are written sRGB and converted with `WWO::Lin` where they meet a material or Slate.
2. **Settings bind by property name.** A slider is a `float` `UPROPERTY` found by name, and that name is
   the key in `Config/ShippedSettings.json` and every player's saved file. Renaming one silently drops
   saved values. Booleans and counts are floats (`>= 0.5f`, `RoundToInt`).
3. **The host decides.** Gameplay state changes only under `HasAuthority()`. A joiner predicts, the host
   confirms or refuses. "Host" means authority, "joiner" means `NM_Client`. A bot is locally controlled
   but not player controlled.
4. **Damage needs an attacker scope.** Wrap host-side hits in `FWWODamageBy`, or kill credit, hit markers
   and damage numbers are wrong. Spawned kit (grenades, pools, fog) must set Owner or Instigator, or the
   loadout and team lookups resolve the wrong player.
5. **Movement must stay deterministic.** `SimulateMove` (`Player/WWOCharacterMove.cpp`) reads only `MoveIn`
   and the move state (`AWWOCharacter::MoveState`). New movement state is a field in `FWWOMoveState` plus a
   line in its `Serialize` (and `ToString`); see `docs/CONVENTIONS.md`.
6. **Don't disturb gun feel.** Recoil, ADS pivot, kick and burst timing in `UWWOWeaponComponent`
   (`WWOWeaponFiring.cpp`, `WWOWeaponView.cpp`, and the call order in `UpdateWeapon`) and
   `AWWOCharacter::AddRecoil` / `UpdateRecoil` (`WWOCharacterRecoil.cpp`) were matched to footage frame by
   frame. Refactors there must be pure moves.
7. **Shots trace `ECC_Visibility`.** Player, enemy and ally capsules ignore it; hitboxes take shots.
8. **Effects stay out of the navmesh.** New effect actors opt out of navigation as `AWWOFXActor` does.
9. **Tests are part of the contract.** Autotests grep log lines (`AutoTest ... FAIL`, `Net:`) and call
   menu pages by title. A failure counts only if the line has both `AutoTest` and `FAIL`.
10. **Tests leave `Saved/SaveGames/Loadout.sav` as they found it, through the loadout guard**
    (`WWOTest::GuardLoadout`, automatic for `-WWOAutoTest...` runs). Don't hand-write a backup.
11. **Unity builds are off** and files reuse helper names in anonymous namespaces. Shared helpers go in
    a named namespace in a shared header, not a second copy.
12. **Not in git:** the Fab packs (`.gitignore` lists them). Code must degrade without them.

## Working agreements

- Look for an existing helper before writing one: `WWOTypes.h` (`WWO::` maths and colour),
  `UWWOWorldSubsystem` (targets, teams, `CanHurt`, `PlayerOf`), `UWWOLoadoutSubsystem` (`FindById`, `M()`),
  `WWOFX` (shapes, lights, materials), `WWOConfig` (JSON config files), `WWOSwitches` (command-line
  switches), `WWOUI` (menu palette, text, buttons, rules: every menu builds with it, in its own tree). `docs/CONVENTIONS.md` lists them.
- A feature that changes how a system works updates that system's section in `docs/`, in place. Dated
  "what I did" notes go in the commit message, not in a doc.
- Verify with a build and the narrowest autotest that covers the change; say what you ran.
