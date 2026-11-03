extends SceneTree
# Builds the low-poly weapon models into res://weapons/models/*.tscn.
# Run from the project folder:  godot --headless -s tools/build_weapon_models.gd
# Each model's origin is its sight point, so aiming down sights centers it.
# Required node names: Magazine (moves during reload), MuzzleFlash (hidden; shown when firing).
# Optional: SightDot and SightRing (glow; turn red over a target).
# Parts: [name, "box", "cyl", "ring" or "sphere", size (box) or Vector2(radius, length) (cyl, along -z)
#         or Vector2(inner, outer radius) (ring, faces up; rotate 90 on x to face you),
#         position, rotation in degrees, color, glow]

const MATERIAL := preload("res://viewmodel_material.tres")

const DARK := Color(0.12, 0.12, 0.13)
const STEEL := Color(0.22, 0.24, 0.28)
const ORANGE := Color(0.95, 0.5, 0.1)
const WOOD := Color(0.38, 0.22, 0.12)
const BRONZE := Color(0.48, 0.34, 0.2)
const GOLD := Color(0.78, 0.6, 0.25)
const OLIVE := Color(0.22, 0.27, 0.2)
const FLASH := Color(1, 0.75, 0.3)

const MODELS := {
	"rifle": [
		["Receiver", "box", Vector3(0.05, 0.08, 0.28), Vector3(0, -0.07, 0.02), Vector3.ZERO, STEEL, 0.0],
		["Stripe", "box", Vector3(0.052, 0.015, 0.2), Vector3(0, -0.065, 0.02), Vector3.ZERO, ORANGE, 0.0],
		["Barrel", "cyl", Vector2(0.015, 0.26), Vector3(0, -0.06, -0.25), Vector3.ZERO, DARK, 0.0],
		["Grip", "box", Vector3(0.045, 0.12, 0.06), Vector3(0, -0.16, 0.11), Vector3(-15, 0, 0), DARK, 0.0],
		["Magazine", "box", Vector3(0.04, 0.13, 0.06), Vector3(0, -0.165, -0.02), Vector3.ZERO, ORANGE, 0.0],
		["Stock", "box", Vector3(0.042, 0.07, 0.15), Vector3(0, -0.08, 0.23), Vector3.ZERO, STEEL, 0.0],
		["SightBase", "box", Vector3(0.034, 0.01, 0.05), Vector3(0, -0.0225, 0), Vector3.ZERO, DARK, 0.0],
		["SightLeft", "box", Vector3(0.005, 0.036, 0.008), Vector3(-0.0175, -0.002, 0), Vector3.ZERO, DARK, 0.0],
		["SightRight", "box", Vector3(0.005, 0.036, 0.008), Vector3(0.0175, -0.002, 0), Vector3.ZERO, DARK, 0.0],
		["SightTop", "box", Vector3(0.04, 0.005, 0.008), Vector3(0, 0.0175, 0), Vector3.ZERO, DARK, 0.0],
		["SightDot", "sphere", Vector3(0.0025, 0, 0), Vector3.ZERO, Vector3.ZERO, Color(0.95, 0.95, 0.95), 4.0],
		["MuzzleFlash", "sphere", Vector3(0.03, 0, 0), Vector3(0, -0.06, -0.41), Vector3.ZERO, FLASH, 6.0],
	],
	"handcannon": [ # revolver with a floating holo reticle above the barrel (no iron sights)
		# Everything sits 1 cm lower than the sight line so the reticle floats clear of the rib.
		["Frame", "box", Vector3(0.045, 0.07, 0.16), Vector3(0, -0.065, 0.02), Vector3.ZERO, BRONZE, 0.0],
		["Barrel", "box", Vector3(0.036, 0.042, 0.18), Vector3(0, -0.046, -0.14), Vector3.ZERO, DARK, 0.0],
		["Rib", "box", Vector3(0.02, 0.008, 0.3), Vector3(0, -0.023, -0.07), Vector3.ZERO, DARK, 0.0],
		["Magazine", "cyl", Vector2(0.032, 0.075), Vector3(0, -0.066, -0.005), Vector3.ZERO, GOLD, 0.0], # the cylinder
		["Grip", "box", Vector3(0.036, 0.12, 0.05), Vector3(0, -0.14, 0.09), Vector3(-20, 0, 0), WOOD, 0.0],
		["Hammer", "box", Vector3(0.012, 0.026, 0.02), Vector3(0, -0.026, 0.1), Vector3.ZERO, DARK, 0.0],
		["SightRing", "ring", Vector2(0.0085, 0.0098), Vector3(0, 0, -0.12), Vector3(90, 0, 0), Color(0.95, 0.95, 0.95), 4.0],
		["SightDot", "sphere", Vector3(0.0016, 0, 0), Vector3(0, 0, -0.12), Vector3.ZERO, Color(0.95, 0.95, 0.95), 4.0],
		["MuzzleFlash", "sphere", Vector3(0.04, 0, 0), Vector3(0, -0.046, -0.26), Vector3.ZERO, FLASH, 6.0],
	],
	"shotgun": [ # pump action with a bead sight at the muzzle
		["Receiver", "box", Vector3(0.05, 0.075, 0.22), Vector3(0, -0.058, 0.08), Vector3.ZERO, OLIVE, 0.0],
		["Barrel", "cyl", Vector2(0.018, 0.46), Vector3(0, -0.032, -0.25), Vector3.ZERO, DARK, 0.0],
		["Tube", "cyl", Vector2(0.013, 0.38), Vector3(0, -0.068, -0.2), Vector3.ZERO, DARK, 0.0],
		["Magazine", "box", Vector3(0.046, 0.046, 0.13), Vector3(0, -0.07, -0.17), Vector3.ZERO, WOOD, 0.0], # the pump
		["Stock", "box", Vector3(0.04, 0.08, 0.22), Vector3(0, -0.09, 0.29), Vector3(-6, 0, 0), WOOD, 0.0],
		["Rib", "box", Vector3(0.012, 0.006, 0.46), Vector3(0, -0.012, -0.25), Vector3.ZERO, DARK, 0.0],
		["SightDot", "sphere", Vector3(0.004, 0, 0), Vector3(0, 0, -0.47), Vector3.ZERO, Color(0.95, 0.95, 0.95), 4.0],
		["MuzzleFlash", "sphere", Vector3(0.05, 0, 0), Vector3(0, -0.032, -0.5), Vector3.ZERO, FLASH, 6.0],
	],
	"sniper": [ # bolt action with a big scope on the sight line
		["Receiver", "box", Vector3(0.05, 0.07, 0.3), Vector3(0, -0.085, 0.05), Vector3.ZERO, STEEL, 0.0],
		["Barrel", "cyl", Vector2(0.014, 0.52), Vector3(0, -0.075, -0.36), Vector3.ZERO, DARK, 0.0],
		["Brake", "box", Vector3(0.036, 0.03, 0.05), Vector3(0, -0.075, -0.64), Vector3.ZERO, DARK, 0.0],
		["Scope", "cyl", Vector2(0.022, 0.3), Vector3(0, 0, 0), Vector3.ZERO, DARK, 0.0],
		["ScopeFront", "cyl", Vector2(0.031, 0.05), Vector3(0, 0, -0.16), Vector3.ZERO, DARK, 0.0],
		["ScopeRear", "cyl", Vector2(0.028, 0.04), Vector3(0, 0, 0.15), Vector3.ZERO, DARK, 0.0],
		["Lens", "cyl", Vector2(0.026, 0.004), Vector3(0, 0, -0.186), Vector3.ZERO, Color(0.3, 0.6, 0.9), 1.5],
		["MountFront", "box", Vector3(0.02, 0.04, 0.02), Vector3(0, -0.04, -0.08), Vector3.ZERO, DARK, 0.0],
		["MountRear", "box", Vector3(0.02, 0.04, 0.02), Vector3(0, -0.04, 0.08), Vector3.ZERO, DARK, 0.0],
		["Bolt", "box", Vector3(0.05, 0.012, 0.012), Vector3(0.045, -0.07, 0.12), Vector3.ZERO, GOLD, 0.0],
		["Magazine", "box", Vector3(0.036, 0.06, 0.07), Vector3(0, -0.145, 0.02), Vector3.ZERO, ORANGE, 0.0],
		["Grip", "box", Vector3(0.04, 0.11, 0.05), Vector3(0, -0.16, 0.16), Vector3(-15, 0, 0), DARK, 0.0],
		["Stock", "box", Vector3(0.045, 0.09, 0.26), Vector3(0, -0.1, 0.34), Vector3.ZERO, STEEL, 0.0],
		["MuzzleFlash", "sphere", Vector3(0.06, 0, 0), Vector3(0, -0.075, -0.68), Vector3.ZERO, FLASH, 6.0],
	],
}


func _initialize() -> void:
	for model_name in MODELS:
		var root := Node3D.new()
		root.name = model_name.capitalize().replace(" ", "")
		for part in MODELS[model_name]:
			var node := MeshInstance3D.new()
			node.name = part[0]
			node.mesh = _mesh(part[1], part[2])
			node.position = part[3]
			node.rotation_degrees = part[4]
			if part[1] == "cyl":
				node.rotation_degrees.x += 90.0 # cylinders run along -z, like a barrel
			node.material_override = MATERIAL
			node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			node.set_instance_shader_parameter("color", part[5])
			node.set_instance_shader_parameter("glow", part[6])
			if part[0] == "MuzzleFlash":
				node.visible = false
			root.add_child(node)
			node.owner = root
		var scene := PackedScene.new()
		scene.pack(root)
		var path := "res://weapons/models/%s.tscn" % model_name
		var err := ResourceSaver.save(scene, path)
		print("saved ", path, " (", root.get_child_count(), " parts) ", "ok" if err == OK else "error %d" % err)
		root.free()
	quit()


func _mesh(kind: String, size) -> Mesh:
	match kind:
		"box":
			var box := BoxMesh.new()
			box.size = size
			return box
		"cyl":
			var cyl := CylinderMesh.new()
			cyl.top_radius = size.x
			cyl.bottom_radius = size.x
			cyl.height = size.y
			cyl.radial_segments = 10
			cyl.rings = 0
			return cyl
		"ring":
			var ring := TorusMesh.new() # size = Vector2(inner radius, outer radius)
			ring.inner_radius = size.x
			ring.outer_radius = size.y
			ring.rings = 24
			ring.ring_segments = 6
			return ring
		_:
			var sphere := SphereMesh.new()
			sphere.radius = size.x
			sphere.height = size.x * 2.0
			sphere.radial_segments = 8
			sphere.rings = 4
			return sphere
