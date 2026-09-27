# Technical Analysis Report: Concentric Clipmap LOD Pipeline & ATC Attribute Pipeline

**Module**: `godot_eden`  
**Milestone**: Milestone 3 — Micro-Voxel Renderer & Shaders Pipeline  
**Author**: Explorer 2 (`explorer_m3_2`)  
**Date**: 2026-08-05  

---

## Executive Summary

This report provides a comprehensive architectural analysis and C++ class specification for two core rendering components of GodotEden Milestone 3:
1. **Concentric Clipmap LOD Pipeline**: Camera-centered clipmap LOD rings for smooth planet-scale micro-voxel level-of-detail transitions, center updates, hysteresis snapping, toroidal shift, and GPU SSBO upload strategy.
2. **Allocation-Tagging-Conversion (ATC) Attribute Pipeline (`AtcAttributePipeline`)**: Dynamic voxel material/attribute slot allocation, spatial voxel tagging, and packed attribute conversions (RGBA8, Oct16 octahedral normals, RGB565, uint32 roughness/metallic) for compact GPU SSBO storage and compute raymarching.

---

## 1. Codebase Baseline & Integration Context

### 1.1 Existing Subsystem Alignment
- **`VoxelBuffer` (`storage/voxel_buffer.h/cpp`)**: 
  - $16^3$ voxel blocks with palette compaction ($74\%+$ memory reduction).
  - Channels: `CHANNEL_SDF` (0), `CHANNEL_MATERIAL` (1), `CHANNEL_COLOR` (2), `CHANNEL_CUSTOM` (3).
  - `CHANNEL_MATERIAL` stores 32-bit integer material tags or palette indices.
- **`LodOctree` (`storage/lod_octree.h/cpp`)**:
  - Pointerless SVO DAG node pool with 16-byte `SvoNode` layout (`std430` GPU SSBO compatible):
    ```cpp
    struct SvoNode {
        uint32_t child_mask = 0;       // Bits 0-7: active children
        uint32_t first_child_idx = 0;  // Index of first contiguous child
        uint32_t material_tag = 0;     // Material attribute ID / tag
        float sdf_value = 0.0f;        // Representative Signed Distance
    };
    ```
- **`VoxelRendererRD` (`rendering/voxel_renderer_rd.h/cpp`)**:
  - `Node3D` engine node using Godot's `RenderingDevice` API.
  - Manages `raymarch_shader`, `raymarch_pipeline`, `clipmap_shader`, `clipmap_pipeline`, `svo_ssbo_buffer`, and `clipmap_ssbo_buffer`.
  - Dispatches `clipmap_lod.glsl` (concentric ring center update pass) and `micro_voxel_raymarch.glsl` (stackless SVO DAG raymarch pass).

---

## 2. Concentric Clipmap LOD Pipeline Design

### 2.1 Concentric Ring Geometry & Mathematical Model
The Concentric Clipmap LOD Pipeline organizes planetary terrain into $N_{LOD}$ nested concentric rings centered around the active camera position $\mathbf{C} = (C_x, C_y, C_z)$.

- **Level Index ($k \in [0, N_{LOD}-1]$)**:
  - $k = 0$: Fine LOD near camera. Voxel scale $S_0 = 1.0$ (or micro-voxel unit).
  - $k$: Coarse LOD at expanding radius. Voxel scale $S_k = S_0 \cdot 2^k$.
- **Grid Resolution ($N_g$)**:
  - Constant grid extent per ring: $N_g \times N_g \times N_g$ voxels (default $32 \times 32 \times 32$).
- **Physical Extent ($E_k$)**:
  $$E_k = N_g \cdot S_k = 32 \cdot 2^k \text{ meters}$$
  - LOD 0: $32 \times 1 = 32\text{m}$ bounding box extent.
  - LOD 1: $32 \times 2 = 64\text{m}$ bounding box extent.
  - LOD 5: $32 \times 32 = 1024\text{m}$ bounding box extent.
  - LOD 7: $32 \times 128 = 4096\text{m}$ bounding box extent.

### 2.2 Camera Snapping & Hysteresis Updates
To prevent continuous spatial jittering and redundant GPU buffer reallocations:
1. **Grid Snapping**:
   As camera moves to $\mathbf{C}$, the ring center $\mathbf{C}_k$ is snapped to integer multiples of $S_k$:
   $$\mathbf{C}_k = \left( \lfloor C_x / S_k \rfloor \cdot S_k, \, \lfloor C_y / S_k \rfloor \cdot S_k, \, \lfloor C_z / S_k \rfloor \cdot S_k \right)$$
2. **Hysteresis Thresholding**:
   An update for ring $k$ is triggered only when the camera distance from the previous ring center exceeds a deadzone factor:
   $$\|\mathbf{C} - \mathbf{C}_k^{last}\|_{\infty} \ge \Delta_{step} \cdot S_k \quad (\text{where } \Delta_{step} = 1.0)$$

### 2.3 Smooth Ring Boundary Transitions (Distance Fade)
To eliminate visible popping across LOD ring boundaries during raymarching, a normalized distance factor $d_k(P)$ is calculated for sample point $P$ within ring $k$:
$$d_k(P) = \frac{\max(|P_x - C_{k,x}|, |P_y - C_{k,y}|, |P_z - C_{k,z}|)}{0.5 \cdot E_k}$$
When $d_k(P) > (1.0 - w_{fade})$ (where transition width $w_{fade} \approx 0.15$):
$$\alpha = \text{clamp}\left( \frac{d_k(P) - (1.0 - w_{fade})}{w_{fade}}, \, 0.0, \, 1.0 \right)$$
The raymarcher linearly interpolates Signed Distance Values and material attributes between LOD level $k$ and level $k+1$:
$$\text{SDF}_{sample} = (1.0 - \alpha) \cdot \text{SDF}_k(P) + \alpha \cdot \text{SDF}_{k+1}(P)$$

### 2.4 GPU Clipmap Storage & Upload Strategy
- **`ClipmapLevelGpu` Struct (`std430` layout, 32 bytes)**:
  ```cpp
  struct ClipmapLevelGpu {
      float center[3];       // 12 bytes
      float voxel_scale;     // 4 bytes
      int32_t grid_size[3];  // 12 bytes
      uint32_t lod_index;    // 4 bytes
  };
  ```
- **Dual Update Mechanisms**:
  1. **CPU Host Update**: `VoxelRendererRD::update_lod_clipmap(cam_pos)` computes snapped centers on CPU and calls `upload_clipmap_ssbo()` (`rd->storage_buffer_create()`).
  2. **GPU Compute Update**: `VoxelRendererRD::dispatch_clipmap_compute(cam_pos)` dispatches `clipmap_lod.glsl` compute shader to update ring centers in-place on Vulkan GPU in a single pass before raymarching.

---

## 3. Allocation-Tagging-Conversion (ATC) Attribute Pipeline

### 3.1 Overview & Concept
The Allocation-Tagging-Conversion (ATC) system enables rich surface and material property representation for planet-scale micro-voxels without incurring massive GPU VRAM overhead:
- **Allocation**: Dynamic registration and lookup of material slots containing high-precision C++ physical material parameters (albedo RGBA, normal vector, roughness, metallic, emission).
- **Tagging**: Encoding material IDs into bit-mapped tag payloads and writing them to voxel regions (`VoxelBuffer::CHANNEL_MATERIAL` or `SvoNode::material_tag`).
- **Conversion**: Bit-packing physical material attributes into compact 16-byte GPU SSBO structures (`AtcPackedGpuMaterial`) using RGBA8 color quantization, Oct16 octahedral normal encoding, RGB565 emission, and packed uint16 roughness/metallic.

### 3.2 Dynamic Material Allocation Subsystem
- **`AtcMaterialSlot` Struct**:
  ```cpp
  struct AtcMaterialSlot {
      uint32_t material_id = 0;
      Color albedo = Color(1.0f, 1.0f, 1.0f, 1.0f);
      Vector3 normal = Vector3(0.0f, 1.0f, 0.0f);
      float roughness = 0.5f;
      float metallic = 0.0f;
      Color emission = Color(0.0f, 0.0f, 0.0f, 0.0f);
  };
  ```
- **Slot Management API**:
  - `int allocate_slot(uint32_t material_id, Color albedo, float roughness, float metallic, Color emission)`
  - `bool free_slot(uint32_t material_id)`
  - `bool has_slot(uint32_t material_id) const`
  - `Dictionary get_slot_info(uint32_t material_id) const`
  - `void clear_slots()`

### 3.3 Spatial Voxel Tagging Subsystem
- **Tag Payload Generation**:
  $$\text{tag\_payload} = (\text{flags} \ll 16) \,|\, (\text{material\_id} \ \& \ 0xFFFF)$$
- **Region & Palette Tagging API**:
  - `uint32_t create_tag(uint32_t material_id, uint16_t flags)`
  - `bool tag_region(Ref<VoxelBuffer> buffer, Vector3i min_pos, Vector3i max_pos, uint32_t tag_id)`
  - `bool tag_buffer_palette(Ref<VoxelBuffer> buffer, Vector<uint32_t> material_tags)`

### 3.4 Packed Attribute Conversion & Bit-Packing Specs

1. **Albedo RGBA8 Quantization**:
   High-precision `Color(r, g, b, a)` mapped to 32-bit uint:
   $$\text{rgba8} = \left( \lfloor r \times 255 \rfloor \ll 24 \right) | \left( \lfloor g \times 255 \rfloor \ll 16 \right) | \left( \lfloor b \times 255 \rfloor \ll 8 \right) | \lfloor a \times 255 \rfloor$$
2. **Octahedral Normal Encoding (Oct16)**:
   Unit 3D vector $\mathbf{n} = (x, y, z)$ mapped to 2D square $[-1, 1]^2$:
   $$L_1 = |x| + |y| + |z|$$
   $$\mathbf{p} = \left( \frac{x}{L_1}, \, \frac{y}{L_1} \right)$$
   $$\text{If } z < 0: \quad \mathbf{p} = \left( (1 - |p_y|)\text{sign}(p_x), \, (1 - |p_x|)\text{sign}(p_y) \right)$$
   Quantized to 8-bit unsigned integers $u_8, v_8 \in [0, 255]$:
   $$\text{oct16} = (u_8 \ll 8) \,|\, v_8$$
3. **Roughness & Metallic Packing**:
   Roughness $r \in [0, 1]$ and metallic $m \in [0, 1]$ mapped to uint16 pair:
   $$\text{rm\_pack} = \left( \lfloor r \times 65535 \rfloor \ll 16 \right) | \lfloor m \times 65535 \rfloor$$
4. **Emission RGB565 Packing**:
   High-dynamic range emission color mapped to 16-bit RGB565 uint:
   $$\text{rgb565} = \left( \lfloor r \times 31 \rfloor \ll 11 \right) | \left( \lfloor g \times 63 \rfloor \ll 5 \right) | \lfloor b \times 31 \rfloor$$

### 3.5 GPU SSBO Payload Layout (`AtcPackedGpuMaterial`)
- **Struct Declaration (16 Bytes, `std430` layout)**:
  ```cpp
  struct AtcPackedGpuMaterial {
      uint32_t albedo_rgba8 = 0xFFFFFFFF;
      uint32_t normal_oct16 = 0x80800000;              // Oct16 normal in upper 16 bits
      uint32_t roughness_metallic_pack = 0x80000000;   // 16-bit roughness + 16-bit metallic
      uint32_t emission_rgb565 = 0;                    // 16-bit RGB565 emission + reserved
  };
  ```
- **Serialization API**:
  - `PackedByteArray export_gpu_material_buffer() const`: Serializes allocated material slots into a contiguous byte buffer of 16-byte `AtcPackedGpuMaterial` elements for upload to Vulkan `RenderingDevice` SSBO (`atc_materials_buffer`).

---

## 4. `AtcAttributePipeline` Complete C++ Class Specification

Below is the complete, self-contained specification for `modules/godot_eden/rendering/atc_attribute_pipeline.h`:

```cpp
/**************************************************************************/
/*  atc_attribute_pipeline.h                                             */
/**************************************************************************/

#pragma once

#include "core/math/color.h"
#include "core/math/vector3.h"
#include "core/math/vector3i.h"
#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "core/variant/dictionary.h"
#include "core/variant/packed_byte_array.h"
#include "modules/godot_eden/storage/voxel_buffer.h"

struct AtcMaterialSlot {
	uint32_t material_id = 0;
	Color albedo = Color(1.0f, 1.0f, 1.0f, 1.0f);
	Vector3 normal = Vector3(0.0f, 1.0f, 0.0f);
	float roughness = 0.5f;
	float metallic = 0.0f;
	Color emission = Color(0.0f, 0.0f, 0.0f, 0.0f);
};

struct AtcPackedGpuMaterial {
	uint32_t albedo_rgba8 = 0xFFFFFFFF;
	uint32_t normal_oct16 = 0x80800000;
	uint32_t roughness_metallic_pack = 0x80000000;
	uint32_t emission_rgb565 = 0;
};

class AtcAttributePipeline : public RefCounted {
	GDCLASS(AtcAttributePipeline, RefCounted);

private:
	HashMap<uint32_t, AtcMaterialSlot> material_slots;

protected:
	static void _bind_methods();

public:
	AtcAttributePipeline();
	~AtcAttributePipeline();

	// Dynamic Allocation Subsystem
	int allocate_slot(uint32_t p_material_id, const Color &p_albedo, float p_roughness = 0.5f, float p_metallic = 0.0f, const Color &p_emission = Color(0, 0, 0, 0));
	bool free_slot(uint32_t p_material_id);
	bool has_slot(uint32_t p_material_id) const;
	Dictionary get_slot_info(uint32_t p_material_id) const;
	int get_allocated_slot_count() const;
	void clear_slots();

	// Spatial Tagging Subsystem
	uint32_t create_tag(uint32_t p_material_id, uint16_t p_flags = 0) const;
	bool tag_region(const Ref<VoxelBuffer> &p_buffer, const Vector3i &p_min, const Vector3i &p_max, uint32_t p_tag_id);
	bool tag_buffer_palette(const Ref<VoxelBuffer> &p_buffer, const Vector<uint32_t> &p_material_tags);

	// Conversion & Bit-Packing Utilities
	static uint32_t pack_rgba8(const Color &p_color);
	static Color unpack_rgba8(uint32_t p_packed);

	static uint32_t pack_octahedral_normal(const Vector3 &p_normal);
	static Vector3 unpack_octahedral_normal(uint32_t p_packed);

	static uint32_t pack_roughness_metallic(float p_roughness, float p_metallic);
	static Vector2 unpack_roughness_metallic(uint32_t p_packed);

	static uint32_t pack_rgb565(const Color &p_color);
	static Color unpack_rgb565(uint32_t p_packed);

	// GPU SSBO Export
	PackedByteArray export_gpu_material_buffer() const;
};
```

---

## 5. Integration Verification & Build Requirements

1. **Build Hooks (`modules/godot_eden/SCsub`)**:
   Ensure `rendering/atc_attribute_pipeline.cpp` and `rendering/voxel_renderer_rd.cpp` are compiled:
   ```python
   env_godot_eden.add_source_files(sources, "rendering/*.cpp")
   ```
2. **ClassDB Registration (`modules/godot_eden/register_types.cpp`)**:
   ```cpp
   ClassDB::register_class<AtcAttributePipeline>();
   ClassDB::register_class<VoxelRendererRD>();
   ```
3. **Unit Tests (`modules/godot_eden/tests/test_main.h`)**:
   Add test cases verifying RGBA8 packing, Oct16 normal roundtrip, material slot allocation, and GPU SSBO export payload size (`slots.size() * 16` bytes).

---
