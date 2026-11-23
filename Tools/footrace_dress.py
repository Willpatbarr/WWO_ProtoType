"""The FootRace map's dressing: a deep forest ravine cut into rooms by old man-made walls.

Called by Tools/build_maps.py's build_footrace() once the layout's boxes and markers are placed. Looks only:
nothing here collides or touches the navmesh (the layout's boxes keep the gameplay; cover boxes and the
lanes' side walls stay as hidden collision under the kit pieces).

Two kits: the Ravine kit (Tools/import_ravine_kit.py: walls, doors, spawn mouths, cover pieces, forest plants)
and Cascade's (Tools/import_cascade_kit.py: the rock cliffs, boulders, small ferns, ivy). A mesh name with
"Cas:" in front is Cascade's. Every mesh goes through one AWWOPropField (instanced, culled per cluster), with
shadow and cull distance per mesh in FIELDS. Placement is seeded.

What goes where (mockup meters, x east along the lanes, y north):
- The lanes' side walls are the ravine's cliffs (Cascade's cliff pieces set back behind the collision wall,
  a band of bushes, saplings, ferns and boulders at their foot so the invisible wall reads as undergrowth),
  with big trees on the rim and trees leaning out over the rooms.
- The cross walls (and the hub's walls) are the man-made walls: the layout's 12 m boxes in weathered concrete,
  carried up to 20 m, with pilasters, a crenellated cap, relief panels, an arched surround at every doorway,
  heavy vines and roots down the faces.
- Enemy spawns: a cave mouth on the cliffs, a culvert gate on the man-made walls, at every EnemySpawn marker.
- Cover: a broken wall, a rock block or a stump over each cover box. The boss's platform gets a kerb with a
  glowing inlay and an obelisk at each corner.
- The floor: grass, ferns and flowers everywhere, logs and fallen blocks along the edges, and the mutated
  plants, a few in Room1 and more in every room toward the boss.
"""
import math
import random

import unreal

import footrace_layout as fl

KITS = {"Rav": "/Game/WWO/Maps/RavineKit/", "Cas": "/Game/WWO/Maps/CascadeKit/"}
WALL_TOP = 20.3           # the man-made walls' face ends here (the kit's cap and pilaster heads go on to 24)
CLIFF_BACK = 4.5          # the cliff pieces' origin sits this far behind the collision wall
CLIFF_TOP = (24.0, 31.0)  # how high the cliffs stand

DEFAULT_FRONT = -90.0     # Blender +Y (the kits' "front") comes in as Unreal -Y
FRONT = {"Ruin_Wall_A": 0.0, "Ruin_Wall_B": 0.0, "Ruin_Wall_C": 0.0, "Rock_Block_A": 0.0, "Rock_Block_B": 0.0, "Stump_A": 0.0,
         "Vine_Curtain_Big_A": 90.0, "Vine_Curtain_Big_B": 90.0, "Roots_Wall_A": 90.0, "Cas:Ivy_Curtain_B": 90.0, "Cas:Ivy_Curtain_C": 90.0}

# mesh -> (cast shadow, cull distance in m or 0)
FIELDS = {
    "Cas:Cliff_A": (True, 0), "Cas:Cliff_B": (True, 0), "Cas:Cliff_C": (True, 0),
    "Cas:Boulder_A": (False, 90), "Cas:Boulder_B": (False, 140), "Cas:Fern_A": (False, 55), "Cas:Fern_B": (False, 60),
    "Cas:Ivy_Curtain_B": (False, 150), "Cas:Ivy_Curtain_C": (False, 150),
    "Wall_Pilaster_A": (True, 0), "Wall_Cap_A": (False, 0), "Wall_Panel_A": (False, 220), "Wall_Panel_B": (False, 220),
    "Door_Arch_A": (True, 0), "Spawn_Cave_A": (True, 0), "Spawn_Gate_A": (True, 0),
    "Ruin_Wall_A": (True, 0), "Ruin_Wall_B": (True, 0), "Ruin_Wall_C": (True, 0), "Rock_Block_A": (True, 0), "Rock_Block_B": (True, 0),
    "Stump_A": (True, 0), "Log_A": (True, 160), "Hewn_Block_A": (False, 120), "Hewn_Block_B": (False, 120),
    "Platform_Edge_A": (False, 0), "Platform_Obelisk_A": (True, 0),
    "Tree_A": (False, 0), "Tree_B": (False, 0), "Tree_Lean_A": (True, 0), "Tree_Sapling_A": (False, 180),
    "Bush_A": (False, 90), "Bush_B": (False, 110), "Fern_Big_A": (False, 60), "Grass_A": (False, 32), "Grass_B": (False, 40),
    "Flowers_A": (False, 40), "Vine_Curtain_Big_A": (False, 0), "Vine_Curtain_Big_B": (False, 0), "Moss_Hang_A": (False, 120),
    "Mutant_Fern_A": (False, 70), "Mutant_Bulb_A": (False, 70), "Mutant_Stalk_A": (False, 100), "Roots_Wall_A": (False, 200),
}
# Slot name -> material, where it is not <kit>Materials/MI_<slot>.
SLOT_MATS = {"Rav_Bark": KITS["Rav"] + "Materials/MI_Rav_Wood", "Rav_Water": "/Game/WWO/Materials/M_WWO_CascadeWater"}
MUTANT_GLOW = KITS["Rav"] + "Materials/MI_Rav_Pod"   # the mutants' pods and nodules: a cold glow, not the lamps' orange


def covers(b, x, y, grow=0.0):
    t = math.radians(b.yaw)
    dx, dy = x - b.x, y - b.y
    u, v = dx * math.cos(t) + dy * math.sin(t), -dx * math.sin(t) + dy * math.cos(t)
    return abs(u) <= b.length * 0.5 + grow and abs(v) <= b.thickness * 0.5 + grow


class Dresser:
    def __init__(self, ns, layout, seed=41):
        self.ns, self.layout, self.rng = ns, layout, random.Random(seed)
        self.items, self.meshes = {}, {}
        self.solid = [b for b in layout["boxes"] if b.kind in ("ruin_wall", "boulder", "stump", "boss_platform", "ramp", "spawn_gate")]
        self.rooms = []      # (lane, name, x0, x1, y0, y1)
        for lane, cy in enumerate(fl.LANE_Y):
            for name, x0, x1, width in fl.lane_rooms():
                self.rooms.append((lane, name, x0, x1, cy - width * 0.5, cy + width * 0.5))

    # --- Placing --------------------------------------------------------------------------------

    def mesh(self, name):
        if name not in self.meshes:
            kit, short = name.split(":") if ":" in name else ("Rav", name)
            path = "%sMeshes/SM_%s_%s" % (KITS[kit], kit, short)
            m = unreal.load_asset(path)
            if not m:
                raise RuntimeError("missing kit mesh %s: run Tools/import_cascade_kit.py and Tools/import_ravine_kit.py" % path)
            self.meshes[name] = m
        return self.meshes[name]

    def put(self, name, x, y, z, facing=0.0, scale=1.0, pitch=0.0, roll=0.0):
        """One instance at mockup (x, y, z), its front turned to mockup direction `facing` (degrees ccw from east)."""
        yaw = (90.0 - facing) - FRONT.get(name, DEFAULT_FRONT)
        s = (scale, scale, scale) if isinstance(scale, (int, float)) else scale
        self.items.setdefault(name, []).append(unreal.Transform(self.ns.mu(x, y, z), unreal.Rotator(roll, pitch, yaw), unreal.Vector(s[0], s[1], s[2])))

    def height(self, name):
        b = self.mesh(name).get_bounding_box()
        return (b.max.z - b.min.z) / 100.0

    def free(self, x, y, grow=0.8):
        return not any(covers(b, x, y, grow) for b in self.solid)

    def flush(self):
        total = 0
        for name in sorted(self.items):
            shadow, cull = FIELDS[name]
            actor = self.ns.actors.spawn_actor_from_class(unreal.WWOPropField, unreal.Vector(0, 0, 0))
            comp = actor.get_editor_property("instances")
            mesh = self.mesh(name)
            comp.set_static_mesh(mesh)
            for i, m in enumerate(mesh.get_editor_property("static_materials")):
                slot = str(m.get_editor_property("material_slot_name"))
                path = SLOT_MATS.get(slot) or "%sMaterials/MI_%s" % (KITS[slot[:3]] if slot[:3] in KITS else KITS["Rav"], slot)
                if slot == "Cas_Glow" and name.startswith("Mutant"):
                    path = MUTANT_GLOW
                mat = unreal.load_asset(path)
                if mat:
                    comp.set_material(i, mat)
                else:
                    self.ns.log("footrace dressing: no material %s for %s" % (path, name))
            comp.set_editor_property("cast_shadow", shadow)
            if cull:
                comp.set_cull_distances(int(cull * 85.0), int(cull * 100.0))
            comp.add_instances(self.items[name], False, True)
            actor.set_actor_label("Rav_Kit_" + name.replace(":", "_"))
            total += len(self.items[name])
        self.ns.log("footrace dressing: %s (%d instances in %d fields)" % (
            ", ".join("%s %d" % (k, len(v)) for k, v in sorted(self.items.items())), total, len(self.items)))
        return total

    # --- The ravine's sides ---------------------------------------------------------------------

    def cliffs(self, side_walls):
        """side_walls: the lanes' side-wall boxes (running along x). Cliffs behind each, growth at its foot,
        trees on the rim."""
        rng = self.rng
        names = ("Cas:Cliff_A", "Cas:Cliff_B", "Cas:Cliff_C")
        k = 0
        for w in side_walls:
            lane_y = fl.LANE_Y[0] if w.y > 0 else fl.LANE_Y[1]
            inward = 1.0 if lane_y > w.y else -1.0           # +y or -y is into the room
            face = 90.0 if inward > 0 else -90.0
            n = max(1, int(math.ceil(w.length / 52.0)))
            for i in range(n):
                x = w.x - w.length * 0.5 + w.length * (i + 0.5) / n + rng.uniform(-2.0, 2.0)
                name = names[k % 3]
                k += 1
                top = rng.uniform(*CLIFF_TOP)
                base = -3.0
                self.put(name, x, w.y - inward * CLIFF_BACK, base, face, (max(w.length / n / 56.0, 0.85), 1.0, (top - base) / self.height(name)))
                # The rim: big trees standing back, and one leaning out over the room.
                for _ in range(3):
                    self.put(rng.choice(("Tree_A", "Tree_B")), x + rng.uniform(-24.0, 24.0), w.y - inward * rng.uniform(9.0, 17.0), top - rng.uniform(2.0, 4.5),
                             rng.uniform(0, 360), rng.uniform(0.85, 1.2))
                if rng.random() < 0.75:
                    self.put("Tree_Lean_A", x + rng.uniform(-20.0, 20.0), w.y - inward * rng.uniform(5.5, 7.5), top - rng.uniform(3.0, 6.0),
                             face + rng.uniform(-25.0, 25.0), rng.uniform(0.9, 1.25))
                for _ in range(4):
                    self.put(rng.choice(("Bush_B", "Bush_A")), x + rng.uniform(-26.0, 26.0), w.y - inward * rng.uniform(6.0, 10.0), top - rng.uniform(1.0, 3.0),
                             rng.uniform(0, 360), rng.uniform(1.4, 2.4))
            # The foot: a band of undergrowth along the collision wall (both sides of it), thick enough to hide
            # that the rock stands a few meters behind.
            d = -w.length * 0.5
            while d < w.length * 0.5:
                x = w.x + d
                pick = rng.random()
                off = rng.uniform(-3.5, 1.6)                  # mostly behind the wall, some reaching into the room
                y = w.y + inward * off
                if not self.free(x, y, 0.3):
                    d += 1.0
                    continue
                if pick < 0.34:
                    self.put(rng.choice(("Bush_A", "Bush_B", "Bush_B")), x, y, 0.0, rng.uniform(0, 360), rng.uniform(0.9, 1.7))
                elif pick < 0.58:
                    self.put("Fern_Big_A", x, y, 0.0, rng.uniform(0, 360), rng.uniform(0.8, 1.3))
                elif pick < 0.72:
                    self.put(rng.choice(("Cas:Boulder_B", "Cas:Boulder_A")), x, w.y - inward * rng.uniform(0.5, 3.0), rng.uniform(0.0, 0.4), rng.uniform(0, 360), rng.uniform(0.8, 1.6))
                elif pick < 0.80:
                    self.put("Tree_Sapling_A", x, w.y - inward * rng.uniform(0.6, 3.0), 0.0, rng.uniform(0, 360), rng.uniform(0.8, 1.4))
                elif pick < 0.90:
                    self.put(rng.choice(("Cas:Fern_A", "Cas:Fern_B")), x, y, 0.0, rng.uniform(0, 360), rng.uniform(0.9, 1.4))
                else:
                    self.put("Grass_B", x, y, 0.0, rng.uniform(0, 360), rng.uniform(1.0, 1.6))
                d += rng.uniform(0.9, 2.2)
            for _ in range(max(1, int(w.length / 38.0))):
                x = w.x + rng.uniform(-0.45, 0.45) * w.length
                self.put("Log_A", x, w.y - inward * rng.uniform(1.0, 2.5), 0.0, rng.uniform(-15.0, 15.0) + 90.0, rng.uniform(0.8, 1.1))

    # --- The man-made walls ---------------------------------------------------------------------

    def wall(self, x0, y0, x1, y1, doors, gates):
        """One straight run of man-made wall from (x0, y0) to (x1, y1): pilasters on both faces, the cap, panels,
        vines. doors / gates: distances along the run that must stay clear (a doorway, a spawn gate)."""
        rng = self.rng
        L = math.hypot(x1 - x0, y1 - y0)
        ux, uy = (x1 - x0) / L, (y1 - y0) / L
        along = math.degrees(math.atan2(uy, ux))
        clear = lambda d, r: all(abs(d - c) > r for c in doors) and all(abs(d - g) > r - 1.0 for g in gates)   # noqa: E731
        n = max(2, int(round(L / 8.5)))
        for i in range(n + 1):
            d = L * i / n
            if not clear(d, 4.6):
                continue
            for side in (1.0, -1.0):
                self.put("Wall_Pilaster_A", x0 + ux * d - uy * side * (fl.WALL_T * 0.5), y0 + uy * d + ux * side * (fl.WALL_T * 0.5), 0.0, along + 90.0 * side)
        for i in range(n):
            d = L * (i + 0.5) / n
            px, py = x0 + ux * d, y0 + uy * d
            self.put("Wall_Cap_A", px, py, WALL_TOP, along - 90.0, (L / n / 8.0, 1.0, 1.0))
            for side in (1.0, -1.0):
                fx, fy = px - uy * side * (fl.WALL_T * 0.5 + 0.02), py + ux * side * (fl.WALL_T * 0.5 + 0.02)
                out = along + 90.0 * side
                roll = rng.random()
                if roll < 0.45:
                    self.put(rng.choice(("Vine_Curtain_Big_A", "Vine_Curtain_Big_B")), fx, fy, WALL_TOP - rng.uniform(0.0, 1.0), out, rng.uniform(0.9, 1.25))
                elif roll < 0.60:
                    self.put("Roots_Wall_A", fx, fy, WALL_TOP, out, rng.uniform(1.0, 1.5))
                elif roll < 0.85 and clear(d, 5.5):
                    self.put(rng.choice(("Wall_Panel_A", "Wall_Panel_B")), fx, fy, rng.uniform(8.5, 13.0), out, rng.uniform(0.9, 1.1))
                if clear(d, 4.0) and rng.random() < 0.5:
                    self.put(rng.choice(("Hewn_Block_A", "Hewn_Block_B")), fx - uy * side * rng.uniform(1.0, 2.5), fy + ux * side * rng.uniform(1.0, 2.5), 0.0,
                             rng.uniform(0, 360), rng.uniform(0.8, 1.3))
        for c in doors:
            self.put("Door_Arch_A", x0 + ux * c, y0 + uy * c, 0.0, along + 90.0)
            for side in (1.0, -1.0):   # beard moss under the lintel
                self.put("Moss_Hang_A", x0 + ux * c - uy * side * 0.5, y0 + uy * c + ux * side * 0.5, 4.0, along + 90.0 * side, (1.6, 1.0, 0.5))

    # --- Spawns, cover, the boss's platform ------------------------------------------------------

    def spawns(self, cliff_ys):
        for m in self.layout["markers"]:
            if m.tags[0] != "EnemySpawn":
                continue
            t = math.radians(m.yaw)
            x, y = m.x - math.cos(t) * fl.MARKER_IN, m.y - math.sin(t) * fl.MARKER_IN      # the wall's face
            on_cliff = abs(math.sin(t)) > 0.7 and any(abs(y - cy) < 1.0 for cy in cliff_ys)
            self.put("Spawn_Cave_A" if on_cliff else "Spawn_Gate_A", x, y, 0.0, m.yaw)

    def cover(self, box, index):
        if box.kind == "ruin_wall":
            name = ("Ruin_Wall_A", "Ruin_Wall_B", "Ruin_Wall_C")[index % 3]
        elif box.kind == "boulder":
            name = ("Rock_Block_A", "Rock_Block_B")[index % 2]
        else:
            name = "Stump_A"
        k = 1.25 if box.kind == "stump" else 1.0      # the stump's roots flare past its box
        self.put(name, box.x, box.y, box.z, box.yaw + (180.0 if index % 4 >= 2 else 0.0), (box.length * k, box.thickness * k, box.height))

    def platform(self, box):
        hl, hw = box.length * 0.5, box.thickness * 0.5
        top = box.z + box.height * 0.5
        ramps = [b for b in self.layout["boxes"] if b.kind == "ramp" and abs(b.x - box.x) < hl + 8.0 and abs(b.y - box.y) < hw + 8.0]
        for (x0, y0, x1, y1, out) in ((box.x - hl, box.y - hw, box.x + hl, box.y - hw, -90.0), (box.x - hl, box.y + hw, box.x + hl, box.y + hw, 90.0),
                                      (box.x - hl, box.y - hw, box.x - hl, box.y + hw, 180.0), (box.x + hl, box.y - hw, box.x + hl, box.y + hw, 0.0)):
            L = math.hypot(x1 - x0, y1 - y0)
            n = int(round(L / 4.0))
            for i in range(n):
                px, py = x0 + (x1 - x0) * (i + 0.5) / n, y0 + (y1 - y0) * (i + 0.5) / n
                ox, oy = math.cos(math.radians(out)), math.sin(math.radians(out))
                if any(covers(r, px + ox * 1.5, py + oy * 1.5, 0.3) for r in ramps):
                    continue       # a ramp meets the platform here
                self.put("Platform_Edge_A", px + ox * 0.05, py + oy * 0.05, top - 1.5, out, (L / n / 4.0, 1.0, 1.0))
        for sx in (-1, 1):
            for sy in (-1, 1):
                self.put("Platform_Obelisk_A", box.x + sx * (hl - 0.9), box.y + sy * (hw - 0.9), top, 0.0, 1.2)

    # --- The floor ------------------------------------------------------------------------------

    def floor(self):
        rng = self.rng
        for lane, name, x0, x1, y0, y1 in self.rooms:
            area = (x1 - x0) * (y1 - y0)
            depth = {"Spawn": 0, "Boss": 6}.get(name, int(name[4:]) if name.startswith("Room") else 0)    # how far down the lane
            plan = [("Grass_A", area / 30.0), ("Grass_B", area / 42.0), ("Cas:Fern_A", area / 150.0), ("Fern_Big_A", area / 240.0),
                    ("Flowers_A", area / 170.0), ("Mutant_Fern_A", depth * 2.5), ("Mutant_Bulb_A", depth * 2.0), ("Mutant_Stalk_A", depth * 1.0)]
            for mesh, count in plan:
                for _ in range(int(count)):
                    x, y = rng.uniform(x0 + 0.8, x1 - 0.8), rng.uniform(y0 + 0.8, y1 - 0.8)
                    big = mesh in ("Fern_Big_A", "Mutant_Stalk_A", "Mutant_Bulb_A")
                    if big and min(y - y0, y1 - y) > 6.0 and rng.random() < 0.6:
                        continue      # the taller plants keep mostly to the edges, clear of the fighting
                    if not self.free(x, y, 0.5 if big else 0.15):
                        continue
                    self.put(mesh, x, y, 0.0, rng.uniform(0, 360), rng.uniform(0.75, 1.3))
        # Growth and rubble at the foot of every cover piece.
        for b in self.layout["boxes"]:
            if b.kind not in ("ruin_wall", "boulder"):
                continue
            t = math.radians(b.yaw)
            for _ in range(5):
                su, sv = rng.choice(((1, rng.uniform(-1, 1)), (-1, rng.uniform(-1, 1)), (rng.uniform(-1, 1), 1), (rng.uniform(-1, 1), -1)))
                u, v = su * (b.length * 0.5 + 0.25), sv * (b.thickness * 0.5 + 0.25)
                self.put(rng.choice(("Grass_B", "Cas:Fern_B", "Grass_A", "Flowers_A", "Fern_Big_A")), b.x + u * math.cos(t) - v * math.sin(t), b.y + u * math.sin(t) + v * math.cos(t),
                         b.z - b.height * 0.5, rng.uniform(0, 360), rng.uniform(0.8, 1.3))
            if b.kind == "ruin_wall" and rng.random() < 0.6:
                side = rng.choice((-1.0, 1.0))
                self.put(rng.choice(("Cas:Ivy_Curtain_B", "Cas:Ivy_Curtain_C")), b.x - side * math.sin(t) * (b.thickness * 0.5 + 0.03), b.y + side * math.cos(t) * (b.thickness * 0.5 + 0.03),
                         b.z + b.height * 0.5 - 0.3, b.yaw + 90.0 * side, (min(b.length / 3.2, 1.6), 1.0, min(b.height / 5.2, 0.7)))
