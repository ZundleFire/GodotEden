# Technical Analysis & Architectural Design: LodOctree & VoxelGeneratorNoise

**Author**: explorer_m2_2 (teamwork_preview_explorer)  
**Milestone**: Milestone 2 — Dual-Tier Voxel Storage & Streaming Pipeline  
**Target Module**: `modules/godot_eden`  
**Date**: 2026-08-05  

---

## Executive Summary

This document provides a comprehensive technical analysis and architectural design for two core subsystems of GodotEden's Milestone 2:
1. **`LodOctree` (`modules/godot_eden/storage/lod_octree.h/cpp`)**: Pointerless index-based Sparse Voxel Octree (SVO) DAG node pools for micro-voxel memory compression at planetary distances (98%+ memory reduction).
2. **`VoxelGeneratorNoise` (`modules/godot_eden/generators/voxel_generator_noise.h/cpp`)**: Spherical Signed Distance Function (SDF) domain warping engine for planet-scale procedural terrain using FastNoise2 / CPU fallbacks.

Both subsystems follow standard Godot 4 C++ engine module conventions, integrate with Godot's `ClassDB`, leverage SCons build integration (`SCsub`), and mirror memory layouts suitable for Vulkan SSBO compute shader dispatch (`RenderingDevice`).

---

## 1. Architectural & Engine Context

### 1.1 Module Structure & File Placement
```
modules/godot_eden/
├── config.py                                 # Module config & doc class exports
├── SCsub                                     # SCons build instructions
├── register_types.h / .cpp                   # ClassDB registrations
├── storage/
│   ├── lod_octree.h                          # SVO DAG Storage header
│   └── lod_octree.cpp                        # SVO DAG Storage implementation
├── generators/
│   ├── voxel_generator_noise.h               # Spherical Noise Generator header
│   └── voxel_generator_noise.cpp             # Spherical Noise Generator implementation
└── tests/
    ├── test_storage.h                        # LodOctree Doctest suite
    └── test_main.h                           # Module main unit test entry point
```

### 1.2 Build Hooks & SCons Setup
In `modules/godot_eden/SCsub`, source collection must be updated to include the new subdirectories:
```python
# Module subdirectories
env_godot_eden.add_source_files(sources, "nodes/*.cpp")
env_godot_eden.add_source_files(sources, "storage/*.cpp")
env_godot_eden.add_source_files(sources, "streaming/*.cpp")
env_godot_eden.add_source_files(sources, "generators/*.cpp")
```

In `modules/godot_eden/config.py`:
```python
def get_doc_classes():
    return [
        "VoxelWorld",
        "VoxelVolume",
        "VoxelRenderer",
        "VoxelStreamer",
        "VoxelGenerator",
        "VoxelGeneratorNoise",
        "LodOctree",
    ]
```

In `modules/godot_eden/register_types.cpp`:
```cpp
#include "generators/voxel_generator_noise.h"
#include "storage/lod_octree.h"

void initialize_godot_eden_module(ModuleInitializationLevel p_level) {
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_CLASS(VoxelWorld);
        GDREGISTER_CLASS(VoxelVolume);
        GDREGISTER_CLASS(VoxelRenderer);
        GDREGISTER_CLASS(VoxelStreamer);
        GDREGISTER_CLASS(VoxelGenerator);
        GDREGISTER_CLASS(VoxelGeneratorNoise);
        GDREGISTER_CLASS(LodOctree);
    }
}
```

---

## 2. Subsystem Analysis: `LodOctree` (Pointerless SVO DAG Storage)

### 2.1 Problem & Design Rationale
Planetary micro-voxel volumes require representing millions of voxels at varying levels of detail. Uncompressed spatial arrays cause prohibitive memory consumption. Standard octrees using 64-bit C++ pointers (`SvoNode* children[8]`) incur 64 bytes of pointer overhead per node alone, leading to severe cache thrashing and memory fragmentation.

`LodOctree` solves this by introducing a **pointerless index-based Sparse Voxel Octree (SVO) Directed Acyclic Graph (DAG)**:
1. **Contiguous Node Pool**: All nodes reside in a single contiguous array (`Vector<SvoNode>`). Pointers are replaced by 32-bit `uint32_t` indices.
2. **DAG Deduplication**: Merges structurally identical subtrees (e.g. uniform underground bedrock, empty atmospheric space) into a single shared node index in the pool. This yields **98%+ memory compression** for planetary micro-voxel volumes.
3. **GPU SSBO Alignment**: The flat array structure exports directly to Vulkan SSBO storage buffers via `get_ssbo_buffer_bytes()`, enabling stackless compute raymarching.

### 2.2 Data Structure Definitions & GPU Alignment

```cpp
// 16-Byte Aligned Struct for GPU Compute SSBO Compatibility
struct SvoNode {
    uint32_t child_mask = 0;       // Bits 0-7: active child mask (1 bit per octant)
    uint32_t first_child_idx = 0;  // Index in pool of the first contiguous child block (0 if leaf)
    uint32_t material_tag = 0;     // 16-bit packed material attribute ID / material tag
    float sdf_value = 0.0f;        // Representative Signed Distance Function value
};

// DAG Deduplication Key (Hashable)
struct SvoDagKey {
    uint32_t children[8];
    uint32_t material_tag;
    uint8_t child_mask;

    bool operator==(const SvoDagKey &p_other) const {
        if (child_mask != p_other.child_mask || material_tag != p_other.material_tag) {
            return false;
        }
        for (int i = 0; i < 8; i++) {
            if (children[i] != p_other.children[i]) {
                return false;
            }
        }
        return true;
    }
};

// Murmur3 Hash Function for SvoDagKey
struct SvoDagKeyHasher {
    static _FORCE_INLINE_ uint32_t hash(const SvoDagKey &p_key) {
        uint32_t h = hash_murmur3_one_32(p_key.child_mask);
        h = hash_murmur3_one_32(p_key.material_tag, h);
        for (int i = 0; i < 8; i++) {
            h = hash_murmur3_one_32(p_key.children[i], h);
        }
        return hash_fmix32(h);
    }
};
```

### 2.3 `LodOctree` C++ Class Definition

`modules/godot_eden/storage/lod_octree.h`:
```cpp
/**************************************************************************/
/*  lod_octree.h                                                          */
/**************************************************************************/

#pragma once

#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "core/variant/packed_byte_array.h"
#include "core/math/vector3.h"

// Forward declaration of SvoNode struct
struct SvoNode;
struct SvoDagKey;
struct SvoDagKeyHasher;

class LodOctree : public RefCounted {
    GDCLASS(LodOctree, RefCounted);

private:
    Vector<SvoNode> nodes;
    HashMap<SvoDagKey, uint32_t, SvoDagKeyHasher> dag_hash_map;
    int max_depth = 16;
    uint32_t root_node_index = 0;

protected:
    static void _bind_methods();

public:
    LodOctree();
    ~LodOctree();

    // Depth & Node Capacity Configuration
    void set_max_depth(int p_depth);
    int get_max_depth() const;

    uint32_t get_node_count() const;
    uint32_t get_root_node_index() const;

    // Node Pool & DAG Operations
    uint32_t insert_node(const SvoNode &p_node);
    uint32_t insert_dag_branch(uint8_t p_child_mask, const uint32_t p_children[8], uint32_t p_material_tag = 0, float p_sdf = 0.0f);
    uint32_t find_matching_dag_node(const SvoDagKey &p_key) const;

    SvoNode get_node(uint32_t p_index) const;
    void clear();

    // Statistics & Compression Metrics
    float get_compression_ratio(uint32_t p_total_leaves) const;
    size_t get_memory_footprint() const;

    // Traversal & Sampling Interfaces
    float sample_sdf_at(Vector3 p_local_pos) const;
    uint32_t get_material_at(Vector3 p_local_pos) const;

    // GPU Compute Serialization (Vulkan SSBO)
    PackedByteArray get_ssbo_buffer_bytes() const;
};
```

### 2.4 Algorithmic Mechanics & Workflow

1. **Root Initialization**:
   `LodOctree` constructor initializes `nodes` with an empty root node at index 0 (`nodes[0]`).
2. **Bottom-Up DAG Deduplication Algorithm**:
   - When inserting a branch node with 8 child indices:
     - Construct `SvoDagKey key` containing child mask, child indices, and material tag.
     - Perform lookup in `dag_hash_map`.
     - If `key` exists in `dag_hash_map`, return the existing `uint32_t` node index.
     - If `key` is new, construct `SvoNode`, assign `node_index = nodes.size()`, append to `nodes`, insert `(key, node_index)` into `dag_hash_map`, and return `node_index`.
3. **Stackless Spatial Traversal Algorithm**:
   - Input: Local position $(x, y, z) \in [0, 1]^3$.
   - Start at `curr_index = root_node_index` (index 0).
   - Loop for depth $d = 0 \dots \text{max\_depth}-1$:
     - Fetch `node = nodes[curr_index]`.
     - If `node.child_mask == 0` or `node.first_child_idx == 0` (leaf node), return `node.sdf_value` / `node.material_tag`.
     - Compute sub-octant index `octant = (x >= 0.5f ? 1 : 0) | (y >= 0.5f ? 2 : 0) | (z >= 0.5f ? 4 : 0)`.
     - Check if `(node.child_mask & (1 << octant)) != 0`.
     - If bit is not set, return `node.sdf_value` (empty/uniform space).
     - Determine child pool offset: `curr_index = node.first_child_idx + octant` (or bit-count offset).
     - Remap coordinate: `x = (x >= 0.5f) ? (x - 0.5f) * 2.0f : x * 2.0f`, repeat for $y, z$.
4. **Copy-On-Write (COW) Micro-Voxel Editing**:
   - Editing a single voxel unshares only the nodes along the target path.
   - Untouched sibling subtrees retain their existing shared DAG node indices.

---

## 3. Subsystem Analysis: `VoxelGeneratorNoise` (Spherical SDF Terrain)

### 3.1 Problem & Design Rationale
Planet-scale voxel games require generating dynamic volumetric spherical terrain across vast coordinate spaces. The terrain isosurface is defined by a 3D Signed Distance Function (SDF) where:
- $SDF(p) < 0$: Interior solid terrain (rock, dirt, minerals).
- $SDF(p) = 0$: The ground surface (isosurface for meshing & raymarching).
- $SDF(p) > 0$: Air / atmosphere / outer space.

`VoxelGeneratorNoise` extends `VoxelGenerator` (Resource) to produce planet-scale terrain using spherical domain warping combined with multi-octave fractal noise (FBM).

### 3.2 Mathematical Model & Spherical SDF Domain Warping

1. **Base Spherical SDF**:
   $$d_{\text{sphere}}(p) = \|p - c\| - R_{\text{planet}}$$
   where $p = (x, y, z)$, $c = (0, 0, 0)$ is planet center, and $R_{\text{planet}}$ is planet radius.
2. **Domain Warping**:
   Before sampling surface noise, domain coordinates are warped to create natural ridges, continents, and overhangs:
   $$p_{\text{warped}} = p + \text{Noise3D}(p \cdot f_{\text{warp}}) \cdot A_{\text{warp}}$$
3. **Multi-Octave Fractal Noise (FBM)**:
   $$H(p) = \sum_{i=0}^{\text{octaves}-1} \text{gain}^i \cdot \text{Simplex3D}(p_{\text{warped}} \cdot \text{frequency} \cdot \text{lacunarity}^i) \cdot \text{height\_scale}$$
4. **Combined Spherical SDF**:
   $$\text{SDF}(p) = (\|p - c\| - R_{\text{planet}}) - H(p)$$

### 3.3 `VoxelGeneratorNoise` C++ Class Definition

`modules/godot_eden/generators/voxel_generator_noise.h`:
```cpp
/**************************************************************************/
/*  voxel_generator_noise.h                                               */
/**************************************************************************/

#pragma once

#include "nodes/voxel_generator.h"
#include "storage/voxel_buffer.h"

class VoxelGeneratorNoise : public VoxelGenerator {
    GDCLASS(VoxelGeneratorNoise, VoxelGenerator);

private:
    float frequency = 0.005f;
    int octaves = 4;
    float lacunarity = 2.0f;
    float gain = 0.5f;
    float planet_radius = 1000.0f;
    float warp_amplitude = 0.0f;
    float warp_frequency = 0.001f;

    // CPU Reference Noise Generation Helpers
    float _sample_fbm(Vector3 p_pos) const;
    float _simplex_noise_3d(Vector3 p_pos) const;

protected:
    static void _bind_methods();

public:
    VoxelGeneratorNoise();
    ~VoxelGeneratorNoise();

    // Property Exports & Getters/Setters
    void set_frequency(float p_freq);
    float get_frequency() const;

    void set_octaves(int p_octaves);
    int get_octaves() const;

    void set_lacunarity(float p_lacunarity);
    float get_lacunarity() const;

    void set_gain(float p_gain);
    float get_gain() const;

    void set_planet_radius(float p_radius);
    float get_planet_radius() const;

    void set_warp_amplitude(float p_amp);
    float get_warp_amplitude() const;

    void set_warp_frequency(float p_freq);
    float get_warp_frequency() const;

    // SDF Evaluation Methods
    float get_single_sdf(Vector3 p_position) const;
    virtual float generate_voxel_f(Vector3 p_position) const override;

    // Bulk Block Generation
    void generate_block(VoxelBuffer &r_buffer, Vector3i p_block_pos, int p_lod = 0) const;
};
```

### 3.4 Property Export & ClassDB Bindings

In `modules/godot_eden/generators/voxel_generator_noise.cpp`:
```cpp
void VoxelGeneratorNoise::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_frequency", "frequency"), &VoxelGeneratorNoise::set_frequency);
    ClassDB::bind_method(D_METHOD("get_frequency"), &VoxelGeneratorNoise::get_frequency);

    ClassDB::bind_method(D_METHOD("set_octaves", "octaves"), &VoxelGeneratorNoise::set_octaves);
    ClassDB::bind_method(D_METHOD("get_octaves"), &VoxelGeneratorNoise::get_octaves);

    ClassDB::bind_method(D_METHOD("set_lacunarity", "lacunarity"), &VoxelGeneratorNoise::set_lacunarity);
    ClassDB::bind_method(D_METHOD("get_lacunarity"), &VoxelGeneratorNoise::get_lacunarity);

    ClassDB::bind_method(D_METHOD("set_gain", "gain"), &VoxelGeneratorNoise::set_gain);
    ClassDB::bind_method(D_METHOD("get_gain"), &VoxelGeneratorNoise::get_gain);

    ClassDB::bind_method(D_METHOD("set_planet_radius", "radius"), &VoxelGeneratorNoise::set_planet_radius);
    ClassDB::bind_method(D_METHOD("get_planet_radius"), &VoxelGeneratorNoise::get_planet_radius);

    ClassDB::bind_method(D_METHOD("set_warp_amplitude", "amplitude"), &VoxelGeneratorNoise::set_warp_amplitude);
    ClassDB::bind_method(D_METHOD("get_warp_amplitude"), &VoxelGeneratorNoise::get_warp_amplitude);

    ClassDB::bind_method(D_METHOD("set_warp_frequency", "frequency"), &VoxelGeneratorNoise::set_warp_frequency);
    ClassDB::bind_method(D_METHOD("get_warp_frequency"), &VoxelGeneratorNoise::get_warp_frequency);

    ClassDB::bind_method(D_METHOD("get_single_sdf", "position"), &VoxelGeneratorNoise::get_single_sdf);
    ClassDB::bind_method(D_METHOD("generate_block", "buffer", "block_pos", "lod"), &VoxelGeneratorNoise::generate_block, DEFVAL(0));

    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "frequency", PROPERTY_HINT_RANGE, "0.0001,1.0,0.0001"), "set_frequency", "get_frequency");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "octaves", PROPERTY_HINT_RANGE, "1,10,1"), "set_octaves", "get_octaves");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lacunarity", PROPERTY_HINT_RANGE, "1.0,4.0,0.1"), "set_lacunarity", "get_lacunarity");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gain", PROPERTY_HINT_RANGE, "0.01,1.0,0.01"), "set_gain", "get_gain");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "planet_radius", PROPERTY_HINT_RANGE, "1.0,1000000.0,1.0"), "set_planet_radius", "get_planet_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "warp_amplitude", PROPERTY_HINT_RANGE, "0.0,1000.0,0.1"), "set_warp_amplitude", "get_warp_amplitude");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "warp_frequency", PROPERTY_HINT_RANGE, "0.0001,0.1,0.0001"), "set_warp_frequency", "get_warp_frequency");
}
```

---

## 4. Doctest Verification Strategy

Unit tests co-located in `modules/godot_eden/tests/test_main.h` (or `test_storage.h`) will verify the functionality and edge cases of both subsystems:

### 4.1 `LodOctree` Test Cases
1. **Instantiation & Defaults**: Verify `max_depth == 16`, `node_count == 1`, `root_node_index == 0`.
2. **DAG Deduplication**: Insert 10,000 identical subtrees, verify node index returned is identical and pool size remains 2 nodes. Calculate compression ratio (>98%).
3. **Point Sampling & Traversal**: Insert a leaf node at specific octant coordinates and verify `sample_sdf_at` and `get_material_at` return correct values.
4. **Copy-On-Write Unsharing**: Edit a micro-voxel in a shared node, verify only the affected path creates new nodes while unchanged siblings remain shared.
5. **SSBO Byte Serialization**: Export `get_ssbo_buffer_bytes()` and verify byte array size equals `node_count * sizeof(SvoNode)`.

### 4.2 `VoxelGeneratorNoise` Test Cases
1. **Defaults & Property Bounds**: Verify defaults (`frequency=0.005`, `octaves=4`, `lacunarity=2.0`, `gain=0.5`, `planet_radius=1000.0`). Test clamping bounds (`octaves >= 1`, `planet_radius >= 0.1`).
2. **Spherical Isosurface SDF**:
   - Origin $(0, 0, 0)$: returns $-1000.0$ (deep interior).
   - Far space $(2000, 0, 0)$: returns $>0$ (exterior).
   - Surface $(1000, 0, 0)$: returns approximately $0.0$.
3. **Seed Determinism**: Verify identical seeds yield bit-exact SDF values while different seeds diverge.
4. **Bulk Block Generation**: Fill a 16x16x16 `VoxelBuffer` via `generate_block` and verify all 4,096 voxels are populated correctly.

---

## 5. Implementation Roadmap & Checklist

| Step | Subsystem | Action Item | Target File |
|------|-----------|-------------|-------------|
| 1 | Infrastructure | Update SCons build rules to collect `storage/*.cpp` and `generators/*.cpp` | `modules/godot_eden/SCsub` |
| 2 | Infrastructure | Add `LodOctree` & `VoxelGeneratorNoise` to ClassDB registration | `modules/godot_eden/register_types.cpp` |
| 3 | Infrastructure | Export doc classes in module configuration | `modules/godot_eden/config.py` |
| 4 | Storage | Implement `SvoNode` struct, `SvoDagKey` hash, and `LodOctree` class | `storage/lod_octree.h/cpp` |
| 5 | Storage | Implement DAG deduplication hash map and stackless traversal | `storage/lod_octree.cpp` |
| 6 | Generators | Implement `VoxelGeneratorNoise` class with spherical SDF & domain warping | `generators/voxel_generator_noise.h/cpp` |
| 7 | Generators | Implement `generate_block` bulk voxel buffer filling | `generators/voxel_generator_noise.cpp` |
| 8 | Testing | Add C++ Doctest unit tests to test harness | `tests/test_main.h` |
