# Scope: E2E Testing Track

## Architecture & Strategy
The E2E Testing Track designs and verifies a 4-tier opaque-box test suite for GodotEden covering all 13 features specified in `PROJECT.md § Feature Inventory`.

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| 1 | Module Layout & SCons Build | config.py, SCsub, register_types.h/cpp, SCons RD_GLSL shader builder | TM1 | survey |
| 2 | ClassDB Core Node Bindings | VoxelWorld (Node3D), VoxelVolume (Resource), VoxelGenerator (Resource) | TM1 | survey |
| 3 | ClassDB Storage & Stream Bindings | VoxelBuffer (RefCounted), VoxelStreamer (RefCounted), AtcAttributePipeline (RefCounted) | TM1 | survey |
| 4 | Micro-Voxel Raymarching Renderer | VoxelRendererRD Vulkan compute pipeline, micro_voxel_raymarch.glsl, std430 SSBO bindings | TM2 | survey |
| 5 | SVO / SVDAG & Clipmap Hierarchy | SvoNode, SvoDagKey Murmur3 deduplication, clipmap_lod.glsl snapped ring origins | TM2 | survey |
| 6 | Planetary LOD & 64-bit Origin Shift | Screen-space error metric, hysteresis margins, ReferenceChangeInfo floating origin shifting | TM2 | survey |
| 7 | Dual-Path Meshing & Physics | PhysicsMeshGenerator Greedy Meshing & Dual Contouring (QEF minimization) for collision | TM2 | survey |
| 8 | Memory-Efficient Volume Storage | VoxelBuffer 16³ chunks, uniform / 4-bit nibble palette / raw compression tiers | TM2 | survey |
| 9 | Multi-Threaded Streaming & Lock Pipeline | VoxelStreamer view center/radius queue, SpatialLock3D 3D reader-writer locks | TM3 | survey |
| 10 | Block Serialization & Persistence | VoxelBlockSerializer (Zstd + EDEN magic header), VoxelStreamSQLite, VoxelStreamRegionFiles (32³ chunks) | TM3 | survey |
| 11 | Procedural 3D Noise Terrain Generator | VoxelGeneratorNoise fast 3D gradient noise, fBm, quintic fade, 3D domain warping, spherical planet SDF | TM3 | survey |
| 12 | Standalone C++ Doctest Verification Suite | test_main.h & test_rendering.h covering all storage, stream, noise & rendering | TM1/TM4 | survey |
| 13 | E2E Python Test Harness & Build Automation | tests/e2e/runner.py 4-tier runner, build_eden_c.bat MSVC SCons script, README.md docs | TM1/TM4 | survey |

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| 1 | TM1: Test Infra & Runner Verification | Verify doctest suite & python runner; draft TEST_INFRA.md | none | IN_PROGRESS |
| 2 | TM2: Tiers 1 & 2 Test Suite | Construct Tier 1 (>=65 feature tests) and Tier 2 (>=65 boundary tests) | TM1 | PLANNED |
| 3 | TM3: Tiers 3 & 4 Test Suite | Construct Tier 3 (cross-feature) and Tier 4 (planetary scenarios) | TM2 | PLANNED |
| 4 | TM4: Suite Execution & TEST_READY.md | Execute runner, verify 100% pass, publish TEST_READY.md & handoff | TM3 | PLANNED |

## Interface Contracts
- Python test runner invocation: `python tests/e2e/runner.py` (or C++ standalone doctest binary/script execution).
- Returns exit code 0 on 100% pass across all 4 tiers.
