# Milestone 2 Technical Analysis: Streaming, Persistence, ClassDB Registration, SCsub Updates, & Doctest Suite

**Author**: explorer_m2_3  
**Target Module**: `modules/godot_eden`  
**Working Directory**: `C:\DEV_DRIVE\Dev\GodotEden\.agents\explorer_m2_3`  
**Date**: 2026-08-05  

---

## 1. Executive Summary

Milestone 2 establishes the core dual-tier voxel data storage, multithreaded chunk streaming, procedural planet generation, Zstd binary serialization, SQLite edit-delta persistence, `mmap` region file persistence, full Godot 4 `ClassDB` registrations, SCons build system updates, and a comprehensive C++ `Doctest` unit test suite.

This technical analysis details the exact software architecture, C++ class declarations, data structures, algorithms, Godot 4 engine integration patterns, and unit test suites required for the streaming, persistence, build, and verification components of Milestone 2.

---

## 2. SpatialLock3D Subsystem Design (`spatial_lock_3d.h/cpp`)

### 2.1 Overview & Requirements
The `SpatialLock3D` class manages thread-safe reader-writer locks across 3D block coordinate neighborhoods (`Vector3i` block position, `Vector3i` neighborhood size). This prevents data races between background streaming tasks running on Godot's `WorkerThreadPool` and main-thread queries or concurrent chunk updates.

### 2.2 Header Specification (`spatial_lock_3d.h`)
```cpp
/**************************************************************************/
/*  spatial_lock_3d.h                                                     */
/**************************************************************************/

#pragma once

#include "core/math/vector3i.h"
#include "core/object/ref_counted.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"

class SpatialLock3D : public RefCounted {
	GDCLASS(SpatialLock3D, RefCounted);

private:
	mutable Mutex mutex;
	// Map lock values: >0 = count of active readers; -1 = active writer lock; 0 = unlocked
	HashMap<Vector3i, int> lock_counts;

protected:
	static void _bind_methods();

public:
	SpatialLock3D();
	~SpatialLock3D();

	bool lock_read(const Vector3i &p_block_pos, const Vector3i &p_size = Vector3i(1, 1, 1));
	void unlock_read(const Vector3i &p_block_pos, const Vector3i &p_size = Vector3i(1, 1, 1));

	bool lock_write(const Vector3i &p_block_pos, const Vector3i &p_size = Vector3i(1, 1, 1));
	void unlock_write(const Vector3i &p_block_pos, const Vector3i &p_size = Vector3i(1, 1, 1));

	bool is_locked(const Vector3i &p_block_pos) const;
	int get_lock_state(const Vector3i &p_block_pos) const;
	void clear();
};
```

### 2.3 Implementation Details (`spatial_lock_3d.cpp`)
- **Reader Acquisition (`lock_read`)**:
  Inspects all 3D blocks in range $[x, x + \text{size.x}) \times [y, y + \text{size.y}) \times [z, z + \text{size.z})$. If any block has `lock_counts[pos] < 0` (write-locked), returns `false`. Otherwise, increments `lock_counts[pos]` for all blocks in range and returns `true`.
- **Writer Acquisition (`lock_write`)**:
  Inspects all 3D blocks in range. If any block has `lock_counts[pos] != 0` (read-locked or write-locked), returns `false`. Otherwise, sets `lock_counts[pos] = -1` for all blocks in range and returns `true`.
- **Releasing Locks (`unlock_read` / `unlock_write`)**:
  Decrements or resets counts for blocks in range. Erases key from `lock_counts` when count reaches 0 to maintain minimal memory footprint.

---

## 3. Multithreaded Chunk Streaming & WorkerThreadPool Architecture

### 3.1 Overview & Integration
Chunk streaming is coordinated by `VoxelStreamer` using Godot's built-in `WorkerThreadPool` singleton (`WorkerThreadPool::get_singleton()`). Streaming tasks run background generation or deserialization out-of-core without stalling the main thread.

### 3.2 Block Streaming State Machine
```
              request_block()
  [UNLOADED] -----------------> [LOADING]
                                   |
                         WorkerThreadPool Task
                                   v
  [DIRTY] <---- edit ---- [LOADED]
     |                       |
   save                      v (view distance exit)
     v                   [UNLOADED]
  [SAVING] -> saved -> [UNLOADED]
```

### 3.3 Task Scheduling Algorithm
1. `VoxelStreamer::request_block(const Vector3i &p_pos)` checks current `pending_requests` count against `max_pending_requests`.
2. If within limits, marks state as `STATE_LOADING` and schedules `WorkerThreadPool::get_singleton()->add_native_task(&VoxelStreamer::_background_load_task, task_data, true, "VoxelStreamerLoad")`.
3. In `_background_load_task`:
   - Acquire `SpatialLock3D::lock_write(p_pos)`
   - Try loading from `VoxelStreamSQLite` or `VoxelStreamRegionFiles`
   - If not found in persistence, invoke `VoxelGeneratorNoise::generate_chunk()`
   - Compact chunk via `VoxelBuffer::compress_palette()`
   - Store result in `VoxelDataMap`
   - Release `SpatialLock3D::unlock_write(p_pos)`
   - Transition state to `STATE_LOADED`

---

## 4. VoxelBlockSerializer Subsystem Design (`voxel_block_serializer.h/cpp`)

### 4.1 Overview & Header Format
`VoxelBlockSerializer` handles binary serialization and Zstd compression for `VoxelBuffer` blocks.

```
+-------------------+--------------------+------------------+-----------------------+-----------------------+
|  Magic "EDEN"     |  Version (uint16)  | Size (uint16)    | CompLen (uint32)      | RawLen (uint32)       |
|  4 bytes          |  2 bytes           | 2 bytes          | 4 bytes               | 4 bytes               |
+-------------------+--------------------+------------------+-----------------------+-----------------------+
|  Zstd Compressed Payload (SDF channels, material indices, compact palette data)                          |
+-----------------------------------------------------------------------------------------------------------+
```

### 4.2 Class Declaration (`voxel_block_serializer.h`)
```cpp
/**************************************************************************/
/*  voxel_block_serializer.h                                              */
/**************************************************************************/

#pragma once

#include "core/io/compression.h"
#include "core/object/ref_counted.h"
#include "core/variant/packed_byte_array.h"
#include "storage/voxel_buffer.h"

class VoxelBlockSerializer : public RefCounted {
	GDCLASS(VoxelBlockSerializer, RefCounted);

public:
	static const uint32_t MAGIC_HEADER = 0x4E454445; // "EDEN" in little-endian

	struct Header {
		char magic[4] = {'E', 'D', 'E', 'N'};
		uint16_t version = 1;
		uint16_t block_size = 16;
		uint32_t compressed_size = 0;
		uint32_t uncompressed_size = 0;
	};

protected:
	static void _bind_methods();

public:
	VoxelBlockSerializer();
	~VoxelBlockSerializer();

	static PackedByteArray serialize_and_compress(const Ref<VoxelBuffer> &p_buffer);
	static Ref<VoxelBuffer> decompress_and_deserialize(const PackedByteArray &p_bytes);
};
```

### 4.3 Implementation Details (`voxel_block_serializer.cpp`)
- Uses `Compression::compress(dst, src, src_len, Compression::MODE_ZSTD)` for ultra-fast compression ratios ($70\%+$ size reduction).
- Validates magic header `"EDEN"`, version, and buffer bounds on deserialization.

---

## 5. VoxelStreamSQLite Delta Persistence Subsystem Design (`voxel_stream_sqlite.h/cpp`)

### 5.1 Overview & Schema
`VoxelStreamSQLite` provides database persistence for user edit deltas and modified chunks.

```sql
CREATE TABLE IF NOT EXISTS edit_deltas (
    x INT,
    y INT,
    z INT,
    lod INT,
    data BLOB,
    PRIMARY KEY (x, y, z, lod)
);
```

### 5.2 Header Specification (`voxel_stream_sqlite.h`)
```cpp
/**************************************************************************/
/*  voxel_stream_sqlite.h                                                 */
/**************************************************************************/

#pragma once

#include "core/io/file_access.h"
#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/packed_byte_array.h"

class VoxelStreamSQLite : public RefCounted {
	GDCLASS(VoxelStreamSQLite, RefCounted);

private:
	String db_path;
	bool is_open_flag = false;
	HashMap<Vector3i, PackedByteArray> in_memory_deltas;

protected:
	static void _bind_methods();

public:
	VoxelStreamSQLite();
	~VoxelStreamSQLite();

	Error open(const String &p_path);
	void close();
	bool is_open() const;

	bool save_block(const Vector3i &p_pos, int p_lod, const PackedByteArray &p_bytes);
	PackedByteArray load_block(const Vector3i &p_pos, int p_lod);
	bool has_block(const Vector3i &p_pos, int p_lod) const;
	void delete_block(const Vector3i &p_pos, int p_lod);
	void flush();
};
```

---

## 6. VoxelStreamRegionFiles mmap Persistence Subsystem Design (`voxel_stream_region_files.h/cpp`)

### 6.1 Overview & Region File Layout
- Region file stores $32 \times 32 \times 32$ chunks ($32,768$ chunks per region file).
- Header block contains $32,768$ entries of 16-byte metadata:
  ```cpp
  struct ChunkRegionHeader {
      uint32_t sector_offset; // 4 KB sector offset
      uint32_t sector_count;  // 4 KB sector count
      uint32_t timestamp;     // Last edit timestamp
      uint32_t flags;         // Compression / status flags
  };
  ```
- Chunk Offset Mathematics:
  $$\text{chunk\_index} = lx + ly \times 32 + lz \times 1024$$
  $$\text{header\_offset} = \text{chunk\_index} \times 16$$

### 6.2 Header Specification (`voxel_stream_region_files.h`)
```cpp
/**************************************************************************/
/*  voxel_stream_region_files.h                                           */
/**************************************************************************/

#pragma once

#include "core/io/file_access.h"
#include "core/object/ref_counted.h"
#include "core/string/ustring.h"

class VoxelStreamRegionFiles : public RefCounted {
	GDCLASS(VoxelStreamRegionFiles, RefCounted);

private:
	String region_dir;
	bool active = false;

protected:
	static void _bind_methods();

public:
	VoxelStreamRegionFiles();
	~VoxelStreamRegionFiles();

	Error open_region_dir(const String &p_dir_path);
	void close();
	bool is_active() const;

	static Vector3i get_region_coords(const Vector3i &p_chunk_pos);
	static int get_chunk_header_offset(const Vector3i &p_chunk_pos);

	PackedByteArray read_chunk_bytes(const Vector3i &p_chunk_pos);
	bool write_chunk_bytes(const Vector3i &p_chunk_pos, const PackedByteArray &p_bytes);
	void flush_region_files();
};
```

---

## 7. SCons SCsub Updates & ClassDB Registration Plan

### 7.1 SCsub Modification
`modules/godot_eden/SCsub` must collect source files from root, `nodes/`, `storage/`, `streaming/`, `generators/`, and include dependency headers:

```python
#!/usr/bin/env python
from misc.utility.scons_hints import *
import os

Import("env")
Import("env_modules")

env_godot_eden = env_modules.Clone()

# Include paths
env_godot_eden.Prepend(CPPPATH=["#modules/godot_eden"])

# GLSL Compute Shader Header Builders
if os.path.exists(env_godot_eden.File("shaders/micro_voxel_raymarch.glsl").rfile().abspath):
    env_godot_eden.RD_GLSL("shaders/micro_voxel_raymarch.glsl")

if os.path.exists(env_godot_eden.File("shaders/clipmap_lod.glsl").rfile().abspath):
    env_godot_eden.RD_GLSL("shaders/clipmap_lod.glsl")

env_godot_eden.Depends(Glob("shaders/*.glsl.gen.h"), ["#glsl_builders.py"])

sources = []
env_godot_eden.add_source_files(sources, "*.cpp")
env_godot_eden.add_source_files(sources, "nodes/*.cpp")
env_godot_eden.add_source_files(sources, "storage/*.cpp")
env_godot_eden.add_source_files(sources, "streaming/*.cpp")
env_godot_eden.add_source_files(sources, "generators/*.cpp")

env.modules_sources += sources
```

### 7.2 ClassDB Registration Map (`register_types.cpp`)
All M2 classes must be bound in `initialize_godot_eden_module(MODULE_INITIALIZATION_LEVEL_SCENE)`:

| Class Name | Inheritance | Category | Purpose |
|------------|-------------|----------|---------|
| `VoxelWorld` | `Node3D` | Scene | Top-level voxel world manager |
| `VoxelVolume` | `Resource` | Resource | Voxel volume metadata resource |
| `VoxelRenderer` | `Node3D` | Scene | Vulkan compute raymarching renderer node |
| `VoxelGenerator` | `Resource` | Resource | Abstract base voxel generator |
| `VoxelGeneratorNoise` | `VoxelGenerator` | Resource | Spherical SDF noise generator (FastNoise2) |
| `VoxelStreamer` | `RefCounted` | Core | Multithreaded chunk streaming manager |
| `VoxelBuffer` | `RefCounted` | Core | $16^3$ palette-compacted voxel block |
| `VoxelDataMap` | `RefCounted` | Core | Spatial hash map for active near-LOD clipmap chunks |
| `LodOctree` | `RefCounted` | Core | Pointerless SVO DAG node pool |
| `SpatialLock3D` | `RefCounted` | Core | 3D spatial neighborhood reader-writer lock |
| `VoxelBlockSerializer` | `RefCounted` | Core | Zstd block compressor & serializer |
| `VoxelStreamSQLite` | `RefCounted` | Core | SQLite edit delta database persistence |
| `VoxelStreamRegionFiles` | `RefCounted` | Core | `mmap` region file persistence |

---

## 8. C++ Doctest Unit Test Suite Design (`modules/godot_eden/tests/`)

### 8.1 Test Module Hierarchy
```
modules/godot_eden/tests/
├── test_main.h          # Test suite entry point & ClassDB registration tests
├── test_storage.h       # VoxelBuffer palette compaction & VoxelDataMap unit tests
├── test_svo_dag.h       # LodOctree SVO DAG node indexing & deduplication tests
├── test_spatial_lock.h  # SpatialLock3D reader/writer lock concurrency tests
├── test_noise.h         # VoxelGeneratorNoise spherical SDF & seed determinism tests
└── test_serialization.h # VoxelBlockSerializer Zstd, SQLite schema, mmap offset tests
```

### 8.2 Test Suite Specifications

#### `test_storage.h`
- `TEST_CASE("[Modules][GodotEden] VoxelBuffer Palette Compaction")`
  - Verifies default 0.0 SDF initialization.
  - Verifies 16-entry palette compaction reduces memory footprint by $>74\%$.
  - Verifies precision of `get_voxel_f` / `set_voxel_f`.
- `TEST_CASE("[Modules][GodotEden] VoxelDataMap Spatial Hash Grid")`
  - Verifies chunk creation, lookup, active chunk counting, and chunk removal.

#### `test_svo_dag.h`
- `TEST_CASE("[Modules][GodotEden] LodOctree SVO DAG Node Indexing & Deduplication")`
  - Verifies root node index at 0.
  - Verifies duplicate node insertions return identical DAG node indices.
  - Verifies compression ratio calculation on uniform volumes ($>90\%$).

#### `test_spatial_lock.h`
- `TEST_CASE("[Modules][GodotEden] SpatialLock3D Neighborhood Concurrency")`
  - Verifies multiple non-overlapping reader locks succeed (`lock_read`).
  - Verifies write lock exclusivity against active reader locks (`lock_write`).
  - Verifies unlock restores block availability for subsequent read/write locks.

#### `test_noise.h`
- `TEST_CASE("[Modules][GodotEden] VoxelGeneratorNoise Spherical SDF & Determinism")`
  - Verifies spherical SDF domain warping formula.
  - Verifies seed determinism across identical generator instances.
  - Verifies SDF sign conventions (negative interior, positive exterior space).

#### `test_serialization.h`
- `TEST_CASE("[Modules][GodotEden] VoxelBlockSerializer & Persistence Engine")`
  - Verifies Zstd serialize and deserialize roundtrip.
  - Verifies magic header bytes `"EDEN"` (0x4E454445).
  - Verifies SQLite delta database schema creation and block delta roundtrip.
  - Verifies `VoxelStreamRegionFiles` chunk header offset calculations:
    $$\text{chunk\_index} = 4 + 8 \times 32 + 12 \times 1024 = 12556; \quad \text{offset} = 12556 \times 16 = 200896$$

---

## 9. Cross-Feature Integration & Verification Matrix

| Feature | Design Specification | Verification Method | Pass Criteria |
|---------|-----------------------|---------------------|---------------|
| **F4: Dual-Tier Storage** | `VoxelBuffer`, `VoxelDataMap` | `test_storage.h` & E2E T1_F4 | Palette compaction memory reduction $>74\%$ |
| **F5: Pointerless SVO DAG** | `LodOctree`, `SvoNode` | `test_svo_dag.h` & E2E T1_F5 | Deduplication hash map returns identical index |
| **F6: Thread-Safe Streaming** | `SpatialLock3D`, `VoxelStreamer` | `test_spatial_lock.h` & E2E T1_F6 | Reader non-exclusivity & writer exclusivity verified |
| **F7: Procedural Noise** | `VoxelGeneratorNoise` | `test_noise.h` & E2E T1_F7 | Spherical SDF warping & seed determinism pass |
| **F8: Persistence & Serializer**| `VoxelBlockSerializer`, `VoxelStreamSQLite`, `VoxelStreamRegionFiles` | `test_serialization.h` & E2E T1_F8 | Zstd roundtrip bit-exact, magic header "EDEN", mmap offset math verified |
| **F13: Doctest Unit Suite** | `tests/*.h` co-located headers | `godot --test --test-suite=godot_eden` & E2E T1_F13 | All C++ Doctest test cases execute and pass |

---
