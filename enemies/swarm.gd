class_name Swarm
extends Enemy
# Enemy: Swarm (see the "Enemy: Swarm" doc in LaminarFlow). Small, fast melee enemies
# that rush you in packs.
# - A 0.95x-size mannequin (nearly Soldier size; the hunch makes it read shorter) (HunchModifier bends the spine).
# - Runs straight at you once it notices you, keeping a little space from other swarm
#   so a pack spreads out instead of stacking.
# - Leaps at you from a few meters away (hurts if it connects), then bites while it's
#   touching you.
# - Head is the crit spot (small and low).
# Tuning: data/enemies/swarm.tres.

@export var data: SwarmData

const RUN_ANIM_SPEED := 6.5 # m/s the Sprint animation looks right at, at full size

var _leap_cooldown := 0.0
var _leaping := false
var _leap_hit := false
var _bite_timer := 0.0

@onready var head_shape: CollisionShape3D = $Head


func _data() -> EnemyData:
	return data


func _setup() -> void:
	crit_shape = head_shape
	pin_shape(head_shape, "Head", model.bone_transform("Head") * Vector3(0.0, 0.12, 0.02))
	var hunch := HunchModifier.new()
	model.skeleton.add_child(hunch)
	# The bend only exists right after the modifier runs, so the head hitbox follows then.
	follow_in_physics = false
	hunch.modification_processed.connect(_follow_bones)
	_leap_cooldown = randf_range(0.0, data.leap_cooldown)


func _crit_radius() -> float:
	return 0.15


func _body_height() -> float:
	return 1.4


func _body_radius() -> float:
	return 0.36


func _on_reset() -> void:
	_leaping = false
	_bite_timer = 0.0


func _think(delta: float) -> Vector3:
	var to_player := flat_to_player()
	var distance := to_player.length()
	if not alerted and distance <= data.sight_range and can_see_player(crit_shape.global_position):
		alerted = true
	if not alerted:
		return Vector3.ZERO
	_leap_cooldown -= delta
	_bite_timer -= delta
	turn_toward(to_player, delta)
	var toward := to_player / maxf(distance, 0.001)

	if _leaping:
		# In the air: hurts once if it reaches you, ends when it lands.
		if not _leap_hit and global_position.distance_to(_player.global_position) < 1.2:
			_leap_hit = true
			_hurt_player(data.leap_damage)
		return Vector3.ZERO

	if distance <= data.bite_range:
		if _bite_timer <= 0.0:
			_bite_timer = data.bite_interval
			_hurt_player(data.bite_damage)
			model.play("Punch_Jab", 1.6, 0.05)
		return Vector3.ZERO # stand and bite

	if distance <= data.leap_range and _leap_cooldown <= 0.0 and is_on_floor():
		_leap(toward)
		return Vector3.ZERO

	return (path_direction(_player.global_position, delta) + _spread_push()).normalized() * data.walk_speed


func _leap(toward: Vector3) -> void:
	_leaping = true
	_leap_hit = false
	_leap_cooldown = data.leap_cooldown
	steering = false # keep the launch velocity until it lands
	velocity = toward * data.leap_speed + Vector3.UP * sqrt(2.0 * GRAVITY * data.leap_height)
	model.play("Jump", 1.0, 0.05)


func _after_move(_delta: float) -> void:
	if _leaping and is_on_floor() and velocity.y <= 0.0:
		_leaping = false
		steering = true


func _spread_push() -> Vector3:
	# A nudge away from nearby swarm, so the pack fans out.
	var push := Vector3.ZERO
	for other in get_tree().get_nodes_in_group("swarm"):
		if other == self or other.dead:
			continue
		var away: Vector3 = global_position - other.global_position
		away.y = 0.0
		var d := away.length()
		if d > 0.01 and d < data.spread_distance:
			push += away / d * (1.0 - d / data.spread_distance)
	return push * 1.5


func _hurt_player(amount: float) -> void:
	damage_target(amount, true) # bites and leaps are melee


func _ready() -> void:
	super._ready()
	add_to_group("swarm")


func _animate(_delta: float) -> void:
	if _leaping:
		return
	if model.anim.current_animation == "Punch_Jab" and model.anim.is_playing():
		return # let the bite finish
	var speed := Vector2(velocity.x, velocity.z).length()
	if speed > 0.5:
		model.play("Sprint", clampf(speed / (RUN_ANIM_SPEED * model_scale), 0.6, 2.5))
	else:
		model.play("Idle")
