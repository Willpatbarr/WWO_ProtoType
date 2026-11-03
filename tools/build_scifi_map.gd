extends SceneTree
# Builds the sci-fi arena map into res://maps/scifi_arena_map.tscn from the Quaternius
# Modular Sci-Fi MegaKit pieces (CC0, res://maps/scifi/). Run from the project folder:
#   godot --headless -s tools/build_scifi_map.gd
# Then tools/build_arena.gd puts it into res://arena.tscn with the player and enemies.
#
# The map is a grid of 4 m tiles, 16 wide (x) by 12 deep (z). Areas:
#   Spawn room (south)   x 6-9,  z 9-11   a wide opening north into the courtyard
#   Courtyard (middle)   x 2-13, z 3-8    open fight space with cover and two raised platforms
#   North hall           x 0-12, z 0-2    big pillared room (the Tank)
#   West corridor        x 0-1,  z 3-9    flank route from the hall, doorways into the courtyard
# Every piece gets collision (a trimesh from its own mesh). A navigation mesh is baked so
# enemies can path around walls.

const KIT := "res://maps/scifi/"
const TILE := 4.0
# Which map: pass "large" after "--" for the large arena (tools/build_scifi_map.gd -- large).
var W := 16
var D := 12
var OUT := "res://maps/scifi_arena_map.tscn"
const PLATFORM_HEIGHT := 2.0
const FILLER_COLOR := Color(0.2, 0.22, 0.26)
const PLATFORM_COLOR := Color(0.24, 0.26, 0.3)

var root_node: Node3D
var _shapes := {} # piece path -> Array of [Shape3D, Transform3D] (shared by every copy)
var _wall_edges := {} # "x,z,edge" -> true, for corner fillers
var _count := 0
var _rails := [] # rail requests, placed last so stairs tops stay open: [x, z, edge, y]
var _stair_tops := {} # "x,z,edge" where a stairs climbs onto a platform: no rail there
var _large := false


func tile(x: float, z: float, y := 0.0) -> Vector3:
	return Vector3((x - (W - 1) * 0.5) * TILE, y, (z - (D - 1) * 0.5) * TILE)


func _initialize() -> void:
	_large = "large" in OS.get_cmdline_user_args()
	if _large:
		W = 24
		D = 16
		OUT = "res://maps/scifi_arena_large_map.tscn"
	seed(7 if _large else 3) # the same crate layout every rebuild
	root_node = Node3D.new()
	root_node.name = "SciFiArenaLarge" if _large else "SciFiArena"
	root.add_child(root_node)
	if _large:
		_build_large_layout()
	else:
		_build_layout()
	_place_rails()
	_corner_fillers()
	_boundary()
	for i in 4:
		await process_frame # let CSG shapes build their collision
	await _bake_navigation()
	_set_owner(root_node, root_node)
	var scene := PackedScene.new()
	scene.pack(root_node)
	var err := ResourceSaver.save(scene, OUT)
	print("saved %s (%d pieces) %s" % [OUT, _count, "ok" if err == OK else "error %d" % err])
	quit()


# --- Layout -------------------------------------------------------------------------

func _build_layout() -> void:
	# Floors.
	for x in range(0, 13):
		for z in range(0, 3):
			floor_tile(x, z, "Platform_Metal" if (x + z) % 2 == 0 else "Platform_Metal2")
	for x in range(0, 2):
		for z in range(3, 10):
			floor_tile(x, z, "Platform_DarkPlates")
	for x in range(2, 14):
		for z in range(3, 9):
			floor_tile(x, z, "Platform_Squares" if (x + z) % 3 else "Platform_CenterPlate")
	for x in range(6, 10):
		for z in range(9, 12):
			floor_tile(x, z, "Platform_DarkPlates")

	# North hall walls: windows along the back, solid sides, two doors south.
	for x in range(0, 13):
		wall(x, 0, "N", "WallWindow_Straight", true)
	for z in range(0, 3):
		wall(0, z, "W", "WallAstra_Straight", true)
		wall(12, z, "E", "WallAstra_Straight", true)
	for x in range(2, 13):
		if x == 5 or x == 10:
			door(x, 2, "S")
		else:
			wall(x, 2, "S", "WallBand_Straight")
	for p in [Vector2(3, 1), Vector2(6, 1), Vector2(9, 1)]:
		piece("Columns/Column_Large_Straight", tile(p.x, p.y) + Vector3(2, 0, 0), 0.0)

	# West corridor: long walls with two doorways into the courtyard.
	for z in range(3, 10):
		wall(0, z, "W", "WallAstra_Straight", true)
		if z == 4 or z == 7:
			door(1, z, "E")
		else:
			wall(1, z, "E", "WallBand_Straight")
	for x in range(0, 2):
		wall(x, 9, "S", "WallAstra_Straight", true)

	# Courtyard outer walls.
	for z in range(3, 9):
		wall(13, z, "E", "WallAstra_Straight", true)
	wall(13, 3, "N", "WallAstra_Straight", true)
	for x in [2, 3, 4, 5, 10, 11, 12, 13]:
		wall(x, 8, "S", "WallAstra_Straight", true)

	# Spawn room: one wide (8 m) opening north into the courtyard.
	for x in range(6, 10):
		if x == 6 or x == 9:
			wall(x, 9, "N", "WallBand_Straight")
		wall(x, 11, "S", "WallAstra_Straight", true)
	for z in range(9, 12):
		wall(6, z, "W", "WallAstra_Straight", true)
		wall(9, z, "E", "WallAstra_Straight", true)
	prop("Props/Prop_Computer", tile(6, 11) + Vector3(-1.0, 0, 1.2), 180.0)
	prop("Props/Prop_Chest", tile(9, 11) + Vector3(0.8, 0, 1.3), 180.0)
	prop("Props/Prop_Light_Floor", tile(7.5, 11) + Vector3(0, 0, 1.4), 0.0)

	# Raised platforms (2 m) with stairs, facades and rails.
	raised_platform(2, 3, 3, 4) # west, by the corridor and hall doors
	stairs(4, 3, "E") # up from the courtyard
	raised_platform(12, 6, 13, 8) # east, high ground over the spawn doors
	stairs(11, 6, "W")
	stairs(12, 5, "N")

	# Courtyard cover.
	low_wall(6, 4, "N"); low_wall(7, 4, "N") # a long barrier in the middle
	low_wall(9, 6, "W"); low_wall(9, 7, "W")
	low_wall(4, 7, "N")
	for c in [Vector3(5.5, 0, 5.2), Vector3(8.0, 0, 3.4), Vector3(10.6, 0, 4.6), Vector3(3.3, 0, 6.3), Vector3(11.2, 0, 7.8), Vector3(7.2, 0, 7.4)]:
		crate_stack(tile(c.x, c.z))
	for b in [Vector3(6.2, 0, 6.6), Vector3(10.2, 0, 3.2), Vector3(2.6, 0, 7.6)]:
		prop("Props/Prop_Barrel_Large", tile(b.x, b.z), randf() * 360.0, true)
	for l in [Vector3(4.5, 0, 5.5), Vector3(10.5, 0, 5.5)]:
		prop("Props/Prop_Light_Floor", tile(l.x, l.z), 90.0)

	# North hall clutter.
	crate_stack(tile(2, 1)); crate_stack(tile(8, 0.4)); crate_stack(tile(11, 1.6))
	prop("Props/Prop_Chest", tile(4.5, 0.2), 0.0, true)
	prop("Props/Prop_Computer", tile(7.5, 0) + Vector3(0, 0, -1.3), 0.0)
	# Enemy spawn points for waves (WaveManager). `kinds` limits which enemies use one.
	spawn_point(0.5, 3.5, "swarm")
	spawn_point(1.0, 8.4, "swarm")
	spawn_point(2.0, 1.0, "swarm soldier")
	spawn_point(7.5, 1.0, "tank soldier")
	spawn_point(11.2, 1.0, "swarm soldier")
	spawn_point(12.5, 3.6, "swarm soldier")
	spawn_point(12.5, 7.0, "soldier", PLATFORM_HEIGHT)
	spawn_point(2.5, 3.5, "soldier", PLATFORM_HEIGHT)
	# West corridor clutter (cover for the swarm to come around).
	crate_stack(tile(0.6, 5.5)); prop("Props/Prop_Barrel_Large", tile(1.3, 8.4), 0.0, true)


# --- Large arena (24 x 16 tiles, two levels) -----------------------------------------
# Ground:  spawn room (south, x 10-13 z 13-15), atrium (x 6-17 z 5-12) with a 4 m central
#          tower, north hall (x 6-17 z 0-4), east hallway (x 18-19) with three side rooms
#          (x 20-23).
# Upper (4 m): the west wing (x 0-5, the whole depth) with two rooms and an open hallway
#          overlooking the atrium; two staircases with landings; a bridge from the wing's
#          north landing to the top of the central tower.

const UPPER := 4.0


func open_edge(x: int, z: int, edge: String) -> void:
	# No rail on this edge (where a landing, bridge or stairs joins).
	_stair_tops["%d,%d,%s" % [x, z, edge]] = true


func _build_large_layout() -> void:
	# Ground floors.
	for x in range(6, 18):
		for z in range(0, 5):
			floor_tile(x, z, "Platform_Metal" if (x + z) % 2 == 0 else "Platform_Metal2")
		for z in range(5, 13):
			floor_tile(x, z, "Platform_Squares" if (x + z) % 3 else "Platform_CenterPlate")
	for x in range(18, 20):
		for z in range(0, 16):
			floor_tile(x, z, "Platform_DarkPlates")
	for x in range(20, 24):
		for z in [0, 1, 2, 3, 4, 6, 7, 8, 9, 11, 12, 13, 14, 15]:
			floor_tile(x, z, "Platform_Simple2")
	for x in range(10, 14):
		for z in range(13, 16):
			floor_tile(x, z, "Platform_DarkPlates")

	# Outer walls (ground).
	for x in range(6, 24):
		wall(x, 0, "N", "WallWindow_Straight" if x < 18 else "WallAstra_Straight", true)
	for z in [0, 1, 2, 3, 4, 6, 7, 8, 9, 11, 12, 13, 14, 15]:
		wall(23, z, "E", "WallAstra_Straight", true)
	for x in [10, 11, 12, 13, 18, 19, 20, 21, 22, 23]:
		wall(x, 15, "S", "WallAstra_Straight", true)
	for x in [6, 7, 8, 9, 14, 15, 16, 17]:
		wall(x, 12, "S", "WallAstra_Straight", true)
	for z in range(13, 16):
		wall(10, z, "W", "WallAstra_Straight", true)
		wall(13, z, "E", "WallAstra_Straight", true)
		wall(18, z, "W", "WallAstra_Straight", true)
	wall(10, 13, "N", "WallBand_Straight")
	wall(13, 13, "N", "WallBand_Straight") # the spawn room opens north between these

	# North hall: three doorways into the atrium, one into the east hallway.
	for x in range(6, 18):
		if x in [8, 11, 15]:
			door(x, 4, "S")
		else:
			wall(x, 4, "S", "WallBand_Straight")
	for z in range(0, 5):
		if z == 2:
			door(17, z, "E")
		else:
			wall(17, z, "E", "WallAstra_Straight")
	for p in [Vector2(9, 2), Vector2(13, 2)]:
		piece("Columns/Column_Large_Straight", tile(p.x, p.y) + Vector3(2, 0, 0), 0.0)

	# Atrium east wall: two doorways into the hallway.
	for z in range(5, 13):
		if z == 7 or z == 10:
			door(17, z, "E")
		else:
			wall(17, z, "E", "WallAstra_Straight")
	# East hallway: doorways into each side room.
	for z in range(0, 16):
		if z in [2, 7, 13]:
			door(19, z, "E")
		else:
			wall(19, z, "E", "WallBand_Straight")
	for x in range(20, 24):
		wall(x, 4, "S", "WallAstra_Straight")
		wall(x, 6, "N", "WallAstra_Straight")
		wall(x, 9, "S", "WallAstra_Straight")
		wall(x, 11, "N", "WallAstra_Straight")

	# West wing: a solid 4 m block, its top is the upper floor.
	raised_platform(0, 0, 5, 15, UPPER, false)
	for z in range(0, 13):
		if z != 8 and z != 9:
			_rails.append([5, z, "E", UPPER]) # overlook rail (gaps where the landings join)
	for z in range(0, 16):
		wall(0, z, "W", "WallAstra_Straight", true, UPPER)
	for x in range(0, 6):
		wall(x, 0, "N", "WallWindow_Straight", true, UPPER)
		wall(x, 15, "S", "WallAstra_Straight", true, UPPER)
	for z in range(13, 16):
		wall(5, z, "E", "WallAstra_Straight", true, UPPER) # the wing's south end is closed
	# Upper rooms: U1 (north) and U2 (south), with the open hallway (x 4-5) between them
	# and the atrium.
	for z in range(0, 6):
		if z == 2:
			door(3, z, "E", UPPER)
		else:
			wall(3, z, "E", "WallBand_Straight", false, UPPER)
	for x in range(0, 4):
		if x == 1:
			door(x, 5, "S", UPPER)
		else:
			wall(x, 5, "S", "WallBand_Straight", false, UPPER)
	for x in range(0, 4):
		if x == 2:
			door(x, 10, "N", UPPER)
		else:
			wall(x, 10, "N", "WallBand_Straight", false, UPPER)
	for z in range(10, 16):
		if z == 13:
			door(3, z, "E", UPPER)
		else:
			wall(3, z, "E", "WallBand_Straight", false, UPPER)

	# Staircases up to the wing (two flights each, on a 2 m support), with landings.
	stairs(6, 11, "N") # you walk on from the atrium floor in front of it (tile 6, 12)
	_block(6, 10, 6, 10, 2.0)
	stairs(6, 10, "N", 2.0)
	raised_platform(6, 9, 6, 9, UPPER)
	open_edge(6, 9, "W"); open_edge(6, 9, "N")
	stairs(6, 6, "S")
	_block(6, 7, 6, 7, 2.0)
	stairs(6, 7, "S", 2.0)
	raised_platform(6, 8, 6, 8, UPPER)
	open_edge(6, 8, "W"); open_edge(6, 8, "E"); open_edge(6, 8, "S") # joins the south landing

	# Central tower (4 m) with stairs up both sides, and the bridge from the north landing.
	raised_platform(10, 8, 13, 9, UPPER)
	stairs(11, 11, "N")
	_block(11, 10, 11, 10, 2.0)
	stairs(11, 10, "N", 2.0)
	stairs(12, 6, "S")
	_block(12, 7, 12, 7, 2.0)
	stairs(12, 7, "S", 2.0)
	bridge(7, 9, 8, UPPER)
	open_edge(10, 8, "W")

	# Cover.
	for c in [Vector2(8, 6.3), Vector2(15.5, 6), Vector2(8.6, 11), Vector2(15, 11.2), Vector2(14.6, 8.6), Vector2(8.2, 9)]:
		crate_stack(tile(c.x, c.y))
	low_wall(9, 6, "N"); low_wall(14, 10, "S"); low_wall(16, 8, "W"); low_wall(7, 11, "E")
	for b in [Vector2(10, 6.5), Vector2(13.5, 11.6), Vector2(16.4, 5.4)]:
		prop("Props/Prop_Barrel_Large", tile(b.x, b.y), randf() * 360.0, true)
	for l in [Vector2(9, 7.5), Vector2(15, 7.5), Vector2(11.5, 12.5)]:
		prop("Props/Prop_Light_Floor", tile(l.x, l.y), 90.0)
	crate_stack(tile(7, 1)); crate_stack(tile(11.5, 0.5)); crate_stack(tile(16, 3))
	prop("Props/Prop_Chest", tile(14, 0.3), 0.0, true)
	crate_stack(tile(18.6, 4.5)); crate_stack(tile(19.2, 11))
	crate_stack(tile(21, 2.5)); crate_stack(tile(22.5, 8)); crate_stack(tile(21.5, 13.5))
	prop("Props/Prop_Computer", tile(23, 7.5) + Vector3(1.3, 0, 0), 270.0)
	crate_stack(tile(1.5, 2.5, UPPER)); crate_stack(tile(2, 12.5, UPPER)); crate_stack(tile(4.6, 6, UPPER))
	prop("Props/Prop_Chest", tile(1, 14.5, UPPER), 0.0, true)
	prop("Props/Prop_Computer", tile(11, 15) + Vector3(-1.0, 0, 1.2), 180.0)

	# Enemy spawn points for waves.
	spawn_point(8.5, 2, "tank soldier")
	spawn_point(15, 2, "tank soldier swarm")
	spawn_point(18.5, 0.6, "swarm")
	spawn_point(21.5, 2, "swarm soldier")
	spawn_point(21.5, 7.5, "swarm soldier")
	spawn_point(21.5, 13.5, "swarm soldier")
	spawn_point(1.5, 2.5, "soldier swarm", UPPER)
	spawn_point(1.5, 12.5, "soldier swarm", UPPER)
	spawn_point(11.5, 8.5, "soldier", UPPER)


# --- Piece helpers -------------------------------------------------------------------

func piece(path: String, at: Vector3, yaw_degrees: float, cover := false, collide := true) -> Node3D:
	var holder := Node3D.new()
	holder.name = "%s_%d" % [path.get_file(), _count]
	_count += 1
	root_node.add_child(holder)
	holder.position = at
	holder.rotation_degrees.y = yaw_degrees
	var scene: PackedScene = load(KIT + path + ".gltf")
	var visual := scene.instantiate()
	holder.add_child(visual)
	if not collide:
		return holder
	var body := StaticBody3D.new()
	body.name = "Collision"
	holder.add_child(body)
	for entry in _collision_for(path, visual):
		var shape := CollisionShape3D.new()
		shape.shape = entry[0]
		shape.transform = entry[1]
		body.add_child(shape)
	if cover:
		holder.add_to_group("cover", true)
	return holder


func _collision_for(path: String, visual: Node) -> Array:
	if _shapes.has(path):
		return _shapes[path]
	var list := []
	for mesh_node in visual.find_children("*", "MeshInstance3D", true, false):
		var mi := mesh_node as MeshInstance3D
		var shape := mi.mesh.create_trimesh_shape()
		shape.backface_collision = true # thin pieces (plates, short walls) block from both sides
		list.append([shape, (visual as Node3D).transform * _relative(mi, visual)])
	_shapes[path] = list
	return list


func _relative(node: Node3D, ancestor: Node) -> Transform3D:
	var xf := Transform3D.IDENTITY
	var n: Node = node
	while n != ancestor and n is Node3D:
		xf = (n as Node3D).transform * xf
		n = n.get_parent()
	return xf


func spawn_point(x: float, z: float, kinds: String, y := 0.0) -> void:
	var marker := Marker3D.new()
	marker.name = "EnemySpawn_%d" % _count
	_count += 1
	marker.position = tile(x, z, y + 0.1)
	marker.set_meta("kinds", kinds)
	marker.add_to_group("enemy_spawn", true)
	root_node.add_child(marker)


func floor_tile(x: float, z: float, kind: String, y := 0.0) -> void:
	piece("Platforms/" + kind, tile(x, z, y), 0.0)


const EDGE_YAW := {"W": 0.0, "S": 90.0, "E": 180.0, "N": 270.0}
const EDGE_DIR := {"W": Vector3(-1, 0, 0), "E": Vector3(1, 0, 0), "N": Vector3(0, 0, -1), "S": Vector3(0, 0, 1)}


func wall(x: int, z: int, edge: String, kind: String, tall := false, y := 0.0) -> void:
	# Wall pieces sit on a tile's -X edge by default; turn them onto the edge we want.
	piece("Walls/" + kind, tile(x, z, y), EDGE_YAW[edge])
	if tall:
		piece("Walls/TopSimple_Straight", tile(x, z, y), EDGE_YAW[edge])
	_wall_edges[_edge_key(x, z, edge, y)] = true


func door(x: int, z: int, edge: String, y := 0.0) -> void:
	# Door frames run along X through their origin, so move them onto the edge.
	var yaw := 0.0 if edge in ["N", "S"] else 90.0
	piece("Platforms/Door_Frame_Square", tile(x, z, y) + EDGE_DIR[edge] * TILE * 0.5, yaw)
	_wall_edges[_edge_key(x, z, edge, y)] = true


func low_wall(x: int, z: int, edge: String, y := 0.0) -> void:
	# A 2 m wall on a tile edge: cover you can shoot over by jumping, or mantle.
	var holder := piece("Walls/ShortWall_Metal2_Straight", tile(x, z, y), EDGE_YAW[edge], true)
	holder.position += EDGE_DIR[edge] * 0.02


func prop(path: String, at: Vector3, yaw: float, cover := false) -> void:
	piece(path, at, yaw, cover)


func crate_stack(at: Vector3) -> void:
	# One to three crates, as cover.
	var holder := piece("Props/Prop_Crate4", at + Vector3(0, 0.56, 0), randf() * 30.0, true)
	if randf() < 0.6:
		piece("Props/Prop_Crate3", at + Vector3(randf_range(-0.2, 0.2), 1.62, randf_range(-0.2, 0.2)), randf() * 90.0)
	if randf() < 0.5:
		piece("Props/Prop_Crate3", at + Vector3(1.1, 0.5, randf_range(-0.3, 0.3)), randf() * 90.0)


func raised_platform(x0: int, z0: int, x1: int, z1: int, height := PLATFORM_HEIGHT, rails := true, base := 0.0) -> void:
	# A solid block (2 m by default) with metal plates on top, short-wall facades and
	# rails on its edges (except where stairs climb onto it). `base` raises the bottom.
	_block(x0, z0, x1, z1, height, base)
	for x in range(x0, x1 + 1):
		for z in range(z0, z1 + 1):
			floor_tile(x, z, "Platform_Metal", base + height + 0.01)
	for x in range(x0, x1 + 1):
		_platform_edge(x, z0, "N", base, height, rails)
		_platform_edge(x, z1, "S", base, height, rails)
	for z in range(z0, z1 + 1):
		_platform_edge(x0, z, "W", base, height, rails)
		_platform_edge(x1, z, "E", base, height, rails)


func _block(x0: int, z0: int, x1: int, z1: int, height: float, base := 0.0) -> void:
	# Just the solid block (supports, stair landings).
	var size := Vector3((x1 - x0 + 1) * TILE, height, (z1 - z0 + 1) * TILE)
	var center := (tile(x0, z0) + tile(x1, z1)) * 0.5
	var block := CSGBox3D.new()
	block.name = "PlatformBlock_%d" % _count
	_count += 1
	block.size = size
	block.position = center + Vector3.UP * (base + height * 0.5)
	block.use_collision = true
	var mat := StandardMaterial3D.new()
	mat.albedo_color = PLATFORM_COLOR
	block.material = mat
	root_node.add_child(block)


func _platform_edge(x: int, z: int, edge: String, base := 0.0, height := PLATFORM_HEIGHT, rails := true) -> void:
	# Short-wall facades up the side (one per 2 m), and a rail along the top.
	var level := 0.0
	while level < height - 0.01:
		var h := piece("Walls/ShortWall_Metal2_Straight", tile(x, z, base + level), EDGE_YAW[edge])
		h.position += EDGE_DIR[edge] * 0.03
		level += 2.0
	if rails:
		_rails.append([x, z, edge, base + height])


func _place_rails() -> void:
	for r in _rails:
		if not _stair_tops.has("%d,%d,%s" % [r[0], r[1], r[2]]):
			_rail(r[0], r[1], r[2], r[3])


func _rail(x: int, z: int, edge: String, y: float) -> void:
	var rail := piece("Props/Prop_Rail_4", tile(x, z, y) + EDGE_DIR[edge] * (TILE * 0.5 - 0.15), EDGE_YAW[edge] + 180.0)
	# Rails run along -Z from their origin; shift so one spans the tile edge.
	var along := Vector3(0, 0, 1).rotated(Vector3.UP, deg_to_rad(EDGE_YAW[edge] + 180.0))
	rail.position += along * (TILE * 0.5 - 0.04)


const OPPOSITE := {"N": "S", "S": "N", "E": "W", "W": "E"}


func stairs(x: int, z: int, toward: String, y := 0.0) -> void:
	# Stairs in tile (x, z) climbing 2 m toward the platform on its `toward` side,
	# starting at height y. The kit's stairs are high at their origin and run down along +Z.
	var high_edge: Vector3 = tile(x, z, y) + EDGE_DIR[toward] * TILE * 0.5
	var down: Vector3 = -EDGE_DIR[toward]
	var yaw := rad_to_deg(atan2(down.x, down.z))
	var holder := piece("Platforms/Platform_Stairs_4Wide", high_edge, yaw, false, false)
	# Collision is a smooth ramp, not the steps: the player can walk up a slope but has no
	# step-up for 25 cm stairs.
	var body := StaticBody3D.new()
	body.name = "RampCollision"
	holder.add_child(body)
	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	var run := TILE
	var rise := 2.0
	box.size = Vector3(TILE, 0.2, sqrt(run * run + rise * rise))
	shape.shape = box
	# In the stairs' own space the top is at z = 0, y = 2 and the bottom at z = 4, y = 0.
	shape.transform = Transform3D(Basis(Vector3.RIGHT, atan2(rise, run)), Vector3(0, rise * 0.5 - 0.1, run * 0.5))
	body.add_child(shape)
	var d: Vector3 = EDGE_DIR[toward]
	_stair_tops["%d,%d,%s" % [x + int(d.x), z + int(d.z), OPPOSITE[toward]]] = true # no rail across the top
	_stair_tops["%d,%d,%s" % [x, z, toward]] = true


func bridge(x0: int, x1: int, z: int, y: float) -> void:
	# A catwalk along x at height y, one tile wide: plates on a thin slab, rails both sides.
	_block(x0, z, x1, z, 0.25, y - 0.25)
	for x in range(x0, x1 + 1):
		floor_tile(x, z, "Platform_Metal2", y + 0.01)
		_rails.append([x, z, "N", y])
		_rails.append([x, z, "S", y])


# --- Finishing ------------------------------------------------------------------------

func _edge_key(x: int, z: int, edge: String, y := 0.0) -> String:
	return "%d,%d,%s,%.1f" % [x, z, edge, y]


func _corner_fillers() -> void:
	# Where two walls meet at a right angle there's a small gap; fill it with a post.
	var corners := {}
	for key in _wall_edges:
		var parts: PackedStringArray = key.split(",")
		var x := int(parts[0]); var z := int(parts[1]); var edge := parts[2]; var y := float(parts[3])
		var c := tile(x, z, y)
		var ends := []
		match edge:
			"W", "E":
				var ex := c.x + (TILE * 0.5 if edge == "E" else -TILE * 0.5)
				ends = [Vector2(ex, c.z - TILE * 0.5), Vector2(ex, c.z + TILE * 0.5)]
			"N", "S":
				var ez := c.z + (TILE * 0.5 if edge == "S" else -TILE * 0.5)
				ends = [Vector2(c.x - TILE * 0.5, ez), Vector2(c.x + TILE * 0.5, ez)]
		for e in ends:
			var k := "%.1f,%.1f,%.1f" % [e.x, e.y, y]
			if not corners.has(k):
				corners[k] = {}
			corners[k][edge in ["W", "E"]] = true
	var mat := StandardMaterial3D.new()
	mat.albedo_color = FILLER_COLOR
	for k in corners:
		if corners[k].size() < 2:
			continue # a straight run, not a corner
		var xy: PackedStringArray = k.split(",")
		var post := CSGBox3D.new()
		post.size = Vector3(1.3, 5.0, 1.3)
		post.position = Vector3(float(xy[0]), float(xy[2]) + 2.5, float(xy[1]))
		post.use_collision = true
		post.material = mat
		root_node.add_child(post)


func _boundary() -> void:
	# Tall invisible walls around the whole map, so double jumps and mantles can't get
	# you over the outer walls.
	var size := Vector3(W * TILE, 0, D * TILE) # W and D are set per map
	var body := StaticBody3D.new()
	body.name = "Boundary"
	root_node.add_child(body)
	for side in [[Vector3(0, 10, -size.z * 0.5 - 1.5), Vector3(size.x + 6, 20, 1)], [Vector3(0, 10, size.z * 0.5 + 1.5), Vector3(size.x + 6, 20, 1)],
			[Vector3(-size.x * 0.5 - 1.5, 10, 0), Vector3(1, 20, size.z + 6)], [Vector3(size.x * 0.5 + 1.5, 10, 0), Vector3(1, 20, size.z + 6)]]:
		var shape := CollisionShape3D.new()
		var box := BoxShape3D.new()
		box.size = side[1]
		shape.shape = box
		shape.position = side[0]
		body.add_child(shape)
	# A big floor under everything, in case something falls through a gap.
	var floor_body := StaticBody3D.new()
	floor_body.name = "SafetyFloor"
	root_node.add_child(floor_body)
	var fs := CollisionShape3D.new()
	var fb := BoxShape3D.new()
	fb.size = Vector3(size.x + 6, 1, size.z + 6)
	fs.shape = fb
	fs.position = Vector3(0, -0.52, 0)
	floor_body.add_child(fs)


func _bake_navigation() -> void:
	var region := NavigationRegion3D.new()
	region.name = "Navigation"
	var navmesh := NavigationMesh.new()
	navmesh.agent_radius = 0.5
	navmesh.agent_height = 1.8
	navmesh.agent_max_climb = 0.5
	navmesh.agent_max_slope = 46.0
	navmesh.cell_size = 0.25
	navmesh.cell_height = 0.25 # matches the project default
	navmesh.geometry_parsed_geometry_type = NavigationMesh.PARSED_GEOMETRY_STATIC_COLLIDERS
	navmesh.geometry_source_geometry_mode = NavigationMesh.SOURCE_GEOMETRY_ROOT_NODE_CHILDREN
	region.navigation_mesh = navmesh
	root_node.add_child(region)
	var source := NavigationMeshSourceGeometryData3D.new()
	NavigationServer3D.parse_source_geometry_data(navmesh, source, root_node)
	NavigationServer3D.bake_from_source_geometry_data(navmesh, source)
	print("navmesh: %d polygons" % navmesh.get_polygon_count())


func _set_owner(node: Node, owner_node: Node) -> void:
	for child in node.get_children():
		child.owner = owner_node
		if child.scene_file_path == "":
			_set_owner(child, owner_node) # don't descend into instanced kit scenes
