#include "eden_foliage.h"

#include "eden_foliage_meshes.h"

#include "core/config/engine.h"
#include "core/io/image.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "core/os/time.h"
#include "modules/noise/fastnoise_lite.h"
#include "modules/voxel/terrain/instancing/voxel_instance_generator.h"
#include "modules/voxel/terrain/instancing/voxel_instance_library.h"
#include "servers/rendering/rendering_server.h"

using zylann::voxel::VoxelInstanceGenerator;
using zylann::voxel::VoxelInstanceLibrary;
using zylann::voxel::VoxelInstancer;

namespace {

// Array from values (this Godot has no Array::make)
template <typename... T>
Array eden_array(const T &...p_values) {
	Array a;
	(a.push_back(Variant(p_values)), ...);
	return a;
}

const int MAX_INSTANCER_LOD = 7; // VoxelInstancer::MAX_LOD - 1
const int MATERIAL_BITS = 6; // grass, rock, snow, sand, dirt, moss (MIXEL4 ids)
const int SITE_SEED_BIG = 4501;
const int SITE_SEED_MEDIUM = 1802;

using L = EdenFoliageLayer;

// Spacing groups: what shares the big site grid, and the medium one; the rest scatter freely
bool spaced_big(int k) {
	switch (k) {
		case L::OAK:
		case L::BIRCH:
		case L::WILLOW:
		case L::FRUIT_TREE:
		case L::PINE:
		case L::PALM:
		case L::DEAD_TREE:
		case L::LOG:
		case L::BOULDER:
		case L::ROCK_SLAB:
		case L::ROCK_SPIRE:
		case L::CACTUS:
			return true;
	}
	return false;
}

bool spaced_medium(int k) {
	return k == L::SHRUB || k == L::FERN || k == L::BERRY_BUSH || k == L::DEAD_BUSH || k == L::TALL_GRASS;
}

// Plants whose leaves follow the seasons; the rest (conifers, palms, cacti, dead wood, rocks) don't
bool deciduous(int k) {
	return k == L::OAK || k == L::BIRCH || k == L::WILLOW || k == L::FRUIT_TREE || k == L::SHRUB || k == L::FERN ||
			k == L::BERRY_BUSH || k == L::TALL_GRASS;
}

Vector2 intersect(Vector2 a, Vector2 b) {
	return Vector2(MAX(a.x, b.x), MIN(a.y, b.y));
}

PackedInt32Array material_ids(int p_bits) {
	PackedInt32Array ids;
	for (int i = 0; i < MATERIAL_BITS; i++) {
		if (p_bits & (1 << i)) {
			ids.push_back(i);
		}
	}
	return ids;
}

Ref<Resource> new_resource(const StringName &p_class) {
	return Ref<Resource>(Object::cast_to<Resource>(ClassDB::instantiate(p_class)));
}

int tris(const Ref<Mesh> &p_mesh) {
	int n = 0;
	for (int s = 0; s < p_mesh->get_surface_count(); s++) {
		const Variant idx = p_mesh->surface_get_arrays(s)[Mesh::ARRAY_INDEX];
		n += (idx.get_type() == Variant::PACKED_INT32_ARRAY ? PackedInt32Array(idx).size() : 0) / 3;
	}
	return n;
}

} // namespace

EdenFoliage::EdenFoliage() {
	set_up_mode(zylann::voxel::UP_MODE_SPHERE);
}

// Properties ----------------------------------------------------------------------------------------------------------

void EdenFoliage::set_config(const Ref<EdenFoliageConfig> &p_config) {
	config = p_config;
	_rebuild();
}

#define EDEN_F_ACTION_REBUILD _rebuild()
#define EDEN_F_ACTION_MATERIALS _update_materials()
#define EDEN_F_SET(m_type, m_name, m_default, m_vtype, m_hint, m_hint_string, m_group, m_action) \
	void EdenFoliage::set_##m_name(m_type p_value) {                                               \
		if (m_name == p_value) {                                                                   \
			return;                                                                                \
		}                                                                                          \
		m_name = p_value;                                                                          \
		EDEN_F_ACTION_##m_action;                                                                  \
	}
EDEN_FOLIAGE_PROPERTIES(EDEN_F_SET)
#undef EDEN_F_SET

void EdenFoliage::set_biome_health_map(const Ref<Texture2D> &p_map) {
	biome_health_map = p_map;
	_update_materials();
}

namespace {

// The plant shader's hash13 (eden_tree_lowpoly.gdshader), in the same float steps so CPU and GPU agree
float hash13(Vector3 p) {
	auto fr = [](float x) { return x - Math::floor(x); };
	p = Vector3(fr(p.x * 0.1031f), fr(p.y * 0.1030f), fr(p.z * 0.0973f));
	const float d = p.x * (p.y + 33.33f) + p.y * (p.z + 33.33f) + p.z * (p.x + 33.33f);
	p += Vector3(d, d, d);
	return fr((p.x + p.y) * p.z);
}

// eden_health_keep(): the share of a stand still standing at a health
float health_keep(float h) {
	const float t = CLAMP((h - 0.05f) / 0.6f, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

} // namespace

float EdenFoliage::health_at(const Vector3 &p_dir) const {
	if (biome_health_image.is_null() || biome_health_image->is_empty() || biome_health_map.is_null()) {
		return 1.0f;
	}
	// As eden_health_at(): lat/lon cell grid, bilinear with repeat
	const Vector3 n = p_dir.normalized();
	const float lat = Math::asin(CLAMP(n.y, -1.0f, 1.0f));
	const float lon = Math::atan2(n.x, n.z);
	const int w = biome_health_image->get_width();
	const int h = biome_health_image->get_height();
	const float u = (lon + Math::PI) / biome_health_cell / float(w);
	const float v = (lat / biome_health_cell + float(h) * 0.5f) / float(h);
	const float fx = u * w - 0.5f, fy = v * h - 0.5f;
	const int x0 = int(Math::floor(fx)), y0 = int(Math::floor(fy));
	const float tx = fx - x0, ty = fy - y0;
	auto px = [&](int x, int y) { return biome_health_image->get_pixel(Math::posmod(x, w), Math::posmod(y, h)).r; };
	return Math::lerp(Math::lerp(px(x0, y0), px(x0 + 1, y0), tx), Math::lerp(px(x0, y0 + 1), px(x0 + 1, y0 + 1), tx), ty);
}

bool EdenFoliage::is_plant_kept(const Vector3 &p_world_position, const Vector3 &p_planet_centre) const {
	return hash13(p_world_position + Vector3(0.37f, 0.37f, 0.37f)) <= health_keep(health_at(p_world_position - p_planet_centre));
}

// Plants the shader thinned out lose their colliders (and get them back when health returns)
void EdenFoliage::_update_thinned_colliders() {
	Node3D *terrain = _terrain();
	const Vector3 centre = terrain != nullptr ? terrain->get_global_position() : Vector3();
	const bool thinning = biome_health_image.is_valid() && biome_health_map.is_valid();
	for (int i = 0; i < get_child_count(); i++) {
		CollisionObject3D *body = Object::cast_to<CollisionObject3D>(get_child(i));
		if (body == nullptr || !body->has_method("get_library_item_id")) {
			continue;
		}
		const int kind = item_kinds.get(int(body->call("get_library_item_id")), -1);
		const bool living = kind >= 0 && !(kind >= L::PEBBLES && kind <= L::ROCK_SPIRE) && kind != L::BRANCH && kind != L::LOG;
		const bool kept = !thinning || !living || is_plant_kept(body->get_global_position(), centre);
		const uint32_t layer = kept ? uint32_t(EDEN_FOLIAGE_COLLISION_LAYER) : 0u;
		if (body->get_collision_layer() != layer) {
			body->set_collision_layer(layer);
		}
	}
}

void EdenFoliage::set_biome_health_cell(float p_cell) {
	biome_health_cell = MAX(p_cell, 1e-4f);
	_update_materials();
}

Ref<EdenFoliageConfig> EdenFoliage::_get_config() {
	if (config.is_null()) {
		config = EdenFoliageConfig::make_default();
	}
	return config;
}

void EdenFoliage::apply_quality(const Dictionary &p_values) {
	bool dirty = false;
	batching = true;
	for (const Variant &k : p_values.keys()) {
		if (get(k) != p_values[k]) {
			set(k, p_values[k]);
			dirty = true;
		}
	}
	batching = false;
	if (dirty) {
		_rebuild();
	}
}

// Resource properties don't signal edits made deep inside arrays of sub-resources, so the editor polls a cheap
// signature of the config and rebuilds when it changes
uint32_t EdenFoliage::_config_signature() {
	Array values;
	Ref<EdenFoliageConfig> cfg = _get_config();
	values.push_back(eden_array(cfg->forest_patch_size, cfg->forest_coverage, cfg->forest_edge, cfg->forest_seed));
	auto resource_values = [](const Ref<Resource> &r) {
		Array out;
		List<PropertyInfo> props;
		r->get_property_list(&props);
		for (const PropertyInfo &p : props) {
			if ((p.usage & PROPERTY_USAGE_STORAGE) && p.name != "layers" && !String(p.name).begins_with("resource_") && p.name != "script") {
				out.push_back(r->get(p.name));
			}
		}
		return out;
	};
	for (int i = 0; i < cfg->biomes.size(); i++) {
		Ref<EdenFoliageBiome> b = cfg->biomes[i];
		if (b.is_null()) {
			continue;
		}
		values.push_back(resource_values(b));
		for (int j = 0; j < b->layers.size(); j++) {
			Ref<EdenFoliageLayer> l = b->layers[j];
			if (l.is_valid()) {
				values.push_back(resource_values(l));
			}
		}
	}
	return Variant(values).hash();
}

void EdenFoliage::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY:
			set_up_mode(zylann::voxel::UP_MODE_SPHERE);
			set_process(true);
			if (get_library().is_null()) { // a script may have assigned one before adding the node
				_rebuild();
			}
			break;
		case NOTIFICATION_EDITOR_PRE_SAVE:
			// Keep the generated library (meshes, shapes) out of the saved scene
			set_library(Ref<VoxelInstanceLibrary>());
			break;
		case NOTIFICATION_EDITOR_POST_SAVE:
			_rebuild();
			break;
		case NOTIFICATION_PROCESS: {
			const uint64_t now = Time::get_singleton()->get_ticks_msec();
			for (int i = retired.size() - 1; i >= 0; i--) {
				if (now >= retired[i].until_ms) {
					retired.remove_at(i);
				}
			}
			if (!Engine::get_singleton()->is_editor_hint() && now >= next_thin_ms) {
				next_thin_ms = now + 250;
				_update_thinned_colliders();
			}
			if (!Engine::get_singleton()->is_editor_hint() || now < next_check_ms) {
				break;
			}
			next_check_ms = now + 500;
			if (_config_signature() != signature) {
				_rebuild();
			}
		} break;
	}
}

void EdenFoliage::_rebuild() {
	if (batching || !is_inside_tree()) {
		return; // setters fire while the scene loads; READY builds once
	}
	signature = _config_signature();
	if (Engine::get_singleton()->is_editor_hint() && !show_in_editor) {
		// Empty, not null: a VoxelInstancer without a library logs an assertion error every frame
		Ref<VoxelInstanceLibrary> empty;
		empty.instantiate();
		set_library(empty);
		return;
	}
	Ref<VoxelInstanceLibrary> old = get_library();
	set_library(build_library());
	if (old.is_valid()) {
		retired.push_back({ old, Time::get_singleton()->get_ticks_msec() + 1000 });
	}
}

void EdenFoliage::_update_materials() {
	// Biome health on every plant material (grass, the shared plant/rock materials, ring materials)
	Vector<Ref<ShaderMaterial>> all = grass_materials;
	all.push_back(EdenFoliageMeshes::get_material(true));
	all.push_back(EdenFoliageMeshes::get_material(false));
	for (const Variant &k : ring_materials.keys()) {
		all.push_back(ring_materials[k]);
	}
	for (const Ref<ShaderMaterial> &m : all) {
		m->set_shader_parameter("u_health_enabled", biome_health_map.is_valid());
		m->set_shader_parameter("u_health_map", biome_health_map);
		m->set_shader_parameter("u_health_cell", biome_health_cell);
	}
	for (const Ref<ShaderMaterial> &m : grass_materials) {
		m->set_shader_parameter("u_fade_start", grass_fade_start);
		m->set_shader_parameter("u_fade_end", grass_fade_end);
		m->set_shader_parameter("u_wind_fade_start", grass_wind_fade_start);
		m->set_shader_parameter("u_wind_fade_end", grass_wind_fade_end);
	}
	Vector<Ref<ShaderMaterial>> trees;
	trees.push_back(EdenFoliageMeshes::get_material(true));
	for (const Variant &k : ring_materials.keys()) {
		trees.push_back(ring_materials[k]);
	}
	for (const Ref<ShaderMaterial> &m : trees) {
		m->set_shader_parameter("u_wind_fade_start", tree_wind_fade_start);
		m->set_shader_parameter("u_wind_fade_end", tree_wind_fade_end);
	}
}

// Library -------------------------------------------------------------------------------------------------------------

Ref<VoxelInstanceLibrary> EdenFoliage::build_library() {
	Ref<VoxelInstanceLibrary> lib;
	lib.instantiate();
	item_kinds.clear();
	grass_materials.clear();
	ring_materials.clear();
	HashSet<int> used;
	const Dictionary sites = spacing_enabled ? _site_ranges() : Dictionary();
	Node3D *terrain = _terrain();
	Ref<EdenFoliageConfig> cfg = _get_config();
	for (int bi = 0; bi < cfg->biomes.size(); bi++) {
		Ref<EdenFoliageBiome> biome = cfg->biomes[bi];
		if (biome.is_null() || !biome->enabled) {
			continue;
		}
		for (int li = 0; li < biome->layers.size(); li++) {
			Ref<EdenFoliageLayer> layer = biome->layers[li];
			if (layer.is_null() || !layer->enabled) {
				continue;
			}
			const Vector2 temperature = intersect(biome->temperature, layer->temperature);
			const Vector2 moisture = intersect(biome->moisture, layer->moisture);
			const Vector2 slope = intersect(biome->slope, layer->slope);
			if (temperature.x > temperature.y || moisture.x > moisture.y || slope.x > slope.y) {
				continue; // the layer's limits exclude the whole biome
			}
			const PackedInt32Array materials = material_ids(layer->materials != 0 ? layer->materials : biome->materials);
			const float density = layer->density * biome->density_scale * density_scale * (layer->is_grass() ? grass_density_scale : 1.0f);
			Vector2 size = Vector2(layer->scale_min, layer->scale_max) * biome->size_scale;
			if (layer->is_tree()) {
				size *= tree_size_scale;
			}
			const int variants = layer->is_grass() ? 1 : layer->variants;
			const String key = biome->name + "/" + layer->name;
			const int base = _alloc_ids(used, key);
			const bool far = far_foliage_enabled && layer->has_far(size.y) && terrain != nullptr;
			for (int v = 0; v < variants; v++) {
				Ref<Resource> g = _gen(density / variants, temperature, moisture, materials, slope);
				g->set("min_scale", size.x);
				g->set("max_scale", size.y);
				g->set("vertical_alignment", layer->vertical_alignment);
				// Sink is in metres at scale 1: bigger instances sink proportionally deeper
				g->set("offset_along_normal", layer->sink * (size.x + size.y) * 0.5f);
				if (layer->placement == L::FOREST) {
					_add_forest_mask(g);
				} else if (layer->clump_size > 0.0f) {
					_add_clumping(g, base, layer->clump_size);
				}
				if (sites.has(key)) {
					_use_sites(g, sites, sites[key], v, variants);
				}
				const String item_name = key + "_" + itos(v);
				Ref<Resource> item;
				if (layer->is_grass()) {
					item = _grass_item(item_name, g, layer);
				} else {
					const Dictionary built = EdenFoliageMeshes::build(layer, v);
					// With far tiers, the near item ends where the first tier starts (its own LOD's range)
					const float dd = (layer->draw_distance > 0.0f || !far) ? layer->draw_distance : _lod_range(layer->lod);
					item = _item(item_name, built["mesh"], g, layer->lod, layer->collision ? Array(built["shape"]) : Array(), dd);
					if (!layer->casts_shadow(size.y)) {
						item->set("cast_shadow", RenderingServer::SHADOW_CASTING_SETTING_OFF);
					}
					const bool dec = deciduous(layer->kind);
					if (far) { // exact per-instance cut where the far tiers take over (chunks only cut coarsely)
						item->set("material_override", _ring_material(layer->sways(), 0.0f, dd, dec));
					} else if (layer->sways() || dec) { // (seasonal plants need u_deciduous: the shared material has it off)
						item->set("material_override", _ring_material(layer->sways(), 0.0f, 1e9f, dec));
					}
					_add_detail_lods(item, layer, v, dd > 0.0f ? dd : _lod_range(layer->lod) * 2.0f);
				}
				lib->call("add_item", base + v, item);
				item_kinds[base + v] = layer->kind;
			}
			if (far) {
				_add_far_tiers(lib, used, biome, layer, density, size, temperature, moisture, materials, slope);
			}
		}
	}
	_update_materials();
	return lib;
}

// Far tiers: the layer again on each coarser terrain LOD, each shown only in its own ring (from the previous LOD's
// range to its own), so big things stay visible far out. Rings grow 4x in area per LOD, so every tier thins out (and
// grows a little to keep coverage). Detail steps down with distance. Positions come from each LOD's own chunks.
void EdenFoliage::_add_far_tiers(const Ref<VoxelInstanceLibrary> &p_lib, HashSet<int> &r_used, const Ref<EdenFoliageBiome> &p_biome,
		const Ref<EdenFoliageLayer> &p_layer, float p_density, Vector2 p_size, Vector2 p_temperature, Vector2 p_moisture,
		const PackedInt32Array &p_materials, Vector2 p_slope) {
	Ref<Mesh> mesh0 = Dictionary(EdenFoliageMeshes::build(p_layer, 0))["mesh"];
	const Vector3 extent = mesh0->get_aabb().size;
	const float metres = MAX(extent.x, MAX(extent.y, extent.z)) * p_size.y;
	const float max_distance = MIN(p_layer->far_distance * far_distance_scale, metres * far_visibility);
	Node3D *terrain = _terrain();
	const int block = terrain ? int(terrain->get("mesh_block_size")) : 16;
	for (int k = p_layer->lod + 1; k <= MAX_INSTANCER_LOD; k++) {
		const float inner = _lod_range(k - 1);
		if (inner >= max_distance) {
			break;
		}
		const float outer = MIN(_lod_range(k), max_distance);
		const int i = k - p_layer->lod - 1;
		const float d = p_density * Math::pow(p_layer->far_density_falloff, float(i));
		const Vector2 s = p_size * Math::pow(p_layer->far_scale_growth, float(i));
		// Detail steps down per ring: a finer voxel copy inside far_detail_distance, the normal one beyond, a coarser
		// one past 4x that. One variant per tier: they're indistinguishable that far out.
		const float detail = p_layer->far_detail_distance;
		int res = inner < detail ? p_layer->far_resolution * 2 : (inner < detail * 4.0f ? p_layer->far_resolution : MAX(p_layer->far_resolution - 2, 2));
		res = MAX(int(Math::round(res * far_resolution_scale)), 2);
		const int base = _alloc_ids(r_used, p_biome->name + "/" + p_layer->name + "/far" + itos(k));
		Ref<Resource> g = _gen(d, p_temperature, p_moisture, p_materials, p_slope);
		g->set("min_scale", s.x);
		g->set("max_scale", s.y);
		g->set("vertical_alignment", p_layer->vertical_alignment);
		g->set("offset_along_normal", p_layer->sink * (s.x + s.y) * 0.5f);
		if (p_layer->placement == L::FOREST) {
			_add_forest_mask(g);
		} else if (p_layer->clump_size > 0.0f) {
			_add_clumping(g, base, p_layer->clump_size);
		}
		Ref<Mesh> mesh = EdenFoliageMeshes::build_far(p_layer, 0, res);
		Ref<Resource> item = new_resource("VoxelInstanceLibraryMultiMeshItem");
		item->set("name", p_biome->name + "/" + p_layer->name + "_far" + itos(k) + "_0");
		item->set("lod_index", k);
		item->set("generator", g);
		item->set("persistent", false);
		item->set("cast_shadow", RenderingServer::SHADOW_CASTING_SETTING_OFF);
		// Mesh LODs by chunk distance, as ratios of this LOD's range: nothing inside the ring (finer tiers draw
		// there), the mesh within it, hidden beyond
		item->set("mesh", EdenFoliageMeshes::empty_mesh());
		item->call("set_mesh", mesh, 1);
		item->set("hide_beyond_max_lod", true);
		// Chunks are measured from their centre, so widen by half a chunk diagonal; the shader cuts exactly
		const float r = _lod_range(k);
		const float margin = block * (1 << k) * 0.87f;
		PackedFloat32Array ratios;
		ratios.push_back(MAX(inner - margin, 0.0f) / r);
		ratios.push_back((outer + margin) / r);
		ratios.push_back(2.0f);
		ratios.push_back(2.0f);
		item->set("_mesh_lod_distance_ratios", ratios);
		item->set("material_override", _ring_material(false, inner, outer, deciduous(p_layer->kind)));
		p_lib->call("add_item", base, item);
	}
}

// Which slice of each spacing grid's owner values every spaced layer gets ("biome/layer" -> [group, start, width]):
// width = its density x the grid's cell area, so its density is kept. Biomes that apply anywhere (no climate limits:
// rocks, cliffs) come first; the regional biomes follow, each from the same point, since their climates mostly exclude
// each other (a biome that would pass 1 is scaled down to fit). "_exclude_big" holds the end of everything the big
// grid can hold, which medium sites keep clear of.
Dictionary EdenFoliage::_site_ranges() {
	Dictionary out;
	Ref<EdenFoliageConfig> cfg = _get_config();
	const float grid[2] = { big_spacing, medium_spacing };
	float cursor[2] = { 0.0f, 0.0f };
	float ends[2] = { 0.0f, 0.0f };
	for (int pass = 0; pass < 2; pass++) {
		const bool pass_global = pass == 0;
		const float base[2] = { cursor[0], cursor[1] };
		for (int bi = 0; bi < cfg->biomes.size(); bi++) {
			Ref<EdenFoliageBiome> b = cfg->biomes[bi];
			if (b.is_null() || !b->enabled) {
				continue;
			}
			const bool is_global = b->temperature == Vector2(0, 1) && b->moisture == Vector2(0, 1);
			if (is_global != pass_global) {
				continue;
			}
			float at[2] = { pass_global ? cursor[0] : base[0], pass_global ? cursor[1] : base[1] };
			struct Entry {
				String name;
				int group;
				float width;
			};
			Vector<Entry> entries;
			for (int li = 0; li < b->layers.size(); li++) {
				Ref<EdenFoliageLayer> l = b->layers[li];
				if (l.is_null() || !l->enabled) {
					continue;
				}
				const int group = spaced_big(l->kind) ? 0 : (spaced_medium(l->kind) ? 1 : -1);
				if (group < 0) {
					continue;
				}
				// (medium sites lose some ground to the big sites' clearance: ask for a little more)
				const float width = l->density * b->density_scale * density_scale * grid[group] * grid[group] * (group == 1 ? 1.3f : 1.0f);
				entries.push_back({ l->name, group, width });
			}
			for (int group = 0; group < 2; group++) {
				float total = 0.0f;
				for (const Entry &e : entries) {
					if (e.group == group) {
						total += e.width;
					}
				}
				const float room = 1.0f - at[group];
				const float fit = total > 0.0f ? MIN(1.0f, room / total) : 1.0f;
				for (const Entry &e : entries) {
					if (e.group == group) {
						out[b->name + "/" + e.name] = eden_array(group == 0 ? "big" : "medium", at[group], e.width * fit);
						at[group] += e.width * fit;
					}
				}
				ends[group] = MAX(ends[group], at[group]);
			}
			if (pass_global) {
				cursor[0] = at[0];
				cursor[1] = at[1];
			}
		}
	}
	out["_exclude_big"] = ends[0];
	return out;
}

void EdenFoliage::_use_sites(const Ref<Resource> &p_gen, const Dictionary &p_sites, const Array &p_entry, int p_variant, int p_variants) {
	const String group = p_entry[0];
	const float width = float(p_entry[2]) / p_variants;
	const float start = float(p_entry[1]) + width * p_variant;
	Node3D *terrain = _terrain();
	Object *gen = terrain ? Object::cast_to<Object>(terrain->get("generator")) : nullptr;
	const Variant radius = gen ? gen->get("planet_radius") : Variant();
	p_gen->set("emit_mode", VoxelInstanceGenerator::EMIT_FROM_SITES);
	p_gen->set("site_spacing", group == "big" ? big_spacing : medium_spacing);
	p_gen->set("site_seed", group == "big" ? SITE_SEED_BIG : SITE_SEED_MEDIUM);
	p_gen->set("site_owner_range", Vector2(start, start + width));
	p_gen->set("site_jitter", 0.35f);
	p_gen->set("site_planet_radius", radius.get_type() == Variant::NIL ? 0.0f : float(radius));
	if (group == "medium" && medium_clearance > 0.0f) {
		p_gen->set("site_exclusion_spacing", big_spacing);
		p_gen->set("site_exclusion_seed", SITE_SEED_BIG);
		p_gen->set("site_exclusion_range", Vector2(0.0f, float(p_sites["_exclude_big"])));
		p_gen->set("site_exclusion_radius", medium_clearance);
	}
}

// Ids seed placement: stable per name, so edits elsewhere don't reshuffle this layer. 8 slots (variants) per name.
int EdenFoliage::_alloc_ids(HashSet<int> &r_used, const String &p_key) {
	int base = int(p_key.hash() & 0x7fffffff) % 8000 * 8;
	while (r_used.has(base)) {
		base = (base + 8) % 64000;
	}
	r_used.insert(base);
	return base;
}

Node3D *EdenFoliage::_terrain() const {
	Node3D *p = Object::cast_to<Node3D>(get_parent());
	return p != nullptr && p->is_class("VoxelLodTerrain") ? p : nullptr;
}

// Distance where terrain LOD `lod` ends (VoxelLodTerrain::get_lod_distances): the instancer measures its mesh LOD ratios
// against it. Legacy octree doubles per LOD; clipbox adds the secondary distance.
float EdenFoliage::_lod_range(int p_lod) const {
	Node3D *terrain = _terrain();
	if (terrain == nullptr) {
		return 128.0f * float(1 << p_lod);
	}
	const float lod_distance = terrain->get("lod_distance");
	if (int(terrain->get("streaming_system")) != 0) {
		return lod_distance + (p_lod > 0 ? float(terrain->get("secondary_lod_distance")) * float(1 << p_lod) : 0.0f);
	}
	return lod_distance * float(1 << p_lod);
}

Ref<Resource> EdenFoliage::_gen(float p_density, Vector2 p_temperature, Vector2 p_moisture, const PackedInt32Array &p_materials, Vector2 p_slope) {
	Ref<Resource> g = new_resource("VoxelInstanceGenerator");
	g->set("density", p_density);
	// Area-based (per m²). FACES_FAST is per triangle, and mesh optimization makes flat ground few big triangles
	g->set("emit_mode", VoxelInstanceGenerator::EMIT_FROM_FACES);
	g->set("random_rotation", true);
	g->set("min_slope_degrees", p_slope.x);
	g->set("max_slope_degrees", p_slope.y);
	g->set("min_slope_falloff_degrees", p_slope.x > 0.0f ? 3.0f : 0.0f);
	g->set("max_slope_falloff_degrees", p_slope.y < 90.0f ? 5.0f : 0.0f);
	// Empty = any material. Cliffs need it: V4 marks rock by altitude, steep faces are rock only in the shader
	g->set("voxel_texture_filter_enabled", !p_materials.is_empty());
	g->set("voxel_texture_filter_array", p_materials);
	g->set("surface_filter_enabled", true);
	g->set("temperature_range", p_temperature);
	g->set("moisture_range", p_moisture);
	return g;
}

// Forest clumps: one noise field (same seed and scale for every forest layer, so a forest's trees, bushes and ferns
// line up) thins instances between patches
Ref<FastNoiseLite> EdenFoliage::_forest_noise() {
	Ref<EdenFoliageConfig> cfg = _get_config();
	if (forest_mask.is_null()) {
		forest_mask.instantiate();
	}
	forest_mask->set_seed(9000 + cfg->forest_seed);
	forest_mask->set_noise_type(FastNoiseLite::TYPE_SIMPLEX_SMOOTH);
	forest_mask->set_frequency(1.0f / MAX(cfg->forest_patch_size, 1.0f));
	return forest_mask;
}

float EdenFoliage::_forest_threshold() {
	return (_get_config()->forest_coverage - 0.5f) * 0.8f;
}

void EdenFoliage::_add_forest_mask(const Ref<Resource> &p_gen) {
	p_gen->set("noise", _forest_noise());
	p_gen->set("noise_dimension", VoxelInstanceGenerator::DIMENSION_3D);
	p_gen->set("noise_threshold", _forest_threshold());
	p_gen->set("noise_falloff", _get_config()->forest_edge);
}

float EdenFoliage::get_forest_density(const Vector3 &p_world_position) {
	Node3D *terrain = _terrain();
	if (terrain == nullptr) {
		return 0.0f;
	}
	if (forest_mask.is_null()) {
		_forest_noise();
	}
	const Vector3 local = p_world_position - (terrain->is_inside_tree() ? terrain->get_global_position() : terrain->get_position());
	const float n = forest_mask->get_noise_3dv(local) + _forest_threshold();
	float keep = CLAMP(n / MAX(_get_config()->forest_edge, 1e-3f), 0.0f, 1.0f);
	keep *= keep;
	Object *gen = Object::cast_to<Object>(terrain->get("generator"));
	if (keep <= 0.0f || gen == nullptr || !gen->has_method("sample_surface")) {
		return 0.0f;
	}
	const Dictionary s = gen->call("sample_surface", local.normalized());
	if (float(s.get("height", 0.0f)) <= 0.0f || !_get_config()->forest_climate(s.get("temperature", 0.0f), s.get("moisture", 0.0f))) {
		return 0.0f;
	}
	return keep;
}

void EdenFoliage::_add_clumping(const Ref<Resource> &p_gen, int p_seed, float p_patch_size) {
	Ref<FastNoiseLite> noise;
	noise.instantiate();
	noise->set_seed(4000 + p_seed); // shared by a layer's variants
	noise->set_noise_type(FastNoiseLite::TYPE_SIMPLEX_SMOOTH);
	noise->set_frequency(1.0f / MAX(p_patch_size, 1.0f));
	p_gen->set("noise", noise);
	p_gen->set("noise_dimension", VoxelInstanceGenerator::DIMENSION_3D);
	p_gen->set("noise_threshold", -0.1f);
	p_gen->set("noise_falloff", 0.4f);
}

Ref<Resource> EdenFoliage::_item(const String &p_name, const Ref<Mesh> &p_mesh, const Ref<Resource> &p_gen, int p_lod, const Array &p_shapes, float p_draw_distance) {
	Ref<Resource> item = new_resource("VoxelInstanceLibraryMultiMeshItem");
	item->set("name", p_name);
	item->set("lod_index", p_lod);
	item->set("generator", p_gen);
	item->set("persistent", false);
	item->set("mesh", p_mesh);
	if (!p_shapes.is_empty() && !Engine::get_singleton()->is_editor_hint()) { // no physics in the editor: bodies only cost time
		item->set("collision_shapes", p_shapes);
		item->set("collision_distance", collision_distance);
		item->set("collision_layer", EDEN_FOLIAGE_COLLISION_LAYER);
	}
	if (p_draw_distance > 0.0f) {
		_limit_draw_distance(item, p_draw_distance);
	}
	return item;
}

// Mesh LODs by chunk distance for heavy meshes (trees, big bushes): full within detail_distance, simplified to
// detail_distance x mid_detail_scale, voxelized beyond, hidden past `end` (the item's draw distance)
void EdenFoliage::_add_detail_lods(const Ref<Resource> &p_item, const Ref<EdenFoliageLayer> &p_layer, int p_variant, float p_end) {
	Node3D *terrain = _terrain();
	const Ref<Mesh> mesh = p_item->get("mesh");
	if (terrain == nullptr || detail_distance <= 0.0f || detail_distance >= p_end || mesh.is_null() || tris(mesh) <= 400) {
		return;
	}
	// Half a chunk diagonal of slack: chunks are measured from their centre
	const int lod = p_item->get("lod_index");
	const float margin = int(terrain->get("mesh_block_size")) * (1 << lod) * 0.45f;
	const float r = _lod_range(lod);
	const float mid = detail_distance * mid_detail_scale;
	// (half the triangles: at a fifth, crowns visibly lost chunks as a whole chunk of them switched at once)
	p_item->call("set_mesh", EdenFoliageMeshes::build_simplified(p_layer, p_variant, 0.5f), 1);
	PackedFloat32Array ratios;
	ratios.push_back((detail_distance + margin) / r);
	if (mid < p_end) {
		p_item->call("set_mesh", EdenFoliageMeshes::build_far(p_layer, p_variant, MAX(int(Math::round(8 * far_resolution_scale)), 2)), 2);
		ratios.push_back((mid + margin) / r);
	}
	ratios.push_back(MIN((p_end + margin) / r, 2.0f));
	while (ratios.size() < 4) {
		ratios.push_back(2.0f);
	}
	p_item->set("hide_beyond_max_lod", true);
	p_item->set("_mesh_lod_distance_ratios", ratios);
}

// Stops drawing whole chunks whose centre is beyond `meters` (plus half a chunk diagonal, so chunks straddling the
// limit still draw). The instancer measures it as a ratio of the terrain's distance for the item's LOD.
void EdenFoliage::_limit_draw_distance(const Ref<Resource> &p_item, float p_meters) {
	Node3D *terrain = _terrain();
	if (terrain == nullptr) {
		return;
	}
	const int lod = p_item->get("lod_index");
	const float margin = int(terrain->get("mesh_block_size")) * (1 << lod) * 0.87f;
	p_item->set("hide_beyond_max_lod", true);
	// Whole-array setter: the per-LOD one clamps LOD0 to the LOD1 default (0.35)
	PackedFloat32Array ratios;
	ratios.push_back(MIN((p_meters + margin) / _lod_range(lod), 2.0f));
	ratios.push_back(2.0f);
	ratios.push_back(2.0f);
	ratios.push_back(2.0f);
	p_item->set("_mesh_lod_distance_ratios", ratios);
}

// The plant shader drawing only instances between camera distances inner..outer; without sway for rocks and wood
Ref<ShaderMaterial> EdenFoliage::_ring_material(bool p_sways, float p_inner, float p_outer, bool p_deciduous) {
	const Array key = eden_array(p_sways, p_inner, p_outer, p_deciduous);
	if (!ring_materials.has(key)) {
		Ref<ShaderMaterial> m;
		m.instantiate();
		m->set_shader(EdenFoliageMeshes::get_tree_shader());
		if (!p_sways) {
			m->set_shader_parameter("u_wind_strength", 0.0f);
			m->set_shader_parameter("u_living", false); // rocks and wood
		}
		m->set_shader_parameter("u_ring", Vector2(p_inner, p_outer));
		m->set_shader_parameter("u_deciduous", p_deciduous);
		ring_materials[key] = m;
	}
	return ring_materials[key];
}

Ref<Resource> EdenFoliage::_grass_item(const String &p_name, const Ref<Resource> &p_gen, const Ref<EdenFoliageLayer> &p_layer) {
	Ref<ShaderMaterial> mat;
	mat.instantiate();
	mat->set_shader(EdenFoliageMeshes::get_grass_shader());
	const Color b = p_layer->grass_base_color;
	const Color t = p_layer->grass_tip_color;
	mat->set_shader_parameter("u_base_color", Vector3(b.r, b.g, b.b));
	mat->set_shader_parameter("u_tip_color", Vector3(t.r, t.g, t.b));
	grass_materials.push_back(mat);
	// Chunks past the fade stop drawing (the shader already shrank their tufts to nothing)
	Ref<Resource> item = _item(p_name, EdenFoliageMeshes::tuft(blades_per_tuft, mat), p_gen, 0, Array(), grass_fade_end);
	item->set("cast_shadow", RenderingServer::SHADOW_CASTING_SETTING_OFF);
	// Mesh LOD per chunk: the full tuft near the camera, a lighter one further out (grass is vertex-bound: the far
	// chunks are most of the blades on screen)
	Node3D *terrain = _terrain();
	if (terrain != nullptr && grass_lod_distance > 0.0f && grass_lod_distance < grass_fade_end && far_blades_per_tuft < blades_per_tuft) {
		const float margin = int(terrain->get("mesh_block_size")) * 0.87f;
		const float r = _lod_range(0);
		item->call("set_mesh", EdenFoliageMeshes::tuft(far_blades_per_tuft, mat, blades_per_tuft), 1);
		PackedFloat32Array ratios;
		ratios.push_back((grass_lod_distance + margin) / r);
		ratios.push_back(MIN((grass_fade_end + margin) / r, 2.0f));
		ratios.push_back(2.0f);
		ratios.push_back(2.0f);
		item->set("_mesh_lod_distance_ratios", ratios);
	}
	return item;
}

void EdenFoliage::_bind_methods() {
	ClassDB::bind_integer_constant(get_class_static(), StringName(), "COLLISION_LAYER", EDEN_FOLIAGE_COLLISION_LAYER);
	ClassDB::bind_method(D_METHOD("set_biome_health_image", "image"), &EdenFoliage::set_biome_health_image);
	ClassDB::bind_method(D_METHOD("get_biome_health_image"), &EdenFoliage::get_biome_health_image);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "biome_health_image", PROPERTY_HINT_RESOURCE_TYPE, "Image", PROPERTY_USAGE_NONE), "set_biome_health_image", "get_biome_health_image");
	ClassDB::bind_method(D_METHOD("health_at", "direction"), &EdenFoliage::health_at);
	ClassDB::bind_method(D_METHOD("is_plant_kept", "world_position", "planet_centre"), &EdenFoliage::is_plant_kept);
	ClassDB::bind_method(D_METHOD("set_config", "config"), &EdenFoliage::set_config);
	ClassDB::bind_method(D_METHOD("get_config"), &EdenFoliage::get_config);
	ClassDB::bind_method(D_METHOD("apply_quality", "values"), &EdenFoliage::apply_quality);
	ClassDB::bind_method(D_METHOD("build_library"), &EdenFoliage::build_library);
	ClassDB::bind_method(D_METHOD("get_forest_density", "world_position"), &EdenFoliage::get_forest_density);
	ClassDB::bind_method(D_METHOD("get_item_kinds"), &EdenFoliage::get_item_kinds);

	ClassDB::bind_method(D_METHOD("set_biome_health_map", "map"), &EdenFoliage::set_biome_health_map);
	ClassDB::bind_method(D_METHOD("get_biome_health_map"), &EdenFoliage::get_biome_health_map);
	ClassDB::bind_method(D_METHOD("set_biome_health_cell", "radians"), &EdenFoliage::set_biome_health_cell);
	ClassDB::bind_method(D_METHOD("get_biome_health_cell"), &EdenFoliage::get_biome_health_cell);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "config", PROPERTY_HINT_RESOURCE_TYPE, "EdenFoliageConfig"), "set_config", "get_config");
	ADD_GROUP("Biome Health", "biome_health_");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "biome_health_map", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D", PROPERTY_USAGE_EDITOR), "set_biome_health_map", "get_biome_health_map");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_health_cell", PROPERTY_HINT_RANGE, "0.001,0.5,0.001"), "set_biome_health_cell", "get_biome_health_cell");
	ADD_GROUP("", "");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "item_kinds", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_item_kinds");
	String group;
#define EDEN_F_BIND(m_type, m_name, m_default, m_vtype, m_hint, m_hint_string, m_group, m_action)          \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &EdenFoliage::set_##m_name);                     \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &EdenFoliage::get_##m_name);                              \
	if (group != m_group) {                                                                                  \
		group = m_group;                                                                                     \
		ADD_GROUP(m_group, "");                                                                              \
	}                                                                                                        \
	ADD_PROPERTY(PropertyInfo(Variant::m_vtype, #m_name, m_hint, m_hint_string), "set_" #m_name, "get_" #m_name);
	EDEN_FOLIAGE_PROPERTIES(EDEN_F_BIND)
#undef EDEN_F_BIND
}
