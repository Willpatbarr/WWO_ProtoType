class_name SporePool
extends Node3D
# The Lingering Pool grenade's puddle: a flat glowing disc that damages every target
# standing in it for a few seconds, then shrinks away.

const TICK := 0.5

var radius := 3.0
var dps := 25.0
var duration := 5.0
var color := Color(0.55, 0.85, 0.35)
var gun: Gun # for hit markers

var _age := 0.0
var _tick := 0.0
var _disc: MeshInstance3D
var _mat := StandardMaterial3D.new()


func _ready() -> void:
	_disc = MeshInstance3D.new()
	var cylinder := CylinderMesh.new()
	cylinder.top_radius = 1.0
	cylinder.bottom_radius = 1.0
	cylinder.height = 0.06
	_disc.mesh = cylinder
	_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	_mat.albedo_color = Color(color, 0.5)
	_mat.emission_enabled = true
	_mat.emission = color
	_disc.material_override = _mat
	_disc.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_disc.scale = Vector3(radius, 1.0, radius)
	add_child(_disc)


func _physics_process(delta: float) -> void:
	_age += delta
	if _age >= duration:
		queue_free()
		return
	var fade := clampf((duration - _age) / 0.5, 0.0, 1.0)
	_disc.scale = Vector3(radius * fade, 1.0, radius * fade)
	_mat.albedo_color = Color(color, 0.5 * fade)
	_tick -= delta
	if _tick > 0.0:
		return
	_tick = TICK
	for target in get_tree().get_nodes_in_group("targets"):
		var feet: Vector3 = (target as Node3D).global_position
		var flat := Vector2(feet.x - global_position.x, feet.z - global_position.z)
		if flat.length() <= radius and absf(feet.y - global_position.y) < 1.5:
			var killed: bool = target.hit(dps * TICK, false, feet + Vector3.UP * 0.3, color)
			if gun:
				gun.hit_confirmed.emit(false, killed)
