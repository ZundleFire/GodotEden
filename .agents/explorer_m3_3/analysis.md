# Architectural & Feature Specification Analysis Report: Milestone 3 (Feature 11, Feature 12, ClassDB & Doctests)

**Author**: Explorer 3  
**Target Milestone**: Milestone 3 — Micro-Voxel Renderer Architecture & Vulkan Compute Raymarching Pipeline  
**Working Directory**: `C:\DEV_DRIVE\Dev\GodotEden\.agents\explorer_m3_3`  
**Date**: 2026-08-05  

---

## 1. Executive Summary

This report defines the complete technical design, public C++ interfaces, ClassDB binding contracts, SCons build requirements, and C++ Doctest unit test strategy for **Feature 11** (Allocation-Tagging-Conversion dynamic voxel attribute pipeline) and **Feature 12** (Physics Collision Mesh Generator with greedy meshing and dual contouring fallbacks producing `ConcavePolygonShape3D`), as well as their integration into GodotEden's module build system and unit test suite.

### Key Targets:
- **Feature 11**: `modules/godot_eden/rendering/atc_attribute_pipeline.h` and `atc_attribute_pipeline.cpp` (`AtcAttributePipeline` inheriting `RefCounted`).
- **Feature 12**: `modules/godot_eden/rendering/physics_mesh_generator.h` and `physics_mesh_generator.cpp` (`PhysicsMeshGenerator` inheriting `RefCounted`).
- **SCons Integration**: Update `modules/godot_eden/SCsub` to include `rendering/*.cpp`.
- **ClassDB Registration**: Register both classes in `modules/godot_eden/register_types.cpp`.
- **C++ Doctest Suite**: Add test cases to `modules/godot_eden/tests/test_rendering.h` (or co-located in `test_main.h`).

---

## 2. Feature 11: ATC Attribute & Material System (`atc_attribute_pipeline.h/cpp`)

### 2.1 Overview & Responsibilities
The Allocation-Tagging-Conversion (ATC) attribute pipeline (inspired by Voxely) bridges raw voxel channels (SDF, material IDs, custom channels) in `VoxelBuffer` with GPU compute shaders and visual rendering parameters.

1. **Allocation**: Dynamic registration and lookup of voxel material tags and attribute parameters.
2. **Tagging**: 16-bit packed voxel material tag encoding (8-bit material ID, 4-bit variant/sub-type, 4-bit flags) and attribute mapping (albedo, roughness, metallic, normal, emission).
3. **Conversion**: High-performance conversion of `VoxelBuffer` data into packed SSBO byte/float arrays for GPU consumption, SDF central-difference surface normal estimation, triplanar blend weights, and slope-based material blending.

### 2.2 Header Specification (`atc_attribute_pipeline.h`)
```cpp
/**************************************************************************/
/*  atc_attribute_pipeline.h                                              */
/**************************************************************************/

#pragma once

#include "core/math/color.h"
#include "core/math/vector3.h"
#include "core/math/vector3i.h"
#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "core/variant/dictionary.h"
#include "core/variant/packed_byte_array.h"
#include "core/variant/packed_float32_array.h"

#include "modules/godot_eden/storage/voxel_buffer.h"

class AtcAttributePipeline : public RefCounted {
	GDCLASS(AtcAttributePipeline, RefCounted);

public:
	struct MaterialTag {
		uint32_t tag_id = 0;
		Color albedo = Color(1.0f, 1.0f, 1.0f, 1.0f);
		float roughness = 0.5f;
		float metallic = 0.0f;
		Vector3 normal = Vector3(0.0f, 1.0f, 0.0f);
		Color emission = Color(0.0f, 0.0f, 0.0f, 0.0f);
		uint32_t flags = 0;
	};

private:
	HashMap<uint32_t, MaterialTag> material_tags;
	Dictionary uniform_parameters;

protected:
	static void _bind_methods();

public:
	AtcAttributePipeline();
	~AtcAttributePipeline();

	// Material Tag Registration & Management
	void register_material_tag(uint32_t p_tag, const Color &p_albedo = Color(1,1,1), float p_roughness = 0.5f, float p_metallic = 0.0f, const Vector3 &p_normal = Vector3(0,1,0), const Color &p_emission = Color(0,0,0));
	Dictionary get_material_tag(uint32_t p_tag) const;
	bool has_material_tag(uint32_t p_tag) const;
	void unregister_material_tag(uint32_t p_tag);
	void clear_material_tags();
	int get_registered_tag_count() const;

	// 16-bit Bit-Packed Material Formatting
	static uint16_t pack_material_tag(uint8_t p_material_id, uint8_t p_sub_type, uint8_t p_flags);
	static uint8_t unpack_material_id(uint16_t p_packed);
	static uint8_t unpack_sub_type(uint16_t p_packed);
	static uint8_t unpack_flags(uint16_t p_packed);

	// Mathematical Helpers (Triplanar & Slope Blending)
	static Vector3 calculate_triplanar_weights(const Vector3 &p_normal, float p_sharpness = 8.0f);
	static float calculate_slope_blend(float p_normal_up, float p_threshold = 0.85f);

	// Attribute & Buffer Conversion
	PackedByteArray convert_voxel_buffer_to_atc(const Ref<VoxelBuffer> &p_buffer) const;
	PackedFloat32Array convert_attributes_to_packed_floats(const Ref<VoxelBuffer> &p_buffer) const;
	Color get_voxel_albedo(const Ref<VoxelBuffer> &p_buffer, const Vector3i &p_pos) const;
	Vector3 compute_surface_normal(const Ref<VoxelBuffer> &p_buffer, const Vector3i &p_pos) const;

	// Dynamic Uniform Management
	void set_uniform_parameter(const String &p_name, const Variant &p_value);
	Variant get_uniform_parameter(const String &p_name) const;
	Dictionary get_all_uniform_parameters() const;
};
```

---

## 3. Feature 12: Physics Collision Mesh Generator (`physics_mesh_generator.h/cpp`)

### 3.1 Overview & Responsibilities
`PhysicsMeshGenerator` produces Godot physics collision shapes (`ConcavePolygonShape3D`) from voxel volume blocks (`VoxelBuffer`). It supports two algorithm paths:
1. **Greedy Meshing**: Fast extraction of axis-aligned quad faces across voxel solid boundaries, merging adjacent coplanar faces with identical material properties into simplified larger quads.
2. **Dual Contouring**: Smooth surface extraction evaluating SDF sign transitions along cell edges and generating dual vertices to represent complex planetary terrain contours.

### 3.2 Header Specification (`physics_mesh_generator.h`)
```cpp
/**************************************************************************/
/*  physics_mesh_generator.h                                              */
/**************************************************************************/

#pragma once

#include "core/math/vector3.h"
#include "core/math/vector3i.h"
#include "core/object/ref_counted.h"
#include "core/variant/packed_vector3_array.h"
#include "scene/resources/3d/concave_polygon_shape_3d.h"

#include "modules/godot_eden/storage/voxel_buffer.h"

class PhysicsMeshGenerator : public RefCounted {
	GDCLASS(PhysicsMeshGenerator, RefCounted);

private:
	float default_isolevel = 0.0f;
	bool enable_greedy_merge = true;

protected:
	static void _bind_methods();

public:
	PhysicsMeshGenerator();
	~PhysicsMeshGenerator();

	void set_default_isolevel(float p_isolevel);
	float get_default_isolevel() const;

	void set_enable_greedy_merge(bool p_enable);
	bool is_enable_greedy_merge() const;

	// Mesh Face Generation
	PackedVector3Array generate_mesh_faces_greedy(const Ref<VoxelBuffer> &p_buffer, float p_isolevel = 0.0f) const;
	PackedVector3Array generate_mesh_faces_dual_contouring(const Ref<VoxelBuffer> &p_buffer, float p_isolevel = 0.0f) const;

	// ConcavePolygonShape3D Collision Shape Construction
	Ref<ConcavePolygonShape3D> generate_collision_shape_greedy(const Ref<VoxelBuffer> &p_buffer, float p_isolevel = 0.0f) const;
	Ref<ConcavePolygonShape3D> generate_collision_shape_dual_contouring(const Ref<VoxelBuffer> &p_buffer, float p_isolevel = 0.0f) const;
};
```

---

## 4. ClassDB Registration & SCons Integration

### 4.1 SCons Configuration (`SCsub`)
Update `modules/godot_eden/SCsub` to collect C++ files from `rendering/`:
```python
# Module subdirectories
env_godot_eden.add_source_files(sources, "nodes/*.cpp")
env_godot_eden.add_source_files(sources, "storage/*.cpp")
env_godot_eden.add_source_files(sources, "streaming/*.cpp")
env_godot_eden.add_source_files(sources, "generators/*.cpp")
env_godot_eden.add_source_files(sources, "rendering/*.cpp")
```

### 4.2 ClassDB Registration (`register_types.cpp`)
In `modules/godot_eden/register_types.cpp`:
```cpp
#include "rendering/atc_attribute_pipeline.h"
#include "rendering/physics_mesh_generator.h"

void initialize_godot_eden_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		...
		GDREGISTER_CLASS(AtcAttributePipeline);
		GDREGISTER_CLASS(PhysicsMeshGenerator);
	}
}
```

---

## 5. C++ Doctest Unit Test Integration Specifications

The unit tests for Feature 11 and Feature 12 should be placed in `modules/godot_eden/tests/test_rendering.h` (included via `test_main.h`).

### 5.1 Test Cases Outline

```cpp
TEST_CASE("[Modules][GodotEden] Rendering ClassDB Registration & Inheritance") {
	SUBCASE("Class Existence Checks") {
		CHECK(ClassDB::class_exists("AtcAttributePipeline"));
		CHECK(ClassDB::class_exists("PhysicsMeshGenerator"));
	}

	SUBCASE("Parent Class Inheritance Checks") {
		CHECK(ClassDB::is_parent_class("AtcAttributePipeline", "RefCounted"));
		CHECK(ClassDB::is_parent_class("PhysicsMeshGenerator", "RefCounted"));
	}
}

TEST_CASE("[Modules][GodotEden] AtcAttributePipeline Material Tagging & Conversions") {
	Ref<AtcAttributePipeline> pipeline = memnew(AtcAttributePipeline);
	REQUIRE(pipeline.is_valid());

	SUBCASE("Tag Registration & Retrieval") {
		pipeline->register_material_tag(1, Color(0.8f, 0.2f, 0.2f), 0.3f, 0.0f);
		CHECK(pipeline->has_material_tag(1));
		Dictionary tag_dict = pipeline->get_material_tag(1);
		CHECK((Color)tag_dict["albedo"] == Color(0.8f, 0.2f, 0.2f));
		CHECK((float)tag_dict["roughness"] == 0.3f);
	}

	SUBCASE("16-bit Bit-Packed Material Tag Layout") {
		uint16_t packed = AtcAttributePipeline::pack_material_tag(42, 3, 1);
		CHECK(AtcAttributePipeline::unpack_material_id(packed) == 42);
		CHECK(AtcAttributePipeline::unpack_sub_type(packed) == 3);
		CHECK(AtcAttributePipeline::unpack_flags(packed) == 1);
	}

	SUBCASE("Triplanar Blend & Slope Threshold Calculations") {
		Vector3 up_norm(0.0f, 1.0f, 0.0f);
		Vector3 weights = AtcAttributePipeline::calculate_triplanar_weights(up_norm, 8.0f);
		CHECK(weights.x == 0.0f);
		CHECK(weights.y == 1.0f);
		CHECK(weights.z == 0.0f);

		float flat_blend = AtcAttributePipeline::calculate_slope_blend(1.0f, 0.85f);
		float steep_blend = AtcAttributePipeline::calculate_slope_blend(0.2f, 0.85f);
		CHECK(flat_blend == 1.0f);
		CHECK(steep_blend == 0.0f);
	}
}

TEST_CASE("[Modules][GodotEden] PhysicsMeshGenerator Collision Mesh & Hulls") {
	Ref<PhysicsMeshGenerator> generator = memnew(PhysicsMeshGenerator);
	REQUIRE(generator.is_valid());

	SUBCASE("Empty Buffer Produces Zero Triangles") {
		Ref<VoxelBuffer> buf = memnew(VoxelBuffer);
		buf->fill_f(1.0f, VoxelBuffer::CHANNEL_SDF); // All air

		PackedVector3Array faces = generator->generate_mesh_faces_greedy(buf, 0.0f);
		CHECK(faces.size() == 0);

		Ref<ConcavePolygonShape3D> shape = generator->generate_collision_shape_greedy(buf, 0.0f);
		REQUIRE(shape.is_valid());
		CHECK(shape->get_faces().size() == 0);
	}

	SUBCASE("Solid Surface Quad Generation & ConcavePolygonShape3D") {
		Ref<VoxelBuffer> buf = memnew(VoxelBuffer);
		// Fill lower half with solid (SDF = -1.0), upper half air (SDF = 1.0)
		for (int z = 0; z < 16; z++) {
			for (int y = 0; y < 16; y++) {
				for (int x = 0; x < 16; x++) {
					float sdf = (y < 8) ? -1.0f : 1.0f;
					buf->set_voxel_f(sdf, x, y, z, VoxelBuffer::CHANNEL_SDF);
				}
			}
		}

		PackedVector3Array faces = generator->generate_mesh_faces_greedy(buf, 0.0f);
		CHECK(faces.size() > 0);
		CHECK(faces.size() % 3 == 0); // Must be triangles

		Ref<ConcavePolygonShape3D> shape = generator->generate_collision_shape_greedy(buf, 0.0f);
		REQUIRE(shape.is_valid());
		CHECK(shape->get_faces().size() == faces.size());
	}
}
```

---

## 6. Synthesis & Proposed Action Plan

1. **Worker Role**:
   - Create `modules/godot_eden/rendering/atc_attribute_pipeline.h` and `.cpp`.
   - Create `modules/godot_eden/rendering/physics_mesh_generator.h` and `.cpp`.
   - Update `modules/godot_eden/SCsub` to include `rendering/*.cpp`.
   - Update `modules/godot_eden/register_types.cpp` with header includes and `GDREGISTER_CLASS` entries.
   - Add/update `modules/godot_eden/tests/test_rendering.h` or `test_main.h` with the specified Doctest unit test cases.
2. **Reviewers & Challengers**:
   - Verify ClassDB registration matches `RefCounted` bindings.
   - Verify boundary safety for `VoxelBuffer` bounds sampling.
   - Run doctest suite (`godot --test`) and E2E runner (`python tests/e2e/runner.py`).

