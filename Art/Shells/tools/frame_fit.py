import os
# Review: the base robot's inner frame (its armour and trim removed), in red, under a shell -- what pokes through?
import bpy, bmesh, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
tag = sys.argv[-1]
o = bpy.data.objects["SK_RobotFrame"]; o.hide_render = False; o.hide_set(False)
ARMOUR = ("M_Robot_06", "M_Robot_13", "M_Robot_08", "M_Robot_09", "M_Robot_12")
# not skeleton either (Will, 2026-11-19): the rings round the shoulders, everything on the back, the head pieces,
# the calf pistons
EXTRA = {"clavicle": ("01", "03", "05", "07"), "spine_02": ("02", "05", "10", "11"), "head": ("00", "01", "10")}
names = {g.index: g.name for g in o.vertex_groups}
bm = bmesh.new(); bm.from_mesh(o.data)
dl = bm.verts.layers.deform.verify()
def bone(v):
    d = v[dl]; n = names[max(d.items(), key=lambda kv: kv[1])[0]] if d else "-"
    return n[:-2] if n.endswith(("_l", "_r")) else n
kill, seen = [], set()
for f0 in bm.faces:
    if f0.index in seen: continue
    st = [f0]; seen.add(f0.index); fs = []
    while st:
        f = st.pop(); fs.append(f)
        for e in f.edges:
            for g in e.link_faces:
                if g.index not in seen: seen.add(g.index); st.append(g)
    m = o.data.materials[fs[0].material_index].name; b = bone(fs[0].verts[0])
    ys = [v.co.y for f in fs for v in f.verts]; zs = [v.co.z for f in fs for v in f.verts]
    if m.startswith(ARMOUR) or m[8:10] in EXTRA.get(b, ()) or (b == "calf" and min(ys) > 6.4 and max(zs) < 40):
        kill += fs
bmesh.ops.delete(bm, geom=kill, context="FACES")
bm.to_mesh(o.data); bm.free()
red = bpy.data.materials.new("FrameRed"); red.use_nodes = True
red.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.8, 0.03, 0.02, 1)
for i in range(len(o.data.materials)): o.data.materials[i] = red
cam, lights = K.studio()
R = (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/%s_framefit2_" % tag)
views = {"three_q": (-0.62, -1, 0.12), "front": (0, -1, 0), "back": (0, 1, 0), "side": (1, 0, 0)}
for n, d in views.items(): K.shoot(R + n + ".png", cam, lights, d, target=(0, 0, 100), span=215)
for s in bpy.data.objects:
    if s.name.startswith("SK_Shell"): s.hide_render = True
K.shoot(R + "alone.png", cam, lights, (-0.62, -1, 0.12), target=(0, 0, 100), span=215)
K.sheet([R + n + ".png" for n in list(views) + ["alone"]], 5, R + "sheet.png")
