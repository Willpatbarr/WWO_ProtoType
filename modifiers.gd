class_name Modifiers
extends Node
# The stat modifier system. Every changeable stat has a base value (what the settings
# sliders set) plus a list of modifiers tagged with where they came from:
#   final = (base + sum of FLAT) * (1 + sum of PERCENT_ADD) * (product of MULTIPLY)
# Equipping something adds its modifiers under its source; unequipping removes that source.
# Today the sources are the three equipped frame parts; traits can add theirs later.

# Stats that can be modified, with display names.
const STAT_NAMES := {
	"walk_speed": "Walk speed",
	"sprint_speed": "Sprint speed",
	"crouch_speed": "Crouch speed",
	"jump_height": "Jump height",
	"air_control": "Air control",
	"coyote_time": "Coyote time",
	"slide_speed": "Slide speed",
	"slide_distance": "Slide distance",
	"mantle_reach": "Mantle reach",
	"mantle_time": "Mantle time",
	"reload_time": "Reload time",
	"ads_time": "Aim-in time",
	"recoil": "Recoil",
	"sway": "Gun sway",
	"melee_time": "Melee time",
	"melee_reach": "Melee reach",
	"throw_distance": "Throw distance",
}

@export var loadout: Loadout

var _by_source := {} # source -> Array[StatModifier]


func _ready() -> void:
	loadout.changed.connect(_rebuild)
	_rebuild()


func add_source(source: String, mods: Array) -> void:
	_by_source[source] = mods.duplicate()


func remove_source(source: String) -> void:
	_by_source.erase(source)


func apply(stat: String, base: float) -> float:
	var flat := 0.0
	var percent := 0.0
	var multiply := 1.0
	for mods in _by_source.values():
		for mod: StatModifier in mods:
			if mod.stat != stat:
				continue
			match mod.op:
				StatModifier.Op.FLAT:
					flat += mod.value
				StatModifier.Op.PERCENT_ADD:
					percent += mod.value
				StatModifier.Op.MULTIPLY:
					multiply *= mod.value
	return (base + flat) * maxf(1.0 + percent, 0.0) * multiply


func net_changes() -> Array[String]:
	# One line per modified stat, e.g. "Sprint speed ×1.12" or "Mantle reach +0.60 m".
	var lines: Array[String] = []
	for stat in STAT_NAMES:
		var scale := apply(stat, 1.0) - apply(stat, 0.0) # the multiplying part
		var flat := apply(stat, 0.0) / maxf(scale, 0.001) # the adding part, before scaling
		if absf(scale - 1.0) < 0.001 and absf(flat) < 0.001:
			continue
		var parts := []
		if absf(flat) >= 0.001:
			parts.append("%+.2f" % flat)
		if absf(scale - 1.0) >= 0.001:
			parts.append("×%.2f" % scale)
		lines.append("%s %s" % [STAT_NAMES[stat], " ".join(parts)])
	return lines


func _rebuild() -> void:
	for slot in Loadout.FRAME_SLOTS:
		remove_source("frame:" + slot)
		var part := loadout.frame_part(slot)
		if part:
			add_source("frame:" + slot, part.modifiers)
