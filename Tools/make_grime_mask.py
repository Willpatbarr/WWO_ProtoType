"""Makes Tools/art/T_Grime_Mask.png, the tiling mask M_WWO_KitConcrete weathers the concrete with
(Tools/create_kit_concrete.py imports it). Needs numpy, so run it with Blender's Python:
  Blender --background --factory-startup --python Tools/make_grime_mask.py

1024 x 1024, tiles in both directions (the noise is filtered in frequency space):
- R: soft cloudy blotches, for large patches of dirt.
- G: leak streaks. They start at the top edge of the image and run down it, each its own width and
  length, so mapping the image's height to a wall's height hangs them from the wall's top.
- B: fine speckle, for breaking up edges.
"""
import os

import bpy
import numpy as np

N = 1024
rng = np.random.default_rng(7)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "art", "T_Grime_Mask.png")


def normalize(a, lo=1.0, hi=99.0):
    p0, p1 = np.percentile(a, [lo, hi])
    return np.clip((a - p0) / (p1 - p0), 0.0, 1.0)


def noise2(falloff, stretch=(1.0, 1.0)):
    """Tiling fractal noise: white noise with a 1/f^falloff spectrum. stretch scales (x, y) detail."""
    fy = np.fft.fftfreq(N)[:, None] * N / stretch[1]
    fx = np.fft.fftfreq(N)[None, :] * N / stretch[0]
    f = np.sqrt(fx * fx + fy * fy)
    f[0, 0] = 1.0
    spec = np.fft.fft2(rng.standard_normal((N, N))) / f ** falloff
    spec[0, 0] = 0.0
    return normalize(np.real(np.fft.ifft2(spec)))


def noise1(falloff):
    f = np.abs(np.fft.fftfreq(N) * N)
    f[0] = 1.0
    spec = np.fft.fft(rng.standard_normal(N)) / f ** falloff
    spec[0] = 0.0
    return normalize(np.real(np.fft.ifft(spec)))


dirt = noise2(1.3)   # blotches a fraction of the tile across, so every wall gets some
speckle = noise2(0.9)

# Streaks: where a 1D noise across the width peaks there is a leak; another sets how far it runs.
where = np.clip((noise1(0.25) - 0.6) * 5.0, 0.0, 1.0)[None, :]   # scattered, not clumped
length = (0.25 + 0.75 * noise1(1.4))[None, :]
v = (np.arange(N) / N)[:, None]                                 # 0 at the top of the image
run = np.clip(1.0 - v / length, 0.0, 1.0) ** 1.4
fibres = 0.55 + 0.45 * noise2(1.2, stretch=(1.0, 14.0))        # thin vertical variation within a streak
streaks = np.clip(where * run * fibres * 1.25, 0.0, 1.0)

px = np.ones((N, N, 4), dtype=np.float32)
px[:, :, 0] = dirt[::-1]       # Blender's rows run bottom to top
px[:, :, 1] = streaks[::-1]
px[:, :, 2] = speckle[::-1]
os.makedirs(os.path.dirname(OUT), exist_ok=True)
im = bpy.data.images.new("T_Grime_Mask", N, N, alpha=False)
im.colorspace_settings.name = 'Non-Color'
im.pixels = px.ravel()
im.filepath_raw = OUT
im.file_format = 'PNG'
im.save()
print("WROTE", OUT, "dirt mean %.2f, streak mean %.2f" % (dirt.mean(), streaks.mean()))
