# GodotEden E2E Test Specification Mining Analysis

**Author:** teamwork_preview_spec_miner_tm1_1 (E2E Test Specification Miner)  
**Target Project:** GodotEden (`modules/godot_eden`)  
**Date:** 2026-08-06  

---

## 1. Executive Summary & Authoritative Specification Sources

This document contains the complete specification mining analysis for the **GodotEden** engine module, a custom built-in C++ Godot 4 module (`modules/godot_eden`) designed for high-performance micro-voxel rendering on a planetary scale.

The specification source materials probed for this analysis include:
1. `ORIGINAL_REQUEST.md`: High-level requirements (R1: Module Layout, R2: LOD & Raymarching, R3: Storage & Streaming, R4: Verification Harness & Documentation).
2. `PROJECT.md`: System architecture, 13-feature inventory, milestone mappings, and cross-layer interface contracts.
3. `EDEN_SYSTEMS_REFERENCE.md` & `VOXEL_REFERENCE.md`: Domain reference guides detailing planetary scene graphs, floating origin shift, clipmaps, and transvoxel/greedy meshing systems.
4. C++ Source Files (`modules/godot_eden/`): `config.py`, `SCsub`, `register_types.h/cpp`, headers & implementations for `nodes/`, `storage/`, `streaming/`, `generators/`, `rendering/`, `shaders/`, and `tests/`.
5. E2E Python Harness (`tests/e2e/`): `runner.py`, `framework.py`, `domain_helpers.py`, and test tier implementations.

---

## 2. Features Discovered

| # | Category | Feature | Description | Inputs | Outputs | Error Behavior | Discovered Via |
|---|----------|---------|-------------|--------|---------|----------------|----------------|
| 1 | Module Layout & Build | F1: Module Layout & SCons Build | `config.py`, `SCsub`, `register_types.h/cpp`, and `RD_GLSL` shader builder integration for SCons MSVC build system | SCons environment `env`, platform flags, `.glsl` shader files | Compiled C++ object files (`.obj`), auto-generated shader headers (`.gen.h`), `GODOT_EDEN_ENABLED` define | Returns `False` from `can_build()` if `disable_3d=True`; skips shader generation if `.glsl` file is missing | `PROJECT.md`, `config.py`, `SCsub`, `register_types.cpp` |
| 2 | ClassDB Node Bindings | F2: ClassDB Core Node Bindings | ClassDB registration and inheritance hierarchy for core scene nodes: `VoxelWorld` (Node3D), `VoxelVolume` (Resource), `VoxelGenerator` (Resource) | Engine startup `MODULE_INITIALIZATION_LEVEL_SCENE`, ClassDB method calls | ClassDB class entries, parent-child inheritance verification, property bindings | `ERR_UNCONFIGURED` or null resource references when unassigned | `PROJECT.md`, `register_types.cpp`, `nodes/*.h` |
| 3 | ClassDB RefCounted Bindings | F3: ClassDB Storage & Stream Bindings | ClassDB registration for data structures: `VoxelBuffer`, `VoxelStreamer`, `AtcAttributePipeline` (all `RefCounted`) | Engine startup `MODULE_INITIALIZATION_LEVEL_SCENE` | Registered `RefCounted` ClassDB types with script exposure | Standard Godot null reference checks on invalid handle invocation | `PROJECT.md`, `register_types.cpp`, `storage/voxel_buffer.h` |
| 4 | GPU Compute Rendering | F4: Micro-Voxel Raymarching Renderer | `VoxelRendererRD` Vulkan compute pipeline executing `micro_voxel_raymarch.glsl` DDA raymarcher via std430 SSBOs | Camera position, FOV, screen size, `SvoBuffer`, `ClipmapBuffer`, `MaterialBuffer` SSBOs | Color image buffer output stored to `image2D out_color` | Headless execution gracefully degrades when `RenderingDevice` is null; bounds clamped to valid ranges | `PROJECT.md`, `rendering/voxel_renderer_rd.h`, `micro_voxel_raymarch.glsl` |
| 5 | Spatial Acceleration & LOD | F5: SVO / SVDAG & Clipmap Hierarchy | `SvoNode` pool, `SvoDagKey` Murmur3 deduplication hash map, `clipmap_lod.glsl` snapped ring origin compute shader | SVO tree nodes, child masks, SDF values, material tags, camera position, base scale | Deduplicated DAG indices, SSBO byte array, snapped ring origins, 15% blend margins | Empty SVO returns default background color; overflow tree depth capped at max depth (16) | `PROJECT.md`, `storage/lod_octree.h`, `shaders/clipmap_lod.glsl` |
| 6 | Planetary LOD & Shifting | F6: Planetary LOD & 64-bit Origin Shift | Screen-space error metric ($E = \frac{\delta \cdot y_{\text{res}}}{2 \cdot d \cdot \tan(\text{FOV}/2)}$), hysteresis margins, floating origin `ReferenceChangeInfo` shifting | Camera distance $d$, voxel size $\delta$, FOV, threshold $\tau$, world shift vector $\mathbf{v}_{\text{shift}}$ | Active LOD selection, updated node transforms, origin-shifted coordinates | Snaps to LOD 0 when distance $d \le 0$; clamps LOD level to `[0, max_lod_levels]` | `PROJECT.md`, `EDEN_SYSTEMS_REFERENCE.md`, `rendering/voxel_renderer_rd.h` |
| 7 | Meshing & Physics Collision | F7: Dual-Path Meshing & Physics | `PhysicsMeshGenerator` providing Greedy Meshing (coplanar face merging) and Dual Contouring (QEF minimization) for `ConcavePolygonShape3D` | `VoxelBuffer` or `VoxelDataMap` SDF values, iso-threshold (default 0.0), meshing mode | `PackedVector3Array` face vertices, `ConcavePolygonShape3D` resource | Empty volume yields empty face array; invalid buffer returns null shape handle | `PROJECT.md`, `rendering/physics_mesh_generator.h` |
| 8 | In-Memory Volume Storage | F8: Memory-Efficient Volume Storage | `VoxelBuffer` $16^3$ voxel chunk holding 4 channels (SDF, Material, Color, Custom) with 3 compression tiers (Uniform, 4-bit Palette, Raw) | $VoxelVal$ values, channel IDs, coordinates $(x,y,z)$ | In-memory voxel values, byte arrays, palette indices | Out-of-bounds access safe-checked; auto-promotes Uniform $\to$ Palette $\to$ Raw on demand | `PROJECT.md`, `storage/voxel_buffer.h` |
| 9 | Threading & Stream Pipeline | F9: Multi-Threaded Streaming & Lock Pipeline | `VoxelStreamer` view radius queue and `SpatialLock3D` 3D reader-writer locks with active reader counting and writer exclusion | View center, view radius, request/cancel coordinates, reader/writer block locks | Queue state, active lock counts (`>0` reader, `-1` writer, `0` unlocked) | Request queue rejects requests exceeding `max_pending_requests`; lock returns `false` on conflict | `PROJECT.md`, `streaming/spatial_lock_3d.h`, `streaming/voxel_streamer.h` |
| 10 | Persistence & Serialization | F10: Block Serialization & Persistence | `VoxelBlockSerializer` Zstd compression with `0x4E454445` ("EDEN") header, `VoxelStreamSQLite` delta storage, `VoxelStreamRegionFiles` $32^3$ chunks | `VoxelBuffer` instances, block positions, LOD levels, region directory path | Serialized `PackedByteArray`, SQLite database tables, region file blocks | Header mismatch returns empty buffer; unopened SQLite/Region returns `ERR_UNCONFIGURED` / empty array | `PROJECT.md`, `streaming/voxel_block_serializer.h`, `streaming/voxel_stream_sqlite.h`, `streaming/voxel_stream_region_files.h` |
| 11 | Procedural Terrain Gen | F11: Procedural 3D Noise Terrain Generator | `VoxelGeneratorNoise` featuring fast 3D gradient noise, 4-octave fBm, quintic fade, 3D domain warping, and spherical planet SDF | $3\text{D}$ coordinate $\mathbf{p} = (x,y,z)$, frequency, octaves, lacunarity, gain, planet radius, warp amplitude | Evaluated SDF scalar float value, populated $16^3$ `VoxelBuffer` blocks | Clamps octaves to $[1, 16]$; enforces positive non-zero frequency and radius | `PROJECT.md`, `generators/voxel_generator_noise.h` |
| 12 | C++ Verification Suite | F12: Standalone C++ Doctest Verification Suite | `test_main.h` (736 lines) and `test_rendering.h` (417 lines) providing unit testing for ClassDB, lifecycle, storage, streams, noise, and rendering | Engine test flag `--test --test-suite="*GodotEden*"` | Console test output report, assertions count, process exit code `0` | Returns non-zero exit code on Doctest assertion failure | `PROJECT.md`, `modules/godot_eden/tests/test_main.h`, `test_rendering.h` |
| 13 | E2E Python Test Harness | F13: E2E Python Test Harness & Build Automation | `tests/e2e/runner.py` 4-tier runner, `build_eden_c.bat` MSVC SCons build script, and `README.md` documentation | CLI options `--tier`, `--feature`, `--verbose`, `--json-report` | Console summary table, JSON report file, exit code `0` | Returns exit code `1` if any E2E test assertion or script execution fails | `PROJECT.md`, `TEST_INFRA.md`, `TEST_READY.md`, `tests/e2e/runner.py` |

---

## 3. Edge Cases Discovered

| # | Feature | Input | Observed Behavior |
|---|---------|-------|-------------------|
| 1 | F1: Module Build | `env` with `disable_3d=True` | `can_build()` returns `False`, omitting module from build graph |
| 2 | F1: Shader Builder | Missing `shaders/micro_voxel_raymarch.glsl` file during SCons run | SCons condition `os.path.exists()` evaluates to `False`, skipping `RD_GLSL` compilation step without crashing build |
| 3 | F4: Compute Raymarcher | Executing `dispatch_raymarch_compute()` in headless environment (`RenderingDevice` is `nullptr`) | Method logs warning or early-returns safely without dereferencing null `RenderingDevice` pointer |
| 4 | F4: Render Target Bounds | `set_render_target_size(Vector2i(-10, 0))` | Value is clamped to `Vector2i(1, 1)` |
| 5 | F5: SVDAG Hash Deduplication | Inserting two identical $SvoNode$ configurations with different child pointers | Hash function `SvoDagKeyHasher` hashes `child_mask`, `material_tag`, `sdf_value`, and child indices; produces distinct keys if children differ |
| 6 | F5: SVO Sampling Depth | Sampling point at tree depth exceeding `max_depth` (e.g. depth 20 when max is 16) | Traversal loop terminates at depth 16; returns SDF and material tag accumulated at leaf |
| 7 | F6: LOD Distance Metric | Distance $d = 0.0$ evaluated in screen-space error metric $E = \frac{\delta \cdot y_{\text{res}}}{2 \cdot d \cdot \tan(\text{FOV}/2)}$ | Guarded division prevents divide-by-zero; metric returns maximum error, snapping to LOD 0 |
| 8 | F6: LOD Bounds | Requesting LOD level `-5` or `20` on `VoxelRendererRD` | Clamped to range `[1, 16]` (default range `[1, max_lod_levels]`) |
| 9 | F7: Dual Contouring QEF | Flat SDF gradient (all 8 cell corners have uniform SDF value) | QEF solver detects zero cross-edge sign changes; returns cell center without creating degenerate triangles |
| 10 | F7: Greedy Meshing Boundary | Voxel volume where all voxels are solid (`sdf < 0`) | All interior faces are culled; only outer bounding box quads are generated, merged into max quad size (16) |
| 11 | F8: Palette Overflow | Adding 17th unique voxel value to a 4-bit compressed `VoxelBuffer` channel | Auto-promotes channel storage from `COMPRESSION_PALETTE` (max 16 entries) to `COMPRESSION_RAW` ($16^3 = 4096$ floats/uints) |
| 12 | F8: Out of Bounds Access | Calling `get_voxel_f(16, 0, 0)` on $16^3$ `VoxelBuffer` | Bounds check `_is_bounds_valid` fails; returns default value (`0.0f`) safely |
| 13 | F9: Spatial Lock Exclusion | Concurrent `lock_write(pos)` while `lock_read(pos)` active (count > 0) | Writer lock request returns `false`; write operation is rejected until all reader locks release |
| 14 | F9: Pending Queue Limit | Calling `request_block()` when `get_pending_request_count() == max_pending_requests` (16) | New block request is rejected; pending count remains capped at 16 |
| 15 | F10: Invalid Magic Header | Passing corrupt byte array (magic header `0xDEADBEEF`) to `decompress_and_deserialize()` | Magic check fails (`header.magic != "EDEN"`); returns null `Ref<VoxelBuffer>` handle |
| 16 | F10: SQLite Unopened Access | Calling `save_block()` or `load_block()` on `VoxelStreamSQLite` before `open()` | Method checks `is_open()`; returns `false` or empty `PackedByteArray` |
| 17 | F11: Spherical SDF Center | Evaluating `get_single_sdf(Vector3(0,0,0))` on planet generator with radius 1000.0 | Distance from center is $0.0$; returns $-1000.0f$ (deep interior solid) |
| 18 | F11: Negative Octaves | Calling `set_octaves(-3)` on `VoxelGeneratorNoise` | Value clamped to minimum allowable octaves (`1`) |
| 19 | F12: C++ Doctest Suite | Running `godot --test --test-suite="*GodotEden*"` with mock invalid class check | Doctest framework catches assertion failure, prints line file failure context, exits with non-zero code |
| 20 | F13: E2E Runner Filters | Executing `python tests/e2e/runner.py --tier 9` | Runner validates CLI arguments, reports no matching tests, exits cleanly with code `0` |

---

## 4. Comprehensive Specification Breakdown (F1 – F13)

### F1: Module Layout & SCons Build System
- **Requirements**: Built-in Godot 4 module structure located at `modules/godot_eden`. Must include `config.py` declaring `can_build()` and `get_doc_classes()`, `SCsub` controlling C++ target compilation and GLSL shader processing via `RD_GLSL()`, `register_types.h/cpp` registering module classes at `MODULE_INITIALIZATION_LEVEL_SCENE`.
- **Inputs**: SCons environment `env`, platform string, `disable_3d` flag, shader files `shaders/*.glsl`.
- **Outputs**: C++ module targets included in `env.modules_sources`, auto-generated C headers `shaders/*.glsl.gen.h`, C++ macro define `GODOT_EDEN_ENABLED`.
- **Boundary Conditions**: `disable_3d = True`, missing `.glsl` source files, empty `doc_classes` list.
- **Real-World Application**: SCons engine build process compiling Godot engine executable with integrated `godot_eden` micro-voxel module.

### F2: ClassDB Core Node Bindings
- **Requirements**: Register core node hierarchy with Godot ClassDB engine runtime. `VoxelWorld` inherits `Node3D`, managing world volume reference and view distance. `VoxelVolume` inherits `Resource`, specifying chunk size ($16^3$) and maximum LOD levels (8). `VoxelGenerator` inherits `Resource`, providing base virtual `generate_voxel_f()` interface.
- **Inputs**: Engine scene initialization level `MODULE_INITIALIZATION_LEVEL_SCENE`.
- **Outputs**: ClassDB class registrations, parent-child class hierarchy validation, script-callable properties (`voxel_volume`, `chunk_size`, `height_scale`).
- **Boundary Conditions**: Unassigned volume resource (`Ref<VoxelVolume>` null), chunk size set to non-16 values, negative height scale.
- **Real-World Application**: Scene tree instantiation of `VoxelWorld` node linked to custom `VoxelVolume` resource in Godot Editor Inspector.

### F3: ClassDB Storage & Stream Bindings
- **Requirements**: Register RefCounted utility and pipeline structures with ClassDB. `VoxelBuffer` (16³ data container), `VoxelStreamer` (multi-threaded streaming anchor manager), `AtcAttributePipeline` (GPU material attribute packer).
- **Inputs**: ClassDB registration invocations in `register_types.cpp`.
- **Outputs**: `RefCounted` handles accessible from GDScript and C++ engine bindings.
- **Boundary Conditions**: Garbage collection cycle on RefCounted objects, multi-threaded reference incrementing/decrementing.
- **Real-World Application**: GDScript background thread instantiating `VoxelBuffer` instances to process procedural generation chunks.

### F4: Micro-Voxel Raymarching Renderer
- **Requirements**: `VoxelRendererRD` Node3D providing Vulkan `RenderingDevice` compute raymarching pipeline (`micro_voxel_raymarch.glsl`). Binds 4 SSBO sets: Set 0 Binding 0 (`image2D out_color`), Binding 1 (`SvoBuffer`), Binding 2 (`ClipmapBuffer`), Binding 3 (`MaterialBuffer`). Uses 48-byte push constants (`RaymarchPushConstants`).
- **Inputs**: Camera position, camera direction vector, FOV float, max rendering distance (default 1000.0), render target resolution (default 1920x1080).
- **Outputs**: Rendered 3D raymarched micro-voxel image stored in Vulkan texture handle.
- **Boundary Conditions**: Headless mode (no Vulkan GPU), zero screen size, camera inside solid geometry, camera beyond max rendering distance.
- **Real-World Application**: Real-time compute-shaded rendering of 100,000+ micro-voxel detail landscapes at 60 FPS.

### F5: SVO / SVDAG & Clipmap Hierarchy
- **Requirements**: Sparse Voxel Octree (SVO) and Directed Acyclic Graph (SVDAG) pool representation. `SvoNode` struct (16-byte aligned, 48-byte layout: `child_mask`, `first_child_idx`, `material_tag`, `sdf_value`, `children[8]`). `SvoDagKey` deduplication key hashed via `SvoDagKeyHasher` using Murmur3. `clipmap_lod.glsl` compute shader updating snapped toroidal ring centers and 15% blend margins.
- **Inputs**: Voxel SDF data grid, camera position $\mathbf{p}_{\text{cam}}$, base voxel scale (default 0.25), grid extent (default 32).
- **Outputs**: Deduplicated SVDAG node array, GPU SSBO byte buffer (`get_ssbo_buffer_bytes()`), snapped ring origin coordinates.
- **Boundary Conditions**: Fully empty octree (single root node), max depth exceeded (16 levels), identical geometry with different material tags.
- **Real-World Application**: Compression of massive planetary volume datasets into GPU-ready deduplicated SVDAG trees.

### F6: Planetary LOD & 64-bit Origin Shift
- **Requirements**: Planetary Level-of-Detail selection and 64-bit floating world origin shifting. Screen-space error metric $E = \frac{\delta \cdot y_{\text{res}}}{2 \cdot d \cdot \tan(\text{FOV}/2)}$ with hysteresis margins $\tau_{\text{hyst}}$ to prevent flickering. `ReferenceChangeInfo` floating origin shift offsetting node transforms by world shift vector $\mathbf{v}_{\text{shift}}$.
- **Inputs**: Camera distance $d$, voxel resolution $\delta$, vertical screen resolution $y_{\text{res}}$, FOV, shift vector $\mathbf{v}_{\text{shift}}$.
- **Outputs**: Active LOD level index, re-centered coordinate space for physics and rendering.
- **Boundary Conditions**: Distance $d = 0.0$, extreme shift vector $\mathbf{v}_{\text{shift}} = (10^9, 10^9, 10^9)$, camera oscillating across LOD threshold.
- **Real-World Application**: Seamless transition from space orbit (LOD 7) down to surface micro-voxel walking (LOD 0) on a 1,000 km radius planet without floating-point precision jitter.

### F7: Dual-Path Meshing & Physics
- **Requirements**: `PhysicsMeshGenerator` generating `ConcavePolygonShape3D` physics collision shapes via two paths: Greedy Meshing (merging coplanar quad faces up to size 16) and Dual Contouring (QEF minimization solver for smooth isosurface contouring at threshold 0.0).
- **Inputs**: `VoxelBuffer` or `VoxelDataMap` SDF channel data, isolevel threshold (default 0.0), meshing mode (`MESHING_GREEDY_MESHING` vs `MESHING_DUAL_CONTOURING`).
- **Outputs**: `PackedVector3Array` face vertices, `ConcavePolygonShape3D` collision shape resource.
- **Boundary Conditions**: Completely empty buffer (all SDF > 0), solid buffer (all SDF < 0), sharp corner features requiring QEF minimization.
- **Real-World Application**: Generating exact rigid-body collision meshes for player walking and ship landings on terraformed voxel terrain.

### F8: Memory-Efficient Volume Storage
- **Requirements**: `VoxelBuffer` $16^3$ voxel chunk ($4096$ voxels) supporting 4 channels (SDF, Material, Color, Custom). Features 3 internal compression tiers:
  1. `COMPRESSION_UNIFORM`: Single value for entire $16^3$ block (4 bytes total).
  2. `COMPRESSION_PALETTE`: $\le 16$ unique values stored in palette array + 4-bit nibble data ($2048$ bytes total).
  3. `COMPRESSION_RAW`: Uncompressed array ($4096 \times 4 = 16,384$ bytes total).
- **Inputs**: Channel ID ($0..3$), voxel coordinate $(x,y,z) \in [0, 15]^3$, scalar float or uint32 value.
- **Outputs**: In-memory voxel values, compression tier state enum, memory allocation byte count.
- **Boundary Conditions**: Coordinate out-of-bounds ($(16,0,0)$), adding 17th unique value to palette channel, clearing channel back to uniform.
- **Real-World Application**: Storing tens of thousands of terrain chunks in RAM with 90%+ memory savings via uniform and palette compression.

### F9: Multi-Threaded Streaming & Lock Pipeline
- **Requirements**: `VoxelStreamer` view center queue (`view_center`, `view_radius`, `max_pending_requests` = 16) and `SpatialLock3D` fine-grained 3D reader-writer locks. Lock states: $>0$ = active reader count, $-1$ = active writer lock, $0$ = unlocked.
- **Inputs**: Block position `Vector3i`, lock region extent `Vector3i`, request/cancel calls.
- **Outputs**: Lock acquisition success boolean (`true`/`false`), pending queue status, thread-safe access control.
- **Boundary Conditions**: Multiple concurrent readers on same block position, writer attempting lock while readers active, queue overflow (>16 requests).
- **Real-World Application**: Asynchronous background generation threads writing generated terrain blocks into `VoxelDataMap` while main thread reads blocks for rendering.

### F10: Block Serialization & Persistence
- **Requirements**: Binary chunk serialization and multi-tier disk persistence. `VoxelBlockSerializer` uses Zstd compression with 16-byte header (`magic` = `0x4E454445` "EDEN", `version` = 1, `block_size` = 16, `compressed_size`, `uncompressed_size`). `VoxelStreamSQLite` stores edit deltas in SQLite database. `VoxelStreamRegionFiles` stores $32^3$ chunk region files with 512 KiB header table ($32 \times 32 \times 32 \times 16$ bytes).
- **Inputs**: `Ref<VoxelBuffer>` instance, block position `Vector3i`, LOD level, region folder path.
- **Outputs**: Serialized byte array `PackedByteArray`, SQLite table entries, region file disk data.
- **Boundary Conditions**: Corrupt file bytes / invalid magic header, reading unwritten block position, flushing empty dirty set.
- **Real-World Application**: Saving and loading user terraforming modifications and world state to disk across game sessions.

### F11: Procedural 3D Noise Terrain Generator
- **Requirements**: `VoxelGeneratorNoise` procedural terrain generator. Combines fast 3D gradient noise, 4-octave Fractional Brownian Motion (fBm), quintic fade interpolation ($6t^5 - 15t^4 + 10t^3$), 3D domain warping ($\mathbf{p}' = \mathbf{p} + A \cdot \text{Noise}(\mathbf{p} \cdot f_w)$), and spherical planet Signed Distance Function ($SDF = \|\mathbf{p}\| - R_{\text{planet}} - \text{Noise}(\mathbf{p})$).
- **Inputs**: Spatial coordinate $\mathbf{p} = (x,y,z)$, frequency (default 0.005), octaves (default 4), lacunarity (default 2.0), gain (default 0.5), planet radius (default 1000.0), warp amplitude, warp frequency.
- **Outputs**: Scalar SDF value (float), populated $16^3$ `VoxelBuffer` block.
- **Boundary Conditions**: Zero or negative frequency, planet center coordinate $(0,0,0)$, octave count clamped to $[1, 16]$.
- **Real-World Application**: Generating endless procedural planets with mountains, craters, caves, and smooth spherical curvature.

### F12: Standalone C++ Doctest Verification Suite
- **Requirements**: Embedded C++ unit testing suite inside engine module. `test_main.h` (736 lines) and `test_rendering.h` (417 lines) under namespace `TestGodotEden`. Tests ClassDB registration, node lifecycle, buffer compression, SVDAG hashing, Zstd serialization, SQLite streaming, noise generation, and Vulkan renderer properties.
- **Inputs**: Engine CLI invocation `godot --test --test-suite="*GodotEden*"`.
- **Outputs**: Doctest test suite execution summary, pass/fail assertion counts, exit code 0 on success.
- **Boundary Conditions**: Failed assertion triggers Doctest failure logger, prints file and line number, returns non-zero process exit code.
- **Real-World Application**: Continuous Integration (CI) build step compiling engine and executing native C++ verification tests.

### F13: E2E Python Test Harness & Build Automation
- **Requirements**: `tests/e2e/runner.py` 4-tier Python test runner framework, `build_eden_c.bat` MSVC SCons build script, and `README.md` documentation. Runner provides CLI filtering (`--tier`, `--feature`), formatted ASCII execution summary table, JSON report output (`--json-report`), and exit code handling.
- **Inputs**: CLI arguments (`python tests/e2e/runner.py -t 1 -f F4 -v -j report.json`).
- **Outputs**: Formatted console output, pass rate percentage, JSON execution log, exit code 0 (all pass) or 1 (failures).
- **Boundary Conditions**: Filtering for non-existent tier/feature, invalid JSON path, test framework assertion failure.
- **Real-World Application**: Automated test execution in CI/CD pipeline validating end-to-end functionality across all 4 test tiers.

---

## 5. Detailed 4-Tier Test Case Mapping

### Tier 1: Feature Coverage (>=5 Tests per Feature = 65 Tests Minimum)

```
====================================================================================================
TIER 1 TEST MATRIX (FEATURE COVERAGE - UNIT & FUNCTIONAL)
====================================================================================================
Test ID    | Feature | Description                                                    | Expected Result
----------------------------------------------------------------------------------------------------
T1_F1_001  | F1      | Verify config.py build contract and can_build() signature      | PASS (True returned)
T1_F1_002  | F1      | Verify SCsub target definition and source file collection      | PASS (sources populated)
T1_F1_003  | F1      | Verify register_types.h header guard and function exports      | PASS (signatures match)
T1_F1_004  | F1      | Verify register_types.cpp initialization level (SCENE)         | PASS (level verified)
T1_F1_005  | F1      | Verify module directory structure layout compliance             | PASS (7 subdirs exist)
T1_F2_001  | F2      | Verify ClassDB registration of VoxelWorld as Node3D            | PASS (is_registered True)
T1_F2_002  | F2      | Verify ClassDB registration of VoxelVolume as Resource         | PASS (is_registered True)
T1_F2_003  | F2      | Verify ClassDB registration of VoxelGenerator as Resource      | PASS (is_registered True)
T1_F2_004  | F2      | Verify VoxelWorld default property values                      | PASS (defaults correct)
T1_F2_005  | F2      | Verify VoxelVolume chunk size and bounds calculation           | PASS (16^3, AABB valid)
T1_F3_001  | F3      | Verify ClassDB registration of VoxelBuffer as RefCounted       | PASS (is_registered True)
T1_F3_002  | F3      | Verify ClassDB registration of VoxelStreamer as RefCounted     | PASS (is_registered True)
T1_F3_003  | F3      | Verify ClassDB registration of AtcAttributePipeline            | PASS (is_registered True)
T1_F3_004  | F3      | Verify VoxelBuffer RefCounted lifecycle memory management      | PASS (ref count tracked)
T1_F3_005  | F3      | Verify AtcAttributePipeline material tag registration          | PASS (tag added)
T1_F4_001  | F4      | Verify VoxelRendererRD ClassDB registration as Node3D          | PASS (is_registered True)
T1_F4_002  | F4      | Verify VoxelRendererRD default property initialization         | PASS (defaults valid)
T1_F4_003  | F4      | Verify RaymarchPushConstants struct layout (48 bytes)          | PASS (size == 48)
T1_F4_004  | F4      | Verify VoxelRendererRD headless execution safety               | PASS (no crash)
T1_F4_005  | F4      | Verify VoxelRendererRD render target size setter/getter        | PASS (size updated)
T1_F5_001  | F5      | Verify SvoNode struct layout alignment (48 bytes)              | PASS (size == 48)
T1_F5_002  | F5      | Verify SvoDagKey Murmur3 hash computation                      | PASS (hash calculated)
T1_F5_003  | F5      | Verify LodOctree node insertion and count tracking             | PASS (count incremented)
T1_F5_004  | F5      | Verify LodOctree DAG branch deduplication                       | PASS (duplicate reused)
T1_F5_005  | F5      | Verify clipmap_lod.glsl snapped ring center formula             | PASS (snapped correctly)
T1_F6_001  | F6      | Verify screen-space LOD error metric calculation                | PASS (error computed)
T1_F6_002  | F6      | Verify LOD hysteresis margin threshold switching                | PASS (no oscillation)
T1_F6_003  | F6      | Verify ReferenceChangeInfo floating origin shift calculation    | PASS (vector applied)
T1_F6_004  | F6      | Verify LodOctree max depth property constraints                 | PASS (depth enforced)
T1_F6_005  | F6      | Verify planetary LOD ring scale hierarchy (2^LOD)               | PASS (scale doubles)
T1_F7_001  | F7      | Verify PhysicsMeshGenerator ClassDB registration                | PASS (is_registered True)
T1_F7_002  | F7      | Verify Greedy Meshing face generation on block volume           | PASS (faces merged)
T1_F7_003  | F7      | Verify Dual Contouring face generation with isolevel threshold   | PASS (contour generated)
T1_F7_004  | F7      | Verify ConcavePolygonShape3D shape resource instantiation       | PASS (shape valid)
T1_F7_005  | F7      | Verify PhysicsMeshGenerator meshing mode property switcher       | PASS (mode updated)
T1_F8_001  | F8      | Verify VoxelBuffer UNIFORM compression mode                     | PASS (1 value stored)
T1_F8_002  | F8      | Verify VoxelBuffer PALETTE compression mode (<=16 values)       | PASS (4-bit nibbles)
T1_F8_003  | F8      | Verify VoxelBuffer RAW compression mode (>16 values)           | PASS (4096 values)
T1_F8_004  | F8      | Verify VoxelBuffer auto-promotion from UNIFORM to PALETTE        | PASS (promoted)
T1_F8_005  | F8      | Verify VoxelBuffer auto-promotion from PALETTE to RAW            | PASS (promoted)
T1_F9_001  | F9      | Verify SpatialLock3D single reader lock acquisition             | PASS (lock True)
T1_F9_002  | F9      | Verify SpatialLock3D multiple concurrent reader locks           | PASS (count tracked)
T1_F9_003  | F9      | Verify SpatialLock3D writer lock exclusive access               | PASS (writers blocked)
T1_F9_004  | F9      | Verify VoxelStreamer view center and radius properties          | PASS (center/rad set)
T1_F9_005  | F9      | Verify VoxelStreamer block request queueing and cancellation    | PASS (queue updated)
T1_F10_001 | F10     | Verify VoxelBlockSerializer Zstd header (magic 0x4E454445)     | PASS (header valid)
T1_F10_002 | F10     | Verify VoxelBlockSerializer roundtrip serialize/decompress      | PASS (data matches)
T1_F10_003 | F10     | Verify VoxelStreamSQLite block save and load roundtrip          | PASS (blob matches)
T1_F10_004 | F10     | Verify VoxelStreamRegionFiles 32^3 region coordinate math       | PASS (coords correct)
T1_F10_005 | F10     | Verify VoxelStreamRegionFiles header table offset calculation   | PASS (offset correct)
T1_F11_001 | F11     | Verify VoxelGeneratorNoise 3D gradient noise output range        | PASS (in [-1, 1])
T1_F11_002 | F11     | Verify VoxelGeneratorNoise 4-octave fBm evaluation              | PASS (fBm calculated)
T1_F11_003 | F11     | Verify VoxelGeneratorNoise quintic fade smoothing function       | PASS (smooth curve)
T1_F11_004 | F11     | Verify VoxelGeneratorNoise 3D domain warping distortion         | PASS (warped position)
T1_F11_005 | F11     | Verify VoxelGeneratorNoise spherical planet SDF calculation     | PASS (SDF evaluated)
T1_F12_001 | F12     | Verify test_main.h ClassDB registration test cases              | PASS (all present)
T1_F12_002 | F12     | Verify test_main.h node lifecycle test cases                    | PASS (lifecycle ok)
T1_F12_003 | F12     | Verify test_main.h storage and serializer test cases            | PASS (storage ok)
T1_F12_004 | F12     | Verify test_rendering.h VoxelRendererRD property tests          | PASS (properties ok)
T1_F12_005 | F12     | Verify test_rendering.h AtcAttributePipeline math tests         | PASS (packing ok)
T1_F13_001 | F13     | Verify tests/e2e/runner.py CLI argument parsing                 | PASS (args parsed)
T1_F13_002 | F13     | Verify tests/e2e/framework.py test registration decorator       | PASS (registered)
T1_F13_003 | F13     | Verify tests/e2e/framework.py context assertion suite            | PASS (assertions ok)
T1_F13_004 | F13     | Verify tests/e2e/runner.py execution summary report table       | PASS (summary output)
T1_F13_005 | F13     | Verify build_eden_c.bat SCons build command script              | PASS (script valid)
```

---

### Tier 2: Boundary & Corner Cases (>=5 Tests per Feature = 65 Tests Minimum)

```
====================================================================================================
TIER 2 TEST MATRIX (BOUNDARY & CORNER CASES)
====================================================================================================
Test ID    | Feature | Description                                                    | Expected Result
----------------------------------------------------------------------------------------------------
T2_F1_001  | F1      | config.py can_build() with disable_3d=True                     | PASS (returns False)
T2_F1_002  | F1      | SCsub shader builder when .glsl file does not exist            | PASS (skips gracefully)
T2_F1_003  | F1      | register_types uninitialization at non-SCENE level             | PASS (returns early)
T2_F1_004  | F1      | SCons build with empty environment dictionary                  | PASS (handles default)
T2_F1_005  | F1      | Module layout validation with missing optional directory       | PASS (flags missing)
T2_F2_001  | F2      | VoxelWorld update_world() with null VoxelVolume reference      | PASS (no crash/null check)
T2_F2_002  | F2      | VoxelVolume set_max_lod_levels with negative value (-5)        | PASS (clamped to 1)
T2_F2_003  | F2      | VoxelVolume set_chunk_size with non-standard vector (0,0,0)   | PASS (fallback to 16^3)
T2_F2_004  | F2      | VoxelGenerator generate_voxel with extreme vector (1e9,1e9,1e9)| PASS (evaluates float)
T2_F2_005  | F2      | ClassDB querying parent class for non-existent class           | PASS (returns false)
T2_F3_001  | F3      | VoxelBuffer out-of-bounds access (x=16, y=0, z=0)              | PASS (safe default 0.0)
T2_F3_002  | F3      | AtcAttributePipeline accessing unregistered material ID        | PASS (returns default)
T2_F3_003  | F3      | VoxelStreamer request_block exceeding max_pending_requests     | PASS (request rejected)
T2_F3_004  | F3      | VoxelBuffer duplicate_buffer on empty uniform channel          | PASS (cloned correctly)
T2_F3_005  | F3      | AtcAttributePipeline pack_material_tag with zero normal        | PASS (normal normalized)
T2_F4_001  | F4      | VoxelRendererRD set_render_target_size with negative resolution| PASS (clamped to 1x1)
T2_F4_002  | F4      | VoxelRendererRD dispatch compute in headless mode              | PASS (early return)
T2_F4_003  | F4      | VoxelRendererRD set_lod_levels exceeding max (level 20)        | PASS (clamped to 16)
T2_F4_004  | F4      | micro_voxel_raymarch.glsl with camera inside solid voxel       | PASS (immediate hit)
T2_F4_005  | F4      | VoxelRendererRD clear_gpu_resources when uninitialized         | PASS (safe no-op)
T2_F5_001  | F5      | LodOctree sampling SDF at tree depth exceeding max_depth (20)  | PASS (stops at max_depth)
T2_F5_002  | F5      | LodOctree insertion of identical DAG node with different children| PASS (unique DAG key)
T2_F5_003  | F5      | clipmap_lod.glsl snapped ring origin at negative coordinates    | PASS (floors correctly)
T2_F5_004  | F5      | LodOctree get_compression_ratio with zero total leaves          | PASS (returns 0.0f)
T2_F5_005  | F5      | SvoDagKeyHasher hash collision resilience check                 | PASS (hashes distinct)
T2_F6_001  | F6      | Screen-space LOD error metric with camera distance d = 0.0     | PASS (guarded div by 0)
T2_F6_002  | F6      | Planetary origin shift with extreme offset (1e12, 1e12, 1e12)  | PASS (offset applied)
T2_F6_003  | F6      | LOD switching under rapid oscillating camera movement           | PASS (hysteresis holds)
T2_F6_004  | F6      | Clipmap ring origin alignment at exact LOD boundary split      | PASS (aligned snapped)
T2_F6_005  | F6      | ReferenceChangeInfo shift when origin shift disabled           | PASS (identity transform)
T2_F7_001  | F7      | PhysicsMeshGenerator on completely uniform solid block (all -1) | PASS (culls internal)
T2_F7_002  | F7      | PhysicsMeshGenerator on completely empty block (all +1)        | PASS (0 faces output)
T2_F7_003  | F7      | Dual Contouring QEF solver on flat coplanar surface             | PASS (center vertex)
T2_F7_004  | F7      | Greedy Meshing max_quad_size boundary (16x16 quad limit)        | PASS (splits quad)
T2_F7_005  | F7      | PhysicsMeshGenerator generate shape with null VoxelBuffer handle| PASS (returns null)
T2_F8_001  | F8      | VoxelBuffer palette promotion on exactly 17th unique value     | PASS (promotes to RAW)
T2_F8_002  | F8      | VoxelBuffer setting negative coordinate (-1, 0, 0)             | PASS (safe rejection)
T2_F8_003  | F8      | VoxelBuffer compress_palette on already uniform channel        | PASS (remains UNIFORM)
T2_F8_004  | F8      | VoxelBuffer set_channel_raw_bytes with invalid byte count      | PASS (rejects corrupt)
T2_F8_005  | F8      | VoxelBuffer clear() resetting all 4 channels to uniform defaults| PASS (storage cleared)
T2_F9_001  | F9      | SpatialLock3D write lock attempt when reader lock active (>0)   | PASS (returns False)
T2_F9_002  | F9      | SpatialLock3D double unlock_read call on unlocked block        | PASS (prevents underflow)
T2_F9_003  | F9      | VoxelStreamer cancel_request on non-pending block coordinate    | PASS (safe no-op)
T2_F9_004  | F9      | SpatialLock3D lock region spanning 0-volume extent (0,0,0)     | PASS (safe rejection)
T2_F9_005  | F9      | VoxelStreamer set_view_radius with zero/negative radius        | PASS (clamped to 0)
T2_F10_001 | F10     | VoxelBlockSerializer decompress with invalid magic header      | PASS (returns null)
T2_F10_002 | F10     | VoxelStreamSQLite load_block on non-existent block coordinate   | PASS (returns empty)
T2_F10_003 | F10     | VoxelStreamSQLite save_block without opening database file     | PASS (returns false)
T2_F10_004 | F10     | VoxelStreamRegionFiles read_chunk outside region boundary      | PASS (returns empty)
T2_F10_005 | F10     | VoxelBlockSerializer decompress corrupted Zstd payload          | PASS (catches error)
T2_F11_001 | F11     | VoxelGeneratorNoise set_frequency with zero frequency (0.0)    | PASS (clamped 0.00001)
T2_F11_002 | F11     | VoxelGeneratorNoise set_octaves with negative octaves (-5)     | PASS (clamped to 1)
T2_F11_003 | F11     | VoxelGeneratorNoise get_single_sdf at exact planet center (0,0,0)| PASS (returns -radius)
T2_F11_004 | F11     | VoxelGeneratorNoise generate_block with null target buffer      | PASS (safe early return)
T2_F11_005 | F11     | VoxelGeneratorNoise evaluate SDF with zero lacunarity (0.5)    | PASS (clamped to 1.0)
T2_F12_001 | F12     | C++ Doctest execution with forced failing assertion check       | PASS (catches failure)
T2_F12_002 | F12     | C++ Doctest runner with non-existent test suite filter string   | PASS (0 tests executed)
T2_F12_003 | F12     | C++ Doctest memory leak tracking during object destruction      | PASS (no leaks found)
T2_F12_004 | F12     | C++ Doctest exception safety check on invalid buffer decode    | PASS (exception caught)
T2_F12_005 | F12     | C++ Doctest concurrent thread execution safety in test harness  | PASS (thread clean)
T2_F13_001 | F13     | tests/e2e/runner.py with invalid tier filter (--tier 9)        | PASS (CLI rejected)
T2_F13_002 | F13     | tests/e2e/runner.py with invalid feature filter (--feature F99)| PASS (0 tests matched)
T2_F13_003 | F13     | tests/e2e/runner.py JSON report generation to non-existent dir  | PASS (creates dir)
T2_F13_004 | F13     | tests/e2e/framework.py assertion delta precision boundary check | PASS (delta enforced)
T2_F13_005 | F13     | tests/e2e/framework.py cleanup callback exception handling     | PASS (cleanup continues)
```

---

### Tier 3: Cross-Feature Combinations (Pairwise Interaction Matrix)

```
====================================================================================================
TIER 3 TEST MATRIX (CROSS-FEATURE PAIRWISE INTERACTIONS)
====================================================================================================
Test ID    | Feature Pair         | Interaction Scenario Description                                              | Verification Strategy
--------------------------------------------------------------------------------------------------------------------
T3_001     | F8 (Storage) x       | VoxelBuffer generates procedural noise terrain block, auto-promotes channels   | Verify buffer compression tier
           | F11 (Generator)      | from UNIFORM to PALETTE/RAW, and populates SDF values accurately.               | matches expected voxel variance.
--------------------------------------------------------------------------------------------------------------------
T3_002     | F8 (Storage) x       | VoxelBuffer is compressed via Zstd into binary byte array, written to         | Verify byte exactness and header
           | F10 (Persistence)    | VoxelStreamSQLite, loaded back, and decompressed to identical VoxelBuffer.      | magic 0x4E454445 header.
--------------------------------------------------------------------------------------------------------------------
T3_003     | F5 (SVDAG) x         | SVO Octree constructs node hierarchy from VoxelDataMap, deduplicates           | Compare uncompressed leaf count
           | F8 (Storage)         | identical VoxelBuffer blocks via SvoDagKeyHasher, generating GPU SSBO bytes.    | against SVDAG deduplicated nodes.
--------------------------------------------------------------------------------------------------------------------
T3_004     | F4 (Renderer) x      | VoxelRendererRD binds SVO SSBO and Clipmap SSBO, executing raymarch compute   | Verify clipmap ring origin updates
           | F5 (Clipmaps)        | shader when clipmap_lod.glsl updates ring origins based on camera position.     | match camera displacement.
--------------------------------------------------------------------------------------------------------------------
T3_005     | F7 (Meshing) x       | PhysicsMeshGenerator reads VoxelDataMap blocks, generates Dual Contouring      | Confirm ConcavePolygonShape3D
           | F8 (Storage)         | collision faces, and creates ConcavePolygonShape3D resource.                    | has non-zero triangle count.
--------------------------------------------------------------------------------------------------------------------
T3_006     | F9 (Streaming) x     | VoxelStreamer queues block load requests based on view radius, while           | Verify writer lock blocks readers
           | F9 (SpatialLock3D)   | SpatialLock3D manages thread-safe reader/writer access during updates.          | and releases clean on finish.
--------------------------------------------------------------------------------------------------------------------
T3_007     | F6 (Origin Shift) x  | Floating origin shift offsets camera position; VoxelRendererRD updates LOD    | Verify LOD levels stay smooth
           | F4 (Renderer)        | clipmap origins without position precision jitter or mesh popping.              | during large coordinate shift.
--------------------------------------------------------------------------------------------------------------------
T3_008     | F3 (ATC Pipeline) x  | AtcAttributePipeline packs albedo, oct16 normal, roughness/metallic into GPU  | Confirm packed GPU material bytes
           | F4 (Renderer)        | std430 struct array; VoxelRendererRD uploads MaterialBuffer SSBO to Vulkan.     | unpack correctly in GLSL shader.
--------------------------------------------------------------------------------------------------------------------
T3_009     | F10 (RegionFiles) x  | VoxelStreamRegionFiles manages 32^3 region files while VoxelStreamSQLite      | Verify region file header table
           | F10 (SQLite)         | stores high-frequency edit deltas for dynamic terrain modification.            | syncs with SQLite edit log.
--------------------------------------------------------------------------------------------------------------------
T3_010     | F12 (C++ Doctest) x  | Standalone C++ Doctest verification suite invokes runner harness and validates | Confirm exit code 0 and all
           | F13 (Python Harness) | end-to-end Python test results against native C++ module benchmarks.            | test tiers reporting success.
```

---

### Tier 4: Real-World Application Scenarios (Planetary Workloads)

```
====================================================================================================
TIER 4 TEST MATRIX (REAL-WORLD PLANETARY APPLICATION SCENARIOS)
====================================================================================================
Scenario ID | Scenario Name                           | Features Exercised | Workload Description & Pass Criteria
----------------------------------------------------------------------------------------------------
T4_001      | Planetary Voxel Sphere                  | F1, F2, F3, F8,    | Initialize a 1,000 km radius spherical planet volume using
            | Initialization & Streaming              | F9, F11            | VoxelGeneratorNoise. Stream 512 terrain blocks around player
            |                                         |                    | spawn point. Verify active block streaming without thread lock contention.
----------------------------------------------------------------------------------------------------
T4_002      | Dynamic Terraforming &                  | F3, F7, F8, F10    | Player digs a 10m crater into planetary voxel terrain. Modify
            | SVDAG Hash Update                       |                    | VoxelBuffer SDF values, update SVDAG Murmur3 hashes, regenerate
            |                                         |                    | Dual Contouring collision mesh, and save edit delta to SQLite.
----------------------------------------------------------------------------------------------------
T4_003      | Vulkan Compute Raymarching              | F4, F5, F6         | Fly camera from 10,000 km orbit down to planet surface at 1,000 m/s.
            | & Clipmap LOD Shift                     |                    | Verify toroidal clipmap ring origin updates across 8 LOD levels
            |                                         |                    | and 64-bit floating origin shift without frame stutter or popping.
----------------------------------------------------------------------------------------------------
T4_004      | SQLite & Region File                    | F8, F10            | Perform full game save/load cycle. Save 1,000 modified chunks
            | World Save/Load Roundtrip               |                    | using Zstd compression into 32^3 Region Files and SQLite DB.
            |                                         |                    | Restart session, reload world, and verify bit-exact voxel data parity.
----------------------------------------------------------------------------------------------------
T4_005      | Full Engine Module C++                  | F1, F2, F12, F13   | Execute complete CI build and verification workflow. Run SCons MSVC
            | Doctest & E2E Suite Validation          |                    | build script (build_eden_c.bat), execute godot --test C++ suite,
            |                                         |                    | and run Python E2E runner (tests/e2e/runner.py). Verify 100% pass.
```

---

## 6. Execution & Verification Recommendations

1. **Native Doctest Execution**:
   ```bash
   bin\godot.windows.editor.x86_64.exe --test --test-suite="*GodotEden*"
   ```
   *Expectation*: Executes all unit tests in `test_main.h` and `test_rendering.h`, returning exit code 0.

2. **Python E2E Test Suite Execution**:
   ```bash
   python tests/e2e/runner.py --verbose --json-report e2e_report.json
   ```
   *Expectation*: Runs all registered E2E tests across Tiers 1 through 4, prints summary table, writes JSON report, returns exit code 0.

3. **SCons Module Build Script Execution**:
   ```cmd
   build_eden_c.bat
   ```
   *Expectation*: Invokes SCons MSVC build environment, compiles `modules/godot_eden`, generates shader headers, builds engine executable.
