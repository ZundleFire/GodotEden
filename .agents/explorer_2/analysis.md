# Technical Specification & Architecture Report: Godot 4 Built-In C++ Engine Module Integration for GodotEden

## 1. Executive Summary
GodotEden is designed as a high-performance built-in C++ engine module inside Godot Engine 4.
This report details the exact specifications, directory structure, ClassDB binding protocols, SCons compilation settings, shader header generators, third-party library integration, and unit testing harness requirements for building `GodotEden`.

---

## 2. Standard Godot 4 C++ Engine Module Architecture & File Layout

A built-in module in Godot 4 resides in `modules/<module_name>` (e.g., `modules/godot_eden`).
The required standard file layout is structured as follows:

```
modules/godot_eden/
├── config.py                 # SCons module configuration & build capability checks
├── SCsub                     # Module SCons build script (sources, defines, include paths, flags)
├── register_types.h          # Module initialization and uninitialization C-function declarations
├── register_types.cpp        # ClassDB class registration and engine singleton lifecycle hooks
├── doc_classes/              # XML class documentation for editor integration
│   ├── VoxelWorld.xml
│   ├── VoxelVolume.xml
│   └── VoxelRenderer.xml
├── shaders/                  # GLSL compute & rendering shaders
│   └── voxel_raymarch.glsl
├── thirdparty/               # Module-local third-party C++ libraries (e.g., Gvox, FastNoise2)
├── tests/                    # Doctest unit test suite header files
│   └── test_voxel_nodes.h
└── voxel_world.h / .cpp      # Core module C++ implementation files
```

### Detailed File Specifications

#### 1. `config.py`
`config.py` is invoked by Godot's SCons build system during module discovery.
```python
def can_build(env, platform):
    # Check platform support, compiler features, or mandatory build tools
    return True

def configure(env):
    # Configure global build environment settings if required
    pass

def get_doc_classes():
    # List of class names exposed to the Godot documentation generator
    return [
        "VoxelWorld",
        "VoxelVolume",
        "VoxelRenderer",
        "VoxelChunk",
        "VoxelLodManager",
    ]

def get_doc_path():
    return "doc_classes"
```

#### 2. `register_types.h`
Must declare initialization functions matching the module directory name in snake_case (`initialize_<module_name>_module` and `uninitialize_<module_name>_module`):
```cpp
#pragma once

#include "modules/register_module_types.h"

void initialize_godot_eden_module(ModuleInitializationLevel p_level);
void uninitialize_godot_eden_module(ModuleInitializationLevel p_level);
```

#### 3. `register_types.cpp`
Dispatches registration based on `ModuleInitializationLevel` (`CORE`, `SERVERS`, `SCENE`, `EDITOR`). Custom node classes must be registered during `MODULE_INITIALIZATION_LEVEL_SCENE`:
```cpp
#include "register_types.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

#include "voxel_world.h"
#include "voxel_volume.h"
#include "voxel_renderer.h"

static VoxelWorld *voxel_world_singleton = nullptr;

void initialize_godot_eden_module(ModuleInitializationLevel p_level) {
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_CLASS(VoxelWorld);
        GDREGISTER_CLASS(VoxelVolume);
        GDREGISTER_CLASS(VoxelRenderer);
    }
}

void uninitialize_godot_eden_module(ModuleInitializationLevel p_level) {
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        if (voxel_world_singleton != nullptr) {
            memdelete(voxel_world_singleton);
            voxel_world_singleton = nullptr;
        }
    }
}
```

#### 4. `SCsub`
The module's SCons compilation recipe:
```python
#!/usr/bin/env python

Import("env")
Import("env_modules")

env_godot_eden = env_modules.Clone()

# Append module-specific preprocessor defines and include paths
env_godot_eden.Append(CPPDEFINES=["GODOT_EDEN_ENABLED"])
env_godot_eden.Append(CPPPATH=["#modules/godot_eden"])

# Collect source files
module_objs = []
env_godot_eden.add_source_files(module_objs, "*.cpp")

# Append objects to Godot main module sources
env.modules_sources += module_objs
```

---

## 3. Godot ClassDB Registration Patterns & GDCLASS Specification

### A. GDCLASS Macro & Class Structure
Any C++ class exposed to GDScript/C# or Godot's inspection system must derive from `Godot Object` (or `RefCounted`, `Resource`, `Node3D`) and include `GDCLASS(ClassName, BaseClassName)`:

```cpp
#pragma once

#include "scene/3d/node_3d.h"
#include "core/io/resource.h"

class VoxelWorld : public Node3D {
    GDCLASS(VoxelWorld, Node3D);

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    VoxelWorld();
    ~VoxelWorld();

    void set_lod_distance(float p_dist);
    float get_lod_distance() const;

private:
    float lod_distance = 512.0f;
};
```

### B. Binding Methods, Properties, Signals, Enums
In `_bind_methods()`:
1. **Methods**:
   `ClassDB::bind_method(D_METHOD("set_lod_distance", "distance"), &VoxelWorld::set_lod_distance);`
   `ClassDB::bind_method(D_METHOD("get_lod_distance"), &VoxelWorld::get_lod_distance);`
2. **Properties**:
   `ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_distance", PROPERTY_HINT_RANGE, "0.0,4096.0,1.0"), "set_lod_distance", "get_lod_distance");`
3. **Groups / Subgroups**:
   `ADD_GROUP("LOD Configuration", "lod_");`
4. **Signals**:
   `ADD_SIGNAL(MethodInfo("chunk_loaded", PropertyInfo(Variant::VECTOR3I, "chunk_pos")));`
5. **Enums**:
   `BIND_ENUM_CONSTANT(RENDER_MODE_RAYMARCH);`
   Macro placed outside class declaration: `VARIANT_ENUM_CAST(VoxelWorld::RenderMode);`

### C. Core GodotEden Nodes & Resources
- **`VoxelWorld` (Node3D)**: High-level planetary world node. Manages streaming, LOD octrees/clipmaps, and camera tracking.
- **`VoxelVolume` (Resource / RefCounted)**: Voxel storage container for chunk data, dense/sparse grids, and procedural voxel generators.
- **`VoxelRenderer` (Node3D / Object)**: Rendering pipeline controller interfacing with Vulkan/RenderingDevice (RD) compute shaders or mesh generation.

---

## 4. SCons Build Pipeline, Shader Header Builders & Library Integration

### A. Shader Header Generation (`.glsl` -> `.gen.h`)
Godot 4 includes `glsl_builders.py` which compiles `.glsl` compute or graphics shaders into C++ headers wrapping `ShaderRD`.
In `SCsub`:
```python
import glsl_builders

shader_headers = env.CommandNoCache(
    "shaders/voxel_raymarch.glsl.gen.h",
    "shaders/voxel_raymarch.glsl",
    env.Run(glsl_builders.build_rd_headers)
)
env.NoCache(shader_headers)
```
In C++ source code:
```cpp
#include "shaders/voxel_raymarch.glsl.gen.h"

VoxelRaymarchShaderRD shader;
RID shader_rid = shader.version_get_shader(shader.version_create(), RenderingDevice::SHADER_STAGE_COMPUTE);
```

### B. Third-Party Library Linking
Nested libraries (such as Gvox, FastNoise2, or SIMD ISPC dependencies) are included via SConscript delegation:
```python
# Include third-party dependency
SConscript("thirdparty/gvox/SConscript", exports=["env", "env_godot_eden"])
env_godot_eden.Append(CPPPATH=["thirdparty/gvox/include"])
```

### C. Compiler Flags & Optimization
```python
if env_godot_eden.msvc:
    env_godot_eden.AppendUnique(CCFLAGS=["/O2", "/fp:fast"])
else:
    env_godot_eden.AppendUnique(CCFLAGS=["-O3", "-ffast-math"])
```

---

## 5. Compilation & Verification Test Harness Setup

### A. Doctest Integration
Godot 4 automatically aggregates module test headers matching `modules/<module_name>/tests/*.h` into `modules_tests.gen.h` when building with `tests=yes`.

Example module test file `modules/godot_eden/tests/test_voxel_world.h`:
```cpp
#pragma once

#include "tests/test_macros.h"
#include "../voxel_world.h"

namespace TestGodotEden {

TEST_CASE("[Modules][GodotEden] VoxelWorld Node Creation") {
    VoxelWorld *world = memnew(VoxelWorld);
    CHECK(world != nullptr);
    world->set_lod_distance(1024.0f);
    CHECK(world->get_lod_distance() == doctest::Approx(1024.0f));
    memdelete(world);
}

} // namespace TestGodotEden
```

### B. Compilation & Test Execution Commands
- **Standard Module Build**:
  `python -m SCons platform=windows target=editor vulkan=yes use_mingw=no d3d12=no voxel_ispc=yes -j8`
- **Build with Unit Tests Enabled**:
  `python -m SCons platform=windows target=editor tests=yes -j8`
- **Run Unit Tests via Godot CLI**:
  `bin/godot.windows.editor.x86_64.exe --test --test-suite="*[GodotEden]*"`

---

## 6. Specification Tables: Discovered Features & Edge Cases

### Features Discovered
| # | Category | Feature | Description | Inputs | Outputs | Error Behavior | Discovered Via |
|---|----------|---------|-------------|--------|---------|----------------|----------------|
| 1 | Layout | Module Auto-Discovery | SCons scans `modules/*/config.py` and compiles active module dirs into `libmodule_<name>.a`. | `modules/godot_eden/` directory | SCons static library target | Skipped if `can_build()` returns False | `modules/SCsub`, `modules/modules_builders.py` |
| 2 | Registration | ClassDB Binding | Registers C++ classes, methods, properties, signals, enums with Godot engine runtime. | `GDCLASS`, `_bind_methods()`, `GDREGISTER_CLASS` | ClassDB runtime type metadata | Compile error on signature mismatch | `core/object/class_db.h`, `modules/eden_planet_gen/register_types.cpp` |
| 3 | Lifecycle | Module Init Levels | Init/uninit hooks dispatched across levels (`CORE`, `SERVERS`, `SCENE`, `EDITOR`). | `ModuleInitializationLevel p_level` | Ordered module startup & cleanup | Crash if registered at wrong initialization level | `modules/register_module_types.h`, `modules/modules_builders.py` |
| 4 | Build | GLSL Shader RD Header | Compiles GLSL compute shaders into generated `ShaderRD` C++ headers at build time. | `.glsl` shader files | `*.glsl.gen.h` header with string array & shader class | SCons build error if GLSL syntax invalid or include missing | `glsl_builders.py` |
| 5 | Testing | Doctest Unit Harness | Built-in C++ test framework integrated with Godot CLI `--test` execution. | `modules/*/tests/*.h` headers | Executable test suite results | Non-zero exit code on test failure | `tests/test_macros.h`, `modules/SCsub` |

### Edge Cases
| # | Feature | Input | Observed Behavior |
|---|---------|-------|-------------------|
| 1 | Module Registration Naming | Folder name vs function name mismatch | SCons generated code `register_module_types.gen.cpp` fails linker check if `initialize_<folder_name>_module` is missing. |
| 2 | Object Memory Allocation | Raw `new`/`delete` vs `memnew()`/`memdelete()` | Using standard `new`/`delete` bypasses Godot custom memory tracking and memory leaks occur; must use `memnew()`/`memdelete()`. |
| 3 | ClassDB Enum Export | Missing `VARIANT_ENUM_CAST(Enum)` | Enum parameters cannot be converted to `Variant` for GDScript calls or Inspector property display. |
| 4 | Shader Recursive Includes | Relative `#include "..."` in GLSL | `glsl_builders.py` recursively parses `#include`; relative file path errors halt SCons build step. |
