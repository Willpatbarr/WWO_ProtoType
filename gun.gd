class_name Gun
extends Node3D
# Holds the equipped weapon (a WeaponData + its model) and handles aiming down sights,
# shooting, reloading and melee. Modeled on Destiny 2:
# - Zoom is a magnification: it divides tan(half FOV), like a real lens.
# - The gun is drawn with its own fixed FOV, so it doesn't grow when you zoom.
# - The hip reticle fades out early while the gun eases onto the sight.
# - While aiming: slower turning (scaled by zoom), slower movement, no sprint,
#   and much less head bob and gun sway.
# Shooting is hitscan: an instant ray (or several pellets) from the center of the screen.
# Weapon stats are read live from the equipped WeaponData, so sliders apply instantly.
# Each model's origin is its sight point, so the weapon's ads_position puts the sight dead center.
# D-pad right/left or the mouse wheel cycle weapons.

signal hit_confirmed(critical: bool, killed: bool) # the HUD shows a hit (or kill) marker

# --- Global values (the settings menu edits these live) ---
@export var hip_fov := 100.0 # horizontal degrees, like Destiny's FOV slider
@export var loadout: Loadout

# --- The equipped weapon's stats (live from its WeaponData) ---
var weapon: WeaponData
var zoom: float:
	get: return weapon.zoom
	set(value): weapon.zoom = value
var ads_time: float:
	get: return weapon.ads_time
	set(value): weapon.ads_time = value
var ads_sensitivity: float:
	get: return weapon.ads_sensitivity
	set(value): weapon.ads_sensitivity = value
var ads_move_speed: float:
	get: return weapon.ads_move_speed
	set(value): weapon.ads_move_speed = value
var viewmodel_fov: float:
	get: return weapon.viewmodel_fov
	set(value): weapon.viewmodel_fov = value
var sway: float:
	get: return weapon.sway
	set(value): weapon.sway = value
var gun_bob: float:
	get: return weapon.gun_bob
	set(value): weapon.gun_bob = value
var ads_vignette: float:
	get: return weapon.ads_vignette
	set(value): weapon.ads_vignette = value
var slide_tilt: float:
	get: return weapon.slide_tilt
	set(value): weapon.slide_tilt = value
var fire_rate: float:
	get: return weapon.fire_rate
	set(value): weapon.fire_rate = value
var damage: float:
	get: return weapon.damage
	set(value): weapon.damage = value
var crit_multiplier: float:
	get: return weapon.crit_multiplier
	set(value): weapon.crit_multiplier = value
var fire_range: float:
	get: return weapon.fire_range
	set(value): weapon.fire_range = value
var recoil_up: float:
	get: return weapon.recoil_up
	set(value): weapon.recoil_up = value
var recoil_side: float:
	get: return weapon.recoil_side
	set(value): weapon.recoil_side = value
var gun_kick: float:
	get: return weapon.gun_kick
	set(value): weapon.gun_kick = value
var magazine_size: float:
	get: return weapon.magazine_size
	set(value): weapon.magazine_size = value
var reload_time: float:
	get: return weapon.reload_time
	set(value): weapon.reload_time = value

# Melee
@export var melee_time := 0.6 # seconds for a whole swing (lower = faster)
@export var melee_reach := 2.5 # meters
@export var melee_damage := 80.0
@export var lunge_range := 5.0 # meters: targets this close (and in the lunge angle) pull you in
@export var lunge_angle := 20.0 # degrees off center a target can be and still trigger a lunge
@export var lunge_speed := 18.0 # m/s while lunging
@export var lunge_vertical := 1.0 # 0 = lunge along the ground only, 1 = full pull up/down (e.g. mid-jump)
@export var lunge_aim := 0.5 # how strongly the view swings onto the target during a lunge (0 = off)

const SWAY_PER_TURN_SPEED := 0.025 # radians of lag per rad/s of turning
const SWAY_MAX := 0.08
# Action poses: where the gun goes while sprinting / sliding (added on top of the hip pose).
# Rotations are (pitch, yaw, roll) in radians, around the sight. Positive yaw = muzzle left, positive roll = tilt left.
const SPRINT_OFFSET := Vector3(-0.1, 0.03, 0.02)
const SPRINT_ROTATION := Vector3(-0.15, 0.55, 0.45) # muzzle down and left, canted
const SLIDE_OFFSET := Vector3(0.0, 0.07, 0.0) # raised up
const AIR_LAG_PER_SPEED := 0.008 # meters the gun lags per m/s of vertical speed
const AIR_LAG_MAX := 0.06
const MANTLE_OFFSET := Vector3(0.02, -0.14, 0.06)
const MANTLE_ROTATION := Vector3(-0.5, 0.0, 0.25)
const THROW_POSE_TIME := 0.35 # the gun dips out of the way while you throw
const THROW_OFFSET := Vector3(0.03, -0.1, 0.04)
const THROW_ROTATION := Vector3(-0.35, -0.15, 0.0)
const SIGHT_COLOR := Color(0.95, 0.95, 0.95) # red dot sight color normally...
const SIGHT_ON_TARGET_COLOR := Color(1.0, 0.1, 0.08) # ...and over a target
const MUZZLE_FLASH_TIME := 0.04
const MATERIAL := preload("res://viewmodel_material.tres") # shared by every weapon model
const SWAP_TIME := 0.35 # the gun dips out and back up when you switch weapons
const SWAP_OFFSET := Vector3(0.0, -0.22, 0.06)
const SWAP_ROTATION := Vector3(-0.7, 0.0, 0.0)
# Reload animation, as fractions of the reload time:
# tip up and roll, old mag drops out, new mag slides in, tip back down.
const RELOAD_TILT := 0.35 # radians the muzzle tips up
const RELOAD_MAG_DROP := 0.2 # meters the magazine travels out the bottom
const RELOAD_LIFT := Vector3(-0.05, 0.1, -0.12) # gun moves up and left while reloading
const RELOAD_ROLL := -0.7 # radians the gun rolls onto its side so the magazine well faces you
const RELOAD_SEATED := 0.75 # ammo refills once the new mag is in
# Melee swing, as fractions of the melee time: wind up, strike, recover.
const MELEE_IMPACT := 0.4 # the hit lands here
const MELEE_LUNGE_HOLD := 0.3 # the swing waits here (wound up) until the lunge arrives
const MELEE_ANGLE := 35.0 # degrees off center a melee can still connect (it's forgiving)
const MELEE_WIND_OFFSET := Vector3(0.06, -0.03, 0.08)
const MELEE_WIND_ROTATION := Vector3(0.0, -0.35, -0.2)
const MELEE_STRIKE_OFFSET := Vector3(-0.14, 0.06, -0.14)
const MELEE_STRIKE_ROTATION := Vector3(0.1, 0.7, 0.6) # swung across, canted

var ads_amount := 0.0 # 0 = hip, 1 = fully aimed (eased)
var _ads_progress := 0.0 # same, but linear in time
var _look_delta := Vector2.ZERO # turning since last frame, for sway
var _sway := Vector2.ZERO
var _sprint_pose := 0.0 # 0..1, eases in and out
var _slide_pose := 0.0
var _air_lag := 0.0
var _mantle_pose := 0.0
var _throw_time := 0.0 # counts down while the gun dips for a grenade throw
var _model: Node3D
var _swap_time := 0.0
var target_under_reticle := false # the HUD turns the reticle red when true
var reticle_target: Target = null # the target under the reticle (the HUD shows its health bar)
var _fire_cooldown := 0.0
var _kick := 0.0 # 1 right after a shot, decays to 0
var _flash_time := 0.0
var aim_assist: AimAssist
var ammo := 30 # rounds left in the magazine
var reloading := false
var reload_progress := 0.0 # 0..1 through the reload animation
var _mag_rest := Vector3.ZERO # where the magazine sits normally
var meleeing := false
var melee_progress := 0.0 # 0..1 through the swing
var _melee_landed := false # each swing hits at most once

@onready var camera: Camera3D = get_parent()
@onready var player: CharacterBody3D = camera.get_parent()


func _ready() -> void:
	camera.keep_aspect = Camera3D.KEEP_WIDTH # FOV is horizontal, like Destiny
	aim_assist = AimAssist.new()
	aim_assist.name = "AimAssist"
	aim_assist.gun = self
	add_child(aim_assist)
	equip(loadout.weapon)
	loadout.changed.connect(func() -> void:
		if loadout.weapon != weapon:
			equip(loadout.weapon))


func equip(new_weapon: WeaponData) -> void:
	# Swap in the weapon's model and start it with a full magazine.
	weapon = new_weapon
	if _model:
		_model.queue_free()
	_model = load(weapon.model).instantiate()
	add_child(_model)
	_mag_rest = _part("Magazine").position
	ammo = int(magazine_size)
	cancel_reload()
	_ads_progress = 0.0
	_fire_cooldown = 0.0
	_swap_time = SWAP_TIME


func _m(stat: String, base: float) -> float:
	# A stat after frame part (and other) modifiers.
	var mods: Modifiers = player.mods
	return mods.apply(stat, base)


func _part(part_name: String) -> Node3D:
	return _model.get_node_or_null(part_name)


func is_swapping() -> bool:
	return _swap_time > SWAP_TIME * 0.4 # can fire once the gun is mostly back up


func is_aiming() -> bool:
	return Input.is_action_pressed("aim") and not reloading and not meleeing and not is_swapping() # not mid-reload, -swing or -swap


func start_reload() -> void:
	if reloading or ammo >= int(magazine_size):
		return
	reloading = true
	reload_progress = 0.0
	player.sprinting = false # reloading stops a sprint


func start_throw_pose() -> void:
	_throw_time = THROW_POSE_TIME


func cancel_reload() -> void:
	reloading = false
	reload_progress = 0.0


func _update_melee(delta: float) -> void:
	if Input.is_action_just_pressed("melee") and not meleeing:
		meleeing = true
		melee_progress = 0.0
		_melee_landed = false
		cancel_reload() # meleeing cancels a reload, like Destiny
		player.sprinting = false
		var lunge_target := _find_lunge_target()
		if lunge_target:
			player.start_lunge(lunge_target)
	if not meleeing:
		return
	melee_progress += delta / maxf(_m("melee_time", melee_time), 0.05)
	if player.lunging:
		melee_progress = minf(melee_progress, MELEE_LUNGE_HOLD) # hold the wind-up until we arrive
	if melee_progress >= MELEE_IMPACT and not _melee_landed:
		_melee_landed = true
		_melee_strike()
	if melee_progress >= 1.0:
		meleeing = false
		melee_progress = 0.0


func _find_lunge_target() -> Target:
	# Closest target inside the lunge range and angle, or any target the swing would hit
	# anyway. Every melee that connects lurches in and swings the camera onto the hit.
	var origin := camera.global_position
	var forward := -camera.global_basis.z
	var best: Target = null
	var best_distance := INF
	for node in get_tree().get_nodes_in_group("targets"):
		var target := node as Target
		for part in target.aim_parts():
			var to: Vector3 = part.position - origin
			var distance: float = to.length() - part.radius
			if distance >= best_distance:
				continue
			var angle := rad_to_deg(forward.angle_to(to))
			var in_lunge_cone := distance <= lunge_range and angle <= lunge_angle
			var will_hit_anyway := distance <= _m("melee_reach", melee_reach) and angle <= MELEE_ANGLE # the swing lands, so lunge too
			if not (in_lunge_cone or will_hit_anyway):
				continue
			var check := cast_ray_to(part.position)
			if check.is_empty() or check.collider != target:
				continue
			best = target
			best_distance = distance
	return best


func _melee_strike() -> void:
	# Hit the closest target part within reach and inside the melee angle.
	var origin := camera.global_position
	var forward := -camera.global_basis.z
	var best: Target = null
	var best_point := Vector3.ZERO
	var best_distance := INF
	for node in get_tree().get_nodes_in_group("targets"):
		var target := node as Target
		for part in target.aim_parts():
			var to: Vector3 = part.position - origin
			var distance: float = to.length() - part.radius # to the surface, roughly
			if distance > _m("melee_reach", melee_reach) or distance >= best_distance:
				continue
			if rad_to_deg(forward.angle_to(to)) > MELEE_ANGLE:
				continue
			var check := cast_ray_to(part.position)
			if check.is_empty() or check.collider != target:
				continue
			best = target
			best_point = check.position
			best_distance = distance
	if best:
		var boosted: float = melee_damage * player.abilities.melee_multiplier() # Thornfist
		var killed := best.hit(boosted, false, best_point) # melee doesn't crit
		hit_confirmed.emit(false, killed)
		_kick = 1.0 # a little jolt on contact


func _update_reload(delta: float) -> void:
	ammo = mini(ammo, int(magazine_size)) # in case the slider shrank the magazine
	if Input.is_action_just_pressed("reload"):
		start_reload()
	if reloading and player.sprinting:
		cancel_reload() # sprinting cancels a reload, like Destiny
	if not reloading:
		return
	reload_progress += delta / maxf(_m("reload_time", reload_time), 0.05)
	if reload_progress >= RELOAD_SEATED and ammo < int(magazine_size):
		ammo = int(magazine_size) # the new magazine is in
	if reload_progress >= 1.0:
		cancel_reload()


func add_look(amount: Vector2) -> void:
	_look_delta += amount


func look_multiplier() -> float:
	return lerpf(1.0, ads_sensitivity / zoom, ads_amount)


func move_multiplier() -> float:
	return lerpf(1.0, ads_move_speed, ads_amount)


func _process(delta: float) -> void:
	if Input.is_action_just_pressed("weapon_next"):
		loadout.cycle_weapon(1)
	elif Input.is_action_just_pressed("weapon_prev"):
		loadout.cycle_weapon(-1)
	_update_melee(delta)
	_update_reload(delta)
	_update_ads(delta)
	_update_fov()
	_update_firing(delta)
	_update_reticle_target()
	_update_pose(delta)


func _wants_to_fire() -> bool:
	# Semi-auto weapons need a fresh trigger pull for every shot.
	var pulled := Input.is_action_just_pressed("fire") if semi_auto() else Input.is_action_pressed("fire")
	if not pulled:
		return false
	# A left click on a free mouse (e.g. closing the menu) shouldn't shoot.
	if Input.mouse_mode != Input.MOUSE_MODE_CAPTURED and Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT):
		return false
	return true


func _update_firing(delta: float) -> void:
	_kick = move_toward(_kick, 0.0, 12.0 * delta)
	_flash_time -= delta
	_part("MuzzleFlash").visible = _flash_time > 0.0

	_fire_cooldown -= delta
	if not _wants_to_fire() or reloading or meleeing or is_swapping():
		_fire_cooldown = maxf(_fire_cooldown, 0.0) # don't bank shots while idle
		return
	if semi_auto() and _fire_cooldown > 0.0:
		return # pulled the trigger before the gun was ready
	while _fire_cooldown <= 0.0:
		if ammo <= 0:
			start_reload() # pulling the trigger on empty reloads
			return
		_shoot()
		ammo -= 1
		_fire_cooldown += 60.0 / maxf(fire_rate, 1.0)
		if semi_auto():
			break # one shot per pull
	if ammo <= 0:
		start_reload() # ran dry: reload automatically


func semi_auto() -> bool:
	return weapon.semi_auto >= 0.5


func _shoot() -> void:
	_kick = 1.0
	_flash_time = MUZZLE_FLASH_TIME
	var pellet_count := maxi(1, roundi(weapon.pellets))
	# Hip-fire spread shrinks to nothing as you aim in; pellets scatter around that aim.
	var aim := _spread(-camera.global_basis.z, deg_to_rad(weapon.hip_spread) * (1.0 - ads_amount))
	var totals := {} # target -> {damage, critical, point}: pellets add up into one hit per target
	for i in pellet_count:
		var dir := _spread(aim, deg_to_rad(weapon.pellet_spread)) if pellet_count > 1 else aim
		var result := cast_ray_to(camera.global_position + dir * fire_range)
		if pellet_count == 1 and (result.is_empty() or not result.collider is Target):
			var magnetized := aim_assist.magnetize() # near-miss? bend it onto the target
			if not magnetized.is_empty():
				result = magnetized
		if result.is_empty():
			continue
		_spawn_impact(result.position)
		var target := result.collider as Target
		if target == null or target.dead: # a dead target's collision can linger for a frame
			continue
		var critical := target.is_critical_shape(result.shape)
		var entry: Dictionary = totals.get(target, {"damage": 0.0, "critical": false, "point": result.position})
		entry.damage += damage * (crit_multiplier if critical else 1.0)
		entry.critical = entry.critical or critical
		totals[target] = entry
	var any_critical := false
	var any_killed := false
	for target in totals:
		var entry: Dictionary = totals[target]
		any_killed = target.hit(entry.damage, entry.critical, entry.point) or any_killed
		any_critical = any_critical or entry.critical
	if not totals.is_empty():
		hit_confirmed.emit(any_critical, any_killed)
	# Recoil kicks the actual view, so you have to pull down to stay on target.
	var recoil_scale := _m("recoil", 1.0) # frame parts can tame or worsen recoil
	player.apply_recoil(deg_to_rad(recoil_up * recoil_scale), deg_to_rad(randf_range(-recoil_side, recoil_side) * recoil_scale))


func _spread(direction: Vector3, cone: float) -> Vector3:
	# A random direction within `cone` radians of `direction` (evenly over the cone's area).
	if cone <= 0.0:
		return direction
	var radius := tan(cone) * sqrt(randf())
	var turn := randf() * TAU
	var offset := camera.global_basis.x * cos(turn) * radius + camera.global_basis.y * sin(turn) * radius
	return (direction + offset).normalized()


func _cast_ray() -> Dictionary:
	return cast_ray_to(camera.global_position - camera.global_basis.z * fire_range)


func cast_ray_to(to: Vector3) -> Dictionary:
	var from := camera.global_position
	var query := PhysicsRayQueryParameters3D.create(from, to)
	query.exclude = [player.get_rid()]
	return player.get_world_3d().direct_space_state.intersect_ray(query)


func _update_reticle_target() -> void:
	var result := _cast_ray()
	reticle_target = null
	if not result.is_empty() and result.collider is Target and not (result.collider as Target).dead:
		reticle_target = result.collider
	target_under_reticle = reticle_target != null
	var dot := _part("SightDot")
	if dot:
		dot.set_instance_shader_parameter("color", SIGHT_ON_TARGET_COLOR if target_under_reticle else SIGHT_COLOR)


func _spawn_impact(point: Vector3) -> void:
	# A small dark dot where the shot landed, gone after a couple seconds.
	var mark := MeshInstance3D.new()
	var mesh := SphereMesh.new()
	mesh.radius = 0.03
	mesh.height = 0.06
	mark.mesh = mesh
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.05, 0.05, 0.05)
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mark.material_override = mat
	mark.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_world_root().add_child(mark)
	mark.global_position = point
	get_tree().create_timer(2.0).timeout.connect(mark.queue_free)


func _update_ads(delta: float) -> void:
	var target := 1.0 if is_aiming() else 0.0
	_ads_progress = move_toward(_ads_progress, target, delta / maxf(_m("ads_time", ads_time), 0.01))
	ads_amount = smoothstep(0.0, 1.0, _ads_progress)


func _update_fov() -> void:
	var half := tan(deg_to_rad(hip_fov) * 0.5) / lerpf(1.0, zoom, ads_amount)
	camera.fov = rad_to_deg(atan(half) * 2.0)
	MATERIAL.set_shader_parameter("fov_ratio", half / tan(deg_to_rad(viewmodel_fov) * 0.5))
	# Scoped weapons hide the gun once the scope is up (the HUD draws the scope).
	_model.visible = not (weapon.scope_overlay >= 0.5 and ads_amount > 0.85)


func _update_pose(delta: float) -> void:
	var settle := 1.0 - 0.75 * ads_amount # aiming steadies the gun

	# Sway: the gun lags behind your turning, then catches up.
	var turn_speed := _look_delta / maxf(delta, 0.001)
	_look_delta = Vector2.ZERO
	var target := (turn_speed * SWAY_PER_TURN_SPEED).limit_length(SWAY_MAX) * _m("sway", sway) * settle
	_sway = _sway.lerp(target, minf(10.0 * delta, 1.0))

	# Action poses fade in/out, and aiming mostly cancels them.
	var sprinting: bool = player.sprinting
	var sliding: bool = player.sliding
	_sprint_pose = move_toward(_sprint_pose, 1.0 if sprinting else 0.0, 5.0 * delta)
	_slide_pose = move_toward(_slide_pose, 1.0 if sliding else 0.0, 6.0 * delta)
	var hip_only := 1.0 - ads_amount
	var sprint_amount := smoothstep(0.0, 1.0, _sprint_pose) * hip_only
	var slide_amount := smoothstep(0.0, 1.0, _slide_pose) * (1.0 - 0.8 * ads_amount)

	# Air: the gun lags behind vertical motion (dips on the way up, floats on the way down).
	var air_target := 0.0
	if not player.is_on_floor():
		air_target = clampf(-player.velocity.y * AIR_LAG_PER_SPEED, -AIR_LAG_MAX, AIR_LAG_MAX)
	_air_lag = lerpf(_air_lag, air_target, minf(8.0 * delta, 1.0))
	var land_offset: float = player._land_offset # extra kick when you land
	var air_offset := Vector3(0.0, _air_lag + land_offset * 0.6, 0.0) * settle

	# Bob: follows the player's step timing. Sprinting swings it much harder.
	var bob_time: float = player._bob_time
	var bob_weight: float = player._bob_weight
	var swing := Vector3(cos(bob_time * 0.5), -absf(sin(bob_time)), 0.0)
	var bob := swing * Vector3(0.012, 0.01, 0.0) * gun_bob * (1.0 - 0.85 * ads_amount)
	bob += swing * Vector3(0.03, 0.025, 0.0) * gun_bob * sprint_amount
	bob *= bob_weight
	var bob_roll := cos(bob_time * 0.5) * 0.08 * gun_bob * sprint_amount * bob_weight

	rotation = Vector3(_sway.y + _air_lag * 2.0 * settle, _sway.x, bob_roll)
	rotation += SPRINT_ROTATION * sprint_amount + Vector3(deg_to_rad(slide_tilt), 0.0, 0.0) * slide_amount
	var kick := _kick * gun_kick * (1.0 - 0.6 * ads_amount) # aiming tames the jolt
	rotation.x += 0.06 * kick
	position = weapon.hip_position.lerp(weapon.ads_position, ads_amount) + bob + air_offset
	position += SPRINT_OFFSET * sprint_amount + SLIDE_OFFSET * slide_amount
	# Mantling: hands are busy, so the gun dips down out of the way.
	var mantling: bool = player.mantling
	_mantle_pose = move_toward(_mantle_pose, 1.0 if mantling else 0.0, 8.0 * delta)
	var mantle_amount := smoothstep(0.0, 1.0, _mantle_pose)
	position += MANTLE_OFFSET * mantle_amount
	rotation += MANTLE_ROTATION * mantle_amount
	# Grenade throw: quick dip down, then back up.
	_throw_time = maxf(_throw_time - delta, 0.0)
	var throw_amount := sin(PI * _throw_time / THROW_POSE_TIME) if _throw_time > 0.0 else 0.0
	position += THROW_OFFSET * throw_amount
	rotation += THROW_ROTATION * throw_amount
	position.z += 0.035 * kick
	# Weapon swap: dip down and back up.
	_swap_time = maxf(_swap_time - delta, 0.0)
	var swap_amount := sin(PI * _swap_time / SWAP_TIME) if _swap_time > 0.0 else 0.0
	position += SWAP_OFFSET * swap_amount
	rotation += SWAP_ROTATION * swap_amount
	_apply_reload_pose()
	_apply_melee_pose()


func _apply_melee_pose() -> void:
	var p := melee_progress if meleeing else 0.0
	# Wind up: pull back and to the right.
	var wind := smoothstep(0.0, 0.3, p) * (1.0 - smoothstep(0.3, MELEE_IMPACT, p))
	# Strike: shove forward and swing across to the left, then ease back.
	var strike := smoothstep(0.3, MELEE_IMPACT, p) * (1.0 - smoothstep(0.55, 1.0, p))
	position += MELEE_WIND_OFFSET * wind + MELEE_STRIKE_OFFSET * strike
	rotation += MELEE_WIND_ROTATION * wind + MELEE_STRIKE_ROTATION * strike


func _apply_reload_pose() -> void:
	var p := reload_progress if reloading else 0.0
	# Tip up over the first 20%, hold, tip back down over the last 20%.
	var tilt := smoothstep(0.0, 0.2, p) * (1.0 - smoothstep(0.8, 1.0, p))
	rotation.x += RELOAD_TILT * tilt
	rotation.z += RELOAD_ROLL * tilt
	position += RELOAD_LIFT * tilt # brings the magazine well up into view
	# Magazine: out the bottom from 20-40%, back in from 50-75%.
	var mag_out := smoothstep(0.2, 0.4, p) * (1.0 - smoothstep(0.5, RELOAD_SEATED, p))
	_part("Magazine").position = _mag_rest + Vector3.DOWN * RELOAD_MAG_DROP * mag_out


func _world_root() -> Node:
	# Where to put spawned effects: the running level (or the tree root as a fallback).
	return get_tree().current_scene if get_tree().current_scene else get_tree().root
