# Plan: Get GodotEden water/terrain demo to 60fps on Ryzen 7 5800X + GTX 750 Ti

## Context

Across this session we built a full water system for the `godot_eden` module (planet-scale
ocean/spring generation, a 3D cellular-automaton simulator, a flat heightfield mesher, a
voxel dig/build toolset, save/load) on top of an existing terrain/octree streaming system.
Along the way the user reported the interactive demo (`demo_eden/water_demo.tscn` /
`water_demo.gd`) as **unplayable**, and asked specifically whether hidden/off-screen water
was wasting simulation time. Three real, separate performance bugs were found and fixed (see
"Already done" below), each verified via a headless, non-interactive reproduction
(`--eden-perf-test` CLI flag, teleports into a known dense coastline and lets the sim run
normally). The worst-case synthetic stress scenario went from a ~36 real-second stall to
~6 real seconds per 30-frame window — a >5x improvement, but still far from smooth, and the
user has now asked to shelve the (already-disabled) ground-absorption feature and treat
**a real, sustained 60fps on their actual hardware (Ryzen 7 5800X CPU, GTX 750 Ti GPU — an
old, weak GPU)** as the concrete target for the next pass, rather than continuing to chase
synthetic worst-case numbers in isolation.

This plan is the continuation point for next session: verify real (not synthetic-worst-case)
frame time on the target hardware, then keep cutting cost — both simulation and rendering —
until 60fps holds up under normal play, not just under a contrived stress test.

## Already done this session (context, not to redo)

- `EdenWaterSimulator::generate_water_mesh_smooth_faces()` rewritten as a heightfield
  mesher (`modules/godot_eden/simulation/eden_water_simulator.h/.cpp`) — flat surface per
  water body, no dual-contouring ripple, real cross-chunk continuity (no seam gaps).
  Verified via C++ tests (`modules/godot_eden/tests/test_water_simulation.h`, the "Flat
  (Heightfield) Water Surface Mesh" TEST_CASE) and visually via screenshots.
- Three perf bugs found and fixed, all in `eden_water_simulator.h/.cpp`:
  1. `max_absorption_per_voxel` was 1.0 (a whole mass unit); absorption keeps a block
     "touched" (never settling) every tick until saturated, so a real coastline's sand/dirt
     shoreline stayed simultaneously active for ~100 ticks. Demo default lowered to 0.08.
     **Absorption itself is now fully shelved** (`absorption_rate := 0.0` in
     `demo_eden/water_demo.gd`) per the user's explicit ask this turn — leave it off; don't
     re-enable without being asked.
  2. `step()`'s catch-up loop ran up to 8 ticks per call; with a large active set each tick
     itself is slow, so catch-up compounded into a feedback spiral. Added
     `max_catchup_ticks` (default 2, settable).
  3. **The dominant cost**: `_tick()` scanned every voxel of every active block, every
     tick, with no cap — a single tick over ~400 simultaneously-active coastal blocks alone
     cost 15-30 real seconds. Added `max_blocks_per_tick` (default 64) + round-robin
     `tick_round_robin_cursor` so a tick's cost is now bounded regardless of active count.
- Demo-side (`water_demo.gd`) mesh-rebuild throttling: `mesh_refresh_batch_size` (caps how
  many active blocks get remeshed per pass, round-robin via `_refresh_cursor`) and
  `water_active_radius` / `water_cull_interval_frames` (periodically calls the new
  `EdenWaterSimulator::deactivate_block()` on blocks far from the camera).
- All changes verified with the full water C++ test suite green (972 assertions, 10 test
  cases) after each change — no regressions.
- Debug CLI flags added to `water_demo.gd` for reproducible testing (keep these, they're
  useful going forward): `--eden-screenshot-tour` (visits ocean/coastline/land, screenshots
  each), `--eden-perf-test` (teleports to a known dense-coastline direction and runs
  normally so the existing per-30-frame profiling prints — `step_ms`/`scan_ms`/
  `refresh_ms`/`cull_ms` — are meaningful).

## Open questions to resolve first (don't guess — measure)

1. **All perf testing so far used the `editor` SCons target** (`build_eden_tests.bat` /
   `build_eden.bat`, `target=editor`), run via `bin/godot.windows.editor.x86_64.console.exe`.
   This has real overhead (debug checks, no LTO/full optimization) that a real player's
   shipped build wouldn't have. Before cutting more simulation/render cost, build a
   `target=template_release` export (or at minimum compare `target=editor` vs
   `target=template_release` numbers) so remaining work is guided by real numbers, not
   editor-build artifacts.
2. **Separate simulation cost from render cost.** The existing per-30-frame print in
   `_process()` (`water_demo.gd`) already breaks out `step_ms` (simulation),
   `scan_ms`/`refresh_ms` (water meshing), `cull_ms`. It does NOT currently measure raw
   terrain streaming/meshing cost (`VoxelWorld::update_world()`) or GPU frame time
   separately from CPU. Given the GTX 750 Ti is the weaker half of this hardware pair, GPU-
   bound cost (raw resident LOD0 chunk/triangle count, draw call count, fill rate from the
   semi-transparent water material) may matter as much or more than the CPU-side water sim
   from here on — profile both before assuming water sim is still the bottleneck.
3. Confirm with the user what "normal play" means for the 60fps target — walking/flying at
   normal `fly_speed` through a mix of land and coastline, not the `--eden-perf-test`
   teleport-into-a-huge-reveal worst case (which will likely always be a slower edge case;
   decide whether it's in-scope to fully fix or just needs to *not crash/freeze*).

## Next steps

1. **Set up the release-build comparison** (Open question 1). Build `target=template_release`
   for this platform, run the same `--eden-perf-test` scenario, compare against the
   existing editor-build numbers. This determines how much of the remaining gap is
   editor-build overhead vs. real cost.
2. **Add GPU/render-side profiling** to `water_demo.gd`'s existing per-30-frame print:
   at minimum, real `Engine.get_frames_per_second()` averaged (not instantaneous) over the
   window, and `resident_lod0_chunks` is already printed — also add total triangle/vertex
   count if cheaply available, since that's a more direct proxy for GPU cost than chunk
   count alone.
3. **Tune `max_blocks_per_tick` further** if simulation is still a meaningful share of
   frame time after the release-build comparison — it's the most direct lever for water-sim
   cost and is already exposed as a setter (`EdenWaterSimulator::set_max_blocks_per_tick`).
   Consider whether it should scale with `mesh_refresh_batch_size`/available frame budget
   dynamically instead of a fixed constant.
4. **If render/GPU-bound**: look at `VoxelWorld`'s octree streaming settings the demo uses
   (`chunks_per_frame_budget := 4096`, `octree_lod_count`, `lod_octree_split_distance`) —
   these control resident chunk count and mesh density, and were tuned for a fast modern
   GPU (`main_10km.gd`'s "proven" defaults), not a GTX 750 Ti. Also reconsider the water
   material itself (`water_demo.gd::_build_water_sim`, `StandardMaterial3D` with alpha
   transparency) — transparency is expensive on older GPUs (no early-Z, overdraw cost scales
   with how much water is stacked/visible at once); a cheaper unshaded or simplified
   material may be worth testing.
5. **Re-verify with the same reproducible tests** at each step: C++ suite
   (`bin\godot.windows.editor.x86_64.console.exe --test --test-case="*Water*"`, expect
   972/972 green, update if new tests are added) and the `--eden-perf-test` /
   `--eden-screenshot-tour` scenarios for a live/visual check. Keep using the established
   pattern: redirect output to a scratch log, poll via `ScheduleWakeup`, kill lingering
   `godot.windows.editor.x86_64.console.exe` processes before each rebuild (stale processes
   holding the `.exe` open caused at least one build failure this session), clean up
   scratch files when done.

## Explicitly out of scope for this pass (per user's own framing)

- Re-enabling ground absorption — shelved, come back to it once sim/render cost is under
  control.
- Chasing the exact `--eden-perf-test` teleport-into-huge-reveal number to zero — it's a
  useful regression guard (already went from ~36s to ~6s per 30-frame window) but is a
  synthetic worst case, not necessarily representative of normal play once profiling
  clarifies what "normal play" costs.
