class_name WaveManager
extends Node
# The arena's game loop: waves of enemies from spawn points around the map.
# - When it starts (and there are spawn points: Marker3Ds in the "enemy_spawn" group),
#   the hand-placed enemies are cleared, then a countdown, then wave 1.
# - Each wave's enemies come in one by one from spawn points away from you, and hunt you
#   right away. Clear them all and the next wave starts after a short break.
# - Clear `waves_to_win` waves: ARENA CLEARED. Go down with no teammate standing:
#   DEFEATED, and the run restarts.
# Shows its own HUD line (top center). Settings → Waves edits the numbers.

@export var enabled := 1.0 # 0 = sandbox: the map keeps its hand-placed enemies
@export var waves_to_win := 5.0
@export var start_delay := 6.0 # seconds before wave 1
@export var break_time := 8.0 # seconds between waves
@export var size_scale := 1.0 # multiplies every wave's enemy counts
@export var spawn_stagger := 0.35 # seconds between each enemy appearing

const SCENES := {
	"swarm": preload("res://enemies/swarm.tscn"),
	"soldier": preload("res://enemies/soldier.tscn"),
	"tank": preload("res://enemies/tank.tscn"),
}
# The waves, in order. Past the last one, it repeats the last wave, 25% bigger each time.
const WAVES := [
	{"swarm": 6},
	{"swarm": 6, "soldier": 2},
	{"swarm": 8, "soldier": 3},
	{"soldier": 4, "tank": 1},
	{"swarm": 10, "soldier": 3, "tank": 1},
]
const MIN_SPAWN_DISTANCE := 14.0 # enemies don't appear right next to you
const CORPSE_CLEANUP := 4.0

var state := "idle" # idle, countdown, spawning, fighting, won, lost
var wave := 0
var _timer := 0.0
var _queue: Array[String] = []
var _alive: Array[Node] = []
var _label: Label
var _player: Node3D


func _ready() -> void:
	add_to_group("wave_manager")
	_build_label()
	_start.call_deferred() # after the level and its enemies are set up


func _start() -> void:
	_player = get_tree().get_first_node_in_group("player")
	var health := get_tree().get_first_node_in_group("player_health")
	if health:
		health.died.connect(_on_player_died)
	if enabled < 0.5 or _spawn_points().is_empty():
		state = "idle"
		return
	for enemy in get_tree().get_nodes_in_group("enemies"):
		enemy.queue_free() # waves replace the hand-placed enemies
	wave = 0
	_countdown(start_delay)


func restart() -> void:
	get_tree().reload_current_scene()


func skip_wave() -> void:
	# Test helper: kill everything in the current wave.
	_queue.clear()
	for enemy in _alive:
		if is_instance_valid(enemy) and not enemy.dead:
			enemy.hit(99999.0, false, (enemy as Node3D).global_position)


func _countdown(seconds: float) -> void:
	state = "countdown"
	_timer = seconds


func _process(delta: float) -> void:
	match state:
		"countdown":
			_timer -= delta
			if _timer <= 0.0:
				_start_wave()
		"spawning", "fighting":
			_alive = _alive.filter(func(e: Node) -> bool: return is_instance_valid(e) and not e.dead)
			if not _queue.is_empty():
				_timer -= delta
				if _timer <= 0.0:
					_timer = spawn_stagger
					_spawn(_queue.pop_front())
			elif _alive.is_empty():
				_wave_cleared()
	_update_label()


func _start_wave() -> void:
	wave += 1
	var plan: Dictionary = WAVES[mini(wave - 1, WAVES.size() - 1)]
	var growth := pow(1.25, maxi(wave - WAVES.size(), 0))
	_queue.clear()
	for kind in plan:
		for i in maxi(roundi(plan[kind] * size_scale * growth), 1):
			_queue.append(kind)
	_queue.shuffle()
	_timer = 0.0
	state = "spawning"


func _wave_cleared() -> void:
	if wave >= roundi(waves_to_win):
		state = "won"
		return
	_countdown(break_time)


func _on_player_died() -> void:
	if state in ["idle", "won", "lost"]:
		return
	if get_tree().get_nodes_in_group("teammates").is_empty():
		state = "lost" # nobody left standing
		get_tree().create_timer(4.0).timeout.connect(restart)


# --- Spawning -----------------------------------------------------------------------

func _spawn_points() -> Array[Node]:
	return get_tree().get_nodes_in_group("enemy_spawn")


func _pick_point(kind: String) -> Node3D:
	# A spawn point that suits this enemy (if any are marked for it), away from you.
	var points := _spawn_points().filter(func(p: Node) -> bool:
		var kinds: String = p.get_meta("kinds", "")
		return kinds == "" or kind in kinds)
	if points.is_empty():
		points = _spawn_points()
	var far := points.filter(func(p: Node) -> bool:
		return _player == null or (p as Node3D).global_position.distance_to(_player.global_position) >= MIN_SPAWN_DISTANCE)
	if not far.is_empty():
		points = far
	return points[randi() % points.size()]


func _spawn(kind: String) -> void:
	var point := _pick_point(kind)
	var enemy: Node3D = SCENES[kind].instantiate()
	var root := get_tree().current_scene if get_tree().current_scene else get_tree().root
	root.add_child(enemy)
	var offset := Vector3(randf_range(-1.5, 1.5), 0.0, randf_range(-1.5, 1.5)) if kind != "tank" else Vector3.ZERO
	enemy.global_position = point.global_position + offset
	enemy.rotation.y = randf() * TAU
	enemy.alerted = true # waves come looking for you
	enemy.manager = null # wave enemies don't respawn
	enemy.died.connect(func() -> void:
		get_tree().create_timer(CORPSE_CLEANUP).timeout.connect(enemy.queue_free))
	_alive.append(enemy)


# --- HUD line -----------------------------------------------------------------------

func _build_label() -> void:
	var layer := CanvasLayer.new()
	layer.layer = 5
	add_child(layer)
	_label = Label.new()
	_label.set_anchors_and_offsets_preset(Control.PRESET_CENTER_TOP)
	_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_label.grow_horizontal = Control.GROW_DIRECTION_BOTH
	_label.position.y = 24
	_label.add_theme_font_size_override("font_size", 28)
	_label.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.85))
	_label.add_theme_constant_override("outline_size", 8)
	_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	layer.add_child(_label)


func _update_label() -> void:
	var total := roundi(waves_to_win)
	match state:
		"idle":
			_label.text = ""
		"countdown":
			_label.text = ("Wave %d of %d in %d" % [wave + 1, total, ceili(_timer)]) if wave > 0 else ("Get ready: wave 1 of %d in %d" % [total, ceili(_timer)])
			_label.modulate = Color(1, 1, 1)
		"spawning", "fighting":
			_label.text = "WAVE %d / %d   ·   %d enemies left" % [wave, total, _alive.size() + _queue.size()]
			_label.modulate = Color(1, 0.9, 0.75)
		"won":
			_label.text = "ARENA CLEARED   ·   %d waves" % wave
			_label.modulate = Color(0.6, 1, 0.65)
		"lost":
			_label.text = "DEFEATED on wave %d   ·   restarting" % wave
			_label.modulate = Color(1, 0.45, 0.4)
