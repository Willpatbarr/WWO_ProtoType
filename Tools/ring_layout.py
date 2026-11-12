"""The ring PvP map (L_ArenaRing) as a list of boxes, worked out from the mockup's JSON
(~/Documents/WWO_PvP_Mockups/ring_map_geometry.json, made by ring_map.py there).

Plain Python (no Unreal), so the layout can be checked outside the editor (Tools/check_ring.py);
Tools/build_maps.py turns each box into an actor. All in mockup meters: x east, y north, origin at
the map center, z up. A box: center (x, y, z), size (length along its yaw, thickness, height), yaw
in degrees counter-clockwise from east, pitch in degrees (up toward its length), and what it is.
- Floor 1: 0-4 m. Floor 2: slab top at 4 m, its walls to WALL_TOP (Will: about three times as tall
  as the first floor's). A roof over the whole map at WALL_TOP.
- Curved walls and the floor-2 slab are small straight pieces along each arc (one per cell, so
  neighbors never open gaps).
"""
import json
import math
import os


def _geometry(name):
    # The mockups folder is where the layout is made (the source of truth); Tools/geometry holds a
    # copy for machines without it. Copy the file there again after changing a layout.
    made = os.path.expanduser("~/Documents/WWO_PvP_Mockups/" + name)
    return made if os.path.exists(made) else os.path.join(os.path.dirname(os.path.abspath(__file__)), "geometry", name)


RING_JSON = os.environ.get("WWO_RING_JSON", _geometry("ring_map_geometry.json"))
FLOOR2 = 4.0
WALL_TOP = 16.0     # floor-2 walls are 12 m (three times floor 1's 4 m)
SLAB = 0.3
ROOF = 0.5
LOW_COVER = 1.2     # Will: you can shoot over it
RAIL = 1.1
SILL, LINTEL = 5.0, 7.0  # floor-2 windows in the inner ring wall: open between these


class Box:
    __slots__ = ("x", "y", "z", "length", "thickness", "height", "yaw", "pitch", "kind", "shape")

    def __init__(self, x, y, z, length, thickness, height, yaw, kind, pitch=0.0, shape="cube"):
        self.x, self.y, self.z = x, y, z
        self.length, self.thickness, self.height = length, thickness, height
        self.yaw, self.pitch, self.kind, self.shape = yaw, pitch, kind, shape


def standing(x, y, z0, z1, length, thickness, yaw, kind, shape="cube"):
    return Box(x, y, (z0 + z1) * 0.5, length, thickness, z1 - z0, yaw, kind, shape=shape)


def arc(r, a0, a1, z0, z1, thickness, kind, step=3.0):
    """Straight pieces along an arc of radius r, angles a0..a1 (degrees ccw from east)."""
    out = []
    span = math.radians(a1 - a0)
    n = max(1, int(math.ceil(abs(span) * r / step)))
    if abs(abs(a1 - a0) - 360.0) < 1e-6:
        n = 4 * int(math.ceil(n / 4.0))  # a whole circle: a multiple of 4 pieces, so it mirrors
    for i in range(n):
        am = math.radians(a0) + span * (i + 0.5) / n
        # Long enough to reach the outer face of the band at both ends, so joins close.
        chord = 2.0 * (r + thickness * 0.5) * math.tan(abs(span) / n * 0.5) + 0.02
        out.append(standing(r * math.cos(am), r * math.sin(am), z0, z1, chord, thickness, math.degrees(am) + 90.0, kind))
    return out


def unwrap_span(poly):
    angs = [math.degrees(math.atan2(poly[0][1], poly[0][0]))]
    for x, y in poly[1:]:
        a = math.degrees(math.atan2(y, x))
        while a - angs[-1] > 180.0:
            a -= 360.0
        while a - angs[-1] < -180.0:
            a += 360.0
        angs.append(a)
    a0, a1 = min(angs), max(angs)
    return (0.0, 360.0) if a1 - a0 > 355.0 else (a0, a1)


def wall_from_polygon(poly, z0, z1, kind):
    """A wall footprint: a curved band (many points) or a rectangle (4 points)."""
    if len(poly) <= 5:
        pts = poly[:4]
        e1 = (pts[1][0] - pts[0][0], pts[1][1] - pts[0][1])
        e2 = (pts[2][0] - pts[1][0], pts[2][1] - pts[1][1])
        l1, l2 = math.hypot(*e1), math.hypot(*e2)
        along = e1 if l1 >= l2 else e2
        cx = sum(p[0] for p in pts) / 4.0
        cy = sum(p[1] for p in pts) / 4.0
        return [standing(cx, cy, z0, z1, max(l1, l2), min(l1, l2), math.degrees(math.atan2(along[1], along[0])), kind)]
    rs = [math.hypot(x, y) for x, y in poly]
    a0, a1 = unwrap_span(poly)
    return arc((min(rs) + max(rs)) * 0.5, a0, a1, z0, z1, max(rs) - min(rs), kind)


def along_curve(line, z0, z1, thickness, kind):
    """A window or railing polyline from the JSON (they all lie on circles about the center): pieces
    along that circle, so mirrored spans get mirrored pieces whichever way their points run."""
    rs = [math.hypot(x, y) for x, y in line]
    a0, a1 = unwrap_span(line)
    return arc(sum(rs) / len(rs), a0, a1, z0, z1, thickness, kind)


def along_polyline(line, z0, z1, thickness, kind, step=3.0):
    out = []
    run = [line[0]]
    total = 0.0
    for idx, p in enumerate(line[1:], 1):
        total += math.hypot(p[0] - run[-1][0], p[1] - run[-1][1])
        run.append(p)
        if total >= step or idx == len(line) - 1:
            (x0, y0), (x1, y1) = run[0], run[-1]
            length = math.hypot(x1 - x0, y1 - y0)
            if length > 0.05:
                out.append(standing((x0 + x1) * 0.5, (y0 + y1) * 0.5, z0, z1, length + thickness, thickness, math.degrees(math.atan2(y1 - y0, x1 - x0)), kind))
            run = [p]
            total = 0.0
    return out


def in_rect(px, py, rect, grow=0.0):
    cx, cy = rect["center"]
    t = math.radians(rect["yaw_deg_ccw_from_east"])
    dx, dy = px - cx, py - cy
    u = dx * math.cos(t) + dy * math.sin(t)
    v = -dx * math.sin(t) + dy * math.cos(t)
    return abs(u) <= rect["length"] * 0.5 + grow and abs(v) <= rect["thickness"] * 0.5 + grow


def slab_band(r0, r1, holes, z_top, thick, kind, radial=2.0, tangential=2.0, fine=0.5):
    """A floor over the band r0..r1: one piece per cell (about radial x tangential m), each long
    enough to cover its wedge at the band's outer edge. A cell that touches a stair opening is split
    into fine cells, and only those clear of the opening are kept, so openings stay close to the
    stair's size. Cell counts are multiples of 4, so the slab mirrors exactly."""
    out = []

    def clear(ra, rb, a0, a1):
        rm, am = (ra + rb) * 0.5, (a0 + a1) * 0.5
        probe = [(rm, am)] + [(rr, aa) for rr in (ra + 0.02, rb - 0.02) for aa in (a0 + 0.001, a1 - 0.001)]
        return not any(in_rect(rr * math.cos(aa), rr * math.sin(aa), h, 0.05) for h in holes for rr, aa in probe)

    def piece(ra, rb, a0, a1):
        rm, am = (ra + rb) * 0.5, (a0 + a1) * 0.5
        chord = 2.0 * rb * math.tan((a1 - a0) * 0.5) + 0.04
        out.append(Box(rm * math.cos(am), rm * math.sin(am), z_top - thick * 0.5, chord, rb - ra + 0.04, thick, math.degrees(am) + 90.0, kind))

    rings = max(1, int(math.ceil((r1 - r0) / radial)))
    for k in range(rings):
        ra = r0 + (r1 - r0) * k / rings
        rb = r0 + (r1 - r0) * (k + 1) / rings
        n = 4 * max(2, int(math.ceil(2.0 * math.pi * rb / tangential / 4.0)))
        for i in range(n):
            a0, a1 = 2.0 * math.pi * i / n, 2.0 * math.pi * (i + 1) / n
            near = any(in_rect(rr * math.cos(aa), rr * math.sin(aa), h, 1.5)
                       for h in holes for rr in (ra, (ra + rb) * 0.5, rb) for aa in (a0, (a0 + a1) * 0.5, a1))
            if not near:
                piece(ra, rb, a0, a1)
                continue
            # Next to an opening: fine cells.
            fr = max(1, int(math.ceil((rb - ra) / fine)))
            ft = max(1, int(math.ceil((a1 - a0) * rb / fine)))
            for u in range(fr):
                for v in range(ft):
                    sa, sb = ra + (rb - ra) * u / fr, ra + (rb - ra) * (u + 1) / fr
                    ta, tb = a0 + (a1 - a0) * v / ft, a0 + (a1 - a0) * (v + 1) / ft
                    if clear(sa, sb, ta, tb):
                        piece(sa, sb, ta, tb)
    return out


def ramp(s, z_bottom, z_top):
    (x0, y0), (x1, y1) = s["bottom"], s["top"]
    run = math.hypot(x1 - x0, y1 - y0)
    rise = z_top - z_bottom
    pitch = math.degrees(math.atan2(rise, run))
    slope = math.hypot(run, rise)
    thick = 0.3
    extra = 0.6  # below the floor at the bottom; the top ends flush with the slab
    ux, uy = (x1 - x0) / run, (y1 - y0) / run
    cos_p, sin_p = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    # Top surface runs from the bottom point (z_bottom) to the top point (z_top); the box is that
    # surface stretched `extra` back past the bottom, and dropped half its thickness.
    mid_along = (slope - extra) * 0.5  # from the bottom point, along the slope
    cx = x0 + ux * mid_along * cos_p
    cy = y0 + uy * mid_along * cos_p
    cz = z_bottom + mid_along * sin_p - thick * 0.5 * cos_p
    return Box(cx, cy, cz, slope + extra, s["thickness"], thick, math.degrees(math.atan2(uy, ux)), "stairs", pitch=pitch)


def crate_height(x, y):
    return (1.0, 1.3, 1.6)[int(round(abs(x) * 7.0 + abs(y) * 3.0)) % 3]  # the same on mirrored crates


def build(data=None):
    """All the boxes, plus the spawns: {"boxes": [...], "spawns": {"blue": [(x, y)], "orange": [...]}}."""
    data = data or json.load(open(RING_JSON))
    R = data["radii"]
    f1, f2 = data["floor1"], data["floor2"]
    boxes = []
    # Ground: one slab under everything; the ring lane and the core a shade different on top.
    boxes.append(Box(0, 0, -0.25, R["map"] * 2 + 4, R["map"] * 2 + 4, 0.5, 0.0, "ground"))
    boxes.append(Box(0, 0, -0.24, R["ring_outer"] * 2, R["ring_outer"] * 2, 0.5, 0.0, "lane", shape="cylinder"))
    boxes.append(Box(0, 0, -0.23, R["core"] * 2, R["core"] * 2, 0.5, 0.0, "ground", shape="cylinder"))

    def ringy(poly):
        rs = [math.hypot(x, y) for x, y in poly]
        return len(poly) > 5 and 21.0 < min(rs) < 33.0

    f2_keys = {(len(p), tuple(p[0])) for p in f2["walls_full_height"]}
    f1_keys = set()
    for poly in f1["walls_full_height"]:
        key = (len(poly), tuple(poly[0]))
        f1_keys.add(key)
        boxes += wall_from_polygon(poly, 0.0, WALL_TOP if key in f2_keys else FLOOR2, "ringwall" if ringy(poly) else "wall")
    for poly in f2["walls_full_height"]:
        if (len(poly), tuple(poly[0])) not in f1_keys:
            boxes += wall_from_polygon(poly, FLOOR2, WALL_TOP, "ringwall" if ringy(poly) else "wall")
    for line in f2["windows_polyline"]:
        boxes += along_curve(line, FLOOR2, SILL, R["wall_thickness"], "ringwall")
        boxes += along_curve(line, LINTEL, WALL_TOP, R["wall_thickness"], "ringwall")
    for line in f2["ledges_railing_polyline"]:
        boxes += along_curve(line, FLOOR2, FLOOR2 + RAIL, 0.2, "rail")
    holes = f2["stairs"]
    boxes += slab_band(6.0, R["core"] + 0.3, holes, FLOOR2, SLAB, "slab")
    boxes += slab_band(R["ring_outer"] - 0.3, R["map"] + 0.4, holes, FLOOR2, SLAB, "slab")
    for s in f1["stairs"]:
        boxes.append(ramp(s, 0.0, FLOOR2))
    for z, fl in ((0.0, f1), (FLOOR2, f2)):
        for c in fl["low_cover"]:
            x, y = c["center"]
            boxes.append(standing(x, y, z, z + LOW_COVER, c["length"], c["thickness"], c["yaw_deg_ccw_from_east"], "cover"))
        for c in fl["crates"]:
            x, y = c["center"]
            boxes.append(standing(x, y, z, z + crate_height(x, y), c["length"], c["thickness"], c["yaw_deg_ccw_from_east"], "crate"))
        for p in fl["pillars"]:
            x, y = p["center"]
            r = math.hypot(x, y)
            top = WALL_TOP if (z == 0.0 and R["core"] < r < R["ring_outer"]) else (FLOOR2 if z == 0.0 else WALL_TOP)
            boxes.append(standing(x, y, z, top, p["radius"] * 2, p["radius"] * 2, 0.0, "pillar", shape="cylinder"))
    # The roof: over the whole map, on top of the walls.
    boxes.append(Box(0, 0, WALL_TOP + ROOF * 0.5, R["map"] * 2 + 4, R["map"] * 2 + 4, ROOF, 0.0, "roof"))
    return {"boxes": boxes, "spawns": f1["spawns"], "data": data}
