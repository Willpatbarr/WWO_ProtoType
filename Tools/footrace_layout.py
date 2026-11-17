"""The FootRace test map (L_FootRaceTest) as boxes and marker points: layout only, gray box.

FootRace: two teams race through their own lane of PvE rooms, three doors each, to a boss; a hub
between the lanes is where the PvP happens. This is the source of truth for the test map's shape;
Tools/build_maps.py (WWO_MAPS=footrace) turns it into a level. Plain Python (no Unreal), so it can be
checked outside the editor: `python3 Tools/footrace_layout.py` prints a summary and runs the checks.

Units are mockup meters, as ring_layout.py and cascade_layout.py: x east, y north, z up, origin at
the hub's center. A box: center (x, y, z), size (length along x, thickness along y, height), kind.

- Lanes run along x, west to east: spawn room, Room1, Room2, Room3, boss room. Lane 0 (team 0) is
  north of the hub, lane 1 south. Each room shares a wall with the next; the shared wall has a
  doorway in the middle. Spawn to Room1 is always open; Door1..Door3 (Room1 to Room2, Room2 to
  Room3, Room3 to the boss) are where the game's doors go.
- The hub is a closed room between the lanes. Nothing walks between a lane and the hub: the breach
  portals teleport (the game spawns them; this file only marks where).
- No roofs: walls are too tall to jump or mantle over, and the sun lights the floors.
- Markers are TargetPoints with tags; the game spawns the gameplay actors on them:
    EnemySpawn PvEOnly Lane<n> Room<k>   room enemies (PvEOnly keeps them out of PvP respawns)
    TeamSpawn Lane<n>                    a team's spawn spots
    PortalAnchor Lane<n> Room<k>|Boss    where a room's breach portal stands (on the hub side)
    DoorSlot Lane<n> Door<d>             the doorway a door closes, facing east (into the lane)
    HubPortal Lane<n>                    the hub end of a team's portal
    LeverSlot Lane<n> Door<d>            a team's lever for each door, in the hub
    BossSpawn Lane<n>                    the boss
    CoreSpawn                            where boss cores appear, mid-hub
  One PlayerStart, in lane 0's spawn room (the game mode's ring of spots goes around it).
  A marker's yaw is the way it faces, in degrees counter-clockwise from east (Unreal yaw 90 - yaw).
"""

WALL_H = 8.0        # too tall to jump or mantle (the mantle reaches about 2 m)
WALL_T = 0.5
FLOOR_T = 0.4       # floor top at z 0
DOOR_W = 4.0
DOOR_H = 4.0
LANE_Y = (45.0, -45.0)   # lane 0 north, lane 1 south

# (name, length along x, width along y); rooms touch end to end.
LANE_ROOMS = (
    ("Spawn", 14.0, 14.0),
    ("Room1", 26.0, 22.0),
    ("Room2", 26.0, 22.0),
    ("Room3", 26.0, 22.0),
    ("Boss", 32.0, 30.0),
)
HUB = (32.0, 32.0)        # length along x, width along y, centered on the origin
ENEMY_SPAWNS_PER_ROOM = 6
TEAM_SPAWNS = 6


class Box:
    __slots__ = ("x", "y", "z", "length", "thickness", "height", "kind")

    def __init__(self, x, y, z, length, thickness, height, kind):
        self.x, self.y, self.z = x, y, z
        self.length, self.thickness, self.height = length, thickness, height
        self.kind = kind

    def to_dict(self):
        return {k: getattr(self, k) for k in self.__slots__}


class Marker:
    __slots__ = ("x", "y", "z", "yaw", "tags")

    def __init__(self, x, y, z, yaw, tags):
        self.x, self.y, self.z, self.yaw, self.tags = x, y, z, yaw, list(tags)

    def to_dict(self):
        return {k: getattr(self, k) for k in self.__slots__}


def wall_x(x, y0, y1, gap_at=None):
    """A wall across the lane at x, from y0 to y1, with a doorway centered on gap_at."""
    lo, hi = min(y0, y1) - WALL_T * 0.5, max(y0, y1) + WALL_T * 0.5
    out = []
    if gap_at is None:
        out.append(Box(x, (lo + hi) * 0.5, WALL_H * 0.5, WALL_T, hi - lo, WALL_H, "wall"))
        return out
    g0, g1 = gap_at - DOOR_W * 0.5, gap_at + DOOR_W * 0.5
    out.append(Box(x, (lo + g0) * 0.5, WALL_H * 0.5, WALL_T, g0 - lo, WALL_H, "wall"))
    out.append(Box(x, (g1 + hi) * 0.5, WALL_H * 0.5, WALL_T, hi - g1, WALL_H, "wall"))
    out.append(Box(x, gap_at, (DOOR_H + WALL_H) * 0.5, WALL_T, DOOR_W, WALL_H - DOOR_H, "wall"))  # lintel
    return out


def wall_y(y, x0, x1):
    """A wall along the lane at y, from x0 to x1 (ends reach the cross walls' outer faces)."""
    lo, hi = min(x0, x1) - WALL_T * 0.5, max(x0, x1) + WALL_T * 0.5
    return [Box((lo + hi) * 0.5, y, WALL_H * 0.5, hi - lo, WALL_T, WALL_H, "wall")]


def floor(x0, x1, y0, y1, kind):
    return Box((x0 + x1) * 0.5, (y0 + y1) * 0.5, -FLOOR_T * 0.5, abs(x1 - x0), abs(y1 - y0), FLOOR_T, kind)


def lane_rooms():
    """Each room's (name, x0, x1, width), west to east, centered on x 0."""
    total = sum(r[1] for r in LANE_ROOMS)
    x = -total * 0.5
    out = []
    for name, length, width in LANE_ROOMS:
        out.append((name, x, x + length, width))
        x += length
    return out


def grid(x0, x1, y0, y1, n, margin):
    """n points spread over a rectangle in two rows."""
    cols = (n + 1) // 2
    pts = []
    for i in range(n):
        r, c = divmod(i, cols)
        fx = (c + 0.5) / cols
        fy = 0.25 if r == 0 else 0.75
        pts.append((x0 + margin + (x1 - x0 - 2 * margin) * fx, y0 + margin + (y1 - y0 - 2 * margin) * fy))
    return pts


def build():
    boxes, markers = [], []
    rooms = lane_rooms()
    for lane, cy in enumerate(LANE_Y):
        lane_tag = "Lane%d" % lane
        hub_side = -1.0 if cy > 0 else 1.0  # direction from the lane toward the hub, in y
        # Floors.
        for name, x0, x1, width in rooms:
            kind = {"Spawn": "spawn%d" % lane, "Boss": "boss_floor"}.get(name, "floor")
            boxes.append(floor(x0, x1, cy - width * 0.5, cy + width * 0.5, kind))
        # Side walls, one per room per side.
        for name, x0, x1, width in rooms:
            for side in (-1.0, 1.0):
                boxes += wall_y(cy + side * width * 0.5, x0, x1)
        # Cross walls: the two ends, and each shared wall with its doorway, as wide as the wider room.
        first, last = rooms[0], rooms[-1]
        boxes += wall_x(first[1], cy - first[3] * 0.5, cy + first[3] * 0.5)
        boxes += wall_x(last[2], cy - last[3] * 0.5, cy + last[3] * 0.5)
        for i in range(len(rooms) - 1):
            a, b = rooms[i], rooms[i + 1]
            half = max(a[3], b[3]) * 0.5
            boxes += wall_x(a[2], cy - half, cy + half, gap_at=cy)
            if i >= 1:  # Room1 -> Room2 is Door1 ... Room3 -> Boss is Door3
                markers.append(Marker(a[2], cy, 0.0, 0.0, ["DoorSlot", lane_tag, "Door%d" % i]))
        # Room markers.
        for name, x0, x1, width in rooms:
            y0, y1 = cy - width * 0.5, cy + width * 0.5
            mid = (x0 + x1) * 0.5
            if name == "Spawn":
                for i, (px, py) in enumerate(grid(x0, x1, y0, y1, TEAM_SPAWNS, 2.5)):
                    markers.append(Marker(px, py, 0.1, 0.0, ["TeamSpawn", lane_tag]))
                if lane == 0:
                    markers.append(Marker(mid, cy, 0.1, 0.0, ["PlayerStart"]))
                continue
            room_tag = "Boss" if name == "Boss" else name
            # The portal stands against the wall nearest the hub, facing into the room.
            inner = cy + hub_side * (width * 0.5 - 2.0)
            markers.append(Marker(mid, inner, 0.1, 90.0 if hub_side < 0 else -90.0, ["PortalAnchor", lane_tag, room_tag]))
            if name == "Boss":
                markers.append(Marker(x1 - 8.0, cy, 0.1, 180.0, ["BossSpawn", lane_tag]))
                continue
            # Enemies on the east two thirds of the room, away from the doorway they enter by.
            for px, py in grid(x0 + (x1 - x0) / 3.0, x1, y0, y1, ENEMY_SPAWNS_PER_ROOM, 2.5):
                markers.append(Marker(px, py, 0.1, 180.0, ["EnemySpawn", "PvEOnly", lane_tag, name]))
    # The hub: a closed room, its portals against the lane-side walls, levers along each team's side.
    hl, hw = HUB
    boxes.append(floor(-hl * 0.5, hl * 0.5, -hw * 0.5, hw * 0.5, "hub_floor"))
    boxes += wall_x(-hl * 0.5, -hw * 0.5, hw * 0.5)
    boxes += wall_x(hl * 0.5, -hw * 0.5, hw * 0.5)
    boxes += wall_y(-hw * 0.5, -hl * 0.5, hl * 0.5)
    boxes += wall_y(hw * 0.5, -hl * 0.5, hl * 0.5)
    for lane, cy in enumerate(LANE_Y):
        side = 1.0 if cy > 0 else -1.0
        markers.append(Marker(0.0, side * (hw * 0.5 - 2.0), 0.1, -90.0 * side, ["HubPortal", "Lane%d" % lane]))
        for d, x in enumerate((-8.0, 0.0, 8.0)):
            markers.append(Marker(x, side * (hw * 0.5 - 6.0), 0.1, -90.0 * side, ["LeverSlot", "Lane%d" % lane, "Door%d" % (d + 1)]))
    markers.append(Marker(0.0, 0.0, 0.1, 0.0, ["CoreSpawn"]))
    return {"boxes": boxes, "markers": markers}


def bounds():
    """(x0, x1, y0, y1) around everything, for the nav bounds and the safety floor."""
    rooms = lane_rooms()
    xs = [rooms[0][1], rooms[-1][2], -HUB[0] * 0.5, HUB[0] * 0.5]
    widest = max(r[3] for r in rooms)
    ys = [LANE_Y[0] + widest * 0.5, LANE_Y[1] - widest * 0.5]
    return min(xs) - 2.0, max(xs) + 2.0, min(ys) - 2.0, max(ys) + 2.0


def check(layout):
    """Layout checks that need no Unreal. Returns a list of problems (empty when fine)."""
    problems = []
    rooms = lane_rooms()
    m = layout["markers"]
    tags = [tuple(x.tags) for x in m]
    for lane in (0, 1):
        lt = "Lane%d" % lane
        for d in (1, 2, 3):
            for kind in ("DoorSlot", "LeverSlot"):
                n = sum(1 for t in tags if t[0] == kind and lt in t and "Door%d" % d in t)
                if n != 1:
                    problems.append("%s %s Door%d: %d markers" % (kind, lt, d, n))
        for room in ("Room1", "Room2", "Room3", "Boss"):
            if sum(1 for t in tags if t[0] == "PortalAnchor" and lt in t and room in t) != 1:
                problems.append("PortalAnchor %s %s missing or doubled" % (lt, room))
        for room in ("Room1", "Room2", "Room3"):
            n = sum(1 for t in tags if t[0] == "EnemySpawn" and lt in t and room in t)
            if n != ENEMY_SPAWNS_PER_ROOM:
                problems.append("EnemySpawn %s %s: %d" % (lt, room, n))
        for kind, want in (("TeamSpawn", TEAM_SPAWNS), ("HubPortal", 1), ("BossSpawn", 1)):
            n = sum(1 for t in tags if t[0] == kind and lt in t)
            if n != want:
                problems.append("%s %s: %d, want %d" % (kind, lt, n, want))
    if sum(1 for t in tags if t[0] == "PlayerStart") != 1:
        problems.append("want exactly one PlayerStart")
    if sum(1 for t in tags if t[0] == "CoreSpawn") != 1:
        problems.append("want exactly one CoreSpawn")

    # Every marker (but door slots, which sit in a wall) stands inside a room or the hub, clear of walls.
    def inside(x, y, x0, x1, y0, y1, margin):
        return x0 + margin <= x <= x1 - margin and y0 + margin <= y <= y1 - margin
    areas = [(-HUB[0] * 0.5, HUB[0] * 0.5, -HUB[1] * 0.5, HUB[1] * 0.5)]
    for cy in LANE_Y:
        for _, x0, x1, w in rooms:
            areas.append((x0, x1, cy - w * 0.5, cy + w * 0.5))
    for k in m:
        if k.tags[0] == "DoorSlot":
            continue
        if not any(inside(k.x, k.y, *a, margin=1.0) for a in areas):
            problems.append("%s at (%.1f, %.1f) is not inside a room" % (" ".join(k.tags), k.x, k.y))
    # The lanes and the hub never touch: no walking between them.
    widest = max(r[3] for r in rooms)
    for cy in LANE_Y:
        gap = abs(cy) - widest * 0.5 - HUB[1] * 0.5
        if gap < 4.0:
            problems.append("lane at y %.0f is %.1f m from the hub" % (cy, gap))
    return problems


if __name__ == "__main__":
    import collections
    layout = build()
    kinds = collections.Counter(b.kind for b in layout["boxes"])
    marks = collections.Counter(k.tags[0] for k in layout["markers"])
    print("boxes:", dict(kinds))
    print("markers:", dict(marks))
    print("bounds:", bounds())
    problems = check(layout)
    for p in problems:
        print("PROBLEM:", p)
    print("ok" if not problems else "%d problems" % len(problems))
    raise SystemExit(1 if problems else 0)
