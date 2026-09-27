# E2E Test Infra: GodotEden

> **DISCLAIMER (added 2026-08-07, see `RECOVERY_LOG.md`):** The Python suite described in this document (`tests/e2e/`) is a standalone, hand-written Python reimplementation of the intended module behavior. It does **not** import, compile, or execute any code in `modules/godot_eden/` — it tests a parallel Python model of the design, not the real C++ module. Treat it as a behavioral specification / design reference only, never as evidence that the real module works. The authoritative verification surface is the C++ Doctest suite (`modules/godot_eden/tests/test_main.h`, `test_rendering.h`), compiled with `build_eden_tests.bat` (`tests=yes`) and run via `bin\godot.windows.editor.x86_64.console.exe --test --headless` — see `RECOVERY_LOG.md` for real, current results.

## Test Philosophy
GodotEden utilizes an **opaque-box, requirement-driven testing methodology** that verifies component behavior against architectural specifications without relying on private implementation details. The strategy combines four rigorous test design techniques:
1. **Category-Partition Testing**: Partitioning input domains for storage, streaming, noise generation, and rendering pipelines into functional equivalence classes.
2. **Boundary Value Analysis (BVA)**: Stressing edge cases, zero/negative bounds, capacity limits, extreme floating-point offsets, and uninitialized resource states.
3. **Pairwise Combinatorial Testing**: Validating cross-feature interactions across storage compression, multi-threaded reader-writer locks, compute SSBO layout export, Zstd binary serialization, and clipmap LOD shifting.
4. **Real-World Workload Testing**: End-to-end production gameplay scenarios including 1,000 km planetary sphere initialization, dynamic crater terraforming, high-speed 64-bit origin-shifted flybys, and SQLite/Region file persistence roundtrips.

## Feature Inventory
| # | Feature | Description | Milestone | Tier 1 | Tier 2 | Tier 3 | Tier 4 |
|---|---------|-------------|-----------|:------:|:------:|:------:|:------:|
| F1 | Module Layout & SCons Build | config.py, SCsub, register_types.h/cpp, SCons RD_GLSL shader builder | M1 | 5 | 5 | ✓ | ✓ |
| F2 | ClassDB Core Node Bindings | VoxelWorld (Node3D), VoxelVolume (Resource), VoxelGenerator (Resource) | M1 | 5 | 5 | ✓ | ✓ |
| F3 | ClassDB Storage & Stream Bindings | VoxelBuffer (RefCounted), VoxelStreamer (RefCounted), AtcAttributePipeline (RefCounted) | M1 | 5 | 5 | ✓ | ✓ |
| F4 | Micro-Voxel Raymarching Renderer | VoxelRendererRD Vulkan compute pipeline, micro_voxel_raymarch.glsl, std430 SSBO bindings | M2 | 5 | 5 | ✓ | ✓ |
| F5 | SVO / SVDAG & Clipmap Hierarchy | SvoNode, SvoDagKey Murmur3 deduplication, clipmap_lod.glsl snapped ring origins | M2 | 5 | 5 | ✓ | ✓ |
| F6 | Planetary LOD & 64-bit Origin Shift | Screen-space error metric, hysteresis margins, ReferenceChangeInfo floating origin shifting | M2 | 5 | 5 | ✓ | ✓ |
| F7 | Dual-Path Meshing & Physics | PhysicsMeshGenerator Greedy Meshing & Dual Contouring (QEF minimization) for collision | M2 | 5 | 5 | ✓ | ✓ |
| F8 | Memory-Efficient Volume Storage | VoxelBuffer 16³ chunks, uniform / 4-bit nibble palette / raw compression tiers | M3 | 5 | 5 | ✓ | ✓ |
| F9 | Multi-Threaded Streaming & Lock Pipeline | VoxelStreamer view center/radius queue, SpatialLock3D 3D reader-writer locks | M3 | 5 | 5 | ✓ | ✓ |
| F10 | Block Serialization & Persistence | VoxelBlockSerializer (Zstd + EDEN magic header), VoxelStreamSQLite, VoxelStreamRegionFiles (32³ chunks) | M3 | 5 | 5 | ✓ | ✓ |
| F11 | Procedural 3D Noise Terrain Generator | VoxelGeneratorNoise fast 3D gradient noise, fBm, quintic fade, 3D domain warping, spherical planet SDF | M3 | 5 | 5 | ✓ | ✓ |
| F12 | Standalone C++ Doctest Verification Suite | test_main.h (736 lines) & test_rendering.h (338 lines) covering all storage, stream, noise & rendering | M4 | 5 | 5 | ✓ | ✓ |
| F13 | E2E Python Test Harness & Build Automation | tests/e2e/runner.py 4-tier runner, build_eden_c.bat MSVC SCons script, README.md docs | M4 | 5 | 5 | ✓ | ✓ |

## Test Architecture
- **E2E Test Runner**: Executed via `python tests/e2e/runner.py`. Supports CLI filtering (`--tier`, `--feature`), formatted table reporting, and structured JSON export (`--json-report test_report.json`).
- **C++ Doctest Suite Integration**: Executed natively via Godot executable CLI: `bin\godot.windows.editor.x86_64.exe --test --test-suite="*GodotEden*"`.
- **Test Case Structure**: Standardized context harness (`E2ETestContext`) providing rich assertions (`assert_equal`, `assert_almost_equal`, `assert_raises`, `assert_true`), resource cleanup hooks (`add_cleanup`), and execution time logging.
- **Reporting**: Console ASCII summary tables and machine-readable JSON logs for CI/CD pipeline consumption.

## Real-World Application Scenarios (Tier 4)
| Scenario ID | Scenario Name | Features Exercised | Description & Verification Goal |
|-------------|---------------|--------------------|--------------------------------|
| T4_001 | Planetary Sphere Initialization | F1, F2, F3, F8, F9, F11 | Stream 512 terrain blocks for a 1,000 km radius planet around player spawn without lock contention. |
| T4_002 | Dynamic Terraforming & SVDAG Update | F3, F7, F8, F10 | Modify VoxelBuffer SDF for 10m crater, update SVDAG Murmur3 hashes, regenerate Dual Contouring physics mesh, save SQLite delta. |
| T4_003 | Vulkan Compute Raymarching LOD Shift | F4, F5, F6 | Fly camera from 10,000 km orbit to surface at 1,000 m/s, updating toroidal clipmaps and 64-bit origin shift seamlessly. |
| T4_004 | SQLite & Region File Save/Load Roundtrip | F8, F10 | Save 1,000 modified chunks into Zstd 32³ Region Files & SQLite DB, reload session, and verify bit-exact data parity. |
| T4_005 | Full C++ Doctest & Python Runner Validation | F1, F2, F12, F13 | Execute complete CI pipeline: SCons build, native Doctest suite run, and 4-tier Python runner verification. |

## Coverage Thresholds
- **Tier 1 (Feature Unit & Functional Coverage)**: 75 test cases (5 per feature across 15 total test categories)
- **Tier 2 (Boundary & Corner Cases)**: 75 test cases (5 boundary/edge cases per feature)
- **Tier 3 (Cross-Feature Pairwise Interactions)**: 15 test cases (pairwise storage, streaming, noise, shader SSBO interactions)
- **Tier 4 (Real-World Planetary Application Scenarios)**: 8 test cases (end-to-end production gameplay workloads)
- **Sanity / Infra Verification**: 2 test cases (framework context assertions & resource cleanup lifecycle)
- **Total E2E Tests**: **175 Test Cases**
