# What Was Ours: Unreal port handoff

The Godot gray-box prototype (`~/Documents/WhatWasOurs`, working copy as of 2026-11-04) rebuilt in
Unreal Engine 5.8 as a C++ project. Same systems, same tuning, Unreal-native structure.

## Run it
- Open `WhatWasOurs.uproject` in UE 5.8 (it will build the C++ module if needed), press Play.
  Starts on `L_Arena`. Maps: `Content/WWO/Maps/L_Arena`, `L_ArenaLarge`, `L_TestRange`
  (switch in Settings -> Maps).
- Standalone: `UnrealEditor WhatWasOurs.uproject /Game/WWO/Maps/L_Arena -game -windowed -ResX=1600 -ResY=900`
- Menus: Tab / Esc / Options open Settings; I / touchpad / Create open Character. In Play-in-Editor,
  Esc goes to the game while its viewport has focus (the editor would stop PIE): stop with the
  toolbar's Stop button.
- `-WWOAutoTest` plays a scripted run (fire, ADS, grenade, melee, ult, both menus, then walk / crouch /
  sprint / slide / stop, logging speeds as `AutoTest move:`; then, by the nearest crate with damage off, a
  standing jump, a double jump and a hop onto the crate, logging arcs as `AutoTest jump:`; then cycles
  through every weapon, firing each, and logs hand-cannon kick and recovery, a reload, the sniper scope
  and the rifle's fire rate as `AutoTest gun:`; then throws every grenade in turn at a training target
  (the Blinding Grenade and Snare Mine at an enemy), logging each throw and what it did as
  `AutoTest grenade:` / `Grenade <id>:`, with blast, after and throw-arm screenshots; then a melee lunge onto the
  target, a punch on an enemy (knockback) and a swing at nothing, as `AutoTest melee:`; then equips every
  ability in turn and sets it off, and casts each flora's ult (Blast, Veil, Entrench), logging
  `AutoTest ability:` / `AutoTest ult:` with 23_ability_* and 24_ult_* screenshots, and puts the loadout back;
  then drives both menus by injected DualSense input alone (Options, L1/R1, the left stick steering
  the cursor onto buttons, cross clicks and slider drags, right-stick scrolling, circle, touchpad) plus
  Tab / Esc / I, logging `AutoTest menu: ... ok|FAIL` with 25_menu_* to 29_menu_* shots) and saves
  screenshots to `Saved/AutoTest/`. `-WWOAutoTestMenus` runs only the menu steps (about 30 s).
- `-WWOWaveTest` plays all 8 waves with god mode on (about 4.5 minutes): it moves you to a random
  reachable spot at the start and middle of each wave, lets each wave fight for 28 s (the teammates
  shoot back), then kills the rest. It logs a navmesh reachability map, every enemy's and teammate's
  state each 3 s, and flags anyone stuck (`WaveTest STUCK`, with what's in the way and its path), stuck
  in the air, or not facing its target; ends with a `WaveTest done:` summary and `wave_*.png` shots.
  Code: `Game/WWOGameModeWaveTest.cpp`.
- Console: `wwo.SkipWave` kills the current wave; `wwo.Wave <n>` jumps straight to wave n.

## Where things are
| Godot | Unreal |
|---|---|
| player.gd | `Player/WWOCharacter` + `WWOCharacterMovement` (CharacterMovementComponent subclass: it runs the character's movement rules right before each move, takes horizontal velocity and gravity from them, climbs slopes like Godot's move_and_slide, crouches from the feet, and steps up only 15 cm like Godot's capsule; falls without a terminal velocity in substeps no longer than Godot's 60 Hz step; the player stands on enemies and allies instead of Unreal's jump-off bounce; floors and collision are Unreal's) |
| gun.gd, aim_assist.gd | `Player/WWOWeaponComponent`, `WWOAimAssistComponent` (viewmodel uses Unreal First Person Rendering for its own FOV) |
| player_health / ability_manager / grenade_thrower / ult_manager | `Player/WWOHealthComponent`, `WWOAbilityComponent`, `WWOGrenadeComponent`, `WWOUltComponent` |
| grenade.gd | `Weapons/WWOGrenade` (ProjectileMovementComponent, plus Godot's rigid-body damping and contact friction with spin, so it bounces and rolls like the Godot ball) |
| characters/fp_arm.gd | First-person arms in `Player/WWOWeaponComponent`, moved by `Player/WWOFPArmPaths.h` (baked from the Godot UAL animations by `Tools/bake_fp_arm_paths.py`) |
| loadout.gd + modifiers.gd | `Systems/WWOLoadoutSubsystem` (GameInstance subsystem, SaveGame slot "Loadout") |
| groups, TargetManager, DamageNumbers | `Systems/WWOWorldSubsystem`, `WWOTargetable` interface |
| enemies/*.gd | `Enemies/WWOEnemy`, `WWOEnemyTypes` (Soldier, Swarm, Tank), `WWOEnemyProjectile` |
| allies/ally.gd | `Allies/WWOAlly` |
| wave_manager.gd | `Game/WWOGameMode` (navmesh built at runtime from the map's NavMeshBoundsVolume; one nav agent sized like Godot's: 0.5 m radius, 1.8 m tall. Effects (`AWWOFXActor`, enemy shots) are kept out of it: Unreal adds every new mesh component to the navigation octree, and the constant tile rebuilds around shots and tracers left holes in the navmesh. Training targets and bark walls are in it, so paths go around them; Godot's baked navmesh has neither) |
| flora/*.gd | `Flora/WWOSporeFog`, `WWOFloraEffects`; `FX/WWOFX` (blasts, particles, lightning) |
| character_model.gd | `Characters/WWOBodyComponent` (Manny + template AnimBPs, tint, grafts) |
| hud.gd, settings_menu.gd, character_menu.gd, menu_cursor.gd | `UI/` (UMG built in C++) |
| data/*.tres | Data assets in `Content/WWO/Data` (Asset Manager primary assets) |
| tools/build_*.gd | `Tools/create_assets.py`, `Tools/build_maps.py` (run with UnrealEditor-Cmd -run=pythonscript) |

## Conventions
- Tunables keep Godot units (meters, seconds, degrees), so slider values match; code converts
  with `WWO::M` (100 cm per m). Godot (x, y, z) = Unreal (-z, x, y) * 100.
- Colors are written as sRGB like Godot's; materials, HUD, menus and the mannequin tint convert to
  linear (`WWO::Lin`).
- UI scale: Godot's canvas_items stretch with aspect "expand" on 1600 x 900. Project Settings use
  the ScaleToFit rule at 1920 x 1080, so menus are laid out in Godot pixels x 1.2 (`UWWOSettingsMenu::K`);
  the HUD paints in Godot pixels scaled by min(width / 1600, height / 900). Godot's default font
  (Open Sans SemiBold) is Roboto Medium here; font sizes are Godot pixels (x 0.75 for Slate points).
- Settings sliders bind to float UPROPERTYs by name. "Save as defaults" writes
  `Saved/WWO/SavedSettings.json`, applied on startup.
- Your Godot `saved_settings.json` values are baked into the data assets and C++ defaults; your
  Godot loadout is `Config/DefaultLoadout.json` (used on first run).
- Shots and sight lines trace `ECC_Visibility`. The player's capsule (Unreal's Pawn profile) ignores
  that channel, so whatever should hit you or be blocked by you (enemy shots, a teammate's view of an
  enemy) also tests the player's capsule directly; enemies are hit through their hitboxes, teammates
  through their capsules.

## Regenerating
- Data from Godot: `python3 Tools/tres_to_json.py ~/Documents/WhatWasOurs Tools/wwo_data.json`
- Then `Tools/create_assets.py` (materials + data assets) and `Tools/build_maps.py` (delete
  `Content/WWO/Maps/*.umap` first).

## Differences from the Godot version
- Mannequin animations come from the UE template: no crouch, slide, hit-flinch or Spell anims;
  the ult uses the template's attack anims, Swarm isn't hunched.
- Flora grafts are simplified gray-box pods, caps and plates (not the procedural vines/mushrooms/bark).
  Frame part region colors show on the first-person arm only.
- Effects are C++ shapes and instanced-mesh particles, not Niagara.
- The first-person punch and throw arms are simple limbs (cylinders and a fist block) instead of the
  mannequin's arm, but they follow the Godot animations' shoulder, elbow, wrist and knuckle paths
  (Melee_Hook, OverhandThrow) with the same timings, placement and arm FOV.
- Wall Grenade walls stand on the ground under the blast. In Godot the ground ray also hits the
  grenade itself (it isn't excluded), so walls there start at the grenade's top: 24 cm up when it
  rests on the floor, higher if it goes off mid-bounce. Probably a Godot bug; not copied.
- Arena layout is identical but built from prototype-grid blocks instead of the Quaternius kit.
- Input actions are created in code (Enhanced Input), not as assets. The character menu is on I,
  the DualSense touchpad click (Godot's button; Unreal's Mac controller code reports it as
  Gamepad_Special_Left) and also the Create button, which Unreal doesn't read on Mac, so it's
  polled from GameController directly (`FWWOMenuKeys`).
- Settings: the Global tab bar wraps onto two rows; Godot keeps one row with scroll arrows.
  Buttons keep the port's rounded style rather than Godot's default theme. The OS pointer is
  hidden over the menus, which draw Godot's ring cursor, as in Godot; the mouse moves it too.
- The character preview's background is a flat backdrop in the panel color (Godot's SubViewport
  is transparent).
- Jumps peak at exactly the set height (the Godot formula's intent). Godot's 60 Hz step adds about
  5 cm (2.05 m for a 2 m jump) and about 0.02 s of air time.
- Right-stick look Y has a Negate in the look mapping: Will saw it inverted without one. (Mac's
  GameController reports stick-up as +Y, which would need no flip, so recheck if it ever feels inverted again.)
- Mouse look ignores the first 2 s of play and, for 0.25 s after a menu closes, single jumps over
  250 px (the cursor recapture). Godot has no such filter. Mouse pixel scale isn't matched to
  Godot's canvas_items stretch, so mouse sensitivity may differ.
- Controller rumble counts down in game time, so a melee hit-stop (a few hundredths of a second at
  0.03 speed) stretches that buzz by about the hit-stop's length. Godot's rumble runs in real time.
- Wave enemies retry their random spot near the spawn marker (up to 8 times) when it's inside a crate
  stack or off the navmesh; Godot drops them wherever the offset lands. Their feet go on the marker
  (the actor is the capsule's center).
- An enemy whose target can't be reached on the navmesh stops at the closest spot when a wall or rail
  is right in front, instead of pressing into it. A Soldier that's too close with a wall right behind
  it strafes instead of backing into the wall. Something on a nav path that isn't in the navmesh is
  sidestepped along its face instead of walked into head-on.
  An enemy that keeps trying to walk the same way for 0.75 s without getting anywhere (wedged on a
  door edge, in a gap between crates, behind another enemy) steps sideways for 0.6 s, alternating sides.
- A teammate getting back up lands 2 m to your right as in Godot, but stops short of a wall instead of
  being put on the far side of it. One that's closer than 3 m to you doesn't back into a wall.
- Also true in Godot (not changed): the arena ramps climb away from their raised platforms (each
  `stairs()` call's high end is on the side away from the platform), so the 2 m platforms are islands
  on the navmesh. Soldiers that spawn up there stay up there and shoot from the rails. A Soldier that
  can't see you right at its 15 m minimum range dithers in place (backs off, then walks back in).
- Not yet checked hands-on with the DualSense.
