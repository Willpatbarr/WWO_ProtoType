class_name StatModifier
extends Resource
# One change to one stat, e.g. "sprint_speed +12%".
# Final value = (base + all FLAT) * (1 + all PERCENT_ADD) * (all MULTIPLY multiplied together)

enum Op { FLAT, PERCENT_ADD, MULTIPLY }

@export var stat := "" # see Modifiers.STAT_NAMES for the stats that can be changed
@export var op := Op.PERCENT_ADD
@export var value := 0.0 # FLAT: amount added; PERCENT_ADD: 0.12 = +12%; MULTIPLY: 1.5 = x1.5


func describe() -> String:
	var name: String = Modifiers.STAT_NAMES.get(stat, stat)
	match op:
		Op.FLAT:
			return "%s %+.2f" % [name, value]
		Op.MULTIPLY:
			return "%s ×%.2f" % [name, value]
	return "%s %+d%%" % [name, roundi(value * 100.0)]
