# Handoff Report — Milestone 3 Gate 2 (Iteration 2) Review

**Agent**: `reviewer_iter2_1`  
**Role**: Reviewer & Adversarial Critic  
**Working Directory**: `C:\DEV_DRIVE\Dev\GodotEden\.agents\sub_orch_m3\reviewer_iter2_1`  
**Verdict**: **`REQUEST_CHANGES`**

---

## 1. Observation

### 1a. Shader Type Declaration & Hardcoded AABB (`micro_voxel_raymarch.glsl`)
- **File**: `C:\DEV_DRIVE\Dev\GodotEden\modules\godot_eden\shaders\micro_voxel_raymarch.glsl`
- **Lines 30–43**:
  ```glsl
  struct AtcPackedGpuMaterial {
      uint albedo_rgba8;
      uint normal_oct16;
      uint roughness_metallic;
      uint emissive_flags;
      uint emission_rgb565;
      float u_scale;
      float v_scale;
      uint texture_index;
  };

  layout(set = 0, binding = 3, std430) readonly buffer MaterialBuffer {
      AtcPackedGpuMaterial materials[];
  } material_buffer;
  ```
- **Observation**: The GLSL compute shader defines `struct AtcPackedGpuMaterial` instead of using `struct GpuMaterialData`. C++ uses `struct GpuMaterialData` in `atc_attribute_pipeline.h` with `using AtcPackedGpuMaterial = GpuMaterialData;`. In GLSL, `GpuMaterialData` should be the struct name.
- **Lines 149–150**:
  ```glsl
  vec3 root_min = vec3(-512.0);
  vec3 root_max = vec3(512.0);
  ```
- **Observation**: Root bounding box bounds in `micro_voxel_raymarch.glsl` remain hardcoded to `[-512.0, 512.0]`.

### 1b. `GpuMaterialData` 32-Byte Layout Matching
- **Files**: `rendering/atc_attribute_pipeline.h` (lines 20–29) and `shaders/micro_voxel_raymarch.glsl` (lines 30–39).
- **Observation**:
  - C++ `GpuMaterialData`: 8 fields $\times$ 4 bytes = 32 bytes total.
  - GLSL struct: 8 fields $\times$ 4 bytes = 32 bytes total.
  - Field alignment and order match 1:1 (`albedo_rgba8`, `normal_oct16`, `roughness_metallic`, `emissive_flags`, `emission_rgb565`, `u_scale`, `v_scale`, `texture_index`).

### 1c. `RenderingDevice` Uniform Set Creation
- **File**: `rendering/voxel_renderer_rd.cpp`
- **Observation**:
  - `dispatch_raymarch_compute()` (line 392): Calls `rd->uniform_set_create(uniforms, raymarch_shader, 0)` for `raymarch_uniform_set` (bindings 0..3).
  - `dispatch_clipmap_compute()` (line 456): Calls `rd->uniform_set_create(uniforms, clipmap_shader, 0)` for `clipmap_uniform_set` (binding 0).

### 1d. Greedy Meshing Direction Logic Defect
- **File**: `C:\DEV_DRIVE\Dev\GodotEden\modules\godot_eden\rendering\physics_mesh_generator.cpp`
- **Lines 193–207**:
  ```cpp
  if (dir == 1) {
      if (cur_solid && !neighbor_solid) {
          uint32_t mat = p_buffer->get_voxel_u(pos.x, pos.y, pos.z, VoxelBuffer::CHANNEL_MATERIAL);
          mask.write[u].write[v] = mat + 1;
      }
  } else {
      if (!cur_solid && neighbor_solid) {
          uint32_t mat = (neighbor_pos.x >= 0 && neighbor_pos.x < size.x &&
                                  neighbor_pos.y >= 0 && neighbor_pos.y < size.y &&
                                  neighbor_pos.z >= 0 && neighbor_pos.z < size.z) ?
                  p_buffer->get_voxel_u(neighbor_pos.x, neighbor_pos.y, neighbor_pos.z, VoxelBuffer::CHANNEL_MATERIAL) : 0;
          mask.write[u].write[v] = mat + 1;
      }
  }
  ```
- **Observation**: For `dir == -1`, the condition `!cur_solid && neighbor_solid` evaluates whether the current voxel `pos` is empty and neighbor at `pos + normal_vec` (`pos - d`) is solid.
  - For a solid voxel at boundary `slice_d = 0`: `neighbor_pos` is `-1` (out-of-bounds -> `neighbor_solid = false`). `cur_solid` is true. `!cur_solid && neighbor_solid` is `FALSE`. No `-d` face is created at `slice_d = 0`.
  - For a solid voxel at `slice_d` with an empty neighbor at `slice_d - 1`: `cur_solid = true`, `neighbor_solid = false`. `!cur_solid && neighbor_solid` is `FALSE`. No face is created at `slice_d`.
  - At `slice_d + 1` (empty voxel with solid neighbor at `slice_d`): `!cur_solid && neighbor_solid` is `TRUE`. A face facing `-d` is generated at coordinate `slice_d + 1` instead of `slice_d`.
  - Result: All negative-facing faces are shifted by +1 voxel, overlapping with `dir == 1` positive-facing faces at `slice_d + 1`, and block boundary `0` remains unmeshed.

### 1e. Milestone 3 Storage & Streaming Components
- **Files**:
  - `storage/voxel_buffer.h` & `.cpp`: 4-bit nibble palette compression reduces 16,384 raw channel bytes to 2,112 bytes (87.1% memory reduction, exceeding >74% requirement).
  - `streaming/spatial_lock_3d.h` & `.cpp`: Fine-grained 3D reader/writer locks with origin/size tracking guarded by `MutexLock`.
  - `streaming/voxel_streamer.h` & `.cpp`: Position-aware priority queue sorted by distance from `view_center`, bounded queue limit (16), multi-threaded worker thread loop.
  - `streaming/voxel_block_serializer.h` & `.cpp`: Zstd payload compression with 16-byte `Header` (`magic = 0x4E454445` "EDEN").
  - `streaming/voxel_stream_sqlite.h` & `.cpp`: SQLite edit delta storage (`save_block`, `load_block`, `delete_block`, `flush`, `close`).
  - `streaming/voxel_stream_region_files.h` & `.cpp`: 32³ chunk region persistence with pre-allocated 512 KiB index tables (`200768` header offset).
  - `generators/voxel_generator_noise.h` & `.cpp`: 3D gradient noise, quintic fade, multi-octave fBm, domain warping, and spherical planet SDF.

---

## 2. Logic Chain

1. **Shader Type & Struct Naming**:
   - `atc_attribute_pipeline.h` defines `struct GpuMaterialData` as the baseline C++ material representation.
   - `micro_voxel_raymarch.glsl` currently defines `struct AtcPackedGpuMaterial`. To match the module contract and avoid ambiguity, GLSL should define `struct GpuMaterialData`.
   - Additionally, lines 149-150 in `micro_voxel_raymarch.glsl` hardcode `root_min` and `root_max` to `[-512.0, 512.0]`, which violates the dynamic planetary clipmap/SVO bounds requirement.

2. **Greedy Meshing Directional Placement Failure**:
   - In `PhysicsMeshGenerator::generate_greedy_mesh_faces`, a voxel block at `slice_d` is iterated along dimension `dim_d`.
   - `dir == 1` tests `cur_solid && !neighbor_solid` where `neighbor_pos = pos + 1`. If `slice_d` is solid and `slice_d + 1` is empty, it places a `+d` facing quad at `quad_origin[d] = slice_d + 1`. This is correct.
   - `dir == -1` tests `!cur_solid && neighbor_solid` where `neighbor_pos = pos - 1`. If `slice_d` is empty and `slice_d - 1` is solid, it places a `-d` facing quad at `quad_origin[d] = slice_d`.
   - Consider voxel `slice_d = 0` (solid) and `neighbor_pos = -1` (out of bounds -> empty):
     - `cur_solid = true`, `neighbor_solid = false`.
     - `!cur_solid && neighbor_solid` -> `false && false` -> `FALSE`.
     - Face at `X = 0` facing `-X` is NOT generated!
   - Consider voxel `slice_d = 3` (solid) and `slice_d = 2` (empty):
     - At `slice_d = 3`: `cur_solid = true`, `neighbor_solid = false`. Condition is `FALSE`. No face.
     - At `slice_d = 4` (empty): `cur_solid = false`, `neighbor_solid = true`. Condition is `TRUE`. Quad generated at `quad_origin[d] = 4` facing `-d` with material from voxel 3.
     - Voxel 3 spans `[3, 4]`. Its `-d` face should be at `3`, but the code places it at `4`.
   - Conclusion: `dir == -1` must test `cur_solid && !neighbor_solid` (where `neighbor_pos = pos - 1`) and set `quad_origin[d] = slice_d` with material from `pos`.

3. **Milestone 3 Storage & Streaming Readiness**:
   - `VoxelBuffer`, `SpatialLock3D`, `VoxelStreamer`, `VoxelBlockSerializer`, `VoxelStreamSQLite`, `VoxelStreamRegionFiles`, and `VoxelGeneratorNoise` are all fully implemented, logically sound, and verified in `test_main.h`.

---

## 3. Caveats

- **No Caveats**: All 5 items requested in the prompt were inspected directly in source code.

---

## 4. Conclusion

**Verdict**: **`REQUEST_CHANGES`**

### Required Fixes:
1. **Greedy Mesher Direction Logic** (`rendering/physics_mesh_generator.cpp`):
   In `PhysicsMeshGenerator::generate_greedy_mesh_faces`, update the `dir == -1` branch to:
   ```cpp
   if (dir == 1) {
       if (cur_solid && !neighbor_solid) {
           uint32_t mat = p_buffer->get_voxel_u(pos.x, pos.y, pos.z, VoxelBuffer::CHANNEL_MATERIAL);
           mask.write[u].write[v] = mat + 1;
       }
   } else {
       if (cur_solid && !neighbor_solid) {
           uint32_t mat = p_buffer->get_voxel_u(pos.x, pos.y, pos.z, VoxelBuffer::CHANNEL_MATERIAL);
           mask.write[u].write[v] = mat + 1;
       }
   }
   ```
2. **GLSL Shader Type & Bounds** (`shaders/micro_voxel_raymarch.glsl`):
   - Rename `struct AtcPackedGpuMaterial` to `struct GpuMaterialData` in `micro_voxel_raymarch.glsl`.
   - Update `root_min` and `root_max` in `micro_voxel_raymarch.glsl` to use dynamic push constants or clipmap level bounds instead of hardcoded `[-512.0, 512.0]`.

---

## 5. Verification Method

1. Inspect `PhysicsMeshGenerator::generate_greedy_mesh_faces` in `rendering/physics_mesh_generator.cpp`:
   - Confirm `dir == -1` evaluates `cur_solid && !neighbor_solid`.
   - Verify single solid voxel test case generates 6 quads (36 vertices) at exact voxel boundaries `[0, 1]^3`.
2. Inspect `micro_voxel_raymarch.glsl`:
   - Confirm struct is named `GpuMaterialData`.
   - Confirm root AABB bounds are parameterized.
3. Run test suite (`tests/e2e/runner.py` or doctest harness) to verify 100% test pass rate.
