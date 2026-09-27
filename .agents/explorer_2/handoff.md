# Handoff Report — Godot 4 Built-In C++ Engine Module Specification

## 1. Observation

1. **Module Auto-Discovery & Code Generation**:
   - `modules/modules_builders.py:15-35`: Generates `register_module_types.gen.cpp` which includes `"<module>/register_types.h"` and invokes `initialize_<module>_module(p_level)` and `uninitialize_<module>_module(p_level)` wrapped inside `#ifdef MODULE_<MODULE>_ENABLED`.
   - `modules/SCsub:18-28`: Executes `modules_enabled_builder` and `register_module_types_builder` during SCons build pass.
   - `modules/SCsub:33-42`: Iterates `env.module_list`, invokes `<module>/SCsub`, compiles source files into `libmodule_<name>.a`, and prepends to `LIBS`.

2. **Existing Module Examples in Codebase**:
   - `modules/eden_planet_gen/config.py:1-20`: Defines `can_build(env, platform)`, `configure(env)`, `get_doc_classes()`, and `get_doc_path()`.
   - `modules/eden_planet_gen/SCsub:1-15`: Clones `env_modules`, adds `.cpp` source files via `add_source_files()`, appends to `env.modules_sources`.
   - `modules/eden_planet_gen/register_types.h:1-7`: Declares `initialize_eden_planet_gen_module(ModuleInitializationLevel p_level)` and `uninitialize_eden_planet_gen_module(ModuleInitializationLevel p_level)`.
   - `modules/eden_planet_gen/register_types.cpp:12-27`: Checks `p_level == MODULE_INITIALIZATION_LEVEL_SCENE` and registers classes via `GDREGISTER_CLASS(...)`.
   - `modules/eden_planet_gen/eden_planet_climate_profile.h:6-7`: Shows `class EdenPlanetClimateProfile : public Resource { GDCLASS(EdenPlanetClimateProfile, Resource); protected: static void _bind_methods(); ...`.
   - `modules/eden_planet_gen/eden_planet_climate_profile.cpp:48-116`: Uses `ClassDB::bind_method(...)`, `ADD_GROUP(...)`, and `ADD_PROPERTY(...)`.

3. **Shader Header Generation Pipeline**:
   - `glsl_builders.py:94-137`: Generates `class <Name>ShaderRD : public ShaderRD` wrapping vertex, fragment, or compute GLSL byte code from `#include` source files.

4. **Unit Test Harness**:
   - `modules/SCsub:44-56`: When `env["tests"]` is true, scans `modules/<module>/tests/*.h`, appends headers, and generates `modules_tests.gen.h`.
   - `tests/test_macros.h:35-55`: Includes `thirdparty/doctest/doctest.h` and defines `TEST_CASE`, `CHECK`, `REQUIRE`, and Godot test assertion aliases (`TEST_COND`, `TEST_FAIL`).
   - `build_eden_c.bat:1-5`: Specifies SCons build invocation `python -m SCons platform=windows target=editor vulkan=yes use_mingw=no d3d12=no voxel_ispc=yes -j8`.

---

## 2. Logic Chain

1. **Observation 1 & 2** demonstrate that any built-in C++ module in Godot 4 requires a strict 4-file boilerplate (`config.py`, `SCsub`, `register_types.h`, `register_types.cpp`). The directory name must match the function naming in `register_types.h/cpp` (`initialize_<folder_name>_module`) for `modules_builders.py` code generation to assemble the module correctly.
2. **Observation 2** establishes that custom Godot nodes (`VoxelWorld`, `VoxelRenderer`) and resources (`VoxelVolume`) must inherit from standard Godot base classes (`Node3D`, `Resource`, `RefCounted`), declare `GDCLASS(ClassName, BaseClassName)`, implement `static void _bind_methods()`, and be registered in `register_types.cpp` during `MODULE_INITIALIZATION_LEVEL_SCENE`.
3. **Observation 3** proves that compute shaders for raymarching or GPU voxel pipelines can be placed in `shaders/` and compiled to static C++ header wrappers (`ShaderRD`) via `glsl_builders.build_rd_headers` in `SCsub`.
4. **Observation 4** proves that module unit testing is native to Godot 4 by co-locating unit test files in `modules/<module>/tests/*.h` using `doctest` macros, which SCons automatically links into `modules_tests.gen.h` when building with `tests=yes`.

---

## 3. Caveats

No caveats. All investigated mechanisms were verified directly from the `GodotEden` codebase files (`modules/modules_builders.py`, `modules/SCsub`, `glsl_builders.py`, `tests/test_macros.h`, `modules/eden_planet_gen/`).

---

## 4. Conclusion

The specification for creating standard Godot 4 C++ built-in engine modules has been completely documented in `C:\DEV_DRIVE\Dev\GodotEden\.agents\explorer_2\analysis.md`. The design patterns for `config.py`, `SCsub`, `register_types.h/cpp`, ClassDB binding macros (`GDCLASS`, `GDREGISTER_CLASS`, `_bind_methods`), GLSL shader builders, and doctest test harness setup are fully mapped and ready for implementation.

---

## 5. Verification Method

1. Inspect `C:\DEV_DRIVE\Dev\GodotEden\.agents\explorer_2\analysis.md` for complete technical tables and specifications.
2. Verify layout compliance against Godot 4 engine build rules by checking `modules/modules_builders.py` and `modules/SCsub`.
3. Validate build command configuration by testing SCons compilation:
   `python -m SCons platform=windows target=editor vulkan=yes use_mingw=no d3d12=no voxel_ispc=yes -j8`
