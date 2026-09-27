# DISPATCH — Worker (Milestone 2)

## Working Directory
`C:\DEV_DRIVE\Dev\GodotEden\.agents\teamwork_preview_worker_m2`

## Mission & Instructions
Verify Milestone 2 (Micro-Voxel Renderer Architecture & LOD System):
- `modules/godot_eden/rendering/voxel_renderer_rd.h/cpp`
- `modules/godot_eden/rendering/physics_mesh_generator.h/cpp`
- `modules/godot_eden/rendering/atc_attribute_pipeline.h/cpp`
- `modules/godot_eden/storage/lod_octree.h/cpp`
- `modules/godot_eden/shaders/micro_voxel_raymarch.glsl`
- `modules/godot_eden/shaders/clipmap_lod.glsl`

Verify:
1. SVO pool memory layout and SVDAG DAG hash deduplication (`SvoDagKeyHasher`).
2. Vulkan `RenderingDevice` compute raymarching pipeline & clipmap LOD ring updates.
3. GPU material packing (std430 32-byte layout, Oct16 normal encoding).
4. Dual-path physics mesh generation (Greedy Meshing quad consolidation & Dual Contouring).
5. Run or verify unit test suite (`modules/godot_eden/tests/test_rendering.h`).

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_preview_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

Deliver your work report to `C:\DEV_DRIVE\Dev\GodotEden\.agents\teamwork_preview_worker_m2\changes.md` and handoff report to `C:\DEV_DRIVE\Dev\GodotEden\.agents\teamwork_preview_worker_m2\handoff.md`.
