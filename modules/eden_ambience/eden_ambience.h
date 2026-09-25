#pragma once

#include "eden_ambience_audio.h"
#include "eden_lightning.h"
#include "eden_weather.h"
#include "scene/3d/node_3d.h"

class GPUParticles3D;
class AudioStreamPlayer;
class DirectionalLight3D;
class ShaderMaterial;
class Shader;
class Environment;
class ImageTexture;
class MeshInstance3D;
class OmniLight3D;

// Lightning colours a strike picks from at random: blue-white, violet, pink, white
static inline PackedColorArray _eden_default_bolt_colors() {
	PackedColorArray c;
	c.push_back(Color(0.65f, 0.75f, 1.0f));
	c.push_back(Color(0.76f, 0.6f, 1.0f));
	c.push_back(Color(1.0f, 0.7f, 0.92f));
	c.push_back(Color(0.95f, 0.93f, 1.0f));
	return c;
}

// X(type, name, default, hint, hint_string, group)
#define EDEN_AMBIENCE_PROPS(X)                                                                        \
	X(bool, look_enabled, true, PROPERTY_HINT_NONE, "", "Look")                                                 \
	X(int, tonemapper, 0, PROPERTY_HINT_ENUM, "Keep,Linear,Reinhard,Filmic,ACES,AgX", "Look")                   \
	X(float, exposure, 0.75f, PROPERTY_HINT_RANGE, "0.05,4.0,0.01", "Look")                                     \
	X(float, white, 1.0f, PROPERTY_HINT_RANGE, "0.5,16.0,0.01", "Look")                                         \
	X(float, contrast, 1.08f, PROPERTY_HINT_RANGE, "0.5,2.0,0.01", "Look")                                      \
	X(float, saturation, 1.2f, PROPERTY_HINT_RANGE, "0.0,2.0,0.01", "Look")                                     \
	X(bool, ssao_enabled, true, PROPERTY_HINT_NONE, "", "Look")                                                 \
	X(float, ssao_intensity, 1.5f, PROPERTY_HINT_RANGE, "0.0,8.0,0.01", "Look")                                 \
	X(float, ssao_radius, 1.5f, PROPERTY_HINT_RANGE, "0.1,8.0,0.01,suffix:m", "Look")                           \
	X(bool, glow_enabled, true, PROPERTY_HINT_NONE, "", "Look")                                                 \
	X(float, glow_intensity, 0.4f, PROPERTY_HINT_RANGE, "0.0,4.0,0.01", "Look")                                 \
	X(float, glow_threshold, 1.2f, PROPERTY_HINT_RANGE, "0.0,8.0,0.01", "Look")                                 \
	X(bool, sun_shadows, true, PROPERTY_HINT_NONE, "", "Look")                                                  \
	X(float, shadow_distance, 150.0f, PROPERTY_HINT_RANGE, "20,4000,1,suffix:m", "Look")                        \
	X(int, shadow_cascades, 1, PROPERTY_HINT_ENUM, "1,2,4", "Look")                                             \
	X(float, storm_exposure_drop, 0.3f, PROPERTY_HINT_RANGE, "0.0,1.0,0.01", "Storm Look")                      \
	X(float, storm_desaturation, 0.3f, PROPERTY_HINT_RANGE, "0.0,1.0,0.01", "Storm Look")                       \
	X(float, storm_sun_dimming, 0.65f, PROPERTY_HINT_RANGE, "0.0,1.0,0.01", "Storm Look")                       \
	X(float, storm_shadow_fade, 0.75f, PROPERTY_HINT_RANGE, "0.0,1.0,0.01", "Storm Look")                       \
	X(float, lightning_brightness, 1.5f, PROPERTY_HINT_RANGE, "0.0,10.0,0.01", "Storm Look")                    \
	X(Color, bolt_color, Color(0.65f, 0.75f, 1.0f), PROPERTY_HINT_COLOR_NO_ALPHA, "", "Lightning")              \
	X(PackedColorArray, bolt_colors, _eden_default_bolt_colors(), PROPERTY_HINT_NONE, "", "Lightning") \
	X(float, bolt_brightness, 25.0f, PROPERTY_HINT_RANGE, "0.0,200.0,0.1", "Lightning")                         \
	X(float, bolt_width, 1.5f, PROPERTY_HINT_RANGE, "0.1,20.0,0.01,suffix:m", "Lightning")                      \
	X(float, lightning_light_energy, 3.0f, PROPERTY_HINT_RANGE, "0.0,500.0,0.1", "Lightning")                  \
	X(float, lightning_light_range, 2500.0f, PROPERTY_HINT_RANGE, "50,20000,1,suffix:m", "Lightning")           \
	X(float, cloud_flash_brightness, 1.8f, PROPERTY_HINT_RANGE, "0.0,20.0,0.01", "Lightning")                   \
	X(float, intra_cloud_chance, 0.4f, PROPERTY_HINT_RANGE, "0.0,1.0,0.01", "Lightning")                        \
	X(float, lightning_view_bias, 0.55f, PROPERTY_HINT_RANGE, "0.0,1.0,0.01", "Lightning")                      \
	X(float, lightning_min_distance, 300.0f, PROPERTY_HINT_RANGE, "20,20000,1,suffix:m", "Lightning")           \
	X(float, lightning_max_distance, 6000.0f, PROPERTY_HINT_RANGE, "100,50000,1,suffix:m", "Lightning")         \
	X(bool, fog_enabled, true, PROPERTY_HINT_NONE, "", "Fog")                                                   \
	X(float, haze_density, 0.0003f, PROPERTY_HINT_RANGE, "0.0,0.01,0.00001", "Fog")                             \
	X(float, haze_height, 300.0f, PROPERTY_HINT_RANGE, "10,4000,1,suffix:m", "Fog")                             \
	X(float, mist_density, 0.003f, PROPERTY_HINT_RANGE, "0.0,0.05,0.00001", "Fog")                              \
	X(float, mist_height, 35.0f, PROPERTY_HINT_RANGE, "2,1000,1,suffix:m", "Fog")                               \
	X(float, mist_depth, 15.0f, PROPERTY_HINT_RANGE, "-200,500,1,suffix:m", "Fog")                              \
	X(float, storm_fog, 0.8f, PROPERTY_HINT_RANGE, "0.0,2.0,0.01", "Fog")                                       \
	X(float, dust_fog, 1.5f, PROPERTY_HINT_RANGE, "0.0,4.0,0.01", "Fog")                                        \
	X(bool, biome_fog_enabled, true, PROPERTY_HINT_NONE, "", "Biome Fog")                                       \
	X(float, humid_haze, 1.6f, PROPERTY_HINT_RANGE, "0.0,5.0,0.01", "Biome Fog")                                \
	X(Color, humid_haze_color, Color(0.8f, 0.87f, 0.9f), PROPERTY_HINT_COLOR_NO_ALPHA, "", "Biome Fog")         \
	X(float, dry_haze, 1.3f, PROPERTY_HINT_RANGE, "0.0,5.0,0.01", "Biome Fog")                                  \
	X(Color, dry_haze_color, Color(0.93f, 0.8f, 0.6f), PROPERTY_HINT_COLOR_NO_ALPHA, "", "Biome Fog")           \
	X(float, cold_haze, 0.6f, PROPERTY_HINT_RANGE, "0.0,5.0,0.01", "Biome Fog")                                 \
	X(Color, cold_haze_color, Color(0.86f, 0.91f, 1.0f), PROPERTY_HINT_COLOR_NO_ALPHA, "", "Biome Fog")         \
	X(float, coast_haze, 1.4f, PROPERTY_HINT_RANGE, "0.0,5.0,0.01", "Biome Fog")                                \
	X(Color, coast_haze_color, Color(0.84f, 0.88f, 0.9f), PROPERTY_HINT_COLOR_NO_ALPHA, "", "Biome Fog")        \
	X(float, wind_speed, 2.0f, PROPERTY_HINT_RANGE, "0.0,30.0,0.1,suffix:m/s", "Wind")                          \
	X(float, wind_heading, 30.0f, PROPERTY_HINT_RANGE, "0,360,1,degrees", "Wind")                               \
	X(float, gustiness, 0.5f, PROPERTY_HINT_RANGE, "0.0,1.0,0.01", "Wind")                                      \
	X(bool, weather_enabled, true, PROPERTY_HINT_NONE, "", "Weather")                                           \
	X(int, weather_seed, 1, PROPERTY_HINT_RANGE, "0,100000,1", "Weather")                                       \
	X(int, storm_count, 48, PROPERTY_HINT_RANGE, "0,256,1", "Weather")                                          \
	X(float, storm_speed, 8.0f, PROPERTY_HINT_RANGE, "0.0,60.0,0.1,suffix:m/s", "Weather")                      \
	X(float, storm_min_radius, 1500.0f, PROPERTY_HINT_RANGE, "100,50000,10,suffix:m", "Weather")                \
	X(float, storm_max_radius, 6000.0f, PROPERTY_HINT_RANGE, "100,50000,10,suffix:m", "Weather")                \
	X(float, weather_time_scale, 1.0f, PROPERTY_HINT_RANGE, "0.0,100.0,0.01,or_greater", "Weather")             \
	X(float, freeze_temperature, 0.3f, PROPERTY_HINT_RANGE, "0.0,1.0,0.01", "Weather")                          \
	X(float, snow_rate, 0.5f, PROPERTY_HINT_RANGE, "0.0,10.0,0.01,suffix:/min", "Weather")                      \
	X(float, melt_rate, 0.25f, PROPERTY_HINT_RANGE, "0.0,10.0,0.01,suffix:/min", "Weather")                     \
	X(float, snow_max_depth, 0.35f, PROPERTY_HINT_RANGE, "0.0,3.0,0.01,suffix:m", "Weather")                    \
	X(bool, lightning_enabled, true, PROPERTY_HINT_NONE, "", "Weather")                                         \
	X(float, storm_humidity_bias, 0.88f, PROPERTY_HINT_RANGE, "0.0,1.0,0.01", "Weather")                        \
	X(float, thunder_chance, 0.5f, PROPERTY_HINT_RANGE, "0.0,1.0,0.01", "Weather")                              \
	X(float, dust_storm_chance, 0.2f, PROPERTY_HINT_RANGE, "0.0,1.0,0.01", "Weather")                           \
	X(float, lightning_frequency, 1.0f, PROPERTY_HINT_RANGE, "0.0,10.0,0.01", "Weather")                        \
	X(float, storm_wind_boost, 1.5f, PROPERTY_HINT_RANGE, "0.0,10.0,0.01", "Weather")                           \
	X(float, overcast_radius, 15000.0f, PROPERTY_HINT_RANGE, "1000,100000,100,suffix:m", "Weather")             \
	X(float, storm_cloud_coverage, 0.6f, PROPERTY_HINT_RANGE, "0.0,1.0,0.01", "Weather")                        \
	X(float, storm_cloud_darkening, 0.55f, PROPERTY_HINT_RANGE, "0.0,1.0,0.01", "Weather")                      \
	X(float, wet_rate, 1.0f, PROPERTY_HINT_RANGE, "0.0,10.0,0.01,suffix:/min", "Weather")                       \
	X(float, dry_rate, 0.12f, PROPERTY_HINT_RANGE, "0.0,10.0,0.01,suffix:/min", "Weather")                      \
	X(Color, snow_color, Color(0.74f, 0.77f, 0.82f), PROPERTY_HINT_COLOR_NO_ALPHA, "", "Weather")               \
	X(float, wet_darkening, 0.4f, PROPERTY_HINT_RANGE, "0.0,1.0,0.01", "Weather")                               \
	X(bool, particles_enabled, true, PROPERTY_HINT_NONE, "", "Particles")                                       \
	X(float, motes_amount, 1.0f, PROPERTY_HINT_RANGE, "0.0,2.0,0.01", "Particles")                              \
	X(float, fireflies_amount, 1.0f, PROPERTY_HINT_RANGE, "0.0,2.0,0.01", "Particles")                          \
	X(float, snow_amount, 1.0f, PROPERTY_HINT_RANGE, "0.0,2.0,0.01", "Particles")                               \
	X(float, rain_amount, 0.0f, PROPERTY_HINT_RANGE, "0.0,1.0,0.01", "Particles")                               \
	X(float, particle_range, 1.0f, PROPERTY_HINT_RANGE, "0.25,4.0,0.01", "Particles")                           \
	X(float, motes_size, 0.04f, PROPERTY_HINT_RANGE, "0.005,0.5,0.001,suffix:m", "Particles")                   \
	X(float, motes_brightness, 1.0f, PROPERTY_HINT_RANGE, "0.0,4.0,0.01", "Particles")                          \
	X(float, fireflies_brightness, 7.0f, PROPERTY_HINT_RANGE, "0.0,20.0,0.1", "Particles")                      \
	X(float, snowflake_size, 0.09f, PROPERTY_HINT_RANGE, "0.01,0.5,0.001,suffix:m", "Particles")                \
	X(float, rain_streak_size, 0.14f, PROPERTY_HINT_RANGE, "0.01,1.0,0.001,suffix:m", "Particles")              \
	X(float, rain_opacity, 0.55f, PROPERTY_HINT_RANGE, "0.0,1.0,0.01", "Particles")                             \
	X(float, leaves_amount, 1.0f, PROPERTY_HINT_RANGE, "0.0,2.0,0.01", "Particles")                             \
	X(float, leaves_size, 0.18f, PROPERTY_HINT_RANGE, "0.02,1.0,0.001,suffix:m", "Particles")                   \
	X(float, dust_amount, 1.0f, PROPERTY_HINT_RANGE, "0.0,2.0,0.01", "Particles")                               \
	X(float, dust_size, 1.6f, PROPERTY_HINT_RANGE, "0.1,8.0,0.01,suffix:m", "Particles")                        \
	X(bool, biome_effects_enabled, true, PROPERTY_HINT_NONE, "", "Biome")                                       \
	X(Color, pollen_color, Color(1.0f, 0.92f, 0.7f), PROPERTY_HINT_COLOR_NO_ALPHA, "", "Biome")                 \
	X(Color, dust_mote_color, Color(0.95f, 0.8f, 0.6f), PROPERTY_HINT_COLOR_NO_ALPHA, "", "Biome")              \
	X(Color, ice_crystal_color, Color(0.85f, 0.93f, 1.0f), PROPERTY_HINT_COLOR_NO_ALPHA, "", "Biome")           \
	X(Color, leaf_color_summer, Color(0.36f, 0.5f, 0.16f), PROPERTY_HINT_COLOR_NO_ALPHA, "", "Biome")           \
	X(Color, leaf_color_autumn, Color(0.86f, 0.45f, 0.12f), PROPERTY_HINT_COLOR_NO_ALPHA, "", "Biome")          \
	X(Color, dust_color, Color(0.78f, 0.65f, 0.45f), PROPERTY_HINT_COLOR_NO_ALPHA, "", "Biome")                 \
	X(float, dust_wind_threshold, 3.0f, PROPERTY_HINT_RANGE, "0.0,30.0,0.1,suffix:m/s", "Biome")                \
	X(bool, audio_enabled, true, PROPERTY_HINT_NONE, "", "Audio")                                               \
	X(bool, audio_in_editor, false, PROPERTY_HINT_NONE, "", "Audio")                                            \
	X(float, volume_db, -6.0f, PROPERTY_HINT_RANGE, "-60,12,0.1,suffix:dB", "Audio")                            \
	X(float, wind_volume, 1.0f, PROPERTY_HINT_RANGE, "0.0,2.0,0.01", "Audio")                                   \
	X(float, leaves_volume, 1.0f, PROPERTY_HINT_RANGE, "0.0,2.0,0.01", "Audio")                                 \
	X(float, surf_volume, 1.0f, PROPERTY_HINT_RANGE, "0.0,2.0,0.01", "Audio")                                   \
	X(float, birds_volume, 0.0f, PROPERTY_HINT_RANGE, "0.0,2.0,0.01", "Audio")                                  \
	X(float, crickets_volume, 0.0f, PROPERTY_HINT_RANGE, "0.0,2.0,0.01", "Audio")                               \
	X(float, rain_volume, 1.0f, PROPERTY_HINT_RANGE, "0.0,2.0,0.01", "Audio")                                   \
	X(float, thunder_volume, 1.0f, PROPERTY_HINT_RANGE, "0.0,2.0,0.01", "Audio")                                \
	X(bool, spatial_audio, true, PROPERTY_HINT_NONE, "", "Spatial Audio")                                       \
	X(float, surf_range, 350.0f, PROPERTY_HINT_RANGE, "20,2000,1,suffix:m", "Spatial Audio")                    \
	X(float, surf_unit_size, 25.0f, PROPERTY_HINT_RANGE, "1,200,0.1", "Spatial Audio")                          \
	X(float, forest_sound_range, 120.0f, PROPERTY_HINT_RANGE, "10,1000,1,suffix:m", "Spatial Audio")            \
	X(float, leaves_unit_size, 10.0f, PROPERTY_HINT_RANGE, "1,200,0.1", "Spatial Audio")

// Environmental ambience for a planet: scene look (exposure, grading, SSAO, glow, sun shadows),
// camera-following particles (motes, fireflies, snow, rain, leaves, dust), regional weather and a procedural soundscape,
// all driven by the climate under the camera and the time of day.
// Place under the planet's VoxelLodTerrain (planet_path defaults to the parent); it finds the
// EdenPlanetAtmosphere next to it for the sun.
class EdenAmbience : public Node3D {
	GDCLASS(EdenAmbience, Node3D);

public:
	enum Effect {
		FX_MOTES,
		FX_FIREFLIES,
		FX_SNOW,
		FX_RAIN,
		FX_LEAVES,
		FX_DUST,
		FX_MAX,
	};

private:
#define EDEN_AMB_MEMBER(m_type, m_name, m_default, m_hint, m_hint_string, m_group) m_type m_name = m_default;
	EDEN_AMBIENCE_PROPS(EDEN_AMB_MEMBER)
#undef EDEN_AMB_MEMBER

	NodePath planet_path = NodePath("..");
	NodePath atmosphere_path;

	GPUParticles3D *fx[FX_MAX] = {};
	Ref<ShaderMaterial> fx_process[FX_MAX];
	Ref<ShaderMaterial> fx_draw[FX_MAX];
	Ref<Shader> process_shader;
	Ref<Shader> draw_shader;
	AudioStreamPlayer *player = nullptr;
	Ref<AudioStreamEdenAmbience> soundscape;

	bool look_dirty = true;
	bool fx_dirty = true; // particle sizes/ranges/colours to push
	ObjectID look_env;
	float survey_timer = 0.0f;
	float mist = -1.0f; // eased; < 0 until first update
	float mist_ground = 0.0f;
	ObjectID fog_atmo; // atmosphere whose fog is being driven
	Vector3 fog_saved; // its own density, height falloff, base altitude
	float sun_energy_saved = 1.0f;
	Color fog_albedo_saved = Color(0.85f, 0.88f, 0.92f);
	float fog_sun_saved = 0.9f;
	Color fog_tint = Color(0, 0, 0, -1); // eased biome tint; alpha < 0 until the first update
	Vector4 biome_weights; // desert, tropical, cold, coast at the camera
	double time = 0.0;

	// Weather
	EdenWeatherSim weather;
	Ref<ImageTexture> weather_texture;
	float weather_timer = 0.0f;
	float weather_accum = 0.0f; // weather-seconds since the last map update
	bool weather_started = false;
	static inline bool globals_added = false; // runtime-registered shader globals (once per process)
	EdenWeatherSim::Sample local; // at the camera
	float local_snow = 0.0f, local_wet = 0.0f, local_freezing = 0.0f;
	float flash = 0.0f; // lightning, decays in a few frames
	float strike_timer = 3.0f;
	int lightning_strikes = 0;

	// Visible lightning: a couple of strikes can overlap
	struct Strike {
		MeshInstance3D *mesh = nullptr;
		OmniLight3D *light = nullptr;
		Ref<ShaderMaterial> material;
		EdenLightningBolt bolt;
		float t = -1.0f; // < 0: idle
		Vector3 top, ground;
		float distance = 0.0f;
		bool cloud_only = false;
		Color color;
	};
	static constexpr int MAX_BOLTS = 2;
	Strike bolts[MAX_BOLTS];
	Ref<Shader> bolt_shader;
	RandomPCG bolt_rng;
	Vector3 cam_forward = Vector3(0, 0, -1);
	float cloud_flash = 0.0f;
	Color cloud_flash_color;

	// Spatial audio: surf from the nearest shorelines, rustling from the nearest dense forest. Plain
	// AudioStreamPlayers panned and attenuated here from the camera: AudioStreamPlayer3D children inside the
	// edited scene made the editor's scene dock error on shutdown.
	struct Emitter {
		AudioStreamPlayer *player = nullptr;
		Ref<AudioStreamEdenAmbience> stream;
		AudioStreamEdenAmbience::Layer layer = AudioStreamEdenAmbience::LAYER_SURF;
		Vector3 pos, target;
		float level = 0.0f, target_level = 0.0f;
	};
	static constexpr int SURF_EMITTERS = 3;
	static constexpr int LEAF_EMITTERS = 4;
	Emitter surf_emitters[SURF_EMITTERS];
	Emitter leaf_emitters[LEAF_EMITTERS];
	float source_timer = 0.0f;
	float forest_here = -1.0f; // forest density within ~25 m (< 0: no foliage node to ask)
	float applied_exposure = -1.0f, applied_saturation = -1.0f;

	// Climate at the camera, refreshed by _survey()
	struct State {
		Vector3 center;
		float planet_radius = 0.0f;
		float ground_height = 0.0f; // above sea level
		float altitude = 1e9f; // camera above ground or sea
		float temperature = 0.5f;
		float moisture = 0.5f;
		float water = 0.0f; // share of nearby samples that are sea
		float day = 1.0f;
		float night = 0.0f;
		float sun_elevation = 0.5f; // sine
		bool valid = false;
	} state;

	Node3D *_get_planet() const;
	Node *_get_atmosphere() const;
	bool _get_camera(Vector3 &r_pos, Basis *r_basis = nullptr) const;
	void _build();
	void _survey(const Vector3 &p_cam);
	void _update(double p_delta);
	void _apply_look();
	void _restore_fog();
	void _apply_fx_params();
	bool _pick_strike_point(Vector3 &r_ground) const;
	void _strike(const Vector3 &p_ground, bool p_cloud_only);
	void _update_bolts(double p_delta, const Vector3 &p_cam);
	void _find_sound_sources(const Vector3 &p_cam);
	void _update_emitters(double p_delta, bool p_play, float p_wind, const Vector3 &p_cam, const Basis &p_cam_basis);
	void _update_weather(double p_delta, const Vector3 &p_cam, const Vector3 &p_sun);
	DirectionalLight3D *_find_sun(Node *p_atmosphere) const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
#define EDEN_AMB_ACCESSORS(m_type, m_name, m_default, m_hint, m_hint_string, m_group) \
	void set_##m_name(m_type p_value);                                                  \
	m_type get_##m_name() const;
	EDEN_AMBIENCE_PROPS(EDEN_AMB_ACCESSORS)
#undef EDEN_AMB_ACCESSORS

	void set_planet_path(const NodePath &p_path);
	NodePath get_planet_path() const;
	void set_atmosphere_path(const NodePath &p_path);
	NodePath get_atmosphere_path() const;

	Ref<AudioStreamEdenAmbience> get_soundscape() const;
	GPUParticles3D *get_effect(Effect p_effect) const;
	Dictionary get_debug_state() const;

	// Weather at a world position: precipitation, cloud, snow, wetness (0..1), temperature, thunder.
	Dictionary get_weather_at(const Vector3 &p_world_position) const;
	// A storm centred at a world position (radius in metres, duration in weather-seconds).
	void add_storm(const Vector3 &p_world_position, float p_radius, float p_intensity, float p_duration, bool p_thunder, bool p_dust);
	void clear_snow_and_wetness();
	// A lightning strike at a world position on the ground (Vector3() = somewhere around the camera, as the
	// storms pick), or a flash inside the cloud above it.
	void strike_lightning(const Vector3 &p_world_position, bool p_cloud_only);
	Ref<ImageTexture> get_weather_texture() const;

	EdenAmbience();
};

VARIANT_ENUM_CAST(EdenAmbience::Effect);
