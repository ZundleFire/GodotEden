# Scope: Milestone 2 — Micro-Voxel Renderer Architecture & LOD System

## Workspace & Context
- Project Root: C:\DEV_DRIVE\Dev\GodotEden
- Original Request File: C:\DEV_DRIVE\Dev\GodotEden\ORIGINAL_REQUEST.md
- Global Project Spec: C:\DEV_DRIVE\Dev\GodotEden\PROJECT.md
- Your Working Directory: C:\DEV_DRIVE\Dev\GodotEden\.agents\sub_orch_m2

## Milestone Requirements (M2)
1. Micro-voxel renderer architecture (`VoxelRendererRD` extending `Node3D`, integrating Godot 4 Vulkan `RenderingDevice`).
2. SVO & SVDAG spatial data structures (`SvoNode` SSBO layout, `SvoDagKey` Murmur3 deduplication key, `lod_octree.h/cpp`).
3. Toroidal clipmap volume rings (`clipmap_lod.glsl`, snapped ring origins, 64-bit floating origin shifting via `ReferenceChangeInfo`).
4. Micro-voxel compute raymarching GLSL shader (`micro_voxel_raymarch.glsl`, DDA traversal, 16-bit octahedral normal unpacking).
5. Dual-path meshing engine (`PhysicsMeshGenerator`, Greedy Meshing & Dual Contouring QEF minimization for physics collision shapes).
6. Screen-space error metric formula ($\text{Error}_{\text{screen}} \le \tau$) and hysteresis margin thresholding for planetary LOD transitions.

Execute the Explorer → Worker → Reviewer → Challenger → Auditor iteration cycle for Milestone 2.
When complete, update `PROJECT.md` to mark M2 as `DONE` and publish your handoff report to `C:\DEV_DRIVE\Dev\GodotEden\.agents\sub_orch_m2\handoff.md`.
