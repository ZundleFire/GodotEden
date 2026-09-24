#ifndef EDEN_TREE_INSTANCE_H
#define EDEN_TREE_INSTANCE_H

#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/material.h"

// Native replacement for the old eden_tree_instance.gd + EdenTreeBuilder.gd pair: a script-free
// Node3D that builds its own trunk/foliage meshes via EdenTreeGenerator (C++) on _ready(), meant
// to be wrapped in a PackedScene and registered as a VoxelInstanceLibrarySceneItem so
// VoxelInstancer spawns/positions/culls copies of it directly on VoxelTerrain -- this node only
// builds ITS OWN geometry, it knows nothing about density/placement rules (that's the
// instancer's job).
//
// Each spawned instance gets a different look: global_position (set by the instancer before
// _ready() runs) seeds an RNG, so no two trees on the same terrain are identical, without
// needing per-instance data from the instancer itself. seed_override lets a test bench force a
// specific tree shape instead of deriving one from world position.
class EdenTreeInstance : public Node3D {
	GDCLASS(EdenTreeInstance, Node3D);

public:
	void set_seed_override(int p_seed);
	int get_seed_override() const { return _seed_override; }

	// 0 = Random (rerolled from the seed), 1..N = EdenTreeShape::TreeType + 1.
	void set_tree_type(int p_type);
	int get_tree_type() const { return _tree_type; }

	void set_season(int p_season);
	int get_season() const { return _season; }

	// Icosphere subdivision level for leaf blobs -- see EdenTreeMesher::set_leaf_subdivisions().
	void set_leaf_subdivisions(int p_level);
	int get_leaf_subdivisions() const { return _leaf_subdivisions; }

	void _notification(int p_what);

protected:
	static void _bind_methods();

private:
	int _seed_override = -1;
	int _tree_type = 0;
	int _season = 1; // EdenTreeShape::SEASON_SUMMER
	int _leaf_subdivisions = 0;

	MeshInstance3D *_trunk_inst = nullptr;
	MeshInstance3D *_foliage_inst = nullptr;
	MeshInstance3D *_fruit_inst = nullptr;
	Ref<StandardMaterial3D> _trunk_mat;
	Ref<StandardMaterial3D> _foliage_mat;
	Ref<StandardMaterial3D> _fruit_mat;

	void _rebuild();
	static uint32_t _hash_position(const Vector3 &p_pos);
};

#endif // EDEN_TREE_INSTANCE_H
