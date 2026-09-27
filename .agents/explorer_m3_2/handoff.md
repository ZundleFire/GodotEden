# Handoff Report: Concentric Clipmap LOD Pipeline & ATC Attribute Pipeline

**Agent**: Explorer 2 (`explorer_m3_2`)  
**Milestone**: Milestone 3  
**Focus Area**: Concentric Clipmap LOD Pipeline & Allocation-Tagging-Conversion (ATC) Attribute Pipeline (`AtcAttributePipeline`)  
**Date**: 2026-08-05  

---

## 1. Observation

Direct observations from the existing codebase and configuration:
- **`modules/godot_eden/storage/voxel_buffer.h` (lines 19-31, 41-55)**: `VoxelBuffer` contains 4 channels (`CHANNEL_SDF`, `CHANNEL_MATERIAL`, `CHANNEL_COLOR`, `CHANNEL_CUSTOM`) with 16-entry palette compaction (`COMPRESSION_PALETTE`, 2048 bytes nibble data) or raw storage.
- **`modules/godot_eden/storage/lod_octree.h` (lines 16-21)**: `SvoNode` is 16-byte aligned for GPU SSBO storage:
  ```cpp
  struct SvoNode {
      uint32_t child_mask = 0;       // Bits 0-7: active children
      uint32_t first_child_idx = 0;  // Index of first contiguous child
      uint32_t material_tag = 0;     // Material attribute ID / tag
      float sdf_value = 0.0f;        // Signed Distance Function value
  };
  ```
- **`modules/godot_eden/rendering/voxel_renderer_rd.h` (lines 18-38, 118-139)**: `VoxelRendererRD` defines `ClipmapLevelGpu` (32 bytes `std430` layout) and implements `update_lod_clipmap()`, `upload_clipmap_ssbo()`, and `dispatch_clipmap_compute()` targeting `shaders/clipmap_lod.glsl`.
- **`modules/godot_eden/shaders/clipmap_lod.glsl` (lines 6-15, 22-35)**: Compute shader layout uses `ClipmapLevel levels[]` SSBO at set 0, binding 0, updating snapped center coordinates: `scale = pow(2.0, float(id)); vec3 snapped_center = floor(params.camera_pos / scale) * scale;`.
- **`modules/godot_eden/shaders/micro_voxel_raymarch.glsl` (lines 26-28, 85-91)**: Micro-voxel raymarcher binds `ClipmapBuffer` at set 0, binding 2 and samples `SvoNode` material tags.
- **Missing File**: `modules/godot_eden/rendering/atc_attribute_pipeline.h/cpp` does not yet exist and needs to be created by Worker for Feature F11.

---

## 2. Logic Chain

1. **Clipmap LOD Geometry**: Ring level $k$ voxel scale is $S_k = 2^k$. Snapping camera position $\mathbf{C}$ via $\mathbf{C}_k = \lfloor \mathbf{C} / S_k \rfloor S_k$ guarantees voxel alignment and avoids temporal spatial jitter during camera translation.
2. **Buffer Synchronization**: `VoxelRendererRD` supports both host CPU upload via `upload_clipmap_ssbo()` (`rd->storage_buffer_create`) and GPU compute execution via `dispatch_clipmap_compute()` (`clipmap_lod.glsl`). The dual mode allows host CPU streaming logic to control chunk loading while GPU compute updates ring parameters in 1 pass before raymarching.
3. **ATC Material Pipeline Architecture**: `SvoNode::material_tag` (32-bit uint) and `VoxelBuffer::CHANNEL_MATERIAL` require a centralized manager (`AtcAttributePipeline`) to translate material tags into physical surface parameters (albedo, normals, roughness, metallic, emission) without expanding per-voxel memory.
4. **Attribute Quantization**: High-precision C++ materials are converted to 16-byte packed structs (`AtcPackedGpuMaterial`) using RGBA8 (color), Oct16 (octahedral normal encoding), RGB565 (emission), and packed uint16 (roughness/metallic). This reduces GPU material buffer bandwidth by $75\%$ compared to unpacked floats (64 bytes -> 16 bytes).

---

## 3. Caveats

- **No Source Code Modifications**: As a read-only Explorer, no source files in `modules/godot_eden/` were modified. Complete designs, headers, and code specifications are provided in `analysis.md` and this handoff.
- **Compute Shader Binding Indexing**: When `AtcAttributePipeline` is integrated into `VoxelRendererRD`, binding 3 in set 0 (`layout(set = 0, binding = 3, std430) readonly buffer MaterialBuffer { GpuMaterial materials[]; }`) should be added to `shaders/micro_voxel_raymarch.glsl`.

---

## 4. Conclusion

The design for both the Concentric Clipmap LOD Pipeline and the Allocation-Tagging-Conversion (ATC) Attribute Pipeline (`AtcAttributePipeline`) is complete, fully specified, and mathematically validated. The implementer (Worker) can proceed directly with creating `modules/godot_eden/rendering/atc_attribute_pipeline.h` and `rendering/atc_attribute_pipeline.cpp`, registering `AtcAttributePipeline` in ClassDB (`register_types.cpp`), adding SCons build rules (`SCsub`), and extending `test_main.h` unit tests.

---

## 5. Verification Method

To independently verify the implementation after Worker code generation:
1. **Compilation & ClassDB Registration Check**:
   Inspect `modules/godot_eden/register_types.cpp` and run:
   ```bash
   godot --test "[Modules][GodotEden] ClassDB Registration Verification"
   ```
2. **ATC Bit-Packing & Quantization Doctest**:
   Execute Doctest unit test suite covering `AtcAttributePipeline`:
   ```bash
   godot --test "[Modules][GodotEden] AtcAttributePipeline Bit-Packing & SSBO Export"
   ```
   *Expected result*: Pass all subcases for RGBA8 packing, Oct16 normal encoding, RGB565 emission, and 16-byte `AtcPackedGpuMaterial` SSBO size (`slots.size() * 16` bytes).
3. **E2E Test Runner**:
   ```bash
   python tests/e2e/runner.py --feature F10 F11
   ```
   *Expected result*: 100% pass across Feature F10 (Concentric Clipmap LOD) and Feature F11 (ATC Attribute & Material System).

---
