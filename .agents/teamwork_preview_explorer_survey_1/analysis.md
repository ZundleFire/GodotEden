# Technical Survey Report — Godot 4 C++ Engine Module Layout & ClassDB Bindings

## Executive Summary
This technical survey details the Godot 4 C++ built-in engine module layout (`modules/godot_eden`), `ClassDB` binding architecture, C++ object hierarchy, engine integration hooks, and build/verification infrastructure for project **GodotEden**. 

GodotEden is designed as a high-performance native built-in C++ engine module within Godot 4, providing micro-voxel rendering on a planetary scale. This document provides an exhaustive reference covering module configuration (`config.py`, `SCsub`, `register_types.h/cpp`), ClassDB bindings across `Node3D`, `Resource`, and `RefCounted` base classes for all core voxel types (`VoxelWorld`, `VoxelVolume`, `VoxelRenderer`, `VoxelBuffer`/`VoxelChunk`, `VoxelStreamer`/`VoxelStream`, `VoxelGenerator`, `AtcAttributePipeline`/`VoxelMaterial`), and low-level Vulkan `RenderingDevice` compute shader integration.

---

## 1. Standard Godot 4 Module Directory Layout

### 1.1 Root Module Structure
Godot 4 built-in modules reside under `modules/` in the engine source tree. The primary module directory for GodotEden is `modules/godot_eden/`.

```
modules/godot_eden/
├── config.py                 # SCons module configuration & documentation registration
├── SCsub                     # SCons build script & GLSL SPIR-V compilation rules
├── register_types.h          # Module initialization/uninitialization header
├── register_types.cpp        # ClassDB class registration at scene initialization level
├── README.md                 # Module documentation
├── nodes/                    # Scene graph nodes and resources
│   ├── voxel_world.h / .cpp        # VoxelWorld Node3D spatial manager
│   ├── voxel_volume.h / .cpp       # VoxelVolume Resource metadata container
│   ├── voxel_renderer.h / .cpp     # VoxelRenderer Node3D renderer node
│   └── voxel_generator.h / .cpp    # VoxelGenerator Resource abstract base
├── storage/                  # In-memory storage & SVO/DAG structures
│   ├── voxel_buffer.h / .cpp       # VoxelBuffer (16^3 chunk storage with palette compression)
│   ├── voxel_data_map.h / .cpp     # VoxelDataMap (Clipmap chunk hash map)
│   └── lod_octree.h / .cpp         # LodOctree (SVO / SVDAG deduplicated DAG index pool)
├── streaming/                # Chunk streaming, thread locks, & persistence
│   ├── spatial_lock_3d.h / .cpp    # SpatialLock3D (3D spatial reader-writer locks)
│   ├── voxel_streamer.h / .cpp     # VoxelStreamer (View-distance chunk request manager)
│   ├── voxel_block_serializer.h    # Zstd binary block serializer
│   ├── voxel_stream_sqlite.h       # SQLite delta persistence engine
│   └── voxel_stream_region_files.h # Memory-mapped (mmap) region files persistence
├── generators/               # Procedural density & SDF generators
│   └── voxel_generator_noise.h / .cpp  # VoxelGeneratorNoise (FastNoise2 3D density field)
├── rendering/                # Compute raymarching, materials & meshing
│   ├── voxel_renderer_rd.h / .cpp      # VoxelRendererRD (Vulkan RenderingDevice raymarcher)
│   ├── atc_attribute_pipeline.h / .cpp # AtcAttributePipeline (Attribute packing & GPU SSBO)
│   └── physics_mesh_generator.h / .cpp  # PhysicsMeshGenerator (Greedy & Dual Contouring)
├── shaders/                  # GLSL compute shaders
│   ├── micro_voxel_raymarch.glsl   # Vulkan compute raymarching shader
│   └── clipmap_lod.glsl            # Dynamic clipmap LOD compute shader
└── tests/                    # Native C++ doctest test suites
    ├── test_main.h                 # Core storage, streaming & ClassDB unit tests
    └── test_rendering.h            # RenderingDevice & ATC pipeline unit tests
```

### 1.2 Configuration Script (`config.py`)
`config.py` acts as the entry point for Godot's SCons build system when probing modules.

```python
def can_build(env, platform):
    if env.get("disable_3d", False):
        return False
    return True

def configure(env):
    env.Append(CPPDEFINES=["GODOT_EDEN_ENABLED"])

def get_doc_classes():
    return [
        "VoxelWorld", "VoxelVolume", "VoxelRenderer", "VoxelStreamer",
        "VoxelGenerator", "VoxelGeneratorNoise", "VoxelBuffer", "VoxelDataMap",
        "LodOctree", "SpatialLock3D", "VoxelBlockSerializer", "VoxelStreamSQLite",
        "VoxelStreamRegionFiles", "VoxelRendererRD", "AtcAttributePipeline",
        "PhysicsMeshGenerator",
    ]

def get_doc_path():
    return "doc_classes"
```
- **`can_build(env, platform)`**: Evaluates build eligibility. Gated by `disable_3d=False`.
- **`configure(env)`**: Injects `GODOT_EDEN_ENABLED` into `CPPDEFINES` compiler flags globally.
- **`get_doc_classes()`**: Registers module classes for Godot Engine Inspector documentation generation.

### 1.3 SCons Module Build Script (`SCsub`)
`SCsub` handles compiler environment setup and shader compilation.

```python
#!/usr/bin/env python
from misc.utility.scons_hints import *
import os

Import("env")
Import("env_modules")

env_godot_eden = env_modules.Clone()
env_godot_eden.Prepend(CPPPATH=["#modules/godot_eden"])

# GLSL Compute Shader Header Builders via RenderingDevice SPIR-V pipeline
if os.path.exists(env_godot_eden.File("shaders/micro_voxel_raymarch.glsl").rfile().abspath):
    env_godot_eden.RD_GLSL("shaders/micro_voxel_raymarch.glsl")

if os.path.exists(env_godot_eden.File("shaders/clipmap_lod.glsl").rfile().abspath):
    env_godot_eden.RD_GLSL("shaders/clipmap_lod.glsl")

env_godot_eden.Depends(Glob("shaders/*.glsl.gen.h"), ["#glsl_builders.py"])

sources = []
env_godot_eden.add_source_files(sources, "*.cpp")
env_godot_eden.add_source_files(sources, "nodes/*.cpp")
env_godot_eden.add_source_files(sources, "storage/*.cpp")
env_godot_eden.add_source_files(sources, "streaming/*.cpp")
env_godot_eden.add_source_files(sources, "generators/*.cpp")
env_godot_eden.add_source_files(sources, "rendering/*.cpp")

env.modules_sources += sources
```
- Clones `env_modules` to prevent compiler flag pollution across modules.
- Prepends `#modules/godot_eden` to `CPPPATH`, enabling clean relative includes (`#include "nodes/voxel_world.h"`).
- Uses `RD_GLSL` to invoke `glsl_builders.py`, compiling `.glsl` files into `shaders/*.glsl.gen.h` C++ SPIR-V headers for runtime loading.

### 1.4 Module Registration Files (`register_types.h` and `register_types.cpp`)
- **`register_types.h`**:
  ```cpp
  #pragma once
  #include "modules/register_module_types.h"

  void initialize_godot_eden_module(ModuleInitializationLevel p_level);
  void uninitialize_godot_eden_module(ModuleInitializationLevel p_level);
  ```
- **`register_types.cpp`**:
  ```cpp
  #include "register_types.h"
  #include "core/object/class_db.h"
  // Includes for all module headers...

  void initialize_godot_eden_module(ModuleInitializationLevel p_level) {
      if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
          GDREGISTER_CLASS(VoxelWorld);
          GDREGISTER_CLASS(VoxelVolume);
          GDREGISTER_CLASS(VoxelRenderer);
          GDREGISTER_CLASS(VoxelStreamer);
          GDREGISTER_CLASS(VoxelGenerator);
          GDREGISTER_CLASS(VoxelGeneratorNoise);
          GDREGISTER_CLASS(VoxelBuffer);
          GDREGISTER_CLASS(VoxelDataMap);
          GDREGISTER_CLASS(LodOctree);
          GDREGISTER_CLASS(SpatialLock3D);
          GDREGISTER_CLASS(VoxelBlockSerializer);
          GDREGISTER_CLASS(VoxelStreamSQLite);
          GDREGISTER_CLASS(VoxelStreamRegionFiles);
          GDREGISTER_CLASS(VoxelRendererRD);
          GDREGISTER_CLASS(AtcAttributePipeline);
          GDREGISTER_CLASS(PhysicsMeshGenerator);
      }
  }

  void uninitialize_godot_eden_module(ModuleInitializationLevel p_level) {
      if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
          return;
      }
  }
  ```
- Gated by `p_level == MODULE_INITIALIZATION_LEVEL_SCENE` to ensure core engine Singletons (`ClassDB`, `Variant`, `SceneTree`, `RenderingServer`) are initialized before module classes register.

---

## 2. ClassDB Binding Requirements & Inheritance Hierarchy

### 2.1 C++ Inheritance Tree
All exposed classes inherit from standard Godot core base types:

```
Godot Core Base Hierarchy:
Object
├── RefCounted (Reference counted objects, managed via Ref<T>)
│   ├── VoxelBuffer               # 16^3 voxel block storage (uniform/palette/raw) [VoxelChunk]
│   ├── VoxelDataMap              # Hash-map chunk storage for clipmap regions
│   ├── LodOctree                 # SVO / SVDAG pointerless octree index pool
│   ├── SpatialLock3D             # Reader-writer spatial 3D block locking
│   ├── VoxelStreamer             # View-distance chunk request manager [VoxelStream]
│   ├── VoxelBlockSerializer      # Zstd block compression & serialization
│   ├── VoxelStreamSQLite         # SQLite delta persistence backend
│   ├── VoxelStreamRegionFiles    # Memory-mapped (mmap) region files persistence
│   ├── AtcAttributePipeline      # Material attribute conversion & GPU SSBO palette [VoxelMaterial]
│   └── PhysicsMeshGenerator      # Greedy Mesh / Dual Contouring collision hull generator
├── Resource (Serializable Data Containers)
│   ├── VoxelVolume               # Volume metadata resource (chunk size, LOD levels)
│   └── VoxelGenerator            # Abstract procedural voxel generator base
│       └── VoxelGeneratorNoise   # FastNoise2 3D density field & spherical SDF generator
└── Node (Scene Graph Entities)
    └── Node3D (3D Spatial Nodes)
        ├── VoxelWorld            # Main spatial scene manager node
        ├── VoxelRenderer         # High-level spatial rendering node
        └── VoxelRendererRD       # Vulkan RenderingDevice compute raymarcher node
```

### 2.2 ClassDB Requirements & Survey for Core Voxel Types

#### 1. `VoxelWorld` (`Node3D`)
- **Inheritance**: `class VoxelWorld : public Node3D`
- **Class Macro**: `GDCLASS(VoxelWorld, Node3D);`
- **ClassDB Registration**:
  - `ClassDB::bind_method(D_METHOD("set_voxel_volume", "volume"), &VoxelWorld::set_voxel_volume);`
  - `ClassDB::bind_method(D_METHOD("get_voxel_volume"), &VoxelWorld::get_voxel_volume);`
  - `ClassDB::bind_method(D_METHOD("set_voxel_size", "size"), &VoxelWorld::set_voxel_size);`
  - `ClassDB::bind_method(D_METHOD("get_voxel_size"), &VoxelWorld::get_voxel_size);`
  - `ClassDB::bind_method(D_METHOD("update_world", "camera_pos"), &VoxelWorld::update_world);`
- **Properties**:
  - `ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "voxel_volume", PROPERTY_HINT_RESOURCE_TYPE, "VoxelVolume"), "set_voxel_volume", "get_voxel_volume");`
  - `ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "voxel_size", PROPERTY_HINT_RANGE, "0.01,100.0,0.01,or_greater"), "set_voxel_size", "get_voxel_size");`

#### 2. `VoxelVolume` (`Resource`)
- **Inheritance**: `class VoxelVolume : public Resource`
- **Class Macro**: `GDCLASS(VoxelVolume, Resource);`
- **ClassDB Registration**:
  - Binds getter/setter pairs for `chunk_size` (`Vector3i`), `max_lod_levels` (`int`), `volume_name` (`String`).
  - Method bindings: `get_voxel_count_per_chunk()`, `get_bounds()`.
- **Properties**:
  - `ADD_PROPERTY(PropertyInfo(Variant::VECTOR3I, "chunk_size"), "set_chunk_size", "get_chunk_size");`
  - `ADD_PROPERTY(PropertyInfo(Variant::INT, "max_lod_levels", PROPERTY_HINT_RANGE, "1,16,1"), "set_max_lod_levels", "get_max_lod_levels");`

#### 3. `VoxelRenderer` & `VoxelRendererRD` (`Node3D`)
- **Inheritance**: `class VoxelRenderer : public Node3D`, `class VoxelRendererRD : public Node3D`
- **Class Macro**: `GDCLASS(VoxelRendererRD, Node3D);`
- **ClassDB Registration**:
  - Binds methods: `set_volume()`, `get_volume()`, `set_svo_octree()`, `set_data_map()`, `set_atc_pipeline()`, `upload_svo_ssbo()`, `dispatch_raymarch_compute()`.
  - Properties: `enabled`, `lod_levels`, `view_distance`, `render_target_size`, `fov`, `base_voxel_scale`.
- **Inspector Integration**: Overrides `PackedStringArray get_configuration_warnings() const override` to alert users if required resources (`VoxelVolume`, `LodOctree`) are unassigned in the scene graph.

#### 4. `VoxelBuffer` / `VoxelChunk` (`RefCounted`)
- **Inheritance**: `class VoxelBuffer : public RefCounted`
- **Class Macro**: `GDCLASS(VoxelBuffer, RefCounted);`
- **ClassDB Registration**:
  - Binds channels: `CHANNEL_SDF`, `CHANNEL_MATERIAL`, `CHANNEL_COLOR`, `CHANNEL_CUSTOM`.
  - Binds compression types: `COMPRESSION_UNIFORM`, `COMPRESSION_PALETTE`, `COMPRESSION_RAW`.
  - Methods: `set_voxel_f()`, `get_voxel_f()`, `set_voxel_u()`, `get_voxel_u()`, `fill_f()`, `fill_u()`, `compress_palette()`, `optimize()`, `duplicate_buffer()`, `get_channel_raw_bytes()`.
  - Enums registered via `VARIANT_ENUM_CAST(VoxelBuffer::ChannelId);` and `VARIANT_ENUM_CAST(VoxelBuffer::CompressionType);`.

#### 5. `VoxelStreamer` / `VoxelStream` (`RefCounted`)
- **Inheritance**: `class VoxelStreamer : public RefCounted`
- **Class Macro**: `GDCLASS(VoxelStreamer, RefCounted);`
- **ClassDB Registration**:
  - Binds `set_view_center()`, `get_view_center()`, `set_view_radius()`, `get_view_radius()`, `request_block()`, `cancel_request()`, `get_pending_request_count()`.
  - Accompanied by persistence backends `VoxelStreamSQLite` and `VoxelStreamRegionFiles` inheriting `RefCounted`.

#### 6. `VoxelGenerator` / `VoxelGeneratorNoise` (`Resource`)
- **Inheritance**: `class VoxelGenerator : public Resource`, `class VoxelGeneratorNoise : public VoxelGenerator`
- **Class Macro**: `GDCLASS(VoxelGeneratorNoise, VoxelGenerator);`
- **Script Override Hook (`GDVIRTUAL`)**:
  - Defines `GDVIRTUAL1R(float, _generate_voxel, Vector3);` in `protected:`.
  - Allows GDScript/C# scripts to override procedural voxel generation logic dynamically via `func _generate_voxel(pos: Vector3) -> float`.
  - Binds noise parameters: `frequency`, `octaves`, `lacunarity`, `gain`, `seed`, `warp_amplitude`, `planet_radius`.

#### 7. `AtcAttributePipeline` / `VoxelMaterial` (`RefCounted`)
- **Inheritance**: `class AtcAttributePipeline : public RefCounted`
- **Class Macro**: `GDCLASS(AtcAttributePipeline, RefCounted);`
- **ClassDB Registration**:
  - Material Tagging & Octahedral Normal Encoding: `encode_normal_oct16()`, `decode_normal_oct16()`, `pack_material_tag_oct()`, `unpack_material_tag_oct()`.
  - Attribute Packing: `pack_voxel_attributes()`, `unpack_voxel_attributes()`, `register_material_tag()`, `get_gpu_material_ssbo_bytes()`.

---

## 3. Engine Integration Points

### 3.1 ClassDB Method & Property Binding System
1. **`_bind_methods()` Routine**:
   - Every GDCLASS requires a `protected: static void _bind_methods();` declaration.
   - Methods are exposed using `ClassDB::bind_method(D_METHOD("method_name", "arg1", "arg2"), &ClassName::method_name);`.
2. **`ADD_PROPERTY()` Macros**:
   - Properties couple getter/setter pairs with inspector metadata.
   - Example: `ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "view_distance", PROPERTY_HINT_RANGE, "10.0,10000.0,1.0"), "set_view_distance", "get_view_distance");`
3. **`ADD_SIGNAL()` Macros**:
   - ClassDB signals allow async notification to GDScript and engine callbacks.
   - Example: `ADD_SIGNAL(MethodInfo("chunk_loaded", PropertyInfo(Variant::VECTOR3I, "chunk_pos")));`

### 3.2 Engine Lifecycle Notifications (`_notification`)
Module nodes hook into Godot's main frame loop via `_notification(int p_what)`:
```cpp
void VoxelWorld::_notification(int p_what) {
    switch (p_what) {
        case NOTIFICATION_ENTER_TREE:
            set_process(true);
            break;
        case NOTIFICATION_PROCESS:
            // Frame update logic: camera tracking, chunk streaming dispatch
            break;
        case NOTIFICATION_EXIT_TREE:
            set_process(false);
            break;
    }
}
```

### 3.3 Godot 4 RenderingServer & RenderingDevice Integration
For micro-voxel rendering on Vulkan, `VoxelRendererRD` bypasses traditional mesh pipelines and directly interface with Godot's `RenderingDevice` API (`RenderingServer::get_singleton()->get_rendering_device()`).

1. **SPIR-V Compute Shader Loading**:
   - `shaders/micro_voxel_raymarch.glsl` is compiled into `shaders/micro_voxel_raymarch.glsl.gen.h`.
   - Loaded into Vulkan via:
     ```cpp
     RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
     Ref<RDShaderFile> shader_file;
     shader_file.instantiate();
     shader_file->set_bytecode(micro_voxel_raymarch_shader_bytecode);
     RID shader_rid = rd->shader_create_from_spirv(shader_file->get_spirv_stages());
     RID pipeline_rid = rd->compute_pipeline_create(shader_rid);
     ```

2. **GPU Storage Buffer Objects (SSBO)**:
   - **SVO SSBO (`svo_ssbo_buffer`)**: Uploads DAG node index buffers (`LodOctree`).
   - **Clipmap SSBO (`clipmap_ssbo_buffer`)**: Uploads ring metadata (`ClipmapLevelGpu` struct, std430 48-byte layout).
   - **Material Palette SSBO (`material_palette_ssbo_buffer`)**: Uploads 32-byte std430 packed material structures (`GpuMaterialData`).

3. **Compute Dispatch Loop**:
   ```cpp
   RD::ComputeList list = rd->compute_list_begin();
   rd->compute_list_bind_compute_pipeline(list, raymarch_pipeline);
   rd->compute_list_bind_uniform_set(list, raymarch_uniform_set, 0);
   rd->compute_list_set_push_constant(list, &push_constants, sizeof(RaymarchPushConstants));
   rd->compute_list_dispatch(list, workgroup_x, workgroup_y, 1);
   rd->compute_list_end();
   ```

---

## 4. C++ Compilation & Build Verification Infrastructure

### 4.1 SCons Build Commands
Godot Eden is compiled directly into the Godot 4 engine executable using MSVC on Windows:

- **Build Script**: `build_eden_c.bat`
- **Command Line Execution**:
  ```cmd
  call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
  python -m SCons platform=windows target=editor vulkan=yes use_mingw=no d3d12=no voxel_ispc=yes -j8
  ```
- **Output Binary**: `bin/godot.windows.editor.x86_64.exe`

### 4.2 C++ Unit Verification (Doctest)
Native unit tests are co-located in `modules/godot_eden/tests/`:
- `test_main.h`: Tests `VoxelBuffer` palette compression, `LodOctree` SVO DAG deduplication, `SpatialLock3D` multi-threaded reader-writer locks, and ClassDB property reflection.
- `test_rendering.h`: Tests `AtcAttributePipeline` octahedral normal packing and `RenderingDevice` SSBO buffer creation.
- **Execution Command**:
  ```bash
  bin/godot.windows.editor.x86_64.exe --test --test-suite=godot_eden
  ```

### 4.3 4-Tier E2E Python Harness
Located at `tests/e2e/runner.py`:
- **Tier 1 (Unit)**: 75+ tests for default properties and basic object instantiation.
- **Tier 2 (Boundary)**: 75+ tests for invalid inputs, zero-sized buffers, and boundary conditions.
- **Tier 3 (Pairwise)**: 15+ tests for cross-subsystem interactions (`VoxelWorld` + `VoxelDataMap` + `SpatialLock3D`).
- **Tier 4 (Scenarios)**: 8 complex gameplay scenarios (planetary terraforming, asteroid mining, crash recovery).
- **Execution Command**:
  ```bash
  python tests/e2e/runner.py
  ```

---

## 5. Architectural Findings Summary Table

| Requirement / Component | File Location | ClassDB Base Class | Status | Technical Detail |
|-------------------------|---------------|-------------------|--------|------------------|
| **Module Config** | `modules/godot_eden/config.py` | N/A | Validated | Defines `GODOT_EDEN_ENABLED`; registers 16 doc classes |
| **SCsub Build Script** | `modules/godot_eden/SCsub` | N/A | Validated | Compiles 5 C++ dirs; compiles GLSL via `RD_GLSL` |
| **Module Type Register**| `modules/godot_eden/register_types.cpp` | N/A | Validated | Gated at `MODULE_INITIALIZATION_LEVEL_SCENE` |
| **VoxelWorld** | `nodes/voxel_world.h/.cpp` | `Node3D` | Validated | Spatial scene graph manager node |
| **VoxelVolume** | `nodes/voxel_volume.h/.cpp` | `Resource` | Validated | Volume metadata container |
| **VoxelRenderer / RD** | `rendering/voxel_renderer_rd.h/.cpp` | `Node3D` | Validated | Vulkan `RenderingDevice` compute raymarcher |
| **VoxelBuffer (Chunk)** | `storage/voxel_buffer.h/.cpp` | `RefCounted` | Validated | 16^3 voxel storage with 3-tier palette compaction |
| **VoxelStreamer (Stream)**| `streaming/voxel_streamer.h/.cpp` | `RefCounted` | Validated | Streaming request manager & SQLite backend |
| **VoxelGenerator / Noise**| `generators/voxel_generator_noise.h` | `Resource` | Validated | FastNoise2 density field with `GDVIRTUAL` hooks |
| **AtcAttributePipeline** | `rendering/atc_attribute_pipeline.h` | `RefCounted` | Validated | Material attribute packing & GPU SSBO buffer |
| **Compute Shaders** | `shaders/micro_voxel_raymarch.glsl` | N/A | Validated | SPIR-V GLSL compute raymarcher & clipmap LOD |
| **Build Automation** | `build_eden_c.bat` | N/A | Validated | SCons MSVC x64 build script |
| **Test Verification** | `tests/e2e/runner.py` & `tests/*.h` | N/A | Validated | Doctest C++ unit tests & 4-tier E2E runner |

---

## 6. Recommendations & Downstream Handoff Summary
1. **Module Architecture**: The built-in module layout in `modules/godot_eden` satisfies all Godot 4 engine conventions and SCons requirements.
2. **ClassDB Bindings**: ClassDB macros, method bindings (`_bind_methods`), property registrations (`ADD_PROPERTY`), and `GDVIRTUAL` hooks across all 16 classes are properly structured and ready for GDScript/C# consumption.
3. **GPU Compute Shader Pipeline**: Shader integration via `RD_GLSL` in SCons generates `.glsl.gen.h` headers seamlessly. `VoxelRendererRD` successfully manages Vulkan `RenderingDevice` RIDs, SSBO uploads, and compute list dispatches.
4. **Verification**: Future feature enhancements should be validated through both C++ Doctest (`--test --test-suite=godot_eden`) and the E2E Python runner (`python tests/e2e/runner.py`).
