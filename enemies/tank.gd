class_name Tank
extends Enemy
# Enemy: Tank (see the "Enemy: Tank" doc in LaminarFlow). Big, slow and armored.
# - A 1.55x mannequin with armor plates on the head, chest and shoulders.
# - Energy shield (a see-through bubble) that takes damage before health and refills
#   after a few seconds without being hit. No crits while it's up.
# - Weak spot: a glowing plate on its back (the only crit spot). Its head is armored.
# - Walks at you slowly and never retreats; turns slowly so you can circle it.
# - Far away: lobs explosive shots in an arc. Close up: raises its arms, then slams the
#   ground, which hurts and slows you. It stands still for a moment after a slam,
#   which is the window to get behind it.
# Tuning: data/enemies/tank.tres.

@export var data: TankData

const ARMOR_COLOR := Color(0.18, 0.16, 0.14)
const WEAK_COLOR := Color(1.0, 0.45, 0.1)
const SHIELD_COLOR := Color(0.35, 0.75, 1.0)
const SHELL_COLOR := Color(1.0, 0.5, 0.15)
const WALK_ANIM_SPEED := 1.6 # m/s the Walk animation looks right at, at full size

var shield := 0.0
var max_shield := 0.0

var _since_hit := 99.0
var _lob_timer := 1.5
var _slam_cooldown := 0.0
var _slam_time := -1.0 # seconds into a slam (windup + recover), or -1
var _slam_landed := false
var _lobbing := 0.0 # seconds of the throw pose left
var _bubble: MeshInstance3D
var _bubble_mat := StandardMaterial3D.new()
var _bubble_flash := 0.0

@onready var weak_shape: CollisionShape3D = $WeakSpot


func _data() -> EnemyData:
	return data


func _setup() -> void:
	var back := global_basis.z # the body's front is -Z
	var chest := model.bone_transform("spine_03").origin
	var s := model_scale
	# The weak spot: a glowing power pack sticking out of the back, between the shoulder
	# blades. It sits outside the body hitbox so shots from behind reach it.
	var weak_center := chest + back * 0.27 * s
	crit_shape = weak_shape
	pin_shape(weak_shape, "spine_03", weak_center)
	model.attach_box("spine_03", weak_center, Vector3(0.22, 0.2, 0.12) * s, WEAK_COLOR, 3.0)
	model.attach_box("spine_03", chest + back * 0.18 * s, Vector3(0.28, 0.26, 0.06) * s, ARMOR_COLOR) # its mount
	# Armor plates.
	var head := model.bone_transform("Head").origin
	model.attach_box("Head", head + Vector3.UP * 0.1 * s, Vector3(0.24, 0.12, 0.27) * s, ARMOR_COLOR)
	model.attach_box("spine_03", chest - back * 0.14 * s, Vector3(0.36, 0.3, 0.07) * s, ARMOR_COLOR)
	for side in ["l", "r"]:
		var shoulder := model.bone_transform("upperarm_" + side).origin
		model.attach_box("upperarm_" + side, shoulder + Vector3.UP * 0.01 * s, Vector3(0.16, 0.07, 0.19) * s, ARMOR_COLOR)
	_build_bubble()
	_reset_shield()


func _crit_radius() -> float:
	return 0.18


func _body_height() -> float:
	return 2.8


func _body_radius() -> float:
	return 0.45


func _build_bubble() -> void:
	_bubble = MeshInstance3D.new()
	var sphere := SphereMesh.new()
	sphere.radius = 1.0
	sphere.height = 2.0
	_bubble.mesh = sphere
	_bubble.scale = Vector3(0.75, 1.15, 0.75) * model_scale
	_bubble.position = Vector3.UP * 1.4
	_bubble_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_bubble_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	_bubble_mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	_bubble_mat.albedo_color = Color(SHIELD_COLOR, 0.06)
	_bubble.material_override = _bubble_mat
	_bubble.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_bubble)


func _reset_shield() -> void:
	max_shield = data.shield
	shield = max_shield
	_since_hit = 99.0


# --- Enemy hooks --------------------------------------------------------------------

func _absorb(damage: float, _critical: bool, point: Vector3) -> float:
	_since_hit = 0.0
	if shield <= 0.0:
		return damage
	var taken := minf(damage, shield)
	shield -= taken
	_bubble_flash = 1.0
	DamageNumbers.spawn(self, taken, false, point, SHIELD_COLOR)
	if shield <= 0.0:
		_shield_break()
	return damage - taken


func _shield_break() -> void:
	# A quick bright pop as the bubble shatters.
	var pop := MeshInstance3D.new()
	pop.mesh = _bubble.mesh
	var mat := _bubble_mat.duplicate() as StandardMaterial3D
	mat.albedo_color = Color(SHIELD_COLOR, 0.6)
	pop.material_override = mat
	DamageNumbers.world_root(self).add_child(pop)
	pop.global_transform = _bubble.global_transform
	var tween := pop.create_tween().set_parallel(true)
	tween.tween_property(pop, "scale", pop.scale * 1.3, 0.25)
	tween.tween_property(mat, "albedo_color:a", 0.0, 0.25)
	tween.chain().tween_callback(pop.queue_free)


func is_critical_shape(shape_index: int) -> bool:
	return shield <= 0.0 and super.is_critical_shape(shape_index) # no crits through the shield


func _on_reset() -> void:
	_reset_shield()
	_slam_time = -1.0
	_lobbing = 0.0
	_lob_timer = 1.5


func _on_death() -> void:
	_slam_time = -1.0
	_bubble.visible = false


func _stand_down() -> void:
	_slam_time = -1.0


func _think(delta: float) -> Vector3:
	_update_shield(delta)
	var to_player := flat_to_player()
	var distance := to_player.length()
	var head := model.bone_transform("Head").origin
	if not alerted and distance <= data.sight_range and can_see_player(head):
		alerted = true
	if not alerted:
		return Vector3.ZERO
	_slam_cooldown -= delta
	_lob_timer -= delta
	_lobbing = maxf(_lobbing - delta, 0.0)

	if _slam_time >= 0.0:
		_update_slam(delta)
		return Vector3.ZERO # planted while slamming and recovering

	turn_toward(to_player, delta)
	if distance <= data.slam_range and _slam_cooldown <= 0.0:
		_slam_time = 0.0
		_slam_landed = false
		model.play("Spell_Simple_Enter", 0.6, 0.15) # arms up: the warning
		return Vector3.ZERO
	if distance > data.slam_range and _lob_timer <= 0.0 and can_see_player(head):
		_lob_timer = data.lob_interval
		_lobbing = 0.5
		model.play("Spell_Simple_Shoot", 1.0, 0.1)
		_lob()
	return path_direction(_player.global_position, delta) * data.walk_speed # never retreats


func _update_shield(delta: float) -> void:
	_since_hit += delta
	if max_shield > 0.0 and _since_hit >= data.shield_regen_delay and shield < max_shield:
		shield = minf(shield + data.shield_regen_rate * delta, max_shield)


func _update_slam(delta: float) -> void:
	_slam_time += delta
	if not _slam_landed and _slam_time >= data.slam_windup:
		_slam_landed = true
		_slam()
	if _slam_time >= data.slam_windup + data.slam_recover:
		_slam_time = -1.0
		_slam_cooldown = data.slam_cooldown


func _slam() -> void:
	model.play("Jump_Land", 1.4, 0.05)
	_shockwave()
	# Hits you and any teammates in range (the slam counts as melee).
	var player := get_tree().get_first_node_in_group("player") as Node3D
	var victims: Array = get_tree().get_nodes_in_group("teammates")
	if player:
		victims.append(player)
	for victim in victims:
		var to: Vector3 = (victim as Node3D).global_position - global_position
		to.y = 0.0
		if to.length() > data.slam_radius or absf(to.y) > 2.5:
			continue
		if victim == player:
			var health_node := player_health()
			if health_node:
				health_node.take_damage(data.slam_damage, true)
			player.apply_slow(data.slow_amount, data.slow_time)
		else:
			victim.take_damage(data.slam_damage, true)


func _shockwave() -> void:
	# A flat ring that spreads out to the slam radius and fades.
	var ring := MeshInstance3D.new()
	var torus := TorusMesh.new()
	torus.inner_radius = 0.85
	torus.outer_radius = 1.0
	ring.mesh = torus
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.albedo_color = Color(1.0, 0.7, 0.3, 0.8)
	ring.material_override = mat
	ring.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	DamageNumbers.world_root(self).add_child(ring)
	ring.global_position = global_position + Vector3.UP * 0.1
	ring.scale = Vector3(0.5, 0.3, 0.5)
	var tween := ring.create_tween().set_parallel(true)
	tween.tween_property(ring, "scale", Vector3(data.slam_radius, 0.3, data.slam_radius), 0.3).set_ease(Tween.EASE_OUT)
	tween.tween_property(mat, "albedo_color:a", 0.0, 0.45)
	tween.chain().tween_callback(ring.queue_free)


func _lob() -> void:
	# An explosive shell in an arc that lands where you're standing now.
	var hand := model.bone_transform("hand_r").origin + Vector3.UP * 0.3
	var target := _player.global_position + Vector3.DOWN * 0.9 # at your feet
	var t := maxf(data.lob_flight_time, 0.3)
	var shell := EnemyProjectile.new()
	shell.gravity = GRAVITY
	shell.velocity = (target - hand) / t + Vector3.UP * 0.5 * GRAVITY * t
	shell.damage = data.lob_damage
	shell.splash_radius = data.lob_radius
	shell.color = SHELL_COLOR
	shell.size = 0.2
	shell.life = t + 2.0
	shell.exclude = enemy_rids()
	DamageNumbers.world_root(self).add_child(shell)
	shell.global_position = hand


# --- Visuals ----------------------------------------------------------------------

func _animate(delta: float) -> void:
	_bubble.visible = shield > 0.0
	_bubble_flash = move_toward(_bubble_flash, 0.0, 4.0 * delta)
	_bubble_mat.albedo_color = Color(SHIELD_COLOR, lerpf(0.06, 0.35, _bubble_flash))
	if _slam_time >= 0.0 or _lobbing > 0.0:
		return # the slam and throw poses play out
	var speed := Vector2(velocity.x, velocity.z).length()
	if speed > 0.3:
		model.play("Walk", clampf(speed / (WALK_ANIM_SPEED * model_scale), 0.5, 2.0))
	else:
		model.play("Idle")
