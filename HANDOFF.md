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
  map views (about 25 s; works on all three maps).
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
- Body animations: `Tools/bake_ual_anims.py` (UnrealEditor-Cmd -run=pythonscript; reads the UAL glb files
  from the Godot project, `WWO_GODOT` or `~/Documents/WhatWasOurs`). It retargets each clip in mesh space:
  the UAL rig rests in a T-pose with three spine bones, Manny in an A-pose with five; mapped bones take the
  UAL bone's world rotation change after a fixed rest alignment, the pelvis moves scaled by the pelvis
  heights, and Manny keeps its own bone lengths.

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
  procedural sky, here on a sky dome the sky light captures (Lumen GI and reflections, so shade and metal
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

## Reticle and input latency
- **Low reticle (Destiny-style):** `AWWOCharacter::ReticleDrop` (Settings, Camera: "Reticle below center", default 0.06 of the screen height), at the hip and aimed in. The camera looks up by the matching angle (`ReticleAngleForFov`, recomputed for the zoomed FOV so the point keeps its place on screen) while the aim stays put, so `AimForward()` (shots, aim assist, the reticle target) points at the reticle; the HUD draws the reticle, hit marker and assist cones there. The gun, drawn with its own FOV, tips down to point at it, and aimed in its sight point (the model's origin) moves down onto it; the sniper's scope overlay is drawn shifted down onto it with the strip above filled black.
- **Latency:** `r.GTSyncType=1` and `r.OneFrameThreadLag=0` (DefaultEngine.ini) stop the game from running up to three frames ahead of the screen (the GPU is near its 16.7 ms budget at 60 Hz, so frames queued); mouse smoothing is off (DefaultInput.ini). Cost: the arena perf test went from 55.3 to 53.3 fps average. Test shots now log their game, render and GPU times.

## Hidden aim assist (after-shot)
Extra controller assist that only works while a shot's recoil plays out, so it hides in motion the gun already makes (`UWWOAimAssistComponent`, toggles in Settings, Aim Assist; amounts per weapon under its "Hidden assist" header).
- **Window:** opens on every shot (`NotifyShot` from `AddRecoil`), stays open while the recoil kicks or falls back and at least 0.15 s (for instant-recoil guns), then fades over 0.12 s. Works only with a target in the friction cone, scaled like the rest of the assist (none on mouse while Mouse assist is 0).
- **Recoil steer** (`RecoverySteer`, default 0.6): every bit of recoil motion (the instant kick, a timed kick and the fall-back) also bends the view toward the target by up to that fraction of the motion. No side-recoil bias (kept random on purpose).
- **Follow-up magnetism** (`FollowupMagnetism`, x1.5): shots fired in the window get a wider magnetism cone.
- **Kick friction / adhesion** (`KickFriction` x1.5, `KickAdhesion` +0.3) during the window.
- **Markers** ("Show hidden assist markers"): STEER, MAGNET (a hit only the wider cone caught) and STICKY (boosted friction or adhesion while you were turning) under the reticle, and a faint ring while the window is open.
- **Test:** `-WWOAutoTestHidden` (on `L_TestRange`) fires the rifle 2 degrees off a head with it off, then on, and logs where the aim ends and the damage done; `39_hidden_assist_markers` shows the markers. First run: off 4.9 degrees and no hits, on 2.5 degrees and hits.

## Character models (prototype)
Placeholder models from Fab (all free): VoltVine = Paragon Wraith, IronBark = Paragon Murdock, VeilSpore = Paragon Kallari (her trailing cloth tentacles and daggers hidden), Soldier = Robot scout R21, Swarm = Paragon Minions (Dusk melee), Tank = Paragon Steel. The links are in the LaminarFlow doc "Model Assets (for now)". The packs are about 11 GB and are not in git (`.gitignore`): on a fresh checkout add each to your Fab library and "Add to Project" (the robot needs "Show all projects" and version 5.4), then run `Tools/build_retargeters.py`. Paragon content may not use the PARAGON name to advertise the game.
- **How it works:** every body keeps the mannequin as an invisible animation driver, so all clips, layers, the hunch, hitboxes and grafts work as before. `UWWOBodyComponent::SetModel` adds the model as a second skeletal mesh whose `UWWOModelAnimInstance` copies the mannequin's pose through `FAnimNode_RetargetPoseFromMesh` and `RTG_Manny_<key>` (`/Game/WWO/Retarget`).
- **Config:** `Config/CharacterModels.json` (mesh, scale, hidden material slots, whether grafts or plain AttachBox boxes stay). Enemies set `Body->ModelKey`; the player and the menu preview use their flora's model. Allies stay on the mannequin. The flora models keep the flora grafts ("grafts": true; pinned to the mannequin's bones, so they sit where its body would be). On the Tank's model the plain armor boxes hide and the glowing weak spot stays.
- **Tools/build_retargeters.py** turns on the skeletal-mesh usage flags the models' materials need (the Mercenary ships without them and renders grey), then builds `IK_<key>` rigs (auto-generated chains) and `RTG_Manny_<key>` retargeters (auto-mapped and auto-aligned). Rerun it after adding a model.
- **Not done:** the hit flash and region/frame-part colors only reach the mannequin, so models don't flash or recolor yet. The first-person arms are still the mannequin.

## Weapon mods (draft)
A test version of the frames-and-mods design (LaminarFlow doc "Weapon Frames & Mods — Design"). The rifle and the hand cannon have all six slots open: barrel, magazine, stock, sight, chambering, grip. A mod's `frame` is the weapon id, so a frame's slots only list its own mods.
- **Mods:** 24 mods in `Config/WeaponMods.json` (12 per weapon), one with a stat trade-off only and one with a perk per slot. Stats are percent changes. Edit the file and restart to retune; it isn't an asset yet. `UWWOModData` is in `WWOData.h`.
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
- **In game:** D-pad Down or B applies the next saved config (`EWWOAction::NextConfig`).
- **Gun models and mod looks:** `Config/WeaponModels.json` gives each weapon a mesh from Epic's Lyra Starter Game (`Content/Weapons/`, copied from a Lyra project: pistol = hand cannon, rifle = rifle and sniper, shotgun), with its scale, rotation and offset from the sight point, the muzzle, and an anchor per mod slot. Each mod's `parts` in `WeaponMods.json` are simple shapes placed at its slot's anchor; `hide_ads` parts hide while aiming. `UWWOWeaponComponent::BuildModel` reads both on every rebuild (equip or mod change), so they retune with a restart. Without a model entry a weapon falls back to the Godot box model. Lyra content is under the Unreal Engine EULA (fine in an Unreal project).
- **Gun kick:** each weapon's Settings has "Vertical kick" (0 to 20, 1 = about 3.4 degrees of tilt) and "Horizontal kick" (-5 to 5, + swings the muzzle right) instead of Godot's single gun_kick (`GunKick` only seeds `VerticalKick` now, and an old save's "GunKick" loads as the vertical one). Looks only; recoil is what moves the aim. The gun test's `38_gun_handcannon_kick_diagonal` shows a diagonal kick.
- **Sight height:** each weapon's Settings section has "Sight dot height above gun (cm)" (`UWWOWeaponData::SightHeight`). It moves the gun and its mod parts down while the sight dot stays put, rebuilt live when the slider moves; Save as defaults keeps it.
- **Gun screenshots:** `-WWOAutoTestGuns` (best on `L_TestRange`, no enemies in the way) saves `38_gun_<weapon>_<hip|ads|plain|perk|perk_ads>`.
- **Autotest:** `-WWOAutoTestMods` (also the last step of `-WWOAutoTest`). It covers the rifle, then the hand cannon (stats checked against the mod data, perk set fired). It backs up and restores `Saved/SaveGames/Loadout.sav`.
