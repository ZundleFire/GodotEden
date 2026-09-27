# Scope: Milestone 3 — Voxel Data Storage & Streaming Pipeline

## Workspace & Context
- Project Root: C:\DEV_DRIVE\Dev\GodotEden
- Original Request File: C:\DEV_DRIVE\Dev\GodotEden\ORIGINAL_REQUEST.md
- Global Project Spec: C:\DEV_DRIVE\Dev\GodotEden\PROJECT.md
- Your Working Directory: C:\DEV_DRIVE\Dev\GodotEden\.agents\sub_orch_m3

## Milestone Requirements (M3)
1. `VoxelBuffer` $16^3$ block storage with 3-tier memory compression (`COMPRESSION_UNIFORM`, 4-bit nibble `COMPRESSION_PALETTE` achieving >74% memory reduction, `COMPRESSION_RAW`).
2. Thread-safe `VoxelDataMap` spatial hash map using fine-grained `RWLock` primitives.
3. Multi-threaded position-aware `VoxelStreamer` with viewer center/radius tracking and bounded request queue limits (`max_pending_requests = 16`).
4. Fine-grained reader/writer 3D spatial region locks via `SpatialLock3D`.
5. Zstd binary payload serialization with magic header `0x4E454445` ("EDEN") via `VoxelBlockSerializer`.
6. Persistence pipelines: SQLite edit delta database (`VoxelStreamSQLite`) and $32^3$ chunk Region files (`VoxelStreamRegionFiles`) with 512 KiB pre-allocated index tables.
7. Procedural 3D noise terrain generator (`VoxelGeneratorNoise`) with 3D gradient noise, quintic fade, multi-octave fBm, 3D domain warping, and spherical planet SDF formula ($\|p\| - R - \text{fBm}$).

Execute the Explorer → Worker → Reviewer → Challenger → Auditor iteration cycle for Milestone 3.
When complete, update `PROJECT.md` to mark M3 as `DONE` and publish your handoff report to `C:\DEV_DRIVE\Dev\GodotEden\.agents\sub_orch_m3\handoff.md`.
