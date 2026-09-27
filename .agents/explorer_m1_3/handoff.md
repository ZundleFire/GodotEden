# Handoff Report: SCons Build Script (SCsub) & Doctest Harness Requirements Analysis

## 1. Observation

Direct observations from the Godot 4 codebase and design specifications:
- **Build Engine & Module System**:
  - `SConstruct` (lines 434–489): Automatically detects modules under `modules/` by checking for `config.py`.
  - `SConstruct` (lines 1151–1168): Defines `GLSL_BUILDERS`, registering `RD_GLSL` with action `glsl_builders.build_rd_headers` producing `*.glsl.gen.h` from `.glsl`.
  - `modules/SCsub` (lines 33–56): Iterates through `env.module_list`, executes `SConscript(base_path + "/SCsub")`, and when `env["tests"]` is true, scans `glob.glob(os.path.join(base_path, "tests", "*.h"))` to generate `modules_tests.gen.h`.
- **Existing Engine Patterns**:
  - `glsl_builders.py` (lines 94–137): Generates C++ `ShaderRD` derived wrapper classes (e.g., `MicroVoxelRaymarchShaderRD`) from `.glsl` compute shaders.
  - `modules/modules_builders.py` (lines 56–61): Concatenates header files from `modules/<module>/tests/*.h` into `#include "modules/<module>/tests/<file>.h"` statements in `modules_tests.gen.h`.
  - `tests/test_main.cpp` (line 210): Includes `modules/modules_tests.gen.h` to include all module Doctest headers into the main engine test runner executable (`godot --test`).
  - `tests/test_macros.h` (lines 42–55): Includes `thirdparty/doctest/doctest.h` and defines test macros (`TEST_CASE`, `CHECK`, `REQUIRE`, `SUBCASE`).
- **Module Requirements**:
  - Module directory structure: `modules/godot_eden/` with subdirectories `nodes/`, `storage/`, `streaming/`, `generators/`, `rendering/`, `shaders/`, and `tests/`.

## 2. Logic Chain

1. **Observation**: Godot 4 uses SCons to automatically discover built-in modules via `config.py` and compile them via `SCsub`.
   - **Deduction**: `modules/godot_eden` must provide a compliant `config.py` implementing `can_build`, `configure`, `get_doc_classes`, and `get_doc_path`, as well as an `SCsub` script cloning `env_modules`.
2. **Observation**: Micro-voxel rendering in `godot_eden` relies on compute shaders (`micro_voxel_raymarch.glsl`, `clipmap_lod.glsl`), and Godot's build system registers the `RD_GLSL` builder in `SConstruct`.
   - **Deduction**: `SCsub` must invoke `env_godot_eden.RD_GLSL("shaders/micro_voxel_raymarch.glsl")` and `env_godot_eden.RD_GLSL("shaders/clipmap_lod.glsl")` with explicit dependency declaration on `glsl_builders.py`.
3. **Observation**: Recursive and subdirectory C++ source collection is required for `nodes/`, `storage/`, `streaming/`, `generators/`, and `rendering/`.
   - **Deduction**: `SCsub` should use `env_godot_eden.add_source_files(sources, "<dir>/*.cpp")` for each module directory and append `sources` to `env.modules_sources`.
4. **Observation**: When `scons tests=yes` is specified, `modules/SCsub` globs all `*.h` files in `modules/<module>/tests/` and includes them into `modules_tests.gen.h`, which is compiled by `tests/test_main.cpp`.
   - **Deduction**: Placing `modules/godot_eden/tests/test_main.h` containing Doctest macros (`TEST_CASE`, `CHECK`, `REQUIRE`) ensures automatic registration and execution of M1 ClassDB and object lifecycle tests via `godot --test`.

## 3. Caveats

- **GLSL Shader Header Timing**: `RD_GLSL` header generation requires SCons to create `.glsl.gen.h` headers before C++ files referencing them are compiled. SCons dependency tracking handles this automatically, but `shaders/` directory and `.glsl` files must exist when the build runs.
- **Test Build Requirement**: Doctest headers in `tests/` are only compiled and executed when building with `scons tests=yes` (which defines `TESTS_ENABLED`).

## 4. Conclusion

The build system and unit test harness specification for `modules/godot_eden` is complete:
1. `config.py` provides Godot 4 standard configuration hooks, enabling `godot_eden` across all 3D-capable platforms and registering doc classes (`VoxelWorld`, `VoxelVolume`, `VoxelRenderer`, `VoxelStreamer`, `VoxelGenerator`).
2. `SCsub` defines cloned environment rules, include paths (`#modules/godot_eden`), GLSL compute shader header generation (`RD_GLSL`), and subdirectory source collection across all subdirectories (`nodes`, `storage`, `streaming`, `generators`, `rendering`).
3. `tests/test_main.h` provides a co-located Doctest test suite for verifying ClassDB registrations and object lifecycle.

All specifications are documented in `C:\DEV_DRIVE\Dev\GodotEden\.agents\explorer_m1_3\analysis.md`.

## 5. Verification Method

To independently verify the implementation once `modules/godot_eden` source files are written:

1. **SCons Build Verification**:
   - Run: `scons platform=windows target=editor dev_build=yes -j8`
   - Expect: `libmodule_godot_eden.windows.editor.dev.x86_64.a` links cleanly with zero compilation errors.
2. **GLSL Builder Verification**:
   - Inspect generated header files: `modules/godot_eden/shaders/micro_voxel_raymarch.glsl.gen.h` and `shaders/clipmap_lod.glsl.gen.h`.
   - Confirm presence of `MicroVoxelRaymarchShaderRD` class extending `ShaderRD`.
3. **Doctest Unit Test Verification**:
   - Compile test binary: `scons platform=windows target=editor tests=yes dev_build=yes -j8`
   - Run tests: `bin/godot.windows.editor.x86_64.exe --test --test-suite="[GodotEden]"`
   - Expect: All test cases in `TestGodotEden` namespace pass with 0 failures.
