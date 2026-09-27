# ClassDB Bindings & C++ Class Architecture Specification (Milestone 1)

## Executive Summary
This document formulates the exact C++ header and implementation specifications for `VoxelRenderer`, `VoxelStreamer`, and `VoxelGenerator`, along with the module registration routine in `register_types.h/cpp` for all 5 Milestone 1 ClassDB classes (`VoxelWorld`, `VoxelVolume`, `VoxelRenderer`, `VoxelStreamer`, `VoxelGenerator`).

All design specifications conform strictly to Godot 4 C++ engine module conventions, including `GDCLASS` macros, `_bind_methods()` property and method registrations, `GDVIRTUAL` hooks, `PROPERTY_HINT_*` annotations, and `MODULE_INITIALIZATION_LEVEL_SCENE` registration.

---

## 1. VoxelRenderer Specification

### File: `modules/godot_eden/nodes/voxel_renderer.h`
```cpp
#pragma once

#include "scene/3d/node_3d.h"
#include "core/templates/ref.h"
#include "nodes/voxel_volume.h"

class VoxelRenderer : public Node3D {
	GDCLASS(VoxelRenderer, Node3D);

private:
	Ref<VoxelVolume> volume;
	bool enabled = true;
	int lod_levels = 6;
	float view_distance = 1000.0f;
	bool wireframe = false;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	VoxelRenderer();
	~VoxelRenderer();

	void set_volume(const Ref<VoxelVolume> &p_volume);
	Ref<VoxelVolume> get_volume() const;

	void set_enabled(bool p_enabled);
	bool is_enabled() const;

	void set_lod_levels(int p_levels);
	int get_lod_levels() const;

	void set_view_distance(float p_distance);
	float get_view_distance() const;

	void set_wireframe(bool p_wireframe);
	bool is_wireframe() const;

	void update_lod(const Vector3 &p_camera_position);
	void clear();

	PackedStringArray get_configuration_warnings() const override;
};
```

### File: `modules/godot_eden/nodes/voxel_renderer.cpp`
```cpp
#include "nodes/voxel_renderer.h"

#include "core/object/class_db.h"

void VoxelRenderer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_volume", "volume"), &VoxelRenderer::set_volume);
	ClassDB::bind_method(D_METHOD("get_volume"), &VoxelRenderer::get_volume);

	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &VoxelRenderer::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &VoxelRenderer::is_enabled);

	ClassDB::bind_method(D_METHOD("set_lod_levels", "levels"), &VoxelRenderer::set_lod_levels);
	ClassDB::bind_method(D_METHOD("get_lod_levels"), &VoxelRenderer::get_lod_levels);

	ClassDB::bind_method(D_METHOD("set_view_distance", "distance"), &VoxelRenderer::set_view_distance);
	ClassDB::bind_method(D_METHOD("get_view_distance"), &VoxelRenderer::get_view_distance);

	ClassDB::bind_method(D_METHOD("set_wireframe", "wireframe"), &VoxelRenderer::set_wireframe);
	ClassDB::bind_method(D_METHOD("is_wireframe"), &VoxelRenderer::is_wireframe);

	ClassDB::bind_method(D_METHOD("update_lod", "camera_position"), &VoxelRenderer::update_lod);
	ClassDB::bind_method(D_METHOD("clear"), &VoxelRenderer::clear);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "volume", PROPERTY_HINT_RESOURCE_TYPE, "VoxelVolume"), "set_volume", "get_volume");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "lod_levels", PROPERTY_HINT_RANGE, "1,16,1"), "set_lod_levels", "get_lod_levels");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "view_distance", PROPERTY_HINT_RANGE, "10.0,100000.0,10.0"), "set_view_distance", "get_view_distance");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "wireframe"), "set_wireframe", "is_wireframe");
}

void VoxelRenderer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process_internal(enabled);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (enabled && volume.is_valid()) {
				// Internal frame processing hook for GPU clipmap LOD updates
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			set_process_internal(false);
		} break;
	}
}

VoxelRenderer::VoxelRenderer() {
}

VoxelRenderer::~VoxelRenderer() {
}

void VoxelRenderer::set_volume(const Ref<VoxelVolume> &p_volume) {
	volume = p_volume;
	update_configuration_warnings();
}

Ref<VoxelVolume> VoxelRenderer::get_volume() const {
	return volume;
}

void VoxelRenderer::set_enabled(bool p_enabled) {
	if (enabled == p_enabled) {
		return;
	}
	enabled = p_enabled;
	if (is_inside_tree()) {
		set_process_internal(enabled);
	}
}

bool VoxelRenderer::is_enabled() const {
	return enabled;
}

void VoxelRenderer::set_lod_levels(int p_levels) {
	lod_levels = CLAMP(p_levels, 1, 16);
}

int VoxelRenderer::get_lod_levels() const {
	return lod_levels;
}

void VoxelRenderer::set_view_distance(float p_distance) {
	view_distance = MAX(0.0f, p_distance);
}

float VoxelRenderer::get_view_distance() const {
	return view_distance;
}

void VoxelRenderer::set_wireframe(bool p_wireframe) {
	wireframe = p_wireframe;
}

bool VoxelRenderer::is_wireframe() const {
	return wireframe;
}

void VoxelRenderer::update_lod(const Vector3 &p_camera_position) {
	// Stub for clipmap LOD calculation based on camera position
}

void VoxelRenderer::clear() {
	// Stub for clearing active render state and pipeline buffers
}

PackedStringArray VoxelRenderer::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (volume.is_null()) {
		warnings.push_back("VoxelRenderer requires a VoxelVolume resource to render voxels.");
	}
	return warnings;
}
```

---

## 2. VoxelStreamer Specification

### File: `modules/godot_eden/streaming/voxel_streamer.h`
```cpp
#pragma once

#include "core/object/ref_counted.h"
#include "core/math/vector3i.h"
#include "core/templates/hash_set.h"

class VoxelStreamer : public RefCounted {
	GDCLASS(VoxelStreamer, RefCounted);

private:
	int max_pending_requests = 16;
	Vector3i view_center = Vector3i(0, 0, 0);
	int view_radius = 8;
	bool active = false;
	HashSet<Vector3i> pending_requests;

protected:
	static void _bind_methods();

public:
	VoxelStreamer();
	~VoxelStreamer();

	void set_max_pending_requests(int p_max);
	int get_max_pending_requests() const;

	void set_view_center(const Vector3i &p_center);
	Vector3i get_view_center() const;

	void set_view_radius(int p_radius);
	int get_view_radius() const;

	void set_active(bool p_active);
	bool is_active() const;

	void request_block(const Vector3i &p_block_pos);
	void cancel_request(const Vector3i &p_block_pos);
	int get_pending_request_count() const;
	void clear_pending_requests();
};
```

### File: `modules/godot_eden/streaming/voxel_streamer.cpp`
```cpp
#include "streaming/voxel_streamer.h"

#include "core/object/class_db.h"

void VoxelStreamer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_max_pending_requests", "max_requests"), &VoxelStreamer::set_max_pending_requests);
	ClassDB::bind_method(D_METHOD("get_max_pending_requests"), &VoxelStreamer::get_max_pending_requests);

	ClassDB::bind_method(D_METHOD("set_view_center", "center"), &VoxelStreamer::set_view_center);
	ClassDB::bind_method(D_METHOD("get_view_center"), &VoxelStreamer::get_view_center);

	ClassDB::bind_method(D_METHOD("set_view_radius", "radius"), &VoxelStreamer::set_view_radius);
	ClassDB::bind_method(D_METHOD("get_view_radius"), &VoxelStreamer::get_view_radius);

	ClassDB::bind_method(D_METHOD("set_active", "active"), &VoxelStreamer::set_active);
	ClassDB::bind_method(D_METHOD("is_active"), &VoxelStreamer::is_active);

	ClassDB::bind_method(D_METHOD("request_block", "block_pos"), &VoxelStreamer::request_block);
	ClassDB::bind_method(D_METHOD("cancel_request", "block_pos"), &VoxelStreamer::cancel_request);
	ClassDB::bind_method(D_METHOD("get_pending_request_count"), &VoxelStreamer::get_pending_request_count);
	ClassDB::bind_method(D_METHOD("clear_pending_requests"), &VoxelStreamer::clear_pending_requests);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_pending_requests", PROPERTY_HINT_RANGE, "1,128,1"), "set_max_pending_requests", "get_max_pending_requests");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3I, "view_center"), "set_view_center", "get_view_center");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "view_radius", PROPERTY_HINT_RANGE, "1,64,1"), "set_view_radius", "get_view_radius");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "active"), "set_active", "is_active");
}

VoxelStreamer::VoxelStreamer() {
}

VoxelStreamer::~VoxelStreamer() {
}

void VoxelStreamer::set_max_pending_requests(int p_max) {
	max_pending_requests = MAX(1, p_max);
}

int VoxelStreamer::get_max_pending_requests() const {
	return max_pending_requests;
}

void VoxelStreamer::set_view_center(const Vector3i &p_center) {
	view_center = p_center;
}

Vector3i VoxelStreamer::get_view_center() const {
	return view_center;
}

void VoxelStreamer::set_view_radius(int p_radius) {
	view_radius = MAX(1, p_radius);
}

int VoxelStreamer::get_view_radius() const {
	return view_radius;
}

void VoxelStreamer::set_active(bool p_active) {
	active = p_active;
}

bool VoxelStreamer::is_active() const {
	return active;
}

void VoxelStreamer::request_block(const Vector3i &p_block_pos) {
	if ((int)pending_requests.size() < max_pending_requests) {
		pending_requests.insert(p_block_pos);
	}
}

void VoxelStreamer::cancel_request(const Vector3i &p_block_pos) {
	pending_requests.erase(p_block_pos);
}

int VoxelStreamer::get_pending_request_count() const {
	return pending_requests.size();
}

void VoxelStreamer::clear_pending_requests() {
	pending_requests.clear();
}
```

---

## 3. VoxelGenerator Specification

### File: `modules/godot_eden/nodes/voxel_generator.h`
```cpp
#pragma once

#include "core/io/resource.h"
#include "core/math/vector3.h"
#include "core/object/gdvirtual.h"

class VoxelGenerator : public Resource {
	GDCLASS(VoxelGenerator, Resource);

private:
	float height_scale = 100.0f;
	int seed = 1337;

protected:
	static void _bind_methods();

	GDVIRTUAL1R(float, _generate_voxel, Vector3);

public:
	VoxelGenerator();
	~VoxelGenerator();

	void set_height_scale(float p_scale);
	float get_height_scale() const;

	void set_seed(int p_seed);
	int get_seed() const;

	virtual float generate_voxel_f(Vector3 p_position) const;
	float generate_voxel(Vector3 p_position) const;
};
```

### File: `modules/godot_eden/nodes/voxel_generator.cpp`
```cpp
#include "nodes/voxel_generator.h"

#include "core/object/class_db.h"

void VoxelGenerator::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_height_scale", "scale"), &VoxelGenerator::set_height_scale);
	ClassDB::bind_method(D_METHOD("get_height_scale"), &VoxelGenerator::get_height_scale);

	ClassDB::bind_method(D_METHOD("set_seed", "seed"), &VoxelGenerator::set_seed);
	ClassDB::bind_method(D_METHOD("get_seed"), &VoxelGenerator::get_seed);

	ClassDB::bind_method(D_METHOD("generate_voxel", "position"), &VoxelGenerator::generate_voxel);

	GDVIRTUAL_BIND(_generate_voxel, "position");

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height_scale", PROPERTY_HINT_RANGE, "0.1,10000.0,0.1"), "set_height_scale", "get_height_scale");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
}

VoxelGenerator::VoxelGenerator() {
}

VoxelGenerator::~VoxelGenerator() {
}

void VoxelGenerator::set_height_scale(float p_scale) {
	height_scale = p_scale;
}

float VoxelGenerator::get_height_scale() const {
	return height_scale;
}

void VoxelGenerator::set_seed(int p_seed) {
	seed = p_seed;
}

int VoxelGenerator::get_seed() const {
	return seed;
}

float VoxelGenerator::generate_voxel_f(Vector3 p_position) const {
	// Base fallback returns distance from origin modified by height_scale
	return p_position.length() - height_scale;
}

float VoxelGenerator::generate_voxel(Vector3 p_position) const {
	float ret = 0.0f;
	if (GDVIRTUAL_CALL(_generate_voxel, p_position, ret)) {
		return ret;
	}
	return generate_voxel_f(p_position);
}
```

---

## 4. Module Registration Specification

### File: `modules/godot_eden/register_types.h`
```cpp
#pragma once

#include "modules/register_module_types.h"

void initialize_godot_eden_module(ModuleInitializationLevel p_level);
void uninitialize_godot_eden_module(ModuleInitializationLevel p_level);
```

### File: `modules/godot_eden/register_types.cpp`
```cpp
#include "register_types.h"

#include "core/object/class_db.h"
#include "nodes/voxel_world.h"
#include "nodes/voxel_volume.h"
#include "nodes/voxel_renderer.h"
#include "nodes/voxel_generator.h"
#include "streaming/voxel_streamer.h"

void initialize_godot_eden_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(VoxelWorld);
		GDREGISTER_CLASS(VoxelVolume);
		GDREGISTER_CLASS(VoxelRenderer);
		GDREGISTER_CLASS(VoxelStreamer);
		GDREGISTER_CLASS(VoxelGenerator);
	}
}

void uninitialize_godot_eden_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}
```

---

## 5. ClassDB Binding & Inheritance Summary Table

| Class Name | Inheritance Base | Godot Binding Macro | File Location | ClassDB Initialization Level |
|------------|------------------|---------------------|---------------|------------------------------|
| `VoxelWorld` | `Node3D` | `GDCLASS(VoxelWorld, Node3D)` | `nodes/voxel_world.h` | `MODULE_INITIALIZATION_LEVEL_SCENE` |
| `VoxelVolume` | `Resource` | `GDCLASS(VoxelVolume, Resource)` | `nodes/voxel_volume.h` | `MODULE_INITIALIZATION_LEVEL_SCENE` |
| `VoxelRenderer` | `Node3D` | `GDCLASS(VoxelRenderer, Node3D)` | `nodes/voxel_renderer.h` | `MODULE_INITIALIZATION_LEVEL_SCENE` |
| `VoxelStreamer` | `RefCounted` | `GDCLASS(VoxelStreamer, RefCounted)` | `streaming/voxel_streamer.h` | `MODULE_INITIALIZATION_LEVEL_SCENE` |
| `VoxelGenerator` | `Resource` | `GDCLASS(VoxelGenerator, Resource)` | `nodes/voxel_generator.h` | `MODULE_INITIALIZATION_LEVEL_SCENE` |

---

## 6. Verification and Inspection Checklist
1. All 5 classes use standard Godot 4 header guards (`#pragma once`) and official includes (`scene/3d/node_3d.h`, `core/io/resource.h`, `core/object/ref_counted.h`, `core/object/class_db.h`).
2. Getter and setter pairs match Godot convention (`set_<prop>` / `get_<prop>` or `is_<prop>`).
3. ClassDB methods are exposed with explicit parameter naming in `D_METHOD`.
4. Properties use correct `Variant::Type` and `PROPERTY_HINT_*` annotations.
5. Abstract/virtual generator hooks use `GDVIRTUAL` macro for script override support.
