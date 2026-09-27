# Scope: Milestone 1 — Godot 4 Module Infrastructure & ClassDB Bindings

## Workspace & Context
- Project Root: C:\DEV_DRIVE\Dev\GodotEden
- Original Request File: C:\DEV_DRIVE\Dev\GodotEden\ORIGINAL_REQUEST.md
- Global Project Spec: C:\DEV_DRIVE\Dev\GodotEden\PROJECT.md
- Your Working Directory: C:\DEV_DRIVE\Dev\GodotEden\.agents\sub_orch_m1

## Milestone Requirements (M1)
1. Standard Godot 4 built-in C++ module directory layout in `modules/godot_eden`.
2. `config.py` implementing `can_build()`, `configure()`, `get_doc_classes()`, `get_doc_path()`.
3. `SCsub` configured for SCons building, adding module include paths, SPIR-V GLSL shader compilation (`RD_GLSL`), and source file collection.
4. `register_types.h` & `register_types.cpp` declaring and initializing all 16 module classes at `MODULE_INITIALIZATION_LEVEL_SCENE` via `GDREGISTER_CLASS`.
5. ClassDB C++ bindings for all core classes: `VoxelWorld` (Node3D), `VoxelVolume` (Resource), `VoxelRenderer` / `VoxelRendererRD` (Node3D), `VoxelBuffer` (RefCounted), `VoxelStreamer` (RefCounted), `VoxelGenerator` (Resource), `AtcAttributePipeline` (RefCounted).
6. Verify C++ compilation capability and ClassDB method/property registrations.

Execute the Explorer → Worker → Reviewer → Challenger → Auditor cycle for Milestone 1. When complete, publish your handoff report to `C:\DEV_DRIVE\Dev\GodotEden\.agents\sub_orch_m1\handoff.md`.
