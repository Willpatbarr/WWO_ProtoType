extends SceneTree
# Builds res://arena.tscn (or res://arena_large.tscn with "-- large"): the game (everything in main.tscn: player, HUD, menus, managers)
# on the sci-fi arena map instead of the gray-box test range. main.tscn stays as the test
# range. Run after tools/build_scifi_map.gd:
#   godot --headless -s tools/build_arena.gd

const TEST_RANGE_GEOMETRY := ["Floor", "Cube", "LowBar", "Pillar", "Ball", "JumpPlatform",
	"DoubleJumpPlatform", "Ramp", "BackWall", "CrateLeft", "CrateRight",
	"SoldierCoverA", "SoldierCoverB", "SoldierCoverC"]
const TILE := 4.0


var _center := Vector2(7.5, 5.5) # the map's middle tile (small map; the large one is 11.5, 7.5)


func tile(x: float, z: float, y := 0.0) -> Vector3:
	return Vector3((x - _center.x) * TILE, y, (z - _center.y) * TILE)


func face(node: Node3D, at: Vector3, look: Vector3) -> void:
	node.position = at
	var d := look - at
	node.rotation = Vector3(0, atan2(-d.x, -d.z), 0) # -Z faces `look`


func _initialize() -> void:
	var game: Node = load("res://main.tscn").instantiate()
	for n in TEST_RANGE_GEOMETRY:
		var node := game.get_node_or_null(n)
		if node:
			game.remove_child(node)
			node.free()
	var large := "large" in OS.get_cmdline_user_args()
	if large:
		_center = Vector2(11.5, 7.5)
	var map: Node3D = load("res://maps/scifi_arena_large_map.tscn" if large else "res://maps/scifi_arena_map.tscn").instantiate()
	game.add_child(map)
	game.move_child(map, 2)
	map.owner = game

	if large:
		_place_large(game)
		_save(game, "res://arena_large.tscn")
		return
	var spawn := tile(7.5, 10.3, 1.0)
	var courtyard := tile(7.5, 5.5)
	face(game.get_node("Player"), spawn, courtyard + Vector3.UP)
	game.get_node("PlayerSpawn").position = spawn

	# Practice dummies: two in the spawn room, the rest out in the courtyard.
	var dummies := {
		"TargetClose": tile(6.3, 11), "TargetLeft": tile(8.7, 11),
		"TargetNear": tile(4, 6), "TargetMid": tile(11, 4.5),
		"TargetFar": tile(7.5, 3.4), "TargetOnPlatform": tile(2.5, 3.5, 2.0),
	}
	for n in dummies:
		face(game.get_node(n), dummies[n], spawn)

	# Soldiers hold the courtyard (one on the east platform's high ground).
	face(game.get_node("SoldierA"), tile(12.5, 7, 2.0), spawn)
	face(game.get_node("SoldierB"), tile(8.5, 3.8), spawn)
	face(game.get_node("SoldierC"), tile(5, 4.4), spawn)
	# The Swarm pack waits at the north end of the west corridor.
	var pack := [Vector2(0.4, 3.3), Vector2(1.4, 3.6), Vector2(0.7, 4.2), Vector2(1.2, 2.6), Vector2(0.4, 2.2), Vector2(1.6, 2.0)]
	for i in pack.size():
		face(game.get_node("Swarm%d" % (i + 1)), tile(pack[i].x, pack[i].y), tile(1, 9))
	# Two AI teammates start in the spawn room with you.
	face(game.get_node("AllyA"), tile(6.6, 10.8, 1.0), courtyard)
	face(game.get_node("AllyB"), tile(8.4, 10.8, 1.0), courtyard)
	# The Tank guards the north hall.
	face(game.get_node("Tank"), tile(7.5, 1), courtyard)

	_save(game, "res://arena.tscn")


func _save(game: Node, path: String) -> void:
	var scene := PackedScene.new()
	scene.pack(game)
	var err := ResourceSaver.save(scene, path)
	print("saved ", path, " ", "ok" if err == OK else "error %d" % err)
	game.free()
	quit()


func _place_large(game: Node) -> void:
	# The large arena: spawn room in the south, atrium, tower, west wing up top.
	var spawn := tile(11.5, 14.3, 1.0)
	var atrium := tile(11.5, 8.5)
	face(game.get_node("Player"), spawn, atrium + Vector3.UP)
	game.get_node("PlayerSpawn").position = spawn
	face(game.get_node("AllyA"), tile(10.6, 14.8, 1.0), atrium)
	face(game.get_node("AllyB"), tile(12.4, 14.8, 1.0), atrium)
	var dummies := {
		"TargetClose": tile(10.4, 15.2), "TargetLeft": tile(12.6, 15.2),
		"TargetNear": tile(8, 7.5), "TargetMid": tile(15.5, 9.5),
		"TargetFar": tile(11.5, 5.4), "TargetOnPlatform": tile(11, 8.6, 4.0),
	}
	for n in dummies:
		face(game.get_node(n), dummies[n], spawn)
	# Sandbox enemies (waves clear these when they start).
	face(game.get_node("SoldierA"), tile(12.5, 8.6, 4.0), spawn) # on the tower
	face(game.get_node("SoldierB"), tile(4.6, 6.5, 4.0), spawn) # the west wing overlook
	face(game.get_node("SoldierC"), tile(16, 6.5), spawn)
	var pack := [Vector2(18.4, 3.2), Vector2(19.4, 3.6), Vector2(18.7, 4.4), Vector2(19.2, 2.4), Vector2(18.4, 1.8), Vector2(19.6, 1.4)]
	for i in pack.size():
		face(game.get_node("Swarm%d" % (i + 1)), tile(pack[i].x, pack[i].y), tile(18.5, 12))
	face(game.get_node("Tank"), tile(11.5, 2), atrium)
