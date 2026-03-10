# terrain_smooth.gdshader — Setup Guide

## What this shader does
- Triplanar texture projection (no UVs needed — works on any voxel mesh shape)
- Mixel4 voxel texture blending (4 textures per voxel, driven by VoxelGeneratorGraph or VoxelTool painting)
- Procedural slope → rock overlay (steep = rock, regardless of voxel data)
- Procedural height → snow cap
- Virtual detail normalmap support (VoxelLodTerrain detail rendering)
- Transvoxel LOD seam correction
- LOD cross-fade dithering

---

## Step 1 — Create a Texture2DArray

In the Godot editor:
1. Import your terrain textures (albedo, normal, roughness) as plain Images (not StreamTexture)
2. Create a `Texture2DArray` resource
3. Add layers **in this order** (must match the slot comments in the shader):
   - Layer 0: grass albedo
   - Layer 1: rock  albedo
   - Layer 2: snow  albedo
   - Layer 3: sand  albedo
   - Layer 4: dirt  albedo
   - Layer 5: moss  albedo
4. Do the same for normal maps → `u_normal_array`
5. Do the same for roughness maps → `u_roughness_array`

All three arrays must have the same layer count and the same layer order.

---

## Step 2 — Create a ShaderMaterial

1. In the Inspector, on your `VoxelLodTerrain` node, set **Material → New ShaderMaterial**
2. In the ShaderMaterial, set **Shader → Load → eden/shaders/terrain_smooth.gdshader**
3. Assign your three `Texture2DArray` resources to:
   - `u_texture_array`
   - `u_normal_array`
   - `u_roughness_array`

---

## Step 3 — Configure VoxelMesherTransvoxel

On the `VoxelMesherTransvoxel` resource attached to your terrain:
- Set `texture_mode` to **Mixel4** (for 4-texture blending) or **Single** (for single index, simpler)
- If using **Single**: in your `VoxelFormat`, set `indices_depth` to `8-bit`

---

## Step 4 — Enable Collisions (Jolt)

On `VoxelLodTerrain`:
- Check **generate_collisions = true**
- Set `collision_lod_count` = 1 (only LOD0 gets physics collision; higher LODs are visual only)
- Set `collision_margin` = 0.04 (default works well with Jolt)

Jolt handles `ConcavePolygonShape3D` (which the voxel module generates per chunk) natively.
No extra work needed — it just works.

---

## Step 5 — Enable LOD Fading (optional, reduces pop-in)

On `VoxelLodTerrain`:
- Set `lod_fade_duration` = 0.25

The shader already has the dithered fade code.

---

## Step 6 — Enable Detail Normalmap (optional, improves distance quality)

On `VoxelLodTerrain`, in the **Normalmap** section:
- Enable `enable_normalmap`
- Start at LOD 2 (LOD 0 and 1 are close enough not to need it)
- Set `max_deviation_degrees` = 60
- Check `run_on_gpu` if you have a GPU supporting Vulkan compute (much faster)

The shader already has all the detail normalmap uniforms wired up.

---

## Slot mapping for VoxelGeneratorGraph

When using `VoxelGeneratorGraph` with material output nodes:

| Texture Slot | What to paint |
|---|---|
| 0 | Grass / ground cover |
| 1 | Rock / cliff face |
| 2 | Snow |
| 3 | Sand / beach |
| 4 | Dirt / path |
| 5 | Moss / wet rock |

In the generator graph, add an `OutputSingleTexture` or `OutputWeight_*` node and connect
a `Select` or noise-driven value to it. The slope/snow blending in the shader will layer
on top of whatever the voxel data says.

---

## Biome-driven material painting (VoxelGeneratorGraph approach)

To drive material indices from the generator instead of the procedural slope overlay:

1. In VoxelGeneratorGraph, create a biome weight calculation branch
2. Connect to `OutputWeight_0` (grass), `OutputWeight_1` (rock), `OutputWeight_2` (snow) etc.
3. The mesher packs these into `CUSTOM1` → the shader reads and blends them

You can then reduce or remove the procedural slope/snow section in the shader
(lines under "Procedural slope/height overlay") if voxel data fully drives it.

---

## TODO / future upgrades

- [ ] Add wetness mask (rain/wet rock darkening)
- [ ] Add puddle normal distortion at low height
- [ ] Add emission mask for lava/glowing ore
- [ ] Per-biome tint via a lookup texture sampled by world XZ position
- [ ] Replace AO approximation with proper AO baked into roughness alpha
