#include "eden_weather.h"

#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"

static inline float _smoothstep(float a, float b, float x) {
	const float t = CLAMP((x - a) / (b - a), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

// Octahedral map, +Y at the centre, -Y at the corners (matches eden_weather.gdshaderinc). No trig to
// look up: shaders sample it per vertex over millions of grass blades, where atan/acos cost ~2 ms.
static inline float _sign_nz(float v) {
	return v >= 0.0f ? 1.0f : -1.0f;
}

Vector3 EdenWeatherSim::texel_dir(int p_x, int p_y) {
	const float ex = (p_x + 0.5f) / W * 2.0f - 1.0f;
	const float ez = (p_y + 0.5f) / H * 2.0f - 1.0f;
	Vector3 d(ex, 1.0f - Math::abs(ex) - Math::abs(ez), ez);
	if (d.y < 0.0f) {
		const float x = d.x;
		d.x = (1.0f - Math::abs(d.z)) * _sign_nz(x);
		d.z = (1.0f - Math::abs(x)) * _sign_nz(d.z);
	}
	return d.normalized();
}

Vector2 EdenWeatherSim::dir_to_uv(const Vector3 &p_dir) {
	const float l1 = Math::abs(p_dir.x) + Math::abs(p_dir.y) + Math::abs(p_dir.z);
	const Vector3 d = p_dir / MAX(l1, 1e-6f);
	Vector2 e(d.x, d.z);
	if (d.y < 0.0f) {
		e = Vector2((1.0f - Math::abs(d.z)) * _sign_nz(d.x), (1.0f - Math::abs(d.x)) * _sign_nz(d.z));
	}
	return e * 0.5f + Vector2(0.5f, 0.5f);
}

EdenWeatherSim::EdenWeatherSim() {
	noise.instantiate();
	noise->set_noise_type(FastNoiseLite::TYPE_SIMPLEX_SMOOTH);
	noise->set_frequency(1.0f);
	noise->set_fractal_octaves(3);
}

EdenWeatherSim::~EdenWeatherSim() {
	if (climate_task != WorkerThreadPool::INVALID_TASK_ID) {
		WorkerThreadPool::get_singleton()->wait_for_task_completion(climate_task);
	}
	if (integrate_task != WorkerThreadPool::INVALID_TASK_ID) {
		WorkerThreadPool::get_singleton()->wait_for_task_completion(integrate_task);
	}
}

// 1 inside the override zone, fading to 0 over its outer 30%
float EdenWeatherSim::_override_weight(const Override &p_o, const Vector3 &p_dir) const {
	if (p_o.mode == Override::NONE || p_o.strength <= 0.0f) {
		return 0.0f;
	}
	const float chord = Math::sqrt(MAX(2.0f - 2.0f * p_dir.dot(p_o.dir), 0.0f));
	return (1.0f - _smoothstep(p_o.radius * 0.7f, p_o.radius, chord)) * p_o.strength;
}

static void _ensure_grids(LocalVector<Vector3> &r_dirs, LocalVector<float> *p_grids[], int p_count) {
	if (r_dirs.size() == (uint32_t)(EdenWeatherSim::W * EdenWeatherSim::H)) {
		return;
	}
	r_dirs.resize(EdenWeatherSim::W * EdenWeatherSim::H);
	for (int y = 0; y < EdenWeatherSim::H; y++) {
		for (int x = 0; x < EdenWeatherSim::W; x++) {
			r_dirs[y * EdenWeatherSim::W + x] = EdenWeatherSim::texel_dir(x, y);
		}
	}
	for (int i = 0; i < p_count; i++) {
		p_grids[i]->resize(r_dirs.size());
		for (float &v : *p_grids[i]) {
			v = 0.0f;
		}
	}
}

void EdenWeatherSim::_bake_climate(void *p_self) {
	EdenWeatherSim *self = (EdenWeatherSim *)p_self;
	Object *gen = ObjectDB::get_instance(self->generator_id);
	if (gen != nullptr) {
		for (uint32_t i = 0; i < self->dirs.size(); i++) {
			const Dictionary s = gen->call("sample_surface", self->dirs[i]);
			self->temperature[i] = s.get("temperature", 0.5f);
			self->moisture[i] = s.get("moisture", 0.5f);
		}
	}
	self->climate_ready.store(gen != nullptr);
}

void EdenWeatherSim::start_climate(Object *p_generator) {
	LocalVector<float> *grids[] = { &temperature, &moisture, &snow, &wet, &precip, &cloud, &ground_snow };
	_ensure_grids(dirs, grids, 7);
	if (climate_task != WorkerThreadPool::INVALID_TASK_ID || p_generator == nullptr || !p_generator->has_method("sample_surface")) {
		return;
	}
	generator_id = p_generator->get_instance_id();
	// ponytail: the generator Resource must outlive the bake; EdenAmbience's planet holds it.
	climate_task = WorkerThreadPool::get_singleton()->add_native_task(&EdenWeatherSim::_bake_climate, this, false, "EdenWeather climate");
}

// ---------------------------------------------------------------------------------------------
// Cells

void EdenWeatherSim::_spawn(Cell &r_cell, bool p_random_age) {
	const bool ready = climate_ready.load();
	Vector3 dir;
	float m = 0.5f, t = 0.5f;
	// Rejection-sample a spot: rain and snow storms where the air is wet, dust storms over hot, dry
	// ground (and a would-be dust storm that finds no desert becomes an ordinary one)
	bool dust = _rand() < params.dust_chance;
	bool found = false;
	for (int tries = 0; tries < 24 && !found; tries++) {
		const float z = _rand() * 2.0f - 1.0f;
		const float phi = _rand() * Math::TAU;
		const float r = Math::sqrt(MAX(0.0f, 1.0f - z * z));
		dir = Vector3(r * Math::cos(phi), z, r * Math::sin(phi));
		m = CLAMP((ready ? _texel(moisture, dir) : 0.5f) + wet_season_offset(dir), 0.0f, 1.0f);
		t = (ready ? _texel(temperature, dir) : 0.5f) + season_offset(dir);
		const float dry = (1.0f - m) * (1.0f - m) * _smoothstep(0.4f, 0.7f, t);
		found = _rand() < (dust ? dry : (1.0f - params.humidity_bias) + params.humidity_bias * m * m);
	}
	dust = dust && found && ready;
	r_cell.dir = dir;
	r_cell.radius = Math::lerp(params.min_radius, params.max_radius, Math::pow(_rand(), 1.5f)) / MAX(planet_radius, 1.0f);
	r_cell.intensity = CLAMP(0.35f + _rand() * (0.35f + m * 0.6f), 0.0f, 1.0f);
	r_cell.life = Math::lerp(360.0f, 1200.0f, _rand());
	r_cell.age = p_random_age ? _rand() * r_cell.life : 0.0f;
	r_cell.dust = dust;
	r_cell.thunder = !dust && t > 0.55f && m > 0.5f && _rand() < params.thunder_chance;
	r_cell.phase = _rand() * Math::TAU;
	// Prevailing winds: easterly trades in the tropics, westerlies at mid latitudes, polar easterlies
	const float lat = Math::abs(Math::asin(CLAMP(dir.y, -1.0f, 1.0f)));
	const float band = lat < Math::deg_to_rad(30.0f) ? -1.0f : (lat < Math::deg_to_rad(62.0f) ? 1.0f : -0.6f);
	r_cell.drift = band * Math::lerp(0.6f, 1.4f, _rand());
}

void EdenWeatherSim::add_cell(const Vector3 &p_dir, float p_radius, float p_intensity, float p_duration, bool p_thunder, bool p_dust) {
	Cell c;
	_spawn(c, false);
	c.dir = p_dir.normalized();
	c.radius = p_radius / MAX(planet_radius, 1.0f);
	c.intensity = CLAMP(p_intensity, 0.0f, 1.0f);
	c.life = MAX(p_duration, 1.0f);
	// Starts at full strength (skips the build-up) and stays put
	c.age = c.life * 0.15f;
	c.drift = 0.0f;
	c.thunder = p_thunder && !p_dust;
	c.dust = p_dust;
	cells.push_back(c);
}

float EdenWeatherSim::_envelope(const Cell &p_cell) const {
	const float x = p_cell.age / p_cell.life;
	return _smoothstep(0.0f, 0.15f, x) * (1.0f - _smoothstep(0.8f, 1.0f, x));
}

void EdenWeatherSim::_cell_contrib(const Cell &p_cell, const Vector3 &p_dir, float &r_precip, float &r_cloud) const {
	r_precip = 0.0f;
	r_cloud = 0.0f;
	const float env = _envelope(p_cell);
	const float chord2 = 2.0f - 2.0f * p_dir.dot(p_cell.dir);
	const float reach = p_cell.radius * 1.8f;
	if (env <= 0.0f || chord2 > reach * reach) {
		return;
	}
	// Noise fixed to the planet (slowly morphing), so a cell's ragged edge and its patches of heavier
	// rain change as it travels through it
	const Vector3 q = p_dir * 18.0f + Vector3(p_cell.phase, 0.0f, (float)time * 0.004f);
	const float edge = noise->get_noise_3dv(q);
	const float cores = noise->get_noise_3dv(q * 2.3f + Vector3(17.0f, 5.0f, 0.0f));
	const float x = Math::sqrt(MAX(chord2, 0.0f)) / p_cell.radius * (1.0f + 0.5f * edge);
	// Rain comes in heavier and lighter patches; a dust storm is a more uniform wall
	const float patch = p_cell.dust ? 0.9f : 0.45f + 0.55f * _smoothstep(-0.4f, 0.5f, cores);
	r_precip = (1.0f - _smoothstep(0.3f, 1.0f, x)) * patch * p_cell.intensity * env;
	r_cloud = (1.0f - _smoothstep(0.4f, 1.7f, x)) * env * (0.5f + 0.5f * p_cell.intensity);
}

void EdenWeatherSim::step(float p_dt) {
	if (natural != params.cell_count) {
		// (Re)seed the natural cells at the front; add_cell() storms after them are kept
		rng = params.seed * 2654435761u + 1u;
		LocalVector<Cell> fresh;
		fresh.resize(MAX(params.cell_count, 0));
		for (Cell &c : fresh) {
			_spawn(c, true);
		}
		for (uint32_t i = MAX(natural, 0); i < cells.size(); i++) {
			fresh.push_back(cells[i]);
		}
		cells = fresh;
		natural = params.cell_count;
	}
	time += p_dt;
	const float omega = params.wind_speed / MAX(planet_radius, 1.0f);
	for (uint32_t i = 0; i < cells.size(); i++) {
		Cell &c = cells[i];
		c.age += p_dt;
		if (c.age >= c.life) {
			if (i >= (uint32_t)natural) { // added by add_cell(): gone when done
				cells.remove_at_unordered(i);
				i--;
				continue;
			}
			_spawn(c, false);
			continue;
		}
		// Zonal drift: rotate about the planet's Y axis
		Vector3 east(c.dir.z, 0.0f, -c.dir.x);
		const float el = east.length();
		if (el > 1e-4f) {
			c.dir = (c.dir + east / el * (omega * c.drift * p_dt)).normalized();
		}
	}
}

EdenWeatherSim::Sample EdenWeatherSim::sample(const Vector3 &p_dir) const {
	float keep_p = 1.0f, keep_c = 1.0f, keep_d = 1.0f;
	Sample s;
	const Override &o = override_zone;
	const float ow = _override_weight(o, p_dir);
	const int extra = o.mode == Override::STORM && ow > 0.0f ? 1 : 0;
	for (uint32_t k = 0; k < cells.size() + extra; k++) {
		const Cell &c = k < cells.size() ? cells[k] : o.cell;
		float p, cl;
		_cell_contrib(c, p_dir, p, cl);
		if (k < cells.size() && o.mode == Override::CLEAR) {
			p *= 1.0f - ow;
			cl *= 1.0f - ow;
		} else if (k >= cells.size()) {
			p *= o.strength;
			cl *= o.strength;
		}
		if (c.dust) {
			keep_d *= 1.0f - p;
			continue;
		}
		keep_p *= 1.0f - p;
		keep_c *= 1.0f - cl;
		if (c.thunder && p > 0.35f) {
			s.thunder = true;
		}
	}
	s.precipitation = 1.0f - keep_p;
	s.cloud = 1.0f - keep_c;
	s.dust = 1.0f - keep_d;
	return s;
}

// ---------------------------------------------------------------------------------------------
// Map

float EdenWeatherSim::_texel(const LocalVector<float> &p_grid, const Vector3 &p_dir) const {
	if (p_grid.size() != (uint32_t)(W * H)) {
		return 0.0f;
	}
	const Vector2 uv = dir_to_uv(p_dir);
	// Bilinear, clamped at the map's edges (like the shaders' sampler)
	const float fx = CLAMP(uv.x * W - 0.5f, 0.0f, W - 1.0f), fy = CLAMP(uv.y * H - 0.5f, 0.0f, H - 1.0f);
	const int x0 = (int)fx, y0 = (int)fy;
	const float tx = fx - x0, ty = fy - y0;
	const int y1 = MIN(y0 + 1, H - 1);
	const int xa = x0, xb = MIN(x0 + 1, W - 1);
	const float a = Math::lerp(p_grid[y0 * W + xa], p_grid[y0 * W + xb], tx);
	const float b = Math::lerp(p_grid[y1 * W + xa], p_grid[y1 * W + xb], tx);
	return Math::lerp(a, b, ty);
}

void EdenWeatherSim::clear_cover() {
	for (float &v : snow) {
		v = 0.0f;
	}
	for (float &v : wet) {
		v = 0.0f;
	}
}

void EdenWeatherSim::integrate(float p_dt, const Vector3 &p_sun_dir) {
	if (integrate_task != WorkerThreadPool::INVALID_TASK_ID) {
		WorkerThreadPool::get_singleton()->wait_for_task_completion(integrate_task);
		integrate_task = WorkerThreadPool::INVALID_TASK_ID;
	}
	_integrate(cells, override_zone, p_dt, p_sun_dir, image);
}

void EdenWeatherSim::integrate_async(float p_dt, const Vector3 &p_sun_dir) {
	if (integrate_task != WorkerThreadPool::INVALID_TASK_ID) {
		return; // the previous one is still running: its dt is folded into the next
	}
	LocalVector<float> *grids[] = { &temperature, &moisture, &snow, &wet, &precip, &cloud, &ground_snow };
	_ensure_grids(dirs, grids, 7);
	job_cells = cells;
	job_override = override_zone;
	job_dt = p_dt;
	job_sun = p_sun_dir;
	integrate_task = WorkerThreadPool::get_singleton()->add_native_task(&EdenWeatherSim::_integrate_job, this, false, "EdenWeather integrate");
}

void EdenWeatherSim::_integrate_job(void *p_self) {
	EdenWeatherSim *self = (EdenWeatherSim *)p_self;
	self->_integrate(self->job_cells, self->job_override, self->job_dt, self->job_sun, self->job_image);
}

bool EdenWeatherSim::poll_integrate() {
	if (integrate_task == WorkerThreadPool::INVALID_TASK_ID || !WorkerThreadPool::get_singleton()->is_task_completed(integrate_task)) {
		return false;
	}
	WorkerThreadPool::get_singleton()->wait_for_task_completion(integrate_task);
	integrate_task = WorkerThreadPool::INVALID_TASK_ID;
	image = job_image;
	return image.is_valid();
}

// Runs on the main thread (integrate) or a worker (integrate_async). The snow/wet grids are shared with
// snow_at()/wetness_at() on the main thread: those may read a texel mid-update, which only means a value
// from this pass or the last -- aligned float stores don't tear.
void EdenWeatherSim::_integrate(const LocalVector<Cell> &p_cells, const Override &p_o, float p_dt, const Vector3 &p_sun_dir, Ref<Image> &r_image) {
	LocalVector<float> *grids[] = { &temperature, &moisture, &snow, &wet, &precip, &cloud, &ground_snow };
	_ensure_grids(dirs, grids, 7);
	const int n = W * H;
	for (int i = 0; i < n; i++) {
		precip[i] = 1.0f; // running products of (1 - contribution)
		cloud[i] = 1.0f;
	}
	// Each cell only touches the texels in the map-space bounding box of its cap. Where the cap crosses
	// a fold of the map (the equator, or the x/z = 0 planes below it) that box would be wrong: scan it all.
	// The override storm rides along as one more cell (at the override's strength)
	Cell ov = p_o.cell;
	ov.intensity *= p_o.strength;
	const int extra = p_o.mode == Override::STORM && p_o.strength > 0.0f ? 1 : 0;
	for (uint32_t k = 0; k < p_cells.size() + extra; k++) {
		const Cell &c = k < p_cells.size() ? p_cells[k] : ov;
		if (_envelope(c) <= 0.0f || c.dust) { // dust storms leave no rain, snow or cloud on the map
			continue;
		}
		const float reach = c.radius * 1.8f;
		int x0 = 0, x1 = W - 1, y0 = 0, y1 = H - 1;
		const bool folds = Math::abs(c.dir.y) < reach * 1.2f || (c.dir.y < 0.0f && (Math::abs(c.dir.x) < reach * 1.2f || Math::abs(c.dir.z) < reach * 1.2f));
		if (!folds) {
			const Vector3 t = c.dir.cross(Math::abs(c.dir.y) < 0.9f ? Vector3(0, 1, 0) : Vector3(1, 0, 0)).normalized();
			const Vector3 bt = c.dir.cross(t);
			Vector2 lo = dir_to_uv(c.dir), hi = lo;
			for (int k = 0; k < 12; k++) {
				const float ang = Math::TAU * k / 12.0f;
				const Vector2 uv = dir_to_uv(c.dir * Math::cos(reach) + (t * Math::cos(ang) + bt * Math::sin(ang)) * Math::sin(reach));
				lo = lo.min(uv);
				hi = hi.max(uv);
			}
			x0 = CLAMP((int)Math::floor(lo.x * W) - 2, 0, W - 1);
			x1 = CLAMP((int)Math::ceil(hi.x * W) + 2, 0, W - 1);
			y0 = CLAMP((int)Math::floor(lo.y * H) - 2, 0, H - 1);
			y1 = CLAMP((int)Math::ceil(hi.y * H) + 2, 0, H - 1);
		}
		for (int y = y0; y <= y1; y++) {
			for (int xx = x0; xx <= x1; xx++) {
				const int i = y * W + xx;
				float p, cl;
				_cell_contrib(c, dirs[i], p, cl);
				precip[i] *= 1.0f - p;
				cloud[i] *= 1.0f - cl;
			}
		}
	}

	const bool ready = climate_ready.load();
	const float minutes = p_dt / 60.0f;
	if (r_image.is_null()) {
		r_image = Image::create_empty(W, H, false, Image::FORMAT_RGBA8);
	}
	float activity = 0.0f;
	Vector<uint8_t> data;
	data.resize(n * 4);
	uint8_t *w = data.ptrw();
	for (int i = 0; i < n; i++) {
		float p = 1.0f - precip[i];
		float cl = 1.0f - cloud[i];
		// Colder at night, warmer under the sun
		float t = (ready ? temperature[i] : 0.5f) + season_offset(dirs[i]) - 0.04f + 0.08f * MAX(dirs[i].dot(p_sun_dir), 0.0f);
		const float freeze = params.freeze_temperature;
		const float ow = _override_weight(p_o, dirs[i]);
		if (ow > 0.0f) {
			if (p_o.mode == Override::CLEAR) {
				p *= 1.0f - ow;
				cl *= 1.0f - ow;
			}
			if (p_o.temperature != 0) {
				t = Math::lerp(t, freeze + 0.1f * p_o.temperature, ow);
			}
		}
		if (t < freeze) {
			snow[i] += p * params.snow_rate * minutes;
		} else {
			const float melt = params.melt_rate * _smoothstep(freeze, freeze + 0.15f, t) * (1.0f + 2.0f * p);
			snow[i] -= melt * minutes;
			wet[i] += p * params.wet_rate * minutes;
		}
		wet[i] -= params.dry_rate * minutes * (1.0f - p) * (0.4f + MAX(dirs[i].dot(p_sun_dir), 0.0f));
		snow[i] = CLAMP(snow[i], 0.0f, 1.0f);
		wet[i] = CLAMP(wet[i], 0.0f, 1.0f);
		// The season's lying snow where today's (daily mean) temperature is well below freezing: from about -3 °C,
		// full by -6 °C. Terrain, grass and trees all read it from here, so they agree where the ground is white.
		const float t_day = (ready ? temperature[i] : 0.5f) + season_offset(dirs[i]);
		const float lying = 1.0f - _smoothstep(freeze - 0.1f, freeze - 0.04f, t_day);
		const float ground = MAX(snow[i], ready ? lying : 0.0f);
		ground_snow[i] = ground;
		activity = MAX(activity, MAX(ground, wet[i]));
		w[i * 4 + 0] = (uint8_t)(p * 255.0f + 0.5f);
		w[i * 4 + 1] = (uint8_t)(ground * 255.0f + 0.5f);
		w[i * 4 + 2] = (uint8_t)(wet[i] * 255.0f + 0.5f);
		w[i * 4 + 3] = (uint8_t)(CLAMP(cl, 0.0f, 1.0f) * 255.0f + 0.5f);
	}
	r_image->set_data(W, H, false, Image::FORMAT_RGBA8, data);
	map_activity = activity;
}
