class_name Ally
extends CharacterBody3D
# An AI teammate (for testing team abilities like IronBark's Shield Share).
# - Follows the player, keeping follow_min to follow_max meters away, pathing around walls.
# - Shoots the nearest enemy it can see (hitscan, a little aim error).
# - Has shields and health like the player; enemies target whoever is closest, so it
#   takes real damage. Shield Share calls restore_shield().
# - When killed it goes down, then gets back up next to the player.
# Its origin is its center (like the player's), with the body model below.
# Tuning: data/allies/ally.tres.

@export var data: AllyData

const BODY_COLOR := Color(0.25, 0.7, 0.65) # friendly teal
const GRAVITY := 20.0
const ACCEL := 14.0

var health := 100.0
var shield := 100.0
var dead := false

var _player: Node3D
var _since_hit := 99.0
var _fire_cooldown := 0.5
var _down_left := 0.0
var _path := PackedVector3Array()
var _path_age := 99.0
var _gun: Node3D
var _muzzle: Marker3D
var _label: Label3D

@onready var model: CharacterModel = $Model


func _ready() -> void:
	add_to_group("teammates")
	add_to_group("all_teammates")
	health = data.max_health
	shield = data.max_shield
	model.set_colors(BODY_COLOR, Color(0.12, 0.14, 0.16))
	_build_gun()
	_label = Label3D.new()
	_label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	_label.pixel_size = 0.004
	_label.font_size = 32
	_label.outline_size = 8
	_label.position = Vector3.UP * 1.25
	_label.no_depth_test = true
	add_child(_label)


func _build_gun() -> void:
	_gun = Node3D.new()
	add_child(_gun)
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.15, 0.15, 0.17)
	var body := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = Vector3(0.06, 0.09, 0.45)
	body.mesh = box
	body.material_override = mat
	body.position = Vector3(0, 0, -0.15)
	_gun.add_child(body)
	_muzzle = Marker3D.new()
	_muzzle.position = Vector3(0, 0, -0.4)
	_gun.add_child(_muzzle)


# --- Damage and shields -------------------------------------------------------------

func take_damage(amount: float, _melee := false) -> void:
	if dead or amount <= 0.0:
		return
	_since_hit = 0.0
	var to_shield := minf(amount, shield)
	shield -= to_shield
	health -= amount - to_shield
	if health <= 0.0:
		_go_down()


func restore_shield(amount: float) -> void:
	# Shield Share: an explosion of yours refilled this teammate's shields.
	if dead:
		return
	var before := shield
	shield = minf(shield + amount, data.max_shield)
	if shield > before:
		DamageNumbers.show_text(self, "+%d" % roundi(shield - before), false, global_position + Vector3.UP * 1.0, Color(0.55, 0.8, 1.0))


func _go_down() -> void:
	dead = true
	health = 0.0
	shield = 0.0
	_down_left = data.respawn_time
	remove_from_group("teammates")
	$Collision.set_deferred("disabled", true)
	model.play("Death01", 1.0, 0.1)
	_gun.visible = false


func _get_up() -> void:
	dead = false
	health = data.max_health
	shield = data.max_shield
	add_to_group("teammates")
	$Collision.set_deferred("disabled", false)
	_gun.visible = true
	if _player:
		global_position = _player.global_position + _player.global_basis.x * 2.0
	velocity = Vector3.ZERO
	model.play("Idle", 1.0, 0.1)


func reset() -> void:
	# The "Reset targets and enemies" button brings teammates back too.
	if dead:
		_get_up()
	health = data.max_health
	shield = data.max_shield


# --- Each frame -----------------------------------------------------------------------

func _physics_process(delta: float) -> void:
	if not is_on_floor():
		velocity.y -= GRAVITY * delta
	if _player == null:
		_player = get_tree().get_first_node_in_group("player")
	if dead:
		_down_left -= delta
		velocity.x = 0.0
		velocity.z = 0.0
		move_and_slide()
		if _down_left <= 0.0:
			_get_up()
		return
	_regen(delta)
	var wish := Vector3.ZERO
	var enemy := _nearest_visible_enemy() if data.ai_enabled >= 0.5 else null
	if data.ai_enabled >= 0.5 and _player:
		wish = _follow(delta) * data.move_speed
	if enemy:
		_face(enemy.global_position - global_position, delta)
		_fire_cooldown -= delta
		if _fire_cooldown <= 0.0:
			_fire_cooldown = data.fire_interval * randf_range(0.85, 1.2)
			_shoot(enemy)
	elif wish.length() > 0.1:
		_face(wish, delta)
	velocity.x = move_toward(velocity.x, wish.x, ACCEL * delta)
	velocity.z = move_toward(velocity.z, wish.z, ACCEL * delta)
	move_and_slide()


func _regen(delta: float) -> void:
	_since_hit += delta
	if _since_hit < data.recharge_delay:
		return
	if shield < data.max_shield:
		shield = minf(shield + data.shield_regen * delta, data.max_shield)
	elif health < data.max_health:
		health = minf(health + data.health_regen * delta, data.max_health)


func _follow(delta: float) -> Vector3:
	var to := _player.global_position - global_position
	to.y = 0.0
	var distance := to.length()
	if distance < data.follow_min:
		return -to.normalized() * 0.5 # give you some room
	if distance <= data.follow_max:
		return Vector3.ZERO
	# Walk back toward you, around walls.
	_path_age += delta
	if _path_age > 0.4 or _path.size() < 2:
		_path = NavigationServer3D.map_get_path(get_world_3d().navigation_map, global_position, _player.global_position, true)
		_path_age = 0.0
	while _path.size() > 1 and Vector2(_path[0].x - global_position.x, _path[0].z - global_position.z).length() < 0.6:
		_path.remove_at(0)
	if _path.size() >= 2:
		var step := _path[0] - global_position
		step.y = 0.0
		if step.length() > 0.01:
			return step.normalized()
	return to.normalized()


func _face(direction: Vector3, delta: float) -> void:
	direction.y = 0.0
	if direction.length() < 0.01:
		return
	rotation.y = lerp_angle(rotation.y, atan2(-direction.x, -direction.z), 1.0 - exp(-10.0 * delta))


func _nearest_visible_enemy() -> Node3D:
	var best: Node3D = null
	var best_distance := data.sight_range
	var eyes := global_position + Vector3.UP * 0.6
	for enemy in get_tree().get_nodes_in_group("enemies"):
		if enemy.dead:
			continue
		var aim: Vector3 = enemy.aim_parts()[-1].position
		var distance := eyes.distance_to(aim)
		if distance >= best_distance:
			continue
		var query := PhysicsRayQueryParameters3D.create(eyes, aim)
		query.exclude = [get_rid()]
		var hit := get_world_3d().direct_space_state.intersect_ray(query)
		if hit.is_empty() or hit.collider != enemy:
			continue
		best = enemy
		best_distance = distance
	return best


func _shoot(enemy: Node3D) -> void:
	var from := _muzzle.global_position
	var aim: Vector3 = enemy.aim_parts()[-1].position
	var direction := (aim - from).normalized()
	var side := direction.cross(Vector3.UP).normalized()
	var up := side.cross(direction)
	var radius := tan(deg_to_rad(data.spread)) * sqrt(randf())
	var turn := randf() * TAU
	direction = (direction + side * cos(turn) * radius + up * sin(turn) * radius).normalized()
	var query := PhysicsRayQueryParameters3D.create(from, from + direction * (data.sight_range + 5.0))
	query.exclude = [get_rid()]
	var hit := get_world_3d().direct_space_state.intersect_ray(query)
	_tracer(from, hit.position if not hit.is_empty() else from + direction * data.sight_range)
	if not hit.is_empty() and Target.is_target(hit.collider) and hit.collider.is_in_group("enemies"):
		hit.collider.hit(data.damage, false, hit.position, Color(0.55, 0.9, 0.85))


func _tracer(from: Vector3, to: Vector3) -> void:
	# A quick teal streak so you can see teammates shooting.
	var line := MeshInstance3D.new()
	var box := BoxMesh.new()
	var length := from.distance_to(to)
	box.size = Vector3(0.02, 0.02, length)
	line.mesh = box
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.albedo_color = Color(0.5, 1.0, 0.9, 0.8)
	line.material_override = mat
	line.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	DamageNumbers.world_root(self).add_child(line)
	line.look_at_from_position((from + to) * 0.5, to, Vector3.UP)
	var tween := line.create_tween()
	tween.tween_property(mat, "albedo_color:a", 0.0, 0.08)
	tween.tween_callback(line.queue_free)


func _process(_delta: float) -> void:
	if dead:
		_label.text = "DOWN %d" % ceili(_down_left)
		_label.modulate = Color(1, 0.4, 0.35)
		return
	_label.text = "S %d  H %d" % [ceili(shield), ceili(health)]
	_label.modulate = Color(0.55, 0.8, 1.0) if shield > 0.0 else Color(1, 0.9, 0.9)
	var speed := Vector2(velocity.x, velocity.z).length()
	model.play("Jog_Fwd" if speed > 0.5 else "Idle", clampf(speed / 4.5, 0.6, 1.6))
	# The gun sits in the right hand, pointing where it faces.
	var hand := model.bone_transform("hand_r")
	_gun.global_position = hand.origin
	_gun.global_rotation = Vector3(0, rotation.y, 0)
