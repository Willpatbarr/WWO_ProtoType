extends SceneTree
# Builds the frame part data files into res://data/frame_parts/*.tres.
# Run from the project folder:  godot --headless -s tools/build_frame_parts.gd
# Each part: [id, name, slot, [[stat, op, value], ...]]
#   op: "flat" adds, "pct" is a percent (0.12 = +12%), "mult" multiplies.

const PARTS := [
	["standard_legs", "Standard Legs", "legs", []],
	["stride_legs", "Stride Legs", "legs", [["sprint_speed", "pct", 0.12], ["jump_height", "pct", -0.10]]],
	["spring_legs", "Spring Legs", "legs", [["jump_height", "pct", 0.20], ["walk_speed", "pct", -0.08]]],
	["glide_legs", "Glide Legs", "legs", [["air_control", "pct", 0.40], ["coyote_time", "flat", 0.08], ["sprint_speed", "pct", -0.06]]],
	["standard_core", "Standard Core", "core", []],
	["light_core", "Light Core", "core", [["walk_speed", "pct", 0.08], ["crouch_speed", "pct", 0.15], ["slide_distance", "pct", -0.15]]],
	["slick_core", "Slick Core", "core", [["slide_distance", "pct", 0.30], ["slide_speed", "pct", 0.15], ["crouch_speed", "pct", -0.10]]],
	["climber_core", "Climber Core", "core", [["mantle_reach", "flat", 0.6], ["mantle_time", "pct", -0.30], ["walk_speed", "pct", -0.04]]],
	["standard_arms", "Standard Arms", "arms", []],
	["quick_arms", "Quick Arms", "arms", [["reload_time", "pct", -0.20], ["ads_time", "pct", -0.15], ["recoil", "pct", 0.15]]],
	["steady_arms", "Steady Arms", "arms", [["recoil", "pct", -0.25], ["sway", "pct", -0.40], ["ads_time", "pct", 0.10]]],
	["strong_arms", "Strong Arms", "arms", [["throw_distance", "pct", 0.25], ["melee_time", "pct", -0.15], ["melee_reach", "flat", 0.5], ["reload_time", "pct", 0.10]]],
]

const OPS := {"flat": StatModifier.Op.FLAT, "pct": StatModifier.Op.PERCENT_ADD, "mult": StatModifier.Op.MULTIPLY}


func _initialize() -> void:
	var order := 0
	for entry in PARTS:
		var part := FramePartData.new()
		part.id = entry[0]
		part.display_name = entry[1]
		part.slot = entry[2]
		part.order = order
		order += 1
		for m in entry[3]:
			var mod := StatModifier.new()
			mod.stat = m[0]
			mod.op = OPS[m[1]]
			mod.value = m[2]
			part.modifiers.append(mod)
		var path := "res://data/frame_parts/%s.tres" % part.id
		var err := ResourceSaver.save(part, path)
		print("saved ", path, " ", "ok" if err == OK else "error %d" % err)
	quit()
