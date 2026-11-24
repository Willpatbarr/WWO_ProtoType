# Close-ups of the skeleton's kitbashed parts (legs, feet, chest), plus the whole skeleton.
#   Blender -b ../base/robot_frame_v2_rigged.blend -P skeleton_detail_views.py [-- thigh calf foot chest full]
# Out: renders/detail_<name>.png (four views each). WWO_SKELETON_DETAIL=0 renders the plain blocks, for a before/after.
import bpy, bmesh, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K, skeleton_edits
from skeleton_split import classify

body = bpy.data.objects["SK_RobotFrame"]
part = classify(body); skeleton_edits.apply(body, part=part)
bm = bmesh.new(); bm.from_mesh(body.data)
bmesh.ops.delete(bm, geom=[f for f in bm.faces if part[f.index] != "Skeleton"], context="FACES")
skel = bpy.data.objects.new("SkeletonOnly", bpy.data.meshes.new("SkeletonOnly"))
for m in body.data.materials: skel.data.materials.append(m)
bm.to_mesh(skel.data); bm.free()
bpy.context.scene.collection.objects.link(skel); skel.matrix_world = body.matrix_world; body.hide_render = True
print("WWO skeleton tris", sum(len(p.vertices) - 2 for p in skel.data.polygons))
TAG = os.environ.get("WWO_VIEW_TAG", "")
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/detail_" + TAG
SHOTS = {"thigh": ((12, 0, 82), 48, [(0.25, -1, 0.1), (1, -0.15, 0.1), (0.8, -1, 0.3), (0.3, 1, 0.15)]),
         "calf": ((14, 3, 38), 55, [(0.25, -1, 0.1), (1, -0.15, 0.1), (0.8, -1, 0.3), (0.3, 1, 0.15)]),
         "foot": ((15, -3, 8), 36, [(0.2, -1, 0.35), (1, 0, 0.15), (0.8, -1, 0.6), (0.5, 1, 0.5)]),
         "chest": ((0, 0, 147), 52, [(0, -1, 0.05), (1, -0.1, 0.05), (0.7, -1, 0.4), (0.5, 1, 0.3)]),
         "full": ((0, 0, 100), 215, [(0, -1, 0), (1, 0, 0), (-0.62, -1, 0.12), (0.62, 1, 0.12)])}
want = [a for a in sys.argv[sys.argv.index("--") + 1:]] if "--" in sys.argv else list(SHOTS)
for n in want:
    t, s, ds = SHOTS[n]
    cam, lights = K.studio((900, 1400) if n == "full" else (1000, 1000))
    ps = []
    for i, dr in enumerate(ds):
        p = R + "%s_%d.png" % (n, i); ps.append(p); K.shoot(p, cam, lights, dr, target=t, span=s)
    K.sheet(ps, 4 if n == "full" else 2, R + n + ".png")
    for p in ps: os.remove(p)
