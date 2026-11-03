class_name Enemy
extends CharacterBody3D
# What every enemy (Soldier, Swarm, Tank) shares:
# - health, hits, damage numbers, the white hit flash, death, corpse and respawn
# - the target interface the gun, melee, grenades, aim assist and HUD use
#   (aim_parts, is_critical_shape, hit, health, max_health, dead)
# - a crit spot (head or weak point) whose collision shape follows a bone
# - finding and seeing the player, turning to face them, ground movement
# Each type extends this, sets its look, and fills in _think() (where to go and when
# to attack, returning the velocity it wants) and _animate().
# The scene needs a CharacterModel child named "Model" and its collision shapes.

signal died

const GRAVITY := 20.0
const ACCEL := 12.0 # m/s per second toward the wanted velocity
const CORPSE_TIME := 3.0 # seconds the body stays before disappearing

@export var body_color := Color(0.55, 0.15, 0.13)
@export var joint_color := Color(0.16, 0.16, 0.18)
@export var model_scale := 1.0

var health := 100.0
var max_health := 100.0
var dead := false
var alerted := false
var manager: Node = null # the TargetManager, set when it starts up
var model: CharacterModel
var crit_shape: CollisionShape3D = null # takes crits (head or weak spot); null = no crit spot
var steering := true # false while the enemy sets its own velocity (a leap)
var slow_left := 0.0 # seconds of Slow left (Snare Mine)
var slow_amount := 0.0
var blind_left := 0.0 # seconds blinded (Blinding Grenade): can't see or attack
var follow_in_physics := true # false: the type updates pinned shapes itself (after a pose modifier)

var _player: Node3D
var _spawn := Transform3D.IDENTITY
var _flash := 0.0
var _pinned := [] # [shape, bone name, transform relative to the bone]
var _path := PackedVector3Array() # navigation path to the current goal
var _path_age := 99.0
var _path_goal := Vector3.INF


func _data() -> EnemyData:
	return null # each enemy type returns its data resource


func _ready() -> void:
	add_to_group("targets") # living targets: aim assist, melee, grenades and lunges look here
	add_to_group("all_targets") # dead or alive, for resets
	add_to_group("enemies")
	_spawn = global_transform
	model = $Model
	model.scale = Vector3.ONE * model_scale
	model.set_colors(body_color, joint_color)
	max_health = _data().max_health
	health = max_health
	model.force_pose("Idle") # measure gear and hitbox spots from the standing pose
	_setup()
	model.play("Idle", 1.0, 0.0)


func _setup() -> void:
	pass


func pin_shape(shape: CollisionShape3D, bone: String, world_point: Vector3) -> void:
	# Keeps a collision shape at `world_point` relative to `bone` (measured now), so it
	# follows that body part through the animations.
	var local := model.bone_transform(bone).affine_inverse() * Transform3D(global_basis, world_point)
	_pinned.append([shape, bone, local])


func _follow_bones() -> void:
	for pin in _pinned:
		var xf: Transform3D = model.bone_transform(pin[1]) * pin[2]
		(pin[0] as CollisionShape3D).global_transform = Transform3D(Basis(xf.basis.get_rotation_quaternion()), xf.origin)


# --- Target interface ---------------------------------------------------------------

func aim_parts() -> Array:
	# Aim assist targets: center and rough radius of each part. Crit spot first.
	var parts := []
	if crit_shape:
		parts.append({"position": crit_shape.global_position, "radius": _crit_radius(), "critical": true})
	parts.append({"position": global_position + Vector3.UP * _body_height() * 0.5, "radius": _body_radius(), "critical": false})
	return parts


func _crit_radius() -> float:
	return 0.15


func _body_height() -> float:
	return 1.8


func _body_radius() -> float:
	return 0.35


func is_critical_shape(shape_index: int) -> bool:
	return crit_shape != null and shape_owner_get_owner(shape_find_owner(shape_index)) == crit_shape


func hit(damage: float, critical: bool, point: Vector3, color := Color(0, 0, 0, 0)) -> bool:
	# `color` tints the damage number (poison and fog use their own colors).
	# Returns true if this hit killed it.
	if dead:
		return false
	alerted = true # getting shot from out of sight range still wakes it up
	_flash = 1.0
	var through := _absorb(damage, critical, point) # shields take their share first
	if through > 0.0:
		DamageNumbers.spawn(self, through, critical, point, color)
		health -= through
	if health <= 0.0:
		_die()
		return true
	_on_hit(critical)
	return false


func _absorb(damage: float, _critical: bool, _point: Vector3) -> float:
	return damage # no shield: everything reaches health


func _on_hit(_critical: bool) -> void:
	pass


func reset() -> void:
	# Back to its spawn point at full health, unaware of you (the "Reset targets" button).
	global_transform = _spawn
	velocity = Vector3.ZERO
	steering = true
	max_health = _data().max_health
	health = max_health
	alerted = false
	slow_left = 0.0
	blind_left = 0.0
	if dead:
		dead = false
		add_to_group("targets")
		_set_collision(true)
	visible = true
	_on_reset()
	model.play("Idle", 1.0, 0.0)


func _on_reset() -> void:
	pass


func _die() -> void:
	dead = true
	health = 0.0
	remove_from_group("targets")
	_set_collision(false)
	_on_death()
	model.play("Death01", 1.0, 0.1)
	died.emit()
	get_tree().create_timer(CORPSE_TIME).timeout.connect(func() -> void:
		if dead:
			visible = false)
	if manager and manager.respawn_time > 0.0:
		get_tree().create_timer(maxf(manager.respawn_time, CORPSE_TIME)).timeout.connect(func() -> void:
			if dead:
				reset())


func _on_death() -> void:
	pass


func _set_collision(on: bool) -> void:
	# Deferred because this can happen in the middle of a physics query.
	for child in get_children():
		if child is CollisionShape3D:
			child.set_deferred("disabled", not on)


# --- Each frame ---------------------------------------------------------------------

func _physics_process(delta: float) -> void:
	if follow_in_physics:
		_follow_bones()
	if not is_on_floor():
		velocity.y -= GRAVITY * delta
	if dead:
		velocity.x = 0.0
		velocity.z = 0.0
		move_and_slide()
		return
	_retarget(delta)
	var wish := Vector3.ZERO
	if _player and _player.has_method("is_invisible") and _player.is_invisible():
		alerted = false # veiled in spore fog: it loses track of you (a hit only wakes it for a moment)
	blind_left = maxf(blind_left - delta, 0.0)
	if blind_left > 0.0:
		_stand_down() # blinded: stands there, can't see or attack
	elif _data().ai_enabled >= 0.5 and _player and _player.process_mode != Node.PROCESS_MODE_DISABLED and not (_player.has_method("is_invisible") and _player.is_invisible()):
		wish = _think(delta)
	else:
		_stand_down() # AI off, or the player is dead
	slow_left = maxf(slow_left - delta, 0.0)
	if slow_left > 0.0:
		wish *= 1.0 - slow_amount
	if steering:
		velocity.x = move_toward(velocity.x, wish.x, ACCEL * delta)
		velocity.z = move_toward(velocity.z, wish.z, ACCEL * delta)
	move_and_slide()
	_after_move(delta)


func _think(_delta: float) -> Vector3:
	return Vector3.ZERO # the velocity this enemy wants (each type decides)


func _stand_down() -> void:
	pass


func _after_move(_delta: float) -> void:
	pass


func _process(delta: float) -> void:
	if dead:
		return
	_flash = move_toward(_flash, 0.0, 6.0 * delta)
	model.set_flash(_flash)
	_animate(delta)


func _animate(_delta: float) -> void:
	pass


# --- Helpers ------------------------------------------------------------------------

func apply_blind(seconds: float) -> void:
	blind_left = maxf(blind_left, seconds)
	DamageNumbers.show_text(self, "BLINDED", false, global_position + Vector3.UP * (_body_height() + 0.3), Color(1.0, 0.95, 0.6))


func apply_slow(amount: float, seconds: float) -> void:
	# Slow status (from the Snare Mine): moves this much slower for a while.
	slow_amount = maxf(clampf(amount, 0.0, 0.95), slow_amount if slow_left > 0.0 else 0.0)
	slow_left = maxf(slow_left, seconds)


func path_direction(goal: Vector3, delta: float) -> Vector3:
	# Which way to walk to reach `goal`, around walls, using the map's navigation mesh
	# (the arena bakes one). With no navigation mesh (the test range) it's a straight line.
	_path_age += delta
	var straight := goal - global_position
	straight.y = 0.0
	var map_rid := get_world_3d().navigation_map
	if _path_age > 0.3 or goal.distance_to(_path_goal) > 1.5:
		_path = NavigationServer3D.map_get_path(map_rid, global_position, goal, true)
		_path_age = 0.0
		_path_goal = goal
	while _path.size() > 1:
		var to_next := _path[0] - global_position
		to_next.y = 0.0
		if to_next.length() > 0.6:
			break
		_path.remove_at(0) # reached that point
	if _path.size() < 2:
		return straight.normalized() if straight.length() > 0.01 else Vector3.ZERO
	var step := _path[0] - global_position
	step.y = 0.0
	return step.normalized() if step.length() > 0.01 else straight.normalized()


var _retarget_left := 0.0


func _retarget(delta: float) -> void:
	# Enemies go after whoever's closest: you or an AI teammate. `_player` is that target
	# (the name stayed from before teammates existed).
	_retarget_left -= delta
	if _player != null and _retarget_left > 0.0 and not (_player.get("dead") == true):
		return
	_retarget_left = 0.5
	var player := get_tree().get_first_node_in_group("player") as Node3D
	var best := player
	var best_distance := global_position.distance_to(player.global_position) if player else INF
	if player and player.has_method("is_invisible") and player.is_invisible():
		best_distance = INF # veiled: teammates are fair game, you aren't
	for mate in get_tree().get_nodes_in_group("teammates"):
		var d := global_position.distance_to((mate as Node3D).global_position)
		if d < best_distance:
			best = mate
			best_distance = d
	_player = best


func damage_target(amount: float, melee := false) -> void:
	# Hurts whoever this enemy is going after (you through PlayerHealth, or a teammate).
	if _player == null:
		return
	if _player.is_in_group("player"):
		var health_node := player_health()
		if health_node:
			health_node.take_damage(amount, melee)
	elif _player.has_method("take_damage"):
		_player.take_damage(amount, melee)


func flat_to_player() -> Vector3:
	var to := _player.global_position - global_position
	to.y = 0.0
	return to


func player_aim_point() -> Vector3:
	return _player.global_position + Vector3.UP * 0.3 # chest height


func can_see_player(from: Vector3) -> bool:
	if SporeFog.blocks(get_tree(), from, player_aim_point()):
		return false # thick spore fog in the way
	var query := PhysicsRayQueryParameters3D.create(from, player_aim_point())
	query.exclude = enemy_rids()
	var result := get_world_3d().direct_space_state.intersect_ray(query)
	return not result.is_empty() and ((result.collider as Node).is_in_group("player") or (result.collider as Node).is_in_group("teammates"))


func enemy_rids() -> Array[RID]:
	var rids: Array[RID] = []
	for enemy in get_tree().get_nodes_in_group("enemies"):
		rids.append((enemy as CollisionObject3D).get_rid())
	return rids


func turn_toward(direction: Vector3, delta: float, speed := -1.0) -> void:
	if direction.length() < 0.01:
		return
	var rate := _data().turn_speed if speed < 0.0 else speed
	var target_yaw := atan2(-direction.x, -direction.z) # the body's front is -Z
	rotation.y = lerp_angle(rotation.y, target_yaw, 1.0 - exp(-rate * delta))


func player_health() -> Node:
	return get_tree().get_first_node_in_group("player_health")
