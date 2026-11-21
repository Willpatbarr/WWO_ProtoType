# Check for the shell 3 mask: Will's sketch lines (red) laid over the built mask's front and side renders.
#   Blender -b -P mask_overlay.py        (after make_shell3.py)
# Out: renders/shell3_mask_overlay.png. The sketch is the head sheet at SCALE px per px; OFFSET is where each
# view's top-left corner sits in the sketch (found by matching the chest, which he did not draw on).
import bpy, os, numpy as np
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCALE, OFFSET = 0.563, {"front": (-62, 15), "side": (503, 15)}


def load(p):
    im = bpy.data.images.load(p); w, h = im.size
    a = np.array(im.pixels[:], dtype=np.float32).reshape(h, w, 4)[::-1, :, :3].copy(); bpy.data.images.remove(im); return a


sk = load(ROOT + "/reference/mask_sketch.jpg")
stroke = (sk[..., 0] > 0.85) & (sk[..., 1] > 0.78) & (sk[..., 2] > 0.68)
out = np.zeros_like(sk) + 0.1
H, W = stroke.shape
for name, (ox, oy) in OFFSET.items():
    t = load(ROOT + "/renders/shell3_head_%s.png" % name)
    ys, xs = np.mgrid[0:H, 0:W]
    ty, tx = np.round((ys - oy) / SCALE).astype(int), np.round((xs - ox) / SCALE).astype(int)
    ok = (ty >= 0) & (ty < 1000) & (tx >= 0) & (tx < 1000)
    out[ok] = t[ty[ok], tx[ok]]
out[stroke] = (1, 0.15, 0.1)
big = np.repeat(np.repeat(out, 2, 0), 2, 1)
im = bpy.data.images.new("o", W * 2, H * 2, alpha=False)
im.pixels = np.concatenate([big[::-1], np.ones((H * 2, W * 2, 1), np.float32)], 2).ravel().tolist()
im.filepath_raw = ROOT + "/renders/shell3_mask_overlay.png"; im.file_format = "PNG"; im.save()
