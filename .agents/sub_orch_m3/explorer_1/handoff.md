# Handoff Report: Milestone 3 Exploration & Technical Design

## 1. Observation

### 1.1 Existing Module Codebase Layout & Files
- `modules/godot_eden/config.py` (34 lines):
  - Line 11: `env.Append(CPPDEFINES=["GODOT_EDEN_ENABLED"])`
  - Lines 14-29: `get_doc_classes()` registers `VoxelWorld`, `VoxelVolume`, `VoxelRenderer`, `VoxelStreamer`, `VoxelGenerator`, `VoxelGeneratorNoise`, `VoxelBuffer`, `VoxelDataMap`, `LodOctree`, `SpatialLock3D`, `VoxelBlockSerializer`, `VoxelStreamSQLite`, `VoxelStreamRegionFiles`.
- `modules/godot_eden/SCsub` (37 lines):
  - Lines 14-18: Calls `RD_GLSL` header builder for `shaders/micro_voxel_raymarch.glsl` and `shaders/clipmap_lod.glsl`.
  - Line 21: `env_godot_eden.Depends(Glob("shaders/*.glsl.gen.h"), ["#glsl_builders.py"])`
  - Lines 30-33: Collects `.cpp` sources from `nodes/`, `storage/`, `streaming/`, `generators/`.
- `modules/godot_eden/register_types.cpp` (46 lines):
  - Lines 22-38: `initialize_godot_eden_module(...)` registers M1 and M2 classes with `GDREGISTER_CLASS`.
- `modules/godot_eden/shaders/micro_voxel_raymarch.glsl` (34 lines):
  - Stub compute shader with `local_size` (8, 8, 1), `image2D out_color` at `(set=0, binding=0)`, and `PushConstants` struct (camera_pos, fov, camera_dir, max_distance, screen_size).
- `modules/godot_eden/shaders/clipmap_lod.glsl` (37 lines):
  - Concentric clipmap compute shader with `local_size` (64, 1, 1), `ClipmapBuffer` SSBO at `(set=0, binding=0)`, and `PushConstants` (camera_pos, max_lod_levels).
- `modules/godot_eden/storage/lod_octree.h` (106 lines):
  - Line 16: `struct SvoNode` (16 bytes aligned for Vulkan SSBO: `child_mask`, `first_child_idx`, `material_tag`, `sdf_value`).
  - Line 104: `PackedByteArray get_ssbo_buffer_bytes() const;` serializes contiguous `SvoNode` array.
- `modules/godot_eden/tests/test_main.h` (729 lines):
  - Unit test suite covering ClassDB registrations, properties, buffer palette compression, octree deduplication, spatial locks, noise generator, and persistence.

---

## 2. Logic Chain

1. **Godot 4 RenderingDevice API Integration**:
   - `RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();` provides direct access to Vulkan compute commands.
   - SCons `RD_GLSL` calls `glsl_builders.build_rd_headers`, generating `micro_voxel_raymarch.glsl.gen.h` and `clipmap_lod.glsl.gen.h` defining `MicroVoxelRaymarchShaderRD::byte_code` and `ClipmapLodShaderRD::byte_code`.
   - Creating shader RIDs (`rd->shader_create_from_spirv(...)`) and compute pipelines (`rd->compute_pipeline_create(...)`) allows `VoxelRendererRD` to dispatch compute raymarching directly on Vulkan GPU.

2. **Buffer & Resource Pipeline Binding**:
   - `LodOctree::get_ssbo_buffer_bytes()` outputs a `PackedByteArray` matching the 16-byte `SvoNode` layout in `micro_voxel_raymarch.glsl`.
   - `VoxelRendererRD` uploads this byte vector into a Vulkan SSBO (`rd->storage_buffer_create(...)`) at Set 0, Binding 1.
   - Clipmap LOD ring metadata (32 bytes per ring level) is uploaded to a Clipmap SSBO at Set 0, Binding 2.
   - ATC material attributes (`AtcAttributePipeline`) are uploaded to a Material Palette SSBO at Set 0, Binding 3.
   - Render target image is bound at Set 0, Binding 0.
   - Uniform sets (`rd->uniform_set_create(...)`) and push constants (`rd->compute_list_set_push_constant(...)`) are bound before calling `rd->compute_list_dispatch(...)`.

3. **Concentric Clipmap & ATC Attribute Systems**:
   - `update_lod_clipmap(...)` calculates camera-centered snapped bounds for concentric LOD rings ($2^l$ scale) using `clipmap_lod.glsl`.
   - `AtcAttributePipeline` converts material tags into albedo colors and surface properties for GLSL raymarching.
   - `PhysicsMeshGenerator` produces fallback collision hulls (`ConcavePolygonShape3D`) using greedy meshing or dual contouring for physics interactions.

4. **Build & Module Wiring**:
   - Creating `rendering/voxel_renderer_rd.h/cpp`, `rendering/atc_attribute_pipeline.h/cpp`, and `rendering/physics_mesh_generator.h/cpp` requires adding `env_godot_eden.add_source_files(sources, "rendering/*.cpp")` in `SCsub`.
   - Updating `config.py` with `"VoxelRendererRD"`, `"AtcAttributePipeline"`, and `"PhysicsMeshGenerator"` in `get_doc_classes()`.
   - Updating `register_types.cpp` with header includes and `GDREGISTER_CLASS(...)` calls.

---

## 3. Caveats

- In non-Vulkan / OpenGL compatibility or headless modes where `RenderingDevice` is null, `VoxelRendererRD` must check `if (rd == nullptr)` and return early to prevent null pointer dereferences.
- Raymarching performance on high-resolution displays depends on workgroup dispatch sizing (`(width + 7) / 8, (height + 7) / 8, 1`) and step limits (256 DDA steps).

---

## 4. Conclusion

The technical design for Milestone 3 is complete and ready for implementation.
The required components (`VoxelRendererRD`, `AtcAttributePipeline`, `PhysicsMeshGenerator`, compute shader extensions, `SCsub`, `config.py`, `register_types.cpp`, and doctest unit tests) have been fully specified with exact C++ header declarations, GLSL layout bindings, and build configuration hooks.

---

## 5. Verification Method

To verify the Milestone 3 implementation once completed by Implementer:

1. **Compilation Test**:
   Execute SCons build for Godot 4 target with `godot_eden` enabled:
   `scons platform=windows target=editor` or run unit tests:
   `godot --test --test-suite="*GodotEden*"`
2. **Unit Test Verification**:
   Inspect `modules/godot_eden/tests/test_main.h` for new test cases covering:
   - `VoxelRendererRD` instantiation & property getters/setters
   - `AtcAttributePipeline` material registration & GPU palette byte serialization
   - `PhysicsMeshGenerator` collision shape face generation
3. **E2E Integration Test**:
   Execute runner script:
   `python tests/e2e/runner.py`
