#ifndef EDEN_PLANET_GENERATOR_V6_NATIVE_H
#define EDEN_PLANET_GENERATOR_V6_NATIVE_H

#include "core/io/image.h"
#include "modules/noise/fastnoise_lite.h"
#include "modules/voxel/generators/voxel_generator.h"
#include "modules/voxel/storage/voxel_buffer.h"
#include "modules/voxel/util/thread/rw_lock.h"
#include "planet_tectonics.h"

// Authoritative runtime planet generator path.
// Keep biome/terrain tuning here first; the GDScript V6 resource is only a bridge/debug source.
class EdenPlanetGeneratorV6Native : public zylann::voxel::VoxelGenerator {
	GDCLASS(EdenPlanetGeneratorV6Native, zylann::voxel::VoxelGenerator);

public:
	static const int MAT_GRASS = 0;
	static const int MAT_ROCK = 1;
	static const int MAT_SNOW = 2;
	static const int MAT_SAND = 3;
	static const int MAT_DIRT = 4;
	static const int MAT_MOSS = 5;
	static const int MAT_OCEAN_FLOOR = 6;

	EdenPlanetGeneratorV6Native();
	~EdenPlanetGeneratorV6Native();

	Result generate_block(VoxelQueryData input) override;
	int get_used_channels_mask() const override;
	void setup();
	Ref<Image> generate_biome_debug_image(int p_width = 1024, int p_height = 512);
	Ref<Image> get_biome_debug_image(int p_width = 1024, int p_height = 512);
	Ref<Image> get_plate_debug_image(int p_width = 1024, int p_height = 512);
	Ref<Image> get_plate_data_image(int p_width = 1024, int p_height = 512);
	Ref<Image> get_macro_debug_image(int p_width = 1024, int p_height = 512);
	Ref<Image> get_climate_debug_image(int p_width = 1024, int p_height = 512);
	Ref<Image> get_topology_cell_debug_image(int p_width = 1024, int p_height = 512);
	Dictionary get_biome_region_stats() const;

	void set_planet_radius(float v);
	float get_planet_radius() const;
	void set_seed(int v);
	int get_seed() const;
	void set_max_terrain_height(float v);
	float get_max_terrain_height() const;

	void set_continent_height(float v);
	float get_continent_height() const;
	void set_ocean_depth(float v);
	float get_ocean_depth() const;
	void set_oceanic_fraction(float v);
	float get_oceanic_fraction() const;
	void set_n_points(int v);
	int get_n_points() const;
	void set_n_plates(int v);
	int get_n_plates() const;
	void set_border_warp(float v);
	float get_border_warp() const;
	void set_oceanic_fraction_influence(float v);
	float get_oceanic_fraction_influence() const;
	void set_continent_cohesion(float v);
	float get_continent_cohesion() const;
	void set_continent_balance(float v);
	float get_continent_balance() const;
	void set_land_coverage_target(float v);
	float get_land_coverage_target() const;
	void set_continent_size_scale(float v);
	float get_continent_size_scale() const;

	void set_base_noise_amp(float v);
	float get_base_noise_amp() const;
	void set_base_noise_freq(float v);
	float get_base_noise_freq() const;
	void set_continent_noise_amp(float v);
	float get_continent_noise_amp() const;
	void set_continent_noise_freq(float v);
	float get_continent_noise_freq() const;
	void set_plateau_noise_amp(float v);
	float get_plateau_noise_amp() const;
	void set_plateau_noise_freq(float v);
	float get_plateau_noise_freq() const;
	void set_oceanic_noise_amp(float v);
	float get_oceanic_noise_amp() const;
	void set_oceanic_noise_freq(float v);
	float get_oceanic_noise_freq() const;
	void set_mountain_noise_amp(float v);
	float get_mountain_noise_amp() const;
	void set_mountain_noise_freq(float v);
	float get_mountain_noise_freq() const;
	void set_detail_noise_amp(float v);
	float get_detail_noise_amp() const;
	void set_detail_noise_freq(float v);
	float get_detail_noise_freq() const;
	void set_mountain_shape_boost(float v);
	float get_mountain_shape_boost() const;
	void set_coast_smoothing_strength(float v);
	float get_coast_smoothing_strength() const;
	void set_coast_smoothing_width(float v);
	float get_coast_smoothing_width() const;
	void set_climate_equator_temp_c(float v);
	float get_climate_equator_temp_c() const;
	void set_climate_polar_temp_c(float v);
	float get_climate_polar_temp_c() const;
	void set_climate_ocean_humidity(float v);
	float get_climate_ocean_humidity() const;
	void set_climate_interior_humidity(float v);
	float get_climate_interior_humidity() const;
	void set_biome_terrain_blend_strength(float v);
	float get_biome_terrain_blend_strength() const;
	void set_biome_region_bias_strength(float v);
	float get_biome_region_bias_strength() const;
	void set_biome_region_min_cells(int v);
	int get_biome_region_min_cells() const;
	void set_biome_region_max_cells(int v);
	int get_biome_region_max_cells() const;
	void set_biome_spawn_desert(float v);
	float get_biome_spawn_desert() const;
	void set_biome_spawn_forest(float v);
	float get_biome_spawn_forest() const;
	void set_biome_spawn_tropical(float v);
	float get_biome_spawn_tropical() const;
	void set_biome_spawn_tundra(float v);
	float get_biome_spawn_tundra() const;
	void set_biome_spawn_grassland(float v);
	float get_biome_spawn_grassland() const;
	void set_biome_spawn_alpine(float v);
	float get_biome_spawn_alpine() const;
	void set_beach_width_m(float v);
	float get_beach_width_m() const;

protected:
	static void _bind_methods();

private:
	struct Parameters {
		bool is_setup = false;

		float planet_radius = 40000.0f;
		int seed = 12345;
		float max_terrain_height = 3200.0f;

		float continent_height = 260.0f;
		float ocean_depth = 1250.0f;
		float oceanic_fraction = 0.62f;
		int n_points = 2400;
		int n_plates = 24;
		float border_warp = 0.10f;
		float oceanic_fraction_influence = 1.0f;
		float continent_cohesion = 0.84f;
		float continent_balance = 0.02f;
		float land_coverage_target = 0.34f;
		float continent_size_scale = 2.95f;

		float base_noise_amp = 120.0f;
		float base_noise_freq = 0.00042f;
		float continent_noise_amp = 280.0f;
		float continent_noise_freq = 0.000045f;
		float plateau_noise_amp = 96.0f;
		float plateau_noise_freq = 0.000095f;
		float oceanic_noise_amp = 42.0f;
		float oceanic_noise_freq = 0.00005f;
		float mountain_noise_amp = 220.0f;
		float mountain_noise_freq = 0.00012f;
		float detail_noise_amp = 32.0f;
		float detail_noise_freq = 0.00048f;
		float mountain_shape_boost = 1.18f;
		float coast_smoothing_strength = 0.82f;
		float coast_smoothing_width = 7600.0f;
		float climate_equator_temp_c = 35.0f;
		float climate_polar_temp_c = 6.0f;
		float climate_ocean_humidity = 0.90f;
		float climate_interior_humidity = 0.24f;
		float biome_terrain_blend_strength = 0.72f;
		float biome_region_bias_strength = 0.78f;
		int biome_region_min_cells = 2;
		int biome_region_max_cells = 12;
		float biome_spawn_desert = 0.38f;
		float biome_spawn_forest = 1.0f;
		float biome_spawn_tropical = 0.85f;
		float biome_spawn_tundra = 0.0f;
		float biome_spawn_grassland = 1.0f;
		float biome_spawn_alpine = 0.06f;
		float beach_width_m = 160.0f;
	};

	Parameters _parameters;
	mutable zylann::RWLock _parameters_lock;

	Ref<FastNoiseLite> _noise_base;
	Ref<FastNoiseLite> _noise_continent;
	Ref<FastNoiseLite> _noise_plateau;
	Ref<FastNoiseLite> _noise_oceanic;
	Ref<FastNoiseLite> _noise_mountain;
	Ref<FastNoiseLite> _noise_detail;
	Ref<PlanetTectonics> _tectonics;

	Ref<FastNoiseLite> _make_noise(int p_seed, float p_freq, int p_octaves = 4, float p_gain = 0.5f, float p_lacunarity = 2.0f,
			int p_noise_type = FastNoiseLite::TYPE_SIMPLEX_SMOOTH, int p_fractal_type = FastNoiseLite::FRACTAL_FBM);
	void _build_biome_regions(const Parameters &p);

	Vector<int> _biome_point_region_id;
	Vector<uint8_t> _biome_region_kind;
	Vector<float> _biome_region_strength;

	static inline float _ss(float e0, float e1, float x) {
		float t = CLAMP((x - e0) / (e1 - e0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	static void _pack_mixel4(int land_mat, int ocean_mat, float transition,
			float sand_start, float sand_end,
			float ocean_start, float ocean_end,
			int &r_indices, int &r_weights);
};

#endif // EDEN_PLANET_GENERATOR_V6_NATIVE_H
