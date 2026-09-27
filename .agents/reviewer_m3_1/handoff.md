# Reviewer Handoff Report — Milestone 3 Vulkan GPU Compute Raymarcher & GLSL Shaders

**Reviewer**: Reviewer 1 (`reviewer_m3_1`)  
**Date**: 2026-08-05  
**Target Milestone**: Milestone 3 (Micro-Voxel Renderer & Shaders Pipeline)  
**Verdict**: **REQUEST_CHANGES**

---

## 1. Observation

### Observation 1.1: Undeclared GLSL Type `AtcPackedGpuMaterial`
- **File**: `modules/godot_eden/shaders/micro_voxel_raymarch.glsl`
- **Lines 32–42**:
```glsl
32: struct GpuMaterialData {
33: 	uint albedo_rgba8;
34: 	uint normal_oct16;
35: 	uint roughness_metallic;
36: 	uint emissive_flags;
37: 	uint emission_rgb565;
38: };
39: 
40: layout(set = 0, binding = 3, std430) readonly buffer MaterialBuffer {
41: 	AtcPackedGpuMaterial materials[];
42: } material_buffer;
```
- **Line 141**:
```glsl
141: 						AtcPackedGpuMaterial gpu_mat = material_buffer.materials[mat_tag];
```
- Direct observation: Struct definition on line 32 is named `GpuMaterialData`, but lines 41 and 141 refer to `AtcPackedGpuMaterial`. `AtcPackedGpuMaterial` is not defined anywhere in the GLSL file.

### Observation 1.2: Missing Vulkan `RenderingDevice::uniform_set_create` Calls
- **File**: `modules/godot_eden/rendering/voxel_renderer_rd.h` and `voxel_renderer_rd.cpp`
- **Header Lines 73–74**:
```cpp
73: 	RID raymarch_uniform_set;
74: 	RID clipmap_uniform_set;
```
- **Implementation Lines 292–294 & 322–324**:
```cpp
292: 	if (raymarch_uniform_set.is_valid()) {
293: 		rd->compute_list_bind_uniform_set(compute_list, raymarch_uniform_set, 0);
294: 	}
...
322: 	if (clipmap_uniform_set.is_valid()) {
323: 		rd->compute_list_bind_uniform_set(compute_list, clipmap_uniform_set, 0);
324: 	}
```
- Direct observation: `upload_svo_ssbo()`, `upload_clipmap_ssbo()`, and `upload_material_palette_ssbo()` create storage buffer RIDs via `rd->storage_buffer_create(...)`, but no code ever populates or calls `rd->uniform_set_create(...)` for `raymarch_uniform_set` or `clipmap_uniform_set`. Thus, `raymarch_uniform_set.is_valid()` remains `false`, and compute dispatches skip binding uniform set 0.

### Observation 1.3: Struct Layout Mismatch for `GpuMaterialData` (C++ vs GLSL)
- **C++ Header**: `modules/godot_eden/rendering/atc_attribute_pipeline.h` (Lines 20–29):
```cpp
struct GpuMaterialData {
	uint32_t albedo_rgba8 = 0xFFFFFFFF;
	uint32_t normal_oct16 = 0x7F7F;
	uint32_t roughness_metallic = 0x8000;
	uint32_t emissive_flags = 0;
	float u_scale = 1.0f;
	float v_scale = 1.0f;
	uint32_t texture_index = 0;
	uint32_t reserved = 0;
};
```
- **GLSL Shader**: `modules/godot_eden/shaders/micro_voxel_raymarch.glsl` (Lines 32–38):
```glsl
struct GpuMaterialData {
	uint albedo_rgba8;
	uint normal_oct16;
	uint roughness_metallic;
	uint emissive_flags;
	uint emission_rgb565;
};
```
- Direct observation: C++ allocates 32 bytes per material entry (`GpuMaterialData`), whereas GLSL defines 5 fields (20 bytes). Layout offsets for material properties mismatch after `emissive_flags`.

### Observation 1.4: Hardcoded AABB Bounds & Traversal Step in `micro_voxel_raymarch.glsl`
- **File**: `modules/godot_eden/shaders/micro_voxel_raymarch.glsl` (Lines 111–112):
```glsl
111: 	vec3 root_min = vec3(-512.0);
112: 	vec3 root_max = vec3(512.0);
```
- Direct observation: SVO raymarching assumes hardcoded bounding box `[-512, 512]` regardless of clipmap scale, base scale, or octree depth.

---

## 2. Logic Chain

1. **GLSL Compilation Logic**:
   - Line 41 of `micro_voxel_raymarch.glsl` references `AtcPackedGpuMaterial`.
   - `AtcPackedGpuMaterial` is not a primitive GLSL type and is not declared as a struct in `micro_voxel_raymarch.glsl`.
   - SCons builder `RD_GLSL` calls `glslangValidator`/`glsl_builders.py`, which will fail compilation due to an undeclared identifier.
   - **Conclusion**: Shader compilation is broken.

2. **RenderingDevice Dispatch Logic**:
   - `micro_voxel_raymarch.glsl` declares bindings 0..3 at `set = 0`.
   - `VoxelRendererRD` creates storage buffers but never constructs `RD::Uniform` descriptors or calls `rd->uniform_set_create()`.
   - `raymarch_uniform_set` remains `RID()`.
   - In `dispatch_raymarch_compute()`, `rd->compute_list_bind_uniform_set` is skipped because `raymarch_uniform_set.is_valid()` is `false`.
   - Dispatching a compute shader with unbound uniform set 0 triggers Vulkan validation errors and results in invalid shader execution / GPU fault.
   - **Conclusion**: RenderingDevice C++ API integration for uniform sets is incomplete.

3. **std430 Alignment & Struct Layout Logic**:
   - C++ uploads `AtcAttributePipeline` material buffer containing array of 32-byte `GpuMaterialData` structs.
   - GLSL reads buffer using a 20-byte struct layout.
   - Array indexing in shader will offset by 20 bytes per element instead of 32 bytes, corrupting all material reads for index > 0.
   - **Conclusion**: Material SSBO std430 layout between C++ and GLSL is mismatched.

---

## 3. Caveats

- **No Integrity Violations Detected**: Code contains real logic, genuine math implementations, proper ClassDB bindings, and SCons hooks. Failure is due to compilation/integration bugs rather than cheating or facade implementations.
- **Headless Unit Test Pass**: C++ unit tests (`test_rendering.h`) pass under headless conditions because `RenderingDevice::get_singleton()` returns `nullptr` in headless mode, masking the missing uniform set initialization and shader SPIR-V runtime binding errors.

---

## 4. Conclusion

**Verdict**: **REQUEST_CHANGES**

The Vulkan GPU Compute Raymarcher implementation contains two **Critical** findings and two **Major** findings that block shader compilation and GPU execution:
1. **[Critical]** GLSL compilation failure in `micro_voxel_raymarch.glsl` due to undeclared identifier `AtcPackedGpuMaterial`.
2. **[Critical]** Incomplete Vulkan `RenderingDevice` integration in `VoxelRendererRD`: Uniform set 0 (`raymarch_uniform_set` and `clipmap_uniform_set`) is never created via `rd->uniform_set_create()`.
3. **[Major]** Mismatched std430 struct layout for `GpuMaterialData` between C++ (32 bytes) and GLSL (20 bytes).
4. **[Major]** Hardcoded root AABB `[-512, 512]` in `micro_voxel_raymarch.glsl` limiting planetary clipmap scaling.

---

## 5. Verification Method

To verify the requested fixes:
1. **Fix `micro_voxel_raymarch.glsl`**:
   Replace `AtcPackedGpuMaterial` with `GpuMaterialData` and match the 32-byte C++ struct layout:
   ```glsl
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
   ```
2. **Implement Uniform Set Creation in `VoxelRendererRD`**:
   Add uniform set instantiation in `VoxelRendererRD` using `RenderingDevice::uniform_set_create()`:
   - Construct `RD::Uniform` vector for `set = 0` (out_color image2D, svo_dag SSBO, clipmap SSBO, material_buffer SSBO).
   - Store created RID into `raymarch_uniform_set`.
3. **Inspect Code & Re-run Tests**:
   - Inspect `micro_voxel_raymarch.glsl` and `voxel_renderer_rd.cpp`.
   - Run `python tests/e2e/runner.py --feature F9` and `python tests/e2e/runner.py --feature F10`.
