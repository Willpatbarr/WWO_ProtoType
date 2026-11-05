# What Was Ours: Unreal port handoff

The Godot gray-box prototype (`~/Documents/WhatWasOurs`, working copy as of 2026-11-04) rebuilt in
Unreal Engine 5.8 as a C++ project. Same systems, same tuning, Unreal-native structure.

## Run it
- Open `WhatWasOurs.uproject` in UE 5.8 (it will build the C++ module if needed), press Play.
  Starts on `L_Arena`. Maps: `Content/WWO/Maps/L_Arena`, `L_ArenaLarge`, `L_TestRange`
  (switch in Settings -> Maps).
- Standalone: `UnrealEditor WhatWasOurs.uproject /Game/WWO/Maps/L_Arena -game -windowed -ResX=1600 -ResY=900`
- In the editor's Play-in-Editor, Esc ends PIE: use Tab to open the settings menu.
- `-WWOAutoTest` plays a scripted run (fire, ADS, grenade, melee, ult, both menus) and saves
  screenshots to `Saved/AutoTest/`.

## Where things are
| Godot | Unreal |
|---|---|
| player.gd | `Player/WWOCharacter` + `WWOCharacterMovement` (CharacterMovementComponent subclass: the character sets horizontal velocity and gravity; floors, steps and crouch are Unreal's) |
| gun.gd, aim_assist.gd | `Player/WWOWeaponComponent`, `WWOAimAssistComponent` (viewmodel uses Unreal First Person Rendering for its own FOV) |
| player_health / ability_manager / grenade_thrower / ult_manager | `Player/WWOHealthComponent`, `WWOAbilityComponent`, `WWOGrenadeComponent`, `WWOUltComponent` |
| grenade.gd | `Weapons/WWOGrenade` (ProjectileMovementComponent) |
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
- Arena layout is identical but built from prototype-grid blocks instead of the Quaternius kit.
- Input actions are created in code (Enhanced Input), not as assets. Character menu on the
  DualSense is mapped to the Create button and I.
- Not yet checked hands-on with the DualSense.
