# DISPATCH — Worker 3 (Milestone 2 Root Node Index Fix)

## Working Directory
`C:\DEV_DRIVE\Dev\GodotEden\.agents\teamwork_preview_worker_m2_3`

## Mission & Instructions
Fix `LodOctree` root node index handling across C++ and GLSL:

1. **`LodOctree` Root Index Setter & Auto-Update (`lod_octree.h` & `cpp`)**:
   - In `modules/godot_eden/storage/lod_octree.h`, expose `void set_root_node_index(uint32_t p_index)` and `uint32_t get_root_node_index() const`. Bind them in `_bind_methods()`.
   - In `insert_dag_branch_raw`, automatically set `root_node_index = root_idx` when building/inserting DAG branches.
   - Update `sample_sdf_at` and `get_material_at` to use `root_node_index` if valid, or fallback to index 0 if empty.

2. **GLSL Raymarching Root Index Support (`shaders/micro_voxel_raymarch.glsl` & `rendering/voxel_renderer_rd.cpp`)**:
   - Pass `root_node_index` into `RaymarchPushConstants` or update `micro_voxel_raymarch.glsl` so root traversal begins at `push_constants.root_node_index` (or `0u` if unassigned).

3. **Unit Test Updates (`tests/test_rendering.h`)**:
   - Update `test_rendering.h` `"SVDAG Non-Contiguous Child Traversal"` test case to call `octree->set_root_node_index(root_idx)` and verify `octree->sample_sdf_at(pos)` returns the expected SDF values.

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_preview_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.


## 2026-08-06T13:34:51Z
**Context**: Milestone 2 Additional Remediation Findings (Challenger 1 & Reviewer 2)

**Content**: Please include these additional critical fixes in your remediation pass:
1. **`pos.y` Typo Fix in `lod_octree.cpp`**: In `sample_sdf_at` and `get_material_at` (lines 201 & 245 of `modules/godot_eden/storage/lod_octree.cpp`), fix `pos.y = pos.x * 2.0f;` to `pos.y = pos.y * 2.0f;`.
2. **`SvoDagKey` SDF Inclusion**: In `modules/godot_eden/storage/lod_octree.h`, include `float sdf_value` in `SvoDagKey` struct, `operator==`, and `SvoDagKeyHasher` so nodes with different SDF values are not incorrectly deduplicated.
3. **`LodOctree` Root Index**: Add `set_root_node_index(uint32_t)` and `get_root_node_index()` to `lod_octree.h/cpp`. Automatically set `root_node_index = root_idx` in `insert_dag_branch_raw` and pass `root_node_index` in push constants to `micro_voxel_raymarch.glsl`.
4. **Toroidal Modulo Direct Integer Math**: In `voxel_renderer_rd.cpp`, compute `Vector3i grid_cell = Vector3i(Math::floor(cam_pos.x / scale), Math::floor(cam_pos.y / scale), Math::floor(cam_pos.z / scale));` directly without re-flooring floating point divisions.
5. **Uniform Set Caching**: In `voxel_renderer_rd.cpp`, reuse `raymarch_uniform_set` and `clipmap_uniform_set` RIDs when underlying SSBO buffers have not changed instead of re-creating per frame.

**Action**: Implement these fixes, verify unit tests, update `changes.md` and `handoff.md`, and report completion.
