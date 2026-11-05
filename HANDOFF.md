# What Was Ours: Unreal port handoff

The Godot gray-box prototype (`~/Documents/WhatWasOurs`, working copy as of 2026-11-04) rebuilt in
Unreal Engine 5.8 as a C++ project. Same systems, same tuning, Unreal-native structure.

## Run it
- Open `WhatWasOurs.uproject` in UE 5.8 (it will build the C++ module if needed), press Play.
  Starts on `L_Arena`. Maps: `Content/WWO/Maps/L_Arena`, `L_ArenaLarge`, `L_TestRange`
  (switch in Settings -> Maps).
- Standalone: `UnrealEditor WhatWasOurs.uproject /Game/WWO/Maps/L_Arena -game -windowed -ResX=1600 -ResY=900`
- In the editor's Play-in-Editor, Esc ends PIE: use Tab to open the settings menu.
- `-WWOAutoTest` plays a scripted run (fire, ADS, grenade, melee, ult, both menus, then walk / crouch /
  sprint / slide / stop, logging speeds as `AutoTest move:`; then, by the nearest crate with damage off, a
  standing jump, a double jump and a hop onto the crate, logging arcs as `AutoTest jump:`; then cycles
  through every weapon, firing each, and logs hand-cannon kick and recovery, a reload, the sniper scope
  and the rifle's fire rate as `AutoTest gun:`; then throws every grenade in turn at a training target
  (the Blinding Grenade and Snare Mine at an enemy), logging each throw and what it did as
  `AutoTest grenade:` / `Grenade <id>:`, with blast, after and throw-arm screenshots; then a melee lunge onto the
  target, a punch on an enemy (knockback) and a swing at nothing, as `AutoTest melee:`) and saves
  screenshots to `Saved/AutoTest/`.

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
| wave_manager.gd | `Game/WWOGameMode` |
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
- Settings sliders bind to float UPROPERTYs by name. "Save as defaults" writes
  `Saved/WWO/SavedSettings.json`, applied on startup.
- Your Godot `saved_settings.json` values are baked into the data assets and C++ defaults; your
  Godot loadout is `Config/DefaultLoadout.json` (used on first run).

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
- Input actions are created in code (Enhanced Input), not as assets. Character menu on the
  DualSense is mapped to the Create button and I.
- Jumps peak at exactly the set height (the Godot formula's intent). Godot's 60 Hz step adds about
  5 cm (2.05 m for a 2 m jump) and about 0.02 s of air time.
- Right-stick look Y has a Negate in the look mapping: Will saw it inverted without one. (Mac's
  GameController reports stick-up as +Y, which would need no flip, so recheck if it ever feels inverted again.)
- Mouse look ignores the first 2 s of play and, for 0.25 s after a menu closes, single jumps over
  250 px (the cursor recapture). Godot has no such filter. Mouse pixel scale isn't matched to
  Godot's canvas_items stretch, so mouse sensitivity may differ.
- Controller rumble counts down in game time, so a melee hit-stop (a few hundredths of a second at
  0.03 speed) stretches that buzz by about the hit-stop's length. Godot's rumble runs in real time.
- Not yet checked hands-on with the DualSense.
