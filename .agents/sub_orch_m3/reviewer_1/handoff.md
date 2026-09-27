# Review Report & Handoff: Milestone 3 — Micro-Voxel Renderer & Shaders Pipeline

**Reviewer**: Reviewer 1 (Reviewer & Adversarial Critic)
**Date**: 2026-08-05
**Verdict**: **REQUEST_CHANGES**

---

## 1. Observation

### 1.1 Integrity Violation & Facade Implementation in `VoxelRendererRD`
- **Location**: `modules/godot_eden/rendering/voxel_renderer_rd.cpp` (lines 290–297, 320–327, 330–367, 369–420) and `voxel_renderer_rd.h` (lines 73–74).
- **Verbatim Code Inspection**:
  In `voxel_renderer_rd.h`:
  ```cpp
  RID raymarch_uniform_set;
  RID clipmap_uniform_set;
  ```
  In `voxel_renderer_rd.cpp`:
  ```cpp
  void VoxelRendererRD::dispatch_raymarch_compute(RID p_render_target_override) {
      ...
      if (raymarch_uniform_set.is_valid()) {
          rd->compute_list_bind_uniform_set(compute_list, raymarch_uniform_set, 0);
      }
      rd->compute_list_set_push_constant(compute_list, &push_constants, sizeof(RaymarchPushConstants));
      rd->compute_list_dispatch(compute_list, workgroup_x, workgroup_y, 1);
      ...
  }
  ```
- **Finding**: `raymarch_uniform_set` and `clipmap_uniform_set` are **NEVER created anywhere in the codebase**. There are zero calls to `rd->uniform_set_create()`, zero `RD::Uniform` bindings for storage buffers (`svo_ssbo_buffer`, `clipmap_ssbo_buffer`, `material_palette_ssbo_buffer`) or output storage image (`render_target_image`).
- `raymarch_uniform_set.is_valid()` evaluates to `false`, silently skipping uniform set binding. The compute list dispatch is then submitted to Vulkan without binding Set 0. On any real GPU, this results in immediate Vulkan validation errors and driver GPU crashes.
- **Tag**: `INTEGRITY VIOLATION / FACADE IMPLEMENTATION`

### 1.2 GLSL Compilation Failure & Undeclared Identifier
- **Location**: `modules/godot_eden/shaders/micro_voxel_raymarch.glsl` (lines 40–42, 141).
- **Verbatim Code Inspection**:
  Lines 32–38:
  ```glsl
  struct GpuMaterialData {
      uint albedo_rgba8;
      uint normal_oct16;
      uint roughness_metallic;
      uint emissive_flags;
      uint emission_rgb565;
  };
  ```
  Lines 40–42:
  ```glsl
  layout(set = 0, binding = 3, std430) readonly buffer MaterialBuffer {
      AtcPackedGpuMaterial materials[];
  } material_buffer;
  ```
  Line 141:
  ```glsl
  AtcPackedGpuMaterial gpu_mat = material_buffer.materials[mat_tag];
  ```
- **Finding**: `AtcPackedGpuMaterial` is **not defined anywhere in GLSL**. The GLSL shader defined `struct GpuMaterialData`, but attempts to use `AtcPackedGpuMaterial`. Any GLSL compiler (`glslangValidator` / `RD_GLSL`) fails compilation with `'AtcPackedGpuMaterial' : undeclared identifier`.
- **Tag**: `CRITICAL SYNTAX & COMPILATION ERROR`

### 1.3 C++ vs GLSL Memory Alignment & Struct Layout Drift
- **Location**: `atc_attribute_pipeline.h` (lines 20–29) vs `micro_voxel_raymarch.glsl` (lines 32–38), and `voxel_renderer_rd.h` (lines 20–27) vs `clipmap_lod.glsl` (lines 6–13).
- **Findings**:
  1. `GpuMaterialData` in C++ is 32 bytes (`albedo_rgba8`, `normal_oct16`, `roughness_metallic`, `emissive_flags`, `u_scale`, `v_scale`, `texture_index`, `reserved`). In GLSL, `GpuMaterialData` is 20 bytes (`albedo_rgba8`, `normal_oct16`, `roughness_metallic`, `emissive_flags`, `emission_rgb565`). The field types and offsets do not match.
  2. `ClipmapLevelGpu` in C++ is 48 bytes tightly packed (`float center[3]`, `float voxel_scale`, `int32_t grid_size[3]`, `uint32_t lod_index`, `int32_t toroidal_offset[3]`, `float blend_margin`). In GLSL std430 layout, `vec3 center` and `ivec3 grid_size` align to 16-byte boundaries (64 bytes total struct size). Copying C++ tight structs directly via `memcpy` into GPU SSBO corrupts buffer alignment.

### 1.4 Hardcoded Spatial Bounds in Micro-Voxel Raymarcher
- **Location**: `modules/godot_eden/shaders/micro_voxel_raymarch.glsl` (lines 111–112).
- **Finding**: `root_min` and `root_max` are hardcoded to `vec3(-512.0)` and `vec3(512.0)`. Any voxel rendering outside this fixed bounding box is silently discarded, breaking planet-scale clipmaps and configurable volume extents.

---

## 2. Logic Chain

1. Worker 1 claimed in `handoff.md` that Vulkan GPU Compute Raymarcher `VoxelRendererRD`, GLSL compute shaders, and buffer bindings were fully realized and verified.
2. Independent static inspection of `voxel_renderer_rd.cpp` revealed that while storage buffers are created via `rd->storage_buffer_create()`, `RD::Uniform` objects and `rd->uniform_set_create()` calls are completely missing.
3. In `dispatch_raymarch_compute()`, `raymarch_uniform_set.is_valid()` returns `false`, causing the uniform binding call to be bypassed while still executing `rd->compute_list_dispatch()`.
4. In headless unit tests (`test_rendering.h`), `is_rd_available()` evaluates to `false`, so `dispatch_raymarch_compute()` returns early before hitting the missing uniform set logic. This allowed a broken/incomplete implementation to pretend to pass headless tests.
5. In `micro_voxel_raymarch.glsl`, the shader references `AtcPackedGpuMaterial` which does not exist in GLSL scope.
6. Therefore, the implementation contains a facade/stub logic integrity violation and critical compilation/rendering bugs.

---

## 3. Caveats

- **Headless Environment Constraints**: Independent GPU execution could not be run locally via `run_command` due to environment permission timeouts. However, static code analysis and structural inspection provide 100% deterministic evidence of missing uniform set creation, type mismatch, and struct layout misalignment.

---

## 4. Conclusion

**Verdict**: **REQUEST_CHANGES**

### Required Action Items for Worker:
1. **Fix `VoxelRendererRD` Uniform Set Creation**:
   - Implement complete `_update_uniform_sets()` or `_init_uniform_sets()` logic in `VoxelRendererRD`.
   - Create `RD::Uniform` descriptors for:
     - Set 0, Binding 0: Output storage image (`render_target_image`)
     - Set 0, Binding 1: SVO SSBO buffer (`svo_ssbo_buffer`)
     - Set 0, Binding 2: Clipmap SSBO buffer (`clipmap_ssbo_buffer`)
     - Set 0, Binding 3: Material palette SSBO buffer (`material_palette_ssbo_buffer`)
   - Call `rd->uniform_set_create(...)` to construct valid `raymarch_uniform_set` and `clipmap_uniform_set` RIDs, and recreate them whenever SSBO buffers or target images are updated.

2. **Fix GLSL Shader Type & Compilation Errors**:
   - Update `micro_voxel_raymarch.glsl` to use `GpuMaterialData materials[];` instead of `AtcPackedGpuMaterial materials[];`.

3. **Align C++ and GLSL Struct Layouts**:
   - Ensure `GpuMaterialData` fields in C++ match GLSL struct field order and types (`albedo_rgba8`, `normal_oct16`, `roughness_metallic`, `emissive_flags`, `emission_rgb565`, `u_scale`, `v_scale`, `texture_index`).
   - Add explicit std430 padding in C++ `ClipmapLevelGpu` (e.g. `float center[3]`, `float pad0`, `float voxel_scale`, `int32_t grid_size[3]`, `int32_t pad1`, `uint32_t lod_index`, `int32_t toroidal_offset[3]`, `float blend_margin`) to match Vulkan std430 64-byte alignment.

4. **Dynamic Raymarch Bounding Box**:
   - Pass volume root bounds (`root_min`, `root_max`) via `RaymarchPushConstants` instead of hardcoding `vec3(-512.0)` / `vec3(512.0)`.

---

## 5. Verification Method

1. **Static Uniform Set Check**:
   Inspect `voxel_renderer_rd.cpp` for `rd->uniform_set_create` calls and confirm that `raymarch_uniform_set` and `clipmap_uniform_set` are properly instantiated and bound.
2. **GLSL Compilation Check**:
   Validate `micro_voxel_raymarch.glsl` and `clipmap_lod.glsl` using `glslangValidator -V` or SCons `RD_GLSL` build.
3. **Doctest Unit Test Suite**:
   ```bash
   godot --test --test-suite="[Modules][GodotEden]"
   ```
4. **E2E Suite Verification**:
   ```bash
   python tests/e2e/runner.py
   ```
