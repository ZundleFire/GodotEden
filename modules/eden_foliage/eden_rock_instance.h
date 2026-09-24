#ifndef EDEN_ROCK_INSTANCE_H
#define EDEN_ROCK_INSTANCE_H

#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/material.h"

// Native, script-free Node3D that builds its own angular rock/boulder mesh via
// EdenTreeMesher::add_rock() on _ready() -- same pattern as EdenTreeInstance, meant to be
// wrapped in a PackedScene and used as a VoxelInstanceLibrarySceneItem so VoxelInstancer can
// scatter rocks/boulders across terrain the same way it scatters trees.
class EdenRockInstance : public Node3D {
	GDCLASS(EdenRockInstance, Node3D);

public:
	enum RockType { ROCK_RANDOM = 0, ROCK_BOULDER = 1, ROCK_CLUSTER = 2, ROCK_PEBBLES = 3, ROCK_SPIRE = 4, ROCK_CRYSTAL = 5 };

	void set_seed_override(int p_seed);
	int get_seed_override() const { return _seed_override; }

	void set_rock_type(int p_type);
	int get_rock_type() const { return _rock_type; }

	void _notification(int p_what);

protected:
	static void _bind_methods();

private:
	int _seed_override = -1;
	int _rock_type = ROCK_RANDOM;

	MeshInstance3D *_mesh_inst = nullptr;
	Ref<StandardMaterial3D> _mat;

	void _rebuild();
	static uint32_t _hash_position(const Vector3 &p_pos);
};

VARIANT_ENUM_CAST(EdenRockInstance::RockType);

#endif // EDEN_ROCK_INSTANCE_H
