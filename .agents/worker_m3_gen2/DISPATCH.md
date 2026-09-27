## 2026-08-05T22:52:07Z
<USER_REQUEST>
You are Worker (worker_m3_gen2) for Milestone 3 (Micro-Voxel Renderer & Shaders Pipeline) of the GodotEden C++ engine module.

Working Directory: C:\DEV_DRIVE\Dev\GodotEden\.agents\worker_m3_gen2

MANDATORY DOCUMENTS TO READ FIRST:
- C:\DEV_DRIVE\Dev\GodotEden\ORIGINAL_REQUEST.md
- C:\DEV_DRIVE\Dev\GodotEden\PROJECT.md
- C:\DEV_DRIVE\Dev\GodotEden\.agents\sub_orch_m3\SCOPE.md
- C:\DEV_DRIVE\Dev\GodotEden\TEST_READY.md
- C:\DEV_DRIVE\Dev\GodotEden\.agents\explorer_m3_1\handoff.md
- C:\DEV_DRIVE\Dev\GodotEden\.agents\explorer_m3_2\handoff.md
- C:\DEV_DRIVE\Dev\GodotEden\.agents\explorer_m3_3\handoff.md

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_preview_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

OBJECTIVES TO IMPLEMENT FOR MILESTONE 3:

1. GLSL Compute Shaders (`modules/godot_eden/shaders/`):
   - Create `micro_voxel_raymarch.glsl`: Vulkan GLSL compute shader (`#version 450`) for stackless SVO DAG micro-voxel raymarching. Must handle SvoNode SSBO buffers (set 0, binding 1), clipmap level buffer (set 0, binding 2), material SSBO buffer (set 0, binding 3), and output storage image (set 0, binding 0). Stackless raymarching using bitCount child index offsets, SDF leaf evaluation, normal estimation, and lighting.
   - Create `clipmap_lod.glsl`: Vulkan GLSL compute shader (`#version 450`) for concentric clipmap LOD traversal and center snapping.
   - Update `modules/godot_eden/SCsub`: Use `glsl_builders.build_rd_headers` to automatically build shader headers (`shaders/micro_voxel_raymarch.glsl.gen.h` and `shaders/clipmap_lod.glsl.gen.h`). Add `rendering/*.cpp` source files to build environment.

2. Vulkan GPU Compute Raymarcher & Clipmap LOD (`modules/godot_eden/rendering/voxel_renderer_rd.h/cpp`):
   - Implement `VoxelRendererRD` inheriting from Node3D.
   - Use Godot's `RenderingDevice` API (`RenderingServer::get_singleton()->get_rendering_device()` with fallback for local engine device).
   - Manage storage buffers (SVO DAG SSBO, clipmap levels SSBO, material SSBO), compute pipelines, uniform sets, and frame dispatches (`update_lod_clipmap()`, `dispatch_raymarch_compute()`).
   - Implement Concentric Clipmap LOD Ring management for camera-centered level-of-detail transitions.

3. ATC Attribute & Material System (`modules/godot_eden/rendering/atc_attribute_pipeline.h/cpp`):
   - Implement `AtcAttributePipeline` (RefCounted/Resource).
   - Support Allocation-Tagging-Conversion of dynamic voxel material attributes (albedo RGBA8, normal Oct16, roughness, metallic, emission RGB565).
   - Convert C++ high-precision material definitions to 16-byte packed `AtcPackedGpuMaterial` SSBO layout.

4. Physics Collision Mesh Generator (`modules/godot_eden/rendering/physics_mesh_generator.h/cpp`):
   - Implement `PhysicsMeshGenerator` (RefCounted/Resource).
   - Support Greedy Meshing and Dual Contouring fallback algorithms on `VoxelBuffer` / `VoxelDataMap` block data.
   - Produce valid `Ref<ConcavePolygonShape3D>` containing collision triangles (`Vector<Vector3>`) for Godot physics.

5. ClassDB Registrations & Build Hooks:
   - Update `modules/godot_eden/register_types.h/cpp` to register `VoxelRendererRD`, `AtcAttributePipeline`, `PhysicsMeshGenerator` with Godot's `ClassDB`.
   - Update `modules/godot_eden/config.py` and `modules/godot_eden/SCsub`.

6. C++ Doctest Unit Tests (`modules/godot_eden/tests/`):
   - Extend `test_main.h` or add `test_rendering.h` with comprehensive Doctest unit tests covering:
     - `VoxelRendererRD` defaults and LOD ring calculation.
     - `AtcAttributePipeline` material slot allocation, RGBA8/Oct16/RGB565 bit-packing, and 16-byte SSBO export.
     - `PhysicsMeshGenerator` greedy face merging, dual contouring fallback, and `ConcavePolygonShape3D` face extraction.
     - ClassDB node/resource registration verification.

7. Verification:
   - Run compilation & unit tests (`godot --test` or build verification).
   - Run E2E test runner (`python tests/e2e/runner.py` or `godot --headless --script tests/e2e/test_runner.gd`).
   - Deliver handoff report in `C:\DEV_DRIVE\Dev\GodotEden\.agents\worker_m3_gen2\handoff.md` and send a message when done.
</USER_REQUEST>
