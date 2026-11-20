import os
# Exports the shell parts for Unreal: one FBX per part, each skinned to the robot's skeleton.
#   Blender -b robot_frame_v2_rigged.blend -P export_shells.py -- base     the in-game robot, split into
#       SK_RobotSkeleton (joints, struts, hands: always shown) and SK_Shell0_<Head|Core|Arms|Legs> (its armour)
#   Blender -b shell1_claude.blend -P export_shells.py -- 1                SK_Shell1_<Head|Core|Arms|Legs>
#   Blender -b shell2_claude.blend -P export_shells.py -- 2
# Out: export/*.fbx. Import with the game's Tools/import_shell_parts.py.
import bpy, bmesh, sys, os

WHICH = sys.argv[-1]
OUT = (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/export")
os.makedirs(OUT, exist_ok=True)
rig = bpy.data.objects["Armature"]
HAND = ("hand_", "thumb_", "index_", "middle_", "ring_", "pinky_")
SLOT = {"head": "Head", "neck_01": "Head", "spine_01": "Core", "spine_02": "Core", "pelvis": "Core", "clavicle": "Core",
        "upperarm": "Arms", "lowerarm": "Arms", "thigh": "Legs", "calf": "Legs", "foot": "Legs"}
# The base robot, by material (the game's Tools/create_robot_materials.py names their roles) and bone.
ARMOUR = ("06", "13", "08", "09", "12")
# Not skeleton either (Will, 2026-11-19): the rings round the shoulders, everything on the back, the head pieces
NOT_SKELETON = {"clavicle": ("01", "03", "05", "07"), "spine_02": ("02", "05", "10", "11"), "head": ("00", "01", "10")}


def log(*a): print("WWO", *a, flush=True)


def export(o):
    for x in bpy.context.scene.objects: x.select_set(False)
    o.hide_set(False); o.hide_render = False
    rig.select_set(True); o.select_set(True); bpy.context.view_layer.objects.active = rig
    path = os.path.join(OUT, o.name + ".fbx")
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"ARMATURE", "MESH"},
                             add_leaf_bones=False, mesh_smooth_type="FACE", bake_anim=False,
                             primary_bone_axis="Y", secondary_bone_axis="X", axis_forward="-Z", axis_up="Y")
    used = sorted({p.material_index for p in o.data.polygons})
    assert len(used) == len(o.data.materials), (o.name, used)
    tris = sum(len(p.vertices) - 2 for p in o.data.polygons)
    log("wrote", path, "tris", tris, "materials", [m.name for m in o.data.materials])


def piece(src, name, keep):
    """A copy of src holding only the faces keep(face) accepts, with unused materials dropped."""
    o = src.copy(); o.data = src.data.copy(); o.name = o.data.name = name
    bpy.context.scene.collection.objects.link(o)
    bm = bmesh.new(); bm.from_mesh(o.data)
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if not keep(f)], context="FACES")
    loose = [v for v in bm.verts if not v.link_faces]
    if loose: bmesh.ops.delete(bm, geom=loose, context="VERTS")
    bm.to_mesh(o.data); bm.free()
    used = sorted({p.material_index for p in o.data.polygons})
    mats = [o.data.materials[i] for i in used]; remap = {i: k for k, i in enumerate(used)}
    idx = [remap[p.material_index] for p in o.data.polygons]
    o.data.materials.clear()                      # (this resets every face to slot 0)
    for m in mats: o.data.materials.append(m)
    o.data.polygons.foreach_set("material_index", idx); o.data.update()
    return o


def face_bones(o):
    """Each face's bone (of its first vertex), with the side taken off."""
    names = {g.index: g.name for g in o.vertex_groups}
    vb = []
    for v in o.data.vertices:
        n = names[max(v.groups, key=lambda g: g.weight).group] if v.groups else "-"
        vb.append(n)
    return {p.index: vb[p.vertices[0]] for p in o.data.polygons}


def side_off(n): return n[:-2] if n.endswith(("_l", "_r")) else n


if WHICH == "base":
    body = bpy.data.objects["SK_RobotFrame"]
    fb = face_bones(body)
    # the robot is made of separate pieces: each whole piece is skeleton or armour
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
    export(piece(body, "SK_RobotSkeleton", lambda f: part[f.index] == "Skeleton"))
    for s in ("Head", "Core", "Arms", "Legs"):
        export(piece(body, "SK_Shell0_" + s, lambda f, s=s: part[f.index] == s))
else:
    for s in ("Head", "Core", "Arms", "Legs"):
        src = bpy.data.objects["SK_Shell%s_%s" % (WHICH, s)]
        fb = face_bones(src)
        mats = [m.name for m in src.data.materials]
        # the hands and the neck pistons are the skeleton's now
        o = piece(src, src.name + "_x", lambda f: not fb[f.index].startswith(HAND) and not mats[f.material_index].startswith("M_Robot"))
        src.name = src.data.name = src.name + "_src"; o.name = o.data.name = "SK_Shell%s_%s" % (WHICH, s)
        for i, m in enumerate(o.data.materials):            # each shell has its own materials (their glow differs)
            if m.name.startswith("M_Shell_"):
                n = m.name.replace("M_Shell_", "M_Shell%s_" % WHICH)
                c = bpy.data.materials.get(n)
                if not c: c = m.copy(); c.name = n
                o.data.materials[i] = c
        export(o)
