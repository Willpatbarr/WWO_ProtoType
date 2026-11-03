class_name SporeFog
extends Node3D
# A cloud of VeilSpore spore fog: a soft purple sphere that fades in, lasts a while,
# then fades out.
# - With the Spore Fog Damage ability equipped, Swarm and Soldiers inside take damage
#   over time (the Tank, a mini-boss, is immune).
# - `blocks_sight` fogs are thick: enemies can't see you through or inside them.
# - The VeilSpore ult makes you invisible while you're inside any spore fog.

const COLOR := Color(0.62, 0.45, 0.85)
const GROUP := "spore_fog"
const TICK := 0.5 # seconds between damage ticks
const FADE := 0.6 # seconds to fade in and out

var radius := 3.0
var duration := 6.0
var blocks_sight := false
var opacity := 0.22 # how thick it looks at full strength

var _age := 0.0
var _tick := 0.0
var _puffs: Array[MeshInstance3D] = []
var _mat := StandardMaterial3D.new()


static func spawn(from: Node, at: Vector3, fog_radius: float, seconds: float, thick := false) -> SporeFog:
	var fog := SporeFog.new()
	fog.radius = fog_radius
	fog.duration = seconds * (1.0 + Loadout.bonus(from.get_tree(), "fog_longer")) # Longer Fog trait
	fog.blocks_sight = thick
	fog.opacity = 0.32 if thick else 0.22
	DamageNumbers.world_root(from).add_child(fog)
	fog.global_position = at
	return fog


static func contains(tree: SceneTree, point: Vector3) -> bool:
	# Is this point inside any spore fog?
	for fog in tree.get_nodes_in_group(GROUP):
		if (fog as SporeFog).strength() > 0.3 and point.distance_to(fog.global_position) <= fog.radius:
			return true
	return false


static func blocks(tree: SceneTree, from: Vector3, to: Vector3) -> bool:
	# Does a thick fog sit on the line between two points (or around either end)?
	for node in tree.get_nodes_in_group(GROUP):
		var fog := node as SporeFog
		if not fog.blocks_sight or fog.strength() < 0.5:
			continue
		var line := to - from
		var t := clampf((fog.global_position - from).dot(line) / maxf(line.length_squared(), 0.0001), 0.0, 1.0)
		if (from + line * t).distance_to(fog.global_position) <= fog.radius * 0.9:
			return true
	return false


func _ready() -> void:
	add_to_group(GROUP)
	_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	_mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	_mat.albedo_color = Color(COLOR, 0.0)
	# A few overlapping blobs so it reads as a cloud, not a ball.
	var offsets := [Vector3.ZERO, Vector3(0.35, 0.1, 0.2), Vector3(-0.3, 0.05, -0.25), Vector3(0.1, 0.3, -0.3), Vector3(-0.2, 0.25, 0.3)]
	for offset in offsets:
		var puff := MeshInstance3D.new()
		var sphere := SphereMesh.new()
		sphere.radius = 1.0
		sphere.height = 2.0
		sphere.radial_segments = 16
		sphere.rings = 8
		puff.mesh = sphere
		puff.material_override = _mat
		puff.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		puff.position = offset * radius
		puff.scale = Vector3(1.0, 0.7, 1.0) * radius * randf_range(0.7, 0.85)
		add_child(puff)
		_puffs.append(puff)


func strength() -> float:
	# 0..1: fades in at the start and out at the end.
	return clampf(minf(_age / FADE, (duration - _age) / FADE), 0.0, 1.0)


func _process(delta: float) -> void:
	_age += delta
	if _age >= duration:
		queue_free()
		return
	_mat.albedo_color = Color(COLOR, opacity * strength())
	for i in _puffs.size(): # a slow churn
		_puffs[i].rotation.y += delta * (0.2 + 0.1 * i)


func _physics_process(delta: float) -> void:
	_tick -= delta
	if _tick > 0.0:
		return
	_tick = TICK
	var damage_ability := _fog_damage_ability()
	if damage_ability == null or strength() < 0.3:
		return
	for enemy in get_tree().get_nodes_in_group("enemies"):
		if not (enemy is Swarm or enemy is Soldier) or enemy.dead: # rank and file only
			continue
		var chest: Vector3 = enemy.global_position + Vector3.UP * 0.8
		if chest.distance_to(global_position) <= radius:
			var dps: float = damage_ability.fog_dps * (1.0 + Loadout.bonus(get_tree(), "poison_damage")) # Stronger Poison
			enemy.hit(dps * TICK, false, chest, COLOR.lightened(0.3))


func _fog_damage_ability() -> AbilityData:
	var abilities := get_tree().get_first_node_in_group("ability_manager")
	return abilities.equipped("spore_fog_damage") if abilities else null
