class_name CharacterModel
extends Node3D
# The mannequin character (Quaternius Universal Animation Library, CC0), recolored.
# The same model the Soldier enemies use. Used for the player's body and the character
# menu preview. Its origin is at the feet and it faces -Z. Plays one animation at a time
# with a short crossfade.

const MODEL := preload("res://enemies/models/ual/UAL1_Standard.glb")
const LOOPING := ["Idle", "Walk", "Jog_Fwd", "Sprint", "Crouch_Idle", "Crouch_Fwd"]
const REGION_SHADER := preload("res://characters/region_color.gdshader")
const REGIONS := ["head", "arms", "core", "legs"] # same names as the frame part slots
# Which region each bone belongs to, by how its name starts.
const BONE_REGIONS := {
	"Head": "head", "neck": "head",
	"upperarm": "arms", "lowerarm": "arms", "hand": "arms",
	"index": "arms", "middle": "arms", "ring": "arms", "pinky": "arms", "thumb": "arms",
	"root": "core", "pelvis": "core", "spine": "core", "clavicle": "core",
	"thigh": "legs", "calf": "legs", "foot": "legs", "ball": "legs",
}

@export var body_color := Color(0.3, 0.42, 0.55)
@export var joint_color := Color(0.15, 0.16, 0.18)

var anim: AnimationPlayer
var skeleton: Skeleton3D
var _flora_parts: Array[Node] = [] # attachments showing the equipped flora
var _shadow_only := false
var _body_mat := ShaderMaterial.new()
var _region_colors: Array[Color] = []
var _joint_mat := StandardMaterial3D.new()


func _ready() -> void:
	var model := MODEL.instantiate() as Node3D
	model.rotation.y = PI # the file faces +Z
	add_child(model)
	anim = model.find_children("*", "AnimationPlayer", true, false)[0]
	skeleton = model.find_children("*", "Skeleton3D", true, false)[0]
	for loop_name in LOOPING:
		anim.get_animation(loop_name).loop_mode = Animation.LOOP_LINEAR
	_joint_mat.roughness = 0.6
	var mesh: MeshInstance3D = model.find_children("*", "MeshInstance3D", true, false)[0]
	_body_mat.shader = REGION_SHADER
	_body_mat.set_shader_parameter("bind_region", _bind_regions(mesh))
	for i in REGIONS.size():
		_region_colors.append(body_color)
	mesh.set_surface_override_material(0, _body_mat)
	mesh.set_surface_override_material(1, _joint_mat)
	set_colors(body_color, joint_color)
	play("Idle")


func set_colors(body: Color, joint: Color) -> void:
	# The whole body one color (every region), plus the joint color.
	body_color = body
	joint_color = joint
	for region in REGIONS:
		set_region_color(region, body)
	_joint_mat.albedo_color = joint


func set_region_color(region: String, color: Color) -> void:
	# Color one region: "head", "arms", "core" or "legs".
	var index := REGIONS.find(region)
	if index == -1:
		return
	_region_colors[index] = color
	_body_mat.set_shader_parameter("region_colors", _region_colors)


func show_loadout(loadout: Loadout) -> void:
	show_frame_parts(loadout)
	show_flora(loadout)


func show_frame_parts(loadout: Loadout) -> void:
	# Each region takes the color of the frame part equipped in that slot.
	for region in REGIONS:
		var part := loadout.frame_part(region)
		set_region_color(region, part.body_color(body_color) if part else body_color)


func follow_loadout(loadout: Loadout) -> void:
	# Show the equipped parts now, and again whenever the loadout changes.
	if not is_node_ready():
		await ready
	show_loadout(loadout)
	loadout.changed.connect(show_loadout.bind(loadout))


# --- Flora markers (test visuals) ----------------------------------------------------
# Grenade: a ball on the right hip, in the grenade's color.
# Seedburst: three smaller balls on the left hip, same color as the grenade.
# Overgrowth: ovals on the front of both shins. Thornfist: ovals on the outside of both
# forearms. Each is pinned to a bone, so it moves with the animations.

const GRENADE_RADIUS := 0.065
const SEED_RADIUS := 0.035
const OVAL_SIZE := Vector2(0.045, 0.2) # radius, length


func show_flora(loadout: Loadout) -> void:
	for node in _flora_parts:
		node.queue_free()
	_flora_parts.clear()
	var ids := []
	for slot in Loadout.ABILITY_SLOTS:
		var ability := loadout.ability(slot)
		if ability:
			ids.append(ability.id)
	var grenade_color := loadout.grenade.color if loadout.grenade else Color.WHITE
	# A flora can give every graft one color (VoltVine: all blue).
	var graft := loadout.flora.graft_color if loadout.flora else Color(0, 0, 0, 0)
	if graft.a > 0.0:
		grenade_color = graft
	# The markers are placed from the standing pose, then follow their bones.
	var was := anim.current_animation
	anim.play("Idle")
	anim.seek(0.0, true)
	var right := global_basis.x.normalized()
	var forward := -global_basis.z.normalized()
	var hips := _bone_point("pelvis")
	if loadout.grenade:
		_attach_ball("pelvis", hips + right * 0.17 + forward * 0.04, GRENADE_RADIUS, grenade_color)
	if "seedburst" in ids:
		for i in 3:
			var spot := hips - right * 0.165 + forward * (0.07 - 0.07 * i) # in a row along the belt
			_attach_ball("pelvis", spot, SEED_RADIUS, grenade_color)
	for side in ["l", "r"]:
		var outward := right if side == "r" else -right
		if "overgrowth" in ids:
			var knee := _bone_point("calf_" + side)
			var ankle := _bone_point("foot_" + side)
			_attach_oval("calf_" + side, knee.lerp(ankle, 0.45) + forward * 0.06, ankle - knee, graft if graft.a > 0.0 else _ability_color(loadout, "overgrowth"))
		if "thornfist" in ids:
			var elbow := _bone_point("lowerarm_" + side)
			var wrist := _bone_point("hand_" + side)
			_attach_oval("lowerarm_" + side, elbow.lerp(wrist, 0.5) + outward * 0.05, wrist - elbow, graft if graft.a > 0.0 else _ability_color(loadout, "thornfist"))
		if "melee_guard" in ids:
			# Melee Guard: a row of studs across the back of each hand.
			var hand := _bone_point("hand_" + side)
			var knuckles := _bone_point("middle_01_" + side)
			for i in 3:
				var across := (i - 1) * 0.03
				_attach_ball("hand_" + side, hand.lerp(knuckles, 0.6) + forward * across + Vector3.UP * 0.025 + outward * 0.02, 0.02,
					graft if graft.a > 0.0 else _ability_color(loadout, "melee_guard"))
	var tint := func(id: String) -> Color: return graft if graft.a > 0.0 else _ability_color(loadout, id)
	var chest := _bone_point("spine_03")
	var back := -forward
	# VeilSpore grafts (all spore purple).
	if "spore_fog_damage" in ids:
		for side in ["l", "r"]: # mushroom caps on the shoulders
			var shoulder := _bone_point("upperarm_" + side) + Vector3.UP * 0.07
			_attach_cap("upperarm_" + side, shoulder, 0.07, tint.call("spore_fog_damage"))
	if "spore_dash" in ids:
		for side in ["l", "r"]: # spore pods on the backs of the calves
			var knee := _bone_point("calf_" + side)
			var ankle := _bone_point("foot_" + side)
			_attach_ball("calf_" + side, knee.lerp(ankle, 0.35) + back * 0.07, 0.045, tint.call("spore_dash"))
			_attach_ball("calf_" + side, knee.lerp(ankle, 0.55) + back * 0.06, 0.035, tint.call("spore_dash"))
	if "spore_patches" in ids:
		for i in 3: # little mushrooms along the back of the belt
			_attach_cap("pelvis", hips + back * 0.13 + right * (i - 1) * 0.09 + Vector3.UP * 0.05, 0.04, tint.call("spore_patches"))
	# IronBark grafts (all bark brown).
	if "root_bombs" in ids:
		for side in [-1.0, 1.0]: # two big bombs carried on the back
			_attach_ball("spine_03", chest + back * 0.2 + right * 0.1 * side, 0.09, tint.call("root_bombs"))
	if "shield_share" in ids: # a bark plate on the chest
		var plate := BoxMesh.new()
		plate.size = Vector3(0.24, 0.2, 0.04)
		_attach("spine_03", Transform3D(global_basis.orthonormalized(), chest + forward * 0.13), plate, tint.call("shield_share"))
	if "extra_grenade" in ids: # a second grenade on the other hip
		_attach_ball("pelvis", hips - right * 0.17 + forward * 0.04, GRENADE_RADIUS, grenade_color)
	if was != "":
		anim.play(was)
	set_shadow_only(_shadow_only) # new markers match the body


func _ability_color(loadout: Loadout, id: String) -> Color:
	for slot in Loadout.ABILITY_SLOTS:
		var ability := loadout.ability(slot)
		if ability and ability.id == id:
			return ability.color
	return Color.WHITE


func _bone_point(bone: String) -> Vector3:
	return bone_transform(bone).origin


func _attach(bone: String, world: Transform3D, mesh: Mesh, color: Color, glow := 0.0, track := true) -> MeshInstance3D:
	# Pins `mesh` to `bone`, at `world` (where it should be in the current pose).
	var attachment := BoneAttachment3D.new()
	attachment.bone_name = bone
	skeleton.add_child(attachment)
	var bone_world := skeleton.global_transform * skeleton.get_bone_global_pose(skeleton.find_bone(bone))
	var instance := MeshInstance3D.new()
	instance.mesh = mesh
	var mat := StandardMaterial3D.new()
	mat.albedo_color = color
	mat.roughness = 0.5
	if glow > 0.0:
		mat.emission_enabled = true
		mat.emission = color
		mat.emission_energy_multiplier = glow
	instance.material_override = mat
	attachment.add_child(instance)
	instance.transform = bone_world.affine_inverse() * world
	if track:
		_flora_parts.append(attachment)
	return instance


func _attach_ball(bone: String, at: Vector3, radius: float, color: Color) -> void:
	var sphere := SphereMesh.new()
	sphere.radius = radius
	sphere.height = radius * 2.0
	_attach(bone, Transform3D(Basis.IDENTITY, at), sphere, color)


func _attach_cap(bone: String, at: Vector3, radius: float, color: Color) -> void:
	# A little mushroom cap (a flattened half-dome).
	var dome := SphereMesh.new()
	dome.radius = radius
	dome.height = radius
	dome.is_hemisphere = true
	_attach(bone, Transform3D(Basis.IDENTITY, at), dome, color, 0.6)


func _attach_oval(bone: String, at: Vector3, along: Vector3, color: Color) -> void:
	var oval := SphereMesh.new()
	oval.radius = OVAL_SIZE.x
	oval.height = OVAL_SIZE.y # a stretched sphere: long along its Y axis
	var up := along.normalized()
	var side := up.cross(Vector3.FORWARD if absf(up.z) < 0.9 else Vector3.RIGHT).normalized()
	var basis := Basis(side, up, side.cross(up))
	_attach(bone, Transform3D(basis, at), oval, color)


# --- Helpers for enemies and gear ----------------------------------------------------

func bone_transform(bone: String) -> Transform3D:
	# Where a bone is right now, in world space.
	return skeleton.global_transform * skeleton.get_bone_global_pose(skeleton.find_bone(bone))


func force_pose(anim_name: String, at := 0.0) -> void:
	# Snaps the skeleton into an animation frame now (for measuring where to put things).
	anim.play(anim_name)
	anim.seek(at, true)


func attach_box(bone: String, center: Vector3, size: Vector3, color: Color, glow := 0.0) -> MeshInstance3D:
	# A box pinned to a bone, placed at `center` (world, current pose), lined up with the
	# body (not the bone), so armor plates sit square.
	var box := BoxMesh.new()
	box.size = size / global_basis.get_scale() # the model's scale grows it back
	return _attach(bone, Transform3D(global_basis.orthonormalized(), center), box, color, glow, false)


func set_flash(amount: float) -> void:
	_body_mat.set_shader_parameter("flash", amount)


func _bind_regions(mesh: MeshInstance3D) -> PackedInt32Array:
	# For each skin bind (what the mesh's bone indices point at), its region number.
	var regions := PackedInt32Array()
	regions.resize(128)
	var skin := mesh.skin
	var count := skin.get_bind_count() if skin else skeleton.get_bone_count()
	for i in mini(count, 128):
		var bone_name: String = skin.get_bind_name(i) if skin else skeleton.get_bone_name(i)
		if bone_name == "" and skin:
			bone_name = skeleton.get_bone_name(skin.get_bind_bone(i))
		regions[i] = REGIONS.find(_region_of(bone_name))
	return regions


func _region_of(bone_name: String) -> String:
	for start in BONE_REGIONS:
		if bone_name.begins_with(start):
			return BONE_REGIONS[start]
	return "core"


func play(anim_name: String, speed := 1.0, blend := 0.2) -> void:
	if anim == null:
		return
	if anim.current_animation != anim_name:
		anim.play(anim_name, blend)
	anim.speed_scale = speed


func set_shadow_only(on: bool) -> void:
	# Draws only the body's shadow (for your own body in first person).
	_shadow_only = on
	for mesh in find_children("*", "MeshInstance3D", true, false):
		(mesh as MeshInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_SHADOWS_ONLY if on else GeometryInstance3D.SHADOW_CASTING_SETTING_ON
