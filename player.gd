extends CharacterBody3D
# First-person movement: walk, sprint, crouch, slide, double jump,
# air momentum, apex hang, coyote time, landing dip, mantling.
# Started from Godot's built-in "CharacterBody3D: Basic Movement" template.

# --- Tunable values (the settings menu edits these live) ---
@export var mouse_sensitivity := 2.0 # x0.001 radians per pixel
@export var stick_sensitivity := 5.0 # radians per second at full tilt
@export var walk_speed := 6.0
@export var sprint_speed := 12.0
@export var crouch_speed := 2.5
@export var slide_speed := 12.0 # speed at the start of a slide
@export var slide_distance := 8.0 # meters a slide covers before slowing to crouch speed
@export var jump_height_1 := 2.0 # meters, ground jump
@export var jump_height_2 := 2.3 # meters, air jump
@export var ascent_gravity := 20.0 # higher = faster rise (height stays the same)
@export var descent_gravity := 32.0 # higher = faster fall
@export var bob_intensity := 1.5 # 0 = no head bob, 1 = normal, 2 = double
@export var air_control := 5.0 # how fast you can steer in the air (m/s per second); 0 = none
@export var apex_hang := 0.5 # how much gravity eases off at the top of a jump; 0 = none, 0.9 = max
@export var coyote_time := 0.12 # seconds after leaving a ledge that a ground jump still works
@export var landing_dip := 1.0 # camera dip on landing; 0 = none, 1 = normal
@export var mantle_reach := 2.0 # how high above your feet a ledge can be and still be mantled; 0 = off
@export var mantle_time := 0.35 # seconds to pull yourself up
@export var walk_stride := 1.6 # meters per step when walking (head bob timing)
@export var sprint_stride := 2.8 # meters per step when sprinting; longer = slower, deeper strides

const MAX_JUMPS := 2 # before abilities (Overgrowth adds more)
const STAND_HEIGHT := 2.0
const CROUCH_HEIGHT := 1.2
const STAND_CAMERA_Y := 0.6
const CAMERA_SMOOTHING := 12.0
# Head bob: one dip per step. Sprinting takes deeper strides.
const WALK_BOB_UP := 0.06 # meters of up/down at intensity 1
const SPRINT_BOB_UP := 0.11
const WALK_BOB_SIDE := 0.04 # meters of side-to-side at intensity 1
const SPRINT_BOB_SIDE := 0.07
const APEX_SPEED := 2.0 # apex hang applies while vertical speed is below this (m/s)
const LANDING_DIP_PER_SPEED := 0.012 # meters of dip per m/s of fall speed
const LANDING_DIP_MAX := 0.25

var sprinting := false
var crouching := false
var sliding := false
var jumps_used := 0
var slide_dir := Vector3.ZERO
var slide_speed_now := 0.0
var takeoff_speed := 0.0 # keeps sprint/slide momentum while airborne
var lunging := false # melee lunge: dashing toward a target
var mantling := false # pulling up onto a ledge
var _mantle_from := Vector3.ZERO
var _mantle_to := Vector3.ZERO
var _mantle_t := 0.0
var _lunge_target: Node3D = null
var _lunge_time := 0.0
var _crouch_was_held := false
var _crouch_toggled := false # crouch is a toggle: tap to crouch, tap again to stand
var _camera_base_y := STAND_CAMERA_Y
var _bob_time := 0.0
var _bob_weight := 0.0 # fades the bob in and out when you start or stop walking
var _sprint_blend := 0.0 # 0 = walking stride, 1 = sprinting stride, eased between
var _air_time := 0.0 # seconds since last on the floor
var _land_kick := 0.0 # where the landing dip is pulling the camera
var _land_offset := 0.0 # current landing dip applied to the camera

@onready var camera: Camera3D = $Camera3D
@onready var collider: CollisionShape3D = $CollisionShape3D
@onready var mesh: MeshInstance3D = $MeshInstance3D
@onready var gun: Gun = $Camera3D/Gun
@export var abilities: AbilityManager
@export var mods: Modifiers # frame parts (and later traits) adjust stats through this


func _ready() -> void:
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		_look(Vector2(event.relative) * mouse_sensitivity * 0.001)
	elif event is InputEventMouseButton and event.pressed:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED # click to recapture


func _look(amount: Vector2) -> void:
	amount *= gun.look_multiplier() # aiming down sights slows turning
	gun.add_look(amount)
	rotate_y(-amount.x) # body turns left/right
	camera.rotate_x(-amount.y) # camera tilts up/down
	camera.rotation.x = clampf(camera.rotation.x, deg_to_rad(-89), deg_to_rad(89))


func _physics_process(delta: float) -> void:
	var stick := Input.get_vector("look_left", "look_right", "look_up", "look_down")
	_look(stick * stick_sensitivity * delta * gun.aim_assist.friction_scale()) # friction: slower near targets

	var input_dir := Input.get_vector("move_left", "move_right", "move_forward", "move_back")
	var wish_dir := (transform.basis * Vector3(input_dir.x, 0, input_dir.y)).normalized()

	if mantling or _try_mantle(input_dir):
		_update_mantle(delta) # takes over movement until you're up
		_update_body_height(delta)
		_update_landing_dip(delta)
		return

	_update_sprint(input_dir)
	_update_crouch_and_slide(wish_dir)
	_apply_gravity(delta)
	_handle_jump(wish_dir)
	_apply_horizontal(wish_dir, delta)
	if lunging:
		_apply_lunge(delta) # overrides normal movement while dashing
	var was_on_floor := is_on_floor()
	var fall_speed := -velocity.y # move_and_slide zeroes this on landing, so grab it first
	move_and_slide()
	if is_on_floor() and not was_on_floor:
		_land(fall_speed)
	_update_body_height(delta)
	_update_landing_dip(delta)
	_update_head_bob(delta)


func _update_sprint(input_dir: Vector2) -> void:
	# Press once to sprint; it stays on until you stop moving or crouch.
	# Sprinting from a crouch stands you up.
	# Aiming down sights stops a sprint and blocks starting one.
	# So does firing.
	if Input.is_action_just_pressed("sprint") and not sliding and not gun.is_aiming() and not Input.is_action_pressed("fire"):
		sprinting = true
		_crouch_toggled = false
	if input_dir.length() < 0.1 or gun.is_aiming() or Input.is_action_pressed("fire"):
		sprinting = false


func _update_crouch_and_slide(wish_dir: Vector3) -> void:
	var crouch_held := Input.is_action_pressed("crouch")
	var crouch_started := crouch_held and not _crouch_was_held
	_crouch_was_held = crouch_held

	# Tapping crouch while sprinting on the ground starts a slide instead.
	# The slide runs its full distance on its own; only a jump ends it early.
	# Afterward you stand back up.
	if crouch_started:
		if sprinting and is_on_floor() and _m("slide_speed", slide_speed) > _m("crouch_speed", crouch_speed):
			sliding = true
			slide_dir = wish_dir if wish_dir else -transform.basis.z
			slide_speed_now = _m("slide_speed", slide_speed)
		elif not sliding:
			_crouch_toggled = not _crouch_toggled

	if sliding or _crouch_toggled:
		crouching = true
		sprinting = false
	elif crouching and _can_stand(): # stays crouched under low ceilings until clear
		crouching = false


func _apply_gravity(delta: float) -> void:
	if is_on_floor():
		jumps_used = 0
		takeoff_speed = 0.0
		_air_time = 0.0
		return

	_air_time += delta
	if jumps_used == 0 and _air_time > _m("coyote_time", coyote_time):
		jumps_used = 1 # walked off a ledge and missed the coyote window: only the air jump is left

	var gravity := ascent_gravity if velocity.y > 0.0 else descent_gravity
	if jumps_used > 0 and absf(velocity.y) < APEX_SPEED:
		gravity *= 1.0 - _apex_hang_clamped() # float a moment at the top of the jump
	velocity.y -= gravity * delta


func _handle_jump(wish_dir: Vector3) -> void:
	if not Input.is_action_just_pressed("jump") or jumps_used >= MAX_JUMPS + abilities.extra_jumps():
		return
	var height := _m("jump_height", jump_height_1 if jumps_used == 0 else jump_height_2)
	velocity.y = _launch_speed(height)
	jumps_used += 1
	var air_speed := Vector2(velocity.x, velocity.z).length()
	if jumps_used > 1 and wish_dir:
		# The air jump lets you change direction: aim your momentum where you're pushing.
		air_speed = maxf(air_speed, _m("walk_speed", walk_speed))
		velocity.x = wish_dir.x * air_speed
		velocity.z = wish_dir.z * air_speed
	takeoff_speed = maxf(takeoff_speed, air_speed)
	sliding = false


func _apply_horizontal(wish_dir: Vector3, delta: float) -> void:
	if sliding:
		# Constant slow-down chosen so the slide covers exactly slide_distance.
		var start := _m("slide_speed", slide_speed)
		var end := _m("crouch_speed", crouch_speed)
		var decel := (start * start - end * end) / (2.0 * _m("slide_distance", slide_distance))
		slide_speed_now = move_toward(slide_speed_now, end, decel * delta)
		velocity.x = slide_dir.x * slide_speed_now
		velocity.z = slide_dir.z * slide_speed_now
		if slide_speed_now <= end:
			sliding = false
		return

	var speed := _m("walk_speed", walk_speed)
	if crouching:
		speed = _m("crouch_speed", crouch_speed)
	elif sprinting:
		speed = _m("sprint_speed", sprint_speed)
	speed *= abilities.speed_multiplier() * gun.move_multiplier()
	if not is_on_floor():
		# In the air you keep your momentum and can only steer it gradually.
		speed = maxf(speed, takeoff_speed)
		if wish_dir:
			var h := Vector2(velocity.x, velocity.z)
			h = h.move_toward(Vector2(wish_dir.x, wish_dir.z) * speed, _m("air_control", air_control) * delta)
			velocity.x = h.x
			velocity.z = h.y
		return

	if wish_dir:
		velocity.x = wish_dir.x * speed
		velocity.z = wish_dir.z * speed
	else:
		velocity.x = move_toward(velocity.x, 0, speed)
		velocity.z = move_toward(velocity.z, 0, speed)


func _update_body_height(delta: float) -> void:
	# Shrink or grow the capsule from the feet so the bottom stays on the floor.
	var height := CROUCH_HEIGHT if crouching else STAND_HEIGHT
	var offset := (height - STAND_HEIGHT) / 2.0
	(collider.shape as CapsuleShape3D).height = height
	collider.position.y = offset
	(mesh.mesh as CapsuleMesh).height = height
	mesh.position.y = offset
	var camera_y := STAND_CAMERA_Y + (height - STAND_HEIGHT)
	_camera_base_y = lerpf(_camera_base_y, camera_y, CAMERA_SMOOTHING * delta)


func _update_head_bob(delta: float) -> void:
	# Up/down once per step, side-to-side once per two steps.
	var speed := Vector2(velocity.x, velocity.z).length()
	var walking := is_on_floor() and not sliding and speed > 0.5
	_sprint_blend = move_toward(_sprint_blend, 1.0 if sprinting else 0.0, 3.0 * delta)
	var stride := maxf(lerpf(walk_stride, sprint_stride, _sprint_blend), 0.1)
	if walking:
		_bob_time += speed / stride * PI * delta # one step = half a sine wave
	_bob_weight = move_toward(_bob_weight, 1.0 if walking else 0.0, 4.0 * delta)
	var amount := bob_intensity * _bob_weight * (1.0 - 0.75 * gun.ads_amount) # steadier when aimed
	var up := lerpf(WALK_BOB_UP, SPRINT_BOB_UP, _sprint_blend) * amount
	var side := lerpf(WALK_BOB_SIDE, SPRINT_BOB_SIDE, _sprint_blend) * amount
	camera.position.x = cos(_bob_time * 0.5) * side
	camera.position.y = _camera_base_y + _land_offset + absf(sin(_bob_time)) * up


func _apex_hang_clamped() -> float:
	return clampf(apex_hang, 0.0, 0.9)


func _launch_speed(height: float) -> float:
	# Upward speed that reaches exactly `height`, accounting for the lighter gravity near the top.
	var hang_gravity := ascent_gravity * (1.0 - _apex_hang_clamped())
	var hang_speed := minf(APEX_SPEED, sqrt(2.0 * hang_gravity * height))
	var hang_height := hang_speed * hang_speed / (2.0 * hang_gravity)
	return sqrt(hang_speed * hang_speed + 2.0 * ascent_gravity * (height - hang_height))


func _land(fall_speed: float) -> void:
	_land_kick = -clampf(fall_speed * LANDING_DIP_PER_SPEED, 0.0, LANDING_DIP_MAX) * landing_dip


func _update_landing_dip(delta: float) -> void:
	# The camera drops quickly toward the kick while the kick itself eases back to zero.
	_land_offset = lerpf(_land_offset, _land_kick, minf(25.0 * delta, 1.0))
	_land_kick = lerpf(_land_kick, 0.0, minf(8.0 * delta, 1.0))


const LUNGE_STOP_DISTANCE := 1.0 # meters (center to center) where a lunge stops short of the target
const LUNGE_MIN_TIME := 0.15 # even point-blank lunges last this long, so the camera swings onto the target
const LUNGE_MAX_TIME := 0.6 # seconds before a lunge gives up (e.g. blocked by a wall)
const LUNGE_TURN_RATE := 20.0 # how fast the view swings onto the target at full camera pull


func start_lunge(target: Node3D) -> void:
	lunging = true
	_lunge_target = target
	_lunge_time = 0.0
	sprinting = false
	sliding = false


func _apply_lunge(delta: float) -> void:
	_lunge_time += delta
	# Aim for where the target's "player-height center" would be, so on level ground
	# this is a flat dash and toward a raised/lowered target it pulls you up or down.
	var goal := _lunge_target.global_position + Vector3.UP * 1.0
	var to := goal - global_position
	to.y *= gun.lunge_vertical # 0 = ground only, 1 = full pull up/down
	var arrived := to.length() <= LUNGE_STOP_DISTANCE
	if (arrived and _lunge_time >= LUNGE_MIN_TIME) or _lunge_time > LUNGE_MAX_TIME:
		lunging = false
		velocity = Vector3.ZERO # stop dead; gravity takes over next frame
		return
	# Dash in (or hold still if already close) while the view swings onto the target.
	velocity = Vector3.ZERO if arrived else to.normalized() * gun.lunge_speed # overrides gravity while dashing
	_turn_toward(_lunge_target.aim_parts()[0].position, gun.lunge_aim, delta) # upper body, so you arrive looking at it


func _turn_toward(point: Vector3, strength: float, delta: float) -> void:
	# Swing the view toward a point. strength 1 = locks on almost instantly, 0 = no pull.
	if strength <= 0.0:
		return
	var dir := (point - camera.global_position).normalized()
	var weight := minf(strength * LUNGE_TURN_RATE * delta, 1.0)
	rotation.y = lerp_angle(rotation.y, atan2(-dir.x, -dir.z), weight)
	camera.rotation.x = clampf(lerpf(camera.rotation.x, asin(dir.y), weight), deg_to_rad(-89), deg_to_rad(89))


func _m(stat: String, base: float) -> float:
	# A stat after frame part (and other) modifiers.
	return mods.apply(stat, base)


func apply_recoil(pitch: float, yaw: float) -> void:
	# Kicks the view directly (not slowed by ADS look scaling).
	rotate_y(yaw)
	camera.rotate_x(pitch)
	camera.rotation.x = clampf(camera.rotation.x, deg_to_rad(-89), deg_to_rad(89))


func reset_input_edges() -> void:
	# Called when the settings menu closes, so the button that closed it
	# (circle is also crouch) doesn't count as a fresh press.
	_crouch_was_held = Input.is_action_pressed("crouch")


const MANTLE_WALL_CHECK := 0.85 # meters ahead (from your center) to look for a wall
const MANTLE_STEP_IN := 0.6 # how far past the wall face you end up standing
const MANTLE_MIN_HEIGHT := 0.3 # ledges lower than this above your feet aren't worth a mantle


func _try_mantle(input_dir: Vector2) -> bool:
	# Mantle when you're airborne, pushing forward, facing a wall with a flat top in reach,
	# and there's room to stand up there.
	var reach := _m("mantle_reach", mantle_reach)
	if mantle_reach <= 0.0 or reach <= 0.0 or is_on_floor() or sliding or lunging or input_dir.y > -0.5:
		return false
	var space := get_world_3d().direct_space_state
	var forward := -global_basis.z
	forward.y = 0.0
	forward = forward.normalized()
	var shape := collider.shape as CapsuleShape3D
	var feet := global_position.y + collider.position.y - shape.height * 0.5

	# 1. A wall right in front of you at chest height.
	var chest := global_position + Vector3.UP * 0.2
	var wall := _ray(space, chest, chest + forward * MANTLE_WALL_CHECK)
	if wall.is_empty() or wall.collider is Target:
		return false

	# 2. A flat top within reach, just past the wall face.
	var probe: Vector3 = wall.position + forward * MANTLE_STEP_IN
	var top := _ray(space, Vector3(probe.x, feet + reach + 0.1, probe.z), Vector3(probe.x, feet + MANTLE_MIN_HEIGHT, probe.z))
	if top.is_empty() or top.normal.y < 0.7 or top.collider is Target:
		return false

	# 3. Room to stand there.
	var stand: Vector3 = top.position + Vector3.UP * (STAND_HEIGHT * 0.5 + 0.05)
	var room := PhysicsShapeQueryParameters3D.new()
	var capsule := CapsuleShape3D.new()
	capsule.height = STAND_HEIGHT
	capsule.radius = shape.radius
	room.shape = capsule
	room.transform = Transform3D(Basis(), stand)
	room.exclude = [get_rid()]
	if not space.intersect_shape(room, 1).is_empty():
		return false

	mantling = true
	_mantle_t = 0.0
	_mantle_from = global_position
	_mantle_to = stand
	_crouch_toggled = false
	crouching = false
	sprinting = false
	return true


func _update_mantle(delta: float) -> void:
	# Rise up along the wall first, then roll forward over the edge.
	_mantle_t += delta / maxf(_m("mantle_time", mantle_time), 0.05)
	var t := minf(_mantle_t, 1.0)
	var up := smoothstep(0.0, 0.55, t)
	var over := smoothstep(0.45, 1.0, t)
	global_position = Vector3(
		lerpf(_mantle_from.x, _mantle_to.x, over),
		lerpf(_mantle_from.y, _mantle_to.y, up),
		lerpf(_mantle_from.z, _mantle_to.z, over))
	velocity = Vector3.ZERO
	if t >= 1.0:
		mantling = false
		jumps_used = 0
		takeoff_speed = 0.0
		_land(4.0) # a small landing dip as you come over the top


func _ray(space: PhysicsDirectSpaceState3D, from: Vector3, to: Vector3) -> Dictionary:
	var query := PhysicsRayQueryParameters3D.create(from, to)
	query.exclude = [get_rid()]
	return space.intersect_ray(query)


func _can_stand() -> bool:
	# Would the capsule hit anything if it grew back to full height?
	return not test_move(global_transform, Vector3.UP * (STAND_HEIGHT - CROUCH_HEIGHT))
