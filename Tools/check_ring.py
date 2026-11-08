"""Checks the ring map's layout (Tools/ring_layout.py) without Unreal: the floor-2 slab has no holes
outside the stair openings, every wall arc closes, and the map mirrors east/west and north/south.
Run: python3 Tools/check_ring.py"""
import math
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import ring_layout as L


def covers(b, x, y):
    t = math.radians(b.yaw)
    dx, dy = x - b.x, y - b.y
    u = dx * math.cos(t) + dy * math.sin(t)
    v = -dx * math.sin(t) + dy * math.cos(t)
    if b.shape == "cylinder":
        return math.hypot(dx, dy) <= b.length * 0.5
    return abs(u) <= b.length * 0.5 and abs(v) <= b.thickness * 0.5


out = L.build()
boxes, data = out["boxes"], out["data"]
R = data["radii"]
slab = [b for b in boxes if b.kind == "slab"]
holes = data["floor2"]["stairs"]
gaps = []
step = 0.25
for (r0, r1) in ((6.05, R["core"] - 0.35), (R["ring_outer"] + 0.35, R["map"] - 0.4)):
    n = int((r1 - r0) / step)
    for i in range(n + 1):
        r = r0 + (r1 - r0) * i / n
        m = int(2 * math.pi * r / step)
        for j in range(m):
            a = 2 * math.pi * j / m
            x, y = r * math.cos(a), r * math.sin(a)
            if any(L.in_rect(x, y, h, 0.6) for h in holes):
                continue
            if not any(covers(b, x, y) for b in slab if abs(b.x - x) < 4 and abs(b.y - y) < 4):
                gaps.append((round(x, 2), round(y, 2)))
print("slab pieces %d, sample gaps %d" % (len(slab), len(gaps)), gaps[:10])

# Holes over the stairs: how much bigger than the stair footprint (worst, meters).
worst = 0.0
for h in holes:
    cx, cy = h["center"]
    for d in [0.1 * k for k in range(1, 40)]:
        for a in range(0, 360, 15):
            x, y = cx + d * math.cos(math.radians(a)), cy + d * math.sin(math.radians(a))
            r = math.hypot(x, y)
            if not (6.3 < r < R["core"] - 0.4 or R["ring_outer"] + 0.4 < r < R["map"] - 0.5):
                continue
            if L.in_rect(x, y, h, 0.0):
                continue
            if not any(covers(b, x, y) for b in slab if abs(b.x - x) < 4 and abs(b.y - y) < 4):
                # distance from the stair rect
                t = math.radians(h["yaw_deg_ccw_from_east"])
                u = (x - cx) * math.cos(t) + (y - cy) * math.sin(t)
                v = -(x - cx) * math.sin(t) + (y - cy) * math.cos(t)
                worst = max(worst, max(abs(u) - h["length"] / 2, abs(v) - h["thickness"] / 2, 0.0))
print("stair openings: at most %.2f m bigger than the stair" % worst)

# Mirror symmetry (x -> -x and y -> -y), matched within 2 cm (a box at yaw t matches one at the
# mirrored yaw, either way round).
def same_yaw(a, b):
    d = (a - b) % 180.0
    return min(d, 180.0 - d) < 0.5
mirror_fail = []
index = {}
for b in boxes:
    index.setdefault((round(b.x), round(b.y)), []).append(b)
for sx, sy in ((-1, 1), (1, -1)):
    for b in boxes:
        mx, my = sx * b.x, sy * b.y
        myaw = math.degrees(math.atan2(sy * math.sin(math.radians(b.yaw)), sx * math.cos(math.radians(b.yaw))))
        found = False
        for gx in (round(mx) - 1, round(mx), round(mx) + 1):
            for gy in (round(my) - 1, round(my), round(my) + 1):
                for c in index.get((gx, gy), []):
                    if (c.kind == b.kind and abs(c.x - mx) < 0.02 and abs(c.y - my) < 0.02 and abs(c.z - b.z) < 0.02
                            and abs(c.length - b.length) < 0.02 and abs(c.thickness - b.thickness) < 0.02 and abs(c.height - b.height) < 0.02
                            and (c.shape == "cylinder" or same_yaw(c.yaw, myaw))):
                        found = True
        if not found:
            mirror_fail.append((b.kind, round(b.x, 2), round(b.y, 2)))
print("boxes %d, unmirrored %d" % (len(boxes), len(mirror_fail)), mirror_fail[:8])
kinds = {}
for b in boxes:
    kinds[b.kind] = kinds.get(b.kind, 0) + 1
print(kinds)
