# The graft-anchor vocabulary: data only, read by make_anchors.py. Edit freely, then re-run the tool
# for base, 1 and 2 (and tools/check_anchors.py).
#
# An ANCHOR is placed by casting a ray at the part's wrap (a closed skin round the part) from outside,
# along -dir, toward a target point on a bone's axis:
#   bone  the bone; t = 0..1 from its head to its tail (the tails are the child bones' heads)
#   dir   where the ray comes from, in robot terms: front (-Y), back (+Y), left (+X), right (-X), up, down,
#         outer / inner (away from / toward the body's centre line). A word, or {word: weight, ...}.
#   perp  True: dir is first made square to the bone (for limbs, so the ray meets the limb side-on)
#   off   {word: cm} shifts the target point itself (same words as dir)
#   up    the anchor's 'up' tangent: 'z' (toward the robot's up, the default for Head and Core),
#         'limb' (along the limb toward the body, the default for Arms and Legs), or a word for a
#         fixed direction used where the surface faces that way (e.g. the top of the head)
#   sided True: the entry is written for the robot's LEFT side (name and bone end in _l); its _r
#         mirror is made automatically. 'outer' then means +X.
#
# A PATH is a line across the wrap through waypoints. A waypoint is an anchor name, '@seam_name'
# (the wrap point nearest that seam anchor of SK_RobotSkeleton), or an inline dict placed like an anchor.
#   ring  {bone, t, n}: a closed ring of n waypoints round the bone's axis at t (starts at the front,
#         goes toward the left)
#   closed True for rings (the line ends where it starts)
#   kind  'line' | 'ring' | 'spiral' (only used to colour the review renders)
#   sided True: written for the left side; the _r mirror swaps every _l/_r and mirrors the inline dirs


def A(bone, t, dir, **kw):
    d = dict(bone=bone, t=t, dir=dir); d.update(kw); return d


ANCHORS = {
    "Head": {
        "head_top":    A("head", 0.80, "up", up="front"),               # crown, straight above the skull
        "head_front":  A("head", 0.76, "front"),                         # the brow, above the visor/eyes/goggles
        "head_back":   A("head", 0.50, "back"),                          # the back of the skull
        "head_side_l": A("head", 0.45, "outer", sided=True),             # over the left ear
        "jaw_front":   A("head", 0.10, {"front": 1, "down": 0.35}),      # the chin / jaw front
    },
    "Core": {
        "chest_front":  A("spine_02", 0.62, "front"),                    # middle of the chest plate
        "chest_side_l": A("spine_02", 0.45, "outer", sided=True),        # the ribs under the left arm
        "collar_l":     A("clavicle_l", 0.40, {"up": 1, "back": 0.1}, sided=True, up="back"),   # top of the trapezius
        "back_upper":   A("spine_02", 0.72, "back"),                     # between the shoulder blades
        "back_lower":   A("spine_02", 0.18, "back"),                     # small of the back
        "belly_front":  A("spine_02", 0.12, "front"),                    # the stomach, above the waist
        "waist_side_l": A("spine_01", 0.50, "outer", sided=True),        # the side of the waist
        "hip_front":    A("pelvis", 0.20, "front"),                      # the front of the pelvis, below the belt
        "hip_back":     A("pelvis", 0.20, "back"),                       # the back of the pelvis
    },
    "Arms": {
        "shoulder_top_l":   A("upperarm_l", 0.05, "up", sided=True, up="front"),       # top of the shoulder cap
        "shoulder_outer_l": A("upperarm_l", 0.10, {"outer": 1, "up": 0.2}, perp=True, sided=True),   # the deltoid's side
        "upperarm_outer_l": A("upperarm_l", 0.50, "outer", perp=True, sided=True),     # the upper arm's outside
        "upperarm_front_l": A("upperarm_l", 0.50, "front", perp=True, sided=True),     # the bicep
        "elbow_outer_l":    A("lowerarm_l", 0.02, "outer", perp=True, sided=True),     # the elbow's outside
        "forearm_outer_l":  A("lowerarm_l", 0.45, "outer", perp=True, sided=True),     # the forearm's outside
        "forearm_inner_l":  A("lowerarm_l", 0.45, "inner", perp=True, sided=True),     # the forearm's inside
        "wrist_outer_l":    A("lowerarm_l", 0.88, "outer", perp=True, sided=True),     # the wrist's outside
    },
    "Legs": {
        "thigh_front_l":  A("thigh_l", 0.40, "front", perp=True, sided=True),          # the quad
        "thigh_outer_l":  A("thigh_l", 0.40, "outer", perp=True, sided=True),          # the outside of the thigh
        "knee_front_l":   A("calf_l", 0.00, "front", perp=True, sided=True),           # the kneecap
        "shin_front_l":   A("calf_l", 0.40, "front", perp=True, sided=True),           # the shin
        "shin_outer_l":   A("calf_l", 0.40, "outer", perp=True, sided=True),           # the outside of the shin
        "calf_back_l":    A("calf_l", 0.30, "back", perp=True, sided=True),            # the calf muscle
        "ankle_outer_l":  A("calf_l", 0.90, "outer", perp=True, sided=True),           # the outer ankle
        "foot_top_l":     A("foot_l", 0.55, {"up": 1, "front": 0.25}, sided=True),     # the top of the foot
    },
}

# Seams live on SK_RobotSkeleton (the same whatever parts are worn): where two slots' paths meet.
# bone: the joint is this bone's head. n: the outward direction (away from the body's centre).
# off: the seam point is the joint moved by this much (cm, robot words), a fixed amount, so it stays
#   independent of the parts worn. The joint centres sit 3.5-6 cm inside every wrap, too deep for paths
#   to meet there; these offsets put the seams where the Core wrap and the Arms/Legs wraps of all three
#   shells come within ~3 cm of each other (fitted once over the wraps, see make_anchors.py --dump).
SEAMS = {
    "seam_neck":       dict(bone="neck_01", n="up"),                     # no paths cross it yet
    "seam_waist":      dict(bone="spine_01", n="front"),                 # inside Core: a reference only
    "seam_shoulder_l": dict(bone="upperarm_l", n="outer", off={"outer": 6.0}, sided=True),          # the shoulder's outside
    "seam_hip_l":      dict(bone="thigh_l", n="outer", off={"outer": 3.0, "up": 2.0}, sided=True),   # the hip's outside
}


def P(via=None, **kw):
    d = dict(via=via or [], closed=False, kind="line", sided=False, from_seam=None, to_seam=None)
    d.update(kw); return d


def W(bone, t, dir, **kw):
    """An inline waypoint (placed like an anchor)."""
    return A(bone, t, dir, **kw)


ARM_SPIRAL = ["@seam_shoulder_l", "shoulder_outer_l",
              W("upperarm_l", 0.30, "front", perp=True), W("upperarm_l", 0.50, "inner", perp=True),
              W("upperarm_l", 0.70, "back", perp=True), "elbow_outer_l",
              W("lowerarm_l", 0.25, "front", perp=True), W("lowerarm_l", 0.45, "inner", perp=True),
              W("lowerarm_l", 0.65, "back", perp=True), "wrist_outer_l"]
LEG_SPIRAL = ["@seam_hip_l", W("thigh_l", 0.12, "outer", perp=True),
              W("thigh_l", 0.32, "front", perp=True), W("thigh_l", 0.52, "inner", perp=True),
              W("thigh_l", 0.72, "back", perp=True), W("thigh_l", 0.95, "outer", perp=True),
              W("calf_l", 0.25, "front", perp=True), W("calf_l", 0.45, "inner", perp=True),
              W("calf_l", 0.65, "back", perp=True), "ankle_outer_l"]



def COIL(bone, t0, t1, turns, start=0):
    """Waypoints winding round a limb bone `turns` times from t0 to t1, a quarter turn apart (front, inner,
    back, outer, as the spirals go), starting at the start'th of those."""
    seq = ["front", "inner", "back", "outer"]
    n = int(round(turns * 4)) + 1
    return [W(bone, t0 + (t1 - t0) * k / (n - 1), seq[(start + k) % 4], perp=True) for k in range(n)]


# Tighter coils than the spirals, one limb segment each (VoltVine's vines wind 1.5 to 2.5 times round each).
UPPERARM_COIL = ["@seam_shoulder_l", "shoulder_outer_l"] + COIL("upperarm_l", 0.24, 0.92, 1.5)
FOREARM_COIL = COIL("lowerarm_l", 0.06, 0.9, 2.5, start=3)
SHIN_COIL = COIL("calf_l", 0.08, 0.9, 2.0, start=0)

PATHS = {
    "Head": {
        "head_ring":  P(ring=dict(bone="head", t=0.76, n=8), closed=True, kind="ring"),   # round the head at brow height
        "head_crest": P(["head_front", "head_top", "head_back"]),                       # over the crown, front to back
    },
    "Core": {
        "waist_ring": P(ring=dict(bone="spine_01", t=0.5, n=8), closed=True, kind="ring"),   # round the waist
        "chest_ring": P(ring=dict(bone="spine_02", t=0.45, n=8), closed=True, kind="ring"),  # round the chest, under the arms
        "sash_l":     P(["collar_l", "chest_front", W("spine_02", 0.12, {"front": 1, "inner": 0.9}), "waist_side_r"],
                      sided=True),                       # a bandolier, left shoulder to right hip (the waypoint fixes its route)
        "spine_line": P(["back_upper", W("spine_02", 0.55, "back"), W("spine_02", 0.35, "back"), "back_lower",
                        "hip_back"]),          # down the spine (mid waypoints keep it on the centre line over back gear)
        "collar_to_shoulder_l": P(["collar_l", "@seam_shoulder_l"], sided=True, to_seam="seam_shoulder_l"),
        "waist_to_hip_l":       P(["waist_side_l", "@seam_hip_l"], sided=True, to_seam="seam_hip_l"),   # meets leg_line
        # sash_l's twin across the back: left shoulder, diagonally down the back, right hip.
        "sash_back_l": P(["collar_l", W("spine_02", 0.55, {"back": 1, "inner": 0.3}), W("spine_02", 0.25, {"back": 1, "inner": 0.8}),
                          W("spine_02", 0.04, {"back": 1, "inner": 1.4}), "waist_side_r"], sided=True),
        # A belt round the waist that leans: about 6 cm higher on its own side (the left here) than the other,
        # just under the waist line in front and behind. Starts at the front, goes round the left first.
        "belt_lean_l": P([W("spine_01", 0.0, "front", off={"up": -0.5}),
                          W("spine_01", 0.0, {"front": 1, "outer": 1}, off={"up": 1.8}),
                          W("spine_01", 0.0, "outer", off={"up": 4.0}),
                          W("spine_01", 0.0, {"back": 1, "outer": 1}, off={"up": 1.5}),
                          W("spine_01", 0.0, "back", off={"up": -1.0}),
                          W("spine_01", 0.0, {"back": 1, "inner": 1}, off={"up": -2.0}),
                          W("spine_01", 0.0, "inner", off={"up": -2.5}),
                          W("spine_01", 0.0, {"front": 1, "inner": 1}, off={"up": -2.0})],
                         closed=True, kind="ring", sided=True),
    },
    "Arms": {
        "arm_line_l":   P(["@seam_shoulder_l", "shoulder_outer_l", "elbow_outer_l", "wrist_outer_l"], sided=True,
                          from_seam="seam_shoulder_l"),
        "arm_spiral_l": P(ARM_SPIRAL, sided=True, kind="spiral", from_seam="seam_shoulder_l"),
        "wrist_ring_l": P(ring=dict(bone="lowerarm_l", t=0.88, n=8), closed=True, kind="ring", sided=True),
        "upperarm_coil_l": P(UPPERARM_COIL, sided=True, kind="spiral", from_seam="seam_shoulder_l"),  # 1.5 turns to the elbow
        "forearm_coil_l":  P(FOREARM_COIL, sided=True, kind="spiral"),                                   # 2.5 turns elbow to wrist
    },
    "Legs": {
        "leg_line_l":   P(["@seam_hip_l", "thigh_outer_l", "shin_outer_l", "ankle_outer_l"], sided=True,
                          from_seam="seam_hip_l"),
        "leg_spiral_l": P(LEG_SPIRAL, sided=True, kind="spiral", from_seam="seam_hip_l"),
        "thigh_ring_l": P(ring=dict(bone="thigh_l", t=0.5, n=8), closed=True, kind="ring", sided=True),
        "ankle_ring_l": P(ring=dict(bone="calf_l", t=0.9, n=8), closed=True, kind="ring", sided=True),
        "shin_coil_l":  P(SHIN_COIL, sided=True, kind="spiral"),                                         # 2 turns knee to ankle
    },
}

# The wrap: voxel size, how wide a gap it closes (cm), and smoothing.
WRAP = dict(voxel=1.25, close=3.0, smooth_iters=12, smooth_lambda=0.6)
STEP = 2.0          # path resampling distance, cm
