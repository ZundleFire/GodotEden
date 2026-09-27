# Handoff & Technical Remediation Plan: Uniform Set Creation & Alignment in `VoxelRendererRD`

**Agent**: Explorer 1 (Sub-Orchestrator M3, Iteration 2 Remediation)  
**Date**: 2026-08-05  
**Target File**: `modules/godot_eden/rendering/voxel_renderer_rd.h/cpp`  
**Related Files**: `modules/godot_eden/shaders/micro_voxel_raymarch.glsl`, `modules/godot_eden/rendering/atc_attribute_pipeline.h`

---

## 1. Observation

Direct code inspection of `VoxelRendererRD` and associated Vulkan GLSL compute shaders revealed the following defects:

1. **Missing Uniform Set Creation in `VoxelRendererRD`**
   - **Location**: `modules/godot_eden/rendering/voxel_renderer_rd.cpp` lines 292–295 and 322–325.
   - **Finding**: `raymarch_uniform_set` and `clipmap_uniform_set` are declared as `RID` member variables in `voxel_renderer_rd.h` (lines 73–74), but were **never created** anywhere in the codebase.
   - **Impact**: `raymarch_uniform_set.is_valid()` evaluates to `false` in `dispatch_raymarch_compute()`, bypassing `rd->compute_list_bind_uniform_set(...)` while still executing `rd->compute_list_dispatch(...)`. This dispatches a Vulkan compute shader with unbound descriptor set 0, leading to driver crashes / Vulkan validation errors.

2. **GLSL Undeclared Identifier (`AtcPackedGpuMaterial`)**
   - **Location**: `modules/godot_eden/shaders/micro_voxel_raymarch.glsl` lines 41 and 141.
   - **Finding**: `micro_voxel_raymarch.glsl` defined `struct GpuMaterialData` at line 32, but declared `AtcPackedGpuMaterial materials[];` at line 41.
   - **Impact**: `glslangValidator` / `RD_GLSL` fails compilation with `'AtcPackedGpuMaterial' : undeclared identifier`.

3. **CPU vs GPU Struct Layout & Alignment Misalignment**
   - **Location**: `voxel_renderer_rd.h` (`ClipmapLevelGpu`), `atc_attribute_pipeline.h` (`GpuMaterialData`), and `micro_voxel_raymarch.glsl` (`ClipmapLevel` and `GpuMaterialData`).
   - **Finding**:
     - `ClipmapLevelGpu` in C++ was 32 bytes tightly packed, whereas GLSL std430 layout requires 16-byte alignment for `vec3` and `ivec3`, making the GLSL struct 64 bytes.
     - `GpuMaterialData` in C++ had 32 bytes (`albedo_rgba8`, `normal_oct16`, `roughness_metallic`, `emissive_flags`, `u_scale`, `v_scale`, `texture_index`, `reserved`), while GLSL had 20 bytes (`albedo_rgba8`, `normal_oct16`, `roughness_metallic`, `emissive_flags`, `emission_rgb565`), creating a layout mismatch.

4. **Hardcoded Raymarch Spatial Bounds**
   - **Location**: `micro_voxel_raymarch.glsl` lines 111–112.
   - **Finding**: `root_min` and `root_max` were hardcoded to `vec3(-512.0)` and `vec3(512.0)`.

---

## 2. Logic Chain

1. **Vulkan Descriptor Set Contract**:
   Godot 4 `RenderingDevice` requires that before dispatching a compute pipeline (`rd->compute_list_dispatch`), all uniform sets referenced by shader `layout(set = X, binding = Y)` must be created using `rd->uniform_set_create()` and bound using `rd->compute_list_bind_uniform_set()`.

2. **Uniform Resource Dependencies**:
   Creating `raymarch_uniform_set` requires 4 valid resource RIDs:
   - Binding 0 (`UNIFORM_TYPE_IMAGE` / Storage Image): `render_target_image` (Format `R32G32B32A32_SFLOAT`, 2D Texture with `TEXTURE_USAGE_STORAGE_BIT`).
   - Binding 1 (`UNIFORM_TYPE_STORAGE_BUFFER`): `svo_ssbo_buffer` (SVO DAG nodes array).
   - Binding 2 (`UNIFORM_TYPE_STORAGE_BUFFER`): `clipmap_ssbo_buffer` (`ClipmapLevelGpu` array).
   - Binding 3 (`UNIFORM_TYPE_STORAGE_BUFFER`): `material_palette_ssbo_buffer` (`GpuMaterialData` array).

3. **Lifecycle & Invalidation Triggers**:
   Whenever any underlying buffer or texture RID changes (e.g. on SVO upload, clipmap recalculation, material palette upload, or render target resize), any existing uniform set RID becomes stale. The old uniform set must be freed with `rd->free_rid()` and recreated via `_update_uniform_sets()`.

4. **Struct Packing Alignment**:
   In Vulkan GLSL std430 rules:
   - `vec3` and `ivec3` have 16-byte alignment.
   - Adding explicit padding fields in C++ `ClipmapLevelGpu` (`pad_center`, `pad0`, `pad_grid`, `pad_toroidal`) ensures exact 64-byte matching.
   - Aligning `GpuMaterialData` in C++ and GLSL to include `emission_rgb565` ensures an identical 32-byte layout across CPU and GPU memory.

---

## 3. Caveats

- **Headless RD Availability**: On systems running without Vulkan-capable GPU drivers, `is_rd_available()` returns `false`. All RD uniform set creation functions must be guarded with `if (is_rd_available())`. Fallback mock methods for headless testing should remain intact.
- **Render Target Override**: When `dispatch_raymarch_compute(RID p_render_target_override)` passes an explicit override RID distinct from `render_target_image`, `_update_uniform_sets(p_render_target_override)` must instantiate a uniform set with that override image RID.

---

## 4. Conclusion & Technical Remediation Design

To remediate these issues, Worker 1 must apply the exact C++ design detailed below in `voxel_renderer_rd.h`, `voxel_renderer_rd.cpp`, `atc_attribute_pipeline.h`, and `micro_voxel_raymarch.glsl`.

### 4.1 Implementation of `_update_uniform_sets()` & Helpers in `VoxelRendererRD`

#### Headers (`modules/godot_eden/rendering/voxel_renderer_rd.h`)

```cpp
// Struct Layout Alignment Fix: ClipmapLevelGpu (std430 64-byte alignment)
struct ClipmapLevelGpu {
	float center[3] = { 0.0f, 0.0f, 0.0f }; // offset 0..12
	float pad_center = 0.0f;                // offset 12..16 (align vec3 to 16 bytes)
	float voxel_scale = 1.0f;               // offset 16..20
	uint32_t lod_index = 0;                 // offset 20..24
	float blend_margin = 0.0f;              // offset 24..28
	uint32_t pad0 = 0;                      // offset 28..32 (align ivec3 to 16 bytes)
	int32_t grid_size[3] = { 32, 32, 32 };  // offset 32..44
	int32_t pad_grid = 0;                   // offset 44..48 (align ivec3 to 16 bytes)
	int32_t toroidal_offset[3] = { 0, 0, 0 };// offset 48..60
	int32_t pad_toroidal = 0;               // offset 60..64
};

// Dynamic Raymarch Push Constants (64-byte alignment)
struct RaymarchPushConstants {
	float camera_pos[3] = { 0.0f, 0.0f, 0.0f };
	float fov = 0.785398f;
	float camera_dir[3] = { 0.0f, 0.0f, -1.0f };
	float max_distance = 1000.0f;
	float screen_size[2] = { 1920.0f, 1080.0f };
	float root_min[3] = { -512.0f, -512.0f, -512.0f };
	float root_max[3] = { 512.0f, 512.0f, 512.0f };
};
```

#### Private Methods in `VoxelRendererRD` (`voxel_renderer_rd.h`):

```cpp
private:
	void _update_uniform_sets(RID p_target_image_override = RID());
	void _ensure_render_target_image();
	void _ensure_fallback_ssbos();
```

#### Implementation in `voxel_renderer_rd.cpp`:

```cpp
void VoxelRendererRD::_ensure_render_target_image() {
	if (!is_rd_available()) {
		return;
	}
	RenderingDevice *rd = _get_rd();
	if (!rd) {
		return;
	}

	if (render_target_image.is_valid()) {
		rd->free_rid(render_target_image);
		render_target_image = RID();
	}

	RD::TextureFormat tf;
	tf.format = RenderingDevice::DATA_FORMAT_R32G32B32A32_SFLOAT;
	tf.width = render_target_size.x;
	tf.height = render_target_size.y;
	tf.depth = 1;
	tf.array_layers = 1;
	tf.mipmaps = 1;
	tf.texture_type = RenderingDevice::TEXTURE_TYPE_2D;
	tf.usage_bits = RenderingDevice::TEXTURE_USAGE_STORAGE_BIT | RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT | RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT | RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT;

	RD::TextureView tv;
	render_target_image = rd->texture_create(tf, tv);
}

void VoxelRendererRD::_ensure_fallback_ssbos() {
	if (!is_rd_available()) {
		return;
	}
	RenderingDevice *rd = _get_rd();
	if (!rd) {
		return;
	}

	if (!svo_ssbo_buffer.is_valid()) {
		PackedByteArray dummy_svo;
		dummy_svo.resize(16); // 1 SvoNode struct (16 bytes)
		dummy_svo.fill(0);
		svo_ssbo_buffer = rd->storage_buffer_create(dummy_svo.size(), dummy_svo);
	}

	if (!clipmap_ssbo_buffer.is_valid()) {
		upload_clipmap_ssbo();
	}

	if (!material_palette_ssbo_buffer.is_valid()) {
		PackedByteArray dummy_mat;
		dummy_mat.resize(32); // 1 GpuMaterialData struct (32 bytes)
		dummy_mat.fill(0);
		material_palette_ssbo_buffer = rd->storage_buffer_create(dummy_mat.size(), dummy_mat);
	}
}

void VoxelRendererRD::_update_uniform_sets(RID p_target_image_override) {
	if (!is_rd_available() || !pipelines_initialized) {
		return;
	}

	RenderingDevice *rd = _get_rd();
	if (!rd) {
		return;
	}

	RID target_image = p_target_image_override.is_valid() ? p_target_image_override : render_target_image;
	if (!target_image.is_valid()) {
		_ensure_render_target_image();
		target_image = render_target_image;
	}

	_ensure_fallback_ssbos();

	// 1. Raymarch Uniform Set (Set 0: Bindings 0, 1, 2, 3)
	if (target_image.is_valid() && svo_ssbo_buffer.is_valid() && clipmap_ssbo_buffer.is_valid() && material_palette_ssbo_buffer.is_valid() && raymarch_shader.is_valid()) {
		if (raymarch_uniform_set.is_valid()) {
			rd->free_rid(raymarch_uniform_set);
			raymarch_uniform_set = RID();
		}

		Vector<Ref<RD::Uniform>> uniforms;

		// Set 0, Binding 0: UNIFORM_TYPE_IMAGE (Storage Image for out_color)
		Ref<RD::Uniform> u_image;
		u_image.instantiate();
		u_image->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
		u_image->set_binding(0);
		u_image->add_id(target_image);
		uniforms.push_back(u_image);

		// Set 0, Binding 1: UNIFORM_TYPE_STORAGE_BUFFER (SVO DAG Buffer)
		Ref<RD::Uniform> u_svo;
		u_svo.instantiate();
		u_svo->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
		u_svo->set_binding(1);
		u_svo->add_id(svo_ssbo_buffer);
		uniforms.push_back(u_svo);

		// Set 0, Binding 2: UNIFORM_TYPE_STORAGE_BUFFER (Clipmap Buffer)
		Ref<RD::Uniform> u_clipmap;
		u_clipmap.instantiate();
		u_clipmap->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
		u_clipmap->set_binding(2);
		u_clipmap->add_id(clipmap_ssbo_buffer);
		uniforms.push_back(u_clipmap);

		// Set 0, Binding 3: UNIFORM_TYPE_STORAGE_BUFFER (Material Palette Buffer)
		Ref<RD::Uniform> u_material;
		u_material.instantiate();
		u_material->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
		u_material->set_binding(3);
		u_material->add_id(material_palette_ssbo_buffer);
		uniforms.push_back(u_material);

		raymarch_uniform_set = rd->uniform_set_create(uniforms, raymarch_shader, 0);
	}

	// 2. Clipmap Uniform Set (Set 0: Binding 0)
	if (clipmap_ssbo_buffer.is_valid() && clipmap_shader.is_valid()) {
		if (clipmap_uniform_set.is_valid()) {
			rd->free_rid(clipmap_uniform_set);
			clipmap_uniform_set = RID();
		}

		Vector<Ref<RD::Uniform>> clipmap_uniforms;

		Ref<RD::Uniform> u_clipmap_buf;
		u_clipmap_buf.instantiate();
		u_clipmap_buf->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
		u_clipmap_buf->set_binding(0);
		u_clipmap_buf->add_id(clipmap_ssbo_buffer);
		clipmap_uniforms.push_back(u_clipmap_buf);

		clipmap_uniform_set = rd->uniform_set_create(clipmap_uniforms, clipmap_shader, 0);
	}
}
```

#### Invalidation Triggers Integration

Invoke `_update_uniform_sets()` inside:
1. `_init_rd_pipelines()`: At end after shaders/pipelines are constructed.
2. `upload_svo_ssbo()`: Immediately after `svo_ssbo_buffer` creation.
3. `upload_clipmap_ssbo()`: Immediately after `clipmap_ssbo_buffer` creation.
4. `upload_material_palette_ssbo()`: Immediately after `material_palette_ssbo_buffer` creation.
5. `set_render_target_size()`: Recreate `render_target_image` and call `_update_uniform_sets()`.
6. `dispatch_raymarch_compute(RID p_render_target_override)`:
   ```cpp
   void VoxelRendererRD::dispatch_raymarch_compute(RID p_render_target_override) {
       if (!is_rd_available() || !is_pipeline_ready()) {
           return;
       }

       RenderingDevice *rd = _get_rd();
       RID target_image = p_render_target_override.is_valid() ? p_render_target_override : render_target_image;
       if (!target_image.is_valid()) {
           _ensure_render_target_image();
           target_image = render_target_image;
       }

       _update_uniform_sets(target_image);

       if (!raymarch_uniform_set.is_valid()) {
           return;
       }

       RaymarchPushConstants push_constants;
       Transform3D xform = get_global_transform();
       Vector3 cam_pos = xform.origin;
       Vector3 cam_dir = -xform.basis.get_column(2);

       push_constants.camera_pos[0] = cam_pos.x;
       push_constants.camera_pos[1] = cam_pos.y;
       push_constants.camera_pos[2] = cam_pos.z;
       push_constants.fov = fov;
       push_constants.camera_dir[0] = cam_dir.x;
       push_constants.camera_dir[1] = cam_dir.y;
       push_constants.camera_dir[2] = cam_dir.z;
       push_constants.max_distance = view_distance;
       push_constants.screen_size[0] = (float)render_target_size.x;
       push_constants.screen_size[1] = (float)render_target_size.y;
       push_constants.root_min[0] = -512.0f;
       push_constants.root_min[1] = -512.0f;
       push_constants.root_min[2] = -512.0f;
       push_constants.root_max[0] = 512.0f;
       push_constants.root_max[1] = 512.0f;
       push_constants.root_max[2] = 512.0f;

       int workgroup_x = (render_target_size.x + 7) / 8;
       int workgroup_y = (render_target_size.y + 7) / 8;

       int64_t compute_list = rd->compute_list_begin();
       rd->compute_list_bind_compute_pipeline(compute_list, raymarch_pipeline);
       rd->compute_list_bind_uniform_set(compute_list, raymarch_uniform_set, 0);
       rd->compute_list_set_push_constant(compute_list, &push_constants, sizeof(RaymarchPushConstants));
       rd->compute_list_dispatch(compute_list, workgroup_x, workgroup_y, 1);
       rd->compute_list_end();
   }
   ```

### 4.2 Fix GLSL Shader & Struct Alignment (`micro_voxel_raymarch.glsl`)

```glsl
struct ClipmapLevel {
	vec3 center;          // offset 0..12, align 16
	float voxel_scale;    // offset 16..20
	uint lod_index;       // offset 20..24
	float blend_margin;   // offset 24..28
	uint pad0;            // offset 28..32 (align ivec3 to 16)
	ivec3 grid_size;      // offset 32..44, align 16
	ivec3 toroidal_offset;// offset 48..60, align 16
};

struct GpuMaterialData {
	uint albedo_rgba8;
	uint normal_oct16;
	uint roughness_metallic;
	uint emissive_flags;
	uint emission_rgb565;
	float u_scale;
	float v_scale;
	uint texture_index;
};

layout(set = 0, binding = 3, std430) readonly buffer MaterialBuffer {
	GpuMaterialData materials[];
} material_buffer;

layout(push_constant) uniform RaymarchPushConstants {
	vec3 camera_pos;
	float fov;
	vec3 camera_dir;
	float max_distance;
	vec2 screen_size;
	vec3 root_min;
	vec3 root_max;
} params;
```

In `main()` of `micro_voxel_raymarch.glsl`:
Replace line 111–112 with:
```glsl
	vec3 root_min = params.root_min;
	vec3 root_max = params.root_max;
```
And replace line 141 with:
```glsl
	GpuMaterialData gpu_mat = material_buffer.materials[mat_tag];
```

---

## 5. Verification Method

1. **Static Inspection of Uniform Sets**:
   Verify in `voxel_renderer_rd.cpp` that `_update_uniform_sets()` calls `rd->uniform_set_create()` with `RD::Uniform` objects for bindings 0, 1, 2, 3, and returns valid `raymarch_uniform_set` and `clipmap_uniform_set` RIDs.

2. **GLSL Compilation Check**:
   Validate GLSL shaders against GLSL standard using `glslangValidator`:
   ```bash
   glslangValidator -V modules/godot_eden/shaders/micro_voxel_raymarch.glsl
   glslangValidator -V modules/godot_eden/shaders/clipmap_lod.glsl
   ```

3. **C++ Unit Test Verification**:
   Execute Doctest test suite to confirm zero regressions:
   ```bash
   godot --test --test-suite="[Modules][GodotEden]"
   ```

4. **E2E Suite Verification**:
   ```bash
   python tests/e2e/runner.py
   ```
