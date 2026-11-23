"""The FootRace test map (L_FootRaceTest) as boxes and marker points: layout only, gray box.

FootRace: two teams race through their own lane of PvE rooms, five doors each, to a boss; a hub
between the lanes is where the PvP happens. This is the source of truth for the test map's shape;
Tools/build_maps.py (WWO_MAPS=footrace) turns it into a level. Plain Python (no Unreal), so it can be
checked outside the editor: `python3 Tools/footrace_layout.py` prints a summary and runs the checks.

Units are mockup meters, as ring_layout.py and cascade_layout.py: x east, y north, z up, origin at
the hub's center. A box: center (x, y, z), size (length along its yaw, thickness, height), kind, yaw
in degrees counter-clockwise from east (0: length along x), pitch (up toward its length).

- Lanes run along x, west to east: spawn room, Room1..Room5, boss room. Lane 0 (team 0) is north of
  the hub, lane 1 south. Each room shares a wall with the next; the shared wall has a doorway in the
  middle. Spawn to Room1 is always open; Door1..Door5 (Room1 to Room2 ... Room5 to the boss) are
  where the game's doors go. ROOMS is the count of lane rooms (and doors); the game's
  FWWOFootRaceRules::DoorCount must match it.
- Lane rooms are long rectangles (76 x 40 m), the boss room 90 x 60, the spawn room 24 x 40.
- The hub is a closed room between the lanes. Nothing walks between a lane and the hub: the breach
  portals teleport (the game spawns them; this file only marks where).
- No roofs: walls are too tall to jump or mantle over, and the sun lights the floors.
- Enemies come out of the walls: a lane room's EnemySpawn markers stand MARKER_IN from the side walls
  and the far (east) wall, most of them in the far half (toward the next door), none within
  NEAR_DOOR_CLEAR of the doorway the team comes in by; the boss room's go round all four walls. Each
  has a spawn_gate box in the wall face behind it (dark, no collision, out of the navmesh: the art pass
  dresses them as cave mouths and grates).
- Cover: every lane room and the boss room has solid obstacles (ruin_wall, boulder, stump; tagged
  Cover in the level, which is how the Soldiers find cover), from a seeded generator in room-local
  coordinates, the same in both lanes (mirrored) so the race is fair. A lane room's stand in three
  broken bands across the room, each with a gap against one side wall (alternating, so the way
  through snakes and no sightline runs down the room), a plug behind the last gap, and a stump; the
  boss room's are scattered between its doorway and the platform. check() holds them to the rules
  (sightlines, clear doorways and portals, a PATH_W wide path, clear of the spawns).
- The boss stands on a raised platform (boss_platform) with a wide ramp facing the doorway and
  narrower ones on its two sides (ramp).
- Markers are TargetPoints with tags; the game spawns the gameplay actors on them:
    EnemySpawn PvEOnly Lane<n> Room<k>|Boss  room enemies, and the boss room's core waves
                                         (PvEOnly keeps them out of PvP respawns); facing into the room
    TeamSpawn Lane<n>                    a team's spawn spots
    PortalAnchor Lane<n> Room<k>|Boss    where a room's breach portal stands (on the hub side)
    DoorSlot Lane<n> Door<d>             the doorway a door closes, facing east (into the lane)
    HubPortal Lane<n>                    the hub end of a team's portal
    LeverSlot Lane<n> Door<d>            a team's lever for each door, in the hub
    BossSpawn Lane<n>                    the boss, on its platform
    CoreSpawn                            where boss cores appear, mid-hub
    RoomArea Lane<n> Spawn|Room<k>|Boss  a room's floor area: the TargetPoint's scale is its size in m
                                         (Unreal X = across the lane, Y = along it, Z = wall height)
    HubArea                              the hub's floor area, the same way
  One PlayerStart, in lane 0's spawn room (the game mode's ring of spots goes around it).
  A marker's yaw is the way it faces, in degrees counter-clockwise from east (Unreal yaw 90 - yaw).
"""
import math
import random

WALL_H = 12.0       # too tall to jump or mantle (the mantle reaches about 2 m); the art pass builds higher
WALL_T = 0.5
FLOOR_T = 0.4       # floor top at z 0
DOOR_W = 4.0
DOOR_H = 4.0
ROOMS = 5           # lane rooms between the spawn room and the boss room, and doors (Door1..Door5)

# (name, length along x, width along y); rooms touch end to end.
LANE_ROOMS = (("Spawn", 24.0, 40.0),) + tuple(("Room%d" % k, 76.0, 40.0) for k in range(1, ROOMS + 1)) + (("Boss", 90.0, 60.0),)
HUB = (60.0, 48.0)                   # length along x, width along y, centered on the origin
LANE_GAP = 8.0                       # solid nothing between the hub and the widest lane room
_LANE = HUB[1] * 0.5 + LANE_GAP + max(r[2] for r in LANE_ROOMS) * 0.5
LANE_Y = (_LANE, -_LANE)             # lane 0 north, lane 1 south
TEAM_SPAWNS = 6
LEVER_X = (-20.0, -10.0, 0.0, 10.0, 20.0)   # Door1..Door5's levers along each team's side of the hub

MARKER_IN = 1.5                      # an enemy spawn stands this far in from its wall's face
NEAR_DOOR_CLEAR = 15.0               # no lane-room spawn this near the doorway the team comes in by
GATE_W, GATE_H, GATE_T = 3.0, 3.5, 0.3
EYE = 1.7
MAX_SIGHT = 35.0                     # no sightline longer than this down a lane room (check())
DOOR_CLEAR = 6.0                     # cover keeps this far from doorways and portal anchors
SPAWN_CLEAR = 4.0                    # and this far from spawn markers, their gates and the boss platform
PATH_W = 4.0                         # a clear walking path this wide from door to door
PLATFORM = 16.0                      # the boss platform, square
PLATFORM_H = 1.5
PLATFORM_BACK = 16.0                 # its center this far from the boss room's east wall
RAMP_RUN = 6.0

OBSTACLES = ("ruin_wall", "boulder", "stump")
BLOCKS_SIGHT = ("ruin_wall", "boulder")   # taller than the eye; stumps are low cover


class Box:
    __slots__ = ("x", "y", "z", "length", "thickness", "height", "kind", "yaw", "pitch")

    def __init__(self, x, y, z, length, thickness, height, kind, yaw=0.0, pitch=0.0):
        self.x, self.y, self.z = x, y, z
        self.length, self.thickness, self.height = length, thickness, height
        self.kind = kind
        self.yaw, self.pitch = yaw, pitch

    def to_dict(self):
        return {k: getattr(self, k) for k in self.__slots__}


class Marker:
    __slots__ = ("x", "y", "z", "yaw", "tags", "size")

    def __init__(self, x, y, z, yaw, tags, size=None):
        self.x, self.y, self.z, self.yaw, self.tags = x, y, z, yaw, list(tags)
        self.size = size  # an area: (length along x, width along y, height), m

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


def room_names():
    return ["Room%d" % k for k in range(1, ROOMS + 1)] + ["Boss"]


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


# --- Plane geometry on box footprints (oriented rectangles) -----------------------------------------

def corners(b):
    c, s = math.cos(math.radians(b.yaw)), math.sin(math.radians(b.yaw))
    hl, ht = b.length * 0.5, b.thickness * 0.5
    return [(b.x + c * a - s * t, b.y + s * a + c * t) for a, t in ((hl, ht), (-hl, ht), (-hl, -ht), (hl, -ht))]


def to_local(b, px, py):
    c, s = math.cos(math.radians(b.yaw)), math.sin(math.radians(b.yaw))
    dx, dy = px - b.x, py - b.y
    return dx * c + dy * s, -dx * s + dy * c


def point_dist(b, px, py):
    """Distance from a point to a box's footprint (0 inside)."""
    a, t = to_local(b, px, py)
    return math.hypot(max(abs(a) - b.length * 0.5, 0.0), max(abs(t) - b.thickness * 0.5, 0.0))


def ray_hit(b, ox, oy, dx, dy):
    """How far along a ray (unit direction) it first meets a box's footprint: 0 from inside, None if never."""
    a0, t0 = to_local(b, ox, oy)
    c, s = math.cos(math.radians(b.yaw)), math.sin(math.radians(b.yaw))
    da, dt = dx * c + dy * s, -dx * s + dy * c
    lo, hi = 0.0, float("inf")
    for p, d, h in ((a0, da, b.length * 0.5), (t0, dt, b.thickness * 0.5)):
        if abs(d) < 1e-9:
            if abs(p) > h:
                return None
            continue
        e0, e1 = (-h - p) / d, (h - p) / d
        if e0 > e1:
            e0, e1 = e1, e0
        lo, hi = max(lo, e0), min(hi, e1)
        if lo > hi:
            return None
    return lo


def _seg_dist(px, py, ax, ay, bx, by):
    vx, vy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * vx + (py - ay) * vy) / max(vx * vx + vy * vy, 1e-12)))
    return math.hypot(px - ax - vx * t, py - ay - vy * t)


def _crosses(p1, p2, p3, p4):
    def side(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    return side(p3, p4, p1) * side(p3, p4, p2) < 0 and side(p1, p2, p3) * side(p1, p2, p4) < 0


def box_dist(a, b):
    """Distance between two footprints (0 if they touch or overlap)."""
    ca, cb = corners(a), corners(b)
    if any(point_dist(b, *p) == 0.0 for p in ca) or any(point_dist(a, *p) == 0.0 for p in cb):
        return 0.0
    for i in range(4):
        for j in range(4):
            if _crosses(ca[i], ca[(i + 1) % 4], cb[j], cb[(j + 1) % 4]):
                return 0.0
    best = float("inf")
    for pts, other in ((ca, cb), (cb, ca)):
        for p in pts:
            for i in range(4):
                best = min(best, _seg_dist(p[0], p[1], *other[i], *other[(i + 1) % 4]))
    return best


def _span_v(b):
    """How much of the room's width (v) a footprint covers."""
    vs = [p[1] for p in corners(b)]
    return max(vs) - min(vs)


# --- A room in its own coordinates: u along it from the doorway in, v across, negative toward the hub ---

class Room:
    """What a lane room or the boss room holds, in its own coordinates; build() places it in each lane."""

    def __init__(self, name, length, width):
        self.name, self.length, self.width = name, length, width
        self.boss = name == "Boss"
        self.spawns = room_spawns(name, length, width)  # (u, v, facing yaw)
        self.platform = boss_platform(length) if self.boss else []
        self.obstacles = []

    def face(self):
        """The wall faces: (u0, u1, v0, v1)."""
        h = WALL_T * 0.5
        return h, self.length - h, -self.width * 0.5 + h, self.width * 0.5 - h

    def doorways(self):
        return [(0.0, 0.0)] if self.boss else [(0.0, 0.0), (self.length, 0.0)]

    def portal(self):
        return self.length * 0.5, -(self.width * 0.5 - 2.0)

    def gates(self):
        return [spawn_gate(self, u, v, yaw) for u, v, yaw in self.spawns]


def room_spawns(name, length, width):
    """The room's EnemySpawn markers (u, v, facing yaw), each MARKER_IN from a wall face, facing into the room."""
    h = WALL_T * 0.5
    far_v, hub_v = width * 0.5 - h - MARKER_IN, -width * 0.5 + h + MARKER_IN
    east_u, west_u = length - h - MARKER_IN, h + MARKER_IN
    pts = []
    if name == "Boss":
        # All four walls: the doorway wall either side of the doorway, the far wall, both sides.
        pts += [(west_u, v, 0.0) for v in (-18.0, 18.0)]
        pts += [(east_u, v, 180.0) for v in (-24.0, -14.0, 14.0, 24.0)]
        for u in (14.0, 30.0, 60.0, 82.0):
            pts += [(u, far_v, -90.0), (u, hub_v, 90.0)]
        return pts
    # A lane room: 8 of 10 in the far half (the far wall, the side walls' far halves), 2 on the side
    # walls of the near half, well clear of the doorway in (and of the cover bands either side).
    pts += [(east_u, v, 180.0) for v in (-15.0, -7.0, 7.0, 15.0)]
    for u in (43.0, 72.0):
        pts += [(u, far_v, -90.0), (u, hub_v, 90.0)]
    pts += [(20.0, far_v, -90.0), (26.0, hub_v, 90.0)]
    return pts


def spawn_gate(room, u, v, yaw):
    """The dark opening in the wall face behind a spawn marker: 5 cm proud of the face, the rest in the wall."""
    u0, u1, v0, v1 = room.face()
    d = GATE_T * 0.5 - 0.05
    if yaw == 180.0:
        return Box(u1 + d, v, GATE_H * 0.5, GATE_T, GATE_W, GATE_H, "spawn_gate")
    if yaw == 0.0:
        return Box(u0 - d, v, GATE_H * 0.5, GATE_T, GATE_W, GATE_H, "spawn_gate")
    if yaw == -90.0:
        return Box(u, v1 + d, GATE_H * 0.5, GATE_W, GATE_T, GATE_H, "spawn_gate")
    return Box(u, v0 - d, GATE_H * 0.5, GATE_W, GATE_T, GATE_H, "spawn_gate")


def boss_spot(room):
    return room.length - PLATFORM_BACK, 0.0


def boss_platform(length):
    """The boss platform and its three ramps: wide (10 m) toward the doorway, 4 m on both sides."""
    cu = length - PLATFORM_BACK
    half = PLATFORM * 0.5
    out = [Box(cu, 0.0, PLATFORM_H * 0.5, PLATFORM, PLATFORM, PLATFORM_H, "boss_platform")]
    pitch = math.degrees(math.atan2(PLATFORM_H, RAMP_RUN))
    t = 0.4
    drop, shift = t * 0.5 * math.cos(math.radians(pitch)), t * 0.5 * math.sin(math.radians(pitch))
    # (where the run starts on the floor, the way it climbs as a yaw, its width)
    for su, sv, yaw, width in ((cu - half - RAMP_RUN, 0.0, 0.0, 10.0), (cu, half + RAMP_RUN, -90.0, 4.0), (cu, -half - RAMP_RUN, 90.0, 4.0)):
        dx, dy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        # The slab's top runs from the floor at the run's start to the platform's edge; a little longer, so
        # its top end tucks under the platform's lip and its bottom end into the floor.
        out.append(Box(su + dx * (RAMP_RUN * 0.5 + shift), sv + dy * (RAMP_RUN * 0.5 + shift), PLATFORM_H * 0.5 - drop,
                       math.hypot(PLATFORM_H, RAMP_RUN) + 0.3, width, t, "ramp", yaw=yaw, pitch=pitch))
    return out


def platform_footprint(room):
    """The platform and its ramps as flat footprints (to keep cover off them and paths round them)."""
    out = []
    for b in room.platform:
        out.append(Box(b.x, b.y, 0.0, RAMP_RUN + 0.6, b.thickness, PLATFORM_H, "ramp", yaw=b.yaw) if b.kind == "ramp" else b)
    return out


# --- The cover generator ----------------------------------------------------------------------------

def _wall_piece(length, yaw, rng):
    return Box(0.0, 0.0, 0.0, length, 0.8, rng.uniform(3.0, 4.0), "ruin_wall", yaw=yaw)


def _boulder(rng, size=None):
    a = size or rng.uniform(3.0, 6.0)
    b = max(3.0, min(6.0, a * rng.uniform(0.7, 1.0)))
    return Box(0.0, 0.0, 0.0, a, b, rng.uniform(2.5, 4.0), "boulder", yaw=rng.uniform(-40.0, 40.0))


def _stump(rng):
    s = rng.uniform(1.0, 1.4)
    return Box(0.0, 0.0, 0.0, s, s, 1.2, "stump", yaw=rng.uniform(0.0, 90.0))


def _run(room, u, from_hub_wall, seg, n, rng, stagger=1.2, overlap=1.5):
    """A broken run of n pieces (walls and one boulder) across the room at u, from one side wall's face
    out seg metres, each overlapping the last 1.5 m across and set alternately in front and behind."""
    u0, u1, v0, v1 = room.face()
    boulder_at = rng.randrange(n)
    pieces = []
    w = -0.1  # how far from the wall the run has reached (it starts touching the wall)
    for i in range(n):
        left = n - i
        target = (seg - w + overlap * (left - 1)) / left
        if i == boulder_at:
            b = _boulder(rng, size=min(6.0, max(4.0, target * 0.75)))
        else:
            dev = rng.uniform(-22.0, 22.0)
            length = (target - 0.8 * abs(math.sin(math.radians(dev)))) / max(math.cos(math.radians(dev)), 0.5)
            b = _wall_piece(min(10.0, max(5.0, length)), 90.0 + dev, rng)
        sv = _span_v(b)
        b.x = u + (stagger if i % 2 else -stagger)
        b.y = w + sv * 0.5           # wall-first coordinates for now
        pieces.append(b)
        w += sv - overlap
    end = max(b.y + _span_v(b) * 0.5 for b in pieces)
    if end > seg:                    # never into the gap: slide the run back (the check catches a wall poke)
        for b in pieces:
            b.y -= end - seg
    for b in pieces:                 # into room coordinates
        if from_hub_wall:
            b.y = v0 + b.y
        else:
            b.y, b.yaw = v1 - b.y, -b.yaw
    return pieces


def _lane_cover(room, rng):
    u0, u1, v0, v1 = room.face()
    width = v1 - v0
    obs = []
    gaps = []
    # Three bands; their gaps go far side, hub side (where the portal stands, mid-room), far side.
    for u, gap_far in ((15.0, True), (30.0, False), (52.0, True)):
        gap = rng.uniform(7.5, 8.5)
        gaps.append(gap)
        obs += _run(room, u + rng.uniform(-1.0, 1.0), gap_far, width - gap, 4, rng)
    # The plug: behind the last gap (from the far wall, a little past the gap), so nothing threads it.
    obs += _run(room, 62.5 + rng.uniform(-0.5, 0.5), False, gaps[2] + 3.0, 2, rng, stagger=0.6)
    # A stump of low cover in the open.
    for _try in range(40):
        s = _stump(rng)
        s.x, s.y = rng.uniform(8.0, 70.0), rng.uniform(v0 + 4.0, v1 - 4.0)
        if all(box_dist(s, o) >= 3.0 for o in obs) and not _placement_problems(room, s):
            obs.append(s)
            break
    return obs


def _boss_cover(room, rng):
    u0, u1, v0, v1 = room.face()
    obs = []
    want = rng.randint(17, 19)
    kinds = ["ruin_wall"] * 9 + ["boulder"] * 6 + ["stump"] * 4
    rng.shuffle(kinds)
    last_u = boss_spot(room)[0] - PLATFORM * 0.5 - RAMP_RUN - 7.0
    for _try in range(3000):
        if len(obs) >= want:
            break
        kind = kinds[len(obs) % len(kinds)]
        b = _wall_piece(rng.uniform(5.0, 10.0), rng.uniform(0.0, 180.0), rng) if kind == "ruin_wall" else _boulder(rng) if kind == "boulder" else _stump(rng)
        b.x, b.y = rng.uniform(9.0, last_u), rng.uniform(v0 + 5.0, v1 - 5.0)
        if any(box_dist(b, o) < 5.0 for o in obs) or _placement_problems(room, b):
            continue
        obs.append(b)
    return obs if len(obs) >= 16 else None


def generate(room, seed):
    """A room's cover (room coordinates) from a seed: the first of seed's attempts that keeps the rules."""
    for attempt in range(500):
        rng = random.Random(seed * 1000 + attempt)
        obs = _boss_cover(room, rng) if room.boss else _lane_cover(room, rng)
        if obs is not None and not room_problems(room, obs):
            return obs
    raise RuntimeError("footrace: no cover for %s keeps the rules" % room.name)


# --- The rules ----------------------------------------------------------------------------------------

def _placement_problems(room, b):
    """One piece of cover against the fixed parts of its room."""
    out = []
    u0, u1, v0, v1 = room.face()
    if any(not (u0 - 0.2 <= cu <= u1 + 0.2 and v0 - 0.2 <= cv <= v1 + 0.2) for cu, cv in corners(b)):
        out.append("%s %s at (%.1f, %.1f) pokes through a wall" % (room.name, b.kind, b.x, b.y))
    for du, dv in room.doorways():
        if point_dist(b, du, dv) < DOOR_CLEAR:
            out.append("%s %s at (%.1f, %.1f) is %.1f m from a doorway" % (room.name, b.kind, b.x, b.y, point_dist(b, du, dv)))
    if point_dist(b, *room.portal()) < DOOR_CLEAR:
        out.append("%s %s at (%.1f, %.1f) is %.1f m from the portal anchor" % (room.name, b.kind, b.x, b.y, point_dist(b, *room.portal())))
    for (su, sv, _), g in zip(room.spawns, room.gates()):
        if point_dist(b, su, sv) < SPAWN_CLEAR or box_dist(b, g) < SPAWN_CLEAR:
            out.append("%s %s at (%.1f, %.1f) is within %.0f m of the spawn at (%.1f, %.1f)" % (room.name, b.kind, b.x, b.y, SPAWN_CLEAR, su, sv))
    if any(box_dist(b, p) < SPAWN_CLEAR for p in platform_footprint(room)):
        out.append("%s %s at (%.1f, %.1f) crowds the boss platform" % (room.name, b.kind, b.x, b.y))
    return out


def free_sight(room, obstacles, ou, ov, ang):
    """How far one sees at eye height from (ou, ov) along ang (degrees from +u), to cover or a wall face."""
    dx, dy = math.cos(math.radians(ang)), math.sin(math.radians(ang))
    u0, u1, v0, v1 = room.face()
    best = float("inf")
    for e, o, d in ((u0, ou, dx), (u1, ou, dx), (v0, ov, dy), (v1, ov, dy)):
        if abs(d) > 1e-9 and (e - o) / d > 0:
            best = min(best, (e - o) / d)
    for b in obstacles:
        if b.kind in BLOCKS_SIGHT and b.height > EYE:
            t = ray_hit(b, ou, ov, dx, dy)
            if t is not None:
                best = min(best, t)
    return best


def sight_starts(room):
    """Where the sightline check looks from: across the doorway-in wall, and across the room's middle."""
    u0, u1, v0, v1 = room.face()
    vs = [v0 + 1.0 + i * 2.0 for i in range(int((v1 - v0 - 2.0) / 2.0) + 1)]
    return [(u0 + 0.75, v) for v in vs] + [(room.length * 0.5, v) for v in vs]


SIGHT_ANGLES = range(-20, 21, 5)      # degrees either side of straight down the room


def longest_sight(room, obstacles):
    best = (0.0, None)
    for ou, ov in sight_starts(room):
        for ang in SIGHT_ANGLES:
            d = free_sight(room, obstacles, ou, ov, ang)
            if d > best[0]:
                best = (d, (ou, ov, ang))
    return best


def walkable(room, obstacles, clearance, step=0.5):
    """Floor cells whose centers are at least clearance from the walls, the cover and the platform."""
    u0, u1, v0, v1 = room.face()
    solid = list(obstacles) + platform_footprint(room)
    cells = set()
    for i in range(int((u1 - u0) / step)):
        u = u0 + (i + 0.5) * step
        if u - u0 < clearance or u1 - u < clearance:
            continue
        near = [(b, math.hypot(b.length, b.thickness) * 0.5 + clearance) for b in solid]
        near = [(b, r) for b, r in near if abs(b.x - u) < r]
        for j in range(int((v1 - v0) / step)):
            v = v0 + (j + 0.5) * step
            if v - v0 < clearance or v1 - v < clearance:
                continue
            if any(abs(b.y - v) < r and point_dist(b, u, v) < clearance for b, r in near):
                continue
            cells.add((i, j))
    return cells, (u0, v0, step)


def reach(cells, frame, start):
    """The cells reachable from the cell nearest start."""
    u0, v0, step = frame
    s = min(cells, key=lambda c: (u0 + (c[0] + 0.5) * step - start[0]) ** 2 + (v0 + (c[1] + 0.5) * step - start[1]) ** 2)
    seen, todo = {s}, [s]
    while todo:
        i, j = todo.pop()
        for n in ((i + 1, j), (i - 1, j), (i, j + 1), (i, j - 1)):
            if n in cells and n not in seen:
                seen.add(n)
                todo.append(n)
    return seen


def _reached(seen, frame, p, within=0.6):
    u0, v0, step = frame
    i, j = int((p[0] - u0) / step), int((p[1] - v0) / step)
    r = int(within / step) + 1
    return any((i + a, j + b) in seen for a in range(-r, r + 1) for b in range(-r, r + 1))


def room_problems(room, obstacles):
    """The rules for one room's cover (room coordinates). Empty when they hold."""
    out = []
    for b in obstacles:
        out += _placement_problems(room, b)
    if out:
        return out
    if not room.boss:
        d, at = longest_sight(room, obstacles)
        if d > MAX_SIGHT:
            return ["%s: a %.1f m sightline from (%.1f, %.1f) at %d degrees" % (room.name, d, at[0], at[1], at[2])]
    else:
        # The boss can be seen (and shot) from the open floor in front of its platform.
        bu, bv = boss_spot(room)
        fu = bu - PLATFORM * 0.5 - RAMP_RUN - 4.0
        for fv in (-10.0, 0.0, 10.0):
            if free_sight(room, obstacles, fu, fv, math.degrees(math.atan2(bv - fv, bu - fu))) < math.hypot(bu - fu, bv - fv) - PLATFORM * 0.5:
                out.append("Boss: cover hides the platform from (%.1f, %.1f)" % (fu, fv))
    # A PATH_W wide way from the doorway in to the doorway out (the boss room: to its platform's ramp).
    cells, frame = walkable(room, obstacles, PATH_W * 0.5)
    seen = reach(cells, frame, (PATH_W * 0.5 + 0.5, 0.0))
    goal = (boss_spot(room)[0] - PLATFORM * 0.5 - RAMP_RUN - PATH_W * 0.5 - 1.0, 0.0) if room.boss else (room.length - PATH_W * 0.5 - 0.5, 0.0)
    if not _reached(seen, frame, goal):
        out.append("%s: no %.0f m wide path from the doorway to %s" % (room.name, PATH_W, "the platform" if room.boss else "the next doorway"))
    # Every spawn and the portal reachable by an enemy-sized body.
    cells, frame = walkable(room, obstacles, 0.6)
    seen = reach(cells, frame, (1.5, 0.0))
    for su, sv, _ in room.spawns:
        if not _reached(seen, frame, (su, sv)):
            out.append("%s: the spawn at (%.1f, %.1f) is cut off" % (room.name, su, sv))
    if not _reached(seen, frame, room.portal()):
        out.append("%s: the portal anchor is cut off" % room.name)
    return out


_PLANS = {}


def room_plan(name):
    """A lane room's or the boss room's plan, its cover generated once (seeded by its place in the lane)."""
    if name not in _PLANS:
        length, width = next((l, w) for n, l, w in LANE_ROOMS if n == name)
        r = Room(name, length, width)
        r.obstacles = generate(r, 7 + [n for n, _, _ in LANE_ROOMS].index(name))
        _PLANS[name] = r
    return _PLANS[name]


def place(b, x0, cy, s):
    """A room-coordinate box into the lane: u from the room's west wall, v toward the hub mirrored (s)."""
    z = b.z if b.kind in ("boss_platform", "ramp", "spawn_gate") else b.height * 0.5
    return Box(x0 + b.x, cy + s * b.y, z, b.length, b.thickness, b.height, b.kind, yaw=s * b.yaw, pitch=b.pitch)


def build():
    boxes, markers = [], []
    rooms = lane_rooms()
    for lane, cy in enumerate(LANE_Y):
        lane_tag = "Lane%d" % lane
        s = 1.0 if cy > 0 else -1.0   # room v (negative toward the hub) to world y: the hub is south of lane 0
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
            if i >= 1:  # Room1 -> Room2 is Door1 ... Room5 -> Boss is Door5
                markers.append(Marker(a[2], cy, 0.0, 0.0, ["DoorSlot", lane_tag, "Door%d" % i]))
        # Room markers, spawns and their gates, cover, the boss platform.
        for name, x0, x1, width in rooms:
            y0, y1 = cy - width * 0.5, cy + width * 0.5
            mid = (x0 + x1) * 0.5
            markers.append(Marker(mid, cy, WALL_H * 0.5, 0.0, ["RoomArea", lane_tag, name], size=(x1 - x0, width, WALL_H)))
            if name == "Spawn":
                for px, py in grid(x0, x1, y0, y1, TEAM_SPAWNS, 2.5):
                    markers.append(Marker(px, py, 0.1, 0.0, ["TeamSpawn", lane_tag]))
                if lane == 0:
                    markers.append(Marker(mid, cy, 0.1, 0.0, ["PlayerStart"]))
                continue
            plan = room_plan(name)
            # The portal stands against the wall nearest the hub, facing into the room.
            pu, pv = plan.portal()
            markers.append(Marker(x0 + pu, cy + s * pv, 0.1, 90.0 * s, ["PortalAnchor", lane_tag, name]))
            for u, v, yaw in plan.spawns:
                markers.append(Marker(x0 + u, cy + s * v, 0.1, yaw if yaw in (0.0, 180.0) else s * yaw, ["EnemySpawn", "PvEOnly", lane_tag, name]))
            boxes += [place(b, x0, cy, s) for b in plan.gates() + plan.obstacles + plan.platform]
            if plan.boss:
                bu, bv = boss_spot(plan)
                markers.append(Marker(x0 + bu, cy + s * bv, PLATFORM_H + 0.1, 180.0, ["BossSpawn", lane_tag]))
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
        for d, x in enumerate(LEVER_X):
            markers.append(Marker(x, side * (hw * 0.5 - 8.0), 0.1, -90.0 * side, ["LeverSlot", "Lane%d" % lane, "Door%d" % (d + 1)]))
    markers.append(Marker(0.0, 0.0, 0.1, 0.0, ["CoreSpawn"]))
    markers.append(Marker(0.0, 0.0, WALL_H * 0.5, 0.0, ["HubArea"], size=(hl, hw, WALL_H)))
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
    if [r[0] for r in rooms] != ["Spawn"] + room_names():
        problems.append("the lane is not Spawn, Room1..Room%d, Boss" % ROOMS)
    for lane in (0, 1):
        lt = "Lane%d" % lane
        for kind in ("DoorSlot", "LeverSlot"):
            for d in range(1, ROOMS + 1):
                n = sum(1 for t in tags if t[0] == kind and lt in t and "Door%d" % d in t)
                if n != 1:
                    problems.append("%s %s Door%d: %d markers" % (kind, lt, d, n))
            n = sum(1 for t in tags if t[0] == kind and lt in t)
            if n != ROOMS:
                problems.append("%s %s: %d, want %d" % (kind, lt, n, ROOMS))
        for room in ["Spawn"] + room_names():
            if sum(1 for t in tags if t[0] == "RoomArea" and lt in t and room in t) != 1:
                problems.append("RoomArea %s %s missing or doubled" % (lt, room))
        for room in room_names():
            if sum(1 for t in tags if t[0] == "PortalAnchor" and lt in t and room in t) != 1:
                problems.append("PortalAnchor %s %s missing or doubled" % (lt, room))
        for kind, want in (("TeamSpawn", TEAM_SPAWNS), ("HubPortal", 1), ("BossSpawn", 1)):
            n = sum(1 for t in tags if t[0] == kind and lt in t)
            if n != want:
                problems.append("%s %s: %d, want %d" % (kind, lt, n, want))
    if sum(1 for t in tags if t[0] == "PlayerStart") != 1:
        problems.append("want exactly one PlayerStart")
    if sum(1 for t in tags if t[0] == "HubArea") != 1:
        problems.append("want exactly one HubArea")
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
    # The levers: room to stand at each and walk between them, inside the hub.
    for a, b in zip(LEVER_X, LEVER_X[1:]):
        if b - a < 6.0:
            problems.append("levers at x %.0f and %.0f are %.1f m apart" % (a, b, b - a))
    if max(abs(x) for x in LEVER_X) > HUB[0] * 0.5 - 4.0:
        problems.append("a lever is within 4 m of the hub's end wall")

    # Enemy spawns: on the walls, facing in, mostly in the far half, clear of the doorway in; the boss
    # room's on all four walls.
    for lane, cy in enumerate(LANE_Y):
        lt = "Lane%d" % lane
        for name, x0, x1, w in rooms:
            if name == "Spawn":
                continue
            spawns = [k for k in m if k.tags[0] == "EnemySpawn" and lt in k.tags and name in k.tags]
            if len(spawns) < 8:
                problems.append("EnemySpawn %s %s: only %d" % (lt, name, len(spawns)))
                continue
            walls, far = set(), 0
            for k in spawns:
                d = {"west": k.x - (x0 + WALL_T * 0.5), "east": (x1 - WALL_T * 0.5) - k.x,
                     "north": (cy + w * 0.5 - WALL_T * 0.5) - k.y, "south": k.y - (cy - w * 0.5 + WALL_T * 0.5)}
                wall, dist = min(d.items(), key=lambda kv: kv[1])
                walls.add(wall)
                if abs(dist - MARKER_IN) > 0.3:
                    problems.append("EnemySpawn %s %s at (%.1f, %.1f) is %.1f m from the %s wall, not %.1f" % (lt, name, k.x, k.y, dist, wall, MARKER_IN))
                into = {"west": 0.0, "east": 180.0, "north": -90.0, "south": 90.0}[wall]
                if abs((k.yaw - into + 180.0) % 360.0 - 180.0) > 1.0:
                    problems.append("EnemySpawn %s %s at (%.1f, %.1f) doesn't face into the room" % (lt, name, k.x, k.y))
                far += k.x > (x0 + x1) * 0.5
                if name != "Boss" and (math.hypot(k.x - x0, k.y - cy) < NEAR_DOOR_CLEAR or wall == "west"):
                    problems.append("EnemySpawn %s %s at (%.1f, %.1f) is by the doorway in" % (lt, name, k.x, k.y))
                if not any(b.kind == "spawn_gate" and math.hypot(b.x - k.x, b.y - k.y) < MARKER_IN + 0.5 for b in layout["boxes"]):
                    problems.append("EnemySpawn %s %s at (%.1f, %.1f) has no gate behind it" % (lt, name, k.x, k.y))
            if name == "Boss":
                if walls != {"west", "east", "north", "south"}:
                    problems.append("Boss room %s spawns on only %s" % (lt, sorted(walls)))
            elif far < 0.75 * len(spawns):
                problems.append("EnemySpawn %s %s: %d of %d in the far half" % (lt, name, far, len(spawns)))
    # Each room's cover against its rules, and the two lanes the same (mirrored).
    for name in room_names():
        plan = room_plan(name)
        lo, hi = (16, 24) if plan.boss else (10, 15)
        if not lo <= len(plan.obstacles) <= hi:
            problems.append("%s has %d pieces of cover, want %d to %d" % (name, len(plan.obstacles), lo, hi))
        problems += room_problems(plan, plan.obstacles)
    for kind in OBSTACLES + ("spawn_gate", "boss_platform", "ramp"):
        north = sorted((round(b.x, 2), round(b.y - LANE_Y[0], 2), round(b.yaw % 180.0, 1)) for b in layout["boxes"] if b.kind == kind and b.y > 0)
        south = sorted((round(b.x, 2), round(LANE_Y[1] - b.y, 2), round(-b.yaw % 180.0, 1)) for b in layout["boxes"] if b.kind == kind and b.y < 0)
        if north != south or not north:
            problems.append("the two lanes' %s differ (or there are none)" % kind)
    # The boss stands on its platform.
    for b in layout["boxes"]:
        if b.kind == "boss_platform" and not any(k.tags[0] == "BossSpawn" and point_dist(b, k.x, k.y) == 0.0 and abs(k.z - PLATFORM_H - 0.1) < 0.01 for k in m):
            problems.append("a boss platform with no BossSpawn on it")
    return problems


if __name__ == "__main__":
    import collections
    layout = build()
    kinds = collections.Counter(b.kind for b in layout["boxes"])
    marks = collections.Counter(k.tags[0] for k in layout["markers"])
    print("boxes:", dict(kinds))
    print("markers:", dict(marks))
    print("bounds:", bounds())
    for name in room_names():
        plan = room_plan(name)
        line = "%s: %.0f x %.0f m, %d pieces of cover, %d spawns" % (name, plan.length, plan.width, len(plan.obstacles), len(plan.spawns))
        if not plan.boss:
            line += ", longest sightline %.1f m" % longest_sight(plan, plan.obstacles)[0]
        print(line)
    problems = check(layout)
    for p in problems:
        print("PROBLEM:", p)
    print("ok" if not problems else "%d problems" % len(problems))
    raise SystemExit(1 if problems else 0)
