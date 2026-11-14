# Bakes the body animations that replace the Godot (UAL) clips: Unreal-native clips from the stock
# mannequin set, Lyra and three Fab packs, copied onto the project's mannequin skeleton as
# /Game/WWO/Anims/Body/A_<Name>, where <Name> is the clip name the game code already plays.
# UWWOBodyComponent::FindAnim looks in Body first and falls back to /Game/WWO/Anims/UAL, so a clip
# that is not listed here (Death01, Hit_Knockback) keeps its Godot version.
#
# Each entry is trimmed to a section of its source and time-scaled so it is a drop-in replacement:
# the same length or the same ground speed as the clip it replaces, so no game code is retuned.
# Roots are locked (the game moves the body itself), and the mannequin keeps its own bone lengths.
#
# Run headless:
#   UnrealEditor-Cmd <uproject> -run=pythonscript -script=<abs path to this file> -unattended -nullrhi
# Sources that must be in the project: Content/FreeAnimationLibrary, Content/OneHand_Throw_Animation,
# Content/MageBundleSamples (Fab), and the Lyra crouch clips with Lyra's skeleton at their Lyra
# paths under Content/Characters/Heroes/Mannequin (copy them from a Lyra project; they are only
# read here and can be deleted afterwards). Writes a report next to this file (bake_body_anims.log).
import os
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
FOLDER = "/Game/WWO/Anims/Body"
FPS = 30
MANNY = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"
STOCK = "/Game/Characters/Mannequins/Anims/"
LYRA = "/Game/Characters/Heroes/Mannequin/Animations/Locomotion/"
THROW = "/Game/OneHand_Throw_Animation/Animations/Grenade/"
MAGE = "/Game/MageBundleSamples/Animations/"
FAL = "/Game/FreeAnimationLibrary/Animations/"
CAST = MAGE + "02_Ability/MANNY/AS_Mage_Ability_Spell_HighRtLight_02"

# name: source, section start, section end (None = clip end), baked length (None = section length)
#   speed=(natural, wanted): ground speed of the source in m/s and the speed the game's playback
#     rate assumes for this clip (WalkAnimSpeed and friends); sets the baked length.
#   hold=t: a 2 s loop that sways gently around the pose at t (ping-pong over +-0.12 s).
#   pose=t: a short still clip of the pose at t.
#   onto="Idle": the section's motion from its first frame, added on top of the baked Idle pose
#     (for flinches that are authored as additive on a rifle pose).
#   low="start" | "hold" | "exit": split around the lowest hip point (the slide).
ANIMS = {
    # Everyone with a gun (players, bots, allies, the Soldier) stands and walks with the rifle up:
    # the rifle idle and walk, and (below) the rifle's aimed pose over the upper body the whole time.
    # The gunless (the Swarm, the Tank, the menu preview) use the *_Unarmed ones.
    "Idle":        dict(src=STOCK + "Rifle/MF_Rifle_Idle_ADS"),
    "Walk":        dict(src=STOCK + "Rifle/Walk/MF_Rifle_Walk_Fwd", speed=(3.05, 1.6)),
    "Idle_Unarmed": dict(src=STOCK + "Unarmed/MM_Idle"),
    "Walk_Unarmed": dict(src=STOCK + "Unarmed/Walk/MF_Unarmed_Walk_Fwd", speed=(3.0, 1.6)),
    # Running with a gun (players, bots and allies always carry one): the rifle jog, for both the
    # jog and the sprint. There is no sprint clip, and the jog only covers 6.2 m/s against a 10 m/s
    # sprint: played to match the ground it spins the legs at 1.6x, so the sprint is baked to play
    # at 1.2x at full sprint instead (speed's second number is what the game's playback rate assumes;
    # 5.24 here against the game's 7) and the feet slide a little.
    "Jog_Fwd":     dict(src=STOCK + "Rifle/Jog/MF_Rifle_Jog_Fwd", speed=(6.24, 4.5)),
    "Sprint":      dict(src=STOCK + "Rifle/Jog/MF_Rifle_Jog_Fwd", speed=(6.24, 5.24)),
    "Sprint_Unarmed": dict(src=STOCK + "Unarmed/Jog/MF_Unarmed_Jog_Fwd", speed=(6.0, 7.0)),   # the Swarm's run: no gun
    # Crouched, the rifle crouch carries the gun itself (the aimed upper body is off then: laid over a
    # crouch it replaces the spine that balances the tipped hips, and the body folds forward).
    "Crouch_Idle": dict(src=LYRA + "Rifle/MM_Rifle_Crouch_Idle"),
    "Crouch_Fwd":  dict(src=LYRA + "Rifle/MM_Rifle_Crouch_Walk_Fwd", speed=(3.0, 1.5)),
    "Jump_Start":  dict(src=STOCK + "Unarmed/Jump/MM_Jump"),
    "Jump":        dict(src=STOCK + "Unarmed/Jump/MM_Fall_Loop"),
    "Jump_Land":   dict(src=STOCK + "Unarmed/Jump/MM_Land"),
    "Punch_Jab":   dict(src=STOCK + "Unarmed/Attack/MM_Attack_01", t0=0.15, length=0.87),
    # The punch and the throw also drive the first-person arms (WWOWeaponComponent), which play fixed
    # moments of the clip: the punch chambered at 0.2 s and landed at 0.264 s; the throw cocked at
    # 0.2 s and released at 0.363 s. The sources are time-warped so their own moments land there.
    "Melee_Hook":  dict(src=STOCK + "Unarmed/Attack/MM_Attack_01", strike=(0.10, 0.30),   # a right-hand punch; Attack_02 leads with the left
                        warp=lambda back, hit, end: [(0.0, max(back - 0.2, 0.0)), (0.2, back), (0.264, hit), (0.3, hit + 0.04), (0.4667, hit + 0.25)]),
    "Hit_Chest":   dict(src=STOCK + "Rifle/HitReact/MM_HitReact_Front_Lgt_01", length=0.40, onto="Idle"),
    "Hit_Head":    dict(src=STOCK + "Rifle/HitReact/MM_HitReact_Front_Med_01", length=0.45, onto="Idle"),
    "Pistol_Aim_Neutral": dict(src=STOCK + "Rifle/MF_Rifle_Idle_ADS", pose=0.0),   # (the name is the Godot one: it is the gun-up upper body)
    "Aim_Pistol":  dict(src=STOCK + "Pistol/MF_Pistol_Idle_ADS", pose=0.0),          # the same for a hand cannon: both hands on the grip
    "OverhandThrow": dict(src=THROW + "Grenade4/AS_Grenade_Throw4", strike=(0.30, 1.10),
                          warp=lambda back, hit, end: [(0.0, back - 0.25), (0.05, back - 0.2), (0.2, back), (0.363, hit), (0.42, hit + 0.06), (1.3333, end)]),
    # Ult cast, one mage spell cut in three: arm up, held high, thrust forward.
    "Spell_Simple_Enter": dict(src=CAST, t0=0.60, t1=1.10, length=0.53),
    "Spell_Simple_Idle":  dict(src=CAST, hold=1.30),
    "Spell_Simple_Shoot": dict(src=CAST, t0=1.60, t1=2.20, length=0.50),
    "Slide_Start": dict(src=FAL + "Slide/anim_Slide_L", low="start"),
    "Slide":       dict(src=FAL + "Slide/anim_Slide_L", low="hold"),
    "Slide_Exit":  dict(src=FAL + "Slide/anim_Slide_L", low="exit"),
}
ORDER = ["Idle"] + [n for n in ANIMS if n != "Idle"]

REPORT = []
lib = unreal.AnimationLibrary
ape = unreal.AnimPoseExtensions
tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary


def qmul(a, b):   # Hamilton product, (x, y, z, w): rotate by b, then a
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def qinv(q):
    return (-q[0], -q[1], -q[2], q[3])


def qnorm(q):
    n = sum(c * c for c in q) ** 0.5 or 1.0
    return tuple(c / n for c in q)


def tq(x):
    return ((x.translation.x, x.translation.y, x.translation.z), (x.rotation.x, x.rotation.y, x.rotation.z, x.rotation.w))


class Source:
    def __init__(self, path, names):
        self.seq = unreal.load_asset(path)
        if not self.seq:
            raise RuntimeError("missing source " + path)
        have = set(str(n) for n in lib.get_animation_track_names(self.seq))
        self.names = [n for n in names if n in have]
        self.length = self.seq.get_play_length()

    def at(self, t):
        t = min(max(t, 0.0), self.length)
        poses = lib.get_bone_poses_for_time(self.seq, self.names, t, False)
        return {n: tq(p) for n, p in zip(self.names, poses)}

    def pelvis_z(self, t):
        return self.at(t)["pelvis"][0][2]

    def mesh(self, t, bone):
        """A bone's position in mesh space at t (cm; the mannequin faces +y, up +z)."""
        out = None
        for b in lib.find_bone_path_to_root(self.seq, bone):     # bone first, root last
            l = lib.get_bone_pose_for_time(self.seq, b, min(max(t, 0.0), self.length), False)
            out = l if out is None else unreal.MathLibrary.compose_transforms(out, l)
        return out.translation

    def strike(self, a, b):
        """Within [a, b]: when the right hand is furthest back, then when it has come furthest forward
        of the head within the next 0.45 s (the punch landing, the throw leaving the hand)."""
        ts = [a + i / 120.0 for i in range(int((min(b + 0.45, self.length) - a) * 120) + 1)]
        fwd = {t: self.mesh(t, "hand_r").y - self.mesh(t, "head").y for t in ts}
        back = min((t for t in ts if t <= b), key=lambda t: fwd[t])
        after = [t for t in ts if back < t <= back + 0.45] or [back]
        peak = max(after, key=lambda t: fwd[t])
        # the hit is where it first gets within 10% of that reach, not the end of the follow-through
        hit = next(t for t in after if fwd[t] >= fwd[back] + 0.9 * (fwd[peak] - fwd[back]))
        return back, hit


def bake():
    skm = unreal.load_asset(MANNY)
    skeleton = skm.skeleton
    ref = ape.get_reference_pose(skeleton)
    names = [str(n) for n in ape.get_bone_names(ref)]
    rest = {n: tq(ape.get_ref_bone_pose(ref, n, unreal.AnimPoseSpaces.LOCAL)) for n in names}
    sources, baked_idle = {}, None
    for name in ORDER:
        e = ANIMS[name]
        if e["src"] not in sources:
            try:
                sources[e["src"]] = Source(e["src"], names)
            except RuntimeError:
                sources[e["src"]] = None
        if sources[e["src"]] is None:       # its source pack is not in the project: the existing bake stays
            REPORT.append("A_%s: source %s is not in the project, left as it is" % (name, e["src"]))
            continue
        src = sources[e["src"]]
        t0, t1 = e.get("t0", 0.0), e.get("t1") or src.length
        times = None
        if "low" in e:
            # The slide: find when the hips are lowest, then the way down, a hold there, the way up.
            steps = [i * src.length / 60.0 for i in range(61)]
            tmin = min(steps, key=src.pelvis_z)
            if e["low"] == "start":
                t0, t1 = 0.0, tmin
            elif e["low"] == "exit":
                t0, t1 = tmin, src.length
            else:
                e = dict(e, hold=tmin)
            REPORT.append("  %s: hips lowest at %.2f s of %.2f (%.0f cm)" % (name, tmin, src.length, src.pelvis_z(tmin)))
        if "warp" in e:
            back, hit = src.strike(*e["strike"])
            pts = e["warp"](back, hit, src.length)
            frames = int(round(pts[-1][0] * FPS))
            times = []
            for f in range(frames + 1):
                t = min(f / FPS, pts[-1][0])
                i = max(k for k in range(len(pts) - 1) if pts[k][0] <= t)
                (o0, s0), (o1, s1) = pts[i], pts[i + 1]
                times.append(s0 + (s1 - s0) * (t - o0) / (o1 - o0))
            REPORT.append("  %s: hand back at %.2f s, strike at %.2f s of the source" % (name, back, hit))
        elif "hold" in e:
            frames = 2 * FPS
            c = e["hold"]
            times = [c - 0.12 + 0.24 * (1.0 - abs(1.0 - 2.0 * f / frames)) for f in range(frames + 1)]   # out and back
        elif "pose" in e:
            frames = 5
            times = [e["pose"]] * (frames + 1)
        else:
            length = t1 - t0
            if "speed" in e:
                length *= e["speed"][0] / e["speed"][1]
            length = e.get("length", length)
            frames = max(int(round(length * FPS)), 1)
            times = [t0 + (t1 - t0) * f / frames for f in range(frames + 1)]
        first = src.at(times[0])
        keys = {n: ([], []) for n in src.names}
        for t in times:
            pose = src.at(t)
            for n in src.names:
                p, q = pose[n]
                if e.get("onto"):
                    bp, bq = baked_idle.get(n, rest[n])
                    fp, fq = first[n]
                    q = qnorm(qmul(qmul(q, qinv(fq)), bq))
                    p = tuple(b + (x - f) for b, x, f in zip(bp, p, fp)) if n == "pelvis" else rest[n][0]
                elif n == "root":
                    p, q = rest[n]            # locked: the game moves the body
                elif n != "pelvis":
                    p = rest[n][0]            # the mannequin's own bone lengths
                keys[n][0].append(unreal.Vector(*p))
                keys[n][1].append(unreal.Quat(*q))
        if name == "Idle":
            baked_idle = {n: (tuple([keys[n][0][0].x, keys[n][0][0].y, keys[n][0][0].z]),
                              (keys[n][1][0].x, keys[n][1][0].y, keys[n][1][0].z, keys[n][1][0].w)) for n in src.names}
        asset = "A_" + name
        path = FOLDER + "/" + asset
        if eal.does_asset_exist(path):
            seq = eal.load_asset(path)
        else:
            factory = unreal.AnimSequenceFactory()
            factory.set_editor_property("target_skeleton", skeleton)
            seq = tools.create_asset(asset, FOLDER, unreal.AnimSequence, factory)
        ctrl = seq.controller
        ctrl.open_bracket(unreal.Text("Bake " + name), False)
        ctrl.remove_all_bone_tracks(False)
        ctrl.set_frame_rate(unreal.FrameRate(FPS, 1), False)
        ctrl.set_number_of_frames(unreal.FrameNumber(frames), False)
        ones = [unreal.Vector(1, 1, 1)] * (frames + 1)
        for n in src.names:
            ctrl.add_bone_curve(n, False)
            ctrl.set_bone_track_keys(n, keys[n][0], keys[n][1], ones, False)
        ctrl.close_bracket(False)
        seq.set_editor_property("enable_root_motion", False)
        eal.save_asset(path, only_if_is_dirty=False)
        REPORT.append("%s: %s [%.2f-%.2f s] -> %.2f s, %d bones" % (asset, e["src"].split("/")[-1], times[0], times[-1], frames / FPS, len(src.names)))


def arm_paths():
    """Source/WhatWasOurs/Player/WWOFPArmPaths.h: where the right arm's joints are, relative to the
    eyes, through the baked punch and throw (the first-person arms hang off the shoulder path)."""
    bones = ["upperarm_r", "lowerarm_r", "hand_r", "middle_01_r"]
    eye_offset = (0.0, 0.07, 0.09)          # Godot fp_arm.gd EYE_OFFSET: above and ahead of the head bone

    def rows(name, end):
        src = Source(FOLDER + "/A_" + name, bones + ["head"])
        out = []
        for k in range(int(round(end * 60)) + 1):
            t = min(k / 60.0, end)
            g = lambda v: (v.x / 100.0, v.z / 100.0, v.y / 100.0)      # mesh space -> Godot model space
            eye = tuple(a + b for a, b in zip(g(src.mesh(t, "head")), eye_offset))
            out.append([tuple(a - b for a, b in zip(g(src.mesh(t, bone)), eye)) for bone in bones])
        return out, src.length

    def table(name, r):
        s = "\tinline const FVector %s[][4] = {\n" % name
        for row in r:
            s += "\t\t{ " + ", ".join("FVector(%.4ff, %.4ff, %.4ff)" % v for v in row) + " },\n"
        return s + "\t};\n"

    hook, hook_length = rows("Melee_Hook", unreal.load_asset(FOLDER + "/A_Melee_Hook").get_play_length())
    throw, _ = rows("OverhandThrow", 0.42)
    header = """#pragma once

// The first-person arms' motion, from the baked body clips /Game/WWO/Anims/Body/A_Melee_Hook and
// A_OverhandThrow (Unreal-native sources, time-warped to the old clips' key moments).
// Generated by Tools/bake_body_anims.py; don't edit by hand.
// Each row is one 60 Hz frame: the right arm's upperarm_r (shoulder), lowerarm_r (elbow),
// hand_r (wrist) and middle_01_r (knuckles) in Godot model space (meters, y up, the model facing
// +z), relative to the eyes (the head bone's position that frame + fp_arm.gd's EYE_OFFSET).

#include "CoreMinimal.h"

namespace WWOArmPaths
{
\tconstexpr float FrameRate = 60.f;
\tconstexpr float MeleeHookLength = %.6ff; // seconds
%s
\t// OverhandThrow, first 0.42 s (the throw only uses that much).
%s}
""" % (hook_length, table("MeleeHook", hook), table("OverhandThrow", throw))
    open(os.path.join(HERE, "..", "Source", "WhatWasOurs", "Player", "WWOFPArmPaths.h"), "w").write(header)
    REPORT.append("WWOFPArmPaths.h: punch %d rows (%.3f s), throw %d rows" % (len(hook), hook_length, len(throw)))


try:
    bake()
    arm_paths()
except Exception:
    import traceback
    REPORT.append("EXCEPTION: " + traceback.format_exc())
open(os.path.join(HERE, "bake_body_anims.log"), "w").write("\n".join(REPORT) + "\n")
