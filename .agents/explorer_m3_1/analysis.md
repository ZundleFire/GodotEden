# Feature 9 Analysis Report: Micro-Voxel Renderer Architecture (`VoxelRendererRD`)

**Author**: Explorer 1 (Milestone 3)  
**Date**: 2026-08-05  
**Target Class**: `VoxelRendererRD` (`modules/godot_eden/rendering/voxel_renderer_rd.h` / `voxel_renderer_rd.cpp`)  
**Target Module**: `modules/godot_eden`  

---

## 1. Executive Summary

Feature 9 (`VoxelRendererRD`) provides the core Vulkan GPU Compute Raymarching pipeline for GodotEden's planet-scale micro-voxel engine. It bridges Godot 4's `RenderingDevice` (RD) Vulkan compute abstraction with the storage and streaming subsystems constructed in M1 and M2 (`LodOctree` SVO DAG, `VoxelBuffer` palette compaction, `VoxelDataMap` spatial hashing, and `VoxelVolume`).

This report provides a comprehensive architectural analysis and exact C++ specifications for `VoxelRendererRD`, detailing:
- The alignment and data layouts required to interface C++ storage structures directly with Vulkan `std430` SSBO buffers and push constants.
- The interface contracts for binding `LodOctree` SVO pools and `VoxelDataMap` near-LOD clipmap rings to Vulkan compute pipelines.
- Robust headless execution and null-safety patterns for Godot's unit test runner (`godot --test`) where `RenderingDevice` singleton may be `nullptr`.
- The exact proposed C++ header declaration (`rendering/voxel_renderer_rd.h`), along with necessary SCons `SCsub` and `register_types.cpp` integration details.

---

## 2. Analysis of Existing M1/M2 Storage Subsystem Interfacing

### 2.1 Far-LOD Micro-Voxel Storage (`LodOctree`)
In M2, `LodOctree` (defined in `storage/lod_octree.h`) was built to hold pointerless Sparse Voxel Octree (SVO) Directed Acyclic Graph (DAG) node pools:
- **`SvoNode` struct**:
  ```cpp
  struct SvoNode {
      uint32_t child_mask = 0;       // Bits 0-7: active child mask
      uint32_t first_child_idx = 0;  // Index in pool of first contiguous child (0 if leaf)
      uint32_t material_tag = 0;     // Material attribute ID / material tag
      float sdf_value = 0.0f;        // Representative Signed Distance Function value
  };
  ```
- **GPU Compatibility**: Each `SvoNode` is precisely 16 bytes (four 32-bit fields), satisfying Vulkan GLSL `std430` alignment rules without padding.
- **SSBO Data Export**: `LodOctree::get_ssbo_buffer_bytes()` exports the internal `Vector<SvoNode>` pool directly into a `PackedByteArray`.
- **GPU Buffer Lifecycle in `VoxelRendererRD`**:
  `VoxelRendererRD` will create a storage buffer via `RenderingDevice::storage_buffer_create(ssbo_bytes.size(), ssbo_bytes)` and update it whenever `LodOctree` updates or is reassigned.

### 2.2 Near-LOD Palette-Compacted Storage (`VoxelDataMap` & `VoxelBuffer`)
In M2, `VoxelDataMap` holds spatial hash maps of active $16^3$ voxel blocks (`Ref<VoxelBuffer>`):
- **`VoxelBuffer` layout**: Supports SDF, Material, Color, and Custom channels with Palette Compaction (16 entries per block, achieving 74%+ memory reduction).
- **Clipmap Binding Strategy**:
  Concentric clipmap LOD rings center on the camera. `VoxelRendererRD` translates camera position into concentric grid center coordinates and manages clipmap level metadata. Near-LOD blocks are transferred to GPU storage buffers or 3D textures for fast compute shader sampling during raymarching.

### 2.3 Existing Node Interfaces (`VoxelRenderer`, `VoxelWorld`, `VoxelVolume`)
- `nodes/voxel_renderer.h` provides high-level property wrappers (`volume`, `lod_levels`, `view_distance`, `wireframe`).
- `nodes/voxel_volume.h` provides spatial volume configurations (`chunk_size`, `max_lod_levels`).
- `VoxelRendererRD` extends `Node3D` (or inherits/pairs with `VoxelRenderer`) to manage low-level Vulkan compute state while accepting `Ref<VoxelVolume>`, `Ref<LodOctree>`, and `Ref<VoxelDataMap>` bindings.

---

## 3. Vulkan `RenderingDevice` Integration Architecture

### 3.1 Shader Compilation & Header Inclusion
Godot 4 uses SCons `RD_GLSL` builders to convert GLSL compute shaders into C++ headers.
- Shaders: `shaders/micro_voxel_raymarch.glsl` and `shaders/clipmap_lod.glsl`.
- Generated headers: `shaders/micro_voxel_raymarch.glsl.gen.h` and `shaders/clipmap_lod.glsl.gen.h`.
- In `VoxelRendererRD`, pipeline initialization instantiates `RDShaderFile` from the generated SPIR-V byte arrays:
  ```cpp
  #include "shaders/micro_voxel_raymarch.glsl.gen.h"
  #include "shaders/clipmap_lod.glsl.gen.h"
  ```

### 3.2 Headless Execution & Null-Safety
When running unit tests via `godot --headless --test`, no Vulkan physical device is initialized, meaning `RenderingDevice::get_singleton()` returns `nullptr` (or a dummy renderer).
- **Hard Requirement**: All calls to `RenderingDevice` methods inside `VoxelRendererRD` MUST be guarded with `if (!is_rd_available()) return;`.
- `is_rd_available()` checks:
  ```cpp
  bool VoxelRendererRD::is_rd_available() const {
      RenderingDevice *rd = RenderingDevice::get_singleton();
      return rd != nullptr;
  }
  ```
- This ensures test suites (`test_main.h`, E2E scripts) can instantiate `VoxelRendererRD`, test property getters/setters, attach `VoxelVolume`/`LodOctree` resources, and verify bounds without runtime crashes.

---

## 4. Exact GPU Data Structures & Alignment

### 4.1 C++ / GLSL Struct Alignment Table

| Structure | Usage | C++ Field Types | Total Size | GLSL Standard Alignment |
|-----------|-------|-----------------|------------|-------------------------|
| `SvoNode` | SVO SSBO | `uint32_t child_mask`<br>`uint32_t first_child_idx`<br>`uint32_t material_tag`<br>`float sdf_value` | 16 Bytes | `std430` 16-byte alignment |
| `ClipmapLevelGpu` | Clipmap SSBO | `float center[3]` (12B)<br>`float voxel_scale` (4B)<br>`int32_t grid_size[3]` (12B)<br>`uint32_t lod_index` (4B) | 32 Bytes | `std430` 16-byte struct alignment |
| `RaymarchPushConstants` | Compute Raymarch Push Constant | `float camera_pos[3]` (12B)<br>`float fov` (4B)<br>`float camera_dir[3]` (12B)<br>`float max_distance` (4B)<br>`float screen_size[2]` (8B) | 40 Bytes | Vulkan 4-byte scalar / 16-byte vector push constant block |
| `ClipmapPushConstants` | Clipmap Compute Push Constant | `float camera_pos[3]` (12B)<br>`uint32_t max_lod_levels` (4B) | 16 Bytes | Vulkan 16-byte push constant block |

### 4.2 C++ Definitions for Header
```cpp
// GPU SSBO Struct for Clipmap Ring Metadata (std430 32-byte alignment)
struct ClipmapLevelGpu {
	float center[3] = { 0.0f, 0.0f, 0.0f };
	float voxel_scale = 1.0f;
	int32_t grid_size[3] = { 32, 32, 32 };
	uint32_t lod_index = 0;
};

// GPU Push Constant layout matching GLSL compute shaders (40-byte alignment)
struct RaymarchPushConstants {
	float camera_pos[3] = { 0.0f, 0.0f, 0.0f };
	float fov = 0.785398f; // ~45 degrees in rad
	float camera_dir[3] = { 0.0f, 0.0f, -1.0f };
	float max_distance = 1000.0f;
	float screen_size[2] = { 1920.0f, 1080.0f };
};

struct ClipmapPushConstants {
	float camera_pos[3] = { 0.0f, 0.0f, 0.0f };
	uint32_t max_lod_levels = 8;
};
```

---

## 5. Compute Pipeline & Uniform Binding Layout

### 5.1 Set 0 Uniform Bindings (`micro_voxel_raymarch.glsl`)
- **Binding 0**: `layout(set = 0, binding = 0, rgba32f) uniform image2D out_color;`  
  *Type*: `RD::UNIFORM_TYPE_IMAGE`  
  *Target*: Render target color texture (`RID render_target_image`).
- **Binding 1**: `layout(set = 0, binding = 1, std430) readonly buffer SvoBuffer { SvoNode nodes[]; } svo_dag;`  
  *Type*: `RD::UNIFORM_TYPE_STORAGE_BUFFER`  
  *Target*: SVO DAG pool buffer (`RID svo_ssbo_buffer`).
- **Binding 2**: `layout(set = 0, binding = 2, std430) readonly buffer ClipmapBuffer { ClipmapLevel levels[]; } clipmap;`  
  *Type*: `RD::UNIFORM_TYPE_STORAGE_BUFFER`  
  *Target*: Concentric clipmap metadata buffer (`RID clipmap_ssbo_buffer`).

### 5.2 Compute Workgroup Dispatch Logic
```cpp
Vector2i screen = render_target_size;
int workgroup_x = (screen.x + 7) / 8;
int workgroup_y = (screen.y + 7) / 8;
int workgroup_z = 1;

int64_t compute_list = rd->compute_list_begin();
rd->compute_list_bind_compute_pipeline(compute_list, raymarch_pipeline);
rd->compute_list_bind_uniform_set(compute_list, raymarch_uniform_set, 0);
rd->compute_list_set_push_constant(compute_list, &push_constants, sizeof(RaymarchPushConstants));
rd->compute_list_dispatch(compute_list, workgroup_x, workgroup_y, workgroup_z);
rd->compute_list_end();
```

---

## 6. Complete C++ Header Declaration: `modules/godot_eden/rendering/voxel_renderer_rd.h`

Below is the complete production-grade C++ header declaration to be created by Implementer 1 at `modules/godot_eden/rendering/voxel_renderer_rd.h`:

```cpp
/**************************************************************************/
/*  voxel_renderer_rd.h                                                   */
/**************************************************************************/

#pragma once

#include "core/object/ref_counted.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"
#include "nodes/voxel_volume.h"
#include "scene/3d/node_3d.h"
#include "servers/rendering/rendering_device.h"
#include "storage/lod_octree.h"
#include "storage/voxel_data_map.h"

// GPU SSBO Struct for Clipmap Ring Metadata (std430 32-byte alignment)
struct ClipmapLevelGpu {
	float center[3] = { 0.0f, 0.0f, 0.0f };
	float voxel_scale = 1.0f;
	int32_t grid_size[3] = { 32, 32, 32 };
	uint32_t lod_index = 0;
};

// GPU Push Constant layout matching GLSL compute shaders (40-byte alignment)
struct RaymarchPushConstants {
	float camera_pos[3] = { 0.0f, 0.0f, 0.0f };
	float fov = 0.785398f; // ~45 degrees in rad
	float camera_dir[3] = { 0.0f, 0.0f, -1.0f };
	float max_distance = 1000.0f;
	float screen_size[2] = { 1920.0f, 1080.0f };
};

struct ClipmapPushConstants {
	float camera_pos[3] = { 0.0f, 0.0f, 0.0f };
	uint32_t max_lod_levels = 8;
};

class VoxelRendererRD : public Node3D {
	GDCLASS(VoxelRendererRD, Node3D);

private:
	Ref<VoxelVolume> volume;
	Ref<LodOctree> svo_octree;
	Ref<VoxelDataMap> data_map;

	bool enabled = true;
	int lod_levels = 6;
	float view_distance = 1000.0f;
	Vector2i render_target_size = Vector2i(1920, 1080);
	float fov = 0.785398f;

	// Vulkan RenderingDevice Resources (RIDs)
	RID raymarch_shader;
	RID raymarch_pipeline;
	RID clipmap_shader;
	RID clipmap_pipeline;

	RID svo_ssbo_buffer;
	RID clipmap_ssbo_buffer;
	RID render_target_image;
	RID raymarch_uniform_set;
	RID clipmap_uniform_set;

	bool pipelines_initialized = false;
	Vector<ClipmapLevelGpu> clipmap_levels;

	void _cleanup_rd_resources();
	void _init_rd_pipelines();
	void _update_clipmap_buffer(const Vector3 &p_camera_pos);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	VoxelRendererRD();
	~VoxelRendererRD();

	// Volume, SVO & DataMap Interface Binding
	void set_volume(const Ref<VoxelVolume> &p_volume);
	Ref<VoxelVolume> get_volume() const;

	void set_svo_octree(const Ref<LodOctree> &p_octree);
	Ref<LodOctree> get_svo_octree() const;

	void set_data_map(const Ref<VoxelDataMap> &p_data_map);
	Ref<VoxelDataMap> get_data_map() const;

	// Configuration Properties
	void set_enabled(bool p_enabled);
	bool is_enabled() const;

	void set_lod_levels(int p_levels);
	int get_lod_levels() const;

	void set_view_distance(float p_distance);
	float get_view_distance() const;

	void set_render_target_size(const Vector2i &p_size);
	Vector2i get_render_target_size() const;

	void set_fov(float p_fov);
	float get_fov() const;

	// Buffer Updates & GPU Upload Interfaces
	bool upload_svo_ssbo();
	bool upload_clipmap_ssbo();
	void update_lod_clipmap(const Vector3 &p_camera_position);

	// Compute Shader Execution / Dispatch
	void dispatch_raymarch_compute(RID p_render_target_override = RID());
	void dispatch_clipmap_compute(const Vector3 &p_camera_pos);

	// Diagnostic & Resource Queries
	bool is_rd_available() const;
	bool is_pipeline_ready() const;
	Dictionary get_gpu_buffer_info() const;
	void clear_gpu_resources();

	PackedStringArray get_configuration_warnings() const override;
};
```

---

## 7. Godot Module Build & Registration Updates

### 7.1 `SCsub` Updates
`modules/godot_eden/SCsub` requires updating to compile the new `rendering/` subdirectory:
```python
# Module subdirectories
env_godot_eden.add_source_files(sources, "nodes/*.cpp")
env_godot_eden.add_source_files(sources, "storage/*.cpp")
env_godot_eden.add_source_files(sources, "streaming/*.cpp")
env_godot_eden.add_source_files(sources, "generators/*.cpp")
env_godot_eden.add_source_files(sources, "rendering/*.cpp")  # NEW FOR M3
```

### 7.2 `register_types.cpp` ClassDB Registration
Include header and register class under `MODULE_INITIALIZATION_LEVEL_SCENE`:
```cpp
#include "rendering/voxel_renderer_rd.h"

void initialize_godot_eden_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		// ... existing registrations ...
		GDREGISTER_CLASS(VoxelRendererRD);
	}
}
```

---

## 8. Verification Strategy & Doctest Integration

To verify `VoxelRendererRD` without requiring hardware Vulkan support in unit tests:
1. **ClassDB & Inheritance Unit Tests**: Add test cases to `modules/godot_eden/tests/test_main.h`:
   - `ClassDB::class_exists("VoxelRendererRD")` is `true`.
   - `ClassDB::is_parent_class("VoxelRendererRD", "Node3D")` is `true`.
2. **Property & Resource Binding Tests**:
   - Instantiate `VoxelRendererRD *renderer = memnew(VoxelRendererRD);`.
   - Verify defaults (`enabled == true`, `lod_levels == 6`, `view_distance == 1000.0f`).
   - Test setters/getters and bounds clamping (`set_lod_levels(-5)` clamps to `1`, `set_view_distance(-10)` clamps to `0.0f`).
   - Attach `VoxelVolume`, `LodOctree`, and `VoxelDataMap` resources; verify getters.
   - Call `update_lod_clipmap(Vector3(100, 0, 0))` and `upload_svo_ssbo()`; verify graceful return when `is_rd_available()` is false.
   - Safely free `memdelete(renderer);`.

---

## 9. Conclusion & Actionable Handoff

The architecture defined herein ensures seamless memory compatibility between M1/M2 voxel storage structures (`LodOctree`, `VoxelBuffer`, `VoxelDataMap`) and Vulkan `RenderingDevice` compute shaders. Implementation can proceed immediately based on this specification.
