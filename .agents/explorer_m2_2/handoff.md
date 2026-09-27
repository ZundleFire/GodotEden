# Handoff Report: SVO/SVDAG, Clipmaps, and Planetary LOD Subsystem Analysis

**Agent**: `explorer_m2_2`  
**Date**: 2026-08-06  
**Target Subsystem**: `modules/godot_eden` (Storage, Rendering, Shaders, Tests)  
**Parent Agent**: `sub_orch_m2`  

---

## 1. Observation

Direct observations from source inspection of `modules/godot_eden`:

### 1.1 SVO / SVDAG Storage (`storage/lod_octree.h` and `lod_octree.cpp`)
- **`SvoNode` GPU Layout** (`lod_octree.h`, lines 16–22):
  ```cpp
  struct SvoNode {
      uint32_t child_mask = 0;       // Bits 0-7: active child mask
      uint32_t first_child_idx = 0;  // Index of children[0] (backward compatibility)
      uint32_t material_tag = 0;     // Material attribute ID / material tag
      float sdf_value = 0.0f;        // Representative Signed Distance Function value
      uint32_t children[8] = { 0 };  // Direct indices to child nodes in DAG pool
  };
  ```
  Total size: 48 bytes (16-byte vector aligned layout). Matches GLSL SSBO struct definition in `micro_voxel_raymarch.glsl` lines 8–14.
- **`SvoDagKey` & Murmur3 Hasher** (`lod_octree.h`, lines 26–65):
  ```cpp
  struct SvoDagKey {
      uint32_t children[8];
      uint32_t material_tag;
      float sdf_value;
      uint8_t child_mask;

      bool operator==(const SvoDagKey &p_other) const {
          if (child_mask != p_other.child_mask || material_tag != p_other.material_tag || !Math::is_equal_approx(sdf_value, p_other.sdf_value)) {
              return false;
          }
          for (int i = 0; i < 8; i++) {
              if (children[i] != p_other.children[i]) {
                  return false;
              }
          }
          return true;
      }
  };

  struct SvoDagKeyHasher {
      static _FORCE_INLINE_ uint32_t hash(const SvoDagKey &p_key) {
          uint32_t h = hash_murmur3_one_32(p_key.child_mask);
          h = hash_murmur3_one_32(p_key.material_tag, h);
          h = hash_murmur3_one_float(p_key.sdf_value, h);
          for (int i = 0; i < 8; i++) {
              h = hash_murmur3_one_32(p_key.children[i], h);
          }
          return hash_fmix32(h);
      }
  };
  ```
- **SVDAG Branch Insertion** (`lod_octree.cpp`, lines 86–120):
  `insert_dag_branch_raw` checks `find_matching_dag_node(key)` before adding a new node. If found, returns existing index; otherwise appends to `nodes` vector and registers key in `dag_hash_map`.
- **SSBO Serialization** (`lod_octree.cpp`, lines 261–269):
  `get_ssbo_buffer_bytes()` serializes `nodes` vector directly into `PackedByteArray` via `memcpy`.

### 1.2 Toroidal Clipmap Compute & Renderer (`shaders/clipmap_lod.glsl` & `rendering/voxel_renderer_rd.cpp`)
- **GPU Compute Shader** (`shaders/clipmap_lod.glsl`, lines 32–47):
  ```glsl
  float scale = base_scale * pow(2.0, float(id));
  vec3 snapped_center = floor(params.camera_pos / scale) * scale;

  clipmap.levels[id].center_and_scale = vec4(snapped_center, scale);
  clipmap.levels[id].grid_size_and_lod = ivec4(int(extent), int(extent), int(extent), int(id));

  ivec3 grid_cell = ivec3(floor(snapped_center / scale));
  ivec3 iextent = ivec3(int(extent));
  ivec3 tor_offset = ((grid_cell % iextent) + iextent) % iextent;

  float outer_radius = float(extent) * 0.5 * scale;
  float blend_margin = outer_radius * 0.15;
  clipmap.levels[id].offset_and_margin = vec4(vec3(tor_offset), blend_margin);
  ```
- **CPU Clipmap Updater** (`rendering/voxel_renderer_rd.cpp`, lines 172–210):
  Mirror image of GLSL calculation in `update_lod_clipmap(const Vector3 &p_camera_position)` using `((grid_cell % extent) + extent) % extent` for negative coordinate safe wrapping.

### 1.3 Micro-Voxel Raymarching Shader (`shaders/micro_voxel_raymarch.glsl`)
- Lines 105–132: Implements SVO traversal in GLSL for DDA compute raymarching with `sample_svo()` querying `svo_dag.nodes[]`.
- Lines 174–187: Unpacks RGBA8 albedo, octahedral 16-bit normal (`oct16`), roughness/metallic, and RGB565 emissive color from `material_buffer.materials[]`.

### 1.4 Verification Suite (`tests/test_rendering.h`)
- Contains 417 lines of C++ Doctest unit tests covering:
  - ClassDB registration & Node3D inheritance (`VoxelRendererRD`, `AtcAttributePipeline`, `PhysicsMeshGenerator`).
  - Headless safe execution & property bounds clamping (`lod_levels` [1, 16], `view_distance` $\ge 0$, `render_target_size` $\ge (1,1)$).
  - Toroidal negative modulo wrapping verification at `camera_pos = Vector3(-105.5f, -42.2f, -250.0f)`.
  - `AtcAttributePipeline` `oct16` normal encoding/decoding roundtrip.
  - `PhysicsMeshGenerator` greedy quad consolidation (4x4x4 cube reduced from 192 to 12 triangles) and Dual Contouring on spherical SDF.
  - `LodOctree` SVDAG branch insertion deduplication (duplicate branch returns existing index, different material/SDF creates new node).

---

## 2. Logic Chain

1. **SVDAG Hash vs Equality Invariant Assessment**:
   - Observation: `SvoDagKey::operator==` uses `Math::is_equal_approx(sdf_value, p_other.sdf_value)` (`lod_octree.h`, line 42), whereas `SvoDagKeyHasher::hash` hashes the exact bitwise representation of `sdf_value` via `hash_murmur3_one_float(p_key.sdf_value, h)` (`lod_octree.h`, line 59).
   - Reasoning: In hash table design (`HashMap`), equal objects **must** produce equal hash values ($A == B \implies hash(A) == hash(B)$). If two float values differ within `KINDA_EQUAL` epsilon (e.g. `0.50000006f` and `0.50000012f`), `operator==` returns `true`, but `hash()` produces completely different hash codes! This breaks hash bucket lookup, causing failed deduplication lookups or map corruption.
   - Conclusion: `SvoDagKey` hashing and equality comparison are inconsistent. Float values must either be bitwise-exact in `operator==` or quantized before hashing and comparison.

2. **Toroidal Clipmap Snapping & Negative Modulo Verification**:
   - Observation: In C++ and GLSL, integer modulo `%` returns negative values for negative dividends.
   - Reasoning: The expression `((grid_cell % extent) + extent) % extent` shifts negative modulo results by `+extent` before taking the final modulo, ensuring the result is strictly in $[0, \text{extent}-1]$.
   - Conclusion: The toroidal clipmap origin snapping and wrapping logic is mathematically sound and verified in `test_rendering.h` (lines 119–125).

3. **64-bit Floating Origin Shift & `ReferenceChangeInfo` Assessment**:
   - Observation: `PROJECT.md` Feature #6 and `SCOPE.md` Requirement #3 reference `ReferenceChangeInfo` for 64-bit floating origin shifting across LOD rings and nodes. However, searching `modules/godot_eden` shows `ReferenceChangeInfo` is not yet declared in C++. `VoxelRendererRD::set_camera_position` accepts a single-precision 32-bit `Vector3`.
   - Reasoning: At planetary scales ($> 100,000$ meters), 32-bit single-precision floats lose precision ($< 1$ mm precision is lost beyond 8 km from origin), causing camera jitter and Z-fighting. High-precision planetary rendering requires a reference origin shift contract.
   - Conclusion: `ReferenceChangeInfo` struct and origin shift delegation methods (`shift_origin()`) must be added to `VoxelRendererRD` and `LodOctree`.

4. **Screen-Space Error Metric ($\text{Error}_{\text{screen}} \le \tau$) and Hysteresis Assessment**:
   - Observation: `clipmap_lod.glsl` and `voxel_renderer_rd.cpp` compute a 15% outer ring `blend_margin` for clipmaps. However, explicit helper functions calculating geometric error projection ($\text{Error}_{\text{screen}} = \frac{E_{\text{geom}} \cdot H_{\text{screen}}}{2 \cdot d \cdot \tan(\text{fov}/2)}$) and hysteresis margin threshold checks ($\tau \cdot (1 \pm H)$) are not explicitly exposed in `VoxelRendererRD` / `LodOctree`.
   - Reasoning: To support dynamic octree traversal in addition to clipmap rings, an explicit screen-space error metric function is required to decide when an SVO node needs to be refined or collapsed without temporal popping.
   - Conclusion: Screen-space error metric and hysteresis evaluation functions should be explicitly added to `LodOctree` / `VoxelRendererRD`.

---

## 3. Caveats

- **Headless GPU Testing**: Full Vulkan compute dispatch (`dispatch_raymarch_compute` and `dispatch_clipmap_compute`) was verified for API safety in headless test mode, but actual GPU frame execution depends on Vulkan hardware drivers at runtime.
- **SVDAG Float Precision Tolerance**: The SVDAG deduplication tests currently pass because tests use exact float literals (`-0.5f`, `1.0f`). The float hash mismatch only triggers under runtime procedural generation with tiny floating point variances.

---

## 4. Conclusion

The core SVO/SVDAG data structures, Murmur3 deduplication pipeline, toroidal clipmap shader/updater, material pipeline, and physics meshing are well-structured, compliant with Godot 4 C++ standards, and verified by `test_rendering.h`.

### Key Areas for Refinement / Enhancements:
1. **Fix `SvoDagKey` Hash/Equality Mismatch**: Change `operator==` in `SvoDagKey` (`lod_octree.h`:42) to bitwise exact float comparison `sdf_value == p_other.sdf_value` or quantize `sdf_value` (e.g. via `Math::snapped(sdf_value, 0.0001f)`).
2. **Implement `ReferenceChangeInfo` Struct & Origin Shifting**:
   Define `ReferenceChangeInfo` in `storage/lod_octree.h` or `rendering/voxel_renderer_rd.h`:
   ```cpp
   struct ReferenceChangeInfo {
       Vector3i old_sector_origin;
       Vector3i new_sector_origin;
       Vector3 old_local_camera;
       Vector3 new_local_camera;
       Vector3 local_shift_delta;
   };
   ```
   Add `shift_origin(const ReferenceChangeInfo &p_info)` methods to update ring centers and node coordinates relative to shifted sector origins.
3. **Implement Explicit Screen-Space Error & Hysteresis Helpers**:
   Add `calculate_screen_space_error(float p_geometric_error, float p_distance, float p_fov, float p_screen_height)` and `evaluate_lod_transition(...)` to `VoxelRendererRD` / `LodOctree`.

---

## 5. Verification Method

### 5.1 C++ Unit Tests Verification
Execute the Doctest unit test suite (via SCons / `build_eden_c.bat` or test runner):
```bash
# Run C++ test suite covering test_rendering.h
bin/godot.windows.editor.x86_64.exe --test --test-suite="[Modules][GodotEden]"
```

### 5.2 Specific Files & Line References to Inspect
1. `modules/godot_eden/storage/lod_octree.h`:
   - Line 16: `SvoNode` struct (48 bytes, SSBO compatible).
   - Line 26: `SvoDagKey` struct & `operator==`.
   - Line 55: `SvoDagKeyHasher` Murmur3 hash implementation.
2. `modules/godot_eden/storage/lod_octree.cpp`:
   - Line 86: `insert_dag_branch_raw` SVDAG deduplication logic.
   - Line 173: `sample_sdf_at` and `get_material_at` non-contiguous child traversal.
3. `modules/godot_eden/shaders/clipmap_lod.glsl`:
   - Line 33: Snapped center calculation `floor(camera_pos / scale) * scale`.
   - Line 41: Safe negative coordinate modulo wrapping `((grid_cell % iextent) + iextent) % iextent`.
4. `modules/godot_eden/rendering/voxel_renderer_rd.cpp`:
   - Line 172: `update_lod_clipmap` CPU toroidal clipmap level calculation.
5. `modules/godot_eden/tests/test_rendering.h`:
   - Line 345: `TEST_CASE("[Modules][GodotEden] LodOctree SVO Pool & SVDAG Key Deduplication")`.

### 5.3 Invalidation Conditions
- Any edit to `SvoNode` that breaks 16-byte SSBO alignment or 48-byte size.
- Any changes to `% extent` wrapping that produce negative clipmap toroidal offsets.
- Any modification to `SvoDagKey` that causes `operator==` and `hash()` to disagree on float equality.
