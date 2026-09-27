## 2026-08-06T20:37:08Z
Investigate Node and Resource ClassDB binding code in `modules/godot_eden/nodes/` and `modules/godot_eden/rendering/`:
1. `VoxelWorld` (`Node3D`): check header/cpp, GDCLASS macro, `_bind_methods()`, properties (`view_distance_chunks`, etc.), getters/setters.
2. `VoxelVolume` (`Resource`): check header/cpp, GDCLASS macro, `_bind_methods()`, dimensions/LOD parameters, properties.
3. `VoxelRenderer` / `VoxelRendererRD` (`Node3D`): check header/cpp, GDCLASS macro, `_bind_methods()`, rendering properties, internal process notifications.
4. `VoxelGenerator` (`Resource`): check header/cpp, GDCLASS macro, `_bind_methods()`, `GDVIRTUAL1R(_generate_voxel)` virtual method binding, procedural parameters.

Write your findings, verification status, and recommended refinement strategy to `C:\DEV_DRIVE\Dev\GodotEden\.agents\explorer_m1_iter3_2\handoff.md` and report back when finished.
