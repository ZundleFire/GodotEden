#ifndef EDEN_PLANET_GENERATOR_CLEAN_H
#define EDEN_PLANET_GENERATOR_CLEAN_H

#include "modules/noise/fastnoise_lite.h"
#include "modules/voxel/generators/voxel_generator.h"
#include "modules/voxel/storage/voxel_buffer.h"
#include "modules/voxel/util/thread/rw_lock.h"

class EdenPlanetGeneratorClean : public zylann::voxel::VoxelGenerator {
	GDCLASS(EdenPlanetGeneratorClean, zylann::voxel::VoxelGenerator);

public:
	enum Biome {
		BIOME_OCEAN = 0,
		BIOME_TUNDRA = 1,
		BIOME_GRASSLAND = 2,
		BIOME_FOREST = 3,
		BIOME_TROPICAL = 4,
		BIOME_DESERT = 5,
	};

	static const int MAT_GRASS = 0;
	static const int MAT_ROCK = 1;
	static const int MAT_SNOW = 2;
	static const int MAT_SAND = 3;
	static const int MAT_DIRT = 4;
	static const int MAT_MOSS = 5;
	static const int MAT_OCEAN_FLOOR = 6;

	EdenPlanetGeneratorClean();
	~EdenPlanetGeneratorClean();

	Result generate_block(VoxelQueryData input) override;
	int get_used_channels_mask() const override;
	void setup();

	void set_planet_radius(float v);
	float get_planet_radius() const;
	void set_seed(int v);
	int get_seed() const;
	void set_max_terrain_height(float v);
	float get_max_terrain_height() const;

	void set_land_coverage(float v);
	float get_land_coverage() const;
	void set_land_height(float v);
	float get_land_height() const;
	void set_ocean_depth(float v);
	float get_ocean_depth() const;
	void set_land_noise_amplitude(float v);
	float get_land_noise_amplitude() const;
	void set_land_noise_falloff(float v);
	float get_land_noise_falloff() const;
	void set_land_noise_freq(float v);
	float get_land_noise_freq() const;

	void set_temp_noise_freq(float v);
	float get_temp_noise_freq() const;
	void set_temp_noise_strength(float v);
	float get_temp_noise_strength() const;
	void set_moisture_noise_freq(float v);
	float get_moisture_noise_freq() const;
	void set_moisture_noise_strength(float v);
	float get_moisture_noise_strength() const;
	void set_evaporation_noise_freq(float v);
	float get_evaporation_noise_freq() const;
	void set_evaporation_noise_strength(float v);
	float get_evaporation_noise_strength() const;
	void set_evaporation_strength(float v);
	float get_evaporation_strength() const;

	void set_biome_region_size(float v);
	float get_biome_region_size() const;
	void set_biome_edge_blend(float v);
	float get_biome_edge_blend() const;
	void set_biome_selector_type(int v);
	int get_biome_selector_type() const;
	void set_biome_selector_freq(float v);
	float get_biome_selector_freq() const;
	void set_biome_selector_octaves(int v);
	int get_biome_selector_octaves() const;
	void set_biome_selector_gain(float v);
	float get_biome_selector_gain() const;
	void set_biome_selector_lacunarity(float v);
	float get_biome_selector_lacunarity() const;

	void set_desert_detail_type(int v);
	int get_desert_detail_type() const;
	void set_desert_detail_freq(float v);
	float get_desert_detail_freq() const;
	void set_desert_detail_octaves(int v);
	int get_desert_detail_octaves() const;
	void set_desert_detail_gain(float v);
	float get_desert_detail_gain() const;
	void set_desert_detail_lacunarity(float v);
	float get_desert_detail_lacunarity() const;
	void set_desert_detail_amp(float v);
	float get_desert_detail_amp() const;

	void set_tundra_detail_type(int v);
	int get_tundra_detail_type() const;
	void set_tundra_detail_freq(float v);
	float get_tundra_detail_freq() const;
	void set_tundra_detail_octaves(int v);
	int get_tundra_detail_octaves() const;
	void set_tundra_detail_gain(float v);
	float get_tundra_detail_gain() const;
	void set_tundra_detail_lacunarity(float v);
	float get_tundra_detail_lacunarity() const;
	void set_tundra_detail_amp(float v);
	float get_tundra_detail_amp() const;

	void set_grassland_detail_type(int v);
	int get_grassland_detail_type() const;
	void set_grassland_detail_freq(float v);
	float get_grassland_detail_freq() const;
	void set_grassland_detail_octaves(int v);
	int get_grassland_detail_octaves() const;
	void set_grassland_detail_gain(float v);
	float get_grassland_detail_gain() const;
	void set_grassland_detail_lacunarity(float v);
	float get_grassland_detail_lacunarity() const;
	void set_grassland_detail_amp(float v);
	float get_grassland_detail_amp() const;

	void set_forest_detail_type(int v);
	int get_forest_detail_type() const;
	void set_forest_detail_freq(float v);
	float get_forest_detail_freq() const;
	void set_forest_detail_octaves(int v);
	int get_forest_detail_octaves() const;
	void set_forest_detail_gain(float v);
	float get_forest_detail_gain() const;
	void set_forest_detail_lacunarity(float v);
	float get_forest_detail_lacunarity() const;
	void set_forest_detail_amp(float v);
	float get_forest_detail_amp() const;

	void set_tropical_detail_type(int v);
	int get_tropical_detail_type() const;
	void set_tropical_detail_freq(float v);
	float get_tropical_detail_freq() const;
	void set_tropical_detail_octaves(int v);
	int get_tropical_detail_octaves() const;
	void set_tropical_detail_gain(float v);
	float get_tropical_detail_gain() const;
	void set_tropical_detail_lacunarity(float v);
	float get_tropical_detail_lacunarity() const;
	void set_tropical_detail_amp(float v);
	float get_tropical_detail_amp() const;

	void set_mountain_area_freq(float v);
	float get_mountain_area_freq() const;
	void set_mountain_area_coverage(float v);
	float get_mountain_area_coverage() const;
	void set_mountain_area_falloff(float v);
	float get_mountain_area_falloff() const;
	void set_mountain_shape_type(int v);
	int get_mountain_shape_type() const;
	void set_mountain_shape_freq(float v);
	float get_mountain_shape_freq() const;
	void set_mountain_shape_octaves(int v);
	int get_mountain_shape_octaves() const;
	void set_mountain_shape_gain(float v);
	float get_mountain_shape_gain() const;
	void set_mountain_shape_lacunarity(float v);
	float get_mountain_shape_lacunarity() const;
	void set_mountain_shape_sharpness(float v);
	float get_mountain_shape_sharpness() const;
	void set_mountain_shape_fractal_type(int v);
	int get_mountain_shape_fractal_type() const;
	void set_mountain_shape_warp_type(int v);
	int get_mountain_shape_warp_type() const;
	void set_mountain_shape_warp_amp(float v);
	float get_mountain_shape_warp_amp() const;
	void set_mountain_shape_warp_freq(float v);
	float get_mountain_shape_warp_freq() const;
	void set_mountain_height(float v);
	float get_mountain_height() const;

protected:
	static void _bind_methods();

private:
	struct Parameters {
		bool is_setup = false;

		float planet_radius = 40000.0f;
		int seed = 42;
		float max_terrain_height = 6000.0f;

		float land_coverage = 0.50f;
		float land_height = 450.0f;
		float ocean_depth = 1200.0f;
		float land_noise_amplitude = 0.65f;
		float land_noise_falloff = 0.28f;
		float land_noise_freq = 0.00019f;

		float temp_noise_freq = 0.00025f;
		float temp_noise_strength = 9.0f;
		float moisture_noise_freq = 0.00020f;
		float moisture_noise_strength = 0.30f;
		float evaporation_noise_freq = 0.00023f;
		float evaporation_noise_strength = 0.30f;
		float evaporation_strength = 0.35f;

		float biome_region_size = 0.55f;
		float biome_edge_blend = 0.10f;
		int biome_selector_type = FastNoiseLite::TYPE_SIMPLEX_SMOOTH;
		float biome_selector_freq = 0.00006f;
		int biome_selector_octaves = 3;
		float biome_selector_gain = 0.5f;
		float biome_selector_lacunarity = 2.0f;
		int desert_detail_type = FastNoiseLite::TYPE_SIMPLEX_SMOOTH;
		float desert_detail_freq = 0.00030f;
		int desert_detail_octaves = 3;
		float desert_detail_gain = 0.50f;
		float desert_detail_lacunarity = 2.2f;
		float desert_detail_amp = 140.0f;

		int tundra_detail_type = FastNoiseLite::TYPE_SIMPLEX_SMOOTH;
		float tundra_detail_freq = 0.00025f;
		int tundra_detail_octaves = 2;
		float tundra_detail_gain = 0.45f;
		float tundra_detail_lacunarity = 2.0f;
		float tundra_detail_amp = 70.0f;

		int grassland_detail_type = FastNoiseLite::TYPE_SIMPLEX_SMOOTH;
		float grassland_detail_freq = 0.00035f;
		int grassland_detail_octaves = 3;
		float grassland_detail_gain = 0.50f;
		float grassland_detail_lacunarity = 2.0f;
		float grassland_detail_amp = 90.0f;

		int forest_detail_type = FastNoiseLite::TYPE_SIMPLEX_SMOOTH;
		float forest_detail_freq = 0.00040f;
		int forest_detail_octaves = 4;
		float forest_detail_gain = 0.50f;
		float forest_detail_lacunarity = 2.0f;
		float forest_detail_amp = 100.0f;

		int tropical_detail_type = FastNoiseLite::TYPE_SIMPLEX_SMOOTH;
		float tropical_detail_freq = 0.00045f;
		int tropical_detail_octaves = 4;
		float tropical_detail_gain = 0.55f;
		float tropical_detail_lacunarity = 2.1f;
		float tropical_detail_amp = 160.0f;

		float mountain_area_freq = 0.00011f;
		float mountain_area_coverage = 0.22f;
		float mountain_area_falloff = 0.18f;
		int mountain_shape_type = FastNoiseLite::TYPE_SIMPLEX_SMOOTH;
		float mountain_shape_freq = 0.00024f;
		int mountain_shape_octaves = 5;
		float mountain_shape_gain = 0.55f;
		float mountain_shape_lacunarity = 2.0f;
		float mountain_shape_sharpness = 2.2f;
		int mountain_shape_fractal_type = FastNoiseLite::FRACTAL_RIDGED;
		int mountain_shape_warp_type = 0; // DOMAIN_WARP_SIMPLEX
		float mountain_shape_warp_amp = 0.0f; // 0 = disabled
		float mountain_shape_warp_freq = 0.0005f;
		float mountain_height = 2200.0f;

		static constexpr int LAND_CDF_SIZE = 128;
		float land_cdf_hf[LAND_CDF_SIZE] = {};
		float land_cdf_lf[LAND_CDF_SIZE] = {};
	};

	Parameters _parameters;
	mutable zylann::RWLock _parameters_lock;

	Ref<FastNoiseLite> _noise_land;
	Ref<FastNoiseLite> _noise_temp;
	Ref<FastNoiseLite> _noise_moisture;
	Ref<FastNoiseLite> _noise_evaporation;
	Ref<FastNoiseLite> _noise_biome_selector;
	Ref<FastNoiseLite> _noise_detail_desert;
	Ref<FastNoiseLite> _noise_detail_tundra;
	Ref<FastNoiseLite> _noise_detail_grassland;
	Ref<FastNoiseLite> _noise_detail_forest;
	Ref<FastNoiseLite> _noise_detail_tropical;
	Ref<FastNoiseLite> _noise_mountain_area;
	Ref<FastNoiseLite> _noise_mountain_shape;

	Ref<FastNoiseLite> _make_noise(int p_seed, float p_freq, int p_octaves = 4, float p_gain = 0.5f, float p_lacunarity = 2.0f,
			int p_noise_type = FastNoiseLite::TYPE_SIMPLEX_SMOOTH, int p_fractal_type = FastNoiseLite::FRACTAL_FBM);

	static inline float _ss(float e0, float e1, float x) {
		float t = CLAMP((x - e0) / (e1 - e0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	static void _pack_mixel4(int land_mat, int ocean_mat, float cont,
			float sand_start, float sand_end,
			float ocean_start, float ocean_end,
			int &r_indices, int &r_weights);

	int _classify_biome(float temp_c, float moisture, float evap, float selector) const;
};

#endif // EDEN_PLANET_GENERATOR_CLEAN_H
