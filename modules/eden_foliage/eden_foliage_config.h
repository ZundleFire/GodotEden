#ifndef EDEN_FOLIAGE_CONFIG_H
#define EDEN_FOLIAGE_CONFIG_H

#include "core/io/resource.h"
#include "core/variant/typed_array.h"

// EdenFoliage's data: a config of biomes (regions of the planet), each with layers (what grows there). Resources
// so they can be saved as .tres, shared between scenes and edited in the inspector; EdenFoliage rebuilds on change.

// Ground material flags (MIXEL4 ids 0..5 of the planet generators): used by EdenFoliageLayer/Biome.materials
enum EdenFoliageMaterial {
	EDEN_FOLIAGE_GRASS = 1,
	EDEN_FOLIAGE_ROCK = 2,
	EDEN_FOLIAGE_SNOW = 4,
	EDEN_FOLIAGE_SAND = 8,
	EDEN_FOLIAGE_DIRT = 16,
	EDEN_FOLIAGE_MOSS = 32,
};

#define EDEN_FOLIAGE_MATERIAL_FLAGS "Grass,Rock,Snow,Sand,Dirt,Moss"

// One thing that spawns inside a biome: what it is, how much, how big, and where exactly. Climate/slope/material
// limits here narrow the biome's own (they never widen them).
//
// X(type, name, default, variant type, hint, hint string, group)
#define EDEN_FOLIAGE_LAYER_PROPERTIES(X)                                                                                  \
	X(bool, enabled, true, BOOL, PROPERTY_HINT_NONE, "", "")                                                              \
	X(int, kind, BOULDER, INT, PROPERTY_HINT_ENUM, EDEN_FOLIAGE_KIND_NAMES, "")                                      \
	X(int, variants, 2, INT, PROPERTY_HINT_RANGE, "1,8", "")                                                               \
	X(float, density, 0.01f, FLOAT, PROPERTY_HINT_RANGE, "0.0,20.0,0.0001,or_greater", "Amount & Size")                   \
	X(float, scale_min, 0.8f, FLOAT, PROPERTY_HINT_RANGE, "0.01,50.0,0.01,or_greater", "Amount & Size")                   \
	X(float, scale_max, 1.3f, FLOAT, PROPERTY_HINT_RANGE, "0.01,50.0,0.01,or_greater", "Amount & Size")                   \
	X(float, clump_size, 0.0f, FLOAT, PROPERTY_HINT_RANGE, "0.0,2000.0,1.0", "Amount & Size")                             \
	X(Vector2, temperature, Vector2(0, 1), VECTOR2, PROPERTY_HINT_NONE, "", "Where")                                      \
	X(Vector2, moisture, Vector2(0, 1), VECTOR2, PROPERTY_HINT_NONE, "", "Where")                                         \
	X(Vector2, slope, Vector2(0, 90), VECTOR2, PROPERTY_HINT_NONE, "", "Where")                                           \
	X(int, materials, 0, INT, PROPERTY_HINT_FLAGS, EDEN_FOLIAGE_MATERIAL_FLAGS, "Where")                                  \
	X(int, placement, ANYWHERE, INT, PROPERTY_HINT_ENUM, "Anywhere,Forest", "Placement")                        \
	X(float, vertical_alignment, 1.0f, FLOAT, PROPERTY_HINT_RANGE, "0.0,1.0,0.01", "Placement")                           \
	X(float, sink, 0.0f, FLOAT, PROPERTY_HINT_RANGE, "-20.0,5.0,0.01", "Placement")                                       \
	X(float, draw_distance, 0.0f, FLOAT, PROPERTY_HINT_RANGE, "0.0,5000.0,1.0", "Placement")                              \
	X(int, lod, 0, INT, PROPERTY_HINT_RANGE, "0,3", "Placement")                                                          \
	X(bool, collision, false, BOOL, PROPERTY_HINT_NONE, "", "Placement")                                                  \
	X(int, shadow_mode, MODE_AUTO, INT, PROPERTY_HINT_ENUM, "Auto,On,Off", "Placement")                                   \
	X(int, far_mode, MODE_AUTO, INT, PROPERTY_HINT_ENUM, "Auto,On,Off", "Far LOD")                                        \
	X(float, far_distance, 8000.0f, FLOAT, PROPERTY_HINT_RANGE, "500.0,20000.0,1.0", "Far LOD")                           \
	X(float, far_detail_distance, 512.0f, FLOAT, PROPERTY_HINT_RANGE, "0.0,4000.0,1.0", "Far LOD")                        \
	X(int, far_resolution, 5, INT, PROPERTY_HINT_RANGE, "2,12", "Far LOD")                                                \
	X(float, far_density_falloff, 0.35f, FLOAT, PROPERTY_HINT_RANGE, "0.05,1.0,0.01", "Far LOD")                          \
	X(float, far_scale_growth, 1.2f, FLOAT, PROPERTY_HINT_RANGE, "1.0,2.0,0.01", "Far LOD")                               \
	X(Color, rock_color, Color(0.46f, 0.45f, 0.42f), COLOR, PROPERTY_HINT_NONE, "", "Rock Look")                          \
	X(float, color_variation, 0.12f, FLOAT, PROPERTY_HINT_RANGE, "0.0,0.5,0.01", "Rock Look")                             \
	X(float, moss, 0.0f, FLOAT, PROPERTY_HINT_RANGE, "0.0,1.0,0.01", "Rock Look")                                         \
	X(Color, moss_color, Color(0.24f, 0.36f, 0.14f), COLOR, PROPERTY_HINT_NONE, "", "Rock Look")                          \
	X(float, lichen, 0.0f, FLOAT, PROPERTY_HINT_RANGE, "0.0,0.5,0.01", "Rock Look")                                       \
	X(Color, lichen_color, Color(0.72f, 0.62f, 0.3f), COLOR, PROPERTY_HINT_NONE, "", "Rock Look")                         \
	X(int, facet_detail, 1, INT, PROPERTY_HINT_RANGE, "0,3", "Rock Look")                                                 \
	X(Vector3, stretch, Vector3(1.0f, 0.75f, 1.0f), VECTOR3, PROPERTY_HINT_NONE, "", "Rock Look")                         \
	X(float, roughness, 0.25f, FLOAT, PROPERTY_HINT_RANGE, "0.0,0.6,0.01", "Rock Look")                                   \
	X(Color, grass_base_color, Color(0.16f, 0.33f, 0.11f), COLOR, PROPERTY_HINT_NONE, "", "Grass Look")                   \
	X(Color, grass_tip_color, Color(0.32f, 0.54f, 0.20f), COLOR, PROPERTY_HINT_NONE, "", "Grass Look")

#define EDEN_FOLIAGE_KIND_NAMES "Grass,Dry Grass,Oak,Pine,Birch,Willow,Palm,Dead Tree,Fruit Tree,Shrub,Fern,Dead Bush,Tall Grass,Cactus,Berry Bush,Pebbles,Boulder,Rock Slab,Rock Spire,Branch,Log"

class EdenFoliageLayer : public Resource {
	GDCLASS(EdenFoliageLayer, Resource);

public:
	enum Kind {
		GRASS,
		DRY_GRASS,
		OAK,
		PINE,
		BIRCH,
		WILLOW,
		PALM,
		DEAD_TREE,
		FRUIT_TREE,
		SHRUB,
		FERN,
		DEAD_BUSH,
		TALL_GRASS,
		CACTUS,
		BERRY_BUSH,
		PEBBLES,
		BOULDER,
		ROCK_SLAB,
		ROCK_SPIRE,
		BRANCH,
		LOG,
		KIND_MAX,
	};
	enum Placement {
		ANYWHERE,
		FOREST, // only inside the config's forest patches (one shared mask for every forest layer)
	};
	// shadow_mode, far_mode: Auto = trees and rocks at least 1.5 m (see casts_shadow / has_far)
	enum Mode {
		MODE_AUTO,
		MODE_ON,
		MODE_OFF,
	};

	// `name` has its own accessors: Resource already binds set_name/get_name (for resource_name)
	String name = "Layer";
	void set_layer_name(const String &p_value);
	String get_layer_name() const { return name; }
	static String kind_name(int p_kind);

#define EDEN_FL_DECL(m_type, m_name, m_default, m_vtype, m_hint, m_hint_string, m_group) \
	m_type m_name = m_default;                                                            \
	void set_##m_name(const m_type &p_value);                                             \
	m_type get_##m_name() const { return m_name; }
	EDEN_FOLIAGE_LAYER_PROPERTIES(EDEN_FL_DECL)
#undef EDEN_FL_DECL

	bool is_tree() const { return kind >= OAK && kind <= FRUIT_TREE; }
	bool is_grass() const { return kind == GRASS || kind == DRY_GRASS; }
	bool is_rock() const { return kind >= PEBBLES && kind <= ROCK_SPIRE; }
	// Far tiers / shadows for a final size range (after the biome's size scale)
	bool has_far(float p_max_size) const;
	bool casts_shadow(float p_max_size) const;
	bool sways() const { return !is_rock() && kind != BRANCH && kind != LOG; }

protected:
	static void _bind_methods();
};

// A region of the planet and what grows there. Climate comes from the terrain's surface data (V4 generator:
// temperature ~0.9 equator, ~0.75 subtropics, ~0.4 mid latitudes, ~0.15 subarctic, ~0 poles; moisture 0..1).
// Regions that aren't climates (cliffs, mountain rock) are biomes too: set a slope range or ground materials.
class EdenFoliageBiome : public Resource {
	GDCLASS(EdenFoliageBiome, Resource);

public:
	String name = "Biome";
	bool enabled = true;
	Vector2 temperature = Vector2(0, 1);
	Vector2 moisture = Vector2(0, 1);
	Vector2 slope = Vector2(0, 90);
	int materials = 0;
	float density_scale = 1.0f;
	float size_scale = 1.0f;
	TypedArray<EdenFoliageLayer> layers;

	void set_biome_name(const String &p_value);
	String get_biome_name() const { return name; }
	void set_enabled(bool p_value);
	bool get_enabled() const { return enabled; }
	void set_temperature(const Vector2 &p_value);
	Vector2 get_temperature() const { return temperature; }
	void set_moisture(const Vector2 &p_value);
	Vector2 get_moisture() const { return moisture; }
	void set_slope(const Vector2 &p_value);
	Vector2 get_slope() const { return slope; }
	void set_materials(int p_value);
	int get_materials() const { return materials; }
	void set_density_scale(float p_value);
	float get_density_scale() const { return density_scale; }
	void set_size_scale(float p_value);
	float get_size_scale() const { return size_scale; }
	void set_layers(const TypedArray<EdenFoliageLayer> &p_value);
	TypedArray<EdenFoliageLayer> get_layers() const { return layers; }

protected:
	static void _bind_methods();
};

// Every biome EdenFoliage spawns, plus the forest mask shared by every Forest-placement layer (also read by
// EdenAmbience, through EdenFoliage.get_forest_density, to place forest sounds and falling leaves).
class EdenFoliageConfig : public Resource {
	GDCLASS(EdenFoliageConfig, Resource);

public:
	TypedArray<EdenFoliageBiome> biomes;
	float forest_patch_size = 900.0f; // typical size of a forest patch (m)
	float forest_coverage = 0.45f; // share of the land (where the climate allows forest) covered by forest
	float forest_edge = 0.25f; // 0 = hard edge, higher = trees thin out gradually into the clearing
	int forest_seed = 7;

	void set_biomes(const TypedArray<EdenFoliageBiome> &p_value);
	TypedArray<EdenFoliageBiome> get_biomes() const { return biomes; }
	void set_forest_patch_size(float p_value);
	float get_forest_patch_size() const { return forest_patch_size; }
	void set_forest_coverage(float p_value);
	float get_forest_coverage() const { return forest_coverage; }
	void set_forest_edge(float p_value);
	float get_forest_edge() const { return forest_edge; }
	void set_forest_seed(int p_value);
	int get_forest_seed() const { return forest_seed; }

	// Whether any enabled biome with forest layers covers this climate
	bool forest_climate(float p_temperature, float p_moisture) const;
	// The built-in setup: climate biomes plus Cliffs, Mountain Rock and Grassland Rocks regions
	static Ref<EdenFoliageConfig> make_default();
	// Dense forest layers for a climate biome (Temperate, Boreal, Tropical; empty otherwise)
	static TypedArray<EdenFoliageLayer> forest_layers(const String &p_biome_name);

protected:
	static void _bind_methods();
};

VARIANT_ENUM_CAST(EdenFoliageLayer::Kind);
VARIANT_ENUM_CAST(EdenFoliageLayer::Placement);
VARIANT_ENUM_CAST(EdenFoliageLayer::Mode);

#endif // EDEN_FOLIAGE_CONFIG_H
