class_name Target
extends StaticBody3D
# A shootable dummy. The "Head" collision shape is the critical spot.
# Flashes when hit and pops up a damage number (white, or yellow for crits).
# Can strafe side to side to test tracking and aim assist.
# Has health: at zero it shrinks away and stops blocking shots until revived
# (by the TargetManager's reset button or its auto-respawn timer).

@export var strafe_distance := 0.0 # meters of side-to-side travel; 0 = stands still
@export var strafe_speed := 3.0 # top speed in m/s

const BODY_COLOR := Color(0.2, 0.85, 0.95)
const HEAD_COLOR := Color(1.0, 0.55, 0.1)

var _body_mat := StandardMaterial3D.new()
var _head_mat := StandardMaterial3D.new()
var _body_flash := 0.0
var _head_flash := 0.0
var _start := Vector3.ZERO
var _time := 0.0
var max_health := 200.0
var health := 200.0
var dead := false
var manager: Node = null # the TargetManager, set when it starts up
var _death_tween: Tween

@onready var head_shape: CollisionShape3D = $Head


static func is_target(node: Object) -> bool:
	# Anything shootable: target dummies and enemies (they share hit/aim_parts/is_critical_shape).
	return node is Node and (node as Node).is_in_group("all_targets")


func _ready() -> void:
	add_to_group("targets") # living targets: aim assist, melee and lunges look here
	add_to_group("all_targets") # dead or alive, for resets
	_start = position
	_body_mat.albedo_color = BODY_COLOR
	_head_mat.albedo_color = HEAD_COLOR
	($BodyMesh as MeshInstance3D).material_override = _body_mat
	($HeadMesh as MeshInstance3D).material_override = _head_mat


func _physics_process(delta: float) -> void:
	if strafe_distance <= 0.0 or dead:
		return
	var half := strafe_distance * 0.5
	_time += delta
	position = _start + basis.x * sin(_time * strafe_speed / half) * half


func aim_parts() -> Array:
	# Aim assist targets: center and rough radius of each part. Head first.
	return [
		{"position": head_shape.global_position, "radius": 0.17, "critical": true},
		{"position": ($Body as CollisionShape3D).global_position, "radius": 0.4, "critical": false},
	]


func is_critical_shape(shape_index: int) -> bool:
	return shape_owner_get_owner(shape_find_owner(shape_index)) == head_shape


func hit(damage: float, critical: bool, point: Vector3, color := Color(0, 0, 0, 0)) -> bool:
	# Returns true if this hit killed the target.
	if dead:
		return false
	if critical:
		_head_flash = 1.0
	else:
		_body_flash = 1.0
	DamageNumbers.spawn(self, damage, critical, point, color)
	health -= damage
	if health <= 0.0:
		_die()
		return true
	return false


func _die() -> void:
	dead = true
	health = 0.0
	remove_from_group("targets")
	_set_collision(false)
	# Shrink away, then hide.
	_death_tween = create_tween().set_parallel(true)
	for mesh in [$BodyMesh, $HeadMesh]:
		_death_tween.tween_property(mesh, "scale", Vector3.ONE * 0.01, 0.25).set_ease(Tween.EASE_IN).set_trans(Tween.TRANS_BACK)
	_death_tween.chain().tween_callback(func() -> void: visible = false)
	if manager and manager.respawn_time > 0.0:
		get_tree().create_timer(manager.respawn_time).timeout.connect(func() -> void:
			if dead:
				revive(max_health))


func revive(new_max_health: float) -> void:
	if _death_tween:
		_death_tween.kill()
	max_health = new_max_health
	health = max_health
	if dead:
		dead = false
		add_to_group("targets")
		_set_collision(true)
	visible = true
	for mesh in [$BodyMesh, $HeadMesh]:
		mesh.scale = Vector3.ONE


func _set_collision(on: bool) -> void:
	# Deferred because this can happen in the middle of a physics query.
	$Body.set_deferred("disabled", not on)
	$Head.set_deferred("disabled", not on)


func _process(delta: float) -> void:
	_body_flash = move_toward(_body_flash, 0.0, 6.0 * delta)
	_head_flash = move_toward(_head_flash, 0.0, 6.0 * delta)
	_body_mat.albedo_color = BODY_COLOR.lerp(Color.WHITE, _body_flash)
	_head_mat.albedo_color = HEAD_COLOR.lerp(Color.WHITE, _head_flash)
