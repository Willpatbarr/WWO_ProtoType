class_name ControllerChord
extends Node
# Controller bumpers: L1 = grenade, R1 = melee, and both together = ult.
# A bumper press waits a moment (WINDOW) to see if the other bumper follows. If it does,
# that's the ult; if not, the grenade or melee goes off. Keyboard keys aren't delayed.
# Readers call `consume("grenade" / "melee" / "ult")` from their own _process.

const WINDOW := 0.1 # seconds to wait for the second bumper
const GROUP := "controller_chord"

var _pending := {} # "grenade" / "melee" -> seconds since that bumper was pressed
var _fired := {} # action -> frames it stays readable (so readers that run first still see it)


static func consume(tree: SceneTree, action: String) -> bool:
	var chord := tree.get_first_node_in_group(GROUP) as ControllerChord
	if chord and chord._fired.has(action):
		chord._fired.erase(action)
		return true
	return false


func _ready() -> void:
	add_to_group(GROUP)
	process_priority = -10 # run before the grenade, gun and ult readers


func _process(delta: float) -> void:
	for action in _fired.keys():
		_fired[action] -= 1
		if _fired[action] < 0:
			_fired.erase(action)
	if Input.is_action_just_pressed("grenade_pad"):
		_pending["grenade"] = 0.0
	if Input.is_action_just_pressed("melee_pad"):
		_pending["melee"] = 0.0
	if _pending.has("grenade") and _pending.has("melee"):
		_pending.clear()
		_fired["ult"] = 1
		return
	for action in _pending.keys():
		_pending[action] += delta
		if _pending[action] >= WINDOW:
			_pending.erase(action)
			_fired[action] = 1
