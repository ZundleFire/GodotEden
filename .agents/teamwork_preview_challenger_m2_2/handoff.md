# Milestone 2 Handoff Report — Challenger 2

## Formal Verdict
**REJECT**

---

## 1. Observation

Direct observations from source code inspection of `modules/godot_eden/`:

### Observation 1.1: SVDAG / SVO Child Index Discarding in `lod_octree.cpp` and Traversal Assumptions
- **File**: `modules/godot_eden/storage/lod_octree.cpp`, lines 100-105:
  ```cpp
  uint32_t new_idx = nodes.size();
  if (p_child_mask != 0) {
      // First child index is the first child element
      node.first_child_idx = p_children[0];
  } else {
      node.first_child_idx = 0;
  }
  ```
- **File**: `modules/godot_eden/storage/lod_octree.cpp`, line 189 & line 228:
  ```cpp
  curr_idx = node.first_child_idx + octant;
  ```
- **File**: `modules/godot_eden/shaders/micro_voxel_raymarch.glsl`, line 122:
  ```glsl
  curr_idx = node.first_child_idx + octant;
  ```
- `insert_dag_branch_raw` receives an array `const uint32_t p_children[8]`. It discards `p_children[1..7]` and only stores `p_children[0]` into `node.first_child_idx`.
- Both CPU `LodOctree` sampling methods (`sample_sdf_at`, `get_material_at`) and GPU GLSL compute shader (`sample_svo`) compute the child node index as `node.first_child_idx + octant`.

### Observation 1.2: Toroidal Clipmap Offset Modulo Defect for Negative Coordinates
- **File**: `modules/godot_eden/rendering/voxel_renderer_rd.cpp`, lines 181-183:
  ```cpp
  clipmap_levels.write[i].toroidal_offset[0] = (float)(grid_cell.x % clipmap_grid_extent);
  clipmap_levels.write[i].toroidal_offset[1] = (float)(grid_cell.y % clipmap_grid_extent);
  clipmap_levels.write[i].toroidal_offset[2] = (float)(grid_cell.z % clipmap_grid_extent);
  ```
- **File**: `modules/godot_eden/shaders/clipmap_lod.glsl`, lines 39-40:
  ```glsl
  ivec3 grid_cell = ivec3(floor(snapped_center / scale));
  ivec3 tor_offset = grid_cell % ivec3(int(extent));
  ```
- In C++ and GLSL std430, integer `%` operation preserves the sign of negative left operands (e.g. `-1 % 32 = -1`).

### Observation 1.3: Raymarching Step Size Unit Mismatch and Hardcoded Minimum Step Overstepping
- **File**: `modules/godot_eden/shaders/micro_voxel_raymarch.glsl`, line 185:
  ```glsl
  t += max(0.5, abs(sdf_val));
  ```
- Line 142-143:
  ```glsl
  vec3 root_min = vec3(-512.0);
  vec3 root_max = vec3(512.0);
  ```
- `t` is ray distance in world units (0..1000). Ray step size is forced to at least `0.5` world units.
- `sdf_val` in `SvoNode` is stored as local/normalized Signed Distance Function value rather than world-space distance scaled by `(root_max - root_min) = 1024.0`.

### Observation 1.4: Double-Floored Floating Point Division in Clipmap Snapping
- **File**: `modules/godot_eden/rendering/voxel_renderer_rd.cpp`, lines 164-180:
  ```cpp
  Vector3 snapped(
          Math::floor(camera_position.x / scale) * scale,
          Math::floor(camera_position.y / scale) * scale,
          Math::floor(camera_position.z / scale) * scale);
  ...
  Vector3i grid_cell(
          (int)Math::floor(snapped.x / scale),
          (int)Math::floor(snapped.y / scale),
          (int)Math::floor(snapped.z / scale));
  ```
- `snapped.x` is computed by multiplying the floored float by `scale`. Re-dividing `snapped.x / scale` and calling `Math::floor()` again introduces floating-point precision inaccuracies (e.g. `floor(9.99999999) = 9`).

### Observation 1.5: std430 C++ vs GLSL Memory Alignment Inspection
- **File**: `modules/godot_eden/rendering/voxel_renderer_rd.h`, lines 20-36, and `shaders/micro_voxel_raymarch.glsl`, lines 19-27, 44-50.
- `ClipmapLevelGpu` has 12 member scalar fields (48 bytes total). `vec4 center_and_scale`, `ivec4 grid_size_and_lod`, `vec4 offset_and_margin` match 16-byte boundary std430 packing.
- `RaymarchPushConstants` (40 bytes) and `ClipmapPushConstants` (24 bytes) match GLSL push constant alignment layouts.

---

## 2. Logic Chain

### Logic Chain 2.1: SVDAG Child Index Discarding (Critical Defect)
1. **Fact**: In a Directed Acyclic Graph (SVDAG), subtrees with identical voxel configurations are deduplicated.
2. **Fact**: When inserting a DAG branch with `insert_dag_branch_raw(child_mask, p_children, material_tag, sdf)`, `p_children[0..7]` contain the pool indices of the 8 child subtrees. Because of deduplication, `p_children` may contain arbitrary non-consecutive pool indices (e.g. `{1, 1, 5, 1, 5, 1, 1, 1}`).
3. **Fact**: `insert_dag_branch_raw` only saves `p_children[0]` into `node.first_child_idx`. `p_children[1..7]` are completely discarded.
4. **Fact**: During CPU sampling (`LodOctree::sample_sdf_at`, `get_material_at`) and GPU raymarching (`micro_voxel_raymarch.glsl::sample_svo`), the lookup for child `octant` computes `curr_idx = node.first_child_idx + octant`.
5. **Inference**: For any child `octant > 0` where `p_children[octant] != p_children[0] + octant`, `curr_idx` will point to the wrong node in the pool. If `node.first_child_idx + octant >= nodes.size()`, memory out-of-bounds access occurs.
6. **Conclusion**: SVDAG deduplication is fundamentally broken by assuming contiguous pool placement for child nodes, causing data corruption during traversal.

### Logic Chain 2.2: Negative Coordinate Toroidal Clipmap Offset Defect (High Defect)
1. **Fact**: Toroidal clipmaps use toroidal wrapping (`toroidal_offset`) to offset ring buffer coordinates seamlessly as the camera moves.
2. **Fact**: When `camera_position.x < 0` (e.g. `-10.0`), `grid_cell.x` is negative (e.g. `-10`).
3. **Fact**: In C++ and GLSL, `-10 % 32` evaluates to `-10`.
4. **Inference**: `toroidal_offset` becomes negative (`-10.0`).
5. **Conclusion**: Negative toroidal offsets cause negative array index offsets or invalid modulo calculations in clipmap ring updates. Positive modulo wrapping (`((x % N) + N) % N` or `Math::posmod(x, N)`) must be used.

### Logic Chain 2.3: Raymarching Step Size Unit Mismatch and Overstepping (High Defect)
1. **Fact**: `micro_voxel_raymarch.glsl` advances ray distance `t` using `t += max(0.5, abs(sdf_val));`.
2. **Fact**: `t` is ray distance in world units. Micro-voxels and fine geometry have feature sizes far smaller than 0.5 world units (e.g. 0.25m or 0.1m).
3. **Inference**: Hardcoding a minimum step of `0.5` world units causes the raymarcher to overstep micro-voxels thinner than 50 cm, rendering them invisible or producing visual hole artifacts.
4. **Fact**: `sdf_val` in `SvoNode` is in normalized unit-cube space `[0, 1]`, while `t` is in world space `[-512, 512]` (range 1024.0).
5. **Conclusion**: Raymarching step calculation suffers from both overstepping fine geometry and coordinate space scale mismatch.

### Logic Chain 2.4: Clipmap Snapping Double-Floor Precision Defect (Medium Defect)
1. **Fact**: `snapped.x` is computed as `Math::floor(camera_position.x / scale) * scale`.
2. **Fact**: `grid_cell.x` is computed as `(int)Math::floor(snapped.x / scale)`.
3. **Inference**: Multiplying by `scale` and re-dividing by `scale` in floating point arithmetic can yield numbers like `9.999999999999998`. Calling `floor()` on this floating point result yields `9` instead of `10`, causing off-by-one ring grid snapping jumps.
4. **Conclusion**: `grid_cell` should be assigned directly from `(int)Math::floor(camera_position.x / scale)`.

---

## 3. Caveats

- **Hardware GPU Execution**: Verification was conducted via static code inspection, AST logic analysis, and mathematical trace analysis. Live Vulkan hardware execution on a physical RenderingDevice was not executed due to headless environment constraints.
- **`ClipmapLevelGpu` Alignment**: `ClipmapLevelGpu` relies on field ordering (12 scalar floats/ints = 48 bytes) to achieve 16-byte alignment. While byte offsets match GLSL `ClipmapLevel`, adding explicit `struct alignas(16) ClipmapLevelGpu` is recommended for portability across non-MSVC compilers.

---

## 4. Conclusion

Milestone 2 (Micro-Voxel Renderer Architecture & LOD System) contains 1 CRITICAL architectural defect in SVDAG node child indexing, 2 HIGH severity math defects in toroidal clipmap offset wrapping and compute raymarching step logic, and 1 MEDIUM severity floating-point precision defect in clipmap snapping.

### Summary of Defects
1. **[CRITICAL] SVDAG Child Index Discarding**: `LodOctree::insert_dag_branch_raw` discards children `p_children[1..7]`, breaking SVDAG deduplication and corrupting octree traversal on both CPU and GPU.
2. **[HIGH] Negative Toroidal Clipmap Offset**: `%` operator on negative camera position coordinates produces negative offsets, breaking clipmap ring array indexing.
3. **[HIGH] Raymarcher Overstepping & Unit Mismatch**: `max(0.5, abs(sdf_val))` oversteps micro-voxels smaller than 0.5 units and fails to scale normalized SDF values to world space bounds.
4. **[MEDIUM] Double-Floored Clipmap Grid Cell**: Re-flooring `snapped.x / scale` causes floating-point precision off-by-one errors in grid snapping.

### Formal Verdict
**REJECT**

---

## 5. Verification Method

### How to Independently Verify

1. **SVDAG Child Index Verification**:
   - Inspect `modules/godot_eden/storage/lod_octree.cpp`, lines 100-105:
     Notice `node.first_child_idx = p_children[0];` discards `p_children[1..7]`.
   - Inspect `modules/godot_eden/storage/lod_octree.cpp`, line 189 and `micro_voxel_raymarch.glsl`, line 122:
     Notice `curr_idx = node.first_child_idx + octant;` assumes consecutive child placement.
   - Construct a test case with non-consecutive deduplicated child indices (e.g. `p_children = {1, 1, 5, 1, 5, 1, 1, 1}`) and call `sample_sdf_at()` or `get_material_at()` for `octant = 2`. Verify it evaluates `node.first_child_idx + 2 = 3` instead of `p_children[2] = 5`.

2. **Toroidal Offset Negative Modulo Verification**:
   - Inspect `modules/godot_eden/rendering/voxel_renderer_rd.cpp`, line 181:
     Set `camera_position = Vector3(-10.0f, 0, 0)`, `scale = 1.0f`, `extent = 32`.
     Evaluate `(-10) % 32`. Observe result is `-10`, yielding `toroidal_offset[0] = -10.0f`.

3. **Raymarching Step Size Verification**:
   - Inspect `modules/godot_eden/shaders/micro_voxel_raymarch.glsl`, line 185:
     Observe `t += max(0.5, abs(sdf_val));`. Note that any voxel feature with thickness < 0.5 units along the ray direction will be skipped in 1 step.
