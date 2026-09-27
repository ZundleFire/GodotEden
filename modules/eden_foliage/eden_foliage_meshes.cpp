#include "eden_foliage_meshes.h"

#include "eden_bush_instance.h"
#include "eden_foliage_config.h"
#include "eden_foliage_shaders.gen.h"
#include "eden_tree_generator.h"
#include "eden_tree_mesher.h"
#include "eden_tree_shape.h"

#include "core/math/random_number_generator.h"
#include "core/templates/hash_map.h"
#include "modules/noise/fastnoise_lite.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/resources/3d/capsule_shape_3d.h"
#include "scene/resources/3d/importer_mesh.h"
#include "scene/resources/3d/sphere_shape_3d.h"
#include "scene/resources/surface_tool.h"

namespace {

// Array from values (this Godot has no Array::make)
template <typename... T>
Array eden_array(const T &...p_values) {
	Array a;
	(a.push_back(Variant(p_values)), ...);
	return a;
}

struct Store {
	HashMap<uint32_t, Variant> cache;
	Ref<Shader> tree_shader, grass_shader;
	Ref<ShaderMaterial> wind_material, static_material;
	Ref<ArrayMesh> empty;
};
Store *g_store = nullptr;

Store &store() {
	if (g_store == nullptr) {
		g_store = memnew(Store);
	}
	return *g_store;
}

Ref<RandomNumberGenerator> make_rng(uint64_t p_seed) {
	Ref<RandomNumberGenerator> rng;
	rng.instantiate();
	rng->set_seed(p_seed);
	return rng;
}

// GDScript hash() of the same values, so meshes match the GDScript prototype's
uint32_t vhash(const Array &p_values) {
	return Variant(p_values).hash();
}

Array layer_key(const Ref<EdenFoliageLayer> &p_l, int p_variant) {
	return eden_array(p_l->kind, p_variant, p_l->rock_color, p_l->color_variation, p_l->moss, p_l->moss_color, p_l->lichen,
			p_l->lichen_color, p_l->facet_detail, p_l->stretch, p_l->roughness);
}

int tree_type_of(int p_kind) {
	switch (p_kind) {
		case EdenFoliageLayer::OAK:
			return EdenTreeShape::TREE_OAK;
		case EdenFoliageLayer::PINE:
			return EdenTreeShape::TREE_PINE;
		case EdenFoliageLayer::BIRCH:
			return EdenTreeShape::TREE_BIRCH;
		case EdenFoliageLayer::WILLOW:
			return EdenTreeShape::TREE_WILLOW;
		case EdenFoliageLayer::PALM:
			return EdenTreeShape::TREE_PALM;
		case EdenFoliageLayer::DEAD_TREE:
			return EdenTreeShape::TREE_DEAD;
		case EdenFoliageLayer::FRUIT_TREE:
			return EdenTreeShape::TREE_FRUIT;
	}
	return -1;
}

int bush_type_of(int p_kind) {
	switch (p_kind) {
		case EdenFoliageLayer::SHRUB:
			return EdenBushInstance::BUSH_SHRUB;
		case EdenFoliageLayer::FERN:
			return EdenBushInstance::BUSH_FERN;
		case EdenFoliageLayer::DEAD_BUSH:
			return EdenBushInstance::BUSH_DEAD;
		case EdenFoliageLayer::TALL_GRASS:
			return EdenBushInstance::BUSH_TALL_GRASS;
		case EdenFoliageLayer::CACTUS:
			return EdenBushInstance::BUSH_CACTUS;
		case EdenFoliageLayer::BERRY_BUSH:
			return EdenBushInstance::BUSH_BERRY;
	}
	return -1;
}

// Unit icosphere subdivided `level` times: vertices and CCW (from outside) triangles
void icosphere(int p_level, LocalVector<Vector3> &r_v, LocalVector<Vector3i> &r_f) {
	const float t = (1.0f + Math::sqrt(5.0f)) / 2.0f;
	const Vector3 base[12] = { Vector3(-1, t, 0), Vector3(1, t, 0), Vector3(-1, -t, 0), Vector3(1, -t, 0), Vector3(0, -1, t),
		Vector3(0, 1, t), Vector3(0, -1, -t), Vector3(0, 1, -t), Vector3(t, 0, -1), Vector3(t, 0, 1), Vector3(-t, 0, -1), Vector3(-t, 0, 1) };
	r_v.clear();
	for (const Vector3 &p : base) {
		r_v.push_back(p.normalized());
	}
	const int faces[20][3] = { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 },
		{ 10, 7, 6 }, { 7, 1, 8 }, { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 }, { 4, 9, 5 }, { 2, 4, 11 },
		{ 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
	r_f.clear();
	for (const auto &f : faces) {
		r_f.push_back(Vector3i(f[0], f[1], f[2]));
	}
	for (int l = 0; l < p_level; l++) {
		HashMap<Vector2i, int> mid;
		LocalVector<Vector3i> nf;
		for (const Vector3i &tri : r_f) {
			int m[3];
			for (int e = 0; e < 3; e++) {
				const int i = tri[e];
				const int j = tri[(e + 1) % 3];
				const Vector2i k(MIN(i, j), MAX(i, j));
				if (!mid.has(k)) {
					mid[k] = r_v.size();
					r_v.push_back(((r_v[i] + r_v[j]) * 0.5f).normalized());
				}
				m[e] = mid[k];
			}
			nf.push_back(Vector3i(tri[0], m[0], m[2]));
			nf.push_back(Vector3i(tri[1], m[1], m[0]));
			nf.push_back(Vector3i(tri[2], m[2], m[1]));
			nf.push_back(Vector3i(m[0], m[1], m[2]));
		}
		r_f = nf;
	}
}

// Faceted low-poly stone: noise-displaced icosphere, cut by cleaving planes (big fractured faces), flat base; each
// plane one shade, moss on upward facets in noise patches, lichen spots
void faceted_stone(const Ref<RandomNumberGenerator> &p_rng, const Ref<EdenFoliageLayer> &p_layer, const Vector3 &p_at, const Vector3 &p_half,
		int p_detail, PackedVector3Array &r_verts, PackedColorArray &r_colors) {
	LocalVector<Vector3> sv;
	LocalVector<Vector3i> sf;
	icosphere(p_detail, sv, sf);
	Ref<FastNoiseLite> noise;
	noise.instantiate();
	noise->set_seed(int(p_rng->randi()));
	noise->set_frequency(1.1); // broad planes: chiselled, not crumpled
	Ref<FastNoiseLite> patches; // moss comes in patches, not per-facet confetti
	patches.instantiate();
	patches->set_seed(int(p_rng->randi()));
	patches->set_frequency(2.2);
	struct Cut {
		Vector3 n;
		float d;
	};
	LocalVector<Cut> cuts;
	if (p_detail > 0) {
		const int count = p_rng->randi_range(5, 8);
		for (int i = 0; i < count; i++) {
			const float x = p_rng->randf_range(-1, 1);
			const float y = p_rng->randf_range(-0.3f, 1.0f);
			const float z = p_rng->randf_range(-1, 1);
			const Vector3 n = Vector3(x, y, z).normalized();
			const float d = p_rng->randf_range(0.55f, 0.8f);
			cuts.push_back({ n, d });
		}
	}
	LocalVector<Vector3> pts;
	for (const Vector3 &p : sv) {
		const float d = 1.0f + noise->get_noise_3dv(p) * p_layer->roughness * 1.4f;
		Vector3 q = p * d;
		for (const Cut &c : cuts) {
			const float over = q.dot(c.n) - c.d;
			if (over > 0.0f) {
				q -= c.n * over;
			}
		}
		q *= p_half;
		q.y = MAX(q.y, -p_half.y * 0.45f); // flat base: sits on the ground instead of rolling
		pts.push_back(q);
	}
	const float base_y = -p_half.y * 0.45f + p_half.y * 0.12f; // sink the base a little
	const uint64_t rng_seed = p_rng->get_seed();
	for (const Vector3i &tri : sf) {
		const Vector3 a = pts[tri.x], b = pts[tri.y], c = pts[tri.z];
		const Vector3 n = (b - a).cross(c - a).normalized(); // icosphere table is CCW from outside
		const Vector3 center = (a + b + c) / 3.0f;
		// One shade per plane (from the rounded normal), so a cleaved face reads as one face
		const Vector3i face(Math::round(n.x * 4.0f), Math::round(n.y * 4.0f), Math::round(n.z * 4.0f));
		Ref<RandomNumberGenerator> shade = make_rng(vhash(eden_array(face, int64_t(rng_seed))));
		Color col = p_layer->rock_color * (1.0f + shade->randf_range(-p_layer->color_variation, p_layer->color_variation));
		col = col.lerp(Color(col.r * 1.05f, col.g, col.b * 0.92f), shade->randf() * 0.5f); // warm/cool facet shifts
		const float up = n.y;
		const float patch = patches->get_noise_3dv(center / MAX(p_half.length(), 0.01f)) * 0.5f + 0.5f;
		if (up > 0.25f && patch < p_layer->moss * (0.4f + up)) {
			col = p_layer->moss_color * p_rng->randf_range(0.85f, 1.15f);
		} else if (p_rng->randf() < p_layer->lichen) {
			col = p_layer->lichen_color * p_rng->randf_range(0.85f, 1.15f);
		}
		col.a = 1.0f;
		const Vector3 off = p_at + Vector3(0, -base_y, 0);
		// Godot front faces are clockwise
		r_verts.push_back(a + off);
		r_verts.push_back(c + off);
		r_verts.push_back(b + off);
		r_colors.push_back(col);
		r_colors.push_back(col);
		r_colors.push_back(col);
	}
}

Ref<ArrayMesh> harvest_bush(EdenBushInstance *p_node, bool p_wind) {
	p_node->notification(Node::NOTIFICATION_READY); // builds without entering the tree (seed_override set)
	Array parts;
	for (int i = 0; i < p_node->get_child_count(); i++) {
		MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(p_node->get_child(i));
		if (mi == nullptr || mi->get_mesh().is_null() || !mi->is_visible()) {
			continue;
		}
		Ref<BaseMaterial3D> mat = mi->get_material_override();
		parts.push_back(eden_array(mi->get_mesh(), mat.is_valid() ? mat->get_albedo() : Color(0.5, 0.5, 0.5)));
	}
	memdelete(p_node);
	return EdenFoliageMeshes::merge(parts, p_wind);
}

} // namespace

// Materials -----------------------------------------------------------------------------------------------------------

Ref<Shader> EdenFoliageMeshes::get_tree_shader() {
	Store &s = store();
	if (s.tree_shader.is_null()) {
		s.tree_shader.instantiate();
		s.tree_shader->set_code(EDEN_FOLIAGE_TREE_SHADER_CODE);
	}
	return s.tree_shader;
}

Ref<Shader> EdenFoliageMeshes::get_grass_shader() {
	Store &s = store();
	if (s.grass_shader.is_null()) {
		s.grass_shader.instantiate();
		s.grass_shader->set_code(EDEN_FOLIAGE_GRASS_SHADER_CODE);
	}
	return s.grass_shader;
}

Ref<ShaderMaterial> EdenFoliageMeshes::get_material(bool p_wind) {
	Store &s = store();
	if (p_wind) {
		if (s.wind_material.is_null()) {
			s.wind_material.instantiate();
			s.wind_material->set_shader(get_tree_shader());
			s.wind_material->set_shader_parameter("u_deciduous", true); // (EdenFoliage overrides it for evergreens)
		}
		return s.wind_material;
	}
	if (s.static_material.is_null()) { // the plant shader without sway, so rocks and wood take snow and rain too
		s.static_material.instantiate();
		s.static_material->set_shader(get_tree_shader());
		s.static_material->set_shader_parameter("u_wind_strength", 0.0);
		s.static_material->set_shader_parameter("u_living", false);
	}
	return s.static_material;
}

void EdenFoliageMeshes::clear_cache() {
	if (g_store != nullptr) {
		memdelete(g_store);
		g_store = nullptr;
	}
}

// Layers --------------------------------------------------------------------------------------------------------------

Dictionary EdenFoliageMeshes::build(const Ref<EdenFoliageLayer> &p_layer, int p_variant) {
	ERR_FAIL_COND_V(p_layer.is_null(), Dictionary());
	const uint32_t key = vhash(layer_key(p_layer, p_variant));
	Store &s = store();
	if (s.cache.has(key)) {
		return s.cache[key];
	}
	Dictionary out;
	const int kind = p_layer->kind;
	const int tt = tree_type_of(kind);
	const int bt = bush_type_of(kind);
	if (tt >= 0) {
		const Dictionary t = tree(tt, EdenTreeShape::SEASON_SUMMER, p_variant);
		Ref<CapsuleShape3D> trunk;
		trunk.instantiate();
		trunk->set_radius(t["trunk_radius"]);
		trunk->set_height(MAX(float(t["trunk_height"]), float(trunk->get_radius()) * 2.0f));
		out["mesh"] = t["mesh"];
		out["shape"] = eden_array(trunk, Transform3D(Basis(), Vector3(0, trunk->get_height() * 0.5f, 0)));
	} else if (bt >= 0) {
		out["mesh"] = bush(bt, EdenTreeShape::SEASON_SUMMER, p_variant);
		out["shape"] = Array();
	} else if (kind == EdenFoliageLayer::BRANCH || kind == EdenFoliageLayer::LOG) {
		const bool is_log = kind == EdenFoliageLayer::LOG;
		Array shape;
		if (is_log) {
			Ref<CapsuleShape3D> cap;
			cap.instantiate();
			cap->set_radius(0.28f);
			cap->set_height(4.0f);
			shape = eden_array(cap, Transform3D(Basis(Vector3(0, 0, 1), Math::PI * 0.5f), Vector3(0, 0.25f, 0)));
		}
		out["mesh"] = wood(is_log, p_variant);
		out["shape"] = shape;
	} else {
		out = rock(p_layer, p_variant);
	}
	s.cache[key] = out;
	return out;
}

Ref<Mesh> EdenFoliageMeshes::build_far(const Ref<EdenFoliageLayer> &p_layer, int p_variant, int p_resolution) {
	ERR_FAIL_COND_V(p_layer.is_null(), Ref<Mesh>());
	const int res = p_resolution > 0 ? p_resolution : p_layer->far_resolution;
	Array k = layer_key(p_layer, p_variant);
	k.push_front(res);
	k.push_front("far");
	const uint32_t key = vhash(k);
	Store &s = store();
	if (!s.cache.has(key)) {
		if (p_layer->is_rock()) {
			// Rocks are already low-poly and voxel cubes armour cliffs in boxes: use the 20-facet version instead
			Ref<EdenFoliageLayer> coarse = p_layer->duplicate();
			coarse->facet_detail = 0;
			s.cache[key] = Dictionary(build(coarse, p_variant))["mesh"];
		} else {
			s.cache[key] = voxelize(Dictionary(build(p_layer, p_variant))["mesh"], res);
		}
	}
	return s.cache[key];
}

Ref<Mesh> EdenFoliageMeshes::build_simplified(const Ref<EdenFoliageLayer> &p_layer, int p_variant, float p_ratio, int p_min_tris) {
	ERR_FAIL_COND_V(p_layer.is_null(), Ref<Mesh>());
	Array k = layer_key(p_layer, p_variant);
	k.push_front(p_ratio);
	k.push_front("mid");
	const uint32_t key = vhash(k);
	Store &s = store();
	if (s.cache.has(key)) {
		return s.cache[key];
	}
	Ref<Mesh> src = Dictionary(build(p_layer, p_variant))["mesh"];
	Ref<ArrayMesh> out;
	out.instantiate();
	for (int si = 0; si < src->get_surface_count(); si++) {
		Array arrays = src->surface_get_arrays(si);
		PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];
		if (indices.size() / 3 > p_min_tris) {
			Ref<ImporterMesh> im;
			im.instantiate();
			im->add_surface(Mesh::PRIMITIVE_TRIANGLES, arrays);
			im->generate_lods(60.0f, Array());
			// The generated LOD closest to the wanted triangle count
			const float want = indices.size() * p_ratio;
			for (int l = 0; l < im->get_surface_lod_count(0); l++) {
				const Vector<int> lod = im->get_surface_lod_indices(0, l);
				if (Math::abs(lod.size() - want) < Math::abs(indices.size() - want)) {
					indices.resize(lod.size());
					for (int i = 0; i < lod.size(); i++) {
						indices.set(i, lod[i]);
					}
				}
			}
			arrays[Mesh::ARRAY_INDEX] = indices;
		}
		out->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		out->surface_set_material(si, src->surface_get_material(si));
	}
	s.cache[key] = out;
	return out;
}

Ref<ArrayMesh> EdenFoliageMeshes::empty_mesh() {
	Store &s = store();
	if (s.empty.is_null()) {
		s.empty.instantiate();
	}
	return s.empty;
}

// Builders ------------------------------------------------------------------------------------------------------------

Dictionary EdenFoliageMeshes::tree(int p_tree_type, int p_season, int p_variant) {
	Ref<RandomNumberGenerator> rng = make_rng(Variant(Vector2i(p_tree_type, p_variant)).hash());
	Ref<EdenTreeShape> shape = EdenTreeGenerator::species_shape(p_tree_type, rng);
	shape->set_season(p_season);
	const Dictionary built = EdenTreeGenerator::build(rng, shape);
	Array parts;
	const char *keys[3][2] = { { "trunk_mesh", "trunk_color" }, { "foliage_mesh", "leaf_color" }, { "fruit_mesh", "fruit_color" } };
	for (const auto &k : keys) {
		const Variant m = built.get(k[0], Variant());
		if (m.get_type() != Variant::NIL) {
			parts.push_back(eden_array(m, built.get(k[1], Color(1, 0, 1))));
		}
	}
	Dictionary out;
	out["mesh"] = merge(parts, true);
	out["trunk_radius"] = shape->get_trunk_base_radius();
	out["trunk_height"] = shape->get_trunk_height();
	return out;
}

Ref<ArrayMesh> EdenFoliageMeshes::bush(int p_bush_type, int p_season, int p_variant) {
	EdenBushInstance *b = memnew(EdenBushInstance);
	b->set_bush_type(p_bush_type);
	b->set_season(p_season);
	b->set_seed_override(int(Variant(Vector2i(100 + p_bush_type, p_variant)).hash() & 0x7fffffff));
	return harvest_bush(b, true);
}

Dictionary EdenFoliageMeshes::rock(const Ref<EdenFoliageLayer> &p_layer, int p_variant) {
	Ref<RandomNumberGenerator> rng = make_rng(vhash(eden_array(p_layer->kind, p_variant, 7)));
	PackedVector3Array verts;
	PackedColorArray colors;
	struct Stone {
		Vector3 at, half;
		int detail;
	};
	LocalVector<Stone> stones;
	if (p_layer->kind == EdenFoliageLayer::PEBBLES) {
		const int count = rng->randi_range(4, 7);
		for (int i = 0; i < count; i++) {
			const float r = rng->randf_range(0.08f, 0.22f);
			const float ax = rng->randf_range(-0.5f, 0.5f);
			const float az = rng->randf_range(-0.5f, 0.5f);
			const float hy = r * rng->randf_range(0.5f, 0.8f);
			const float hz = r * rng->randf_range(0.8f, 1.2f);
			stones.push_back({ Vector3(ax, 0, az), Vector3(r, hy, hz), 0 });
		}
	} else {
		Vector3 s = p_layer->stretch;
		if (p_layer->kind == EdenFoliageLayer::ROCK_SLAB) {
			s *= Vector3(1.9f, 0.55f, 1.3f);
		} else if (p_layer->kind == EdenFoliageLayer::ROCK_SPIRE) {
			s *= Vector3(0.45f, 2.6f, 0.45f);
		}
		const float sx = rng->randf_range(0.85f, 1.15f);
		const float sy = rng->randf_range(0.85f, 1.15f);
		const float sz = rng->randf_range(0.85f, 1.15f);
		s *= Vector3(sx, sy, sz);
		stones.push_back({ Vector3(), s * 0.5f, p_layer->facet_detail });
	}
	for (const Stone &st : stones) {
		faceted_stone(rng, p_layer, st.at, st.half, st.detail, verts, colors);
	}
	PackedVector3Array normals;
	normals.resize(verts.size());
	for (int i = 0; i + 2 < verts.size(); i += 3) {
		const Vector3 n = (verts[i + 2] - verts[i]).cross(verts[i + 1] - verts[i]).normalized();
		normals.set(i, n);
		normals.set(i + 1, n);
		normals.set(i + 2, n);
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_COLOR] = colors;
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	mesh->surface_set_material(0, get_material(false));
	Array shape;
	if (p_layer->kind != EdenFoliageLayer::PEBBLES) {
		const AABB aabb = mesh->get_aabb();
		Ref<SphereShape3D> sphere;
		sphere.instantiate();
		sphere->set_radius(MIN(MAX(aabb.size.x, aabb.size.z), aabb.size.y * 1.6f) * 0.42f);
		shape = eden_array(sphere, Transform3D(Basis(), aabb.get_center()));
	}
	Dictionary out;
	out["mesh"] = mesh;
	out["shape"] = shape;
	return out;
}

Ref<ArrayMesh> EdenFoliageMeshes::wood(bool p_is_log, int p_variant) {
	Ref<RandomNumberGenerator> rng = make_rng(Variant(Vector2i(300 + int(p_is_log), p_variant)).hash());
	EdenTreeMesher m;
	m.set_sides(p_is_log ? 5 : 4);
	const float length = p_is_log ? rng->randf_range(3.0f, 5.0f) : rng->randf_range(1.2f, 2.4f);
	const float radius = p_is_log ? rng->randf_range(0.22f, 0.34f) : rng->randf_range(0.04f, 0.07f);
	const Vector3 a(-length * 0.5f, radius * 0.8f, 0.0f);
	const float mx = rng->randf_range(-0.2f, 0.2f);
	const float mz = rng->randf_range(-0.15f, 0.15f);
	const Vector3 mid(mx, radius * 0.8f, mz);
	const float bz = rng->randf_range(-0.2f, 0.2f);
	const Vector3 b(length * 0.5f, radius * 0.7f, bz);
	m.add_segment(a, mid, radius, radius * 0.9f);
	m.add_segment(mid, b, radius * 0.9f, radius * 0.6f);
	if (!p_is_log) {
		const int twigs = rng->randi_range(2, 3);
		for (int k = 0; k < twigs; k++) {
			const Vector3 o = a.lerp(b, rng->randf_range(0.25f, 0.8f));
			const float dx = rng->randf_range(-0.3f, 0.6f);
			const float dy = rng->randf_range(0.05f, 0.4f);
			const float dz = rng->randf_range(-1.0f, 1.0f);
			const Vector3 dir = Vector3(dx, dy, dz).normalized();
			m.add_segment(o, o + dir * rng->randf_range(0.3f, 0.7f), radius * 0.5f, radius * 0.15f);
		}
	}
	const Color bark = Color(0.33, 0.24, 0.16).lerp(Color(0.42, 0.37, 0.3), rng->randf()); // weathered, some greyer
	return merge(eden_array(eden_array(m.build_mesh(), bark)), false);
}

Ref<ArrayMesh> EdenFoliageMeshes::tuft(int p_blades, const Ref<Material> &p_material) {
	Ref<RandomNumberGenerator> rng = make_rng(7); // every tuft is the same mesh; variety comes from instance scale/rotation
	Ref<SurfaceTool> st;
	st.instantiate();
	st->begin(Mesh::PRIMITIVE_TRIANGLES);
	for (int i = 0; i < p_blades; i++) {
		const float yaw = Math::TAU * (float(i) + rng->randf() * 0.5f) / float(p_blades);
		const Vector3 dir(Math::cos(yaw), 0.0f, Math::sin(yaw));
		const Vector3 side(-dir.z, 0.0f, dir.x);
		const Vector3 root = dir * rng->randf_range(0.0f, 0.15f);
		const float half_w = rng->randf_range(0.04f, 0.07f);
		const float out = rng->randf_range(0.08f, 0.2f);
		const float up = rng->randf_range(0.35f, 0.65f);
		const Vector3 tip = root + dir * out + Vector3(0, 1, 0) * up;
		st->set_uv(Vector2(0, 0));
		st->add_vertex(root - side * half_w);
		st->set_uv(Vector2(1, 0));
		st->add_vertex(root + side * half_w);
		st->set_uv(Vector2(0.5f, 1));
		st->add_vertex(tip);
	}
	st->set_material(p_material);
	return st->commit();
}

Ref<ArrayMesh> EdenFoliageMeshes::merge(const Array &p_parts, bool p_wind) {
	PackedVector3Array verts;
	PackedVector3Array normals;
	PackedColorArray colors;
	PackedInt32Array indices;
	for (int pi = 0; pi < p_parts.size(); pi++) {
		const Array part = p_parts[pi];
		Ref<Mesh> mesh = part[0];
		const Color color = part[1];
		if (mesh.is_null()) {
			continue;
		}
		for (int s = 0; s < mesh->get_surface_count(); s++) {
			const Array a = mesh->surface_get_arrays(s);
			const int base = verts.size();
			const PackedVector3Array sv = a[Mesh::ARRAY_VERTEX];
			verts.append_array(sv);
			const Variant snv = a[Mesh::ARRAY_NORMAL];
			const PackedVector3Array sn = snv.get_type() == Variant::PACKED_VECTOR3_ARRAY ? PackedVector3Array(snv) : PackedVector3Array();
			if (sn.size() == sv.size()) {
				normals.append_array(sn);
			} else {
				for (int i = 0; i < sv.size(); i++) {
					normals.push_back(Vector3(0, 1, 0));
				}
			}
			for (int i = 0; i < sv.size(); i++) {
				colors.push_back(color);
			}
			const Variant siv = a[Mesh::ARRAY_INDEX];
			const PackedInt32Array si = siv.get_type() == Variant::PACKED_INT32_ARRAY ? PackedInt32Array(siv) : PackedInt32Array();
			if (si.size() > 0) {
				for (int i = 0; i < si.size(); i++) {
					indices.push_back(base + si[i]);
				}
			} else {
				for (int i = 0; i < sv.size(); i++) {
					indices.push_back(base + i);
				}
			}
		}
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_COLOR] = colors;
	arrays[Mesh::ARRAY_INDEX] = indices;
	Ref<ArrayMesh> out;
	out.instantiate();
	if (verts.size() > 0) {
		out->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		out->surface_set_material(0, get_material(p_wind));
	}
	return out;
}

// Voxelize ------------------------------------------------------------------------------------------------------------

// Cells the surface passes through (and everything they enclose) become solid; only faces between solid and outside
// cells are emitted, coloured by the dominant vertex colour of the surface inside their cell and greedily merged.
// Roughly 100-300 triangles for a 1-10k triangle tree at resolution 5.
Ref<ArrayMesh> EdenFoliageMeshes::voxelize(const Ref<Mesh> &p_mesh, int p_resolution) {
	ERR_FAIL_COND_V(p_mesh.is_null(), Ref<ArrayMesh>());
	const AABB aabb = p_mesh->get_aabb();
	// Cells across the crown width, not the height: tall thin trees (pines) would otherwise become 1-cell pillars
	const float cell = MAX(MAX(aabb.size.x, aabb.size.z), aabb.size.y * 0.25f) / float(MAX(p_resolution, 1));
	// One empty cell of padding on each side
	const Vector3i dims = Vector3i((aabb.size / cell).floor()) + Vector3i(3, 3, 3);
	const Vector3 origin = aabb.position - Vector3(1, 1, 1) * cell;
	const int count = dims.x * dims.y * dims.z;
	auto idx = [&](const Vector3i &c) { return c.x + dims.x * (c.y + dims.y * c.z); };

	// Palette of the mesh's (flat) part colours, and per cell how much surface of each colour it holds
	LocalVector<Color> palette;
	auto colour_id = [&](const Color &c) {
		for (uint32_t i = 0; i < palette.size(); i++) {
			if (palette[i] == c) {
				return int(i);
			}
		}
		palette.push_back(c);
		return int(palette.size() - 1);
	};
	LocalVector<uint8_t> solid;
	solid.resize(count);
	memset(solid.ptr(), 0, count);
	HashMap<int64_t, int> cell_counts; // (cell * 256 + colour) -> samples

	// 1. Rasterize the surface: sample each triangle densely enough to touch every cell it crosses
	for (int s = 0; s < p_mesh->get_surface_count(); s++) {
		const Array a = p_mesh->surface_get_arrays(s);
		const PackedVector3Array v = a[Mesh::ARRAY_VERTEX];
		const Variant colsv = a[Mesh::ARRAY_COLOR];
		const PackedColorArray cols = colsv.get_type() == Variant::PACKED_COLOR_ARRAY ? PackedColorArray(colsv) : PackedColorArray();
		const Variant indv = a[Mesh::ARRAY_INDEX];
		const PackedInt32Array ind = indv.get_type() == Variant::PACKED_INT32_ARRAY ? PackedInt32Array(indv) : PackedInt32Array();
		const bool indexed = ind.size() > 0;
		const int tri_count = (indexed ? ind.size() : v.size()) / 3;
		for (int t = 0; t < tri_count; t++) {
			const int i0 = indexed ? ind[t * 3] : t * 3;
			const int i1 = indexed ? ind[t * 3 + 1] : t * 3 + 1;
			const int i2 = indexed ? ind[t * 3 + 2] : t * 3 + 2;
			const Vector3 pa = v[i0], pb = v[i1], pc = v[i2];
			const int cid = MIN(colour_id(cols.size() > i0 ? cols[i0] : Color(0.5, 0.5, 0.5)), 255);
			const float edge = MAX(MAX(pa.distance_to(pb), pb.distance_to(pc)), pc.distance_to(pa));
			const int n = CLAMP(int(Math::ceil(edge / cell * 2.0f)), 1, 24);
			for (int u = 0; u <= n; u++) {
				for (int w = 0; w <= n - u; w++) {
					const Vector3 p = pa + (pb - pa) * (float(u) / n) + (pc - pa) * (float(w) / n);
					const Vector3i c = Vector3i(((p - origin) / cell).floor());
					const int k = idx(c);
					if (k < 0 || k >= count) {
						continue;
					}
					solid[k] = 1;
					const int64_t ck = int64_t(k) * 256 + cid;
					cell_counts[ck] = cell_counts.has(ck) ? cell_counts[ck] + 1 : 1;
				}
			}
		}
	}

	// 2. Flood the outside from the padding; whatever it can't reach is solid (fills canopies and rock interiors)
	LocalVector<uint8_t> outside;
	outside.resize(count);
	memset(outside.ptr(), 0, count);
	const Vector3i dirs[6] = { Vector3i(1, 0, 0), Vector3i(-1, 0, 0), Vector3i(0, 1, 0), Vector3i(0, -1, 0), Vector3i(0, 0, 1), Vector3i(0, 0, -1) };
	LocalVector<Vector3i> stack;
	stack.push_back(Vector3i());
	outside[0] = 1;
	while (!stack.is_empty()) {
		const Vector3i c = stack[stack.size() - 1];
		stack.remove_at(stack.size() - 1);
		for (const Vector3i &d : dirs) {
			const Vector3i nc = c + d;
			if (nc.x < 0 || nc.y < 0 || nc.z < 0 || nc.x >= dims.x || nc.y >= dims.y || nc.z >= dims.z) {
				continue;
			}
			const int k = idx(nc);
			if (outside[k] == 0 && solid[k] == 0) {
				outside[k] = 1;
				stack.push_back(nc);
			}
		}
	}

	// 3. Cell colours: the colour most of the cell's surface has; enclosed cells take the mesh's most common colour
	LocalVector<int> colour;
	colour.resize(count);
	for (int k = 0; k < count; k++) {
		colour[k] = -1;
	}
	LocalVector<int> totals;
	totals.resize(MAX(palette.size(), 1u));
	for (uint32_t i = 0; i < totals.size(); i++) {
		totals[i] = 0;
	}
	LocalVector<int> best;
	best.resize(count);
	for (int k = 0; k < count; k++) {
		best[k] = -1;
	}
	for (const KeyValue<int64_t, int> &e : cell_counts) {
		const int k = int(e.key / 256);
		const int c = int(e.key % 256);
		totals[c] += e.value;
		// Ties: the lower palette id (first seen), as a stable choice
		if (e.value > best[k] || (e.value == best[k] && c < colour[k])) {
			best[k] = e.value;
			colour[k] = c;
		}
	}
	int common = 0;
	for (uint32_t i = 1; i < totals.size(); i++) {
		if (totals[i] > totals[common]) {
			common = int(i);
		}
	}
	for (int k = 0; k < count; k++) {
		if (solid[k] == 0 && outside[k] == 0) {
			colour[k] = common;
		}
		if (colour[k] < 0) {
			colour[k] = common;
		}
	}
	if (palette.is_empty()) {
		palette.push_back(Color(0.5, 0.5, 0.5));
	}

	// 4. Faces between solid and outside cells, greedily merged into rectangles of one colour per slice
	PackedVector3Array verts;
	PackedVector3Array normals;
	PackedColorArray colors;
	LocalVector<int> mask;
	for (const Vector3i &d : dirs) {
		const int ax = d.x != 0 ? 0 : (d.y != 0 ? 1 : 2); // slice axis; u, v span the slice
		const int u = (ax + 1) % 3;
		const int v = (ax + 2) % 3;
		const int sign = d[ax];
		mask.resize(dims[u] * dims[v]);
		for (int i = 0; i < dims[ax]; i++) {
			for (int jv = 0; jv < dims[v]; jv++) {
				for (int ju = 0; ju < dims[u]; ju++) {
					Vector3i c;
					c[ax] = i;
					c[u] = ju;
					c[v] = jv;
					const int k = idx(c);
					const Vector3i nc = c + d;
					const bool in_grid = nc.x >= 0 && nc.y >= 0 && nc.z >= 0 && nc.x < dims.x && nc.y < dims.y && nc.z < dims.z;
					mask[ju + jv * dims[u]] = (outside[k] == 0 && in_grid && outside[idx(nc)] == 1) ? k : -1;
				}
			}
			for (int jv = 0; jv < dims[v]; jv++) {
				for (int ju = 0; ju < dims[u]; ju++) {
					const int k0 = mask[ju + jv * dims[u]];
					if (k0 < 0) {
						continue;
					}
					const int col = colour[k0];
					int w = 1;
					while (ju + w < dims[u] && mask[ju + w + jv * dims[u]] >= 0 && colour[mask[ju + w + jv * dims[u]]] == col) {
						w++;
					}
					int h = 1;
					bool grow = true;
					while (grow && jv + h < dims[v]) {
						for (int x = 0; x < w; x++) {
							const int m = mask[ju + x + (jv + h) * dims[u]];
							if (m < 0 || colour[m] != col) {
								grow = false;
								break;
							}
						}
						if (grow) {
							h++;
						}
					}
					for (int y = 0; y < h; y++) {
						for (int x = 0; x < w; x++) {
							mask[ju + x + (jv + y) * dims[u]] = -1;
						}
					}
					Vector3 corners[4];
					const Vector2i uvs[4] = { Vector2i(0, 0), Vector2i(w, 0), Vector2i(w, h), Vector2i(0, h) };
					for (int q = 0; q < 4; q++) {
						Vector3 p;
						p[ax] = i + (sign > 0 ? 1 : 0);
						p[u] = ju + uvs[q].x;
						p[v] = jv + uvs[q].y;
						corners[q] = origin + p * cell;
					}
					// Godot front faces: (v1 - v0) x (v2 - v0) points into the solid; flip if not
					const bool inward = (corners[1] - corners[0]).cross(corners[2] - corners[0]).dot(Vector3(d)) < 0.0f;
					const int order_in[6] = { 0, 1, 2, 0, 2, 3 };
					const int order_out[6] = { 0, 2, 1, 0, 3, 2 };
					for (int t = 0; t < 6; t++) {
						verts.push_back(corners[inward ? order_in[t] : order_out[t]]);
						normals.push_back(Vector3(d));
						colors.push_back(palette[col]);
					}
				}
			}
		}
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_COLOR] = colors;
	Ref<ArrayMesh> out;
	out.instantiate();
	if (verts.size() > 0) {
		out->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		out->surface_set_material(0, get_material(false));
	}
	return out;
}

void EdenFoliageMeshes::_bind_methods() {
	ClassDB::bind_static_method("EdenFoliageMeshes", D_METHOD("get_material", "wind"), &EdenFoliageMeshes::get_material, DEFVAL(true));
	ClassDB::bind_static_method("EdenFoliageMeshes", D_METHOD("get_tree_shader"), &EdenFoliageMeshes::get_tree_shader);
	ClassDB::bind_static_method("EdenFoliageMeshes", D_METHOD("get_grass_shader"), &EdenFoliageMeshes::get_grass_shader);
	ClassDB::bind_static_method("EdenFoliageMeshes", D_METHOD("build", "layer", "variant"), &EdenFoliageMeshes::build);
	ClassDB::bind_static_method("EdenFoliageMeshes", D_METHOD("build_far", "layer", "variant", "resolution"), &EdenFoliageMeshes::build_far, DEFVAL(0));
	ClassDB::bind_static_method("EdenFoliageMeshes", D_METHOD("build_simplified", "layer", "variant", "ratio", "min_tris"), &EdenFoliageMeshes::build_simplified, DEFVAL(400));
	ClassDB::bind_static_method("EdenFoliageMeshes", D_METHOD("voxelize", "mesh", "resolution"), &EdenFoliageMeshes::voxelize);
	ClassDB::bind_static_method("EdenFoliageMeshes", D_METHOD("empty_mesh"), &EdenFoliageMeshes::empty_mesh);
	ClassDB::bind_static_method("EdenFoliageMeshes", D_METHOD("tree", "tree_type", "season", "variant"), &EdenFoliageMeshes::tree);
	ClassDB::bind_static_method("EdenFoliageMeshes", D_METHOD("bush", "bush_type", "season", "variant"), &EdenFoliageMeshes::bush);
	ClassDB::bind_static_method("EdenFoliageMeshes", D_METHOD("rock", "layer", "variant"), &EdenFoliageMeshes::rock);
	ClassDB::bind_static_method("EdenFoliageMeshes", D_METHOD("wood", "is_log", "variant"), &EdenFoliageMeshes::wood);
	ClassDB::bind_static_method("EdenFoliageMeshes", D_METHOD("tuft", "blades", "material"), &EdenFoliageMeshes::tuft);
	ClassDB::bind_static_method("EdenFoliageMeshes", D_METHOD("merge", "parts", "wind"), &EdenFoliageMeshes::merge);
	ClassDB::bind_static_method("EdenFoliageMeshes", D_METHOD("clear_cache"), &EdenFoliageMeshes::clear_cache);
}
