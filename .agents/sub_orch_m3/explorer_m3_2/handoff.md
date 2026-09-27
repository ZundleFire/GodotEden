# Handoff Report: Streaming & Persistence Subsystem Analysis (M3-2)

## 1. Observation

### 1.1 `spatial_lock_3d.h` and `spatial_lock_3d.cpp`
- **File Location**: `modules/godot_eden/streaming/spatial_lock_3d.h` (37 lines) & `spatial_lock_3d.cpp` (142 lines).
- **Observed Behavior**:
  - Implements fine-grained 3D spatial region reader-writer locking. `Mutex mutex` protects `HashMap<Vector3i, int> lock_counts`.
  - In `lock_read` (lines 31–40) and `lock_write` (lines 85–94), a two-pass algorithm is used: pass 1 checks whether any block in the `(p_size.x, p_size.y, p_size.z)` bounding box is locked (`lock_counts[pos] < 0` for readers, `lock_counts[pos] != 0` for writers); pass 2 sets/increments lock state (`+1` for readers, `-1` for writers).
  - In `unlock_read` (lines 67–74) and `unlock_write` (lines 117–121), the locks are released for the specified bounding box.
- **Observed Flaws**:
  - **No Ownership / Size Mismatch Tracking**: `unlock_read` and `unlock_write` rely entirely on the caller providing the exact same `p_block_pos` and `p_size` as the corresponding `lock_*` call. If a writer locks a $2 \times 2 \times 2$ region at `(0,0,0)` (setting 8 map entries to `-1`), but unlocks with `p_size = (1,1,1)`, 7 cells remain permanently locked as `-1` (dangling locks).
  - **Non-blocking Poll Model Only**: `lock_read` and `lock_write` return `false` immediately if a lock cannot be acquired. There is no blocking condition variable or wait queue mechanism for worker threads.

### 1.2 `voxel_streamer.h` and `voxel_streamer.cpp`
- **File Location**: `modules/godot_eden/streaming/voxel_streamer.h` (45 lines) & `voxel_streamer.cpp` (88 lines).
- **Observed Behavior**:
  - Contains properties: `max_pending_requests` (default 16), `view_center` (`Vector3i`), `view_radius` (default 8), `active` (`bool`), and `pending_requests` (`HashSet<Vector3i>`).
  - In `request_block` (lines 71–75):
    ```cpp
    void VoxelStreamer::request_block(const Vector3i &p_block_pos) {
        if ((int)pending_requests.size() < max_pending_requests) {
            pending_requests.insert(p_block_pos);
        }
    }
    ```
- **Observed Flaws**:
  - **Missing Thread Safety**: `HashSet<Vector3i> pending_requests` is accessed without any `Mutex` or atomic synchronization in `request_block`, `cancel_request`, `get_pending_request_count`, and `clear_pending_requests`. Calling these methods across background worker threads or main thread causes data races and memory corruption.
  - **Missing Thread Pool / Worker Loop**: SCOPE.md and task instructions specify a "multi-threaded streamer with worker loop and priority sorting." `VoxelStreamer` currently lacks any thread pool, background thread management (`Thread`), or job execution loop.
  - **Missing Priority Sorting**: `pending_requests` is an unordered `HashSet<Vector3i>`. There is no distance-based priority queue (e.g. sorting request coordinates by distance to `view_center`).

### 1.3 `voxel_block_serializer.h` and `voxel_block_serializer.cpp`
- **File Location**: `modules/godot_eden/streaming/voxel_block_serializer.h` (36 lines) & `voxel_block_serializer.cpp` (115 lines).
- **Observed Behavior**:
  - Header struct layout (16 bytes): `magic[4]` (`"EDEN"`), `version` (1), `block_size` (16), `compressed_size` (`uint32`), `uncompressed_size` (`uint32`).
  - Serializes 4 channels $\times 4096 \times 4\text{ B} = 65,536\text{ B}$ raw payload using `Compression::compress(..., Compression::MODE_ZSTD)`.
  - In `decompress_and_deserialize` (lines 91–96):
    ```cpp
    PackedByteArray uncompressed_payload;
    uncompressed_payload.resize(uncompressed_len);
    int decompressed_len = Compression::decompress(uncompressed_payload.ptrw(), uncompressed_len, p_bytes.ptr() + header_len, compressed_len, Compression::MODE_ZSTD);
    if (decompressed_len <= 0) {
        return buffer;
    }
    ```
- **Observed Flaws**:
  - **Unbounded Memory Allocation Vulnerability**: `header.uncompressed_size` is trusted without sanity validation. A corrupted or malicious packet specifying `uncompressed_size = 2,000,000,000` causes `uncompressed_payload.resize()` to allocate ~2 GB of memory, causing OOM crashes.
  - **Missing Header Field Checks**: `header.version` and `header.block_size` are stored in the header but never validated during decompression.
  - **Incomplete Decompression Check**: If `decompressed_len > 0` but `decompressed_len != uncompressed_len`, deserialization proceeds with partially uninitialized payload memory.

### 1.4 `voxel_stream_sqlite.h` and `voxel_stream_sqlite.cpp`
- **File Location**: `modules/godot_eden/streaming/voxel_stream_sqlite.h` (59 lines) & `voxel_stream_sqlite.cpp` (153 lines).
- **Observed Behavior**:
  - Manages `HashMap<VoxelStreamBlockKey, PackedByteArray, VoxelStreamBlockKeyHasher> edit_deltas`.
  - Saves binary file format with magic header `0x4553514C` (`"ESQL"`).
- **Observed Flaws**:
  - **Discrepancy with Name/Spec**: Named `VoxelStreamSQLite` and specified as "SQLite delta database persistence", but does NOT use SQLite C API (`sqlite3.h`), SQL statements, table schemas, or transactions. It is a custom in-memory hash map dumped to disk.
  - **Missing Thread Safety**: `edit_deltas` and `dirty_flag` are not protected by a `Mutex`. Concurrent reads/writes across worker threads cause hash map corruption.
  - **Non-Atomic File Flush**: `flush()` opens the destination file in `FileAccess::WRITE` mode directly. If the application crashes during `flush()`, the existing file is truncated/destroyed without backup or recovery option.
  - **Unbounded Buffer Allocation on Load**: `open()` reads `len` from file and calls `data.resize(len)` without bounds checks, exposing the loader to memory exhaustion on corrupted files.

### 1.5 `voxel_stream_region_files.h` and `voxel_stream_region_files.cpp`
- **File Location**: `modules/godot_eden/streaming/voxel_stream_region_files.h` (49 lines) & `voxel_stream_region_files.cpp` (200 lines).
- **Observed Behavior**:
  - Manages $32^3$ chunk region files (`reg_X_Y_Z.edr`) with 512 KiB pre-allocated 16-byte header table ($32,768 \text{ chunks} \times 16\text{ B} = 524,288\text{ B}$).
  - `get_region_coords` correctly uses `Math::floor((float)p_chunk_pos.x / 32.0f)` for negative chunk positions.
  - `get_chunk_header_offset` correctly uses positive modulo `((pos % 32) + 32) % 32` to calculate offset in range `[0, 524272]`.
- **Observed Flaws**:
  - **Missing Thread Safety**: `region_cache` (`HashMap`) and `dirty_chunks` (`HashSet`) are un-synchronized and will crash under concurrent multi-threaded access.
  - **Space Leak / File Bloat (No Sector Allocation Table)**:
    - In `flush_region_files()` (lines 177–180):
      ```cpp
      uint32_t target_offset = old_offset;
      if (target_offset == 0 || data.size() > (int)old_length) {
          target_offset = (uint32_t)MAX((uint64_t)f->get_length(), (uint64_t)HEADER_TABLE_SIZE_BYTES);
      }
      ```
      When a chunk payload expands beyond `old_length`, a new sector is appended to the end of the file (`f->get_length()`), while `old_offset` is abandoned. There is no Sector Allocation Table (SAT) or free-list to reclaim or track orphaned file space, leading to unbounded file growth over multiple save edits.
  - **Unchecked Header Table Bounds in Reader**:
    - In `read_chunk_bytes()` (lines 105–108): `sector_offset` is checked against `f->get_length()`, but is NOT checked for `sector_offset >= HEADER_TABLE_SIZE_BYTES`. Corrupted file headers with `0 < sector_offset < 524288` will read header table bytes as chunk payload.

---

## 2. Logic Chain

1. **Spatial Lock 3D**:
   - *Observation*: Two-pass lock acquisition under `MutexLock` guarantees internal hashmap safety, but `lock_counts` entries are stored without lock owners or region bounds.
   - *Reasoning*: If thread A locks region $2 \times 2 \times 2$ at `(0,0,0)` and thread B (or A) attempts to unlock $1 \times 1 \times 1$ at `(0,0,0)`, 7 cells remain locked at `-1`.
   - *Conclusion*: Missing region size/owner validation causes dangling locks.

2. **Voxel Streamer**:
   - *Observation*: `pending_requests` is a plain `HashSet<Vector3i>` without a `Mutex`, thread pool, or priority queue.
   - *Reasoning*: Multi-threaded streaming requires worker threads submitting requests and pulling blocks safely from a priority queue ordered by distance to `view_center`.
   - *Conclusion*: `VoxelStreamer` requires a `Mutex` for thread safety, a priority queue sorted by viewer distance, and worker thread pool integration.

3. **Voxel Block Serializer**:
   - *Observation*: `decompress_and_deserialize` allocates `uncompressed_payload` based on `header.uncompressed_size` without upper bound checks.
   - *Reasoning*: Malformed inputs can trigger OOM allocations. Expected size for 16³ blocks (4 channels) is strictly $65,536$ bytes.
   - *Conclusion*: Enforce `header.uncompressed_size == VoxelBuffer::MAX_CHANNELS * VoxelBuffer::BLOCK_VOLUME * sizeof(uint32_t)` (65,536 bytes) and validate `decompressed_len == uncompressed_len`.

4. **Voxel Stream SQLite**:
   - *Observation*: Uses custom binary file dump (`"ESQL"`) rather than SQLite C API, lacks mutex protection, and uses non-atomic file writes.
   - *Reasoning*: Concurrent edits will race on `edit_deltas`, and power loss during `flush()` destroys the delta file.
   - *Conclusion*: Either integrate standard SQLite C API (`sqlite3.h`) or add a `Mutex` and atomic temporary file swapping (`user://...tmp` -> rename).

5. **Voxel Stream Region Files**:
   - *Observation*: Appends new sector to `f->get_length()` whenever chunk data grows, without tracking freed space.
   - *Reasoning*: Repeated editing of voxels causes region file fragmentation and unbounded storage growth.
   - *Conclusion*: Implement a simple Sector Allocation Table (SAT) / free sector bitmap or defragmentation pass, add a `Mutex` for thread safety, and validate `sector_offset >= HEADER_TABLE_SIZE_BYTES`.

---

## 3. Caveats

- **No Source Code Modifications Made**: Per agent role instructions, this is a read-only investigation. No source code changes were committed to `modules/godot_eden/streaming/`.
- **Doctest Unit Test Suite Coverage**: `test_main.h` includes basic unit tests for all streaming classes (`TEST_CASE("[Modules][GodotEden] SpatialLock3D Neighborhood Concurrency")`, etc.), but does not currently exercise multi-threaded concurrency or file corruption stress cases.

---

## 4. Conclusion

The streaming and persistence pipeline in `modules/godot_eden/streaming/` has a clean modular foundation and passes single-threaded unit tests. However, several critical vulnerabilities and architectural gaps require resolution:
1. **Thread Safety**: `VoxelStreamer`, `VoxelStreamSQLite`, and `VoxelStreamRegionFiles` lack `Mutex` primitives for safe concurrent access across background worker threads.
2. **Persistence Storage Leaks**: `VoxelStreamRegionFiles` lacks a Sector Allocation Table (SAT) or free-list, leading to file bloat on payload updates.
3. **Data Integrity & Vulnerabilities**: Serializer and SQLite/Region loaders lack upper-bound checks on payload length fields, exposing the subsystem to memory allocation panics on corrupted files.
4. **Implementation Gaps**: `VoxelStreamer` lacks a distance-sorted priority queue and worker thread pool; `VoxelStreamSQLite` is a custom binary file rather than SQLite database.

---

## 5. Verification Method

### 5.1 Independent File Inspection
1. `modules/godot_eden/streaming/spatial_lock_3d.cpp`: Check lines 109–123 to verify lack of ownership/region size tracking in `unlock_write`.
2. `modules/godot_eden/streaming/voxel_streamer.h` / `.cpp`: Check absence of `Mutex` and worker thread pool.
3. `modules/godot_eden/streaming/voxel_block_serializer.cpp`: Check lines 85–94 for missing `header.uncompressed_size` max bound check.
4. `modules/godot_eden/streaming/voxel_stream_sqlite.cpp`: Check absence of `sqlite3.h` and direct `FileAccess::WRITE` in `flush()`.
5. `modules/godot_eden/streaming/voxel_stream_region_files.cpp`: Check lines 177–180 for unbounded append behavior on sector resize.

### 5.2 Test Verification Command
Run the Doctest verification suite:
```powershell
python tests/e2e/runner.py
```
Or execute SCons build script:
```cmd
build_eden_c.bat
```
