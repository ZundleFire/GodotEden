# Handoff Report: Milestone 3 — Concentric Clipmap LOD Pipeline & ATC Attribute System

## 1. Observation
Direct observations from the GodotEden codebase and specification documents (`PROJECT.md`, `SCOPE.md`, `modules/godot_eden/`):

- **Module Layout & Build Hooks (`modules/godot_eden/SCsub`)**:
  - `SCsub` currently compiles GLSL compute shaders `shaders/micro_voxel_raymarch.glsl` and `shaders/clipmap_lod.glsl` using `env_godot_eden.RD_GLSL()`.
  - Header files generated: `shaders/clipmap_lod.glsl.gen.h` and `shaders/micro_voxel_raymarch.glsl.gen.h`.
  - C++ source files collected from `*.cpp`, `nodes/*.cpp`, `storage/*.cpp`, `streaming/*.cpp`, `generators/*.cpp`.
  - `rendering/*.cpp` must be added to `SCsub` to build `rendering/voxel_renderer_rd.cpp` and `rendering/atc_attribute_pipeline.cpp`.

- **Existing Storage Data Structures (`modules/godot_eden/storage/`)**:
  - `VoxelBuffer` (`storage/voxel_buffer.h:16-58`):
    - `BLOCK_SIZE = 16` ($16^3 = 4096$ voxels).
    - 4 channels: `CHANNEL_SDF` (0), `CHANNEL_MATERIAL` (1), `CHANNEL_COLOR` (2), `CHANNEL_CUSTOM` (3).
    - 3 compression modes: `COMPRESSION_UNIFORM` (0), `COMPRESSION_PALETTE` (1, max 16 entries, 4-bit nibbles, 2048 bytes), `COMPRESSION_RAW` (2, 4096 32-bit values).
  - `LodOctree` (`storage/lod_octree.h:16-21`):
    - `SvoNode`: 16-byte aligned struct (`uint32_t child_mask`, `uint32_t first_child_idx`, `uint32_t material_tag`, `float sdf_value`).
    - Provides `get_ssbo_buffer_bytes()` for Vulkan GPU SSBO upload.
  - `VoxelDataMap` (`storage/voxel_data_map.h:14-43`):
    - Thread-safe `HashMap<Vector3i, Ref<VoxelBuffer>>` with `RWLock`.

- **Existing Nodes & Registrations (`modules/godot_eden/nodes/`, `register_types.cpp`)**:
  - `VoxelRenderer` (`nodes/voxel_renderer.h:11-48`): Node3D wrapper with properties `volume`, `enabled`, `lod_levels` (default 6), `view_distance` (default 1000.0). `update_lod(const Vector3 &p_camera_position)` stub.
  - `VoxelWorld` (`nodes/voxel_world.h:12-46`): Node3D wrapper with `voxel_volume`, `voxel_size`, `view_distance_chunks`, `enable_collision`.
  - `register_types.cpp:22-38`: ClassDB bindings currently cover nodes, storage, streaming, and generators. `VoxelRendererRD` and `AtcAttributePipeline` must be added.

- **Existing Shaders (`modules/godot_eden/shaders/`)**:
  - `clipmap_lod.glsl`:
    - Local size `(64, 1, 1)`.
    - `ClipmapLevel` layout: `vec3 center; float voxel_scale; ivec3 grid_size; uint lod_index;`.
    - Push constants: `camera_pos` (vec3), `max_lod_levels` (uint).
    - Snapped center calculation: `floor(camera_pos / scale) * scale`.
  - `micro_voxel_raymarch.glsl`:
    - Local size `(8, 8, 1)`.
    - Ray calculation stub writing RGBA color to `out_color` image.

---

## 2. Logic Chain

### 2.1 Concentric Clipmap LOD Pipeline Logic
1. **Scale Hierarchy**:
   - Voxel grids at LOD level $L \in [0, M-1]$ have cell size $s_L = s_0 \cdot 2^L$, where $s_0$ is the base voxel scale (e.g., $0.25\text{m}$).
   - Ring grid extent $N \times N \times N$ (e.g. $32 \times 32 \times 32$). The extent of ring $L$ in world units is $R_L = N \cdot s_L$.
2. **Camera-Centered Snapping & Ring Shifting**:
   - Camera position $C = (x_c, y_c, z_c)$.
   - Snapped origin for ring $L$: $O_L = \text{floor}(C / s_L) \cdot s_L$.
   - Axis-aligned bounding box for ring $L$: $\text{AABB}_L = [O_L - \frac{N}{2} s_L, \, O_L + \frac{N}{2} s_L]$.
   - When camera moves by $\Delta C$, origin shifts by $\Delta O_L = O_L - O_{L, \text{prev}}$.
   - If $\Delta O_L \neq (0,0,0)$, only slices entering the new AABB along shifted axes are updated, avoiding full buffer re-generation.
3. **Chebyshev Distance LOD Query & Level Selection**:
   - Distance from camera to point $P$: $d_\infty(P, C) = \max(|P_x - C_x|, |P_y - C_y|, |P_z - C_z|)$.
   - Chebyshev norm guarantees square box ring boundaries matching grid geometry.
   - Ring $L$ applies when $d_\infty \in [R_{L-1, \text{outer}}, \, R_{L, \text{outer}}]$, where $R_{L, \text{outer}} = \frac{N}{2} s_L$.
4. **Seamless LOD Transition & SDF Cross-Fading**:
   - At ring boundary $R_{\text{outer}} = \frac{N}{2} s_L$, define blend margin width $W = \beta \cdot R_{\text{outer}}$ (with $\beta = 0.15$).
   - Transition blend factor $\alpha$:
     $$\alpha = \text{clamp}\left( \frac{d_\infty - (R_{\text{outer}} - W)}{W}, \, 0.0, \, 1.0 \right)$$
   - Smoothstep curve: $t = \alpha^2 (3 - 2\alpha)$.
   - Blended Signed Distance Value:
     $$\Phi_{\text{blended}}(P) = (1 - t) \cdot \Phi_L(P) + t \cdot \Phi_{L+1}(P)$$
   - At boundary $L_{\text{clip\_max}}$ between active voxel grids and far SVO DAG volumes (`LodOctree`):
     $$\Phi_{\text{blended}}(P) = (1 - t) \cdot \Phi_{\text{clipmap}}(P) + t \cdot \Phi_{\text{svo}}(P)$$

### 2.2 ATC (Allocation-Tagging-Conversion) System Logic
1. **Allocation Phase**:
   - Manages a 256-slot global GPU material palette (`GpuMaterialData`).
   - Assigns unique 8-bit material indices to registered material descriptors (`AtcMaterialEntry`).
   - Local voxel blocks (`VoxelBuffer`) compact 4096 voxel material IDs into 16-entry local palettes (4-bit nibble storage, 2048 bytes).
2. **Tagging Phase**:
   - Encodes material attributes into 32-bit voxel channel `CHANNEL_MATERIAL`.
   - Layout: Bits 0-7: Material ID ($0..255$), Bits 8-23: Octahedral normal `oct16`, Bits 24-31: Roughness/Metallic modifier.
3. **Conversion Phase**:
   - `AtcAttributePipeline` converts material attributes to Vulkan `std430` SSBO layout (32 bytes per material entry).
   - Packs Albedo RGB to `uint32_t` (`0xAARRGGBB`).
   - Normal Encoding (`oct16`): Maps 3D unit normal $(x,y,z)$ to $L_1$ octahedral projection $(u,v) \in [-1,1]^2$, quantized to 2x 8-bit unsigned bytes. Packs 12-byte normal vector into 2 bytes (83.3% memory reduction).
   - Dynamic SSBO upload via Godot `RenderingDevice::buffer_update()`.

---

## 3. Caveats
- **Hardware Constraints**: GPU compute raymarching requires Vulkan RD (`RenderingDevice`). In Godot headless test runners without active GPU context, fallback CPU dummy handle paths must be provided to prevent crashes during automated doctests.
- **Max Clipmap Rings**: System defaults to 8 clipmap levels ($L=0 \dots 7$). Scaling beyond 8 levels is constrained by GPU storage buffer allocation and push constant limits.
- **Max Global Materials**: Global GPU palette is sized at 256 materials (1 byte tag in SVO nodes and clipmap voxels), requiring 8 KB SSBO size ($256 \times 32$ bytes).

---

## 4. Conclusion & Technical Design Specifications

### 4.1 Concentric Clipmap Data Structures & GLSL Interfaces

#### GLSL Compute Shader Update (`shaders/clipmap_lod.glsl`)
```glsl
#[compute]
#version 450

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct ClipmapLevel {
	vec3 center;
	float voxel_scale;
	ivec3 grid_size;
	uint lod_index;
	ivec3 toroidal_offset;
	float blend_margin;
};

layout(set = 0, binding = 0, std430) buffer ClipmapBuffer {
	ClipmapLevel levels[];
} clipmap;

layout(push_constant) uniform PushConstants {
	vec3 camera_pos;
	uint max_lod_levels;
	float base_voxel_scale;
	uint grid_extent;
} params;

void main() {
	uint id = gl_GlobalInvocationID.x;
	if (id >= params.max_lod_levels) {
		return;
	}

	float scale = params.base_voxel_scale * pow(2.0, float(id));
	vec3 snapped_center = floor(params.camera_pos / scale) * scale;

	clipmap.levels[id].center = snapped_center;
	clipmap.levels[id].voxel_scale = scale;
	clipmap.levels[id].grid_size = ivec3(int(params.grid_extent));
	clipmap.levels[id].lod_index = id;
	
	// Calculate toroidal grid offset from camera displacement
	ivec3 grid_cell = ivec3(floor(snapped_center / scale));
	clipmap.levels[id].toroidal_offset = grid_cell % ivec3(params.grid_extent);
	
	// Blend margin: 15% of outer ring radius
	float outer_radius = float(params.grid_extent) * 0.5 * scale;
	clipmap.levels[id].blend_margin = outer_radius * 0.15;
}
```

### 4.2 ATC Attribute & Material System (`AtcAttributePipeline`)

#### Header File (`modules/godot_eden/rendering/atc_attribute_pipeline.h`)
```cpp
/**************************************************************************/
/*  atc_attribute_pipeline.h                                              */
/**************************************************************************/

#pragma once

#include "core/math/color.h"
#include "core/math/vector3.h"
#include "core/object/ref_counted.h"
#include "core/templates/vector.h"
#include "core/variant/packed_byte_array.h"
#include "servers/rendering_server.h"

// GPU SSBO std430 Material Structure (32 Bytes per entry)
struct GpuMaterialData {
	uint32_t albedo_rgba8 = 0xFFFFFFFF;       // Packed RGBA 8-bit channels
	uint32_t normal_oct16 = 0x7F7F;          // Octahedral 16-bit normal (u8, v8)
	uint32_t roughness_metallic = 0x8000;    // Packed u16 roughness + u16 metallic
	uint32_t emissive_flags = 0;             // Packed u16 emissive + u16 material flags
	float u_scale = 1.0f;                    // Triplanar U scale
	float v_scale = 1.0f;                    // Triplanar V scale
	uint32_t texture_index = 0;              // Bindless texture array index
	uint32_t reserved = 0;                   // 16-byte alignment padding
};

struct AtcMaterialEntry {
	Color albedo = Color(1.0f, 1.0f, 1.0f, 1.0f);
	Vector3 normal = Vector3(0.0f, 1.0f, 0.0f);
	float roughness = 0.5f;
	float metallic = 0.0f;
	float emissive = 0.0f;
	uint32_t flags = 0;
	float u_scale = 1.0f;
	float v_scale = 1.0f;
	uint32_t texture_index = 0;
};

class AtcAttributePipeline : public RefCounted {
	GDCLASS(AtcAttributePipeline, RefCounted);

public:
	static const uint32_t MAX_GLOBAL_MATERIALS = 256;

private:
	Vector<AtcMaterialEntry> materials;
	Vector<GpuMaterialData> gpu_materials;
	bool dirty = true;
	RID gpu_ssbo_rid;

protected:
	static void _bind_methods();

public:
	AtcAttributePipeline();
	~AtcAttributePipeline();

	// Allocation Phase
	uint32_t register_material(const AtcMaterialEntry &p_entry);
	bool set_material(uint32_t p_id, const AtcMaterialEntry &p_entry);
	AtcMaterialEntry get_material(uint32_t p_id) const;
	uint32_t get_material_count() const;

	// Tagging Phase & Octahedral Normal Math
	static uint32_t encode_normal_oct16(const Vector3 &p_normal);
	static Vector3 decode_normal_oct16(uint32_t p_oct16);
	static uint32_t pack_material_tag(uint8_t p_material_id, const Vector3 &p_normal, float p_roughness);
	static void unpack_material_tag(uint32_t p_tag, uint8_t &r_material_id, Vector3 &r_normal, float &r_roughness);

	// Conversion & GPU Buffer Phase
	void update_conversion_buffers();
	PackedByteArray get_gpu_material_ssbo_bytes() const;
	RID get_gpu_ssbo_rid() const { return gpu_ssbo_rid; }
	void update_gpu_device(RenderingDevice *p_rd);

	void clear();
};
```

#### Implementation File (`modules/godot_eden/rendering/atc_attribute_pipeline.cpp`)
```cpp
/**************************************************************************/
/*  atc_attribute_pipeline.cpp                                            */
/**************************************************************************/

#include "rendering/atc_attribute_pipeline.h"
#include "core/math/math_funcs.h"

void AtcAttributePipeline::_bind_methods() {
	ClassDB::bind_method(D_METHOD("register_material", "albedo", "roughness", "metallic", "emissive"), &AtcAttributePipeline::_register_material_bind);
	ClassDB::bind_method(D_METHOD("get_material_count"), &AtcAttributePipeline::get_material_count);
	ClassDB::bind_method(D_METHOD("encode_normal_oct16", "normal"), &AtcAttributePipeline::encode_normal_oct16);
	ClassDB::bind_method(D_METHOD("decode_normal_oct16", "oct16"), &AtcAttributePipeline::decode_normal_oct16);
	ClassDB::bind_method(D_METHOD("pack_material_tag", "material_id", "normal", "roughness"), &AtcAttributePipeline::pack_material_tag);
	ClassDB::bind_method(D_METHOD("clear"), &AtcAttributePipeline::clear);
}

AtcAttributePipeline::AtcAttributePipeline() {
	materials.resize(MAX_GLOBAL_MATERIALS);
	gpu_materials.resize(MAX_GLOBAL_MATERIALS);
	
	// Initialize default fallback material (ID 0)
	AtcMaterialEntry default_mat;
	materials.write[0] = default_mat;
	update_conversion_buffers();
}

AtcAttributePipeline::~AtcAttributePipeline() {
	clear();
}

uint32_t AtcAttributePipeline::register_material(const AtcMaterialEntry &p_entry) {
	for (int i = 0; i < materials.size(); i++) {
		if (i >= (int)get_material_count()) {
			materials.write[i] = p_entry;
			dirty = true;
			return (uint32_t)i;
		}
	}
	return 0; // Fallback to index 0 if full
}

uint32_t AtcAttributePipeline::encode_normal_oct16(const Vector3 &p_normal) {
	Vector3 n = p_normal.normalized();
	float l1 = Math::abs(n.x) + Math::abs(n.y) + Math::abs(n.z);
	if (l1 < 0.0001f) {
		return 0x7F7F; // Default upward normal (0, 1, 0)
	}
	
	float u = n.x / l1;
	float v = n.y / l1;
	if (n.z < 0.0f) {
		float u_old = u;
		u = (1.0f - Math::abs(v)) * (u_old >= 0.0f ? 1.0f : -1.0f);
		v = (1.0f - Math::abs(u_old)) * (v >= 0.0f ? 1.0f : -1.0f);
	}
	
	uint32_t u_byte = (uint32_t)Math::clamp((int)Math::round((u * 0.5f + 0.5f) * 255.0f), 0, 255);
	uint32_t v_byte = (uint32_t)Math::clamp((int)Math::round((v * 0.5f + 0.5f) * 255.0f), 0, 255);
	
	return u_byte | (v_byte << 8);
}

Vector3 AtcAttributePipeline::decode_normal_oct16(uint32_t p_oct16) {
	float u = ((float)(p_oct16 & 0xFF) / 255.0f) * 2.0f - 1.0f;
	float v = ((float)((p_oct16 >> 8) & 0xFF) / 255.0f) * 2.0f - 1.0f;
	
	float z = 1.0f - Math::abs(u) - Math::abs(v);
	Vector3 n;
	if (z < 0.0f) {
		n.x = (1.0f - Math::abs(v)) * (u >= 0.0f ? 1.0f : -1.0f);
		n.y = (1.0f - Math::abs(u)) * (v >= 0.0f ? 1.0f : -1.0f);
		n.z = z;
	} else {
		n = Vector3(u, v, z);
	}
	return n.normalized();
}

uint32_t AtcAttributePipeline::pack_material_tag(uint8_t p_material_id, const Vector3 &p_normal, float p_roughness) {
	uint32_t oct = encode_normal_oct16(p_normal);
	uint32_t rough_byte = (uint32_t)Math::clamp((int)Math::round(p_roughness * 255.0f), 0, 255);
	
	return (uint32_t)p_material_id | (oct << 8) | (rough_byte << 24);
}

void AtcAttributePipeline::unpack_material_tag(uint32_t p_tag, uint8_t &r_material_id, Vector3 &r_normal, float &r_roughness) {
	r_material_id = (uint8_t)(p_tag & 0xFF);
	uint32_t oct = (p_tag >> 8) & 0xFFFF;
	r_normal = decode_normal_oct16(oct);
	r_roughness = (float)((p_tag >> 24) & 0xFF) / 255.0f;
}

void AtcAttributePipeline::update_conversion_buffers() {
	if (!dirty) {
		return;
	}
	
	for (int i = 0; i < (int)MAX_GLOBAL_MATERIALS; i++) {
		const AtcMaterialEntry &src = materials[i];
		GpuMaterialData &dst = gpu_materials.write[i];
		
		uint32_t r = (uint32_t)Math::clamp((int)(src.albedo.r * 255.0f), 0, 255);
		uint32_t g = (uint32_t)Math::clamp((int)(src.albedo.g * 255.0f), 0, 255);
		uint32_t b = (uint32_t)Math::clamp((int)(src.albedo.b * 255.0f), 0, 255);
		uint32_t a = (uint32_t)Math::clamp((int)(src.albedo.a * 255.0f), 0, 255);
		dst.albedo_rgba8 = r | (g << 8) | (b << 16) | (a << 24);
		
		dst.normal_oct16 = encode_normal_oct16(src.normal);
		
		uint32_t rough_u16 = (uint32_t)Math::clamp((int)(src.roughness * 65535.0f), 0, 65535);
		uint32_t metal_u16 = (uint32_t)Math::clamp((int)(src.metallic * 65535.0f), 0, 65535);
		dst.roughness_metallic = rough_u16 | (metal_u16 << 16);
		
		uint32_t emissive_u16 = (uint32_t)Math::clamp((int)(src.emissive * 65535.0f), 0, 65535);
		dst.emissive_flags = emissive_u16 | ((src.flags & 0xFFFF) << 16);
		
		dst.u_scale = src.u_scale;
		dst.v_scale = src.v_scale;
		dst.texture_index = src.texture_index;
		dst.reserved = 0;
	}
	
	dirty = false;
}

PackedByteArray AtcAttributePipeline::get_gpu_material_ssbo_bytes() const {
	PackedByteArray pba;
	pba.resize(MAX_GLOBAL_MATERIALS * sizeof(GpuMaterialData));
	uint8_t *w = pba.ptrw();
	memcpy(w, gpu_materials.ptr(), MAX_GLOBAL_MATERIALS * sizeof(GpuMaterialData));
	return pba;
}

void AtcAttributePipeline::update_gpu_device(RenderingDevice *p_rd) {
	if (!p_rd) {
		return;
	}
	update_conversion_buffers();
	PackedByteArray bytes = get_gpu_material_ssbo_bytes();
	
	if (!gpu_ssbo_rid.is_valid()) {
		gpu_ssbo_rid = p_rd->storage_buffer_create(bytes.size(), bytes);
	} else {
		p_rd->buffer_update(gpu_ssbo_rid, 0, bytes.size(), bytes.ptr());
	}
}

void AtcAttributePipeline::clear() {
	materials.clear();
	materials.resize(MAX_GLOBAL_MATERIALS);
	gpu_materials.clear();
	gpu_materials.resize(MAX_GLOBAL_MATERIALS);
	dirty = true;
}
```

### 4.3 Vulkan GPU Compute Raymarcher Node (`VoxelRendererRD`)

#### Header File (`modules/godot_eden/rendering/voxel_renderer_rd.h`)
```cpp
/**************************************************************************/
/*  voxel_renderer_rd.h                                                   */
/**************************************************************************/

#pragma once

#include "nodes/voxel_volume.h"
#include "rendering/atc_attribute_pipeline.h"
#include "scene/3d/node_3d.h"
#include "servers/rendering_server.h"

class VoxelRendererRD : public Node3D {
	GDCLASS(VoxelRendererRD, Node3D);

private:
	Ref<VoxelVolume> volume;
	Ref<AtcAttributePipeline> atc_pipeline;
	
	bool enabled = true;
	int lod_levels = 8;
	float base_voxel_scale = 0.25f;
	int clipmap_grid_extent = 32;
	
	RID clipmap_buffer_rid;
	RID svo_nodes_buffer_rid;
	RID compute_pipeline_rid;
	RID uniform_set_rid;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	VoxelRendererRD();
	~VoxelRendererRD();

	void set_volume(const Ref<VoxelVolume> &p_volume);
	Ref<VoxelVolume> get_volume() const;

	void set_atc_pipeline(const Ref<AtcAttributePipeline> &p_pipeline);
	Ref<AtcAttributePipeline> get_atc_pipeline() const;

	void set_lod_levels(int p_levels);
	int get_lod_levels() const;

	void set_base_voxel_scale(float p_scale);
	float get_base_voxel_scale() const;

	void update_lod_clipmap(const Vector3 &p_camera_position);
	void dispatch_raymarch_compute(RID p_render_target, const Vector3 &p_camera_pos, const Vector3 &p_camera_dir);
	void clear();
};
```

---

## 5. Verification Method

To verify the Concentric Clipmap LOD Pipeline and ATC Attribute System implementation:

1. **Unit Verification (`tests/test_rendering.h`)**:
   - Octahedral Normal Encoding Roundtrip: Encode random unit normals $\vec{n} \in S^2$ to `oct16`, decode back to $\vec{n}'$, verify $\|\vec{n} - \vec{n}'\| < 0.01$.
   - Material Tag Packing/Unpacking: Verify `pack_material_tag` and `unpack_material_tag` maintain bitwise fidelity.
   - GPU SSBO Layout Size & Alignment: Verify `sizeof(GpuMaterialData) == 32` bytes and `get_gpu_material_ssbo_bytes().size() == 8192` bytes.
   - Clipmap Ring Bounds Calculation: Test `update_lod_clipmap` output with camera movement, verifying snapped centers shift by exact multiples of voxel scale $s_L$.

2. **E2E Suite Execution**:
   - Command: `python tests/e2e/runner.py --feature F10` (Clipmap LOD) and `--feature F11` (ATC Material System).
   - Expected Output: 100% Pass across all 15 tests for F10 and 13 tests for F11 without memory leaks or regression.
