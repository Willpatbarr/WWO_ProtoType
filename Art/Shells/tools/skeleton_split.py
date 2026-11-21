# Which pieces of the in-game robot (SK_RobotFrame in base/robot_frame_v2_rigged.blend) are skeleton
# and which are its armour. The skeleton (joints, struts, hands) is always shown; the armour is
# shell 0, swapped per slot like any other shell. Used by export_shells.py and skeleton_views.py,
# so the review renders always show exactly what the export will cut.
import bmesh

HAND = ("hand_", "thumb_", "index_", "middle_", "ring_", "pinky_")
SLOT = {"head": "Head", "neck_01": "Head", "spine_01": "Core", "spine_02": "Core", "pelvis": "Core", "clavicle": "Core",
        "upperarm": "Arms", "lowerarm": "Arms", "thigh": "Legs", "calf": "Legs", "foot": "Legs"}
# The base robot, by material (the game's Tools/create_robot_materials.py names their roles) and bone.
# Armour is the outer plating: these materials, wherever they are.
ARMOUR = ("06", "13", "08", "09", "12")
# Not skeleton either (Will, 2026-11-19): everything on the back, and the hexagonal rings round the shoulders
NOT_SKELETON = {"spine_02": ("02", "05", "10", "11")}
# Skeleton whatever the material (Will, 2026-11-19): the head under the mask (skull, ear discs, their bolts)
# and the large shoulder joints (the white capsules, their housings and the bar between them)
UNDER_ARMOUR = {"head": ("00", "01", "09", "10"), "clavicle": ("01", "03", "05", "07", "08")}
# The legs under the plating (the solid thigh and calf blocks, the calf pistons, the soles) are skeleton
# by the default rule: their material is "04", which is in neither list.

def face_bones(o):
    """Each face's bone (of its first vertex)."""
    names = {g.index: g.name for g in o.vertex_groups}
    vb = []
    for v in o.data.vertices:
        n = names[max(v.groups, key=lambda g: g.weight).group] if v.groups else "-"
        vb.append(n)
    return {p.index: vb[p.vertices[0]] for p in o.data.polygons}


def side_off(n): return n[:-2] if n.endswith(("_l", "_r")) else n


def islands(bm):
    """The separate pieces of a mesh: lists of faces joined edge to edge."""
    seen = set()
    for f0 in bm.faces:
        if f0.index in seen: continue
        st = [f0]; seen.add(f0.index); fs = []
        while st:
            f = st.pop(); fs.append(f)
            for e in f.edges:
                for g in e.link_faces:
                    if g.index not in seen: seen.add(g.index); st.append(g)
        yield fs


def classify(body):
    """{face index: "Skeleton" | "Head" | "Core" | "Arms" | "Legs"} for the base robot.
    The robot is made of separate pieces: each whole piece is skeleton or armour."""
    fb = face_bones(body)
    bm = bmesh.new(); bm.from_mesh(body.data); bm.faces.ensure_lookup_table()
    part = {}
    for fs in islands(bm):
        raw = fb[fs[0].index]; b = side_off(raw)
        m = body.data.materials[fs[0].material_index].name[8:10]
        co = [v.co for f in fs for v in f.verts]
        xs, ys, zs = [c.x for c in co], [c.y for c in co], [c.z for c in co]
        if raw.startswith(HAND): where = "Skeleton"
        elif m in UNDER_ARMOUR.get(b, ()): where = "Skeleton"
        elif b == "spine_02" and m == "09" and max(xs) - min(xs) > 15: where = "Skeleton"   # the chest block under the torso armour
        # the upper arm's piston (rod, body, pin) and the small rods at the shoulder share the plates' material;
        # only the two long plates are armour (Will, 2026-11-20: parts of the piston were missing)
        elif b == "upperarm" and m == "06" and max(zs) - min(zs) < 15: where = "Skeleton"
        elif m in ARMOUR or m in NOT_SKELETON.get(b, ()): where = SLOT[b]
        else: where = "Skeleton"
        for f in fs: part[f.index] = where
    bm.free()
    return part
