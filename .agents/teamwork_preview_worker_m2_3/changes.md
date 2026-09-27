# Changes Summary — Worker 3 (Milestone 2 Root Node Index Fix & Remediation)

## Modified Files

### 1. `modules/godot_eden/storage/lod_octree.h`
- Updated `SvoDagKey` struct to include `float sdf_value`.
- Updated `SvoDagKey::operator==` to check `Math::is_equal_approx(sdf_value, p_other.sdf_value)`.
- Updated `SvoDagKeyHasher` to include `hash_murmur3_one_float(p_key.sdf_value, h)` in hash calculation to prevent improper deduplication of nodes with identical topology/materials but different SDF values.
- Exposed `void set_root_node_index(uint32_t p_index)` in `LodOctree`.

### 2. `modules/godot_eden/storage/lod_octree.cpp`
- Bound `set_root_node_index` and `get_root_node_index` in `_bind_methods()` with property registration `ADD_PROPERTY(PropertyInfo(Variant::INT, "root_node_index"), ...)`.
- Implemented `void LodOctree::set_root_node_index(uint32_t p_index) { root_node_index = p_index; }`.
- Updated `insert_dag_branch_raw` to populate `key.sdf_value = p_sdf;` and automatically update `root_node_index` to the returned DAG branch node index (both on matching deduplicated hit and new node insertion).
- Fixed critical `pos.y` typo in `sample_sdf_at` (line 210) and `get_material_at` (line 253): changed `pos.y = (pos.y >= 0.5f) ? (pos.y - 0.5f) * 2.0f : pos.x * 2.0f;` to `pos.y = (pos.y >= 0.5f) ? (pos.y - 0.5f) * 2.0f : pos.y * 2.0f;`.
- Updated `sample_sdf_at` and `get_material_at` to validate `root_node_index` against `nodes.size()`, using `root_node_index` if valid or falling back to index 0.

### 3. `modules/godot_eden/shaders/micro_voxel_raymarch.glsl`
- Updated `RaymarchPushConstants` uniform block to include `uint root_node_index;` and `uint pad0;` (offset 40, size 48).
- Updated `sample_svo` function to start traversal at `params.root_node_index` instead of hardcoded `0u`.

### 4. `modules/godot_eden/rendering/voxel_renderer_rd.h`
- Updated `RaymarchPushConstants` struct to include `uint32_t root_node_index = 0;` and `uint32_t pad0 = 0;` (48 bytes, matching GLSL push constant layout).

### 5. `modules/godot_eden/rendering/voxel_renderer_rd.cpp`
- Fixed clipmap toroidal offset calculation in `update_lod_clipmap` to compute `grid_cell` directly from `camera_position` and `scale` via integer math (`(int)Math::floor(camera_position.x / scale)`), eliminating float re-flooring imprecision.
- Updated `dispatch_raymarch_compute` to set `push_constants.root_node_index = svo_octree.is_valid() ? svo_octree->get_root_node_index() : 0;` and `push_constants.pad0 = 0;`.
- Implemented uniform set caching in `dispatch_raymarch_compute` and `dispatch_clipmap_compute`, reusing `raymarch_uniform_set` and `clipmap_uniform_set` RIDs across frames when underlying SSBO buffers have not changed.
- Added automatic invalidation of cached uniform sets (`raymarch_uniform_set` and `clipmap_uniform_set`) inside `upload_svo_ssbo()`, `upload_clipmap_ssbo()`, `upload_material_palette_ssbo()`, and `set_render_target_size()`.

### 6. `modules/godot_eden/tests/test_rendering.h`
- Updated `"SVDAG Non-Contiguous Child Traversal"` test case to call `octree->set_root_node_index(idx_parent)` and verify `CHECK(octree->get_root_node_index() == idx_parent)`.
- Updated `"DAG Branch Insertion & Deduplication"` test case to verify deduplication behavior when `sdf_value` differs.
