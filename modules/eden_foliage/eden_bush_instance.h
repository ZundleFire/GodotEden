#ifndef EDEN_BUSH_INSTANCE_H
#define EDEN_BUSH_INSTANCE_H

#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/material.h"

// Native, script-free Node3D companion to EdenTreeInstance for smaller ground-cover foliage:
// a handful of thin woody stems (EdenTreeMesher beams) topped with small leaf blobs, instead of
// a full trunk+branch tree. Same build-on-_ready() pattern as EdenTreeInstance/EdenRockInstance,
// meant to be wrapped in a PackedScene and used as a VoxelInstanceLibrarySceneItem.
class EdenBushInstance : public Node3D {
	GDCLASS(EdenBushInstance, Node3D);

public:
	enum BushType { BUSH_RANDOM = 0, BUSH_SHRUB = 1, BUSH_FERN = 2, BUSH_DEAD = 3, BUSH_TALL_GRASS = 4, BUSH_CACTUS = 5, BUSH_BERRY = 6 };
	// Matches EdenTreeShape::Season's values -- kept as a separate enum (not a shared include)
	// since bushes don't need any of EdenTreeShape's tree-specific fields, just the same four
	// season indices for their own, much simpler leaf-color table.
	enum Season { SEASON_SPRING = 0, SEASON_SUMMER = 1, SEASON_FALL = 2, SEASON_WINTER = 3 };

	void set_seed_override(int p_seed);
	int get_seed_override() const { return _seed_override; }

	void set_bush_type(int p_type);
	int get_bush_type() const { return _bush_type; }

	void set_season(int p_season);
	int get_season() const { return _season; }

	void set_leaf_subdivisions(int p_level);
	int get_leaf_subdivisions() const { return _leaf_subdivisions; }

	void _notification(int p_what);

protected:
	static void _bind_methods();

private:
	int _seed_override = -1;
	int _bush_type = BUSH_RANDOM;
	int _season = SEASON_SUMMER;
	int _leaf_subdivisions = 0;

	MeshInstance3D *_stem_inst = nullptr;
	MeshInstance3D *_leaf_inst = nullptr;
	MeshInstance3D *_fruit_inst = nullptr;
	Ref<StandardMaterial3D> _stem_mat;
	Ref<StandardMaterial3D> _leaf_mat;
	Ref<StandardMaterial3D> _fruit_mat;

	void _rebuild();
	static uint32_t _hash_position(const Vector3 &p_pos);
};

VARIANT_ENUM_CAST(EdenBushInstance::BushType);
VARIANT_ENUM_CAST(EdenBushInstance::Season);

#endif // EDEN_BUSH_INSTANCE_H
