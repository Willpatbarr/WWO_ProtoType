"""Cascade's dressing: the art kit (Tools/import_cascade_kit.py) placed over the layout's boxes.

Called by Tools/build_cascade.py. Everything here is looks only: no collision, out of the navmesh. The
layout's boxes keep the gameplay (cover boxes stay as hidden collision under their kit blocks).

Every kit mesh is drawn through one AWWOPropField per mesh (a hierarchical instanced component: a few
draw calls per mesh, culled and LOD-picked per cluster), with a far cull distance for the small things
(FIELDS). Placement is seeded, so a rebuild gives the same map.

What goes where (mockup meters, x east, y north):
- Rock: a pillar under every pier, a rock mass under the citadel, rock stacks under the maples, a ragged
  cliff ring around the map (low toward the giant arches, tall on the diagonals where the big falls pour),
  a crag behind every waterfall that has no deck to pour from, boulders at the water line, karst spires
  where the layout's mesas stood and more beyond them, the six giant arches.
- Ruin: the citadel's tower cluster on the hall's drum, chipped blocks and pillars over the cover boxes,
  buttresses on the walls' outer faces, light slits on their inner faces, banners on the citadel drum
  and the base fronts, lone towers out on the cliffs.
- Plants: red maples on every rock stack, the cliff rim and the base roofs, ivy curtains over deck edges,
  bridges, wall tops and roof edges, ferns and tufts at the foot of walls and cover, shrubs on the rocks.
- Water: a lip and a sheet per waterfall, mist cards and a foam disc where it lands.
"""
import math
import random

import unreal

import cascade_layout as cl

KIT = "/Game/WWO/Maps/CascadeKit/Meshes/SM_Cas_"
KIT_MATS = "/Game/WWO/Maps/CascadeKit/Materials/"
WATER_Z = cl.WATER_Z
SLOT_MATS = {"Cas_Water": "MI_Cas_Falls"}   # slot name -> instance, where it is not MI_<slot>
SUN_FROM_DEG = 235.0      # where the sun stands (mockup degrees): the cliffs stay lower there, so the map isn't in their shade

# Which way a piece's "front" (the manifest's facing) points in its own Unreal space after import, in degrees of
# yaw (0 = +X, 90 = +Y). Read off the bounds in Tools/import_cascade_kit.log.
FRONT = {"Block_A": 0.0, "Block_B": 0.0, "Block_C": 0.0, "Column_A": 0.0,
         "Ivy_Curtain_A": 90.0, "Ivy_Curtain_B": 90.0, "Ivy_Curtain_C": 90.0}   # the curtains hang facing Blender -Y
DEFAULT_FRONT = -90.0   # Blender +Y (the kits' "front") comes in as Unreal -Y

# mesh -> (cast shadow, cull distance in m or 0, material override or None)
FIELDS = {
    "Cliff_A": (True, 0, None), "Cliff_B": (True, 0, None), "Cliff_C": (True, 0, None),
    "Pillar_A": (True, 0, None), "Pillar_B": (True, 0, None), "Mass_A": (True, 0, None),
    "Islet_A": (True, 0, None), "Islet_B": (True, 0, None),
    "Boulder_A": (False, 140, None), "Boulder_B": (False, 220, None), "Boulder_C": (False, 0, None),
    "Spire_A": (False, 0, "MI_Cas_RockFar"), "Spire_B": (False, 0, "MI_Cas_RockFar"), "Spire_C": (False, 0, "MI_Cas_RockFar"),
    "ArchGiant": (False, 0, "MI_Cas_StoneFar"),
    "Citadel_Towers": (True, 0, None), "Tower_A": (False, 0, None), "Tower_B": (False, 0, None), "Tower_C": (False, 0, None),
    "Block_A": (True, 0, "MI_Cas_StoneCover"), "Block_B": (True, 0, "MI_Cas_StoneCover"), "Block_C": (True, 0, "MI_Cas_StoneCover"),
    "Column_A": (True, 0, "MI_Cas_StoneCover"),
    "Buttress_A": (True, 0, None), "LightSlit_A": (False, 120, None), "Rubble_A": (False, 160, None), "Rubble_B": (False, 200, None),
    "Banner_A": (False, 160, None),
    "Maple_A": (True, 0, None), "Maple_B": (True, 0, None), "Maple_C": (True, 0, None),
    "Shrub_Red": (False, 130, None), "Shrub_Green": (False, 130, None),
    "Fern_A": (False, 70, None), "Fern_B": (False, 80, None), "Moss_Tuft_A": (False, 45, None),
    "Ivy_Curtain_A": (False, 150, None), "Ivy_Curtain_B": (False, 190, None), "Ivy_Curtain_C": (False, 190, None),
    "Ivy_Wall_A": (False, 150, None), "Vine_Hang_A": (False, 170, None),
    "Waterfall_Lip": (False, 0, None), "Waterfall_Sheet": (False, 0, None), "Mist_Card": (False, 0, None), "Foam_Disc": (False, 260, None),
}


class Dresser:
    def __init__(self, ns, layout, seed=29):
        self.ns = ns
        self.layout = layout
        self.rng = random.Random(seed)
        self.items = {}          # mesh -> [unreal.Transform]
        self.meshes = {}
        boxes = layout["boxes"]
        self.solid = cl.Index([b for b in boxes if b.kind in cl.DECKS or b.kind in ("steps", "roof", "tunnel_roof", "gallery_roof")])
        self.walk = cl.Index([b for b in boxes if b.kind in cl.DECKS])
        self.everything = cl.Index([b for b in boxes if b.kind not in ("giant_arch", "mesa", "trim", "ivy", "ramp")], cell=8.0)

    # --- Queries --------------------------------------------------------------------------------

    def deck_top(self, x, y):
        """Height of the walkable deck over (x, y), or None over the chasm."""
        best = None
        for b in self.walk.near(x, y):
            if cl.covers2d(b, x, y):
                t = cl.top_at(b, x, y)
                best = t if best is None else max(best, t)
        return best

    def clear(self, x, y, r):
        """Nothing of the map within r m of (x, y) in plan."""
        for dx in (-r, 0.0, r):
            for dy in (-r, 0.0, r):
                for b in self.everything.near(x + dx, y + dy):
                    if cl.covers2d(b, x, y, grow=r):
                        return False
        return True

    # --- Placing --------------------------------------------------------------------------------

    def mesh(self, name):
        if name not in self.meshes:
            path = KIT + name
            m = unreal.load_asset(path)
            if not m:
                raise RuntimeError("missing kit mesh %s: run Tools/import_cascade_kit.py" % path)
            self.meshes[name] = m
        return self.meshes[name]

    def put(self, name, x, y, z, facing=0.0, scale=1.0, pitch=0.0, roll=0.0):
        """One instance: origin at mockup (x, y, z), its front turned to mockup direction `facing` (degrees
        counter-clockwise from east). scale: a number or (along its local x, y, z)."""
        yaw = (90.0 - facing) - FRONT.get(name, DEFAULT_FRONT)
        s = (scale, scale, scale) if isinstance(scale, (int, float)) else scale
        self.items.setdefault(name, []).append(unreal.Transform(
            self.ns.mu(x, y, z), unreal.Rotator(roll, pitch, yaw), unreal.Vector(s[0], s[1], s[2])))

    def flush(self):
        """One prop field per mesh."""
        total = 0
        for name in sorted(self.items):
            shadow, cull, override = FIELDS[name]
            actor = self.ns.actors.spawn_actor_from_class(unreal.WWOPropField, unreal.Vector(0, 0, 0))
            comp = actor.get_editor_property("instances")
            comp.set_static_mesh(self.mesh(name))
            # Materials by slot name (the kit's MI_Cas_<slot>), set on the component: the build never depends on
            # what the imported mesh's own slots hold.
            slots = [str(m.get_editor_property("material_slot_name")) for m in self.mesh(name).get_editor_property("static_materials")]
            for i, slot in enumerate(slots):
                mat = unreal.load_asset(KIT_MATS + (override if (override and i == 0) else SLOT_MATS.get(slot, "MI_" + slot)))
                if mat:
                    comp.set_material(i, mat)
                else:
                    self.ns.log("cascade dressing: no material for %s slot %s" % (name, slot))
            comp.set_editor_property("cast_shadow", shadow)
            if cull:
                comp.set_cull_distances(int(cull * 85.0), int(cull * 100.0))
            comp.add_instances(self.items[name], False, True)
            actor.set_actor_label("Cas_Kit_" + name)
            total += len(self.items[name])
        self.ns.log("cascade dressing: %s (%d instances in %d fields)" % (
            ", ".join("%s %d" % (k, len(v)) for k, v in sorted(self.items.items())), total, len(self.items)))
        return total

    # --- Rock -----------------------------------------------------------------------------------

    def cliff_top(self, a):
        """How high the cliff ring stands at mockup angle a: low on the axes (the giant arches show over it,
        the falls behind the terminals and bases pour from it), tall on the diagonals, lower toward the sun."""
        diag = abs(math.sin(math.radians(2.0 * a)))            # 0 on the axes, 1 on the diagonals
        top = 12.0 + 14.0 * diag ** 1.5
        d = abs((a - SUN_FROM_DEG + 180.0) % 360.0 - 180.0)
        if d < 70.0:
            top -= (1.0 - d / 70.0) * 8.0
        return top

    def rocks(self):
        rng, data = self.rng, self.layout["data"]
        # Under the decks.
        for i, p in enumerate(data["piers"]):
            x, y = p["center"]
            top = p["z_top"] - cl.PIER_UNDER
            if p["radius"] > 15.0:
                self.put("Mass_A", x, y, top - 16.0 * 1.05, rng.uniform(0, 360), (p["radius"] / 20.0, p["radius"] / 20.0, 1.05))
                continue
            k = p["radius"] * 2.0 / 6.0 * 1.15
            h = (top - (WATER_Z - 3.0)) / 16.0
            self.put("Pillar_A" if i % 2 else "Pillar_B", x, y, top - 16.0 * h, rng.uniform(0, 360), (k, k, h))
        # Rock stacks under the layout's maples, and more of them out in the chasm.
        stacks = [(i["center"][0], i["center"][1], i["top_z"], i["radius"]) for i in data["scenery"]["islets_with_trees"]]
        tries = 0
        while len(stacks) < 44 and tries < 6000:
            tries += 1
            a, r = rng.uniform(0, 2 * math.pi), rng.uniform(40.0, 82.0)
            x, y = r * math.cos(a), r * math.sin(a)
            rad = rng.uniform(2.6, 4.2)
            if not self.clear(x, y, rad + 1.6) or any(math.hypot(x - s[0], y - s[1]) < 11.0 for s in stacks):
                continue
            if any(math.hypot(x - w["at"][0], y - w["at"][1]) < w["width"] + 5.0 for w in self.layout["waterfalls"]):
                continue
            stacks.append((x, y, rng.uniform(-3.0, 4.5), rad))
        self.stacks = stacks
        for i, (x, y, top, rad) in enumerate(stacks):
            name, h = ("Islet_B", 19.0) if (top > 2.0 or i % 3 == 0) else ("Islet_A", 14.0)
            k = rad * 2.0 / 6.0
            hz = max((top - (WATER_Z - 4.0)) / h, 0.6)
            self.put(name, x, y, top - h * hz, rng.uniform(0, 360), (k, k, hz))
        # The cliff ring.
        n = 13
        self.cliffs = []
        for i in range(n):
            a = 360.0 * i / n + rng.uniform(-4.0, 4.0)
            r = 92.0 + rng.uniform(-3.0, 5.0)
            top = self.cliff_top(a) + rng.uniform(-2.0, 3.0)
            name, h = [("Cliff_A", self.cliff_h("Cliff_A")), ("Cliff_B", self.cliff_h("Cliff_B")), ("Cliff_C", self.cliff_h("Cliff_C"))][i % 3]
            base = WATER_Z - 4.0
            x, y = r * math.cos(math.radians(a)), r * math.sin(math.radians(a))
            self.put(name, x, y, base, a + 180.0, (1.0, 1.0, (top - base) / h))
            self.cliffs.append((a, r, top))
        # A crag behind each waterfall that pours from "a cliff" where the layout has none.
        for w in self.layout["waterfalls"]:
            if "cliff" not in w["source"] and "window" not in w["source"]:
                continue
            f = math.radians(w["facing_deg"])
            x, y = w["at"][0] - math.cos(f) * (w["width"] * 0.45 + 1.0), w["at"][1] - math.sin(f) * (w["width"] * 0.45 + 1.0)
            if not self.clear(x, y, w["width"] * 0.5):
                continue       # a deck is in the way
            top = w["top_z"] + 0.6
            k = w["width"] * 1.5 / 6.0
            self.put("Islet_B", x, y, top - 19.0 * ((top - (WATER_Z - 4.0)) / 19.0), rng.uniform(0, 360), (k, k, (top - (WATER_Z - 4.0)) / 19.0))
        # Boulders: at the cliff's foot, round the pillars and loose in the water.
        spots = []
        for a, r, _ in self.cliffs:
            for _ in range(4):
                b = math.radians(a + rng.uniform(-13.0, 13.0))
                spots.append(((r - rng.uniform(2.0, 9.0)) * math.cos(b), (r - rng.uniform(2.0, 9.0)) * math.sin(b)))
        for p in data["piers"]:
            for _ in range(2 if p["radius"] < 15.0 else 10):
                b = rng.uniform(0, 2 * math.pi)
                d = p["radius"] * 1.3 + rng.uniform(0.5, 3.0)
                spots.append((p["center"][0] + d * math.cos(b), p["center"][1] + d * math.sin(b)))
        for x, y, _, rad in stacks:
            b = rng.uniform(0, 2 * math.pi)
            spots.append((x + (rad + 2.0) * math.cos(b), y + (rad + 2.0) * math.sin(b)))
        for x, y in spots:
            name = rng.choice(("Boulder_A", "Boulder_B", "Boulder_B", "Boulder_C"))
            self.put(name, x, y, WATER_Z - rng.uniform(0.1, 0.6), rng.uniform(0, 360), rng.uniform(0.8, 1.5))
        # The distance: spires where the mesas stood (and a farther ring of them), the giant arches.
        spire_h = {"Spire_A": self.height("Spire_A"), "Spire_B": self.height("Spire_B"), "Spire_C": self.height("Spire_C")}
        names = sorted(spire_h)
        for i, m in enumerate(data["scenery"]["mesas"]):
            x, y = m["center"]
            name = names[i % 3]
            k = (m["height"] * 1.25 + 14.0) / spire_h[name]
            self.put(name, x, y, WATER_Z - 4.0, rng.uniform(0, 360), k)
        for i in range(16):
            a = math.radians(360.0 * i / 16 + rng.uniform(-8.0, 8.0))
            r = rng.uniform(230.0, 340.0)
            name = names[(i + 1) % 3]
            k = rng.uniform(1.0, 1.7)
            self.put(name, r * math.cos(a), r * math.sin(a), WATER_Z - 6.0, rng.uniform(0, 360), (k * 1.2, k * 1.2, k))
        for g in data["scenery"]["giant_arches"]:
            k = g["radius"] / 55.5
            self.put("ArchGiant", g["center"][0], g["center"][1], g["base_z"] - 2.0, g["plane_yaw_deg"] + 90.0, k)

    def height(self, name):
        b = self.mesh(name).get_bounding_box()
        return (b.max.z - b.min.z) / 100.0

    def cliff_h(self, name):
        return self.height(name)

    # --- Ruin -----------------------------------------------------------------------------------

    def citadel(self):
        self.put("Citadel_Towers", 0.0, 0.0, 14.0, 20.0, 1.25)

    def cover(self, box, index):
        """The kit block (or column) over one cover box: unit pieces, their x along the box's length."""
        name = "Column_A" if box.kind == "pillar" else ("Block_A", "Block_B", "Block_C")[index % 3]
        self.put(name, box.x, box.y, box.z, box.yaw, (box.length, box.thickness, box.height))

    def walls(self):
        rng = self.rng
        slits = butts = 0
        for b in self.layout["boxes"]:
            if b.kind not in ("wall", "tunnel_wall", "gallery_wall") or b.height < 5.0:
                continue
            t = math.radians(b.yaw)
            ux, uy, nx, ny = math.cos(t), math.sin(t), -math.sin(t), math.cos(t)
            foot, top = b.z - b.height * 0.5, b.z + b.height * 0.5
            steps = max(1, int(round(b.length / 3.2)))
            for k in range(steps):
                u = ((k + 0.5) / steps - 0.5) * b.length + rng.uniform(-0.4, 0.4)
                cx, cy = b.x + ux * u, b.y + uy * u
                for side in (-1.0, 1.0):
                    fx, fy = cx + nx * side * (b.thickness * 0.5 + 0.02), cy + ny * side * (b.thickness * 0.5 + 0.02)
                    out = math.degrees(math.atan2(ny * side, nx * side))
                    deck = self.deck_top(cx + nx * side * (b.thickness * 0.5 + 0.8), cy + ny * side * (b.thickness * 0.5 + 0.8))
                    inside = deck is not None and abs(deck - foot) < 1.0
                    if inside:
                        if rng.random() < 0.42 * b.length / (steps * 3.2):
                            self.put("LightSlit_A", fx, fy, foot + 2.6, out)
                            slits += 1
                        if rng.random() < 0.3:
                            self.put(rng.choice(("Ivy_Curtain_A", "Ivy_Curtain_B")), fx, fy, top - rng.uniform(0.0, 0.5), out, rng.uniform(0.8, 1.15))
                        if rng.random() < 0.55:
                            self.put(rng.choice(("Fern_A", "Fern_B", "Moss_Tuft_A", "Moss_Tuft_A")), fx + nx * side * 0.25, fy + ny * side * 0.25, foot, rng.uniform(0, 360), rng.uniform(0.7, 1.2))
                    else:
                        if b.kind != "tunnel_wall" and rng.random() < 0.5:
                            self.put("Buttress_A", fx, fy, foot - 0.5, out, (1.0, 1.0, (b.height + 0.4) / 6.5))
                            butts += 1
                        if rng.random() < 0.55:
                            self.put(rng.choice(("Ivy_Curtain_B", "Ivy_Curtain_C", "Ivy_Curtain_A")), fx, fy, top - rng.uniform(0.0, 0.8), out, rng.uniform(0.9, 1.3))
                        elif rng.random() < 0.3:
                            self.put("Ivy_Wall_A", fx, fy, foot + b.height * rng.uniform(0.4, 0.7), out, rng.uniform(0.8, 1.3))
        # The citadel's drum: banners between the doors, long ivy from its rim.
        r = self.layout["data"]["radii"]["citadel_platform"] + 0.08
        for a in (32.0, 58.0, 122.0, 148.0, 212.0, 238.0, 302.0, 328.0):
            self.put("Banner_A", r * math.cos(math.radians(a)), r * math.sin(math.radians(a)), 12.6, a, 1.3)
        for k in range(26):
            a = 360.0 * k / 26 + rng.uniform(-5.0, 5.0)
            if min(abs((a - d + 180.0) % 360.0 - 180.0) for d in (0.0, 90.0, 180.0, 270.0)) < 11.0:
                continue
            self.put(rng.choice(("Ivy_Curtain_C", "Ivy_Curtain_B", "Vine_Hang_A")), r * math.cos(math.radians(a)), r * math.sin(math.radians(a)),
                     14.0 - rng.uniform(0.0, 0.6), a, rng.uniform(0.9, 1.25))
        # The base rooms: banners on the front wall beside the opening, growth on the roofs.
        for roof in (b for b in self.layout["boxes"] if b.kind == "roof"):
            sx = 1.0 if roof.x > 0 else -1.0
            top = roof.z + roof.height * 0.5
            front = roof.x - sx * roof.length * 0.5
            for y in (-10.5, 10.5):
                self.put("Banner_A", front - sx * 0.6, y, top - 0.9, 180.0 if sx > 0 else 0.0, 1.2)
            hl, hw = roof.length * 0.5, roof.thickness * 0.5
            for k in range(26):
                edge = rng.randrange(4)
                s = rng.uniform(-0.95, 0.95)
                x, y, out = [(roof.x + hl, roof.y + s * hw, 0.0), (roof.x - hl, roof.y + s * hw, 180.0),
                             (roof.x + s * hl, roof.y + hw, 90.0), (roof.x + s * hl, roof.y - hw, 270.0)][edge]
                self.put(rng.choice(("Ivy_Curtain_A", "Ivy_Curtain_B", "Ivy_Curtain_C")), x, y, top, out, rng.uniform(0.9, 1.3))
            for k in range(3):
                self.put(rng.choice(("Maple_A", "Maple_C")), roof.x + rng.uniform(-0.7, 0.7) * hl, roof.y + rng.uniform(-0.8, 0.8) * hw, top - 0.1,
                         rng.uniform(0, 360), rng.uniform(0.8, 1.05))
            for k in range(10):
                self.put(rng.choice(("Shrub_Green", "Shrub_Red", "Fern_B", "Rubble_A")), roof.x + rng.uniform(-0.9, 0.9) * hl, roof.y + rng.uniform(-0.9, 0.9) * hw, top,
                         rng.uniform(0, 360), rng.uniform(0.8, 1.3))
        return slits, butts

    # --- Plants ---------------------------------------------------------------------------------

    def edges(self):
        """Ivy over the decks' open edges and the bridges' sides."""
        rng = self.rng
        count = 0
        for name, (regions, _, z) in self.layout["decks"].items():
            if name.startswith("NodeDais"):
                continue
            for region in regions:
                for ring in region.rings():
                    for i in range(len(ring)):
                        (x0, y0), (x1, y1) = ring[i], ring[(i + 1) % len(ring)]
                        L = math.hypot(x1 - x0, y1 - y0)
                        if L < 0.5:
                            continue
                        ux, uy = (x1 - x0) / L, (y1 - y0) / L
                        d = rng.uniform(0.5, 3.0)
                        while d < L:
                            px, py = x0 + ux * d, y0 + uy * d
                            for nx, ny in ((-uy, ux), (uy, -ux)):
                                if self.over_anything(px + nx * 0.9, py + ny * 0.9) or not self.over_anything(px - nx * 0.9, py - ny * 0.9):
                                    continue
                                if rng.random() < 0.6:
                                    self.put(rng.choice(("Ivy_Curtain_A", "Ivy_Curtain_B", "Ivy_Curtain_B", "Ivy_Curtain_C")), px + nx * 0.05, py + ny * 0.05, z - 0.02,
                                             math.degrees(math.atan2(ny, nx)), rng.uniform(0.85, 1.3))
                                    count += 1
                            d += rng.uniform(2.2, 5.5)
        # Ferns, tufts and the odd shrub along the foot of the rails (both sides: what is over the edge hangs out).
        for b in self.layout["boxes"]:
            if b.kind != "rail" or b.length < 2.0:
                continue
            t, p = math.radians(b.yaw), math.radians(b.pitch)
            run = b.length * math.cos(p)
            d = -run * 0.5 + rng.uniform(0.3, 1.5)
            while d < run * 0.5:
                side = rng.choice((-1.0, 1.0))
                px = b.x + math.cos(t) * d - math.sin(t) * side * 0.3
                py = b.y + math.sin(t) * d + math.cos(t) * side * 0.3
                z = b.z + d * math.tan(p) - b.height * 0.5
                self.put(rng.choice(("Fern_A", "Fern_B", "Moss_Tuft_A", "Moss_Tuft_A", "Fern_A", "Shrub_Green")), px, py, z, rng.uniform(0, 360), rng.uniform(0.6, 1.0))
                d += rng.uniform(1.2, 3.2)
        for b in self.layout["boxes"]:
            if b.kind != "bridge" or b.length < 6.0:
                continue
            t = math.radians(b.yaw)
            ux, uy, nx, ny = math.cos(t), math.sin(t), -math.sin(t), math.cos(t)
            run = b.length * math.cos(math.radians(b.pitch))
            d = -run * 0.5 + rng.uniform(0.5, 2.5)
            while d < run * 0.5:
                for side in (-1.0, 1.0):
                    if rng.random() < 0.45:
                        px, py = b.x + ux * d + nx * side * (b.thickness * 0.5 + 0.05), b.y + uy * d + ny * side * (b.thickness * 0.5 + 0.05)
                        self.put(rng.choice(("Ivy_Curtain_A", "Ivy_Curtain_B", "Vine_Hang_A")), px, py, cl.top_at(b, b.x + ux * d, b.y + uy * d) - 0.05,
                                 math.degrees(math.atan2(ny * side, nx * side)), rng.uniform(0.85, 1.25))
                        count += 1
                d += rng.uniform(2.5, 5.0)
        return count

    def over_anything(self, x, y):
        for b in self.solid.near(x, y):
            if cl.covers2d(b, x, y):
                return True
        return False

    def plants(self):
        rng = self.rng
        maples = 0
        # The rock stacks: a maple each, shrubs and ferns round its foot.
        for x, y, top, rad in self.stacks:
            self.put(rng.choice(("Maple_A", "Maple_B", "Maple_B", "Maple_C")), x, y, top - 0.15, rng.uniform(0, 360), rng.uniform(0.9, 1.25))
            maples += 1
            for _ in range(3):
                a, d = rng.uniform(0, 2 * math.pi), rng.uniform(0.9, rad * 0.8)
                self.put(rng.choice(("Shrub_Red", "Shrub_Green", "Fern_B", "Fern_A")), x + d * math.cos(a), y + d * math.sin(a), top - 0.1, rng.uniform(0, 360), rng.uniform(0.8, 1.3))
            for _ in range(3):
                a = rng.uniform(0, 360)
                self.put(rng.choice(("Ivy_Curtain_A", "Ivy_Curtain_B")), x + rad * 0.9 * math.cos(math.radians(a)), y + rad * 0.9 * math.sin(math.radians(a)), top - 0.3, a, rng.uniform(0.9, 1.3))
        # The cliff rim: maples and shrubs along it, the odd ruined tower, long ivy down the face.
        for i, (a, r, top) in enumerate(self.cliffs):
            for _ in range(4):
                b = math.radians(a + rng.uniform(-12.0, 12.0))
                d = r + rng.uniform(3.0, 8.0)
                self.put(rng.choice(("Maple_A", "Maple_B", "Maple_C")), d * math.cos(b), d * math.sin(b), top - rng.uniform(1.0, 2.5), rng.uniform(0, 360), rng.uniform(1.0, 1.5))
                maples += 1
            for _ in range(5):
                b = math.radians(a + rng.uniform(-13.0, 13.0))
                d = r + rng.uniform(1.5, 6.0)
                self.put(rng.choice(("Shrub_Red", "Shrub_Green", "Shrub_Green")), d * math.cos(b), d * math.sin(b), top - rng.uniform(0.8, 2.0), rng.uniform(0, 360), rng.uniform(1.2, 2.2))
            if i % 3 == 1:
                b = math.radians(a + rng.uniform(-8.0, 8.0))
                d = r + rng.uniform(9.0, 14.0)
                self.put(rng.choice(("Tower_A", "Tower_B", "Tower_C")), d * math.cos(b), d * math.sin(b), top - 3.0, rng.uniform(0, 360), rng.uniform(1.0, 1.4))
        # The layout's ferns, and growth at the foot of every cover piece.
        for kind, x, y, z in self.layout["plants"]:
            if kind == "fern":
                self.put(rng.choice(("Fern_A", "Fern_B")), x, y, z, rng.uniform(0, 360), rng.uniform(0.8, 1.2))
        for b in self.layout["boxes"]:
            if b.kind not in ("block", "low_cover", "pillar", "monolith", "crate"):
                continue
            foot = b.z - b.height * 0.5
            t = math.radians(b.yaw)
            for _ in range(2 if b.kind in ("pillar", "crate") else 3):
                su, sv = rng.choice(((1, rng.uniform(-1, 1)), (-1, rng.uniform(-1, 1)), (rng.uniform(-1, 1), 1), (rng.uniform(-1, 1), -1)))
                u, v = su * (b.length * 0.5 + 0.12), sv * (b.thickness * 0.5 + 0.12)
                self.put(rng.choice(("Moss_Tuft_A", "Moss_Tuft_A", "Fern_A")), b.x + u * math.cos(t) - v * math.sin(t), b.y + u * math.sin(t) + v * math.cos(t), foot,
                         rng.uniform(0, 360), rng.uniform(0.6, 1.0))
            if b.kind in ("block", "monolith") and rng.random() < 0.5:
                side = rng.choice((-1.0, 1.0))
                self.put("Ivy_Curtain_A", b.x - side * math.sin(t) * (b.thickness * 0.5 + 0.03), b.y + side * math.cos(t) * (b.thickness * 0.5 + 0.03), b.z + b.height * 0.5,
                         b.yaw + 90.0 * side, (min(b.length / 2.2, 1.2), 1.0, min(b.height / 3.2, 1.0)))
        return maples

    # --- Water ----------------------------------------------------------------------------------

    def water(self):
        rng = self.rng
        for w in self.layout["waterfalls"]:
            x, y = w["at"]
            f = w["facing_deg"]
            top, width = w["top_z"], w["width"]
            self.put("Waterfall_Lip", x, y, top, f, (width, 1.0, 1.0))
            fx, fy = x + math.cos(math.radians(f)) * 0.7, y + math.sin(math.radians(f)) * 0.7
            self.put("Waterfall_Sheet", fx, fy, top - 1.45, f, (width, 1.0, top - 1.45 - WATER_Z + 0.3))
            self.put("Foam_Disc", fx, fy, WATER_Z + 0.06, rng.uniform(0, 360), width * 1.1)
            for k, (up, wide, tall) in enumerate(((1.6, 1.7, 4.5), (3.4, 1.2, 6.0), (top - WATER_Z - 0.8, 1.0, 2.0))):
                self.put("Mist_Card", fx, fy, WATER_Z + up, f + rng.uniform(-20, 20), (width * wide, width * wide, tall))


def dress(ns, layout):
    """Everything but the cover blocks (build_cascade calls cover() per box). Returns the dresser to flush."""
    d = Dresser(ns, layout)
    return d
