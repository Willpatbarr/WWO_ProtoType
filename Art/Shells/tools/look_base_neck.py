import bpy, sys, os, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
body = bpy.data.objects["SK_RobotFrame"]
me = body.data; names = {g.index: g.name for g in body.vertex_groups}
co = np.array([v.co[:] for v in me.vertices]); vb = np.array([names[max(v.groups, key=lambda g: g.weight).group] for v in me.vertices])
for n in ("neck_01", "head", "spine_02"):
    c = co[vb == n]; print("WWO", n, len(c), np.round(c.min(0), 1), np.round(c.max(0), 1))
c = co[(vb == "head") & (co[:, 2] < 168)]; print("WWO head below 168:", len(c), np.round(c.min(0), 1) if len(c) else None, np.round(c.max(0), 1) if len(c) else None)
c = co[(vb == "spine_02") & (co[:, 2] > 158)]; print("WWO chest above 158:", len(c), np.round(c.min(0), 1) if len(c) else None, np.round(c.max(0), 1) if len(c) else None)
cam, lights = K.studio((800, 800))
R = (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/base_neck_")
for n, d in {"side": (1, 0, 0), "three_q": (-0.7, -1, 0.1), "back_q": (0.6, 1, 0.1)}.items():
    K.shoot(R + n + ".png", cam, lights, d, target=(0, 2, 166), span=40)
K.sheet([R + n + ".png" for n in ("side", "three_q", "back_q")], 3, R + "sheet.png")
