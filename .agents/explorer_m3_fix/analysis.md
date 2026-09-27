# Remediation Strategy Blueprint — Milestone 3 (Iteration 2)

**Author**: `explorer_m3_fix` (Explorer — M3 Remediation Strategy)  
**Milestone**: Milestone 3 — Micro-Voxel Renderer Architecture & Vulkan Compute Raymarching Pipeline  
**Parent Conversation ID**: `3cc06f64-9446-4e4d-8a16-68ab01c0033e`  
**Working Directory**: `C:\DEV_DRIVE\Dev\GodotEden\.agents\explorer_m3_fix`  
**Date**: 2026-08-05  

---

## Executive Summary

During Iteration 1 gate evaluation for Milestone 3, the submission failed gate approval (Reviewer 1 requested changes, Challenger 2 rejected) due to four distinct issues across GLSL compute shaders and C++ Vulkan RenderingDevice pipeline management:

1. **Undeclared Identifier in GLSL (`micro_voxel_raymarch.glsl`)**: Lines 41 & 141 referenced an undeclared type `AtcPackedGpuMaterial` instead of `GpuMaterialData`.
2. **Missing Vulkan Uniform Set Creation (`voxel_renderer_rd.cpp`)**: `rd->uniform_set_create(...)` was omitted for both `raymarch_uniform_set` and `clipmap_uniform_set`, leaving uniform set RIDs uninitialized and preventing Vulkan compute dispatch.
3. **SVO Raymarch Infinite Loop Risk (`micro_voxel_raymarch.glsl`)**: SVO raymarching traversal lacked an explicit step count limit (`step_count < 256`) and consistent ray parameter advancement (`t += dt`) when traversing internal octree nodes, risking GPU TDR lockup.
4. **Incorrect Camera Ray Generation Math (`micro_voxel_raymarch.glsl`)**: View ray direction calculation lacked screen aspect ratio scaling (`aspect = width / height`), half-FOV tangent scaling (`tan(fov * 0.5)`), and camera basis vector rotation.

This blueprint provides line-by-line remediation specifications, code diffs, mathematical proofs, and verification procedures for the implementation team.

---

## 1. Remediation Specification 1: GLSL Type Mismatch & Struct Alignment

### 1.1 Root Cause & Analysis
In `modules/godot_eden/shaders/micro_voxel_raymarch.glsl`:
- Line 32 defines `struct GpuMaterialData`:
  ```glsl
  struct GpuMaterialData {
  	uint albedo_rgba8;
  	uint normal_oct16;
  	uint roughness_metallic;
  	uint emissive_flags;
  	uint emission_rgb565;
  };
  ```
- Lines 40–42 declare the material SSBO:
  ```glsl
  layout(set = 0, binding = 3, std430) readonly buffer MaterialBuffer {
  	AtcPackedGpuMaterial materials[];
  } material_buffer;
  ```
- Line 141 instantiates:
  ```glsl
  AtcPackedGpuMaterial gpu_mat = material_buffer.materials[mat_tag];
  ```

`AtcPackedGpuMaterial` is undeclared in GLSL. Furthermore, C++ `GpuMaterialData` in `modules/godot_eden/rendering/atc_attribute_pipeline.h` (lines 20–29) has a 32-byte layout:
```cpp
struct GpuMaterialData {
	uint32_t albedo_rgba8 = 0xFFFFFFFF;    // offset 0
	uint32_t normal_oct16 = 0x7F7F;       // offset 4
	uint32_t roughness_metallic = 0x8000; // offset 8
	uint32_t emissive_flags = 0;          // offset 12
	float u_scale = 1.0f;                 // offset 16
	float v_scale = 1.0f;                 // offset 20
	uint32_t texture_index = 0;           // offset 24
	uint32_t reserved = 0;                // offset 28
};
```
To ensure SSBO array indexing `materials[mat_tag]` in GLSL aligns with the 32-byte stride exported by `AtcAttributePipeline::get_gpu_material_ssbo_bytes()` (8192 bytes total for 256 materials), GLSL `struct GpuMaterialData` must match the 32-byte C++ definition.

### 1.2 Targeted Changes (`micro_voxel_raymarch.glsl`)
1. **Lines 32–38**: Update `struct GpuMaterialData` to 32 bytes (8 uint/float fields).
2. **Line 41**: Replace `AtcPackedGpuMaterial materials[];` with `GpuMaterialData materials[];`.
3. **Line 141**: Replace `AtcPackedGpuMaterial gpu_mat` with `GpuMaterialData gpu_mat`.

```glsl
// Proposed GLSL Code Change (lines 32-42)
struct GpuMaterialData {
	uint albedo_rgba8;
	uint normal_oct16;
	uint roughness_metallic;
	uint emissive_flags;
	float u_scale;
	float v_scale;
	uint texture_index;
	uint reserved;
};

layout(set = 0, binding = 3, std430) readonly buffer MaterialBuffer {
	GpuMaterialData materials[];
} material_buffer;
```

---

## 2. Remediation Specification 2: Vulkan Uniform Set Creation Logic

### 2.1 Root Cause & Analysis
In `modules/godot_eden/rendering/voxel_renderer_rd.cpp`:
- SSBO storage buffers (`svo_ssbo_buffer`, `clipmap_ssbo_buffer`, `material_palette_ssbo_buffer`) are created via `rd->storage_buffer_create(...)`.
- `dispatch_raymarch_compute()` and `dispatch_clipmap_compute()` check `if (raymarch_uniform_set.is_valid())` and `if (clipmap_uniform_set.is_valid())`.
- However, `rd->uniform_set_create(...)` was never called, leaving uniform sets invalid (`RID()`) and bypassing Vulkan binding during dispatch.

### 2.2 Binding Requirements Matrix
- **`raymarch_uniform_set` (Set 0 for `micro_voxel_raymarch.glsl`)**:
  - Binding 0: `out_color` (`image2D`, `UNIFORM_TYPE_IMAGE`, RID: `render_target_image`)
  - Binding 1: `svo_dag` (`SvoBuffer`, `UNIFORM_TYPE_STORAGE_BUFFER`, RID: `svo_ssbo_buffer`)
  - Binding 2: `clipmap` (`ClipmapBuffer`, `UNIFORM_TYPE_STORAGE_BUFFER`, RID: `clipmap_ssbo_buffer`)
  - Binding 3: `material_buffer` (`MaterialBuffer`, `UNIFORM_TYPE_STORAGE_BUFFER`, RID: `material_palette_ssbo_buffer`)
- **`clipmap_uniform_set` (Set 0 for `clipmap_lod.glsl`)**:
  - Binding 0: `clipmap` (`ClipmapBuffer`, `UNIFORM_TYPE_STORAGE_BUFFER`, RID: `clipmap_ssbo_buffer`)

### 2.3 Targeted Changes (`voxel_renderer_rd.h` & `voxel_renderer_rd.cpp`)
1. Declare helper private methods in `voxel_renderer_rd.h`:
   ```cpp
   void _update_raymarch_uniform_set(RID p_target_image);
   void _update_clipmap_uniform_set();
   void _ensure_render_target_image();
   ```
2. Implement uniform set creation in `voxel_renderer_rd.cpp`:

```cpp
void VoxelRendererRD::_ensure_render_target_image() {
	if (!is_rd_available()) {
		return;
	}
	RenderingDevice *rd = _get_rd();
	if (!render_target_image.is_valid()) {
		RD::RDTextureFormat tf;
		tf.format = RD::DATA_FORMAT_R32G32B32A32_SFLOAT;
		tf.width = render_target_size.x;
		tf.height = render_target_size.y;
		tf.texture_type = RD::TEXTURE_TYPE_2D;
		tf.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_UPDATE_BIT | RD::TEXTURE_USAGE_CAN_READ_BIT;

		RD::RDTextureView tv;
		render_target_image = rd->texture_create(tf, tv);
	}
}

void VoxelRendererRD::_update_raymarch_uniform_set(RID p_target_image) {
	if (!is_rd_available() || !raymarch_shader.is_valid()) {
		return;
	}
	RenderingDevice *rd = _get_rd();
	if (!p_target_image.is_valid() || !svo_ssbo_buffer.is_valid() || !clipmap_ssbo_buffer.is_valid() || !material_palette_ssbo_buffer.is_valid()) {
		return;
	}

	if (raymarch_uniform_set.is_valid()) {
		rd->free_rid(raymarch_uniform_set);
		raymarch_uniform_set = RID();
	}

	RD::Uniform u_image;
	u_image.uniform_type = RD::UNIFORM_TYPE_IMAGE;
	u_image.binding = 0;
	u_image.add_id(p_target_image);

	RD::Uniform u_svo;
	u_svo.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
	u_svo.binding = 1;
	u_svo.add_id(svo_ssbo_buffer);

	RD::Uniform u_clipmap;
	u_clipmap.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
	u_clipmap.binding = 2;
	u_clipmap.add_id(clipmap_ssbo_buffer);

	RD::Uniform u_material;
	u_material.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
	u_material.binding = 3;
	u_material.add_id(material_palette_ssbo_buffer);

	Vector<RD::Uniform> uniforms;
	uniforms.push_back(u_image);
	uniforms.push_back(u_svo);
	uniforms.push_back(u_clipmap);
	uniforms.push_back(u_material);

	raymarch_uniform_set = rd->uniform_set_create(uniforms, raymarch_shader, 0);
}

void VoxelRendererRD::_update_clipmap_uniform_set() {
	if (!is_rd_available() || !clipmap_shader.is_valid()) {
		return;
	}
	RenderingDevice *rd = _get_rd();
	if (!clipmap_ssbo_buffer.is_valid()) {
		return;
	}

	if (clipmap_uniform_set.is_valid()) {
		rd->free_rid(clipmap_uniform_set);
		clipmap_uniform_set = RID();
	}

	RD::Uniform u_clipmap;
	u_clipmap.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
	u_clipmap.binding = 0;
	u_clipmap.add_id(clipmap_ssbo_buffer);

	Vector<RD::Uniform> uniforms;
	uniforms.push_back(u_clipmap);

	clipmap_uniform_set = rd->uniform_set_create(uniforms, clipmap_shader, 0);
}
```

3. Call `_update_raymarch_uniform_set()` and `_update_clipmap_uniform_set()` within buffer upload methods (`upload_svo_ssbo`, `upload_clipmap_ssbo`, `upload_material_palette_ssbo`) and inside `dispatch_raymarch_compute` / `dispatch_clipmap_compute` if uniform set RIDs are invalid.

---

## 3. Remediation Specification 3: Ray Advancement & Loop Iteration Safeguard

### 3.1 Root Cause & Analysis
In `micro_voxel_raymarch.glsl` (lines 119–169):
- The raymarching `while (t < t_far && t < params.max_distance)` loop lacked a step count cap.
- Descending into non-leaf nodes updated `current_idx` without advancing `t`. If traversal did not reach a terminating leaf or empty octant, the loop could execute infinitely, triggering GPU TDR resets.

### 3.2 Targeted Changes (`micro_voxel_raymarch.glsl`)
1. Introduce iteration counter `int step_count = 0;`.
2. Limit max steps: `while (t < t_far && t < params.max_distance && step_count < 256)`.
3. In leaf node processing (`first_child_idx == 0u || child_mask == 0u`):
   - If non-hitting leaf (`sdf_value > 0`), advance ray `t += max(0.25, abs(node.sdf_value));` and reset `current_idx = 0u;` to restart traversal at the new ray sample position.
4. In internal node processing (`child_mask & (1u << octant) == 0u`):
   - In empty octants, advance ray `t += 0.5;` and reset `current_idx = 0u;`.

```glsl
// Proposed GLSL Code Change (lines 116-170)
t_near = max(t_near, 0.0);
float t = t_near;
uint current_idx = 0u;
int step_count = 0;

while (t < t_far && t < params.max_distance && step_count < 256) {
	step_count++;
	vec3 p = ray_origin + ray_dir * (t + 0.001);
	vec3 norm_p = (p - root_min) / (root_max - root_min);

	if (any(lessThan(norm_p, vec3(0.0))) || any(greaterThanEqual(norm_p, vec3(1.0)))) {
		break;
	}

	if (current_idx >= svo_dag.nodes.length()) {
		break;
	}

	SvoNode node = svo_dag.nodes[current_idx];
	if (node.first_child_idx == 0u || node.child_mask == 0u) {
		if (node.sdf_value <= 0.0f || node.material_tag > 0u) {
			vec3 estimated_normal = normalize(p - (root_min + root_max) * 0.5);
			vec4 albedo = vec4(0.3, 0.7, 0.4, 1.0);
			vec3 emission = vec3(0.0);

			uint mat_tag = node.material_tag;
			if (mat_tag < material_buffer.materials.length()) {
				GpuMaterialData gpu_mat = material_buffer.materials[mat_tag];
				albedo = unpack_rgba8(gpu_mat.albedo_rgba8);
				vec3 oct_norm = unpack_oct16(gpu_mat.normal_oct16);
				if (length(oct_norm) > 0.001) {
					estimated_normal = oct_norm;
				}
			}

			vec3 light_dir = normalize(vec3(0.5, 1.0, 0.3));
			float diff = max(dot(estimated_normal, light_dir), 0.2);
			vec3 final_rgb = albedo.rgb * diff + emission;
			color = vec4(final_rgb, albedo.a);
			break;
		}
		t += max(0.25, abs(node.sdf_value));
		current_idx = 0u;
	} else {
		ivec3 oct = ivec3(clamp(floor(norm_p * 2.0), 0.0, 1.0));
		uint octant = uint(oct.x | (oct.y << 1) | (oct.z << 2));

		if ((node.child_mask & (1u << octant)) != 0u) {
			uint child_offset = bitCount(node.child_mask & ((1u << octant) - 1u));
			current_idx = node.first_child_idx + child_offset;
		} else {
			t += 0.5;
			current_idx = 0u;
		}
	}
}
```

---

## 4. Remediation Specification 4: Camera Aspect Ratio & Basis Vector Rotation

### 4.1 Root Cause & Analysis
In `micro_voxel_raymarch.glsl` (lines 102–105):
```glsl
vec2 uv = (vec2(pixel_coords) + vec2(0.5)) / params.screen_size;
vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);

vec3 ray_dir = normalize(params.camera_dir + vec3(ndc.x * params.fov, ndc.y * params.fov, 0.0));
```
- Missing aspect ratio multiplier (`aspect = screen_size.x / screen_size.y`).
- Missing perspective half-FOV tangent scaling (`tan(fov * 0.5)`).
- Adding raw NDC components directly to `params.camera_dir` assumes fixed camera alignment along world Z, breaking when the camera rotates.

### 4.2 Targeted Changes (`micro_voxel_raymarch.glsl`)
1. Compute screen aspect ratio and FOV tangent:
   ```glsl
   float aspect = params.screen_size.x / params.screen_size.y;
   float tan_half_fov = tan(params.fov * 0.5);
   vec2 ray_ndc = vec2(ndc.x * aspect * tan_half_fov, ndc.y * tan_half_fov);
   ```
2. Derive camera forward, right, and up vectors from `params.camera_dir`:
   ```glsl
   vec3 forward = normalize(params.camera_dir);
   vec3 world_up = abs(forward.y) > 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
   vec3 right = normalize(cross(forward, world_up));
   vec3 up = cross(right, forward);
   ```
3. Construct world-space ray direction:
   ```glsl
   vec3 ray_dir = normalize(forward + right * ray_ndc.x + up * ray_ndc.y);
   ```

---

## 5. Summary of Files To Be Modified

| File Path | Nature of Modifications |
|---|---|
| `modules/godot_eden/shaders/micro_voxel_raymarch.glsl` | Fix `AtcPackedGpuMaterial` to `GpuMaterialData` (lines 41, 141), align struct to 32 bytes (line 32), add `step_count < 256` and `t += dt` (lines 119-169), correct aspect ratio & camera basis view ray generation (lines 102-105). |
| `modules/godot_eden/rendering/voxel_renderer_rd.h` | Declare `_update_raymarch_uniform_set(RID)`, `_update_clipmap_uniform_set()`, and `_ensure_render_target_image()` helper methods. |
| `modules/godot_eden/rendering/voxel_renderer_rd.cpp` | Implement `_update_raymarch_uniform_set`, `_update_clipmap_uniform_set`, and `_ensure_render_target_image()`. Integrate calls into SSBO upload methods and compute dispatch functions. |

---

## 6. Verification Method

1. **SCons Header & Shader Build Verification**:
   Build Godot with SCons to ensure GLSL compute shaders compile without syntax errors:
   `scons platform=windows target=editor dev_build=yes`
2. **Headless Unit Tests**:
   Run C++ unit tests to verify property setters, buffer uploads, and headless safety:
   `bin/godot.windows.editor.x86_64.exe --headless --test --test-case="*GodotEden*"`
3. **Vulkan Uniform Set & Execution Verification**:
   Inspect `voxel_renderer_rd.cpp` to confirm `uniform_set_create` returns valid RIDs when RD is active.
