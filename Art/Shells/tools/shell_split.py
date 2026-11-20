# How a robot is split into the parts the game shows: the always-on SK_RobotSkeleton and the four
# shell parts SK_Shell<N>_<Head|Core|Arms|Legs>. This is the same logic as export_shells.py (kept
# there too so the exporter stays a single self-contained script); keep the two in step.
import bpy, bmesh

HAND = ("hand_", "thumb_", "index_", "middle_", "ring_", "pinky_")
SLOT = {"head": "Head", "neck_01": "Head", "spine_01": "Core", "spine_02": "Core", "pelvis": "Core", "clavicle": "Core",
        "upperarm": "Arms", "lowerarm": "Arms", "thigh": "Legs", "calf": "Legs", "foot": "Legs"}
SLOTS = ("Head", "Core", "Arms", "Legs")
# The base robot, by material (the game's Tools/create_robot_materials.py names their roles) and bone.
ARMOUR = ("06", "13", "08", "09", "12")
# Not skeleton either (Will, 2026-11-19): the rings round the shoulders, everything on the back, the head pieces
NOT_SKELETON = {"clavicle": ("01", "03", "05", "07"), "spine_02": ("02", "05", "10", "11"), "head": ("00", "01", "10")}


def side_off(n): return n[:-2] if n.endswith(("_l", "_r")) else n


def slot_bones(slot):
    """The rig's bones that belong to a slot (with their sides)."""
    rig = bpy.data.objects["Armature"]
    return sorted(b.name for b in rig.data.bones if SLOT.get(side_off(b.name)) == slot)


def vert_bones(o):
    """Each vertex's dominant vertex group name ('-' if none)."""
    names = {g.index: g.name for g in o.vertex_groups}
    return [names[max(v.groups, key=lambda g: g.weight).group] if v.groups else "-" for v in o.data.vertices]


def face_bones(o):
    """Each face's bone (of its first vertex), with its side."""
    vb = vert_bones(o)
    return {p.index: vb[p.vertices[0]] for p in o.data.polygons}


def classify_base(body):
    """{face index: 'Skeleton' | slot} for the base robot SK_RobotFrame (as export_shells.py splits it)."""
    fb = face_bones(body)
    bm = bmesh.new(); bm.from_mesh(body.data); bm.faces.ensure_lookup_table()
    part, seen = {}, set()
    for f0 in bm.faces:
        if f0.index in seen: continue
        st = [f0]; seen.add(f0.index); fs = []
        while st:
            f = st.pop(); fs.append(f)
            for e in f.edges:
                for g in e.link_faces:
                    if g.index not in seen: seen.add(g.index); st.append(g)
        raw = fb[fs[0].index]; b = side_off(raw)
        m = body.data.materials[fs[0].material_index].name[8:10]
        co = [v.co for f in fs for v in f.verts]
        xs, ys, zs = [c.x for c in co], [c.y for c in co], [c.z for c in co]
        if raw.startswith(HAND): where = "Skeleton"
        elif m in ARMOUR or m in NOT_SKELETON.get(b, ()): where = SLOT[b]
        elif b == "calf" and min(ys) > 6.4 and max(zs) < 40: where = "Legs"                 # the calf pistons
        elif b == "thigh" and max(xs) - min(xs) > 10 and max(zs) - min(zs) > 25: where = "Legs"   # the solid thigh block
        elif b == "calf" and max(zs) - min(zs) > 35: where = "Legs"                          # the solid calf block
        elif b == "foot" and max(zs) < 2.0: where = "Legs"                                   # the sole plates
        else: where = "Skeleton"
        for f in fs: part[f.index] = where
    bm.free()
    return part


def shell_keep(src):
    """{face index: kept} for a shell part as exported: the hands and the neck pistons are the skeleton's."""
    fb = face_bones(src)
    mats = [m.name for m in src.data.materials]
    return {p.index: not fb[p.index].startswith(HAND) and not mats[p.material_index].startswith("M_Robot")
            for p in src.data.polygons}


def piece(src, name, keep):
    """A copy of src holding only the faces keep(face index) accepts, with unused materials dropped."""
    o = src.copy(); o.data = src.data.copy(); o.name = o.data.name = name
    bpy.context.scene.collection.objects.link(o)
    bm = bmesh.new(); bm.from_mesh(o.data)
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if not keep(f.index)], context="FACES")
    loose = [v for v in bm.verts if not v.link_faces]
    if loose: bmesh.ops.delete(bm, geom=loose, context="VERTS")
    bm.to_mesh(o.data); bm.free()
    used = sorted({p.material_index for p in o.data.polygons})
    mats = [o.data.materials[i] for i in used]; remap = {i: k for k, i in enumerate(used)}
    idx = [remap[p.material_index] for p in o.data.polygons]
    o.data.materials.clear()
    for m in mats: o.data.materials.append(m)
    o.data.polygons.foreach_set("material_index", idx); o.data.update()
    return o


def game_parts(which):
    """The objects the game shows for this file: {'Skeleton': obj, 'Head': obj, ...} (new copies;
    the source objects are hidden). which = 'base' or a shell number as a string."""
    body = bpy.data.objects["SK_RobotFrame"]
    split = classify_base(body)
    out = {"Skeleton": piece(body, "SK_RobotSkeleton", lambda i: split[i] == "Skeleton")}
    if which == "base":
        for s in SLOTS:
            out[s] = piece(body, "SK_Shell0_" + s, lambda i, s=s: split[i] == s)
    else:
        for s in SLOTS:
            src = bpy.data.objects["SK_Shell%s_%s" % (which, s)]
            keep = shell_keep(src)
            out[s] = piece(src, "SK_Shell%s_%s_game" % (which, s), lambda i, k=keep: k[i])
            src.hide_render = True; src.hide_set(True)
    body.hide_render = True; body.hide_set(True)
    for o in out.values():
        o.hide_render = False; o.hide_set(False)
    return out
