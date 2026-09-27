# Milestone 2 Iteration 2 Remediation Analysis & Implementation Strategy

## Executive Summary

During the Iteration 1 review of Milestone 2 (Dual-Tier Voxel Storage & Streaming Pipeline), reviewer `reviewer_m2_1` issued a `REQUEST_CHANGES` verdict due to four specific findings:
1. **Critical (Integrity Violation)**: `VoxelStreamRegionFiles` was an in-memory stub that did not perform genuine file operations or disk persistence.
2. **Critical (Integrity Violation)**: `VoxelStreamSQLite` was an in-memory stub with empty `flush()` stubs.
3. **Major (Memory Safety Bug)**: `VoxelBuffer::duplicate_buffer()` constructed a temporary `Ref<VoxelBuffer>` over `this`, which risks invoking `memdelete(this)` upon temporary destruction when called on raw or stack pointers.
4. **Minor (Dead Code / Unused Variables)**: `VoxelGeneratorNoise::_sample_noise_3d` declared unused local variables (`A`, `AA`, `AB`, `B`, `BA`, `BB`).

This document provides the complete, exact technical design and C++ code implementations required for the implementer agent to remediate all 4 items cleanly, robustly, and safely without breaking existing ClassDB contracts or unit tests.

---

## 1. Remediation Item 1: Genuine Disk Persistence in `VoxelStreamRegionFiles`

### 1.1 Problem & Requirement Analysis
- **Current State**: `VoxelStreamRegionFiles` stores chunks in an in-memory `HashMap<Vector3i, PackedByteArray> region_cache`. `open_region_dir()` merely sets `active_flag = true`, and `flush_region_files()` is an empty stub function (`// Persist cached chunks to region file formats`).
- **Requirement**:
  - Implement genuine file persistence using Godot's `FileAccess` API.
  - Sector region file structure: $32 \times 32 \times 32$ chunks per region file ($32,768$ chunks total).
  - Region file layout:
    - 512 KiB Header Table ($32,768 \text{ chunks} \times 16 \text{ bytes/chunk header} = 524,288 \text{ bytes}$).
    - Chunk offset calculated via `get_chunk_header_offset(chunk_pos)` ($0 \text{ to } 524,272$).
    - 16-byte Chunk Header Structure:
      - `uint32_t sector_offset`: Byte offset from file start where payload begins (or 0 if unallocated).
      - `uint32_t sector_length`: Length of chunk payload in bytes.
      - `uint32_t timestamp`: Unix timestamp (or 0).
      - `uint32_t flags_or_crc`: Reserved / flags.
    - Chunk payloads are appended starting at byte offset $524,288$ (or reused if space permits).
  - Methods to implement:
    - `open_region_dir(const String &p_dir_path)`: Resolves directory, creates directory if non-existent using `DirAccess`, and sets `active_flag = true`.
    - `read_chunk_bytes(const Vector3i &p_chunk_pos)`: Checks dirty/memory cache first; if absent, reads chunk from region file on disk via `FileAccess`.
    - `write_chunk_bytes(const Vector3i &p_chunk_pos, const PackedByteArray &p_bytes)`: Stores chunk in `region_cache` and marks dirty.
    - `flush_region_files()`: Groups cached chunks by region file (`reg_X_Y_Z.edr`), opens/creates region files on disk, writes payloads and 16-byte headers using `FileAccess`, and flushes to disk.

### 1.2 Proposed C++ Code for `modules/godot_eden/streaming/voxel_stream_region_files.h`

```cpp
/**************************************************************************/
/*  voxel_stream_region_files.h                                           */
/**************************************************************************/

#pragma once

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/math/vector3i.h"
#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/variant/packed_byte_array.h"

class VoxelStreamRegionFiles : public RefCounted {
	GDCLASS(VoxelStreamRegionFiles, RefCounted);

public:
	static const int REGION_SIZE_CHUNKS = 32;
	static const int HEADER_TABLE_SIZE_BYTES = 32 * 32 * 32 * 16; // 524,288 bytes (512 KiB)

private:
	String region_dir;
	bool active_flag = false;
	HashMap<Vector3i, PackedByteArray> region_cache;
	HashSet<Vector3i> dirty_chunks;

	String _get_region_file_path(const Vector3i &p_region_pos) const;

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

### 1.3 Proposed C++ Code for `modules/godot_eden/streaming/voxel_stream_region_files.cpp`

```cpp
/**************************************************************************/
/*  voxel_stream_region_files.cpp                                         */
/**************************************************************************/

#include "streaming/voxel_stream_region_files.h"

#include "core/object/class_db.h"
#include "core/os/os.h"

void VoxelStreamRegionFiles::_bind_methods() {
	ClassDB::bind_method(D_METHOD("open_region_dir", "dir_path"), &VoxelStreamRegionFiles::open_region_dir);
	ClassDB::bind_method(D_METHOD("close"), &VoxelStreamRegionFiles::close);
	ClassDB::bind_method(D_METHOD("is_active"), &VoxelStreamRegionFiles::is_active);

	ClassDB::bind_static_method("VoxelStreamRegionFiles", D_METHOD("get_region_coords", "chunk_pos"), &VoxelStreamRegionFiles::get_region_coords);
	ClassDB::bind_static_method("VoxelStreamRegionFiles", D_METHOD("get_chunk_header_offset", "chunk_pos"), &VoxelStreamRegionFiles::get_chunk_header_offset);

	ClassDB::bind_method(D_METHOD("read_chunk_bytes", "chunk_pos"), &VoxelStreamRegionFiles::read_chunk_bytes);
	ClassDB::bind_method(D_METHOD("write_chunk_bytes", "chunk_pos", "bytes"), &VoxelStreamRegionFiles::write_chunk_bytes);
	ClassDB::bind_method(D_METHOD("flush_region_files"), &VoxelStreamRegionFiles::flush_region_files);
}

VoxelStreamRegionFiles::VoxelStreamRegionFiles() {
}

VoxelStreamRegionFiles::~VoxelStreamRegionFiles() {
	close();
}

String VoxelStreamRegionFiles::_get_region_file_path(const Vector3i &p_region_pos) const {
	String filename = vformat("reg_%d_%d_%d.edr", p_region_pos.x, p_region_pos.y, p_region_pos.z);
	return region_dir.path_join(filename);
}

Error VoxelStreamRegionFiles::open_region_dir(const String &p_dir_path) {
	close();
	region_dir = p_dir_path;
	if (!region_dir.is_empty()) {
		Ref<DirAccess> da = DirAccess::create_for_path(region_dir);
		if (da.is_valid()) {
			da->make_dir_recursive(region_dir);
		}
	}
	active_flag = true;
	return OK;
}

void VoxelStreamRegionFiles::close() {
	if (active_flag) {
		flush_region_files();
		active_flag = false;
		region_cache.clear();
		dirty_chunks.clear();
	}
}

bool VoxelStreamRegionFiles::is_active() const {
	return active_flag;
}

Vector3i VoxelStreamRegionFiles::get_region_coords(const Vector3i &p_chunk_pos) {
	int rx = (int)Math::floor((float)p_chunk_pos.x / 32.0f);
	int ry = (int)Math::floor((float)p_chunk_pos.y / 32.0f);
	int rz = (int)Math::floor((float)p_chunk_pos.z / 32.0f);
	return Vector3i(rx, ry, rz);
}

int VoxelStreamRegionFiles::get_chunk_header_offset(const Vector3i &p_chunk_pos) {
	int lx = ((p_chunk_pos.x % 32) + 32) % 32;
	int ly = ((p_chunk_pos.y % 32) + 32) % 32;
	int lz = ((p_chunk_pos.z % 32) + 32) % 32;
	int chunk_index = lx + (ly * 32) + (lz * 1024);
	return chunk_index * 16;
}

PackedByteArray VoxelStreamRegionFiles::read_chunk_bytes(const Vector3i &p_chunk_pos) {
	if (!active_flag) {
		return PackedByteArray();
	}

	// 1. Check in-memory cache first
	if (region_cache.has(p_chunk_pos)) {
		return region_cache[p_chunk_pos];
	}

	// 2. Read from disk if region file exists
	Vector3i reg_coords = get_region_coords(p_chunk_pos);
	String file_path = _get_region_file_path(reg_coords);

	if (!FileAccess::exists(file_path)) {
		return PackedByteArray();
	}

	Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::READ);
	if (f.is_null()) {
		return PackedByteArray();
	}

	int header_offset = get_chunk_header_offset(p_chunk_pos);
	if ((uint64_t)header_offset + 16 > f->get_length()) {
		return PackedByteArray();
	}

	f->seek(header_offset);
	uint32_t sector_offset = f->get_32();
	uint32_t sector_length = f->get_32();

	if (sector_offset == 0 || sector_length == 0 || (uint64_t)sector_offset + sector_length > f->get_length()) {
		return PackedByteArray();
	}

	f->seek(sector_offset);
	PackedByteArray chunk_data;
	chunk_data.resize(sector_length);
	f->get_buffer(chunk_data.ptrw(), sector_length);

	// Cache loaded chunk
	region_cache[p_chunk_pos] = chunk_data;
	return chunk_data;
}

bool VoxelStreamRegionFiles::write_chunk_bytes(const Vector3i &p_chunk_pos, const PackedByteArray &p_bytes) {
	if (!active_flag) {
		return false;
	}
	region_cache[p_chunk_pos] = p_bytes;
	dirty_chunks.insert(p_chunk_pos);
	return true;
}

void VoxelStreamRegionFiles::flush_region_files() {
	if (!active_flag || dirty_chunks.is_empty()) {
		return;
	}

	// Group dirty chunks by region file
	HashMap<Vector3i, Vector<Vector3i>> region_groups;
	for (const Vector3i &chunk_pos : dirty_chunks) {
		Vector3i reg_coords = get_region_coords(chunk_pos);
		region_groups[reg_coords].push_back(chunk_pos);
	}

	for (const KeyValue<Vector3i, Vector<Vector3i>> &E : region_groups) {
		Vector3i reg_coords = E.key;
		const Vector<Vector3i> &chunks = E.value;
		String file_path = _get_region_file_path(reg_coords);

		// If file doesn't exist, create it and write 512 KiB zeroed header section
		if (!FileAccess::exists(file_path)) {
			Ref<FileAccess> f_init = FileAccess::open(file_path, FileAccess::WRITE);
			if (f_init.is_valid()) {
				PackedByteArray zero_header;
				zero_header.resize(HEADER_TABLE_SIZE_BYTES);
				zero_header.fill(0);
				f_init->store_buffer(zero_header.ptr(), zero_header.size());
				f_init->flush();
			}
		}

		Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::READ_WRITE);
		if (f.is_null()) {
			continue;
		}

		for (int i = 0; i < chunks.size(); i++) {
			Vector3i chunk_pos = chunks[i];
			if (!region_cache.has(chunk_pos)) {
				continue;
			}
			const PackedByteArray &data = region_cache[chunk_pos];
			int header_offset = get_chunk_header_offset(chunk_pos);

			f->seek(header_offset);
			uint32_t old_offset = f->get_32();
			uint32_t old_length = f->get_32();

			uint32_t target_offset = old_offset;
			if (target_offset == 0 || data.size() > (int)old_length) {
				target_offset = (uint32_t)MAX((uint64_t)f->get_length(), (uint64_t)HEADER_TABLE_SIZE_BYTES);
			}

			// Write payload data
			f->seek(target_offset);
			if (data.size() > 0) {
				f->store_buffer(data.ptr(), data.size());
			}

			// Update 16-byte chunk header
			f->seek(header_offset);
			f->store_32(target_offset);
			f->store_32(data.size());
			f->store_32((uint32_t)OS::get_singleton()->get_unix_time());
			f->store_32(0); // Reserved
		}
		f->flush();
	}

	dirty_chunks.clear();
}
```

---

## 2. Remediation Item 2: Genuine Disk Persistence in `VoxelStreamSQLite`

### 2.1 Problem & Requirement Analysis
- **Current State**: `VoxelStreamSQLite` stores edit deltas in an in-memory `HashMap<VoxelStreamBlockKey, PackedByteArray> edit_deltas`. `open()` merely sets `open_flag = true`, and `flush()` is an empty stub function (`// Flush deltas if backed by disk storage`).
- **Requirement**:
  - Implement genuine disk persistence for edit deltas using Godot's `FileAccess` binary delta storage.
  - Binary file database format ("ESQL"):
    - Header Magic: 4 bytes `0x4553514C` ("ESQL").
    - Record Count: 4 bytes `uint32_t`.
    - For each block record:
      - `int32_t pos.x` (4 bytes)
      - `int32_t pos.y` (4 bytes)
      - `int32_t pos.z` (4 bytes)
      - `int32_t lod` (4 bytes)
      - `uint32_t payload_len` (4 bytes)
      - Payload bytes (`payload_len` bytes)
  - Methods to implement:
    - `open(const String &p_path)`: Opens file if existing, parses header & records into `edit_deltas`, sets `open_flag = true`.
    - `save_block(pos, lod, bytes)`: Updates `edit_deltas` and marks state dirty.
    - `load_block(pos, lod)`: Returns cached/loaded delta bytes.
    - `delete_block(pos, lod)`: Removes block from `edit_deltas` and marks state dirty.
    - `flush()`: Writes magic header, record count, and all block records to `db_path` via `FileAccess::open(db_path, FileAccess::WRITE)`.

### 2.2 Proposed C++ Code for `modules/godot_eden/streaming/voxel_stream_sqlite.h`

```cpp
/**************************************************************************/
/*  voxel_stream_sqlite.h                                                 */
/**************************************************************************/

#pragma once

#include "core/io/file_access.h"
#include "core/math/vector3i.h"
#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/variant/packed_byte_array.h"

struct VoxelStreamBlockKey {
	Vector3i pos;
	int lod = 0;

	bool operator==(const VoxelStreamBlockKey &p_other) const {
		return pos == p_other.pos && lod == p_other.lod;
	}
};

struct VoxelStreamBlockKeyHasher {
	static _FORCE_INLINE_ uint32_t hash(const VoxelStreamBlockKey &p_key) {
		uint32_t h = hash_murmur3_one_32(p_key.pos.x);
		h = hash_murmur3_one_32(p_key.pos.y, h);
		h = hash_murmur3_one_32(p_key.pos.z, h);
		h = hash_murmur3_one_32(p_key.lod, h);
		return hash_fmix32(h);
	}
};

class VoxelStreamSQLite : public RefCounted {
	GDCLASS(VoxelStreamSQLite, RefCounted);

private:
	String db_path;
	bool open_flag = false;
	bool dirty_flag = false;
	HashMap<VoxelStreamBlockKey, PackedByteArray, VoxelStreamBlockKeyHasher> edit_deltas;

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

### 2.3 Proposed C++ Code for `modules/godot_eden/streaming/voxel_stream_sqlite.cpp`

```cpp
/**************************************************************************/
/*  voxel_stream_sqlite.cpp                                               */
/**************************************************************************/

#include "streaming/voxel_stream_sqlite.h"

#include "core/object/class_db.h"

void VoxelStreamSQLite::_bind_methods() {
	ClassDB::bind_method(D_METHOD("open", "path"), &VoxelStreamSQLite::open);
	ClassDB::bind_method(D_METHOD("close"), &VoxelStreamSQLite::close);
	ClassDB::bind_method(D_METHOD("is_open"), &VoxelStreamSQLite::is_open);

	ClassDB::bind_method(D_METHOD("save_block", "pos", "lod", "bytes"), &VoxelStreamSQLite::save_block);
	ClassDB::bind_method(D_METHOD("load_block", "pos", "lod"), &VoxelStreamSQLite::load_block);
	ClassDB::bind_method(D_METHOD("has_block", "pos", "lod"), &VoxelStreamSQLite::has_block);
	ClassDB::bind_method(D_METHOD("delete_block", "pos", "lod"), &VoxelStreamSQLite::delete_block);
	ClassDB::bind_method(D_METHOD("flush"), &VoxelStreamSQLite::flush);
}

VoxelStreamSQLite::VoxelStreamSQLite() {
}

VoxelStreamSQLite::~VoxelStreamSQLite() {
	close();
}

Error VoxelStreamSQLite::open(const String &p_path) {
	close();
	db_path = p_path;
	edit_deltas.clear();
	dirty_flag = false;

	if (FileAccess::exists(db_path)) {
		Ref<FileAccess> f = FileAccess::open(db_path, FileAccess::READ);
		if (f.is_valid()) {
			uint32_t magic = f->get_32();
			if (magic == 0x4553514C || magic == 0x45444442) { // "ESQL" or "EDDB"
				uint32_t count = f->get_32();
				for (uint32_t i = 0; i < count; i++) {
					if (f->get_error() != OK || f->eof_reached()) {
						break;
					}
					VoxelStreamBlockKey key;
					key.pos.x = (int32_t)f->get_32();
					key.pos.y = (int32_t)f->get_32();
					key.pos.z = (int32_t)f->get_32();
					key.lod = (int32_t)f->get_32();
					uint32_t len = f->get_32();
					PackedByteArray data;
					if (len > 0) {
						data.resize(len);
						f->get_buffer(data.ptrw(), len);
					}
					edit_deltas[key] = data;
				}
			}
		}
	}

	open_flag = true;
	return OK;
}

void VoxelStreamSQLite::close() {
	if (open_flag) {
		flush();
		open_flag = false;
		edit_deltas.clear();
		dirty_flag = false;
	}
}

bool VoxelStreamSQLite::is_open() const {
	return open_flag;
}

bool VoxelStreamSQLite::save_block(const Vector3i &p_pos, int p_lod, const PackedByteArray &p_bytes) {
	if (!open_flag) {
		return false;
	}
	VoxelStreamBlockKey key;
	key.pos = p_pos;
	key.lod = p_lod;
	edit_deltas[key] = p_bytes;
	dirty_flag = true;
	return true;
}

PackedByteArray VoxelStreamSQLite::load_block(const Vector3i &p_pos, int p_lod) {
	if (!open_flag) {
		return PackedByteArray();
	}
	VoxelStreamBlockKey key;
	key.pos = p_pos;
	key.lod = p_lod;
	if (edit_deltas.has(key)) {
		return edit_deltas[key];
	}
	return PackedByteArray();
}

bool VoxelStreamSQLite::has_block(const Vector3i &p_pos, int p_lod) const {
	if (!open_flag) {
		return false;
	}
	VoxelStreamBlockKey key;
	key.pos = p_pos;
	key.lod = p_lod;
	return edit_deltas.has(key);
}

void VoxelStreamSQLite::delete_block(const Vector3i &p_pos, int p_lod) {
	if (!open_flag) {
		return;
	}
	VoxelStreamBlockKey key;
	key.pos = p_pos;
	key.lod = p_lod;
	if (edit_deltas.has(key)) {
		edit_deltas.erase(key);
		dirty_flag = true;
	}
}

void VoxelStreamSQLite::flush() {
	if (!open_flag || db_path.is_empty()) {
		return;
	}

	Ref<FileAccess> f = FileAccess::open(db_path, FileAccess::WRITE);
	if (f.is_null()) {
		return;
	}

	// Write Header Magic 0x4553514C ("ESQL") and record count
	f->store_32(0x4553514C);
	f->store_32(edit_deltas.size());

	for (const KeyValue<VoxelStreamBlockKey, PackedByteArray> &E : edit_deltas) {
		f->store_32((uint32_t)E.key.pos.x);
		f->store_32((uint32_t)E.key.pos.y);
		f->store_32((uint32_t)E.key.pos.z);
		f->store_32((uint32_t)E.key.lod);
		f->store_32((uint32_t)E.value.size());
		if (E.value.size() > 0) {
			f->store_buffer(E.value.ptr(), E.value.size());
		}
	}
	f->flush();
	dirty_flag = false;
}
```

---

## 3. Remediation Item 3: Memory Safety Fix in `VoxelBuffer::duplicate_buffer()`

### 3.1 Problem & Requirement Analysis
- **Current State**:
  ```cpp
  Ref<VoxelBuffer> VoxelBuffer::duplicate_buffer() const {
      Ref<VoxelBuffer> dup;
      dup.instantiate();
      dup->copy_from(Ref<VoxelBuffer>(const_cast<VoxelBuffer *>(this)));
      return dup;
  }
  ```
  Constructing `Ref<VoxelBuffer>(const_cast<VoxelBuffer *>(this))` creates a temporary smart pointer over `this`. When the line finishes, the temporary `Ref` destructor runs, calling `unreference()`. If `duplicate_buffer()` is invoked on an unreferenced object or raw pointer, the reference count drops to 0, causing `memdelete(this)` and leading to premature object destruction and heap corruption.
- **Requirement**:
  Introduce a raw pointer helper `void copy_from_raw(const VoxelBuffer *p_other)` and delegate `duplicate_buffer()` and `copy_from(const Ref<VoxelBuffer> &p_other)` to it.

### 3.2 Proposed Changes in `modules/godot_eden/storage/voxel_buffer.h`

Add declaration of `copy_from_raw(const VoxelBuffer *p_other)`:
```cpp
	// Memory & Data Serialization / Export
	size_t get_allocated_memory_bytes() const;
	Ref<VoxelBuffer> duplicate_buffer() const;
	void copy_from_raw(const VoxelBuffer *p_other);
	void copy_from(const Ref<VoxelBuffer> &p_other);
```

### 3.3 Proposed Changes in `modules/godot_eden/storage/voxel_buffer.cpp`

Replace `duplicate_buffer()` and `copy_from()` (around line 409–427):

```cpp
Ref<VoxelBuffer> VoxelBuffer::duplicate_buffer() const {
	Ref<VoxelBuffer> dup;
	dup.instantiate();
	dup->copy_from_raw(this);
	return dup;
}

void VoxelBuffer::copy_from_raw(const VoxelBuffer *p_other) {
	if (!p_other) {
		return;
	}
	for (int c = 0; c < MAX_CHANNELS; c++) {
		channels[c].compression = p_other->channels[c].compression;
		channels[c].uniform_val = p_other->channels[c].uniform_val;
		channels[c].palette = p_other->channels[c].palette;
		channels[c].nibble_data = p_other->channels[c].nibble_data.duplicate();
		channels[c].raw_data = p_other->channels[c].raw_data;
	}
}

void VoxelBuffer::copy_from(const Ref<VoxelBuffer> &p_other) {
	if (p_other.is_null()) {
		return;
	}
	copy_from_raw(p_other.ptr());
}
```

---

## 4. Remediation Item 4: Noise Generator Cleanup in `VoxelGeneratorNoise::_sample_noise_3d`

### 4.1 Problem & Requirement Analysis
- **Current State**:
  In `modules/godot_eden/generators/voxel_generator_noise.cpp` lines 145–150:
  `int A = hash_3d(X, Y, Z);`
  `int AA = hash_3d(X, Y, Z);`
  `int AB = hash_3d(X, Y + 1, Z);`
  `int B = hash_3d(X + 1, Y, Z);`
  `int BA = hash_3d(X + 1, Y, Z);`
  `int BB = hash_3d(X + 1, Y + 1, Z);`
  None of these variables are referenced in the subsequent interpolation expression.
- **Requirement**:
  Precompute all 8 corner hashes into clean local variables `h000` through `h111` and use them directly in the `_grad_dot` interpolation calculations. This eliminates unused variable compiler warnings and avoids redundant hash recalculations.

### 4.2 Proposed Change in `modules/godot_eden/generators/voxel_generator_noise.cpp`

Replace lines 145–160 with:

```cpp
	int h000 = hash_3d(X, Y, Z);
	int h100 = hash_3d(X + 1, Y, Z);
	int h010 = hash_3d(X, Y + 1, Z);
	int h110 = hash_3d(X + 1, Y + 1, Z);
	int h001 = hash_3d(X, Y, Z + 1);
	int h101 = hash_3d(X + 1, Y, Z + 1);
	int h011 = hash_3d(X, Y + 1, Z + 1);
	int h111 = hash_3d(X + 1, Y + 1, Z + 1);

	float res = _lerp(w, _lerp(v, _lerp(u, _grad_dot(h000, x, y, z),
										   _grad_dot(h100, x - 1, y, z)),
								   _lerp(u, _grad_dot(h010, x, y - 1, z),
										   _grad_dot(h110, x - 1, y - 1, z))),
					   _lerp(v, _lerp(u, _grad_dot(h001, x, y, z - 1),
										   _grad_dot(h101, x - 1, y, z - 1)),
							   _lerp(u, _grad_dot(h011, x, y - 1, z - 1),
									   _grad_dot(h111, x + 1, y + 1, z + 1)))); // note: exact coordinates per grad_dot
```

Specifically, exact coordinate parameters for `_grad_dot`:
```cpp
	float res = _lerp(w, _lerp(v, _lerp(u, _grad_dot(h000, x, y, z),
										   _grad_dot(h100, x - 1, y, z)),
								   _lerp(u, _grad_dot(h010, x, y - 1, z),
										   _grad_dot(h110, x - 1, y - 1, z))),
					   _lerp(v, _lerp(u, _grad_dot(h001, x, y, z - 1),
										   _grad_dot(h101, x - 1, y, z - 1)),
							   _lerp(u, _grad_dot(h011, x, y - 1, z - 1),
									   _grad_dot(h111, x - 1, y - 1, z - 1))));
	return res;
```

---

## 5. Verification Strategy & Test Commands

After applying these code modifications, the implementer can verify the build and tests as follows:

1. **Compilation**:
   Run SCons build or C++ compilation check:
   ```bash
   scons platform=windows target=editor -j8
   ```
2. **C++ Unit Tests**:
   Run doctest suite:
   ```bash
   bin/godot.windows.editor.x86_64.exe --test --test-case="*[GodotEden]*"
   ```
3. **E2E Test Suite**:
   Run python E2E test runner:
   ```bash
   python tests/e2e/runner.py
   ```
