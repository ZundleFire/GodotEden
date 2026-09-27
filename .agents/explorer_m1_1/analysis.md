# Milestone 1 Class & Module Architecture Analysis: GodotEden

**Author**: `explorer_m1_1`  
**Date**: 2026-08-05  
**Target Module Directory**: `modules/godot_eden`  
**Godot Version Target**: Godot 4 C++ Engine Module (Built-in)

---

## 1. Executive Summary

This document specifies the exact C++ module layout, type registration infrastructure, and core skeleton class definitions (`VoxelWorld` and `VoxelVolume`) for Milestone 1 of the **GodotEden** micro-voxel engine module.

GodotEden integrates directly into Godot 4's build system (SCons) as a built-in engine module under `modules/godot_eden`. The module registers custom engine classes with `ClassDB`, exposing them to GDScript, C#, and GDExtension bindings.

---

## 2. Directory & Module Layout

The proposed directory layout inside `modules/godot_eden/` for Milestone 1:

```
modules/godot_eden/
├── config.py                         # Godot build system module configuration
├── SCsub                             # SCons compilation instructions (built by implementer)
├── register_types.h                  # Module initialization interface header
├── register_types.cpp                # ClassDB class registrations
└── nodes/                            # Core Godot Scene Graph Node & Resource definitions
    ├── voxel_world.h                 # VoxelWorld (Node3D) header
    ├── voxel_world.cpp               # VoxelWorld (Node3D) implementation
    ├── voxel_volume.h                # VoxelVolume (Resource) header
    └── voxel_volume.cpp              # VoxelVolume (Resource) implementation
```

---

## 3. Detailed Specifications

### 3.1 `config.py`

`config.py` defines build environment checks, doc class registration, and doc paths for SCons.

```python
# modules/godot_eden/config.py

def can_build(env, platform):
    return not env["disable_3d"]


def configure(env):
    pass


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

### 3.2 `register_types.h` and `register_types.cpp`

Module type registration follows Godot 4's `ModuleInitializationLevel` lifecycle (`MODULE_INITIALIZATION_LEVEL_SCENE`).

#### `register_types.h`
```cpp
/**************************************************************************/
/*  register_types.h                                                      */
/**************************************************************************/

#pragma once

#include "modules/register_module_types.h"

void initialize_godot_eden_module(ModuleInitializationLevel p_level);
void uninitialize_godot_eden_module(ModuleInitializationLevel p_level);
```

#### `register_types.cpp`
```cpp
/**************************************************************************/
/*  register_types.cpp                                                    */
/**************************************************************************/

#include "register_types.h"

#include "nodes/voxel_volume.h"
#include "nodes/voxel_world.h"

#include "core/object/class_db.h"

void initialize_godot_eden_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(VoxelVolume);
		GDREGISTER_CLASS(VoxelWorld);
	}
}

void uninitialize_godot_eden_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}
```

---

### 3.3 Skeleton Class 1: `VoxelWorld` (Inheriting `Node3D`)

`VoxelWorld` is the primary scene graph entry point (`Node3D`) for managing voxel volumes, camera tracking, and rendering configuration.

#### Header: `nodes/voxel_world.h`
```cpp
/**************************************************************************/
/*  voxel_world.h                                                         */
/**************************************************************************/

#pragma once

#include "scene/3d/node_3d.h"
#include "core/object/class_db.h"
#include "core/object/ref_counted.h"
#include "voxel_volume.h"

class VoxelWorld : public Node3D {
	GDCLASS(VoxelWorld, Node3D);

private:
	Ref<VoxelVolume> voxel_volume;
	float voxel_size = 1.0f;
	int view_distance_chunks = 16;
	bool enable_collision = true;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	VoxelWorld();
	~VoxelWorld();

	void set_voxel_volume(const Ref<VoxelVolume> &p_volume);
	Ref<VoxelVolume> get_voxel_volume() const;

	void set_voxel_size(float p_size);
	float get_voxel_size() const;

	void set_view_distance(int p_distance);
	int get_view_distance() const;

	void set_enable_collision(bool p_enable);
	bool is_collision_enabled() const;

	void update_world(const Vector3 &p_camera_pos);
};
```

#### Implementation: `nodes/voxel_world.cpp`
```cpp
/**************************************************************************/
/*  voxel_world.cpp                                                       */
/**************************************************************************/

#include "voxel_world.h"

void VoxelWorld::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_voxel_volume", "volume"), &VoxelWorld::set_voxel_volume);
	ClassDB::bind_method(D_METHOD("get_voxel_volume"), &VoxelWorld::get_voxel_volume);

	ClassDB::bind_method(D_METHOD("set_voxel_size", "size"), &VoxelWorld::set_voxel_size);
	ClassDB::bind_method(D_METHOD("get_voxel_size"), &VoxelWorld::get_voxel_size);

	ClassDB::bind_method(D_METHOD("set_view_distance", "distance"), &VoxelWorld::set_view_distance);
	ClassDB::bind_method(D_METHOD("get_view_distance"), &VoxelWorld::get_view_distance);

	ClassDB::bind_method(D_METHOD("set_enable_collision", "enable"), &VoxelWorld::set_enable_collision);
	ClassDB::bind_method(D_METHOD("is_collision_enabled"), &VoxelWorld::is_collision_enabled);

	ClassDB::bind_method(D_METHOD("update_world", "camera_pos"), &VoxelWorld::update_world);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "voxel_volume", PROPERTY_HINT_RESOURCE_TYPE, "VoxelVolume"), "set_voxel_volume", "get_voxel_volume");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "voxel_size", PROPERTY_HINT_RANGE, "0.01,100.0,0.01,or_greater"), "set_voxel_size", "get_voxel_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "view_distance_chunks", PROPERTY_HINT_RANGE, "1,128,1"), "set_view_distance", "get_view_distance");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enable_collision"), "set_enable_collision", "is_collision_enabled");
}

void VoxelWorld::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process(true);
		} break;
		case NOTIFICATION_PROCESS: {
			// Frame update hook for LOD and clipmap tracking
		} break;
		case NOTIFICATION_EXIT_TREE: {
			set_process(false);
		} break;
	}
}

VoxelWorld::VoxelWorld() {
}

VoxelWorld::~VoxelWorld() {
}

void VoxelWorld::set_voxel_volume(const Ref<VoxelVolume> &p_volume) {
	voxel_volume = p_volume;
}

Ref<VoxelVolume> VoxelWorld::get_voxel_volume() const {
	return voxel_volume;
}

void VoxelWorld::set_voxel_size(float p_size) {
	voxel_size = MAX(0.001f, p_size);
}

float VoxelWorld::get_voxel_size() const {
	return voxel_size;
}

void VoxelWorld::set_view_distance(int p_distance) {
	view_distance_chunks = MAX(1, p_distance);
}

int VoxelWorld::get_view_distance() const {
	return view_distance_chunks;
}

void VoxelWorld::set_enable_collision(bool p_enable) {
	enable_collision = p_enable;
}

bool VoxelWorld::is_collision_enabled() const {
	return enable_collision;
}

void VoxelWorld::update_world(const Vector3 &p_camera_pos) {
	// Stub for clipmap and chunk streaming updates
}
```

---

### 3.4 Skeleton Class 2: `VoxelVolume` (Inheriting `Resource`)

`VoxelVolume` is a Godot `Resource` class defining chunk dimensions, maximum LOD levels, and volume configuration parameters.

#### Header: `nodes/voxel_volume.h`
```cpp
/**************************************************************************/
/*  voxel_volume.h                                                        */
/**************************************************************************/

#pragma once

#include "core/io/resource.h"
#include "core/object/class_db.h"
#include "core/math/vector3i.h"
#include "core/math/aabb.h"

class VoxelVolume : public Resource {
	GDCLASS(VoxelVolume, Resource);

private:
	Vector3i chunk_size = Vector3i(16, 16, 16);
	int max_lod_levels = 8;
	String volume_name = "VoxelVolume";

protected:
	static void _bind_methods();

public:
	VoxelVolume();
	~VoxelVolume();

	void set_chunk_size(const Vector3i &p_size);
	Vector3i get_chunk_size() const;

	void set_max_lod_levels(int p_levels);
	int get_max_lod_levels() const;

	void set_volume_name(const String &p_name);
	String get_volume_name() const;

	int get_voxel_count_per_chunk() const;
	AABB get_bounds() const;
};
```

#### Implementation: `nodes/voxel_volume.cpp`
```cpp
/**************************************************************************/
/*  voxel_volume.cpp                                                      */
/**************************************************************************/

#include "voxel_volume.h"

void VoxelVolume::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_chunk_size", "size"), &VoxelVolume::set_chunk_size);
	ClassDB::bind_method(D_METHOD("get_chunk_size"), &VoxelVolume::get_chunk_size);

	ClassDB::bind_method(D_METHOD("set_max_lod_levels", "levels"), &VoxelVolume::set_max_lod_levels);
	ClassDB::bind_method(D_METHOD("get_max_lod_levels"), &VoxelVolume::get_max_lod_levels);

	ClassDB::bind_method(D_METHOD("set_volume_name", "name"), &VoxelVolume::set_volume_name);
	ClassDB::bind_method(D_METHOD("get_volume_name"), &VoxelVolume::get_volume_name);

	ClassDB::bind_method(D_METHOD("get_voxel_count_per_chunk"), &VoxelVolume::get_voxel_count_per_chunk);
	ClassDB::bind_method(D_METHOD("get_bounds"), &VoxelVolume::get_bounds);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3I, "chunk_size"), "set_chunk_size", "get_chunk_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_lod_levels", PROPERTY_HINT_RANGE, "1,16,1"), "set_max_lod_levels", "get_max_lod_levels");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "volume_name"), "set_volume_name", "get_volume_name");
}

VoxelVolume::VoxelVolume() {
}

VoxelVolume::~VoxelVolume() {
}

void VoxelVolume::set_chunk_size(const Vector3i &p_size) {
	chunk_size.x = MAX(1, p_size.x);
	chunk_size.y = MAX(1, p_size.y);
	chunk_size.z = MAX(1, p_size.z);
}

Vector3i VoxelVolume::get_chunk_size() const {
	return chunk_size;
}

void VoxelVolume::set_max_lod_levels(int p_levels) {
	max_lod_levels = CLAMP(p_levels, 1, 16);
}

int VoxelVolume::get_max_lod_levels() const {
	return max_lod_levels;
}

void VoxelVolume::set_volume_name(const String &p_name) {
	volume_name = p_name;
}

String VoxelVolume::get_volume_name() const {
	return volume_name;
}

int VoxelVolume::get_voxel_count_per_chunk() const {
	return chunk_size.x * chunk_size.y * chunk_size.z;
}

AABB VoxelVolume::get_bounds() const {
	return AABB(Vector3(0, 0, 0), Vector3(chunk_size.x, chunk_size.y, chunk_size.z));
}
```

---

## 4. Header & Macro Compliance Matrix

| Class / File | Base Class | Required Headers | Macro Requirements |
|---|---|---|---|
| `VoxelWorld` | `Node3D` | `<scene/3d/node_3d.h>`, `<core/object/class_db.h>`, `<core/object/ref_counted.h>` | `GDCLASS(VoxelWorld, Node3D)` |
| `VoxelVolume` | `Resource` | `<core/io/resource.h>`, `<core/object/class_db.h>`, `<core/math/vector3i.h>`, `<core/math/aabb.h>` | `GDCLASS(VoxelVolume, Resource)` |
| `register_types.cpp` | N/A | `"register_types.h"`, `"nodes/voxel_world.h"`, `"nodes/voxel_volume.h"`, `<core/object/class_db.h>` | `GDREGISTER_CLASS(T)` |
