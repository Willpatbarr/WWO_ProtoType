import bpy, sys, numpy as np
a, b = sys.argv[-2], sys.argv[-1]
def load(p):
    im = bpy.data.images.load(p); w, h = im.size
    return np.array(im.pixels[:], dtype=np.float32).reshape(h, w, 4)
A, B = load(a), load(b); d = np.abs(A - B).sum(axis=2)
ys, xs = np.nonzero(d > 0.05)
print("WWO differing pixels", len(ys), "of", d.size, "rows", (ys.min(), ys.max()) if len(ys) else None, "cols", (xs.min(), xs.max()) if len(ys) else None)
