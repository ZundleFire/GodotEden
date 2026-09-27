# Changes Report — Milestone 2 (Micro-Voxel Renderer Architecture & LOD System)

## Overview
Worker M2 conducted a full code audit and verification of the Milestone 2 rendering and LOD architecture for `GodotEden`. Key bugs and layout mismatches were identified and fixed to ensure genuine, robust, high-performance rendering and meshing pipelines.

## Modified Files & Summary

### 1. `modules/godot_eden/rendering/atc_attribute_pipeline.cpp`
- **Issue Fixed**: `dst.reserved = 0;` was attempting to write to a non-existent field on `GpuMaterialData`, causing a C++ build error.
- **Change**: Removed the invalid `dst.reserved` assignment from `update_conversion_buffers()`.
- **Impact**: Ensures compilation passes cleanly without struct member error.

### 2. `modules/godot_eden/rendering/voxel_renderer_rd.h` & `voxel_renderer_rd.cpp`
- **Issue Fixed**: `ClipmapLevelGpu` struct in C++ and `ClipmapLevel` in GLSL std430 had potential layout alignment mismatches due to unaligned `ivec3` / `vec3` fields in std430 layout.
- **Change**: Aligned `ClipmapLevelGpu` memory layout using 3 16-byte vector blocks: `center_and_scale` (vec4), `grid_size_and_lod` (ivec4), and `offset_and_margin` (vec4). Cast `toroidal_offset` assignments to float in `update_lod_clipmap`.
- **Impact**: Guarantees identical 48-byte SSBO buffer alignment between host C++ and Vulkan GPU shaders across all platforms and compilers.

### 3. `modules/godot_eden/shaders/clipmap_lod.glsl`
- **Change**: Updated GLSL `ClipmapLevel` struct layout to match 16-byte vector aligned `center_and_scale`, `grid_size_and_lod`, and `offset_and_margin` fields.
- **Impact**: Aligns compute shader clipmap updates with host SSBO layout.

### 4. `modules/godot_eden/shaders/micro_voxel_raymarch.glsl`
- **Issues Fixed**:
  1. Raymarching loop retained sub-node `current_idx` across ray steps without restarting traversal from root `0u` at new sample points, causing incorrect traversal after step 1.
  2. Sub-octant indexing used `bitCount` popcount indexing which mismatched `LodOctree` pool octant indexing (`first_child_idx + octant`).
- **Changes**: Introduced `sample_svo(vec3 norm_p, out uint mat_tag, out float sdf_val)` function that traverses the octree from root (`0u`) to leaf for any point using octant indexing `first_child_idx + octant`. Integrated `sample_svo` into `main()` raymarching.
- **Impact**: Ensures GPU raymarching correctly samples SVO voxels and aligns GPU tree traversal with CPU `LodOctree::sample_sdf_at`.

### 5. `modules/godot_eden/rendering/physics_mesh_generator.cpp`
- **Issue Fixed**: Quad vertex assignment in `generate_greedy_mesh_faces` resulted in inverted triangle winding order and cross-product normals facing inwards.
- **Changes**: Re-ordered quad vertices (`p0 = origin`, `p1 = origin + du`, `p2 = origin + du + dv`, `p3 = origin + dv`) and set triangle winding `(p0, p1, p2)` & `(p0, p2, p3)` for `dir == 1` (+ face) and `(p0, p2, p1)` & `(p0, p3, p2)` for `dir == -1` (- face).
- **Impact**: Produces correct outwards-facing surface normals for generated collision shapes.

### 6. `modules/godot_eden/tests/test_rendering.h`
- **Enhancement**: Added `#include "modules/godot_eden/storage/lod_octree.h"` and new `TEST_CASE("[Modules][GodotEden] LodOctree SVO Pool & SVDAG Key Deduplication")`.
- **Impact**: Explicitly tests `LodOctree` SVO pool memory layout, Murmur3 DAG hash deduplication (`SvoDagKeyHasher`), compression metrics, and SSBO serialization.

## Verification
All 5 Milestone 2 requirements have been verified:
1. SVO pool memory layout & Murmur3 SVDAG deduplication (`SvoDagKeyHasher`).
2. Vulkan `RenderingDevice` compute raymarching pipeline & clipmap LOD updates.
3. GPU material packing (std430 32-byte layout, Oct16 normal encoding).
4. Dual-path physics mesh generation (Greedy Meshing & Dual Contouring).
5. Comprehensive unit test suite (`modules/godot_eden/tests/test_rendering.h`).
