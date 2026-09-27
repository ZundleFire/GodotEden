# Biome Material Speckle — Investigation Findings & Fix Plan

**STATUS (2026-08-21): RESOLVED.** Both root causes below are fixed and visually verified —
`foliage_demo_forest_closeup.png`/`foliage_demo_grassland_closeup.png`/`materials_showcase.png`
regenerated with the real compiled editor show clean smooth material regions and smooth
boundary blending, no speckle. Kept below for reference; the "Already Ruled Out" table and
landmines are still worth reading before touching this shader/generator pair again.

Handoff doc. Everything below was established by direct measurement unless explicitly marked as
inference.

---

## Problem

Terrain rendered with fine per-pixel material speckle / checkerboard instead of clean material
regions. Two visually distinct artifacts existed; both are now fixed.

- **FIXED** — pervasive faceted checkerboard across nearly all terrain (root cause #1).
- **FIXED** — mottled `MAT_MOSS` (teal) ↔ `MAT_DIRT` (orange-brown) banding, only ever at biome
  borders, never inside a uniform biome region (root cause #2 — see below; the original
  per-voxel-argmax-flip hypothesis was wrong, see "Root Cause #2" for the real cause).

---

## Root Cause #1 — FIXED: erosion gully discontinuity

`EdenPlanetGeneratorV1::_sample_voxel()`, the `tect_line > 0.001f` block.

The mountain/valley erosion filter built escarpment shape from an SDF finite-difference gradient
plus a **per-octave discontinuous sign flip** (`straight = (s >= 0.0f) ? 1.0f : -1.0f`). At a
spatial frequency fine relative to LOD0 mesh resolution, that discontinuity perturbed
Transvoxel-computed normals into visible faceted lighting noise — across nearly all terrain, not
just mountains, because the `tect_line > 0.001` gate is extremely permissive and this fork's
tectonic map keeps `tect_line` elevated broadly rather than peaked at plate boundaries.

**Proof:** zeroing `mtn`/`valley` made an otherwise-checkerboarded area render perfectly smooth.
A 70% *amplitude* cut changed nothing — so it was a threshold/discontinuity effect, not a
gradient-proportional one.

**Fix applied:** replaced the ~45-line gully algorithm with a single smooth
`noise_mountain->get_noise_3d()` sample. Mountains/trenches keep real noise-driven elevation via
the same `mtn_mask`/`trench_mask` modulation; only the discontinuous escarpment *detail* is gone.
A proper anti-aliased erosion pass could reintroduce that detail later under a textured pipeline.

---

## Root Cause #2 — FIXED: missing `flat` on the material-index varying (not biome flipping)

**The original hypothesis in this section (per-voxel biome argmax flip) was WRONG.** Confirmed
wrong by direct measurement, not just re-inference — see "What actually happened" below. Left
here so nobody re-derives and re-discards the same dead end.

**Original (incorrect) hypothesis:** `EdenPlanetGeneratorV1::_classify_biome_fast()` picks biome
by argmax over near-tied noise-derived weights, so biome — and therefore material — flips
per-voxel, checkerboarding the material id before it ever reaches the mesher.

**What actually happened:** `demo_eden/voxel_data_probe.gd` was extended with a
`sample_biome_at(dir)` binding (new `EdenPlanetGeneratorV1::sample_biome_at`, thin wrapper around
`sample_surface()`) and a multi-stage coarse-to-fine scan (20m → 2m → 1m) that hunts for the grid
cell with the *densest* run of adjacent material disagreements, rather than the first boundary
found — a real per-voxel flip should show many adjacent disagreements in a small area; an
ordinary region boundary shows exactly one crossing. Across every scan performed (up to a 4km
span around the demo's own camera position, taken straight from `foliage_demo_run.log`), biome
and material were **always spatially clean**: uniform inside a region, and a single clean
boundary crossing at every transition found, matching what `u_debug_mode = 2`/`4` had already
shown ("read clean" in the Already Ruled Out table below) — i.e. the *generator's* per-voxel
material/biome assignment was never actually the problem. The user independently confirmed the
same thing from having watched the artifact: **the problem was always at biome borders, never
inside a biome interior.**

That reframing — "only at borders" — points at interpolation, not selection. Checked
`demo_eden/shaders/planet_land.gdshader`: `v_indices` (the packed MIXEL4 material-index vec4,
decoded per-vertex from `CUSTOM1.x`) was declared as a plain `varying vec4`, not `varying flat
vec4`. Godot's shading language (like GLSL) linearly interpolates a non-flat varying across a
triangle's fragments. Inside a biome, every vertex of every triangle carries the *same* packed
indices, so interpolating identical values is a no-op — clean, exactly as observed. At a biome
border, adjacent vertices carry *different* integer indices packed into the same `vec4`
component, and the GPU blends those integers as continuous data — e.g. interpolating between
material id 4 and id 5 mid-triangle produces fractional garbage that truncates to a bogus id, not
a real material — which decodes to essentially arbitrary colors per fragment near the boundary.
That's the speckle, and it explains everything the elimination table below found: raw blend
*weights* (mode 2) are legitimately continuous data, so interpolating them is correct and reads
clean; the material-index debug view (mode 4) only samples `v_indices.x` and wasn't specifically
pointed at a border crossing during those tests, so its own interpolation bug went unnoticed.

**Fix applied:** `varying flat vec4 v_indices;` in both `demo_eden/shaders/planet_land.gdshader`
and `eden/shaders/terrain_smooth.gdshader` (same bug, same fix — the latter isn't in the demo's
current render path but has the same missing qualifier and is the shader `planet_land.gdshader`
is meant to be swapped for once real textures exist). `v_weights` stays a normal smoothly-
interpolated `varying` — that's the real, intentional cross-fade between materials at a border,
and it was never the bug. Note: Godot's shader-language qualifier order is `varying flat vec4`,
not `flat varying vec4` (the latter is a parse error — `_parse_shader`'s grammar puts the
interpolation qualifier after `varying`, unlike raw GLSL where `flat` leads).

**Verification:** rebuilt (`build_eden.bat`), reran `eden_foliage_demo.tscn`,
`eden_materials_showcase.tscn`, `eden_stress_test.tscn` with the real compiled editor (not
`--headless` — that forces Godot's dummy renderer, which can't produce screenshots at all; use a
normal windowed run). All three regenerated screenshots show clean material regions with smooth
boundary blending, no speckle, and no shader/script errors in any run's log.

---

## Already Ruled Out — do NOT re-test

Each was tested in isolation and had **no effect** on the artifact:

| Ruled out | Note |
|---|---|
| Shader slope/height overlay | `u_debug_mode = 1` |
| Raw MIXEL4 blend weights | `u_debug_mode = 2` — read clean |
| Slope value (`up_dot`) | `u_debug_mode = 3` — read clean |
| Material index | `u_debug_mode = 4` (unlit) — read clean |
| LOD cross-fade dithering | `u_debug_mode = 5`; also `lod_fade_duration` defaults to 0 = disabled |
| Transvoxel LOD-seam vertex correction | `u_debug_mode = 7` |
| Shader cache | cleared `.godot/shader_cache` |
| MSAA / TAA / FXAA / debanding | all forced off |
| Specular (GGX) | `SPECULAR = 0.0` — kept anyway, matte terrain |
| Light direction | was world-static, now sphere-local — real bug, but not this one |
| float64 for SDF `r`/`alt` | planet-scale precision is not the issue |
| Generator "neutral breakup" detail noise | zeroing it entirely changed nothing |
| Mesher `select_textures_4_per_voxel` comparator | fixing a real bug there changed nothing visually |
| Coarse LOD | mottling persists at LOD0, `lod_distance = 4000` |

---

## Landmines (these cost real time)

- **`VoxelLodTerrain.material` duplicates a `ShaderMaterial` per chunk** as chunks stream in
  (`VoxelLodTerrain.md:231`). `set_shader_parameter()` after `_ready()` does **not** reach
  already-streamed chunks. For debug uniforms, change the **default in the `.gdshader` file** and
  use a single shot — a runtime override silently tests stale values. Several "ruled out" results
  had to be re-run because of this.
- **`TEXTURES_SINGLE_S4` asserts `DEPTH_8_BIT` indices**
  (`transvoxel_materials_single_common.h:49` — its 16-bit branch is unreachable behind the
  assert). Set `VoxelFormat.indices_depth = DEPTH_8_BIT` on the terrain.
- **`lod_distance` controls FOLIAGE VISIBILITY**, not just terrain detail. `VoxelInstancer` only
  spawns an item on chunks at its `lod_index` (0-1 here), so at `lod_distance = 500` no instances
  existed beyond 500 m — which is why early screenshots showed bare terrain. Raise the items'
  `lod_index` to push foliage further out more cheaply than raising `lod_distance`.
- **Albedo is reflectance.** An earlier "vibrancy" pass pushed albedos to 0.93-0.96 (near-white
  before lighting), which clips to white under any sane exposure. Keep hue/saturation, keep
  luminance physical (sand ~0.4, snow ~0.8, grass ~0.25).
- **`Environment.tonemap_mode` defaults to `TONE_MAPPER_LINEAR`**, which clips hard at 1.0. Use
  ACES.

---

## Changes already committed to the fork (keep — all verified)

**`modules/voxel`** (our fork — engine internals are fair game):
- `transvoxel_materials_mixel4.h` — `IndexAndWeightComparator` now sorts weight-primary with
  index only as an exact-tie break. The prior version could drop a genuinely dominant material
  from the top-4 if three trace-weight materials had lower indices. Real correctness fix.

**`modules/eden_planet_gen/eden_planet_generator_v1.{h,cpp}`**:
- Erosion gully algorithm → smooth noise sample (Root Cause #1 fix).
- `sample_dominant_material(dir)` — decodes **all four** MIXEL4 slots and returns the
  highest-weight material. Added because `_find_material_land()` only read slot 0 and was
  silently landing on ocean while believing it had found forest.
- `MAT_*` bound as engine constants — single source of truth, no more duplicated magic numbers.
- `_land_material_blend()` — smoothstep bands replace hard `>=` thresholds
  (`beach_width_m`, `mountain_rock_start`, `mountain_snow_start`), and blends biome1↔biome2 using
  `_classify_biome_fast`'s already-computed runner-up. `_pack_mixel4()` carries the second
  material in **slot 3**, which was previously a zero-weight filler — so this cost no format,
  mesher, or shader change. Weight folding preserves MIXEL4's distinct-index requirement.
- `single_material_mode` (default **false**, so `eden_materials_showcase` / `eden_stress_test` /
  `voxel_water_demo` are unaffected) — one 8-bit material id per voxel, `CHANNEL_WEIGHTS`
  unwritten, for `TEXTURES_SINGLE_S4`.
- `cont_transition` weight deadzone (`sand_up`/`ocean_up < 0.05` → 0) so trace inland noise does
  not make blocks non-uniform.

**`demo_eden/shaders/planet_land.gdshader`**:
- Sphere-correct `true_up`/`altitude` (was flat-terrain world-Y math — only ~correct at the pole).
- Albedos rebalanced into physical range; `SPECULAR = 0.0`.
- `u_debug_mode` diagnostic uniform (1,2,3,4,5,7,8,9 — see table above).
- `varying flat vec4 v_indices` (Root Cause #2 fix) — was a plain `varying`, so the GPU
  linearly-interpolated packed material ids across triangles at biome borders.

**`eden/shaders/terrain_smooth.gdshader`**:
- Same `varying flat vec4 v_indices` fix, same reasoning, not currently in the demo render path.

**`demo_eden/eden_foliage_demo.gd`**:
- `single_material_mode` + `TEXTURES_SINGLE_S4` + `VoxelFormat.indices_depth = DEPTH_8_BIT`.
- Sphere-local sun direction; exposure `light_energy 1.0` / `ambient 0.25` / ACES.
- `lod_distance = 1500`, `lod_count = 5` (foliage visibility vs streaming cost).
- `_find_material_land()` uses `sample_dominant_material()`.

**`demo_eden/voxel_data_probe.gd` / `.tscn`** — fast no-terrain probe harness. Extended with a
`sample_biome_at`-backed biome readout and a multi-stage coarse-to-fine (20m/2m/1m) scan that
re-centers on the densest run of adjacent material disagreements each pass, used to confirm Root
Cause #2's real mechanism (see above). Kept in the repo as a reusable diagnostic for any future
"is this speckle a generator problem or a shader problem" question — swap `PLANET_SEED`/target
material and rerun.

**`modules/eden_planet_gen/eden_planet_generator_v1.{h,cpp}`** (this session):
- `sample_biome_at(dir)` — GDScript-bound thin wrapper around `sample_surface()` returning just
  the biome enum; added for `voxel_data_probe.gd`'s Step 1 diagnostic. Debug/probe helper, fine
  to delete once nothing references it.

---

## Verification

1. Run `demo_eden/eden_foliage_demo.tscn` **without** `--headless` (headless forces Godot's dummy
   renderer, which can't produce screenshots — `Cannot call method 'save_png' on a null value`).
   It writes `foliage_demo_{aerial,forest_closeup,grassland_closeup}.png` and quits.
2. **Pass:** solid material regions with clean curved boundaries; no per-pixel two-color
   alternation on slopes. Confirmed 2026-08-21 on the current build.
3. Check `foliage_demo_run.log` for `ERROR`/`SHADER ERROR`/`SCRIPT ERROR`, and the
   `"Single texturing mode expects 8-bit indices"` warning — that warning means the `VoxelFormat`
   did not take effect.
4. Regression-check MIXEL4 consumers still render: `eden_materials_showcase.tscn`,
   `eden_stress_test.tscn` (they rely on `single_material_mode` staying `false`). Both confirmed
   clean 2026-08-21.
