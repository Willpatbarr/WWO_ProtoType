extends SceneTree
# Builds the frame part data files into res://data/frame_parts/*.tres.
# Run from the project folder:  godot --headless -s tools/build_frame_parts.gd
# Each part: [id, name, slot, {group: points}]. Groups: agility, aerial, dexterity, durability.

const PARTS := [
	# Every part (besides Standard) has a spike (+2) in one group and a half spike (+1) in
	# another. Nothing lowers a group for now. All 12 pairs appear exactly once.
	["standard_head", "Standard Head", "head", {}],
	["focus_head", "Focus Head", "head", {"dexterity": 2, "aerial": 1}],
	["guard_head", "Guard Head", "head", {"durability": 2, "dexterity": 1}],
	["scout_head", "Scout Head", "head", {"aerial": 2, "dexterity": 1}],
	["standard_arms", "Standard Arms", "arms", {}],
	["quick_arms", "Quick Arms", "arms", {"dexterity": 2, "agility": 1}],
	["braced_arms", "Braced Arms", "arms", {"dexterity": 2, "durability": 1}],
	["strong_arms", "Strong Arms", "arms", {"agility": 2, "dexterity": 1}],
	["standard_legs", "Standard Legs", "legs", {}],
	["stride_legs", "Stride Legs", "legs", {"agility": 2, "aerial": 1}],
	["spring_legs", "Spring Legs", "legs", {"aerial": 2, "agility": 1}],
	["sturdy_legs", "Sturdy Legs", "legs", {"durability": 2, "agility": 1}],
	["standard_core", "Standard Core", "core", {}],
	["light_core", "Light Core", "core", {"agility": 2, "durability": 1}],
	["armored_core", "Armored Core", "core", {"durability": 2, "aerial": 1}],
	["thruster_core", "Thruster Core", "core", {"aerial": 2, "durability": 1}],
]


func _initialize() -> void:
	DirAccess.make_dir_recursive_absolute("res://data/frame_parts")
	for f in DirAccess.get_files_at("res://data/frame_parts"):
		DirAccess.remove_absolute("res://data/frame_parts/" + f) # parts can be renamed or removed
	var order := 0
	for entry in PARTS:
		var part := FramePartData.new()
		part.id = entry[0]
		part.display_name = entry[1]
		part.slot = entry[2]
		part.order = order
		order += 1
		for group in entry[3]:
			part.set(group, float(entry[3][group]))
		var path := "res://data/frame_parts/%s.tres" % part.id
		var err := ResourceSaver.save(part, path)
		print("saved ", path, " ", "ok" if err == OK else "error %d" % err)
	quit()
