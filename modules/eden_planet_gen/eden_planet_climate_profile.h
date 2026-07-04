#ifndef EDEN_PLANET_CLIMATE_PROFILE_H
#define EDEN_PLANET_CLIMATE_PROFILE_H

#include "core/io/resource.h"

class EdenPlanetClimateProfile : public Resource {
	GDCLASS(EdenPlanetClimateProfile, Resource);

public:
	EdenPlanetClimateProfile();

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
	void set_day_of_year(float v);
	float get_day_of_year() const;
	void set_season_strength(float v);
	float get_season_strength() const;
	void set_season_phase_day(float v);
	float get_season_phase_day() const;

protected:
	static void _bind_methods();

private:
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
	float biome_spawn_desert = 1.0f;
	float biome_spawn_forest = 1.0f;
	float biome_spawn_tropical = 1.0f;
	float biome_spawn_tundra = 1.0f;
	float biome_spawn_grassland = 1.0f;
	float day_of_year = 0.0f;
	float season_strength = 1.0f;
	float season_phase_day = 80.0f;
};

#endif // EDEN_PLANET_CLIMATE_PROFILE_H