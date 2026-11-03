class_name EntrenchRoots
extends Node3D
# The IronBark ult's look: a ring of iron-bark roots that burst up around your feet and
# stay while you're entrenched, then sink away.

const COLOR := Color(0.42, 0.3, 0.19)

var duration := 10.0


func _ready() -> void:
	var mat := StandardMaterial3D.new()
	mat.albedo_color = COLOR
	mat.roughness = 0.9
	for i in 9:
		var root := MeshInstance3D.new()
		var cone := CylinderMesh.new()
		cone.top_radius = 0.02
		cone.bottom_radius = 0.12
		cone.height = randf_range(0.9, 1.4)
		cone.radial_segments = 6
		root.mesh = cone
		root.material_override = mat
		var angle := TAU * i / 9.0 + randf_range(-0.15, 0.15)
		var out := Vector3(cos(angle), 0.0, sin(angle))
		root.position = out * 0.75 + Vector3.UP * cone.height * 0.4
		root.rotation = Vector3(out.z * 0.45, 0.0, -out.x * 0.45) # leaning outward
		add_child(root)
	scale = Vector3(1.0, 0.01, 1.0)
	create_tween().tween_property(self, "scale", Vector3.ONE, 0.2).set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)
	get_tree().create_timer(duration, false).timeout.connect(func() -> void:
		var sink := create_tween()
		sink.tween_property(self, "scale", Vector3(1.0, 0.01, 1.0), 0.3)
		sink.tween_callback(queue_free))
