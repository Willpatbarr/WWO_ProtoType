# What Was Ours: Unreal port handoff

The Godot gray-box prototype (`~/Documents/WhatWasOurs`, working copy as of 2026-11-04) rebuilt in
Unreal Engine 5.8 as a C++ project. Same systems, same tuning, Unreal-native structure.

## Run it
- Open `WhatWasOurs.uproject` in UE 5.8 (it will build the C++ module if needed), press Play.
  Starts on `L_Arena`. Maps: `Content/WWO/Maps/L_Arena`, `L_ArenaLarge`, `L_TestRange`
  (switch in Settings -> Maps).
- Standalone: `UnrealEditor WhatWasOurs.uproject /Game/WWO/Maps/L_Arena -game -windowed -ResX=1600 -ResY=900`
- A packaged build, or `-game` with no map and no `-WWO...` switch, opens on the **launch screen**
  (see "Launch screen" below). A map or any `-WWO` switch on the command line, and Play-in-Editor, go
  straight into the game as before; `-WWOTitle` forces the launch screen.
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
  Tab / Esc / I, logging `AutoTest menu: ... ok|FAIL` with 25_menu_* to 29_menu_* shots); then, on a floor
  built 40 m above the arena, a row of mannequins in each body state (crouch, crouch walk, slide, Soldier
  aim and hit flinch, death, knockback death, Swarm run, ult charge and cast, Tank slam warning) with a
  close-up of each, your own body in third person crouched, crouch walking and sliding, and the
  first-person throw and punch arms in slow motion, logging `AutoTest body:` with 30_body_row,
  31_body_*, 32_player_* and 33_fp_* shots; then six loadouts (two per flora, with frame parts) in the
  character menu preview, front and back (34_grafts_*) and close up (35_grafts_*), logging `AutoTest grafts:`,
  and puts the loadout back. Screenshots go to `Saved/AutoTest/`.
  Last, the level from fixed cameras (spawn, overview, platforms and ramps, halls, top), 36_map_*.
  `-WWOAutoTestMenus` runs only the menu steps (about 30 s), `-WWOAutoTestBody` only the body steps
  (about 1 minute), `-WWOAutoTestGrafts` only the graft steps (about 25 s), `-WWOAutoTestMaps` only the
  map views (about 25 s; works on every map).
- `-WWOWaveTest` plays all 8 waves with god mode on (about 4.5 minutes): it moves you to a random
  reachable spot at the start and middle of each wave, lets each wave fight for 28 s (the teammates
  shoot back), then kills the rest. It logs a navmesh reachability map, every enemy's and teammate's
  state each 3 s, and flags anyone stuck (`WaveTest STUCK`, with what's in the way and its path), stuck
  in the air, or not facing its target; ends with a `WaveTest done:` summary and `wave_*.png` shots.
  Code: `Game/WWOGameModeWaveTest.cpp`.
- Console: `wwo.SkipWave` kills the current wave; `wwo.Wave <n>` jumps straight to wave n.

## Performance
- Rendering is set up for an M4-class Mac (Config/DefaultEngine.ini): no Lumen (sky light ambient
  + screen-space reflections, matching Godot's unshadowed ambient), TAA instead of TSR, virtual
  shadow maps kept (cheaper than cascaded shadows here, thanks to Nanite). About 80 fps at
  1600x900, up from 28.
- `-WWOPerfTest` walks the map-autotest cameras, logs frame and GPU time per view
  (`PerfTest ...` lines) plus one `profilegpu` dump, then quits. Use it before and after render
  changes.

## Where things are
| Godot | Unreal |
|---|---|
| player.gd | `Player/WWOCharacter` + `WWOCharacterMovement` (CharacterMovementComponent subclass: it runs the character's movement rules right before each move, takes horizontal velocity and gravity from them, climbs slopes like Godot's move_and_slide, crouches from the feet, and steps up only 15 cm like Godot's capsule; falls without a terminal velocity in substeps no longer than Godot's 60 Hz step; the player stands on enemies and allies instead of Unreal's jump-off bounce; floors and collision are Unreal's) |
| gun.gd, aim_assist.gd | `Player/WWOWeaponComponent`, `WWOAimAssistComponent` (viewmodel uses Unreal First Person Rendering for its own FOV) |
| player_health / ability_manager / grenade_thrower / ult_manager | `Player/WWOHealthComponent`, `WWOAbilityComponent`, `WWOGrenadeComponent`, `WWOUltComponent` |
| grenade.gd | `Weapons/WWOGrenade` (ProjectileMovementComponent, plus Godot's rigid-body damping and contact friction with spin, so it bounces and rolls like the Godot ball) |
| characters/fp_arm.gd | First-person arms in `Player/WWOWeaponComponent`: the mannequin (a PoseableMeshComponent) posed bone by bone from the baked Melee_Hook / OverhandThrow, the rest of the body shrunk away into the shoulder, the shoulder on the Godot arm's path from `Player/WWOFPArmPaths.h` (baked by `Tools/bake_fp_arm_paths.py`) |
| loadout.gd + modifiers.gd | `Systems/WWOLoadoutSubsystem` (GameInstance subsystem, SaveGame slot "Loadout") |
| groups, TargetManager, DamageNumbers | `Systems/WWOWorldSubsystem`, `WWOTargetable` interface |
| enemies/*.gd | `Enemies/WWOEnemy`, `WWOEnemyTypes` (Soldier, Swarm, Tank), `WWOEnemyProjectile` |
| allies/ally.gd | `Allies/WWOAlly` |
| wave_manager.gd | `Game/WWOGameMode` (navmesh built at runtime from the map's NavMeshBoundsVolume; one nav agent sized like Godot's: 0.5 m radius, 1.8 m tall. Effects (`AWWOFXActor`, enemy shots) are kept out of it: Unreal adds every new mesh component to the navigation octree, and the constant tile rebuilds around shots and tracers left holes in the navmesh. Training targets and bark walls are in it, so paths go around them; Godot's baked navmesh has neither) |
| flora/vine_graft.gd, bark_graft.gd, spore_graft.gd | `Characters/WWOGraftBuilder`: the same procedural meshes (vines, leaves, thorns, crystal pods, bark plates, seed pods, mycelium, shelf mushrooms) as ProceduralMeshComponent sections, with the graft shaders as `M_WWO_Graft*` materials. region_color.gdshader is `M_WWO_Body` |
| flora/*.gd | `Flora/WWOSporeFog`, `WWOFloraEffects`; `FX/WWOFX` (blasts, particles, lightning) |
| character_model.gd | `Characters/WWOBodyComponent` (Manny, region tints, grafts; the grafts and frame part colors in `WWOBodyGrafts.cpp`) + `WWOBodyAnimInstance` (a native AnimInstance, no AnimBP: AnimationPlayer-style play / crossfade / speed / queue, the Soldier's upper-body aim and flinch layers, HunchModifier's bend). The Godot body animations are baked onto the mannequin skeleton as `/Game/WWO/Anims/UAL/A_<Godot name>` by `Tools/bake_ual_anims.py`, and each owner picks them by their Godot names as its Godot script does |
| hud.gd, settings_menu.gd, character_menu.gd, menu_cursor.gd | `UI/` (UMG built in C++) |
| data/*.tres | Data assets in `Content/WWO/Data` (Asset Manager primary assets) |
| tools/build_*.gd | `Tools/create_assets.py`, `Tools/import_kit.py`, `Tools/build_maps.py` (run with UnrealEditor-Cmd -run=pythonscript) |
| maps/scifi (Quaternius Sci-Fi kit), WorldEnvironment + Sun | `/Game/WWO/Kit` (imported by `Tools/import_kit.py`: the pieces the arenas use, `M_WWO_Kit` + one `MI_Kit_*` per glTF material); per-level Sun, `SkyDome` (`M_WWO_Sky`, Godot's procedural sky), real-time `SkyLight`, `HeightFog`, `PostProcess` (from `build_maps.py`) |

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
- Then `Tools/create_assets.py` (materials + data assets), `Tools/import_kit.py` (the Sci-Fi kit from the
  Godot project's `maps/scifi`, `WWO_GODOT` or `~/Documents/WhatWasOurs`) and `Tools/build_maps.py` (delete
  `Content/WWO/Maps/*.umap` first).
- Body and graft assets: `Tools/create_graft_assets.py` (UnrealEditor-Cmd -run=pythonscript; reads
  `WWO_GODOT` or `~/Documents/WhatWasOurs`): the nature pack textures and two mushrooms, the `M_WWO_Graft*`
  materials, and `M_WWO_Body` (a copy of the mannequin material whose paint tint is picked per region)
  with `MI_WWO_Body_01` / `_02`.
- Environment plants: `Tools/import_flora.py` (UnrealEditor-Cmd -run=pythonscript, with the script's full path;
  reads `WWO_FLORA_SRC` or `~/Documents/WWO_Flora/arboretum`, made from the downloaded packs in its `source/`
  folder by `~/Documents/WWO_Flora/tools/prep_arboretum_packs.py` in Blender): `/Game/WWO/Flora` (the spring
  tree as four meshes on one pivot, 13 tropical plants and a ground-leaf piece), `M_WWO_Flora` and
  `M_WWO_FloraLeaf` (masked, two-sided foliage) with a `Tint` parameter for recoloring.
- Concrete kit look: `Tools/create_kit_concrete.py` (UnrealEditor-Cmd -run=pythonscript, full script path, after
  `import_kit.py`): `M_WWO_KitConcrete` and `/Game/WWO/Kit/Materials/Concrete/MI_KitConcrete_<name>`, one per
  structural kit material. The concrete surface in `/Game/WWO/Kit/Concrete` is copied from Lyra's example
  content (the copy command is in the script's header). A builder swaps MI_Kit_<name> for
  MI_KitConcrete_<name> on the walls, floors, columns and door frames it places.
  The material weathers itself (dirt patches, leak streaks from each piece's top, grime at its foot; `Grime` 0 =
  clean) from `T_Grime_Mask`, made by `Tools/make_grime_mask.py` (Blender's Python) into `Tools/art`.
  `MI_KitConcrete_M_Glass` (`M_WWO_KitGlass`) is see-through dusty glass for the kit's windows, whose own glass
  is opaque. `import_kit.py` now imports all 158 pieces of the free kit, not only the arenas' 21.
- Arboretum map (`L_Arboretum`, 4v4 PvP): layout in `~/Documents/WWO_PvP_Mockups/arboretum_map.py` (mockup PNG +
  `arboretum_map_geometry.json`, the source of truth; Will approved it), turned into boxes by
  `Tools/arboretum_layout.py` (run it alone to check the floor and the east/west mirror) and built by
  `build_arboretum()` in `Tools/build_maps.py`: delete `Content/WWO/Maps/L_Arboretum.umap`, then
  `WWO_MAPS=arboretum UnrealEditor <uproject> -ExecutePythonScript=<full path>/Tools/build_maps.py -unattended`
  (the full editor, so the boxes merge; don't add `WWO_QUIT=1`, the double exit crashes the editor on its
  way out). Needs `create_kit_concrete.py` and `import_flora.py` first. Concrete boxes merged per kind into
  `/Game/WWO/Maps/Arboretum`, glass greenhouse roofs and center skylight, kit crates, red plants on the
  planters, the spring tree, a tunnel 4 m under the map (stairs in Propagation and Lab A, two spiral ramps
  in the center room), warm point lights. It is in the Maps menu and `MapsToCook`.
- Body animations: `Tools/bake_ual_anims.py` (UnrealEditor-Cmd -run=pythonscript; reads the UAL glb files
  from the Godot project, `WWO_GODOT` or `~/Documents/WhatWasOurs`). It retargets each clip in mesh space:
  the UAL rig rests in a T-pose with three spine bones, Manny in an A-pose with five; mapped bones take the
  UAL bone's world rotation change after a fixed rest alignment, the pelvis moves scaled by the pelvis
  heights, and Manny keeps its own bone lengths.

## Cascade map (L_Cascade, 6v6 PvP + PvE): built and played
- Design and source of truth: `~/Documents/WWO_PvP_Mockups/HANDOFF_Cascade.md` and
  `cascade_map_geometry.json` (made by `cascade_map.py` there; change the layout in that script and re-run it,
  never hand-edit the JSON). `Cascade_Plan.png` shows it. The layout is at 1.25x plan scale (`S` in `cascade_map.py`; heights
  and player-sized pieces unchanged), so its sections come from `Tools/preview_cascade.py`, not the old hand-drawn `Cascade_Sec_*.png`.
- `Tools/blender_cascade.py` (run with Blender -b --python) makes a low-poly 1:1 blockout, `cascade_build/Cascade_Blockout.blend`.
- `Tools/cascade_layout.py` (plain Python) turns the JSON into boxes. Run it alone to check the layout
  (exit 0 = good): `python3 Tools/cascade_layout.py`. It checks 180-degree point symmetry (the map is not
  mirrored), deck coverage, counts against the JSON, bridge ends, floating pieces, spawns, the hall and
  balcony doors, the tunnel windows and the ring beside the hub stairs.
- `python3 Tools/preview_cascade.py` (needs matplotlib) draws the plan and the seven sections from the
  boxes into `~/Documents/WWO_PvP_Mockups/cascade_build/`. Set them beside the mockup's images.
- `Tools/build_cascade.py` builds it; `Tools/build_maps.py` calls it for `WWO_MAPS=cascade`. It needs
  `create_kit_concrete.py` and `import_flora.py` first, plus the Arboretum's helpers in `build_maps.py`
  (`concrete_instance`, `lamp_instance`, `fit_mesh`, `merge_kind` with base/prefix). Close the game and
  editor, delete only `Content/WWO/Maps/L_Cascade.umap` (the merged meshes in `Content/WWO/Maps/Cascade` are
  remade), then `WWO_MAPS=cascade UnrealEditor <uproject> -ExecutePythonScript=<full path>/Tools/build_maps.py
  -unattended` (the full editor, so the boxes merge). Then `-WWOAutoTestMaps` on `L_Cascade` gives nine views
  (`36_map_cascade_*`).
- Gameplay hooks: a `TargetPoint` tagged `KillZ` makes falling kill at z -9 (UWWOHealthComponent).
  `EnemySpawn` points tagged `PvEOnly` are for the waves only, not PvP respawns. 6v6 needs nothing new:
  PvP takes 12 players and up to 11 bots.
- Built and played (2026-11-11): solo and a 6v6 bot match run at about 87 fps. Cascade has its own sky,
  `M_WWO_Sky_Cascade` (M_WWO_Sky's shader with a clear blue; `SKY_TOP`/`SKY_HORIZON` in build_cascade.py,
  remade every build), and tints its waterfalls through `MI_Waterfall_Cascade` (`WATERFALL_COLOR`).
- Looks pass (2026-11-11, all in `build_cascade.py`, constants at its top): the sky light is **movable** and
  tinted warm (`SKY_LIGHT`; as a stationary light it gave nothing without built lighting, so every shaded
  face was black); the sun is turned `SUN_YAW` off the axes; lighter concrete, the far arches and mesas pale
  (`Cas_Far`); a lit glossy teal water plane 6 km across (`M_WWO_CascadeWater`, `WATER_COLOR`); three mist
  puffs per waterfall (`MI_Cascade_Mist*`, spheres in `M_WWO_SoftAlpha`); room lights at `LIGHT_CANDELAS`
  200, pulled toward white; muted spawn pads; bloom, vignette, contact shade, a little saturation and
  contrast in the post process. Frame time with 12 players went from about 10.2 to about 10.7 ms.
- Still rough: shadows on the floors are strongly blue (sky-lit only: no bounce light in this project);
  the mist puffs read as soft balls from above; interiors are evenly lit by the sky light (nothing shades
  it indoors). Plants are actors, not instances (about 150).

## Differences from the Godot version
- Bodies are the UE mannequin playing the Godot animations retargeted onto it (same clips, timings and
  speeds), not the Godot robot / UAL mesh. The Godot clips loop where their names end in _Loop.
- Flora grafts are built as in Godot, from the same calls, but their random leaf, vein and bump
  layout differs (a seeded FRandomStream, not Godot's RNG), and they sit on the mannequin's bones
  (Godot's spine_02 / spine_03 / Head are Manny's spine_03 / spine_05 / head), so they fit Manny's
  proportions, not the UAL body's. The pack mushrooms' turn is measured on the body facing front;
  Godot turns them in world space, which only matches when the body faces -Z.
- Frame part region colors: Godot picks each vertex's region from the bone that moves it most;
  Unreal materials can't read bone weights, so `M_WWO_Body` picks it from where the vertex sits in
  Manny's reference pose (arms out from the shoulders, head above the neck, legs below the hips).
  Manny's second material slot (torso, forearms, trim) keeps the joint color, or takes a darker
  shade of the region's part color when one is equipped, as the first-person arm does.
- The character preview is lit by its own key and fill light (Godot's preview light and ambient),
  which reach only lighting channel 2 (the preview body and its grafts): the level above it shades
  it from the level's sun.
- Effects are C++ shapes and instanced-mesh particles, not Niagara.
- The first-person arms are the mannequin's right arm, flat-colored like fp_arm.gd. Godot's arm is the
  robot's, all in the skin color; the mannequin's forearm and hand are mostly its trim, so the trim
  takes a darker shade of the skin color. The arm FOV squeeze moves the bones toward the view axis and
  shrinks them (Godot squeezes the projection), so the arm's depth isn't squeezed.
- Wall Grenade walls stand on the ground under the blast. In Godot the ground ray also hits the
  grenade itself (it isn't excluded), so walls there start at the grenade's top: 24 cm up when it
  rests on the floor, higher if it goes off mid-bounce. Probably a Godot bug; not copied.
- Arenas are built from the same Quaternius kit pieces, placed as build_scifi_map.gd places them, each
  colliding as its own mesh. Lighting: Godot's sun (-60 degrees, white, shadows) and its default
  procedural sky, here on a sky dome the sky light captures (no Lumen, see Performance: sky light ambient and screen-space reflections, so shade and metal
  floors come out darker than Godot's unshadowed ambient). Godot has no fog; a thin height fog in the
  horizon color only softens the far walls. Godot's exposure is fixed and its tonemap linear; here auto
  exposure is held to EV100 0.5 to 1.5 around the Godot look (about 0.9) with the filmic tonemapper.
  Floor lights are the kit's emissive strip only, as in Godot (the port's old point lights are gone).
  The test range is in Godot's flat CSG colors (its floor in Godot's default white), not the grid.
- Fix over Godot: the small arena's three stairs climb onto their 2 m platforms. Godot's `stairs()`
  calls name the side away from each platform (`stairs(4, 3, "E")`, `(11, 6, "W")`, `(12, 5, "N")`), so
  there the platforms are islands on the navmesh and Soldiers that spawn up there stay up there. The
  large arena's stairs were already right.
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
- Also true in Godot (not changed): a Soldier that can't see you right at its 15 m minimum range
  dithers in place (backs off, then walks back in).
- Not yet checked hands-on with the DualSense.

## Launch screen (2026-11-12)

- `UI/WWOLaunchMenu`: in the character and settings menus' style, over the start map (`L_Arena`), paused.
  Title: the name and one big **PLAY** (plus Loadout, Settings, Quit). PLAY opens the setup screen, one
  choice at a time: **1 Connection** (Online host / Online join / Local), **2 Mode** for host and local
  (PvP / PvE) or the list of games on the network for join (searched again every 4 s), **3 Map** (the ones
  that suit the mode), then **START GAME** (JOIN GAME) appears bottom right. Circle / Esc goes back a step.
- The maps and which modes each suits are one table, `UWWONetSubsystem::Maps()` (a new map goes there, and
  in `MapsToCook`). Local PvP fills both teams with bots (`?bots=N` on the map's URL, from the map's team size).
  Hosted PvP takes the host's Bots setting.
- It shows when `UWWONetSubsystem::WantsTitle` says so: the first map load of a normal run, after
  **Quit to the title screen** (Settings, Match, Multiplayer; `LeaveToTitle`), and after a lost connection
  (the reason shows along the bottom). The player controller makes the widget in `BeginPlay`.
- "Online" is the LAN play in NETWORKING_HANDOFF.md: games on the same network, no internet matchmaking.
- Test: `-WWOAutoTestTitle` (no map on the command line) walks every step, plays a local PvP match, quits
  to the title and hosts; `-WWOAutoTestTitleJoin` in a second copy finds that game in the list and joins
  it. Shots `60_title` to `72_joiner_multiplayer`; lines `AutoTest title: ... ok|FAIL`. To keep the host up
  for the joiner add `-WWOTitleHostTime=95` and start the joiner about 75 s after the host.

## Firing tester and gun motion (2026-11-12)

- **What it is:** a bench for how a gun looks and kicks. Launch screen, PLAY, "Firing tester", START (`UWWONetSubsystem::PlayTester` opens `L_TestRange?tester`; `-WWOTester` on the command line does the same). `AWWOGameMode::bTester`: no waves, no enemies, the map's dummies removed and one put 20 m ahead of the start (`UpdateTester`).
- **The screen** (`UI/WWOFiringTester`): the pane on the left has the gun and saved-config pickers and that gun's motion sliders (real Settings rows, `UWWOSettingsMenu::MakeRowFor`, so "Save as defaults" keeps them). The player's view is the real game drawn in a 16:9 window: the engine's "no split" layout entry is shrunk to the window while the tester is up (`SetViewRect`), and the HUD is added to the player's screen (`AddToPlayerScreen`), so it sits in the window too.
- **Control:** TAKE CONTROL hands you the player (mouse and keys or controller); Tab / Esc / Options gives the cursor back to the pane (`HandleMenuPress`). The game doesn't pause under the pane. ADS, ADS + FIRE, HIP FIRE and STRAFE play that action without you taking control, through the controller's scripted input (`ScriptHold` / `ScriptPress` / `ScriptMove`, read by `IsHeld` / `WasJustPressed` / `GetMoveAxis` like real input). RESET puts the player back at the start, facing the dummy.
- **Gun motion, rebuilt from Destiny footage** (frame-by-frame notes: reticle fixed to the screen, camera does the recoil, gun moves around the aim point):
  - The aim mark when aimed in is drawn by the HUD at the aim point (`UWWOWeaponData::AdsMark`: 1 a dot, 2 three iron-sight marks, 0 none), not on the gun: the model's `SightDot` part is never shown. Kick, sway and strafe lean can't move it.
  - The gun model's kick has a shape: full at the shot, held `KickHold`, eased back over `KickReturn`; `AdsKick` is how much is left aimed in; `KickPush` pushes it back toward you. Iron-sight frames (hand cannon, shotgun) start with a long hold and a big aimed-in kick, dot frames with a short one and 15%.
  - Aimed in, nothing moves the gun bodily: bob, air lag, the landing dip and the slide's shift fade to zero. What is left is turning about the sight point, plus the strafe's sideways shift and roll (`StrafeShift`, `StrafeRoll`).
  - The hip reticle's ticks open to the gun's hip spread.
  - Camera recoil (`AWWOCharacter::AddRecoil`) is unchanged.
- **Matched to footage (hand cannon):** the tester can save frames in a row at a locked 60 fps (`-WWOTesterFrames=N -benchmark -fps=60`, plus `-WWOTesterStrafe` / `-WWOTesterNoShot`; files `87_frame_NNN`), which were laid beside Destiny 2 footage frame by frame. From that: `RecoilShape` 1 makes the view's recoil follow the measured rise and fall (`RiseShape` / `FallShape` in `WWOCharacter.cpp`, over the rise and fall-back times); the kick turns about a point ahead of and below the sight (`KickPivotAhead` / `KickPivotDown`), shifts right (`KickSide`), and its sideways part returns sooner than its tilt; iron-sight marks are a red pip between brackets and swing away with the gun while it is flipped. The hand cannon's numbers live in the Settings values, not in code.
- **A model for local testing only:** `Tools/import_test_gun.py` imports a glTF into `Content/WWO/Test` (git-ignored), and `Saved/WWO/WeaponModels.local.json` (this machine only) replaces a weapon's entry in `Config/WeaponModels.json`; `"no_mods": true` in an entry shows no mod parts. Delete the local file to get the game's own model back.
- **Test:** `-WWOTester -WWOAutoTestTester [-WWOTesterGun=<id>]` on `L_TestRange` (screenshots `86_tester_*`).
- **Not done:** the amounts are first guesses to tune in the tester; the sign of the strafe shift and roll is a guess (both sliders go negative); jumping, hip-fire movement and fast turns were not in the footage.

- **ADS gun angle** (`AdsTilt`, per gun, degrees, in the tester's "Aiming in" group): tips the gun about its sight point when aimed. The global `AdsLevel` only does anything on guns whose top sits below the sight line (not the test hand cannon), so it left the tester pane.
- **Iron-sight marks**: the pip is drawn on the aim point; the two brackets are drawn where the gun's sight point is seen (`UWWOWeaponComponent::SightOnScreen`, through the gun's own FOV) and turned with it, so they follow the strafe shift, sway and kick. Aimed in, the camera keeps `AdsBob` of its head bob (global, default 0.5) and the gun keeps `AdsGunBob` of its walking bob (per gun, default 0.5); the pip is on the HUD so it holds still.
- **Hip kick vs aimed kick**: `VerticalKick`/`HorizontalKick` are the hip amounts and `AdsKick` the share left when aimed. The hand cannon was 20 x 0.34 (69 degrees from the hip); it is now 8 x 0.85 (28 degrees hip, the same 23 aimed). `-WWOTesterHip` captures frames from the hip.

## Settings online: the host's tuning, your own preferences (2026-11-12)

- In someone else's game the Settings menu shows only a player's own settings: Camera (field of view,
  reticle height, aimed gun size, mouse and stick sensitivity, rumble, punch, shake, flinch, head bob), HUD,
  Audio, the key bindings, and Multiplayer's status line with Quit to the title screen and Switch team.
  Everything else (movement, combat, enemies, waves, bots, items) is development tuning: the host's menu
  and a solo player's have all of it.
- `FWWOSettingRow::bPref` marks a player's own row; the list is `OwnSettings()` at the top of
  `WWOSettingsMenu.cpp` (a page name, and the rows on it or none for the whole page). A new slider is
  host-only unless it's added there. `UWWOSettingsMenu::bRestricted` is set in `Prepare` (`NM_Client`).
- A joiner's Reset and Save touch only those rows; Save merges them into its file and leaves the rest.
- Not done: the host's tuning isn't sent to joiners. Each machine still plays by its own saved values
  (the same in practice: every build ships the same `Config/ShippedSettings.json`), except where the host
  already overrides them (PvP movement, gun and melee checks).

## Windows

- Checked on the Mac first (2026-11-12): the Game target compiles, the cook takes the burst rifle, and the
  staged app (`Saved/StagedBuilds/Mac`) opens on the launch screen and passes `-WWOAutoTestTitle`. The app in
  `/Volumes/UnrealSSD/Builds` is older: run `Tools/package_mac.sh` for a new one.
- Unreal can't build for Windows from a Mac: on a Windows PC with UE 5.8 and Visual Studio 2022 ("Game
  development with C++"), run `Tools\package_win.bat` (output in `Builds\Windows`; `UE_ROOT` and `WWO_OUT`
  override the folders). Not tried yet: no Windows build has been made.
- Done ahead of it: `Config/Sounds.json` is staged (it wasn't: packaged builds were silent);
  `Config/ShippedSettings.json` is in git now, so a Windows build starts from the same Settings values;
  the Mac-only code is behind `PLATFORM_MAC` (the DualSense Create button poll); DX12 / SM6 is set in
  `DefaultEngine.ini`; MSVC's shadow errors are off for this module on Windows (`WhatWasOurs.Build.cs`
  says why).
- Expect on the first try: MSVC is stricter than clang in places, so a few compile errors are likely;
  the Fab character packs aren't in git (see "Character models": add them on the PC too, or bodies fall
  back to the mannequin); Windows Firewall asks on the first host or join (allow private networks).
- Mac and Windows copies of the same commit should see each other on a LAN (same engine, same network
  version); untested.

## Review fixes (2026-11-12)

A read-through of the whole codebase found about 100 issues (the report is the doc "What Was Ours: codebase review findings"). The twelve that mattered most are fixed:
- **Join list:** `JoinGame` joins from `FoundResults`, the finished search the list was drawn from, not from the search in flight (with two hosts it could join the wrong one).
- **Failed join:** a connection that never gets going, or a failed travel, lands on the launch screen with "Couldn't join (...)"; before, it dropped you into a solo game. Starting a game on purpose (`Host`, `PlayLocal`, `JoinGame`, `JoinAddress`) clears the show-title flag. Test: `-WWOAutoTestTitleJoinFail`.
- **Menus and combat input:** `AWWOPlayerController::IsHeld` / `WasJustPressed` / `ConsumeChord` read nothing while a menu is open, and the bumpers don't arm a chord then. Online the game runs on under a menu, and L1 / R1 are both the tab keys and grenade / melee.
- **Character menu:** the four tabs keep their click handlers (`HeaderHandlers`); the preview's scene capture runs only while the menu is open (`SetPreviewLive`).
- **Co-op stats:** on the host, `bNetStats` covers only the movement stats a joiner sends; its health, shield and grenade stats take its loadout again.
- **Dead enemies on a joiner** stop blocking shots and steps (`AWWOEnemy::OnRep_Dead`).
- **PvP:** a dead player's capsule no longer blocks shots; poison credits whoever applied it (`UWWODamageOverTime::By`) and damage from nobody doesn't erase the last attacker; Shield Share skips the other team.
- **Swaps and death:** each gun keeps the rounds and perk state it was put away with (`GunStates`; a swap is not a reload, and Overpressure and Cold Bore don't re-arm); a change of mods on the same gun takes a swap and keeps a part-used magazine; dying or starting an ult stops a burst, reload or punch (`Interrupt`), and a respawn starts every gun full (`ResetForRespawn`).

Also 2026-11-12: **Quit to main menu** is a button in the Settings sidebar on every tab (and on Match, Multiplayer); it calls `LeaveToTitle`. Hosting, that ends the match for everyone; a person leaving a PvP match is replaced by a bot on the host (`AWWOGameMode::Logout`; co-op has no bots, so nobody replaces a leaver there); alone it just ends. **Bot health and shields** are on Settings, Enemies, Bots (`AWWOGameMode::BotHealth` / `BotShield`, applied to every bot each tick; a change refills them); a player's own stay on Player, "Health and shields".

Second batch (2026-11-12): the report's co-op and combat groups.
- **What joiners see:** a Soldier's gun sits in its hand, comes up in a fight, glows before a burst and goes with the body (`AWWOSoldier::AnimateRemote`, from the replicated `NetGlow` / `bNetAlerted`; `AWWOEnemy::AnimateRemote` is the hook for any enemy's joiner-side looks). Allies replicate health, shield and down (`AWWOAlly::OnRep_Down`). A player, enemy or target already hurt when a joiner arrives keeps the host's values (start-up values are set on the host only). Other players' footsteps are heard (`AWWOCharacter::RemoteFootsteps`). A PvP joiner respawns facing the way the host turned it (`UWWOHealthComponent::SpawnYaw`).
- **What joiners do:** Root Bombs fly where the joiner looks (`ServerUseAbility` carries the aim; the host keeps it in `NetAim`). A grenade, ability or ult the host refuses is undone on the joiner and its charge returned (`ClientKitRefused`, `UWWOAbilityComponent::UndoUse`, `UWWOUltComponent::Abort`). A lunge ends a sprint or slide inside the simulated move, and mantling and crouching test the lunge's flight, so both machines agree. A reviver going down clears the revive. Ricochet and Static arcs from a joiner use the gun's modded damage; the Static count is per gun. Melee hit-stop is skipped online (it is the whole world's time).
- **Other players' actions (new):** every player's body raises its arms as far as its gun is up (aimed in, or just fired from the hip) and leans with its look, up or down (`UpdateModel`: the Pistol_Aim_Neutral upper-body layer and the spine hunch). A joiner tells the host ten times a second (`SendPose` / `ServerSetPose`); the host's copy poses its body, which replicates to everyone as body animation already does. A grenade throw plays OverhandThrow on the thrower's body. `-WWOAutoTestPose` shows your own body from outside (`84_pose_*`). The gun shows in the right hand of every body others see whole (`UWWOWeaponComponent::UpdateWorldGun`: the weapon's own mesh, gripped at the model's `hand` point in `Config/WeaponModels.json`; lowered at rest, up along the look as the aim pose comes in; read back from the body's pose, so it works on every machine). No mods show on it, and there is no reload clip.
- **Enemies:** below the map's kill height they die (`UWWOHealthComponent::MapKillHeight`), and a Soldier won't back or strafe off a ledge (`LedgeAhead`). Swarm bites and leaps and the Tank's slam need the target on their level (`HeightToTarget`), the slam a line of sight too. A veiled target is dropped for another at once.
- **PvP rules:** the launch screen's bot count is exact (a local 4 v 4 is 7 bots whatever the Bots slider says); a person who leaves is replaced by a bot and a person who joins mid-match takes a bot's place and spawns away from the other team (`BotsExtra`, `RetireBotFor`). Respawns pick among the three best free spots. The time limit runs on time played, so it can be changed mid-match. Kill credit lasts 10 s. A match starts with ults empty. Dying ends an ult or a wound-up grenade. Snare Mines slow, and Blinding Grenades white out, the other team's players (`AWWOCharacter::ApplyBlind`; a blinded bot sees nobody).
- **Feel:** looking straight up no longer tips past vertical; the ground stop, eye-height and landing-dip easing and damage-over-time ticks are frame-rate independent; six more perks show on the HUD; Held Breath's text says what it does (recoil; a scoped sniper has no sway to remove).

### Third batch (2026-11-12): the rest of the report

- **What the host believes from a joiner:** a shot may carry at most the gun's pellets, one hit per target, and not from a gun that is locked; each gun has its own fire-rate budget (`ShotBudgets`), so swapping guns doesn't refill it. The damage cap is worked out per hit from what the host can check itself: range (Long Shot, Point Blank), the target's health (Finisher), time since the last shot (Cold Bore) and whether the hit is near the head (`CheckGunHit`). In PvP a punch is capped from the host's own numbers and needs a swing within 2 s; a lunge needs a swing within 1.5 s and a launch a ready ability (`MoveAutonomous`); frozen states (ult, Entrench) come from the host; `ServerSetMoveStats` is clamped. A loadout sent up always has a flora and an unlocked weapon (`ApplyNet`). The perk numbers live in one place (`Weapons/WWOPerks.h`).
- **Sessions:** a full game refuses the join with a message (`PreLogin`: 6 in co-op, 12 in PvP) and shows as "full" in the list; the search is cancelled when you host, join or leave; a failed host no longer opens the map twice; the status line counts people, not bots; in co-op a leaver who was the last one standing ends the run. Switching teams goes to the smaller team only, at most every 10 s, and respawns you at that team's spawn.
- **Menus and saves:** the loadout is written once when the menu closes (and on quit), not on every click, and never when the weapon or mod files failed to load; the join list redraws only when it changes; the world isn't drawn behind the launch screen; rebinding a key swaps every row that had it; the Maps page comes from the one map table (`UWWONetSubsystem::Maps`); click handlers of rebuilt pages are dropped; model and weapon-model JSON is read once.
- **Repository and packaging:** the three parked hero packs are out of the cook list; the unused Android file-server block (it held a token) is out of `DefaultEngine.ini`; `package_mac.sh` no longer blanks `ShippedSettings.json` on a fresh clone; `package_win.bat` stops if the Fab packs are missing (`WWO_SKIP_PACKS=1` to go on) and says where the exe lands; `.gitattributes` fixes line endings; Python bytecode and tool logs are ignored; the map geometry JSONs have a copy in `Tools/geometry` for machines without the mockups folder.
- **Tests:** every scripted run ends with `AutoTest result: PASS` or `FAIL (n)` and exit code 1 on a failure (`Player/WWOTestRun`); the net and PvP runs now fail if a forged shot, a throw with no grenade, extra punches or friendly fire get through; the graft run keeps a copy of the saved loadout.
- **Not done, on purpose:** one shared base class for the three menus and splitting the 300 to 500 line functions (a refactor best done by itself, with the menus' screenshots compared before and after); moving HUD marking out of paint and caching the vignette and stat bars; rebinding the alternate keyboard keys.
- **DualSense on Windows:** untested. The Create-button poll is Mac-only code; on Windows the pad arrives through Unreal's own input, where touchpad and Create may map differently.

## Settings menu groups
Every header with sliders under it is a collapsible group (`UWWOSettingsMenu::BuildRows`): + to open, - to close. The first group of each section starts open, the rest closed, and each stays as you left it while the game runs. A header with no sliders under it stays a plain label. Each row is two lines: the label with its value on the right, then the slider at the panel's full width.

## Networking
LAN multiplayer, co-op waves and PvP, hosted and joined from the launch screen: see NETWORKING_HANDOFF.md (its "Status" section first) for what is done, how it works and what is next.

## Reticle and input latency
- **Low reticle (Destiny-style):** `AWWOCharacter::ReticleDrop` (Settings, Camera: "Reticle below center", default 0.06 of the screen height), at the hip and aimed in. The camera looks up by the matching angle (`ReticleAngleForFov`, recomputed for the zoomed FOV so the point keeps its place on screen) while the aim stays put, so `AimForward()` (shots, aim assist, the reticle target) points at the reticle; the HUD draws the reticle, hit marker and assist cones there. The gun, drawn with its own FOV, tips down to point at it, and aimed in its sight point (the model's origin) moves down onto it; the sniper's scope overlay is drawn shifted down onto it with the strip above filled black.
- **Latency:** `r.GTSyncType=1` and `r.OneFrameThreadLag=0` (DefaultEngine.ini) stop the game from running up to three frames ahead of the screen (the GPU is near its 16.7 ms budget at 60 Hz, so frames queued); mouse smoothing is off (DefaultInput.ini). Cost: the arena perf test went from 55.3 to 53.3 fps average. Test shots now log their game, render and GPU times.

## Character menu

- Fills the window (`UWWOCharacterMenu`). Tabs: Loadout, Flora, Weapon, Shell (bumpers or Q / E; "Shell" is the frame parts, still `FramePart` / "Frame Parts" in code and in the settings menu). Loadout is the overview built by `BuildOverview` (frame stat bars and the build list, the character, cards for everything equipped; a card opens the tab that edits it). The other three are the old tiles' editors beside the character. Accent color = the equipped flora's.
- The Loadout tab's Build section is notes, not a list of items: "Effects you apply" (from the grenade's kind, the abilities and the traits with an `Effect`) and "Buffs on you" (ability self-buffs and the traits' stat `Mods`), each with its source. New abilities need a line in `BuildOverview`.
- Hovering the flora, grenade, an ability, a trait or a weapon slot on the Loadout tab drops a row of the other choices under it (`UpdateHoverPicker` / `OpenHoverPicker`); clicking the tile itself opens its tab.
- **Two weapon slots** (`UWWOLoadoutSubsystem::SetSlotConfig` / `SwapWeaponSlot`, saved as `WeaponSlots`): each holds a saved weapon config, any config in either slot. Weapon swap (scroll / Triangle) goes to the other slot's config; with that slot empty it steps through every weapon as before. The B / d-pad down key also goes to the other slot, and names the config it lands on.
- A shell card offers that slot's other parts the same way. The part under the cursor shows on the four stat bars (`UpdateStatPreview`): one you could equip in green (gain) and red (loss); the one you have on in yellow (what it adds) and faint red (what it costs). Only shell parts move those bars.
- **Weapon and Shell fit on one screen (2026-11-12):** neither tab scrolls. Weapon is four columns across the full width (`BuildWeaponRows` / `BuildModRows`; widths in `WWOCharacterMenu.h`): the five weapons, the frame's stat bars with the equipped perks under them, the six mod slots, and the saved configs; hovering a slot shows its mods where the configs were. Click a config to apply it; Save over and Delete sit beside it. Only the config list scrolls, inside its own box, past about eight. Shell is a row per slot (name at the left, the parts sharing the width) and the net effect. `-WWOAutoTestTabs` checks both at the fullest case (`AutoTest tabs: ... fits on one screen`, shots `80_tab_weapon`, `81_tab_weapon_mods`, `82_tab_shell`); passes at 1280x720, 1440x900, 1600x900 and 1920x1080.
- Icons are stand-ins: the item's initials in a box, until there is art. The frame part cards have room above their text for it.
- The character stands still. `UpdateTurn` turns it: right stick (the left one moves the menu cursor), A / D, or dragging it with the mouse or Cross.
- The preview camera is 2.75 m away (it was Godot's 4.4 m) so the character fills the tall middle column; its backdrop is tuned to come out as the page color.

## Settings menu

- Fills the window in the character menu's style (`UWWOSettingsMenu`), 95% opaque so the game behind can still be watched. Six tabs across the top (bumpers or Q / E): Player, Combat, Enemies, Match, Items, Controls (the key bindings). Each lists its pages down the left (`Pages`; the `Layout` table in `Prepare` says which global section goes under which tab, and a new section not named there lands under Player). The open page fills one column on the right.
- Items' pages (Equipped, Grenade Types, Abilities, Traits, Weapons, Shell) are the item browsers: a list, then the picked item's sliders.
- The search box lists every slider whose name, page or item matches (`OnSearchChanged`, up to 80), under where each lives. Enter, Esc or a click elsewhere gives the keyboard back to the game.
- **Key bindings** (the Controls tab, `BuildBinds`): one line per action with a keyboard / mouse key and a controller button; click one, then press the new key (`AWWOPlayerController::BeginKeyCapture`, taken by `FWWOMenuCursor::Capture` before the menu sees it). `AWWOPlayerController::BindRows` is the list; changes are kept in `Saved/WWO/Keybinds.json` and `MapKeys` rebuilds the Enhanced Input mappings. A key already on another line swaps with it. The menu's own keys and the sticks aren't rebindable. Test runs (`-WWOAutoTest*`, `-WWOStress`) ignore and never write the saved bindings.
- `ShowPage(Title)` opens a page by name (the tests use it); `-WWOAutoTestMenus` saves `25_menu_settings_search`.

## Feel additions (2026-11-11)

- **Explosions shake you** (`UWWOWeaponComponent::Shake`, called from `AWWOGrenade::SpawnBlast`): a camera punch and rumble for the local player, by distance and blast size. Settings, Camera: "Shake from explosions near you".
- **Damage flinch:** taking damage punches the view and rumbles a little (never the aim). Settings, Camera: "Jolt when you take damage".
- **Melee hits** throw sparks at the hit point (the gold flare on a kill); **grenades** kick up dust and sparks where they bounce.

## Ability and kit effects (2026-11-11 pass)

- While Thornfist is up or Static Charge is armed, both fists glow (`UWWOBodyComponent::SetHandGlow`). The host knows it for everyone and sends it to every machine (`AWWOCharacter::NetFists`); your own are lit from your own ability state.
- Damage pools have a low haze of their color over the surface and light what stands in them. Spore fog draws its edge on the floor and has wisps rising inside. Blasts leave a scorch that fades over 5 s (`AWWOFXActor::Scorch`).
- `-WWOFxShots` puts a scorch in its `_burst` shot; `-WWOFxGallery` stages the rest (`42_fx_*`). (An activation flare at the feet was tried and removed: Will didn't like it.)

## Other players' bodies

- Every other player's body (a bot's, a joiner's, the host's as a joiner sees it) shows its flora's grafts and its shell's colors, so you can read what they're running. The host sends each character's loadout to every machine as `AWWOCharacter::NetLook` (set in `ServerSetLoadout` and kept current in `PrepareMovement`); each machine keeps a copy of it on that character (`NetLoadout`, so `UWWOLoadoutSubsystem::Get` on anything of theirs reads theirs) and `ShowLook` rebuilds the body from it when the visible parts change. Your own character always reads your own loadout.
- Checked with a host (4 idle bots) and a joiner on one machine: both logs list every other character's look (`Net visuals: <character> shows <flora>|<grenade>|<abilities>|<shell parts>`).

## Sound effects

- `UWWOSound` (`Systems/WWOSound`): the game plays an event by name, `Play(ctx, "explosion", Where)` in the world, `PlayFlat(ctx, "hit")` for the player here only, `PlayFor(Character, "reload_start")` for a character's own noise (flat for you, in the world for anyone else). Nothing is sent over the network: each hook sits where its cause is already seen on that machine, next to the matching visual effect.
- `Config/Sounds.json` maps each event to the sounds it picks from at random, a volume, a pitch range and how far it carries (m; 0 = only ever flat). An event not in the file is silent, so a hook can go in first. Edit and restart; no build.
- Sounds are SoundWaves in `/Game/WWO/Audio`, imported from the .wav files in `Tools/audio_src` by `Tools/import_sounds.py` (UnrealEditor-Cmd -run=pythonscript). The current ones are placeholders from Kenney's Sci-Fi, Impact and Interface sound packs (CC0, `Tools/audio_src/LICENSE.txt`). To swap one: drop a .wav in that folder, rerun the script, name it in Sounds.json.
- Events hooked so far: `fire_<weapon id>` (else `fire`), `hit` / `hit_crit` / `kill`, `reload_start` / `reload_done`, `weapon_swap`, `melee_swing` / `melee_hit`, `grenade_throw` / `grenade_bounce` / `explosion`, `ability`, `ult`, `jump` / `land` / `footstep`, `hurt`, `death` / `death_own`, `enemy_death`, `soldier_shot`, and the menus' `ui_click` / `ui_tab` / `ui_open` / `ui_close` / `ui_bind`.
- Volume: Settings, Player, Audio (`AWWOCharacter::SoundVolume`). The `-WWOStress` log's "Bots sounds:" line counts what has played. No music, ambience or voice yet; no per-surface footsteps.

## Enemy readability

- **PvP marking** (`UWWOHUDWidget`, `UWWOWeaponComponent::Marked`): a player is marked when the reticle comes over them (hip) or when they enter the marker range while aimed in (`HealthBarRadius`, times `HealthBarAdsScale` aimed in), and dropped on leaving the range. Enemies show bars while marked; teammates' bars (light grey health, 60% white shield) show whenever in sight. On being marked a player's outline flashes once (`AWWOCharacter::UpdateTeamTint`, `M_WWO_Highlight`'s `Rim`): red for foes, blue for teammates. The reticle is blue over a teammate. A diamond sits over foes in sight, a chevron over teammates (through walls).
- **Your HUD:** vitals are top center; status chips (shield down, slowed, poisoned, in fog / veiled, entrenched, guard) sit under them with timers; a faint red spike (a long shaded pyramid drawn in perspective below your line of sight) points at where damage came from (`UWWOHealthComponent::DamageCameFrom` on the host, `DamageMarks` on the victim's machine).
- **Low-health vignette (2026-11-12):** red creeps in from the screen's edges as your health and shield drop (together: nothing at full, faint with the shield gone, heavy near death) and fades as they come back; under the rest of the HUD. `FPainter::Vignette` in `WWOHUDWidget.cpp` (a vertex-colored grid, no material); the amount is `HurtShown`. Settings, HUD: "Red vignette as your health drops" (`AWWOCharacter::HealthVignette`, 0 = off; a player's own setting, so joiners have it). `-WWOAutoTestVignette` holds 100 / 75 / 50 / 25 / 8% and saves `83_vignette_*`.
- **Looking at it:** `-WWOBotsIdle` makes the bots stand still; `-WWOStressShots` on a `-WWOStress` bot run turns the host to the nearest teammate or foe every 4 s, fakes a hit and a slow, and saves `41_pvp_view_*`.
Settings, HUD (`AWWOCharacter` properties):
- **Hover tint** (`TargetHighlight`, 0.12): the enemy under the reticle gets `M_WWO_Highlight` (made by `Tools/create_highlight_material.py`: unlit translucent red, a bit stronger at the outline) as its body mesh's overlay material (`UWWOBodyComponent::SetHighlight`, driven from `UWWOWeaponComponent::UpdateReticleTarget`).
- **Health bars near the reticle** (`HealthBarRadius`, 140 HUD px): besides the target under the reticle, every enemy in sight whose middle is within that distance of the reticle on screen shows its bar.
- **Damage numbers to the side** (`DamageNumberOffset`, 55 HUD px): each number is pushed left or right of the hit (by its random jitter) so it doesn't cover the target.
- `-WWOAutoTestHidden` ends with `40_readability_hover` and `40_readability_numbers`.

## Hidden aim assist (after-shot)
Extra controller assist that only works while a shot's recoil plays out, so it hides in motion the gun already makes (`UWWOAimAssistComponent`, toggles in Settings, Aim Assist; amounts per weapon under its "Hidden assist" header).
- **Window:** opens on every shot (`NotifyShot` from `AddRecoil`), stays open while the recoil kicks or falls back and at least 0.15 s (for instant-recoil guns), then fades over 0.12 s. Works only with a target in the friction cone, scaled like the rest of the assist (none on mouse while Mouse assist is 0).
- **Recoil steer:** recoil motion also bends the view toward the target by up to a fraction of that motion. `KickSteer` (0.6) during the kick (instant or timed) steers sideways only, so the kick climbs the same over a target as over a wall; `RecoverySteer` (0.35) during the fall-back steers both ways. No side-recoil bias (kept random on purpose).
- **Follow-up magnetism** (`FollowupMagnetism`, x1.5): shots fired in the window get a wider magnetism cone.
- **Kick friction / adhesion** (`KickFriction` x1.5, `KickAdhesion` +0.3) during the window.
- **Markers** ("Show hidden assist markers"): STEER, MAGNET (a hit only the wider cone caught) and STICKY (boosted friction or adhesion while you were turning) under the reticle, and a faint ring while the window is open.
- **Test:** `-WWOAutoTestHidden` (on `L_TestRange`) fires the rifle 2 degrees off a head with it off, then on, and logs where the aim ends and the damage done; `39_hidden_assist_markers` shows the markers. It logs the sideways and vertical parts: with assist on the climb matches (3.90 degrees both) and the sideways drift closes (1.76 to 0.03).

## Character models (prototype)
Placeholder models from Fab (all free). In use: Soldier = Robot scout R21, Swarm = Paragon Minions (Dusk melee), Tank = Paragon Steel. Parked (`models_off` in `Config/CharacterModels.json`, not cooked, players use the mannequin with grafts): VoltVine = Paragon Wraith, IronBark = Paragon Murdock, VeilSpore = Paragon Kallari. The links are in the LaminarFlow doc "Model Assets (for now)". The packs are about 11 GB and are not in git (`.gitignore`): on a fresh checkout add each to your Fab library and "Add to Project" (the robot needs "Show all projects" and version 5.4), then run `Tools/build_retargeters.py`. Paragon content may not use the PARAGON name to advertise the game.
- **How it works:** every body keeps the mannequin as an invisible animation driver, so all clips, layers, the hunch, hitboxes and grafts work as before. `UWWOBodyComponent::SetModel` adds the model as a second skeletal mesh whose `UWWOModelAnimInstance` copies the mannequin's pose through `FAnimNode_RetargetPoseFromMesh` and `RTG_Manny_<key>` (`/Game/WWO/Retarget`).
- **Config:** `Config/CharacterModels.json` (mesh, scale, hidden material slots, whether grafts or plain AttachBox boxes stay). Enemies set `Body->ModelKey`; the player and the menu preview use their flora's model. Allies stay on the mannequin. The flora models keep the flora grafts ("grafts": true; pinned to the mannequin's bones, so they sit where its body would be). On the Tank's model the plain armor boxes hide and the glowing weak spot stays.
- **Tools/build_retargeters.py** turns on the skeletal-mesh usage flags the models' materials need (the Mercenary ships without them and renders grey), then builds `IK_<key>` rigs (auto-generated chains) and `RTG_Manny_<key>` retargeters (auto-mapped and auto-aligned). Rerun it after adding a model.
- **Not done:** the hit flash and region/frame-part colors only reach the mannequin, so models don't flash or recolor yet. The first-person arms are still the mannequin.

## Weapon mods (draft)
A test version of the frames-and-mods design (LaminarFlow doc "Weapon Frames & Mods — Design"). All five weapons are frames with all six slots open: barrel, magazine, stock, sight, chambering, grip. A mod's `frame` is the weapon id, so a frame's slots only list its own mods; a weapon is a frame as soon as the file has mods for it.
- **Burst rifle (2026-11-12):** the fifth frame, `DA_Weapon_burstrifle`: 3-round bursts (`UWWOWeaponData::Burst`, `BurstRate` 900 rpm inside a burst; a burst gun's `FireRate` is bursts per minute, 150), 15 damage, 120 m range against the rifle's 80. Hold the trigger for burst after burst; a burst finishes once started (`UWWOWeaponComponent::UpdateFiring`, `FireRound`). Made by `Tools/create_burst_rifle.py` (a copy of the rifle's asset with the rifle's saved Settings values, then the burst rifle's own from `Tools/wwo_data.json`). Model: the Lyra rifle with a boxed barrel (`Config/WeaponModels.json`). The host's shot check allows a burst's rounds (`FWWOGunLimits::Burst`).
- **Shotgun and sniper (2026-11-12):** frames now too, 12 mods each. New stats `pellet_spread` and `hip_spread`.
- **Rifle and burst rifle sights (2026-11-12):** all four of their sight mods look like a holo sight: a base on the rail and an open square hood around the aim line, with the gun's own thin sight dot inside it. The hood stays while aiming (no `hide_ads`): you look through it. Both guns use `/Game/WWO/Meshes/SM_Rifle_NoSights`, Lyra's rifle with its iron sights pressed flat onto the rail (`Tools/create_rifle_nosights.py`; the Lyra mesh is one welded shell, so they can't be lifted off). The sniper keeps the original mesh under its scope. A cyan strip marks the plain mod, a red one the perk mod. The shapes are the mods' `parts` in `Config/WeaponMods.json`.
- **New perks:** shotgun: Point Blank (+25% within 4 m), Slideshot (sliding: half the spread, +15% damage), Tighten (aiming tightens the spread 30%); sniper: Cold Bore (first shot after 4 s +25%), Marksman (a crit returns the round), Held Breath (aimed 1 s: no sway, -30% recoil), Long Shot (+20% beyond 30 m), Finisher (+30% to targets under half health); burst rifle: Full Burst (every round lands: next burst +20%), Fourth Round (4-round bursts). The rest reuse Scavenger, Final Round, Hammer, Adrenal, Brace, Hunter. Any frame can carry any perk: they're keyed by name, not by weapon.
- **Test:** `-WWOAutoTestFrames` (on `L_TestRange`): each new frame's plain mods against the mod data, and the burst rifle's rounds per pull (3, 4 with Fourth Round, 15 in 1.9 s held). `-WWOAutoTestGuns` now shoots all five guns with both mod sets.
- **Mods:** 60 mods in `Config/WeaponMods.json` (12 per weapon), one with a stat trade-off only and one with a perk per slot. Stats are percent changes. Edit the file and restart to retune; it isn't an asset yet. `UWWOModData` is in `WWOData.h`.
- **Equipping and configs:** `UWWOLoadoutSubsystem`, under "Weapon mods". Mods are kept per frame, and configs are saved in the loadout save. Fixed weapon stats are scaled in `UWWOWeaponComponent::RefreshStats`, which rebuilds a copy of the weapon asset each frame, so `GetWeaponData()` returns the modded copy and Settings sliders still edit the asset. Reload, aim-in, swap and ready times go through `M()`, where `Apply` adds `ModPercent`.
- **Perks:** in `UWWOWeaponComponent::RefreshStats`, `OnShotResult` and `PerkStatus`.
  - focus, brace and hunter: stat changes while they're active
  - scavenger and adrenal: trigger on kills
  - static: `AWWOChainLightning::SpawnFrom`
  - Hand cannon: overpressure (first shot after a reload +50%), final_round (last round x2) and patience (aimed 0.6 s: +0.6 crit) scale the shot in `Shoot`; anchor (aimed and still: -50% recoil) and hammer (+25% fire rate 3 s after a kill) in `RefreshStats`; ricochet (a crit bounces once for 60%) uses `SpawnFrom`.
  - The HUD shows perk counters above the weapon name.
- **Menu:** Character, then Weapons. The tile takes the full width and hides the character preview for now.
  - **Stat bars:** `UWWOStatBars` paints them. Every bar reads as "longer is better", the halfway tick is the frame with no mods, and a full bar is twice that.
  - **Slots:** all six are shown in one column. Hovering a slot opens a popup beside it listing Empty and that slot's mods, with the equipped one marked. The popup closes 0.35 s after the cursor leaves both the slots and the popup.
  - **Preview:** hovering a mod in the popup shows on the bars what it would add (green) or remove (red). Clicking it equips it.
  - **Configs:** listed under the slots, with save new, apply, overwrite and delete. Hover is polled in `NativeTick` (`UpdateModHover`).
- **In game:** D-pad Down or B (the keyboard key; rebindable under Controls, and the menu and HUD hints show whatever it is bound to) applies the next saved config (`EWWOAction::NextConfig`).
- **Gun models and mod looks:** `Config/WeaponModels.json` gives each weapon a mesh from Epic's Lyra Starter Game (`Content/Weapons/`, copied from a Lyra project: pistol = hand cannon, rifle = rifle and sniper, shotgun), with its scale, rotation and offset from the sight point, the muzzle, and an anchor per mod slot. Each mod's `parts` in `WeaponMods.json` are simple shapes placed at its slot's anchor; `hide_ads` parts hide while aiming. `UWWOWeaponComponent::BuildModel` reads both on every rebuild (equip or mod change), so they retune with a restart. Without a model entry a weapon falls back to the Godot box model. Lyra content is under the Unreal Engine EULA (fine in an Unreal project).
- **Gun kick:** each weapon's Settings has "Vertical kick" (0 to 20, 1 = about 3.4 degrees of tilt) and "Horizontal kick" (-5 to 5, + swings the muzzle right) instead of Godot's single gun_kick (`GunKick` only seeds `VerticalKick` now, and an old save's "GunKick" loads as the vertical one). Looks only; recoil is what moves the aim. The gun test's `38_gun_handcannon_kick_diagonal` shows a diagonal kick.
- **Level top when aimed (2026-11-12):** aimed in, the gun tips down about its sight point until the plane of its top passes through your eye, so you look straight along the top (it shows as a level edge under the sight, with the back of the gun below it) instead of down a ramp running up to the sight. The angle is atan(sight line above the gun's top / eye-to-sight distance): `TopBelowSight` from the mesh's bounds in `BuildModel`, applied in `UpdatePose`. Settings, Camera: "Level the gun's top when aimed" (`UWWOWeaponComponent::AdsLevel`, 1 = fully, 0 = off). The sight stays on the reticle; shots are unaffected (looks only).
- **ADS gun size:** Settings, Camera, "Gun size when aimed" (`UWWOWeaponComponent::AdsGunScale`, 1 = same as the hip), one for all weapons. The gun model scales around its sight point as you aim in, so the sight stays on the reticle.
- **Sight height:** each weapon's Settings section has "Sight dot height above gun (cm)" (`UWWOWeaponData::SightHeight`). It moves the gun and its mod parts down while the sight dot stays put, rebuilt live when the slider moves; Save as defaults keeps it.
- **Gun screenshots:** `-WWOAutoTestGuns` (best on `L_TestRange`, no enemies in the way) saves `38_gun_<weapon>_<hip|ads|plain|perk|perk_ads>`. Add `-WWOFxShots` and each gun also fires once from the hip in slow motion, with a second shot drawn across the view as another player's would be: `39_fx_<weapon>_flash` (muzzle flash) and `_trail` (the shots in the air a moment later). A third shot, `_burst`, catches a body hit, a headshot, a death burst and a soldier's bolt placed in front of you. The effects are `AWWOFXActor::BulletTrail`, `MuzzleFlash`, `HitSpark` and `DeathBurst`; your own gun's flash is the `MuzzleFlash*` parts built in `UWWOWeaponComponent::BuildModel`.
- **Autotest:** `-WWOAutoTestMods` (also the last step of `-WWOAutoTest`). It covers the rifle, then the hand cannon (stats checked against the mod data, perk set fired). It backs up and restores `Saved/SaveGames/Loadout.sav`.

## Radar (2026-11-12)

- **What it is**: a ring of 8 sections in the HUD's top left (`UWWOHUDWidget::Radar`, drawn in `NativePaint`). Section 0 is the way you look, the rest go clockwise. Each section fills from its rim inward in steps as the nearest enemy that way gets closer, and is full inside melee distance. The fill is fainter the more the enemy is above or below you (it does not say which).
- **Who shows**: everything in `UWWOWorldSubsystem::LivingTargets` (PvE enemies, target dummies, and in PvP the other team). Always, whether or not they are moving or in sight.
- **Settings** (Settings, HUD, Radar; on `AWWOCharacter`): `RadarSize` (0 = off), `RadarRange` 30 m, `RadarFullRange` 4 m, `RadarSteps` 4, `RadarHeightFade` 6 m, `RadarMinAlpha` 0.2.
- **Gone**: the diamond over the other team's heads in PvP. Teammates keep their chevron.
- **Teammates** (`RadarMates`): in the centre triangle's blue, a dot at their place when inside the range, else an arrow on the rim pointing their way. Everyone you can't hurt counts (your team in PvP, all players in co-op).
- **Enemy bars**: health in the radar's red, 126 x 7 px, with a 3 px shield bar directly on top (one backing, no gap), 16 px clear of the head. `FPainter::Tri` shows a colour darker than `Box` does, hence `FoeRed` and `FoeRedBox`.
- **Not done**: the `no_radar` trait still does nothing. Only checked with one dummy at one height (the tester autotest).

## Pulse rifle pass (2026-11-12)

- **Matched to the Destiny clip** (`Pulse Rifle.MP4`, bare gun, aimed in): the view's rise and fall over a 3-round burst is within 1-2 px per frame at 540p. Destiny's burst kicks 9, 3.5, 3.5 px (first round much the biggest), rounds 4.5 frames apart, and the whole burst falls back in about 0.4 s. Its gun model barely moves aimed in (pushes back about 2%, a few px down and right).
- **Code**: `BurstRecoilLater` (per gun: a burst's later rounds kick this share of the first). Shaped recoil (`RecoilShape`) now also owes back what earlier shots hadn't fallen back yet, so a burst or quick follow-up shots come down as one (before, only the last shot's kick fell back).
- **Burst rifle settings** (in `Saved/WWO/SavedSettings.json` on this machine, previous file `SavedSettings.before-pulse.json`): RecoilShape 1, RecoilUp 0.75, BurstRecoilLater 0.4, RecoilKickTime 0.06, RecoilRecoveryTime 0.45, RecoilSide 0.08, BurstRate 820, AdsKick 0.025, KickPush 0.6, KickSide 0.15, KickPivotAhead 5, KickHold 0.03, KickReturn 0.06.
- **Measure bare**: weapon mods change recoil, so frame captures for matching use `-WWOTesterNoMods` (the saved loadout is put back afterwards).
- **Not matched**: hip fire (no footage), and strafing only by eye.

## Logos (2026-11-12)

- **Title screen**: the name is the wordmark logo (`/Game/WWO/Textures/UI/T_LogoTitle`), as wide as the text it replaced; the "//" above it is gone. Both old texts are still in the layout, unseen, so nothing else moved (`UWWOSettingsMenu::MakeLogo`).
- **Headers**: the "//" before "WHAT WAS OURS" is the round leaf mark (`T_LogoMark`) in the character menu and on the setup screen after PLAY. The Settings header ("// SETTINGS") and the build title inside the Weapon tab keep theirs.
- **PLAY**: set in Poppins Light, read at run time from `Content/WWO/Fonts/Poppins-Light.ttf` (SIL Open Font License, text beside it; from the Google Fonts repository). The folder is staged with the game (`DirectoriesToAlwaysStageAsUFS`); not checked in a packaged build yet.
- **Source art**: `Tools/art/ui/*.png` (white on clear, cut from the supplied black-background images); `Tools/import_ui_logos.py` imports them. `/Game/WWO/Textures/UI` is always cooked (they are loaded by path).
