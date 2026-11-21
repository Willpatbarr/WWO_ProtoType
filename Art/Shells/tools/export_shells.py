# Exports the shell parts for Unreal: one FBX per part, each skinned to the robot's skeleton.
#   Blender -b robot_frame_v2_rigged.blend -P export_shells.py -- base     the in-game robot, split into
#       SK_RobotSkeleton (joints, struts, hands: always shown) and SK_Shell0_<Head|Core|Arms|Legs> (its armour)
#   Blender -b shell1_claude.blend -P export_shells.py -- 1                SK_Shell1_<Head|Core|Arms|Legs>
#   Blender -b shell2_claude.blend -P export_shells.py -- 2
# Out: export/*.fbx. Import with the game's Tools/import_shell_parts.py.
import bpy, bmesh, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

WHICH = sys.argv[-1]
OUT = (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/export")
os.makedirs(OUT, exist_ok=True)
rig = bpy.data.objects["Armature"]
from skeleton_split import HAND, classify, face_bones
import skeleton_edits


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


if WHICH == "base":
    body = bpy.data.objects["SK_RobotFrame"]
    part = classify(body)
    skeleton_edits.apply(body)          # in memory only: the .blend is never saved
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
