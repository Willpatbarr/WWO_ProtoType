# Conventions

What the code does consistently, and what to reuse instead of rewriting. Paths are relative to
`Source/WhatWasOurs/`.

## Units and colours

- Tunables are metres, seconds and degrees (the Godot prototype's units, so slider values carried over).
  Convert with `* WWO::M` (100 cm per metre) at the point of use, not when storing.
- Pitch and recoil are radians; yaw is degrees. `ApplyRecoil` yaw is positive to the left.
- Colours are written as sRGB. Convert with `WWO::Lin` where the value meets a material or Slate.
- Menu sizes are Godot pixels x `WWOUI::K` (1.2); menu fonts are pixels x 0.75 x `K` (`WWOUI::Font`).
  Menu colours are the named ones in `WWOUI` (`PageColor`, `CardColor`, `CardEdge`, ...). The HUD paints in
  1600 x 900 units through `FWWOPainter`.

## Tunables and settings

- A tunable is a `UPROPERTY(EditAnywhere) float` on the class that uses it, exposed with one `Row(...)`
  line in one of `UWWOSettingsMenu::RegisterSections`' parts (`UI/WWOSettingsSections.cpp`).
- Booleans and counts are floats: test with `>= 0.5f`, read counts with `RoundToInt`.
- **The property name is the saved-file key.** Renaming one drops every saved value for it, in
  `Config/ShippedSettings.json` and on players' machines. If you must rename, add a migration where
  `GunKick` has one in `LoadSaved`.
- Row ids must be unique within a section.
- A new slider is host-only until its id is added to `OwnSettings()`.

## Authority and roles

- Vocabulary used in comments and logs: **host** = `HasAuthority()`, **joiner** = `NM_Client`.
- Actors check `HasAuthority()`; components check the net mode.
- A bot is locally controlled but not player controlled. "My human player" is
  `AWWOCharacter::IsLocalHuman()`; the host's copy of a joiner's character is `IsHostCopyOfRemote()`.
  "In a networked game" is `WWO::IsOnline(Actor)`.
- Gameplay state changes on the host only. A joiner's `TakeHit` / `ApplyHit` is cosmetic and returns false.
- Kit follows predict, confirm, refuse: the joiner acts at once, the host checks, `ClientKitRefused`
  undoes it and returns the charge.
- Anything the host believes from a joiner is checked against what the host can work out itself
  (`WWOCharacterGunNet.cpp`). New joiner-to-host RPCs need the same.

## Damage

- Wrap host-side damage in an `FWWODamageBy` scope.
- Spawned kit sets `Params.Owner` (or Instigator) to the character that made it.
- Ask `UWWOWorldSubsystem::CanHurt` before hurting a player; don't re-derive team rules.
- An area effect ("everyone within R of this point") is a `FWWOAreaQuery` passed to
  `UWWOWorldSubsystem::ForEachTargetInRadius` (or `NearestTargetInRadius`), never a hand loop over
  `LivingTargets` / `GetPlayers`: say where it measures, `<` or `<=`, who it includes and whether it needs a
  line of sight. "The best part inside my aim cone" is `BestAimPart` with your own score.
- Effects everyone should see: the `AWWOGameState` statics. Sounds only this machine should hear:
  `UWWOSound`.

## Movement

- `SimulateMove` reads only `MoveIn` and the move state: no other member, no world query whose answer
  could differ between machines.
- The move state is one struct, `FWWOMoveState`, held once by the character (`MoveState`; read it as
  `P->MoveState.bSliding`). `GetMoveState` / `SetMoveState` copy it whole, and the saved moves store it whole.
  New movement state goes in two places: a field in `FWWOMoveState` (`Player/WWOCharacter.h`), and a line in
  `FWWOMoveState::Serialize` (`Player/WWOCharacterMove.cpp`; the wire: a missing line is a silent desync after
  corrections). Add it to `ToString` too, for the correction logs. Timers, input edges and easing that a correction need not restore
  (camera, bob, lunge steering, status timers) stay loose members.
- Movement values exchanged with the host (`ServerSetMoveStats`, `ClientSetMoveBase`, `BaseMoveStats`) come from
  `MoveStatTable` in `WWOCharacterMove.cpp`: one line each (member, loadout stat, host clamp low / high), in wire order.
  Add new ones at the end (tests read the array by index).
- The joiner's `MoveIn` is quantised (`Quantize`) so both machines simulate the same numbers.
- Movement ticks before the character; `PreparedFrame` guards against preparing twice in a frame.

## Time

- Gameplay uses accumulators in `Tick` / `UpdateX` (`Age`, `...Left`), not timers.
- UI uses real time (`FPlatformTime`), because menus pause the game. Tests use `FApp::GetDeltaTime`.
- Easing toward a value is frame-rate independent: `Lerp(Value, Target, WWO::Damp(Rate, dt))`, or
  `WWO::MoveToward`.

## Logging

- `LogWWO`, Display level, plain sentences. Prefixes the tests grep: `AutoTest`, `Net:`, `NetShot`,
  `NetMelee`, `PerfTest`, `WaveTest`, `Bots:`.

## Files and names

- Classes are `WWO`-prefixed (`AWWO...`, `UWWO...`, `FWWO...`); free helpers live in `namespace WWO` or a
  system namespace (`WWOFX`, `WWONav`, `WWOTest`, `WWOConfig`, `WWOSwitches`, `WWOUI`).
- Unity builds are off (`WhatWasOurs.Build.cs`) because files reuse helper names in anonymous namespaces.
  A helper needed in two files moves to a shared header under a namespace.
- A large class is split across `.cpp` files by concern (`WWOAutoTest*.cpp`, `WWOCharacter*.cpp`,
  `WWOWeapon*.cpp`, `WWOBodyGrafts.cpp`), not by making the header bigger.
- Platform-only code sits behind `PLATFORM_MAC` and the like. MSVC is stricter than clang: initialise
  out-pointers, one static member per declaration in exported classes.

## Helpers that already exist

Look here before writing a new one.

| Need | Use |
|---|---|
| Move a value toward a target, smoothstep, lighten / darken, sRGB to linear, alpha, random direction in a cone | `WWO::MoveToward`, `MoveToward2D`, `SmoothStep`, `Lightened`, `Darkened`, `Lin`, `WithAlpha`, `RandomInCone` (`WWOTypes.h`) |
| A random number that decides gameplay (a shot, a kick, an AI choice) | `WWO::FRand`, `FRandRange`, `RandRange` (`WWOTypes.h`): `FMath`'s in play, a seeded stream in the regression scenarios. Looks-only randomness uses `FMath` |
| Frame-rate independent easing alpha, ease out with overshoot | `WWO::Damp(Rate, dt)`, `WWO::BackOut(T)` (`WWOTypes.h`) |
| Spawn parameters that always spawn (optionally owned, optionally deferred) | `WWO::AlwaysSpawn(Owner)`, `WWO::AlwaysSpawnDeferred(Owner)` (`WWOTypes.h`) |
| Read or write a JSON file in `Config/` or `Saved/WWO/` | `WWOConfig::Load`, `LoadCached` (once a run), `LoadSavedElseShipped`, `Save`, `Path`, `SavedPath`; field readers `Vec`, `Color` (`Systems/WWOConfig.*`) |
| A command-line switch, "is this a test run" | `WWOSwitches::Has`, `Value`, and the named predicates (`HasAutoTestText`, `IsStress`, ...). Each predicate's comment says exactly what it matches (`WWOSwitches.h`) |
| This machine's player; every player and bot | `UWWOWorldSubsystem::GetPlayer()`, `GetPlayers()` |
| Living targets for an attacker | `UWWOWorldSubsystem::LivingTargets` |
| Everyone within a radius (measure point, `<` / `<=`, who, line of sight); the nearest such; the best aim part in a cone | `ForEachTargetInRadius`, `NearestTargetInRadius` with an `FWWOAreaQuery`; `BestAimPart` (`Systems/WWOWorldSubsystem.h`) |
| Team rules, the player behind an actor | `UWWOWorldSubsystem::CanHurt`, `TeamOf`, `IsPvP`, `PlayerOf` |
| A data asset by id | `UWWOLoadoutSubsystem::FindById` |
| A modified stat | `Character->M("stat_name", Base)` |
| Another character's loadout | `UWWOLoadoutSubsystem::Get(Context)` |
| An effect's age, grow-in, fade, tail and end (the host destroys) | `FWWOEffectLife` (`Flora/WWOEffectLife.h`) |
| Show an effect on every machine (from the host) | the `AWWOGameState` statics; a new one ends with `HostRelay(World)` and its multicast |
| Basic shapes, materials, lights, ground lookup for effects | `WWOFX::Sphere` / `Cube` / `AddShape` / `AddSoftSphere` / `AddGroundDisc` / `GroundBelow`, `WWOFX::AddPointLight` (any actor), `AWWOFXActor::AddLight` (`FX/WWOFX.*`) |
| A navmesh path | `WWONav::FindPath` (`Enemies/WWOEnemy.*`) |
| A timed section in the perf log | `WWO_PROF` (`Player/WWOProf.h`) |
| A regression scenario (exact numbers, compared with a golden file) | `FWWORegressScenario` and `FWWORegress`'s helpers (`Player/WWORegress.h`); `Tools/regress.sh` |
| In a scripted test: a timed step, an ok / FAIL line, a screenshot, the way out, keeping the saved loadout | `WWOTest::Crossed`, `Check`, `Shot` / `ShotPath`, `Exit`, `GuardLoadout` / `RestoreLoadout` (`Player/WWOTestRun.h`) |
| A world sound, a flat sound, a character's own sound | `UWWOSound::Play`, `PlayFlat`, `PlayFor` |
| In a menu: a label, spaced capitals, a thin line, a size box, a fill share, the accent colour, the palette | `WWOUI::Text`, `Spaced`, `Rule` / `RuleDown`, `Sized` / `Wide`, `Fill`, `Accent(this)`; `WWOUI::PageColor`, `CardColor`, `CardEdge`, `Dim`, ... (`UI/WWOUI.h`). Each takes the menu's own `WidgetTree` |
| Immediate-mode drawing in the HUD's 1600 x 900 layout (box, outline, line, triangle, vignette, circle, dot, text, measure, material); the layout's scale | `FWWOPainter` (`UI/WWOPainter.h`), `WWOHUD::Scale` (`UI/WWOHUDLayout.h`) |
| In a menu: a button, its click (plays ui_click), keeping its handler alive; a text box; fonts, rounded brushes, the logo, the ring cursor | `WWOUI::Button` / `TextButton` with a look (`FilledLook`, `CardLook`, `PlainLook`, `StillLook`), `OnClick`, `KeepPruned`; `StyleTextBox`; `Font`, `RoundBrush`, `MakeLogo`, `PaintCursor`, `K` (`UI/WWOUI.h`) |

## Load-bearing names

- Gun model part names `SightDot`, `MuzzleFlash`, `Magazine`, `Body`.
- Spawn tags on `ATargetPoint`: `EnemySpawn`, an enemy kind, `PvEOnly`, `KillZ`.
- Menu page titles and public test hooks on widgets (`ShowPage`, `AutoTestSearch`, `DebugState`): the
  autotests call them.
- Map URL options `?pvp`, `?tester`, `?bots=N`, `?Tag=`.
