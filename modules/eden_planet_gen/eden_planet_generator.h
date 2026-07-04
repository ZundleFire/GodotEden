#ifndef EDEN_PLANET_GENERATOR_H
#define EDEN_PLANET_GENERATOR_H

#include "modules/noise/fastnoise_lite.h"
#include "modules/voxel/generators/voxel_generator.h"
#include "modules/voxel/storage/voxel_buffer.h"
#include "modules/voxel/util/thread/rw_lock.h"
#include "eden_planet_climate_profile.h"
#include "planet_tectonics.h"

// C++ planet generator replacing Eden_VoxelPlanetGen_Biomes_V1.gd (866 lines).
//
// Extends VoxelGenerator directly (not VoxelGeneratorScript) so that
// generate_block() runs on the voxel engine's background worker threads.
// This eliminates the LOD transition artifacts caused by main-thread-only
// GDScript generation.
//
// Thread safety: setup() acquires a write lock, generate_block() acquires a
// read lock. Multiple worker threads can generate blocks simultaneously.

class EdenPlanetGenerator : public zylann::voxel::VoxelGenerator {
	GDCLASS(EdenPlanetGenerator, zylann::voxel::VoxelGenerator);

public:
	// Biome IDs (Whittaker classification).
	enum Biome {
		BIOME_OCEAN = 0,
		BIOME_TUNDRA = 1,
		BIOME_GRASSLAND = 2,
		BIOME_FOREST = 3,
		BIOME_TROPICAL = 4,
		BIOME_DESERT = 5,
		BIOME_HILLS_MEADOWS = 6,
	};

	// Material IDs — must match VoxelMesherTransvoxel MIXEL4 layer slots.
	static const int MAT_GRASS = 0;
	static const int MAT_ROCK = 1;
	static const int MAT_SNOW = 2;
	static const int MAT_SAND = 3;
	static const int MAT_DIRT = 4;
	static const int MAT_MOSS = 5;
	static const int MAT_OCEAN_FLOOR = 6;

	EdenPlanetGenerator();
	~EdenPlanetGenerator();

	// Result of a per-position surface query (consumed by WorldDataModule — ADR-0005).
	struct SurfaceSample {
		float height = 0.0f; // surface offset from planet_radius, world units (negative = below sea level)
		float temperature01 = 0.0f; // 0 = polar_temperature .. 1 = equator_temperature
		float rainfall01 = 0.0f; // moisture, already 0..1
		int biome = BIOME_OCEAN; // internal Biome enum value
		bool is_ocean = true;
	};

	// Samples the terrain surface along the radial ray in unit direction `dir`
	// (bisection on the same SDF pipeline generate_block uses, caves included).
	// Thread-safe (shared read lock, same as generate_block). Returns false if
	// setup() has not completed. Cost: ~30 full pipeline samples — fine for
	// per-entity queries, do not call per-voxel.
	// ponytail: temperature/rainfall are 0 over open ocean (climate is only
	// evaluated on land) — extend the climate gate if sea temperature is needed.
	bool sample_surface(const Vector3 &dir, SurfaceSample &out) const;

	// ── Core VoxelGenerator overrides ────────────────────────────────────
	Result generate_block(VoxelQueryData input) override;
	int get_used_channels_mask() const override;

	// ── Setup ────────────────────────────────────────────────────────────
	void setup();

	// ── Access to tectonics (for debug) ──────────────────────────────────
	Ref<PlanetTectonics> get_tectonics() const;
	void set_climate_profile(const Ref<EdenPlanetClimateProfile> &p_profile);
	Ref<EdenPlanetClimateProfile> get_climate_profile() const;

	// ── All parameters (getters/setters exposed to ClassDB) ──────────────
	// These all acquire the write lock internally.

	void set_planet_radius(float v);
	float get_planet_radius() const;
	void set_seed_val(int v);
	int get_seed_val() const;
	void set_max_terrain_height(float v);
	float get_max_terrain_height() const;

	// Tectonic plates
	void set_num_plates(int v);
	int get_num_plates() const;
	void set_num_voronoi_points(int v);
	int get_num_voronoi_points() const;
	void set_oceanic_fraction(float v);
	float get_oceanic_fraction() const;
	void set_plate_border_hops(int v);
	int get_plate_border_hops() const;
	void set_plate_collision_threshold(float v);
	float get_plate_collision_threshold() const;
	void set_plate_noise_freq(float v);
	float get_plate_noise_freq() const;
	void set_plate_noise_octaves(int v);
	int get_plate_noise_octaves() const;
	void set_plate_noise_gain(float v);
	float get_plate_noise_gain() const;
	void set_plate_noise_lacunarity(float v);
	float get_plate_noise_lacunarity() const;
	void set_plate_warp_strength(float v);
	float get_plate_warp_strength() const;
	void set_plate_warp_freq(float v);
	float get_plate_warp_freq() const;

	// Continent shape
	void set_continent_height(float v);
	float get_continent_height() const;
	void set_ocean_depth(float v);
	float get_ocean_depth() const;
	void set_land_coverage(float v);
	float get_land_coverage() const;
	void set_continent_size(float v);
	float get_continent_size() const;
	void set_biome_patch_size(float v);
	float get_biome_patch_size() const;
	void set_biome_patch_strength(float v);
	float get_biome_patch_strength() const;
	void set_terrain_variation(float v);
	float get_terrain_variation() const;
	void set_continent_amplitude(float v);
	float get_continent_amplitude() const;
	void set_continent_falloff(float v);
	float get_continent_falloff() const;
	void set_continent_lowfreq_mix(float v);
	float get_continent_lowfreq_mix() const;
	void set_tectonic_influence(float v);
	float get_tectonic_influence() const;
	void set_tectonic_line_width_km(float v);
	float get_tectonic_line_width_km() const;
	// kept for .tres backwards compat
	void set_continent_lo(float v);
	float get_continent_lo() const;
	void set_continent_hi(float v);
	float get_continent_hi() const;
	void set_continent_ratio_bias(float v);
	float get_continent_ratio_bias() const;
	void set_continent_shape_lowfreq_mix(float v);
	float get_continent_shape_lowfreq_mix() const;

	// Coastal transitions
	void set_cont_sand_start(float v);
	float get_cont_sand_start() const;
	void set_cont_sand_end(float v);
	float get_cont_sand_end() const;
	void set_cont_ocean_start(float v);
	float get_cont_ocean_start() const;
	void set_cont_ocean_end(float v);
	float get_cont_ocean_end() const;
	void set_shoreline_material_width_scale(float v);
	float get_shoreline_material_width_scale() const;
	void set_shoreline_elevation_falloff_width(float v);
	float get_shoreline_elevation_falloff_width() const;
	void set_shoreline_elevation_falloff_power(float v);
	float get_shoreline_elevation_falloff_power() const;
	void set_shoreline_shape_width(float v);
	float get_shoreline_shape_width() const;
	void set_shoreline_shape_strength(float v);
	float get_shoreline_shape_strength() const;
	void set_shoreline_land_shape_width(float v);
	float get_shoreline_land_shape_width() const;
	void set_shoreline_land_shape_strength(float v);
	float get_shoreline_land_shape_strength() const;
	void set_shoreline_ocean_shape_width(float v);
	float get_shoreline_ocean_shape_width() const;
	void set_shoreline_ocean_shape_strength(float v);
	float get_shoreline_ocean_shape_strength() const;
	void set_beach_width_m(float v);
	float get_beach_width_m() const;

	// Hills
	void set_hills_amplitude(float v);
	float get_hills_amplitude() const;
	void set_hills_meadow_power(float v);
	float get_hills_meadow_power() const;
	void set_continental_relief_strength(float v);
	float get_continental_relief_strength() const;

	// Mountains
	void set_mountain_height(float v);
	float get_mountain_height() const;
	void set_mountain_falloff_start(float v);
	float get_mountain_falloff_start() const;
	void set_mountain_falloff_end(float v);
	float get_mountain_falloff_end() const;
	void set_mountain_sharpness(float v);
	float get_mountain_sharpness() const;
	void set_mountain_plate_boost(float v);
	float get_mountain_plate_boost() const;
	void set_mountain_plate_min(float v);
	float get_mountain_plate_min() const;
	void set_mountain_landmask_strength(float v);
	float get_mountain_landmask_strength() const;
	void set_mountain_rock_start(float v);
	float get_mountain_rock_start() const;
	void set_mountain_snow_start(float v);
	float get_mountain_snow_start() const;

	// Valleys
	void set_trench_depth(float v);
	float get_trench_depth() const;
	void set_rift_depth(float v);
	float get_rift_depth() const;
	void set_valley_falloff_start(float v);
	float get_valley_falloff_start() const;
	void set_valley_falloff_end(float v);
	float get_valley_falloff_end() const;

	// Climate
	void set_axis_tilt(float v);
	float get_axis_tilt() const;
	void set_tilt_direction(float v);
	float get_tilt_direction() const;
	void set_equator_temperature(float v);
	float get_equator_temperature() const;
	void set_polar_temperature(float v);
	float get_polar_temperature() const;
	void set_altitude_lapse_rate(float v);
	float get_altitude_lapse_rate() const;
	void set_ocean_temp_moderation(float v);
	float get_ocean_temp_moderation() const;
	void set_ocean_mean_temp(float v);
	float get_ocean_mean_temp() const;
	void set_continental_interior_amp(float v);
	float get_continental_interior_amp() const;
	void set_temp_noise_amplitude(float v);
	float get_temp_noise_amplitude() const;
	void set_ocean_humidity(float v);
	float get_ocean_humidity() const;
	void set_interior_humidity(float v);
	float get_interior_humidity() const;
	void set_subtropical_dry_factor(float v);
	float get_subtropical_dry_factor() const;
	void set_humidity_noise_amplitude(float v);
	float get_humidity_noise_amplitude() const;
	void set_evaporation_strength(float v);
	float get_evaporation_strength() const;
	void set_temp_lat_falloff_start(float v);
	float get_temp_lat_falloff_start() const;
	void set_temp_lat_falloff_end(float v);
	float get_temp_lat_falloff_end() const;
	void set_continental_hot_lat_max(float v);
	float get_continental_hot_lat_max() const;
	void set_continental_cold_lat_min(float v);
	float get_continental_cold_lat_min() const;
	void set_band_tropical_end(float v);
	float get_band_tropical_end() const;
	void set_band_subtropical_start(float v);
	float get_band_subtropical_start() const;
	void set_band_subtropical_end(float v);
	float get_band_subtropical_end() const;
	void set_band_polar_start(float v);
	float get_band_polar_start() const;
	void set_day_of_year(float v);
	float get_day_of_year() const;
	void set_season_strength(float v);
	float get_season_strength() const;
	void set_season_phase_day(float v);
	float get_season_phase_day() const;

	// Biome detail amplitudes
	void set_amp_desert(float v);
	float get_amp_desert() const;
	void set_amp_forest(float v);
	float get_amp_forest() const;
	void set_amp_tropical(float v);
	float get_amp_tropical() const;
	void set_amp_tundra(float v);
	float get_amp_tundra() const;
	void set_amp_grassland(float v);
	float get_amp_grassland() const;
	void set_biome_spawn_desert(float v);
	float get_biome_spawn_desert() const;
	void set_biome_spawn_forest(float v);
	float get_biome_spawn_forest() const;
	void set_biome_spawn_tropical(float v);
	float get_biome_spawn_tropical() const;
	void set_biome_spawn_tundra(float v);
	float get_biome_spawn_tundra() const;
	void set_biome_spawn_hills_meadows(float v);
	float get_biome_spawn_hills_meadows() const;
	void set_biome_spawn_grassland(float v);
	float get_biome_spawn_grassland() const;
	void set_biome_region_noise_freq(float v);
	float get_biome_region_noise_freq() const;
	void set_biome_region_strength(float v);
	float get_biome_region_strength() const;
	void set_biome_region_seed_offset(int v);
	int get_biome_region_seed_offset() const;

	// Caves
	void set_enable_caves(bool v);
	bool get_enable_caves() const;
	void set_cave_carve_strength(float v);
	float get_cave_carve_strength() const;
	void set_cave_max_altitude(float v);
	float get_cave_max_altitude() const;
	void set_cave_fade_outer(float v);
	float get_cave_fade_outer() const;
	void set_cave_fade_inner(float v);
	float get_cave_fade_inner() const;
	void set_cave_threshold_low(float v);
	float get_cave_threshold_low() const;
	void set_cave_threshold_high(float v);
	float get_cave_threshold_high() const;

	// ── Per-layer noise getters/setters ─────────────────────────────
	// Continent noise
	void set_continent_noise_type(int v); int get_continent_noise_type() const;
	void set_continent_noise_freq(float v); float get_continent_noise_freq() const;
	void set_continent_noise_octaves(int v); int get_continent_noise_octaves() const;
	void set_continent_noise_gain(float v); float get_continent_noise_gain() const;
	void set_continent_noise_lacunarity(float v); float get_continent_noise_lacunarity() const;
	void set_continent_fractal_type(int v); int get_continent_fractal_type() const;
	void set_continent_warp_enabled(bool v); bool get_continent_warp_enabled() const;
	void set_continent_warp_freq(float v); float get_continent_warp_freq() const;
	void set_continent_warp_amp(float v); float get_continent_warp_amp() const;

	// Hills noise
	void set_hills_noise_type(int v); int get_hills_noise_type() const;
	void set_hills_noise_freq(float v); float get_hills_noise_freq() const;
	void set_hills_noise_octaves(int v); int get_hills_noise_octaves() const;
	void set_hills_noise_gain(float v); float get_hills_noise_gain() const;
	void set_hills_noise_lacunarity(float v); float get_hills_noise_lacunarity() const;
	void set_hills_fractal_type(int v); int get_hills_fractal_type() const;

	// Mountain noise
	void set_mountain_noise_type(int v); int get_mountain_noise_type() const;
	void set_mountain_noise_freq(float v); float get_mountain_noise_freq() const;
	void set_mountain_noise_octaves(int v); int get_mountain_noise_octaves() const;
	void set_mountain_noise_gain(float v); float get_mountain_noise_gain() const;
	void set_mountain_noise_lacunarity(float v); float get_mountain_noise_lacunarity() const;
	void set_mountain_fractal_type(int v); int get_mountain_fractal_type() const;

	// Climate noise
	void set_climate_noise_type(int v); int get_climate_noise_type() const;
	void set_climate_noise_freq(float v); float get_climate_noise_freq() const;
	void set_climate_noise_octaves(int v); int get_climate_noise_octaves() const;
	void set_climate_noise_gain(float v); float get_climate_noise_gain() const;
	void set_climate_noise_lacunarity(float v); float get_climate_noise_lacunarity() const;
	void set_climate_fractal_type(int v); int get_climate_fractal_type() const;

	// Desert noise
	void set_desert_noise_type(int v); int get_desert_noise_type() const;
	void set_desert_noise_freq(float v); float get_desert_noise_freq() const;
	void set_desert_noise_octaves(int v); int get_desert_noise_octaves() const;
	void set_desert_noise_gain(float v); float get_desert_noise_gain() const;
	void set_desert_noise_lacunarity(float v); float get_desert_noise_lacunarity() const;
	void set_desert_fractal_type(int v); int get_desert_fractal_type() const;

	// Forest noise
	void set_forest_noise_type(int v); int get_forest_noise_type() const;
	void set_forest_noise_freq(float v); float get_forest_noise_freq() const;
	void set_forest_noise_octaves(int v); int get_forest_noise_octaves() const;
	void set_forest_noise_gain(float v); float get_forest_noise_gain() const;
	void set_forest_noise_lacunarity(float v); float get_forest_noise_lacunarity() const;
	void set_forest_fractal_type(int v); int get_forest_fractal_type() const;

	// Tropical noise
	void set_tropical_noise_type(int v); int get_tropical_noise_type() const;
	void set_tropical_noise_freq(float v); float get_tropical_noise_freq() const;
	void set_tropical_noise_octaves(int v); int get_tropical_noise_octaves() const;
	void set_tropical_noise_gain(float v); float get_tropical_noise_gain() const;
	void set_tropical_noise_lacunarity(float v); float get_tropical_noise_lacunarity() const;
	void set_tropical_fractal_type(int v); int get_tropical_fractal_type() const;

	// Tundra noise
	void set_tundra_noise_type(int v); int get_tundra_noise_type() const;
	void set_tundra_noise_freq(float v); float get_tundra_noise_freq() const;
	void set_tundra_noise_octaves(int v); int get_tundra_noise_octaves() const;
	void set_tundra_noise_gain(float v); float get_tundra_noise_gain() const;
	void set_tundra_noise_lacunarity(float v); float get_tundra_noise_lacunarity() const;
	void set_tundra_fractal_type(int v); int get_tundra_fractal_type() const;

	// Grassland noise
	void set_grassland_noise_type(int v); int get_grassland_noise_type() const;
	void set_grassland_noise_freq(float v); float get_grassland_noise_freq() const;
	void set_grassland_noise_octaves(int v); int get_grassland_noise_octaves() const;
	void set_grassland_noise_gain(float v); float get_grassland_noise_gain() const;
	void set_grassland_noise_lacunarity(float v); float get_grassland_noise_lacunarity() const;
	void set_grassland_fractal_type(int v); int get_grassland_fractal_type() const;

	// Cave noise
	void set_cave_noise_type(int v); int get_cave_noise_type() const;
	void set_cave_noise_freq(float v); float get_cave_noise_freq() const;
	void set_cave_noise_octaves(int v); int get_cave_noise_octaves() const;
	void set_cave_noise_gain(float v); float get_cave_noise_gain() const;
	void set_cave_noise_lacunarity(float v); float get_cave_noise_lacunarity() const;
	void set_cave_fractal_type(int v); int get_cave_fractal_type() const;

protected:
	static void _bind_methods();

private:
	// All parameters that need to be read from worker threads.
	// Copied under read-lock at start of generate_block().
	struct Parameters {
		bool is_setup = false;

		// Planet core
		float planet_radius = 40000.0f;
		int seed_val = 42;
		float max_terrain_height = 6000.0f;

		// Tectonic plates
		int num_plates = 16;
		int num_voronoi_points = 1200;
		float oceanic_fraction = 0.68f;
		int plate_border_hops = 7;
		float plate_collision_threshold = 0.02f;
		float plate_noise_freq = 0.55f;
		int plate_noise_octaves = 6;
		float plate_noise_gain = 0.50f;
		float plate_noise_lacunarity = 2.0f;
		float plate_warp_strength = 0.20f;
		float plate_warp_freq = 1.8f;

		// Continent shape
		float continent_height = 400.0f;
		float ocean_depth = 1000.0f;
		float land_coverage = 0.50f;
		float continent_size = 0.55f;
		float biome_patch_size = 0.55f;
		float biome_patch_strength = 0.75f;
		float terrain_variation = 0.55f;
		// Height-based ocean/land mask params
		float continent_amplitude   = 0.60f;  // noise contribution strength
		float continent_falloff     = 0.30f;  // coastline transition half-width (same noise units)
		float tectonic_influence    = 0.80f;  // kept for .tres compat
		float tectonic_line_width_km = 2.0f;  // target plate-line width (~1-3 km)
		float continent_lowfreq_mix = 0.60f;  // kept for .tres compat
		// Empirical sorted CDF from setup() — continent_cdf[k] is the noise value at
		// rank k out of CONTINENT_CDF_SIZE sphere-surface samples (ascending order).
		// In generate_block, looking up index = int((1-land_coverage)*N) gives the exact
		// noise threshold such that land_coverage fraction of the sphere is above it.
		static constexpr int CONTINENT_CDF_SIZE = 128;
		float continent_cdf_hf[CONTINENT_CDF_SIZE] = {};
		float continent_cdf_lf[CONTINENT_CDF_SIZE] = {};
		// kept for .tres backwards compat — not used in generation
		float continent_lo = -0.1f;
		float continent_hi = 0.55f;
		float continent_ratio_bias = 0.0f;
		float continent_shape_lowfreq_mix = 0.55f;

		// Coastal transitions
		float cont_sand_start = 0.46f;
		float cont_sand_end = 0.50f;
		float cont_ocean_start = 0.50f;
		float cont_ocean_end = 0.54f;
		float shoreline_material_width_scale = 1.0f;
		float shoreline_elevation_falloff_width = 0.22f;
		float shoreline_elevation_falloff_power = 1.0f;
		float shoreline_shape_width = 0.10f;
		float shoreline_shape_strength = 0.0f;
		float shoreline_land_shape_width = 0.10f;
		float shoreline_land_shape_strength = 0.0f;
		float shoreline_ocean_shape_width = 0.10f;
		float shoreline_ocean_shape_strength = 0.0f;
		float beach_width_m = 50.0f;

		// Hills
		float hills_amplitude = 300.0f;
		float hills_meadow_power = 1.5f;
		float continental_relief_strength = 0.40f;

		// Mountains
		float mountain_height = 1500.0f;
		float mountain_falloff_start = 0.0f;
		float mountain_falloff_end = 0.55f;
		float mountain_sharpness = 1.5f;
		float mountain_plate_boost = 1.0f;
		float mountain_plate_min = 0.02f;
		float mountain_landmask_strength = 1.5f;
		float mountain_rock_start = 0.70f;
		float mountain_snow_start = 0.90f;

		// Valleys
		float trench_depth = 500.0f;
		float rift_depth = 300.0f;
		float valley_falloff_start = 0.0f;
		float valley_falloff_end = 0.45f;

		// Climate
		float axis_tilt = 23.5f;
		float tilt_direction = 0.0f;
		float equator_temperature = 38.0f;
		float polar_temperature = -20.0f;
		float altitude_lapse_rate = 6.5f;
		float ocean_temp_moderation = 0.35f;
		float ocean_mean_temp = 18.0f;
		float continental_interior_amp = 4.0f;
		float temp_noise_amplitude = 8.0f;
		float ocean_humidity = 0.90f;
		float interior_humidity = 0.20f;
		float subtropical_dry_factor = 0.20f;
		float humidity_noise_amplitude = 0.15f;
		float evaporation_strength = 0.20f;
		float temp_lat_falloff_start = 0.50f;
		float temp_lat_falloff_end = 0.90f;
		float continental_hot_lat_max = 0.30f;
		float continental_cold_lat_min = 0.60f;
		float band_tropical_end = 0.20f;
		float band_subtropical_start = 0.35f;
		float band_subtropical_end = 0.55f;
		float band_polar_start = 0.72f;
		float day_of_year = 0.0f;
		float season_strength = 1.0f;
		float season_phase_day = 80.0f;

		// Biome detail amplitudes
		float amp_desert = 200.0f;
		float amp_forest = 120.0f;
		float amp_tropical = 180.0f;
		float amp_tundra = 80.0f;
		float amp_grassland = 100.0f;
		float biome_spawn_desert = 1.0f;
		float biome_spawn_forest = 1.0f;
		float biome_spawn_tropical = 1.0f;
		float biome_spawn_tundra = 1.0f;
		float biome_spawn_hills_meadows = 1.0f;
		float biome_spawn_grassland = 1.0f;
		float biome_region_noise_freq = 0.00006f;
		float biome_region_strength = 0.75f;
		int biome_region_seed_offset = 0;

		// Caves
		bool enable_caves = false;
		float cave_carve_strength = 80.0f;
		float cave_max_altitude = 200.0f;
		float cave_fade_outer = 500.0f;
		float cave_fade_inner = 50.0f;
		float cave_threshold_low = -0.06f;
		float cave_threshold_high = 0.06f;

		// ── Per-layer noise configuration ───────────────────────────
		// Continent noise
		int continent_noise_type = 5;
		float continent_noise_freq = 0.000025f;
		int continent_noise_octaves = 5;
		float continent_noise_gain = 0.5f;
		float continent_noise_lacunarity = 2.0f;
		int continent_fractal_type = 1;
		bool continent_warp_enabled = true;
		float continent_warp_freq = 0.00033f;
		float continent_warp_amp = 20.0f;

		// Hills noise
		int hills_noise_type = 5;
		float hills_noise_freq = 0.00025f;
		int hills_noise_octaves = 4;
		float hills_noise_gain = 0.5f;
		float hills_noise_lacunarity = 2.0f;
		int hills_fractal_type = 1;

		// Mountain noise
		int mountain_noise_type = 5;
		float mountain_noise_freq = 0.000125f;
		int mountain_noise_octaves = 5;
		float mountain_noise_gain = 0.55f;
		float mountain_noise_lacunarity = 2.0f;
		int mountain_fractal_type = 2; // Ridged

		// Climate noise
		int climate_noise_type = 5;
		float climate_noise_freq = 0.00025f;
		int climate_noise_octaves = 3;
		float climate_noise_gain = 0.5f;
		float climate_noise_lacunarity = 2.0f;
		int climate_fractal_type = 1;

		// Desert noise
		int desert_noise_type = 5;
		float desert_noise_freq = 0.000167f;
		int desert_noise_octaves = 3;
		float desert_noise_gain = 0.5f;
		float desert_noise_lacunarity = 2.0f;
		int desert_fractal_type = 1;

		// Forest noise
		int forest_noise_type = 5;
		float forest_noise_freq = 0.00025f;
		int forest_noise_octaves = 3;
		float forest_noise_gain = 0.5f;
		float forest_noise_lacunarity = 2.0f;
		int forest_fractal_type = 1;

		// Tropical noise
		int tropical_noise_type = 3; // Perlin
		float tropical_noise_freq = 0.000333f;
		int tropical_noise_octaves = 4;
		float tropical_noise_gain = 0.5f;
		float tropical_noise_lacunarity = 2.0f;
		int tropical_fractal_type = 1;

		// Tundra noise
		int tundra_noise_type = 5;
		float tundra_noise_freq = 0.000125f;
		int tundra_noise_octaves = 3;
		float tundra_noise_gain = 0.5f;
		float tundra_noise_lacunarity = 2.0f;
		int tundra_fractal_type = 1;

		// Grassland noise
		int grassland_noise_type = 5;
		float grassland_noise_freq = 0.0002f;
		int grassland_noise_octaves = 3;
		float grassland_noise_gain = 0.5f;
		float grassland_noise_lacunarity = 2.0f;
		int grassland_fractal_type = 1;

		// Cave noise
		int cave_noise_type = 5;
		float cave_noise_freq = 0.0005f;
		int cave_noise_octaves = 4;
		float cave_noise_gain = 0.5f;
		float cave_noise_lacunarity = 2.0f;
		int cave_fractal_type = 1;

		// Derived (computed in setup)
		Vector3 tilt_axis = Vector3(0, 1, 0);
	};

	Parameters _parameters;
	mutable zylann::RWLock _parameters_lock;

	// Tectonics (created during setup, read-only after).
	Ref<PlanetTectonics> _tectonics;
	Ref<EdenPlanetClimateProfile> _climate_profile;

	// Baked tectonic maps — equirectangular arrays.
	static const int TECT_W = 512;
	static const int TECT_H = 256;
	Vector<float> _tect_oceanic;
	Vector<float> _tect_mountain;
	Vector<float> _tect_valley;

	// Noise resources — duplicated for thread safety, stored in Parameters-adjacent area.
	// Created in setup(), read-only during generation.
	Ref<FastNoiseLite> _noise_continent;
	Ref<FastNoiseLite> _noise_hills;
	Ref<FastNoiseLite> _noise_mountain;
	Ref<FastNoiseLite> _noise_climate;
	Ref<FastNoiseLite> _noise_desert;
	Ref<FastNoiseLite> _noise_forest;
	Ref<FastNoiseLite> _noise_tropical;
	Ref<FastNoiseLite> _noise_tundra;
	Ref<FastNoiseLite> _noise_grassland;
	Ref<FastNoiseLite> _noise_biome_region;
	Ref<FastNoiseLite> _noise_cave;

	// ── Internal helpers ─────────────────────────────────────────────────

	// Read-only state captured under one read-lock, shared by generate_block
	// and sample_surface so both run the exact same pipeline.
	struct SampleNoises {
		Ref<FastNoiseLite> continent, hills, mountain, climate, desert, forest,
				tropical, tundra, grassland, biome_region, cave;
		const Vector<float> *tect_oceanic = nullptr;
		const Vector<float> *tect_mountain = nullptr;
		const Vector<float> *tect_valley = nullptr;
	};

	// Per-run constants derived from Parameters once per block / query.
	struct SampleConsts {
		float budget = 0.0f;
		float alt_inner = 0.0f, alt_outer = 0.0f;
		float cont_sand_start = 0.0f, cont_sand_end = 0.0f;
		float cont_ocean_start = 0.0f, cont_ocean_end = 0.0f;
		float elev_half = 0.0f, elev_pow = 1.0f, beach_width_m = 1.0f;
		int rock_indices = 0, rock_weights = 0;
		int ocean_indices = 0, ocean_weights = 0;
	};

	// What _sample_voxel decided for one position (mirrors the historical
	// early-out writes of the generate_block loop exactly).
	enum VoxelCode {
		VOXEL_SKIP_AIR, //   alt > alt_outer          → sdf +1
		VOXEL_SKIP_SOLID, // alt < alt_inner          → sdf -1, rock
		VOXEL_EARLY_AIR, //  base_sdf > budget        → sdf = base_sdf
		VOXEL_EARLY_SOLID, // base_sdf < -budget      → sdf = base_sdf, rock
		VOXEL_FULL, //       full pipeline ran
	};

	struct VoxelSample {
		float sdf = 0.0f;
		float temp = 0.0f; // raw units (equator/polar temperature scale)
		float moist = 0.0f; // 0..1
		int biome = BIOME_OCEAN; // after material-section adjustments when with_materials
		float cont_transition = 1.0f; // 0=land 1=ocean
		float macro_land_mask = 0.0f;
		int indices = 0, weights = 0; // MIXEL4, valid when with_materials and VOXEL_FULL
	};

	static SampleConsts _make_sample_consts(const Parameters &params);

	// The complete per-position SDF/climate/material pipeline, extracted from
	// the generate_block voxel loop. Must stay behavior-identical to it.
	VoxelCode _sample_voxel(float wx, float wy, float wz,
			const Parameters &params, const SampleNoises &n, const SampleConsts &c,
			bool with_materials, VoxelSample &out) const;

	Ref<FastNoiseLite> _make_noise(int p_seed, int noise_type, float freq,
			int octaves, float gain, float lacunarity, int fractal_type);

	static inline float _ss(float e0, float e1, float x) {
		float t = CLAMP((x - e0) / (e1 - e0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	float _temperature_fast(const Ref<FastNoiseLite> &climate_noise,
			float sx, float sy, float sz, float signed_lat, float lat, float alt, float cont,
			const Parameters &p) const;

	float _moisture_fast(const Ref<FastNoiseLite> &climate_noise,
			float sx, float sy, float sz, float lat, float cont, float temp,
			const Parameters &p) const;

	int _classify_biome_fast(float temp, float moist, float cont, float lat, float alt, float climate_zone, float region_selector, float roll,
			const Parameters &p, int &out_biome2, float &out_blend) const;

	float _biome_detail_fast(float sx, float sy, float sz, int biome,
			const Ref<FastNoiseLite> &hills_noise,
			const Ref<FastNoiseLite> &desert_noise,
			const Ref<FastNoiseLite> &forest_noise,
			const Ref<FastNoiseLite> &tropical_noise,
			const Ref<FastNoiseLite> &tundra_noise,
			const Ref<FastNoiseLite> &grassland_noise,
			const Parameters &p) const;

	float _cave_carve(const Ref<FastNoiseLite> &cave_noise, float wx, float wy, float wz, float alt,
			const Parameters &p) const;

	int _land_material_for(int biome, float alt, float temp, float mtn,
			const Parameters &p) const;

	static void _pack_mixel4(int land_mat, int ocean_mat, float cont,
			float sand_start, float sand_end,
			float ocean_start, float ocean_end,
			int &r_indices, int &r_weights);

	// Bilinear tectonic map sampling.
	float _sample_tect_map(const Vector<float> &map, float map_u, float map_v) const;
	void _apply_climate_profile(Parameters &p, const Ref<EdenPlanetClimateProfile> &profile) const;
	void _on_climate_profile_changed();
};

#endif // EDEN_PLANET_GENERATOR_H
