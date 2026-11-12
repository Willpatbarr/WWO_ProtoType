"""A low-poly Blender blockout of L_Cascade, from Tools/cascade_layout.py's boxes (1 Blender unit =
1 m, x east, y north, z up), for looking at the map's proportions. No Unreal needed.

  /Applications/Blender.app/Contents/MacOS/Blender -b --python Tools/blender_cascade.py -- [out.blend]

Default output: ~/Documents/WWO_PvP_Mockups/cascade_build/Cascade_Blockout.blend (plus an overview
and a close-up render next to it, and pov/POV_<player>_<view>.png: what each yellow player-sized
capsule sees, at the game's eye height and field of view). One object per kind of piece, in collections (Decks,
Structure, Roofs, Cover, Rails, Trim, Scenery, Markers), so roofs or the building can be hidden to
see inside. Cylinders have 16 sides; the far mesas and arches are kept, the plants are markers.
"""
import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cascade_layout as L  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[0] if argv else os.path.expanduser("~/Documents/WWO_PvP_Mockups/cascade_build/Cascade_Blockout.blend")

GROUPS = {
    "Decks": ("floor", "floor_z3", "floor_z5", "balcony", "dais", "plinth", "landing", "bridge", "steps"),
    "Structure": ("hall_wall", "body", "wall", "tunnel_wall", "gallery_wall", "pier", "islet", "arch", "bracket"),
    "Roofs": ("roof", "tunnel_roof", "gallery_roof"),
    "Cover": ("low_cover", "block", "crate", "pillar", "monolith", "column"),
    "Rails": ("rail",),
    "Trim": ("trim", "ivy"),
    "Scenery": ("giant_arch", "mesa"),
}
COLORS = {
    "floor": "#a59e8f", "floor_z3": "#c9c2b0", "floor_z5": "#e6d9a8", "balcony": "#d9cc95", "dais": "#bdb08a", "plinth": "#8f8878",
    "landing": "#a59e8f", "bridge": "#b9b2a2", "steps": "#8c8676", "hall_wall": "#6f6a5c", "body": "#5e5a4f", "wall": "#4a4740",
    "roof": "#57534a", "tunnel_wall": "#6b6658", "tunnel_roof": "#8b8574", "gallery_wall": "#6b6658", "gallery_roof": "#8b8574",
    "rail": "#e8c95a", "pier": "#4f4b43", "islet": "#55524a", "arch": "#7d776a", "bracket": "#5b574d", "low_cover": "#5f8a4f",
    "block": "#4f6f8f", "crate": "#b07a3c", "pillar": "#2b2b2b", "monolith": "#2b2b2b", "column": "#e08a2c", "trim": "#ff8a26",
    "ivy": "#3f6b2f", "giant_arch": "#6d675b", "mesa": "#5c574c",
}
SIDES = 16


def rgba(hexcolor, a=1.0):
    h = hexcolor.lstrip("#")
    srgb = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    lin = [c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4 for c in srgb]
    return (lin[0], lin[1], lin[2], a)


def material(name, color):
    m = bpy.data.materials.new(name)
    m.diffuse_color = color                       # the solid viewport and Workbench use this
    if hasattr(m, "use_nodes") and not m.node_tree:
        m.use_nodes = True                        # older Blender; 5.x materials have nodes already
    bsdf = m.node_tree.nodes.get("Principled BSDF") if m.node_tree else None
    if bsdf:
        bsdf.inputs["Base Color"].default_value = color
        bsdf.inputs["Roughness"].default_value = 0.9
    return m


def box_geometry(b, verts, faces):
    """Append a ring_layout.Box (cube or 16-sided cylinder) to verts/faces, in world meters."""
    base = len(verts)
    if b.shape == "cylinder":
        r = b.length * 0.5
        z0, z1 = b.z - b.height * 0.5, b.z + b.height * 0.5
        for z in (z0, z1):
            for i in range(SIDES):
                a = 2.0 * math.pi * i / SIDES
                verts.append((b.x + r * math.cos(a), b.y + r * math.sin(a), z))
        faces.append(tuple(base + i for i in reversed(range(SIDES))))
        faces.append(tuple(base + SIDES + i for i in range(SIDES)))
        for i in range(SIDES):
            j = (i + 1) % SIDES
            faces.append((base + i, base + j, base + SIDES + j, base + SIDES + i))
        return
    t, p = math.radians(b.yaw), math.radians(b.pitch)
    d = (math.cos(t) * math.cos(p), math.sin(t) * math.cos(p), math.sin(p))     # along the length (pitched)
    s = (-math.sin(t), math.cos(t), 0.0)                                         # across
    n = (-math.cos(t) * math.sin(p), -math.sin(t) * math.sin(p), math.cos(p))   # up
    hl, hw, hh = b.length * 0.5, b.thickness * 0.5, b.height * 0.5
    for su, sv, sw in ((-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1), (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)):
        verts.append(tuple(c + su * hl * d[k] + sv * hw * s[k] + sw * hh * n[k] for k, c in enumerate((b.x, b.y, b.z))))
    for f in ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)):
        faces.append(tuple(base + i for i in f))


PLAYER_RADIUS, PLAYER_HEIGHT = 0.5, 2.0       # AWWOCharacter's capsule (50 cm radius, 100 cm half-height)
EYE_HEIGHT = 1.6                              # its camera: 60 cm above the capsule's center


def capsule_geometry(x, y, z_floor, verts, faces, r=PLAYER_RADIUS, h=PLAYER_HEIGHT, rings=4):
    """A low-poly capsule standing on z_floor: a cylinder with half-sphere ends."""
    base = len(verts)
    lats = [(-math.pi / 2 + (math.pi / 2) * i / rings) for i in range(1, rings + 1)]          # bottom cap, up to the equator
    lats += [(math.pi / 2) * i / rings for i in range(0, rings)]                                # top cap, from the equator
    centers = [z_floor + r] * rings + [z_floor + h - r] * rings
    verts.append((x, y, z_floor))
    for lat, cz in zip(lats, centers):
        for i in range(SIDES):
            a = 2.0 * math.pi * i / SIDES
            verts.append((x + r * math.cos(lat) * math.cos(a), y + r * math.cos(lat) * math.sin(a), cz + r * math.sin(lat)))
    verts.append((x, y, z_floor + h))
    top = len(verts) - 1
    nring = len(lats)
    ring = lambda k, i: base + 1 + k * SIDES + (i % SIDES)
    for i in range(SIDES):
        faces.append((base, ring(0, i + 1), ring(0, i)))
        faces.append((top, ring(nring - 1, i), ring(nring - 1, i + 1)))
        for k in range(nring - 1):
            faces.append((ring(k, i), ring(k, i + 1), ring(k + 1, i + 1), ring(k + 1, i)))


def player_object(name, spots, mat, collection):
    verts, faces = [], []
    for x, y, z in spots:
        capsule_geometry(x, y, z, verts, faces)
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    ob = bpy.data.objects.new(name, me)
    ob.data.materials.append(mat)
    collection.objects.link(ob)
    return ob


def mesh_object(name, boxes, mat, collection):
    verts, faces = [], []
    for b in boxes:
        box_geometry(b, verts, faces)
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    ob = bpy.data.objects.new(name, me)
    ob.data.materials.append(mat)
    collection.objects.link(ob)
    return ob


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    out = L.build()
    by_kind = {}
    for b in out["boxes"]:
        if b.kind == "ramp":
            continue                                  # hidden collision; the steps show the stairs
        by_kind.setdefault(b.kind, []).append(b)
    cols = {}
    for g in list(GROUPS) + ["Markers"]:
        cols[g] = bpy.data.collections.new(g)
        scene.collection.children.link(cols[g])
    group_of = {k: g for g, ks in GROUPS.items() for k in ks}
    for kind, boxes in sorted(by_kind.items()):
        mesh_object(kind, boxes, material("M_" + kind, rgba(COLORS.get(kind, "#ff00ff"))), cols[group_of.get(kind, "Structure")])

    # Markers: spawns (cones), PvE enemy spawns, lights, maples, waterfalls, a 1.8 m person for scale, the water.
    mk = cols["Markers"]

    def marker(name, kind, pts, size, color):
        boxes = []
        for (x, y, z) in pts:
            if kind == "cyl":
                boxes.append(L.Box(x, y, z + size[1] * 0.5, size[0], size[0], size[1], 0.0, name, shape="cylinder"))
            else:
                boxes.append(L.Box(x, y, z + size[2] * 0.5, size[0], size[1], size[2], 0.0, name))
        return mesh_object(name, boxes, material("M_" + name, rgba(color)), mk)

    marker("spawn_blue", "cyl", [(x, y, z) for x, y, z, _ in out["spawns"]["blue"]], (1.0, 1.8), "#3a78e0")
    marker("spawn_orange", "cyl", [(x, y, z) for x, y, z, _ in out["spawns"]["orange"]], (1.0, 1.8), "#f08a2a")
    marker("pve_enemy_spawn", "box", [(x, y, z) for x, y, z, _ in out["pve"]["enemies"]], (0.8, 0.8, 1.8), "#d83a3a")
    marker("lights", "box", [(x, y, z - 0.2) for x, y, z, *_ in out["lights"]], (0.4, 0.4, 0.4), "#ffd27a")
    marker("maples", "cyl", [(x, y, z) for k, x, y, z in out["plants"] if k == "maple"], (5.0, 7.0), "#cc3333")
    marker("ferns", "cyl", [(x, y, z) for k, x, y, z in out["plants"] if k == "fern"], (0.8, 0.6), "#3f7a2f")
    # Player-sized capsules (0.5 m radius, 2 m tall), bright yellow: on each node's dais, in each hall
    # doorway, and one in the Blue base.
    data = out["data"]
    nd = data["radii"]["node_distance"] / math.sqrt(2.0)
    el = data["elevations_m"]
    # Each is its own object (in Players), so a point-of-view render can hide the one it stands in.
    # players: name -> ((x, y, floor z), [(view name, look-at x, y)])
    k = data["map"].get("plan_scale", 1.0)
    players = {}
    for sx in (1, -1):
        for sy in (1, -1):
            tag = ("N" if sy > 0 else "S") + ("E" if sx > 0 else "W")
            x, y = sx * nd, sy * nd
            link = [b for b in data["bridges"] if b["kind"] == "base_link" and math.dist(b["end"], (x, y)) < data["radii"]["node_radius"] + 1.0][0]
            players["Node" + tag] = ((x, y, el["node_" + tag] + data["heights_m"]["node_dais_step"]),
                                     [("to_citadel", 0.0, 0.0), ("to_base_door", link["start"][0], link["start"][1])])
    hall = data["citadel"]["hall"]
    r_door = (hall["r_clear"] + hall["wall_r_out"]) * 0.5
    for d in data["citadel"]["doors"]:
        a = math.radians(d["angle_deg"])
        tag = {0: "E", 90: "N", 180: "W", 270: "S"}[int(d["angle_deg"]) % 360]
        players["Door" + tag] = ((r_door * math.cos(a), r_door * math.sin(a), d["sill_z"]),
                                 [("out", 60.0 * math.cos(a), 60.0 * math.sin(a)), ("into_hall", 0.0, 0.0)])
    players["BlueBase"] = ((-58.0 * k, 6.0 * k, 0.0), [("out_the_arm", 0.0, 6.0 * k)])
    pcol = bpy.data.collections.new("Players")
    scene.collection.children.link(pcol)
    pmat = material("M_player_scale", rgba("#ffe600"))
    pobs = {n: player_object("player_" + n, [spot], pmat, pcol) for n, (spot, _) in players.items()}
    falls = [L.Box(w["at"][0], w["at"][1], (L.WATER_Z + w["top_z"]) * 0.5, 0.1, w["width"], w["top_z"] - L.WATER_Z, w["facing_deg"], "waterfall")
             for w in out["waterfalls"]]
    mesh_object("waterfalls", falls, material("M_waterfall", rgba("#bfe6ff", 0.6)), mk)
    mesh_object("water", [L.Box(0, 0, L.WATER_Z - 0.05, 400.0, 400.0, 0.1, 0.0, "water")], material("M_water", rgba("#1f5866")), mk)

    # A sun and a camera looking down from the south-west, and the solid view set to show colors.
    sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN"))
    sun.rotation_euler = (math.radians(52), 0.0, math.radians(200))
    scene.collection.objects.link(sun)
    cam = bpy.data.objects.new("Camera", bpy.data.cameras.new("Camera"))
    cam.location = (-58.0 * k, -72.0 * k, 78.0 * k)
    scene.collection.objects.link(cam)
    target = bpy.data.objects.new("CameraTarget", None)
    target.location = (0.0, 0.0, 0.0)
    scene.collection.objects.link(target)
    c = cam.constraints.new("TRACK_TO")
    c.target = target
    c.track_axis = "TRACK_NEGATIVE_Z"
    c.up_axis = "UP_Y"
    cam.data.lens = 32.0
    cam.data.clip_end = 1000.0
    scene.camera = cam
    scene.world = bpy.data.worlds.new("Sky")
    scene.world.color = rgba("#a9c7dc")[:3]
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.render.resolution_x, scene.render.resolution_y = 1800, 1100
    scene.unit_settings.system = "METRIC"

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=OUT)
    scene.render.filepath = os.path.splitext(OUT)[0] + ".png"
    bpy.ops.render.render(write_still=True)
    # A closer look: the east hall door and the NE / SE nodes, at about eye height on the ring.
    cam.location = (40.0 * k, -6.0 * k, 9.0)
    target.location = (18.0 * k, 4.0 * k, 3.5)
    cam.data.lens = 24.0
    scene.render.filepath = os.path.splitext(OUT)[0] + "_Closeup.png"
    bpy.ops.render.render(write_still=True)
    cam.location = (-58.0 * k, -72.0 * k, 78.0 * k)  # back to the overview for the saved file's camera
    target.location = (0.0, 0.0, 0.0)
    cam.data.lens = 32.0
    # Points of view: from inside each yellow player, at the game's eye height (capsule center + 60 cm =
    # 1.6 m above the feet) with its 100 degree horizontal field of view, looking level.
    pov = os.path.join(os.path.dirname(OUT), "pov")
    os.makedirs(pov, exist_ok=True)
    cam.data.sensor_fit = "HORIZONTAL"
    cam.data.angle = math.radians(100.0)
    cam.data.clip_start = 0.05
    scene.render.resolution_x, scene.render.resolution_y = 1600, 900
    for n, ((x, y, z), views) in players.items():
        pobs[n].hide_render = True
        for view, tx, ty in views:
            cam.location = (x, y, z + EYE_HEIGHT)
            target.location = (tx, ty, z + EYE_HEIGHT)
            scene.render.filepath = os.path.join(pov, "POV_%s_%s.png" % (n, view))
            bpy.ops.render.render(write_still=True)
        pobs[n].hide_render = False
    nverts = sum(len(o.data.vertices) for o in bpy.data.objects if o.type == "MESH")
    print("Cascade blockout: %d objects, %d vertices -> %s" % (sum(1 for o in bpy.data.objects if o.type == "MESH"), nverts, OUT))


main()
