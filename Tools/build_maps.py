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
- L_Arboretum: the Arboretum PvP map, from its mockup's JSON (see build_arboretum); needs
  Tools/create_kit_concrete.py and Tools/import_flora.py to have run.
- L_FootRaceTest: the FootRace mode's gray-box test map (two lanes of rooms and a hub), from
  Tools/footrace_layout.py; layout only (build_footrace).
- L_Cascade: the 6v6 PvP map (ruins over waterfalls), from its mockup's JSON through Tools/cascade_layout.py;
  built by Tools/build_cascade.py with this file's helpers (concrete_instance, lamp_instance, fit_mesh,
  merge_kind with base/prefix), after Tools/create_kit_concrete.py and import_flora.py.
WWO_MAPS=ring (or range, large, arena, arboretum, cascade, footrace, comma separated) builds only those.
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
# The layout is Tools/ring_layout.py (plain Python, checked by Tools/check_ring.py): boxes in mockup
# meters (x east, y north, origin at the center). Unreal X = y * 100, Unreal Y = x * 100 (north up
# in the top view); a yaw counter-clockwise from east is Unreal yaw 90 - yaw.
# Gray box: flat-colored boxes, then merged into one mesh per kind (floor, walls, slab, railings,
# stairs, roof), collision included, so thousands of small pieces draw as a handful. Cover,
# crates and pillars stay separate actors tagged "Cover".
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ring_layout
import arboretum_layout
import footrace_layout

RING_COLORS = {
    "ground": (0.18, 0.19, 0.21), "lane": (0.23, 0.22, 0.2), "slab": (0.26, 0.27, 0.3), "wall": (0.55, 0.57, 0.6),
    "ringwall": (0.45, 0.48, 0.52), "cover": (0.25, 0.3, 0.4), "crate": (0.5, 0.36, 0.2), "pillar": (0.38, 0.4, 0.44),
    "stairs": (0.32, 0.34, 0.38), "rail": (0.7, 0.72, 0.75), "roof": (0.3, 0.31, 0.34),
}
RING_SEPARATE = ("cover", "crate", "pillar")  # stay their own actors (tagged Cover)
RING_MERGED = "/Game/WWO/Maps/Ring"


def mu(x, y, z=0.0):
    """A mockup point (meters) in Unreal cm."""
    return unreal.Vector(y * 100.0, x * 100.0, z * 100.0)


def merge_kind(kind, group, base=None, prefix="Ring"):
    """One static mesh (with the pieces' collision) for a kind; the pieces go."""
    base = base or RING_MERGED
    sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    opts = unreal.MergeStaticMeshActorsOptions()
    opts.set_editor_property("destroy_source_actors", True)
    opts.set_editor_property("spawn_merged_actor", True)
    opts.set_editor_property("base_package_name", base + "/SM_" + prefix + "_" + kind)
    settings = opts.get_editor_property("mesh_merging_settings")
    settings.set_editor_property("merge_physics_data", True)
    settings.set_editor_property("merge_materials", False)
    opts.set_editor_property("mesh_merging_settings", settings)
    # The subsystem isn't there when this runs headless; the older library call still is.
    merged = sub.merge_static_mesh_actors(group, opts) if sub else unreal.EditorLevelLibrary.merge_static_mesh_actors(group, opts)
    if merged:
        merged.set_actor_label(prefix + "_" + kind)
        merged.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)
        if kind == "roof" and prefix == "Ring":
            # The roof doesn't block the sun, so the arena stays lit under it (gray box).
            merged.static_mesh_component.set_editor_property("cast_shadow", False)
    return merged


def build_ring_arena():
    new_level("/Game/WWO/Maps/L_ArenaRing")
    if eal.does_directory_exist(RING_MERGED):
        eal.delete_directory(RING_MERGED)
    layout = ring_layout.build()
    b = Builder(1, 1, 7)
    lighting()
    mats = {k: solid_material("Ring_" + k, c) for k, c in RING_COLORS.items()}
    groups = {}
    for box in layout["boxes"]:
        a = b.box(mu(box.x, box.y, box.z), (box.length, box.thickness, box.height), mats[box.kind], "Ring_" + box.kind,
                  ("Cover",) if box.kind in RING_SEPARATE else (), yaw=90.0 - box.yaw, pitch=box.pitch,
                  mesh=CYLINDER if box.shape == "cylinder" else None)
        if box.kind == "roof":
            a.static_mesh_component.set_editor_property("cast_shadow", False)
        if box.kind not in RING_SEPARATE:
            groups.setdefault(box.kind, []).append(a)
    merged = 0
    for kind, group in groups.items():
        if len(group) < 2:
            continue
        try:
            if merge_kind(kind, group):
                merged += 1
        except Exception as e:  # keep the separate pieces if merging isn't available
            log("ring: couldn't merge %s (%s); keeping %d pieces" % (kind, e, len(group)))
    if merged:
        eal.save_directory(RING_MERGED, only_if_is_dirty=False, recursive=True)  # the merged meshes, before the level that uses them
    # Spawns: a Player Start on each point, facing the middle, on a team-colored pad.
    for team, color in (("blue", (0.2, 0.4, 0.9)), ("orange", (0.95, 0.5, 0.15))):
        mat = solid_material("Team" + team.capitalize(), color)
        for i, (x, y) in enumerate(layout["spawns"][team]):
            b.box(mu(x, y, 0.02), (2.2, 2.2, 0.04), mat, "SpawnPad", mesh=CYLINDER)
            place(unreal.PlayerStart, mu(x, y, 1.05), mu(0, 0, 1.05), 0.0, "PlayerStart_%s%d" % (team.capitalize(), i + 1))
    b.box(unreal.Vector(0, 0, -60.0), (110, 110, 0.2), None, "SafetyFloor", hidden=True)
    vol = actors.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(0, 0, 900.0))
    ext = max(vol.get_actor_bounds(False)[1].x, 1.0)
    vol.set_actor_scale3d(unreal.Vector(5200.0 / ext, 5200.0 / ext, 1100.0 / ext))
    levels.save_current_level()
    log("L_ArenaRing: %d boxes, %d kinds merged" % (len(layout["boxes"]), merged))


# --- FootRace test map (L_FootRaceTest) ------------------------------------------------------
# Layout only, from Tools/footrace_layout.py (mockup meters, the ring's axes): two lanes of rooms
# and a closed hub, as flat-colored boxes merged per kind, plus tagged TargetPoints the FootRace rules
# spawn their actors on (doors, portals, levers, the bosses, cores). Gray box: no props or textures.
# Cover (ruin_wall, boulder, stump) stays one actor per piece tagged Cover (how the Soldiers find it);
# spawn gates are merged but visual only (no collision, out of the navmesh); the boss platform and its
# ramps are solid and on the navmesh.
FOOTRACE_COLORS = {
    "floor": (0.3, 0.31, 0.33), "wall": (0.55, 0.57, 0.6), "hub_floor": (0.26, 0.3, 0.27),
    "boss_floor": (0.34, 0.24, 0.24), "spawn0": (0.2, 0.32, 0.6), "spawn1": (0.7, 0.4, 0.16),
    "ruin_wall": (0.46, 0.44, 0.4), "boulder": (0.36, 0.33, 0.28), "stump": (0.3, 0.22, 0.14),
    "spawn_gate": (0.02, 0.02, 0.025), "boss_platform": (0.4, 0.28, 0.27), "ramp": (0.4, 0.28, 0.27),
}
FOOTRACE_COVER = ("ruin_wall", "boulder", "stump")
FOOTRACE_VISUAL = ("spawn_gate",)


def visual_only(actor):
    """No collision and no say in the navmesh (a spawn gate: enemies walk out of it)."""
    comp = actor.static_mesh_component
    comp.set_collision_profile_name("NoCollision")
    comp.set_editor_property("can_ever_affect_navigation", False)
    comp.set_editor_property("cast_shadow", False)
FOOTRACE_MERGED = "/Game/WWO/Maps/FootRace"


def build_footrace():
    """L_FootRaceTest from Tools/footrace_layout.py, dressed as a forest ravine (Tools/footrace_dress.py; the Ravine
    and Cascade kits must be imported: Tools/import_cascade_kit.py, then Tools/import_ravine_kit.py).
    The layout's boxes keep the gameplay. What you see: the floors in forest floor, the cross walls and the hub's
    walls in weathered concrete carried up to 20 m (the extra height has no collision: the 12 m boxes already can't
    be climbed), the lanes' side walls hidden under the cliffs, the cover boxes hidden under kit pieces, the spawn
    gates' dark boxes replaced by cave mouths and culverts."""
    import types
    import build_cascade
    import footrace_dress
    new_level("/Game/WWO/Maps/L_FootRaceTest")
    if eal.does_directory_exist(FOOTRACE_MERGED):
        eal.delete_directory(FOOTRACE_MERGED)
    layout = footrace_layout.build()
    problems = footrace_layout.check(layout)
    if problems:
        raise RuntimeError("footrace layout: " + "; ".join(problems))
    b = Builder(1, 1, 11)
    ns = types.SimpleNamespace(**globals())
    # Cascade's sky, sun and haze, the sun turned to shine down the lanes (the cross walls throw the long shadows,
    # the cliffs only a strip along one side), the haze a little green.
    keep = (build_cascade.SUN_YAW, build_cascade.FOG_COLOR, build_cascade.SKY_LIGHT)
    build_cascade.SUN_YAW, build_cascade.FOG_COLOR, build_cascade.SKY_LIGHT = 68.0, (0.50, 0.66, 0.52), 1.15
    try:
        build_cascade.lighting(ns)
    finally:
        build_cascade.SUN_YAW, build_cascade.FOG_COLOR, build_cascade.SKY_LIGHT = keep
    rav, cas = "/Game/WWO/Maps/RavineKit/Materials/", "/Game/WWO/Maps/CascadeKit/Materials/"
    load = lambda path: unreal.load_asset(path) or (_ for _ in ()).throw(RuntimeError("missing %s: import the kits first" % path))   # noqa: E731
    forest, concrete, stone = load(rav + "MI_Rav_Forest"), load(rav + "MI_Rav_Concrete"), load(rav + "MI_Rav_Stone")
    mats = {"floor": forest, "boss_floor": forest, "spawn0": stone, "spawn1": stone, "hub_floor": load(cas + "MI_Cas_Paver"),
            "wall": concrete, "cliff_wall": None, "boss_platform": stone, "ramp": stone, "ruin_wall": None, "boulder": None, "stump": None}
    dresser = footrace_dress.Dresser(ns, layout)
    groups = {}
    cover = 0
    side_walls, runs = [], {}
    for box in layout["boxes"]:
        kind = box.kind
        if kind == "spawn_gate":
            continue                                   # the kit's cave mouths and culverts stand there (dresser.spawns)
        if kind == "wall":
            along_x = box.length > box.thickness
            if along_x and abs(box.y) > footrace_layout.HUB[1] * 0.5 + 1.0:
                kind = "cliff_wall"                    # a lane's side: collision only, the cliff is what you see
                side_walls.append(box)
            else:
                # One run per straight line of wall (a cross wall's pieces round its doorway are one run).
                key = ("x", round(box.x, 2), box.y > 0) if not along_x else ("y", round(box.y, 2), 0)
                runs.setdefault(key, []).append(box)
        hidden = kind == "cliff_wall" or kind in FOOTRACE_COVER
        tags = ("Cover",) if kind in FOOTRACE_COVER else ()
        a = b.box(mu(box.x, box.y, box.z), (box.length, box.thickness, box.height), mats[kind], "FootRace_" + kind, tags,
                  yaw=90.0 - box.yaw, pitch=box.pitch, hidden=hidden)
        if kind in FOOTRACE_COVER:
            dresser.cover(box, cover)
            cover += 1
            continue
        groups.setdefault(kind, []).append(a)
    # The man-made walls: each run carried up to the kit's wall top (looks only), then dressed.
    for key, boxes in runs.items():
        if key[0] == "x":
            lo = min(w.y - w.thickness * 0.5 for w in boxes)
            hi = max(w.y + w.thickness * 0.5 for w in boxes)
            x0, y0, x1, y1 = boxes[0].x, lo, boxes[0].x, hi
            size = (footrace_layout.WALL_T, hi - lo, footrace_dress.WALL_TOP - footrace_layout.WALL_H)
            door = [w for w in boxes if w.z > footrace_layout.WALL_H * 0.5 + 0.1]       # the lintel over a doorway
            doors = [d.y - lo for d in door]
        else:
            lo = min(w.x - w.length * 0.5 for w in boxes)
            hi = max(w.x + w.length * 0.5 for w in boxes)
            x0, y0, x1, y1 = lo, boxes[0].y, hi, boxes[0].y
            size = (hi - lo, footrace_layout.WALL_T, footrace_dress.WALL_TOP - footrace_layout.WALL_H)
            doors = []
        up = b.box(mu((x0 + x1) * 0.5, (y0 + y1) * 0.5, (footrace_dress.WALL_TOP + footrace_layout.WALL_H) * 0.5), (size[0], size[1], size[2]), concrete, "FootRace_wall_top", yaw=90.0)
        visual_only(up)
        up.static_mesh_component.set_editor_property("cast_shadow", True)
        groups.setdefault("wall_top", []).append(up)
        gates = []
        for m in layout["markers"]:
            if m.tags[0] != "EnemySpawn":
                continue
            if key[0] == "x" and abs(m.x - x0) < footrace_layout.MARKER_IN + 0.5 and lo <= m.y <= hi:
                gates.append(m.y - lo)
            elif key[0] == "y" and abs(m.y - y0) < footrace_layout.MARKER_IN + 0.5 and lo <= m.x <= hi:
                gates.append(m.x - lo)
        dresser.wall(x0, y0, x1, y1, doors, gates)
    dresser.cliffs(side_walls)
    dresser.spawns(sorted(set(round(w.y, 2) for w in side_walls)))
    for box in layout["boxes"]:
        if box.kind == "boss_platform":
            dresser.platform(box)
    dresser.floor()
    merged = 0
    for kind, group in groups.items():
        if len(group) < 2:
            continue
        try:
            m = merge_kind(kind, group, base=FOOTRACE_MERGED, prefix="FootRace")
            if m:
                merged += 1
                if kind == "cliff_wall":
                    m.set_actor_hidden_in_game(True)
                    m.static_mesh_component.set_editor_property("cast_shadow", False)
                if kind == "wall_top":
                    m.static_mesh_component.set_collision_profile_name("NoCollision")
                    m.static_mesh_component.set_editor_property("can_ever_affect_navigation", False)
        except Exception as e:  # keep the separate pieces if merging isn't available
            log("footrace: couldn't merge %s (%s); keeping %d pieces" % (kind, e, len(group)))
    if merged:
        eal.save_directory(FOOTRACE_MERGED, only_if_is_dirty=False, recursive=True)
    instances = dresser.flush()
    for i, m in enumerate(layout["markers"]):
        at = mu(m.x, m.y, m.z)
        rot = unreal.Rotator(0.0, 0.0, 90.0 - m.yaw)
        if m.tags[0] == "PlayerStart":
            actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(at.x, at.y, at.z + 95.0), rot).set_actor_label("PlayerStart")
            continue
        if m.size:  # an area: unturned, its scale the size in m (Unreal X = mockup y)
            rot = unreal.Rotator(0.0, 0.0, 0.0)
        point = actors.spawn_actor_from_class(unreal.TargetPoint, at, rot)
        point.set_editor_property("tags", m.tags)
        if m.size:
            point.set_actor_scale3d(unreal.Vector(m.size[1], m.size[0], m.size[2]))
        point.set_actor_label("%s_%d" % ("_".join(m.tags), i))
    x0, x1, y0, y1 = footrace_layout.bounds()
    cx, cy = (x0 + x1) * 0.5, (y0 + y1) * 0.5
    b.box(mu(cx, cy, -0.6), (y1 - y0 + 10.0, x1 - x0 + 10.0, 0.2), None, "SafetyFloor", hidden=True)
    vol = actors.spawn_actor_from_class(unreal.NavMeshBoundsVolume, mu(cx, cy, 4.0))
    ext = max(vol.get_actor_bounds(False)[1].x, 1.0)
    vol.set_actor_scale3d(unreal.Vector((y1 - y0) * 50.0 / ext, (x1 - x0) * 50.0 / ext, 600.0 / ext))
    levels.save_current_level()
    log("L_FootRaceTest: %d boxes (%d cover), %d kinds merged, %d markers, %d kit instances" % (len(layout["boxes"]), cover, merged, len(layout["markers"]), instances))


# --- Arboretum (L_Arboretum) -----------------------------------------------------------------
# The PvP map from ~/Documents/WWO_PvP_Mockups (arboretum_map.py's JSON, through
# Tools/arboretum_layout.py): a ring corridor round a center room with outer rooms off it and a
# tunnel underneath. Structure is boxes merged per kind and surfaced with the weathered concrete
# (M_WWO_KitConcrete projects in world space, so plain boxes take it), greenhouse roofs and the
# center skylight are the dusty glass, crates are the kit's, planters carry the red tropical
# plants (Tools/import_flora.py) and the spring tree stands in the middle. Opaque roofs shade the
# inside, so it is lit by warm point lights, with the sun coming in through the glass.
# Decoration: pipes (flat-colored cylinders) run round the ring, the rooms and the tunnel, with the
# kit's pipe clusters on the ring wall; every light has a housing and a glowing panel (M_WWO_Lamp);
# plants in six sizes crowd the planters and the walls, with fallen leaves on the floor.
ARB_MERGED = "/Game/WWO/Maps/Arboretum"
ARB_SEPARATE = ("cover", "crate", "planter", "root")     # stay their own actors
ARB_NO_SHADOW = ("glass", "lamp", "lamp_tunnel")
FLORA = "/Game/WWO/Flora/Meshes/SM_"
ARB_PLANTS = {
    "small": ["Tropical_Plant_3", "Tropical_Plant_2_1", "Tropical_Plant_6", "Tropical_Plant_1_1", "Tropical_Plant_9"],
    "mid": ["Tropical_Plant_2_2", "Tropical_Plant_1_2", "Tropical_Plant_7_1", "Tropical_Plant_8"],
    "big": ["Tropical_Plant_4", "Tropical_Plant_5", "Tropical_Plant_7_2", "Tropical_Plant_8"],
}
ARB_PLANTS["tiny"] = ARB_PLANTS["small"]
ARB_PLANTS["huge"] = ["Tropical_Plant_5", "Tropical_Plant_4", "Tropical_Plant_7_2"]
ARB_PLANTS["leaf"] = ["Tropical_Ground_Leaves"]
ARB_PLANT_SCALE = {"tiny": (0.28, 0.5), "small": (0.6, 1.1), "mid": (0.65, 1.05), "big": (0.6, 1.0), "huge": (1.0, 1.4), "leaf": (1.2, 2.6)}
# where: (candelas, color, attenuation radius cm)
ARB_LIGHTS = {
    "ring": (110.0, (1.0, 0.84, 0.64), 1600.0), "connector": (80.0, (1.0, 0.84, 0.64), 1400.0),
    "center": (120.0, (1.0, 0.9, 0.78), 2000.0), "room": (150.0, (1.0, 0.88, 0.72), 2400.0),
    "greenhouse": (190.0, (1.0, 0.92, 0.82), 2800.0), "tunnel": (40.0, (1.0, 0.62, 0.42), 1300.0),
    "fill": (28.0, (1.0, 0.92, 0.82), 2600.0),      # low in each room, so faces the ceiling lights miss aren't black
}


def concrete_instance(name, tint=None, **scalars):
    """An instance of the weathered concrete for plain boxes (no kit textures behind it)."""
    folder = "/Game/WWO/Kit/Materials/Concrete"
    path = folder + "/MI_Arb_" + name
    if eal.does_asset_exist(path):
        mi = eal.load_asset(path)
    else:
        mi = tools.create_asset("MI_Arb_" + name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    parent = unreal.load_asset("/Game/WWO/Materials/M_WWO_KitConcrete.M_WWO_KitConcrete")
    if not parent:
        raise RuntimeError("missing M_WWO_KitConcrete: run Tools/create_kit_concrete.py first")
    mi.set_editor_property("parent", parent)
    if tint:
        mel.set_material_instance_vector_parameter_value(mi, "ConcreteTint", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
    for k, v in scalars.items():
        mel.set_material_instance_scalar_parameter_value(mi, k, v)
    eal.save_loaded_asset(mi)
    return mi


def lamp_instance(name, color, glow):
    """A glowing panel for a light fixture: M_WWO_Lamp (unlit: Color x Glow), made on first use."""
    path = "/Game/WWO/Materials/M_WWO_Lamp"
    parent = eal.load_asset(path) if eal.does_asset_exist(path) else None
    if not parent:
        parent = tools.create_asset("M_WWO_Lamp", "/Game/WWO/Materials", unreal.Material, unreal.MaterialFactoryNew())
        parent.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        c = mel.create_material_expression(parent, unreal.MaterialExpressionVectorParameter, -500, 0)
        c.set_editor_property("parameter_name", "Color")
        c.set_editor_property("default_value", unreal.LinearColor(1.0, 0.85, 0.65, 1.0))
        k = mel.create_material_expression(parent, unreal.MaterialExpressionScalarParameter, -500, 200)
        k.set_editor_property("parameter_name", "Glow")
        k.set_editor_property("default_value", 4.0)
        mul = mel.create_material_expression(parent, unreal.MaterialExpressionMultiply, -250, 100)
        mel.connect_material_expressions(c, "", mul, "A")
        mel.connect_material_expressions(k, "", mul, "B")
        mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        mel.recompile_material(parent)
        eal.save_loaded_asset(parent)
    folder = "/Game/WWO/Materials/Map"
    ipath = folder + "/MI_Lamp_" + name
    mi = eal.load_asset(ipath) if eal.does_asset_exist(ipath) else tools.create_asset(
        "MI_Lamp_" + name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", parent)
    mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(color[0], color[1], color[2], 1.0))
    mel.set_material_instance_scalar_parameter_value(mi, "Glow", glow)
    eal.save_loaded_asset(mi)
    return mi


def fit_mesh(mesh, x, y, z0, size, yaw, label, tags=()):
    """A mesh scaled and placed so its bounds fill a mockup box: center (x, y), base z0, size
    (length along yaw, thickness, height) in meters."""
    bb = mesh.get_bounding_box()
    ext = unreal.Vector(max(bb.max.x - bb.min.x, 1.0), max(bb.max.y - bb.min.y, 1.0), max(bb.max.z - bb.min.z, 1.0))
    scale = unreal.Vector(size[0] * 100.0 / ext.x, size[1] * 100.0 / ext.y, size[2] * 100.0 / ext.z)
    cx, cy, cz = [(a + b) * 0.5 * k for a, b, k in ((bb.min.x, bb.max.x, scale.x), (bb.min.y, bb.max.y, scale.y), (bb.min.z, bb.max.z, scale.z))]
    uyaw = 90.0 - yaw
    t = math.radians(uyaw)
    target = mu(x, y, z0 + size[2] * 0.5)
    loc = unreal.Vector(target.x - (cx * math.cos(t) - cy * math.sin(t)), target.y - (cx * math.sin(t) + cy * math.cos(t)), target.z - cz)
    a = actors.spawn_actor_from_class(unreal.StaticMeshActor, loc, unreal.Rotator(0.0, 0.0, uyaw))
    a.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)
    a.static_mesh_component.set_static_mesh(mesh)
    a.set_actor_scale3d(scale)
    a.set_actor_label(label)
    if tags:
        a.set_editor_property("tags", list(tags))
    return a


def build_arboretum():
    new_level("/Game/WWO/Maps/L_Arboretum")
    if eal.does_directory_exist(ARB_MERGED):
        eal.delete_directory(ARB_MERGED)
    layout = arboretum_layout.build()
    b = Builder(1, 1, 11)
    rng = random.Random(11)
    lighting()
    wall = concrete_instance("Wall")
    floor = concrete_instance("Floor", (0.35, 0.335, 0.305), KitContrast=0.0, DirtStrength=0.85)
    tunnel = concrete_instance("Tunnel", (0.29, 0.27, 0.245), KitContrast=0.0, DirtStrength=0.9)
    roof = concrete_instance("Roof", (0.32, 0.31, 0.29), KitContrast=0.0, Grime=0.5)
    # Small pieces: the foot grime and streaks are sized for walls, so turn them down.
    planter = concrete_instance("Planter", (0.385, 0.37, 0.335), KitContrast=0.0, BaseStrength=0.3, BaseHeight=30.0, StreakStrength=0.6)
    cover = concrete_instance("Cover", (0.415, 0.4, 0.36), KitContrast=0.0, BaseStrength=0.3, BaseHeight=30.0, StreakStrength=0.6)
    glass = unreal.load_asset("/Game/WWO/Kit/Materials/Concrete/MI_KitConcrete_M_Glass")
    mats = {"floor": floor, "ramp": floor, "wall": wall, "wall_tall": wall, "wall_center": wall, "buttress": wall, "cover": cover,
            "roof": roof, "glass": glass, "tunnel_floor": tunnel, "tunnel_wall": tunnel, "planter": planter,
            "rail": solid_material("ArbRail", (0.1, 0.1, 0.11)), "frame": solid_material("ArbRail", (0.1, 0.1, 0.11)),
            "fixture": solid_material("ArbRail", (0.1, 0.1, 0.11)), "pipe_dark": solid_material("ArbRail", (0.1, 0.1, 0.11)),
            "pipe": solid_material("ArbPipe", (0.36, 0.4, 0.38)), "pipe_rust": solid_material("ArbPipeRust", (0.45, 0.22, 0.1)),
            "lamp": lamp_instance("Warm", (1.0, 0.84, 0.62), 5.0), "lamp_tunnel": lamp_instance("Tunnel", (1.0, 0.55, 0.3), 4.0), "root": concrete_instance("Root", (0.3, 0.2, 0.12), KitContrast=0.0, DirtStrength=1.0)}
    crate_meshes = [kit_mesh("Prop_Crate3"), kit_mesh("Prop_Crate4")]
    groups = {}
    crates = 0
    for box in layout["boxes"]:
        if box.kind == "crate":
            fit_mesh(crate_meshes[crates % 2], box.x, box.y, box.z - box.height * 0.5, (box.length, box.thickness, box.height),
                     box.yaw, "Arb_crate_%d" % crates, ("Cover",))
            crates += 1
            continue
        a = b.box(mu(box.x, box.y, box.z), (box.length, box.thickness, box.height), mats[box.kind], "Arb_" + box.kind,
                  ("Cover",) if box.kind in ARB_SEPARATE and box.kind != "root" else (), yaw=90.0 - box.yaw, pitch=box.pitch,
                  mesh=CYLINDER if box.shape == "cylinder" else None)
        if box.kind in ARB_NO_SHADOW:
            a.static_mesh_component.set_editor_property("cast_shadow", False)   # the sun comes in through the glass
        if box.kind not in ARB_SEPARATE:
            groups.setdefault(box.kind, []).append(a)
    merged = 0
    for kind, group in groups.items():
        if len(group) < 2:
            continue
        try:
            m = merge_kind(kind, group, ARB_MERGED, "Arb")
            if m:
                merged += 1
                if kind in ARB_NO_SHADOW:
                    m.static_mesh_component.set_editor_property("cast_shadow", False)
        except Exception as e:  # keep the separate pieces if merging isn't available
            log("arboretum: couldn't merge %s (%s); keeping %d pieces" % (kind, e, len(group)))
    if merged:
        eal.save_directory(ARB_MERGED, only_if_is_dirty=False, recursive=True)
    # The kit's pipe clusters against the ring's outer wall.
    for i, (name, x, y, z0, size, yaw) in enumerate(layout["props"]):
        fit_mesh(kit_mesh(name), x, y, z0, size, yaw, "Arb_%s_%d" % (name, i))
    # Plants on the planters and along the walls, fallen leaves, and the tree in the middle.
    for i, (x, y, z, size) in enumerate(layout["plants"]):
        mesh = unreal.load_asset(FLORA + rng.choice(ARB_PLANTS[size]))
        a = actors.spawn_actor_from_class(unreal.StaticMeshActor, mu(x, y, z - 0.05), unreal.Rotator(0.0, 0.0, rng.uniform(0.0, 360.0)))
        a.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)
        a.static_mesh_component.set_static_mesh(mesh)
        a.set_actor_scale3d(unreal.Vector(1, 1, 1) * rng.uniform(*ARB_PLANT_SCALE[size]))
        a.set_actor_label("Arb_plant_%d" % i)
    planter_top = layout["data"]["heights_m"]["planter"]
    for part in ("Trunk", "Crown", "Planter", "Petals"):
        a = actors.spawn_actor_from_class(unreal.StaticMeshActor, mu(0, 0, planter_top - 0.02), unreal.Rotator(0.0, 0.0, 0.0))
        a.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)
        a.static_mesh_component.set_static_mesh(unreal.load_asset(FLORA + "SpringTree_" + part))
        a.set_actor_scale3d(unreal.Vector(1.15, 1.15, 1.15))
        a.set_actor_label("Arb_tree_" + part)
    # Lights.
    for i, (x, y, z, where) in enumerate(layout["lights"]):
        candelas, color, radius = ARB_LIGHTS[where]
        a = actors.spawn_actor_from_class(unreal.PointLight, mu(x, y, z), unreal.Rotator(0.0, 0.0, 0.0))
        c = a.get_component_by_class(unreal.PointLightComponent)
        c.set_mobility(unreal.ComponentMobility.MOVABLE)
        c.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
        c.set_intensity(candelas)
        c.set_light_color(unreal.LinearColor(color[0], color[1], color[2], 1.0))
        c.set_editor_property("attenuation_radius", radius)
        c.set_editor_property("cast_shadows", False)
        a.set_actor_label("Arb_light_%s_%d" % (where, i))
    # Spawns: a Player Start on each point, facing the middle, on a team-colored pad.
    for team, color in (("blue", (0.2, 0.4, 0.9)), ("orange", (0.95, 0.5, 0.15))):
        mat = solid_material("Team" + team.capitalize(), color)
        for i, (x, y) in enumerate(layout["spawns"][team]):
            b.box(mu(x, y, 0.02), (2.2, 2.2, 0.04), mat, "SpawnPad", mesh=CYLINDER)
            place(unreal.PlayerStart, mu(x, y, 1.05), mu(0, 0, 1.05), 0.0, "PlayerStart_%s%d" % (team.capitalize(), i + 1))
    x0, x1, y0, y1 = layout["bounds"]
    b.box(unreal.Vector(0, 0, layout["tunnel_z"] * 100.0 - 300.0), (y1 - y0 + 10, x1 - x0 + 10, 0.2), None, "SafetyFloor", hidden=True)
    vol = actors.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector((y0 + y1) * 50.0, 0, 250.0))
    ext = max(vol.get_actor_bounds(False)[1].x, 1.0)
    vol.set_actor_scale3d(unreal.Vector((y1 - y0 + 4) * 50.0 / ext, (x1 - x0 + 4) * 50.0 / ext, 800.0 / ext))
    levels.save_current_level()
    log("L_Arboretum: %d boxes, %d kinds merged, %d crates, %d kit props, %d plants, %d lights"
        % (len(layout["boxes"]), merged, crates, len(layout["props"]), len(layout["plants"]), len(layout["lights"])))


# WWO_MAPS picks which to build (comma separated: range, large, arena, ring, arboretum, cascade, footrace); all by default.
_wanted = os.environ.get("WWO_MAPS", "range,large,arena,ring,arboretum,cascade,footrace").split(",")
if "range" in _wanted:
    build_test_range()
if "large" in _wanted:
    build_arena_large()
if "arena" in _wanted:
    build_arena()
if "ring" in _wanted:
    build_ring_arena()
if "arboretum" in _wanted:
    build_arboretum()
if "footrace" in _wanted:
    build_footrace()
if "cascade" in _wanted:
    import types
    import build_cascade
    build_cascade.build(types.SimpleNamespace(**globals()))   # this file's helpers, as build_cascade's ns
# Run inside the full editor (merging needs it): UnrealEditor ... -ExecutePythonScript=<full path>/Tools/build_maps.py
# -ExecutePythonScript closes the editor itself when the script ends; WWO_QUIT=1 is only for running this from
# an open editor's console (with -ExecutePythonScript as well, the two exits collide and the editor crashes on
# its way out, after the maps are saved). Delete a map's .umap before rebuilding it.
if os.environ.get("WWO_QUIT") == "1":
    unreal.SystemLibrary.quit_editor()
