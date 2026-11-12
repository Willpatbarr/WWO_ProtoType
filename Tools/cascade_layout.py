"""The Cascade PvP map (L_Cascade, 6v6) as a list of boxes, worked out from the mockup's JSON
(~/Documents/WWO_PvP_Mockups/cascade_map_geometry.json, made by cascade_map.py there; env
WWO_CASCADE_JSON overrides the path). The JSON is the source of truth: fix the layout in
cascade_map.py and re-run it, never here and never by hand in the JSON.

Plain Python, standard library only (no Unreal), so the layout can be checked outside the editor:
run this file (exit code 0 = every check passed). Tools/build_maps.py (build_cascade) turns each box
into an actor; Tools/preview_cascade.py draws the boxes as a plan and sections.

All in mockup meters: x east, y north, origin at the map center, z up. Boxes are ring_layout.Box:
center (x, y, z), size (length along its yaw, thickness, height), yaw in degrees counter-clockwise
from east, pitch in degrees (up toward its length), kind, shape ("cube" or "cylinder"; a cylinder's
length is its diameter).

What becomes what:
- Decks (decks[*].polygons): flat cells that sit wholly inside the polygon (4 m cells, split to 1 m
  along the edges) plus a 1.6 m strip along every polygon edge, set just inside it, so the outline is
  the polygon's own (no 1 m stair-steps along the round pads and the ring). Kinds: floor (z 0),
  floor_z3 (N/S arm ends and terminals), floor_z5 (high nodes), balcony, dais. The hall floor is the
  citadel's plinth (a solid cylinder r 18, z 0 to 3), not cells.
- bridge: one pitched slab per bridge (1 m thick), with a flat 1 m landing past each end so the deck
  meets the round node rims without corner gaps. arch: the visual arch under each bridge
  (bridge_arches; vertical slices, no collision).
- Stairs (hub_stairs, arm_stairs, citadel.balcony.interior_stairs): ramp (a smooth pitched slab, the
  collision, hidden in game; the Ring map's approach) plus steps (solid 0.25 m steps, visual only).
- Citadel: plinth; hall_wall (arc pieces r 15 to 18, z 3 to 11) with the four 6 m door gaps (z 3 to
  9, lintel above) and the four 3 m balcony doors (z 5 to 8.5, wall below and above); body: the
  building as stacked cylinders, r 18 from z 11 to 14 (the hall's ceiling slab is its underside), then
  1 m slices whose radius steps down each tier to the spire at z 40 (a 32-sided-ish ziggurat: plain
  BasicShapes cylinders, nothing new to make or test in Unreal); bracket: visual struts under the
  balcony.
- Tunnels: tunnel_roof (cells + strips from the roof polygons, z 6 to 6.6), tunnel_wall (the outer
  wall at r 29.6 as arc pieces, window gaps z 1 to 4.5); gallery_roof / gallery_wall on the NE and
  SW radial bridges (one window each side). The inner wall is the citadel's own wall (not repeated).
- wall / roof (base_walls with the back-wall window, base_roofs), cover by its kind (low_cover,
  block, crate, pillar, monolith, column) at its own z0, rail (1.1 m, pitched along sloped bridges
  and stairs), pier (cylinders down into the water), islet (rock cylinders under the maples).
- Scenery: trim (the orange emissive strips: frames, bands, rings, glyph; no collision), ivy (thin
  hanging cards), giant_arch (block segments), mesa (cliff pillars).
Points come back for the builder: plants (maples, ferns), lights, waterfalls, spawns, pve, nav,
boundary.
"""
import json
import math
import os
import sys


def _geometry(name):
    # The mockups folder is where the layout is made (the source of truth); Tools/geometry holds a
    # copy for machines without it. Copy the file there again after changing a layout.
    made = os.path.expanduser("~/Documents/WWO_PvP_Mockups/" + name)
    return made if os.path.exists(made) else os.path.join(os.path.dirname(os.path.abspath(__file__)), "geometry", name)


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ring_layout import Box, arc, standing  # noqa: E402

CASCADE_JSON = os.environ.get("WWO_CASCADE_JSON", _geometry("cascade_map_geometry.json"))
DECK_KIND = {"Ground": "floor", "NorthSouthArmsAndTerminals": "floor_z3", "HighNodes": "floor_z5", "Balcony": "balcony",
             "NodeDaisLow": "dais", "NodeDaisHigh": "dais"}
COARSE, FINE, STRIP = 4.0, 1.0, 1.6     # deck cells and the edge strip (STRIP > FINE * sqrt(2), so they meet)
GRID_X, GRID_Y = 100.0, 88.0            # the cell grid runs from -GRID to +GRID (multiples of COARSE): symmetric about 0; covers the 1.25x layout
BRIDGE_T, LANDING = 1.4, 1.0               # bridge decks 1.4 m thick, as the mockup sections draw them
RAMP_T, RAMP_DIP = 0.3, 0.6             # ramp slab thickness; how far it runs on under the floor at its foot
LANDING_RUN = 0.6
STEP_RISE = 0.25
PIER_UNDER = 0.3                        # a pier's top stays this far under the deck it carries
RAIL_T = 0.15
HALL_WALL_T = 3.0
DOOR_HALF_GAP = 3.5                     # m either side of a hall door's axis at the hall's inner face (6 m clear after the pieces' overshoot)
BALCONY_DOOR_HALF_GAP = 1.9             # the same for the 3 m balcony doors
TRIM_T = 0.3
WATER_Z = -10.0
STRUCTURE = ("floor", "floor_z3", "floor_z5", "balcony", "dais", "plinth", "hall_wall", "body", "bridge", "ramp", "landing", "rail", "wall",
             "roof", "tunnel_wall", "tunnel_roof", "gallery_wall", "gallery_roof", "pier", "islet")
COVER = ("low_cover", "block", "crate", "pillar", "monolith", "column")
NO_COLLISION = ("steps", "arch", "bracket", "trim", "ivy", "giant_arch", "mesa")
DECKS = ("floor", "floor_z3", "floor_z5", "balcony", "dais", "plinth", "bridge", "ramp", "landing")


# --- Geometry helpers -----------------------------------------------------------------------------

def polar(r, a):
    t = math.radians(a)
    return (r * math.cos(t), r * math.sin(t))


def in_polygon(px, py, poly):
    inside = False
    n = len(poly)
    for i in range(n):
        (x0, y0), (x1, y1) = poly[i], poly[(i + 1) % n]
        if (y0 > py) != (y1 > py) and px < x0 + (py - y0) * (x1 - x0) / (y1 - y0):
            inside = not inside
    return inside


def bbox(pts):
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return min(xs), max(xs), min(ys), max(ys)


def seg_dist(px, py, a, b):
    (x0, y0), (x1, y1) = a, b
    dx, dy = x1 - x0, y1 - y0
    L2 = dx * dx + dy * dy
    t = 0.0 if L2 == 0 else max(0.0, min(1.0, ((px - x0) * dx + (py - y0) * dy) / L2))
    return math.hypot(px - (x0 + t * dx), py - (y0 + t * dy))


class Region:
    """A deck polygon with holes: point tests with a bounding-box shortcut."""

    def __init__(self, poly):
        self.ext = [tuple(p) for p in poly["exterior"]]
        self.holes = [[tuple(p) for p in h] for h in poly["holes"]]
        self.bb = bbox(self.ext)
        self.hbb = [bbox(h) for h in self.holes]

    def rings(self):
        return [self.ext] + self.holes

    def keep(self, x, y):
        x0, x1, y0, y1 = self.bb
        if not (x0 <= x <= x1 and y0 <= y <= y1) or not in_polygon(x, y, self.ext):
            return False
        for h, (a0, a1, b0, b1) in zip(self.holes, self.hbb):
            if a0 <= x <= a1 and b0 <= y <= b1 and in_polygon(x, y, h):
                return False
        return True

    def edge_distance(self, x, y):
        best = 1e9
        for ring in self.rings():
            for i in range(len(ring)):
                best = min(best, seg_dist(x, y, ring[i], ring[(i + 1) % len(ring)]))
        return best


def flat(x, y, z_top, length, thickness, thick, yaw, kind):
    return Box(x, y, z_top - thick * 0.5, length, thickness, thick, yaw, kind)


def slab(p0, z0, p1, z1, width, thick, kind, ext0=0.0, ext1=0.0):
    """A pitched slab whose top surface runs from (p0, z0) to (p1, z1), carried on ext0 m past p0 and
    ext1 m past p1 (along the slope)."""
    (x0, y0), (x1, y1) = p0, p1
    run = math.hypot(x1 - x0, y1 - y0)
    ux, uy = (x1 - x0) / run, (y1 - y0) / run
    p = math.atan2(z1 - z0, run)
    slope = math.hypot(run, z1 - z0)
    s = (slope + ext1 - ext0) * 0.5          # mid-point of the top surface, along the slope from p0
    sx, sy, sz = x0 + ux * s * math.cos(p), y0 + uy * s * math.cos(p), z0 + s * math.sin(p)
    # Down half the thickness along the slab's normal.
    cx, cy, cz = sx + ux * math.sin(p) * thick * 0.5, sy + uy * math.sin(p) * thick * 0.5, sz - math.cos(p) * thick * 0.5
    return Box(cx, cy, cz, slope + ext0 + ext1, width, thick, math.degrees(math.atan2(uy, ux)), kind, pitch=math.degrees(p))


def seg_box(p0, p1, z0, z1, thickness, kind, extend=0.0):
    (x0, y0), (x1, y1) = p0, p1
    return standing((x0 + x1) * 0.5, (y0 + y1) * 0.5, z0, z1, math.hypot(x1 - x0, y1 - y0) + extend, thickness,
                    math.degrees(math.atan2(y1 - y0, x1 - x0)), kind)


def lerp2(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


def angle_of(x, y):
    return math.degrees(math.atan2(y, x))


def unwrap(angles):
    out = [angles[0]]
    for a in angles[1:]:
        while a - out[-1] > 180.0:
            a -= 360.0
        while a - out[-1] < -180.0:
            a += 360.0
        out.append(a)
    return out


# --- Decks ------------------------------------------------------------------------------------------

def deck_cells(region, z_top, thick, kind):
    """Flat cells wholly inside the region: COARSE cells, split down to FINE ones along the edges.
    The grid is fixed (centered on the origin), so a point-symmetric region gets point-symmetric cells."""
    out = []
    keep = region.keep

    def visit(ax, bx, ay, by):
        n = 5
        e = 1e-4   # samples just inside the cell, so a cell edge lying on a polygon edge tests the same on both halves of the map
        hits = [keep(ax + e + (bx - ax - 2 * e) * i / (n - 1), ay + e + (by - ay - 2 * e) * j / (n - 1)) for i in range(n) for j in range(n)]
        if all(hits):
            out.append(flat((ax + bx) * 0.5, (ay + by) * 0.5, z_top, bx - ax + 0.02, by - ay + 0.02, thick, 0.0, kind))
            return
        if (bx - ax) <= FINE + 1e-6 or (not any(hits) and (bx - ax) <= FINE * 2):
            return
        mx, my = (ax + bx) * 0.5, (ay + by) * 0.5
        for cx0, cx1 in ((ax, mx), (mx, bx)):
            for cy0, cy1 in ((ay, my), (my, by)):
                visit(cx0, cx1, cy0, cy1)

    x0, x1, y0, y1 = region.bb
    nx, ny = int(2 * GRID_X / COARSE), int(2 * GRID_Y / COARSE)
    for i in range(nx):
        ax = -GRID_X + COARSE * i
        if ax + COARSE < x0 or ax > x1:
            continue
        for j in range(ny):
            ay = -GRID_Y + COARSE * j
            if ay + COARSE < y0 or ay > y1:
                continue
            visit(ax, ax + COARSE, ay, ay + COARSE)
    return out


def deck_strips(region, z_top, thick, kind, width=STRIP):
    """A strip along every edge of the region's outline and holes, set just inside it. At a reflex
    corner the two strips run on past the corner (width x tan(half the turn)) so they close."""
    out = []
    for ring in region.rings():
        n = len(ring)
        if ring[0] == ring[-1]:
            ring = ring[:-1]
            n -= 1
        dirs, inward = [], []
        for i in range(n):
            (x0, y0), (x1, y1) = ring[i], ring[(i + 1) % n]
            L = math.hypot(x1 - x0, y1 - y0)
            ux, uy = (x1 - x0) / L, (y1 - y0) / L
            mx, my = (x0 + x1) * 0.5, (y0 + y1) * 0.5
            left = region.keep(mx - uy * 0.05, my + ux * 0.05)
            dirs.append((ux, uy, L))
            inward.append((-uy, ux) if left else (uy, -ux))
        ext = [[0.0, 0.0] for _ in range(n)]   # extension at each edge's start and end
        for i in range(n):
            a, b = dirs[i - 1], dirs[i]             # edge i-1 ends at vertex i, edge i starts there
            cross = a[0] * b[1] - a[1] * b[0]
            dot = a[0] * b[0] + a[1] * b[1]
            turn = math.atan2(cross, dot)
            # Interior on the left of travel: a left turn is convex, a right turn reflex.
            left_side = inward[i][0] == -b[1] and inward[i][1] == b[0]
            reflex = (turn < 0) if left_side else (turn > 0)
            if reflex:
                e = min(width * math.tan(abs(turn) * 0.5), width * 1.5)
                ext[i - 1][1] = e
                ext[i][0] = e
        for i in range(n):
            ux, uy, L = dirs[i]
            nx, ny = inward[i]
            e0, e1 = ext[i]
            (x0, y0) = ring[i]
            s = (L + e1 - e0) * 0.5
            cx, cy = x0 + ux * s + nx * width * 0.5, y0 + uy * s + ny * width * 0.5
            out.append(flat(cx, cy, z_top, L + e0 + e1, width, thick, math.degrees(math.atan2(uy, ux)), kind))
    return out


def deck(region, z_top, thick, kind):
    return deck_cells(region, z_top, thick, kind) + deck_strips(region, z_top, thick, kind)


# --- Stairs -----------------------------------------------------------------------------------------

def stair(bottom, top, width, base_z):
    """A ramp (collision, hidden in game) and solid steps (visual) from bottom (x, y, z) to top, and a
    flat landing running on 0.6 m past the top (the hub stairs' straight top edge meets the round
    plinth, which leaves slivers at its corners otherwise)."""
    L = math.hypot(top[0] - bottom[0], top[1] - bottom[1])
    ux, uy = (top[0] - bottom[0]) / L, (top[1] - bottom[1]) / L
    out = [slab(bottom[:2], bottom[2], top[:2], top[2], width, RAMP_T, "ramp", ext0=RAMP_DIP),
           slab(top[:2], top[2], (top[0] + ux * LANDING_RUN, top[1] + uy * LANDING_RUN), top[2], width, 0.5, "landing")]
    rise = top[2] - bottom[2]
    n = max(1, int(round(rise / STEP_RISE)))
    for i in range(n):
        a, b = lerp2(bottom, top, i / n), lerp2(bottom, top, (i + 1) / n)
        out.append(seg_box(a, b, base_z, bottom[2] + rise * (i + 1) / n, width, "steps"))
    return out


# --- Lines (rails, walls with windows) ---------------------------------------------------------------

def polyline_boxes(points, z, z_end, height, thickness, kind, overlap=0.1):
    """Pieces along a polyline whose foot climbs linearly (by length) from z to z_end."""
    out = []
    lens = [math.hypot(points[i + 1][0] - points[i][0], points[i + 1][1] - points[i][1]) for i in range(len(points) - 1)]
    total = sum(lens) or 1.0
    run = 0.0
    for i, L in enumerate(lens):
        if L < 0.01:
            continue
        za = z + (z_end - z) * run / total
        zb = z + (z_end - z) * (run + L) / total
        run += L
        (x0, y0), (x1, y1) = points[i], points[i + 1]
        p = math.atan2(zb - za, L)
        out.append(Box((x0 + x1) * 0.5, (y0 + y1) * 0.5, (za + zb) * 0.5 + height * 0.5, math.hypot(L, zb - za) + overlap, thickness,
                       height, math.degrees(math.atan2(y1 - y0, x1 - x0)), kind, pitch=math.degrees(p)))
    return out


def wall_with_openings(p0, p1, z0, z1, thickness, kind, openings):
    """A straight wall from p0 to p1 with openings [(t0, t1, sill, top)] (t along the wall, 0..1)."""
    out = []
    L = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
    cuts = sorted(openings)
    t = 0.0
    for t0, t1, sill, top in cuts:
        if t0 > t:
            out.append(seg_box(lerp2(p0, p1, t), lerp2(p0, p1, t0), z0, z1, thickness, kind))
        a, b = lerp2(p0, p1, t0), lerp2(p0, p1, t1)
        if sill > z0:
            out.append(seg_box(a, b, z0, sill, thickness, kind))
        if top < z1:
            out.append(seg_box(a, b, top, z1, thickness, kind))
        t = t1
    if t < 1.0 - 1e-6:
        out.append(seg_box(lerp2(p0, p1, t), p1, z0, z1, thickness, kind))
    return [b for b in out if b.length > 0.02 + 1e-6 or L < 0.05]


def arc_with_openings(r, a0, a1, z0, z1, thickness, kind, openings):
    """arc() from a0 to a1 with openings [(b0, b1, sill, top)] in degrees."""
    out = []
    a = a0
    for b0, b1, sill, top in sorted(openings):
        if b0 > a:
            out += arc(r, a, b0, z0, z1, thickness, kind)
        if sill > z0:
            out += arc(r, b0, b1, z0, sill, thickness, kind)
        if top < z1:
            out += arc(r, b0, b1, top, z1, thickness, kind)
        a = b1
    if a < a1:
        out += arc(r, a, a1, z0, z1, thickness, kind)
    return out


def frame(x, y, yaw, width, z0, height, kind="trim", t=TRIM_T):
    """A glowing door/portal frame: two jambs and a lintel across the opening (the opening faces yaw)."""
    tx, ty = -math.sin(math.radians(yaw)), math.cos(math.radians(yaw))
    out = []
    for s in (-1, 1):
        out.append(standing(x + tx * s * (width * 0.5 + t * 0.5), y + ty * s * (width * 0.5 + t * 0.5), z0, z0 + height, t, t, yaw, kind))
    out.append(standing(x, y, z0 + height, z0 + height + t, width + 2 * t, t, yaw + 90.0, kind))
    return out


# --- The map -----------------------------------------------------------------------------------------

def body_slices(tiers):
    """The citadel body above the hall: (z0, z1, r) cylinders. Tier 1 (r 18 to z 14) starts at the
    hall's ceiling (z 11); the tapering tiers are 1 m slices, each at its mid-height radius."""
    out = []
    t1 = tiers[0]
    out.append((11.0, t1["z1"], t1["r_bottom"]))
    for t in tiers[1:]:
        n = max(1, int(round(t["z1"] - t["z0"])))
        for i in range(n):
            za = t["z0"] + (t["z1"] - t["z0"]) * i / n
            zb = t["z0"] + (t["z1"] - t["z0"]) * (i + 1) / n
            f = (i + 0.5) / n
            out.append((za, zb, t["r_bottom"] + (t["r_top"] - t["r_bottom"]) * f))
    return out


def build(data=None):
    data = data or json.load(open(CASCADE_JSON))
    boxes = []
    decks = {}            # deck name -> (regions, boxes) for the coverage check

    # Decks.
    for d in data["decks"]:
        if d["name"] == "CitadelHallFloor":
            continue      # the plinth below is the hall floor
        kind = DECK_KIND[d["name"]]
        regions = [Region(p) for p in d["polygons"]]
        made = []
        for r in regions:
            made += deck(r, d["z"], d["thickness"], kind)
        decks[d["name"]] = (regions, made, d["z"])
        boxes += made

    # Bridges, their landings and the visual arch under each.
    bridges = {b["name"]: b for b in data["bridges"]}
    for b in data["bridges"]:
        s, e, z0, z1 = b["start"], b["end"], b["z_start"], b["z_end"]
        L = math.hypot(e[0] - s[0], e[1] - s[1])
        ux, uy = (e[0] - s[0]) / L, (e[1] - s[1]) / L
        if abs(z1 - z0) < 1e-6:
            boxes.append(slab(s, z0, e, z1, b["width"], BRIDGE_T, "bridge", LANDING, LANDING))
        else:
            boxes.append(slab(s, z0, e, z1, b["width"], BRIDGE_T, "bridge"))
            boxes.append(slab((s[0] - ux * LANDING, s[1] - uy * LANDING), z0, s, z0, b["width"], BRIDGE_T, "bridge"))
            boxes.append(slab(e, z1, (e[0] + ux * LANDING, e[1] + uy * LANDING), z1, b["width"], BRIDGE_T, "bridge"))
    # The arch spans only the open gap (a radial bridge starts inside the ring lane: no arch under the ring).
    didx = Index([c for v in decks.values() for c in v[1]])
    for a in data["bridge_arches"]:
        b = bridges[a["on"]]
        s, e = b["start"], b["end"]
        # (2 cm inside the decks' edges, so a sample right on an edge counts the same on both halves of the map)
        free = [k / 200.0 for k in range(201) if not any(covers2d(c, *lerp2(s, e, k / 200.0), grow=-0.02) for c in didx.near(*lerp2(s, e, k / 200.0)))]
        ta, tb = min(free), max(free)
        n = 12
        for i in range(n):
            t0, t1 = ta + (tb - ta) * i / n, ta + (tb - ta) * (i + 1) / n
            tm = (t0 + t1) * 0.5
            f = (i + 0.5) / n
            deck_bottom = a["z_start"] + (a["z_end"] - a["z_start"]) * tm - BRIDGE_T
            low = deck_bottom - a["thickness"] - a["rise"] * (2.0 * f - 1.0) ** 2
            boxes.append(seg_box(lerp2(s, e, t0), lerp2(s, e, t1), low, deck_bottom + 0.4, b["width"] - 1.0, "arch", extend=0.05))

    # Stairs.
    for s in data["hub_stairs"] + data["arm_stairs"]:
        boxes += stair(s["bottom"], s["top"], s["width"], -1.5)
    cit = data["citadel"]
    for s in cit["balcony"]["interior_stairs"]:
        boxes += stair(s["bottom"], s["top"], s["width"], s["bottom"][2])

    # Citadel: plinth, hall wall with its doors, body, balcony brackets.
    plat = cit["platform"]
    z_low = min(plat["z0"], -1.5)          # down to the ring deck's underside, so no gap shows under the plinth
    boxes.append(Box(0, 0, (z_low + plat["z1"]) * 0.5, plat["r"] * 2, plat["r"] * 2, plat["z1"] - z_low, 0.0, "plinth", shape="cylinder"))
    hall = cit["hall"]
    r_in, r_out = hall["r_clear"], hall["wall_r_out"]
    openings = []
    for d in cit["doors"]:
        h = math.degrees(math.asin(DOOR_HALF_GAP / r_in))
        openings.append((d["angle_deg"] - h, d["angle_deg"] + h, d["sill_z"], d["sill_z"] + d["height"]))
    for d in cit["balcony"]["doors"]:
        h = math.degrees(math.asin(BALCONY_DOOR_HALF_GAP / r_in))
        openings.append((d["angle_deg"] - h, d["angle_deg"] + h, d["sill_z"], d["sill_z"] + d["height"]))
    # Start the circle at the east door's edge, so the seam is in a gap and every run of wall is whole
    # (the run and its 180-degree twin then split into the same pieces).
    seam = -math.degrees(math.asin(DOOR_HALF_GAP / r_in))
    openings = [((a0 - seam) % 360.0 + seam, (a0 - seam) % 360.0 + seam + (a1 - a0), s, t) for a0, a1, s, t in openings]
    boxes += arc_with_openings((r_in + r_out) * 0.5, seam, seam + 360.0, hall["floor_z"], hall["ceiling_z"], r_out - r_in, "hall_wall", openings)
    slices = body_slices(cit["body_tiers"])
    for z0, z1, r in slices:
        boxes.append(Box(0, 0, (z0 + z1) * 0.5, r * 2, r * 2, z1 - z0, 0.0, "body", shape="cylinder"))
    bal = cit["balcony"]
    for a0, a1 in bal["segments_deg"]:
        k = max(2, int((a1 - a0) / 6.0))
        for i in range(k + 1):
            a = a0 + 1.5 + (a1 - a0 - 3.0) * i / k
            p0, p1 = polar(r_out, a), polar(bal["r_out"] - 0.6, a)
            boxes.append(slab(p0, 2.6, p1, bal["underside_z"], 0.5, 0.6, "bracket", ext0=0.2))

    # Tunnels: roofs, the outer wall with its windows, the bridge galleries.
    for t in data["tunnels"]:
        roof = t["roof"]
        for p in roof["polygons"]:
            boxes += deck(Region(p), roof["z"] + roof["thickness"], roof["thickness"], "tunnel_roof")
        ow = t["outer_wall"]
        angs = unwrap([angle_of(*p) for p in ow["points"]])
        a0, a1 = min(angs), max(angs)
        wins = []                                   # windows are given in 0..360; bring each next to the wall's angles
        for w in ow["windows"]:
            b0 = w["a0"]
            while b0 - a0 > 180.0:
                b0 -= 360.0
            while a0 - b0 > 180.0:
                b0 += 360.0
            wins.append((b0, b0 + (w["a1"] - w["a0"]), w["sill"], w["top"]))
        boxes += arc_with_openings(ow["r"], a0, a1, ow["z0"], ow["z0"] + ow["height"], ow["thickness"], "tunnel_wall", wins)
        g = t.get("bridge_gallery")
        if g:
            gr = g["roof"]
            for p in gr["polygons"]:
                boxes += deck(Region(p), gr["z"] + gr["thickness"], gr["thickness"], "gallery_roof")
            for w in g["walls"]:
                p0, p1 = w["points"]
                r0, r1 = math.hypot(*p0), math.hypot(*p1)
                win = w["window"]
                tm = (win["at_r"] - r0) / (r1 - r0)
                L = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
                dt = win["width"] * 0.5 / L
                boxes += wall_with_openings(p0, p1, -BRIDGE_T, gr["z"] + gr["thickness"], g["wall_thickness"], "gallery_wall",
                                            [(tm - dt, tm + dt, win["sill"], win["top"])])

    # Base rooms.
    for w in data["base_walls"]:
        cx, cy = w["center"]
        t = math.radians(w["yaw_deg_ccw_from_east"])
        ux, uy = math.cos(t), math.sin(t)
        p0 = (cx - ux * w["length"] * 0.5, cy - uy * w["length"] * 0.5)
        p1 = (cx + ux * w["length"] * 0.5, cy + uy * w["length"] * 0.5)
        z0, z1 = w["z0"], w["z0"] + w["height"]
        ops = []
        if "window" in w:
            win = w["window"]
            # The window's span is given in world y (the back walls run north-south).
            ta = [(v - p0[1]) / (p1[1] - p0[1]) for v in win["y"]]
            ops.append((min(ta), max(ta), win["sill"], win["top"]))
        boxes += wall_with_openings(p0, p1, z0, z1, w["thickness"], "wall", ops)
    for r in data["base_roofs"]:
        cx, cy = r["center"]
        boxes.append(Box(cx, cy, r["z"] + r["thickness"] * 0.5, r["size"][0], r["size"][1], r["thickness"], 0.0, "roof"))

    # Cover.
    for c in data["cover"]:
        x, y = c["center"]
        boxes.append(standing(x, y, c["z0"], c["z0"] + c["height"], c["length"], c["thickness"], c["yaw_deg_ccw_from_east"], c["kind"]))

    # Rails.
    for r in data["rails"]:
        boxes += polyline_boxes(r["points"], r["z"], r["z_end"], r["height"], RAIL_T, "rail")

    # Piers: the JSON's tops are flush with the deck (two faces fighting) and, under the arm stairs and
    # the sloped base links, above the walking surface; keep each 0.3 m under the lowest walkable top
    # over its footprint.
    walkable = [b for b in boxes if b.kind in DECKS]
    widx = Index(walkable)
    for p in data["piers"]:
        x, y = p["center"]
        top = p["z_top"]
        for k in range(17):
            sx, sy = (x, y) if k == 16 else (x + p["radius"] * math.cos(k * math.pi / 8), y + p["radius"] * math.sin(k * math.pi / 8))
            for c in widx.near(sx, sy):
                if covers2d(c, sx, sy):
                    top = min(top, top_at(c, sx, sy) - PIER_UNDER)
        boxes.append(Box(x, y, (top + p["z_bottom"]) * 0.5, p["radius"] * 2, p["radius"] * 2, top - p["z_bottom"], 0.0, "pier", shape="cylinder"))
    sc = data["scenery"]
    plants = []
    for i in sc["islets_with_trees"]:
        x, y = i["center"]
        z_bottom = WATER_Z - 4.0
        boxes.append(Box(x, y, (i["top_z"] + z_bottom) * 0.5, i["radius"] * 2, i["radius"] * 2, i["top_z"] - z_bottom, 0.0, "islet", shape="cylinder"))
        plants.append(("maple", x, y, i["top_z"]))
    for f in sc["ferns"]:
        x, y, z = f["at"]
        plants.append(("fern", x, y, z))

    # Orange trim.
    tier_r = {}
    for z0, z1, r in slices:
        tier_r[z1] = r
    for s in sc["emissive_strips"]:
        on = s["on"]
        if on in ("citadel_door", "tunnel_portal", "tunnel_gallery_portal"):
            x, y = s["at"]
            width = s["width"]
            if on == "citadel_door":
                width = 2.0 * r_out * math.sin(math.asin(DOOR_HALF_GAP / r_in))   # the opening's width at the outer face
                x, y = x + math.cos(math.radians(s["yaw_deg_ccw_from_east"])) * TRIM_T * 0.5, y + math.sin(math.radians(s["yaw_deg_ccw_from_east"])) * TRIM_T * 0.5
            boxes += frame(x, y, s["yaw_deg_ccw_from_east"], width, s["z0"], s["height"])
        elif on == "citadel_tier_ring":
            r = tier_r.get(s["z0"], s["radius"]) + 0.08
            boxes += arc(r, 0.0, 360.0, s["z0"] - 0.6, s["z0"] - 0.2, 0.15, "trim", step=r * 2 * math.pi / 32.0)
        elif on == "balcony_edge":
            for a0, a1 in bal["segments_deg"]:
                if a0 <= s["angle_deg"] <= a1:
                    boxes += arc(s["radius"] + 0.08, a0 + 0.3, a1 - 0.3, bal["underside_z"] + 0.35, bal["underside_z"] + 0.65, 0.15, "trim")
        elif on == "node_dais_ring":
            x, y = s["at"]
            for b in arc(s["radius"], 0.0, 360.0, s["z0"] - 0.3, s["z0"] - 0.1, 0.15, "trim", step=1.2):
                b.x += x
                b.y += y
                boxes.append(b)
        elif on == "tunnel_wall":
            x, y = s["at"]
            boxes.append(standing(x, y, s["z0"], s["z0"] + s["height"], s["length"], 0.12, s["yaw_deg_ccw_from_east"], "trim"))
        elif on == "arm_monolith":
            x, y = s["at"]
            mono = [c for c in data["cover"] if c["kind"] == "monolith" and abs(c["center"][0] - x) < 0.1 and abs(c["center"][1] - y) < 0.1][0]
            top = min(s["z0"] + s["height"], mono["z0"] + mono["height"] - 0.2)
            for sx in (-1, 1):
                boxes.append(standing(x + sx * (mono["length"] * 0.5 + 0.06), y, s["z0"], top, 0.12, 0.4, 0.0, "trim"))
        elif on == "base_glyph":
            x, y = s["at"]
            boxes.append(Box(x, y, s["z0"], s["radius"] * 2, s["radius"] * 2, 0.04, 0.0, "trim", shape="cylinder"))
        else:
            raise ValueError("unknown emissive strip: " + on)

    # Ivy: thin cards hanging from z_top.
    for v in sc["ivy"]:
        x, y = v["at"]
        if v["on"] == "bridge_underside":
            yaw = 0.0
        elif v["on"] == "arm_monolith":
            x += 2.1 if y > 0 else -2.1
            yaw = 90.0
        elif v["on"] == "base_back_window":
            x += -0.6 if x > 0 else 0.6
            yaw = 90.0
        else:                                          # citadel wall: on its face
            a = angle_of(x, y)
            x, y = polar(math.hypot(x, y) + 0.1, a)
            yaw = a + 90.0
        boxes.append(standing(x, y, v["z_top"] - v["length"], v["z_top"], 2.0, 0.08, yaw, "ivy"))

    # Distance: giant arches (half circles of blocks, legs down into the water) and mesas.
    for g in sc["giant_arches"]:
        cx, cy = g["center"]
        ux, uy = math.cos(math.radians(g["plane_yaw_deg"])), math.sin(math.radians(g["plane_yaw_deg"]))
        n = 26
        R, T = g["radius"], g["thickness"]
        for i in range(n):
            th = math.pi * (i + 0.5) / n
            chord = 2.0 * (R + T * 0.5) * math.tan(math.pi / n * 0.5) + 0.05
            x, y, z = cx + ux * R * math.cos(th), cy + uy * R * math.cos(th), g["base_z"] + R * math.sin(th)
            boxes.append(Box(x, y, z, chord, g["depth"], T, g["plane_yaw_deg"], "giant_arch", pitch=math.degrees(th) - 90.0))
        for s in (-1, 1):
            boxes.append(standing(cx + s * ux * R, cy + s * uy * R, g["base_z"] - 30.0, g["base_z"], T, g["depth"], g["plane_yaw_deg"], "giant_arch"))
    for m in sc["mesas"]:
        x, y = m["center"]
        boxes.append(standing(x, y, WATER_Z - 30.0, WATER_Z + m["height"], m["size"][0], m["size"][1], angle_of(x, y), "mesa"))

    lights = [(l["at"][0], l["at"][1], l["z"], tuple(l["color"]), l["radius"], l["name"]) for l in data["lights"]]
    spawns = {team: [(s["at"][0], s["at"][1], s["z"], s["yaw_deg_ccw_from_east"]) for s in data["spawns"][team]] for team in ("blue", "orange")}
    pve = data["pve"]
    pve_out = {"player": (pve["player_start"]["at"][0], pve["player_start"]["at"][1], pve["player_start"]["z"], pve["player_start"]["yaw_deg_ccw_from_east"]),
               "teammates": [(t["at"][0], t["at"][1], t["z"]) for t in pve["teammate_starts"]],
               "enemies": [(e["at"][0], e["at"][1], e["z"], list(e["kinds"])) for e in pve["enemy_spawn"]]}
    return {"boxes": boxes, "decks": decks, "plants": plants, "lights": lights, "waterfalls": sc["waterfalls"], "spawns": spawns,
            "pve": pve_out, "nav": data["nav"], "boundary": data["boundary"], "body_slices": slices, "data": data}


# --- Checks (run this file) -----------------------------------------------------------------------------

def local(b, x, y, z):
    """A point in the box's own frame (u along its length, v across, w up), pitch included."""
    t = math.radians(b.yaw)
    dx, dy, dz = x - b.x, y - b.y, z - b.z
    u = dx * math.cos(t) + dy * math.sin(t)
    v = -dx * math.sin(t) + dy * math.cos(t)
    p = math.radians(b.pitch)
    return u * math.cos(p) + dz * math.sin(p), v, -u * math.sin(p) + dz * math.cos(p)


def contains(b, x, y, z, grow=0.0):
    u, v, w = local(b, x, y, z)
    if abs(w) > b.height * 0.5 + grow:
        return False
    if b.shape == "cylinder":
        return math.hypot(x - b.x, y - b.y) <= b.length * 0.5 + grow
    return abs(u) <= b.length * 0.5 + grow and abs(v) <= b.thickness * 0.5 + grow


def covers2d(b, x, y, grow=0.0):
    if b.shape == "cylinder":
        return math.hypot(x - b.x, y - b.y) <= b.length * 0.5 + grow
    t = math.radians(b.yaw)
    dx, dy = x - b.x, y - b.y
    u = dx * math.cos(t) + dy * math.sin(t)
    v = -dx * math.sin(t) + dy * math.cos(t)
    return abs(u) <= b.length * 0.5 * math.cos(math.radians(b.pitch)) + grow and abs(v) <= b.thickness * 0.5 + grow


def top_at(b, x, y):
    """Height of the box's top face over (x, y) (pitched boxes slope)."""
    if b.shape == "cylinder" or abs(b.pitch) < 1e-9:
        return b.z + b.height * 0.5
    t = math.radians(b.yaw)
    u = (x - b.x) * math.cos(t) + (y - b.y) * math.sin(t)
    p = math.radians(b.pitch)
    return b.z + u * math.tan(p) + b.height * 0.5 / math.cos(p)


def bottom_at(b, x, y):
    if b.shape == "cylinder" or abs(b.pitch) < 1e-9:
        return b.z - b.height * 0.5
    return top_at(b, x, y) - b.height / math.cos(math.radians(b.pitch))


def corners2d(b):
    if b.shape == "cylinder":
        r = b.length * 0.5
        return [(b.x + r * math.cos(a), b.y + r * math.sin(a)) for a in (0, math.pi / 2, math.pi, 3 * math.pi / 2)]
    t = math.radians(b.yaw)
    hl = b.length * 0.5 * math.cos(math.radians(b.pitch))
    hw = b.thickness * 0.5
    return [(b.x + su * hl * math.cos(t) - sv * hw * math.sin(t), b.y + su * hl * math.sin(t) + sv * hw * math.cos(t)) for su in (-1, 1) for sv in (-1, 1)]


class Index:
    """Boxes bucketed by 4 m plan cells, for point queries."""

    def __init__(self, boxes, cell=4.0):
        self.cell = cell
        self.grid = {}
        for b in boxes:
            xs, ys = zip(*corners2d(b))
            for i in range(int(math.floor(min(xs) / cell)), int(math.floor(max(xs) / cell)) + 1):
                for j in range(int(math.floor(min(ys) / cell)), int(math.floor(max(ys) / cell)) + 1):
                    self.grid.setdefault((i, j), []).append(b)

    def near(self, x, y):
        return self.grid.get((int(math.floor(x / self.cell)), int(math.floor(y / self.cell))), [])


def same_line(a, b, tol=0.5):
    """Two direction vectors along the same line (either way), within tol degrees."""
    d = max(-1.0, min(1.0, abs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2])))
    return math.degrees(math.acos(d)) < tol


def direction(b):
    t, p = math.radians(b.yaw), math.radians(b.pitch)
    return (math.cos(t) * math.cos(p), math.sin(t) * math.cos(p), math.sin(p))


def check():
    out = build()
    boxes, data = out["boxes"], out["data"]
    ok = True
    kinds = {}
    for b in boxes:
        kinds[b.kind] = kinds.get(b.kind, 0) + 1
    print("boxes %d: %s" % (len(boxes), ", ".join("%s %d" % kv for kv in sorted(kinds.items()))))

    def report(name, failures, show=6):
        nonlocal ok
        good = not failures
        ok = ok and good
        print("%-4s %s%s" % ("ok" if good else "FAIL", name, "" if good else ": %d %s" % (len(failures), failures[:show])))

    # 1. Point symmetry (180 degrees about the center; not a mirror). Mesas are random set dressing.
    index = {}
    for b in boxes:
        index.setdefault((b.kind, round(b.x), round(b.y)), []).append(b)
    lone = []
    for b in boxes:
        if b.kind == "mesa":
            continue
        rx, ry = -b.x, -b.y
        rd = direction(b)
        rd = (-rd[0], -rd[1], rd[2])
        found = False
        for gx in (round(rx) - 1, round(rx), round(rx) + 1):
            for gy in (round(ry) - 1, round(ry), round(ry) + 1):
                for c in index.get((b.kind, gx, gy), []):
                    if (abs(c.x - rx) < 0.02 and abs(c.y - ry) < 0.02 and abs(c.z - b.z) < 0.02 and abs(c.length - b.length) < 0.02
                            and abs(c.thickness - b.thickness) < 0.02 and abs(c.height - b.height) < 0.02
                            and (b.shape == "cylinder" or same_line(direction(c), rd))):
                        found = True
        if not found:
            lone.append((b.kind, round(b.x, 2), round(b.y, 2), round(b.z, 2)))
    report("180-degree symmetry (every kind but mesa)", lone)

    # 2. Deck coverage: every point of every deck polygon is under a box of that deck; and no deck
    #    box sticks out of its polygon.
    gaps, poke = [], []
    sidx_all = Index(boxes)
    for name, (regions, made, z) in out["decks"].items():
        idx = Index(made)
        for r in regions:
            x0, x1, y0, y1 = r.bb
            step = 0.25
            nx, ny = int((x1 - x0) / step) + 1, int((y1 - y0) / step) + 1
            for i in range(nx):
                for j in range(ny):
                    x, y = x0 + i * step + 0.01, y0 + j * step + 0.013
                    if r.keep(x, y) and not any(covers2d(b, x, y) for b in idx.near(x, y)):
                        gaps.append((name, round(x, 2), round(y, 2)))
            for b in made:
                for (cx, cy) in corners2d(b):
                    if not any(rr.keep(cx, cy) for rr in regions):
                        d = r.edge_distance(cx, cy)
                        # Under the plinth or a stair it doesn't show (the slivers where the hub stairs' cut meets the plinth).
                        hidden = any(c.kind in ("plinth", "ramp", "steps", "landing") and covers2d(c, cx, cy) for c in sidx_all.near(cx, cy))
                        if d > 0.12 and r.keep(b.x, b.y) and not hidden:
                            poke.append((name, round(cx, 2), round(cy, 2), round(d, 2)))
    report("deck coverage (0.25 m samples, every deck polygon)", gaps)
    report("no deck box more than 12 cm outside its polygon", poke)
    print("     deck boxes: %s" % ", ".join("%s %d" % (n, len(v[1])) for n, v in out["decks"].items()))

    # 3. Counts against the JSON.
    want = data["checks"]["counts"]
    have = {"cover": sum(kinds.get(k, 0) for k in COVER), "rails": len(data["rails"]), "waterfalls": len(out["waterfalls"]),
            "lights": len(out["lights"]), "islets_with_trees": kinds.get("islet", 0), "spawns_each_team": len(out["spawns"]["blue"]),
            "enemy_spawn": len(out["pve"]["enemies"])}
    report("counts match the JSON %s" % have, [(k, have[k], want[k]) for k in want if have.get(k) != want[k]])
    report("12 spawns (6 a team)", [] if len(out["spawns"]["blue"]) == 6 and len(out["spawns"]["orange"]) == 6 else [len(out["spawns"]["blue"]), len(out["spawns"]["orange"])])
    report("lights: 42 and no shadows", [] if len(data["lights"]) == 42 and not any(l["cast_shadow"] for l in data["lights"]) else [len(data["lights"])])

    solid = [b for b in boxes if b.kind not in NO_COLLISION and b.kind not in ("mesa",)]
    sidx = Index(solid)
    floors = [b for b in boxes if b.kind in DECKS or b.kind in ("body",)]
    fidx = Index(floors)

    def surface(x, y, z, tol=0.05, exclude=None):
        """A walkable top within tol of z at (x, y)."""
        return any(b is not exclude and covers2d(b, x, y) and abs(top_at(b, x, y) - z) <= tol for b in fidx.near(x, y))

    # 4. Every bridge end lands on a deck at its height (just past the end, off the bridge itself).
    bad = []
    for b in data["bridges"]:
        s, e = b["start"], b["end"]
        L = math.hypot(e[0] - s[0], e[1] - s[1])
        ux, uy = (e[0] - s[0]) / L, (e[1] - s[1]) / L
        for (px, py), z, sgn in ((s, b["z_start"], -1), (e, b["z_end"], 1)):
            for off in (-2.0, 0.0, 2.0):                 # the middle and both sides of the 6 m deck
                x, y = px + sgn * ux * 1.5 - uy * off, py + sgn * uy * 1.5 + ux * off
                if any(c.kind != "bridge" and covers2d(c, x, y) and abs(top_at(c, x, y) - z) <= 0.05 for c in fidx.near(x, y)):
                    continue
                # The base links meet their 6 m doors at a skew, so a side of the deck runs into the door's jamb.
                if not any(c.kind == "wall" and contains(c, x, y, z + 1.0, 0.6) for c in sidx.near(x, y)):
                    bad.append((b["name"], "start" if sgn < 0 else "end", off))
    report("every bridge end lands on a deck at its height (or a door jamb)", bad)

    # 5. Nothing floats: cover stands exactly on a walkable top; walls and rails stand on (or in) something.
    floating = []
    for b in boxes:
        if b.kind in COVER:
            if not surface(b.x, b.y, b.z - b.height * 0.5):
                floating.append((b.kind, round(b.x, 2), round(b.y, 2), round(b.z - b.height * 0.5, 2)))
        elif b.kind in ("wall", "rail", "tunnel_wall", "gallery_wall", "hall_wall", "steps", "pier", "islet"):
            zb = bottom_at(b, b.x, b.y)
            if zb < WATER_Z or (b.kind == "steps" and abs(zb + 1.5) < 1e-6):
                continue                                 # down in the water; steps filling a stair well to the deck's underside
            grow = 0.5 if b.kind in ("rail", "gallery_wall") else 0.1
            if any(c is not b and covers2d(c, b.x, b.y, grow) and bottom_at(c, b.x, b.y) - 0.05 <= zb <= top_at(c, b.x, b.y) + 0.05
                   for c in sidx.near(b.x, b.y) if c.kind not in ("rail",)):
                continue
            # A lintel or a piece over a window: held by the pieces either side of it.
            t = math.radians(b.yaw)
            ends = [(b.x + s * (b.length * 0.5 + 0.08) * math.cos(t), b.y + s * (b.length * 0.5 + 0.08) * math.sin(t)) for s in (-1, 1)]
            if all(any(c is not b and contains(c, ex, ey, zb + 0.05, 0.1) for c in sidx.near(ex, ey)) for ex, ey in ends):
                continue
            floating.append((b.kind, round(b.x, 2), round(b.y, 2), round(zb, 2)))
    report("nothing floats (cover on a walkable top, walls/rails/steps on something or held at both ends)", floating)
    # Piers stay under the decks they carry.
    poking = []
    for b in boxes:
        if b.kind != "pier":
            continue
        top = b.z + b.height * 0.5
        for k in range(16):
            x, y = b.x + b.length * 0.5 * math.cos(k * math.pi / 8), b.y + b.length * 0.5 * math.sin(k * math.pi / 8)
            if any(c.kind in DECKS and covers2d(c, x, y) and top_at(c, x, y) < top + PIER_UNDER - 0.01 for c in fidx.near(x, y)):
                poking.append((round(b.x, 1), round(b.y, 1), round(top, 2)))
                break
    report("pier tops 0.3 m under every deck over them", poking)

    # 6. Spawns and PvE markers stand on a floor, clear of everything.
    bad = []
    pts = [("spawn", x, y, z) for t in ("blue", "orange") for (x, y, z, _) in out["spawns"][t]]
    pts += [("pve_player", out["pve"]["player"][0], out["pve"]["player"][1], out["pve"]["player"][2])]
    pts += [("teammate", x, y, z) for (x, y, z) in out["pve"]["teammates"]]
    pts += [("enemy", x, y, z) for (x, y, z, _) in out["pve"]["enemies"]]
    for name, x, y, z in pts:
        if not surface(x, y, z):
            bad.append((name, x, y, z, "no floor"))
        for h in (0.5, 1.0, 1.7):
            if any(contains(b, x, y, z + h) for b in sidx.near(x, y) if b.kind != "ramp"):
                bad.append((name, x, y, z, "blocked at +%.1f" % h))
                break
    report("spawns and PvE markers on a floor and clear (%d points)" % len(pts), bad)

    # 7. The hall doors are open: a 6 m wide, 5.8 m tall corridor through each; the balcony doors 3 m.
    walls = [b for b in boxes if b.kind in ("hall_wall", "body")]
    blocked = []
    for d, half, height in [(d, 3.0, d["height"]) for d in data["citadel"]["doors"]] + [(d, 1.5, d["height"]) for d in data["citadel"]["balcony"]["doors"]]:
        a = math.radians(d["angle_deg"])
        h_in, h_out = data["citadel"]["hall"]["r_clear"], data["citadel"]["hall"]["wall_r_out"]
        for r in [h_in - 0.4 + (h_out - h_in + 0.8) * k / 19 for k in range(20)]:
            for off in [-half + 0.05 + (2 * half - 0.1) * k / 8 for k in range(9)]:
                for z in [d["sill_z"] + 0.1 + (height - 0.2) * k / 6 for k in range(7)]:
                    x, y = r * math.cos(a) - off * math.sin(a), r * math.sin(a) + off * math.cos(a)
                    if any(contains(b, x, y, z) for b in walls):
                        blocked.append((d["angle_deg"], round(r, 1), round(off, 1), round(z, 1)))
    report("hall doors (6 m) and balcony doors (3 m) open", blocked)
    # ...and the wall is there between them.
    shut = []
    for a in (30.0, 120.0, 210.0, 300.0):
        x, y = polar((data["citadel"]["hall"]["r_clear"] + data["citadel"]["hall"]["wall_r_out"]) * 0.5, a)
        if not any(contains(b, x, y, 7.0) for b in walls):
            shut.append(a)
    report("hall wall solid between the doors", shut)

    # 8. Tunnel windows are open (and the wall is solid beside them); gallery windows too.
    twalls = [b for b in boxes if b.kind in ("tunnel_wall", "gallery_wall")]
    bad = []
    for t in data["tunnels"]:
        ow = t["outer_wall"]
        for w in ow["windows"]:
            for f in (0.15, 0.5, 0.85):
                a = w["a0"] + (w["a1"] - w["a0"]) * f
                for z in (w["sill"] + 0.2, (w["sill"] + w["top"]) * 0.5, w["top"] - 0.2):
                    x, y = polar(ow["r"], a)
                    if any(contains(b, x, y, z) for b in twalls):
                        bad.append((t["name"], round(a, 1), z, "window blocked"))
            for a, z in ((w["a0"] - 0.6, 3.0), (w["a0"] + 1.0, 0.5), (w["a0"] + 1.0, 5.5)):
                x, y = polar(ow["r"], a)
                if not any(contains(b, x, y, z) for b in twalls):
                    bad.append((t["name"], round(a, 1), z, "wall missing"))
        g = t.get("bridge_gallery")
        if g:
            for w in g["walls"]:
                p0, p1 = w["points"]
                r0, r1 = math.hypot(*p0), math.hypot(*p1)
                tm = (w["window"]["at_r"] - r0) / (r1 - r0)
                x, y = lerp2(p0, p1, tm)
                if any(contains(b, x, y, 2.5) for b in twalls):
                    bad.append((t["name"], "gallery window blocked"))
                x, y = lerp2(p0, p1, 0.1)
                if not any(contains(b, x, y, 2.5) for b in twalls):
                    bad.append((t["name"], "gallery wall missing"))
    report("tunnel and gallery windows open, walls solid beside them", bad)

    # 9. Tunnels: 6 m clear inside (nothing but the support pillars between the floor and the roof).
    bad = []
    for t in data["tunnels"]:
        for f in (0.2, 0.5, 0.8):
            a = t["a0"] + (t["a1"] - t["a0"]) * f
            for r in (t["r_in"] + 1.5, (t["r_in"] + t["r_out"]) * 0.5, t["r_out"] - 1.5):
                x, y = polar(r, a)
                roof = [b for b in sidx.near(x, y) if b.kind == "tunnel_roof" and covers2d(b, x, y)]
                if not roof or abs(min(bottom_at(b, x, y) for b in roof) - t["clear_height"]) > 0.02:
                    bad.append((t["name"], round(a), r, "roof"))
    report("tunnel roofs over the ring lane with 6 m clear", bad)

    # 10. The ring stays passable beside each hub stair (the outer half of the lane, r 24 to 30, open
    #     to 3 m up), and the balcony leaves 4 m of headroom over the ring lane.
    structure = [b for b in solid if b.kind not in COVER and b.kind not in DECKS]
    bad = []
    for s in data["hub_stairs"]:
        a = angle_of(*s["bottom"][:2])
        rb, rr = math.hypot(*s["bottom"][:2]), data["radii"]["ring_outer"]
        for r in (rb + 0.8, (rb + rr) * 0.5, rr - 1.5):
            for da in (-8.0, 0.0, 8.0):
                x, y = polar(r, a + da)
                for z in (0.3, 1.5, 2.8):
                    if any(contains(b, x, y, z) for b in structure + [b for b in solid if b.kind in ("ramp", "landing")]):
                        bad.append((s["name"], r, da, z))
    for a0, a1 in data["citadel"]["balcony"]["segments_deg"]:
        for f in (0.25, 0.5, 0.75):
            bal = data["citadel"]["balcony"]
            x, y = polar(bal["r_in"] + (bal["r_out"] - bal["r_in"]) * 0.6, a0 + (a1 - a0) * f)
            for z in (0.5, 2.0, 3.5, 3.95):
                if any(contains(b, x, y, z) for b in structure):
                    bad.append(("balcony", a0, f, z))
            if not surface(x, y, data["citadel"]["balcony"]["top_z"]):
                bad.append(("balcony floor", a0, f))
    report("ring passable beside the hub stairs; 4 m under the balcony", bad)

    # Waterfalls: each sheet (width across its facing, from the water up to top_z) stays 0.2 m clear of every
    # solid piece (decks, bridges, arches, rails, islets, ...), so none pours through a walkway.
    scenery = ("giant_arch", "mesa", "ivy", "trim")
    bad = []
    for w in out["waterfalls"]:
        f = math.radians(w["facing_deg"])
        for i in range(21):
            t = (i / 20.0 - 0.5) * w["width"]
            x, y = w["at"][0] - math.sin(f) * t, w["at"][1] + math.cos(f) * t
            hit = next((b.kind for b in boxes if b.kind not in scenery and covers2d(b, x, y, 0.2)
                        and bottom_at(b, x, y) < w["top_z"] and top_at(b, x, y) > WATER_Z), None)
            if hit:
                bad.append((w["name"], tuple(w["at"]), hit))
                break
    report("waterfalls clear of every deck, bridge and islet", bad)

    sizes = {}
    for p in out["plants"]:
        sizes[p[0]] = sizes.get(p[0], 0) + 1
    print("     plants %s, lights %d, waterfalls %d, spawns %d + %d, PvE enemy markers %d, body slices %d"
          % (sizes, len(out["lights"]), len(out["waterfalls"]), len(out["spawns"]["blue"]), len(out["spawns"]["orange"]),
             len(out["pve"]["enemies"]), len(out["body_slices"])))
    return ok


if __name__ == "__main__":
    raise SystemExit(0 if check() else 1)
