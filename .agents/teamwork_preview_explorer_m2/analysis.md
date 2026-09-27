# Technical Analysis Report: Milestone 2 — Micro-Voxel Renderer Architecture & LOD System

## Executive Summary
Milestone 2 establishes the core high-performance GPU compute rendering, LOD spatial hierarchy, material packaging, and dual-path physics mesh generation for the `GodotEden` engine module. The architecture directly integrates Vulkan `RenderingDevice` compute dispatches with custom SVO/SVDAG pointerless octrees, GPU clipmap LOD rings, std430-aligned 32-byte material pipelines, and hybrid physics meshing (Greedy Meshing + Dual Contouring).

---

## 1. `LodOctree` SVO Pool & SVDAG DAG Hash Deduplication (`SvoDagKeyHasher`)

### 1.1 `SvoNode` GPU Memory Layout
- **Alignment & Size**: Exactly **16 Bytes**, matching Vulkan std430 SSBO alignment requirements.
- **Fields**:
  - `child_mask` (`uint32_t`, 4B): Bits 0–7 denote active child octants.
  - `first_child_idx` (`uint32_t`, 4B): Index into `nodes` pool array of the first child among 8 contiguous sub-octants (0 for leaf nodes).
  - `material_tag` (`uint32_t`, 4B): Material tag ID for attribute palette lookup.
  - `sdf_value` (`float`, 4B): Signed Distance Field scalar value.

```cpp
struct SvoNode {
	uint32_t child_mask = 0;       // Bits 0-7: active child mask
	uint32_t first_child_idx = 0;  // Index in pool of first contiguous child
	uint32_t material_tag = 0;     // Material attribute ID / material tag
	float sdf_value = 0.0f;        // Representative Signed Distance Function value
};
```

### 1.2 `SvoDagKey` & Murmur3 Hash Deduplication (`SvoDagKeyHasher`)
- **Key Struct**: `SvoDagKey` encapsulates `children[8]` (`uint32_t[8]`), `material_tag` (`uint32_t`), and `child_mask` (`uint8_t`).
- **Equality Comparison**: Verifies `child_mask`, `material_tag`, and all 8 child indices match.
- **Murmur3 Hasher**: `SvoDagKeyHasher` uses Godot core hash primitives `hash_murmur3_one_32` for `child_mask`, `material_tag`, and each child index, finalized via `hash_fmix32`.

```cpp
struct SvoDagKeyHasher {
	static _FORCE_INLINE_ uint32_t hash(const SvoDagKey &p_key) {
		uint32_t h = hash_murmur3_one_32(p_key.child_mask);
		h = hash_murmur3_one_32(p_key.material_tag, h);
		for (int i = 0; i < 8; i++) {
			h = hash_murmur3_one_32(p_key.children[i], h);
		}
		return hash_fmix32(h);
	}
};
```

### 1.3 Deduplication & Compression Engine
- `LodOctree` manages a contiguous `Vector<SvoNode> nodes` pool and a `HashMap<SvoDagKey, uint32_t, SvoDagKeyHasher> dag_hash_map`.
- `insert_dag_branch_raw`: Queries `dag_hash_map` via `find_matching_dag_node`. If a matching key exists, returns the existing node index without adding a duplicate branch. Otherwise, pushes the new `SvoNode` to `nodes` and registers it in `dag_hash_map`.
- **Compression Metric**: `get_compression_ratio(total_leaves)` calculates $(1.0 - \frac{\text{node\_count}}{\text{total\_leaves}}) \times 100.0\%$.
- **SSBO Export**: `get_ssbo_buffer_bytes()` performs a direct `memcpy` of the contiguous `nodes` vector into a `PackedByteArray` for zero-copy GPU SSBO upload.

---

## 2. `VoxelRendererRD` Vulkan Compute Raymarching Pipeline & Clipmap LOD Updates

### 2.1 Vulkan `RenderingDevice` Integration & Pipelines
- Singleton access via `RenderingDevice::get_singleton()`.
- Pipeline initialization creates two distinct Vulkan compute pipelines from compiled SPIR-V bytecode (`micro_voxel_raymarch.glsl.gen.h` and `clipmap_lod.glsl.gen.h`):
  1. `raymarch_pipeline`: Executes micro-voxel DDA raymarching.
  2. `clipmap_pipeline`: Dynamically updates LOD clipmap ring metadata on GPU.

### 2.2 SSBO Storage Buffer Allocation
| Buffer | Binding | Struct Layout | Purpose |
|---|---|---|---|
| `svo_ssbo_buffer` | Set 0, Binding 1 | `SvoNode[]` (16B) | Serialized pointerless octree DAG |
| `clipmap_ssbo_buffer` | Set 0, Binding 2 / Binding 0 | `ClipmapLevelGpu[]` (32B) | LOD ring centers, voxel scales, toroidal offsets |
| `material_palette_ssbo_buffer` | Set 0, Binding 3 | `AtcPackedGpuMaterial[]` (32B) | Global material palette (256 entries) |
| `render_target_image` | Set 0, Binding 0 | RGBA32F 2D Storage Image | Raymarch compute output texture |

### 2.3 Clipmap Ring Center Updates & Toroidal Offsets
- **Update Logic (`update_lod_clipmap`)**:
  - Voxel scale per LOD level $i$: $S_i = \text{base\_voxel\_scale} \times 2^i$.
  - Snapped camera center: $\text{center} = \lfloor \frac{\text{camera\_position}}{S_i} \rfloor \times S_i$.
  - Toroidal offset: $\text{toroidal\_offset} = \lfloor \frac{\text{center}}{S_i} \rfloor \pmod{\text{grid\_extent}}$.
  - Blend margin: $15\%$ of outer ring radius ($0.15 \times \text{grid\_extent} \times 0.5 \times S_i$).

```cpp
float scale = base_voxel_scale * Math::pow(2.0f, (float)i);
Vector3 snapped(
		Math::floor(camera_position.x / scale) * scale,
		Math::floor(camera_position.y / scale) * scale,
		Math::floor(camera_position.z / scale) * scale);
Vector3i grid_cell(
		(int)Math::floor(snapped.x / scale),
		(int)Math::floor(snapped.y / scale),
		(int)Math::floor(snapped.z / scale));
clipmap_levels.write[i].toroidal_offset[0] = grid_cell.x % clipmap_grid_extent;
clipmap_levels.write[i].toroidal_offset[1] = grid_cell.y % clipmap_grid_extent;
clipmap_levels.write[i].toroidal_offset[2] = grid_cell.z % clipmap_grid_extent;
```

### 2.4 Compute Dispatches & Shader Algorithms
- **Raymarcher Dispatch**: Group dispatch $(8 \times 8 \times 1)$ for screen resolution $(W+7)/8 \times (H+7)/8$.
- **Raymarcher Shader (`micro_voxel_raymarch.glsl`)**:
  - Calculates NDC coordinates and ray direction from camera transform & FOV.
  - Performs ray-AABB intersection test against world bounds `[-512, 512]`.
  - Performs DDA traversal down `svo_dag` using `child_mask` and GLSL `bitCount`.
  - Reads material attributes from `material_buffer` (unpacks RGBA8 albedo, Oct16 normal, RGB565 emission, Roughness/Metallic) to calculate diffuse directional lighting.
- **Clipmap Compute Shader (`clipmap_lod.glsl`)**:
  - Group dispatch $(64 \times 1 \times 1)$ over LOD levels $(N+63)/64$. Parallel GPU recalculation of ring centers, scale, toroidal offset, and blend margins.

---

## 3. `AtcAttributePipeline` GPU Material Packing (std430 Alignment, 32-Byte Layout)

### 3.1 32-Byte `GpuMaterialData` Layout
- Standardized 32-byte layout fully compliant with GPU std430 alignment:

| Field | Type | Offset / Size | Packing Format |
|---|---|---|---|
| `albedo_rgba8` | `uint32_t` | 0..3 (4B) | RGBA 8-bit channels (`r \| g<<8 \| b<<16 \| a<<24`) |
| `normal_oct16` | `uint32_t` | 4..7 (4B) | Octahedral 16-bit normal (`u8` in bits 0-7, `v8` in bits 8-15) |
| `roughness_metallic` | `uint32_t` | 8..11 (4B) | `u16` Roughness (0-15) + `u16` Metallic (16-31) |
| `emissive_flags` | `uint32_t` | 12..15 (4B) | `u16` Emissive intensity (0-15) + `u16` Flags (16-31) |
| `emission_rgb565` | `uint32_t` | 16..19 (4B) | RGB 565 emissive color (`r5<<11 \| g6<<5 \| b5`) |
| `u_scale` | `float` | 20..23 (4B) | Triplanar U scale factor |
| `v_scale` | `float` | 24..27 (4B) | Triplanar V scale factor |
| `texture_index` | `uint32_t` | 28..31 (4B) | Bindless texture array index |

### 3.2 Octahedral Normal Math (`encode_normal_oct16` / `decode_normal_oct16`)
- Projects unit normal vector onto octahedral $L_1$ norm space ($|n_x| + |n_y| + |n_z|$).
- Encodes $n_z < 0$ hemisphere using octahedral folding $(1 - |v|) \times \text{sign}(u)$.
- Maps $[-1, 1]$ coordinates to uint8 bytes for $u$ and $v$.
- Decoding reconstructs $z = 1 - |u| - |v|$, unfolds lower hemisphere if $z < 0$, and normalizes vector.

```cpp
uint32_t AtcAttributePipeline::encode_normal_oct16(const Vector3 &p_normal) {
	Vector3 n = p_normal.is_zero_approx() ? Vector3(0.0f, 1.0f, 0.0f) : p_normal.normalized();
	float l1 = Math::abs(n.x) + Math::abs(n.y) + Math::abs(n.z);
	float u = n.x / l1;
	float v = n.y / l1;
	if (n.z < 0.0f) {
		float u_old = u;
		u = (1.0f - Math::abs(v)) * (u_old >= 0.0f ? 1.0f : -1.0f);
		v = (1.0f - Math::abs(u_old)) * (v >= 0.0f ? 1.0f : -1.0f);
	}
	uint32_t u_byte = (uint32_t)Math::clamp((int)Math::round((u * 0.5f + 0.5f) * 255.0f), 0, 255);
	uint32_t v_byte = (uint32_t)Math::clamp((int)Math::round((v * 0.5f + 0.5f) * 255.0f), 0, 255);
	return u_byte | (v_byte << 8);
}
```

### 3.3 Palette Management & Conversions
- `MAX_GLOBAL_MATERIALS = 256`: Total SSBO buffer size is $256 \times 32 = 8192$ bytes.
- `convert_voxel_buffer_to_atc`: Converts 3D `VoxelBuffer` into 4-byte packed material tag + int16 SDF per voxel.
- `convert_attributes_to_packed_floats`: Converts `VoxelBuffer` attributes into 8-float array per voxel (RGBA, Rough, Metal, Normal XYZ).

---

## 4. `PhysicsMeshGenerator` Dual-Path Mesh Generation

### 4.1 Architecture & Meshing Modes
- **Supported Modes**:
  1. `MESHING_GREEDY_MESHING`: Fast blocky quad consolidation for physics colliders.
  2. `MESHING_DUAL_CONTOURING`: Smooth SDF surface contouring for organic terrain colliders.

### 4.2 Greedy Meshing Pipeline (`generate_greedy_mesh_faces`)
1. Sweeps through 3 primary axes $d \in \{X, Y, Z\}$ across directions $-1$ and $+1$.
2. For each 2D slice, builds a 2D mask matching active solid surface boundaries against `iso_threshold`.
3. Greedily expands quad width along `v_axis` up to `max_quad_size`, then expands height along `u_axis`.
4. Outputs 2 triangles (6 vertices) per consolidated quad with proper face winding.
5. **Efficiency**: For a $4 \times 4 \times 4$ voxel cube, raw meshing requires 192 triangles (576 vertices); greedy meshing consolidates faces into 12 triangles (36 vertices), achieving **$93.75\%$ vertex reduction**.

### 4.3 Dual Contouring Pipeline (`generate_dual_contouring_faces`)
1. Iterates over grid cells $(x, y, z)$.
2. Evaluates sign crossings on cell edges (X-edge, Y-edge, Z-edge) relative to `iso_threshold`.
3. Evaluates 8 corner SDF values per cell and tests 12 cell edges for zero crossings.
4. Computes exact edge intersection points via linear interpolation $t = \frac{\text{iso} - v_0}{v_1 - v_0}$.
5. Positions dual vertex inside the cell by averaging intersection points.
6. Connects dual vertices across active edge crossings to form smooth quad faces.

### 4.4 Godot Physics Integration
- Converts triangle face arrays directly into Godot engine `ConcavePolygonShape3D` resources (`shape->set_faces(faces)`).
- `generate_data_map_collision_shape` merges multi-block data map regions with 3D block coordinate offsets.

---

## 5. Verification & Unit Tests Summary
All components are verified by native doctest unit tests in `modules/godot_eden/tests/test_rendering.h`:
- ClassDB registration & `Node3D` / `RefCounted` inheritance.
- Headless safety, property clamping (LOD levels 1–16, FOV, render target size).
- Material tag registration, Oct16 normal encoding/decoding roundtrips, GPU 8192-byte palette export.
- Greedy quad consolidation verification (36 vertices for $4 \times 4 \times 4$ block).
- Dual contouring spherical SDF mesh generation & `ConcavePolygonShape3D` creation.
