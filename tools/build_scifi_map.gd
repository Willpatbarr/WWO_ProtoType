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
const W := 16
const D := 12
const OUT := "res://maps/scifi_arena_map.tscn"
const PLATFORM_HEIGHT := 2.0
const FILLER_COLOR := Color(0.2, 0.22, 0.26)
const PLATFORM_COLOR := Color(0.24, 0.26, 0.3)

var root_node: Node3D
var _shapes := {} # piece path -> Array of [Shape3D, Transform3D] (shared by every copy)
var _wall_edges := {} # "x,z,edge" -> true, for corner fillers
var _count := 0


func tile(x: float, z: float, y := 0.0) -> Vector3:
	return Vector3((x - (W - 1) * 0.5) * TILE, y, (z - (D - 1) * 0.5) * TILE)


func _initialize() -> void:
	root_node = Node3D.new()
	root_node.name = "SciFiArena"
	root.add_child(root_node)
	_build_layout()
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
	# West corridor clutter (cover for the swarm to come around).
	crate_stack(tile(0.6, 5.5)); prop("Props/Prop_Barrel_Large", tile(1.3, 8.4), 0.0, true)


# --- Piece helpers -------------------------------------------------------------------

func piece(path: String, at: Vector3, yaw_degrees: float, cover := false) -> Node3D:
	var holder := Node3D.new()
	holder.name = "%s_%d" % [path.get_file(), _count]
	_count += 1
	root_node.add_child(holder)
	holder.position = at
	holder.rotation_degrees.y = yaw_degrees
	var scene: PackedScene = load(KIT + path + ".gltf")
	var visual := scene.instantiate()
	holder.add_child(visual)
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


func floor_tile(x: float, z: float, kind: String, y := 0.0) -> void:
	piece("Platforms/" + kind, tile(x, z, y), 0.0)


const EDGE_YAW := {"W": 0.0, "S": 90.0, "E": 180.0, "N": 270.0}
const EDGE_DIR := {"W": Vector3(-1, 0, 0), "E": Vector3(1, 0, 0), "N": Vector3(0, 0, -1), "S": Vector3(0, 0, 1)}


func wall(x: int, z: int, edge: String, kind: String, tall := false) -> void:
	# Wall pieces sit on a tile's -X edge by default; turn them onto the edge we want.
	piece("Walls/" + kind, tile(x, z), EDGE_YAW[edge])
	if tall:
		piece("Walls/TopSimple_Straight", tile(x, z), EDGE_YAW[edge])
	_wall_edges[_edge_key(x, z, edge)] = true


func door(x: int, z: int, edge: String) -> void:
	# Door frames run along X through their origin, so move them onto the edge.
	var yaw := 0.0 if edge in ["N", "S"] else 90.0
	piece("Platforms/Door_Frame_Square", tile(x, z) + EDGE_DIR[edge] * TILE * 0.5, yaw)
	_wall_edges[_edge_key(x, z, edge)] = true


func low_wall(x: int, z: int, edge: String) -> void:
	# A 2 m wall on a tile edge: cover you can shoot over by jumping, or mantle.
	var holder := piece("Walls/ShortWall_Metal2_Straight", tile(x, z), EDGE_YAW[edge], true)
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


func raised_platform(x0: int, z0: int, x1: int, z1: int) -> void:
	# A solid 2 m block with metal plates on top, short-wall facades and rails on the
	# open edges.
	var size := Vector3((x1 - x0 + 1) * TILE, PLATFORM_HEIGHT, (z1 - z0 + 1) * TILE)
	var center := (tile(x0, z0) + tile(x1, z1)) * 0.5
	var block := CSGBox3D.new()
	block.name = "PlatformBlock_%d" % _count
	block.size = size
	block.position = center + Vector3.UP * PLATFORM_HEIGHT * 0.5
	block.use_collision = true
	var mat := StandardMaterial3D.new()
	mat.albedo_color = PLATFORM_COLOR
	block.material = mat
	root_node.add_child(block)
	for x in range(x0, x1 + 1):
		for z in range(z0, z1 + 1):
			floor_tile(x, z, "Platform_Metal", PLATFORM_HEIGHT + 0.01)
	for x in range(x0, x1 + 1):
		_platform_edge(x, z0, "N")
		_platform_edge(x, z1, "S")
	for z in range(z0, z1 + 1):
		_platform_edge(x0, z, "W")
		_platform_edge(x1, z, "E")


func _platform_edge(x: int, z: int, edge: String) -> void:
	var h := piece("Walls/ShortWall_Metal2_Straight", tile(x, z), EDGE_YAW[edge])
	h.position += EDGE_DIR[edge] * 0.03
	var rail := piece("Props/Prop_Rail_4", tile(x, z, PLATFORM_HEIGHT) + EDGE_DIR[edge] * (TILE * 0.5 - 0.15), EDGE_YAW[edge] + 180.0)
	# Rails run along -Z from their origin; shift so one spans the tile edge.
	var along := Vector3(0, 0, 1).rotated(Vector3.UP, deg_to_rad(EDGE_YAW[edge] + 180.0))
	rail.position += along * (TILE * 0.5 - 0.04)


func stairs(x: int, z: int, toward: String) -> void:
	# Stairs in tile (x, z) climbing toward the platform on its `toward` side. The kit's
	# stairs are high at their origin and run down along +Z.
	var high_edge: Vector3 = tile(x, z) + EDGE_DIR[toward] * TILE * 0.5
	var down: Vector3 = -EDGE_DIR[toward]
	piece("Platforms/Platform_Stairs_4Wide", high_edge, rad_to_deg(atan2(down.x, down.z)))


# --- Finishing ------------------------------------------------------------------------

func _edge_key(x: int, z: int, edge: String) -> String:
	return "%d,%d,%s" % [x, z, edge]


func _corner_fillers() -> void:
	# Where two walls meet at a right angle there's a small gap; fill it with a post.
	var corners := {}
	for key in _wall_edges:
		var parts: PackedStringArray = key.split(",")
		var x := int(parts[0]); var z := int(parts[1]); var edge := parts[2]
		var c := tile(x, z)
		var ends := []
		match edge:
			"W", "E":
				var ex := c.x + (TILE * 0.5 if edge == "E" else -TILE * 0.5)
				ends = [Vector2(ex, c.z - TILE * 0.5), Vector2(ex, c.z + TILE * 0.5)]
			"N", "S":
				var ez := c.z + (TILE * 0.5 if edge == "S" else -TILE * 0.5)
				ends = [Vector2(c.x - TILE * 0.5, ez), Vector2(c.x + TILE * 0.5, ez)]
		for e in ends:
			var k := "%.1f,%.1f" % [e.x, e.y]
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
		post.position = Vector3(float(xy[0]), 2.5, float(xy[1]))
		post.use_collision = true
		post.material = mat
		root_node.add_child(post)


func _boundary() -> void:
	# Tall invisible walls around the whole map, so double jumps and mantles can't get
	# you over the outer walls.
	var size := Vector3(W * TILE, 0, D * TILE)
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
