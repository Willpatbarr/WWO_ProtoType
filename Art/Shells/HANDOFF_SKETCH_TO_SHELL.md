# Sketch → shell edit — handoff for the MacBook Air (process settled, skeleton edits open)

<small>

<span style="color:#1C8E9C">Setup</span> — Get the files · Build a shell · Render a sheet
<span style="color:#B06A1E">Process</span> — Sheet · Sketch · Map · Read facets · Build · Check · Report
<span style="color:#5A8C33">Rules</span> — Facets · Options · Wording · Game constraints · Sending work back
<span style="color:#C24A4A">Open</span> — Skeleton edits · Blender version

</small>

Ordering: Setup and Process are in the order they are done. Rules and Open are alphabetical.

---

## <span style="color:#1C8E9C">Setup</span>

### <span style="color:#1C8E9C">│</span> Get the files

| Field | Value |
|---|---|
| Decision | Clone only `Art/Shells` of `Willpatbarr/WWO_ProtoType`, branch `shell-parts` |
| Because | **C** — the whole repo is several GB of Unreal content the Air does not need |
| Lives in | `Art/Shells/` |

partial clone that downloads the shells folder only

```bash
git clone --filter=blob:none --sparse -b shell-parts \
  git@github.com:Willpatbarr/WWO_ProtoType.git WWO
cd WWO && git sparse-checkout set Art/Shells
```

| Folder | Holds |
|---|---|
| `base/` | `robot_frame_v2_rigged.blend` — the in-game robot and its rig |
| `base/enemy_<soldier\|swarm\|tank>_rig.blend` | the three enemy skeletons as Blender rigs, bones only. **Not in this repo** (it is public and they are Fab pack data): rebuild them with `tools/make_enemy_rigs.py` from the private `Willpatbarr/WWO_EnemySkeletons` repo cloned beside the WWO folder, or copy them from that repo's `remnant-models` branch. Same units, facing and FBX settings as the robot rig. `tools/enemy_rig_views.py` renders a sheet of them |
| `tools/` | `shell_kit.py`, `make_shell1.py`, `make_shell2.py`, review scripts |
| `reference/` | concept sheets and every sketch drawn so far |
| `shell1_claude.blend`, `shell2_claude.blend` | built output, rebuilt by the tools |
| `bleakswarm_claude.blend` | the BleakThorn swarm enemy (Remnant melee), rebuilt by `tools/make_bleakswarm.py` on `base/enemy_swarm_rig.blend`; `-- pose` adds a posed skinning sheet, `-- export` writes `export/SK_BleakSwarm.fbx`. One skinned mesh, five material roles (`M_Bleak_Wood`, `Sinew`, `Bone`, `Leaf`, `Vine`). Its colour variation lives in the vertex colours (`Col`, exported sRGB), so the game material must read vertex colour or every strand comes out one flat shade. Like the other two enemy files it holds a Fab skeleton, so it is ignored here; the built `.blend` and FBX are on `WWO_EnemySkeletons`, branch `remnant-models` |
| `cindersoldier_claude.blend` | the CinderBloom soldier (Remnant ranged), rebuilt by `tools/make_cindersoldier.py` on `base/enemy_soldier_rig.blend`, same switches, writes `export/SK_CinderSoldier.fbx`. Six roles (`M_Cinder_Wood`, `Sinew`, `Bone`, `Leaf`, `Vine`, `Ember`); `Ember` is the one that glows. No weapon on it: the game attaches one |
| `ironbark_claude.blend` | the IronBark tank (Remnant heavy, also the FootRace boss), rebuilt by `tools/make_ironbark.py` on `base/enemy_tank_rig.blend`, same switches, writes `export/SK_IronBark.fbx`. Built at the skeleton's own size; the game draws it at 0.55. Six roles (`M_Iron_Wood`, `Root`, `Bark`, `Leaf`, `Cap`, `Amber`); `Amber` glows |
| `tools/remnant_lineup.py` | renders every built Remnant enemy side by side at true scale: `renders/remnant_lineup.png` |
| `tools/remnant_kit.py` | what the Remnant enemy scripts share: strands, leaves, pierced plates, skinning, vertex colour, renders and export |
| `renders/`, `export/` | rebuilt by the tools, not in git |

### <span style="color:#1C8E9C">│</span> Build a shell

| Field | Value |
|---|---|
| Decision | A shell is never edited by hand in Blender. `make_shell<N>.py` is the model |
| Because | **C** — a rebuild overwrites `shell<N>_claude.blend`, so hand edits are lost |
| Lives in | `tools/make_shell1.py` (sleek) · `tools/make_shell2.py` (scout) |

rebuilds shell 2 from the base rig and writes its whole-robot renders

```bash
cd Art/Shells/tools
/Applications/Blender.app/Contents/MacOS/Blender -b \
  ../base/robot_frame_v2_rigged.blend -P make_shell2.py
```

- **Why:**
  - **C** — `base/robot_frame_v2_rigged.blend` is opened read-only; the script never saves it
  - **C** — every tool finds the shells folder from its own location, so any clone path works

### <span style="color:#1C8E9C">│</span> Render a sheet

| Field | Value |
|---|---|
| Decision | Review happens on a four-view sheet: front, side, 3/4, low 3/4 |
| Because | **B** — Will draws over this sheet; two flat views give every corner in 3D |
| Lives in | `tools/head_quad.py` · `tools/paul_quad.py` · `tools/torso_views.py` · `tools/full_views.py` |

renders the pauldron sheet from the built file into `renders/`

```bash
/Applications/Blender.app/Contents/MacOS/Blender -b \
  ../shell2_claude.blend -P paul_quad.py
```

For a new body part, copy `paul_quad.py` and change `target` (cm, the point the camera
looks at) and `span` (cm, the height the view covers).

---

## <span style="color:#B06A1E">Process</span>

### <span style="color:#B06A1E">│</span> 1 · Sheet

| Field | Value |
|---|---|
| Decision | Render each view at 1000 px with a known `target` and `span` before Will sketches |
| Because | **C** — a known span gives the scale: `span / 1000` cm per pixel at the target |

### <span style="color:#B06A1E">│</span> 2 · Sketch

| Field | Value |
|---|---|
| Decision | Will draws on the sheet. Save the drawing into `reference/` under a clear name |
| Because | **C** — the build script cites the sketch file a shape came from |
| Lives in | `reference/jaw_sketch.png` · `reference/top_sketch.png` · `reference/pauldron_sketch.jpg` |

How to read a drawing:

| Mark | Means |
|---|---|
| One filled shade | one flat facet |
| An outline with no fill | a facet whose edges are hard edges |
| A second, darker shade on the same piece | that piece's thickness |
| A different colour | a separate structure, or a cutout (he says which) |
| A circled spot | move that corner; the message says which way |
| A plain line | a new silhouette edge for the existing shape |

### <span style="color:#B06A1E">│</span> 3 · Map the drawing onto the sheet

| Field | Value |
|---|---|
| Decision | Find the drawing's scale and offset against the sheet from two or three landmarks |
| Because | **C** — Will sends a crop or a zoom of the sheet, never the sheet's own pixels |

1. Measure the drawing's pixel size with `sips -g pixelWidth -g pixelHeight`.
2. Pick landmarks visible in both: a silhouette corner, a recess corner, a light strip.
3. Solve `sheet = drawing / scale + offset` per axis.
4. Check the front and side views give the same scale. A few pixels of disagreement is
   normal; split the difference and say so.

### <span style="color:#B06A1E">│</span> 4 · Read each facet's corners

| Field | Value |
|---|---|
| Decision | Convert each corner from pixels to rig coordinates, correcting for perspective |
| Because | **C** — the camera is a 70 mm perspective lens, so near surfaces draw larger |

| View | Camera looks along | Image right is | Image up is |
|---|---|---|---|
| front | +Y | +X (robot's left) | +Z |
| side | −X | +Y (robot's back) | +Z |

- Rig space: centimetres, robot faces −Y, +X is the robot's left, feet at Z = 0.
- Camera distance `D = (span / 2) / tan(atan(12 / 70))`. For `span = 50`, `D ≈ 146` cm.
- A surface `d` cm nearer the camera than `target` draws `D / (D − d)` times larger.
  Divide its offsets from the view centre by that factor.
- The front view gives X and Z. The side view gives Y and Z. A corner needs both.

### <span style="color:#B06A1E">│</span> 5 · Build

| Field | Value |
|---|---|
| Decision | Use the helper that matches the shape; write corners as numbers in the script |
| Lives in | `tools/make_shell2.py` · `tools/shell_kit.py` |

| Helper | Use for |
|---|---|
| `solid(name, faces, …)` | exactly the drawn facets, each its own flat plate, open behind |
| `hull(name, points, …)` | a closed convex block (the chest, the back armour) |
| `cut_solid(name, planes, …)` | a block cut down by planes, every face exactly flat |
| `top_plate(name, stations, …)` | a slightly curved plate with hard edges across it |
| `recess(obj, points)` | a cutout through a piece (the pauldron's square) |
| `Form` + `patch` / `tube` | first-pass slab limbs; replaced by the helpers above |

- Model the robot's left side (+X); pieces on a `_l` bone are mirrored automatically.
- A four-corner facet must be flat. Solve one coordinate of one corner so it is, and tell
  Will which corner moved and by how much.
- `solid()` plates pass `bevel=0.0, smooth=1.0`: hard edges, no rounding.

### <span style="color:#B06A1E">│</span> 6 · Check

| Field | Value |
|---|---|
| Decision | Re-render the same view and compare landmark pixels with the drawing before sending |
| Because | **B** — Will: "Overlay my images to check the dimensions of yours if necessary" |
| Lives in | `tools/overlay.py` · `tools/overlay_top.py` |

Target: every drawn corner within a few pixels. Look at the back view too: a cut made for
one side has broken the far side before.

### <span style="color:#B06A1E">│</span> 7 · Report

| Field | Value |
|---|---|
| Decision | Send the four-view sheet and the whole robot, then list what differs from the drawing |
| Because | **B** — a difference Will finds himself costs a round; one he is told costs nothing |

Always say: which corners moved to keep a facet flat, anything added that was not drawn,
anything dropped, and where two views disagreed.

---

## <span style="color:#5A8C33">Rules</span>

### <span style="color:#5A8C33">│</span> Facets

| Field | Value |
|---|---|
| Decision | "Each shade is its own facet". Exact facet count, hard edges, no bevels |
| Because | **B** — Will: "You didn't obey the single facet per shade rule" |

- Never add a face he did not draw. A closing face under the pauldron was rejected:
  "nah that shouldn't be there". Build open plates with `solid()` instead.
- When old geometry pokes through a new plate, trim the old geometry, not the plate.

### <span style="color:#5A8C33">│</span> Game constraints

| Field | Value |
|---|---|
| Decision | Shape is free; the rig and the slot split are fixed |
| Because | **C** — the game swaps parts per slot on one shared skeleton |
| Lives in | `tools/export_shells.py` (`SLOT`) |

- Every piece is rigid to exactly one bone, named as in Unreal (`upperarm_l`, `spine_02`).
- A piece's bone decides its slot: head and neck → Head; spine, pelvis, clavicle → Core;
  upper and lower arm → Arms; thigh, calf, foot → Legs.
- Do not move, rename, add or remove bones. Animations and the retarget depend on them.
- Hands belong to the skeleton, not to any shell.
- Materials are roles: `Primary`, `Secondary`, `Frame`, `Glow`, and shell 2's
  `PrimaryLight`. A new role needs a matching entry in the game's
  `Tools/import_shell_parts.py`.

### <span style="color:#5A8C33">│</span> Options

| Field | Value |
|---|---|
| Decision | "do 1 cm, 2 cm and 3 cm" → one image, variants side by side, with the baseline |
| Because | **B** — Will: "Those all look the same were the images taken correctly?" |

Zoom in on the part that changes, and state whether an amount is per side or in total.

### <span style="color:#5A8C33">│</span> Sending work back

| Field | Value |
|---|---|
| Decision | Commit only under `Art/Shells/` on `shell-parts`, then push |
| Because | **C** — game code changes on the same branch from the Mac mini; paths never overlap |

pull first so the push is a fast-forward

```bash
git pull --rebase origin shell-parts
git add Art/Shells && git commit -m "Shell 2: <what changed>"
git push origin shell-parts
```

The Mac mini then runs `tools/export_shells.py` and the game's
`Tools/import_shell_parts.py`. Nothing on the Air needs Unreal.

### <span style="color:#5A8C33">│</span> Wording

| Field | Value |
|---|---|
| Decision | Ask when a direction word could mean two things |
| Because | **B** — "the back needs to stick out more" meant the torso, and was built on the head |

Also ambiguous so far: "taller" (up or down) and "bring in" (per side or total).

---

## <span style="color:#C24A4A">Open</span>

### <span style="color:#C24A4A">│</span> Blender version

| Field | Value |
|---|---|
| Decision | None yet. The Mac mini runs Blender 5.2.3 |
| Because | **C** — the scripts are untested on any other version |

### <span style="color:#C24A4A">│</span> Skeleton edits

| Field | Value |
|---|---|
| Decision | None yet. The skeleton has no build script |
| Because | **C** — the skeleton is cut out of `SK_RobotFrame` in `base/robot_frame_v2_rigged.blend` |
| Lives in | `tools/export_shells.py` (the `base` branch: `ARMOUR`, `NOT_SKELETON`) |

- Which pieces count as skeleton is a rule in `export_shells.py`, changeable without
  touching the mesh.
- Reshaping skeleton pieces means editing the mesh in that `.blend` by hand. Save it under
  a new name and say so in the commit, so the Mac mini does not build on a surprise.
