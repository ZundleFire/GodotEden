#pragma once

#include "core/io/image.h"
#include "core/math/vector3.h"
#include "core/object/worker_thread_pool.h"
#include "core/templates/local_vector.h"
#include "modules/noise/fastnoise_lite.h"

#include <atomic>

// Planet-wide weather: storm cells that drift with the prevailing winds, spawn over humid ground, and
// grow and fade over their lifetime. Integrates snow cover and ground wetness per map texel from them
// (snow where it is below freezing, melt and drying where it is not), and bakes an octahedral
// map around the planet centre (see texel_dir()): R precipitation, G snow cover, B wetness, A storm cloud.
// Plain class owned by EdenAmbience; directions are unit vectors from the planet centre.
class EdenWeatherSim {
public:
	static constexpr int W = 256;
	static constexpr int H = 256;

	struct Cell {
		Vector3 dir;
		float radius = 0.05f; // angular (radians)
		float intensity = 1.0f;
		float age = 0.0f, life = 600.0f; // seconds of weather time
		float drift = 1.0f; // zonal speed multiplier (+ east, - west)
		float phase = 0.0f;
		bool thunder = false;
		bool dust = false; // a dust storm: no rain or cloud, blowing sand
	};

	struct Params {
		int cell_count = 48;
		float wind_speed = 8.0f; // m/s the cells travel at
		float min_radius = 1500.0f, max_radius = 6000.0f; // metres
		float freeze_temperature = 0.3f; // climate temperature (0..1) below which precipitation is snow
		float snow_rate = 0.5f; // cover per minute of full precipitation
		float melt_rate = 0.25f; // cover per minute, fully above freezing
		float wet_rate = 1.0f; // wetness per minute of full rain
		float dry_rate = 0.12f; // per minute, in sun
		float humidity_bias = 0.88f; // 0 = storms form anywhere, 1 = only over humid ground
		float thunder_chance = 0.5f; // of storms forming in warm, humid air
		float dust_chance = 0.2f; // of new storms being dust storms over hot, dry ground
		// Seasons (EdenCalendar through EdenAmbience): the year's phase, 0 = the northern spring equinox, < 0 = no
		// seasons; the swing in climate temperature toward the poles; the planet's spin axis
		float year_phase = -1.0f;
		float season_strength = 0.25f;
		float wet_season_strength = 0.25f; // tropical wet/dry swing in climate moisture (0..1)
		Vector3 spin_axis = Vector3(0, 1, 0);
		uint32_t seed = 1;
	};

	struct Sample {
		float precipitation = 0.0f;
		float cloud = 0.0f;
		bool thunder = false; // inside a thunder cell's core
		float dust = 0.0f; // inside a dust storm
	};

	Params params;
	// Weather forced around a point (EdenAmbience.weather_override): a storm that follows it, or a clear zone
	// that suppresses the natural cells there, with freezing (snow) or thawing (rain) forced inside
	struct Override {
		enum Mode {
			NONE,
			CLEAR,
			STORM,
		};
		Mode mode = NONE;
		Vector3 dir = Vector3(0, 1, 0);
		float radius = 0.15f; // angular
		float strength = 0.0f; // 0..1, eased by the caller
		Cell cell; // STORM: its shape and kind; dir and radius are the override's
		int temperature = 0; // -1 force freezing, +1 force thaw, 0 as the climate says
	};
	Override override_zone;

	float planet_radius = 40000.0f;
	double time = 0.0;
	LocalVector<Cell> cells; // the first `natural` roam; any after were added by add_cell()
	int natural = -1;

	~EdenWeatherSim();

	// Starts baking the climate (temperature, moisture) grid on a worker thread from `p_generator`'s
	// sample_surface(); weather runs without it (neutral climate) until it is ready.
	void start_climate(Object *p_generator);
	bool is_climate_ready() const { return climate_ready.load(); }

	// Advances cells by `p_dt` weather-seconds.
	void step(float p_dt);
	// Integrates snow/wetness over `p_dt` weather-seconds and rewrites the map image.
	void integrate(float p_dt, const Vector3 &p_sun_dir);
	// The same on a worker thread (with a snapshot of the cells). poll_integrate() returns true once, on the
	// main thread, when a new map is in `image`.
	void integrate_async(float p_dt, const Vector3 &p_sun_dir);
	bool poll_integrate();
	bool is_integrating() const { return integrate_task != WorkerThreadPool::INVALID_TASK_ID; }
	// Largest snow cover or wetness anywhere on the map after the last integrate (shaders skip weather when 0)
	float map_activity = 0.0f;
	Sample sample(const Vector3 &p_dir) const;
	// Snow on the ground: storm snow or the season's lying snow, whichever is deeper (what the map's G channel holds)
	float snow_at(const Vector3 &p_dir) const { return ground_snow.size() == snow.size() ? _texel(ground_snow, p_dir) : _texel(snow, p_dir); }
	float wetness_at(const Vector3 &p_dir) const { return _texel(wet, p_dir); }
	float temperature_at(const Vector3 &p_dir) const { return (climate_ready.load() ? _texel(temperature, p_dir) : 0.5f) + season_offset(p_dir); }
	// Tropical wet and dry seasons: the rain belt follows the overhead sun, so the tropics are wettest in their own
	// summer and driest in their winter (a change to climate moisture); nothing on the equator itself, where the
	// belt passes twice a year, or outside the tropics
	float wet_season_offset(const Vector3 &p_dir) const {
		if (params.year_phase < 0.0f) {
			return 0.0f;
		}
		const float s = p_dir.dot(params.spin_axis);
		const float a = Math::abs(s);
		const float in = CLAMP((a - 0.03f) / 0.17f, 0.0f, 1.0f);
		const float out = CLAMP((a - 0.38f) / 0.14f, 0.0f, 1.0f);
		const float tropics = in * in * (3.0f - 2.0f * in) * (1.0f - out * out * (3.0f - 2.0f * out));
		const float phase = params.year_phase + (s < 0.0f ? 0.5f : 0.0f);
		return params.wet_season_strength * tropics * Math::sin((float)Math::TAU * (phase - 0.1f));
	}
	// The season's change to the climate temperature at a direction: none at the equator, growing toward the poles,
	// opposite in the two hemispheres, warmest about a month after the summer solstice
	float season_offset(const Vector3 &p_dir) const {
		if (params.year_phase < 0.0f) {
			return 0.0f;
		}
		const float s = p_dir.dot(params.spin_axis); // sine of the latitude
		const float phase = params.year_phase + (s < 0.0f ? 0.5f : 0.0f);
		const float a = CLAMP((Math::abs(s) - 0.05f) / 0.85f, 0.0f, 1.0f);
		return params.season_strength * a * a * (3.0f - 2.0f * a) * Math::sin((float)Math::TAU * (phase - 0.08f));
	}

	// A storm of `p_radius` metres at `p_dir`, lasting `p_duration` weather-seconds (tests, gameplay).
	void add_cell(const Vector3 &p_dir, float p_radius, float p_intensity, float p_duration, bool p_thunder, bool p_dust = false);
	void clear_cover();

	Ref<Image> image;
	Ref<FastNoiseLite> noise;

	EdenWeatherSim();

	static Vector3 texel_dir(int p_x, int p_y);
	static Vector2 dir_to_uv(const Vector3 &p_dir);

private:
	uint32_t rng = 1;
	LocalVector<float> temperature, moisture; // climate grid (filled by the worker)
	LocalVector<float> ground_snow; // snow cover including the season's lying snow (see _integrate)
	LocalVector<float> snow, wet, precip, cloud;
	LocalVector<Vector3> dirs;
	std::atomic<bool> climate_ready{ false };
	WorkerThreadPool::TaskID climate_task = WorkerThreadPool::INVALID_TASK_ID;
	ObjectID generator_id;

	float _rand() {
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		return (rng >> 8) * (1.0f / 16777216.0f);
	}
	void _spawn(Cell &r_cell, bool p_random_age);
	float _envelope(const Cell &p_cell) const;
	void _cell_contrib(const Cell &p_cell, const Vector3 &p_dir, float &r_precip, float &r_cloud) const;
	float _texel(const LocalVector<float> &p_grid, const Vector3 &p_dir) const;
	static void _bake_climate(void *p_self);
	float _override_weight(const Override &p_o, const Vector3 &p_dir) const;
	void _integrate(const LocalVector<Cell> &p_cells, const Override &p_o, float p_dt, const Vector3 &p_sun_dir, Ref<Image> &r_image);

	// async integrate
	LocalVector<Cell> job_cells;
	Override job_override;
	float job_dt = 0.0f;
	Vector3 job_sun;
	Ref<Image> job_image;
	WorkerThreadPool::TaskID integrate_task = WorkerThreadPool::INVALID_TASK_ID;
	static void _integrate_job(void *p_self);
};
