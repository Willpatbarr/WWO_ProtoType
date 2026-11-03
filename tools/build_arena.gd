extends SceneTree
# Builds res://arena.tscn: the game (everything in main.tscn: player, HUD, menus, managers)
# on the sci-fi arena map instead of the gray-box test range. main.tscn stays as the test
# range. Run after tools/build_scifi_map.gd:
#   godot --headless -s tools/build_arena.gd

const TEST_RANGE_GEOMETRY := ["Floor", "Cube", "LowBar", "Pillar", "Ball", "JumpPlatform",
	"DoubleJumpPlatform", "Ramp", "BackWall", "CrateLeft", "CrateRight",
	"SoldierCoverA", "SoldierCoverB", "SoldierCoverC"]
const TILE := 4.0


func tile(x: float, z: float, y := 0.0) -> Vector3:
	return Vector3((x - 7.5) * TILE, y, (z - 5.5) * TILE)


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
	var map: Node3D = load("res://maps/scifi_arena_map.tscn").instantiate()
	game.add_child(map)
	game.move_child(map, 2)
	map.owner = game

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

	var scene := PackedScene.new()
	scene.pack(game)
	var err := ResourceSaver.save(scene, "res://arena.tscn")
	print("saved res://arena.tscn ", "ok" if err == OK else "error %d" % err)
	game.free()
	quit()
