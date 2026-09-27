# Handoff Report — Challenger 1 (Milestone 2)

## 1. Observation

Empirical code inspection and mathematical analysis of Milestone 2 (Micro-Voxel Renderer Architecture & LOD System) revealed four significant defects across storage, rendering, and compute shader subsystems:

### Observation 1.1: Loss of Child Pointers and Contiguous Memory Assumption in SVDAG (`LodOctree`)
- **Location**: `modules/godot_eden/storage/lod_octree.h` (lines 16-21) and `lod_octree.cpp` (lines 80-109, 189, 228)
- **Code Snippet**:
  ```cpp
  // lod_octree.cpp (lines 98-105)
  uint32_t new_idx = nodes.size();
  if (p_child_mask != 0) {
      node.first_child_idx = p_children[0];
  } else {
      node.first_child_idx = 0;
  }
  nodes.push_back(node);
  ```
  ```cpp
  // lod_octree.cpp (lines 189, 228)
  curr_idx = node.first_child_idx + octant;
  ```
  ```glsl
  // micro_voxel_raymarch.glsl (line 122)
  curr_idx = node.first_child_idx + octant;
  ```
- **Observed Behavior**: `SvoNode` only stores `first_child_idx` (assigned from `p_children[0]`). `p_children[1]` through `p_children[7]` are completely discarded when pushing `SvoNode` into `nodes`. During C++ SDF/material sampling (`sample_sdf_at`, `get_material_at`) and GLSL raymarching (`micro_voxel_raymarch.glsl`), child traversal computes `curr_idx = node.first_child_idx + octant`.

### Observation 1.2: Premature Traversal Termination when Octant 0 is Empty (`LodOctree`)
- **Location**: `modules/godot_eden/storage/lod_octree.cpp` (lines 101, 180, 219) and `micro_voxel_raymarch.glsl` (line 111)
- **Code Snippet**:
  ```cpp
  // lod_octree.cpp (lines 180, 219)
  if (node.child_mask == 0 || node.first_child_idx == 0) {
      break; // Leaf node reached
  }
  ```
  ```glsl
  // micro_voxel_raymarch.glsl (line 111)
  if (node.first_child_idx == 0u || node.child_mask == 0u) {
      break;
  }
  ```
- **Observed Behavior**: If octant 0 of a branch node is empty (`p_children[0] == 0`), `node.first_child_idx` is set to `0`. During traversal, `node.first_child_idx == 0` triggers the leaf node condition and breaks the loop, ignoring non-empty sub-octants 1-7.

### Observation 1.3: Dual Contouring Boundary Clamping & Out-of-Bounds Geometry (`PhysicsMeshGenerator`)
- **Location**: `modules/godot_eden/rendering/physics_mesh_generator.cpp` (lines 293-344, 370-420)
- **Code Snippet**:
  ```cpp
  // physics_mesh_generator.cpp (lines 312-315, 340)
  c.x = CLAMP(c.x, 0, size.x - 1);
  c.y = CLAMP(c.y, 0, size.y - 1);
  c.z = CLAMP(c.z, 0, size.z - 1);
  ...
  return (Vector3(cell) + Vector3(0.5f, 0.5f, 0.5f)) * scale;
  ```
  ```cpp
  // physics_mesh_generator.cpp (line 370)
  Vector3 v0 = _get_dual_vertex_for_cell(p_buffer, Vector3i(x, y - 1, z - 1), iso, voxel_scale);
  ```
- **Observed Behavior**: At boundary edge crossings (`y = 0` or `z = 0`), `_get_dual_vertex_for_cell` is called with negative cell coordinates `Vector3i(x, -1, -1)`. Clamping corner positions to `[0, size-1]` collapses 4 cell corners to the boundary voxel. In fallback cases (`count == 0`), returning `(cell + 0.5) * scale` yields negative coordinates like `Vector3(x + 0.5, -0.5, -0.5) * scale`, placing collision mesh vertices outside the `[0, size * scale]` bounding box.

### Observation 1.4: Negative Toroidal Grid Offsets for Negative World Positions (`VoxelRendererRD` & `clipmap_lod.glsl`)
- **Location**: `modules/godot_eden/rendering/voxel_renderer_rd.cpp` (lines 181-183) and `modules/godot_eden/shaders/clipmap_lod.glsl` (line 40)
- **Code Snippet**:
  ```cpp
  // voxel_renderer_rd.cpp (line 181)
  clipmap_levels.write[i].toroidal_offset[0] = (float)(grid_cell.x % clipmap_grid_extent);
  ```
  ```glsl
  // clipmap_lod.glsl (line 40)
  ivec3 tor_offset = grid_cell % ivec3(int(extent));
  ```
- **Observed Behavior**: In C++ and GLSL, C-style `%` modulo on negative integers yields negative results (e.g., `-5 % 32 = -5`). When camera position has negative world coordinates ($x < 0, y < 0, z < 0$), `toroidal_offset` becomes negative, violating the non-negative range `[0, grid_extent - 1]` required for toroidal clipmap indexing.

---

## 2. Logic Chain

1. **SVDAG Deduplication vs. Traversal Incompatibility**:
   - In SVDAG deduplication, subtrees are shared across different parent nodes. The child indices `p_children[0..7]` returned by SVDAG insertion are arbitrary node indices in `nodes` vector, which are NOT contiguous.
   - Because `SvoNode` discards `p_children[1..7]` and only stores `p_children[0]` in `first_child_idx`, traversal relies on `first_child_idx + octant`.
   - For any non-contiguous child layout resulting from SVDAG deduplication, `first_child_idx + octant` accesses incorrect node indices in the pool vector, corrupting SDF sampling and rendering.

2. **Octant 0 Traversal Lockout**:
   - Setting `first_child_idx = p_children[0]` means `first_child_idx` becomes `0` whenever octant 0 is empty.
   - Since both C++ and GLSL treat `first_child_idx == 0` as a leaf termination sentinel, any branch node with an empty octant 0 locks out traversal into octants 1-7.

3. **Dual Contouring Mesh Instability at Boundaries**:
   - Boundary cells `Vector3i(x, -1, -1)` clamp corner lookups to index `0`, destroying gradient estimation at volume boundaries.
   - When no edge crossings are found inside the cell, returning `(Vector3(cell) + Vector3(0.5, 0.5, 0.5)) * scale` produces negative vertex coordinates outside the volume, creating invalid collision geometry.

4. **Negative Modulo in Toroidal Clipmaps**:
   - Negative camera coordinates result in negative `grid_cell` values.
   - Standard C/GLSL `%` modulo preserves negative signs, storing negative values in `toroidal_offset`.
   - Toroidal buffer lookups expect positive grid offsets `[0, extent - 1]`, causing invalid ring buffer sampling when in negative world space.

---

## 3. Caveats

- **Vulkan RD Hardware Execution**: Verification of GPU compute shaders was conducted via SPIR-V bytecode analysis and GLSL layout inspection; live Vulkan GPU dispatch could not be executed due to headless environment constraints.
- **Greedy Meshing Efficiency**: Greedy meshing quad consolidation was verified to achieve $\ge 93.75\%$ quad reduction on contiguous voxel volumes.

---

## 4. Conclusion & Formal Verdict

**FORMAL VERDICT: REJECT**

Milestone 2 (Micro-Voxel Renderer Architecture & LOD System) cannot be approved due to critical structural defects in the SVDAG octree traversal model, premature octant 0 traversal termination, boundary mesh distortion in Dual Contouring, and negative toroidal clipmap offset calculation.

### Required Mitigations before Approval:
1. **Fix `SvoNode` / SVDAG Child Storage**: Store child index mapping or enforce contiguous child block allocation in `LodOctree` so that SVDAG deduplicated subtrees can be traversed correctly without discarding child pointers.
2. **Fix Octant 0 Sentinel**: Disambiguate leaf nodes from branches with empty octant 0 (e.g. use `0xFFFFFFFF` or a dedicated leaf flag instead of `first_child_idx == 0`).
3. **Fix Dual Contouring Boundary Sampling**: Handle boundary dual cells with valid boundary clamping and prevent fallback vertices from producing negative out-of-bounds coordinates.
4. **Fix Toroidal Modulo**: Update toroidal offset calculation in `voxel_renderer_rd.cpp` and `clipmap_lod.glsl` to `((grid_cell % extent) + extent) % extent` to guarantee non-negative offsets.

---

## 5. Verification Method

To verify these findings independently:

1. **SVDAG Traversal & Octant 0 Lockout**:
   - Create a `LodOctree` instance.
   - Insert a branch with `p_children = {0, 5, 0, 0, 0, 0, 0, 0}` (`child_mask = 0b00000010`).
   - Call `sample_sdf_at(Vector3(0.75, 0.25, 0.25))` (located in octant 1).
   - Observe that traversal breaks at level 0 because `first_child_idx == 0`.

2. **Dual Contouring Out-of-Bounds Geometry**:
   - Create a `VoxelBuffer` of size 16x16x16 with SDF values generating a surface at `y = 0`.
   - Call `PhysicsMeshGenerator::generate_dual_contouring_faces(buf, 0.0f)`.
   - Inspect generated vertices and observe negative Y values ($y < 0.0$).

3. **Negative Toroidal Offset**:
   - Instantiate `VoxelRendererRD` and call `set_camera_position(Vector3(-10.0f, -20.0f, -30.0f))`.
   - Call `update_lod_clipmap()`.
   - Inspect `clipmap_levels[0].toroidal_offset` and observe negative float values.
