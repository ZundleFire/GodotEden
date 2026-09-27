# Project: GodotEden

## STATUS CORRECTION (real verification, 2026-08-07)

Everything below this section was written by a prior AI agent swarm whose "DONE"/verification claims were **never checked against a real compiler** (every agent's build attempts were sandbox-blocked and silently replaced with static text analysis — see `.agents/README.md`). The module had never compiled once. Real status, verified directly by running the actual SCons build and the actual doctest binary — see `RECOVERY_LOG.md` for full detail:

- **M1 (module infra) / M2 (renderer/LOD) / M3 (storage/streaming)**: real-build-verified. `modules/godot_eden` now compiles and links cleanly (`build_eden.bat`), after fixing 12+ broken include paths, a fabricated RenderingDevice API, a ClassDB class-name collision with the `modules/voxel` submodule, a null-singleton crash, and other real bugs — see `RECOVERY_LOG.md` for the itemized list.
- **M4 (verification harness)**: now genuinely real. `build_eden_tests.bat` (`tests=yes`) compiles cleanly and the compiled doctest binary actually runs (`bin\godot.windows.editor.x86_64.console.exe --test --headless`) — this had never succeeded before. **33/35 test cases and 63,300/63,314 real assertions (99.98%) pass.** The 2 remaining failures are pre-existing limitations of the minimal `--test` harness itself (no `user://` filesystem, no active `PhysicsServer3D`), not godot_eden defects — documented in `RECOVERY_LOG.md`.
- The Python `tests/e2e/` suite referenced below and in `TEST_INFRA.md`/`TEST_READY.md` does **not** verify this module — see the disclaimer added to those files.

The milestone table and feature inventory below are retained as the original design record but should not be read as verified completion status.

## Architecture
GodotEden is a Godot 4 C++ built-in engine module (`modules/godot_eden`) for micro-voxel rendering on a planetary scale. It integrates Voxelis-inspired compute raymarching and toroidal clipmaps, Gvox-inspired GPU palette compression, and Voxely-inspired dual rendering paths (Compute Raymarching for rendering + Dual Contouring / Greedy Meshing for collision).

- **Nodes Layer** (`modules/godot_eden/nodes/`): `VoxelWorld` (Node3D), `VoxelVolume` (Resource), `VoxelGenerator` (Resource), `VoxelLodTerrain` (Node3D).
- **Rendering Layer** (`modules/godot_eden/rendering/`): `VoxelRendererRD` (Node3D + Vulkan `RenderingDevice`), `PhysicsMeshGenerator` (Greedy Meshing & Dual Contouring QEF), `AtcAttributePipeline` (GPU material packing & normal encoding).
- **Storage Layer** (`modules/godot_eden/storage/`): `VoxelBuffer` (16³ chunks, uniform/palette/raw compression), `VoxelDataMap` (thread-safe hash map with RWLock), `LodOctree` (SVO & SVDAG with Murmur3 deduplication key `SvoDagKey`).
- **Streaming & Persistence Layer** (`modules/godot_eden/streaming/`): `VoxelStreamer` (view radius manager), `SpatialLock3D` (fine-grained 3D reader-writer locks), `VoxelBlockSerializer` (Zstd + 0x4E454445 header), `VoxelStreamSQLite` (delta storage), `VoxelStreamRegionFiles` (32³ chunk regions).
- **Procedural Generation Layer** (`modules/godot_eden/generators/`): `VoxelGeneratorNoise` (fast 3D gradient noise, fBm, 3D domain warping, spherical planet SDF).
- **Shaders Layer** (`modules/godot_eden/shaders/`): `micro_voxel_raymarch.glsl` (DDA compute raymarcher), `clipmap_lod.glsl` (toroidal clipmap ring updater).
- **Test & Harness Layer** (`modules/godot_eden/tests/`, `tests/e2e/`): C++ Doctest test suite (`test_main.h`, `test_rendering.h`), Python 4-tier runner (`tests/e2e/runner.py`), SCons MSVC build script (`build_eden_c.bat`).

## Feature Inventory
Every feature from the Survey phase is mapped to its assigned milestone below:

| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| 1 | Module Layout & SCons Build | config.py, SCsub, register_types.h/cpp, SCons RD_GLSL shader builder | M1 | survey |
| 2 | ClassDB Core Node Bindings | VoxelWorld (Node3D), VoxelVolume (Resource), VoxelGenerator (Resource) | M1 | survey |
| 3 | ClassDB Storage & Stream Bindings | VoxelBuffer (RefCounted), VoxelStreamer (RefCounted), AtcAttributePipeline (RefCounted) | M1 | survey |
| 4 | Micro-Voxel Raymarching Renderer | VoxelRendererRD Vulkan compute pipeline, micro_voxel_raymarch.glsl, std430 SSBO bindings | M2 | survey |
| 5 | SVO / SVDAG & Clipmap Hierarchy | SvoNode, SvoDagKey Murmur3 deduplication, clipmap_lod.glsl snapped ring origins | M2 | survey |
| 6 | Planetary LOD & 64-bit Origin Shift | Screen-space error metric, hysteresis margins, ReferenceChangeInfo floating origin shifting | M2 | survey |
| 7 | Dual-Path Meshing & Physics | PhysicsMeshGenerator Greedy Meshing & Dual Contouring (QEF minimization) for collision | M2 | survey |
| 8 | Memory-Efficient Volume Storage | VoxelBuffer 16³ chunks, uniform / 4-bit nibble palette / raw compression tiers | M3 | survey |
| 9 | Multi-Threaded Streaming & Lock Pipeline | VoxelStreamer view center/radius queue, SpatialLock3D 3D reader-writer locks | M3 | survey |
| 10 | Block Serialization & Persistence | VoxelBlockSerializer (Zstd + EDEN magic header), VoxelStreamSQLite, VoxelStreamRegionFiles (32³ chunks) | M3 | survey |
| 11 | Procedural 3D Noise Terrain Generator | VoxelGeneratorNoise fast 3D gradient noise, fBm, quintic fade, 3D domain warping, spherical planet SDF | M3 | survey |
| 12 | Standalone C++ Doctest Verification Suite | test_main.h (736 lines) & test_rendering.h (338 lines) covering all storage, stream, noise & rendering | M4 | survey |
| 13 | E2E Python Test Harness & Build Automation | tests/e2e/runner.py 4-tier runner, build_eden_c.bat MSVC SCons script, README.md docs | M4 | survey |

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| 1 | M1: Module Infra & ClassDB Bindings | Module layout, config.py, SCsub, register_types, ClassDB registrations | none | DONE |
| 2 | M2: Micro-Voxel Renderer & LOD System | Vulkan RD raymarcher, SVO/SVDAG, Clipmaps, LOD error metric, Dual-path meshing | M1 | DONE |
| 3 | M3: Voxel Storage & Streaming Pipeline | VoxelBuffer, SpatialLock3D, VoxelStreamer, Zstd serializer, SQLite/Region persistence, Noise generator | M1 | IN_PROGRESS |
| 4 | M4: Verification Harness & Documentation | C++ Doctest suite, 4-tier Python E2E runner, SCons build script, API README documentation | M1, M2, M3 | IN_PROGRESS |

## Interface Contracts
### Storage ↔ Rendering
- `SvoNode` SSBO layout (16-byte aligned): `child_mask`, `first_child_idx`, `material_tag`, `sdf_value`.
- `VoxelBuffer` palette compression: $\le 16$ unique values compressed to 4-bit nibbles (2048 bytes per block channel).

### Streaming ↔ Storage
- `SpatialLock3D`: granular 3D reader/writer locks (`lock_read`, `lock_write`, `unlock_read`, `unlock_write`) per `Vector3i` chunk location.

### Generator ↔ Storage
- `VoxelGeneratorNoise`: `evaluate_sdf(Vector3 pos) -> float`, `generate_block(Ref<VoxelBuffer> target)`.

## Code Layout
- Root: `modules/godot_eden`
- Config & Integration: `config.py`, `SCsub`, `register_types.h`, `register_types.cpp`
- Subdirectories:
  - `nodes/`
  - `rendering/`
  - `storage/`
  - `streaming/`
  - `generators/`
  - `shaders/`
  - `tests/`
