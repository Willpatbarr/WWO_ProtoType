"""Builds the three levels (Godot: tools/build_scifi_map.gd + tools/build_arena.gd + main.tscn).

Run headless from the project folder after Tools/create_assets.py:
  UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script=Tools/build_maps.py -unattended -nullrhi

- L_Arena: the small arena, a grid of 4 m tiles, 16 wide by 12 deep: spawn room (south), a
  courtyard with cover and two 2 m raised platforms, a pillared north hall (the Tank), a west flank
  corridor (the Swarm pack). 8 enemy spawn points for the waves.
- L_ArenaLarge: 24 x 16 tiles, two levels: spawn room, atrium with a 4 m central tower, north hall,
  east hallway with three side rooms; upper west wing with two rooms and an overlook, two staircases,
  and a bridge to the tower. 9 spawn points.
- L_TestRange: the original gray-box range (no spawn points, so it stays a sandbox).
The arena pieces are gray-box blocks with the template's prototype grid materials (the Godot maps
used the Quaternius Sci-Fi kit). Stairs collide as smooth ramps, like the Godot ones.
Coordinates: a Godot point (x, y, z) in meters is Unreal (-z, x, y) * 100.
"""
import math
import random

import unreal

eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
mel = unreal.MaterialEditingLibrary

TILE = 4.0
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
CYLINDER = unreal.load_asset("/Engine/BasicShapes/Cylinder.Cylinder")
SPHERE = unreal.load_asset("/Engine/BasicShapes/Sphere.Sphere")
GRID = unreal.load_asset("/Game/LevelPrototyping/Materials/M_PrototypeGrid.M_PrototypeGrid")


def log(msg):
    unreal.log_warning("WWO: " + msg)




_materials = {}


def grid_material(name, background, grid=None):
    """A prototype grid material instance tinted with a Godot color."""
    if name in _materials:
        return _materials[name]
    path = "/Game/WWO/Materials/Map/MI_" + name
    if eal.does_asset_exist(path):
        mi = eal.load_asset(path)
    else:
        mi = tools.create_asset("MI_" + name, "/Game/WWO/Materials/Map", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", GRID)
    lin = lambda c: tuple(v ** 2.2 for v in c)  # Godot colors are sRGB
    background = lin(background)
    grid = lin(grid) if grid else None
    mel.set_material_instance_vector_parameter_value(mi, "SurfaceColor", unreal.LinearColor(*background, 1.0))
    if grid:
        mel.set_material_instance_vector_parameter_value(mi, "GridColor", unreal.LinearColor(*grid, 1.0))
    eal.save_loaded_asset(mi)
    _materials[name] = mi
    return mi


class Builder:
    def __init__(self, width, depth, seed):
        self.W = width
        self.D = depth
        self.rng = random.Random(seed)
        self.count = 0
        self.wall_edges = {}
        self.rails = []
        self.stair_tops = set()
        self.FLOOR = grid_material("Floor", (0.32, 0.33, 0.36))
        self.FLOOR_DARK = grid_material("FloorDark", (0.2, 0.21, 0.24))
        self.FLOOR_METAL = grid_material("FloorMetal", (0.4, 0.41, 0.44))
        self.WALL = grid_material("Wall", (0.5, 0.51, 0.54))
        self.WALL_BAND = grid_material("WallBand", (0.36, 0.38, 0.43))
        self.WALL_WINDOW = grid_material("WallWindow", (0.3, 0.38, 0.48))
        self.PLATFORM = grid_material("Platform", (0.24, 0.26, 0.3))
        self.FILLER = grid_material("Filler", (0.2, 0.22, 0.26))
        self.RAIL = grid_material("Rail", (0.12, 0.12, 0.14))
        self.CRATE = grid_material("Crate", (0.55, 0.42, 0.25))
        self.BARREL = grid_material("Barrel", (0.62, 0.32, 0.15))
        self.PROP = grid_material("Prop", (0.25, 0.3, 0.38))

    # --- Coordinates -------------------------------------------------------------------------

    def tile(self, x, z, y=0.0):
        """Center of Godot tile (x, z) at height y, in Unreal cm."""
        gx = (x - (self.W - 1) * 0.5) * TILE
        gz = (z - (self.D - 1) * 0.5) * TILE
        return unreal.Vector(-gz * 100.0, gx * 100.0, y * 100.0)

    # Godot edge -> Unreal direction (N = Godot -z = Unreal +x, W = Godot -x = Unreal -y).
    EDGE_DIR = {"N": (1, 0), "S": (-1, 0), "W": (0, -1), "E": (0, 1)}

    def edge_point(self, x, z, edge, y=0.0, inset=0.0):
        c = self.tile(x, z, y)
        dx, dy = self.EDGE_DIR[edge]
        d = (TILE * 0.5 - inset) * 100.0
        return unreal.Vector(c.x + dx * d, c.y + dy * d, c.z)

    # --- Pieces ------------------------------------------------------------------------------

    def box(self, center, size_m, material, label, tags=(), yaw=0.0, pitch=0.0, hidden=False, mesh=None):
        actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, center, unreal.Rotator(0.0, pitch, yaw))
        comp = actor.static_mesh_component
        comp.set_mobility(unreal.ComponentMobility.STATIC)
        comp.set_static_mesh(mesh or CUBE)
        if material:
            comp.set_material(0, material)
        actor.set_actor_scale3d(unreal.Vector(*size_m))
        actor.set_actor_label("%s_%d" % (label, self.count))
        self.count += 1
        if tags:
            actor.set_editor_property("tags", list(tags))
        if hidden:
            actor.set_actor_hidden_in_game(True)
            comp.set_editor_property("cast_shadow", False)
        return actor

    def edge_box(self, x, z, edge, y, length, height, thickness, material, label, tags=(), inset=0.0, z_base=0.0):
        p = self.edge_point(x, z, edge, y, inset)
        p.z += (z_base + height * 0.5) * 100.0
        along_y = edge in ("N", "S")
        size = (thickness, length, height) if along_y else (length, thickness, height)
        return self.box(p, size, material, label, tags)

    def floor_tile(self, x, z, kind="squares", y=0.0):
        material = {"metal": self.FLOOR_METAL, "dark": self.FLOOR_DARK}.get(kind, self.FLOOR)
        c = self.tile(x, z, y)
        c.z -= 10.0
        self.box(c, (TILE, TILE, 0.2), material, "Floor")

    def wall(self, x, z, edge, kind="astra", tall=False, y=0.0):
        material = {"band": self.WALL_BAND, "window": self.WALL_WINDOW}.get(kind, self.WALL)
        self.edge_box(x, z, edge, y, TILE, 5.0 if tall else 4.0, 0.3, material, "Wall")
        self.wall_edges[(x, z, edge, y)] = True

    def door(self, x, z, edge, y=0.0):
        # A doorway: 2.2 m wide, 3 m tall, in a 4 m wall.
        p = self.edge_point(x, z, edge, y)
        along_y = edge in ("N", "S")
        for side in (-1, 1):
            offset = side * (1.1 + 0.45) * 100.0
            c = unreal.Vector(p.x + (0 if along_y else offset), p.y + (offset if along_y else 0), p.z + 200.0)
            self.box(c, (0.3, 0.9, 4.0) if along_y else (0.9, 0.3, 4.0), self.WALL_BAND, "DoorSide")
        c = unreal.Vector(p.x, p.y, p.z + 350.0)
        self.box(c, (0.3, 2.2, 1.0) if along_y else (2.2, 0.3, 1.0), self.WALL_BAND, "DoorTop")
        self.wall_edges[(x, z, edge, y)] = True

    def low_wall(self, x, z, edge, y=0.0):
        # A 2 m wall on a tile edge: cover you can shoot over by jumping, or mantle.
        self.edge_box(x, z, edge, y, TILE, 2.0, 0.3, self.WALL_BAND, "LowWall", tags=("Cover",), inset=-0.02)

    def block(self, x0, z0, x1, z1, height, base=0.0, material=None):
        a = self.tile(x0, z0)
        b = self.tile(x1, z1)
        center = unreal.Vector((a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (base + height * 0.5) * 100.0)
        size = ((z1 - z0 + 1) * TILE, (x1 - x0 + 1) * TILE, height)
        self.box(center, size, material or self.PLATFORM, "Block")

    def raised_platform(self, x0, z0, x1, z1, height=2.0, rails=True, base=0.0):
        self.block(x0, z0, x1, z1, height, base)
        if not rails:
            return
        for x in range(x0, x1 + 1):
            self.rails.append((x, z0, "N", base + height))
            self.rails.append((x, z1, "S", base + height))
        for z in range(z0, z1 + 1):
            self.rails.append((x0, z, "W", base + height))
            self.rails.append((x1, z, "E", base + height))

    def open_edge(self, x, z, edge):
        self.stair_tops.add((x, z, edge))

    OPPOSITE = {"N": "S", "S": "N", "E": "W", "W": "E"}
    EDGE_YAW = {"N": 0.0, "S": 180.0, "E": 90.0, "W": -90.0}

    def stairs(self, x, z, toward, y=0.0):
        # A 2 m climb across one tile toward `toward`, colliding as a smooth ramp.
        c = self.tile(x, z, y)
        run, rise = TILE, 2.0
        length = math.sqrt(run * run + rise * rise)
        c.z += rise * 50.0 - 10.0
        self.box(c, (length, TILE, 0.2), self.FLOOR_METAL, "Stairs", yaw=self.EDGE_YAW[toward], pitch=math.degrees(math.atan2(rise, run)))
        dx, dz = {"N": (0, -1), "S": (0, 1), "E": (1, 0), "W": (-1, 0)}[toward]
        self.stair_tops.add((x + dx, z + dz, self.OPPOSITE[toward]))  # no rail across the top
        self.stair_tops.add((x, z, toward))

    def bridge(self, x0, x1, z, y):
        self.block(x0, z, x1, z, 0.25, y - 0.25)
        for x in range(x0, x1 + 1):
            self.rails.append((x, z, "N", y))
            self.rails.append((x, z, "S", y))

    def place_rails(self):
        for x, z, edge, y in self.rails:
            if (x, z, edge) not in self.stair_tops:
                self.edge_box(x, z, edge, y, TILE, 1.0, 0.1, self.RAIL, "Rail", inset=0.15)

    def column(self, center):
        center.z += 250.0
        self.box(center, (1.2, 1.2, 5.0), self.WALL_BAND, "Column", mesh=CYLINDER)

    def crate_stack(self, at):
        # One to three crates, as cover.
        c = unreal.Vector(at.x, at.y, at.z + 56.0)
        self.box(c, (1.12, 1.12, 1.12), self.CRATE, "Crate", tags=("Cover",), yaw=self.rng.uniform(0, 30))
        if self.rng.random() < 0.6:
            self.box(unreal.Vector(at.x + self.rng.uniform(-20, 20), at.y + self.rng.uniform(-20, 20), at.z + 157.0), (0.9, 0.9, 0.9), self.CRATE, "Crate",
                     yaw=self.rng.uniform(0, 90))
        if self.rng.random() < 0.5:
            self.box(unreal.Vector(at.x + self.rng.uniform(-30, 30), at.y + 110.0, at.z + 45.0), (0.9, 0.9, 0.9), self.CRATE, "Crate", yaw=self.rng.uniform(0, 90))

    def barrel(self, at):
        self.box(unreal.Vector(at.x, at.y, at.z + 60.0), (1.0, 1.0, 1.2), self.BARREL, "Barrel", tags=("Cover",), mesh=CYLINDER)

    def prop(self, at, size, label):
        self.box(unreal.Vector(at.x, at.y, at.z + size[2] * 50.0), size, self.PROP, label)

    def floor_light(self, at):
        self.box(unreal.Vector(at.x, at.y, at.z + 15.0), (0.3, 0.3, 0.3), self.PROP, "FloorLight", mesh=CYLINDER)
        light = actors.spawn_actor_from_class(unreal.PointLight, unreal.Vector(at.x, at.y, at.z + 60.0))
        lc = light.point_light_component
        lc.set_editor_property("intensity", 3000.0)
        lc.set_editor_property("attenuation_radius", 900.0)
        lc.set_light_color(unreal.LinearColor(0.75, 0.85, 1.0, 1.0))

    def spawn_point(self, x, z, kinds, y=0.0):
        p = self.tile(x, z, y + 0.1)
        point = actors.spawn_actor_from_class(unreal.TargetPoint, p)
        point.set_editor_property("tags", ["EnemySpawn"] + kinds.split())
        point.set_actor_label("EnemySpawn_%d" % self.count)
        self.count += 1

    def corner_fillers(self):
        # Where two walls meet at a right angle there's a small gap; fill it with a post.
        corners = {}
        for (x, z, edge, y) in self.wall_edges:
            c = self.tile(x, z, y)
            h = TILE * 50.0
            if edge in ("N", "S"):
                ex = c.x + (h if edge == "N" else -h)
                ends = [(ex, c.y - h), (ex, c.y + h)]
            else:
                ey = c.y + (h if edge == "E" else -h)
                ends = [(c.x - h, ey), (c.x + h, ey)]
            for e in ends:
                key = (round(e[0]), round(e[1]), y)
                corners.setdefault(key, set()).add(edge in ("N", "S"))
        for (ex, ey, y), kinds in corners.items():
            if len(kinds) < 2:
                continue  # a straight run, not a corner
            self.box(unreal.Vector(ex, ey, y * 100.0 + 250.0), (1.3, 1.3, 5.0), self.FILLER, "Corner")

    def boundary(self):
        # Tall invisible walls around the whole map, and a floor under everything.
        sx = self.D * TILE * 100.0
        sy = self.W * TILE * 100.0
        for center, size in (((sx * 0.5 + 150.0, 0, 1000.0), (1.0, self.W * TILE + 6, 20.0)), ((-sx * 0.5 - 150.0, 0, 1000.0), (1.0, self.W * TILE + 6, 20.0)),
                             ((0, sy * 0.5 + 150.0, 1000.0), (self.D * TILE + 6, 1.0, 20.0)), ((0, -sy * 0.5 - 150.0, 1000.0), (self.D * TILE + 6, 1.0, 20.0))):
            self.box(unreal.Vector(*center), size, None, "Boundary", hidden=True)
        self.box(unreal.Vector(0, 0, -52.0), (self.D * TILE + 6, self.W * TILE + 6, 1.0), None, "SafetyFloor", hidden=True)

    def nav_bounds(self, height=12.0):
        vol = actors.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(0, 0, height * 50.0 - 100.0))
        box = vol.get_actor_bounds(False)[1]
        ext = max(box.x, 1.0)
        vol.set_actor_scale3d(unreal.Vector((self.D * TILE + 4) * 50.0 / ext, (self.W * TILE + 4) * 50.0 / ext, height * 50.0 / ext))
        log("nav bounds extent %s" % vol.get_actor_bounds(False)[1])


def lighting():
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000.0), unreal.Rotator(0.0, -55.0, 30.0))
    sun.light_component.set_editor_property("intensity", 6.0)
    sun.light_component.set_editor_property("atmosphere_sun_light", True)
    sun.set_actor_label("Sun")
    actors.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
    sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 500.0))
    sky.light_component.set_editor_property("real_time_capture", True)
    sky.light_component.set_editor_property("intensity", 1.2)
    fog = actors.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, -200.0))
    fog.component.set_editor_property("fog_density", 0.004)


def face_yaw(at, look):
    return math.degrees(math.atan2(look.y - at.y, look.x - at.x))


def place(cls, at, look, z_offset=0.0, label=None, **props):
    a = actors.spawn_actor_from_class(cls, unreal.Vector(at.x, at.y, at.z + z_offset), unreal.Rotator(0.0, 0.0, face_yaw(at, look)))
    for k, v in props.items():
        a.set_editor_property(k, v)
    if label:
        a.set_actor_label(label)
    return a


SOLDIER_Z, SWARM_Z, TANK_Z = 95.0, 87.0, 145.0


def place_people(b, spawn, look_at, dummies, soldiers, pack, pack_look, allies, tank):
    place(unreal.PlayerStart, spawn, look_at, 0.0, "PlayerStart")
    for name, at in dummies.items():
        place(unreal.WWOTarget, at, spawn, 0.0, name)
    for i, at in enumerate(soldiers):
        place(unreal.WWOSoldier, at, spawn, SOLDIER_Z, "Soldier%d" % (i + 1))
    for i, at in enumerate(pack):
        place(unreal.WWOSwarm, at, pack_look, SWARM_Z, "Swarm%d" % (i + 1))
    for i, at in enumerate(allies):
        place(unreal.WWOAlly, at, look_at, 5.0, "Ally%d" % (i + 1))
    place(unreal.WWOTank, tank, look_at, TANK_Z, "Tank")


def new_level(path):
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    try:
        ok = levels.new_level(path, False)
    except TypeError:
        ok = levels.new_level(path)
    if not ok:
        raise RuntimeError("could not create " + path)


# --- Small arena ----------------------------------------------------------------------------

def build_arena():
    new_level("/Game/WWO/Maps/L_Arena")
    b = Builder(16, 12, 3)
    t = b.tile
    lighting()
    for x in range(0, 13):
        for z in range(0, 3):
            b.floor_tile(x, z, "metal")
    for x in range(0, 2):
        for z in range(3, 10):
            b.floor_tile(x, z, "dark")
    for x in range(2, 14):
        for z in range(3, 9):
            b.floor_tile(x, z)
    for x in range(6, 10):
        for z in range(9, 12):
            b.floor_tile(x, z, "dark")
    # North hall: windows along the back, solid sides, two doors south.
    for x in range(0, 13):
        b.wall(x, 0, "N", "window", True)
    for z in range(0, 3):
        b.wall(0, z, "W", "astra", True)
        b.wall(12, z, "E", "astra", True)
    for x in range(2, 13):
        if x in (5, 10):
            b.door(x, 2, "S")
        else:
            b.wall(x, 2, "S", "band")
    for px, pz in ((3, 1), (6, 1), (9, 1)):
        c = t(px, pz)
        b.column(unreal.Vector(c.x, c.y + 200.0, 0.0))
    # West corridor: long walls with two doorways into the courtyard.
    for z in range(3, 10):
        b.wall(0, z, "W", "astra", True)
        if z in (4, 7):
            b.door(1, z, "E")
        else:
            b.wall(1, z, "E", "band")
    for x in range(0, 2):
        b.wall(x, 9, "S", "astra", True)
    # Courtyard outer walls.
    for z in range(3, 9):
        b.wall(13, z, "E", "astra", True)
    b.wall(13, 3, "N", "astra", True)
    for x in (2, 3, 4, 5, 10, 11, 12, 13):
        b.wall(x, 8, "S", "astra", True)
    # Spawn room: one wide (8 m) opening north into the courtyard.
    for x in range(6, 10):
        if x in (6, 9):
            b.wall(x, 9, "N", "band")
        b.wall(x, 11, "S", "astra", True)
    for z in range(9, 12):
        b.wall(6, z, "W", "astra", True)
        b.wall(9, z, "E", "astra", True)
    b.prop(t(6, 11) + unreal.Vector(-120.0, -100.0, 0), (0.7, 1.0, 1.4), "Computer")
    b.prop(t(9, 11) + unreal.Vector(-130.0, 80.0, 0), (0.7, 1.2, 0.8), "Chest")
    b.floor_light(t(7.5, 11) + unreal.Vector(-140.0, 0, 0))
    # Raised platforms (2 m) with stairs and rails.
    b.raised_platform(2, 3, 3, 4)
    b.stairs(4, 3, "E")
    b.raised_platform(12, 6, 13, 8)
    b.stairs(11, 6, "W")
    b.stairs(12, 5, "N")
    # Courtyard cover.
    b.low_wall(6, 4, "N")
    b.low_wall(7, 4, "N")
    b.low_wall(9, 6, "W")
    b.low_wall(9, 7, "W")
    b.low_wall(4, 7, "N")
    for cx, cz in ((5.5, 5.2), (8.0, 3.4), (10.6, 4.6), (3.3, 6.3), (11.2, 7.8), (7.2, 7.4)):
        b.crate_stack(t(cx, cz))
    for bx, bz in ((6.2, 6.6), (10.2, 3.2), (2.6, 7.6)):
        b.barrel(t(bx, bz))
    for lx, lz in ((4.5, 5.5), (10.5, 5.5)):
        b.floor_light(t(lx, lz))
    # North hall clutter.
    b.crate_stack(t(2, 1))
    b.crate_stack(t(8, 0.4))
    b.crate_stack(t(11, 1.6))
    b.prop(t(4.5, 0.2), (0.7, 1.2, 0.8), "Chest")
    b.prop(t(7.5, 0) + unreal.Vector(130.0, 0, 0), (0.7, 1.0, 1.4), "Computer")
    # Enemy spawn points for the waves; the tags limit which enemies use each.
    b.spawn_point(0.5, 3.5, "swarm")
    b.spawn_point(1.0, 8.4, "swarm")
    b.spawn_point(2.0, 1.0, "swarm soldier")
    b.spawn_point(7.5, 1.0, "tank soldier")
    b.spawn_point(11.2, 1.0, "swarm soldier")
    b.spawn_point(12.5, 3.6, "swarm soldier")
    b.spawn_point(12.5, 7.0, "soldier", 2.0)
    b.spawn_point(2.5, 3.5, "soldier", 2.0)
    # West corridor clutter.
    b.crate_stack(t(0.6, 5.5))
    b.barrel(t(1.3, 8.4))
    b.place_rails()
    b.corner_fillers()
    b.boundary()
    b.nav_bounds()
    spawn = t(7.5, 10.3, 1.05)
    courtyard = t(7.5, 5.5)
    place_people(b, spawn, courtyard,
                 {"TargetClose": t(6.3, 11), "TargetLeft": t(8.7, 11), "TargetNear": t(4, 6), "TargetMid": t(11, 4.5), "TargetFar": t(7.5, 3.4),
                  "TargetOnPlatform": t(2.5, 3.5, 2.0)},
                 [t(12.5, 7, 2.0), t(8.5, 3.8), t(5, 4.4)],
                 [t(x, z) for x, z in ((0.4, 3.3), (1.4, 3.6), (0.7, 4.2), (1.2, 2.6), (0.4, 2.2), (1.6, 2.0))], t(1, 9),
                 [t(6.6, 10.8, 1.0), t(8.4, 10.8, 1.0)], t(7.5, 1))
    levels.save_current_level()
    log("L_Arena: %d pieces" % b.count)


# --- Large arena ------------------------------------------------------------------------------

def build_arena_large():
    new_level("/Game/WWO/Maps/L_ArenaLarge")
    b = Builder(24, 16, 7)
    t = b.tile
    UPPER = 4.0
    lighting()
    for x in range(6, 18):
        for z in range(0, 5):
            b.floor_tile(x, z, "metal")
        for z in range(5, 13):
            b.floor_tile(x, z)
    for x in range(18, 20):
        for z in range(0, 16):
            b.floor_tile(x, z, "dark")
    for x in range(20, 24):
        for z in (0, 1, 2, 3, 4, 6, 7, 8, 9, 11, 12, 13, 14, 15):
            b.floor_tile(x, z, "dark")
    for x in range(10, 14):
        for z in range(13, 16):
            b.floor_tile(x, z, "dark")
    # Outer walls (ground).
    for x in range(6, 24):
        b.wall(x, 0, "N", "window" if x < 18 else "astra", True)
    for z in (0, 1, 2, 3, 4, 6, 7, 8, 9, 11, 12, 13, 14, 15):
        b.wall(23, z, "E", "astra", True)
    for x in (10, 11, 12, 13, 18, 19, 20, 21, 22, 23):
        b.wall(x, 15, "S", "astra", True)
    for x in (6, 7, 8, 9, 14, 15, 16, 17):
        b.wall(x, 12, "S", "astra", True)
    for z in range(13, 16):
        b.wall(10, z, "W", "astra", True)
        b.wall(13, z, "E", "astra", True)
        b.wall(18, z, "W", "astra", True)
    b.wall(10, 13, "N", "band")
    b.wall(13, 13, "N", "band")
    # North hall: three doorways into the atrium, one into the east hallway.
    for x in range(6, 18):
        if x in (8, 11, 15):
            b.door(x, 4, "S")
        else:
            b.wall(x, 4, "S", "band")
    for z in range(0, 5):
        if z == 2:
            b.door(17, z, "E")
        else:
            b.wall(17, z, "E", "astra")
    for px, pz in ((9, 2), (13, 2)):
        c = t(px, pz)
        b.column(unreal.Vector(c.x, c.y + 200.0, 0.0))
    # Atrium east wall: two doorways into the hallway.
    for z in range(5, 13):
        if z in (7, 10):
            b.door(17, z, "E")
        else:
            b.wall(17, z, "E", "astra")
    # East hallway: doorways into each side room.
    for z in range(0, 16):
        if z in (2, 7, 13):
            b.door(19, z, "E")
        else:
            b.wall(19, z, "E", "band")
    for x in range(20, 24):
        b.wall(x, 4, "S", "astra")
        b.wall(x, 6, "N", "astra")
        b.wall(x, 9, "S", "astra")
        b.wall(x, 11, "N", "astra")
    # West wing: a solid 4 m block; its top is the upper floor.
    b.raised_platform(0, 0, 5, 15, UPPER, False)
    for z in range(0, 13):
        if z not in (8, 9):
            b.rails.append((5, z, "E", UPPER))  # the overlook rail (gaps where the landings join)
    for z in range(0, 16):
        b.wall(0, z, "W", "astra", True, UPPER)
    for x in range(0, 6):
        b.wall(x, 0, "N", "window", True, UPPER)
        b.wall(x, 15, "S", "astra", True, UPPER)
    for z in range(13, 16):
        b.wall(5, z, "E", "astra", True, UPPER)
    # Upper rooms U1 (north) and U2 (south), with the open hallway between them and the atrium.
    for z in range(0, 6):
        if z == 2:
            b.door(3, z, "E", UPPER)
        else:
            b.wall(3, z, "E", "band", False, UPPER)
    for x in range(0, 4):
        if x == 1:
            b.door(x, 5, "S", UPPER)
        else:
            b.wall(x, 5, "S", "band", False, UPPER)
    for x in range(0, 4):
        if x == 2:
            b.door(x, 10, "N", UPPER)
        else:
            b.wall(x, 10, "N", "band", False, UPPER)
    for z in range(10, 16):
        if z == 13:
            b.door(3, z, "E", UPPER)
        else:
            b.wall(3, z, "E", "band", False, UPPER)
    # Staircases up to the wing (two flights each, on a 2 m support), with landings.
    b.stairs(6, 11, "N")
    b.block(6, 10, 6, 10, 2.0)
    b.stairs(6, 10, "N", 2.0)
    b.raised_platform(6, 9, 6, 9, UPPER)
    b.open_edge(6, 9, "W")
    b.open_edge(6, 9, "N")
    b.stairs(6, 6, "S")
    b.block(6, 7, 6, 7, 2.0)
    b.stairs(6, 7, "S", 2.0)
    b.raised_platform(6, 8, 6, 8, UPPER)
    b.open_edge(6, 8, "W")
    b.open_edge(6, 8, "E")
    b.open_edge(6, 8, "S")
    # Central tower (4 m) with stairs up both sides, and the bridge from the north landing.
    b.raised_platform(10, 8, 13, 9, UPPER)
    b.stairs(11, 11, "N")
    b.block(11, 10, 11, 10, 2.0)
    b.stairs(11, 10, "N", 2.0)
    b.stairs(12, 6, "S")
    b.block(12, 7, 12, 7, 2.0)
    b.stairs(12, 7, "S", 2.0)
    b.bridge(7, 9, 8, UPPER)
    b.open_edge(10, 8, "W")
    # Cover.
    for cx, cz in ((8, 6.3), (15.5, 6), (8.6, 11), (15, 11.2), (14.6, 8.6), (8.2, 9)):
        b.crate_stack(t(cx, cz))
    b.low_wall(9, 6, "N")
    b.low_wall(14, 10, "S")
    b.low_wall(16, 8, "W")
    b.low_wall(7, 11, "E")
    for bx, bz in ((10, 6.5), (13.5, 11.6), (16.4, 5.4)):
        b.barrel(t(bx, bz))
    for lx, lz in ((9, 7.5), (15, 7.5), (11.5, 12.5)):
        b.floor_light(t(lx, lz))
    b.crate_stack(t(7, 1))
    b.crate_stack(t(11.5, 0.5))
    b.crate_stack(t(16, 3))
    b.prop(t(14, 0.3), (0.7, 1.2, 0.8), "Chest")
    b.crate_stack(t(18.6, 4.5))
    b.crate_stack(t(19.2, 11))
    b.crate_stack(t(21, 2.5))
    b.crate_stack(t(22.5, 8))
    b.crate_stack(t(21.5, 13.5))
    b.prop(t(23, 7.5) + unreal.Vector(0, 130.0, 0), (1.0, 0.7, 1.4), "Computer")
    b.crate_stack(t(1.5, 2.5, UPPER))
    b.crate_stack(t(2, 12.5, UPPER))
    b.crate_stack(t(4.6, 6, UPPER))
    b.prop(t(1, 14.5, UPPER), (0.7, 1.2, 0.8), "Chest")
    b.prop(t(11, 15) + unreal.Vector(-120.0, -100.0, 0), (0.7, 1.0, 1.4), "Computer")
    # Enemy spawn points for the waves.
    b.spawn_point(8.5, 2, "tank soldier")
    b.spawn_point(15, 2, "tank soldier swarm")
    b.spawn_point(18.5, 0.6, "swarm")
    b.spawn_point(21.5, 2, "swarm soldier")
    b.spawn_point(21.5, 7.5, "swarm soldier")
    b.spawn_point(21.5, 13.5, "swarm soldier")
    b.spawn_point(1.5, 2.5, "soldier swarm", UPPER)
    b.spawn_point(1.5, 12.5, "soldier swarm", UPPER)
    b.spawn_point(11.5, 8.5, "soldier", UPPER)
    b.place_rails()
    b.corner_fillers()
    b.boundary()
    b.nav_bounds(16.0)
    spawn = t(11.5, 14.3, 1.05)
    atrium = t(11.5, 8.5)
    place_people(b, spawn, atrium,
                 {"TargetClose": t(10.4, 15.2), "TargetLeft": t(12.6, 15.2), "TargetNear": t(8, 7.5), "TargetMid": t(15.5, 9.5), "TargetFar": t(11.5, 5.4),
                  "TargetOnPlatform": t(11, 8.6, 4.0)},
                 [t(12.5, 8.6, 4.0), t(4.6, 6.5, 4.0), t(16, 6.5)],
                 [t(x, z) for x, z in ((18.4, 3.2), (19.4, 3.6), (18.7, 4.4), (19.2, 2.4), (18.4, 1.8), (19.6, 1.4))], t(18.5, 12),
                 [t(10.6, 14.8, 1.0), t(12.4, 14.8, 1.0)], t(11.5, 2))
    levels.save_current_level()
    log("L_ArenaLarge: %d pieces" % b.count)


# --- Test range -----------------------------------------------------------------------------

def g(x, y, z):
    """A Godot position (meters) in Unreal cm."""
    return unreal.Vector(-z * 100.0, x * 100.0, y * 100.0)


def build_test_range():
    new_level("/Game/WWO/Maps/L_TestRange")
    b = Builder(1, 1, 1)
    lighting()
    floor = grid_material("RangeFloor", (0.35, 0.36, 0.38))
    colors = {
        "cube": (0.9, 0.45, 0.1), "teal": (0.1, 0.6, 0.6), "blue": (0.15, 0.3, 0.85), "green": (0.2, 0.7, 0.25), "purple": (0.5, 0.25, 0.75),
        "yellow": (0.95, 0.8, 0.15), "red": (0.8, 0.15, 0.15), "pink": (0.9, 0.45, 0.65), "navy": (0.1, 0.12, 0.4), "lime": (0.6, 0.9, 0.2),
    }
    mats = {k: grid_material("Range_" + k, v) for k, v in colors.items()}
    # Godot CSG size (x, y, z) -> Unreal scale (z, x, y).
    def csg(pos, size, mat, label, tags=(), pitch=0.0, mesh=None):
        return b.box(g(*pos), (size[2], size[0], size[1]), mat, label, tags, pitch=pitch, mesh=mesh)

    csg((0, -0.5, 0), (100, 1, 100), floor, "Floor")
    csg((0, 1, -6), (2, 2, 2), mats["cube"], "Cube")
    csg((6, 1.75, -6), (4, 0.5, 2), mats["teal"], "LowBar")
    csg((-6, 2, -8), (1.2, 4, 1.2), mats["blue"], "Pillar", mesh=CYLINDER)
    csg((-3, 1, -13), (2, 2, 2), mats["green"], "Ball", mesh=SPHERE)
    csg((4, 1, -14), (4, 2, 4), mats["purple"], "JumpPlatform")
    csg((10, 2, -17), (4, 4, 4), mats["yellow"], "DoubleJumpPlatform")
    csg((-10, 1.45, -6), (3, 0.4, 9), mats["red"], "Ramp", pitch=20.0)
    csg((0, 4, -35), (16, 8, 1), mats["pink"], "BackWall")
    csg((-7, 0.75, 5), (1.5, 1.5, 1.5), mats["navy"], "CrateLeft", ("Cover",))
    csg((7, 0.6, 6), (1.2, 1.2, 1.2), mats["lime"], "CrateRight", ("Cover",))
    csg((22, 0.75, -24), (2, 1.5, 1), mats["navy"], "SoldierCoverA", ("Cover",))
    csg((31, 0.75, -27), (2, 1.5, 1), mats["navy"], "SoldierCoverB", ("Cover",))
    csg((26, 1.25, -34), (1, 2.5, 3), mats["navy"], "SoldierCoverC", ("Cover",))
    vol = actors.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(0, 0, 400.0))
    ext = max(vol.get_actor_bounds(False)[1].x, 1.0)
    vol.set_actor_scale3d(unreal.Vector(5000.0 / ext, 5000.0 / ext, 600.0 / ext))
    spawn = g(0, 1.05, 0)
    ahead = g(0, 1, -10)
    dummies = {"TargetClose": g(10, 0, -8), "TargetLeft": g(-12, 0, -16), "TargetNear": g(-4, 0, -20), "TargetMid": g(3, 0, -28), "TargetFar": g(-1, 0, -45),
               "TargetOnPlatform": g(10, 4, -17)}
    place(unreal.PlayerStart, spawn, ahead, 0.0, "PlayerStart")
    for name, at in dummies.items():
        strafe = {"TargetNear": 4.0, "TargetMid": 6.0}.get(name, 0.0)
        place(unreal.WWOTarget, at, spawn, 0.0, name, strafe_distance=strafe)
    for i, at in enumerate((g(23, 0, -29), g(29, 0, -31), g(27, 0, -38))):
        place(unreal.WWOSoldier, at, spawn, SOLDIER_Z, "Soldier%d" % (i + 1))
    for i, (x, z) in enumerate(((-22, -22), (-24, -21), (-23, -24.5), (-26, -23), (-25, -26), (-21, -25))):
        place(unreal.WWOSwarm, g(x, 0, z), spawn, SWARM_Z, "Swarm%d" % (i + 1))
    place(unreal.WWOTank, g(-24, 0, -44), spawn, TANK_Z, "Tank")
    for i, at in enumerate((g(-2.5, 1, 2.5), g(2.5, 1, 2.5))):
        place(unreal.WWOAlly, at, ahead, 5.0, "Ally%d" % (i + 1))
    levels.save_current_level()
    log("L_TestRange done")


build_test_range()
build_arena_large()
build_arena()
