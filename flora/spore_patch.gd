class_name SporePatch
extends Node3D
# Spore Patches: a little gray-box mushroom that sprouts where a grenade touches down and
# puffs out a small spore fog while it lasts, then shrinks away.

const GROUP := "spore_patches"
const STEM_COLOR := Color(0.85, 0.82, 0.7)
const CAP_COLOR := Color(0.62, 0.45, 0.85)

var radius := 2.0
var duration := 6.0

var _age := 0.0


func _ready() -> void:
	add_to_group(GROUP) # keeps the Spore Patches HUD box lit
	var stem := MeshInstance3D.new()
	var cyl := CylinderMesh.new()
	cyl.top_radius = 0.05
	cyl.bottom_radius = 0.07
	cyl.height = 0.25
	stem.mesh = cyl
	stem.position.y = 0.125
	stem.material_override = _mat(STEM_COLOR, 0.0)
	add_child(stem)
	var cap := MeshInstance3D.new()
	var dome := SphereMesh.new()
	dome.radius = 0.16
	dome.height = 0.16
	dome.is_hemisphere = true
	cap.mesh = dome
	cap.position.y = 0.25
	cap.material_override = _mat(CAP_COLOR, 1.5)
	add_child(cap)
	scale = Vector3.ONE * 0.01
	create_tween().tween_property(self, "scale", Vector3.ONE, 0.25).set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)
	_puff.call_deferred() # after whoever spawned it has moved it into place


func _puff() -> void:
	SporeFog.spawn(self, global_position + Vector3.UP * 0.4, radius, duration)


func _mat(color: Color, glow: float) -> StandardMaterial3D:
	var mat := StandardMaterial3D.new()
	mat.albedo_color = color
	if glow > 0.0:
		mat.emission_enabled = true
		mat.emission = color
		mat.emission_energy_multiplier = glow
	return mat


func _process(delta: float) -> void:
	_age += delta
	if _age >= duration:
		var shrink := create_tween()
		shrink.tween_property(self, "scale", Vector3.ONE * 0.01, 0.3)
		shrink.tween_callback(queue_free)
		set_process(false)
