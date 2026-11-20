# Shell 1, "sleek": graded hard-surface armour, a visor slit, a crest, round joints, thin white light strips.
# Reference: reference/shell1_sleek.png
#
#   /Applications/Blender.app/Contents/MacOS/Blender -b \
#     base/robot_frame_v2_rigged.blend \
#     -P tools/make_shell1.py
# Writes shell1_claude.blend and renders/shell1_*.png.
import bpy, sys, os
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
from shell_kit import Surf, Form, tube, patch, panel, strip, cyl, joint, ball, hull

OUT = (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "")
P, S, F = "Primary", "Secondary", "Frame"
E = (0.16, 0.16, 0.006)        # the usual plate edge chamfer

# joints of the skeleton (metres)
SHO, ELB, WRI = (0.232, 0.04, 1.528), (0.34, 0.04, 1.26), (0.449, 0.04, 0.992)
HIP, KNEE, ANK = (0.12, 0.0, 1.032), (0.137, 0.022, 0.629), (0.157, 0.046, 0.166)


def head():
    b, s = "head", "head"
    k, kw = 0.94 * 0.95, 0.94                   # head height and width against the first passes
    top = 1.615 + 0.28 * k
    h = Surf((0, -0.012, top), (0, -0.012, 1.60), [(r[0] * k,) + tuple(v * kw for v in r[1:5]) + tuple(r[5:]) for r in (
        (0.000, 0.018, 0.018, 0.018, 0.032),
        (0.009, 0.040, 0.040, 0.044, 0.070),
        (0.025, 0.060, 0.060, 0.066, 0.104),
        (0.070, 0.085, 0.085, 0.099, 0.158),
        (0.115, 0.092, 0.092, 0.110, 0.166),
        (0.155, 0.092, 0.092, 0.113, 0.136),
        (0.200, 0.092, 0.092, 0.110, 0.094, 0.8),
        # the jaw: the same width from the ears down to a flat underside; the back cuts in to the neck
        (0.235, 0.0814, 0.0814, 0.107, 0.056, 0.62),
        (0.262, 0.0708, 0.0708, 0.102, 0.024, 0.55),
        (0.280, 0.0708, 0.0708, 0.101, 0.014, 0.55))], pw=0.86)
    tube("skull", h, 0.0, 0.28 * k, P, b, s, nt=34, nth=72, bevel=0.0008)
    T = lambda line: [(a, t * k) for a, t in line]
    # all one piece: only a shallow brow line over the visor, a shallow mask plane under it and a chin plane
    patch("brow", h, T([(30, 0.112), (90, 0.126), (150, 0.112)]), T([(22, 0.139), (90, 0.151), (158, 0.139)]), P, b, s,
          nu=40, nv=6, lift=0.0035, thick=0.006, edge=(0.08, 0.45, 0.003))
    patch("visor", h, T([(20, 0.143), (90, 0.155), (160, 0.143)]), T([(20, 0.151), (90, 0.163), (160, 0.151)]), "Glow", b, s,
          nu=40, nv=2, lift=0.0025, thick=0.006, bevel=0.0)
    patch("mask", h, T([(34, 0.158), (90, 0.169), (146, 0.158)]), T([(58, 0.236), (90, 0.25), (122, 0.236)]), P, b, s,
          nu=36, nv=12, lift=0.003, thick=0.006, edge=(0.14, 0.2, 0.003))
    patch("chin", h, T([(70, 0.222), (110, 0.222)]), T([(76, 0.274), (104, 0.274)]), P, b, s,
          nu=12, nv=8, lift=0.004, thick=0.006, edge=(0.2, 0.2, 0.003))
    patch("jaw", h, T([(8, 0.215), (60, 0.228)]), T([(10, 0.272), (66, 0.272)]), P, b, s,
          nu=16, nv=6, lift=0.003, thick=0.006, edge=(0.12, 0.25, 0.003), mirror=True)
    for r, hh, role in ((0.039, 0.010, P), (0.029, 0.016, F), (0.018, 0.021, S), (0.008, 0.025, F)):
        cyl("ear", (0.0855, 0.0, top - 0.158 * k), (1, 0, 0), r, hh, role, b, s, mirror=True, bevel=0.002)
    n = Surf((0, 0.022, 1.675), (0, 0.036, 1.56), [(0, 0.043, 0.043, 0.043, 0.043), (0.115, 0.05, 0.05, 0.048, 0.05)], pw=1.0)
    tube("neck", n, 0.0, 0.115, F, "neck_01", s, nt=4, nth=28)
    for t in (0.03, 0.055, 0.08):
        tube("neck_ring", n, t, t + 0.014, S, "neck_01", s, nt=1, nth=28, lift=0.005, bevel=0.002)


def core():
    s = "core"
    # ---- chest: a wedge, wide and deep at the pecs, cut in hard to the waist
    b = "spine_02"
    c = Form((0, 0.02, 1.60), (0, 0.02, 1.20), [
        (0.00, 0.100, 0.100, 0.078, 0.080, dict(c=0.5)),
        (0.05, 0.180, 0.180, 0.112, 0.122),
        (0.13, 0.208, 0.208, 0.142, 0.140),
        (0.24, 0.180, 0.180, 0.128, 0.126),
        (0.32, 0.136, 0.136, 0.100, 0.100),
        (0.40, 0.108, 0.108, 0.084, 0.082)], c=[(0.5, 0.42), (0.5, 0.42), 0.3, 0.3], b=[0.03, 0.035, 0.03, 0.05], soft=8)
    tube("chest_under", c, 0.0, 0.40, F, b, s, nt=18, nth=56, lift=-0.008)
    patch("pec", c, [(18, 0.07), (52, 0.03), (86.5, 0.04)], [(18, 0.20), (50, 0.262), (86.5, 0.215)], P, b, s,
          nu=30, nv=18, edge=(0.1, 0.12, 0.007), mirror=True)
    patch("sternum", c, [(64, 0.062), (116, 0.062)], [(73, 0.185), (90, 0.238), (107, 0.185)], P, b, s,
          nu=22, nv=12, lift=0.009, thick=0.012, edge=(0.2, 0.16, 0.007))
    strip("chest_light", c, 89.0, 91.0, 0.115, 0.162, b, s, lift=0.012)
    patch("collar", c, [(34, -0.004), (82, 0.0)], [(22, 0.036), (84, 0.05)], S, b, s, nu=14, nv=5, lift=0.007, thick=0.012, edge=E, mirror=True)
    patch("lat", c, [(-42, 0.125), (12, 0.095)], [(-42, 0.30), (-10, 0.325), (14, 0.25)], P, b, s, nu=16, nv=12, edge=E, mirror=True)
    for i, t in enumerate((0.262, 0.312)):
        patch("rib%d" % i, c, [(30 + 4 * i, t - 0.016), (88.6, t + 0.016)], [(36 + 4 * i, t + 0.026), (88.6, t + 0.060)], S, b, s,
              nu=16, nv=5, lift=0.002, thick=0.01, edge=(0.12, 0.3, 0.004), mirror=True)
    patch("blade", c, [(274, 0.04), (300, 0.03), (340, 0.065)], [(277, 0.215), (298, 0.262), (340, 0.19)], P, b, s,
          nu=24, nv=14, edge=(0.1, 0.12, 0.007), mirror=True)
    patch("blade_top", c, [(286, 0.06), (330, 0.075)], [(292, 0.165), (326, 0.15)], S, b, s, nu=10, nv=8, lift=0.006, thick=0.009, edge=(0.2, 0.2, 0.004), mirror=True)
    for i, (t0, t1) in enumerate(((0.012, 0.085), (0.095, 0.17), (0.18, 0.255), (0.265, 0.33), (0.34, 0.385))):
        patch("spine%d" % i, c, [(261, t0), (270, t0 + 0.008), (279, t0)], [(262.5, t1), (270, t1 + 0.008), (277.5, t1)], S, b, s,
              nu=8, nv=6, lift=0.009, thick=0.013, edge=(0.3, 0.2, 0.004))
    strip("spine_light_a", c, 269.0, 271.0, 0.115, 0.15, b, s, lift=0.0125)
    strip("spine_light_b", c, 269.0, 271.0, 0.20, 0.235, b, s, lift=0.0125)
    for i, t in enumerate((0.275, 0.325)):
        patch("backrib%d" % i, c, [(284, t - 0.004), (330 - 4 * i, t - 0.02)], [(284, t + 0.036), (326 - 4 * i, t + 0.02)], S, b, s,
              nu=14, nv=4, lift=0.002, thick=0.01, edge=(0.12, 0.3, 0.004), mirror=True)
    ball("shoulder_ball", SHO, 0.052, F, "clavicle_l", s)
    for r, hh, role in ((0.046, 0.05, S), (0.034, 0.058, F), (0.02, 0.063, S)):
        cyl("shoulder_ring", (SHO[0] - 0.02, SHO[1], SHO[2] - 0.004), (0, 1, 0), r, hh, role, "clavicle_l", s)
    # ---- abdomen: stacked plates, each a pair meeting on the centre line
    b = "spine_01"
    a = Form((0, 0.015, 1.245), (0, 0.006, 1.08), [
        (0.00, 0.106, 0.106, 0.082, 0.076), (0.08, 0.092, 0.092, 0.074, 0.070), (0.165, 0.106, 0.106, 0.080, 0.076)], c=0.42, b=0.03)
    tube("abdomen", a, 0.0, 0.165, F, b, s, nt=8, nth=40, lift=-0.003)
    for i, t in enumerate((0.0, 0.046, 0.092)):
        patch("ab%d" % i, a, [(40, t), (88.6, t + 0.018)], [(44, t + 0.036), (88.6, t + 0.056)], S, b, s,
              nu=14, nv=5, lift=0.004, thick=0.01, edge=(0.12, 0.3, 0.004), mirror=True)
        patch("flank%d" % i, a, [(-36, t + 0.012), (30, t - 0.004)], [(-36, t + 0.046), (34, t + 0.03)], P if i == 1 else S, b, s,
              nu=12, nv=4, lift=0.003, thick=0.009, edge=(0.12, 0.3, 0.004), mirror=True)
    patch("lumbar", a, [(238, 0.0), (270, 0.012), (302, 0.0)], [(246, 0.145), (270, 0.16), (294, 0.145)], S, b, s, nu=14, nv=10, lift=0.004, thick=0.01, edge=E)
    # ---- pelvis: a belt that dips to a point in front, guards hanging over the hips
    b = "pelvis"
    p = Form((0, 0.008, 1.135), (0, 0.0, 0.93), [
        (0.000, 0.114, 0.114, 0.086, 0.084),
        (0.040, 0.130, 0.130, 0.096, 0.096),
        (0.095, 0.126, 0.126, 0.096, 0.098),
        (0.140, 0.080, 0.080, 0.084, 0.088),
        (0.205, 0.044, 0.044, 0.058, 0.060)], c=0.45, b=0.02)
    tube("pelvis_under", p, 0.0, 0.205, F, b, s, nt=14, nth=44, lift=-0.006)
    patch("belt_front", p, [(40, 0.0), (90, 0.022), (140, 0.0)], [(44, 0.058), (72, 0.08), (90, 0.102), (108, 0.08), (136, 0.058)], P, b, s,
          nu=30, nv=8, lift=0.009, thick=0.016, edge=(0.08, 0.2, 0.006))
    patch("belt_back", p, [(220, 0.0), (270, 0.014), (320, 0.0)], [(224, 0.06), (270, 0.088), (316, 0.06)], P, b, s, nu=30, nv=8, lift=0.009, thick=0.016, edge=(0.08, 0.2, 0.006))
    patch("hip_guard", p, [(-50, 0.0), (38, 0.0)], [(-44, 0.115), (-8, 0.185), (32, 0.10)], P, b, s,
          nu=22, nv=14, lift=0.02, thick=0.018, edge=(0.12, 0.14, 0.007), mirror=True)
    patch("tasset", p, [(40, 0.05), (80, 0.07)], [(46, 0.15), (64, 0.185), (79, 0.14)], P, b, s,
          nu=14, nv=10, lift=0.016, thick=0.014, edge=(0.14, 0.14, 0.006), mirror=True)
    patch("tasset_back", p, [(282, 0.06), (322, 0.05)], [(283, 0.14), (300, 0.18), (318, 0.15)], P, b, s,
          nu=14, nv=10, lift=0.016, thick=0.014, edge=(0.14, 0.14, 0.006), mirror=True)
    patch("cod", p, [(64, 0.092), (116, 0.092)], [(80, 0.2), (100, 0.2)], S, b, s, nu=14, nv=10, lift=0.005, thick=0.011, edge=E)
    patch("seat", p, [(236, 0.08), (304, 0.08)], [(256, 0.198), (284, 0.198)], S, b, s, nu=14, nv=10, lift=0.005, thick=0.011, edge=E)
    ball("hip_ball", HIP, 0.058, F, b, s, mirror=True)


def arms():
    s = "arms"
    b = "upperarm_l"
    A = np.array(SHO) + (-0.055, 0.0, 0.03)
    pa = Surf(A, A + np.array((0.76, 0.0, -0.65)), [
        (0.000, 0.060, 0.03, 0.072, 0.072),
        (0.050, 0.068, 0.03, 0.079, 0.079),
        (0.110, 0.064, 0.03, 0.073, 0.073),
        (0.155, 0.048, 0.03, 0.054, 0.054),
        (0.180, 0.026, 0.02, 0.028, 0.028)], out=(0.65, 0, 0.76), pw=1.0)
    patch("pauldron", pa, [(-96, 0.0), (96, 0.0)], [(-70, 0.178), (70, 0.178)], P, b, s,
          nu=40, nv=20, thick=0.013, edge=(0.07, 0.07, 0.006))
    patch("pauldron_ridge", pa, [(-9, 0.012), (9, 0.012)], [(-12, 0.15), (12, 0.15)], S, b, s,
          nu=6, nv=16, lift=0.003, thick=0.006, edge=(0.3, 0.08, 0.003))
    u = Form(SHO, ELB, [
        (0.030, 0.046, 0.044, 0.050, 0.050),
        (0.140, 0.058, 0.048, 0.060, 0.058),
        (0.240, 0.047, 0.042, 0.050, 0.050),
        (0.285, 0.040, 0.038, 0.042, 0.042)], c=0.4, b=[0.06, 0.06, 0.03, 0.04])
    tube("upperarm_under", u, 0.03, 0.285, F, b, s, nt=8, nth=32, lift=-0.007)
    patch("bicep", u, [(-118, 0.12), (0, 0.10), (118, 0.12)], [(-118, 0.225), (-40, 0.262), (40, 0.262), (118, 0.225)], P, b, s,
          nu=32, nv=10, edge=(0.08, 0.16, 0.006))
    patch("upperarm_in", u, [(136, 0.15), (224, 0.15)], [(146, 0.245), (214, 0.245)], S, b, s, nu=12, nv=6, edge=E)
    b = "lowerarm_l"
    joint("elbow", ELB, u.o, 0.04, 0.033, b, s)
    f = Form(ELB, WRI, [
        (0.020, 0.044, 0.040, 0.046, 0.048),
        (0.085, 0.066, 0.048, 0.062, 0.068),
        (0.200, 0.048, 0.040, 0.048, 0.048),
        (0.285, 0.035, 0.033, 0.036, 0.036)], c=[0.48, 0.3, 0.3, 0.48], b=[0.03, 0.06, 0.03, 0.06])
    tube("forearm_under", f, 0.02, 0.285, F, b, s, nt=10, nth=32, lift=-0.007)
    patch("gauntlet", f, [(-128, 0.07), (-50, 0.045), (0, 0.03), (50, 0.045), (128, 0.07)], [(-128, 0.21), (-45, 0.25), (0, 0.268), (45, 0.25), (128, 0.21)], P, b, s,
          nu=40, nv=14, edge=(0.06, 0.12, 0.006))
    patch("gauntlet_top", f, [(-30, 0.062), (30, 0.062)], [(-15, 0.236), (15, 0.236)], S, b, s, nu=10, nv=12, lift=0.006, thick=0.009, edge=(0.25, 0.1, 0.004))
    strip("gauntlet_light", f, -1.5, 1.5, 0.12, 0.2, b, s, lift=0.0095)
    patch("forearm_in", f, [(140, 0.06), (220, 0.06)], [(150, 0.225), (210, 0.225)], S, b, s, nu=10, nv=8, edge=E)
    tube("cuff", f, 0.262, 0.288, S, b, s, nt=1, nth=32, lift=0.004, bevel=0.002)


def legs():
    s = "legs"
    b = "thigh_l"
    t = Form(HIP, KNEE, [
        (-0.030, 0.064, 0.050, 0.074, 0.074),
        (0.000, 0.076, 0.058, 0.086, 0.084),
        (0.100, 0.098, 0.066, 0.108, 0.098),
        (0.240, 0.080, 0.060, 0.088, 0.080),
        (0.360, 0.055, 0.047, 0.064, 0.058),
        (0.400, 0.048, 0.044, 0.054, 0.052)], c=[(0.5, 0.4), 0.3, 0.3, 0.4], b=[0.03, 0.07, 0.03, 0.05], soft=8)
    tube("thigh_under", t, 0.0, 0.40, F, b, s, nt=14, nth=40, lift=-0.008)
    patch("thigh_front", t, [(-62, 0.05), (-10, 0.0), (40, 0.012), (90, 0.045), (158, 0.075)], [(-62, 0.30), (0, 0.345), (90, 0.388), (158, 0.31)], P, b, s,
          nu=44, nv=18, edge=(0.06, 0.1, 0.007))
    patch("thigh_side", t, [(-34, 0.04), (10, 0.03)], [(-24, 0.29), (2, 0.325)], S, b, s, nu=10, nv=14, lift=0.006, thick=0.009, edge=(0.25, 0.1, 0.004))
    patch("thigh_back", t, [(174, 0.085), (230, 0.06), (284, 0.07)], [(178, 0.29), (230, 0.335), (284, 0.30)], P, b, s, nu=24, nv=14, edge=(0.1, 0.12, 0.006))
    b = "calf_l"
    joint("knee", KNEE, (1, 0, 0), 0.044, 0.043, b, s)
    kx, ky, kz = KNEE
    hull("kneecap", [(kx + q * 0.024, ky - 0.04, kz + 0.052) for q in (-1, 1)] + [(kx + q * 0.038, ky - 0.05, kz + 0.012) for q in (-1, 1)] +
         [(kx, ky - 0.064, kz + 0.014), (kx, ky - 0.052, kz - 0.058)] + [(kx + q * 0.022, ky - 0.044, kz - 0.04) for q in (-1, 1)] +
         [(kx + q * 0.032, ky - 0.026, kz + z) for q in (-1, 1) for z in (0.036, -0.026)], P, b, s, bevel=0.003)
    c = Form(KNEE, ANK, [
        (0.030, 0.050, 0.044, 0.054, 0.054),
        (0.130, 0.066, 0.054, 0.066, 0.096),
        (0.300, 0.051, 0.044, 0.057, 0.060),
        (0.420, 0.040, 0.036, 0.047, 0.042),
        (0.458, 0.036, 0.034, 0.040, 0.038)], c=[0.5, 0.5, 0.32, 0.32], b=[0.03, 0.1, 0.03, 0.06], soft=6)
    tube("calf_under", c, 0.03, 0.458, F, b, s, nt=14, nth=36, lift=-0.007)
    patch("shin", c, [(8, 0.10), (55, 0.065), (90, 0.045), (125, 0.065), (172, 0.10)], [(8, 0.36), (55, 0.41), (90, 0.44), (125, 0.41), (172, 0.36)], P, b, s,
          nu=40, nv=20, edge=(0.06, 0.08, 0.006))
    patch("shin_side", c, [(16, 0.12), (62, 0.10)], [(30, 0.355), (58, 0.385)], S, b, s, nu=10, nv=14, lift=0.006, thick=0.009, edge=(0.25, 0.1, 0.004))
    strip("shin_light", c, 42.5, 45.3, 0.27, 0.345, b, s, lift=0.0095)
    patch("calf_back", c, [(192, 0.075), (270, 0.05), (348, 0.075)], [(204, 0.27), (270, 0.375), (336, 0.27)], P, b, s, nu=30, nv=16, edge=(0.08, 0.1, 0.006))
    b = "foot_l"
    joint("ankle", ANK, (1, 0, 0), 0.033, 0.036, b, s)
    f = Form((0.157, 0.125, 0.003), (0.157, -0.228, 0.003), [
        (0.000, 0.040, 0.040, 0.075, 0.002),
        (0.040, 0.048, 0.048, 0.135, 0.002),
        (0.090, 0.050, 0.050, 0.152, 0.002),
        (0.150, 0.052, 0.052, 0.105, 0.002),
        (0.220, 0.056, 0.056, 0.072, 0.002),
        (0.300, 0.056, 0.056, 0.056, 0.002),
        (0.338, 0.049, 0.049, 0.044, 0.002),
        (0.353, 0.036, 0.036, 0.028, 0.002)], front=(0, 0, 1), c=[(0.3, 0.4), (0.3, 0.4), 0.03, 0.03], b=[0.02, 0.05, 0.02, 0.0], soft=5)
    tube("foot", f, 0.0, 0.353, S, b, s, nt=18, nth=40, lift=-0.002, bevel=0.003)
    patch("toe", f, [(4, 0.262), (176, 0.262)], [(4, 0.35), (176, 0.35)], P, b, s, nu=24, nv=8, lift=0.003, thick=0.008, edge=(0.06, 0.2, 0.004))
    patch("instep", f, [(4, 0.15), (90, 0.118), (176, 0.15)], [(4, 0.256), (90, 0.268), (176, 0.256)], P, b, s, nu=24, nv=10, lift=0.005, thick=0.009, edge=(0.06, 0.16, 0.005))
    patch("heel", f, [(4, 0.0), (176, 0.0)], [(4, 0.07), (60, 0.062), (120, 0.062), (176, 0.07)], P, b, s, nu=24, nv=6, lift=0.003, thick=0.008, edge=(0.06, 0.2, 0.004))
    patch("ankle_guard", f, [(2, 0.075), (52, 0.085)], [(2, 0.142), (40, 0.122)], S, b, s, nu=10, nv=6, lift=0.004, thick=0.008, edge=E, mirror=True)
    patch("ankle_guard_in", f, [(128, 0.085), (178, 0.075)], [(140, 0.122), (178, 0.142)], S, b, s, nu=10, nv=6, lift=0.004, thick=0.008, edge=E, mirror=True)


head(); core(); arms(); legs()
parts = K.finish("Shell1")
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "shell1_claude.blend"))

if "--norender" not in sys.argv:
    cam, lights = K.studio()
    R = os.path.join(OUT, "renders", "shell1_")
    views = {"three_q": (-0.62, -1, 0.12), "rear_q": (0.7, 1, 0.15), "front": (0, -1, 0), "back": (0, 1, 0), "side": (1, 0, 0)}
    for n, d in views.items():
        K.shoot(R + n + ".png", cam, lights, d)
    K.pose([("spine_02", (1, 0, 0), 10), ("head", (0, 0, 1), -20),
            ("thigh_l", (1, 0, 0), -40), ("calf_l", (1, 0, 0), 55), ("thigh_r", (1, 0, 0), 18), ("calf_r", (1, 0, 0), 25),
            ("upperarm_r", (1, 0, 0), -65), ("lowerarm_r", (1, 0, 0), -45), ("upperarm_l", (1, 0, 0), -35), ("lowerarm_l", (1, 0, 0), -80)])
    K.shoot(R + "pose.png", cam, lights, (-0.8, -1, 0.15))
    K.unpose()
    K.sheet([R + n + ".png" for n in list(views) + ["pose"]], 6, R + "sheet.png")
