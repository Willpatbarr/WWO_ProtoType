import bpy, sys, os, numpy as np
R = (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/")
def load(p):
    im = bpy.data.images.load(p); w, h = im.size
    return np.array(im.pixels[:], dtype=np.float32).reshape(h, w, 4)
a = load(R + "reference/top_sketch.png"); b = load(R + "renders/shell2_sketch_head_quad.png")
mix = a * 0.5 + b * 0.5
H = a.shape[0]
def crop(img, x0, y0, x1, y1): return img[H - y1:H - y0, x0:x1]
for name, box in (("front", (280, 150, 720, 420)), ("side", (1130, 150, 1770, 420))):
    row = np.concatenate([crop(a, *box), crop(b, *box), crop(mix, *box)], axis=1)
    row = np.repeat(np.repeat(row, 2, axis=0), 2, axis=1)
    h, w, _ = row.shape
    im = bpy.data.images.new("o", w, h, alpha=False); im.pixels = row.ravel().tolist()
    im.filepath_raw = R + "renders/shell2_top_overlay_%s.png" % name; im.file_format = "PNG"; im.save()
