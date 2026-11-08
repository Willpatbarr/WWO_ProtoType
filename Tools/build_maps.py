"""Builds the three levels (Godot: tools/build_scifi_map.gd + tools/build_arena.gd + main.tscn).

Run headless from the project folder after Tools/create_assets.py and Tools/import_kit.py:
  UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script=Tools/build_maps.py -unattended -nullrhi

- L_Arena: the small arena, a grid of 4 m tiles, 16 wide by 12 deep: spawn room (south), a
  courtyard with cover and two 2 m raised platforms, a pillared north hall (the Tank), a west flank
  corridor (the Swarm pack). 8 enemy spawn points for the waves.
- L_ArenaLarge: 24 x 16 tiles, two levels: spawn room, atrium with a 4 m central tower, north hall,
  east hallway with three side rooms; upper west wing with two rooms and an overlook, two staircases,
  and a bridge to the tower. 9 spawn points.
- L_TestRange: the original gray-box range in Godot's flat colors (no spawn points, so it stays a sandbox).
- L_ArenaRing: the 4v4 PvP ring map, gray box, from the mockup's JSON (see build_ring_arena).
WWO_MAPS=ring (or range, large, arena, comma separated) builds only those.
The arenas are built from the same Quaternius Sci-Fi kit pieces as the Godot maps (imported by
Tools/import_kit.py), placed as build_scifi_map.gd places them: positions and yaws are worked out in
Godot space and converted. Each piece collides as its own mesh (Godot's trimesh shapes); stairs
collide as smooth ramps, like the Godot ones. Platform blocks and corner posts are flat-colored
boxes, as Godot's CSG boxes are.
Fix over Godot: the small arena's three stairs climb toward their 2 m platforms (Godot's calls name
the side away from the platform, so the platforms can't be reached).
Coordinates: a Godot point (x, y, z) in meters is Unreal (-z, x, y) * 100.
"""
import json
import math
import os
import random

import unreal

eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
mel = unreal.MaterialEditingLibrary

TILE = 4.0
PLATFORM_HEIGHT = 2.0
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
CYLINDER = unreal.load_asset("/Engine/BasicShapes/Cylinder.Cylinder")
SPHERE = unreal.load_asset("/Engine/BasicShapes/Sphere.Sphere")
SKY_SPHERE = unreal.load_asset("/Engine/EngineSky/SM_SkySphere.SM_SkySphere")
SOLID = unreal.load_asset("/Game/WWO/Materials/M_WWO_Solid.M_WWO_Solid")
KIT = "/Game/WWO/Kit/Meshes/SM_"


def log(msg):
    unreal.log_warning("WWO: " + msg)


def g(x, y, z):
    """A Godot position (meters) in Unreal cm."""
    return unreal.Vector(-z * 100.0, x * 100.0, y * 100.0)


def vadd(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def vscale(a, k):
    return (a[0] * k, a[1] * k, a[2] * k)


_materials = {}


def solid_material(name, color):
    """A flat M_WWO_Solid instance in a Godot (sRGB) color, like a StandardMaterial3D albedo."""
    if name in _materials:
        return _materials[name]
    folder = "/Game/WWO/Materials/Map"
    path = folder + "/MI_Solid_" + name
    if eal.does_asset_exist(path):
        mi = eal.load_asset(path)
    else:
        mi = tools.create_asset("MI_Solid_" + name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", SOLID)
    mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(color[0], color[1], color[2], 1.0))
    eal.save_loaded_asset(mi)
    _materials[name] = mi
    return mi


_kit = {}


def kit_mesh(path):
    name = path.split("/")[-1]
    if name not in _kit:
        _kit[name] = unreal.load_asset(KIT + name)
        if not _kit[name]:
            raise RuntimeError("missing kit mesh %s: run Tools/import_kit.py first" % name)
    return _kit[name]


class Builder:
    # Godot edges (build_scifi_map.gd): yaw that turns a wall piece (on a tile's -X edge) onto
    # the edge, and the edge's outward direction, both in Godot space.
    EDGE_YAW = {"W": 0.0, "S": 90.0, "E": 180.0, "N": 270.0}
    EDGE_DIR = {"W": (-1, 0, 0), "E": (1, 0, 0), "N": (0, 0, -1), "S": (0, 0, 1)}
    OPPOSITE = {"N": "S", "S": "N", "E": "W", "W": "E"}
    FILLER_COLOR = (0.2, 0.22, 0.26)
    PLATFORM_COLOR = (0.24, 0.26, 0.3)

    def __init__(self, width, depth, seed):
        self.W = width
        self.D = depth
        self.rng = random.Random(seed)
        self.count = 0
        self.wall_edges = {}
        self.rails = []
        self.stair_tops = set()
        self.PLATFORM = solid_material("Platform", self.PLATFORM_COLOR)
        self.FILLER = solid_material("Filler", self.FILLER_COLOR)

    # --- Coordinates -------------------------------------------------------------------------

    def gtile(self, x, z, y=0.0):
        """Center of tile (x, z) at height y, in Godot space (meters)."""
        return ((x - (self.W - 1) * 0.5) * TILE, y, (z - (self.D - 1) * 0.5) * TILE)

    def tile(self, x, z, y=0.0):
        """Center of Godot tile (x, z) at height y, in Unreal cm."""
        return g(*self.gtile(x, z, y))

    def edge(self, edge, k):
        return vscale(self.EDGE_DIR[edge], k)

    # --- Pieces ------------------------------------------------------------------------------

    def label(self, actor, label):
        actor.set_actor_label("%s_%d" % (label, self.count))
        self.count += 1

    def piece(self, path, at, yaw, cover=False, collide=True):
        """A kit piece at Godot position `at`, turned `yaw` degrees about Godot's up axis.
        Imported pieces lie in glTF axes as Interchange converts them (x, z, y), so 90 degrees of
        Unreal yaw puts them in this port's Godot convention, and a Godot yaw is a negative
        Unreal one."""
        actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, g(*at), unreal.Rotator(0.0, 0.0, 90.0 - yaw))
        comp = actor.static_mesh_component
        comp.set_mobility(unreal.ComponentMobility.STATIC)
        comp.set_static_mesh(kit_mesh(path))
        if not collide:
            comp.set_collision_profile_name("NoCollision")
        if cover:
            actor.set_editor_property("tags", ["Cover"])
        self.label(actor, path.split("/")[-1])
        return actor

    def box(self, center, size_m, material, label, tags=(), yaw=0.0, pitch=0.0, hidden=False, mesh=None):
        actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, center, unreal.Rotator(0.0, pitch, yaw))
        comp = actor.static_mesh_component
        comp.set_mobility(unreal.ComponentMobility.STATIC)
        comp.set_static_mesh(mesh or CUBE)
        if material:
            comp.set_material(0, material)
        actor.set_actor_scale3d(unreal.Vector(*size_m))
        self.label(actor, label)
        if tags:
            actor.set_editor_property("tags", list(tags))
        if hidden:
            actor.set_actor_hidden_in_game(True)
            comp.set_editor_property("cast_shadow", False)
        return actor

    def floor_tile(self, x, z, kind, y=0.0):
        self.piece("Platforms/" + kind, self.gtile(x, z, y), 0.0)

    def wall(self, x, z, edge, kind, tall=False, y=0.0):
        # Wall pieces sit on a tile's -X edge by default; turn them onto the edge we want.
        self.piece("Walls/" + kind, self.gtile(x, z, y), self.EDGE_YAW[edge])
        if tall:
            self.piece("Walls/TopSimple_Straight", self.gtile(x, z, y), self.EDGE_YAW[edge])
        self.wall_edges[(x, z, edge, y)] = True

    def door(self, x, z, edge, y=0.0):
        # Door frames run along X through their origin, so move them onto the edge.
        yaw = 0.0 if edge in ("N", "S") else 90.0
        self.piece("Platforms/Door_Frame_Square", vadd(self.gtile(x, z, y), self.edge(edge, TILE * 0.5)), yaw)
        self.wall_edges[(x, z, edge, y)] = True

    def low_wall(self, x, z, edge, y=0.0):
        # A 2 m wall on a tile edge: cover you can shoot over by jumping, or mantle.
        self.piece("Walls/ShortWall_Metal2_Straight", vadd(self.gtile(x, z, y), self.edge(edge, 0.02)), self.EDGE_YAW[edge], True)

    def prop(self, path, at, yaw, cover=False):
        self.piece(path, at, yaw, cover)

    def crate_stack(self, at):
        # One to three crates, as cover.
        r = self.rng
        self.piece("Props/Prop_Crate4", vadd(at, (0, 0.56, 0)), r.random() * 30.0, True)
        if r.random() < 0.6:
            self.piece("Props/Prop_Crate3", vadd(at, (r.uniform(-0.2, 0.2), 1.62, r.uniform(-0.2, 0.2))), r.random() * 90.0)
        if r.random() < 0.5:
            self.piece("Props/Prop_Crate3", vadd(at, (1.1, 0.5, r.uniform(-0.3, 0.3))), r.random() * 90.0)

    def block(self, x0, z0, x1, z1, height, base=0.0):
        # Just the solid block (Godot: a CSG box in the platform color).
        a = self.tile(x0, z0)
        b = self.tile(x1, z1)
        center = unreal.Vector((a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (base + height * 0.5) * 100.0)
        size = ((z1 - z0 + 1) * TILE, (x1 - x0 + 1) * TILE, height)
        self.box(center, size, self.PLATFORM, "PlatformBlock")

    def raised_platform(self, x0, z0, x1, z1, height=PLATFORM_HEIGHT, rails=True, base=0.0):
        # A solid block with metal plates on top, short-wall facades and rails on its edges
        # (except where stairs climb onto it).
        self.block(x0, z0, x1, z1, height, base)
        for x in range(x0, x1 + 1):
            for z in range(z0, z1 + 1):
                self.floor_tile(x, z, "Platform_Metal", base + height + 0.01)
        for x in range(x0, x1 + 1):
            self.platform_edge(x, z0, "N", base, height, rails)
            self.platform_edge(x, z1, "S", base, height, rails)
        for z in range(z0, z1 + 1):
            self.platform_edge(x0, z, "W", base, height, rails)
            self.platform_edge(x1, z, "E", base, height, rails)

    def platform_edge(self, x, z, edge, base, height, rails):
        # Short-wall facades up the side (one per 2 m), and a rail along the top.
        level = 0.0
        while level < height - 0.01:
            self.piece("Walls/ShortWall_Metal2_Straight", vadd(self.gtile(x, z, base + level), self.edge(edge, 0.03)), self.EDGE_YAW[edge])
            level += 2.0
        if rails:
            self.rails.append((x, z, edge, base + height))

    def open_edge(self, x, z, edge):
        # No rail on this edge (where a landing, bridge or stairs joins).
        self.stair_tops.add((x, z, edge))

    def stairs(self, x, z, toward, y=0.0):
        # Stairs in tile (x, z) climbing 2 m toward the platform on its `toward` side, starting
        # at height y. The kit's stairs are high at their origin and run down along +Z.
        d = self.EDGE_DIR[toward]
        high_edge = vadd(self.gtile(x, z, y), vscale(d, TILE * 0.5))
        yaw = math.degrees(math.atan2(-d[0], -d[2]))
        self.piece("Platforms/Platform_Stairs_4Wide", high_edge, yaw, collide=False)
        # Collision is a smooth ramp, not the steps: the player can walk up a slope but has no
        # step-up for 25 cm stairs.
        c = self.tile(x, z, y)
        run, rise = TILE, 2.0
        length = math.sqrt(run * run + rise * rise)
        c.z += rise * 50.0 - 10.0
        ue_yaw = {"N": 0.0, "S": 180.0, "E": 90.0, "W": -90.0}[toward]
        self.box(c, (length, TILE, 0.2), None, "Ramp", yaw=ue_yaw, pitch=math.degrees(math.atan2(rise, run)), hidden=True)
        self.stair_tops.add((x + int(d[0]), z + int(d[2]), self.OPPOSITE[toward]))  # no rail across the top
        self.stair_tops.add((x, z, toward))

    def bridge(self, x0, x1, z, y):
        # A catwalk along x at height y, one tile wide: plates on a thin slab, rails both sides.
        self.block(x0, z, x1, z, 0.25, y - 0.25)
        for x in range(x0, x1 + 1):
            self.floor_tile(x, z, "Platform_Metal2", y + 0.01)
            self.rails.append((x, z, "N", y))
            self.rails.append((x, z, "S", y))

    def place_rails(self):
        for x, z, edge, y in self.rails:
            if (x, z, edge) in self.stair_tops:
                continue
            yaw = self.EDGE_YAW[edge] + 180.0
            # Rails run along -Z from their origin; shift so one spans the tile edge.
            along = (math.sin(math.radians(yaw)), 0.0, math.cos(math.radians(yaw)))
            at = vadd(vadd(self.gtile(x, z, y), self.edge(edge, TILE * 0.5 - 0.15)), vscale(along, TILE * 0.5 - 0.04))
            self.piece("Props/Prop_Rail_4", at, yaw)

    def column(self, x, z):
        self.piece("Columns/Column_Large_Straight", vadd(self.gtile(x, z), (2, 0, 0)), 0.0)

    def floor_light(self, at, yaw):
        # The kit's floor light: an emissive strip (Godot adds no light of its own for it).
        self.piece("Props/Prop_Light_Floor", at, yaw)

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


# --- Lighting ---------------------------------------------------------------------------------
# Godot (main.tscn, arena.tscn): a DirectionalLight3D turned -60 degrees about x (white, energy 1,
# shadows), and a WorldEnvironment with the default ProceduralSkyMaterial as background and
# ambient light, no fog, linear tonemap. Here the sun keeps its direction and color, the sky is
# Godot's procedural sky on a sky dome (M_WWO_Sky) that the sky light captures in real time, so
# the ambient light comes from the same colors. In Godot a white surface facing the sun shows
# the sun's energy (1) and the sky shows its colors as they are, so the sky's emissive is scaled
# to the sun's lux / pi (what a lit white surface gives off in Unreal) to keep that ratio.
SUN_LUX = 6.0
SUN_PITCH = -60.0  # Godot's rotation about x; it shines toward Godot -z (north, Unreal +x)
SKY_ENERGY = SUN_LUX / math.pi
HORIZON = tuple(c ** 2.2 for c in (0.6463, 0.6558, 0.6708))


def sky_instance():
    path = "/Game/WWO/Materials/Map/MI_WWO_Sky"
    if eal.does_asset_exist(path):
        mi = eal.load_asset(path)
    else:
        mi = tools.create_asset("MI_WWO_Sky", "/Game/WWO/Materials/Map", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", unreal.load_asset("/Game/WWO/Materials/M_WWO_Sky.M_WWO_Sky"))
    p = math.radians(-SUN_PITCH)
    mel.set_material_instance_vector_parameter_value(mi, "SunDir", unreal.LinearColor(-math.cos(p), 0.0, math.sin(p), 0.0))
    mel.set_material_instance_scalar_parameter_value(mi, "Energy", SKY_ENERGY)
    eal.save_loaded_asset(mi)
    return mi


def lighting():
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000.0), unreal.Rotator(0.0, SUN_PITCH, 0.0))
    sun.light_component.set_editor_property("intensity", SUN_LUX)
    sun.light_component.set_light_color(unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    sun.set_actor_label("Sun")
    dome = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
    comp = dome.static_mesh_component
    comp.set_static_mesh(SKY_SPHERE)
    comp.set_material(0, sky_instance())
    comp.set_collision_profile_name("NoCollision")
    comp.set_editor_property("cast_shadow", False)
    comp.set_editor_property("can_ever_affect_navigation", False)
    radius = max(SKY_SPHERE.get_bounding_box().max.x, 1.0)
    dome.set_actor_scale3d(unreal.Vector(1, 1, 1) * (500000.0 / radius))  # a 5 km dome
    dome.set_actor_label("SkyDome")
    sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 500.0))
    sc = sky.light_component
    sc.set_editor_property("real_time_capture", True)
    sc.set_editor_property("lower_hemisphere_is_black", False)  # Godot's sky has a ground half
    sc.set_editor_property("intensity", 1.0)
    sky.set_actor_label("SkyLight")
    # Godot has no fog; a thin haze in the horizon's color only softens the far walls.
    fog = actors.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, -200.0))
    fc = fog.component
    fc.set_editor_property("fog_density", 0.0025)
    fc.set_editor_property("fog_height_falloff", 0.2)
    fc.set_editor_property("start_distance", 2500.0)
    fc.set_editor_property("fog_max_opacity", 0.6)
    fc.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(*(c * SKY_ENERGY for c in HORIZON), 1.0))
    fog.set_actor_label("HeightFog")
    # Auto exposure, kept to a band around Godot's look: Godot's exposure doesn't adapt, and its
    # sky and lit walls come out as they do here at about EV100 0.9 (the sky's colors measured in
    # the map screenshots). The band lets the dark metal arenas open up a little and the white test
    # range close down a little, and stops a sunlit wall blowing out when you look out of the
    # shade (or the shade going black under a bright sky).
    ppv = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0))
    ppv.set_editor_property("unbound", True)
    s = ppv.get_editor_property("settings")
    for k, v in (("auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM), ("auto_exposure_min_brightness", EXPOSURE_MIN_EV),
                 ("auto_exposure_max_brightness", EXPOSURE_MAX_EV), ("auto_exposure_bias", EXPOSURE_BIAS)):
        s.set_editor_property("override_" + k, True)
        s.set_editor_property(k, v)
    ppv.set_editor_property("settings", s)
    ppv.set_actor_label("PostProcess")


EXPOSURE_MIN_EV = 0.5
EXPOSURE_MAX_EV = 1.5
EXPOSURE_BIAS = 0.0


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
    gt = b.gtile
    lighting()
    # Floors.
    for x in range(0, 13):
        for z in range(0, 3):
            b.floor_tile(x, z, "Platform_Metal" if (x + z) % 2 == 0 else "Platform_Metal2")
    for x in range(0, 2):
        for z in range(3, 10):
            b.floor_tile(x, z, "Platform_DarkPlates")
    for x in range(2, 14):
        for z in range(3, 9):
            b.floor_tile(x, z, "Platform_Squares" if (x + z) % 3 else "Platform_CenterPlate")
    for x in range(6, 10):
        for z in range(9, 12):
            b.floor_tile(x, z, "Platform_DarkPlates")
    # North hall walls: windows along the back, solid sides, two doors south.
    for x in range(0, 13):
        b.wall(x, 0, "N", "WallWindow_Straight", True)
    for z in range(0, 3):
        b.wall(0, z, "W", "WallAstra_Straight", True)
        b.wall(12, z, "E", "WallAstra_Straight", True)
    for x in range(2, 13):
        if x in (5, 10):
            b.door(x, 2, "S")
        else:
            b.wall(x, 2, "S", "WallBand_Straight")
    for px, pz in ((3, 1), (6, 1), (9, 1)):
        b.column(px, pz)
    # West corridor: long walls with two doorways into the courtyard.
    for z in range(3, 10):
        b.wall(0, z, "W", "WallAstra_Straight", True)
        if z in (4, 7):
            b.door(1, z, "E")
        else:
            b.wall(1, z, "E", "WallBand_Straight")
    for x in range(0, 2):
        b.wall(x, 9, "S", "WallAstra_Straight", True)
    # Courtyard outer walls.
    for z in range(3, 9):
        b.wall(13, z, "E", "WallAstra_Straight", True)
    b.wall(13, 3, "N", "WallAstra_Straight", True)
    for x in (2, 3, 4, 5, 10, 11, 12, 13):
        b.wall(x, 8, "S", "WallAstra_Straight", True)
    # Spawn room: one wide (8 m) opening north into the courtyard.
    for x in range(6, 10):
        if x in (6, 9):
            b.wall(x, 9, "N", "WallBand_Straight")
        b.wall(x, 11, "S", "WallAstra_Straight", True)
    for z in range(9, 12):
        b.wall(6, z, "W", "WallAstra_Straight", True)
        b.wall(9, z, "E", "WallAstra_Straight", True)
    b.prop("Props/Prop_Computer", vadd(gt(6, 11), (-1.0, 0, 1.2)), 180.0)
    b.prop("Props/Prop_Chest", vadd(gt(9, 11), (0.8, 0, 1.3)), 180.0)
    b.prop("Props/Prop_Light_Floor", vadd(gt(7.5, 11), (0, 0, 1.4)), 0.0)
    # Raised platforms (2 m) with stairs, facades and rails. Godot's calls climb away from the
    # platforms (stairs(4, 3, "E"), (11, 6, "W"), (12, 5, "N")); these climb onto them.
    b.raised_platform(2, 3, 3, 4)  # west, by the corridor and hall doors
    b.stairs(4, 3, "W")  # up from the courtyard
    b.raised_platform(12, 6, 13, 8)  # east, high ground over the spawn doors
    b.stairs(11, 6, "E")
    b.stairs(12, 5, "S")
    # Courtyard cover.
    b.low_wall(6, 4, "N")
    b.low_wall(7, 4, "N")  # a long barrier in the middle
    b.low_wall(9, 6, "W")
    b.low_wall(9, 7, "W")
    b.low_wall(4, 7, "N")
    for cx, cz in ((5.5, 5.2), (8.0, 3.4), (10.6, 4.6), (3.3, 6.3), (11.2, 7.8), (7.2, 7.4)):
        b.crate_stack(gt(cx, cz))
    for bx, bz in ((6.2, 6.6), (10.2, 3.2), (2.6, 7.6)):
        b.prop("Props/Prop_Barrel_Large", gt(bx, bz), b.rng.random() * 360.0, True)
    for lx, lz in ((4.5, 5.5), (10.5, 5.5)):
        b.floor_light(gt(lx, lz), 90.0)
    # North hall clutter.
    b.crate_stack(gt(2, 1))
    b.crate_stack(gt(8, 0.4))
    b.crate_stack(gt(11, 1.6))
    b.prop("Props/Prop_Chest", gt(4.5, 0.2), 0.0, True)
    b.prop("Props/Prop_Computer", vadd(gt(7.5, 0), (0, 0, -1.3)), 0.0)
    # Enemy spawn points for the waves; the tags limit which enemies use each.
    b.spawn_point(0.5, 3.5, "swarm")
    b.spawn_point(1.0, 8.4, "swarm")
    b.spawn_point(2.0, 1.0, "swarm soldier")
    b.spawn_point(7.5, 1.0, "tank soldier")
    b.spawn_point(11.2, 1.0, "swarm soldier")
    b.spawn_point(12.5, 3.6, "swarm soldier")
    b.spawn_point(12.5, 7.0, "soldier", PLATFORM_HEIGHT)
    b.spawn_point(2.5, 3.5, "soldier", PLATFORM_HEIGHT)
    # West corridor clutter (cover for the swarm to come around).
    b.crate_stack(gt(0.6, 5.5))
    b.prop("Props/Prop_Barrel_Large", gt(1.3, 8.4), 0.0, True)
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
    gt = b.gtile
    UPPER = 4.0
    lighting()
    # Ground floors.
    for x in range(6, 18):
        for z in range(0, 5):
            b.floor_tile(x, z, "Platform_Metal" if (x + z) % 2 == 0 else "Platform_Metal2")
        for z in range(5, 13):
            b.floor_tile(x, z, "Platform_Squares" if (x + z) % 3 else "Platform_CenterPlate")
    for x in range(18, 20):
        for z in range(0, 16):
            b.floor_tile(x, z, "Platform_DarkPlates")
    for x in range(20, 24):
        for z in (0, 1, 2, 3, 4, 6, 7, 8, 9, 11, 12, 13, 14, 15):
            b.floor_tile(x, z, "Platform_Simple2")
    for x in range(10, 14):
        for z in range(13, 16):
            b.floor_tile(x, z, "Platform_DarkPlates")
    # Outer walls (ground).
    for x in range(6, 24):
        b.wall(x, 0, "N", "WallWindow_Straight" if x < 18 else "WallAstra_Straight", True)
    for z in (0, 1, 2, 3, 4, 6, 7, 8, 9, 11, 12, 13, 14, 15):
        b.wall(23, z, "E", "WallAstra_Straight", True)
    for x in (10, 11, 12, 13, 18, 19, 20, 21, 22, 23):
        b.wall(x, 15, "S", "WallAstra_Straight", True)
    for x in (6, 7, 8, 9, 14, 15, 16, 17):
        b.wall(x, 12, "S", "WallAstra_Straight", True)
    for z in range(13, 16):
        b.wall(10, z, "W", "WallAstra_Straight", True)
        b.wall(13, z, "E", "WallAstra_Straight", True)
        b.wall(18, z, "W", "WallAstra_Straight", True)
    b.wall(10, 13, "N", "WallBand_Straight")
    b.wall(13, 13, "N", "WallBand_Straight")  # the spawn room opens north between these
    # North hall: three doorways into the atrium, one into the east hallway.
    for x in range(6, 18):
        if x in (8, 11, 15):
            b.door(x, 4, "S")
        else:
            b.wall(x, 4, "S", "WallBand_Straight")
    for z in range(0, 5):
        if z == 2:
            b.door(17, z, "E")
        else:
            b.wall(17, z, "E", "WallAstra_Straight")
    for px, pz in ((9, 2), (13, 2)):
        b.column(px, pz)
    # Atrium east wall: two doorways into the hallway.
    for z in range(5, 13):
        if z in (7, 10):
            b.door(17, z, "E")
        else:
            b.wall(17, z, "E", "WallAstra_Straight")
    # East hallway: doorways into each side room.
    for z in range(0, 16):
        if z in (2, 7, 13):
            b.door(19, z, "E")
        else:
            b.wall(19, z, "E", "WallBand_Straight")
    for x in range(20, 24):
        b.wall(x, 4, "S", "WallAstra_Straight")
        b.wall(x, 6, "N", "WallAstra_Straight")
        b.wall(x, 9, "S", "WallAstra_Straight")
        b.wall(x, 11, "N", "WallAstra_Straight")
    # West wing: a solid 4 m block; its top is the upper floor.
    b.raised_platform(0, 0, 5, 15, UPPER, False)
    for z in range(0, 13):
        if z not in (8, 9):
            b.rails.append((5, z, "E", UPPER))  # the overlook rail (gaps where the landings join)
    for z in range(0, 16):
        b.wall(0, z, "W", "WallAstra_Straight", True, UPPER)
    for x in range(0, 6):
        b.wall(x, 0, "N", "WallWindow_Straight", True, UPPER)
        b.wall(x, 15, "S", "WallAstra_Straight", True, UPPER)
    for z in range(13, 16):
        b.wall(5, z, "E", "WallAstra_Straight", True, UPPER)  # the wing's south end is closed
    # Upper rooms U1 (north) and U2 (south), with the open hallway between them and the atrium.
    for z in range(0, 6):
        if z == 2:
            b.door(3, z, "E", UPPER)
        else:
            b.wall(3, z, "E", "WallBand_Straight", False, UPPER)
    for x in range(0, 4):
        if x == 1:
            b.door(x, 5, "S", UPPER)
        else:
            b.wall(x, 5, "S", "WallBand_Straight", False, UPPER)
    for x in range(0, 4):
        if x == 2:
            b.door(x, 10, "N", UPPER)
        else:
            b.wall(x, 10, "N", "WallBand_Straight", False, UPPER)
    for z in range(10, 16):
        if z == 13:
            b.door(3, z, "E", UPPER)
        else:
            b.wall(3, z, "E", "WallBand_Straight", False, UPPER)
    # Staircases up to the wing (two flights each, on a 2 m support), with landings.
    b.stairs(6, 11, "N")  # you walk on from the atrium floor in front of it (tile 6, 12)
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
    b.open_edge(6, 8, "S")  # joins the south landing
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
        b.crate_stack(gt(cx, cz))
    b.low_wall(9, 6, "N")
    b.low_wall(14, 10, "S")
    b.low_wall(16, 8, "W")
    b.low_wall(7, 11, "E")
    for bx, bz in ((10, 6.5), (13.5, 11.6), (16.4, 5.4)):
        b.prop("Props/Prop_Barrel_Large", gt(bx, bz), b.rng.random() * 360.0, True)
    for lx, lz in ((9, 7.5), (15, 7.5), (11.5, 12.5)):
        b.floor_light(gt(lx, lz), 90.0)
    b.crate_stack(gt(7, 1))
    b.crate_stack(gt(11.5, 0.5))
    b.crate_stack(gt(16, 3))
    b.prop("Props/Prop_Chest", gt(14, 0.3), 0.0, True)
    b.crate_stack(gt(18.6, 4.5))
    b.crate_stack(gt(19.2, 11))
    b.crate_stack(gt(21, 2.5))
    b.crate_stack(gt(22.5, 8))
    b.crate_stack(gt(21.5, 13.5))
    b.prop("Props/Prop_Computer", vadd(gt(23, 7.5), (1.3, 0, 0)), 270.0)
    b.crate_stack(gt(1.5, 2.5, UPPER))
    b.crate_stack(gt(2, 12.5, UPPER))
    b.crate_stack(gt(4.6, 6, UPPER))
    b.prop("Props/Prop_Chest", gt(1, 14.5, UPPER), 0.0, True)
    b.prop("Props/Prop_Computer", vadd(gt(11, 15), (-1.0, 0, 1.2)), 180.0)
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

def build_test_range():
    new_level("/Game/WWO/Maps/L_TestRange")
    b = Builder(1, 1, 1)
    lighting()
    floor = solid_material("RangeFloor", (1.0, 1.0, 1.0))  # Godot's default material
    colors = {
        "cube": (0.9, 0.45, 0.1), "teal": (0.1, 0.6, 0.6), "blue": (0.15, 0.3, 0.85), "green": (0.2, 0.7, 0.25), "purple": (0.5, 0.25, 0.75),
        "yellow": (0.95, 0.8, 0.15), "red": (0.8, 0.15, 0.15), "pink": (0.9, 0.45, 0.65), "navy": (0.1, 0.12, 0.4), "lime": (0.6, 0.9, 0.2),
    }
    mats = {k: solid_material("Range_" + k, v) for k, v in colors.items()}
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


# --- Ring (4v4 PvP) ---------------------------------------------------------------------------
# From ~/Documents/WWO_PvP_Mockups/ring_map_geometry.json (ring_map.py makes it; see that folder's
# HANDOFF_RingMap.md). Mockup meters: x east, y north, origin at the center. Unreal X = y * 100,
# Unreal Y = x * 100 (north up in the top view); a direction (dx, dy) has Unreal yaw atan2(dx, dy).
# Gray box: flat-colored boxes. Curved walls are straight pieces of about 4 m along each arc (the
# arcs end exactly at the door edges). Floor-1 walls stand 0-4 m, floor-2 walls 4-8 m.
RING_JSON = os.environ.get("WWO_RING_JSON", os.path.expanduser("~/Documents/WWO_PvP_Mockups/ring_map_geometry.json"))
FLOOR2 = 4.0
SLAB = 0.3          # floor-2 slab thickness (top at 4 m)
LOW_COVER = 1.2     # Will: shoot over it
RAIL = 1.1


def mu(x, y, z=0.0):
    """A mockup point (meters) in Unreal cm."""
    return unreal.Vector(y * 100.0, x * 100.0, z * 100.0)


def mu_yaw(dx, dy):
    return math.degrees(math.atan2(dx, dy))


def ring_box(b, x, y, z0, z1, length, thickness, yaw, mat, label, tags=()):
    """A box standing from z0 to z1 (meters) centered on mockup (x, y), `length` along Unreal yaw `yaw`."""
    return b.box(mu(x, y, (z0 + z1) * 0.5), (length, thickness, z1 - z0), mat, label, tags, yaw=yaw)


def arc_pieces(b, r, a0, a1, z0, z1, thickness, mat, label, step=4.0, hidden=False):
    """Straight pieces along an arc of radius r (meters) from angle a0 to a1 (degrees, ccw from east)."""
    span = math.radians(a1 - a0)
    n = max(1, int(math.ceil(abs(span) * r / step)))
    for i in range(n):
        am = math.radians(a0) + span * (i + 0.5) / n
        chord = 2.0 * r * math.sin(abs(span) / n * 0.5) + 0.05  # a hair long, so the joins close
        x, y = r * math.cos(am), r * math.sin(am)
        tangent = (-math.sin(am), math.cos(am))
        a = ring_box(b, x, y, z0, z1, chord, thickness, mu_yaw(*tangent), mat, label)
        if hidden:
            a.set_actor_hidden_in_game(True)
            a.static_mesh_component.set_editor_property("cast_shadow", False)


def polygon_wall(b, poly, z0, z1, mat, label):
    """A wall footprint from the JSON: a curved band (many points) or a rectangle (4 points)."""
    if len(poly) <= 5:
        pts = poly[:4]
        e1 = (pts[1][0] - pts[0][0], pts[1][1] - pts[0][1])
        e2 = (pts[2][0] - pts[1][0], pts[2][1] - pts[1][1])
        l1, l2 = math.hypot(*e1), math.hypot(*e2)
        along = e1 if l1 >= l2 else e2
        cx = sum(p[0] for p in pts) / 4.0
        cy = sum(p[1] for p in pts) / 4.0
        ring_box(b, cx, cy, z0, z1, max(l1, l2), min(l1, l2), mu_yaw(*along), mat, label)
        return
    rs = [math.hypot(x, y) for x, y in poly]
    r = (min(rs) + max(rs)) * 0.5
    thickness = max(rs) - min(rs)
    # Unwrap the angles along the outline; the arc runs from the smallest to the largest.
    angs = [math.degrees(math.atan2(poly[0][1], poly[0][0]))]
    for x, y in poly[1:]:
        a = math.degrees(math.atan2(y, x))
        while a - angs[-1] > 180.0:
            a -= 360.0
        while a - angs[-1] < -180.0:
            a += 360.0
        angs.append(a)
    a0, a1 = min(angs), max(angs)
    if a1 - a0 > 355.0:
        a0, a1 = 0.0, 360.0
    arc_pieces(b, r, a0, a1, z0, z1, thickness, mat, label)


def polyline_pieces(b, line, z0, z1, thickness, mat, label, step=4.0):
    """Straight pieces along a polyline (windows' sills and lintels, railings)."""
    run = [line[0]]
    total = 0.0
    for p in line[1:]:
        total += math.hypot(p[0] - run[-1][0], p[1] - run[-1][1])
        run.append(p)
        if total >= step or p is line[-1]:
            (x0, y0), (x1, y1) = run[0], run[-1]
            length = math.hypot(x1 - x0, y1 - y0)
            if length > 0.05:
                ring_box(b, (x0 + x1) * 0.5, (y0 + y1) * 0.5, z0, z1, length + 0.05, thickness, mu_yaw(x1 - x0, y1 - y0), mat, label)
            run = [p]
            total = 0.0


def in_rect(px, py, rect, grow=0.0):
    """Is mockup point (px, py) inside the rotated rectangle (center, length, thickness, yaw ccw from east)?"""
    cx, cy = rect["center"]
    t = math.radians(rect["yaw_deg_ccw_from_east"])
    dx, dy = px - cx, py - cy
    u = dx * math.cos(t) + dy * math.sin(t)
    v = -dx * math.sin(t) + dy * math.cos(t)
    return abs(u) <= rect["length"] * 0.5 + grow and abs(v) <= rect["thickness"] * 0.5 + grow


def slab_band(b, r0, r1, holes, mat, label, radial=2.0, tangential=2.0):
    """The floor-2 slab over a ring band r0..r1: cells about radial x tangential meters, with stair
    openings left out. Cells away from any opening merge along the band into longer pieces."""
    rings = max(1, int(round((r1 - r0) / radial)))
    for k in range(rings):
        ra = r0 + (r1 - r0) * k / rings
        rb = r0 + (r1 - r0) * (k + 1) / rings
        rm = (ra + rb) * 0.5
        n = max(8, int(math.ceil(2.0 * math.pi * rb / tangential)))
        keep = []
        for i in range(n):
            am = 2.0 * math.pi * (i + 0.5) / n
            # Leave the cell out if its middle or its corners fall in an opening.
            probe = [(rm, am)] + [(rr, am + s * math.pi / n) for rr in (ra, rb) for s in (-1, 1)]
            blocked = any(in_rect(rr * math.cos(aa), rr * math.sin(aa), h, 0.1) for h in holes for rr, aa in probe)
            keep.append(not blocked)
        # Runs of kept cells (wrapping around) become one piece each, at most 6 cells long.
        i = 0
        while i < n:
            if not keep[i]:
                i += 1
                continue
            j = i
            while j + 1 < n and keep[j + 1] and j + 1 - i < 6:
                j += 1
            a0 = 360.0 * i / n
            a1 = 360.0 * (j + 1) / n
            span = math.radians(a1 - a0)
            am = math.radians(a0) + span * 0.5
            chord = 2.0 * rb * math.sin(span * 0.5) + 0.05
            ring_box(b, rm * math.cos(am), rm * math.sin(am), FLOOR2 - SLAB, FLOOR2, chord, rb - ra + 0.02, mu_yaw(-math.sin(am), math.cos(am)), mat, label)
            i = j + 1


def ramp(b, s, z_bottom, z_top, mat):
    """A stair as a smooth ramp from its bottom point to its top point."""
    (x0, y0), (x1, y1) = s["bottom"], s["top"]
    run = math.hypot(x1 - x0, y1 - y0)
    rise = z_top - z_bottom
    pitch = math.degrees(math.atan2(rise, run))
    slope = math.hypot(run, rise)
    thick = 0.3
    cz = (z_bottom + z_top) * 0.5 - thick * 0.5 * math.cos(math.radians(pitch))
    # Stretch it a little past both ends so it meets the floor and the slab without a lip.
    b.box(mu((x0 + x1) * 0.5, (y0 + y1) * 0.5, cz), (slope + 0.6, s["thickness"], thick), mat, "Stairs", yaw=mu_yaw(x1 - x0, y1 - y0), pitch=pitch)


def build_ring_arena():
    new_level("/Game/WWO/Maps/L_ArenaRing")
    data = json.load(open(RING_JSON))
    b = Builder(1, 1, 7)
    lighting()
    ground = solid_material("RingGround", (0.18, 0.19, 0.21))
    ring_floor = solid_material("RingLane", (0.23, 0.22, 0.2))
    slab = solid_material("RingSlab", (0.26, 0.27, 0.3))
    wall = solid_material("RingWall", (0.55, 0.57, 0.6))
    ringwall = solid_material("RingWallRing", (0.45, 0.48, 0.52))
    cover = solid_material("RingCover", (0.25, 0.3, 0.4))
    crate = solid_material("RingCrate", (0.5, 0.36, 0.2))
    pillar = solid_material("RingPillar", (0.38, 0.4, 0.44))
    stairs = solid_material("RingStairs", (0.32, 0.34, 0.38))
    rail = solid_material("RingRail", (0.7, 0.72, 0.75))
    blue = solid_material("TeamBlue", (0.2, 0.4, 0.9))
    orange = solid_material("TeamOrange", (0.95, 0.5, 0.15))
    R = data["radii"]
    f1, f2 = data["floor1"], data["floor2"]

    # Ground: one slab under the whole map (its corners sit outside the 8 m boundary wall); the
    # ring's lane a shade lighter on top, so the open middle band reads from above.
    b.box(mu(0, 0, -0.25), (R["map"] * 2 + 4, R["map"] * 2 + 4, 0.5), ground, "Ground")
    b.box(mu(0, 0, -0.24), (R["ring_outer"] * 2, R["ring_outer"] * 2, 0.5), ring_floor, "Lane", mesh=CYLINDER)
    b.box(mu(0, 0, -0.23), (R["core"] * 2, R["core"] * 2, 0.5), ground, "CoreFloor", mesh=CYLINDER)
    # Walls: a wall on both floors with the same footprint is one 8 m piece.
    f2_keys = {(len(p), tuple(p[0])) for p in f2["walls_full_height"]}
    f1_keys = set()
    for poly in f1["walls_full_height"]:
        key = (len(poly), tuple(poly[0]))
        f1_keys.add(key)
        rs = [math.hypot(x, y) for x, y in poly]
        ringy = len(poly) > 5 and 21.0 < min(rs) < 33.0
        polygon_wall(b, poly, 0.0, 8.0 if key in f2_keys else FLOOR2, ringwall if ringy else wall, "Wall")
    for poly in f2["walls_full_height"]:
        if (len(poly), tuple(poly[0])) in f1_keys:
            continue
        rs = [math.hypot(x, y) for x, y in poly]
        ringy = len(poly) > 5 and 21.0 < min(rs) < 33.0
        polygon_wall(b, poly, FLOOR2, 8.0, ringwall if ringy else wall, "Wall2")
    # Floor-2 windows in the inner ring wall: open from the 5 m sill to 7 m, wall below and above.
    for line in f2["windows_polyline"]:
        polyline_pieces(b, line, FLOOR2, 5.0, R["wall_thickness"], ringwall, "WindowSill")
        polyline_pieces(b, line, 7.0, 8.0, R["wall_thickness"], ringwall, "WindowLintel")
    # Floor-2 ledges (outer ring wall, and the atrium's edge): open, with a railing.
    for line in f2["ledges_railing_polyline"]:
        polyline_pieces(b, line, FLOOR2, FLOOR2 + RAIL, 0.2, rail, "Railing")
    # Floor-2 slab: the core (outside the atrium hole) and the outer band, with the stair openings.
    holes = f2["stairs"]
    slab_band(b, 6.0, R["core"] + 0.3, holes, slab, "Slab")
    slab_band(b, R["ring_outer"] - 0.3, R["map"] + 0.4, holes, slab, "Slab")
    # Stairs: ramps rising from floor 1 to floor 2 (the same list is in both floors).
    for s in f1["stairs"]:
        ramp(b, s, 0.0, FLOOR2, stairs)
    # Cover, crates and pillars on both floors.
    for z, fl in ((0.0, f1), (FLOOR2, f2)):
        for c in fl["low_cover"]:
            x, y = c["center"]
            ring_box(b, x, y, z, z + LOW_COVER, c["length"], c["thickness"], 90.0 - c["yaw_deg_ccw_from_east"], cover, "LowCover", ("Cover",))
        for c in fl["crates"]:
            x, y = c["center"]
            h = (1.0, 1.3, 1.6)[int(round(abs(x) * 7.0 + abs(y) * 3.0)) % 3]  # mixed heights, the same on mirrored crates
            ring_box(b, x, y, z, z + h, c["length"], c["thickness"], 90.0 - c["yaw_deg_ccw_from_east"], crate, "Crate", ("Cover",))
        for p in fl["pillars"]:
            x, y = p["center"]
            r = math.hypot(x, y)
            top = 8.0 if (z == 0.0 and R["core"] < r < R["ring_outer"]) else z + FLOOR2  # the ring is open sky: full height
            b.box(mu(x, y, (z + top) * 0.5), (p["radius"] * 2, p["radius"] * 2, top - z), pillar, "Pillar", ("Cover",), mesh=CYLINDER)
    # Spawns: a Player Start on each point, facing the middle, on a team-colored pad.
    for team, mat in (("blue", blue), ("orange", orange)):
        for i, (x, y) in enumerate(f1["spawns"][team]):
            b.box(mu(x, y, 0.02), (2.2, 2.2, 0.04), mat, "SpawnPad", mesh=CYLINDER)
            place(unreal.PlayerStart, mu(x, y, 1.05), mu(0, 0, 1.05), 0.0, "PlayerStart_%s%d" % (team.capitalize(), i + 1))
    # Out of bounds: invisible walls above the 8 m boundary wall, a floor under everything, nav.
    arc_pieces(b, R["map"] + 0.3, 0.0, 360.0, 8.0, 30.0, 0.6, None, "Boundary", step=6.0, hidden=True)
    b.box(unreal.Vector(0, 0, -60.0), (R["map"] * 2 + 10, R["map"] * 2 + 10, 0.2), None, "SafetyFloor", hidden=True)
    vol = actors.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(0, 0, 500.0))
    ext = max(vol.get_actor_bounds(False)[1].x, 1.0)
    vol.set_actor_scale3d(unreal.Vector((R["map"] + 2) * 100.0 / ext, (R["map"] + 2) * 100.0 / ext, 700.0 / ext))
    levels.save_current_level()
    log("L_ArenaRing: %d pieces" % b.count)


# WWO_MAPS picks which to build (comma separated: range, large, arena, ring); all by default.
_wanted = os.environ.get("WWO_MAPS", "range,large,arena,ring").split(",")
if "range" in _wanted:
    build_test_range()
if "large" in _wanted:
    build_arena_large()
if "arena" in _wanted:
    build_arena()
if "ring" in _wanted:
    build_ring_arena()
