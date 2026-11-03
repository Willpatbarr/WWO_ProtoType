class_name HunchModifier
extends SkeletonModifier3D
# Bends a character forward at the spine on top of whatever animation is playing, and
# tilts the neck and head back up so it still looks ahead. Used for the Swarm's
# hunched-over run. Add it as a child of the Skeleton3D.

@export var spine_bend := 55.0 # degrees, spread over the three spine bones
@export var head_lift := 45.0 # degrees, spread over the neck and head

# The skeleton faces +Z, so its +X axis is the one to bend forward around.
const FORWARD_AXIS := Vector3(1, 0, 0)


func _process_modification() -> void:
	var skeleton := get_skeleton()
	if skeleton == null:
		return
	for bone in ["spine_01", "spine_02", "spine_03"]:
		_bend(skeleton, bone, spine_bend / 3.0)
	for bone in ["neck_01", "Head"]:
		_bend(skeleton, bone, -head_lift / 2.0)


func _bend(skeleton: Skeleton3D, bone_name: String, degrees: float) -> void:
	var bone := skeleton.find_bone(bone_name)
	if bone == -1:
		return
	# Rotate around the skeleton's forward-bend axis, expressed in the bone's own frame.
	var bone_basis := skeleton.get_bone_global_pose(bone).basis.orthonormalized()
	var local_axis := (bone_basis.inverse() * FORWARD_AXIS).normalized()
	var bent := skeleton.get_bone_pose_rotation(bone) * Quaternion(local_axis, deg_to_rad(degrees))
	skeleton.set_bone_pose_rotation(bone, bent)
