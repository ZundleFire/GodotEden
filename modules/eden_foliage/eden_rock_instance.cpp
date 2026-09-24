#include "eden_rock_instance.h"

#include "eden_tree_mesher.h"
#include "scene/resources/mesh.h"

namespace {

// One irregular chunk, sized in world units, resting on the local ground plane (y=0) and
// partly "sunk" into it like a real rock instead of floating on top -- embed_frac controls how
// much of its height sits below y=0.
void add_ground_rock(EdenTreeMesher &r_mesher, const Ref<RandomNumberGenerator> &p_rng,
		const Vector3 &p_center_xz, const Vector3 &p_size, float p_embed_frac) {
	const Vector3 center = Vector3(p_center_xz.x, p_size.y * (1.0f - p_embed_frac), p_center_xz.z);
	r_mesher.add_rock(center, p_size, p_rng);
}

} // namespace

void EdenRockInstance::set_seed_override(int p_seed) {
	_seed_override = p_seed;
	if (is_inside_tree()) {
		_rebuild();
	}
}

void EdenRockInstance::set_rock_type(int p_type) {
	_rock_type = p_type;
	if (is_inside_tree()) {
		_rebuild();
	}
}

void EdenRockInstance::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		_rebuild();
	}
}

// Same quantized-position hashing as EdenTreeInstance -- see that class for why (stable per
// world position, no cross-language hash-matching requirement).
uint32_t EdenRockInstance::_hash_position(const Vector3 &p_pos) {
	const int64_t xi = int64_t(Math::round(p_pos.x * 4.0));
	const int64_t yi = int64_t(Math::round(p_pos.y * 4.0));
	const int64_t zi = int64_t(Math::round(p_pos.z * 4.0));

	uint32_t h = 2166136261u;
	h = (h ^ uint32_t(xi)) * 16777619u;
	h = (h ^ uint32_t(yi)) * 16777619u;
	h = (h ^ uint32_t(zi)) * 16777619u;
	return h;
}

void EdenRockInstance::_rebuild() {
	Ref<RandomNumberGenerator> rng;
	rng.instantiate();
	if (_seed_override >= 0) {
		rng->set_seed(uint64_t(_seed_override));
	} else {
		rng->set_seed(_hash_position(get_global_position()));
	}

	int type = _rock_type;
	if (type == ROCK_RANDOM) {
		type = rng->randi_range(1, 5);
	}

	EdenTreeMesher mesher;
	switch (type) {
		case ROCK_SPIRE: {
			// One tall, thin standing pillar -- height dominates both horizontal axes, unlike
			// Boulder where height is at most comparable to width.
			const float sx = rng->randf_range(0.35f, 0.6f);
			const float sy = sx * rng->randf_range(2.5f, 4.0f);
			const float sz = sx * rng->randf_range(0.8f, 1.3f);
			add_ground_rock(mesher, rng, Vector3(), Vector3(sx, sy, sz), rng->randf_range(0.05f, 0.15f));
			break;
		}
		case ROCK_CRYSTAL: {
			// A cluster of thin, sharp shards angled outward/upward from a shared base point,
			// instead of resting flat on the ground like every other rock type.
			const int count = rng->randi_range(3, 6);
			for (int i = 0; i < count; i++) {
				const float sx = rng->randf_range(0.1f, 0.22f);
				const float sy = sx * rng->randf_range(2.2f, 3.5f);
				const float sz = sx * rng->randf_range(0.9f, 1.3f);
				const float angle = rng->randf_range(0.0f, float(Math::TAU));
				const float dist = rng->randf_range(0.0f, 0.25f);
				const Vector3 offset = Vector3(Math::cos(angle) * dist, 0.0f, Math::sin(angle) * dist);
				add_ground_rock(mesher, rng, offset, Vector3(sx, sy, sz), rng->randf_range(0.35f, 0.55f));
			}
			break;
		}
		case ROCK_CLUSTER: {
			// A handful of overlapping mid-size chunks piled together, like a rockfall.
			const int count = rng->randi_range(3, 5);
			for (int i = 0; i < count; i++) {
				const float sx = rng->randf_range(0.4f, 0.9f);
				const float sy = sx * rng->randf_range(0.6f, 0.9f);
				const float sz = rng->randf_range(0.4f, 0.9f);
				const float angle = rng->randf_range(0.0f, float(Math::TAU));
				const float dist = rng->randf_range(0.0f, 0.5f);
				const Vector3 offset = Vector3(Math::cos(angle) * dist, 0.0f, Math::sin(angle) * dist);
				add_ground_rock(mesher, rng, offset, Vector3(sx, sy, sz), rng->randf_range(0.25f, 0.45f));
			}
			break;
		}
		case ROCK_PEBBLES: {
			// A loose scatter of small stones spread across a wider patch of ground.
			const int count = rng->randi_range(5, 9);
			for (int i = 0; i < count; i++) {
				const float sx = rng->randf_range(0.12f, 0.3f);
				const float sy = sx * rng->randf_range(0.5f, 0.8f);
				const float sz = rng->randf_range(0.12f, 0.3f);
				const float angle = rng->randf_range(0.0f, float(Math::TAU));
				const float dist = rng->randf_range(0.0f, 1.1f);
				const Vector3 offset = Vector3(Math::cos(angle) * dist, 0.0f, Math::sin(angle) * dist);
				add_ground_rock(mesher, rng, offset, Vector3(sx, sy, sz), rng->randf_range(0.3f, 0.55f));
			}
			break;
		}
		default: { // ROCK_BOULDER
			// One dominant chunk, optionally elongated on one horizontal axis, plus a couple
			// of small chips at its base so it doesn't look like a single perfect gem.
			const float sx = rng->randf_range(1.1f, 2.0f);
			const float sy = sx * rng->randf_range(0.6f, 0.95f);
			const float sz = sx * rng->randf_range(0.7f, 1.3f);
			add_ground_rock(mesher, rng, Vector3(), Vector3(sx, sy, sz), rng->randf_range(0.15f, 0.3f));

			const int chips = rng->randi_range(0, 2);
			for (int i = 0; i < chips; i++) {
				const float cs = sx * rng->randf_range(0.2f, 0.35f);
				const float angle = rng->randf_range(0.0f, float(Math::TAU));
				const float dist = sx * rng->randf_range(0.6f, 0.9f);
				const Vector3 offset = Vector3(Math::cos(angle) * dist, 0.0f, Math::sin(angle) * dist);
				add_ground_rock(mesher, rng, offset, Vector3(cs, cs * 0.8f, cs), rng->randf_range(0.3f, 0.5f));
			}
			break;
		}
	}

	if (_mesh_inst == nullptr) {
		_mesh_inst = memnew(MeshInstance3D);
		_mat.instantiate();
		_mat->set_roughness(0.95f);
		_mesh_inst->set_material_override(_mat);
		add_child(_mesh_inst);
	}
	if (type == ROCK_CRYSTAL) {
		_mat->set_albedo(Color(0.55f, 0.62f, 0.85f));
		_mat->set_roughness(0.25f);
		_mat->set_feature(StandardMaterial3D::FEATURE_EMISSION, true);
		_mat->set_emission(Color(0.25f, 0.3f, 0.55f));
	} else {
		_mat->set_albedo(Color(0.45f, 0.43f, 0.40f));
		_mat->set_roughness(0.95f);
		_mat->set_feature(StandardMaterial3D::FEATURE_EMISSION, false);
	}
	_mesh_inst->set_mesh(mesher.build_rock_mesh());
}

void EdenRockInstance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_seed_override", "value"), &EdenRockInstance::set_seed_override);
	ClassDB::bind_method(D_METHOD("get_seed_override"), &EdenRockInstance::get_seed_override);
	ClassDB::add_property("EdenRockInstance", PropertyInfo(Variant::INT, "seed_override"), "set_seed_override", "get_seed_override");

	ClassDB::bind_method(D_METHOD("set_rock_type", "value"), &EdenRockInstance::set_rock_type);
	ClassDB::bind_method(D_METHOD("get_rock_type"), &EdenRockInstance::get_rock_type);
	ClassDB::add_property("EdenRockInstance", PropertyInfo(Variant::INT, "rock_type", PROPERTY_HINT_ENUM, "Random,Boulder,Cluster,Pebbles,Spire,Crystal"), "set_rock_type", "get_rock_type");

	BIND_ENUM_CONSTANT(ROCK_RANDOM);
	BIND_ENUM_CONSTANT(ROCK_BOULDER);
	BIND_ENUM_CONSTANT(ROCK_CLUSTER);
	BIND_ENUM_CONSTANT(ROCK_PEBBLES);
	BIND_ENUM_CONSTANT(ROCK_SPIRE);
	BIND_ENUM_CONSTANT(ROCK_CRYSTAL);
}
