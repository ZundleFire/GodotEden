#include "eden_tree_instance.h"

#include "eden_tree_generator.h"
#include "eden_tree_shape.h"
#include "scene/resources/mesh.h"

void EdenTreeInstance::set_seed_override(int p_seed) {
	_seed_override = p_seed;
	if (is_inside_tree()) {
		_rebuild();
	}
}

void EdenTreeInstance::set_tree_type(int p_type) {
	_tree_type = p_type;
	if (is_inside_tree()) {
		_rebuild();
	}
}

void EdenTreeInstance::set_season(int p_season) {
	_season = p_season;
	if (is_inside_tree()) {
		_rebuild();
	}
}

void EdenTreeInstance::set_leaf_subdivisions(int p_level) {
	_leaf_subdivisions = p_level;
	if (is_inside_tree()) {
		_rebuild();
	}
}

void EdenTreeInstance::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		_rebuild();
	}
}

// Seeds the same way the old GDScript did: hashing a quantized world position so the same spot
// always regrows the same tree shape (stable across reloads) but different spots differ, with
// quantization avoiding float-precision seed clashes for nearby positions. Does not need to
// match GDScript's own hash() bit-for-bit -- nothing else depends on that anymore.
uint32_t EdenTreeInstance::_hash_position(const Vector3 &p_pos) {
	const int64_t xi = int64_t(Math::round(p_pos.x * 4.0));
	const int64_t yi = int64_t(Math::round(p_pos.y * 4.0));
	const int64_t zi = int64_t(Math::round(p_pos.z * 4.0));

	uint32_t h = 2166136261u;
	h = (h ^ uint32_t(xi)) * 16777619u;
	h = (h ^ uint32_t(yi)) * 16777619u;
	h = (h ^ uint32_t(zi)) * 16777619u;
	return h;
}

void EdenTreeInstance::_rebuild() {
	Ref<RandomNumberGenerator> rng;
	rng.instantiate();
	if (_seed_override >= 0) {
		rng->set_seed(uint64_t(_seed_override));
	} else {
		rng->set_seed(_hash_position(get_global_position()));
	}

	Ref<EdenTreeShape> shape;
	if (_tree_type == 0) {
		shape = EdenTreeGenerator::build_random_shape(rng);
	} else {
		shape = EdenTreeGenerator::species_shape(_tree_type - 1, rng);
	}
	shape->set_season(_season);
	shape->set_leaf_subdivisions(_leaf_subdivisions);

	Dictionary result = EdenTreeGenerator::build(rng, shape);

	if (_trunk_inst == nullptr) {
		_trunk_inst = memnew(MeshInstance3D);
		_trunk_mat.instantiate();
		_trunk_mat->set_roughness(0.85f);
		_trunk_inst->set_material_override(_trunk_mat);
		add_child(_trunk_inst);
	}
	_trunk_inst->set_mesh(result["trunk_mesh"]);
	_trunk_mat->set_albedo(result["trunk_color"]);

	Ref<ArrayMesh> foliage_mesh = result["foliage_mesh"];
	if (foliage_mesh.is_valid() && foliage_mesh->get_surface_count() > 0) {
		if (_foliage_inst == nullptr) {
			_foliage_inst = memnew(MeshInstance3D);
			_foliage_mat.instantiate();
			_foliage_mat->set_roughness(0.95f);
			_foliage_inst->set_material_override(_foliage_mat);
			add_child(_foliage_inst);
		}
		_foliage_inst->set_mesh(foliage_mesh);
		_foliage_mat->set_albedo(result["leaf_color"]);
		_foliage_inst->set_visible(true);
	} else if (_foliage_inst != nullptr) {
		// Deciduous species in Winter: no foliage this rebuild.
		_foliage_inst->set_visible(false);
	}

	Ref<ArrayMesh> fruit_mesh = result["fruit_mesh"];
	if (fruit_mesh.is_valid() && fruit_mesh->get_surface_count() > 0) {
		if (_fruit_inst == nullptr) {
			_fruit_inst = memnew(MeshInstance3D);
			_fruit_mat.instantiate();
			_fruit_mat->set_roughness(0.5f);
			_fruit_inst->set_material_override(_fruit_mat);
			add_child(_fruit_inst);
		}
		_fruit_inst->set_mesh(fruit_mesh);
		_fruit_mat->set_albedo(result["fruit_color"]);
		_fruit_inst->set_visible(true);
	} else if (_fruit_inst != nullptr) {
		_fruit_inst->set_visible(false);
	}
}

void EdenTreeInstance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_seed_override", "value"), &EdenTreeInstance::set_seed_override);
	ClassDB::bind_method(D_METHOD("get_seed_override"), &EdenTreeInstance::get_seed_override);
	ClassDB::add_property("EdenTreeInstance", PropertyInfo(Variant::INT, "seed_override"), "set_seed_override", "get_seed_override");

	ClassDB::bind_method(D_METHOD("set_tree_type", "value"), &EdenTreeInstance::set_tree_type);
	ClassDB::bind_method(D_METHOD("get_tree_type"), &EdenTreeInstance::get_tree_type);
	ClassDB::add_property("EdenTreeInstance", PropertyInfo(Variant::INT, "tree_type", PROPERTY_HINT_ENUM, "Random,Oak,Pine,Birch,Willow,Palm,Dead,Fruit"), "set_tree_type", "get_tree_type");

	ClassDB::bind_method(D_METHOD("set_season", "value"), &EdenTreeInstance::set_season);
	ClassDB::bind_method(D_METHOD("get_season"), &EdenTreeInstance::get_season);
	ClassDB::add_property("EdenTreeInstance", PropertyInfo(Variant::INT, "season", PROPERTY_HINT_ENUM, "Spring,Summer,Fall,Winter"), "set_season", "get_season");

	ClassDB::bind_method(D_METHOD("set_leaf_subdivisions", "value"), &EdenTreeInstance::set_leaf_subdivisions);
	ClassDB::bind_method(D_METHOD("get_leaf_subdivisions"), &EdenTreeInstance::get_leaf_subdivisions);
	ClassDB::add_property("EdenTreeInstance", PropertyInfo(Variant::INT, "leaf_subdivisions", PROPERTY_HINT_RANGE, "0,4,1"), "set_leaf_subdivisions", "get_leaf_subdivisions");
}
