class_name Soldier
extends Enemy
# Enemy: Soldier. A mid-range gunner (see the "Enemy: Soldier" doc in LaminarFlow).
# - Notices you inside sight range (or when shot), then turns to face you.
# - Keeps 15–25 m away: walks in if you're far, backs off if you're close, and
#   strafes side to side in between.
# - Fires bursts of slow projectiles. Each burst has a windup glow on its gun first.
#   Soldiers space their bursts out so a group doesn't all fire at once.
# - Heads for the nearest cover (nodes in the "cover" group) when its health is low.
# - Head is the crit spot; its hitbox follows the head bone through animations.
# Health, hits, death and respawn come from Enemy. Tuning: data/enemies/soldier.tres.

@export var data: SoldierData

const GUN_COLOR := Color(0.12, 0.12, 0.13)
const GLOW_COLOR := Color(1.0, 0.35, 0.1)
const WALK_ANIM_SPEED := 1.6 # m/s the Walk animation looks right at
const LOWER_BODY := ["root", "pelvis", "thigh", "calf", "foot", "ball"] # bone name starts

static var _next_burst_time := 0.0 # shared by every soldier: spaces out their bursts

var _strafe_dir := 1.0
var _strafe_left := 0.0
var _cooldown := 1.0
var _windup := -1.0 # seconds into the windup, or -1 when not winding up
var _shots_left := 0
var _shot_timer := 0.0
var _glow := 0.0
var _aim_blend := 0.0
var _cover: Node3D = null
var _cover_check := 0.0
var _sees := false

var _tree: AnimationTree
var _hit_anim: AnimationNodeAnimation
var _glow_mat := StandardMaterial3D.new()
var _gun: Node3D
var _muzzle: Marker3D
var _muzzle_light: OmniLight3D

@onready var head_shape: CollisionShape3D = $Head


func _data() -> EnemyData:
	return data


func _setup() -> void:
	crit_shape = head_shape
	pin_shape(head_shape, "Head", model.bone_transform("Head") * Vector3(0.0, 0.12, 0.02))
	_build_gun()
	_build_animation_tree()
	_cooldown = randf_range(0.5, data.burst_cooldown)
	_strafe_dir = 1.0 if randf() < 0.5 else -1.0


func _crit_radius() -> float:
	return 0.14


func _body_height() -> float:
	return 1.9


# --- Setup ------------------------------------------------------------------------

func _build_gun() -> void:
	# A gray-box pistol held in the right hand. Its muzzle glows during the windup.
	_gun = Node3D.new()
	add_child(_gun)
	var gun_mat := StandardMaterial3D.new()
	gun_mat.albedo_color = GUN_COLOR
	_add_box(_gun, Vector3(0.05, 0.08, 0.26), Vector3(0.0, 0.0, -0.09), gun_mat) # barrel/body
	_add_box(_gun, Vector3(0.045, 0.12, 0.06), Vector3(0.0, -0.08, 0.0), gun_mat) # grip
	_glow_mat.albedo_color = GLOW_COLOR
	_glow_mat.emission_enabled = true
	_glow_mat.emission = GLOW_COLOR
	_add_box(_gun, Vector3(0.06, 0.06, 0.03), Vector3(0.0, 0.0, -0.23), _glow_mat) # muzzle
	_muzzle = Marker3D.new()
	_muzzle.position = Vector3(0.0, 0.0, -0.26)
	_gun.add_child(_muzzle)
	_muzzle_light = OmniLight3D.new()
	_muzzle_light.light_color = GLOW_COLOR
	_muzzle_light.omni_range = 2.0
	_muzzle_light.light_energy = 0.0
	_muzzle.add_child(_muzzle_light)


func _add_box(parent: Node3D, size: Vector3, pos: Vector3, mat: Material) -> void:
	var box := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	box.mesh = mesh
	box.material_override = mat
	box.position = pos
	parent.add_child(box)


func _build_animation_tree() -> void:
	# Legs: idle or walk, by speed. Upper body: blends to the pistol aim when in combat.
	# Hits play a quick flinch on the upper body on top of everything.
	var anim := model.anim
	var skeleton := model.skeleton
	anim.get_animation("Pistol_Aim_Neutral").loop_mode = Animation.LOOP_LINEAR
	var blend := AnimationNodeBlendTree.new()
	var idle := AnimationNodeAnimation.new()
	idle.animation = "Idle"
	var walk := AnimationNodeAnimation.new()
	walk.animation = "Walk"
	var aim := AnimationNodeAnimation.new()
	aim.animation = "Pistol_Aim_Neutral"
	_hit_anim = AnimationNodeAnimation.new()
	_hit_anim.animation = "Hit_Chest"
	var upper := AnimationNodeBlend2.new()
	var hit_shot := AnimationNodeOneShot.new()
	hit_shot.fadein_time = 0.05
	hit_shot.fadeout_time = 0.2
	# Only the upper body aims and flinches; the legs keep walking.
	var glb_root := anim.get_parent()
	var skeleton_path := str(glb_root.get_path_to(skeleton))
	for upper_node in [upper, hit_shot]:
		upper_node.filter_enabled = true
		for i in skeleton.get_bone_count():
			var bone := skeleton.get_bone_name(i)
			if not LOWER_BODY.any(func(start: String) -> bool: return bone.begins_with(start)):
				upper_node.set_filter_path(NodePath(skeleton_path + ":" + bone), true)
	blend.add_node("idle", idle)
	blend.add_node("walk", walk)
	blend.add_node("walk_speed", AnimationNodeTimeScale.new())
	blend.add_node("move", AnimationNodeBlend2.new())
	blend.add_node("aim", aim)
	blend.add_node("upper", upper)
	blend.add_node("hit_anim", _hit_anim)
	blend.add_node("hit", hit_shot)
	blend.connect_node("walk_speed", 0, "walk")
	blend.connect_node("move", 0, "idle")
	blend.connect_node("move", 1, "walk_speed")
	blend.connect_node("upper", 0, "move")
	blend.connect_node("upper", 1, "aim")
	blend.connect_node("hit", 0, "upper")
	blend.connect_node("hit", 1, "hit_anim")
	blend.connect_node("output", 0, "hit")
	_tree = AnimationTree.new()
	glb_root.add_child(_tree)
	_tree.root_node = NodePath("..")
	_tree.anim_player = _tree.get_path_to(anim)
	_tree.tree_root = blend
	_tree.active = true
	anim.active = false # the tree drives the skeleton; the player only plays the death


# --- Enemy hooks --------------------------------------------------------------------

func _on_hit(critical: bool) -> void:
	_hit_anim.animation = "Hit_Head" if critical else "Hit_Chest"
	_tree.set("parameters/hit/request", AnimationNodeOneShot.ONE_SHOT_REQUEST_FIRE)


func _on_reset() -> void:
	_cancel_attack()
	_cooldown = randf_range(0.5, data.burst_cooldown)
	_cover = null
	model.anim.active = false
	_tree.active = true


func _on_death() -> void:
	_cancel_attack()
	_tree.active = false # the death animation plays on its own
	model.anim.active = true


func _stand_down() -> void:
	_cancel_attack()


func _think(delta: float) -> Vector3:
	var to_player := flat_to_player()
	var distance := to_player.length()
	_sees = can_see_player(crit_shape.global_position)
	if not alerted and _sees and distance <= data.sight_range:
		alerted = true
	if not alerted:
		return Vector3.ZERO
	turn_toward(to_player, delta)
	_update_attack(delta, _sees)
	return _choose_move(to_player, distance, delta) * data.walk_speed


func _after_move(_delta: float) -> void:
	if is_on_wall():
		_strafe_dir = -_strafe_dir # bumped into something: strafe the other way


func _choose_move(to_player: Vector3, distance: float, delta: float) -> Vector3:
	var toward := to_player / maxf(distance, 0.001)
	# Low on health: get behind cover if there's any nearby.
	if health <= max_health * data.cover_health:
		_cover_check -= delta
		if _cover_check <= 0.0:
			_cover_check = 0.5
			_cover = _nearest_cover()
		if _cover:
			var to_spot := _cover_spot(_cover) - global_position
			to_spot.y = 0.0
			return path_direction(_cover_spot(_cover), delta) if to_spot.length() > 0.4 else Vector3.ZERO
	if distance < data.min_range:
		return -toward
	if distance > data.max_range or not _sees:
		return path_direction(_player.global_position, delta) # walk around walls to get a shot
	_strafe_left -= delta
	if _strafe_left <= 0.0:
		_strafe_left = data.strafe_time * randf_range(0.6, 1.4)
		_strafe_dir = -_strafe_dir
	return toward.cross(Vector3.UP) * _strafe_dir


func _nearest_cover() -> Node3D:
	var best: Node3D = null
	var best_distance := 25.0 # ignore cover farther than this
	for node in get_tree().get_nodes_in_group("cover"):
		var cover := node as Node3D
		var distance := global_position.distance_to(cover.global_position)
		if distance < best_distance:
			best = cover
			best_distance = distance
	return best


func _cover_spot(cover: Node3D) -> Vector3:
	# Just behind the cover, on the side away from the player.
	var away := cover.global_position - _player.global_position
	away.y = 0.0
	var half_width := 0.75
	if cover is CSGBox3D:
		var size := (cover as CSGBox3D).size
		half_width = maxf(size.x, size.z) * 0.5
	var spot := cover.global_position + away.normalized() * (half_width + 0.7)
	spot.y = global_position.y
	return spot


# --- Shooting ---------------------------------------------------------------------

func _update_attack(delta: float, sees: bool) -> void:
	if _shots_left > 0: # mid-burst
		_shot_timer -= delta
		if _shot_timer <= 0.0:
			_fire()
			_shots_left -= 1
			_shot_timer = data.burst_interval
			if _shots_left == 0:
				_cooldown = data.burst_cooldown * randf_range(0.85, 1.15)
		return
	if _windup >= 0.0:
		_windup += delta
		if _windup >= data.windup_time:
			_windup = -1.0
			_shots_left = maxi(roundi(data.burst_count), 1)
			_shot_timer = 0.0
		return
	_cooldown -= delta
	if _cooldown > 0.0 or not sees:
		return
	var now := Time.get_ticks_msec() / 1000.0
	if now < Soldier._next_burst_time:
		return # another soldier just started a burst; wait a moment
	Soldier._next_burst_time = now + data.group_spacing
	_windup = 0.0


func _cancel_attack() -> void:
	_windup = -1.0
	_shots_left = 0


func is_attacking() -> bool:
	return _windup >= 0.0 or _shots_left > 0


func _fire() -> void:
	var from := _muzzle.global_position
	var direction := (player_aim_point() - from).normalized() # no leading: strafing dodges
	# Random aim error inside a small cone.
	var side := direction.cross(Vector3.UP).normalized()
	var up := side.cross(direction)
	var radius := tan(deg_to_rad(data.spread)) * sqrt(randf())
	var turn := randf() * TAU
	direction = (direction + side * cos(turn) * radius + up * sin(turn) * radius).normalized()
	var shot := EnemyProjectile.new()
	shot.velocity = direction * data.projectile_speed
	shot.damage = data.projectile_damage
	shot.exclude = enemy_rids()
	DamageNumbers.world_root(self).add_child(shot)
	shot.global_position = from


# --- Visuals ----------------------------------------------------------------------

func _animate(delta: float) -> void:
	# Windup tell: the muzzle glow ramps up, then stays bright through the burst.
	var glow_target := 0.0
	if _windup >= 0.0:
		glow_target = clampf(_windup / maxf(data.windup_time, 0.01), 0.0, 1.0)
	elif _shots_left > 0:
		glow_target = 1.0
	_glow = move_toward(_glow, glow_target, 8.0 * delta) if glow_target < _glow else glow_target
	_glow_mat.emission_energy_multiplier = lerpf(0.4, 8.0, _glow)
	_muzzle_light.light_energy = _glow * 2.5
	# Animation: aim when in combat, walk by speed.
	_aim_blend = move_toward(_aim_blend, 1.0 if alerted else 0.0, 4.0 * delta)
	var speed := Vector2(velocity.x, velocity.z).length()
	_tree.set("parameters/upper/blend_amount", _aim_blend)
	_tree.set("parameters/move/blend_amount", clampf(speed / 1.0, 0.0, 1.0))
	_tree.set("parameters/walk_speed/scale", maxf(speed / WALK_ANIM_SPEED, 0.5))
	_place_gun()


func _place_gun() -> void:
	# The gun sits in the right hand. In combat it points at you; otherwise it points
	# where the soldier faces, tipped down.
	var hand := model.bone_transform("hand_r")
	_gun.global_position = hand.origin
	var forward := -global_basis.z
	var aim_point := hand.origin + forward + Vector3.DOWN * 0.6
	if alerted and _player:
		aim_point = aim_point.lerp(player_aim_point(), _aim_blend)
	if not hand.origin.is_equal_approx(aim_point):
		_gun.look_at(aim_point, Vector3.UP)
