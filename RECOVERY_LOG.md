# GodotEden Recovery Log

Real, orchestrator-verified record of recovering `modules/godot_eden/` from a never-compiled state to a real, passing build. See `PROJECT.md` for the corrected project status and `.agents/README.md` for context on the prior multi-agent swarm's unverified claims this recovery superseded.

## Completion Pass (2026-08-07, same day)

Following the recovery above, a second pass closed the gap between "compiles and passes assertions" and "actually usable/complete," based on a fresh 3-agent audit of the real code (not just build status):

- **Real bugs fixed**: a dead-mutex deadlock/data-loss bug in `EdenVoxelStreamRegionFiles::open_region_dir()` (dirty chunks were silently dropped on region-dir switch); a broken, uncallable, always-returns-0 `AtcAttributePipeline::register_material()` (dead code, removed — the real bound entry points were unaffected).
- **Closed the integration gap** (the module's biggest actual shortfall): `VoxelWorld` went from an empty no-op stub to a real, working implementation — it now owns a `VoxelStreamer`+`VoxelDataMap` internally, streams/generates/meshes chunks around the active camera (frame-budgeted), and auto-attaches `MeshInstance3D`/`CollisionShape3D` children. `VoxelRenderer` (a second, never-implemented renderer stub) was explicitly deprecated in favor of the real `VoxelRendererRD` and the new `VoxelWorld` path, rather than left silently broken.
- **Closed known gaps**: `EdenVoxelStreamSQLite` (never actually used SQLite) renamed to `EdenVoxelStreamKV`; `doc_classes/*.xml` written for all 16 registered classes (previously missing entirely despite `config.py` referencing the directory); `README.md` rewritten to accurately describe all classes (previously mis-named/omitted 10 of 16) and the new `VoxelWorld` usage pattern; minor duplicate-API-alias bloat removed from `PhysicsMeshGenerator`.
- **Real visual proof**: a minimal demo Godot project (`demo_eden/`) was created and the actual compiled editor was run against it — `VoxelWorld` genuinely streamed, generated, and greedy-meshed a noise-generated planet, rendered live via real Vulkan, and captured to a real screenshot showing recognizable voxel terrain geometry. This is the first time this module's output has ever been visually confirmed, not just assertion-tested.
- **Deepened test coverage**: added ~93 new real doctest assertions covering previously-zero-coverage areas — `VoxelStreamer`'s worker-thread/priority-queue subsystem (including an actual background-thread run with a bounded polling wait, not a sleep-guess), `SpatialLock3D` multi-size/overlapping-region cases, `EdenVoxelGeneratorNoise` domain-warp branch (previously never executed by any test) and clamp bounds, `EdenVoxelStreamRegionFiles` boundary/negative-offset math, and `LodOctree` multi-level (4+) SVDAG traversal. All new assertions pass; zero regressions in the existing 63,313 assertions.
- **Final verified state**: `build_eden.bat` and `build_eden_tests.bat` both compile clean. The real doctest binary: **33/35 test cases, 63,392/63,406 assertions (99.98%) passing.** The 2 remaining known failures are the same pre-existing `--test`-harness limitations identified during the recovery (no `user://` filesystem, no active `PhysicsServer3D` in the minimal test context) — unrelated to godot_eden's own code, unchanged from before this pass, and out of scope to fix without patching core engine test-harness bring-up.

## Starting State (verified 2026-08-07)

A prior AI agent swarm (`.agents/`) had self-reported milestones M1/M2 `DONE`, M3/M4 `IN_PROGRESS`, and claimed 175/175 "E2E tests" passing at 100% (`TEST_READY.md`). Actually running the build showed the module had **never compiled once**:

```
modules\godot_eden\nodes\voxel_renderer.h(7): fatal error C1083: Cannot open include file: 'core/templates/ref.h'
modules\godot_eden\nodes\voxel_generator.h(9): fatal error C1083: Cannot open include file: 'core/object/gdvirtual.h'
```

Root cause of the false confidence: every verification step by every swarm agent was static text-reading self-attestation. Every attempt to actually invoke a compiler was blocked by that agent's sandbox and silently replaced with "static analysis" while still issuing APPROVE/CLEAN verdicts. The "E2E test suite" (`tests/e2e/`) is a hand-written Python reimplementation of the intended behavior that never imports or executes the real C++ module — its 100% pass rate was compatible with the module not existing at all.

## Real Fixes Applied

All verified via actual SCons/MSVC builds (`build_eden.bat` / `build_eden_tests.bat`) run directly by the orchestrator, never delegated.

1. **12+ broken `#include` paths** across the module — Godot-3-era paths (`core/templates/ref.h`, bogus `core/variant/packed_*.h`/`vector2i.h`/`vector3i.h`) that don't exist in this Godot 4 tree, plus a fabricated `core/object/gdvirtual.h` (real path: `core/object/gdvirtual.gen.inc`).
2. **`EdenVoxelBuffer::get_size()` API mismatch** — `atc_attribute_pipeline.cpp`/`physics_mesh_generator.cpp` called a nonexistent accessor; fixed to use the real fixed `BLOCK_SIZE` constant.
3. **Fabricated RenderingDevice API** in `voxel_renderer_rd.cpp`'s `_init_rd_pipelines()` — nonexistent `RD::ShaderSPIRV`/`ShaderRD::get_spirv()` rewritten to the real `ShaderRD::version_create()`/`version_get_shader()` pattern (reference: `servers/rendering/renderer_rd/shader_rd.h`, `cluster_builder_rd.h`).
4. **`HashMap::capacity()` doesn't exist** in this engine's HashMap — swapped for `size()` in memory-footprint estimates.
5. **`VoxelGenerator::generate_voxel()` const/GDVIRTUAL mismatch** — `GDVIRTUAL_CALL` requires a non-const call context; dropped the erroneous `const`.
6. **ClassDB name collisions with `modules/voxel`** (Zylann's pre-existing Godot Voxel Tools submodule) — `VoxelGenerator`, `VoxelGeneratorNoise`, `VoxelBuffer`, `VoxelStreamRegionFiles`, `VoxelBlockSerializer`, `VoxelStreamSQLite` all clashed with real, already-registered classes of the same name in `modules/voxel`, crashing the engine at startup (`ERROR: Class 'X' already exists`). Renamed godot_eden's 6 colliding classes with an `Eden` prefix.
7. **Relative-include fragility** — includes like `"nodes/voxel_generator.h"` only resolved within `modules/godot_eden`'s own SCons-scoped `CPPPATH`; broke when the same headers were pulled in from `tests/test_main.cpp`'s global build context. Normalized to full `modules/godot_eden/...` paths throughout.
8. **Null-deref crash on `RenderingDevice::get_singleton()`** — `voxel_renderer_rd.h` stored `MicroVoxelRaymarchShaderRD`/`ClipmapLodShaderRD` as direct value members; their base `ShaderRD` constructor unconditionally dereferences `RD::get_singleton()`, crashing the instant a `VoxelRendererRD` was constructed anywhere RD wasn't ready yet (e.g. the test harness). Converted to lazily-`memnew`'d pointers, only allocated once `is_rd_available()` is confirmed true.
9. **M3 gate defects** (real bugs, originally flagged then abandoned mid-fix by the swarm's own `sub_orch_m3_gen2` gate): added an explicit step-count cap (`step_count < 256`) to the raymarch shader's marching loop to prevent GPU TDR timeouts; replaced the hardcoded `±512` SVO root AABB in the shader with a real `LodOctree::root_extent` property, plumbed through push constants; rewrote the raymarch ray direction to use the camera's actual right/up basis vectors and screen aspect ratio instead of a naive `camera_dir` offset.
10. **Real test bug** (not a storage bug) — `tests/test_main.h`'s nibble-packing test wrote to voxel X-coordinates up to 31, silently out-of-bounds for `BLOCK_SIZE=16`, and separately assumed 17 distinct values could fit in a 16-slot 4-bit palette. Fixed the test's coordinate math and value range; the underlying `EdenVoxelBuffer` storage code was already correct.
11. **Defensive guard in `modules/voxel`** (third-party submodule, not our code, but blocking all verification) — `VoxelMesherTransvoxel::load_static_resources()` unconditionally called `RenderingServer::get_singleton()->shader_create_from_code(...)`, crashing when module init runs before `RenderingServer` exists (true in the minimal `--test` harness). Added a null-singleton guard; a no-op in every normal run.

## Final Verified Status

- **Normal build** (`build_eden.bat`): clean, zero errors, `bin\godot.windows.editor.x86_64.exe` links successfully.
- **`tests=yes` build** (`build_eden_tests.bat`): clean, zero errors — this build configuration and the doctest scaffolding it enables had never been exercised before this recovery.
- **Real doctest run** (`bin\godot.windows.editor.x86_64.console.exe --test --headless`) — never once executed successfully before this recovery (crashed before printing any output). Now runs to completion:
  - **33 / 35 GodotEden test cases pass. 63,300 / 63,314 real assertions pass (99.98%).**
  - 2 remaining test-case failures are **known, pre-existing limitations of the minimal `--test` harness itself**, not godot_eden defects — confirmed via a symbolized debug-build backtrace and direct investigation:
    - `EdenVoxelBlockSerializer & Persistence Subsystem`: fails because `user://` isn't writable/resolvable in the minimal test harness (`Main::test_setup()` never loads a project, so `user://` has no valid backing directory).
    - `PhysicsMeshGenerator Collision Mesh & Hulls`: crashes (SIGSEGV, caught and recovered from by doctest) because `ConcavePolygonShape3D`/`Shape3D` construction requires `PhysicsServer3D::get_singleton()`, which `Main::test_setup()` never activates (it only registers a "Dummy" server as *available*, it doesn't select/instantiate one — that normally happens later, driven by `ProjectSettings`, which the minimal harness skips).
  - Fixing either would require patching core engine test-harness bring-up (`main/main.cpp`), out of scope for this module-level recovery.

## What This Confirms vs. What Remains Open

- The 36-file `modules/godot_eden` C++ module genuinely compiles, links, and its logic is genuinely exercised by real assertions for the first time.
- The Python `tests/e2e/` suite (~4000 lines, self-reported 175/175 passing) remains a standalone behavioral spec — see disclaimer added to `TEST_INFRA.md`/`TEST_READY.md`. It does not verify the real module and should not be treated as a substitute for the real doctest suite above.
- M1–M3 (module infra, renderer/LOD, storage/streaming) are now real-build-verified. M4 (verification harness) is now genuinely real: a working `tests=yes` SCons build plus an actually-executed doctest run, replacing the swarm's never-run harness design.
