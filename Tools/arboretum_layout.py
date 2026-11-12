"""The Arboretum PvP map (L_Arboretum) as a list of boxes, worked out from the mockup's JSON
(~/Documents/WWO_PvP_Mockups/arboretum_map_geometry.json, made by arboretum_map.py there).

Plain Python (no Unreal), so the layout can be checked outside the editor (run this file);
Tools/build_maps.py turns each box into an actor. All in mockup meters: x east, y north, origin at
the map center, z up. Boxes are ring_layout.Box: center, size (length along its yaw, thickness,
height), yaw in degrees counter-clockwise from east, pitch (up toward its length), kind, shape.

One floor at z 0 with the tunnel 4 m under it:
- floor: a slab over the whole footprint, cut out over the two straight stairs and the two spiral
  stairs (cells, fine ones along the cut edges).
- wall (to 5 m), wall_tall (the ring's outer wall and the greenhouses, 8 m), wall_center (the
  center room, 9 m): the JSON's wall arcs as short straight pieces and its straight segments, with
  a lintel over every doorway from the door's height to the wall's top. buttress: the ring's fins.
- roof (5 m): one band over the ring, connectors and the solid core, and cells over every outer
  room that isn't a greenhouse. glass: the greenhouses' roofs (8 m) and the center room's skylight;
  frame: the ribs, hub and rings under that skylight.
- tunnel_floor, tunnel_wall, root (the column under the tree): the run under the map with the root
  chamber in the middle. Its ceiling is the floor slab's underside, so it is 3.5 m clear.
- ramp: the two straight stairs (8 m run) and the two spirals (short pitched pieces round the arc).
  rail: 1.1 m rails round the stair openings, open at each stair's top.
- planter (1 m), cover (1.2 m), crate: the cover. plants, lights and spawns come back as points.
- Decoration: pipe, pipe_rust, pipe_dark (runs along the ring's ceiling and walls, the connectors,
  round every room and through the tunnel, with risers; the kit's pipe clusters come back as
  props); fixture and lamp / lamp_tunnel (a housing and glowing panel at every light, on a rod where
  the ceiling is higher, on a pole for the low fill lights). Plants: dense on the planters, in
  clusters along the walls (thick in the greenhouses), with fallen leaves; sizes tiny to huge.
"""
import json
import math
import os
import random

import ring_layout as RL
from ring_layout import Box, arc, in_rect, standing

ARBORETUM_JSON = os.environ.get("WWO_ARBORETUM_JSON", os.path.expanduser("~/Documents/WWO_PvP_Mockups/arboretum_map_geometry.json"))
SLAB = 0.5
RAIL_H, RAIL_T = 1.1, 0.15
STAIR_HOLE = 6.5      # of a straight stair's 8 m run, the part open to the room (the rest is under the floor)
GLASS_T, ROOF_T = 0.15, 0.3


def polar(r, a):
    t = math.radians(a)
    return (r * math.cos(t), r * math.sin(t))


def seg_box(p0, p1, z0, z1, thickness, kind, extend=0.0):
    (x0, y0), (x1, y1) = p0, p1
    return standing((x0 + x1) * 0.5, (y0 + y1) * 0.5, z0, z1, math.hypot(x1 - x0, y1 - y0) + extend, thickness,
                    math.degrees(math.atan2(y1 - y0, x1 - x0)), kind)


def in_arc(px, py, r0, r1, a0, a1, grow=0.0):
    r = math.hypot(px, py)
    if not (r0 - grow <= r <= r1 + grow):
        return False
    lo, hi = min(a0, a1), max(a0, a1)
    pad = math.degrees(grow / max(r, 0.1))
    a = math.degrees(math.atan2(py, px))
    return any(lo - pad <= a + k <= hi + pad for k in (-360.0, 0.0, 360.0))


def in_polygon(px, py, poly):
    inside = False
    n = len(poly)
    for i in range(n):
        (x0, y0), (x1, y1) = poly[i], poly[(i + 1) % n]
        if (y0 > py) != (y1 > py) and px < x0 + (py - y0) * (x1 - x0) / (y1 - y0):
            inside = not inside
    return inside


def cells(x0, x1, y0, y1, keep, z_top, thick, kind, coarse=4.0, fine=0.5):
    """Flat boxes covering the part of the rectangle where keep(x, y) holds: coarse cells where a
    whole cell is in, split down to fine ones along the edge."""
    out = []

    def visit(ax, bx, ay, by):
        n = 5
        hits = [keep(ax + (bx - ax) * i / (n - 1), ay + (by - ay) * j / (n - 1)) for i in range(n) for j in range(n)]
        if all(hits):
            out.append(Box((ax + bx) * 0.5, (ay + by) * 0.5, z_top - thick * 0.5, bx - ax + 0.02, by - ay + 0.02, thick, 0.0, kind))
        elif any(hits) or (bx - ax) > fine * 2:
            if (bx - ax) <= fine + 1e-6:
                if keep((ax + bx) * 0.5, (ay + by) * 0.5):
                    out.append(Box((ax + bx) * 0.5, (ay + by) * 0.5, z_top - thick * 0.5, bx - ax + 0.02, by - ay + 0.02, thick, 0.0, kind))
                return
            mx, my = (ax + bx) * 0.5, (ay + by) * 0.5
            for cx0, cx1 in ((ax, mx), (mx, bx)):
                for cy0, cy1 in ((ay, my), (my, by)):
                    visit(cx0, cx1, cy0, cy1)

    nx = int(math.ceil((x1 - x0) / coarse))
    ny = int(math.ceil((y1 - y0) / coarse))
    for i in range(nx):
        for j in range(ny):
            visit(x0 + coarse * i, x0 + coarse * (i + 1), y0 + coarse * j, y0 + coarse * (j + 1))
    return out


def spiral_ramp(s, z_top, z_bottom, pieces=24):
    """A ramp round an arc from its top angle (z_top) to its bottom angle (z_bottom), as short
    pitched pieces: each spans the stair's width radially and climbs along the tangent."""
    out = []
    r0, r1 = s["r_inner"], s["r_outer"]
    rm = (r0 + r1) * 0.5
    top, bottom = s["top_deg_ccw_from_east"], s["bottom_deg_ccw_from_east"]
    turn = 1.0 if bottom > top else -1.0       # which way round it descends
    step = abs(bottom - top) / pieces
    rise = (z_top - z_bottom) / pieces
    pitch = math.degrees(math.atan2(rise, rm * math.radians(step)))
    thick = 0.3
    for i in range(pieces):
        a = top + (bottom - top) * (i + 0.5) / pieces
        z = z_top - (z_top - z_bottom) * (i + 0.5) / pieces
        x, y = polar(rm, a)
        length = (r1 * math.radians(step) + 0.12) / math.cos(math.radians(pitch))
        out.append(Box(x, y, z - thick * 0.5 * math.cos(math.radians(pitch)), length, r1 - r0, thick, a - 90.0 * turn, "ramp", pitch=pitch))
    return out


def room_centroid(outline):
    """The polygon's area centroid (a vertex average would sit against the ring, where the outline's arc
    has most of its points)."""
    a = cx = cy = 0.0
    n = len(outline)
    for i in range(n):
        (x0, y0), (x1, y1) = outline[i], outline[(i + 1) % n]
        w = x0 * y1 - x1 * y0
        a += w
        cx += (x0 + x1) * w
        cy += (y0 + y1) * w
    return (cx / (3.0 * a), cy / (3.0 * a))


def build(data=None):
    data = data or json.load(open(ARBORETUM_JSON))
    R, H, T = data["radii"], data["heights_m"], data["tunnel"]
    r_center, r_in, r_out, wall_t = R["center_room"], R["ring_inner"], R["ring_outer"], R["wall_thickness"]
    tz = T["floor_z_m"]
    tdeg = T["run"]["yaw_deg_ccw_from_east"]
    half_w = T["width_m"] * 0.5
    chamber_r = T["root_chamber"]["radius"]
    boxes = []

    # --- Stair openings in the floor ---
    holes = []
    for s in data["stairs"]:
        (bx, by), (tx, ty) = s["bottom"], s["top"]
        run = math.hypot(tx - bx, ty - by)
        ux, uy = (tx - bx) / run, (ty - by) / run
        cx, cy = tx - ux * STAIR_HOLE * 0.5, ty - uy * STAIR_HOLE * 0.5
        holes.append({"center": [cx, cy], "length": STAIR_HOLE, "thickness": s["thickness"], "yaw_deg_ccw_from_east": s["yaw_deg_ccw_from_east"]})
    spirals = data["spiral_stairs"]

    def on_floor(x, y):
        if any(in_rect(x, y, h, 0.0) for h in holes):
            return False
        return not any(in_arc(x, y, s["r_inner"], s["r_outer"], s["top_deg_ccw_from_east"], s["bottom_deg_ccw_from_east"]) for s in spirals)

    outlines = [r["outline"] for r in data["rooms_outside_ring"]]
    xs = [p[0] for o in outlines for p in o]
    ys = [p[1] for o in outlines for p in o]
    x0, x1 = math.floor(min(xs)) - 2.0, math.ceil(max(xs)) + 2.0
    y0, y1 = math.floor(min(ys)) - 2.0, math.ceil(max(ys)) + 2.0
    half = max(-x0, x1)                       # the same cells either side of the north-south axis
    boxes += cells(-half, half, y0, y1, on_floor, 0.0, SLAB, "floor")

    # --- Walls ---
    def wall_kind(h):
        return "wall_center" if h >= H["center_room"] else ("wall_tall" if h > H["lab"] else "wall")

    for w in data["wall_arcs"]:
        boxes += arc(w["radius"], w["from_deg"], w["to_deg"], 0.0, w["height"], w["thickness"], wall_kind(w["height"]))
    for w in data["wall_segments"]:
        boxes.append(seg_box(w["from"], w["to"], 0.0, w["height"], w["thickness"], wall_kind(w["height"]), extend=w["thickness"]))
    arc_radii = sorted({w["radius"] for w in data["wall_arcs"]})
    for d in data["doorways"]:
        cx, cy = d["center"]
        r = math.hypot(cx, cy)
        kind = wall_kind(d["wall_height"])
        if any(abs(r - rr) < 0.02 for rr in arc_radii):
            a = math.degrees(math.atan2(cy, cx))
            h = math.degrees(math.asin(d["width"] * 0.5 / r))
            boxes += arc(r, a - h, a + h, d["height"], d["wall_height"], wall_t, kind)
        else:
            boxes.append(standing(cx, cy, d["height"], d["wall_height"], d["width"] + wall_t, wall_t, d["wall_yaw_deg_ccw_from_east"], kind))
    for f in data["buttresses_full_height"]:
        x, y = f["center"]
        boxes.append(standing(x, y, 0.0, H["ring_corridor"], f["length"], f["thickness"], f["yaw_deg_ccw_from_east"], "buttress"))

    # --- Roofs ---
    core_roof = H["ring_corridor"]
    boxes += arc((r_center + r_out) * 0.5, 0.0, 360.0, core_roof, core_roof + ROOF_T, r_out - r_center + wall_t, "roof")
    boxes.append(Box(0, 0, H["center_room"] + GLASS_T * 0.5, (r_center + wall_t) * 2, (r_center + wall_t) * 2, GLASS_T, 0.0, "glass", shape="cylinder"))
    # The center skylight is one pane, which reads as open sky from below: a frame of ribs, a hub and a
    # rim under it makes it read as a glass roof.
    top = H["center_room"]
    for k in range(8):
        a = k * 45.0
        x, y = polar((r_center + wall_t) * 0.5, a)
        boxes.append(Box(x, y, top - 0.15, r_center + wall_t, 0.22, 0.3, a, "frame"))
    boxes.append(Box(0, 0, top - 0.2, 1.6, 1.6, 0.4, 0.0, "frame", shape="cylinder"))
    boxes += arc(r_center - 0.1, 0.0, 360.0, top - 0.35, top, 0.3, "frame", step=1.2)
    for rr in (r_center * 0.55,):
        boxes += arc(rr, 0.0, 360.0, top - 0.25, top, 0.18, "frame", step=0.9)
    glass_rooms = [r["outline"] for r in data["rooms_outside_ring"] if r["kind"] == "greenhouse"]

    def under_glass(x, y):
        return any(in_polygon(x, y, o) for o in glass_rooms)

    def under_roof(x, y):
        return math.hypot(x, y) > r_out - 0.05 and not under_glass(x, y)

    boxes += cells(-half, half, y0, y1, under_roof, H["lab"] + ROOF_T, ROOF_T, "roof")
    boxes += cells(-half, half, y0, y1, under_glass, H["greenhouse"] + GLASS_T, GLASS_T, "glass")

    # --- Tunnel ---
    ux, uy = polar(1.0, tdeg)
    vx, vy = -uy, ux                           # across the tunnel
    foot = math.hypot(*T["to"])                # how far from the center each stair's foot is
    top = math.hypot(*data["stairs"][0]["top"])
    stair_half = data["stairs"][0]["thickness"] * 0.5
    ceiling = -SLAB
    boxes.append(Box(0, 0, tz - 0.15, foot * 2 + 1.0, T["width_m"] + 1.0, 0.3, tdeg, "tunnel_floor"))
    boxes.append(Box(0, 0, tz - 0.14, (chamber_r + 0.5) * 2, (chamber_r + 0.5) * 2, 0.3, 0.0, "tunnel_floor", shape="cylinder"))
    mouth = math.sqrt(chamber_r ** 2 - half_w ** 2)
    gap = math.degrees(math.asin(half_w / chamber_r))
    for a0, a1 in ((tdeg + gap, tdeg + 180.0 - gap), (tdeg + 180.0 + gap, tdeg + 360.0 - gap)):
        boxes += arc(chamber_r + wall_t * 0.5, a0, a1, tz, ceiling, wall_t, "tunnel_wall")
    for end in (-1.0, 1.0):
        for side in (-1.0, 1.0):
            off = half_w + wall_t * 0.5
            p0 = (end * ux * (mouth - 0.3) + side * vx * off, end * uy * (mouth - 0.3) + side * vy * off)
            p1 = (end * ux * foot + side * vx * off, end * uy * foot + side * vy * off)
            boxes.append(seg_box(p0, p1, tz, ceiling, wall_t, "tunnel_wall", extend=wall_t))
            # the stair runs between closer walls, up to the floor
            soff = stair_half + wall_t * 0.5
            q0 = (end * ux * (foot - 0.25) + side * vx * soff, end * uy * (foot - 0.25) + side * vy * soff)
            q1 = (end * ux * (top + 0.25) + side * vx * soff, end * uy * (top + 0.25) + side * vy * soff)
            boxes.append(seg_box(q0, q1, tz, 0.0 - 0.02, wall_t, "tunnel_wall"))
            j0 = (end * ux * foot + side * vx * stair_half, end * uy * foot + side * vy * stair_half)
            j1 = (end * ux * foot + side * vx * (half_w + wall_t), end * uy * foot + side * vy * (half_w + wall_t))
            boxes.append(seg_box(j0, j1, tz, ceiling, wall_t, "tunnel_wall"))
    col = T["root_column"]
    boxes.append(standing(col["center"][0], col["center"][1], tz, ceiling, col["radius"] * 2, col["radius"] * 2, 0.0, "root", shape="cylinder"))

    # --- Stairs and their rails ---
    for s in data["stairs"]:
        b = RL.ramp(s, tz, 0.0)
        b.kind = "ramp"
        boxes.append(b)
        (bx, by), (tx, ty) = s["bottom"], s["top"]
        run = math.hypot(tx - bx, ty - by)
        sx, sy = (tx - bx) / run, (ty - by) / run
        nx, ny = -sy, sx
        ix, iy = tx - sx * STAIR_HOLE, ty - sy * STAIR_HOLE      # the opening's inner end
        off = s["thickness"] * 0.5 + RAIL_T * 0.5
        for side in (-1.0, 1.0):
            boxes.append(seg_box((ix + nx * off * side, iy + ny * off * side), (tx + nx * off * side, ty + ny * off * side), 0.0, RAIL_H, RAIL_T, "rail"))
        boxes.append(seg_box((ix + nx * off, iy + ny * off), (ix - nx * off, iy - ny * off), 0.0, RAIL_H, RAIL_T, "rail", extend=RAIL_T))
    for s in spirals:
        boxes += spiral_ramp(s, 0.0, tz)
        a_top, a_bottom = s["top_deg_ccw_from_east"], s["bottom_deg_ccw_from_east"]
        lo, hi = min(a_top, a_bottom), max(a_top, a_bottom)
        boxes += arc(s["r_outer"] + RAIL_T * 0.5, lo, hi, 0.0, RAIL_H, RAIL_T, "rail", step=1.0)
        boxes += arc(s["r_inner"] - RAIL_T * 0.5, lo, hi, 0.0, RAIL_H, RAIL_T, "rail", step=0.6)
        boxes.append(seg_box(polar(s["r_inner"] - RAIL_T, a_bottom), polar(s["r_outer"] + RAIL_T, a_bottom), 0.0, RAIL_H, RAIL_T, "rail"))

    # --- Cover ---
    plants = []
    rng = random.Random(5)
    step = 0.95

    def jit(k=0.18):
        return rng.uniform(-k, k)

    for i, p in enumerate(data["planters"]):
        if "radius" in p:        # the tree's round planter
            x, y = p["center"]
            boxes.append(standing(x, y, 0.0, H["planter"], p["radius"] * 2, p["radius"] * 2, 0.0, "planter", shape="cylinder"))
            for k in range(12):
                px, py = polar(p["radius"] - rng.choice((0.45, 0.8, 1.2)), 15.0 + k * 30.0)
                plants.append((px, py, H["planter"], rng.choice(("small", "small", "tiny", "mid"))))
        elif p.get("arc"):       # along the ring's inner wall
            rs = [math.hypot(q[0], q[1]) for q in p["outline"]]
            a0, a1 = RL.unwrap_span(p["outline"])
            ra, rb = min(rs), max(rs)
            boxes += arc((ra + rb) * 0.5, a0, a1, 0.0, H["planter"], rb - ra, "planter")
            n = max(2, int(math.radians(a1 - a0) * (ra + rb) * 0.5 / step))
            for k in range(n):
                px, py = polar((ra + rb) * 0.5, a0 + (a1 - a0) * (k + 0.5) / n)
                plants.append((px + jit(), py + jit(), H["planter"], rng.choice(("small", "small", "mid", "tiny"))))
        else:
            x, y = p["center"]
            yaw = p["yaw_deg_ccw_from_east"]
            boxes.append(standing(x, y, 0.0, H["planter"], p["length"], p["thickness"], yaw, "planter"))
            n = max(2, int(p["length"] / step))
            dx, dy = polar(1.0, yaw)
            big = math.hypot(x, y) > r_out      # the rooms' planters carry the tall plants
            for k in range(n):
                t = (k + 0.5) / n - 0.5
                size = rng.choice(("big", "mid", "mid", "small", "tiny")) if big else rng.choice(("small", "small", "tiny"))
                plants.append((x + dx * p["length"] * t + jit(), y + dy * p["length"] * t + jit(), H["planter"], size))
    for c in data["low_cover"]:
        x, y = c["center"]
        boxes.append(standing(x, y, 0.0, H["low_cover"], c["length"], c["thickness"], c["yaw_deg_ccw_from_east"], "cover"))
    for c in data["crates"]:
        x, y = c["center"]
        boxes.append(standing(x, y, 0.0, RL.crate_height(x, y), c["length"], c["thickness"], c["yaw_deg_ccw_from_east"], "crate"))

    # --- Where decoration may stand: on the floor, clear of doors, stairs, spawns and cover ---
    door_list = [(d["center"][0], d["center"][1], d["width"]) for d in data["doorways"]]
    spawn_list = [tuple(p) for team in data["spawns"].values() for p in team]
    cover_rects = [c for c in data["low_cover"] + data["crates"]] + [p for p in data["planters"] if "length" in p]

    def near_door(x, y, extra=1.5):
        return any(math.hypot(x - dx, y - dy) < w * 0.5 + extra for dx, dy, w in door_list)

    def clear(x, y, door_extra=1.5):
        if near_door(x, y, door_extra) or any(math.hypot(x - sx, y - sy) < 2.4 for sx, sy in spawn_list):
            return False
        if any(in_rect(x, y, c, 0.45) for c in cover_rects):
            return False
        return all(on_floor(x + ox, y + oy) for ox, oy in ((0, 0), (0.9, 0), (-0.9, 0), (0, 0.9), (0, -0.9)))

    def cluster(x, y, sizes, z=0.0, spread=0.7):
        for size in sizes:
            px, py = x + rng.uniform(-spread, spread), y + rng.uniform(-spread, spread)
            if clear(px, py):
                plants.append((px, py, z, size))

    def room_edges(room):
        """A room's straight walls as (p0, p1, inward normal), the arc against the ring left out."""
        o = room["outline"]
        out = []
        for i in range(len(o)):
            (ax, ay), (bx, by) = o[i], o[(i + 1) % len(o)]
            if all(abs(math.hypot(qx, qy) - (r_out + wall_t * 0.5)) < 0.05 for qx, qy in ((ax, ay), (bx, by))):
                continue
            ln = math.hypot(bx - ax, by - ay)
            if ln < 0.5:
                continue
            nx, ny = -(by - ay) / ln, (bx - ax) / ln
            mx, my = (ax + bx) * 0.5, (ay + by) * 0.5
            if not in_polygon(mx + nx * 0.4, my + ny * 0.4, o):
                nx, ny = -nx, -ny
            out.append(((ax, ay), (bx, by), (nx, ny)))
        return out

    # --- Flora on the floor: overgrowth along the walls, thick in the greenhouses, thin elsewhere ---
    for room in data["rooms_outside_ring"]:
        if room["kind"] in ("connector", "hall"):
            continue
        green = room["kind"] == "greenhouse"
        for (ax, ay), (bx, by), (nx, ny) in room_edges(room):
            ln = math.hypot(bx - ax, by - ay)
            n = int(ln / (2.0 if green else 4.5))
            for k in range(n):
                t = (k + 0.5) / n
                x, y = ax + (bx - ax) * t + nx * 0.95, ay + (by - ay) * t + ny * 0.95
                if green:
                    cluster(x, y, [rng.choice(("huge", "big", "mid")), "small", "tiny", "tiny"])
                elif rng.random() < 0.7:
                    cluster(x, y, [rng.choice(("mid", "small")), "tiny"])
        if green:      # and loose in the middle of the greenhouses
            xs_ = [q[0] for q in room["outline"]]
            ys_ = [q[1] for q in room["outline"]]
            placed = tries = 0
            while placed < 9 and tries < 200:
                tries += 1
                x, y = rng.uniform(min(xs_), max(xs_)), rng.uniform(min(ys_), max(ys_))
                if in_polygon(x, y, room["outline"]) and clear(x, y, 3.0):
                    cluster(x, y, [rng.choice(("huge", "big")), "mid", "tiny", "tiny"], spread=0.9)
                    placed += 1
    riser_angles = [38, 142, 212, 328]            # single risers on the ring's outer wall
    cluster_pipe_angles = [0, 68, 112, 180, 240, 300]   # the kit's pipe clusters
    for k in range(40):                           # along the ring's outer wall
        a = k * 9.0 + 4.5
        x, y = polar(r_out - 0.95, a)
        if near_door(x, y, 2.2) or any(abs(((a - b + 180) % 360) - 180) < 4.0 for b in riser_angles + cluster_pipe_angles):
            continue
        cluster(x, y, ["small", "tiny"] if k % 2 else ["tiny", "tiny", "mid"], spread=0.5)
    for k in range(10):                           # the root chamber's edge, clear of the tunnel mouths and stair feet
        a = k * 36.0 + 10.0
        if any(abs(((a - b + 180) % 360) - 180) < 28.0 for b in (tdeg, tdeg + 180.0, spirals[0]["bottom_deg_ccw_from_east"], spirals[1]["bottom_deg_ccw_from_east"])):
            continue
        x, y = polar(chamber_r - 0.8, a)
        plants.append((x, y, tz, rng.choice(("small", "tiny"))))
    litter = 0
    while litter < 90:                            # fallen leaves
        x, y = rng.uniform(-half, half), rng.uniform(y0, y1)
        r = math.hypot(x, y)
        inside = r_in + 0.6 < r < r_out - 0.6 or r < r_center - 0.6 or any(in_polygon(x, y, rm["outline"]) for rm in data["rooms_outside_ring"])
        if inside and on_floor(x, y) and not any(in_rect(x, y, c, 0.2) for c in cover_rects):
            plants.append((x, y, 0.03, "leaf"))
            litter += 1

    # --- Pipes: along the ceilings and walls, down the walls, through the tunnel ---
    props = []      # (kit mesh, x, y, base z, (length, thickness, height), yaw)

    def pipe(p0, p1, z, dia, kind):
        (ax, ay), (bx, by) = p0, p1
        ln = math.hypot(bx - ax, by - ay)
        if ln > 0.05:
            boxes.append(Box((ax + bx) * 0.5, (ay + by) * 0.5, z, dia, dia, ln + 0.04, math.degrees(math.atan2(by - ay, bx - ax)), kind, pitch=90.0, shape="cylinder"))

    def pipe_arc(r, a0, a1, z, dia, kind, step=1.6, clamps=True):
        n = max(1, int(math.ceil(math.radians(abs(a1 - a0)) * r / step)))
        for i in range(n):
            pipe(polar(r, a0 + (a1 - a0) * i / n), polar(r, a0 + (a1 - a0) * (i + 1) / n), z, dia, kind)
            if clamps and i % 3 == 0:        # a clamp to the wall every few pieces
                am = a0 + (a1 - a0) * (i + 0.5) / n
                cx, cy = polar(r, am)
                boxes.append(Box(cx, cy, z, dia + 0.1, dia + 0.1, 0.1, am + 90.0, "pipe_dark", pitch=90.0, shape="cylinder"))

    def riser(x, y, z0, z1, dia, kind):
        boxes.append(standing(x, y, z0, z1, dia, dia, 0.0, kind, shape="cylinder"))

    ring_top = H["ring_corridor"]
    pipe_arc(r_out - 0.55, 0.0, 360.0, ring_top - 0.5, 0.3, "pipe")           # the main run, high on the outer wall
    pipe_arc(r_out - 0.95, 0.0, 360.0, ring_top - 0.38, 0.16, "pipe_rust")
    pipe_arc(r_out - 0.42, 0.0, 360.0, ring_top - 0.95, 0.11, "pipe_dark", clamps=False)
    pipe_arc(r_in + 0.6, 0.0, 360.0, ring_top - 0.6, 0.42, "pipe_rust")       # a fat duct over the inner planters
    for a in riser_angles:
        x, y = polar(r_out - 0.55, a)
        riser(x, y, 0.0, ring_top - 0.5, 0.3, "pipe")
        x, y = polar(r_out - 0.45, a + 1.6)
        riser(x, y, 0.0, ring_top - 0.38, 0.14, "pipe_rust")
    for a in cluster_pipe_angles:
        x, y = polar(r_out - wall_t * 0.5 - 0.5, a)
        props.append(("Column_Pipes", x, y, 0.0, (0.87, 0.87, ring_top), a))
    for a in (0, 90, 180, 270):               # the connectors' ceilings
        for side in (-1.0, 1.0):
            ox, oy = polar(R["connector_width"] * 0.5 - 0.4, a + 90.0 * side)
            (sx, sy), (ex, ey) = polar(r_center + 0.3, a), polar(r_in - 0.3, a)
            pipe((sx + ox, sy + oy), (ex + ox, ey + oy), H["connector"] - 0.45, 0.24 if side > 0 else 0.14, "pipe" if side > 0 else "pipe_rust")
    for room in data["rooms_outside_ring"]:   # round each room, above the doors
        for (ax, ay), (bx, by), (nx, ny) in room_edges(room):
            ln = math.hypot(bx - ax, by - ay)
            if ln < 3.0:
                continue
            tx_, ty_ = (bx - ax) / ln, (by - ay) / ln
            for off, z, dia, kind in ((0.42, 4.5, 0.22, "pipe"), (0.36, 4.12, 0.12, "pipe_rust")):
                pipe((ax + nx * off + tx_ * 0.4, ay + ny * off + ty_ * 0.4), (bx + nx * off - tx_ * 0.4, by + ny * off - ty_ * 0.4), z, dia, kind)
    for end in (-1.0, 1.0):                   # the tunnel: two runs under the ceiling on one side
        for off, z, dia, kind in ((half_w - 0.35, ceiling - 0.4, 0.24, "pipe"), (half_w - 0.3, ceiling - 0.8, 0.13, "pipe_rust")):
            p0 = (end * ux * mouth + vx * off, end * uy * mouth + vy * off)
            p1 = (end * ux * (foot - 0.3) + vx * off, end * uy * (foot - 0.3) + vy * off)
            pipe(p0, p1, z, dia, kind)

    # --- Lights (x, y, z, where) ---
    lights = []
    rm = (r_in + r_out) * 0.5
    for k in range(16):
        x, y = polar(rm, 11.25 + k * 22.5)
        lights.append((x, y, H["ring_corridor"] - 0.5, "ring"))
    for a in (0, 90, 180, 270):
        x, y = polar((r_center + r_in) * 0.5, a)
        lights.append((x, y, H["connector"] - 0.5, "connector"))
    for a in (45, 135, 225, 315):
        x, y = polar(r_center - 2.0, a)
        lights.append((x, y, H["center_room"] - 2.5, "center"))
    for r in data["rooms_outside_ring"]:
        cx, cy = room_centroid(r["outline"])
        where = "greenhouse" if r["kind"] == "greenhouse" else "room"
        if r["kind"] in ("connector", "hall"):
            lights.append((cx, cy, r["height"] - 0.6, where))
            continue
        # Two lights either side of the middle, across the line to the map's center, so no wall or
        # planter face is lit only from straight above.
        lights.append((cx, cy, 2.2, "fill"))
        d = math.hypot(cx, cy)
        px, py = -cy / d, cx / d
        for side in (-1.0, 1.0):
            lights.append((cx + px * 4.5 * side, cy + py * 4.5 * side, r["height"] - 0.6, where))
    for end in (-1.0, 1.0):
        d = mouth + 4.0
        while d < foot:
            lights.append((end * ux * d, end * uy * d, ceiling - 0.4, "tunnel"))
            d += 8.0
    for a in (tdeg + 90.0, tdeg - 90.0):
        x, y = polar(chamber_r - 2.2, a)
        lights.append((x, y, ceiling - 0.4, "tunnel"))

    # --- Light fixtures: every light hangs in a housing with a glowing panel (the fill lights stand on poles) ---
    ceilings = {"ring": H["ring_corridor"], "connector": H["connector"], "center": H["center_room"], "room": H["lab"],
                "greenhouse": H["greenhouse"], "tunnel": ceiling}
    for x, y, z, where in lights:
        if where == "fill":
            boxes.append(standing(x, y, 0.0, z - 0.1, 0.09, 0.09, 0.0, "fixture", shape="cylinder"))
            boxes.append(standing(x, y, 0.0, 0.06, 0.6, 0.6, 0.0, "fixture", shape="cylinder"))
            boxes.append(Box(x, y, z + 0.05, 0.5, 0.5, 0.3, 45.0, "fixture"))
            boxes.append(Box(x, y, z - 0.12, 0.4, 0.4, 0.05, 45.0, "lamp"))
            continue
        yaw = tdeg if where == "tunnel" else (math.degrees(math.atan2(y, x)) + 90.0)
        length, width = (0.7, 0.3) if where == "tunnel" else (1.5, 0.42)
        boxes.append(Box(x, y, z + 0.2, length, width, 0.14, yaw, "fixture"))
        boxes.append(Box(x, y, z + 0.115, length - 0.16, width - 0.14, 0.04, yaw, "lamp_tunnel" if where == "tunnel" else "lamp"))
        top = ceilings[where]
        if top - (z + 0.27) > 0.12:           # hangs on a rod
            boxes.append(standing(x, y, z + 0.27, top, 0.06, 0.06, 0.0, "fixture", shape="cylinder"))

    return {"boxes": boxes, "spawns": data["spawns"], "plants": plants, "lights": lights, "props": props, "data": data,
            "bounds": (-half, half, y0, y1), "tunnel_z": tz}


def check():
    """Run without Unreal: the floor is whole except over the stairs, and the layout mirrors."""
    out = build()
    boxes, data = out["boxes"], out["data"]
    kinds = {}
    for b in boxes:
        kinds[b.kind] = kinds.get(b.kind, 0) + 1
    print("boxes %d: %s" % (len(boxes), ", ".join("%s %d" % kv for kv in sorted(kinds.items()))))
    floor = [b for b in boxes if b.kind == "floor"]

    def covered(x, y):
        return any(abs(b.x - x) <= b.length * 0.5 and abs(b.y - y) <= b.thickness * 0.5 for b in floor)

    r_out = data["radii"]["ring_outer"]
    gaps = 0
    for i in range(0, int(r_out * 4)):
        r = i * 0.25
        m = max(8, int(2 * math.pi * r / 0.3))
        for j in range(m):
            x, y = polar(r, 360.0 * j / m)
            in_spiral = any(in_arc(x, y, s["r_inner"], s["r_outer"], s["top_deg_ccw_from_east"], s["bottom_deg_ccw_from_east"], 0.55) for s in data["spiral_stairs"])
            if not in_spiral and not covered(x, y):
                gaps += 1
    print("floor gaps inside the ring away from the spiral stairs: %d" % gaps)
    # Mirror east/west: every kind but the tunnel, the stairs and the flat cells.
    skip = ("tunnel_floor", "tunnel_wall", "ramp", "rail", "root", "floor", "roof", "glass", "pipe", "pipe_rust", "pipe_dark",
            "fixture", "lamp", "lamp_tunnel")
    keys = set()
    for b in boxes:
        if b.kind not in skip:
            # Walls mirror in plan only: the greenhouses (west) are taller than the labs (east).
            wall = b.kind.startswith("wall")
            keys.add(("wall" if wall else b.kind, round(b.x, 2), round(b.y, 2), 0 if wall else round(b.z, 2), round(b.length, 2), round(b.thickness, 2)))
    lone = [k for k in keys if (k[0], round(-k[1], 2), k[2], k[3], k[4], k[5]) not in keys]
    print("pieces without an east/west mirror: %d" % len(lone), lone[:6])
    sizes = {}
    for p in out["plants"]:
        sizes[p[3]] = sizes.get(p[3], 0) + 1
    print("plants %d (%s), lights %d, kit props %d, spawns %d + %d" % (len(out["plants"]), ", ".join("%s %d" % kv for kv in sorted(sizes.items())),
          len(out["lights"]), len(out["props"]), len(out["spawns"]["blue"]), len(out["spawns"]["orange"])))
    return gaps == 0 and not lone


if __name__ == "__main__":
    raise SystemExit(0 if check() else 1)
