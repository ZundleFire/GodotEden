# Technical Analysis & Architecture Specification: VoxelBuffer & VoxelDataMap

**Author**: `explorer_m2_1`  
**Milestone**: M2 (Dual-Tier Voxel Storage & Streaming Pipeline)  
**Date**: 2026-08-05  

---

## 1. Executive Summary

This document presents the complete technical design, architectural specifications, data structures, and C++ interface contracts for `VoxelBuffer` and `VoxelDataMap` within the `modules/godot_eden/storage/` subsystem of GodotEden.

- **`VoxelBuffer`**: A memory-compacted $16 \times 16 \times 16$ ($4,096$ voxels) block storage container supporting multiple independent channels (Signed Distance Field, Material/Block Indices, Color/Custom). Features automatic 3-tier compression: **Uniform Value Storage** ($99.9\%$ memory savings), **16-Entry Palette Compaction** ($74\% - 87\%$ memory savings via 4-bit nibble packing), and **Raw Flat Storage**.
- **`VoxelDataMap`**: A thread-safe, high-performance hash map container (`HashMap<Vector3i, Ref<VoxelBuffer>>`) for active near-LOD clipmap chunks. Built with Godot engine synchronization primitives (`RWLock`), fast spatial hashing (`Vector3i`), block lifecycle management (allocation/deallocation), and $3 \times 3 \times 3$ neighborhood region sampling for seamless meshing and streaming updates.

---

## 2. VoxelBuffer Subsystem Architecture

### 2.1 Domain & Volume Constants
- **Block Size**: $16 \times 16 \times 16$ voxels.
- **Voxel Volume**: $4,096$ voxels per block.
- **Indexing Formula**: For local coordinates $(x, y, z) \in [0, 15]^3$:
  $$\text{Index}(x, y, z) = x + (y \times 16) + (z \times 256) = x + 16(y + 16z)$$
- **Channels**:
  - `CHANNEL_SDF = 0`: Float signed distance value (used for smooth surface contouring / Marching Cubes / Dual Contouring).
  - `CHANNEL_MATERIAL = 1`: Integer material or block type identifier (used for terrain texturing, material properties).
  - `CHANNEL_COLOR = 2`: RGBA / Color data (used for custom vertex tinting or micro-voxel colors).
  - `CHANNEL_CUSTOM = 3`: Custom user attribute channel.
  - `MAX_CHANNELS = 4`.

---

### 2.2 Compression Modes & Memory Footprint Analysis

Each channel in `VoxelBuffer` maintains an independent compression state:

```
                  +-----------------------------------+
                  |        Channel Data Storage       |
                  +-----------------------------------+
                                    |
          +-------------------------+-------------------------+
          |                         |                         |
          v                         v                         v
+-------------------+     +-------------------+     +-------------------+
| COMPRESSION_      |     | COMPRESSION_      |     | COMPRESSION_      |
| UNIFORM           |     | PALETTE           |     | RAW               |
|                   |     |                   |     |                   |
| 1 value stored    |     | <=16 palette ent. |     | 4096 full values  |
| 4 bytes           |     | 4-bit nibbles     |     | 16,384 bytes      |
| ~99.9% savings    |     | 2,112 bytes total |     | Uncompressed      |
|                   |     | 74%-87% savings   |     |                   |
+-------------------+     +-------------------+     +-------------------+
```

#### Detailed Memory Breakdown per 4,096-Voxel Channel:

| Compression Mode | Storage Mechanism | Data Allocation | Total Footprint (bytes) | Memory Reduction |
|------------------|-------------------|-----------------|-------------------------|------------------|
| **`UNIFORM`** | Single 32-bit float or uint32 value | No buffer allocated | **4 bytes** | **99.98%** |
| **`PALETTE`** | 16-entry palette table + 4-bit nibbles | $16 \times 4\text{ B palette} + (4096 / 2)\text{ B nibbles}$ | **2,112 bytes** | **87.11%** (vs 32-bit raw) / **74.22%** (vs 16-bit raw) |
| **`RAW`** | Uncompressed array of 4,096 elements | $4096 \times 4\text{ B elements}$ | **16,384 bytes** | 0.00% (Baseline) |

---

### 2.3 Nibble Packing & Bitwise Palette Compaction Math

In `PALETTE` mode:
1. Palette array `palette_table` contains up to 16 unique values ($N \le 16$).
2. Nibble array `nibble_data` is a `PackedByteArray` of size $2,048$ bytes ($4,096 \times 4\text{ bits} / 8\text{ bits/byte}$).
3. For a voxel at linear index $i \in [0, 4095]$:
   - Byte index: $b = i / 2$
   - Nibble shift: $s = (i \bmod 2) \times 4$
   - Palette index read:
     $$\text{palette\_idx} = (\text{nibble\_data}[b] \gg s) \ \& \ 0x0F$$
     $$\text{voxel\_value} = \text{palette\_table}[\text{palette\_idx}]$$
   - Palette index write:
     $$\text{nibble\_data}[b] = (\text{nibble\_data}[b] \ \& \ \sim(0x0F \ll s)) \ | \ ((\text{palette\_idx} \ \& \ 0x0F) \ll s)$$

---

### 2.4 Auto-Expansion & Compression Lifecycle

When `set_voxel_f` or `set_voxel_u` is called on a voxel:

1. **State: `COMPRESSION_UNIFORM`**:
   - If `new_val == uniform_val` $\rightarrow$ No operation required.
   - If `new_val != uniform_val` $\rightarrow$ Demote channel to `COMPRESSION_PALETTE` (with palette `[uniform_val, new_val]`) or `COMPRESSION_RAW` (fill array with `uniform_val`), then set the new voxel value.

2. **State: `COMPRESSION_PALETTE`**:
   - If `new_val` exists in `palette_table` $\rightarrow$ Write palette index into nibble array.
   - If `new_val` is NOT in `palette_table` and `palette_table.size() < 16` $\rightarrow$ Append `new_val` to palette table, write new index into nibble array.
   - If `new_val` is NOT in `palette_table` and `palette_table.size() == 16` $\rightarrow$ Demote channel to `COMPRESSION_RAW`, expand all 4,096 nibbles to full 32-bit values in a flat array, clear nibble array, then write `new_val`.

3. **Explicit Optimization (`compress_palette`)**:
   - Scans the 4,096 voxel values of a channel.
   - Builds a set of unique values.
   - If unique count $== 1 \rightarrow$ Convert to `COMPRESSION_UNIFORM`.
   - If $2 \le \text{unique count} \le 16 \rightarrow$ Build 16-entry palette, construct nibble buffer, convert to `COMPRESSION_PALETTE`.
   - If unique count $> 16 \rightarrow$ Retain `COMPRESSION_RAW`.

---

### 2.5 Proposed C++ Class Definition (`voxel_buffer.h`)

```cpp
/**************************************************************************/
/*  voxel_buffer.h                                                        */
/**************************************************************************/

#pragma once

#include "core/object/ref_counted.h"
#include "core/templates/vector.h"
#include "core/variant/packed_byte_array.h"
#include "core/math/vector3i.h"

class VoxelBuffer : public RefCounted {
	GDCLASS(VoxelBuffer, RefCounted);

public:
	static const int BLOCK_SIZE = 16;
	static const int BLOCK_VOLUME = 4096; // 16 * 16 * 16

	enum ChannelId {
		CHANNEL_SDF = 0,
		CHANNEL_MATERIAL = 1,
		CHANNEL_COLOR = 2,
		CHANNEL_CUSTOM = 3,
		MAX_CHANNELS = 4
	};

	enum CompressionType {
		COMPRESSION_UNIFORM = 0,
		COMPRESSION_PALETTE = 1,
		COMPRESSION_RAW = 2
	};

private:
	struct ChannelStorage {
		CompressionType compression = COMPRESSION_UNIFORM;
		
		// UNIFORM storage
		union {
			float uniform_val_f;
			uint32_t uniform_val_u;
		};

		// PALETTE storage (max 16 entries)
		Vector<float> palette_f;
		Vector<uint32_t> palette_u;
		PackedByteArray nibble_data; // 2048 bytes when active

		// RAW storage (4096 elements)
		Vector<float> raw_data_f;
		Vector<uint32_t> raw_data_u;

		ChannelStorage() {
			uniform_val_u = 0;
		}
	};

	ChannelStorage channels[MAX_CHANNELS];

	_FORCE_INLINE_ static int _get_voxel_index(int p_x, int p_y, int p_z) {
		return p_x + (p_y * BLOCK_SIZE) + (p_z * BLOCK_SIZE * BLOCK_SIZE);
	}

	_FORCE_INLINE_ static bool _is_bounds_valid(int p_x, int p_y, int p_z) {
		return (unsigned int)p_x < BLOCK_SIZE && (unsigned int)p_y < BLOCK_SIZE && (unsigned int)p_z < BLOCK_SIZE;
	}

	void _expand_uniform_to_raw(int p_channel);
	void _expand_palette_to_raw(int p_channel);

protected:
	static void _bind_methods();

public:
	VoxelBuffer();
	~VoxelBuffer();

	// Float Voxel Accessors (SDF / Color)
	void set_voxel_f(float p_val, int p_x, int p_y, int p_z, int p_channel = CHANNEL_SDF);
	float get_voxel_f(int p_x, int p_y, int p_z, int p_channel = CHANNEL_SDF) const;
	void set_voxel_f_vec(float p_val, const Vector3i &p_pos, int p_channel = CHANNEL_SDF);
	float get_voxel_f_vec(const Vector3i &p_pos, int p_channel = CHANNEL_SDF) const;

	// Integer Voxel Accessors (Material Index)
	void set_voxel_u(uint32_t p_val, int p_x, int p_y, int p_z, int p_channel = CHANNEL_MATERIAL);
	uint32_t get_voxel_u(int p_x, int p_y, int p_z, int p_channel = CHANNEL_MATERIAL) const;
	void set_voxel_u_vec(uint32_t p_val, const Vector3i &p_pos, int p_channel = CHANNEL_MATERIAL);
	uint32_t get_voxel_u_vec(const Vector3i &p_pos, int p_channel = CHANNEL_MATERIAL) const;

	// Bulk operations & Fill
	void fill_f(float p_val, int p_channel = CHANNEL_SDF);
	void fill_u(uint32_t p_val, int p_channel = CHANNEL_MATERIAL);
	void clear();

	// Compression & Optimization
	bool compress_palette(int p_channel = -1);
	void optimize();
	CompressionType get_channel_compression_type(int p_channel) const;
	bool is_uniform(int p_channel) const;
	float get_uniform_value_f(int p_channel) const;
	uint32_t get_uniform_value_u(int p_channel) const;

	// Memory & Data Export
	size_t get_allocated_memory_bytes() const;
	Ref<VoxelBuffer> duplicate_buffer() const;
	void copy_from(const Ref<VoxelBuffer> &p_other);
};
```

---

## 3. VoxelDataMap Subsystem Architecture

### 3.1 Role & Near-LOD Clipmap Map Storage
`VoxelDataMap` acts as the spatial hash map store for active near-LOD clipmap chunks. 
- World space is partitioned into $16^3$ block units indexed by `Vector3i(bx, by, bz)`.
- Chunks within the active camera view radius are inserted into `VoxelDataMap`.
- Chunks moving outside the view radius are evicted / saved to persistence.

---

### 3.2 Thread-Safety & Spatial Lock Integration

GodotEden uses a dual-layer synchronization model:

1. **`VoxelDataMap` Internal `RWLock`**:
   - Read lock (`rw_lock.read_lock()`) during block queries (`get_block`, `has_block`, `get_block_neighborhood`). Multiple reader threads (e.g. GPU uploaders, raymarch mesh extractors) can read block pointers concurrently without blocking.
   - Write lock (`rw_lock.write_lock()`) during map insertions, updates, and removals (`set_block`, `remove_block`, `clear`).
2. **`SpatialLock3D` Co-operation**:
   - `SpatialLock3D` operates at the region level (3D spatial coordinate ranges) to lock chunk content edits (voxel modifications, noise generation).
   - `VoxelDataMap` protects map structure integrity (hash map bucket modification).

---

### 3.3 Spatial Neighborhood Lookup ($3 \times 3 \times 3$)

Mesh generators (Dual Contouring, Greedy Meshing) and normal estimation require sampling voxels at chunk borders. Accessing neighboring chunks one-by-one with separate locks causes high locking overhead.

`VoxelDataMap::get_block_neighborhood` acquires a single read-lock and extracts all 27 neighboring blocks around a target block $(X, Y, Z)$:

$$\text{Neighborhood Index}(dx, dy, dz) = (dx+1) + 3(dy+1) + 9(dz+1) \quad \text{for } dx, dy, dz \in \{-1, 0, 1\}$$

If a neighbor block is missing in the map, a `nullptr` or empty/uniform default `Ref<VoxelBuffer>` is populated in the 27-element output array.

---

### 3.4 Proposed C++ Class Definition (`voxel_data_map.h`)

```cpp
/**************************************************************************/
/*  voxel_data_map.h                                                      */
/**************************************************************************/

#pragma once

#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "core/os/rw_lock.h"
#include "core/variant/typed_array.h"
#include "core/math/vector3i.h"
#include "storage/voxel_buffer.h"

class VoxelDataMap : public RefCounted {
	GDCLASS(VoxelDataMap, RefCounted);

private:
	HashMap<Vector3i, Ref<VoxelBuffer>> blocks;
	mutable RWLock rw_lock;

protected:
	static void _bind_methods();

public:
	VoxelDataMap();
	~VoxelDataMap();

	// Block Accessors
	bool has_block(const Vector3i &p_pos) const;
	Ref<VoxelBuffer> get_block(const Vector3i &p_pos) const;
	Ref<VoxelBuffer> get_or_create_block(const Vector3i &p_pos);
	void set_block(const Vector3i &p_pos, const Ref<VoxelBuffer> &p_buffer);
	bool remove_block(const Vector3i &p_pos);
	void clear();

	// Map Info & Statistics
	int get_block_count() const;
	TypedArray<Vector3i> get_active_block_positions() const;
	size_t get_total_memory_usage() const;

	// Neighborhood Operations (3x3x3 Neighborhood Retrieval)
	TypedArray<VoxelBuffer> get_block_neighborhood(const Vector3i &p_center_pos, int p_radius = 1) const;
};
```

---

## 4. Godot Engine Integration & Module Build Hooks

### 4.1 SCsub Integration
The new `storage` directory will be integrated into `modules/godot_eden/SCsub`:

```python
# Module subdirectories
env_godot_eden.add_source_files(sources, "nodes/*.cpp")
env_godot_eden.add_source_files(sources, "storage/*.cpp")
env_godot_eden.add_source_files(sources, "streaming/*.cpp")
```

### 4.2 ClassDB Registrations (`register_types.cpp`)
In `initialize_godot_eden_module(ModuleInitializationLevel p_level)`:
```cpp
#include "storage/voxel_buffer.h"
#include "storage/voxel_data_map.h"

void initialize_godot_eden_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(VoxelWorld);
		GDREGISTER_CLASS(VoxelVolume);
		GDREGISTER_CLASS(VoxelRenderer);
		GDREGISTER_CLASS(VoxelStreamer);
		GDREGISTER_CLASS(VoxelGenerator);
		
		// Milestone 2 Registrations
		GDREGISTER_CLASS(VoxelBuffer);
		GDREGISTER_CLASS(VoxelDataMap);
	}
}
```

---

## 5. Verification Strategy & Test Matrix

To guarantee robust functionality, tests must be added to `modules/godot_eden/tests/test_storage.h` and executed via `godot --test`.

### 5.1 Test Cases for `VoxelBuffer`
1. **Default State**: Initial buffer created is UNIFORM (0.0 for SDF, 0 for material).
2. **Read/Write Accuracy**: Write SDF float values to arbitrary coordinates $(x,y,z)$, verify correct reads.
3. **Auto-Demotion (Uniform $\rightarrow$ Palette $\rightarrow$ Raw)**:
   - Fill buffer with 1 uniform value.
   - Write 5 distinct values $\rightarrow$ verify compression state transitions to `COMPRESSION_PALETTE`.
   - Write 20 distinct values $\rightarrow$ verify compression state transitions to `COMPRESSION_RAW`.
4. **Palette Compaction Verification**:
   - Populate buffer with 10 unique values across 4,096 voxels.
   - Call `compress_palette()`.
   - Verify `get_channel_compression_type()` returns `COMPRESSION_PALETTE`.
   - Verify memory usage is $\approx 2,112$ bytes (a $74\%+$ reduction).
   - Verify every voxel query `get_voxel_f` returns exact expected float value.

### 5.2 Test Cases for `VoxelDataMap`
1. **Thread-Safe Insertion & Retrieval**: Single & multi-thread `set_block` and `get_block`.
2. **Neighborhood Sampling**: Insert center block and 26 surrounding blocks. Call `get_block_neighborhood` and verify all 27 blocks returned match inserted positions.
3. **Memory Aggregation**: Verify `get_total_memory_usage()` accurately sums memory across all allocated `VoxelBuffer` blocks.
