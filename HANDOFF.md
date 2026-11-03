# What Was Ours: gray-box prototype handoff

This is everything built in the first session, so a new session can pick up and keep iterating.
Read sections 1–3 first. The rest is reference.

---

## 1. What this is and how to run it

- **Game:** *What Was Ours*, a first-person shooter prototype inspired by **Destiny 2**. The player uses "flora" (a subclass-like system with abilities and traits), weapons, and "frame parts" (gear that changes mechanical stats).
- **Engine:** Godot **4.7.2** stable (installed via Homebrew at `/opt/homebrew/bin/godot`), Forward+ renderer, all GDScript.
- **Project folder:** `/Users/williambarr/Documents/WhatWasOurs`
- **Repo:** https://github.com/Willpatbarr/WWO_ProtoType (private, branch `main`).
- **Run the game:** `godot --path .` from the project folder, or open it in the Godot editor and press F5.
  It boots straight into the test level, with no main menu (by design).
- **Window:** 1600×900 base, with UI scaled by stretch mode `canvas_items` / aspect `expand`.
- **Controller:** the user plays on a **PS5 DualSense** over Bluetooth on macOS. Godot 4.5+ uses SDL3, so no library is needed.
  Support both controller and keyboard/mouse for everything.

### Where saved state lives (important)

| What | Where | Notes |
|---|---|---|
| The player's equipped loadout (weapon, grenade, abilities, frame parts) | `~/Library/Application Support/Godot/app_userdata/What Was Ours/loadout.json` | Written whenever something is equipped. **Tests must restore the user's real loadout afterward** (see section 9). |
| Tuning saved with "Save as defaults" in the settings menu | `res://saved_settings.json` (project root) | Gitignored. Loaded on startup, and Reset goes back to it. When the user says "lock these in", read this file and copy the values into the code and data defaults. **Delete it after any test that calls save.** |

---

## 2. How the user likes to work

- **Destiny 2 is the reference** for feel: aiming, aim assist, melee lunge, grenade recharge HUD, sprint/slide poses, menu cursor. When unsure how something should behave, match Destiny and say so.
- **Every tunable gets a slider** in the in-game settings menu. The user tunes by feel, then asks to "set these as defaults". New mechanics should ship with sliders.
- **Gray-box only:** primitive shapes and no real art. "Doesn't need to be pretty, just basic."
- **Prefers built-in and existing solutions** over writing from scratch, but no addons are installed yet. An earlier library survey suggested LimboAI (enemy AI), imgui-godot (dev menu), Debug Draw 3D and GUT/gdUnit4 for later.
- **Verify before reporting:** I tested every feature headlessly with scripted checks, and took windowed screenshots for anything visual. The user values seeing screenshots.
- **The user is often away** while work happens, so keep reports short, plain-language and outcome-first.
- **Asks for mockups or options** for layout and design questions before building (they picked layout "B" for the Flora tile from an HTML mockup). Offer 3–4 options with a recommendation.
- They may type quickly with typos ("gram parts" meant "frame parts"; "that job is scrollable" meant "that tab").

---

## 3. What's next: open threads

1. **Frame part groups: DONE (2026-10-31).** Parts give group points; each point moves every stat in its group by `Modifiers.point_value` (5%, slider in Global → Player). Groups: Agility (walk, sprint, crouch, slide speed, slide distance, melee time, throw distance), Aerial (jump height, air control), Dexterity (reload, aim-in, swap time, handling = ready time after sprint/mantle), Durability (health, shields, recovery delay). Mantle, coyote, recoil, sway and melee reach are no longer frame stats. Player health/shields/death/respawn were built alongside (`player_health.gd`, `PlayerSpawn` marker).
2. **Flora.** Picking a flora is built (2026-11-02): `flora/flora_data.gd` + `data/flora/*.tres`; grenades and abilities have a `flora` id and the loadout only offers the equipped flora's (`grenades_for_flora()`, `abilities_for_flora()`, `equip_flora()`). Three floras: **VoltVine** (was "Starter"; matches the LaminarFlow "VoltVine" doc: Volt Grenade (explosive), Shock Pool (pool), Snare Mine (`tripmine`: sticks, arms, blows up near an enemy and slows enemies via `Enemy.apply_slow`); Overgrowth, Thornfist, **Melee Guard** (triggered: landing a melee hit cuts incoming melee damage 50% for 4 s; Swarm bites/leaps and the Tank slam count as melee, `PlayerHealth.take_damage(amount, melee)`); ult = massive blast; `graft_color` makes every body graft blue. Seedburst still exists in code but belongs to no flora and is locked) and **VeilSpore** (from the LaminarFlow "VeilSpore" doc):
   - Grenades (`GrenadeData.kind`): Lingering Pool (`pool`: small blast + `flora/spore_pool.gd` damage puddle), Poison Spike (`spike`: no fuse, bounces until it touches a target, then hits + `flora/damage_over_time.gd` poison; it curves toward targets within `seek_radius` 7 m at `seek_strength` 160°/s and counts as touching within 0.45 m of a body, so throws up to ~3 m off still connect), Spore Blast (regular explosive).
   - Abilities (generic names, the user's choice): Spore Fog Damage (passive: `flora/spore_fog.gd` fogs hurt Swarm and Soldiers, not the Tank), Spore Dash (active: `player.launch()` backwards + thick fog that blocks enemy sight), Spore Patches (triggered: grenades sprout `flora/spore_patch.gd` mushrooms that emit fog where they touch down).
   - Ult "Veil" (`UltManager._run_veil`): a quick third-person flash, a big thick fog, then `player.veil_left`: while in any spore fog you're invisible (`player.is_invisible()`; enemies drop their alert) and melee does ×2.5. HUD shows "VEILED".
   - **IronBark (2026-11-02, from its LaminarFlow doc):** grenades Wall Grenade (`wall`: `flora/bark_wall.gd`, a bark slab facing the thrower, in the `cover` group, sinks after `wall_time`), Iron Grenade (explosive), Blinding Grenade (`blind`: `Enemy.apply_blind`, blinded enemies stand still and can't attack). Abilities Root Bombs (active: two big impact bombs), Shield Share (passive: explosions refill shields of nodes in the `teammates` group, i.e. the AI teammates), Extra Grenade (passive: `GrenadeThrower.max_charges()` = 2; `charge` now counts charges). Ult "Entrench" (`UltManager._entrench`, quick flash): `player.entrench_left` roots you (can look and shoot), blocks all damage, grenade recharge ×4; `flora/entrench_roots.gd` ring of roots; HUD "ENTRENCHED". The ult's quick third-person flash is shared with Veil (`_run_quick`).
   - **Grafts** (body markers, `CharacterModel.show_flora`), one color per flora (`FloraData.graft_color`): VoltVine blue (hip grenade, Overgrowth shin ovals, Thornfist forearm ovals, Melee Guard knuckle studs); VeilSpore purple (hip grenade, Spore Fog Damage shoulder mushroom caps, Spore Dash calf pods, Spore Patches belt mushrooms); IronBark bark brown (hip grenade, Root Bombs on the back, Shield Share chest plate, Extra Grenade second hip grenade).
   - **Traits (2026-11-02):** `flora/trait_data.gd` + `data/traits/*.tres`, 5 per flora (from each flora's doc). Equip up to `Loadout.TRAIT_SLOTS` (4, placeholder) in the character menu's Traits row; switching flora clears them. A trait has stat `mods` (added to Modifiers as "trait:<id>"; new stats `shield_regen` and `melee_damage`) and/or an `effect` + `amount` read with `Loadout.trait_bonus()` / `Loadout.bonus(tree, ...)`: fog_longer, more_bounce, poison_damage, slow_longer, wall_longer, impact_fuse, blast_bigger (new stat `grenade_recharge` too). "Radar Veil" (no_radar) does nothing yet (no radar/PvP). Settings → Flora → Traits has a page per trait.
   - No body markers for VeilSpore abilities yet (the flora test markers only cover Starter's).
   Traits should plug into `Modifiers` as a source (`"trait:<id>"`), the same way frame parts do.
3. **From the original plan, not built yet:**
   - Pause menu (resume / restart / quit / sensitivity / FOV). The settings menu currently covers sensitivity and FOV.
   - Dev menu: spawn enemies at the crosshair, kill all, god mode, infinite ammo, time scale, FPS counter, debug overlay.
   - Enemies: **Soldier, Swarm and Tank DONE (2026-11-02).** Still to do: an enemy spawner, and the Burn status effect (Slow is done, from the Tank's slam).
     - **Shared base:** `enemies/enemy.gd` (`Enemy`): health, hits, damage numbers, flash, death, respawn, crit shapes pinned to bones (`pin_shape`), seeing/turning helpers. Types override `_data()`, `_setup()`, `_think()` (returns the wanted velocity), `_animate()`, `_absorb()` (shields). All use `CharacterModel` (the same mannequin as the player) with their own colors and `model_scale`. Tuning resources extend `EnemyData`.
     - **Swarm:** `enemies/swarm.gd`, 0.95 scale (the hunch makes it read shorter), yellow-green, hunched via `characters/hunch_modifier.gd` (a SkeletonModifier3D; the head hitbox updates on its `modification_processed` signal because script reads of bone poses don't include the bend). 40 hp, runs at 8 m/s, leaps from 4 m (15 dmg), bites 10 every 0.8 s, packs spread out. A pack of 6 is placed around (-23, -24).
     - **Tank:** `enemies/tank.gd`, 1.55 scale, olive drab with dark armor plates. 1200 hp + 300 shield (bubble; no crits while up; refills after 6 s). Crit only on the glowing back power pack. Lobbed splash shells at range; arms-up slam within 4 m (40 dmg + Slow 50% for 2.5 s), then stands still 1.5 s. One placed at (-24, -44).
     - **Slow:** `player.apply_slow()`, `slow_multiplier()`; HUD shows "SLOWED". Enemy projectiles support `gravity` and `splash_radius`. HUD target bar shows a shield bar for the Tank.
     - Settings tabs: Soldier, Swarm, Tank (each with "AI on" and a reset button).
     - Model: Quaternius Universal Animation Library mannequin (CC0), `enemies/models/ual/UAL1_Standard.glb`, recolored red. 43 animations; the free version has no strafe animations.
     - `enemies/soldier.gd` / `soldier.tscn`: keeps 15–25 m, strafes, windup glow then 3-shot bursts, group burst spacing, cover at low health (nodes in the `cover` group), head hitbox follows the head bone. AnimationTree: legs idle/walk + upper-body aim + hit flinch.
     - `enemies/enemy_projectile.gd`: slow glowing shot, hurts the player via the `player_health` group.
     - Tuning lives in `data/enemies/soldier.tres` (`SoldierData`), with a Global → Soldier settings tab (incl. "AI on" toggle).
     - Three soldiers stand by the navy cover boxes around (22–31, −24 to −38), out of sight range from spawn.
     - **Player body (2026-11-02):** the player uses the same mannequin (`characters/character_model.gd`, `CharacterModel`), steel blue. In first person it's shadow-only; `player._update_model()` picks Idle / Walk / Jog_Fwd / Sprint / Crouch / Jump from movement state. The character menu preview uses it too. It doesn't hold the gun yet.
     - **Part colors:** `characters/region_color.gdshader` colors head / arms / core / legs by bone. Each region shows its equipped frame part's spike-group color (`FramePartData.GROUP_COLORS`: Agility green, Aerial sky blue, Dexterity gold, Durability steel gray; Standard keeps the base steel blue). `CharacterModel.follow_loadout()` updates on equip, for the player body and the menu preview.
     - **Flora markers (test visuals, 2026-11-02):** `CharacterModel.show_flora()` pins meshes to bones: grenade = ball on the right hip (grenade color); Seedburst = 3 smaller balls on the left hip (grenade color); Overgrowth = ovals on the front of both shins; Thornfist = ovals on the outside of both forearms (ability colors). Rebuilt on every loadout change.
     - **Ult (2026-11-02):** `ult_manager.gd` (`UltManager`, F / R3). For now a massive grenade blast (600 dmg, 12 m) centered on you, in the grenade's color. Sequence: controls lock and the gun hides (`player.ulting`), a temporary camera glides to third person behind you, the body plays Spell_Simple_Enter → Idle (an orb grows between the hands) → Shoot, the blast fires (a `Grenade` exploded on the spot), then the camera glides back. No damage taken while it plays. Charges over 60 s (starts full). HUD box "Ult (F)"; Global → Ult settings tab with a "Fill ult" button.
     - **Tuning pass (2026-11-02):** crouch in the air after a sprint jump queues a slide on landing (`_slide_queued`), and crouch within 0.2 s of landing still slides. Hand cannon: floating ring sight (`SightRing`, "ring" part type in the model builder) and kick-and-recover recoil (`recoil_kick_time` / `recoil_recovery` / `recoil_recovery_time` on WeaponData, applied by `player.add_recoil`). Headshots tightened: `crit_magnetism` shrinks the magnetism cone for heads (rifle 0.35, hand cannon 0.2, sniper 0.15), magnetism angles and ADS boost lowered, dummy head 0.45 → 0.32 m, Soldier head box smaller. saved_settings.json was updated with the same weapon values.
     - **Melee arm (2026-11-02):** melee now drops the gun out of view and swings a gray-box first-person right arm (`gun.gd` `_build_arm` / `_apply_melee_arm`, keyframes `PUNCH_KEYS` / `SWING_KEYS`). If a target is in reach when you press melee (the same check that triggers the lunge), it's a straight punch that stops on impact with a small view jolt; otherwise a wide hook that follows through. The arm takes the equipped Arms frame part's color.
     - **Grenade throw (2026-11-02):** `GrenadeThrower.LAUNCH_ANGLE` lowered from 20° to 7°, so throws are fast and flat (~29 m/s at the default distance) instead of a lob. The throw distance setting still sets where a level throw lands.
     - **Arena map (2026-11-03):** the game now starts in `arena.tscn` (the project's main scene); the old gray-box test range is still `main.tscn`. The map is built from the Quaternius Modular Sci-Fi MegaKit (CC0, free Standard version, `maps/scifi/`, textures in `maps/scifi/Textures/`) by `tools/build_scifi_map.gd` into `maps/scifi_arena_map.tscn`: a 64 x 48 m grid of 4 m tiles with a spawn room (south), a courtyard with cover and two 2 m raised platforms, a pillared north hall (the Tank) and a west flank corridor (the Swarm pack). Every piece has trimesh collision; invisible boundary walls; a baked navigation mesh. `tools/build_arena.gd` copies main.tscn's systems onto the map and places the player, dummies and enemies. Rebuild both after changing the layout. Enemies path around walls with `Enemy.path_direction()` (straight line where there's no navmesh).
     - **AI teammates (2026-11-03):** `allies/ally.gd` / `ally.tscn` (`Ally`, teal mannequin, tuning `data/allies/ally.tres`, Settings → Allies). Follows the player (3-7 m, navmesh), shoots the nearest visible enemy (hitscan + tracer), has shields/health with regen, goes down and gets back up next to you after 8 s. Groups: `teammates` (alive) and `all_teammates`. Enemies now target the closest of player/teammates (`Enemy._retarget`, still stored in `_player`) and hurt it through `Enemy.damage_target()`; projectiles, splash and the Tank slam hit teammates too. Shield Share calls `Ally.restore_shield()`. Two are placed by the player in both maps.
     - **Waves (2026-11-03):** `wave_manager.gd` (`WaveManager`, Settings → Waves). If there are `enemy_spawn` Marker3Ds (the arena has 8; meta `kinds` limits which enemies use each), it clears the hand-placed enemies, counts down, then runs `WAVES` (6 Swarm → +2 Soldiers → 8 Swarm + 3 Soldiers → 4 Soldiers + Tank → 10 Swarm + 3 Soldiers + Tank; repeats bigger after). Spawns away from you, enemies come alerted and don't respawn. Win: clear `waves_to_win`. Lose: you die with no teammate up (run restarts). HUD line at top center. `enabled` 0 = sandbox (main.tscn has no spawn points, so it stays a sandbox).
     - **Large arena (2026-11-03):** `arena_large.tscn` (map `maps/scifi_arena_large_map.tscn`, 24 x 16 tiles, about 2x the small one). Build with `godot --headless -s tools/build_scifi_map.gd -- large` then `tools/build_arena.gd -- large` (zsh doesn't split variables, so pass `-- large` literally). Ground: spawn room, atrium with a 4 m central tower, north hall, east hallway with three side rooms. Upper (4 m): west wing with two rooms and an overlook hallway, two two-flight staircases with landings, and a bridge to the tower. Stairs look like stairs but collide as smooth ramps (the player has no step-up). Switch maps in Settings → Maps (small arena, large arena, test range).
     - Anything shootable is in the `all_targets` group; use `Target.is_target(node)` instead of `is Target`. Damage numbers moved to `damage_numbers.gd`.
   - Health/ammo pickups were skipped on purpose.
4. **Smaller ideas offered and not yet taken:**
   - Ground vault for waist-high obstacles. Mantle only triggers in the air today.
   - A slider for menu cursor speed.
   - A hotkey or touchpad shortcut to reset targets.
   - Weapon swap speed as a stat.
   - Making test runs ignore the real controller (see section 9).
   - Overgrowth's speed bonus could move onto the modifier system.

---

## 4. Controls

| Action | Keyboard / mouse | DualSense | Input action |
|---|---|---|---|
| Move / look | WASD / mouse | Left / right stick | move_*, look_* |
| Jump (double; triple with Overgrowth) | Space | ✕ | jump |
| Sprint (press once; latches until you stop) | Shift | L3 | sprint |
| Crouch (toggle) / slide (tap while sprinting) | C or Ctrl | ◯ | crouch |
| Aim down sights (hold) | Right mouse | L2 | aim |
| Fire | Left mouse | R2 | fire |
| Reload | R | □ | reload |
| Melee (always lunges toward a hit target) | V | R1 | melee (key) / melee_pad (R1) |
| Grenade | G | L1 | grenade (key) / grenade_pad (L1) |
| Ult | F | L1 + R1 together | ult (key); the pad chord is `controller_chord.gd`: a bumper waits 0.1 s for the other one |
| Ability slot 1 / 2 (active abilities: Thornfist, Spore Dash) | 1 / 2 | D-pad left / right | ability_1 / ability_2 |
| Next / previous weapon | Mouse wheel | △ (next) | weapon_next / weapon_prev |
| Settings menu | Tab / Esc | Options | menu |
| Back out one level in menus | Esc | ◯ | menu_back (Godot's `ui_cancel` has no controller button!) |
| Cycle Global settings tabs | Q / E | L1 / R1 | tab_prev / tab_next |
| Character menu | I | Touchpad click (**joypad button 20** in Godot 4.7, not 15) | character_menu |
| Menu cursor | Mouse | Left stick moves, ✕ clicks (hold to drag sliders), right stick scrolls | (MenuCursor) |

---

## 5. Project layout

### Scene tree (`main.tscn`)

The order matters for `_ready` and `_input` (later siblings get input first):

```
Main
├─ Loadout          loadout.gd        equipped items + save file (must be first: others read it in _ready)
├─ Modifiers        modifiers.gd      stat modifier system (frame parts today, traits later)
├─ AbilityManager   ability_manager.gd
├─ WorldEnvironment, Sun, Floor (100×100 m CSG), level props (CSG, each its own color, none floor-colored)
├─ Target* ×6       target.tscn       killable dummies (2 of them strafe)
├─ Player           player.gd         CharacterBody3D (capsule)
│  └─ Camera3D
│     └─ Gun        gun.tscn/gun.gd   holds the equipped weapon model; creates the AimAssist child
├─ GrenadeThrower   grenade_thrower.gd
├─ TargetManager    target_manager.gd
├─ HUD              hud.gd            CanvasLayer
├─ SettingsMenu     settings_menu.gd  CanvasLayer, layer 10
└─ CharacterMenu    character_menu.gd CanvasLayer, layer 11 (after SettingsMenu, so it sees input first)
```

Cross-node references use `@export var x: Type` with `node_paths` set in `main.tscn`.

### Scripts

| File | What it does |
|---|---|
| `player.gd` | All movement: walk, latching sprint, crouch toggle, tap-to-slide (exact slide distance), double/triple jump with exact heights, separate ascent/descent gravity, apex hang, coyote time, air momentum and air control, landing dip, stride-based head bob (deeper and slower when sprinting), mantling, melee lunge (3D, with camera pull), recoil application. Reads stats through `_m(stat, base)`, which applies modifiers. |
| `gun.gd` | The equipped weapon: swaps models, reads weapon stats live, Destiny ADS (zoom as true magnification, horizontal FOV, fixed viewmodel FOV), hitscan shooting with pellets and spread, semi/auto fire, recoil, reload and its animation, melee and lunge targeting, every gun pose (sprint, slide, air lag, mantle, throw, swap, reload, melee), muzzle flash, impact marks, reticle target detection. |
| `aim_assist.gd` | Controller aim assist: bullet magnetism (with head bias), friction, optional adhesion. Scales with the weapon's assistance stat, range falloff (stretched by zoom) and an ADS boost. Detects controller vs mouse automatically. |
| `target.gd` / `target.tscn` | Dummy with a body and a head (crit) collision box. Health, death (shrinks, collision off, leaves the "targets" group), revive, damage numbers (`Label3D`), optional strafing. |
| `target_manager.gd` | Target health setting, auto-respawn timer, reset all. |
| `grenade_thrower.gd` / `grenade.gd` | Throw arc solved for a set flat-ground distance, recharge, fuse, blast with falloff (full damage at center, half at edge) that walls block. Seedburst splits into seeds. |
| `ability_manager.gd` | Runs the 2 equipped abilities, with effect hooks: `speed_multiplier()`, `extra_jumps()`, `melee_multiplier()`, `seedburst()`. |
| `modifiers.gd` / `stat_modifier.gd` | The stat system: `final = (base + ΣFLAT) × (1 + ΣPERCENT_ADD) × ΠMULTIPLY`. Modifiers are stored by source; equipping adds a source and unequipping removes it. `STAT_NAMES` lists the 17 stats that can be modified. |
| `loadout.gd` | Loads every data file from `data/*`, tracks what's equipped, saves and loads `user://loadout.json`, emits `changed`. |
| `hud.gd` | Reticle (red over a target, fades when aiming), hit/crit/kill markers, ADS vignette, sniper scope overlay, target health bar under the reticle, ammo plus weapon name plus reload bar, grenade and ability squares with their charge states, aim assist debug cones (magnetism, friction, lunge). |
| `settings_menu.gd` | Two-pane settings (see section 7). |
| `character_menu.gd` | Loadout screen (see section 7). |
| `menu_cursor.gd` | Shared Destiny-style cursor for both menus. |
| `*_data.gd` | Resource classes: `WeaponData`, `GrenadeData`, `AbilityData`, `FramePartData`, `StatModifier`. Each has an `unlocked` field (always true for now). |
| `viewmodel.gdshader` | Gun shader. Draws with its own FOV (`fov_ratio` uniform) and pushes depth toward the camera so the gun never clips into walls. Per-part color and glow use **instance uniforms**. |
| `vignette.gdshader`, `scope.gdshader` | HUD overlays. |
| `viewmodel_material.tres` | One shared material for every gun part. `gun.gd` sets `fov_ratio` on it. |

### Data (all `.tres` resources, loaded automatically from their folders)

- `data/weapons/` (rifle, handcannon, shotgun, sniper): every weapon stat, including per-weapon aim assist and hip/ADS positions.
- `data/grenades/` (green, red, blue): color, damage, blast radius, and multipliers on the global throw, recharge and fuse values.
- `data/abilities/` (overgrowth, thornfist, seedburst): kind plus effect parameters.
- `data/frame_parts/` (12 parts): generated, don't hand-edit (see below).

### Builder tools (run headless and they regenerate assets)

- `godot --headless --path . -s tools/build_weapon_models.gd` writes `weapons/models/*.tscn`.
  Each model is a parts table. The origin is the sight point. Every model needs `Magazine` and `MuzzleFlash`, and may have `SightDot`.
- `godot --headless --path . -s tools/build_frame_parts.gd` writes `data/frame_parts/*.tres` from the `PARTS` table.

---

## 6. Systems: current behavior and defaults

### Movement (player.gd; Global settings tabs)

| Area | Settings and behavior |
|---|---|
| Speeds | Walk 6, sprint 12, crouch 2.5 |
| Slide | Starts at 12 and covers exactly 8 m, then you stand up |
| Jumps | Jump 1 is 2.0 m, jump 2 is 2.3 m. Heights are exact whatever the gravity. Ascent gravity 20, descent gravity 32. |
| Air | Air control 5 (momentum carries; the air jump redirects momentum). Apex hang 0.5, coyote time 0.12 s (missing it costs the ground jump). |
| Mantle | Reach 2.0 m, time 0.35 s. Triggers in the air when pushing forward at a wall with a flat top and room to stand. |
| Camera | Mouse sensitivity 2.0, stick sensitivity 5.0. Head bob 1.5. Walk stride 1.6 m, sprint stride 2.8 m. Landing dip 1.0. Horizontal FOV 100. |

### Weapons (gun.gd and WeaponData; Specific → Weapons, 30 sliders each)

| | Rifle | Hand Cannon | Shotgun | Sniper |
|---|---|---|---|---|
| Fire | Auto, 600 RPM | Semi, 140 RPM | Semi, 65 RPM, 8 pellets | Semi, 70 RPM |
| Damage / crit | 20 / ×1.6 | 55 / ×2 | 18 per pellet / ×1.2 | 160 / ×2.5 |
| Magazine / reload | 30 / 2.0 s | 12 / 2.2 s | 5 / 2.6 s | 4 / 2.9 s |
| Range | 120 m | 60 m | 15 m | 300 m |
| Zoom | 1.5× (red dot) | 1.4× (iron sights) | 1.2× (bead) | 6× (scope overlay; model hides) |
| Notes | | | Pellets merge into one hit per target | 3° hip spread |

- All weapons: ADS move speed multiplier, recoil (kicks the real view), gun kick, viewmodel FOV, sway, bob and slide tilt are per weapon.
- Reload cancels on sprint, is cancelled by melee or a grenade throw, and blocks ADS. It refills at 75% through the animation.
- The magazine auto-reloads when empty or when you pull the trigger on empty.

### Aim assist (per weapon, plus Global mouse assist and debug cones)

Rifle defaults: stat 70, magnetism 2.5°, friction strength 0.45, friction cone 3.7°, adhesion 0 (off, like Destiny), range 40 m, ADS boost 1.4. Mouse assist is 0. The debug cones are on by default (`show_debug` = 1).

### Melee (Global → Melee)

- 0.6 s swing, 2.5 m reach, 80 damage, no crits.
- **Always lunges** at any target the swing would hit, or one inside the lunge cone (range 5 m, angle 20°, speed 18 m/s). The lunge is 3D: vertical pull 1.0 lifts you mid-jump.
- Camera pull 0.5. The minimum lunge time is 0.15 s, so the camera swings even point-blank.

### Grenades

- Global: throw distance 18 m, recharge 8 s, fuse 1.5 s.

  | Grenade | Damage | Radius |
  |---|---|---|
  | Green | 160 | 3 m |
  | Red | 110 | 4.5 m |
  | Blue | 75 | 6 m |

- Each grenade has multipliers on the Global throw, recharge and fuse, shown in the menu like "×1.00 = 8.0 s".
- The grenade glows in its color.
- The HUD square goes gray and fills with color as it recharges.

### Abilities (2 equipped; picking one that's in the other slot swaps them)

| Ability | Kind | Effect | HUD |
|---|---|---|---|
| Overgrowth | Passive | ×1.15 walk/sprint/crouch speed, +1 jump | Half-grayed |
| Thornfist | Active | ×2 melee damage for 8 s, 20 s recharge | Like the grenade square, plus a pulsing border while active |
| Seedburst | Triggered | Grenade bursts into 3 seeds at 0.5× damage and radius, 0.6 s fuse | Dark until thrown; lit until the last seed explodes |

### Frame parts (Head / Arms / Legs / Core; Standard = no changes and is the default)

Rule (2026-11-02): every non-Standard part has a spike (+2) in one group and a half spike (+1) in another. Nothing lowers a group for now. All 12 spike/half pairs appear exactly once, 3 parts per slot.

| Slot | Parts (group points) |
|---|---|
| Head | Focus: Dexterity +2, Aerial +1 · Guard: Durability +2, Dexterity +1 · Scout: Aerial +2, Dexterity +1 |
| Arms | Quick: Dexterity +2, Agility +1 · Braced: Dexterity +2, Durability +1 · Strong: Agility +2, Dexterity +1 |
| Legs | Stride: Agility +2, Aerial +1 · Spring: Aerial +2, Agility +1 · Sturdy: Durability +2, Agility +1 |
| Core | Light: Agility +2, Durability +1 · Armored: Durability +2, Aerial +1 · Thruster: Aerial +2, Durability +1 |

Note: "Save as defaults" stores frame part values in saved_settings.json, which overrides the .tres files. After rebuilding parts, drop the stale `frame/` entries from that file.

### Player health (player_health.gd; Global → Player)

100 health, 100 shields. Damage hits shields first. After a 4 s recovery delay, health refills (40/s), then shields (60/s). Death disables the player and respawns them at `PlayerSpawn` after 2 s; falling below y = −30 also kills. HUD: shield and health bars bottom center, red damage flash, death screen. Test buttons: "Take 30 damage" and "Die". God mode slider. Weapon swap time (0.35 s) and ready time after sprint (0.1 s) live on the same tab.

### Targets

- 6 dummies with 200 HP. They die and shrink away.
- Respawn is off (0) by default. "Reset targets" is a button on the Global → Targets tab.

### The user's current loadout (don't clobber it in tests)

Rifle, Blue grenade, Seedburst (slot 1) + Overgrowth (slot 2), all Standard frame parts.

---

## 7. Menus

### Settings menu (`settings_menu.gd`)

- Opened with Tab / Esc / Options. It pauses the game.
- **Left pane, Global:** tabs Walk/Sprint, Camera, Jump, Crouch/Slide, Melee, Grenades, Targets, Aim Assist.
- **Right pane, Specific:** tabs Equipped, Flora (sub-tabs Grenades / Abilities / Traits), Weapons, Frame Parts.
  These are item browsers: a list of items → one item's slider page, with ‹ Back and Reset item.
- **Sections:** everything is a *section* keyed like `global/Jump`, `weapon/rifle`, `grenade/red`, `ability/thornfist` or `frame/strong_arms`.
- **Rows:** a row is `{id, obj, prop, label, min, max, step, show}` and binds a slider to any object's property (a node or a Resource).
  `id` is usually `prop`. Frame part rows use `modN_<stat>` because every modifier edits a field called `value`.
- **Save file:** "Save as defaults" writes `{section: {row id: value}}` to `res://saved_settings.json`.
- **API used by tests:** `set_value(key, id, v)`, `get_value`, `reset_section`, `reset_all`, `save_defaults`, `_open_item(browser, key)`.
- **Look:** sliders have `scrollable = false`, so the wheel and right stick scroll the page instead. Tab text is 17 px so all 8 Global tabs fit, and the Global pane is 1.15× wider.

### Character menu (`character_menu.gd`)

- Opened with the touchpad or I. It pauses the game.
- **Left:** three tiles (Flora, Weapons, Frame Parts). A tile expands to fill the area and has a Collapse button. Expanded content scrolls.
- **Right:** a rotating capsule preview in its own SubViewport.
- **Flora tile:** layout "B", labeled rows Flora / Abilities (Grenade, Ability 1, Ability 2) / Traits. Clicking Grenade or an Ability opens an inline picker row.
- **Weapons tile:** the equipped weapon plus a 2×2 picker with stats.
- **Frame Parts tile:** a row per slot plus a "Net effect" summary.
- **Back:** circle/Esc closes a picker, then the tile, then the menu.

### Menu cursor (`menu_cursor.gd`)

- The left stick moves it with a squared response at 1400 px/s, ✕ clicks (holding drags sliders), and the right stick scrolls the ScrollContainer under it.
- The mouse drives the same cursor, and the OS pointer is hidden while it's active.
- Buttons use `focus_mode = NONE`, so the cursor, not focus navigation, drives everything.

---

## 8. Godot gotchas learned (save yourself the debugging)

- **Typed inference fails on untyped access.** `var x := player.some_prop`, where `player` is typed `CharacterBody3D`, is a parse error ("cannot infer type"). Annotate explicitly (`var x: float = ...`) or use `class_name` types. That's why `Gun`, `AimAssist`, `Loadout` and so on have class names, and why gun.gd has a `_m()` helper.
- `seed` is a built-in function, so don't use it as a variable name. (I renamed it `pod` in grenade.gd.)
- **Node names can't contain "/"** (it's replaced with "_"). For TabContainer titles, use `set_tab_title()`.
- `ui_cancel` has **no controller button** by default. That's why the `menu_back` action exists.
- The DualSense touchpad is **`JOY_BUTTON_TOUCHPAD` = 20** in Godot 4.7.
- **Horizontal FOV:** set `Camera3D.keep_aspect = KEEP_WIDTH` so `fov` is horizontal, like Destiny.
- **Viewmodel shader:**
  - Writing `POSITION.z` in the vertex shader broke near-plane clipping and smeared the gun across the screen. Write `DEPTH = mix(FRAGCOORD.z, 1.0, 0.9)` in the fragment shader instead (reversed-Z).
  - Instance uniforms (`instance uniform vec4 color`) let one material color many parts.
- Moving a CSG node at runtime doesn't reliably update its collision for physics queries in tests. Use a StaticBody instead.
- `HSlider` eats mouse-wheel events unless `scrollable = false`.
- Godot releases every held action when its window loses focus, so `Input.action_press` in **windowed** tests doesn't stick. Set state directly, or use `parse_input_event`.

---

## 9. Testing approach (the scripts aren't in the repo; recreate them as needed)

Pattern: a `SceneTree` script run with `godot --headless --path . -s <script>`:

```gdscript
extends SceneTree
func pf(n):
	for i in n: await physics_frame
func _initialize():
	var scene = load("res://main.tscn").instantiate()
	var p = scene.get_node("Player"); p.position = Vector3(10, 1, -2)  # set BEFORE add_child
	root.add_child(scene); await pf(10)
	var g = scene.get_node("Player/Camera3D/Gun")
	# ...drive inputs with Input.action_press/release, call methods, print results...
	quit()
```

Gotchas:
- **Await 2 frames after a press.** `process_frame` and `physics_frame` fire *before* nodes process, so read results 2 frames after pressing.
- `get_tree().current_scene` is **null** in `-s` scripts, so spawned effects land on `root`, not the scene.
- **Headless viewport size is bogus (40000 px).** Run UI layout and cursor tests **windowed** (`godot --path . -s ...`). They pop a window briefly.
- **Screenshots:** `await RenderingServer.frame_post_draw`, then `root.get_viewport().get_texture().get_image().save_png(path)`. Windowed only.
- **Watch the shared state:**
  - Equipping writes the user's `loadout.json`. Save the original (`lo.weapon`, `lo.grenade`, `lo.ability(0/1)`, `lo.frame_part(slot)`) and restore it at the end.
  - `save_defaults()` writes `saved_settings.json`, so delete it afterward.
- **Real controller:** the user's DualSense stays connected, and test processes read it too. One batch had three odd results that all passed when rerun alone, cause unconfirmed. If tests are ever flaky, consider making tests ignore joypads.
- **Weapon stats in tests:** forwarded weapon stats on `Gun` (e.g. `g.recoil_up = 0`) write through to the WeaponData resource. That's runtime only and harmless.

---

## 10. History (in order)

1. Library research → no addons used. Built a basic FPS controller on Godot's CharacterBody3D template.
2. Sprint, double jump, crouch, slide, and the first settings menu. Head bob, slide made tap-to-start, crouch made a toggle, sprint strides tuned.
3. Air momentum, landing dip, apex hang, coyote time. The user's tuned values were set as defaults, and "Save as defaults" was added.
4. Tabbed settings that remember the last tab. Gun model, Destiny ADS, more level objects, circle closes menus.
5. Gun poses for sprint, jump and slide (slide later changed to point up, with a tilt slider). Shooting, targets, reticle recognition, hit and crit markers.
6. Aim assist (magnetism, friction, adhesion, debug cones). Ammo and reloading, then melee, then lunge (3D, camera pull, always on hit).
7. Killable targets with health bars and reset. Character menu (touchpad), tiles, controller cursor, Flora layout B with scrolling.
8. Mantling, then the grenade picker, then real grenades with recharge and per-grenade stats.
9. Two-pane Global/Specific settings with a shared cursor. Flora abilities (3, two equipped, three HUD states).
10. Three new weapons with a weapon data system and model builder. Frame parts and the stat modifier system.
11. Pushed to the private GitHub repo `WWO_ProtoType`. This handoff.
