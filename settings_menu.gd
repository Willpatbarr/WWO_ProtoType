extends CanvasLayer
# Settings menu: two panes side by side.
# - Global (left): settings that aren't tied to a piece of gear: movement, camera,
#   melee, grenade base values, targets, debug.
# - Specific (right): settings for individual items. Tabs: Equipped, Flora, Weapons,
#   Frame Parts. Flora has sub-tabs (Grenades, Abilities, Traits). Pick an item from a
#   list to open its sliders; Back (or circle / Esc) returns to the list.
# Tab / Esc / Options opens it; inside, circle / Esc / Options back out one level at a
# time, then close. L1/R1 (Q/E) cycle the Global tabs. Pauses the game while open.
# Controller: the shared MenuCursor (left stick moves, cross clicks and drags sliders,
# right stick scrolls).
# Every slider belongs to a "section" (a Global tab or one item). "Save as defaults"
# writes every section to saved_settings.json; the game loads that file on startup,
# and the Reset buttons go back to it.

@export var player: CharacterBody3D
@export var gun: Gun
@export var target_manager: TargetManager
@export var grenade_thrower: GrenadeThrower
@export var loadout: Loadout
@export var health: PlayerHealth
@export var mods: Modifiers
@export var soldier_data: SoldierData
@export var swarm_data: SwarmData
@export var tank_data: TankData
@export var ally_data: AllyData
@export var ult: UltManager

const SAVE_PATH := "res://saved_settings.json"

# section key -> {"title": String, "rows": Array}
# A row is {id, obj, prop, label, min, max, step, show} or {"header": "Text"}.
# `id` names the row in saves and resets (usually the same as prop; frame part rows all
# edit a modifier's "value", so they get ids like "mod0").
# `show` (optional) turns a value into the text beside the slider.
var _sections := {}
var _global_keys: Array[String] = [] # in tab order
var _defaults := {} # section key -> {prop: value}
var _live := [] # sliders currently on screen: {slider, label, obj, prop, row}

var _global_tabs: TabContainer
var _specific_tabs: TabContainer
var _browsers: Array[VBoxContainer] = []
var cursor: MenuCursor


func _ready() -> void:
	layer = 10 # above the HUD
	process_mode = Node.PROCESS_MODE_ALWAYS # keep working while the game is paused
	_register_sections()
	_record_defaults()
	_load_saved()
	_build()
	visible = false


# --- What's in the menu ---------------------------------------------------------

func _row(obj: Object, prop: String, label: String, lo: float, hi: float, step: float, show := Callable()) -> Dictionary:
	return {"id": prop, "obj": obj, "prop": prop, "label": label, "min": lo, "max": hi, "step": step, "show": show}


func _register_sections() -> void:
	var p := player
	var g := gun
	var aa := gun.aim_assist
	var th := grenade_thrower
	_add_global("Walk / Sprint", [
		_row(p, "walk_speed", "Walk speed", 1.0, 15.0, 0.5),
		_row(p, "sprint_speed", "Sprint speed", 1.0, 25.0, 0.5),
	])
	_add_global("Camera", [
		_row(g, "hip_fov", "Field of view (horizontal)", 55.0, 120.0, 1.0),
		_row(p, "mouse_sensitivity", "Mouse sensitivity", 0.1, 10.0, 0.1),
		_row(p, "stick_sensitivity", "Stick sensitivity", 0.5, 10.0, 0.1),
		_row(p, "bob_intensity", "Head bob intensity", 0.0, 3.0, 0.1),
		_row(p, "walk_stride", "Walk stride length (m)", 0.5, 5.0, 0.1),
		_row(p, "sprint_stride", "Sprint stride length (m)", 0.5, 6.0, 0.1),
		_row(p, "landing_dip", "Landing dip", 0.0, 3.0, 0.1),
	])
	_add_global("Jump", [
		_row(p, "jump_height_1", "Jump 1 height (m)", 0.2, 5.0, 0.1),
		_row(p, "jump_height_2", "Jump 2 height (m)", 0.0, 5.0, 0.1),
		_row(p, "ascent_gravity", "Jump ascent speed", 2.0, 80.0, 1.0),
		_row(p, "descent_gravity", "Jump descent speed", 2.0, 80.0, 1.0),
		_row(p, "air_control", "Air control", 0.0, 40.0, 0.5),
		_row(p, "apex_hang", "Apex hang", 0.0, 0.9, 0.05),
		_row(p, "coyote_time", "Coyote time (s)", 0.0, 0.3, 0.01),
		_row(p, "mantle_reach", "Mantle reach (m, 0 = off)", 0.0, 3.5, 0.1),
		_row(p, "mantle_time", "Mantle time (s)", 0.1, 1.0, 0.05),
	])
	_add_global("Crouch / Slide", [
		_row(p, "crouch_speed", "Crouch walk speed", 0.5, 10.0, 0.5),
		_row(p, "slide_speed", "Slide speed", 1.0, 30.0, 0.5),
		_row(p, "slide_distance", "Slide distance (m)", 1.0, 20.0, 0.5),
	])
	_add_global("Melee", [
		_row(g, "melee_time", "Melee time (s, lower = faster)", 0.2, 2.0, 0.05),
		_row(g, "melee_reach", "Melee reach (m)", 0.5, 6.0, 0.1),
		_row(g, "melee_damage", "Melee damage", 1.0, 300.0, 1.0),
		_row(g, "lunge_range", "Lunge range (m)", 0.0, 10.0, 0.1),
		_row(g, "lunge_angle", "Lunge angle (degrees)", 0.0, 60.0, 1.0),
		_row(g, "lunge_speed", "Lunge speed (m/s)", 5.0, 40.0, 0.5),
		_row(g, "lunge_vertical", "Lunge vertical pull", 0.0, 1.0, 0.05),
		_row(g, "lunge_aim", "Lunge camera pull", 0.0, 1.0, 0.05),
	])
	_add_global("Grenades", [
		_row(th, "throw_distance", "Throw distance (m)", 3.0, 40.0, 0.5),
		_row(th, "recharge_time", "Recharge time (s)", 0.5, 60.0, 0.5),
		_row(th, "detonation_time", "Detonation time (s)", 0.1, 5.0, 0.05),
	])
	_add_global("Player", [
		{"header": "Health and shields"},
		_row(health, "base_health", "Health", 10.0, 500.0, 5.0),
		_row(health, "base_shield", "Shields", 0.0, 500.0, 5.0),
		_row(health, "recharge_delay", "Recovery delay (s)", 0.5, 15.0, 0.25),
		_row(health, "health_regen", "Health regen (per s)", 5.0, 300.0, 5.0),
		_row(health, "shield_regen", "Shield regen (per s)", 5.0, 300.0, 5.0),
		_row(health, "respawn_time", "Respawn time (s)", 0.0, 10.0, 0.25),
		_row(health, "god_mode", "God mode (0/1)", 0.0, 1.0, 1.0),
		{"header": "Weapon handling"},
		_row(g, "swap_time", "Weapon swap time (s)", 0.05, 1.5, 0.01),
		_row(g, "ready_time", "Ready time after sprint (s)", 0.0, 0.6, 0.01),
		{"header": "Frame parts"},
		_row(mods, "point_value", "Each group point changes stats by", 0.0, 0.25, 0.01,
			func(v: float) -> String: return "%d%%" % roundi(v * 100.0)),
	])
	_add_global("Targets", [
		_row(target_manager, "target_health", "Target health", 10.0, 2000.0, 10.0),
		_row(target_manager, "respawn_time", "Auto respawn (s, 0 = off)", 0.0, 10.0, 0.5),
	])
	_add_global("Ult", [
		{"header": "Ult (F / L1+R1). Starter: massive grenade blast"},
		_row(ult, "charge_time", "Charge time (s)", 1.0, 600.0, 1.0),
		_row(ult, "damage", "Damage (center)", 0.0, 5000.0, 10.0),
		_row(ult, "blast_radius", "Blast radius (m)", 1.0, 40.0, 0.5),
		{"header": "Animation and camera"},
		_row(ult, "windup_time", "Windup before the blast (s)", 0.2, 4.0, 0.05),
		_row(ult, "hold_time", "Watch the blast for (s)", 0.0, 4.0, 0.05),
		_row(ult, "camera_blend", "Camera glide time (s)", 0.05, 2.0, 0.05),
		_row(ult, "camera_distance", "Camera distance behind (m)", 1.0, 12.0, 0.25),
		_row(ult, "camera_height", "Camera height (m)", -1.0, 6.0, 0.25),
		_row(ult, "camera_side", "Camera over-shoulder (m)", -3.0, 3.0, 0.1),
		{"header": "Veil (VeilSpore ult)"},
		_row(ult, "veil_time", "Veiled for (s)", 1.0, 60.0, 0.5),
		_row(ult, "veil_fog_radius", "Fog radius (m)", 1.0, 20.0, 0.25),
		_row(ult, "veil_fog_time", "Fog time (s)", 1.0, 60.0, 0.5),
		_row(ult, "veil_melee_mult", "Melee damage while veiled", 1.0, 6.0, 0.1,
			func(v: float) -> String: return "×%.1f" % v),
		_row(ult, "veil_camera_blend", "Camera flash time (s)", 0.05, 1.0, 0.01),
		_row(ult, "veil_hold", "Time in third person (s)", 0.0, 2.0, 0.05),
		{"header": "Entrench (IronBark ult)"},
		_row(ult, "entrench_time", "Entrenched for (s)", 1.0, 60.0, 0.5),
		_row(ult, "entrench_grenade_mult", "Grenade recharge speed", 1.0, 20.0, 0.5,
			func(v: float) -> String: return "×%.1f" % v),
	])
	var sd := soldier_data
	_add_global("Soldier", [
		{"header": "Enemy: Soldier (shared by all soldiers)"},
		_row(sd, "ai_enabled", "AI on (0/1)", 0.0, 1.0, 1.0),
		_row(sd, "max_health", "Health", 10.0, 1000.0, 5.0),
		_row(sd, "walk_speed", "Walk speed (m/s)", 0.5, 10.0, 0.25),
		_row(sd, "min_range", "Backs off inside (m)", 0.0, 50.0, 0.5),
		_row(sd, "max_range", "Moves in beyond (m)", 1.0, 80.0, 0.5),
		_row(sd, "sight_range", "Notices you within (m)", 5.0, 120.0, 1.0),
		_row(sd, "strafe_time", "Strafe switch time (s)", 0.3, 6.0, 0.1),
		_row(sd, "turn_speed", "Turn speed", 1.0, 30.0, 0.5),
		{"header": "Shooting"},
		_row(sd, "windup_time", "Windup glow (s)", 0.0, 2.0, 0.05),
		_row(sd, "burst_count", "Shots per burst", 1.0, 10.0, 1.0),
		_row(sd, "burst_interval", "Time between shots (s)", 0.03, 1.0, 0.01),
		_row(sd, "burst_cooldown", "Time between bursts (s)", 0.2, 8.0, 0.1),
		_row(sd, "group_spacing", "Group burst spacing (s)", 0.0, 3.0, 0.05),
		_row(sd, "projectile_damage", "Damage per shot", 0.0, 100.0, 1.0),
		_row(sd, "projectile_speed", "Shot speed (m/s)", 5.0, 150.0, 1.0),
		_row(sd, "spread", "Aim error (degrees)", 0.0, 10.0, 0.1),
		{"header": "Cover"},
		_row(sd, "cover_health", "Seeks cover below health", 0.0, 1.0, 0.05,
			func(v: float) -> String: return "%d%%" % roundi(v * 100.0)),
	])
	var sw := swarm_data
	_add_global("Swarm", [
		{"header": "Enemy: Swarm (shared by all swarm)"},
		_row(sw, "ai_enabled", "AI on (0/1)", 0.0, 1.0, 1.0),
		_row(sw, "max_health", "Health", 5.0, 500.0, 5.0),
		_row(sw, "walk_speed", "Run speed (m/s)", 1.0, 20.0, 0.25),
		_row(sw, "sight_range", "Notices you within (m)", 5.0, 120.0, 1.0),
		_row(sw, "turn_speed", "Turn speed", 1.0, 30.0, 0.5),
		_row(sw, "spread_distance", "Pack spacing (m)", 0.0, 5.0, 0.1),
		{"header": "Leap"},
		_row(sw, "leap_range", "Leaps from (m)", 0.0, 12.0, 0.25),
		_row(sw, "leap_speed", "Leap speed (m/s)", 2.0, 30.0, 0.5),
		_row(sw, "leap_height", "Leap height (m)", 0.0, 4.0, 0.1),
		_row(sw, "leap_damage", "Leap damage", 0.0, 100.0, 1.0),
		_row(sw, "leap_cooldown", "Time between leaps (s)", 0.2, 10.0, 0.1),
		{"header": "Bite"},
		_row(sw, "bite_range", "Bites within (m)", 0.5, 4.0, 0.05),
		_row(sw, "bite_damage", "Bite damage", 0.0, 100.0, 1.0),
		_row(sw, "bite_interval", "Time between bites (s)", 0.1, 4.0, 0.05),
	])
	var tk := tank_data
	_add_global("Tank", [
		{"header": "Enemy: Tank"},
		_row(tk, "ai_enabled", "AI on (0/1)", 0.0, 1.0, 1.0),
		_row(tk, "max_health", "Health", 50.0, 5000.0, 25.0),
		_row(tk, "walk_speed", "Walk speed (m/s)", 0.5, 10.0, 0.25),
		_row(tk, "sight_range", "Notices you within (m)", 5.0, 120.0, 1.0),
		_row(tk, "turn_speed", "Turn speed", 0.2, 20.0, 0.1),
		{"header": "Shield (applies on reset)"},
		_row(tk, "shield", "Shield (0 = none)", 0.0, 2000.0, 10.0),
		_row(tk, "shield_regen_delay", "Refill delay (s)", 0.5, 30.0, 0.5),
		_row(tk, "shield_regen_rate", "Refill rate (per s)", 5.0, 500.0, 5.0),
		{"header": "Slam"},
		_row(tk, "slam_range", "Slams when you're within (m)", 1.0, 10.0, 0.25),
		_row(tk, "slam_radius", "Slam radius (m)", 1.0, 12.0, 0.25),
		_row(tk, "slam_damage", "Slam damage", 0.0, 200.0, 1.0),
		_row(tk, "slam_windup", "Arms-up warning (s)", 0.1, 3.0, 0.05),
		_row(tk, "slam_recover", "Stands still after (s)", 0.0, 5.0, 0.1),
		_row(tk, "slam_cooldown", "Time between slams (s)", 0.5, 15.0, 0.25),
		_row(tk, "slow_amount", "Slow amount", 0.0, 0.95, 0.05,
			func(v: float) -> String: return "%d%% slower" % roundi(v * 100.0)),
		_row(tk, "slow_time", "Slow time (s)", 0.0, 10.0, 0.25),
		{"header": "Lobbed shot"},
		_row(tk, "lob_damage", "Damage (center)", 0.0, 200.0, 1.0),
		_row(tk, "lob_radius", "Splash radius (m)", 0.5, 10.0, 0.25),
		_row(tk, "lob_interval", "Time between shots (s)", 0.5, 15.0, 0.25),
		_row(tk, "lob_flight_time", "Time in the air (s)", 0.3, 4.0, 0.05),
	])
	var al := ally_data
	_add_global("Allies", [
		{"header": "AI teammates (for testing team abilities like Shield Share)"},
		_row(al, "ai_enabled", "AI on (0/1)", 0.0, 1.0, 1.0),
		_row(al, "max_health", "Health", 10.0, 500.0, 5.0),
		_row(al, "max_shield", "Shields", 0.0, 500.0, 5.0),
		_row(al, "recharge_delay", "Recovery delay (s)", 0.5, 15.0, 0.25),
		_row(al, "shield_regen", "Shield regen (per s)", 1.0, 300.0, 5.0),
		_row(al, "move_speed", "Move speed (m/s)", 1.0, 15.0, 0.25),
		_row(al, "follow_min", "Stays at least (m) from you", 0.0, 10.0, 0.25),
		_row(al, "follow_max", "Walks back past (m)", 1.0, 30.0, 0.5),
		{"header": "Shooting"},
		_row(al, "damage", "Damage per shot", 0.0, 100.0, 1.0),
		_row(al, "fire_interval", "Time between shots (s)", 0.05, 3.0, 0.05),
		_row(al, "spread", "Aim error (degrees)", 0.0, 15.0, 0.25),
		_row(al, "sight_range", "Shoots enemies within (m)", 5.0, 120.0, 1.0),
		_row(al, "respawn_time", "Down for (s)", 1.0, 60.0, 1.0),
	])
	_add_global("Aim Assist", [
		_row(aa, "mouse_assist", "Mouse assist (0 = off)", 0.0, 1.0, 0.05),
		_row(aa, "show_debug", "Show assist cones (0/1)", 0.0, 1.0, 1.0),
	])

	for weapon in loadout.weapons:
		_sections[_weapon_key(weapon)] = {"title": weapon.display_name, "rows": _weapon_rows(weapon)}

	for part in loadout.frame_parts:
		_sections["frame/" + part.id] = {"title": part.display_name, "rows": _frame_rows(part)}

	for ability in loadout.abilities:
		_sections[_ability_key(ability)] = {"title": ability.display_name, "rows": _ability_rows(ability)}

	for flora_trait in loadout.traits:
		_sections[_trait_key(flora_trait)] = {"title": flora_trait.display_name, "rows": _trait_rows(flora_trait)}

	for grenade in loadout.grenades:
		_sections[_grenade_key(grenade)] = {"title": grenade.display_name, "rows": [
			{"header": "Blast"},
			_row(grenade, "damage", "Damage (center)", 1.0, 500.0, 1.0),
			_row(grenade, "blast_radius", "Blast radius (m)", 0.5, 12.0, 0.1),
			{"header": "Adjustments on top of the global grenade settings"},
			_row(grenade, "throw_distance_mult", "Throw distance", 0.25, 3.0, 0.05,
				func(v: float) -> String: return "×%.2f = %.1f m" % [v, th.throw_distance * v]),
			_row(grenade, "recharge_mult", "Recharge time", 0.25, 3.0, 0.05,
				func(v: float) -> String: return "×%.2f = %.1f s" % [v, th.recharge_time * v]),
			_row(grenade, "fuse_mult", "Detonation time", 0.25, 3.0, 0.05,
				func(v: float) -> String: return "×%.2f = %.2f s" % [v, th.detonation_time * v]),
			_row(grenade, "bounce", "Bounciness", 0.0, 1.0, 0.05),
		] + _grenade_kind_rows(grenade)}


func _grenade_kind_rows(g: GrenadeData) -> Array:
	match g.kind:
		"pool":
			return [{"header": "Lingering pool (radius = blast radius)"},
				_row(g, "pool_dps", "Pool damage per second", 0.0, 200.0, 1.0),
				_row(g, "pool_time", "Pool time (s)", 0.5, 20.0, 0.5)]
		"wall":
			return [{"header": "Wall"},
				_row(g, "wall_width", "Wall width (m)", 1.0, 12.0, 0.25),
				_row(g, "wall_height", "Wall height (m)", 0.5, 6.0, 0.1),
				_row(g, "wall_time", "Wall lasts (s)", 1.0, 60.0, 0.5)]
		"blind":
			return [{"header": "Blinding"},
				_row(g, "blind_time", "Blinds enemies for (s)", 0.5, 15.0, 0.25)]
		"tripmine":
			return [{"header": "Slowing tripmine"},
				_row(g, "arm_time", "Arms after (s)", 0.0, 5.0, 0.05),
				_row(g, "trigger_radius", "Goes off within (m)", 0.5, 8.0, 0.25),
				_row(g, "mine_life", "Lasts (s)", 1.0, 120.0, 1.0),
				_row(g, "slow_amount", "Slows enemies by", 0.0, 0.95, 0.05,
					func(v: float) -> String: return "%d%%" % roundi(v * 100.0)),
				_row(g, "slow_time", "Slow time (s)", 0.0, 15.0, 0.25)]
		"spike":
			return [{"header": "Poison spike (damage = the direct hit)"},
				_row(g, "poison_dps", "Poison damage per second", 0.0, 200.0, 1.0),
				_row(g, "poison_time", "Poison time (s)", 0.5, 20.0, 0.5),
				_row(g, "spike_life", "Bounces around for (s)", 1.0, 20.0, 0.5),
				_row(g, "seek_radius", "Seeks targets within (m)", 0.0, 20.0, 0.5),
				_row(g, "seek_strength", "Seek turn rate (degrees/s)", 0.0, 720.0, 10.0)]
	return []


func _add_global(title: String, rows: Array) -> void:
	var key := "global/" + title
	_sections[key] = {"title": title, "rows": rows}
	_global_keys.append(key)


func _grenade_key(grenade: GrenadeData) -> String:
	return "grenade/" + grenade.id


func _weapon_key(weapon: WeaponData) -> String:
	return "weapon/" + weapon.id


func _weapon_rows(w: WeaponData) -> Array:
	return [
		{"header": w.archetype},
		{"header": "Aim down sights"},
		_row(w, "zoom", "ADS zoom (magnification)", 1.0, 10.0, 0.1),
		_row(w, "ads_time", "ADS time (s)", 0.05, 1.0, 0.01),
		_row(w, "ads_sensitivity", "ADS sensitivity multiplier", 0.1, 2.0, 0.05),
		_row(w, "ads_move_speed", "ADS move speed multiplier", 0.1, 1.0, 0.05),
		_row(w, "ads_vignette", "ADS vignette", 0.0, 1.0, 0.05),
		_row(w, "scope_overlay", "Scope overlay (0/1)", 0.0, 1.0, 1.0),
		{"header": "Shooting"},
		_row(w, "semi_auto", "Semi-auto (0 = hold to fire)", 0.0, 1.0, 1.0),
		_row(w, "fire_rate", "Fire rate (rounds/min)", 20.0, 1200.0, 5.0),
		_row(w, "damage", "Damage (per pellet)" if w.pellets > 1.0 else "Damage", 1.0, 400.0, 1.0),
		_row(w, "crit_multiplier", "Critical hit multiplier", 1.0, 4.0, 0.05),
		_row(w, "fire_range", "Range (m)", 5.0, 400.0, 5.0),
		_row(w, "pellets", "Pellets per shot", 1.0, 20.0, 1.0),
		_row(w, "pellet_spread", "Pellet spread (degrees)", 0.0, 15.0, 0.1),
		_row(w, "hip_spread", "Hip-fire spread (degrees)", 0.0, 10.0, 0.1),
		_row(w, "recoil_up", "Recoil up (degrees/shot)", 0.0, 8.0, 0.05),
		_row(w, "recoil_side", "Recoil side (degrees/shot)", 0.0, 3.0, 0.05),
		_row(w, "recoil_kick_time", "Recoil rise time (s, 0 = instant)", 0.0, 0.3, 0.005),
		_row(w, "recoil_recovery", "Recoil falls back (0-1)", 0.0, 1.0, 0.05),
		_row(w, "recoil_recovery_time", "Recoil fall-back speed (s)", 0.02, 1.0, 0.01),
		_row(w, "gun_kick", "Gun kick", 0.0, 5.0, 0.1),
		_row(w, "magazine_size", "Magazine size", 1.0, 100.0, 1.0),
		_row(w, "reload_time", "Reload time (s)", 0.3, 6.0, 0.05),
		{"header": "Handling"},
		_row(w, "viewmodel_fov", "Gun FOV", 40.0, 100.0, 1.0),
		_row(w, "sway", "Gun sway", 0.0, 3.0, 0.1),
		_row(w, "gun_bob", "Gun bob", 0.0, 3.0, 0.1),
		_row(w, "slide_tilt", "Slide gun tilt (degrees)", 0.0, 60.0, 1.0),
		{"header": "Aim assist"},
		_row(w, "aim_assistance", "Aim assistance stat", 0.0, 100.0, 1.0),
		_row(w, "crit_magnetism", "Head magnetism (fraction of cone)", 0.0, 1.0, 0.05),
		_row(w, "magnetism_angle", "Magnetism cone (degrees)", 0.0, 10.0, 0.1),
		_row(w, "friction_strength", "Friction strength", 0.0, 0.9, 0.05),
		_row(w, "friction_angle", "Friction cone (degrees)", 0.0, 15.0, 0.1),
		_row(w, "adhesion", "Adhesion strength", 0.0, 1.0, 0.05),
		_row(w, "assist_range", "Assist range (m)", 5.0, 300.0, 1.0),
		_row(w, "ads_boost", "ADS assist boost", 1.0, 3.0, 0.05),
	]


func _frame_rows(part: FramePartData) -> Array:
	# One slider per stat group: the points this part gives (negative = a penalty).
	var rows: Array = [{"header": part.slot.capitalize() + " part · group points"}]
	for group in Modifiers.GROUPS:
		var stats: Array = Modifiers.GROUPS[group].map(func(st: String) -> String: return Modifiers.STAT_NAMES[st].to_lower())
		rows.append({"header": "%s: %s" % [group.capitalize(), ", ".join(stats)]})
		rows.append(_row(part, group, group.capitalize() + " points", -5.0, 5.0, 1.0,
			func(v: float) -> String: return "%+d" % roundi(v)))
	return rows


func _frame_items() -> Array:
	var items := []
	for part in loadout.frame_parts:
		var equipped := part == loadout.frame_part(part.slot)
		items.append({"key": "frame/" + part.id, "title": part.display_name,
			"subtitle": part.slot.capitalize() + (" · equipped" if equipped else "")})
	return items


func _trait_key(flora_trait: TraitData) -> String:
	return "trait/" + flora_trait.id


func _trait_rows(t: TraitData) -> Array:
	var rows: Array = [{"header": t.description}]
	for i in t.mods.size():
		var mod: StatModifier = t.mods[i]
		var row := _row(mod, "value", mod.describe().split(" +")[0].split(" -")[0] + " change", -0.9, 2.0, 0.01,
			func(v: float) -> String: return "%+d%%" % roundi(v * 100.0))
		row.id = "mod%d" % i # every mod edits "value", so give each its own id
		rows.append(row)
	if t.effect != "" and t.effect != "no_radar":
		rows.append(_row(t, "amount", "Effect strength", 0.0, 3.0, 0.05,
			func(v: float) -> String: return "%+d%%" % roundi(v * 100.0)))
	return rows


func _trait_items() -> Array:
	var items := []
	for flora_trait in loadout.traits:
		var flora_name := flora_trait.flora.capitalize()
		items.append({"key": _trait_key(flora_trait), "title": flora_trait.display_name,
			"subtitle": flora_name + (" · equipped" if loadout.is_trait_equipped(flora_trait) else "")})
	return items


func _ability_key(ability: AbilityData) -> String:
	return "ability/" + ability.id


func _ability_rows(a: AbilityData) -> Array:
	var rows: Array = [{"header": a.kind.capitalize() + " · " + a.description}]
	match a.id:
		"overgrowth":
			rows.append(_row(a, "speed_mult", "Speed multiplier", 1.0, 2.0, 0.05))
			rows.append(_row(a, "extra_jumps", "Extra jumps", 0.0, 3.0, 1.0))
		"thornfist":
			rows.append(_row(a, "melee_damage_mult", "Melee damage multiplier", 1.0, 5.0, 0.05))
			rows.append(_row(a, "duration", "Duration (s)", 1.0, 30.0, 0.5))
			rows.append(_row(a, "cooldown", "Recharge time (s)", 1.0, 120.0, 1.0))
		"root_bombs":
			rows.append(_row(a, "cooldown", "Recharge time (s)", 1.0, 120.0, 1.0))
			rows.append(_row(a, "bomb_damage", "Damage per bomb (center)", 0.0, 1000.0, 5.0))
			rows.append(_row(a, "bomb_radius", "Blast radius (m)", 0.5, 12.0, 0.25))
			rows.append(_row(a, "bomb_speed", "Throw speed (m/s)", 5.0, 60.0, 1.0))
		"shield_share":
			rows.append(_row(a, "shield_amount", "Shield given per explosion", 0.0, 300.0, 5.0))
		"extra_grenade":
			rows.append(_row(a, "extra_charges", "Extra grenade charges", 0.0, 3.0, 1.0))
		"melee_guard":
			rows.append(_row(a, "guard_reduction", "Melee damage cut by", 0.0, 1.0, 0.05,
				func(v: float) -> String: return "%d%%" % roundi(v * 100.0)))
			rows.append(_row(a, "duration", "Lasts after a melee hit (s)", 0.5, 20.0, 0.5))
		"spore_fog_damage":
			rows.append(_row(a, "fog_dps", "Fog damage per second", 0.0, 100.0, 1.0))
		"spore_dash":
			rows.append(_row(a, "cooldown", "Recharge time (s)", 1.0, 120.0, 1.0))
			rows.append(_row(a, "dash_speed", "Dash speed backwards (m/s)", 2.0, 40.0, 0.5))
			rows.append(_row(a, "dash_up", "Dash lift (m/s)", 0.0, 15.0, 0.25))
			rows.append(_row(a, "dash_fog_radius", "Fog radius (m)", 0.5, 12.0, 0.25))
			rows.append(_row(a, "dash_fog_time", "Fog time (s)", 0.5, 30.0, 0.5))
		"spore_patches":
			rows.append(_row(a, "patch_max", "Patches per grenade", 1.0, 10.0, 1.0))
			rows.append(_row(a, "patch_radius", "Fog radius per patch (m)", 0.5, 8.0, 0.25))
			rows.append(_row(a, "patch_time", "Patch time (s)", 0.5, 30.0, 0.5))
		"seedburst":
			rows.append(_row(a, "sub_count", "Seed count", 1.0, 8.0, 1.0))
			rows.append(_row(a, "sub_scale", "Seed size (blast and damage)", 0.1, 1.0, 0.05))
			rows.append(_row(a, "sub_spread", "Seed spread (m/s)", 1.0, 15.0, 0.5))
			rows.append(_row(a, "sub_fuse", "Seed fuse (s)", 0.1, 3.0, 0.05))
	return rows


# Item lists for the Specific pane. Each item is {key, title, subtitle}.
func _equipped_items() -> Array:
	var items := []
	if loadout.grenade:
		items.append({"key": _grenade_key(loadout.grenade), "title": loadout.grenade.display_name, "subtitle": "Equipped grenade"})
	for slot in Loadout.ABILITY_SLOTS:
		var ability := loadout.ability(slot)
		if ability:
			items.append({"key": _ability_key(ability), "title": ability.display_name, "subtitle": "Equipped ability %d" % (slot + 1)})
	if loadout.weapon:
		items.append({"key": _weapon_key(loadout.weapon), "title": loadout.weapon.display_name, "subtitle": "Equipped weapon"})
	for slot in Loadout.FRAME_SLOTS:
		var part := loadout.frame_part(slot)
		if part:
			items.append({"key": "frame/" + part.id, "title": part.display_name, "subtitle": "Equipped " + slot})
	return items


func _ability_items() -> Array:
	var items := []
	for ability in loadout.abilities:
		var slot := loadout.ability_slot_of(ability)
		var subtitle := ability.kind.capitalize() + (" · equipped in slot %d" % (slot + 1) if slot != -1 else "")
		items.append({"key": _ability_key(ability), "title": ability.display_name, "subtitle": subtitle})
	return items


func _grenade_items() -> Array:
	var items := []
	for grenade in loadout.grenades:
		items.append({"key": _grenade_key(grenade), "title": grenade.display_name,
			"subtitle": "Equipped" if grenade == loadout.grenade else ""})
	return items


func _weapon_items() -> Array:
	var items := []
	for weapon in loadout.weapons:
		var subtitle := weapon.archetype + (" · equipped" if weapon == loadout.weapon else "")
		items.append({"key": _weapon_key(weapon), "title": weapon.display_name, "subtitle": subtitle})
	return items


# --- Values, defaults, saving ---------------------------------------------------

func _record_defaults() -> void:
	for key in _sections:
		var values := {}
		for row in _sections[key].rows:
			if row.has("prop"):
				values[row.id] = row.obj.get(row.prop)
		_defaults[key] = values


func _row_for(key: String, id: String) -> Dictionary:
	# Rows are found by id; an id may also be given as the bare prop name (first match).
	if _sections.has(key):
		for row in _sections[key].rows:
			if row.get("id", "") == id:
				return row
		for row in _sections[key].rows:
			if row.get("prop", "") == id:
				return row
	return {}


func get_value(key: String, id: String) -> float:
	var row := _row_for(key, id)
	return row.obj.get(row.prop) if not row.is_empty() else 0.0


func set_value(key: String, id: String, value: float) -> void:
	var row := _row_for(key, id)
	if not row.is_empty():
		row.obj.set(row.prop, value)
		_sync_sliders()


func reset_section(key: String) -> void:
	for id in _defaults.get(key, {}):
		var row := _row_for(key, id)
		row.obj.set(row.prop, _defaults[key][id])
	_sync_sliders()


func reset_all() -> void:
	for key in _sections:
		reset_section(key)


func save_defaults() -> bool:
	var data := {}
	for key in _sections:
		var values := {}
		for row in _sections[key].rows:
			if row.has("prop"):
				values[row.id] = row.obj.get(row.prop)
		data[key] = values
		_defaults[key] = values.duplicate() # Reset now returns here
	var file := FileAccess.open(SAVE_PATH, FileAccess.WRITE)
	if file == null:
		return false
	file.store_string(JSON.stringify(data, "\t"))
	return true


func _load_saved() -> void:
	if not FileAccess.file_exists(SAVE_PATH):
		return
	var data = JSON.parse_string(FileAccess.get_file_as_string(SAVE_PATH))
	if not data is Dictionary:
		return
	for key in data:
		if not (_sections.has(key) and data[key] is Dictionary):
			continue
		for id in data[key]:
			var row := _row_for(key, id)
			if not row.is_empty():
				row.obj.set(row.prop, float(data[key][id]))
				_defaults[key][row.id] = float(data[key][id])


# --- Building the UI -------------------------------------------------------------

func _style(color: Color, radius := 8, pad := 0) -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = color
	box.set_corner_radius_all(radius)
	box.content_margin_left = pad
	box.content_margin_right = pad
	box.content_margin_top = pad * 0.5
	box.content_margin_bottom = pad * 0.5
	return box


func _button(text: String, on_press: Callable) -> Button:
	var button := Button.new()
	button.text = text
	button.focus_mode = Control.FOCUS_NONE # the cursor drives everything, not focus
	button.pressed.connect(on_press)
	return button


func _build() -> void:
	var backdrop := ColorRect.new() # dims the game behind the menu
	backdrop.color = Color(0, 0, 0, 0.6)
	backdrop.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	backdrop.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(backdrop)

	var center := CenterContainer.new()
	center.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(center)

	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(1560, 860)
	panel.theme = Theme.new()
	panel.theme.default_font_size = 22
	panel.theme.set_font_size("font_size", "TabContainer", 15) # all 9 Global tabs fit
	# Slimmer tab padding (also so all 8 Global tabs fit).
	for state in ["tab_selected", "tab_unselected", "tab_hovered"]:
		var shade: float = {"tab_selected": 0.24, "tab_unselected": 0.13, "tab_hovered": 0.18}[state]
		var tab := _style(Color(shade, shade + 0.01, shade + 0.04), 4, 9)
		tab.content_margin_top = 6
		tab.content_margin_bottom = 6
		panel.theme.set_stylebox(state, "TabContainer", tab)
	# Visible slider tracks on the dark background.
	var track := _style(Color(0.3, 0.32, 0.37), 3)
	track.content_margin_top = 3
	track.content_margin_bottom = 3
	var filled := _style(Color(0.55, 0.6, 0.7), 3)
	panel.theme.set_stylebox("slider", "HSlider", track)
	panel.theme.set_stylebox("grabber_area", "HSlider", filled)
	panel.theme.set_stylebox("grabber_area_highlight", "HSlider", _style(Color(0.7, 0.76, 0.88), 3))
	panel.add_theme_stylebox_override("panel", _style(Color(0.1, 0.11, 0.13), 12))
	center.add_child(panel)

	var margin := MarginContainer.new()
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 20)
	panel.add_child(margin)

	var page := VBoxContainer.new()
	page.add_theme_constant_override("separation", 12)
	margin.add_child(page)

	var header := HBoxContainer.new()
	var title := Label.new()
	title.text = "Settings"
	title.add_theme_font_size_override("font_size", 32)
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(title)
	var hint := Label.new()
	hint.text = "◯ / Esc to go back  ·  L1/R1 or Q/E: Global tabs"
	hint.add_theme_font_size_override("font_size", 18)
	hint.modulate = Color(1, 1, 1, 0.6)
	header.add_child(hint)
	page.add_child(header)

	var panes := HBoxContainer.new()
	panes.size_flags_vertical = Control.SIZE_EXPAND_FILL
	panes.add_theme_constant_override("separation", 20)
	page.add_child(panes)
	panes.add_child(_build_global_pane())
	panes.add_child(_build_specific_pane())

	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 12)
	buttons.add_child(_button("Reset all", reset_all))
	var save := Button.new()
	save.text = "Save as defaults"
	save.focus_mode = Control.FOCUS_NONE
	save.pressed.connect(func() -> void:
		save.text = "Saved ✓" if save_defaults() else "Save failed"
		get_tree().create_timer(1.5).timeout.connect(func() -> void: save.text = "Save as defaults"))
	buttons.add_child(save)
	buttons.add_child(_button("Close", toggle))
	page.add_child(buttons)

	cursor = MenuCursor.new() # last child, so it draws on top
	add_child(cursor)


func _pane(title: String) -> VBoxContainer:
	var pane := VBoxContainer.new()
	pane.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	pane.add_theme_constant_override("separation", 8)
	var label := Label.new()
	label.text = title
	label.add_theme_font_size_override("font_size", 26)
	pane.add_child(label)
	return pane


func _build_global_pane() -> VBoxContainer:
	var pane := _pane("Global")
	pane.size_flags_stretch_ratio = 1.15 # a bit wider: it has more tabs than Specific
	_global_tabs = TabContainer.new()
	_global_tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	pane.add_child(_global_tabs)
	for key in _global_keys:
		var rows := VBoxContainer.new()
		rows.add_theme_constant_override("separation", 8)
		_build_rows(rows, key)
		if key == "global/Player":
			var hurt := _button("Take 30 damage", func() -> void: health.take_damage(30.0))
			hurt.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
			rows.add_child(hurt)
			var die := _button("Die (test respawn)", health.kill)
			die.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
			rows.add_child(die)
		if key == "global/Ult":
			var fill := _button("Fill ult", ult.fill)
			fill.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
			rows.add_child(fill)
		if key in ["global/Targets", "global/Soldier", "global/Swarm", "global/Tank", "global/Allies"]:
			var reset_targets := _button("Reset targets and enemies", target_manager.reset_targets)
			reset_targets.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
			rows.add_child(reset_targets)
		_add_tab(_global_tabs, _sections[key].title, _scroll_page(rows))
	var reset_tab := _button("Reset this tab", func() -> void: reset_section(_global_keys[_global_tabs.current_tab]))
	reset_tab.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	pane.add_child(reset_tab)
	return pane


func _build_specific_pane() -> VBoxContainer:
	var pane := _pane("Specific")
	_specific_tabs = TabContainer.new()
	_specific_tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	pane.add_child(_specific_tabs)
	_add_tab(_specific_tabs, "Equipped", _make_browser(_equipped_items))
	var flora := TabContainer.new() # sub-tabs for each Flora slot type
	_add_tab(flora, "Grenades", _make_browser(_grenade_items))
	_add_tab(flora, "Abilities", _make_browser(_ability_items))
	_add_tab(flora, "Traits", _make_browser(_trait_items))
	_add_tab(_specific_tabs, "Flora", flora)
	_add_tab(_specific_tabs, "Weapons", _make_browser(_weapon_items))
	_add_tab(_specific_tabs, "Frame Parts", _make_browser(_frame_items))
	return pane


func _add_tab(tabs: TabContainer, title: String, page: Control) -> void:
	tabs.add_child(page)
	tabs.set_tab_title(tabs.get_tab_count() - 1, title)


func _empty_page() -> Control:
	var label := Label.new()
	label.text = "Nothing here yet."
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	label.modulate = Color(1, 1, 1, 0.5)
	return label


func _scroll_page(content: Control) -> ScrollContainer:
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	var margin := MarginContainer.new()
	margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 12)
	margin.add_child(content)
	content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(margin)
	return scroll


func _build_rows(container: VBoxContainer, key: String) -> void:
	for row in _sections[key].rows:
		if row.has("header"):
			var header := Label.new()
			header.text = row.header
			header.add_theme_font_size_override("font_size", 18)
			header.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART # long headers wrap instead of widening the pane
			header.custom_minimum_size.x = 200
			header.modulate = Color(1, 1, 1, 0.55)
			container.add_child(header)
		else:
			container.add_child(_make_row(row))


func _make_row(row: Dictionary) -> HBoxContainer:
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", 12)
	var label := Label.new()
	label.text = row.label
	label.custom_minimum_size.x = 270
	line.add_child(label)
	var slider := HSlider.new()
	slider.min_value = row.min
	slider.max_value = row.max
	slider.step = row.step
	slider.value = row.obj.get(row.prop)
	slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	slider.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	slider.custom_minimum_size.y = 28
	slider.focus_mode = Control.FOCUS_NONE
	slider.scrollable = false # the mouse wheel / right stick scroll the page, not the slider
	line.add_child(slider)
	var value_label := Label.new()
	var show: Callable = row.show
	value_label.custom_minimum_size.x = 170 if show.is_valid() else 80 # long readouts only where needed
	value_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	value_label.text = _value_text(row, slider.value)
	line.add_child(value_label)
	slider.value_changed.connect(func(v: float) -> void:
		row.obj.set(row.prop, v)
		value_label.text = _value_text(row, v))
	_live.append({"slider": slider, "label": value_label, "obj": row.obj, "prop": row.prop, "row": row})
	return line


func _value_text(row: Dictionary, v: float) -> String:
	var show: Callable = row.show
	if show.is_valid():
		return show.call(v)
	return "%.2f" % v if row.step < 0.1 else "%.1f" % v


func _sync_sliders() -> void:
	# Push current values back into whatever sliders are on screen (after a reset).
	_live = _live.filter(func(l: Dictionary) -> bool: return is_instance_valid(l.slider))
	for l in _live:
		var v: float = l.obj.get(l.prop)
		l.slider.set_value_no_signal(v)
		l.label.text = _value_text(l.row, v)


# --- Item browsers (list of items <-> one item's sliders) ------------------------

func _make_browser(provider: Callable) -> VBoxContainer:
	var browser := VBoxContainer.new()
	browser.set_meta("provider", provider)

	var list := VBoxContainer.new()
	list.add_theme_constant_override("separation", 10)
	var list_scroll := _scroll_page(list)
	list_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	browser.add_child(list_scroll)

	var detail := VBoxContainer.new()
	detail.size_flags_vertical = Control.SIZE_EXPAND_FILL
	var bar := HBoxContainer.new()
	bar.add_theme_constant_override("separation", 12)
	bar.add_child(_button("‹ Back", func() -> void: _show_list(browser)))
	var title := Label.new()
	title.add_theme_font_size_override("font_size", 26)
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bar.add_child(title)
	bar.add_child(_button("Reset item", func() -> void: reset_section(browser.get_meta("open_key"))))
	detail.add_child(bar)
	var rows := VBoxContainer.new()
	rows.add_theme_constant_override("separation", 8)
	var rows_scroll := _scroll_page(rows)
	rows_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	detail.add_child(rows_scroll)
	browser.add_child(detail)

	browser.set_meta("list", list)
	browser.set_meta("list_scroll", list_scroll)
	browser.set_meta("detail", detail)
	browser.set_meta("title", title)
	browser.set_meta("rows", rows)
	browser.set_meta("rows_scroll", rows_scroll)
	_browsers.append(browser)
	_show_list(browser)
	return browser


func _show_list(browser: VBoxContainer) -> void:
	var list: VBoxContainer = browser.get_meta("list")
	for child in list.get_children():
		list.remove_child(child)
		child.queue_free()
	for item in browser.get_meta("provider").call():
		var text: String = item.title + ("\n" + item.subtitle if item.subtitle != "" else "")
		var button := _button(text, _open_item.bind(browser, item.key))
		button.alignment = HORIZONTAL_ALIGNMENT_LEFT
		button.custom_minimum_size.y = 72
		for state in ["normal", "hover", "pressed"]:
			var shade := 0.08 if state == "hover" else (0.15 if state == "pressed" else 0.0)
			button.add_theme_stylebox_override(state, _style(Color(0.18, 0.19, 0.23).lightened(shade), 8, 20))
		list.add_child(button)
	browser.get_meta("list_scroll").visible = true
	browser.get_meta("detail").visible = false


func _open_item(browser: VBoxContainer, key: String) -> void:
	browser.set_meta("open_key", key)
	browser.get_meta("title").text = _sections[key].title
	var rows: VBoxContainer = browser.get_meta("rows")
	for child in rows.get_children():
		rows.remove_child(child)
		child.queue_free()
	_build_rows(rows, key)
	browser.get_meta("rows_scroll").scroll_vertical = 0
	browser.get_meta("list_scroll").visible = false
	browser.get_meta("detail").visible = true


func _open_browser() -> VBoxContainer:
	# The browser that's on screen with an item open, if any.
	for browser in _browsers:
		if browser.is_visible_in_tree() and browser.get_meta("detail").visible:
			return browser
	return null


# --- Opening, closing, navigation ------------------------------------------------

func _input(event: InputEvent) -> void:
	# Runs in _input (before the UI) so a slider under the cursor can't swallow these.
	if not visible:
		if event.is_action_pressed("menu"):
			toggle()
			get_viewport().set_input_as_handled()
		return
	if event.is_action_pressed("menu_back") or event.is_action_pressed("menu"):
		var browser := _open_browser()
		if browser:
			_show_list(browser) # back out of an item first
		else:
			toggle()
		get_viewport().set_input_as_handled()
	elif event.is_action_pressed("tab_next"):
		_global_tabs.current_tab = (_global_tabs.current_tab + 1) % _global_tabs.get_tab_count()
		get_viewport().set_input_as_handled()
	elif event.is_action_pressed("tab_prev"):
		_global_tabs.current_tab = posmod(_global_tabs.current_tab - 1, _global_tabs.get_tab_count())
		get_viewport().set_input_as_handled()


func toggle() -> void:
	visible = not visible
	get_tree().paused = visible
	if visible:
		# Item lists can change (e.g. a different grenade equipped), so refresh any showing.
		for browser in _browsers:
			if not browser.get_meta("detail").visible:
				_show_list(browser)
		_sync_sliders()
		cursor.activate()
	else:
		cursor.deactivate()
		player.reset_input_edges() # circle is also crouch: don't count the closing press
