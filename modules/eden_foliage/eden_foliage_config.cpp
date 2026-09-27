#include "eden_foliage_config.h"

#include "core/object/class_db.h"

// EdenFoliageLayer ----------------------------------------------------------------------------------------------------

#define EDEN_FL_SET(m_type, m_name, m_default, m_vtype, m_hint, m_hint_string, m_group) \
	void EdenFoliageLayer::set_##m_name(const m_type &p_value) {                       \
		if (m_name == p_value) {                                                        \
			return;                                                                     \
		}                                                                               \
		m_name = p_value;                                                               \
		emit_changed();                                                                 \
	}
EDEN_FOLIAGE_LAYER_PROPERTIES(EDEN_FL_SET)
#undef EDEN_FL_SET

void EdenFoliageLayer::set_layer_name(const String &p_value) {
	name = p_value;
	emit_changed();
}

String EdenFoliageLayer::kind_name(int p_kind) {
	static const char *names[KIND_MAX] = { "GRASS", "DRY_GRASS", "OAK", "PINE", "BIRCH", "WILLOW", "PALM", "DEAD_TREE", "FRUIT_TREE",
		"SHRUB", "FERN", "DEAD_BUSH", "TALL_GRASS", "CACTUS", "BERRY_BUSH", "PEBBLES", "BOULDER", "ROCK_SLAB", "ROCK_SPIRE", "BRANCH", "LOG" };
	return p_kind >= 0 && p_kind < KIND_MAX ? String(names[p_kind]) : String();
}

bool EdenFoliageLayer::has_far(float p_max_size) const {
	if (far_mode == MODE_ON) {
		return !is_grass();
	}
	if (far_mode == MODE_OFF) {
		return false;
	}
	return is_tree() || (is_rock() && kind != PEBBLES && p_max_size >= 1.5f);
}

bool EdenFoliageLayer::casts_shadow(float p_max_size) const {
	if (shadow_mode == MODE_ON) {
		return true;
	}
	if (shadow_mode == MODE_OFF) {
		return false;
	}
	return is_tree() || (is_rock() && kind != PEBBLES && p_max_size >= 1.5f);
}

void EdenFoliageLayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_layer_name", "value"), &EdenFoliageLayer::set_layer_name);
	ClassDB::bind_method(D_METHOD("get_layer_name"), &EdenFoliageLayer::get_layer_name);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "name"), "set_layer_name", "get_layer_name");
	ClassDB::bind_static_method("EdenFoliageLayer", D_METHOD("kind_name", "kind"), &EdenFoliageLayer::kind_name);
	String group;
#define EDEN_FL_BIND(m_type, m_name, m_default, m_vtype, m_hint, m_hint_string, m_group)                                       \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &EdenFoliageLayer::set_##m_name);                                   \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &EdenFoliageLayer::get_##m_name);                                            \
	if (group != m_group) {                                                                                                     \
		group = m_group;                                                                                                        \
		ADD_GROUP(m_group, "");                                                                                                 \
	}                                                                                                                           \
	ADD_PROPERTY(PropertyInfo(Variant::m_vtype, #m_name, m_hint, m_hint_string), "set_" #m_name, "get_" #m_name);
	EDEN_FOLIAGE_LAYER_PROPERTIES(EDEN_FL_BIND)
#undef EDEN_FL_BIND

	ClassDB::bind_method(D_METHOD("is_tree"), &EdenFoliageLayer::is_tree);
	ClassDB::bind_method(D_METHOD("is_grass"), &EdenFoliageLayer::is_grass);
	ClassDB::bind_method(D_METHOD("is_rock"), &EdenFoliageLayer::is_rock);
	ClassDB::bind_method(D_METHOD("has_far", "max_size"), &EdenFoliageLayer::has_far);
	ClassDB::bind_method(D_METHOD("casts_shadow", "max_size"), &EdenFoliageLayer::casts_shadow);
	ClassDB::bind_method(D_METHOD("sways"), &EdenFoliageLayer::sways);

	BIND_ENUM_CONSTANT(GRASS);
	BIND_ENUM_CONSTANT(DRY_GRASS);
	BIND_ENUM_CONSTANT(OAK);
	BIND_ENUM_CONSTANT(PINE);
	BIND_ENUM_CONSTANT(BIRCH);
	BIND_ENUM_CONSTANT(WILLOW);
	BIND_ENUM_CONSTANT(PALM);
	BIND_ENUM_CONSTANT(DEAD_TREE);
	BIND_ENUM_CONSTANT(FRUIT_TREE);
	BIND_ENUM_CONSTANT(SHRUB);
	BIND_ENUM_CONSTANT(FERN);
	BIND_ENUM_CONSTANT(DEAD_BUSH);
	BIND_ENUM_CONSTANT(TALL_GRASS);
	BIND_ENUM_CONSTANT(CACTUS);
	BIND_ENUM_CONSTANT(BERRY_BUSH);
	BIND_ENUM_CONSTANT(PEBBLES);
	BIND_ENUM_CONSTANT(BOULDER);
	BIND_ENUM_CONSTANT(ROCK_SLAB);
	BIND_ENUM_CONSTANT(ROCK_SPIRE);
	BIND_ENUM_CONSTANT(BRANCH);
	BIND_ENUM_CONSTANT(LOG);
	BIND_ENUM_CONSTANT(ANYWHERE);
	BIND_ENUM_CONSTANT(FOREST);
	BIND_ENUM_CONSTANT(MODE_AUTO);
	BIND_ENUM_CONSTANT(MODE_ON);
	BIND_ENUM_CONSTANT(MODE_OFF);
}

// EdenFoliageBiome ----------------------------------------------------------------------------------------------------

#define EDEN_FB_SET(m_type, m_name)                         \
	void EdenFoliageBiome::set_##m_name(m_type p_value) { \
		m_name = p_value;                                  \
		emit_changed();                                    \
	}
void EdenFoliageBiome::set_biome_name(const String &p_value) {
	name = p_value;
	emit_changed();
}
EDEN_FB_SET(bool, enabled)
EDEN_FB_SET(const Vector2 &, temperature)
EDEN_FB_SET(const Vector2 &, moisture)
EDEN_FB_SET(const Vector2 &, slope)
EDEN_FB_SET(int, materials)
EDEN_FB_SET(float, density_scale)
EDEN_FB_SET(float, size_scale)
EDEN_FB_SET(const TypedArray<EdenFoliageLayer> &, layers)
#undef EDEN_FB_SET

void EdenFoliageBiome::_bind_methods() {
#define EDEN_FB_BIND(m_name, m_info)                                                              \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &EdenFoliageBiome::set_##m_name);     \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &EdenFoliageBiome::get_##m_name);              \
	ADD_PROPERTY(m_info, "set_" #m_name, "get_" #m_name);
	ClassDB::bind_method(D_METHOD("set_biome_name", "value"), &EdenFoliageBiome::set_biome_name);
	ClassDB::bind_method(D_METHOD("get_biome_name"), &EdenFoliageBiome::get_biome_name);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "name"), "set_biome_name", "get_biome_name");
	EDEN_FB_BIND(enabled, PropertyInfo(Variant::BOOL, "enabled"));
	EDEN_FB_BIND(temperature, PropertyInfo(Variant::VECTOR2, "temperature"));
	EDEN_FB_BIND(moisture, PropertyInfo(Variant::VECTOR2, "moisture"));
	EDEN_FB_BIND(slope, PropertyInfo(Variant::VECTOR2, "slope"));
	EDEN_FB_BIND(materials, PropertyInfo(Variant::INT, "materials", PROPERTY_HINT_FLAGS, EDEN_FOLIAGE_MATERIAL_FLAGS));
	EDEN_FB_BIND(density_scale, PropertyInfo(Variant::FLOAT, "density_scale", PROPERTY_HINT_RANGE, "0.0,10.0,0.01,or_greater"));
	EDEN_FB_BIND(size_scale, PropertyInfo(Variant::FLOAT, "size_scale", PROPERTY_HINT_RANGE, "0.01,10.0,0.01,or_greater"));
	EDEN_FB_BIND(layers, PropertyInfo(Variant::ARRAY, "layers", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("EdenFoliageLayer")));
#undef EDEN_FB_BIND
}

// EdenFoliageConfig ---------------------------------------------------------------------------------------------------

void EdenFoliageConfig::set_biomes(const TypedArray<EdenFoliageBiome> &p_value) {
	biomes = p_value;
	emit_changed();
}
void EdenFoliageConfig::set_forest_patch_size(float p_value) {
	forest_patch_size = p_value;
	emit_changed();
}
void EdenFoliageConfig::set_forest_coverage(float p_value) {
	forest_coverage = p_value;
	emit_changed();
}
void EdenFoliageConfig::set_forest_edge(float p_value) {
	forest_edge = p_value;
	emit_changed();
}
void EdenFoliageConfig::set_forest_seed(int p_value) {
	forest_seed = p_value;
	emit_changed();
}

bool EdenFoliageConfig::forest_climate(float p_temperature, float p_moisture) const {
	for (int i = 0; i < biomes.size(); i++) {
		Ref<EdenFoliageBiome> b = biomes[i];
		if (b.is_null() || !b->enabled || p_temperature < b->temperature.x || p_temperature > b->temperature.y ||
				p_moisture < b->moisture.x || p_moisture > b->moisture.y) {
			continue;
		}
		for (int j = 0; j < b->layers.size(); j++) {
			Ref<EdenFoliageLayer> l = b->layers[j];
			if (l.is_valid() && l->enabled && l->placement == EdenFoliageLayer::FOREST) {
				return true;
			}
		}
	}
	return false;
}

namespace {

using K = EdenFoliageLayer;
const int GRASS = EDEN_FOLIAGE_GRASS, ROCK = EDEN_FOLIAGE_ROCK, SNOW = EDEN_FOLIAGE_SNOW, SAND = EDEN_FOLIAGE_SAND,
		  DIRT = EDEN_FOLIAGE_DIRT, MOSS = EDEN_FOLIAGE_MOSS;
const int VEG = GRASS | DIRT | MOSS;

// A layer with the per-kind defaults of the GDScript original, then `p_props` on top
Ref<EdenFoliageLayer> layer(const String &p_name, int p_kind, float p_density, Vector2 p_scale, const Dictionary &p_props = Dictionary()) {
	Ref<EdenFoliageLayer> l;
	l.instantiate();
	l->name = p_name;
	l->kind = p_kind;
	l->density = p_density;
	l->scale_min = p_scale.x;
	l->scale_max = p_scale.y;
	if (l->is_tree()) {
		l->lod = 1;
		l->variants = 3;
		l->collision = true;
		l->clump_size = 150.0f;
		l->slope = Vector2(0, 30);
	} else if (l->is_grass()) {
		l->variants = 1;
		l->slope = Vector2(0, 35);
		l->vertical_alignment = 0.6f;
	}
	for (const Variant &k : p_props.keys()) {
		l->set(k, p_props[k]);
	}
	return l;
}

Ref<EdenFoliageBiome> biome(const String &p_name, Vector2 p_temperature, Vector2 p_moisture, const TypedArray<EdenFoliageLayer> &p_layers,
		const Dictionary &p_props = Dictionary()) {
	Ref<EdenFoliageBiome> b;
	b.instantiate();
	b->name = p_name;
	b->temperature = p_temperature;
	b->moisture = p_moisture;
	b->layers = p_layers;
	for (const Variant &k : p_props.keys()) {
		b->set(k, p_props[k]);
	}
	return b;
}

Dictionary merged(Dictionary p_a, const Dictionary &p_b) {
	p_a = p_a.duplicate();
	for (const Variant &k : p_b.keys()) {
		p_a[k] = p_b[k];
	}
	return p_a;
}

Dictionary props(std::initializer_list<std::pair<const char *, Variant>> p_list) {
	Dictionary d;
	for (const auto &e : p_list) {
		d[e.first] = e.second;
	}
	return d;
}

} // namespace

TypedArray<EdenFoliageLayer> EdenFoliageConfig::forest_layers(const String &p_biome_name) {
	const Dictionary f = props({ { "placement", EdenFoliageLayer::FOREST } });
	// Forest trees: full meshes only on LOD 0 chunks (~130 m), voxelized far copies beyond, thinning faster than
	// meadow trees -- a forest is ~5x the trees of open country, and at full detail to 256 m it cost 40 ms
	const Dictionary tree = merged(f, props({ { "lod", 0 }, { "far_detail_distance", 256.0 }, { "far_density_falloff", 0.28 } }));
	const Dictionary under = merged(f, props({ { "variants", 1 } }));
	TypedArray<EdenFoliageLayer> out;
	if (p_biome_name == "Temperate") {
		out.push_back(layer("Forest oak", K::OAK, 0.012f, Vector2(2.2, 3.4), merged(tree, props({ { "materials", VEG }, { "temperature", Vector2(0.42, 1.0) } }))));
		out.push_back(layer("Forest birch", K::BIRCH, 0.011f, Vector2(2.0, 3.2), merged(tree, props({ { "materials", VEG }, { "temperature", Vector2(0.0, 0.6) } }))));
		out.push_back(layer("Forest willow", K::WILLOW, 0.005f, Vector2(2.0, 2.8), merged(tree, props({ { "materials", VEG }, { "moisture", Vector2(0.7, 1.0) } }))));
		out.push_back(layer("Undergrowth", K::SHRUB, 0.04f, Vector2(0.7, 1.5), merged(under, props({ { "materials", VEG }, { "draw_distance", 70.0 }, { "variants", 2 } }))));
		out.push_back(layer("Forest ferns", K::FERN, 0.05f, Vector2(0.7, 1.4), merged(under, props({ { "materials", GRASS | MOSS | DIRT }, { "draw_distance", 45.0 } }))));
		out.push_back(layer("Forest berries", K::BERRY_BUSH, 0.012f, Vector2(0.7, 1.2), merged(under, props({ { "materials", VEG }, { "draw_distance", 60.0 } }))));
	} else if (p_biome_name == "Boreal") {
		out.push_back(layer("Forest pine", K::PINE, 0.022f, Vector2(2.2, 3.8), merged(tree, props({ { "materials", VEG | SNOW } }))));
		out.push_back(layer("Undergrowth", K::SHRUB, 0.025f, Vector2(0.6, 1.2), merged(under, props({ { "materials", VEG }, { "draw_distance", 70.0 } }))));
		out.push_back(layer("Forest ferns", K::FERN, 0.03f, Vector2(0.6, 1.2), merged(under, props({ { "materials", GRASS | MOSS | DIRT }, { "moisture", Vector2(0.45, 1.0) }, { "draw_distance", 45.0 } }))));
	} else if (p_biome_name == "Tropical") {
		out.push_back(layer("Jungle palm", K::PALM, 0.012f, Vector2(2.0, 3.2), merged(tree, props({ { "materials", VEG | SAND } }))));
		out.push_back(layer("Jungle fruit tree", K::FRUIT_TREE, 0.01f, Vector2(1.8, 2.8), merged(tree, props({ { "materials", VEG } }))));
		out.push_back(layer("Jungle undergrowth", K::SHRUB, 0.045f, Vector2(0.8, 1.6), merged(under, props({ { "materials", VEG }, { "draw_distance", 70.0 }, { "variants", 2 } }))));
		out.push_back(layer("Jungle ferns", K::FERN, 0.06f, Vector2(0.8, 1.6), merged(under, props({ { "materials", GRASS | MOSS | DIRT }, { "draw_distance", 45.0 } }))));
	}
	return out;
}

Ref<EdenFoliageConfig> EdenFoliageConfig::make_default() {
	Ref<EdenFoliageConfig> c;
	c.instantiate();
	const Dictionary small_rocks = props({ { "materials", VEG | SAND | ROCK }, { "slope", Vector2(0, 40) }, { "sink", -0.15 }, { "moss", 0.25 },
			{ "lichen", 0.05 }, { "draw_distance", 90.0 } });
	auto layers = [](std::initializer_list<Ref<EdenFoliageLayer>> p_list) {
		TypedArray<EdenFoliageLayer> a;
		for (const Ref<EdenFoliageLayer> &l : p_list) {
			a.push_back(l);
		}
		return a;
	};
	TypedArray<EdenFoliageBiome> b;
	b.push_back(biome("Temperate", Vector2(0.35, 0.8), Vector2(0.35, 1.0), layers({
			layer("Grass", K::GRASS, 8.0f, Vector2(0.7, 1.4), props({ { "materials", GRASS } })),
			layer("Oak", K::OAK, 0.0035f, Vector2(2.2, 3.2), props({ { "materials", VEG }, { "temperature", Vector2(0.42, 1.0) } })),
			layer("Birch", K::BIRCH, 0.003f, Vector2(2.0, 3.0), props({ { "materials", VEG }, { "temperature", Vector2(0.0, 0.6) } })),
			layer("Willow", K::WILLOW, 0.002f, Vector2(2.0, 2.8), props({ { "materials", VEG }, { "moisture", Vector2(0.72, 1.0) } })),
			layer("Fruit tree", K::FRUIT_TREE, 0.0015f, Vector2(1.6, 2.4), props({ { "materials", VEG }, { "temperature", Vector2(0.55, 1.0) } })),
			layer("Shrub", K::SHRUB, 0.02f, Vector2(0.7, 1.3), props({ { "materials", VEG }, { "draw_distance", 110.0 } })),
			layer("Fern", K::FERN, 0.03f, Vector2(0.7, 1.3), props({ { "materials", GRASS | MOSS }, { "moisture", Vector2(0.6, 1.0) }, { "variants", 1 }, { "draw_distance", 70.0 } })),
			layer("Berry bush", K::BERRY_BUSH, 0.006f, Vector2(0.7, 1.2), props({ { "materials", VEG }, { "temperature", Vector2(0.0, 0.7) }, { "variants", 1 }, { "draw_distance", 90.0 } })),
			layer("Branches", K::BRANCH, 0.012f, Vector2(0.8, 1.2), props({ { "materials", VEG }, { "vertical_alignment", 0.2 }, { "draw_distance", 60.0 } })),
			layer("Logs", K::LOG, 0.002f, Vector2(0.8, 1.2), props({ { "materials", VEG }, { "vertical_alignment", 0.2 }, { "slope", Vector2(0, 25) }, { "collision", true }, { "draw_distance", 150.0 } })),
	})));
	b.push_back(biome("Boreal", Vector2(0.06, 0.4), Vector2(0.25, 1.0), layers({
			layer("Grass", K::GRASS, 5.0f, Vector2(0.6, 1.1), props({ { "materials", GRASS }, { "grass_base_color", Color(0.17, 0.3, 0.14) }, { "grass_tip_color", Color(0.36, 0.48, 0.25) } })),
			layer("Pine", K::PINE, 0.007f, Vector2(2.2, 3.4), props({ { "materials", VEG | SNOW } })),
			layer("Birch", K::BIRCH, 0.0015f, Vector2(2.0, 3.0), props({ { "materials", VEG }, { "temperature", Vector2(0.25, 1.0) } })),
			layer("Shrub", K::SHRUB, 0.012f, Vector2(0.6, 1.1), props({ { "materials", VEG }, { "draw_distance", 110.0 } })),
			layer("Branches", K::BRANCH, 0.012f, Vector2(0.8, 1.2), props({ { "materials", VEG }, { "vertical_alignment", 0.2 }, { "draw_distance", 60.0 } })),
			layer("Logs", K::LOG, 0.003f, Vector2(0.8, 1.2), props({ { "materials", VEG }, { "vertical_alignment", 0.2 }, { "slope", Vector2(0, 25) }, { "collision", true }, { "draw_distance", 150.0 } })),
	})));
	b.push_back(biome("Tropical", Vector2(0.72, 1.0), Vector2(0.45, 1.0), layers({
			layer("Grass", K::GRASS, 8.0f, Vector2(0.8, 1.5), props({ { "materials", GRASS } })),
			layer("Palm", K::PALM, 0.004f, Vector2(1.8, 2.8), props({ { "materials", VEG | SAND } })),
			layer("Fruit tree", K::FRUIT_TREE, 0.002f, Vector2(1.6, 2.4), props({ { "materials", VEG } })),
			layer("Fern", K::FERN, 0.04f, Vector2(0.8, 1.5), props({ { "materials", GRASS | MOSS }, { "variants", 1 }, { "draw_distance", 70.0 } })),
			layer("Shrub", K::SHRUB, 0.02f, Vector2(0.8, 1.4), props({ { "materials", VEG }, { "draw_distance", 110.0 } })),
	})));
	b.push_back(biome("Dry", Vector2(0.45, 1.0), Vector2(0.0, 0.38), layers({
			// Deserts and steppe: sparse clumps of dry grass, none at all in the driest land (drawn as sand there; its
			// material channel still says grass, so the moisture limit does the work)
			layer("Dry grass", K::DRY_GRASS, 0.3f, Vector2(0.6, 1.2), props({ { "materials", GRASS }, { "moisture", Vector2(0.26, 1.0) }, { "clump_size", 14.0 }, { "grass_base_color", Color(0.36, 0.33, 0.14) }, { "grass_tip_color", Color(0.66, 0.58, 0.30) } })),
			layer("Dead tree", K::DEAD_TREE, 0.0004f, Vector2(1.6, 2.6), props({ { "materials", VEG | SAND }, { "clump_size", 0.0 } })),
			layer("Dead bush", K::DEAD_BUSH, 0.01f, Vector2(0.7, 1.3), props({ { "materials", VEG | SAND }, { "draw_distance", 110.0 } })),
			layer("Cactus", K::CACTUS, 0.004f, Vector2(0.7, 1.4), props({ { "materials", SAND | DIRT | GRASS }, { "temperature", Vector2(0.7, 1.0) }, { "variants", 1 }, { "draw_distance", 150.0 } })),
	})));
	b.push_back(biome("Tundra", Vector2(0.0, 0.1), Vector2(0.0, 1.0), layers({
			layer("Dead tree", K::DEAD_TREE, 0.0005f, Vector2(1.4, 2.2), props({ { "materials", VEG | SNOW }, { "clump_size", 0.0 } })),
			layer("Pine", K::PINE, 0.0008f, Vector2(1.2, 2.0), props({ { "materials", VEG | SNOW } })),
			layer("Stones", K::BOULDER, 0.004f, Vector2(0.4, 1.2), merged(small_rocks, props({ { "moss", 0.0 }, { "lichen", 0.15 }, { "variants", 3 } }))),
	})));
	// Regions, any climate
	b.push_back(biome("Grassland Rocks", Vector2(0.0, 1.0), Vector2(0.0, 1.0), layers({
			layer("Pebbles", K::PEBBLES, 0.02f, Vector2(0.3, 0.8), props({ { "materials", VEG | SAND | ROCK }, { "slope", Vector2(0, 45) }, { "variants", 1 }, { "draw_distance", 50.0 } })),
			layer("Stones", K::BOULDER, 0.003f, Vector2(0.3, 0.9), merged(small_rocks, props({ { "variants", 3 } }))),
			layer("Boulders", K::BOULDER, 0.0004f, Vector2(1.0, 2.5), merged(small_rocks, props({ { "variants", 3 }, { "lod", 1 }, { "sink", -0.12 }, { "collision", true }, { "draw_distance", 0.0 }, { "facet_detail", 2 }, { "moss", 0.4 } }))),
	}), props({ { "materials", VEG | SAND } })));
	b.push_back(biome("Mountain Rock", Vector2(0.0, 1.0), Vector2(0.0, 1.0), layers({
			layer("Boulder field", K::BOULDER, 0.0015f, Vector2(1.5, 5.0), props({ { "variants", 3 }, { "lod", 1 }, { "sink", -0.15 }, { "collision", true }, { "facet_detail", 1 }, { "lichen", 0.1 } })),
	}), props({ { "materials", ROCK | SNOW }, { "slope", Vector2(0, 35) } })));
	b.push_back(biome("Cliffs", Vector2(0.0, 1.0), Vector2(0.0, 1.0), layers({
			// Big slabs set into the face read as rock strata; a few boulders and spires break the outline
			layer("Cliff slabs", K::ROCK_SLAB, 0.0003f, Vector2(18.0, 55.0), props({ { "variants", 3 }, { "lod", 1 }, { "vertical_alignment", 0.0 }, { "sink", -0.18 }, { "collision", true }, { "facet_detail", 1 }, { "roughness", 0.18 } })),
			layer("Cliff boulders", K::BOULDER, 0.0005f, Vector2(8.0, 25.0), props({ { "variants", 2 }, { "lod", 1 }, { "vertical_alignment", 0.0 }, { "sink", -0.15 }, { "collision", true }, { "facet_detail", 1 }, { "roughness", 0.3 } })),
			layer("Spires", K::ROCK_SPIRE, 0.00004f, Vector2(15.0, 40.0), props({ { "variants", 2 }, { "lod", 1 }, { "vertical_alignment", 0.4 }, { "sink", -0.2 }, { "collision", true }, { "slope", Vector2(30, 80) } })),
	}), props({ { "slope", Vector2(35, 90) } })));
	for (int i = 0; i < b.size(); i++) {
		Ref<EdenFoliageBiome> bi = b[i];
		bi->layers.append_array(forest_layers(bi->name));
	}
	c->biomes = b;
	return c;
}

void EdenFoliageConfig::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_biomes", "value"), &EdenFoliageConfig::set_biomes);
	ClassDB::bind_method(D_METHOD("get_biomes"), &EdenFoliageConfig::get_biomes);
	ClassDB::bind_method(D_METHOD("set_forest_patch_size", "value"), &EdenFoliageConfig::set_forest_patch_size);
	ClassDB::bind_method(D_METHOD("get_forest_patch_size"), &EdenFoliageConfig::get_forest_patch_size);
	ClassDB::bind_method(D_METHOD("set_forest_coverage", "value"), &EdenFoliageConfig::set_forest_coverage);
	ClassDB::bind_method(D_METHOD("get_forest_coverage"), &EdenFoliageConfig::get_forest_coverage);
	ClassDB::bind_method(D_METHOD("set_forest_edge", "value"), &EdenFoliageConfig::set_forest_edge);
	ClassDB::bind_method(D_METHOD("get_forest_edge"), &EdenFoliageConfig::get_forest_edge);
	ClassDB::bind_method(D_METHOD("set_forest_seed", "value"), &EdenFoliageConfig::set_forest_seed);
	ClassDB::bind_method(D_METHOD("get_forest_seed"), &EdenFoliageConfig::get_forest_seed);
	ClassDB::bind_method(D_METHOD("forest_climate", "temperature", "moisture"), &EdenFoliageConfig::forest_climate);
	ClassDB::bind_static_method("EdenFoliageConfig", D_METHOD("make_default"), &EdenFoliageConfig::make_default);
	ClassDB::bind_static_method("EdenFoliageConfig", D_METHOD("forest_layers", "biome_name"), &EdenFoliageConfig::forest_layers);

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "biomes", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("EdenFoliageBiome")), "set_biomes", "get_biomes");
	ADD_GROUP("Forests", "forest_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "forest_patch_size", PROPERTY_HINT_RANGE, "50.0,20000.0,1.0"), "set_forest_patch_size", "get_forest_patch_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "forest_coverage", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_forest_coverage", "get_forest_coverage");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "forest_edge", PROPERTY_HINT_RANGE, "0.01,1.0,0.01"), "set_forest_edge", "get_forest_edge");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "forest_seed"), "set_forest_seed", "get_forest_seed");
}
