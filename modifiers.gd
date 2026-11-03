class_name Modifiers
extends Node
# The stat modifier system. Every changeable stat has a base value (what the settings
# sliders set) plus modifiers from equipped gear:
#   final = (base + sum of FLAT) * (1 + sum of PERCENT_ADD) * (product of MULTIPLY)
# Frame parts give group points (e.g. +2 Agility). Each point moves every stat in its
# group by `point_value` (5% by default) in the good direction: faster speeds, shorter
# times, more health. Other sources (traits, later) add StatModifiers under their own
# source name with add_source / remove_source.

# Stat groups, in display order. Frame parts give points in these.
const GROUPS := {
	"agility": ["walk_speed", "sprint_speed", "crouch_speed", "slide_speed", "slide_distance", "melee_time", "throw_distance"],
	"aerial": ["jump_height", "air_control"],
	"dexterity": ["reload_time", "ads_time", "swap_time", "ready_time"],
	"durability": ["max_health", "max_shield", "recharge_delay"],
}

# Stats that can be modified, with display names.
const STAT_NAMES := {
	"walk_speed": "Walk speed",
	"sprint_speed": "Sprint speed",
	"crouch_speed": "Crouch speed",
	"slide_speed": "Slide speed",
	"slide_distance": "Slide distance",
	"melee_time": "Melee time",
	"throw_distance": "Throw distance",
	"jump_height": "Jump height",
	"air_control": "Air control",
	"reload_time": "Reload time",
	"ads_time": "Aim-in time",
	"swap_time": "Swap time",
	"ready_time": "Handling (ready time)",
	"max_health": "Health",
	"max_shield": "Shields",
	"recharge_delay": "Recovery delay",
	"shield_regen": "Shield refill speed",
	"melee_damage": "Melee damage",
	"grenade_recharge": "Grenade recharge time",
}

# For these, lower is better, so a point shortens them.
const LOWER_IS_BETTER := ["melee_time", "reload_time", "ads_time", "swap_time", "ready_time", "recharge_delay"]

@export var loadout: Loadout
@export var point_value := 0.05 # how much one group point changes each stat in its group (0.05 = 5%)

var _by_source := {} # source -> Array[StatModifier]  (traits and other non-frame sources)


func _ready() -> void:
	# Equipped traits add their stat changes as sources "trait:<id>".
	loadout.changed.connect(_sync_traits)
	_sync_traits.call_deferred() # after the loadout has loaded


func _sync_traits() -> void:
	for source in _by_source.keys():
		if source.begins_with("trait:"):
			_by_source.erase(source)
	for equipped in loadout.equipped_traits():
		if not equipped.mods.is_empty():
			add_source("trait:" + equipped.id, equipped.mods)


func add_source(source: String, mods: Array) -> void:
	_by_source[source] = mods.duplicate()


func remove_source(source: String) -> void:
	_by_source.erase(source)


func group_of(stat: String) -> String:
	for group in GROUPS:
		if stat in GROUPS[group]:
			return group
	return ""


func group_points(group: String) -> float:
	# Total points in a group across the equipped frame parts.
	var total := 0.0
	for slot in Loadout.FRAME_SLOTS:
		var part := loadout.frame_part(slot)
		if part:
			total += part.points(group)
	return total


func apply(stat: String, base: float) -> float:
	var flat := 0.0
	var percent := 0.0
	var multiply := 1.0
	var group := group_of(stat)
	if group != "":
		var sign := -1.0 if stat in LOWER_IS_BETTER else 1.0
		percent += group_points(group) * point_value * sign
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


func group_summary() -> Array[String]:
	# One line per group with points, e.g. "Agility +2  (each stat +10%)".
	var lines: Array[String] = []
	for group in GROUPS:
		var pts := group_points(group)
		if absf(pts) < 0.01:
			continue
		lines.append("%s %+d  (each stat %+d%%)" % [group.capitalize(), roundi(pts), roundi(pts * point_value * 100.0)])
	return lines


func net_changes() -> Array[String]:
	# One line per modified stat, e.g. "Sprint speed ×1.12".
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
