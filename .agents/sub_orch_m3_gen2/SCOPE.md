# Scope: Milestone 3 — Micro-Voxel Renderer Architecture & Vulkan Compute Raymarching Pipeline

## Architecture & Subsystems
- **Vulkan GPU Compute Raymarcher (`modules/godot_eden/rendering/`)**:
  - `VoxelRendererRD` (`voxel_renderer_rd.h/cpp`): Node3D engine integration using Godot's `RenderingDevice` API to dispatch compute shaders. Manages storage buffers (SVO DAG buffers, clipmap textures, material palettes), compute pipelines, and frame updates.
- **GLSL Compute Shaders (`modules/godot_eden/shaders/`)**:
  - `micro_voxel_raymarch.glsl`: Vulkan GLSL compute shader for stackless SVO DAG micro-voxel raymarching.
  - `clipmap_lod.glsl`: GLSL compute shader for concentric clipmap LOD traversal.
  - Shader integration in `SCsub` using `glsl_builders.build_rd_headers`.
- **Concentric Clipmap LOD Pipeline (`modules/godot_eden/rendering/`)**:
  - Concentric camera-centered clipmap LOD rings for smooth planet-scale micro-voxel level-of-detail transitions.
- **ATC Attribute & Material System (`modules/godot_eden/rendering/`)**:
  - `AtcAttributePipeline` (`atc_attribute_pipeline.h/cpp`): Allocation-Tagging-Conversion dynamic voxel attribute pipeline for albedo, surface normals, and material properties.
- **Physics Collision Mesh Generator (`modules/godot_eden/rendering/`)**:
  - `PhysicsMeshGenerator` (`physics_mesh_generator.h/cpp`): Greedy meshing and dual contouring fallback generator that constructs collision hulls (`ConcavePolygonShape3D`) for Godot physics.
- **Doctest & Integration**:
  - Unit tests co-located in `modules/godot_eden/tests/` verifying render pipeline initialization, compute buffer layout, ATC conversion, and physics mesh generation.

## Feature Inventory (Milestone 3)
| # | Feature | Description | File Locations | Status |
|---|---------|-------------|----------------|--------|
| 9 | Vulkan GPU Compute Raymarcher | `RenderingDevice` GLSL compute raymarching pipeline for stackless SVO DAG micro-voxel traversal | `rendering/voxel_renderer_rd.h/cpp`, `shaders/micro_voxel_raymarch.glsl` | PLANNED |
| 10 | Concentric Clipmap LOD Pipeline | Concentric camera-centered clipmap LOD rings for smooth planet-scale micro-voxel LOD transitions | `rendering/voxel_renderer_rd.h/cpp`, `shaders/clipmap_lod.glsl` | PLANNED |
| 11 | ATC Attribute & Material System | Allocation-Tagging-Conversion dynamic voxel attribute pipeline (albedo, normals, material properties) | `rendering/atc_attribute_pipeline.h/cpp` | PLANNED |
| 12 | Physics Collision Mesh Generator | Dual Contour / Greedy Mesh generator fallback for physics collision hulls (`ConcavePolygonShape3D`) | `rendering/physics_mesh_generator.h/cpp` | PLANNED |

## Interface Contracts & Class Registrations
- Integrate new classes (`VoxelRendererRD`, `AtcAttributePipeline`, `PhysicsMeshGenerator`) into `SCsub` and register in `register_types.cpp` with ClassDB.
- Unit tests added to `modules/godot_eden/tests/test_rendering.h` or `test_main.h` and executed via `godot --test`.
- Run E2E tests (`godot --headless --script tests/e2e/test_runner.gd`) to confirm zero regressions.

## Rules & Verification
- Iteration loop: 3 parallel Explorers -> 1 Worker -> 2 Reviewers + 2 Challengers + Forensic Auditor (`teamwork_preview_auditor`).
- Gate criteria: Build & unit/E2E tests pass, ALL Reviewers APPROVE, ALL Challengers APPROVE, Auditor CLEAN.
