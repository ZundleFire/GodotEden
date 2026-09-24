#include "eden_bush_instance.h"

#include "core/math/color.h"
#include "eden_tree_mesher.h"
#include "scene/resources/mesh.h"

namespace {

struct LeafColorRange {
	bool has_leaves = true;
	float hue_min = 0.0f, hue_max = 0.0f;
	float sat_min = 0.0f, sat_max = 0.0f;
	float val_min = 0.0f, val_max = 0.0f;
};

// Deciduous shrub: bare in Winter (like Oak/Birch), warm colors in Fall.
LeafColorRange shrub_leaf_range(int p_season) {
	LeafColorRange r;
	switch (p_season) {
		case EdenBushInstance::SEASON_WINTER:
			r.has_leaves = false;
			break;
		case EdenBushInstance::SEASON_SPRING:
			r.hue_min = 0.27f; r.hue_max = 0.34f; r.sat_min = 0.50f; r.sat_max = 0.70f; r.val_min = 0.55f; r.val_max = 0.70f;
			break;
		case EdenBushInstance::SEASON_FALL:
			r.hue_min = 0.02f; r.hue_max = 0.13f; r.sat_min = 0.55f; r.sat_max = 0.85f; r.val_min = 0.45f; r.val_max = 0.65f;
			break;
		default: // SUMMER
			r.hue_min = 0.22f; r.hue_max = 0.36f; r.sat_min = 0.45f; r.sat_max = 0.75f; r.val_min = 0.40f; r.val_max = 0.60f;
			break;
	}
	return r;
}

// Evergreen fern: keeps its fronds year-round, just duller in Winter -- same evergreen logic
// Pine uses in eden_tree_generator.cpp.
LeafColorRange fern_leaf_range(int p_season) {
	LeafColorRange r;
	r.hue_min = 0.32f;
	r.hue_max = 0.40f;
	r.sat_min = 0.45f;
	r.sat_max = 0.65f;
	if (p_season == EdenBushInstance::SEASON_WINTER) {
		r.val_min = 0.22f;
		r.val_max = 0.32f;
	} else {
		r.val_min = 0.28f;
		r.val_max = 0.42f;
	}
	return r;
}

Vector3 stem_direction(const Ref<RandomNumberGenerator> &p_rng, float p_upright) {
	const float angle = p_rng->randf_range(0.0f, float(Math::TAU));
	const float lift = p_rng->randf_range(p_upright, 1.0f);
	const Vector3 horiz = Vector3(Math::cos(angle), 0.0f, Math::sin(angle));
	return (horiz * (1.0f - lift) + Vector3(0, 1, 0) * lift).normalized();
}

} // namespace

void EdenBushInstance::set_seed_override(int p_seed) {
	_seed_override = p_seed;
	if (is_inside_tree()) {
		_rebuild();
	}
}

void EdenBushInstance::set_bush_type(int p_type) {
	_bush_type = p_type;
	if (is_inside_tree()) {
		_rebuild();
	}
}

void EdenBushInstance::set_season(int p_season) {
	_season = p_season;
	if (is_inside_tree()) {
		_rebuild();
	}
}

void EdenBushInstance::set_leaf_subdivisions(int p_level) {
	_leaf_subdivisions = p_level;
	if (is_inside_tree()) {
		_rebuild();
	}
}

void EdenBushInstance::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		_rebuild();
	}
}

uint32_t EdenBushInstance::_hash_position(const Vector3 &p_pos) {
	const int64_t xi = int64_t(Math::round(p_pos.x * 4.0));
	const int64_t yi = int64_t(Math::round(p_pos.y * 4.0));
	const int64_t zi = int64_t(Math::round(p_pos.z * 4.0));

	uint32_t h = 2166136261u;
	h = (h ^ uint32_t(xi)) * 16777619u;
	h = (h ^ uint32_t(yi)) * 16777619u;
	h = (h ^ uint32_t(zi)) * 16777619u;
	return h;
}

void EdenBushInstance::_rebuild() {
	Ref<RandomNumberGenerator> rng;
	rng.instantiate();
	if (_seed_override >= 0) {
		rng->set_seed(uint64_t(_seed_override));
	} else {
		rng->set_seed(_hash_position(get_global_position()));
	}

	int type = _bush_type;
	if (type == BUSH_RANDOM) {
		type = rng->randi_range(1, 6);
	}

	EdenTreeMesher mesher;
	mesher.set_sides(5);
	mesher.set_cap_ends(true);

	bool has_leaves = false;
	Vector<Vector3> leaf_tips;
	float leaf_radius = 0.0f;

	switch (type) {
		case BUSH_TALL_GRASS: {
			// Many thin, short blades, no leaf blobs at all -- the blades themselves ARE the
			// silhouette, unlike every other bush type where stems are just an armature.
			const int stems = rng->randi_range(8, 14);
			for (int i = 0; i < stems; i++) {
				const Vector3 dir = stem_direction(rng, 0.7f);
				const float len = rng->randf_range(0.2f, 0.5f);
				mesher.add_segment(Vector3(), dir * len, 0.015f, 0.002f);
			}
			has_leaves = false;
			break;
		}
		case BUSH_CACTUS: {
			// A few thick, mostly-vertical arms -- fewer and fatter than any other bush type,
			// with rounded (domed) tips from the mesher's default cap style. No leaves, ever.
			const int arms = rng->randi_range(2, 4);
			for (int i = 0; i < arms; i++) {
				const Vector3 dir = stem_direction(rng, 0.85f);
				const float len = rng->randf_range(0.35f, 0.7f);
				mesher.add_segment(Vector3(), dir * len, 0.09f, 0.06f);
			}
			has_leaves = false;
			break;
		}
		case BUSH_FERN: {
			const int stems = rng->randi_range(6, 10);
			for (int i = 0; i < stems; i++) {
				const Vector3 dir = stem_direction(rng, 0.75f);
				const float len = rng->randf_range(0.4f, 0.9f);
				const Vector3 tip = dir * len;
				mesher.add_segment(Vector3(), tip, 0.025f, 0.008f);
				leaf_tips.push_back(tip);
			}
			has_leaves = true;
			leaf_radius = rng->randf_range(0.15f, 0.25f);
			break;
		}
		case BUSH_DEAD: {
			const int stems = rng->randi_range(5, 9);
			for (int i = 0; i < stems; i++) {
				const Vector3 dir = stem_direction(rng, 0.35f);
				const float len = rng->randf_range(0.3f, 0.65f);
				mesher.add_segment(Vector3(), dir * len, 0.03f, 0.006f);
			}
			has_leaves = false;
			break;
		}
		case BUSH_BERRY: {
			// Smaller and denser than Shrub -- more stems packed into a tighter mound, like a
			// real berry bush rather than a shade shrub. Fruit is added separately below.
			const int stems = rng->randi_range(6, 10);
			for (int i = 0; i < stems; i++) {
				const Vector3 dir = stem_direction(rng, 0.5f);
				const float len = rng->randf_range(0.2f, 0.45f);
				const Vector3 tip = dir * len;
				mesher.add_segment(Vector3(), tip, 0.025f, 0.012f);
				leaf_tips.push_back(tip);
			}
			has_leaves = true;
			leaf_radius = rng->randf_range(0.18f, 0.28f);
			break;
		}
		default: { // BUSH_SHRUB
			const int stems = rng->randi_range(4, 7);
			for (int i = 0; i < stems; i++) {
				const Vector3 dir = stem_direction(rng, 0.45f);
				const float len = rng->randf_range(0.3f, 0.7f);
				const Vector3 tip = dir * len;
				mesher.add_segment(Vector3(), tip, 0.035f, 0.015f);
				leaf_tips.push_back(tip);
			}
			has_leaves = true;
			leaf_radius = rng->randf_range(0.25f, 0.45f);
			break;
		}
	}

	// Grass blades and cactus arms are green flesh, not brown woody stems -- everything else
	// keeps the original brown.
	const bool green_stem = type == BUSH_TALL_GRASS || type == BUSH_CACTUS;
	const Color stem_color = green_stem ? Color(0.35f, 0.5f, 0.22f) : Color(0.32f, 0.24f, 0.15f);

	if (_stem_inst == nullptr) {
		_stem_inst = memnew(MeshInstance3D);
		_stem_mat.instantiate();
		_stem_mat->set_roughness(0.9f);
		_stem_inst->set_material_override(_stem_mat);
		add_child(_stem_inst);
	}
	_stem_mat->set_albedo(stem_color);
	_stem_inst->set_mesh(mesher.build_mesh());

	Ref<ArrayMesh> leaf_mesh;
	Color leaf_color;
	bool leaves_visible_this_season = false;
	if (has_leaves) {
		LeafColorRange range = (type == BUSH_FERN) ? fern_leaf_range(_season) : shrub_leaf_range(_season);
		leaves_visible_this_season = range.has_leaves;
		if (range.has_leaves) {
			EdenTreeMesher leaf_mesher;
			leaf_mesher.set_leaf_jitter(0.22f);
			leaf_mesher.set_leaf_subdivisions(_leaf_subdivisions);
			for (const Vector3 &tip : leaf_tips) {
				const int clusters = rng->randi_range(1, 2);
				for (int i = 0; i < clusters; i++) {
					const Vector3 offset = Vector3(
												rng->randf_range(-1.0f, 1.0f), rng->randf_range(-0.3f, 0.5f), rng->randf_range(-1.0f, 1.0f)) *
							leaf_radius * 0.4f;
					const float blob_radius = leaf_radius * rng->randf_range(0.7f, 1.0f);
					leaf_mesher.add_leaf_blob(tip + offset, blob_radius, rng);
				}
			}
			leaf_mesh = leaf_mesher.build_foliage_mesh();
			leaf_color = Color::from_hsv(
					rng->randf_range(range.hue_min, range.hue_max),
					rng->randf_range(range.sat_min, range.sat_max),
					rng->randf_range(range.val_min, range.val_max));
		}
	}

	if (leaf_mesh.is_valid() && leaf_mesh->get_surface_count() > 0) {
		if (_leaf_inst == nullptr) {
			_leaf_inst = memnew(MeshInstance3D);
			_leaf_mat.instantiate();
			_leaf_mat->set_roughness(0.95f);
			_leaf_inst->set_material_override(_leaf_mat);
			add_child(_leaf_inst);
		}
		_leaf_inst->set_mesh(leaf_mesh);
		_leaf_mat->set_albedo(leaf_color);
		_leaf_inst->set_visible(true);
	} else if (_leaf_inst != nullptr) {
		_leaf_inst->set_visible(false);
	}

	// Berries: only BUSH_BERRY, only while it actually has leaves, and only Summer/Fall --
	// same reasoning as EdenTreeInstance's fruit (blossom in Spring, bare in Winter, fruit sits
	// between).
	const bool wants_berries = type == BUSH_BERRY && leaves_visible_this_season &&
			(_season == SEASON_SUMMER || _season == SEASON_FALL);
	Ref<ArrayMesh> fruit_mesh;
	Color fruit_color;
	if (wants_berries) {
		EdenTreeMesher fruit_mesher;
		const float berry_radius = leaf_radius * 0.3f;
		for (const Vector3 &tip : leaf_tips) {
			const int count = rng->randi_range(0, 2);
			for (int i = 0; i < count; i++) {
				// Near the leaf blobs' own outer surface, not buried near the tip -- see
				// eden_tree_generator.cpp's build_fruit_mesh for why a small offset hides fruit
				// entirely inside the foliage.
				Vector3 dir = Vector3(
						rng->randf_range(-1.0f, 1.0f), rng->randf_range(-0.3f, 0.6f), rng->randf_range(-1.0f, 1.0f));
				if (dir.length_squared() < CMP_EPSILON2) {
					dir = Vector3(0, 1, 0);
				} else {
					dir.normalize();
				}
				const Vector3 offset = dir * (leaf_radius * rng->randf_range(0.8f, 1.15f));
				fruit_mesher.add_leaf_blob(tip + offset, berry_radius * rng->randf_range(0.85f, 1.15f), rng);
			}
		}
		fruit_mesh = fruit_mesher.build_foliage_mesh();
		fruit_color = Color::from_hsv(rng->randf_range(0.0f, 0.05f), rng->randf_range(0.75f, 0.9f), rng->randf_range(0.5f, 0.7f));
	}

	if (fruit_mesh.is_valid() && fruit_mesh->get_surface_count() > 0) {
		if (_fruit_inst == nullptr) {
			_fruit_inst = memnew(MeshInstance3D);
			_fruit_mat.instantiate();
			_fruit_mat->set_roughness(0.5f);
			_fruit_inst->set_material_override(_fruit_mat);
			add_child(_fruit_inst);
		}
		_fruit_inst->set_mesh(fruit_mesh);
		_fruit_mat->set_albedo(fruit_color);
		_fruit_inst->set_visible(true);
	} else if (_fruit_inst != nullptr) {
		_fruit_inst->set_visible(false);
	}
}

void EdenBushInstance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_seed_override", "value"), &EdenBushInstance::set_seed_override);
	ClassDB::bind_method(D_METHOD("get_seed_override"), &EdenBushInstance::get_seed_override);
	ClassDB::add_property("EdenBushInstance", PropertyInfo(Variant::INT, "seed_override"), "set_seed_override", "get_seed_override");

	ClassDB::bind_method(D_METHOD("set_bush_type", "value"), &EdenBushInstance::set_bush_type);
	ClassDB::bind_method(D_METHOD("get_bush_type"), &EdenBushInstance::get_bush_type);
	ClassDB::add_property("EdenBushInstance", PropertyInfo(Variant::INT, "bush_type", PROPERTY_HINT_ENUM, "Random,Shrub,Fern,DeadBush,TallGrass,Cactus,Berry"), "set_bush_type", "get_bush_type");

	ClassDB::bind_method(D_METHOD("set_season", "value"), &EdenBushInstance::set_season);
	ClassDB::bind_method(D_METHOD("get_season"), &EdenBushInstance::get_season);
	ClassDB::add_property("EdenBushInstance", PropertyInfo(Variant::INT, "season", PROPERTY_HINT_ENUM, "Spring,Summer,Fall,Winter"), "set_season", "get_season");

	ClassDB::bind_method(D_METHOD("set_leaf_subdivisions", "value"), &EdenBushInstance::set_leaf_subdivisions);
	ClassDB::bind_method(D_METHOD("get_leaf_subdivisions"), &EdenBushInstance::get_leaf_subdivisions);
	ClassDB::add_property("EdenBushInstance", PropertyInfo(Variant::INT, "leaf_subdivisions", PROPERTY_HINT_RANGE, "0,4,1"), "set_leaf_subdivisions", "get_leaf_subdivisions");

	BIND_ENUM_CONSTANT(BUSH_RANDOM);
	BIND_ENUM_CONSTANT(BUSH_SHRUB);
	BIND_ENUM_CONSTANT(BUSH_FERN);
	BIND_ENUM_CONSTANT(BUSH_DEAD);
	BIND_ENUM_CONSTANT(BUSH_TALL_GRASS);
	BIND_ENUM_CONSTANT(BUSH_CACTUS);
	BIND_ENUM_CONSTANT(BUSH_BERRY);

	BIND_ENUM_CONSTANT(SEASON_SPRING);
	BIND_ENUM_CONSTANT(SEASON_SUMMER);
	BIND_ENUM_CONSTANT(SEASON_FALL);
	BIND_ENUM_CONSTANT(SEASON_WINTER);
}
