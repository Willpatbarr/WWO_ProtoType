# Performance

Where the frame goes, what has been done, and which larger changes are worth an experiment.
Last measured 2026-11-12 on the M4 Mac mini; reviewed 2026-11-14. Windows has never been measured.

## Where the frame goes

12-player bot match on Arboretum, bots throwing and casting constantly, 2560 x 1440 output (the engine
renders about 1552 x 873 and upsamples). Source: `Saved/Profiling/CSV` from that date and the 2026-11-12
optimization pass.

| | Time |
|---|---|
| Frame | 13.4 ms (about 75 fps) |
| GPU | 12.5 to 13.3 ms |
| Game thread, actual work | 4.4 ms (the other 9 ms is waiting for the GPU) |
| Render thread, actual work | under 2 ms |

**The game is GPU-limited by a wide margin.** Nothing on the CPU side can raise the frame rate until the
GPU cost drops by more than half.

Game thread: character movement 1.1 ms, actor ticks 1.2, UI 0.55, animation 0.14.

GPU, itemised (about 9.2 ms): the 58 unshadowed point lights 1.9, base pass 1.4, TAA upsample 1.25,
shadows 1.0, sun 0.8, reflections 0.8, lit-translucency volume 0.56, tonemap 0.55, sky 0.5, ambient
occlusion 0.4. About 3 ms is not itemised (prepass, velocity, translucent effect overdraw, UI).

Not measured anywhere: hitching (shader compiles, spawn spikes), and everything on Windows.

## Already done

- No Lumen, TAA instead of TSR (`Config/DefaultEngine.ini`).
- Mac only (`Config/Mac/MacEngine.ini`): Nanite off (about 3 ms) and cascaded shadows instead of virtual
  shadow maps (about 2.5 ms). Other platforms keep both.
- `r.GTSyncType=1`, `r.OneFrameThreadLag=0` for input latency, at a cost of about 2 fps.

## Larger changes worth an experiment

Ranked by expected gain. Each has a cheap test to run before committing to the work.

| Change | Expected gain | Cost | Main risk |
|---|---|---|---|
| **Bake the lamp and sky lighting** | 2 to 2.5 ms (about 75 to 90 fps). The lights part is measured, the rest is a guess | Medium to large | Look changes: static lights lose highlights, characters are lit from a volume |
| **Forward shading** (`r.ForwardShading=1`) | 1 to 3 ms, low confidence | Small to try, medium to restore the look | No screen-space reflections, ambient occlusion or contact shadows; possible Windows parity problem |
| **Mobile renderer on the Mac** | **Measured 2026-11-15: about half the GPU cost at equal resolution** (see below) | One config line and a launch switch; look judged acceptable | Mac and Windows would run different renderers; packaged default and full-screen start untested |
| **Dynamic resolution** | A frame-rate floor in heavy scenes, not a higher average | Small to medium | Softer image under load |
| **Effects on Niagara** | Unknown; scalability and spawn spikes, not average fps | Large (rewrite of `FX/WWOFX` and the flora effects) | Re-matching every effect's look |

### Baked lighting
`r.AllowStaticLighting=False` today; every lamp is spawned movable and unshadowed, and the sky light is
a movable real-time capture (`Tools/build_cascade.py`, `Tools/build_maps.py`). The lamps are the largest
single GPU item. The work: turn static lighting on, give the merged map meshes lightmap UVs, add a
Lightmass bake to the map build scripts, make the lamps and sky light static or stationary.
**Cheap test:** a perf sweep with the lamps hidden gives the upper bound before any work.

### Forward shading
Suits one sun plus many unshadowed lights and a stylised look. Screen-space reflections (0.8 ms) and
ambient occlusion (0.4 ms) go away because forward does not support them, but those can be turned off in
deferred too, so the real question is whether base pass plus lights gets cheaper than today's 4.1 ms.
**Cheap test:** sweep `r.SSR.Quality 0` and `r.AmbientOcclusionLevels 0` first to see the part that is
free, then turn forward on in a branch and run `-WWOPerfTest`.
Epic's doc: https://dev.epicgames.com/documentation/unreal-engine/forward-shading-renderer-in-unreal-engine

### Mobile renderer on the Mac
UE 5.8 defines a "Mac Metal Mobile" shader platform (`SF_METAL_ES3_1`), the path built for Apple's
tile-based GPUs. Tried on the branch `exp/mac-mobile-renderer` (2026-11-15): add
`+TargetedRHIs=SF_METAL_ES3_1` under `MacTargetSettings` in `Config/DefaultEngine.ini` and launch with
`-FeatureLevelES31` (it only applies to `-game` runs and packaged builds, not the editor). No code changes.

12-player bot match, bots busy, whole-map average from `-WWOPerfTest`, one run each:

| Map, window | Desktop renderer | Mobile renderer |
|---|---|---|
| Arboretum, 1600 x 900 | 11.60 ms (86 fps) | 9.23 ms (108 fps) |
| Cascade, 1600 x 900 | 11.53 ms (87 fps) | 8.57 ms (117 fps) |
| Arboretum, 2560 x 1440 | 14.91 ms (67 fps) | 14.15 ms (71 fps) |
| Cascade, 2560 x 1440 | 14.14 ms (71 fps) | 11.01 ms (91 fps) |

At 2560 x 1440 the two are not drawing the same pixels: by default the desktop renderer draws about 60%
and upsamples, the mobile renderer draws 100%. GPU time for one heavy Arboretum view by
`r.ScreenPercentage`: desktop 16.3 / 17.5 / 19.7 / 29.2 ms at 50 / 60 / 75 / 100%, mobile 7.7 / 8.8 /
10.2 / 13.6 ms. So mobile at native resolution costs about what desktop costs at 60%.

Every scripted test gave the same result as on the desktop renderer, and Will judged the side-by-side
screenshots acceptable. Not done: how a reduced resolution looks on mobile (no temporal upsampling
there), a default launch mode for packaged builds (the engine has no project setting; the app's default
command line is the likely route), and full-screen start from the command line, which hung during the
test (cause not found, not tried on the desktop renderer). Windows is unaffected.

### Dynamic resolution
Nearly every remaining GPU cost scales with pixels, and there is no screen-percentage or
dynamic-resolution setting in the project today. Metal reports support for it. MetalFX upscaling is not
available: the engine ships only its headers, no upscaler plugin.

### Effects on Niagara
`FX/WWOFX.cpp` spawns an actor per effect, each shape with its own dynamic material, so nothing batches;
about 490 effect actors and 360 particle actors were alive on average in the busy test. Their CPU cost is
small today. The possible win is translucent overdraw inside the 3 ms that is not itemised.
**Cheap test:** read the Translucency line of `profilegpu` during `-WWOBotsBusy` before deciding.

## Checked and not worth doing here

- **Iris, replication graph, push model:** networking costs about 0.001 ms a frame on a 12-player LAN game.
- **Mass, significance manager, tick budgeting, Animation Budget Allocator, update-rate optimisation:**
  animation is 0.14 ms for 12 characters and the game thread has 9 ms idle.
- **Mover:** still experimental in 5.8; character movement is 1.1 ms and not the limit.
- **Pre-baked retargeting:** all animation work is about 0.3 ms.
- **UMG invalidation or retainer boxes:** UI is 0.55 ms.
- **HLOD, instancing, PCG for map geometry and plants:** the render thread does under 2 ms of work, so
  draw submission is not the limit.
- **Shipping configuration, LTO, PGO:** right for a release build, but they speed up CPU code only.

## First step on Windows

Run `-WWOPerfTest` there. Nanite and virtual shadow maps are still on, and the Mac's conclusions may not
transfer.

## Tools

See `TESTING.md`, Performance.
