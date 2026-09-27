#ifndef EDEN_FOLIAGE_H
#define EDEN_FOLIAGE_H

#include "eden_foliage_config.h"

#include "modules/voxel/terrain/instancing/voxel_instancer.h"

class FastNoiseLite;

// Low-poly GPU foliage for a VoxelLodTerrain planet (EdenPlanetGeneratorV4), all MultiMesh. What spawns where is data:
// an EdenFoliageConfig of biomes (climate / slope / material regions), each with layers (trees, bushes, grass, rocks,
// wood) and their density, size range and placement. Edit it in the inspector; the foliage rebuilds. VoxelLodTerrain
// allows only ONE VoxelInstancer, so every layer is an item in this node's library.
//
// Biomes read the terrain's surface data (mesher surface_data_enabled) through the VoxelInstanceGenerator climate
// filter. The library is generated, never saved: it is detached around editor saves.
//
// X(type, name, default, variant type, hint, hint string, group, what a change does: REBUILD or MATERIALS)
#define EDEN_FOLIAGE_PROPERTIES(X)                                                                                                        \
	X(bool, show_in_editor, true, BOOL, PROPERTY_HINT_NONE, "", "", REBUILD)                                                             \
	X(float, density_scale, 1.0f, FLOAT, PROPERTY_HINT_RANGE, "0.0,4.0,0.01", "", REBUILD)                                               \
	X(float, collision_distance, 64.0f, FLOAT, PROPERTY_HINT_NONE, "", "", REBUILD)                                                      \
	X(float, grass_density_scale, 1.0f, FLOAT, PROPERTY_HINT_RANGE, "0.0,4.0,0.01", "Grass", REBUILD)                                    \
	X(int, blades_per_tuft, 7, INT, PROPERTY_HINT_NONE, "", "Grass", REBUILD)                                                            \
	X(float, grass_lod_distance, 25.0f, FLOAT, PROPERTY_HINT_NONE, "", "Grass", REBUILD)                                                 \
	X(int, far_blades_per_tuft, 4, INT, PROPERTY_HINT_RANGE, "1,12", "Grass", REBUILD)                                                   \
	X(float, grass_fade_start, 45.0f, FLOAT, PROPERTY_HINT_NONE, "", "Grass", MATERIALS)                                                 \
	X(float, grass_fade_end, 60.0f, FLOAT, PROPERTY_HINT_NONE, "", "Grass", REBUILD)                                                     \
	X(float, grass_wind_fade_start, 20.0f, FLOAT, PROPERTY_HINT_NONE, "", "Grass", MATERIALS)                                            \
	X(float, grass_wind_fade_end, 35.0f, FLOAT, PROPERTY_HINT_NONE, "", "Grass", MATERIALS)                                              \
	X(bool, spacing_enabled, true, BOOL, PROPERTY_HINT_NONE, "", "Spacing", REBUILD)                                                     \
	X(float, big_spacing, 4.5f, FLOAT, PROPERTY_HINT_RANGE, "1.0,20.0,0.1", "Spacing", REBUILD)                                          \
	X(float, medium_spacing, 1.8f, FLOAT, PROPERTY_HINT_RANGE, "0.5,10.0,0.1", "Spacing", REBUILD)                                       \
	X(float, medium_clearance, 1.4f, FLOAT, PROPERTY_HINT_RANGE, "0.0,10.0,0.1", "Spacing", REBUILD)                                     \
	X(bool, far_foliage_enabled, true, BOOL, PROPERTY_HINT_NONE, "", "Far Foliage", REBUILD)                                             \
	X(float, far_resolution_scale, 1.0f, FLOAT, PROPERTY_HINT_RANGE, "0.3,2.0,0.05", "Far Foliage", REBUILD)                             \
	X(float, far_distance_scale, 1.0f, FLOAT, PROPERTY_HINT_RANGE, "0.1,4.0,0.01", "Far Foliage", REBUILD)                               \
	X(float, far_visibility, 400.0f, FLOAT, PROPERTY_HINT_RANGE, "50.0,2000.0,1.0", "Far Foliage", REBUILD)                              \
	X(float, detail_distance, 60.0f, FLOAT, PROPERTY_HINT_RANGE, "0.0,1000.0,1.0", "Trees & Bushes", REBUILD)                            \
	X(float, mid_detail_scale, 3.0f, FLOAT, PROPERTY_HINT_RANGE, "1.5,8.0,0.1", "Trees & Bushes", REBUILD)                               \
	X(float, tree_size_scale, 1.0f, FLOAT, PROPERTY_HINT_RANGE, "0.1,5.0,0.01,or_greater", "Trees & Bushes", REBUILD)                    \
	X(float, tree_wind_fade_start, 60.0f, FLOAT, PROPERTY_HINT_NONE, "", "Trees & Bushes", MATERIALS)                                    \
	X(float, tree_wind_fade_end, 90.0f, FLOAT, PROPERTY_HINT_NONE, "", "Trees & Bushes", MATERIALS)

class EdenFoliage : public zylann::voxel::VoxelInstancer {
	GDCLASS(EdenFoliage, zylann::voxel::VoxelInstancer);

public:
	EdenFoliage();

	void set_config(const Ref<EdenFoliageConfig> &p_config);
	Ref<EdenFoliageConfig> get_config() const { return config; }

#define EDEN_F_DECL(m_type, m_name, m_default, m_vtype, m_hint, m_hint_string, m_group, m_action) \
	void set_##m_name(m_type p_value);                                                              \
	m_type get_##m_name() const { return m_name; }
	EDEN_FOLIAGE_PROPERTIES(EDEN_F_DECL)
#undef EDEN_F_DECL

	// Sets several properties (name -> value) and rebuilds once (EdenGraphics presets)
	void apply_quality(const Dictionary &p_values);
	// The instance library for the current config and settings (also what the node uses)
	Ref<zylann::voxel::VoxelInstanceLibrary> build_library();
	// How much forest grows at a world position (0..1): the instancer's keep probability for forest layers there, zero
	// where no biome with forest layers fits the climate or under the sea. EdenAmbience places forest sounds with it.
	float get_forest_density(const Vector3 &p_world_position);
	// Library item id -> EdenFoliageLayer.Kind of the layer it places (a collider hit can tell a tree from a rock)
	Dictionary get_item_kinds() const { return item_kinds; }

protected:
	void _notification(int p_what);
	static void _bind_methods();

private:
	Ref<EdenFoliageConfig> config;
#define EDEN_F_MEMBER(m_type, m_name, m_default, m_vtype, m_hint, m_hint_string, m_group, m_action) m_type m_name = m_default;
	EDEN_FOLIAGE_PROPERTIES(EDEN_F_MEMBER)
#undef EDEN_F_MEMBER

	Dictionary item_kinds;
	Vector<Ref<ShaderMaterial>> grass_materials;
	Dictionary ring_materials; // [sways, inner, outer, deciduous] -> ShaderMaterial
	Ref<FastNoiseLite> forest_mask;
	uint32_t signature = 0;
	uint64_t next_check_ms = 0;
	bool batching = false;
	// Libraries just replaced, kept a moment: the renderer still draws with their materials for a frame or two
	struct Retired {
		Ref<Resource> library;
		uint64_t until_ms;
	};
	Vector<Retired> retired;

	Ref<EdenFoliageConfig> _get_config();
	uint32_t _config_signature();
	void _rebuild();
	void _update_materials();
	Node3D *_terrain() const;
	float _lod_range(int p_lod) const;
	Dictionary _site_ranges();
	void _use_sites(const Ref<Resource> &p_gen, const Dictionary &p_sites, const Array &p_entry, int p_variant, int p_variants);
	Ref<Resource> _gen(float p_density, Vector2 p_temperature, Vector2 p_moisture, const PackedInt32Array &p_materials, Vector2 p_slope);
	Ref<FastNoiseLite> _forest_noise();
	float _forest_threshold();
	void _add_forest_mask(const Ref<Resource> &p_gen);
	void _add_clumping(const Ref<Resource> &p_gen, int p_seed, float p_patch_size);
	Ref<Resource> _item(const String &p_name, const Ref<Mesh> &p_mesh, const Ref<Resource> &p_gen, int p_lod, const Array &p_shapes, float p_draw_distance);
	void _add_detail_lods(const Ref<Resource> &p_item, const Ref<EdenFoliageLayer> &p_layer, int p_variant, float p_end);
	void _limit_draw_distance(const Ref<Resource> &p_item, float p_meters);
	Ref<ShaderMaterial> _ring_material(bool p_sways, float p_inner, float p_outer, bool p_deciduous);
	Ref<Resource> _grass_item(const String &p_name, const Ref<Resource> &p_gen, const Ref<EdenFoliageLayer> &p_layer);
	void _add_far_tiers(const Ref<zylann::voxel::VoxelInstanceLibrary> &p_lib, HashSet<int> &r_used, const Ref<EdenFoliageBiome> &p_biome,
			const Ref<EdenFoliageLayer> &p_layer, float p_density, Vector2 p_size, Vector2 p_temperature, Vector2 p_moisture,
			const PackedInt32Array &p_materials, Vector2 p_slope);
	static int _alloc_ids(HashSet<int> &r_used, const String &p_key);
};

#endif // EDEN_FOLIAGE_H
