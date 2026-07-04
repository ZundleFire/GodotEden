#include "eden_planet_climate_profile.h"

EdenPlanetClimateProfile::EdenPlanetClimateProfile() {
}

#define EDEN_CLIMATE_PROP(name) \
	void EdenPlanetClimateProfile::set_##name(float v) { \
		name = v; \
		emit_changed(); \
	} \
	float EdenPlanetClimateProfile::get_##name() const { \
		return name; \
	}

EDEN_CLIMATE_PROP(axis_tilt)
EDEN_CLIMATE_PROP(tilt_direction)
EDEN_CLIMATE_PROP(equator_temperature)
EDEN_CLIMATE_PROP(polar_temperature)
EDEN_CLIMATE_PROP(altitude_lapse_rate)
EDEN_CLIMATE_PROP(ocean_temp_moderation)
EDEN_CLIMATE_PROP(ocean_mean_temp)
EDEN_CLIMATE_PROP(continental_interior_amp)
EDEN_CLIMATE_PROP(temp_noise_amplitude)
EDEN_CLIMATE_PROP(ocean_humidity)
EDEN_CLIMATE_PROP(interior_humidity)
EDEN_CLIMATE_PROP(subtropical_dry_factor)
EDEN_CLIMATE_PROP(humidity_noise_amplitude)
EDEN_CLIMATE_PROP(evaporation_strength)
EDEN_CLIMATE_PROP(temp_lat_falloff_start)
EDEN_CLIMATE_PROP(temp_lat_falloff_end)
EDEN_CLIMATE_PROP(continental_hot_lat_max)
EDEN_CLIMATE_PROP(continental_cold_lat_min)
EDEN_CLIMATE_PROP(band_tropical_end)
EDEN_CLIMATE_PROP(band_subtropical_start)
EDEN_CLIMATE_PROP(band_subtropical_end)
EDEN_CLIMATE_PROP(band_polar_start)
EDEN_CLIMATE_PROP(biome_spawn_desert)
EDEN_CLIMATE_PROP(biome_spawn_forest)
EDEN_CLIMATE_PROP(biome_spawn_tropical)
EDEN_CLIMATE_PROP(biome_spawn_tundra)
EDEN_CLIMATE_PROP(biome_spawn_grassland)
EDEN_CLIMATE_PROP(day_of_year)
EDEN_CLIMATE_PROP(season_strength)
EDEN_CLIMATE_PROP(season_phase_day)

#undef EDEN_CLIMATE_PROP

void EdenPlanetClimateProfile::_bind_methods() {
	#define BIND_FLOAT(name) \
		ClassDB::bind_method(D_METHOD("set_" #name, "value"), &EdenPlanetClimateProfile::set_##name); \
		ClassDB::bind_method(D_METHOD("get_" #name), &EdenPlanetClimateProfile::get_##name);

	BIND_FLOAT(axis_tilt)
	BIND_FLOAT(tilt_direction)
	BIND_FLOAT(equator_temperature)
	BIND_FLOAT(polar_temperature)
	BIND_FLOAT(altitude_lapse_rate)
	BIND_FLOAT(ocean_temp_moderation)
	BIND_FLOAT(ocean_mean_temp)
	BIND_FLOAT(continental_interior_amp)
	BIND_FLOAT(temp_noise_amplitude)
	BIND_FLOAT(ocean_humidity)
	BIND_FLOAT(interior_humidity)
	BIND_FLOAT(subtropical_dry_factor)
	BIND_FLOAT(humidity_noise_amplitude)
	BIND_FLOAT(evaporation_strength)
	BIND_FLOAT(temp_lat_falloff_start)
	BIND_FLOAT(temp_lat_falloff_end)
	BIND_FLOAT(continental_hot_lat_max)
	BIND_FLOAT(continental_cold_lat_min)
	BIND_FLOAT(band_tropical_end)
	BIND_FLOAT(band_subtropical_start)
	BIND_FLOAT(band_subtropical_end)
	BIND_FLOAT(band_polar_start)
	BIND_FLOAT(biome_spawn_desert)
	BIND_FLOAT(biome_spawn_forest)
	BIND_FLOAT(biome_spawn_tropical)
	BIND_FLOAT(biome_spawn_tundra)
	BIND_FLOAT(biome_spawn_grassland)
	BIND_FLOAT(day_of_year)
	BIND_FLOAT(season_strength)
	BIND_FLOAT(season_phase_day)

	ADD_GROUP("Climate", "");
	// axis_tilt and tilt_direction are intentionally hidden from inspector for now.
	// Seasonal calculations currently use signed latitude directly.
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "equator_temperature"), "set_equator_temperature", "get_equator_temperature");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "polar_temperature"), "set_polar_temperature", "get_polar_temperature");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "altitude_lapse_rate", PROPERTY_HINT_RANGE, "0.0,15.0,0.1"), "set_altitude_lapse_rate", "get_altitude_lapse_rate");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ocean_temp_moderation", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_ocean_temp_moderation", "get_ocean_temp_moderation");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ocean_mean_temp"), "set_ocean_mean_temp", "get_ocean_mean_temp");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continental_interior_amp", PROPERTY_HINT_RANGE, "0.0,15.0,0.5"), "set_continental_interior_amp", "get_continental_interior_amp");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "temp_noise_amplitude", PROPERTY_HINT_RANGE, "0.0,20.0,0.5"), "set_temp_noise_amplitude", "get_temp_noise_amplitude");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ocean_humidity", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_ocean_humidity", "get_ocean_humidity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "interior_humidity", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_interior_humidity", "get_interior_humidity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "subtropical_dry_factor", PROPERTY_HINT_RANGE, "0.0,0.5,0.01"), "set_subtropical_dry_factor", "get_subtropical_dry_factor");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "humidity_noise_amplitude", PROPERTY_HINT_RANGE, "0.0,0.5,0.01"), "set_humidity_noise_amplitude", "get_humidity_noise_amplitude");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "evaporation_strength", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_evaporation_strength", "get_evaporation_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "temp_lat_falloff_start", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_temp_lat_falloff_start", "get_temp_lat_falloff_start");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "temp_lat_falloff_end", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_temp_lat_falloff_end", "get_temp_lat_falloff_end");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continental_hot_lat_max", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_continental_hot_lat_max", "get_continental_hot_lat_max");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continental_cold_lat_min", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_continental_cold_lat_min", "get_continental_cold_lat_min");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "band_tropical_end", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_band_tropical_end", "get_band_tropical_end");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "band_subtropical_start", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_band_subtropical_start", "get_band_subtropical_start");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "band_subtropical_end", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_band_subtropical_end", "get_band_subtropical_end");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "band_polar_start", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_band_polar_start", "get_band_polar_start");
	ADD_GROUP("Biome Spawn Chance", "biome_spawn_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_spawn_desert", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_spawn_desert", "get_biome_spawn_desert");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_spawn_forest", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_spawn_forest", "get_biome_spawn_forest");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_spawn_tropical", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_spawn_tropical", "get_biome_spawn_tropical");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_spawn_tundra", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_spawn_tundra", "get_biome_spawn_tundra");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_spawn_grassland", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_spawn_grassland", "get_biome_spawn_grassland");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "day_of_year", PROPERTY_HINT_RANGE, "0.0,365.0,1.0"), "set_day_of_year", "get_day_of_year");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "season_strength", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_season_strength", "get_season_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "season_phase_day", PROPERTY_HINT_RANGE, "0.0,365.0,1.0"), "set_season_phase_day", "get_season_phase_day");

	#undef BIND_FLOAT
}