# Shape edits to the skeleton, applied to SK_RobotFrame in memory. base/robot_frame_v2_rigged.blend is
# never saved, so this file is the record of every way the skeleton differs from that mesh.
# export_shells.py (base) and skeleton_views.py both call apply(), so renders show what the game gets.
import bmesh, os
from skeleton_split import face_bones, islands

# The solid thigh blocks (Will, 2026-11-19: "just a tiny bit thinner" on the front and the side;
# he picked 1.5 from the options sheet, tools/thigh_opts.py).
# Centimetres taken off the front face and off the outer side face. The back and the inner side stay
# where they are and the block is squeezed toward them evenly, so every facet stays flat.
THIGH_FRONT = 1.5
THIGH_SIDE = 1.5


def thin_thighs(body, front, side):
    fb = face_bones(body)
    bm = bmesh.new(); bm.from_mesh(body.data); bm.faces.ensure_lookup_table()
    n = 0
    for fs in islands(bm):
        if not fb[fs[0].index].startswith("thigh") or body.data.materials[fs[0].material_index].name[8:10] != "04": continue
        vs = {v for f in fs for v in f.verts}
        xs, ys, zs = [v.co.x for v in vs], [v.co.y for v in vs], [v.co.z for v in vs]
        if max(xs) - min(xs) < 10 or max(zs) - min(zs) < 25: continue          # only the solid block
        back = max(ys); ky = 1 - front / (back - min(ys))                      # the robot faces -Y
        inner = min(xs, key=abs); kx = 1 - side / (max(xs) - min(xs))
        for v in vs:
            v.co.y = back + (v.co.y - back) * ky
            v.co.x = inner + (v.co.x - inner) * kx
        n += 1
    assert n == 2, n
    bm.to_mesh(body.data); bm.free(); body.data.update()


# The chest block's front corners (2026-11-20): shell 3's chest plates sweep back from a centre ridge to the
# arm openings (reference/chest_sketch_1.jpg and Will's marked-up sheet), and the block's square front
# corners stood up to 3 cm proud of them. Handoff rule: trim the old geometry, not the plate.
# The spine's top segments, whose front corners came through the lowest plates, get the same treatment.
# (make_shell3.py logs these planes as "chest plane"; copy them here when the plates move.)
# Each entry is one plate's plane on the robot's left (outward normal, a point on it, and the height above
# which it applies); the right side mirrors it. Any corner of the block nearer than CHEST_CLEAR cm to a
# plane is pushed straight back along that plane's normal. No faces are added or removed.
CHEST_CLEAR = 1.0
CHEST_TRIM = [((0.618, -0.777, 0.122), (15.62, -0.50, 161.34), 150),
              ((0.611, -0.691, 0.387), (15.59, -0.96, 158.60), 150),
              ((0.673, -0.739, -0.025), (15.50, -2.10, 156.71), 129.5),
              ((0.674, -0.739, -0.026), (15.51, -1.94, 152.46), 129.5),
              ((0.715, -0.626, -0.312), (15.52, -1.82, 149.30), 129.5),
              ((0.698, -0.660, -0.277), (15.20, -0.68, 146.27), 129.5),
              # the back half
              ((0.173, -0.013, 0.985), (8.94, -2.59, 163.97), 150),
              ((0.365, 0.353, 0.862), (15.93, 3.50, 162.82), 150),
              ((0.303, 0.723, 0.620), (4.00, 10.50, 165.00), 150),
              ((0.525, 0.832, 0.179), (5.00, 16.94, 157.00), 150),
              ((0.522, 0.853, 0.010), (5.00, 16.94, 157.00), 129.5),
              ((0.583, 0.778, -0.235), (7.50, 15.52, 147.00), 129.5),
              ((0.770, 0.393, -0.502), (4.50, 13.54, 133.00), 129.5),
              ((0.942, 0.001, -0.336), (15.20, -0.68, 146.27), 129.5),
              ((0.0, 0.990, -0.140), (0.0, 16.80, 156.00), 129.5),       # the back's flat panel
              ((0.0, 0.779, 0.627), (4.00, 10.50, 165.00), 150)]         # the facet above it


# Shell 3's chest depth (Will, 2026-11-20: "bring the depth of the chest back"). The chest's plates, and the
# trim planes above with them, are squeezed toward the plane y = CHEST_PIVOT (the shoulder joints) so that the
# front-most point of the top layer, CHEST_FRONT, moves back by CHEST_DEPTH cm. Everything behind the pivot
# stays where it is. make_shell3.py uses chest_y() for its plates; WWO_CHEST_DEPTH=<cm> tries another value.
CHEST_DEPTH = float(os.environ.get("WWO_CHEST_DEPTH", 1.0))      # Will picked 1 from renders/options/chest_depth_sheet.png
CHEST_PIVOT, CHEST_FRONT = 3.5, -18.8


def chest_k(): return 1 - CHEST_DEPTH / (CHEST_PIVOT - CHEST_FRONT)
def chest_y(y): return CHEST_PIVOT + (y - CHEST_PIVOT) * chest_k() if y < CHEST_PIVOT else y


def chest_planes():
    out = []
    for (nx, ny, nz), (px, py, pz), zmin in CHEST_TRIM:
        if py < CHEST_PIVOT:
            ny = ny / chest_k(); L = (nx * nx + ny * ny + nz * nz) ** 0.5
            nx, ny, nz, py = nx / L, ny / L, nz / L, chest_y(py)
        out.append(((nx, ny, nz), (px, py, pz), zmin))
    return out


def trim_chest(body):
    fb = face_bones(body)
    bm = bmesh.new(); bm.from_mesh(body.data); bm.faces.ensure_lookup_table()
    n = 0; planes = chest_planes()
    for fs in islands(bm):
        m = body.data.materials[fs[0].material_index].name[8:10]
        if fb[fs[0].index] != "spine_02" or m not in ("09", "03"): continue
        vs = {v for f in fs for v in f.verts}
        xs = [v.co.x for v in vs]
        if max(xs) - min(xs) < 15: continue                # the chest block (09) and the spine's top segments (03)
        for _ in range(6):
            for v in vs:
                for side in (1, -1):
                    for (nx, ny, nz), (px, py, pz), zmin in planes:
                        if v.co.z < zmin: continue
                        d = side * nx * (v.co.x - side * px) + ny * (v.co.y - py) + nz * (v.co.z - pz) + CHEST_CLEAR
                        if d > 0: v.co.x -= side * nx * d; v.co.y -= ny * d; v.co.z -= nz * d
        n += m == "09"
    assert n == 1, n
    bm.to_mesh(body.data); bm.free(); body.data.update()


def apply(body, thigh_front=None, thigh_side=None, part=None, detail=True):
    """part: skeleton_split.classify(body), made before this call. The detail pass adds faces, so it needs
    the split to keep it in step (it rewrites the dict in place); without part there is no detail pass."""
    f = THIGH_FRONT if thigh_front is None else thigh_front
    s = THIGH_SIDE if thigh_side is None else thigh_side
    if f or s: thin_thighs(body, f, s)
    trim_chest(body)
    if detail and part is not None and os.environ.get("WWO_SKELETON_DETAIL", "1") != "0":
        import skeleton_detail
        skeleton_detail.build(body, part)
