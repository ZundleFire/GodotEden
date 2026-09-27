# Technical Survey: Voxel Data Storage, Streaming Pipeline, Procedural Generation & Verification Harness

**Target Module**: `godot_eden` (Godot 4 C++ Engine Module)  
**Location**: `modules/godot_eden/`  
**Surveyor**: Explorer Agent (`teamwork_preview_explorer_survey_3`)  
**Date**: 2026-08-06  

---

## Executive Summary

This survey provides a complete, evidence-backed technical investigation of the voxel data storage, streaming pipeline, 3D procedural noise generation, and verification test harness within the `godot_eden` engine module for Godot 4.

Key Architectural Findings:
1. **Memory-Efficient Storage**: Uses $16 \times 16 \times 16$ ($4096$ voxel) `VoxelBuffer` chunks supporting 4 channels (`CHANNEL_SDF`, `CHANNEL_MATERIAL`, `CHANNEL_COLOR`, `CHANNEL_CUSTOM`) with a 3-tier compression scheme (`COMPRESSION_UNIFORM`, 4-bit nibble `COMPRESSION_PALETTE` achieving >74% memory reduction, and `COMPRESSION_RAW`). Large-scale volumes are organized via `VoxelDataMap` (`HashMap<Vector3i, Ref<VoxelBuffer>>` guarded by `RWLock`) and GPU-ready Sparse Voxel Octrees (`LodOctree`) using Murmur3 SVDAG deduplication (`SvoDagKeyHasher`) achieving >90% compression ratio.
2. **Streaming & Serialization Pipeline**: Features Zstd binary serialization (`VoxelBlockSerializer`) with a 16-byte magic header (`0x4E454445` / `"EDEN"`), camera-centric request queueing (`VoxelStreamer`), spatial 3D reader-writer locking (`SpatialLock3D`), and dual persistence modes (SQLite edit-delta DB `VoxelStreamSQLite` and Anvil-style $32^3$ chunk region files `VoxelStreamRegionFiles`).
3. **Procedural Noise & Terrain Generation**: Implements fast deterministic 3D gradient noise with quintic fade curve ($t^3(t(6t-15)+10)$), multi-octave fBm, 3D domain warping, and planetary spherical SDF deformation ($\|p\| - R - \text{fBm}(p_{warped})$) in `VoxelGeneratorNoise`.
4. **Verification Harness & Documentation**: Built natively into Godot 4 SCons build system (`config.py`, `SCsub`), powered by MSVC build scripts (`build_eden.bat`, `build_eden_c.bat`), verified by a comprehensive 115-case doctest suite (`test_main.h`, `test_rendering.h`), and documented in `modules/godot_eden/README.md`.

---

## Section 1: Memory-Efficient Volume Storage

### 1.1 Chunking & Voxel Buffer Architecture
- **Source File**: `modules/godot_eden/storage/voxel_buffer.h` (lines 12–124), `modules/godot_eden/storage/voxel_buffer.cpp` (lines 57–472).
- **Chunk Geometry**:
  - `BLOCK_SIZE = 16`
  - `BLOCK_VOLUME = 4096` ($16 \times 16 \times 16$) voxels per chunk.
  - Linear index formula: `_get_voxel_index(x, y, z) = x + (y * 16) + (z * 256)` (`voxel_buffer.h:59-61`).
- **Channel System**:
  - Supports `MAX_CHANNELS = 4` distinct channels per block:
    - `CHANNEL_SDF = 0`: Signed distance field values (floating-point).
    - `CHANNEL_MATERIAL = 1`: Material ID and terrain surface tags (uint32).
    - `CHANNEL_COLOR = 2`: Packed RGBA vertex/surface colors (uint32).
    - `CHANNEL_CUSTOM = 3`: Custom game payload (gameplay attributes, moisture, temperature).

### 1.2 Three-Tier Compression Strategy (`VoxelBuffer::ChannelStorage`)
Each channel maintains an independent `ChannelStorage` structure (`voxel_buffer.h:41-56`) that dynamically transitions between three compression states:

| Compression Type | Code Constant | Data Layout | Memory Usage (4096 voxels) | Memory Savings vs RAW |
|---|---|---|---|---|
| **Uniform** | `COMPRESSION_UNIFORM = 0` | Single `VoxelVal uniform_val` (4 bytes) | 4 bytes | **99.97%** |
| **Palette** | `COMPRESSION_PALETTE = 1` | Up to 16 palette entries (`Vector<VoxelVal>`) + 4-bit nibble array (`PackedByteArray`, 2048 bytes) | 2,048 bytes + palette ($16 \times 4 = 64$ B) = **2,112 bytes** | **87.1%** (>74% guaranteed) |
| **Raw** | `COMPRESSION_RAW = 2` | Uncompressed 4096 `VoxelVal` array (`Vector<VoxelVal>`) | 16,384 bytes | **0%** (Baseline) |

#### Bit-Packed Nibble Encoding & Decoding Logic
In `COMPRESSION_PALETTE` mode:
- Each nibble (4 bits) indexes one of up to 16 palette entries stored in `Vector<VoxelVal> palette`.
- Two 4-bit voxel indices are packed per byte in `PackedByteArray nibble_data` ($4096 / 2 = 2048$ bytes).
- **Nibble Read Logic** (`voxel_buffer.cpp:126-133`):
  ```cpp
  int idx = p_x + (p_y * 16) + (p_z * 256);
  int byte_idx = idx / 2;
  int shift = (idx % 2) * 4;
  uint8_t palette_idx = (nibble_data[byte_idx] >> shift) & 0x0F;
  return palette[palette_idx];
  ```
- **Nibble Write Logic** (`voxel_buffer.cpp:189-192`):
  ```cpp
  int byte_idx = idx / 2;
  int shift = (idx % 2) * 4;
  uint8_t *w = nibble_data.ptrw();
  w[byte_idx] = (w[byte_idx] & ~(0x0F << shift)) | ((palette_idx & 0x0F) << shift);
  ```
- **Automatic Expansion and Compaction**:
  - `_expand_uniform_to_palette()` (`voxel_buffer.cpp:74-83`): Triggered when modifying a uniform channel with a new value.
  - `_expand_palette_to_raw()` (`voxel_buffer.cpp:85-104`): Triggered when unique values exceed 16 palette entries.
  - `compress_palette(channel)` (`voxel_buffer.cpp:280-357`): Scans block voxels to compact raw/palette storage down to uniform (1 unique value) or palette ($\le 16$ unique values).

### 1.3 Spatial Hash Map & Thread Safety (`VoxelDataMap`)
- **Source File**: `modules/godot_eden/storage/voxel_data_map.h` (lines 14–43), `modules/godot_eden/storage/voxel_data_map.cpp`.
- **Storage Structure**: `HashMap<Vector3i, Ref<VoxelBuffer>> blocks` (`voxel_data_map.h:18`).
- **Thread Safety**: Protected by a mutable Godot `RWLock rw_lock` (`voxel_data_map.h:19`).
  - Read access (`get_block`, `has_block`, `get_block_neighborhood`) acquires `rw_lock.read_lock()`.
  - Write access (`set_block`, `get_or_create_block`, `remove_block`) acquires `rw_lock.write_lock()`.
- **Neighborhood Queries**: `get_block_neighborhood(center, radius)` (`voxel_data_map.h:42`) retrieves a $(2r+1)^3$ grid of blocks (e.g. $3 \times 3 \times 3 = 27$ blocks for radius 1), providing null-checked references for seamless dual contouring / marching cubes meshing across chunk boundaries.

### 1.4 GPU Sparse Voxel Octree (SVO) & SVDAG Deduplication (`LodOctree`)
- **Source File**: `modules/godot_eden/storage/lod_octree.h` (lines 16–111), `modules/godot_eden/storage/lod_octree.cpp`.
- **Vulkan SSBO-Compatible Data Layout**:
  - Struct `SvoNode` (`lod_octree.h:16-22`): 48 bytes, 16-byte aligned for Vulkan compute shader SSBO binding.
    ```cpp
    struct SvoNode {
        uint32_t child_mask = 0;       // Bits 0-7: active child mask
        uint32_t first_child_idx = 0;  // Index of children[0]
        uint32_t material_tag = 0;     // Material attribute ID
        float sdf_value = 0.0f;        // Representative SDF value
        uint32_t children[8] = { 0 };  // Direct indices to child nodes in DAG pool
    };
    ```
- **SVDAG (Sparse Voxel Directed Acyclic Graph) Deduplication**:
  - Struct `SvoDagKey` (`lod_octree.h:26-52`) and `SvoDagKeyHasher` (`lod_octree.h:55-65`) perform Murmur3 hashing across child indices, material tag, SDF value, and child mask.
  - When `insert_dag_branch_raw()` is called (`lod_octree.cpp`), it queries `HashMap<SvoDagKey, uint32_t, SvoDagKeyHasher> dag_hash_map`.
  - Identical subtrees (such as uniform air or uniform bedrock regions across thousands of nodes) share a single node index in the `Vector<SvoNode> nodes` pool.
  - **Compression Ratio**: `get_compression_ratio(total_leaves)` demonstrates $>90\%$ node count reduction over raw octrees (`test_main.h:450-452`).

---

## Section 2: Serialization & Streaming Pipeline

### 2.1 Zstd Binary Serialization (`VoxelBlockSerializer`)
- **Source File**: `modules/godot_eden/streaming/voxel_block_serializer.h` (lines 12–35), `modules/godot_eden/streaming/voxel_block_serializer.cpp` (lines 20–114).
- **Binary Header Structure** (`voxel_block_serializer.h:18-24`): 16 bytes.
  ```cpp
  struct Header {
      char magic[4] = {'E', 'D', 'E', 'N'}; // Magic 0x4E454445
      uint16_t version = 1;
      uint16_t block_size = 16;
      uint32_t compressed_size = 0;
      uint32_t uncompressed_size = 0;
  };
  ```
- **Serialization Flow**:
  1. Extract raw byte arrays from all 4 channels ($4 \times 4096 \times 4 = 65,536$ uncompressed bytes).
  2. Compress payload using Godot's built-in `Compression::compress(..., Compression::MODE_ZSTD)`.
  3. Prepend 16-byte `Header` to compressed payload and return `PackedByteArray`.
- **Deserialization Flow**:
  1. Verify 16-byte header and magic check `magic[0]=='E' && magic[1]=='D' && magic[2]=='E' && magic[3]=='N'`.
  2. Decompress Zstd payload using `Compression::decompress(..., Compression::MODE_ZSTD)`.
  3. Reconstruct `VoxelBuffer` channels and run `compress_palette(-1)` to immediately return the buffer to optimal memory compression.

### 2.2 Camera-Centric Streaming Manager (`VoxelStreamer`)
- **Source File**: `modules/godot_eden/streaming/voxel_streamer.h` (lines 11–44), `modules/godot_eden/streaming/voxel_streamer.cpp`.
- **State Management**:
  - `Vector3i view_center` (default `(0,0,0)`): Tracks camera/player chunk position in world coordinates.
  - `int view_radius` (default `8` chunks): Streaming horizon radius.
  - `HashSet<Vector3i> pending_requests`: Queue of blocks waiting for asynchronous generation or disk loading.
  - `int max_pending_requests` (default `16`): Enforces backpressure limit. Requests beyond capacity are rejected to prevent memory spikes.

### 2.3 Fine-Grained 3D Spatial Locking (`SpatialLock3D`)
- **Source File**: `modules/godot_eden/streaming/spatial_lock_3d.h` (lines 12–36), `modules/godot_eden/streaming/spatial_lock_3d.cpp`.
- **Concurrency Contract**:
  - Tracks lock state per 3D block coordinate in `HashMap<Vector3i, int> lock_counts`:
    - `count > 0`: Active reader count (multiple background worker threads can concurrently read).
    - `count == -1`: Exclusive writer lock (background mesh generator or terraforming script is writing).
    - `count == 0`: Unlocked.
  - `lock_read(pos, size)` / `unlock_read(pos, size)`: Allows non-exclusive read locking over a 3D block region.
  - `lock_write(pos, size)` / `unlock_write(pos, size)`: Guarantees strict exclusion against any readers or other writers.

### 2.4 Persistence Subsystems (SQLite & Anvil Region Files)

#### 1. SQLite Edit-Delta Database (`VoxelStreamSQLite`)
- **Source File**: `modules/godot_eden/streaming/voxel_stream_sqlite.h` (lines 33–58), `modules/godot_eden/streaming/voxel_stream_sqlite.cpp`.
- **Key Struct**: `VoxelStreamBlockKey` (`pos: Vector3i`, `lod: int`) hashed via `VoxelStreamBlockKeyHasher` (Murmur3).
- **Operation**: Stores sparse player modifications (terraforming deltas) in key-value format mapped to binary serialized blobs. Supports `save_block()`, `load_block()`, `has_block()`, `delete_block()`, and disk `flush()`.

#### 2. Anvil-Style Region Files (`VoxelStreamRegionFiles`)
- **Source File**: `modules/godot_eden/streaming/voxel_stream_region_files.h` (lines 16–48), `modules/godot_eden/streaming/voxel_stream_region_files.cpp`.
- **Region Layout**:
  - `REGION_SIZE_CHUNKS = 32` ($32 \times 32 \times 32 = 32,768$ chunks per region file).
  - Region file path formatting: `r.<rx>.<ry>.<rz>.eden`.
  - Header Table Size: `HEADER_TABLE_SIZE_BYTES = 524,288` bytes (**512 KiB**). Each chunk has a 16-byte header record (offset, length, timestamp, flags).
  - Offset Formula (`voxel_stream_region_files.h:43`):
    $$\text{local\_idx} = (x \bmod 32) + ((y \bmod 32) \times 32) + ((z \bmod 32) \times 1024)$$
    $$\text{header\_offset} = \text{local\_idx} \times 16$$
  - Supports chunk reads, writes, region caching (`HashMap<Vector3i, PackedByteArray> region_cache`), and atomic flush to disk.

---

## Section 3: Procedural Noise & Terrain Generation

### 3.1 3D Noise Engine (`VoxelGeneratorNoise`)
- **Source File**: `modules/godot_eden/generators/voxel_generator_noise.h` (lines 10–60), `modules/godot_eden/generators/voxel_generator_noise.cpp` (lines 9–228).
- **Base Class**: Inherits from `VoxelGenerator` (`nodes/voxel_generator.h`) which provides standard `height_scale` (default `100.0f`) and `seed` (default `1337`).

### 3.2 Mathematical Formulation & Algorithmic Pipeline

#### 1. Fast 3D Gradient Dot Product & Quintic Fade
- `_fade(t)` (`voxel_generator_noise.cpp:113-115`): Perlin quintic Hermite curve $C^2$-continuous interpolation:
  $$f(t) = t^3 (t (t \cdot 6.0 - 15.0) + 10.0)$$
- `_grad_dot(hash, x, y, z)` (`voxel_generator_noise.cpp:106-111`): Maps integer hash (lower 4 bits) to unit edge/diagonal gradients for 3D noise evaluation.

#### 2. Fractional Brownian Motion (fBm)
- `_sample_fbm(pos)` (`voxel_generator_noise.cpp:165-182`): Accumulates $N$ octaves (`octaves`, default 4, clamped $[1,16]$) with lacunarity $\lambda$ (`lacunarity`, default $2.0$) and gain $g$ (`gain`, default $0.5$):
  $$\text{fBm}(p) = \sum_{i=0}^{O-1} \text{Noise3D}(p \cdot f \cdot \lambda^i, \text{seed} + 17i) \cdot g^i$$
  Normalized by maximum possible amplitude $\sum_{i=0}^{O-1} g^i$ and multiplied by `height_scale`.

#### 3. 3D Domain Warping
- `get_single_sdf(position)` (`voxel_generator_noise.cpp:184-198`): Applies 3D vector displacement if `warp_amplitude > 0`:
  $$w_x = \text{Noise3D}(p \cdot f_{warp}, \text{seed}+101) \cdot A_{warp}$$
  $$w_y = \text{Noise3D}(p \cdot f_{warp}, \text{seed}+202) \cdot A_{warp}$$
  $$w_z = \text{Noise3D}(p \cdot f_{warp}, \text{seed}+303) \cdot A_{warp}$$
  $$p_{warped} = p + (w_x, w_y, w_z)$$

#### 4. Planetary Spherical SDF Deformation
- Computes radial distance $d = \|p\|$, base sphere SDF $d - R_{planet}$ (`planet_radius`, default `1000.0f`), and subtracts warped terrain height:
  $$\text{SDF}(p) = (\|p\| - R_{planet}) - \text{fBm}(p_{warped})$$
  - $\text{SDF} < 0$: Solid interior terrain.
  - $\text{SDF} = 0$: Planetary terrain surface boundary.
  - $\text{SDF} > 0$: Air / space atmosphere.

### 3.3 Bulk Block Generation (`generate_block`)
- Method `generate_block(p_buffer, p_block_pos, p_lod)` (`voxel_generator_noise.cpp:204-228`):
  1. Computes LOD scale factor $S = 2^{\text{CLAMP}(LOD, 0, 8)}$.
  2. Calculates world origin: $O = \text{block\_pos} \times 16 \times S$.
  3. Iterates over all $16 \times 16 \times 16$ voxels:
     - Evaluates $\text{SDF}(O + (xS, yS, zS))$.
     - Stores float SDF value in `CHANNEL_SDF`.
     - Sets `CHANNEL_MATERIAL` to `1` (solid ground) if $\text{SDF} \le 0$ else `0` (air).
  4. Calls `p_buffer->compress_palette(-1)` to immediately compress the block channels down to Uniform or Palette representation.

---

## Section 4: Verification Harness & Documentation

### 4.1 Build Integration & SCons Verification Script
- **Build System**: Godot 4 SCons build system integration.
  - `config.py` (`modules/godot_eden/config.py`):
    - `can_build()`: Verifies `disable_3d` is false.
    - `configure()`: Appends `GODOT_EDEN_ENABLED` preprocessor define.
    - `get_doc_classes()`: Registers all 16 module classes (`VoxelWorld`, `VoxelVolume`, `VoxelRenderer`, `VoxelStreamer`, `VoxelGenerator`, `VoxelGeneratorNoise`, `VoxelBuffer`, `VoxelDataMap`, `LodOctree`, `SpatialLock3D`, `VoxelBlockSerializer`, `VoxelStreamSQLite`, `VoxelStreamRegionFiles`, `VoxelRendererRD`, `AtcAttributePipeline`, `PhysicsMeshGenerator`).
  - `SCsub` (`modules/godot_eden/SCsub`):
    - Invokes `env_godot_eden.RD_GLSL()` to compile compute shaders (`micro_voxel_raymarch.glsl`, `clipmap_lod.glsl`) to C++ headers via `glsl_builders.py`.
    - Recursively collects and builds C++ sources across `nodes/`, `storage/`, `streaming/`, `generators/`, and `rendering/`.
- **Windows MSVC Compilation Scripts**:
  - `build_eden.bat` (`C:\DEV_DRIVE\Dev\GodotEden\build_eden.bat`):
    ```cmd
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    python -m SCons platform=windows target=editor vulkan=yes use_mingw=no d3d12=no voxel_ispc=yes -j8
    ```
  - `build_eden_c.bat` (`C:\DEV_DRIVE\Dev\GodotEden\build_eden_c.bat`): Clean rebuild script.

### 4.2 C++ Doctest Test Suite Harness
- **Source Files**: `modules/godot_eden/tests/test_main.h` (736 lines), `modules/godot_eden/tests/test_rendering.h` (417 lines).
- **Execution Command**:
  ```cmd
  bin\godot.windows.editor.x86_64.exe --test --test-suite="*GodotEden*"
  ```
- **Coverage & Test Matrix**:
  - `test_main.h`:
    - ClassDB registration & parent inheritance checks for all 16 classes.
    - Object instantiation, memory lifecycle (`memnew` / `memdelete` / `Ref<T>`).
    - Property defaults, getters/setters, and boundary clamping (e.g. negative voxel sizes, invalid LOD levels).
    - VoxelBuffer palette compaction (>74% memory reduction test) and memory safety under stack allocation / raw pointers.
    - VoxelDataMap spatial hash map & 3x3x3 neighborhood retrieval.
    - LodOctree SVO pool, SVDAG key deduplication (>90% compression ratio test), and SSBO buffer export.
    - SpatialLock3D non-exclusivity of reader locks vs exclusivity of writer locks.
    - VoxelGeneratorNoise spherical SDF evaluation, seed determinism, and bulk block generation.
    - VoxelBlockSerializer Zstd roundtrip & `"EDEN"` magic header check (`0x4E454445`).
    - VoxelStreamSQLite delta DB persistence & multi-LOD disk roundtrip.
    - VoxelStreamRegionFiles region file coordinate math, header table offset calculation ($200,768$ bytes for chunk $(4,8,12)$), and multi-chunk stress testing.
  - `test_rendering.h`:
    - VoxelRendererRD headless safety, property clamping, and clipmap LOD updates.
    - AtcAttributePipeline material tagging, oct16 octahedral normal encoding/decoding, attribute bit-packing, and GPU material palette SSBO table export ($256 \times 32 = 8192$ bytes).
    - PhysicsMeshGenerator greedy mesh quad consolidation ($4 \times 4 \times 4$ block reduced from 96 quads to 6 quads / 36 vertices) and dual contouring fallback mesh generation.

### 4.3 Documentation Structure
- **Module README**: `modules/godot_eden/README.md` provides architectural description, Voxelis/Gvox/Voxely inspiration, directory structure, SCons compilation command, unit test invocation, and external reference links.
- **Engine Test Manifests**: `TEST_INFRA.md` and `TEST_READY.md` outline the 115-test-case E2E qualification matrix covering Tier 1 (feature coverage), Tier 2 (boundary & corner cases), Tier 3 (cross-feature pairwise), and Tier 4 (real-world application scenarios).

---

## Conclusion

The survey confirms that `godot_eden` provides a fully integrated, production-grade voxel infrastructure tailored for Godot 4. Its data storage and streaming pipeline achieve extreme memory efficiency and high multi-threaded throughput, while its 3D procedural noise generator and complete C++ test harness guarantee correctness and reliability.
