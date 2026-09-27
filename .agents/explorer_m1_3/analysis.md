# SCons Build Script (SCsub), config.py, and Doctest Harness Requirements Analysis

## Executive Summary
This document provides the complete, authoritative specification and structural design for the build configuration (`config.py`), SCons build script (`SCsub`), GLSL compute shader builder integration (`glsl_builders.build_rd_headers`), and co-located Doctest unit test harness (`modules/godot_eden/tests/test_main.h`) for the `godot_eden` Godot 4 built-in C++ module.

---

## 1. Standard Godot 4 Module Build System Architecture

Godot 4 employs SCons as its primary build engine. Built-in modules reside under the `modules/` directory (or custom paths specified via `custom_modules`). The engine's top-level `SConstruct` and `modules/SCsub` orchestrate module discovery, configuration, compilation, and linking into the final engine binary.

### Module Build Lifecycle Sequence
1. **Module Detection (`SConstruct`)**:
   - `SConstruct` scans `modules/` for subdirectories containing `config.py`.
   - For each detected module (e.g. `godot_eden`), `SConstruct` dynamically imports `config.py`.
   - Module options (such as `module_godot_eden_enabled`) are added to SCons build options.
2. **Environment & Dependency Configuration (`SConstruct` & `config.py`)**:
   - For enabled modules, `SConstruct` calls `config.can_build(env, platform)`. If `True`, it verifies module dependencies.
   - `SConstruct` calls `config.configure(env)` to inject preprocessor definitions, custom compiler flags, or module-specific include directories.
   - Documentation paths (`config.get_doc_classes()`, `config.get_doc_path()`) are registered into `env.doc_class_path`.
3. **Source Compilation (`modules/SCsub`)**:
   - `modules/SCsub` iterates over active modules in `env.module_list`.
   - It sets `env.modules_sources = []` and invokes `SConscript("modules/godot_eden/SCsub")`.
   - Inside `modules/godot_eden/SCsub`, source files (`.cpp`) are collected into `env.modules_sources` (or local lists appended to `env.modules_sources`).
   - `modules/SCsub` creates a static library `libmodule_godot_eden.a` (or platform equivalent `.lib`) containing `env.modules_sources` and prepends it to `LIBS`.
4. **Test Header Discovery (`modules/SCsub` & `modules_builders.py`)**:
   - When building with unit tests enabled (`scons tests=yes`), `modules/SCsub` globs all header files matching `modules/<module>/tests/*.h`.
   - `modules_builders.modules_tests_builder` generates `modules/modules_tests.gen.h`, inserting `#include "modules/godot_eden/tests/test_main.h"` (and all other discovered test headers).
   - `tests/test_main.cpp` includes `modules/modules_tests.gen.h`, making all `TEST_CASE` definitions available for execution via `godot --test`.
5. **Module Type Registration (`modules/SCsub` & `modules_builders.py`)**:
   - `modules_builders.register_module_types_builder` generates `modules/register_module_types.gen.cpp`, which defines `initialize_modules(p_level)` and `uninitialize_modules(p_level)` calling `initialize_godot_eden_module(p_level)` and `uninitialize_godot_eden_module(p_level)`.

---

## 2. Python Module Configuration (`config.py`) Specification

The `config.py` script serves as the configuration entry point for SCons during build initialization.

### Required Functions

#### `can_build(env, platform)`
- **Purpose**: Determines if the module can be built on the given target platform and environment.
- **Specification**:
  - `godot_eden` micro-voxel engine is compatible with all standard Godot target platforms (Windows, Linux, macOS, Android, iOS, Web).
  - Returns `True` by default.
  - Optional guard: If building for headless or custom server builds lacking 3D/rendering capabilities, `can_build` checks `not env.get("disable_3d", False)`.

#### `configure(env)`
- **Purpose**: Modifies the SCons build environment prior to source collection.
- **Specification**:
  - Appends preprocessor macro `#define GODOT_EDEN_ENABLED` to `CPPDEFINES`.
  - Configures C++ standard flags or optimization definitions if required.

#### `get_doc_classes()`
- **Purpose**: Returns the list of ClassDB class names registered by `godot_eden` that require XML documentation integration.
- **Specification**:
  - Returns `["VoxelWorld", "VoxelVolume", "VoxelRenderer", "VoxelStreamer", "VoxelGenerator"]`.

#### `get_doc_path()`
- **Purpose**: Specifies the directory relative to the module root where doc XML files are located.
- **Specification**:
  - Returns `"doc_classes"`.

### Complete `config.py` Implementation

```python
# modules/godot_eden/config.py

def can_build(env, platform):
    # Enable godot_eden for all platforms unless 3D is explicitly disabled
    if env.get("disable_3d", False):
        return False
    return True


def configure(env):
    # Inject module preprocessor flag
    env.Append(CPPDEFINES=["GODOT_EDEN_ENABLED"])


def get_doc_classes():
    return [
        "VoxelWorld",
        "VoxelVolume",
        "VoxelRenderer",
        "VoxelStreamer",
        "VoxelGenerator",
    ]


def get_doc_path():
    return "doc_classes"
```

---

## 3. SCons Build Script (`SCsub`) Specification

The `SCsub` file defines source file collection, include search paths, GLSL shader compilation rules, and subdirectory recursion for `godot_eden`.

### Key Elements of `SCsub`

1. **Imports & Environment Setup**:
   - `Import("env")` and `Import("env_modules")`.
   - Create a cloned environment `env_godot_eden = env_modules.Clone()` to avoid mutating global environment state.
2. **Include Paths (`CPPPATH`)**:
   - `env_godot_eden.Prepend(CPPPATH=["#modules/godot_eden"])` allows `#include "nodes/voxel_world.h"`, `#include "storage/voxel_buffer.h"`, etc., using relative or root-anchored include paths.
3. **GLSL Compute Shader Builder Integration**:
   - SCons builder `RD_GLSL` (defined in `SConstruct`) maps `.glsl` source files to generated headers `.glsl.gen.h` using `glsl_builders.build_rd_headers`.
   - `env_godot_eden.RD_GLSL("shaders/micro_voxel_raymarch.glsl")`
   - `env_godot_eden.RD_GLSL("shaders/clipmap_lod.glsl")`
   - Set cache/dependency rules: `env_godot_eden.Depends(Glob("shaders/*.glsl.gen.h"), ["#glsl_builders.py"])`.
4. **Source Collection**:
   - Local array `sources = []`.
   - Top-level sources: `env_godot_eden.add_source_files(sources, "*.cpp")` (collects `register_types.cpp`).
   - Subdirectory sources:
     - `nodes/*.cpp` (`voxel_world.cpp`, `voxel_volume.cpp`, `voxel_renderer.cpp`, `voxel_streamer.cpp`, `voxel_generator.cpp`)
     - `storage/*.cpp` (`voxel_buffer.cpp`, `voxel_data_map.cpp`, `lod_octree.cpp`)
     - `streaming/*.cpp` (`spatial_lock_3d.cpp`, `voxel_block_serializer.cpp`, `voxel_stream_sqlite.cpp`, `region_file_mmap.cpp`)
     - `generators/*.cpp` (`voxel_generator_noise.cpp`, `generate_block_gpu_task.cpp`)
     - `rendering/*.cpp` (`voxel_renderer_rd.cpp`, `atc_attribute_pipeline.cpp`, `collision_mesh_generator.cpp`)
   - Append collected sources to global `env.modules_sources += sources`.

### Complete `SCsub` Implementation

```python
#!/usr/bin/env python
from misc.utility.scons_hints import *
import os

Import("env")
Import("env_modules")

env_godot_eden = env_modules.Clone()

# Include paths for godot_eden module and subdirectories
env_godot_eden.Prepend(CPPPATH=["#modules/godot_eden"])

# GLSL Compute Shader Header Builders
# Generates .glsl.gen.h headers from .glsl shader source files
if os.path.exists(env_godot_eden.File("shaders/micro_voxel_raymarch.glsl").rfile().abspath):
    env_godot_eden.RD_GLSL("shaders/micro_voxel_raymarch.glsl")

if os.path.exists(env_godot_eden.File("shaders/clipmap_lod.glsl").rfile().abspath):
    env_godot_eden.RD_GLSL("shaders/clipmap_lod.glsl")

# Add dependency tracking on glsl_builders.py
env_godot_eden.Depends(Glob("shaders/*.glsl.gen.h"), ["#glsl_builders.py"])

# Collect C++ source files
sources = []

# Module root (register_types.cpp)
env_godot_eden.add_source_files(sources, "*.cpp")

# Module subdirectories
env_godot_eden.add_source_files(sources, "nodes/*.cpp")
env_godot_eden.add_source_files(sources, "storage/*.cpp")
env_godot_eden.add_source_files(sources, "streaming/*.cpp")
env_godot_eden.add_source_files(sources, "generators/*.cpp")
env_godot_eden.add_source_files(sources, "rendering/*.cpp")

# Append to engine modules sources list
env.modules_sources += sources
```

---

## 4. GLSL Shader Builder Pipeline Integration Details

Micro-voxel raymarching and clipmap LOD traversal rely on Vulkan compute shaders processed by Godot's `RenderingDevice` API. In Godot 4, shader source files (`.glsl`) are transformed at build time into C++ header files (`.glsl.gen.h`).

### Mechanism (`glsl_builders.py`)
1. **Parser & Struct (`RDHeaderStruct`)**:
   - `glsl_builders.py` parses `#[vertex]`, `#[fragment]`, and `#[compute]` directives in `.glsl` files.
   - Handles `#include "..."` statements recursively within shader files (e.g. including shared GLSL noise functions or data structures).
2. **C++ Class Header Generation**:
   - For shader file `micro_voxel_raymarch.glsl`, `build_rd_header` generates `micro_voxel_raymarch.glsl.gen.h`.
   - Generates a C++ class `MicroVoxelRaymarchShaderRD` inheriting from `ShaderRD` (`servers/rendering/renderer_rd/shader_rd.h`).
   - The shader GLSL source text is embedded as a raw character string literal (`static const char _compute_code[]`).
   - In constructor `MicroVoxelRaymarchShaderRD()`, it calls `setup(nullptr, nullptr, _compute_code, "MicroVoxelRaymarchShaderRD")`.
3. **Usage in Renderer Code (`VoxelRendererRD`)**:
   - C++ render code includes `#include "shaders/micro_voxel_raymarch.glsl.gen.h"`.
   - Instantiates `MicroVoxelRaymarchShaderRD shader; shader.initialize();` to obtain compiled `RID` shader resources via Godot's `RenderingDevice`.

---

## 5. Doctest Unit Test Harness (`modules/godot_eden/tests/test_main.h`) Specification

Godot 4 utilizes Doctest (a fast, header-only C++ testing framework) co-located in `thirdparty/doctest/doctest.h` and accessed via `tests/test_macros.h`.

### Build & Discovery Integration
- When building with `scons tests=yes`, `modules/SCsub` scans `modules/godot_eden/tests/*.h`.
- The entry point header `modules/godot_eden/tests/test_main.h` is automatically included in `modules/modules_tests.gen.h`.
- `tests/test_main.cpp` compiles `modules_tests.gen.h` within the engine unit test runner.
- Running `godot --test` (or `bin/godot.windows.editor.x86_64.exe --test --test-suite="[GodotEden]"`) runs all tests matching the suite tag.

### Test Requirements for Milestone 1
- Verify that `ClassDB` has registered all M1 classes: `VoxelWorld`, `VoxelVolume`, `VoxelRenderer`, `VoxelStreamer`, `VoxelGenerator`.
- Verify instantiation and inheritance constraints:
  - `VoxelWorld` is a `Node3D`.
  - `VoxelVolume` is a `Resource`.
  - `VoxelRenderer` is a `Node3D`.
  - `VoxelStreamer` is a `RefCounted`.
  - `VoxelGenerator` is a `Resource`.
- Provide inclusion entry points for future milestone test files (`test_voxel_buffer.h`, `test_svo_dag.h`, `test_serialization.h`).

### Complete `tests/test_main.h` Implementation

```cpp
/**************************************************************************/
/*  test_main.h                                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "tests/test_macros.h"

#include "core/object/class_db.h"
#include "modules/godot_eden/register_types.h"
#include "modules/godot_eden/nodes/voxel_world.h"
#include "modules/godot_eden/nodes/voxel_volume.h"
#include "modules/godot_eden/nodes/voxel_renderer.h"
#include "modules/godot_eden/nodes/voxel_streamer.h"
#include "modules/godot_eden/nodes/voxel_generator.h"

namespace TestGodotEden {

TEST_CASE("[Modules][GodotEden] ClassDB Registration Verification") {
	SUBCASE("Class Existence Checks") {
		CHECK_MESSAGE(ClassDB::class_exists("VoxelWorld"), "VoxelWorld class must be registered with ClassDB");
		CHECK_MESSAGE(ClassDB::class_exists("VoxelVolume"), "VoxelVolume class must be registered with ClassDB");
		CHECK_MESSAGE(ClassDB::class_exists("VoxelRenderer"), "VoxelRenderer class must be registered with ClassDB");
		CHECK_MESSAGE(ClassDB::class_exists("VoxelStreamer"), "VoxelStreamer class must be registered with ClassDB");
		CHECK_MESSAGE(ClassDB::class_exists("VoxelGenerator"), "VoxelGenerator class must be registered with ClassDB");
	}

	SUBCASE("Parent Class Inheritance Checks") {
		CHECK(ClassDB::is_parent_class("VoxelWorld", "Node3D"));
		CHECK(ClassDB::is_parent_class("VoxelVolume", "Resource"));
		CHECK(ClassDB::is_parent_class("VoxelRenderer", "Node3D"));
		CHECK(ClassDB::is_parent_class("VoxelStreamer", "RefCounted"));
		CHECK(ClassDB::is_parent_class("VoxelGenerator", "Resource"));
	}
}

TEST_CASE("[Modules][GodotEden] Object Instantiation & Lifecycle") {
	SUBCASE("Node3D Instantiation (VoxelWorld & VoxelRenderer)") {
		VoxelWorld *world = memnew(VoxelWorld);
		REQUIRE(world != nullptr);
		CHECK(world->is_class("VoxelWorld"));
		memdelete(world);

		VoxelRenderer *renderer = memnew(VoxelRenderer);
		REQUIRE(renderer != nullptr);
		CHECK(renderer->is_class("VoxelRenderer"));
		memdelete(renderer);
	}

	SUBCASE("Resource & RefCounted Instantiation (VoxelVolume, VoxelGenerator, VoxelStreamer)") {
		Ref<VoxelVolume> volume = memnew(VoxelVolume);
		REQUIRE(volume.is_valid());
		CHECK(volume->is_class("VoxelVolume"));

		Ref<VoxelGenerator> generator = memnew(VoxelGenerator);
		REQUIRE(generator.is_valid());
		CHECK(generator->is_class("VoxelGenerator"));

		Ref<VoxelStreamer> streamer = memnew(VoxelStreamer);
		REQUIRE(streamer.is_valid());
		CHECK(streamer->is_class("VoxelStreamer"));
	}
}

} // namespace TestGodotEden
```

---

## 6. Build & Test Verification Workflow

### Compilation Commands
- **Standard Editor Build**:
  ```bash
  scons platform=windows target=editor dev_build=yes -j8
  ```
- **Unit Test Build**:
  ```bash
  scons platform=windows target=editor tests=yes dev_build=yes -j8
  ```

### Executing Unit Tests
- **Run All Module Tests**:
  ```bash
  bin/godot.windows.editor.x86_64.exe --test
  ```
- **Run Only GodotEden Tests**:
  ```bash
  bin/godot.windows.editor.x86_64.exe --test --test-suite="[GodotEden]"
  ```

---

## 7. Summary of Deliverables & Requirements Mapping

| Requirement | File | Primary Responsibility | Verification |
|-------------|------|------------------------|--------------|
| **Module Config** | `modules/godot_eden/config.py` | SCons platform check, doc classes registration, preprocessor defines | `scons` module detection |
| **SCons Build Script** | `modules/godot_eden/SCsub` | Source collection, CPPPATH, GLSL shader headers compilation | Successful C++ build & link into `libmodule_godot_eden.a` |
| **Shader Builders** | `glsl_builders.py` integration | Auto-generation of `.glsl.gen.h` C++ `ShaderRD` headers | Header creation in `shaders/` directory |
| **Doctest Harness** | `modules/godot_eden/tests/test_main.h` | Co-located C++ unit test suite for ClassDB and object lifecycle | `godot --test` pass |
