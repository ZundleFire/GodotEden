# Remediation Plan & Handoff: Milestone 3 (Iteration 2) — Struct Alignment & Doctest Remediation

**Author**: Explorer 3  
**Date**: 2026-08-05  
**Target Milestone**: Milestone 3 (Iteration 2 Remediation)  
**Status**: Plan Complete  

---

## 1. Observation

### 1.1 `GpuMaterialData` Struct Alignment & Compilation Errors
- **Files**: `modules/godot_eden/rendering/atc_attribute_pipeline.h` (lines 20–29), `atc_attribute_pipeline.cpp` (lines 410–440), and `modules/godot_eden/shaders/micro_voxel_raymarch.glsl` (lines 32–42, 141).
- **Direct Observations**:
  1. `micro_voxel_raymarch.glsl` lines 40–42 declared:
     ```glsl
     layout(set = 0, binding = 3, std430) readonly buffer MaterialBuffer {
         AtcPackedGpuMaterial materials[];
     } material_buffer;
     ```
     `AtcPackedGpuMaterial` is an undeclared identifier in GLSL, breaking shader compilation.
  2. `GpuMaterialData` in C++ (`atc_attribute_pipeline.h`) contained:
     `albedo_rgba8`, `normal_oct16`, `roughness_metallic`, `emissive_flags`, `u_scale`, `v_scale`, `texture_index`, `reserved` (32 bytes total).
  3. `GpuMaterialData` in GLSL (`micro_voxel_raymarch.glsl`) contained:
     `albedo_rgba8`, `normal_oct16`, `roughness_metallic`, `emissive_flags`, `emission_rgb565` (20 bytes total).
  4. Field types, order, and total bytes between C++ and GLSL were mismatched, leading to memory offset corruption when uploading SSBO buffer data.

### 1.2 `ClipmapLevelGpu` Struct Alignment & std430 Memory Drift
- **Files**: `modules/godot_eden/rendering/voxel_renderer_rd.h` (lines 20–27), `modules/godot_eden/shaders/clipmap_lod.glsl` (lines 6–13), and `modules/godot_eden/shaders/micro_voxel_raymarch.glsl` (lines 19–26).
- **Direct Observations**:
  1. C++ `ClipmapLevelGpu` was 48 bytes tightly packed (`center[3]`, `voxel_scale`, `grid_size[3]`, `lod_index`, `toroidal_offset[3]`, `blend_margin`).
  2. Under Vulkan GLSL std430 rules, `vec3` and `ivec3` require 16-byte base alignment. In std430, `vec3 center` at offset 0 occupies bytes 0..11. Without explicit padding, scalar fields cause implicit alignment gaps before subsequent `ivec3` members.
  3. Direct `memcpy` of C++ 48-byte structs into GPU SSBO results in layout misalignment and corrupted LOD sampling on Vulkan drivers.

### 1.3 Toroidal Grid Modulo Calculations
- **File**: `modules/godot_eden/rendering/voxel_renderer_rd.cpp` (lines 181–183) and `modules/godot_eden/shaders/clipmap_lod.glsl` (line 45).
- **Direct Observations**:
  1. C++ computed `toroidal_offset` via `grid_cell.x % clipmap_grid_extent`.
  2. Standard C++ and GLSL `%` operators produce negative results for negative coordinates (e.g. `-10 % 32 == -10`).
  3. Toroidal ring offsets must strictly wrap in range `[0, extent - 1]`. Negative modulo results corrupt clipmap texture indexing when camera position is negative.

---

## 2. Logic Chain

1. **GpuMaterialData Alignment Logic**:
   - `GpuMaterialData` requires 32-byte std430 alignment on the GPU (8 scalar 32-bit fields = 32 bytes).
   - Aligning field-by-field: `albedo_rgba8` (offset 0), `normal_oct16` (offset 4), `roughness_metallic` (offset 8), `emissive_flags` (offset 12), `emission_rgb565` (offset 16), `u_scale` (offset 20), `v_scale` (offset 24), `texture_index` (offset 28).
   - In GLSL `micro_voxel_raymarch.glsl`, changing `AtcPackedGpuMaterial materials[];` to `GpuMaterialData materials[];` fixes the undeclared identifier compiler error while maintaining byte-exact memory alignment.

2. **ClipmapLevelGpu 64-Byte std430 Alignment Logic**:
   - In Vulkan std430 layout, every 16-byte block containing a `vec3` or `ivec3` requires 4 bytes of trailing padding (`pad0`, `pad1`, `pad2`, `pad3`) or 4 scalar fields to guarantee zero implicit padding gaps.
   - Field layout:
     - Block 0 (0..15): `center[3]` / `vec3 center` (12B) + `pad0` (4B) = 16B.
     - Block 1 (16..31): `voxel_scale` (4B) + `lod_index` (4B) + `blend_margin` (4B) + `pad1` (4B) = 16B.
     - Block 2 (32..47): `grid_size[3]` / `ivec3 grid_size` (12B) + `pad2` (4B) = 16B. (Aligned to 32B boundary: `32 % 16 == 0`).
     - Block 3 (48..63): `toroidal_offset[3]` / `ivec3 toroidal_offset` (12B) + `pad3` (4B) = 16B. (Aligned to 48B boundary: `48 % 16 == 0`).
   - Total struct size is exactly 64 bytes in C++ (`sizeof(ClipmapLevelGpu) == 64`) and 64 bytes in GLSL std430.

3. **Positive Toroidal Modulo Logic**:
   - Formula `(x % N + N) % N` guarantees positive toroidal index wrapping in range `[0, N - 1]`.
   - Applying `(grid_cell % extent + extent) % extent` in C++ and `((grid_cell % ext) + ext) % ext` in GLSL prevents negative array indices on camera movement across negative world space.

4. **Doctest Validation Logic**:
   - Adding explicit `sizeof()` and `offsetof()` Doctest assertions in `test_rendering.h` guarantees that C++ compiler layout match std430 expectations.
   - Adding positive modulo tests verifies negative coordinate toroidal wrapping.

---

## 3. Caveats

- **Headless GPU Testing**: In headless CI environments without a Vulkan display driver (`is_rd_available() == false`), `RenderingDevice` calls gracefully report unavailable status. C++ Doctests use `sizeof()` and `offsetof()` static/runtime checks to verify memory layout independently of GPU driver presence.
- No other caveats.

---

## 4. Remediation Plan & Exact Code Implementation

### 4.1 `GpuMaterialData` Alignment Code Blocks

#### A. C++ Header: `modules/godot_eden/rendering/atc_attribute_pipeline.h`
```cpp
// GPU SSBO std430 Material Structure (32 Bytes per entry)
struct GpuMaterialData {
	uint32_t albedo_rgba8 = 0xFFFFFFFF;       // Offset 0:  Packed RGBA 8-bit channels
	uint32_t normal_oct16 = 0x7F7F;          // Offset 4:  Octahedral 16-bit normal (u8, v8)
	uint32_t roughness_metallic = 0x8000;    // Offset 8:  Packed u16 roughness + u16 metallic
	uint32_t emissive_flags = 0;             // Offset 12: Packed u16 emissive + u16 material flags
	uint32_t emission_rgb565 = 0;            // Offset 16: Packed RGB565 emission color
	float u_scale = 1.0f;                    // Offset 20: Triplanar U scale
	float v_scale = 1.0f;                    // Offset 24: Triplanar V scale
	uint32_t texture_index = 0;              // Offset 28: Bindless texture array index
};
static_assert(sizeof(GpuMaterialData) == 32, "GpuMaterialData must be 32 bytes to match GLSL std430 layout");
```

Update method signature in `atc_attribute_pipeline.h` (line 111):
```cpp
static GpuMaterialData pack_material_to_gpu(const MaterialTag &p_tag);
```

#### B. C++ Implementation: `modules/godot_eden/rendering/atc_attribute_pipeline.cpp`
Update `pack_material_to_gpu` (lines 259–266):
```cpp
GpuMaterialData AtcAttributePipeline::pack_material_to_gpu(const MaterialTag &p_tag) {
	GpuMaterialData gpu_mat;
	gpu_mat.albedo_rgba8 = pack_rgba8(p_tag.albedo);
	gpu_mat.normal_oct16 = pack_oct16(p_tag.normal);
	gpu_mat.roughness_metallic = pack_roughness_metallic(p_tag.roughness, p_tag.metallic);
	gpu_mat.emissive_flags = p_tag.flags;
	gpu_mat.emission_rgb565 = pack_rgb565(p_tag.emission);
	gpu_mat.u_scale = 1.0f;
	gpu_mat.v_scale = 1.0f;
	gpu_mat.texture_index = 0;
	return gpu_mat;
}
```

Update `update_conversion_buffers` (lines 415–438):
```cpp
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

		dst.emission_rgb565 = pack_rgb565(Color(src.emissive, src.emissive, src.emissive));
		dst.u_scale = src.u_scale;
		dst.v_scale = src.v_scale;
		dst.texture_index = src.texture_index;
	}

	dirty = false;
}
```

#### C. GLSL Shader: `modules/godot_eden/shaders/micro_voxel_raymarch.glsl`
Update struct and buffer declaration (lines 32–42):
```glsl
struct GpuMaterialData {
	uint albedo_rgba8;       // Offset 0: 4 B
	uint normal_oct16;       // Offset 4: 4 B
	uint roughness_metallic; // Offset 8: 4 B
	uint emissive_flags;     // Offset 12: 4 B
	uint emission_rgb565;    // Offset 16: 4 B
	float u_scale;           // Offset 20: 4 B
	float v_scale;           // Offset 24: 4 B
	uint texture_index;      // Offset 28: 4 B
};

layout(set = 0, binding = 3, std430) readonly buffer MaterialBuffer {
	GpuMaterialData materials[];
} material_buffer;
```

Update shader sampling usage (lines 140–149):
```glsl
	uint mat_tag = node.material_tag;
	if (mat_tag < material_buffer.materials.length()) {
		GpuMaterialData gpu_mat = material_buffer.materials[mat_tag];
		albedo = unpack_rgba8(gpu_mat.albedo_rgba8);
		vec3 oct_norm = unpack_oct16(gpu_mat.normal_oct16);
		if (length(oct_norm) > 0.001) {
			estimated_normal = oct_norm;
		}
		rough_metal = unpack_rough_metal(gpu_mat.roughness_metallic);
		emission = unpack_rgb565(gpu_mat.emission_rgb565);
	}
```

---

### 4.2 `ClipmapLevelGpu` 64-Byte Alignment Code Blocks

#### A. C++ Header: `modules/godot_eden/rendering/voxel_renderer_rd.h`
```cpp
// GPU SSBO Struct for Clipmap Ring Metadata (std430 64-byte 16-byte aligned layout)
struct ClipmapLevelGpu {
	float center[3] = { 0.0f, 0.0f, 0.0f }; // Offset 0..11  (12B)
	float pad0 = 0.0f;                      // Offset 12..15 (4B)  -> 16-byte boundary
	float voxel_scale = 1.0f;               // Offset 16..19 (4B)
	uint32_t lod_index = 0;                 // Offset 20..23 (4B)
	float blend_margin = 0.0f;              // Offset 24..27 (4B)
	int32_t pad1 = 0;                       // Offset 28..31 (4B)  -> 32-byte boundary
	int32_t grid_size[3] = { 32, 32, 32 };  // Offset 32..43 (12B)
	int32_t pad2 = 0;                       // Offset 44..47 (4B)  -> 48-byte boundary
	int32_t toroidal_offset[3] = { 0, 0, 0 };// Offset 48..59 (12B)
	int32_t pad3 = 0;                       // Offset 60..63 (4B)  -> 64-byte boundary
};
static_assert(sizeof(ClipmapLevelGpu) == 64, "ClipmapLevelGpu must be 64 bytes to match GPU std430 layout");
```

#### B. C++ Implementation: `modules/godot_eden/rendering/voxel_renderer_rd.cpp`
Update `update_lod_clipmap` (lines 158–192):
```cpp
void VoxelRendererRD::update_lod_clipmap(const Vector3 &p_camera_position) {
	camera_position = p_camera_position;
	clipmap_levels.resize(lod_levels);

	for (int i = 0; i < lod_levels; i++) {
		float scale = base_voxel_scale * Math::pow(2.0f, (float)i);
		Vector3 snapped(
				Math::floor(camera_position.x / scale) * scale,
				Math::floor(camera_position.y / scale) * scale,
				Math::floor(camera_position.z / scale) * scale);
		clipmap_levels.write[i].center[0] = snapped.x;
		clipmap_levels.write[i].center[1] = snapped.y;
		clipmap_levels.write[i].center[2] = snapped.z;
		clipmap_levels.write[i].pad0 = 0.0f;

		clipmap_levels.write[i].voxel_scale = scale;
		clipmap_levels.write[i].lod_index = (uint32_t)i;

		float outer_radius = (float)clipmap_grid_extent * 0.5f * scale;
		clipmap_levels.write[i].blend_margin = outer_radius * 0.15f;
		clipmap_levels.write[i].pad1 = 0;

		clipmap_levels.write[i].grid_size[0] = clipmap_grid_extent;
		clipmap_levels.write[i].grid_size[1] = clipmap_grid_extent;
		clipmap_levels.write[i].grid_size[2] = clipmap_grid_extent;
		clipmap_levels.write[i].pad2 = 0;

		Vector3i grid_cell(
				(int)Math::floor(snapped.x / scale),
				(int)Math::floor(snapped.y / scale),
				(int)Math::floor(snapped.z / scale));

		// Positive toroidal modulo wrap: (x % extent + extent) % extent
		int extent = clipmap_grid_extent;
		clipmap_levels.write[i].toroidal_offset[0] = (grid_cell.x % extent + extent) % extent;
		clipmap_levels.write[i].toroidal_offset[1] = (grid_cell.y % extent + extent) % extent;
		clipmap_levels.write[i].toroidal_offset[2] = (grid_cell.z % extent + extent) % extent;
		clipmap_levels.write[i].pad3 = 0;
	}

	if (is_rd_available()) {
		upload_clipmap_ssbo();
	}
}
```

#### C. GLSL Shader: `modules/godot_eden/shaders/clipmap_lod.glsl`
```glsl
struct ClipmapLevel {
	vec3 center;           // Offset 0..11
	float pad0;            // Offset 12..15
	float voxel_scale;     // Offset 16..19
	uint lod_index;        // Offset 20..23
	float blend_margin;    // Offset 24..27
	int pad1;              // Offset 28..31
	ivec3 grid_size;       // Offset 32..43 (aligned to 16)
	int pad2;              // Offset 44..47
	ivec3 toroidal_offset; // Offset 48..59 (aligned to 16)
	int pad3;              // Offset 60..63
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

	float base_scale = (params.base_voxel_scale > 0.0) ? params.base_voxel_scale : 0.25;
	uint extent = (params.grid_extent > 0u) ? params.grid_extent : 32u;

	float scale = base_scale * pow(2.0, float(id));
	vec3 snapped_center = floor(params.camera_pos / scale) * scale;

	clipmap.levels[id].center = snapped_center;
	clipmap.levels[id].pad0 = 0.0;
	clipmap.levels[id].voxel_scale = scale;
	clipmap.levels[id].lod_index = id;

	float outer_radius = float(extent) * 0.5 * scale;
	clipmap.levels[id].blend_margin = outer_radius * 0.15;
	clipmap.levels[id].pad1 = 0;

	clipmap.levels[id].grid_size = ivec3(int(extent));
	clipmap.levels[id].pad2 = 0;

	// Positive toroidal grid offset calculation
	ivec3 grid_cell = ivec3(floor(snapped_center / scale));
	ivec3 ext = ivec3(int(extent));
	clipmap.levels[id].toroidal_offset = ((grid_cell % ext) + ext) % ext;
	clipmap.levels[id].pad3 = 0;
}
```

#### D. GLSL Shader: `modules/godot_eden/shaders/micro_voxel_raymarch.glsl`
Update `ClipmapLevel` struct in `micro_voxel_raymarch.glsl` (lines 19–26) to match the identical 64-byte definition above.

---

### 4.3 Updated C++ Doctests: `modules/godot_eden/tests/test_rendering.h`

Add the following three TEST_CASE blocks to `modules/godot_eden/tests/test_rendering.h`:

```cpp
TEST_CASE("[Modules][GodotEden] GPU std430 Struct Memory Alignment & Sizes") {
	SUBCASE("GpuMaterialData Alignment") {
		CHECK(sizeof(GpuMaterialData) == 32);
		CHECK(offsetof(GpuMaterialData, albedo_rgba8) == 0);
		CHECK(offsetof(GpuMaterialData, normal_oct16) == 4);
		CHECK(offsetof(GpuMaterialData, roughness_metallic) == 8);
		CHECK(offsetof(GpuMaterialData, emissive_flags) == 12);
		CHECK(offsetof(GpuMaterialData, emission_rgb565) == 16);
		CHECK(offsetof(GpuMaterialData, u_scale) == 20);
		CHECK(offsetof(GpuMaterialData, v_scale) == 24);
		CHECK(offsetof(GpuMaterialData, texture_index) == 28);
	}

	SUBCASE("ClipmapLevelGpu Alignment") {
		CHECK(sizeof(ClipmapLevelGpu) == 64);
		CHECK(offsetof(ClipmapLevelGpu, center) == 0);
		CHECK(offsetof(ClipmapLevelGpu, pad0) == 12);
		CHECK(offsetof(ClipmapLevelGpu, voxel_scale) == 16);
		CHECK(offsetof(ClipmapLevelGpu, lod_index) == 20);
		CHECK(offsetof(ClipmapLevelGpu, blend_margin) == 24);
		CHECK(offsetof(ClipmapLevelGpu, pad1) == 28);
		CHECK(offsetof(ClipmapLevelGpu, grid_size) == 32);
		CHECK(offsetof(ClipmapLevelGpu, pad2) == 44);
		CHECK(offsetof(ClipmapLevelGpu, toroidal_offset) == 48);
		CHECK(offsetof(ClipmapLevelGpu, pad3) == 60);
	}

	SUBCASE("Push Constants Sizes") {
		CHECK(sizeof(RaymarchPushConstants) == 40);
		CHECK(sizeof(ClipmapPushConstants) == 28);
	}
}

TEST_CASE("[Modules][GodotEden] Clipmap Toroidal Positive Modulo Calculations") {
	SUBCASE("Negative Camera Position Toroidal Offset Positive Wrap") {
		int extent = 32;
		int cell_x = (int)Math::floor(-10.0f / 1.0f);  // -10
		int cell_y = (int)Math::floor(-50.0f / 1.0f);  // -50
		int cell_z = (int)Math::floor(-100.0f / 1.0f); // -100

		int offset_x = (cell_x % extent + extent) % extent; // (-10 % 32 + 32) % 32 = 22
		int offset_y = (cell_y % extent + extent) % extent; // (-50 % 32 + 32) % 32 = 14
		int offset_z = (cell_z % extent + extent) % extent; // (-100 % 32 + 32) % 32 = 28

		CHECK(offset_x >= 0);
		CHECK(offset_x < extent);
		CHECK(offset_y >= 0);
		CHECK(offset_y < extent);
		CHECK(offset_z >= 0);
		CHECK(offset_z < extent);

		CHECK(offset_x == 22);
		CHECK(offset_y == 14);
		CHECK(offset_z == 28);
	}
}

TEST_CASE("[Modules][GodotEden] VoxelRendererRD Push Constant Bounds & Buffer Info") {
	VoxelRendererRD *renderer = memnew(VoxelRendererRD);
	REQUIRE(renderer != nullptr);

	SUBCASE("Raymarch Push Constant Bounds") {
		RaymarchPushConstants pc;
		CHECK(sizeof(RaymarchPushConstants) == 40);
		CHECK(pc.fov > 0.0f);
		CHECK(pc.max_distance > 0.0f);
		CHECK(pc.screen_size[0] > 0.0f);
		CHECK(pc.screen_size[1] > 0.0f);
	}

	SUBCASE("GPU Buffer Info Keys") {
		Dictionary info = renderer->get_gpu_buffer_info();
		CHECK(info.has("rd_available"));
		CHECK(info.has("pipelines_initialized"));
		CHECK(info.has("lod_levels"));
		CHECK(info.has("view_distance"));
		CHECK(info.has("render_target_size"));
		CHECK(info.has("svo_ssbo_valid"));
		CHECK(info.has("clipmap_ssbo_valid"));
		CHECK(info.has("material_ssbo_valid"));
	}

	memdelete(renderer);
}
```

---

## 5. Verification Method

1. **Struct Size & Offset Verification**:
   Inspect `atc_attribute_pipeline.h` and `voxel_renderer_rd.h` to verify `sizeof(GpuMaterialData) == 32` and `sizeof(ClipmapLevelGpu) == 64`.
2. **GLSL Compilation Check**:
   Confirm `micro_voxel_raymarch.glsl` and `clipmap_lod.glsl` contain matching struct definitions and no undeclared type references.
3. **C++ Unit Tests Execution**:
   Run Doctest unit test suite:
   ```bash
   godot --test --test-suite="[Modules][GodotEden]"
   ```
