# Checks the graft-anchor JSON that make_anchors.py writes (plain Python 3, no Blender):
#   python3 Art/Shells/tools/check_anchors.py
# Exits 1 if any check fails. Mirror differences on parts listed in ASYMMETRIC are reported, not failed.
import json, math, os, sys, glob, itertools

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
DIR = os.path.join(ROOT, "Config", "ShellAnchors")
SLOTS = ("Head", "Core", "Arms", "Legs")
SIDED = lambda names: [n + s for n in names for s in ("_l", "_r")]
BONES = {"Head": {"head", "neck_01"},
         "Core": {"pelvis", "spine_01", "spine_02", "clavicle_l", "clavicle_r"},
         "Arms": set(SIDED(["upperarm", "lowerarm"])),
         "Legs": set(SIDED(["thigh", "calf", "foot"]))}
BONES["Skeleton"] = set().union(*BONES.values())
# Mirror differences that are real geometry: reported, not failed. A part name covers all its anchors and
# paths; (part, name) covers one.
ASYMMETRIC = {
    "SK_Shell2_Head": "the scout head is deliberately one-sided",
    ("SK_Shell0_Core", "chest_ring"): "the base robot's torso has a one-sided component under the right arm "
                                      "(x -17..-7, z 135-145); its wrap differs left/right by up to 4 cm there",
    ("SK_Shell0_Core", "chest_side_line_l"): "it passes the same one-sided component under the right arm as chest_ring",
}
STEP, STEP_TOL, JUMP, MIRROR_TOL, SEAM_TOL = 2.0, 1.0, 5.0, 1.5, 4.0
fails, notes = [], []


def dist(a, b): return math.dist(a, b)
def norm(v): return math.sqrt(sum(x * x for x in v))
def mir(p): return [-p[0], p[1], p[2]]
def fail(*a): fails.append(" ".join(str(x) for x in a))


def twin(name):
    if name.endswith("_l"): return name[:-2] + "_r"
    return None


def load(name):
    with open(os.path.join(DIR, name + ".json")) as f: return json.load(f)


docs = {os.path.basename(p)[:-5]: None for p in glob.glob(os.path.join(DIR, "*.json"))}
for k in docs: docs[k] = load(k)
shells = sorted({int(k[8]) for k in docs if k.startswith("SK_Shell")})
print("files:", len(docs), "shells:", shells)
for k in sorted(docs):
    print("  %-22s %6.1f KB  anchors %2d  paths %2d  surface %4d" % (k, os.path.getsize(os.path.join(DIR, k + ".json")) / 1024,
                                                      len(docs[k]["anchors"]), len(docs[k]["paths"]),
                                                      len(docs[k].get("surface", {}).get("points", []))))

# 1. the same names per slot
for slot in SLOTS:
    parts = ["SK_Shell%d_%s" % (s, slot) for s in shells]
    missing = [p for p in parts if p not in docs]
    if missing: fail("missing parts", missing); continue
    a0, p0 = set(docs[parts[0]]["anchors"]), set(docs[parts[0]]["paths"])
    for p in parts[1:]:
        if set(docs[p]["anchors"]) != a0: fail(p, "anchor names differ from", parts[0], set(docs[p]["anchors"]) ^ a0)
        if set(docs[p]["paths"]) != p0: fail(p, "path names differ from", parts[0], set(docs[p]["paths"]) ^ p0)
    print("%-5s same names on all shells: %d anchors, %d paths" % (slot, len(a0), len(p0)))

# 2. per file: unit vectors, spacing, closure, bones, header
worst_step = (0, None); worst_unit = 0.0
for name, d in sorted(docs.items()):
    slot = d["slot"]
    if d.get("part") != name or d.get("space") != "blender_rig_cm": fail(name, "bad header")
    for an, a in d["anchors"].items():
        for key in ("n", "up"):
            e = abs(norm(a[key]) - 1); worst_unit = max(worst_unit, e)
            if e > 2e-3: fail(name, an, key, "not unit", norm(a[key]))
        if abs(sum(x * y for x, y in zip(a["n"], a["up"]))) > 0.02: fail(name, an, "up not tangent")
        if not 2.0 <= a["size"] <= 20.0: fail(name, an, "size out of range", a["size"])
        if a["bone"] not in BONES[slot]: fail(name, an, "bone", a["bone"], "not in", slot)
    for pn, p in d["paths"].items():
        pts = p["points"]
        if len(pts) < 2: fail(name, pn, "too few points"); continue
        for q in pts:
            e = abs(norm(q["n"]) - 1); worst_unit = max(worst_unit, e)
            if e > 2e-3: fail(name, pn, "normal not unit")
            if q["bone"] not in BONES[slot]: fail(name, pn, "bone", q["bone"], "not in", slot)
        segs = [dist(a["p"], b["p"]) for a, b in zip(pts, pts[1:])]
        for i, s in enumerate(segs):
            dev = abs(s - STEP)
            if dev > worst_step[0]: worst_step = (dev, "%s %s seg %d = %.2f cm" % (name, pn, i, s))
            if dev > STEP_TOL or s > JUMP: fail(name, pn, "segment %d is %.2f cm" % (i, s))
        if p["closed"] and dist(pts[0]["p"], pts[-1]["p"]) > 1e-3: fail(name, pn, "closed path does not close")
        if abs(sum(segs) - p["length"]) > 0.05 * p["length"] + 0.1: fail(name, pn, "length mismatch")
        for key in ("from_seam", "to_seam"):
            if p[key] and p[key] not in docs["SK_RobotSkeleton"]["anchors"]: fail(name, pn, key, p[key], "unknown seam")
    surf = d.get("surface")
    if slot != "Skeleton":
        if not surf or not surf.get("points"): fail(name, "no surface samples")
        else:
            for row in surf["points"]:
                if abs(norm(row[3:6]) - 1) > 0.01: fail(name, "surface normal not unit", row); break
                if surf["bones"][row[6]] not in BONES[slot]: fail(name, "surface bone", surf["bones"][row[6]], "not in", slot); break
print("worst unit-length error %.5f; worst step deviation from %.1f cm: %.2f (%s)" % (worst_unit, STEP, worst_step[0], worst_step[1]))

# 3. _l/_r mirror pairs
worst_mirror = {}
for name, d in sorted(docs.items()):
    w = (0.0, ""); errs = []
    for an, a in d["anchors"].items():
        t = twin(an)
        if t and t in d["anchors"]:
            e = dist(a["p"], mir(d["anchors"][t]["p"])); errs.append((e, an))
            if e > w[0]: w = (e, an)
    for pn, p in d["paths"].items():
        t = twin(pn)
        if t and t in d["paths"]:
            A = [q["p"] for q in p["points"]]; B = [mir(q["p"]) for q in d["paths"][t]["points"]]
            e = max(max(min(dist(a, b) for b in B) for a in A), max(min(dist(a, b) for a in A) for b in B))
            errs.append((e, pn))
            if e > w[0]: w = (e, pn)
    # centre-line anchors and paths (no side) should be their own mirror
    for an, a in d["anchors"].items():
        if an.endswith(("_l", "_r")): continue
        e = 2 * abs(a["p"][0]); errs.append((e, an + " (self)"))
        if e > w[0]: w = (e, an + " (self)")
    for pn, p in d["paths"].items():
        if pn.endswith(("_l", "_r")): continue
        A = [q["p"] for q in p["points"]]
        e = max(min(dist(mir(a), b) for b in A) for a in A)
        errs.append((e, pn + " (self)"))
        if e > w[0]: w = (e, pn + " (self)")
    worst_mirror[name] = w
    for e, at in errs:
        if e <= MIRROR_TOL: continue
        msg = "%s mirror error %.2f cm at %s" % (name, e, at)
        why = ASYMMETRIC.get(name) or ASYMMETRIC.get((name, at.split(" ")[0]))
        if why: notes.append("known asymmetry: %s (%s)" % (msg, why))
        else: fail(msg)
print("worst mirror error per part:")
for k, (e, at) in sorted(worst_mirror.items()): print("  %-22s %.2f cm  (%s)" % (k, e, at))

# 4. seam joins across shells: every Core with every Arms/Legs
seams = docs["SK_RobotSkeleton"]["anchors"]
worst = (0, "")
joins = [("Core", "collar_to_shoulder_%s", "Arms", "arm_line_%s", "seam_shoulder_%s"),
         ("Core", "collar_to_shoulder_%s", "Arms", "arm_spiral_%s", "seam_shoulder_%s"),
         ("Core", "collar_to_shoulder_%s", "Arms", "upperarm_coil_%s", "seam_shoulder_%s"),
         ("Core", "waist_to_hip_%s", "Legs", "leg_line_%s", "seam_hip_%s"),
         ("Core", "waist_to_hip_%s", "Legs", "leg_spiral_%s", "seam_hip_%s"),
         ("Core", "neck_to_collar_%s", "Head", "head_to_collar_%s", "seam_neck_%s")]
gap_worst = (0, "")
for (cs, cp, ls, lp, sn), side in itertools.product(joins, ("l", "r")):
    for a, b in itertools.product(shells, shells):
        core = docs["SK_Shell%d_%s" % (a, cs)]["paths"][cp % side]
        limb = docs["SK_Shell%d_%s" % (b, ls)]["paths"][lp % side]
        s = seams[sn % side]["p"]
        if core.get("from_seam") == sn % side:          # the neck: the head's path ends where the core's starts
            assert limb["to_seam"] == sn % side
            ce, le = core["points"][0]["p"], limb["points"][-1]["p"]
        else:
            assert core["to_seam"] == sn % side and limb["from_seam"] == sn % side
            ce, le = core["points"][-1]["p"], limb["points"][0]["p"]
        e1 = dist(ce, s); e2 = dist(le, s)
        gap = dist(ce, le)
        tag = "%s core shell %d + %s shell %d, %s: core end %.2f, limb start %.2f, gap %.2f" % (sn % side, a, ls, b, lp % side, e1, e2, gap)
        if max(e1, e2) > worst[0]: worst = (max(e1, e2), tag)
        if gap > gap_worst[0]: gap_worst = (gap, tag)
        if max(e1, e2) > SEAM_TOL: fail("seam join too far:", tag)
print("worst seam-join distance to the seam anchor: %.2f cm  (%s)" % worst)
print("worst gap between the two joined path ends: %.2f cm  (%s)" % gap_worst)

for n in notes: print("NOTE", n)
for f in fails: print("FAIL", f)
print("RESULT:", "FAIL (%d)" % len(fails) if fails else "PASS")
sys.exit(1 if fails else 0)
