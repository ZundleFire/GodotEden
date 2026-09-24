#include "eden_planet_generator_v1.h"
#include "core/math/math_defs.h"
#include "core/math/math_funcs.h"
#include "modules/voxel/storage/voxel_buffer.h"
#include "world_data_module.h"

using zylann::voxel::VoxelBuffer;

EdenPlanetGeneratorV1::EdenPlanetGeneratorV1() {
}

EdenPlanetGeneratorV1::~EdenPlanetGeneratorV1() {
}

// ══════════════════════════════════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════════════════════════════════

Ref<FastNoiseLite> EdenPlanetGeneratorV1::_make_noise(int p_seed, int noise_type, float freq,
		int octaves, float gain, float lacunarity, int fractal_type) {
	Ref<FastNoiseLite> n;
	n.instantiate();
	n->set_seed(p_seed);
	n->set_noise_type((FastNoiseLite::NoiseType)noise_type);
	n->set_frequency(freq);
	const int safe_fractal_type = CLAMP(fractal_type,
			int(FastNoiseLite::FRACTAL_NONE),
			int(FastNoiseLite::FRACTAL_PING_PONG));
	n->set_fractal_type((FastNoiseLite::FractalType)safe_fractal_type);
	n->set_fractal_octaves(octaves);
	n->set_fractal_gain(gain);
	n->set_fractal_lacunarity(lacunarity);
	return n;
}

void EdenPlanetGeneratorV1::setup() {
	zylann::RWLockWrite wlock(_parameters_lock);

	Parameters &p = _parameters;

	// ── Tectonic plates ──────────────────────────────────────────────────
	_tectonics.instantiate();

	print_line(String("[EdenPlanetGeneratorV1] setup() — mtn_falloff_start={0} end={1} sharpness={2}").format(
			varray(p.mountain_falloff_start, p.mountain_falloff_end, p.mountain_sharpness)));

	Dictionary tect_params;
	tect_params["n_points"] = p.num_voronoi_points;
	tect_params["n_plates"] = p.num_plates;
	tect_params["plate_freq"] = p.plate_noise_freq;
	tect_params["plate_octaves"] = p.plate_noise_octaves;
	tect_params["plate_lacunarity"] = p.plate_noise_lacunarity;
	tect_params["plate_gain"] = p.plate_noise_gain;
	tect_params["oceanic_fraction"] = p.oceanic_fraction;
	tect_params["border_hops"] = p.plate_border_hops;
	tect_params["collision_threshold"] = p.plate_collision_threshold;
	tect_params["border_warp"] = p.plate_warp_strength;
	tect_params["warp_freq"] = p.plate_warp_freq;
	_tectonics->configure(tect_params);
	_tectonics->generate(p.seed_val);

	// ── Bake tectonic data to equirectangular arrays ─────────────────────
	int map_size = TECT_W * TECT_H;
	_tect_oceanic.resize(map_size);
	_tect_mountain.resize(map_size);
	_tect_valley.resize(map_size);

	for (int py = 0; py < TECT_H; py++) {
		float v = float(py) / float(TECT_H);
		float lat_r = (0.5f - v) * Math::PI;
		float cos_lat = Math::cos(lat_r);
		float sin_lat = Math::sin(lat_r);
		for (int px = 0; px < TECT_W; px++) {
			float u = float(px) / float(TECT_W);
			float lon = (u - 0.5f) * Math::TAU;
			Vector3 dir = Vector3(cos_lat * Math::cos(lon), sin_lat, cos_lat * Math::sin(lon));
			PlanetTectonics::TerrainData td = _tectonics->get_terrain_data(dir);
			int idx = py * TECT_W + px;

			_tect_oceanic.write[idx] = td.oceanic_s;

			// Mountain proximity
			float mtn_p = 0.0f;
			if (td.bnd_type_a == PlanetTectonics::BND_MOUNTAIN ||
					td.bnd_type_a == PlanetTectonics::BND_ISLAND_ARC) {
				mtn_p = 1.0f - _ss(p.mountain_falloff_start, p.mountain_falloff_end, td.falloff_s);
				mtn_p = Math::pow(mtn_p, p.mountain_sharpness);
			}
			_tect_mountain.write[idx] = mtn_p;

			// Valley proximity
			float val_p = 0.0f;
			if (td.bnd_type_a == PlanetTectonics::BND_TRENCH ||
					td.bnd_type_a == PlanetTectonics::BND_RIFT) {
				val_p = 1.0f - _ss(p.valley_falloff_start, p.valley_falloff_end, td.falloff_s);
				// Kept in [0,1] like _tect_mountain -- trench_depth/rift_depth are applied once,
				// at point of use (valley = trench_depth * trench_mask), not here. Scaling here
				// saturated line_raw = MAX(t_mountain, t_valley) against [0,1]-scale thresholds,
				// blowing valleys out across the whole falloff band and starving mountains of
				// their share of the shared tect_line mask.
			}
			_tect_valley.write[idx] = val_p;
		}
	}

	// ── Axis tilt ────────────────────────────────────────────────────────
	float tilt_rad = Math::deg_to_rad(p.axis_tilt);
	float dir_rad = Math::deg_to_rad(p.tilt_direction);
	p.tilt_axis = Vector3(
			Math::sin(tilt_rad) * Math::sin(dir_rad),
			Math::cos(tilt_rad),
			Math::sin(tilt_rad) * Math::cos(dir_rad))
							 .normalized();

	// ── Noise resources ──────────────────────────────────────────────────
	_noise_continent = _make_noise(p.seed_val, p.continent_noise_type, p.continent_noise_freq,
			p.continent_noise_octaves, p.continent_noise_gain, p.continent_noise_lacunarity,
			p.continent_fractal_type);
	if (p.continent_warp_enabled && p.continent_warp_freq > 0.0f) {
		_noise_continent->set_domain_warp_enabled(true);
		_noise_continent->set_domain_warp_type(FastNoiseLite::DOMAIN_WARP_SIMPLEX);
		_noise_continent->set_domain_warp_frequency(p.continent_warp_freq);
		_noise_continent->set_domain_warp_amplitude(p.continent_warp_amp);
	}

	// Build empirical CDF of continent noise over the planet sphere.
	// Samples are sorted ascending so that continent_cdf[k] is the noise value at
	// rank k/N.  In generate_block the threshold for a given land_coverage fraction
	// is looked up directly — no statistics assumptions, works for any noise shape.
	{
		const int N_S = Parameters::CONTINENT_CDF_SIZE;
		const float GOLDEN = 2.4f;
		// Collect samples
		for (int i = 0; i < N_S; ++i) {
			const float cos_lat = 1.0f - (2.0f * float(i) + 1.0f) / float(N_S);
			const float sin_lat = Math::sqrt(MAX(1.0f - cos_lat * cos_lat, 0.0f));
			const float ang     = GOLDEN * float(i);
			const float sx = p.planet_radius * sin_lat * Math::cos(ang);
			const float sy = p.planet_radius * cos_lat;
			const float sz = p.planet_radius * sin_lat * Math::sin(ang);
			p.continent_cdf_hf[i] = _noise_continent->get_noise_3d(sx, sy, sz);
			p.continent_cdf_lf[i] = _noise_continent->get_noise_3d(sx * 0.35f, sy * 0.35f, sz * 0.35f);
		}
		// Insertion sort both arrays (N=128, fast enough)
		for (int i = 1; i < N_S; ++i) {
			for (int j = i; j > 0 && p.continent_cdf_hf[j - 1] > p.continent_cdf_hf[j]; --j) {
				const float t = p.continent_cdf_hf[j];
				p.continent_cdf_hf[j] = p.continent_cdf_hf[j - 1];
				p.continent_cdf_hf[j - 1] = t;
			}
			for (int j = i; j > 0 && p.continent_cdf_lf[j - 1] > p.continent_cdf_lf[j]; --j) {
				const float t = p.continent_cdf_lf[j];
				p.continent_cdf_lf[j] = p.continent_cdf_lf[j - 1];
				p.continent_cdf_lf[j - 1] = t;
			}
		}
	}

	_noise_hills = _make_noise(p.seed_val + 50, p.hills_noise_type, p.hills_noise_freq,
			p.hills_noise_octaves, p.hills_noise_gain, p.hills_noise_lacunarity,
			p.hills_fractal_type);
	_noise_mountain = _make_noise(p.seed_val + 1, p.mountain_noise_type, p.mountain_noise_freq,
			p.mountain_noise_octaves, p.mountain_noise_gain, p.mountain_noise_lacunarity,
			p.mountain_fractal_type);
	_noise_climate = _make_noise(p.seed_val + 3, p.climate_noise_type, p.climate_noise_freq,
			p.climate_noise_octaves, p.climate_noise_gain, p.climate_noise_lacunarity,
			p.climate_fractal_type);
	_noise_biome_region = _make_noise(p.seed_val + 77 + p.biome_region_seed_offset, FastNoiseLite::TYPE_CELLULAR, p.biome_region_noise_freq,
			1, 1.0f, 2.0f, FastNoiseLite::FRACTAL_NONE);
	_noise_desert = _make_noise(p.seed_val + 10, p.desert_noise_type, p.desert_noise_freq,
			p.desert_noise_octaves, p.desert_noise_gain, p.desert_noise_lacunarity,
			p.desert_fractal_type);
	_noise_forest = _make_noise(p.seed_val + 11, p.forest_noise_type, p.forest_noise_freq,
			p.forest_noise_octaves, p.forest_noise_gain, p.forest_noise_lacunarity,
			p.forest_fractal_type);
	_noise_tropical = _make_noise(p.seed_val + 12, p.tropical_noise_type, p.tropical_noise_freq,
			p.tropical_noise_octaves, p.tropical_noise_gain, p.tropical_noise_lacunarity,
			p.tropical_fractal_type);
	_noise_tundra = _make_noise(p.seed_val + 13, p.tundra_noise_type, p.tundra_noise_freq,
			p.tundra_noise_octaves, p.tundra_noise_gain, p.tundra_noise_lacunarity,
			p.tundra_fractal_type);
	_noise_grassland = _make_noise(p.seed_val + 14, p.grassland_noise_type, p.grassland_noise_freq,
			p.grassland_noise_octaves, p.grassland_noise_gain, p.grassland_noise_lacunarity,
			p.grassland_fractal_type);
	_noise_cave = _make_noise(p.seed_val + 30, p.cave_noise_type, p.cave_noise_freq,
			p.cave_noise_octaves, p.cave_noise_gain, p.cave_noise_lacunarity,
			p.cave_fractal_type);

	p.is_setup = true;

	// Auto-wire this generator as the WorldDataModule query backend (ADR-0005).
	// Safe under the write lock: set_generator only stores the reference.
	WorldDataModule *wdm = WorldDataModule::get_singleton();
	if (wdm) {
		wdm->set_generator(Ref<EdenPlanetGeneratorV1>(this));
	}
}

// ══════════════════════════════════════════════════════════════════════════════
// GENERATE BLOCK — the complete SDF pipeline
// ══════════════════════════════════════════════════════════════════════════════

zylann::voxel::VoxelGenerator::Result EdenPlanetGeneratorV1::generate_block(VoxelQueryData input) {
	Result result;

	Parameters params;
	Ref<EdenPlanetClimateProfile> climate_profile;
	Vector<float> tect_oceanic;
	Vector<float> tect_mountain;
	Vector<float> tect_valley;
	Ref<FastNoiseLite> noise_continent;
	Ref<FastNoiseLite> noise_hills;
	Ref<FastNoiseLite> noise_mountain;
	Ref<FastNoiseLite> noise_climate;
	Ref<FastNoiseLite> noise_desert;
	Ref<FastNoiseLite> noise_forest;
	Ref<FastNoiseLite> noise_tropical;
	Ref<FastNoiseLite> noise_tundra;
	Ref<FastNoiseLite> noise_grassland;
	Ref<FastNoiseLite> noise_biome_region;
	Ref<FastNoiseLite> noise_cave;
	{
		zylann::RWLockRead rlock(_parameters_lock);
		params = _parameters;
		climate_profile = _climate_profile;
		tect_oceanic = _tect_oceanic;
		tect_mountain = _tect_mountain;
		tect_valley = _tect_valley;
		noise_continent = _noise_continent;
		noise_hills = _noise_hills;
		noise_mountain = _noise_mountain;
		noise_climate = _noise_climate;
		noise_desert = _noise_desert;
		noise_forest = _noise_forest;
		noise_tropical = _noise_tropical;
		noise_tundra = _noise_tundra;
		noise_grassland = _noise_grassland;
		noise_biome_region = _noise_biome_region;
		noise_cave = _noise_cave;
	}
	_apply_climate_profile(params, climate_profile);

	if (!params.is_setup) {
		// Auto-initialize on first generate_block() call, like GDScript _init().
		setup();
		// Re-read params after setup.
		zylann::RWLockRead rlock(_parameters_lock);
		params = _parameters;
		climate_profile = _climate_profile;
		tect_oceanic = _tect_oceanic;
		tect_mountain = _tect_mountain;
		tect_valley = _tect_valley;
		noise_continent = _noise_continent;
		noise_hills = _noise_hills;
		noise_mountain = _noise_mountain;
		noise_climate = _noise_climate;
		noise_desert = _noise_desert;
		noise_forest = _noise_forest;
		noise_tropical = _noise_tropical;
		noise_tundra = _noise_tundra;
		noise_grassland = _noise_grassland;
		noise_biome_region = _noise_biome_region;
		noise_cave = _noise_cave;
	}
	_apply_climate_profile(params, climate_profile);

	if (!params.is_setup) {
		// setup() failed somehow — return air.
		input.voxel_buffer.clear_channel_f(VoxelBuffer::CHANNEL_SDF, 100.0f);
		result.max_lod_hint = true;
		return result;
	}

	VoxelBuffer &buffer = input.voxel_buffer;
	const Vector3i origin = input.origin_in_voxels;
	const int lod = input.lod;
	const Vector3i size = buffer.get_size();
	const int step = 1 << lod;
	const float half_step = step * 0.5f;

	// Block centre in world space.
	const float bws = float(size.x * step);
	const float cx = float(origin.x) + bws * 0.5f;
	const float cy = float(origin.y) + bws * 0.5f;
	const float cz = float(origin.z) + bws * 0.5f;
	const float dist_c = Math::sqrt(cx * cx + cy * cy + cz * cz);
	const float diag = bws * 1.732f;

	const SampleConsts sc = _make_sample_consts(params);
	const float shell_inner = params.planet_radius + sc.alt_inner;
	const float shell_outer = params.planet_radius + sc.alt_outer;

	// ── Shell rejection ──────────────────────────────────────────────────
	if (dist_c + diag * 0.5f < shell_inner) {
		buffer.clear_channel_f(VoxelBuffer::CHANNEL_SDF, -100.0f);
		if (params.single_material_mode) {
			buffer.clear_channel(VoxelBuffer::CHANNEL_INDICES, MAT_ROCK);
		} else {
			buffer.clear_channel(VoxelBuffer::CHANNEL_INDICES, sc.rock_indices);
			buffer.clear_channel(VoxelBuffer::CHANNEL_WEIGHTS, sc.rock_weights);
		}
		result.max_lod_hint = true;
		return result;
	}

	if (dist_c - diag * 0.5f > shell_outer) {
		buffer.clear_channel_f(VoxelBuffer::CHANNEL_SDF, 100.0f);
		result.max_lod_hint = true;
		return result;
	}

	const bool do_materials = (lod <= 4);

	SampleNoises sn;
	sn.continent = noise_continent;
	sn.hills = noise_hills;
	sn.mountain = noise_mountain;
	sn.climate = noise_climate;
	sn.desert = noise_desert;
	sn.forest = noise_forest;
	sn.tropical = noise_tropical;
	sn.tundra = noise_tundra;
	sn.grassland = noise_grassland;
	sn.biome_region = noise_biome_region;
	sn.cave = noise_cave;
	sn.tect_oceanic = &tect_oceanic;
	sn.tect_mountain = &tect_mountain;
	sn.tect_valley = &tect_valley;

	// ── Per-voxel loop (same formula at ALL LODs) ────────────────────────
	VoxelSample vs;
	for (int z = 0; z < size.z; ++z) {
		const float wz = float(origin.z) + z * step + half_step;
		for (int y = 0; y < size.y; ++y) {
			const float wy = float(origin.y) + y * step + half_step;
			for (int x = 0; x < size.x; ++x) {
				const float wx = float(origin.x) + x * step + half_step;

				const VoxelCode code = _sample_voxel(wx, wy, wz, params, sn, sc, do_materials, vs);

				// Bake ocean water directly into CHANNEL_DATA5 (== VoxelWaterSimulator::
				// WATER_CHANNEL) for air voxels in oceanic columns at/below nominal sea level
				// (alt <= 0, i.e. within the planet_radius shell). Without this, the generator
				// only ever painted the SEABED ocean-floor-colored -- the water volume itself
				// was just empty air, nothing a VoxelWaterSimulator could render or simulate.
				// Seeding a whole ocean voxel-by-voxel via add_water() at runtime is exactly
				// the cost this bake-at-generation-time path avoids (see VoxelWaterSimulator's
				// own class doc comment on why baked water starts inert until activate_block()
				// wakes it near actual activity). cont_transition (0=land, 1=ocean) is used
				// rather than the full biome classification because most open-ocean voxels hit
				// the cheaper VOXEL_EARLY_AIR path below, which never computes a biome at all.
				auto bake_water_if_ocean = [&]() {
					if (vs.sdf <= 0.0f || vs.cont_transition <= 0.5f) {
						return;
					}
					const double r = Math::sqrt(double(wx) * wx + double(wy) * wy + double(wz) * wz);
					if (float(r) - params.planet_radius <= 0.0f) {
						buffer.set_voxel_f(1.0f, x, y, z, VoxelBuffer::CHANNEL_DATA5);
					}
				};

				// In single_material_mode only CHANNEL_INDICES is written (one 8-bit id);
				// CHANNEL_WEIGHTS is deliberately left alone, since there is no per-voxel
				// weight field in that mode. See set_single_material_mode().
				const bool single_mode = params.single_material_mode;
				auto write_material = [&](int packed_indices, int packed_weights, int single_id) {
					if (single_mode) {
						buffer.set_voxel(single_id, x, y, z, VoxelBuffer::CHANNEL_INDICES);
					} else {
						buffer.set_voxel(packed_indices, x, y, z, VoxelBuffer::CHANNEL_INDICES);
						buffer.set_voxel(packed_weights, x, y, z, VoxelBuffer::CHANNEL_WEIGHTS);
					}
				};

				switch (code) {
					case VOXEL_SKIP_AIR:
						buffer.set_voxel_f(1.0f, x, y, z, VoxelBuffer::CHANNEL_SDF);
						break;
					case VOXEL_SKIP_SOLID:
						buffer.set_voxel_f(-1.0f, x, y, z, VoxelBuffer::CHANNEL_SDF);
						write_material(sc.rock_indices, sc.rock_weights, MAT_ROCK);
						break;
					case VOXEL_EARLY_AIR:
						buffer.set_voxel_f(vs.sdf, x, y, z, VoxelBuffer::CHANNEL_SDF);
						bake_water_if_ocean();
						break;
					case VOXEL_EARLY_SOLID:
						buffer.set_voxel_f(vs.sdf, x, y, z, VoxelBuffer::CHANNEL_SDF);
						write_material(sc.rock_indices, sc.rock_weights, MAT_ROCK);
						break;
					case VOXEL_FULL:
						buffer.set_voxel_f(vs.sdf, x, y, z, VoxelBuffer::CHANNEL_SDF);
						if (do_materials) {
							write_material(vs.indices, vs.weights, vs.single_mat);
						} else if (vs.cont_transition > 0.5f) {
							write_material(sc.ocean_indices, sc.ocean_weights, MAT_OCEAN_FLOOR);
						} else {
							write_material(sc.rock_indices, sc.rock_weights, MAT_ROCK);
						}
						bake_water_if_ocean();
						break;
				}
			}
		}
	}

	return result;
}

EdenPlanetGeneratorV1::SampleConsts EdenPlanetGeneratorV1::_make_sample_consts(const Parameters &params) {
	SampleConsts c;
	c.alt_inner = -params.max_terrain_height - 500.0f;
	c.alt_outer = params.max_terrain_height + 500.0f;

	// Budget for early-out (all SDF layers included at every LOD).
	c.budget = params.mountain_height + params.trench_depth + params.hills_amplitude +
			MAX(MAX(MAX(params.amp_desert, params.amp_forest),
					MAX(params.amp_tropical, params.amp_tundra)),
					params.amp_grassland) +
			50.0f;

	// Coast transition tuning for MIXEL4 material ramps.
	const float shore_width_scale = MAX(params.shoreline_material_width_scale, 0.05f);
	const float sand_mid = (params.cont_sand_start + params.cont_sand_end) * 0.5f;
	const float sand_half = MAX((params.cont_sand_end - params.cont_sand_start) * 0.5f * shore_width_scale, 0.001f);
	c.cont_sand_start = CLAMP(sand_mid - sand_half, 0.0f, 1.0f);
	c.cont_sand_end = CLAMP(sand_mid + sand_half, 0.0f, 1.0f);
	const float ocean_mid = (params.cont_ocean_start + params.cont_ocean_end) * 0.5f;
	const float ocean_half = MAX((params.cont_ocean_end - params.cont_ocean_start) * 0.5f * shore_width_scale, 0.001f);
	c.cont_ocean_start = CLAMP(ocean_mid - ocean_half, 0.0f, 1.0f);
	c.cont_ocean_end = CLAMP(ocean_mid + ocean_half, 0.0f, 1.0f);
	c.elev_half = MAX(params.shoreline_elevation_falloff_width * 0.5f, 0.0005f);
	c.elev_pow = MAX(params.shoreline_elevation_falloff_power, 0.05f);
	c.beach_width_m = MAX(params.beach_width_m, 1.0f);

	// Pre-compute MIXEL4 for bulk fills.
	// Uniform presets: no secondary land material (blend 0).
	_pack_mixel4(MAT_ROCK, MAT_ROCK, 0.0f, MAT_OCEAN_FLOOR, 0.0f,
			c.cont_sand_start, c.cont_sand_end,
			c.cont_ocean_start, c.cont_ocean_end,
			c.rock_indices, c.rock_weights);
	_pack_mixel4(MAT_OCEAN_FLOOR, MAT_OCEAN_FLOOR, 0.0f, MAT_OCEAN_FLOOR, 1.0f,
			c.cont_sand_start, c.cont_sand_end,
			c.cont_ocean_start, c.cont_ocean_end,
			c.ocean_indices, c.ocean_weights);
	return c;
}

EdenPlanetGeneratorV1::VoxelCode EdenPlanetGeneratorV1::_sample_voxel(float wx, float wy, float wz,
		const Parameters &params, const SampleNoises &n, const SampleConsts &c,
		bool with_materials, VoxelSample &out) const {
	const float INV_TAU = 1.0f / float(Math::TAU);
	const float INV_PI = 1.0f / float(Math::PI);

	const Ref<FastNoiseLite> &noise_continent = n.continent;
	const Ref<FastNoiseLite> &noise_hills = n.hills;
	const Ref<FastNoiseLite> &noise_mountain = n.mountain;
	const Ref<FastNoiseLite> &noise_climate = n.climate;
	const Ref<FastNoiseLite> &noise_desert = n.desert;
	const Ref<FastNoiseLite> &noise_forest = n.forest;
	const Ref<FastNoiseLite> &noise_tropical = n.tropical;
	const Ref<FastNoiseLite> &noise_tundra = n.tundra;
	const Ref<FastNoiseLite> &noise_grassland = n.grassland;
	const Ref<FastNoiseLite> &noise_biome_region = n.biome_region;
	const Ref<FastNoiseLite> &noise_cave = n.cave;
	const Vector<float> &tect_oceanic = *n.tect_oceanic;
	const Vector<float> &tect_mountain = *n.tect_mountain;
	const Vector<float> &tect_valley = *n.tect_valley;
	const float budget = c.budget;
	const float elev_pow = c.elev_pow;
	const float beach_width_m = c.beach_width_m;
	const float cont_sand_start = c.cont_sand_start;
	const float cont_sand_end = c.cont_sand_end;
	const float cont_ocean_start = c.cont_ocean_start;
	const float cont_ocean_end = c.cont_ocean_end;

	const float r = Math::sqrt(wx * wx + wy * wy + wz * wz);
	const float alt = r - params.planet_radius;

	// Per-voxel shell skip.
	if (alt > c.alt_outer) {
		out.sdf = 1.0f;
		return VOXEL_SKIP_AIR;
	}
	if (alt < c.alt_inner) {
		out.sdf = -1.0f;
		return VOXEL_SKIP_SOLID;
	}

	{
				// Sphere projection.
				const float inv_r = 1.0f / MAX(r, 1.0f);
				const float ux = wx * inv_r;
				const float uy = wy * inv_r;
				const float uz = wz * inv_r;
				const float sph_x = ux * params.planet_radius;
				const float sph_y = uy * params.planet_radius;
				const float sph_z = uz * params.planet_radius;

				// ── Sample baked tectonic maps (bilinear) ────────────
				const float map_u = Math::atan2(uz, ux) * INV_TAU + 0.5f;
				const float map_v = 0.5f - Math::asin(CLAMP(uy, -1.0f, 1.0f)) * INV_PI;

				const float t_oceanic = _sample_tect_map(tect_oceanic, map_u, map_v);
				float t_mountain = _sample_tect_map(tect_mountain, map_u, map_v);
				float t_valley = _sample_tect_map(tect_valley, map_u, map_v);

				// Near the poles, map_u = atan2(uz, ux) becomes degenerate (all longitudes
				// converge to a point), so bilinear sampling of the baked equirect map blends
				// between texels that represent physically distant, unrelated plate states.
				// That shows up as chaotic mountain/trench spikes right along the pole axis.
				// Fade the tectonic contribution out over the last ~5 degrees of latitude to
				// avoid sampling the singularity; the polar cap is a negligible fraction of
				// surface area so losing plate detail there is unnoticeable.
				const float pole_fade = 1.0f - _ss(0.9962f, 1.0f, Math::abs(uy));
				t_mountain *= pole_fade;
				t_valley *= pole_fade;

				// ── Continent mask (pure noise) ────────────────────
				// Land vs ocean is determined entirely by continent noise — tectonic plate
				// type is NOT used here (it caused phantom landmasses from misclassified plates).
				// Tectonic plates still drive mountains and trenches via t_mountain / t_valley.
				//
				//   continent_amplitude — scales noise contribution against the falloff width.
				//   continent_falloff   — half-width of the 0→1 land_mask ramp at the shore.
				//   land_coverage       — fraction of planet surface that is land (0=all ocean,
				//                         0.5=~50% land, 1.0=all land).  Properly calibrated
				//                         using noise sigma so it works at any amp/falloff.
				const float raw_cont     = noise_continent->get_noise_3d(sph_x, sph_y, sph_z);
				const float raw_cont_low = noise_continent->get_noise_3d(sph_x * 0.35f, sph_y * 0.35f, sph_z * 0.35f);
				const float amp       = MAX(params.continent_amplitude, 0.001f);
				const float falloff_h = MAX(params.continent_falloff,   0.001f);
				const float land_cov  = CLAMP(params.land_coverage, 0.0f, 1.0f);
				// Look up the exact noise threshold for this land_coverage fraction from
				// the pre-sorted empirical CDF built in setup().
				// continent_cdf[k] is the kth-lowest sample, so index (1-cov)*N gives the
				// value that (1-cov) fraction of the sphere is BELOW → cov fraction is above.
				const int N_C = Parameters::CONTINENT_CDF_SIZE;
				const int cdf_idx = CLAMP((int)((1.0f - land_cov) * float(N_C)), 0, N_C - 1);
				const float land_thr_hf = params.continent_cdf_hf[cdf_idx];
				const float land_thr_lf = params.continent_cdf_lf[cdf_idx];
				// Subtract threshold so positive cont_height = land, negative = ocean.
				// amp controls coastline detail (how jagged shores are).
				// falloff_h controls the width of the 0→1 blend zone at the shoreline.
				const float cont_height     = (raw_cont     - land_thr_hf) * amp;
				const float cont_height_low = (raw_cont_low - land_thr_lf) * amp;
				// Linear ramp: 1.0=land, 0.0=ocean. The ±falloff_h band is the coastline zone.
				const float land_mask     = CLAMP((cont_height     + falloff_h) / (2.0f * falloff_h), 0.0f, 1.0f);
				const float land_mask_low = CLAMP((cont_height_low + falloff_h) / (2.0f * falloff_h), 0.0f, 1.0f);
				const float cont_transition = 1.0f - land_mask;  // 0=land, 1=ocean
				// Low-freq authority mask: 0=ocean, 1=established land. Gates hills, mountains, biome.
				const float macro_land_mask = _ss(0.08f, 0.42f, land_mask_low);

				const float shore_half = MAX(params.shoreline_elevation_falloff_width * 0.5f, 0.03f);
				const float shore_signed = cont_transition - 0.5f;
				const float coast_t = _ss(-shore_half, shore_half, shore_signed); // 0=land, 1=ocean
				const float coast_near = 1.0f - _ss(0.0f, shore_half, Math::abs(shore_signed));

				const float coast_width = MAX(params.shoreline_shape_width, 0.001f);
				const float coast_proximity = 1.0f - _ss(0.0f, coast_width, Math::abs(shore_signed));
				const float land_width = MAX(params.shoreline_land_shape_width, 0.001f);
				const float ocean_width = MAX(params.shoreline_ocean_shape_width, 0.001f);
				const float land_dist = MAX(-shore_signed, 0.0f);
				const float ocean_dist = MAX(shore_signed, 0.0f);
				const float land_proximity = 1.0f - _ss(0.0f, land_width, land_dist);
				const float ocean_proximity = 1.0f - _ss(0.0f, ocean_width, ocean_dist);

				// Optional S-curve shaping of coast transition while preserving midpoint.
				float coast_t_shaped = coast_t;
				if (Math::abs(elev_pow - 1.0f) > 0.0001f) {
					if (coast_t_shaped < 0.5f) {
						coast_t_shaped = 0.5f * Math::pow(CLAMP(coast_t_shaped * 2.0f, 0.0f, 1.0f), elev_pow);
					} else {
						coast_t_shaped = 1.0f - 0.5f * Math::pow(CLAMP((1.0f - coast_t_shaped) * 2.0f, 0.0f, 1.0f), elev_pow);
					}
				}

				// Ocean depth is measured inward from planet radius:
				// 0 at shoreline, max depth at the innermost ocean.
				const float deep_ocean = _ss(0.0f, 0.5f, shore_signed);
				const float ocean_depth_t = Math::pow(deep_ocean, 1.12f);
				const float ocean_base = -params.ocean_depth * ocean_depth_t;
				const float ocean_floor_n = noise_hills->get_noise_3d(sph_x * 0.45f, sph_y * 0.45f, sph_z * 0.45f) * 0.5f + 0.5f;
				const float ocean_carve = ocean_floor_n * (params.hills_amplitude * 0.18f) * ocean_depth_t;

				const float land_height = params.continent_height;
				const float ocean_height = ocean_base - ocean_carve;
				const float coast_shape =
						(-params.shoreline_shape_strength * coast_proximity
						 -params.shoreline_land_shape_strength * land_proximity
						 -params.shoreline_ocean_shape_strength * ocean_proximity) * coast_near;

				// Keep land from dipping into a coastline moat: begin ocean blending close to
				// the shoreline and mostly on the ocean side, instead of equally on both sides.
				// Blend HF shore_signed with the stable low-freq mask to prevent cliff-like
				// elevation snaps where HF noise crosses the threshold before the LF coast settles.
				// land_mask_low: 1=land, 0=ocean → (0.5 - land_mask_low) has same sign as shore_signed.
				const float shore_signed_stable = shore_signed * 0.25f + (0.5f - land_mask_low) * 0.75f;
				const float coast_blend_t = _ss(-shore_half * 0.20f, shore_half, shore_signed_stable);
				const float plate_bias_smooth = Math::lerp(land_height, ocean_height, coast_blend_t);
				const float plate_bias = plate_bias_smooth + coast_shape;

				// ── Hills & meadows ──────────────────────────────────
				float hills = 0.0f;
				const float land_factor = CLAMP((1.0f - coast_t_shaped) * macro_land_mask, 0.0f, 1.0f);
				if (land_factor > 0.02f) {
					float raw_h = noise_hills->get_noise_3d(sph_x, sph_y, sph_z);
					float shaped = (raw_h >= 0.0f ? 1.0f : -1.0f) * Math::pow(Math::abs(raw_h), params.hills_meadow_power);
					hills = shaped * params.hills_amplitude * land_factor;

					// Add interior continental relief so large continents don't become flat plateaus.
					const float interior_mask = _ss(0.25f, 0.85f, land_factor) * (0.35f + 0.65f * cont_transition);
					const float macro_h = noise_hills->get_noise_3d(sph_x * 0.33f + 71.0f, sph_y * 0.33f - 149.0f, sph_z * 0.33f + 43.0f);
					const float micro_h = noise_hills->get_noise_3d(sph_x * 1.15f - 193.0f, sph_y * 1.15f + 227.0f, sph_z * 1.15f - 61.0f);
					const float interior_relief = (macro_h * 0.65f + micro_h * 0.35f) * params.hills_amplitude *
							params.continental_relief_strength * interior_mask;
					hills += interior_relief;
				}

				// ── Early-out budget check ───────────────────────────
				float base_sdf = alt - plate_bias - hills;
				if (base_sdf > budget) {
					out.sdf = base_sdf;
					out.cont_transition = cont_transition;
					out.macro_land_mask = macro_land_mask;
					return VOXEL_EARLY_AIR;
				}
				if (base_sdf < -budget) {
					out.sdf = base_sdf;
					out.cont_transition = cont_transition;
					out.macro_land_mask = macro_land_mask;
					return VOXEL_EARLY_SOLID;
				}

				// ── Tectonic line mask + eroded mountains/trenches ───
				// Lines come from tectonic maps and are sharpened to thin belts.
				// A secondary noise partitions belts into mountain-dominant vs trench-dominant.
				const float line_raw = MAX(t_mountain, t_valley);
				const float width01 = CLAMP((params.tectonic_line_width_km - 1.0f) / 2.0f, 0.0f, 1.0f);
				const float line_start = Math::lerp(0.93f, 0.84f, width01);
				const float line_end = Math::lerp(0.985f, 0.95f, width01);
				const float tect_line = _ss(line_start, line_end, line_raw);
				const float tect_split = noise_climate->get_noise_3d(
						sph_x * 0.45f + 701.0f,
						sph_y * 0.45f - 233.0f,
						sph_z * 0.45f + 127.0f);
				const float mtn_lane = CLAMP(0.5f + 0.5f * tect_split, 0.0f, 1.0f);
				const float trench_lane = 1.0f - mtn_lane;

				// Smooth stand-in for `mtn` (below) used ONLY for material classification, not
				// terrain shape. `mtn` gets multiplied by `erosion_shape`, an intentionally
				// jagged per-octave gully simulation (sign-flipping "straight" term, see the loop
				// below) -- great for carving visible erosion channels into the SDF, but
				// _land_material_for() thresholds mtn/mountain_height into a single discrete
				// material choice per voxel, so every gully-line sign flip near that threshold
				// flipped the material category too, producing a fine salt-and-pepper
				// grass/rock (or moss/rock) speckle with no blending between materials.
				// tect_line is already the smooth (bilinear-sampled tectonic map) envelope that
				// erosion_shape modulates, so using it directly gives material classification the
				// mountain's general shape without its per-voxel jaggedness.
				const float mtn_smooth = params.mountain_height * tect_line * mtn_lane * macro_land_mask;

				float mtn = 0.0f;
				float valley = 0.0f;
				if (tect_line > 0.001f) {
					// EDEN FORK: this used to be a "Runevision-inspired fast erosion filter"
					// building escarpment/gully shape from a finite-difference SDF gradient plus
					// a per-octave DISCONTINUOUS sign-flip ("straight gullies", see git history
					// for the original ~45-line algorithm). That discontinuity, at a spatial
					// frequency fine enough relative to LOD0 mesh resolution, perturbed
					// Transvoxel-computed normals into a persistent faceted-lighting checkerboard
					// under flat-colored MIXEL4 shading -- confirmed by elimination (zeroing this
					// contribution entirely turned an otherwise-checkerboarded area perfectly
					// smooth; a 70% amplitude CUT of the original algorithm did not help at all,
					// showing this was a threshold/discontinuity effect, not a
					// gradient-proportional-to-amplitude one). Tried gating activation by
					// tect_line (only run near a real mountain/trench core) first -- didn't help
					// either, because this fork's baked tectonic map (PlanetTectonics,
					// num_plates=16, num_voronoi_points=1200) keeps tect_line elevated broadly
					// rather than sharply peaked at plate boundaries, so there's no clean "far
					// tail" region to gate out.
					//
					// Replaced with a single smooth noise sample: still gives real, continuous,
					// noise-driven mountain/trench elevation via the same mtn_mask/trench_mask
					// proximity+plate-lane+land-mask modulation as before, just without the
					// discontinuous escarpment/gully DETAIL. If a textured (not flat-color)
					// material pipeline exists later, a proper anti-aliased erosion pass could
					// reintroduce that detail without this artifact.
					const float erosion_shape = CLAMP(noise_mountain->get_noise_3d(sph_x, sph_y, sph_z) * 0.5f + 0.5f, 0.0f, 1.0f);
					const float mtn_mask = tect_line * mtn_lane * macro_land_mask;
					const float trench_mask = tect_line * trench_lane;
					mtn = erosion_shape * params.mountain_height * mtn_mask;
					const float valley_shore_mask = 1.0f - coast_near * 0.80f;
					valley = params.trench_depth * trench_mask * valley_shore_mask;
				}

				// ── Climate (3 editable noises) + biome regions ─────
				float signed_lat = CLAMP(uy, -1.0f, 1.0f);
				float lat = Math::abs(signed_lat);
				float temp = 0.0f;
				float moist = 0.0f;
				float climate_zone = 0.0f;
				float region_selector = 0.0f;
				float biome_roll = 0.0f;
				int biome = BIOME_OCEAN;
				int biome2 = BIOME_GRASSLAND;
				float biome_blend = 0.0f;
				if (cont_transition <= 0.5f && macro_land_mask > 0.1f) {
					const float region_count = Math::lerp(160.0f, 20.0f, CLAMP(params.biome_patch_size, 0.0f, 1.0f));
					const float region_count_v = region_count * 0.5f;
					const float u = map_u;
					const float v = map_v;
					const float uf = u * region_count;
					const float vf = v * region_count_v;
					const int ui = int(Math::floor(uf));
					const int vi = int(Math::floor(vf));
					const float fu = uf - float(ui);
					const float fv = vf - float(vi);
					const float edge_dist = MIN(MIN(fu, 1.0f - fu), MIN(fv, 1.0f - fv));
					const float edge_w = Math::lerp(0.02f, 0.18f, CLAMP(params.biome_patch_strength, 0.0f, 1.0f));

					auto _sample_region = [&](int ru, int rv, float &out_temp, float &out_moist, float &out_evap, float &out_lat) {
						const int uu = ((ru % int(region_count)) + int(region_count)) % int(region_count);
						const int vv = CLAMP(rv, 0, int(region_count_v) - 1);
						const float cu = (float(uu) + 0.5f) / region_count;
						const float cv = (float(vv) + 0.5f) / region_count_v;
						const float lon = (cu - 0.5f) * float(Math::TAU);
						const float lat_a = (0.5f - cv) * float(Math::PI);
						const float clat = Math::cos(lat_a);
						const float sx = params.planet_radius * clat * Math::cos(lon);
						const float sy = params.planet_radius * Math::sin(lat_a);
						const float sz = params.planet_radius * clat * Math::sin(lon);
						const float lat01 = Math::abs(Math::sin(lat_a));
						const float temp_lat = Math::lerp(params.equator_temperature, params.polar_temperature, lat01);
						const float t_noise = noise_climate->get_noise_3d(sx, sy, sz);
						const float m_noise = noise_desert->get_noise_3d(sx, sy, sz);
						const float e_noise = noise_forest->get_noise_3d(sx, sy, sz);
						out_evap = CLAMP(0.5f + 0.5f * e_noise, 0.0f, 1.0f);
						out_temp = temp_lat + t_noise * params.temp_noise_amplitude;
						out_moist = CLAMP(0.5f + 0.5f * m_noise - out_evap * params.evaporation_strength, 0.0f, 1.0f);
						out_lat = lat01;
					};

					float r_temp1, r_moist1, r_evap1, r_lat1;
					_sample_region(ui, vi, r_temp1, r_moist1, r_evap1, r_lat1);

					int nu = ui;
					int nv = vi;
					if (fu < edge_w) nu = ui - 1;
					else if ((1.0f - fu) < edge_w) nu = ui + 1;
					if (fv < edge_w) nv = vi - 1;
					else if ((1.0f - fv) < edge_w) nv = vi + 1;

					float r_temp2, r_moist2, r_evap2, r_lat2;
					_sample_region(nu, nv, r_temp2, r_moist2, r_evap2, r_lat2);

					const float edge_t = 1.0f - _ss(0.0f, edge_w, edge_dist);
					temp = Math::lerp(r_temp1, r_temp2, edge_t);
					moist = Math::lerp(r_moist1, r_moist2, edge_t);
					lat = Math::lerp(r_lat1, r_lat2, edge_t);
					biome_blend = edge_t;

					climate_zone = CLAMP(0.5f + 0.5f * noise_tropical->get_noise_3d(sph_x * 0.35f, sph_y * 0.35f, sph_z * 0.35f), 0.0f, 1.0f);
					region_selector = CLAMP(0.5f + 0.5f * noise_biome_region->get_noise_3d(sph_x * 0.65f, sph_y * 0.65f, sph_z * 0.65f), 0.0f, 1.0f);
					biome_roll = CLAMP(0.5f + 0.5f * noise_grassland->get_noise_3d(sph_x * 0.95f, sph_y * 0.95f, sph_z * 0.95f), 0.0f, 1.0f);
					biome = _classify_biome_fast(temp, moist, cont_transition, lat, alt, climate_zone, region_selector, biome_roll, params, biome2, biome_blend);
				}

				// Blend biome detail noise for smooth biome transitions.
				float detail = 0.0f;
				const float detail_mask = CLAMP(land_factor, 0.0f, 1.0f);
				if (biome != BIOME_OCEAN && detail_mask > 0.01f) {
					float d1 = _biome_detail_fast(sph_x, sph_y, sph_z, biome,
							noise_hills, noise_desert, noise_forest, noise_tropical, noise_tundra, noise_grassland,
							params);
					if (biome_blend > 0.01f) {
						float d2 = _biome_detail_fast(sph_x, sph_y, sph_z, biome2,
								noise_hills, noise_desert, noise_forest, noise_tropical, noise_tundra, noise_grassland,
								params);
						d1 = d1 * (1.0f - biome_blend) + d2 * biome_blend;
					}
					detail = d1 * detail_mask;
					// Small neutral breakup ensures no continent region stays overly flat.
					// (Amplitude left at the original 0.08 -- root cause of the faceted-lighting
					// checkerboard turned out to be the mountain/valley erosion mask below, not
					// this term; see that fix's comment. Ruled out here via elimination: forcing
					// detail to 0 entirely didn't fix the artifact either.)
					detail += noise_hills->get_noise_3d(sph_x, sph_y, sph_z) *
							(params.hills_amplitude * 0.08f) * detail_mask;
				}

				// ── SDF assembly ──────────────────────────────────────
				float sdf = base_sdf - mtn + valley - detail;

				// ── Caves ────────────────────────────────────────────
				if (params.enable_caves) {
					float cave = _cave_carve(noise_cave, wx, wy, wz, alt, params);
					if (cave > 0.01f) {
						sdf = MAX(sdf, cave * params.cave_carve_strength);
					}
				}

				// ── Material (MIXEL4) ────────────────────────────────
				if (with_materials) {
					if (macro_land_mask <= 0.1f) {
						biome = BIOME_OCEAN;
					}
					// The material path deliberately uses the biome from BEFORE the hard beach
					// override below: _land_material_blend() now applies the beach as a smooth
					// MAT_SAND weight itself, so also forcing BIOME_DESERT here would both
					// double-count sand and reintroduce exactly the abrupt per-voxel flip this
					// blending exists to remove. The override is still applied to `biome` after,
					// so out.biome / sample_surface() keep their existing semantics.
					const int material_biome = biome;
					if (alt >= 0.0f && alt <= beach_width_m && biome != BIOME_OCEAN) {
						biome = BIOME_DESERT;
					}
					int land_a = MAT_OCEAN_FLOOR;
					int land_b = MAT_OCEAN_FLOOR;
					float land_blend = 0.0f;
					if (material_biome != BIOME_OCEAN) {
						_land_material_blend(material_biome, biome2, biome_blend,
								alt, temp, mtn_smooth, params,
								land_a, land_b, land_blend);
					}
					_pack_mixel4(land_a, land_b, land_blend, MAT_OCEAN_FLOOR, cont_transition,
							cont_sand_start, cont_sand_end,
							cont_ocean_start, cont_ocean_end,
							out.indices, out.weights);

					// single_material_mode: collapse the same decision to ONE material id rather
					// than a weighted mix. Both the sand and ocean shares come from the smooth
					// `cont_transition` field, so thresholding them at 0.5 puts the boundary on a
					// clean level-set curve rather than scattering it voxel-to-voxel; the mesher
					// then blends across that curve geometrically.
					{
						const float sand_share = CLAMP(
								(cont_transition - cont_sand_start) / MAX(cont_sand_end - cont_sand_start, 0.001f),
								0.0f, 1.0f);
						const float ocean_share = CLAMP(
								(cont_transition - cont_ocean_start) / MAX(cont_ocean_end - cont_ocean_start, 0.001f),
								0.0f, 1.0f);
						if (ocean_share >= 0.5f) {
							out.single_mat = MAT_OCEAN_FLOOR;
						} else if (sand_share >= 0.5f) {
							out.single_mat = MAT_SAND;
						} else {
							out.single_mat = land_a;
						}
					}
				}

				out.sdf = sdf;
				out.temp = temp;
				out.moist = moist;
				out.biome = biome;
				out.cont_transition = cont_transition;
				out.macro_land_mask = macro_land_mask;
				return VOXEL_FULL;
	}
}

int EdenPlanetGeneratorV1::get_used_channels_mask() const {
	return (1 << VoxelBuffer::CHANNEL_SDF) |
		   (1 << VoxelBuffer::CHANNEL_INDICES) |
		   (1 << VoxelBuffer::CHANNEL_WEIGHTS) |
		   (1 << VoxelBuffer::CHANNEL_DATA5); // baked ocean water mass, see VOXEL_FULL below
}

// ══════════════════════════════════════════════════════════════════════════════
// BILINEAR TECTONIC MAP SAMPLING
// ══════════════════════════════════════════════════════════════════════════════

float EdenPlanetGeneratorV1::_sample_tect_map(const Vector<float> &map, float map_u, float map_v) const {
	const float fu = map_u * TECT_W - 0.5f;
	const float fv = map_v * TECT_H - 0.5f;
	const float flu = Math::floor(fu);
	const float flv = Math::floor(fv);
	const float fx = fu - flu;
	const float fy = fv - flv;
	const int ix0 = int(flu);
	const int iy0 = int(flv);
	const int ix0w = ((ix0 % TECT_W) + TECT_W) % TECT_W;
	const int ix1w = (((ix0 + 1) % TECT_W) + TECT_W) % TECT_W;
	const int iy0c = CLAMP(iy0, 0, TECT_H - 1);
	const int iy1c = CLAMP(iy0 + 1, 0, TECT_H - 1);
	const int t00 = iy0c * TECT_W + ix0w;
	const int t10 = iy0c * TECT_W + ix1w;
	const int t01 = iy1c * TECT_W + ix0w;
	const int t11 = iy1c * TECT_W + ix1w;
	const float w00 = (1.0f - fx) * (1.0f - fy);
	const float w10 = fx * (1.0f - fy);
	const float w01 = (1.0f - fx) * fy;
	const float w11 = fx * fy;
	return map[t00] * w00 + map[t10] * w10 + map[t01] * w01 + map[t11] * w11;
}

// ══════════════════════════════════════════════════════════════════════════════
// CLIMATE HELPERS
// ══════════════════════════════════════════════════════════════════════════════

float EdenPlanetGeneratorV1::_temperature_fast(const Ref<FastNoiseLite> &climate_noise,
		float sx, float sy, float sz, float signed_lat, float lat, float alt, float cont,
		const Parameters &p) const {
	float temp_lat_start = MIN(p.temp_lat_falloff_start, p.temp_lat_falloff_end);
	float temp_lat_end = MAX(p.temp_lat_falloff_start, p.temp_lat_falloff_end);
	if (temp_lat_end - temp_lat_start < 0.001f) {
		temp_lat_end = temp_lat_start + 0.001f;
	}

	const float lat_noise = climate_noise->get_noise_3d(
			sx * 0.45f + 211.0f,
			sy * 0.45f - 37.0f,
			sz * 0.45f + 149.0f) * 0.12f;
	const float noisy_lat = CLAMP(lat + lat_noise, 0.0f, 1.0f);

	float warm_frac = _ss(temp_lat_start, temp_lat_end, noisy_lat);
	float mild_drop = noisy_lat * noisy_lat * 4.0f;
	float temp_range = p.equator_temperature - p.polar_temperature;
	if (temp_range < 1.0f) {
		temp_range = 1.0f;
	}
	float temp_c = p.equator_temperature - mild_drop -
			warm_frac * (p.equator_temperature - p.polar_temperature - mild_drop);

	float season_phase = Math::TAU * ((p.day_of_year - p.season_phase_day) / 365.0f);
	float lat_weight = _ss(0.08f, 0.60f, Math::abs(signed_lat));
	float hemi = signed_lat >= 0.0f ? 1.0f : -1.0f;
	temp_c += Math::sin(season_phase) * hemi * lat_weight * CLAMP(p.season_strength, 0.0f, 1.0f) * temp_range * 0.18f;

	// Ocean moderation.
	temp_c = Math::lerp(temp_c, p.ocean_mean_temp, cont * p.ocean_temp_moderation * 0.5f);

	// Continental interior amplification.
	float interior = 1.0f - cont;
	if (noisy_lat < p.continental_hot_lat_max) {
		temp_c += interior * p.continental_interior_amp;
	} else if (noisy_lat > p.continental_cold_lat_min) {
		temp_c -= interior * p.continental_interior_amp;
	}

	// Altitude lapse.
	if (alt > 0.0f) {
		temp_c -= alt * 0.001f * p.altitude_lapse_rate;
	}

	// Noise.
	temp_c += climate_noise->get_noise_3d(sx, sy, sz) * p.temp_noise_amplitude;

	// Normalize.
	return CLAMP((temp_c - p.polar_temperature) / temp_range, 0.0f, 1.0f);
}

float EdenPlanetGeneratorV1::_moisture_fast(const Ref<FastNoiseLite> &climate_noise,
		float sx, float sy, float sz, float lat, float cont, float temp,
		const Parameters &p) const {
	const float belt_noise = climate_noise->get_noise_3d(
			sx * 0.55f - 191.0f,
			sy * 0.55f + 97.0f,
			sz * 0.55f - 53.0f) * 0.10f;
	const float climate_lat = CLAMP(lat + belt_noise, 0.0f, 1.0f);

	// Maritime (cont->1) should remain wet, continental interior (cont->0) should dry out.
	float base = Math::lerp(p.interior_humidity, p.ocean_humidity, cont);

	// Subtropical dry belt.
	float subtrop_start = MIN(p.band_subtropical_start, p.band_subtropical_end);
	float subtrop_end = MAX(p.band_subtropical_start, p.band_subtropical_end);
	if (subtrop_end - subtrop_start < 0.001f) {
		subtrop_end = subtrop_start + 0.001f;
	}
	if (climate_lat > subtrop_start && climate_lat < subtrop_end) {
		const float mid = (subtrop_start + subtrop_end) * 0.5f;
		const float half_width = (subtrop_end - subtrop_start) * 0.5f;
		const float belt = 1.0f - Math::abs(climate_lat - mid) / MAX(half_width, 0.0001f);
		base -= p.subtropical_dry_factor * CLAMP(belt, 0.0f, 1.0f);
	}

	base -= climate_lat * 0.10f; // Polar dryness.
	if (climate_lat < p.band_tropical_end) {
		const float tropical_end = MAX(p.band_tropical_end, 0.0001f);
		base += (tropical_end - climate_lat) / tropical_end * 0.3f; // Tropical boost.
	}

	// Noise (offset sampling to decorrelate from temperature).
	base += climate_noise->get_noise_3d(sx * 0.6f, sy * 0.6f, sz * 0.6f + 500.0f) * p.humidity_noise_amplitude;

	// Evaporation stress: hotter continental interiors lose moisture more aggressively.
	const float interior = 1.0f - CLAMP(cont, 0.0f, 1.0f);
	const float evap = CLAMP((temp - 0.45f) / 0.55f, 0.0f, 1.0f) * (0.35f + 0.65f * interior);
	base -= evap * CLAMP(p.evaporation_strength, 0.0f, 1.0f);

	return CLAMP(base, 0.0f, 1.0f);
}

int EdenPlanetGeneratorV1::_classify_biome_fast(float temp, float moist, float cont, float lat, float alt,
		float climate_zone, float region_selector, float roll, const Parameters &p,
		int &out_biome2, float &out_blend) const {
	out_biome2 = BIOME_GRASSLAND;
	out_blend = 0.0f;
	if (cont > 0.5f) return BIOME_OCEAN;

	const float zone = CLAMP(climate_zone, -1.0f, 1.0f);
	const float tundra_temp = CLAMP(0.25f + zone * 0.06f, 0.12f, 0.40f);
	const float polar_start = CLAMP(p.band_polar_start + zone * 0.10f, 0.55f, 0.98f);
	const float alpine_start = 2800.0f + zone * 350.0f;

	float w_tundra = 1.0f - _ss(tundra_temp - 0.10f, tundra_temp + 0.08f, temp);
	w_tundra = MAX(w_tundra, _ss(polar_start - 0.10f, polar_start + 0.05f, lat));
	w_tundra = MAX(w_tundra, _ss(alpine_start - 500.0f, alpine_start + 500.0f, alt));

	const float warm_pool = CLAMP(1.0f - w_tundra, 0.0f, 1.0f);
	const float hot_temp = CLAMP(0.60f + zone * 0.06f, 0.45f, 0.80f);
	const float desert_moist = CLAMP(0.35f + zone * 0.08f, 0.20f, 0.60f);
	const float tropical_moist = CLAMP(0.45f + zone * 0.08f, 0.25f, 0.75f);
	const float hot = _ss(hot_temp - 0.10f, hot_temp + 0.10f, temp) * warm_pool;
	float w_desert = hot * (1.0f - _ss(desert_moist - 0.12f, desert_moist + 0.10f, moist));
	float w_tropical = hot * _ss(tropical_moist - 0.12f, tropical_moist + 0.10f, moist);

	const float forest_moist = CLAMP(0.45f + zone * 0.08f, 0.25f, 0.80f);
	const float moderate_pool = CLAMP(warm_pool - hot, 0.0f, 1.0f);
	float w_forest = moderate_pool * _ss(forest_moist - 0.14f, forest_moist + 0.12f, moist);
	const float meadow_pref = CLAMP(1.0f - Math::abs(moist - 0.50f) / 0.50f, 0.0f, 1.0f);
	float w_hills_meadows = moderate_pool * meadow_pref * 0.65f;
	float w_grass = CLAMP(1.0f - w_tundra - w_desert - w_tropical - w_forest - w_hills_meadows, 0.0f, 1.0f);

	// Biome placement noise: creates continent-scale patches so neighboring
	// regions can favor different biome families beyond pure latitude bands.
	const float region = CLAMP(region_selector, 0.0f, 1.0f);
	const float region_strength = CLAMP(p.biome_region_strength, 0.0f, 1.0f);
	auto region_pref = [](float r, float center) {
		const float half_width = 0.22f;
		return CLAMP(1.0f - Math::abs(r - center) / half_width, 0.0f, 1.0f);
	};
	const float pref_tundra = region_pref(region, 0.08f);
	const float pref_desert = region_pref(region, 0.28f);
	const float pref_grass = region_pref(region, 0.50f);
	const float pref_hills = region_pref(region, 0.60f);
	const float pref_forest = region_pref(region, 0.72f);
	const float pref_tropical = region_pref(region, 0.90f);
	const float mul_tundra = CLAMP(1.0f + region_strength * (pref_tundra * 2.0f - 1.0f), 0.15f, 2.25f);
	const float mul_desert = CLAMP(1.0f + region_strength * (pref_desert * 2.0f - 1.0f), 0.15f, 2.25f);
	const float mul_grass = CLAMP(1.0f + region_strength * (pref_grass * 2.0f - 1.0f), 0.15f, 2.25f);
	const float mul_hills = CLAMP(1.0f + region_strength * (pref_hills * 2.0f - 1.0f), 0.15f, 2.25f);
	const float mul_forest = CLAMP(1.0f + region_strength * (pref_forest * 2.0f - 1.0f), 0.15f, 2.25f);
	const float mul_tropical = CLAMP(1.0f + region_strength * (pref_tropical * 2.0f - 1.0f), 0.15f, 2.25f);
	w_tundra *= mul_tundra;
	w_desert *= mul_desert;
	w_tropical *= mul_tropical;
	w_forest *= mul_forest;
	w_hills_meadows *= mul_hills;
	w_grass *= mul_grass;

	// Biome spawn percentages (0..1) modulate baseline suitability.
	w_tundra *= CLAMP(p.biome_spawn_tundra, 0.0f, 1.0f);
	w_desert *= CLAMP(p.biome_spawn_desert, 0.0f, 1.0f);
	w_tropical *= CLAMP(p.biome_spawn_tropical, 0.0f, 1.0f);
	w_forest *= CLAMP(p.biome_spawn_forest, 0.0f, 1.0f);
	w_hills_meadows *= CLAMP(p.biome_spawn_hills_meadows, 0.0f, 1.0f);
	w_grass *= CLAMP(p.biome_spawn_grassland, 0.0f, 1.0f);

	const float total = w_tundra + w_desert + w_tropical + w_forest + w_hills_meadows + w_grass;
	if (total <= 0.0001f) {
		const float s_tundra = CLAMP(p.biome_spawn_tundra, 0.0f, 1.0f);
		const float s_desert = CLAMP(p.biome_spawn_desert, 0.0f, 1.0f);
		const float s_tropical = CLAMP(p.biome_spawn_tropical, 0.0f, 1.0f);
		const float s_forest = CLAMP(p.biome_spawn_forest, 0.0f, 1.0f);
		const float s_hills = CLAMP(p.biome_spawn_hills_meadows, 0.0f, 1.0f);
		const float s_grass = CLAMP(p.biome_spawn_grassland, 0.0f, 1.0f);
		const float s_total = s_tundra + s_desert + s_tropical + s_forest + s_hills + s_grass;
		if (s_total <= 0.0001f) {
			return BIOME_GRASSLAND;
		}

		const float s_pick = CLAMP(roll, 0.0f, 1.0f) * (s_total - 0.0001f);
		float s_cursor = s_tundra;
		if (s_pick < s_cursor) {
			return BIOME_TUNDRA;
		}
		s_cursor += s_desert;
		if (s_pick < s_cursor) {
			return BIOME_DESERT;
		}
		s_cursor += s_tropical;
		if (s_pick < s_cursor) {
			return BIOME_TROPICAL;
		}
		s_cursor += s_forest;
		if (s_pick < s_cursor) {
			return BIOME_FOREST;
		}
		s_cursor += s_hills;
		if (s_pick < s_cursor) {
			return BIOME_HILLS_MEADOWS;
		}
		return BIOME_GRASSLAND;
	}

	// Find top two biomes by weight for smooth detail noise blending.
	float ws6[6] = { w_tundra, w_desert, w_tropical, w_forest, w_hills_meadows, w_grass };
	const int ids6[6] = { BIOME_TUNDRA, BIOME_DESERT, BIOME_TROPICAL, BIOME_FOREST, BIOME_HILLS_MEADOWS, BIOME_GRASSLAND };
	int i1 = 0, i2 = 1;
	for (int i = 1; i < 6; ++i) {
		if (ws6[i] > ws6[i1]) { i2 = i1; i1 = i; }
		else if (i != i1 && ws6[i] > ws6[i2]) { i2 = i; }
	}
	out_biome2 = ids6[i2];
	const float sum12 = ws6[i1] + ws6[i2];
	const float raw_blend = (sum12 > 0.001f) ? (ws6[i2] / sum12) : 0.0f;
	// Suppress trivial blends (< 10% contribution) to avoid redundant noise evaluations.
	out_blend = (raw_blend > 0.10f) ? raw_blend : 0.0f;
	return ids6[i1];
}

float EdenPlanetGeneratorV1::_biome_detail_fast(float sx, float sy, float sz, int biome,
		const Ref<FastNoiseLite> &hills_noise,
		const Ref<FastNoiseLite> &desert_noise,
		const Ref<FastNoiseLite> &forest_noise,
		const Ref<FastNoiseLite> &tropical_noise,
		const Ref<FastNoiseLite> &tundra_noise,
		const Ref<FastNoiseLite> &grassland_noise,
		const Parameters &p) const {
	switch (biome) {
		case BIOME_DESERT: return desert_noise->get_noise_3d(sx, sy, sz) * p.amp_desert;
		case BIOME_FOREST: return forest_noise->get_noise_3d(sx, sy, sz) * p.amp_forest;
		case BIOME_TROPICAL: return tropical_noise->get_noise_3d(sx, sy, sz) * p.amp_tropical;
		case BIOME_TUNDRA: return tundra_noise->get_noise_3d(sx, sy, sz) * p.amp_tundra;
		case BIOME_HILLS_MEADOWS: return hills_noise->get_noise_3d(sx, sy, sz) * (p.hills_amplitude * 0.35f);
		case BIOME_GRASSLAND: return grassland_noise->get_noise_3d(sx, sy, sz) * p.amp_grassland;
		default: return 0.0f;
	}
}

float EdenPlanetGeneratorV1::_cave_carve(const Ref<FastNoiseLite> &cave_noise, float wx, float wy, float wz, float alt,
		const Parameters &p) const {
	if (alt > p.cave_max_altitude) return 0.0f;
	float near = _ss(p.cave_fade_outer, p.cave_fade_inner, Math::abs(alt));
	if (near < 0.01f) return 0.0f;
	float raw = cave_noise->get_noise_3d(wx, wy, wz);
	return _ss(p.cave_threshold_low, p.cave_threshold_high, raw) * near;
}

// ══════════════════════════════════════════════════════════════════════════════
// MATERIAL HELPERS
// ══════════════════════════════════════════════════════════════════════════════

int EdenPlanetGeneratorV1::_land_material_for(int biome, float alt, float temp, float mtn,
		const Parameters &p) const {
	if (alt >= 0.0f && alt <= p.beach_width_m) {
		return MAT_SAND;
	}

	const float mtn_norm = CLAMP(mtn / MAX(p.mountain_height, 1.0f), 0.0f, 2.0f);
	if (mtn_norm >= p.mountain_snow_start && temp < 0.68f) {
		return MAT_SNOW;
	}
	if (mtn_norm >= p.mountain_rock_start) {
		return MAT_ROCK;
	}
	if (alt > 2500.0f && temp < 0.55f) {
		return MAT_SNOW;
	}
	switch (biome) {
		case BIOME_TUNDRA: return MAT_SNOW;
		case BIOME_DESERT: return MAT_SAND;
		case BIOME_TROPICAL: return MAT_MOSS;
		case BIOME_FOREST: return MAT_DIRT;
		case BIOME_HILLS_MEADOWS: return MAT_GRASS;
		case BIOME_GRASSLAND: return MAT_GRASS;
		default: return MAT_GRASS;
	}
}

static inline int eden_biome_base_material(int biome) {
	switch (biome) {
		case EdenPlanetGeneratorV1::BIOME_TUNDRA: return EdenPlanetGeneratorV1::MAT_SNOW;
		case EdenPlanetGeneratorV1::BIOME_DESERT: return EdenPlanetGeneratorV1::MAT_SAND;
		case EdenPlanetGeneratorV1::BIOME_TROPICAL: return EdenPlanetGeneratorV1::MAT_MOSS;
		case EdenPlanetGeneratorV1::BIOME_FOREST: return EdenPlanetGeneratorV1::MAT_DIRT;
		case EdenPlanetGeneratorV1::BIOME_HILLS_MEADOWS: return EdenPlanetGeneratorV1::MAT_GRASS;
		case EdenPlanetGeneratorV1::BIOME_GRASSLAND: return EdenPlanetGeneratorV1::MAT_GRASS;
		default: return EdenPlanetGeneratorV1::MAT_GRASS;
	}
}

void EdenPlanetGeneratorV1::_land_material_blend(int biome, int biome2, float biome_blend,
		float alt, float temp, float mtn,
		const Parameters &p, int &r_mat_a, int &r_mat_b, float &r_blend_b) const {
	// Base material for this biome (same mapping _land_material_for's switch used).
	const int biome_mat = eden_biome_base_material(biome);
	const int biome_mat2 = eden_biome_base_material(biome2);
	const float b2 = CLAMP(biome_blend, 0.0f, 1.0f);

	const float mtn_norm = CLAMP(mtn / MAX(p.mountain_height, 1.0f), 0.0f, 2.0f);

	// Each of these replaces one hard `>=` threshold in _land_material_for with a smoothstep
	// band, so a voxel sitting near the boundary gets a partial weight instead of an abrupt
	// full-material flip relative to its neighbor.
	const float mtn_band = 0.08f; // half-width, in mtn_norm units
	const float cold = _ss(0.72f, 0.64f, temp); // 1 = cold enough for snow
	const float snow_mtn = _ss(p.mountain_snow_start - mtn_band, p.mountain_snow_start + mtn_band, mtn_norm) * cold;
	const float rock_mtn = _ss(p.mountain_rock_start - mtn_band, p.mountain_rock_start + mtn_band, mtn_norm);
	const float snow_alt = _ss(2200.0f, 2800.0f, alt) * _ss(0.60f, 0.50f, temp);
	// Beach: was `alt >= 0 && alt <= beach_width_m`. beach_width_m defaults to 50m, and terrain
	// altitude naturally hovers around that value across whole sloped bands, so this specific
	// threshold was the dominant source of per-voxel sand/biome-material dithering.
	const float beach = (alt >= -p.beach_width_m * 0.25f)
			? (1.0f - _ss(p.beach_width_m * 0.5f, p.beach_width_m * 1.5f, alt))
			: 0.0f;

	// Resolve to a normalized set, respecting the original priority order
	// (snow > rock > beach > biome) so behavior stays recognisable, just softened.
	const float w_snow = CLAMP(MAX(snow_mtn, snow_alt), 0.0f, 1.0f);
	const float w_rock = CLAMP(rock_mtn, 0.0f, 1.0f - w_snow);
	const float w_beach = CLAMP(beach, 0.0f, 1.0f - w_snow - w_rock);
	const float w_biome = CLAMP(1.0f - w_snow - w_rock - w_beach, 0.0f, 1.0f);

	float weights[16] = {};
	weights[MAT_SNOW] += w_snow;
	weights[MAT_ROCK] += w_rock;
	weights[MAT_SAND] += w_beach;
	// Split the biome share across the top-two biomes rather than committing entirely to the
	// argmax winner -- this is what removes the per-voxel biome flip described in the header.
	weights[biome_mat] += w_biome * (1.0f - b2);
	weights[biome_mat2] += w_biome * b2;

	// Pick the two strongest materials.
	int best_a = MAT_GRASS, best_b = MAT_GRASS;
	float wa = -1.0f, wb = -1.0f;
	for (int i = 0; i < 16; ++i) {
		if (weights[i] > wa) {
			wb = wa;
			best_b = best_a;
			wa = weights[i];
			best_a = i;
		} else if (weights[i] > wb) {
			wb = weights[i];
			best_b = i;
		}
	}

	r_mat_a = best_a;
	r_mat_b = (wb > 0.0f) ? best_b : best_a;
	r_blend_b = (wa + wb > 1e-6f) ? CLAMP(wb / (wa + wb), 0.0f, 1.0f) : 0.0f;
}

void EdenPlanetGeneratorV1::_pack_mixel4(int land_mat, int land_mat_b, float land_blend_b, int ocean_mat, float cont,
		float sand_start, float sand_end,
		float ocean_start, float ocean_end,
		int &r_indices, int &r_weights) {
	float sand_width = MAX(sand_end - sand_start, 0.001f);
	float ocean_width = MAX(ocean_end - ocean_start, 0.001f);
	float sand_up = CLAMP((cont - sand_start) / sand_width, 0.0f, 1.0f);
	float ocean_up = CLAMP((cont - ocean_start) / ocean_width, 0.0f, 1.0f);
	// EDEN FORK: `cont` (continent transition) carries real, continuous noise (continent_warp
	// etc.) even far inland, well below sand_start -- rare enough not to change the visible
	// biome/height there, but frequent enough that Math::round() below occasionally rounds a
	// near-zero-but-nonzero sand_up/ocean_up up to weight 1 (out of 15) instead of 0. That
	// single-voxel weight flicker was enough to make a VoxelBuffer block fail
	// VoxelBuffer::is_uniform() and fall into the mesher's per-cell reselection path across
	// ~30% of all cells scene-wide (confirmed via mesher-side instrumentation counting how often
	// that path fires), which is what was producing a persistent fine speckle/checkerboard even
	// in areas a sparser point-sample probe found completely clean. Snapping near-zero blend
	// fractions to exactly zero keeps genuinely-inland voxels bit-for-bit identical to their
	// neighbors, so those blocks stay uniform and skip the noisy path entirely; only real
	// coastal transitions (well above this deadzone) are unaffected.
	if (sand_up < 0.05f) {
		sand_up = 0.0f;
	}
	if (ocean_up < 0.05f) {
		ocean_up = 0.0f;
	}

	int o4 = CLAMP(int(Math::round(ocean_up * 15.0f)), 0, 15);
	int s4 = CLAMP(int(Math::round(sand_up * 15.0f)), 0, 15 - o4);
	int l4 = 15 - o4 - s4;

	int sand_mat = MAT_SAND;

	// Split the land portion between the primary and secondary land material (see
	// _land_material_blend). Slot 3 used to always be a zero-weight filler, so carrying a real
	// second land material there is free -- no voxel-format, mesher, or shader change needed.
	// All the folding below exists because MIXEL4 requires the 4 slot indices to be DISTINCT
	// (see mixel4::debug_check_texture_indices): whenever the secondary material collides with
	// a slot that's already present, its weight is merged into that slot instead of duplicated.
	// Every branch only moves weight between slots, so the total stays exactly 15.
	int l4_b = CLAMP(int(Math::round(float(l4) * CLAMP(land_blend_b, 0.0f, 1.0f))), 0, l4);
	int l4_a = l4 - l4_b;
	if (land_mat_b == land_mat) {
		l4_a += l4_b;
		l4_b = 0;
	}

	if (land_mat == MAT_SAND) {
		s4 += l4_a;
		l4_a = 0;
	}
	if (l4_b > 0 && land_mat_b == sand_mat) {
		s4 += l4_b;
		l4_b = 0;
	}
	if (l4_b > 0 && land_mat_b == ocean_mat) {
		o4 += l4_b;
		l4_b = 0;
	}

	// Find filler material IDs (ones not used by land/sand/ocean, nor by the secondary land
	// material while it still carries weight).
	int filler0 = -1, filler1 = -1;
	for (int i = 0; i < 16; i++) {
		if (i != land_mat && i != sand_mat && i != ocean_mat && !(l4_b > 0 && i == land_mat_b)) {
			if (filler0 < 0) {
				filler0 = i;
			} else if (filler1 < 0) {
				filler1 = i;
				break;
			}
		}
	}

	int idx0 = (land_mat != sand_mat) ? land_mat : filler0;
	int idx3;
	if (l4_b > 0) {
		idx3 = land_mat_b;
	} else {
		idx3 = (land_mat != sand_mat) ? filler0 : filler1;
	}

	r_indices = (idx0 & 0xF) |
			((sand_mat & 0xF) << 4) |
			((ocean_mat & 0xF) << 8) |
			((idx3 & 0xF) << 12);

	r_weights = (l4_a & 0xF) |
			((s4 & 0xF) << 4) |
			((o4 & 0xF) << 8) |
			((l4_b & 0xF) << 12);
}

// ══════════════════════════════════════════════════════════════════════════════
// ACCESSORS
// ══════════════════════════════════════════════════════════════════════════════

Ref<PlanetTectonics> EdenPlanetGeneratorV1::get_tectonics() const {
	return _tectonics;
}

void EdenPlanetGeneratorV1::set_climate_profile(const Ref<EdenPlanetClimateProfile> &p_profile) {
	Ref<EdenPlanetClimateProfile> old_profile;
	{
		zylann::RWLockWrite wlock(_parameters_lock);
		old_profile = _climate_profile;
		_climate_profile = p_profile;
	}
	if (old_profile.is_valid()) {
		old_profile->disconnect("changed", callable_mp(this, &EdenPlanetGeneratorV1::_on_climate_profile_changed));
	}
	if (p_profile.is_valid()) {
		p_profile->connect("changed", callable_mp(this, &EdenPlanetGeneratorV1::_on_climate_profile_changed));
	}
	emit_changed();
}

Ref<EdenPlanetClimateProfile> EdenPlanetGeneratorV1::get_climate_profile() const {
	zylann::RWLockRead rlock(_parameters_lock);
	return _climate_profile;
}

void EdenPlanetGeneratorV1::_apply_climate_profile(Parameters &p, const Ref<EdenPlanetClimateProfile> &profile) const {
	if (profile.is_null()) {
		return;
	}
	p.axis_tilt = profile->get_axis_tilt();
	p.tilt_direction = profile->get_tilt_direction();
	p.equator_temperature = profile->get_equator_temperature();
	p.polar_temperature = profile->get_polar_temperature();
	p.altitude_lapse_rate = profile->get_altitude_lapse_rate();
	p.ocean_temp_moderation = profile->get_ocean_temp_moderation();
	p.ocean_mean_temp = profile->get_ocean_mean_temp();
	p.continental_interior_amp = profile->get_continental_interior_amp();
	p.temp_noise_amplitude = profile->get_temp_noise_amplitude();
	p.ocean_humidity = profile->get_ocean_humidity();
	p.interior_humidity = profile->get_interior_humidity();
	p.subtropical_dry_factor = profile->get_subtropical_dry_factor();
	p.humidity_noise_amplitude = profile->get_humidity_noise_amplitude();
	p.evaporation_strength = profile->get_evaporation_strength();
	p.temp_lat_falloff_start = profile->get_temp_lat_falloff_start();
	p.temp_lat_falloff_end = profile->get_temp_lat_falloff_end();
	p.continental_hot_lat_max = profile->get_continental_hot_lat_max();
	p.continental_cold_lat_min = profile->get_continental_cold_lat_min();
	p.band_tropical_end = profile->get_band_tropical_end();
	p.band_subtropical_start = profile->get_band_subtropical_start();
	p.band_subtropical_end = profile->get_band_subtropical_end();
	p.band_polar_start = profile->get_band_polar_start();
	p.biome_spawn_desert = profile->get_biome_spawn_desert();
	p.biome_spawn_forest = profile->get_biome_spawn_forest();
	p.biome_spawn_tropical = profile->get_biome_spawn_tropical();
	p.biome_spawn_tundra = profile->get_biome_spawn_tundra();
	p.biome_spawn_grassland = profile->get_biome_spawn_grassland();
	p.day_of_year = profile->get_day_of_year();
	p.season_strength = profile->get_season_strength();
	p.season_phase_day = profile->get_season_phase_day();

	float tilt_rad = Math::deg_to_rad(p.axis_tilt);
	float dir_rad = Math::deg_to_rad(p.tilt_direction);
	p.tilt_axis = Vector3(
			Math::sin(tilt_rad) * Math::sin(dir_rad),
			Math::cos(tilt_rad),
			Math::sin(tilt_rad) * Math::cos(dir_rad))
					 .normalized();
}

void EdenPlanetGeneratorV1::_on_climate_profile_changed() {
	emit_changed();
}

void EdenPlanetGeneratorV1::set_land_coverage(float v) {
	v = CLAMP(v, 0.0f, 1.0f);
	{
		zylann::RWLockWrite wlock(_parameters_lock);
		_parameters.land_coverage = v;
		// 0 = ocean world, 1 = land world.
		_parameters.continent_ratio_bias = (v - 0.5f) * 0.40f;
	}
	emit_changed();
}

float EdenPlanetGeneratorV1::get_land_coverage() const {
	zylann::RWLockRead rlock(_parameters_lock);
	return _parameters.land_coverage;
}

void EdenPlanetGeneratorV1::set_continent_size(float v) {
	v = CLAMP(v, 0.0f, 1.0f);
	{
		zylann::RWLockWrite wlock(_parameters_lock);
		_parameters.continent_size = v;
		// Larger value => smoother/larger continental masses.
		_parameters.continent_shape_lowfreq_mix = v;
		// Lower frequency yields larger continents.
		_parameters.continent_noise_freq = Math::lerp(0.00035f, 0.00006f, v);
		_parameters.is_setup = false;
	}
	emit_changed();
}

float EdenPlanetGeneratorV1::get_continent_size() const {
	zylann::RWLockRead rlock(_parameters_lock);
	return _parameters.continent_size;
}

void EdenPlanetGeneratorV1::set_biome_patch_size(float v) {
	v = CLAMP(v, 0.0f, 1.0f);
	{
		zylann::RWLockWrite wlock(_parameters_lock);
		_parameters.biome_patch_size = v;
		// Lower frequency => larger biome patches.
		_parameters.biome_region_noise_freq = Math::lerp(0.00025f, 0.000015f, v);
		_parameters.is_setup = false;
	}
	emit_changed();
}

float EdenPlanetGeneratorV1::get_biome_patch_size() const {
	zylann::RWLockRead rlock(_parameters_lock);
	return _parameters.biome_patch_size;
}

void EdenPlanetGeneratorV1::set_biome_patch_strength(float v) {
	v = CLAMP(v, 0.0f, 1.0f);
	{
		zylann::RWLockWrite wlock(_parameters_lock);
		_parameters.biome_patch_strength = v;
		_parameters.biome_region_strength = v;
	}
	emit_changed();
}

float EdenPlanetGeneratorV1::get_biome_patch_strength() const {
	zylann::RWLockRead rlock(_parameters_lock);
	return _parameters.biome_patch_strength;
}

void EdenPlanetGeneratorV1::set_terrain_variation(float v) {
	v = CLAMP(v, 0.0f, 1.0f);
	{
		zylann::RWLockWrite wlock(_parameters_lock);
		_parameters.terrain_variation = v;
		_parameters.hills_amplitude = Math::lerp(120.0f, 520.0f, v);
		_parameters.continental_relief_strength = Math::lerp(0.15f, 1.00f, v);
	}
	emit_changed();
}

float EdenPlanetGeneratorV1::get_terrain_variation() const {
	zylann::RWLockRead rlock(_parameters_lock);
	return _parameters.terrain_variation;
}

// Macro for repetitive float property getters/setters.
// emit_changed() is called outside the lock so the editor / Resource system
// knows the resource was modified (inspector dirty flag, undo-redo, etc.).
#define EDEN_FLOAT_PROP(name) \
	void EdenPlanetGeneratorV1::set_##name(float v) { \
		{ \
			zylann::RWLockWrite wlock(_parameters_lock); \
			_parameters.name = v; \
		} \
		emit_changed(); \
	} \
	float EdenPlanetGeneratorV1::get_##name() const { \
		zylann::RWLockRead rlock(_parameters_lock); \
		return _parameters.name; \
	}

#define EDEN_INT_PROP(name) \
	void EdenPlanetGeneratorV1::set_##name(int v) { \
		{ \
			zylann::RWLockWrite wlock(_parameters_lock); \
			_parameters.name = v; \
		} \
		emit_changed(); \
	} \
	int EdenPlanetGeneratorV1::get_##name() const { \
		zylann::RWLockRead rlock(_parameters_lock); \
		return _parameters.name; \
	}

#define EDEN_BOOL_PROP(name) \
	void EdenPlanetGeneratorV1::set_##name(bool v) { \
		{ \
			zylann::RWLockWrite wlock(_parameters_lock); \
			_parameters.name = v; \
		} \
		emit_changed(); \
	} \
	bool EdenPlanetGeneratorV1::get_##name() const { \
		zylann::RWLockRead rlock(_parameters_lock); \
		return _parameters.name; \
	}

// _SETUP variants: also clear is_setup so that setup() re-runs on next
// generate_block(). Use for params that affect noise creation, tectonics,
// or tectonic map baking.
#define EDEN_FLOAT_PROP_SETUP(name) \
	void EdenPlanetGeneratorV1::set_##name(float v) { \
		{ \
			zylann::RWLockWrite wlock(_parameters_lock); \
			_parameters.name = v; \
			_parameters.is_setup = false; \
		} \
		emit_changed(); \
	} \
	float EdenPlanetGeneratorV1::get_##name() const { \
		zylann::RWLockRead rlock(_parameters_lock); \
		return _parameters.name; \
	}

#define EDEN_INT_PROP_SETUP(name) \
	void EdenPlanetGeneratorV1::set_##name(int v) { \
		{ \
			zylann::RWLockWrite wlock(_parameters_lock); \
			_parameters.name = v; \
			_parameters.is_setup = false; \
		} \
		emit_changed(); \
	} \
	int EdenPlanetGeneratorV1::get_##name() const { \
		zylann::RWLockRead rlock(_parameters_lock); \
		return _parameters.name; \
	}

#define EDEN_BOOL_PROP_SETUP(name) \
	void EdenPlanetGeneratorV1::set_##name(bool v) { \
		{ \
			zylann::RWLockWrite wlock(_parameters_lock); \
			_parameters.name = v; \
			_parameters.is_setup = false; \
		} \
		emit_changed(); \
	} \
	bool EdenPlanetGeneratorV1::get_##name() const { \
		zylann::RWLockRead rlock(_parameters_lock); \
		return _parameters.name; \
	}

// ── generate_block()-only params (no setup needed) ──────────────────────
EDEN_FLOAT_PROP(planet_radius)
EDEN_FLOAT_PROP(max_terrain_height)
EDEN_FLOAT_PROP(continent_height)
EDEN_FLOAT_PROP(ocean_depth)
EDEN_FLOAT_PROP(continent_amplitude)
EDEN_FLOAT_PROP(continent_falloff)
EDEN_FLOAT_PROP(continent_lowfreq_mix)
EDEN_FLOAT_PROP(tectonic_influence)
EDEN_FLOAT_PROP(tectonic_line_width_km)
// kept for .tres backwards compat
EDEN_FLOAT_PROP(continent_lo)
EDEN_FLOAT_PROP(continent_hi)
EDEN_FLOAT_PROP(continent_ratio_bias)
EDEN_FLOAT_PROP(continent_shape_lowfreq_mix)
EDEN_FLOAT_PROP(cont_sand_start)
EDEN_FLOAT_PROP(cont_sand_end)
EDEN_FLOAT_PROP(cont_ocean_start)
EDEN_FLOAT_PROP(cont_ocean_end)
EDEN_FLOAT_PROP(shoreline_material_width_scale)
EDEN_FLOAT_PROP(shoreline_elevation_falloff_width)
EDEN_FLOAT_PROP(shoreline_elevation_falloff_power)
EDEN_FLOAT_PROP(shoreline_shape_width)
EDEN_FLOAT_PROP(shoreline_shape_strength)
EDEN_FLOAT_PROP(shoreline_land_shape_width)
EDEN_FLOAT_PROP(shoreline_land_shape_strength)
EDEN_FLOAT_PROP(shoreline_ocean_shape_width)
EDEN_FLOAT_PROP(shoreline_ocean_shape_strength)
EDEN_FLOAT_PROP(beach_width_m)
EDEN_FLOAT_PROP(hills_amplitude)
EDEN_FLOAT_PROP(hills_meadow_power)
EDEN_FLOAT_PROP(continental_relief_strength)
EDEN_FLOAT_PROP(mountain_height)
EDEN_FLOAT_PROP(mountain_plate_boost)
EDEN_FLOAT_PROP(mountain_plate_min)
EDEN_FLOAT_PROP(mountain_landmask_strength)
EDEN_FLOAT_PROP(mountain_rock_start)
EDEN_FLOAT_PROP(mountain_snow_start)
EDEN_FLOAT_PROP(amp_desert)
EDEN_FLOAT_PROP(amp_forest)
EDEN_FLOAT_PROP(amp_tropical)
EDEN_FLOAT_PROP(amp_tundra)
EDEN_FLOAT_PROP(amp_grassland)
EDEN_FLOAT_PROP(biome_spawn_desert)
EDEN_FLOAT_PROP(biome_spawn_forest)
EDEN_FLOAT_PROP(biome_spawn_tropical)
EDEN_FLOAT_PROP(biome_spawn_tundra)
EDEN_FLOAT_PROP(biome_spawn_hills_meadows)
EDEN_FLOAT_PROP(biome_spawn_grassland)
EDEN_FLOAT_PROP(biome_region_strength)
EDEN_BOOL_PROP(single_material_mode)
EDEN_BOOL_PROP(enable_caves)
EDEN_FLOAT_PROP(cave_carve_strength)
EDEN_FLOAT_PROP(cave_max_altitude)
EDEN_FLOAT_PROP(cave_fade_outer)
EDEN_FLOAT_PROP(cave_fade_inner)
EDEN_FLOAT_PROP(cave_threshold_low)
EDEN_FLOAT_PROP(cave_threshold_high)
EDEN_FLOAT_PROP(equator_temperature)
EDEN_FLOAT_PROP(polar_temperature)
EDEN_FLOAT_PROP(altitude_lapse_rate)
EDEN_FLOAT_PROP(ocean_temp_moderation)
EDEN_FLOAT_PROP(ocean_mean_temp)
EDEN_FLOAT_PROP(continental_interior_amp)
EDEN_FLOAT_PROP(temp_noise_amplitude)
EDEN_FLOAT_PROP(ocean_humidity)
EDEN_FLOAT_PROP(interior_humidity)
EDEN_FLOAT_PROP(subtropical_dry_factor)
EDEN_FLOAT_PROP(humidity_noise_amplitude)
EDEN_FLOAT_PROP(evaporation_strength)
EDEN_FLOAT_PROP(temp_lat_falloff_start)
EDEN_FLOAT_PROP(temp_lat_falloff_end)
EDEN_FLOAT_PROP(continental_hot_lat_max)
EDEN_FLOAT_PROP(continental_cold_lat_min)
EDEN_FLOAT_PROP(band_tropical_end)
EDEN_FLOAT_PROP(band_subtropical_start)
EDEN_FLOAT_PROP(band_subtropical_end)
EDEN_FLOAT_PROP(band_polar_start)
EDEN_FLOAT_PROP(day_of_year)
EDEN_FLOAT_PROP(season_strength)
EDEN_FLOAT_PROP(season_phase_day)

// ── Params that require setup() re-run (noise, tectonics, map baking) ───
EDEN_INT_PROP_SETUP(seed_val)
EDEN_INT_PROP_SETUP(num_plates)
EDEN_INT_PROP_SETUP(num_voronoi_points)
EDEN_FLOAT_PROP_SETUP(oceanic_fraction)
EDEN_INT_PROP_SETUP(plate_border_hops)
EDEN_FLOAT_PROP_SETUP(plate_collision_threshold)
EDEN_FLOAT_PROP_SETUP(plate_noise_freq)
EDEN_INT_PROP_SETUP(plate_noise_octaves)
EDEN_FLOAT_PROP_SETUP(plate_noise_gain)
EDEN_FLOAT_PROP_SETUP(plate_noise_lacunarity)
EDEN_FLOAT_PROP_SETUP(plate_warp_strength)
EDEN_FLOAT_PROP_SETUP(plate_warp_freq)
EDEN_FLOAT_PROP_SETUP(mountain_falloff_start)
EDEN_FLOAT_PROP_SETUP(mountain_falloff_end)
EDEN_FLOAT_PROP_SETUP(mountain_sharpness)
EDEN_FLOAT_PROP_SETUP(trench_depth)
EDEN_FLOAT_PROP_SETUP(rift_depth)
EDEN_FLOAT_PROP_SETUP(valley_falloff_start)
EDEN_FLOAT_PROP_SETUP(valley_falloff_end)
EDEN_FLOAT_PROP_SETUP(axis_tilt)
EDEN_FLOAT_PROP_SETUP(tilt_direction)
EDEN_FLOAT_PROP_SETUP(biome_region_noise_freq)
EDEN_INT_PROP_SETUP(biome_region_seed_offset)

// Per-layer noise configs (all need setup)
EDEN_INT_PROP_SETUP(continent_noise_type)
EDEN_FLOAT_PROP_SETUP(continent_noise_freq)
EDEN_INT_PROP_SETUP(continent_noise_octaves)
EDEN_FLOAT_PROP_SETUP(continent_noise_gain)
EDEN_FLOAT_PROP_SETUP(continent_noise_lacunarity)
EDEN_INT_PROP_SETUP(continent_fractal_type)
EDEN_BOOL_PROP_SETUP(continent_warp_enabled)
EDEN_FLOAT_PROP_SETUP(continent_warp_freq)
EDEN_FLOAT_PROP_SETUP(continent_warp_amp)

EDEN_INT_PROP_SETUP(hills_noise_type)
EDEN_FLOAT_PROP_SETUP(hills_noise_freq)
EDEN_INT_PROP_SETUP(hills_noise_octaves)
EDEN_FLOAT_PROP_SETUP(hills_noise_gain)
EDEN_FLOAT_PROP_SETUP(hills_noise_lacunarity)
EDEN_INT_PROP_SETUP(hills_fractal_type)

EDEN_INT_PROP_SETUP(mountain_noise_type)
EDEN_FLOAT_PROP_SETUP(mountain_noise_freq)
EDEN_INT_PROP_SETUP(mountain_noise_octaves)
EDEN_FLOAT_PROP_SETUP(mountain_noise_gain)
EDEN_FLOAT_PROP_SETUP(mountain_noise_lacunarity)
EDEN_INT_PROP_SETUP(mountain_fractal_type)

EDEN_INT_PROP_SETUP(climate_noise_type)
EDEN_FLOAT_PROP_SETUP(climate_noise_freq)
EDEN_INT_PROP_SETUP(climate_noise_octaves)
EDEN_FLOAT_PROP_SETUP(climate_noise_gain)
EDEN_FLOAT_PROP_SETUP(climate_noise_lacunarity)
EDEN_INT_PROP_SETUP(climate_fractal_type)

EDEN_INT_PROP_SETUP(desert_noise_type)
EDEN_FLOAT_PROP_SETUP(desert_noise_freq)
EDEN_INT_PROP_SETUP(desert_noise_octaves)
EDEN_FLOAT_PROP_SETUP(desert_noise_gain)
EDEN_FLOAT_PROP_SETUP(desert_noise_lacunarity)
EDEN_INT_PROP_SETUP(desert_fractal_type)

EDEN_INT_PROP_SETUP(forest_noise_type)
EDEN_FLOAT_PROP_SETUP(forest_noise_freq)
EDEN_INT_PROP_SETUP(forest_noise_octaves)
EDEN_FLOAT_PROP_SETUP(forest_noise_gain)
EDEN_FLOAT_PROP_SETUP(forest_noise_lacunarity)
EDEN_INT_PROP_SETUP(forest_fractal_type)

EDEN_INT_PROP_SETUP(tropical_noise_type)
EDEN_FLOAT_PROP_SETUP(tropical_noise_freq)
EDEN_INT_PROP_SETUP(tropical_noise_octaves)
EDEN_FLOAT_PROP_SETUP(tropical_noise_gain)
EDEN_FLOAT_PROP_SETUP(tropical_noise_lacunarity)
EDEN_INT_PROP_SETUP(tropical_fractal_type)

EDEN_INT_PROP_SETUP(tundra_noise_type)
EDEN_FLOAT_PROP_SETUP(tundra_noise_freq)
EDEN_INT_PROP_SETUP(tundra_noise_octaves)
EDEN_FLOAT_PROP_SETUP(tundra_noise_gain)
EDEN_FLOAT_PROP_SETUP(tundra_noise_lacunarity)
EDEN_INT_PROP_SETUP(tundra_fractal_type)

EDEN_INT_PROP_SETUP(grassland_noise_type)
EDEN_FLOAT_PROP_SETUP(grassland_noise_freq)
EDEN_INT_PROP_SETUP(grassland_noise_octaves)
EDEN_FLOAT_PROP_SETUP(grassland_noise_gain)
EDEN_FLOAT_PROP_SETUP(grassland_noise_lacunarity)
EDEN_INT_PROP_SETUP(grassland_fractal_type)

EDEN_INT_PROP_SETUP(cave_noise_type)
EDEN_FLOAT_PROP_SETUP(cave_noise_freq)
EDEN_INT_PROP_SETUP(cave_noise_octaves)
EDEN_FLOAT_PROP_SETUP(cave_noise_gain)
EDEN_FLOAT_PROP_SETUP(cave_noise_lacunarity)
EDEN_INT_PROP_SETUP(cave_fractal_type)

#undef EDEN_FLOAT_PROP
#undef EDEN_INT_PROP
#undef EDEN_BOOL_PROP
#undef EDEN_FLOAT_PROP_SETUP
#undef EDEN_INT_PROP_SETUP
#undef EDEN_BOOL_PROP_SETUP

// ══════════════════════════════════════════════════════════════════════════════
// BINDINGS
// ══════════════════════════════════════════════════════════════════════════════

// Macro for binding a float property with a group.
#define BIND_FLOAT(name, hint_str) \
	ClassDB::bind_method(D_METHOD("set_" #name, "value"), &EdenPlanetGeneratorV1::set_##name); \
	ClassDB::bind_method(D_METHOD("get_" #name), &EdenPlanetGeneratorV1::get_##name);

#define BIND_INT(name) \
	ClassDB::bind_method(D_METHOD("set_" #name, "value"), &EdenPlanetGeneratorV1::set_##name); \
	ClassDB::bind_method(D_METHOD("get_" #name), &EdenPlanetGeneratorV1::get_##name);

#define BIND_BOOL(name) \
	ClassDB::bind_method(D_METHOD("set_" #name, "value"), &EdenPlanetGeneratorV1::set_##name); \
	ClassDB::bind_method(D_METHOD("get_" #name), &EdenPlanetGeneratorV1::get_##name);

void EdenPlanetGeneratorV1::_bind_methods() {
	ClassDB::bind_method(D_METHOD("setup"), &EdenPlanetGeneratorV1::setup);
	ClassDB::bind_method(D_METHOD("sample_dominant_material", "dir"), &EdenPlanetGeneratorV1::sample_dominant_material);
	ClassDB::bind_method(D_METHOD("sample_biome_at", "dir"), &EdenPlanetGeneratorV1::sample_biome_at);

	// MAT_* were previously C++-only constants, forcing every GDScript caller (demo scripts,
	// VoxelInstanceGenerator.voxel_texture_filter_array setup) to duplicate the same magic
	// numbers by hand with no compiler check they stayed in sync with this class. Binding them
	// once here is the single source of truth from now on -- e.g. EdenPlanetGeneratorV1.MAT_DIRT.
	BIND_CONSTANT(MAT_GRASS);
	BIND_CONSTANT(MAT_ROCK);
	BIND_CONSTANT(MAT_SNOW);
	BIND_CONSTANT(MAT_SAND);
	BIND_CONSTANT(MAT_DIRT);
	BIND_CONSTANT(MAT_MOSS);
	BIND_CONSTANT(MAT_OCEAN_FLOOR);
	ClassDB::bind_method(D_METHOD("get_tectonics"), &EdenPlanetGeneratorV1::get_tectonics);
	ClassDB::bind_method(D_METHOD("set_climate_profile", "profile"), &EdenPlanetGeneratorV1::set_climate_profile);
	ClassDB::bind_method(D_METHOD("get_climate_profile"), &EdenPlanetGeneratorV1::get_climate_profile);

	// Planet core
	BIND_FLOAT(planet_radius, "");
	BIND_INT(seed_val);
	BIND_FLOAT(max_terrain_height, "");
	BIND_FLOAT(land_coverage, "");
	BIND_FLOAT(continent_size, "");
	BIND_FLOAT(biome_patch_size, "");
	BIND_FLOAT(biome_patch_strength, "");
	BIND_FLOAT(terrain_variation, "");

	// Tectonic plates
	BIND_INT(num_plates);
	BIND_INT(num_voronoi_points);
	BIND_FLOAT(oceanic_fraction, "");
	BIND_INT(plate_border_hops);
	BIND_FLOAT(plate_collision_threshold, "");
	BIND_FLOAT(plate_noise_freq, "");
	BIND_INT(plate_noise_octaves);
	BIND_FLOAT(plate_noise_gain, "");
	BIND_FLOAT(plate_noise_lacunarity, "");
	BIND_FLOAT(plate_warp_strength, "");
	BIND_FLOAT(plate_warp_freq, "");

	// Continent shape
	BIND_FLOAT(continent_height, "");
	BIND_FLOAT(ocean_depth, "");
	BIND_FLOAT(continent_amplitude, "");
	BIND_FLOAT(continent_falloff, "");
	BIND_FLOAT(continent_lowfreq_mix, "");
	BIND_FLOAT(tectonic_influence, "");
	BIND_FLOAT(tectonic_line_width_km, "");
	// kept for .tres backwards compat
	BIND_FLOAT(continent_lo, "");
	BIND_FLOAT(continent_hi, "");
	BIND_FLOAT(continent_ratio_bias, "");
	BIND_FLOAT(continent_shape_lowfreq_mix, "");
	BIND_FLOAT(cont_sand_start, "");
	BIND_FLOAT(cont_sand_end, "");
	BIND_FLOAT(cont_ocean_start, "");
	BIND_FLOAT(cont_ocean_end, "");
	BIND_FLOAT(shoreline_material_width_scale, "");
	BIND_FLOAT(shoreline_elevation_falloff_width, "");
	BIND_FLOAT(shoreline_elevation_falloff_power, "");
	BIND_FLOAT(shoreline_shape_width, "");
	BIND_FLOAT(shoreline_shape_strength, "");
	BIND_FLOAT(shoreline_land_shape_width, "");
	BIND_FLOAT(shoreline_land_shape_strength, "");
	BIND_FLOAT(shoreline_ocean_shape_width, "");
	BIND_FLOAT(shoreline_ocean_shape_strength, "");
	BIND_FLOAT(beach_width_m, "");

	// Hills
	BIND_FLOAT(hills_amplitude, "");
	BIND_FLOAT(hills_meadow_power, "");
	BIND_FLOAT(continental_relief_strength, "");

	// Mountains
	BIND_FLOAT(mountain_height, "");
	BIND_FLOAT(mountain_plate_boost, "");
	BIND_FLOAT(mountain_plate_min, "");
	BIND_FLOAT(mountain_landmask_strength, "");
	BIND_FLOAT(mountain_rock_start, "");
	BIND_FLOAT(mountain_snow_start, "");
	BIND_FLOAT(mountain_falloff_start, "");
	BIND_FLOAT(mountain_falloff_end, "");
	BIND_FLOAT(mountain_sharpness, "");

	// Valleys
	BIND_FLOAT(trench_depth, "");
	BIND_FLOAT(rift_depth, "");
	BIND_FLOAT(valley_falloff_start, "");
	BIND_FLOAT(valley_falloff_end, "");

	// Climate
	BIND_FLOAT(axis_tilt, "");
	BIND_FLOAT(tilt_direction, "");
	BIND_FLOAT(equator_temperature, "");
	BIND_FLOAT(polar_temperature, "");
	BIND_FLOAT(altitude_lapse_rate, "");
	BIND_FLOAT(ocean_temp_moderation, "");
	BIND_FLOAT(ocean_mean_temp, "");
	BIND_FLOAT(continental_interior_amp, "");
	BIND_FLOAT(temp_noise_amplitude, "");
	BIND_FLOAT(ocean_humidity, "");
	BIND_FLOAT(interior_humidity, "");
	BIND_FLOAT(subtropical_dry_factor, "");
	BIND_FLOAT(humidity_noise_amplitude, "");
	BIND_FLOAT(evaporation_strength, "");
	BIND_FLOAT(temp_lat_falloff_start, "");
	BIND_FLOAT(temp_lat_falloff_end, "");
	BIND_FLOAT(continental_hot_lat_max, "");
	BIND_FLOAT(continental_cold_lat_min, "");
	BIND_FLOAT(band_tropical_end, "");
	BIND_FLOAT(band_subtropical_start, "");
	BIND_FLOAT(band_subtropical_end, "");
	BIND_FLOAT(band_polar_start, "");
	BIND_FLOAT(day_of_year, "");
	BIND_FLOAT(season_strength, "");
	BIND_FLOAT(season_phase_day, "");

	// Biome detail
	BIND_FLOAT(amp_desert, "");
	BIND_FLOAT(amp_forest, "");
	BIND_FLOAT(amp_tropical, "");
	BIND_FLOAT(amp_tundra, "");
	BIND_FLOAT(amp_grassland, "");
	BIND_FLOAT(biome_spawn_desert, "");
	BIND_FLOAT(biome_spawn_forest, "");
	BIND_FLOAT(biome_spawn_tropical, "");
	BIND_FLOAT(biome_spawn_tundra, "");
	BIND_FLOAT(biome_spawn_hills_meadows, "");
	BIND_FLOAT(biome_spawn_grassland, "");
	BIND_FLOAT(biome_region_noise_freq, "");
	BIND_FLOAT(biome_region_strength, "");
	BIND_INT(biome_region_seed_offset);

	// Caves
	BIND_BOOL(single_material_mode);
	BIND_BOOL(enable_caves);
	BIND_FLOAT(cave_carve_strength, "");
	BIND_FLOAT(cave_max_altitude, "");
	BIND_FLOAT(cave_fade_outer, "");
	BIND_FLOAT(cave_fade_inner, "");
	BIND_FLOAT(cave_threshold_low, "");
	BIND_FLOAT(cave_threshold_high, "");

	// ── Per-layer noise bindings ────────────────────────────────────────
	// Continent noise
	BIND_INT(continent_noise_type);
	BIND_FLOAT(continent_noise_freq, "");
	BIND_INT(continent_noise_octaves);
	BIND_FLOAT(continent_noise_gain, "");
	BIND_FLOAT(continent_noise_lacunarity, "");
	BIND_INT(continent_fractal_type);
	BIND_BOOL(continent_warp_enabled);
	BIND_FLOAT(continent_warp_freq, "");
	BIND_FLOAT(continent_warp_amp, "");

	// Hills noise
	BIND_INT(hills_noise_type);
	BIND_FLOAT(hills_noise_freq, "");
	BIND_INT(hills_noise_octaves);
	BIND_FLOAT(hills_noise_gain, "");
	BIND_FLOAT(hills_noise_lacunarity, "");
	BIND_INT(hills_fractal_type);

	// Mountain noise
	BIND_INT(mountain_noise_type);
	BIND_FLOAT(mountain_noise_freq, "");
	BIND_INT(mountain_noise_octaves);
	BIND_FLOAT(mountain_noise_gain, "");
	BIND_FLOAT(mountain_noise_lacunarity, "");
	BIND_INT(mountain_fractal_type);

	// Climate noise
	BIND_INT(climate_noise_type);
	BIND_FLOAT(climate_noise_freq, "");
	BIND_INT(climate_noise_octaves);
	BIND_FLOAT(climate_noise_gain, "");
	BIND_FLOAT(climate_noise_lacunarity, "");
	BIND_INT(climate_fractal_type);

	// Desert noise
	BIND_INT(desert_noise_type);
	BIND_FLOAT(desert_noise_freq, "");
	BIND_INT(desert_noise_octaves);
	BIND_FLOAT(desert_noise_gain, "");
	BIND_FLOAT(desert_noise_lacunarity, "");
	BIND_INT(desert_fractal_type);

	// Forest noise
	BIND_INT(forest_noise_type);
	BIND_FLOAT(forest_noise_freq, "");
	BIND_INT(forest_noise_octaves);
	BIND_FLOAT(forest_noise_gain, "");
	BIND_FLOAT(forest_noise_lacunarity, "");
	BIND_INT(forest_fractal_type);

	// Tropical noise
	BIND_INT(tropical_noise_type);
	BIND_FLOAT(tropical_noise_freq, "");
	BIND_INT(tropical_noise_octaves);
	BIND_FLOAT(tropical_noise_gain, "");
	BIND_FLOAT(tropical_noise_lacunarity, "");
	BIND_INT(tropical_fractal_type);

	// Tundra noise
	BIND_INT(tundra_noise_type);
	BIND_FLOAT(tundra_noise_freq, "");
	BIND_INT(tundra_noise_octaves);
	BIND_FLOAT(tundra_noise_gain, "");
	BIND_FLOAT(tundra_noise_lacunarity, "");
	BIND_INT(tundra_fractal_type);

	// Grassland noise
	BIND_INT(grassland_noise_type);
	BIND_FLOAT(grassland_noise_freq, "");
	BIND_INT(grassland_noise_octaves);
	BIND_FLOAT(grassland_noise_gain, "");
	BIND_FLOAT(grassland_noise_lacunarity, "");
	BIND_INT(grassland_fractal_type);

	// Cave noise
	BIND_INT(cave_noise_type);
	BIND_FLOAT(cave_noise_freq, "");
	BIND_INT(cave_noise_octaves);
	BIND_FLOAT(cave_noise_gain, "");
	BIND_FLOAT(cave_noise_lacunarity, "");
	BIND_INT(cave_fractal_type);

	// ── ADD_PROPERTY declarations ────────────────────────────────────────

	ADD_GROUP("Planet", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "planet_radius"), "set_planet_radius", "get_planet_radius");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "seed_val"), "set_seed_val", "get_seed_val");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_terrain_height"), "set_max_terrain_height", "get_max_terrain_height");

	ADD_GROUP("Quick Controls", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "land_coverage", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_land_coverage", "get_land_coverage");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continent_size", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_continent_size", "get_continent_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "terrain_variation", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_terrain_variation", "get_terrain_variation");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_patch_size", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_patch_size", "get_biome_patch_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_patch_strength", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_patch_strength", "get_biome_patch_strength");

	ADD_GROUP("Tectonic Plates", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "num_plates", PROPERTY_HINT_RANGE, "4,40"), "set_num_plates", "get_num_plates");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "num_voronoi_points", PROPERTY_HINT_RANGE, "200,5000"), "set_num_voronoi_points", "get_num_voronoi_points");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "oceanic_fraction", PROPERTY_HINT_RANGE, "0.1,0.95,0.01"), "set_oceanic_fraction", "get_oceanic_fraction");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "plate_border_hops", PROPERTY_HINT_RANGE, "1,20"), "set_plate_border_hops", "get_plate_border_hops");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "plate_collision_threshold", PROPERTY_HINT_RANGE, "0.001,0.1,0.001"), "set_plate_collision_threshold", "get_plate_collision_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "plate_noise_freq", PROPERTY_HINT_RANGE, "0.1,3.0,0.01"), "set_plate_noise_freq", "get_plate_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "plate_noise_octaves", PROPERTY_HINT_RANGE, "1,8"), "set_plate_noise_octaves", "get_plate_noise_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "plate_noise_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_plate_noise_gain", "get_plate_noise_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "plate_noise_lacunarity", PROPERTY_HINT_RANGE, "1.0,4.0,0.1"), "set_plate_noise_lacunarity", "get_plate_noise_lacunarity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "plate_warp_strength", PROPERTY_HINT_RANGE, "0.0,0.5,0.01"), "set_plate_warp_strength", "get_plate_warp_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "plate_warp_freq", PROPERTY_HINT_RANGE, "0.5,5.0,0.1"), "set_plate_warp_freq", "get_plate_warp_freq");

	ADD_GROUP("Continent Shape", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continent_height"), "set_continent_height", "get_continent_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ocean_depth"), "set_ocean_depth", "get_ocean_depth");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continent_amplitude",  PROPERTY_HINT_RANGE, "0.01,2.0,0.01"),  "set_continent_amplitude",  "get_continent_amplitude");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continent_falloff",    PROPERTY_HINT_RANGE, "0.05,2.0,0.01"),  "set_continent_falloff",    "get_continent_falloff");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tectonic_line_width_km", PROPERTY_HINT_RANGE, "1.0,3.0,0.05"), "set_tectonic_line_width_km", "get_tectonic_line_width_km");
	// tectonic_influence no longer used for ocean/land — kept for .tres compat.
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tectonic_influence", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_tectonic_influence", "get_tectonic_influence");
	// Storage-only — kept so existing .tres files load without data loss, not shown in editor.
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continent_lo",               PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_continent_lo",               "get_continent_lo");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continent_hi",               PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_continent_hi",               "get_continent_hi");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continent_ratio_bias",       PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_continent_ratio_bias",       "get_continent_ratio_bias");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continent_shape_lowfreq_mix",PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_continent_shape_lowfreq_mix","get_continent_shape_lowfreq_mix");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continent_lowfreq_mix",      PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_continent_lowfreq_mix",      "get_continent_lowfreq_mix");

	ADD_GROUP("Shoreline", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shoreline_elevation_falloff_width", PROPERTY_HINT_RANGE, "0.01,1.0,0.01"),       "set_shoreline_elevation_falloff_width", "get_shoreline_elevation_falloff_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shoreline_elevation_falloff_power", PROPERTY_HINT_RANGE, "0.2,4.0,0.05"),        "set_shoreline_elevation_falloff_power", "get_shoreline_elevation_falloff_power");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shoreline_shape_width",            PROPERTY_HINT_RANGE, "0.0,0.5,0.01"),         "set_shoreline_shape_width",             "get_shoreline_shape_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shoreline_shape_strength",         PROPERTY_HINT_RANGE, "-2000.0,2000.0,10.0"),  "set_shoreline_shape_strength",         "get_shoreline_shape_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shoreline_land_shape_width",       PROPERTY_HINT_RANGE, "0.0,0.5,0.01"),         "set_shoreline_land_shape_width",        "get_shoreline_land_shape_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shoreline_land_shape_strength",    PROPERTY_HINT_RANGE, "-2000.0,2000.0,10.0"),  "set_shoreline_land_shape_strength",    "get_shoreline_land_shape_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shoreline_ocean_shape_width",      PROPERTY_HINT_RANGE, "0.0,0.5,0.01"),         "set_shoreline_ocean_shape_width",       "get_shoreline_ocean_shape_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shoreline_ocean_shape_strength",   PROPERTY_HINT_RANGE, "-2000.0,2000.0,10.0"),  "set_shoreline_ocean_shape_strength",   "get_shoreline_ocean_shape_strength");

	ADD_GROUP("Shoreline Materials", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "beach_width_m",                  PROPERTY_HINT_RANGE, "1.0,250.0,1.0"),   "set_beach_width_m",                  "get_beach_width_m");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shoreline_material_width_scale", PROPERTY_HINT_RANGE, "0.1,4.0,0.05"),   "set_shoreline_material_width_scale", "get_shoreline_material_width_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cont_sand_start",                PROPERTY_HINT_RANGE, "0.0,1.0,0.01"),   "set_cont_sand_start",                "get_cont_sand_start");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cont_sand_end",                  PROPERTY_HINT_RANGE, "0.0,1.0,0.01"),   "set_cont_sand_end",                  "get_cont_sand_end");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cont_ocean_start",               PROPERTY_HINT_RANGE, "0.0,1.0,0.01"),   "set_cont_ocean_start",               "get_cont_ocean_start");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cont_ocean_end",                 PROPERTY_HINT_RANGE, "0.0,1.0,0.01"),   "set_cont_ocean_end",                 "get_cont_ocean_end");

	ADD_GROUP("Hills & Meadows", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hills_amplitude"), "set_hills_amplitude", "get_hills_amplitude");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hills_meadow_power", PROPERTY_HINT_RANGE, "1.0,3.0,0.1"), "set_hills_meadow_power", "get_hills_meadow_power");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continental_relief_strength", PROPERTY_HINT_RANGE, "0.0,2.0,0.01"), "set_continental_relief_strength", "get_continental_relief_strength");

	ADD_GROUP("Mountains", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_height"),         "set_mountain_height",        "get_mountain_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_plate_boost",     PROPERTY_HINT_RANGE, "0.0,3.0,0.05"),   "set_mountain_plate_boost",   "get_mountain_plate_boost");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_plate_min",       PROPERTY_HINT_RANGE, "0.0,1.0,0.01"),   "set_mountain_plate_min",     "get_mountain_plate_min");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_rock_start",      PROPERTY_HINT_RANGE, "0.40,0.95,0.01"), "set_mountain_rock_start",    "get_mountain_rock_start");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_snow_start",      PROPERTY_HINT_RANGE, "0.55,1.50,0.01"), "set_mountain_snow_start",    "get_mountain_snow_start");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_falloff_start",   PROPERTY_HINT_RANGE, "0.0,1.0,0.01"),   "set_mountain_falloff_start", "get_mountain_falloff_start");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_falloff_end",     PROPERTY_HINT_RANGE, "0.0,1.0,0.01"),   "set_mountain_falloff_end",   "get_mountain_falloff_end");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_sharpness",       PROPERTY_HINT_RANGE, "0.5,3.0,0.1"),    "set_mountain_sharpness",     "get_mountain_sharpness");
	// Storage-only — mountain_landmask_strength no longer used in generation.
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_landmask_strength", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_mountain_landmask_strength", "get_mountain_landmask_strength");

	ADD_GROUP("Valleys & Trenches", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "trench_depth"), "set_trench_depth", "get_trench_depth");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rift_depth"), "set_rift_depth", "get_rift_depth");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "valley_falloff_start", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_valley_falloff_start", "get_valley_falloff_start");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "valley_falloff_end", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_valley_falloff_end", "get_valley_falloff_end");

	ADD_GROUP("Climate", "");
	ADD_PROPERTY(
			PropertyInfo(
					Variant::OBJECT,
					"climate_profile",
					PROPERTY_HINT_RESOURCE_TYPE,
					"EdenPlanetClimateProfile",
					PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_EDITOR_INSTANTIATE_OBJECT),
			"set_climate_profile",
			"get_climate_profile");
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
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "day_of_year", PROPERTY_HINT_RANGE, "0.0,365.0,1.0"), "set_day_of_year", "get_day_of_year");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "season_strength", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_season_strength", "get_season_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "season_phase_day", PROPERTY_HINT_RANGE, "0.0,365.0,1.0"), "set_season_phase_day", "get_season_phase_day");

	ADD_GROUP("Simple Climate Noises", "");
	// Temp noise aliases (mapped to climate_noise_*)
	ADD_PROPERTY(PropertyInfo(Variant::INT, "temp_noise_type"), "set_climate_noise_type", "get_climate_noise_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "temp_noise_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_climate_noise_freq", "get_climate_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "temp_noise_octaves", PROPERTY_HINT_RANGE, "1,10,1"), "set_climate_noise_octaves", "get_climate_noise_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "temp_noise_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_climate_noise_gain", "get_climate_noise_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "temp_noise_lacunarity", PROPERTY_HINT_RANGE, "1.0,6.0,0.01"), "set_climate_noise_lacunarity", "get_climate_noise_lacunarity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "temp_noise_strength", PROPERTY_HINT_RANGE, "0.0,40.0,0.1"), "set_temp_noise_amplitude", "get_temp_noise_amplitude");
	// Moisture noise aliases (mapped to desert_noise_*)
	ADD_PROPERTY(PropertyInfo(Variant::INT, "moisture_noise_type"), "set_desert_noise_type", "get_desert_noise_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "moisture_noise_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_desert_noise_freq", "get_desert_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "moisture_noise_octaves", PROPERTY_HINT_RANGE, "1,10,1"), "set_desert_noise_octaves", "get_desert_noise_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "moisture_noise_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_desert_noise_gain", "get_desert_noise_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "moisture_noise_lacunarity", PROPERTY_HINT_RANGE, "1.0,6.0,0.01"), "set_desert_noise_lacunarity", "get_desert_noise_lacunarity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "moisture_noise_strength", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_humidity_noise_amplitude", "get_humidity_noise_amplitude");
	// Evaporation noise aliases (mapped to forest_noise_*)
	ADD_PROPERTY(PropertyInfo(Variant::INT, "evaporation_noise_type"), "set_forest_noise_type", "get_forest_noise_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "evaporation_noise_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_forest_noise_freq", "get_forest_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "evaporation_noise_octaves", PROPERTY_HINT_RANGE, "1,10,1"), "set_forest_noise_octaves", "get_forest_noise_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "evaporation_noise_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_forest_noise_gain", "get_forest_noise_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "evaporation_noise_lacunarity", PROPERTY_HINT_RANGE, "1.0,6.0,0.01"), "set_forest_noise_lacunarity", "get_forest_noise_lacunarity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "evaporation_noise_strength", PROPERTY_HINT_RANGE, "0.0,2.0,0.01"), "set_evaporation_strength", "get_evaporation_strength");

	ADD_GROUP("Biome Detail", "amp_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "amp_desert"), "set_amp_desert", "get_amp_desert");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "amp_forest"), "set_amp_forest", "get_amp_forest");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "amp_tropical"), "set_amp_tropical", "get_amp_tropical");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "amp_tundra"), "set_amp_tundra", "get_amp_tundra");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "amp_grassland"), "set_amp_grassland", "get_amp_grassland");
	ADD_GROUP("Biome Spawn Chance", "biome_spawn_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_spawn_desert", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_spawn_desert", "get_biome_spawn_desert");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_spawn_forest", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_spawn_forest", "get_biome_spawn_forest");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_spawn_tropical", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_spawn_tropical", "get_biome_spawn_tropical");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_spawn_tundra", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_spawn_tundra", "get_biome_spawn_tundra");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_spawn_hills_meadows", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_spawn_hills_meadows", "get_biome_spawn_hills_meadows");
	// Legacy grassland channel kept separately for backwards compatibility.
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_spawn_grassland", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_spawn_grassland", "get_biome_spawn_grassland");
	ADD_GROUP("Biome Placement", "biome_region_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_region_noise_freq", PROPERTY_HINT_RANGE, "0.000001,0.001,0.000001,or_greater"), "set_biome_region_noise_freq", "get_biome_region_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_region_strength", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_region_strength", "get_biome_region_strength");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "biome_region_seed_offset", PROPERTY_HINT_RANGE, "-10000,10000,1"), "set_biome_region_seed_offset", "get_biome_region_seed_offset");

	ADD_GROUP("Caves", "cave_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "single_material_mode"), "set_single_material_mode", "get_single_material_mode");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enable_caves"), "set_enable_caves", "get_enable_caves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cave_carve_strength", PROPERTY_HINT_RANGE, "0.0,500.0,1.0"), "set_cave_carve_strength", "get_cave_carve_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cave_max_altitude", PROPERTY_HINT_RANGE, "-1000.0,5000.0,10.0"), "set_cave_max_altitude", "get_cave_max_altitude");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cave_fade_outer", PROPERTY_HINT_RANGE, "0.0,2000.0,10.0"), "set_cave_fade_outer", "get_cave_fade_outer");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cave_fade_inner", PROPERTY_HINT_RANGE, "0.0,500.0,5.0"), "set_cave_fade_inner", "get_cave_fade_inner");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cave_threshold_low", PROPERTY_HINT_RANGE, "-1.0,0.0,0.01"), "set_cave_threshold_low", "get_cave_threshold_low");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cave_threshold_high", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_cave_threshold_high", "get_cave_threshold_high");
	ADD_SUBGROUP("Cave Noise", "cave_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cave_noise_type", PROPERTY_HINT_ENUM, "Simplex,Simplex Smooth,Cellular,Perlin,Value Cubic,Value"), "set_cave_noise_type", "get_cave_noise_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cave_noise_freq", PROPERTY_HINT_RANGE, "0.00001,0.01,0.00001,or_greater"), "set_cave_noise_freq", "get_cave_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cave_noise_octaves", PROPERTY_HINT_RANGE, "1,8"), "set_cave_noise_octaves", "get_cave_noise_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cave_noise_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_cave_noise_gain", "get_cave_noise_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cave_noise_lacunarity", PROPERTY_HINT_RANGE, "1.0,4.0,0.1"), "set_cave_noise_lacunarity", "get_cave_noise_lacunarity");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cave_fractal_type", PROPERTY_HINT_ENUM, "None,FBM,Ridged,Ping Pong"), "set_cave_fractal_type", "get_cave_fractal_type");

	// ── Noise configuration groups ──────────────────────────────────────

	ADD_GROUP("Continent Noise", "continent_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "continent_noise_type", PROPERTY_HINT_ENUM, "Simplex,Simplex Smooth,Cellular,Perlin,Value Cubic,Value"), "set_continent_noise_type", "get_continent_noise_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continent_noise_freq", PROPERTY_HINT_RANGE, "0.000001,0.001,0.000001,or_greater"), "set_continent_noise_freq", "get_continent_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "continent_noise_octaves", PROPERTY_HINT_RANGE, "1,8"), "set_continent_noise_octaves", "get_continent_noise_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continent_noise_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_continent_noise_gain", "get_continent_noise_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continent_noise_lacunarity", PROPERTY_HINT_RANGE, "1.0,4.0,0.1"), "set_continent_noise_lacunarity", "get_continent_noise_lacunarity");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "continent_fractal_type", PROPERTY_HINT_ENUM, "None,FBM,Ridged,Ping Pong"), "set_continent_fractal_type", "get_continent_fractal_type");
	ADD_SUBGROUP("Domain Warp", "continent_warp_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "continent_warp_enabled"), "set_continent_warp_enabled", "get_continent_warp_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continent_warp_freq", PROPERTY_HINT_RANGE, "0.00001,0.01,0.00001,or_greater"), "set_continent_warp_freq", "get_continent_warp_freq");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continent_warp_amp", PROPERTY_HINT_RANGE, "0.0,200.0,0.5"), "set_continent_warp_amp", "get_continent_warp_amp");

	ADD_GROUP("Hills Noise", "hills_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "hills_noise_type", PROPERTY_HINT_ENUM, "Simplex,Simplex Smooth,Cellular,Perlin,Value Cubic,Value"), "set_hills_noise_type", "get_hills_noise_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hills_noise_freq", PROPERTY_HINT_RANGE, "0.00001,0.01,0.00001,or_greater"), "set_hills_noise_freq", "get_hills_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "hills_noise_octaves", PROPERTY_HINT_RANGE, "1,8"), "set_hills_noise_octaves", "get_hills_noise_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hills_noise_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_hills_noise_gain", "get_hills_noise_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hills_noise_lacunarity", PROPERTY_HINT_RANGE, "1.0,4.0,0.1"), "set_hills_noise_lacunarity", "get_hills_noise_lacunarity");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "hills_fractal_type", PROPERTY_HINT_ENUM, "None,FBM,Ridged,Ping Pong"), "set_hills_fractal_type", "get_hills_fractal_type");

	ADD_GROUP("Mountain Noise", "mountain_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mountain_noise_type", PROPERTY_HINT_ENUM, "Simplex,Simplex Smooth,Cellular,Perlin,Value Cubic,Value"), "set_mountain_noise_type", "get_mountain_noise_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_noise_freq", PROPERTY_HINT_RANGE, "0.00001,0.01,0.00001,or_greater"), "set_mountain_noise_freq", "get_mountain_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mountain_noise_octaves", PROPERTY_HINT_RANGE, "1,8"), "set_mountain_noise_octaves", "get_mountain_noise_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_noise_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_mountain_noise_gain", "get_mountain_noise_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_noise_lacunarity", PROPERTY_HINT_RANGE, "1.0,4.0,0.1"), "set_mountain_noise_lacunarity", "get_mountain_noise_lacunarity");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mountain_fractal_type", PROPERTY_HINT_ENUM, "None,FBM,Ridged,Ping Pong"), "set_mountain_fractal_type", "get_mountain_fractal_type");

	ADD_GROUP("Climate Noise", "climate_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "climate_noise_type", PROPERTY_HINT_ENUM, "Simplex,Simplex Smooth,Cellular,Perlin,Value Cubic,Value"), "set_climate_noise_type", "get_climate_noise_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "climate_noise_freq", PROPERTY_HINT_RANGE, "0.00001,0.01,0.00001,or_greater"), "set_climate_noise_freq", "get_climate_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "climate_noise_octaves", PROPERTY_HINT_RANGE, "1,8"), "set_climate_noise_octaves", "get_climate_noise_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "climate_noise_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_climate_noise_gain", "get_climate_noise_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "climate_noise_lacunarity", PROPERTY_HINT_RANGE, "1.0,4.0,0.1"), "set_climate_noise_lacunarity", "get_climate_noise_lacunarity");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "climate_fractal_type", PROPERTY_HINT_ENUM, "None,FBM,Ridged,Ping Pong"), "set_climate_fractal_type", "get_climate_fractal_type");

	ADD_GROUP("Desert Noise", "desert_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "desert_noise_type", PROPERTY_HINT_ENUM, "Simplex,Simplex Smooth,Cellular,Perlin,Value Cubic,Value"), "set_desert_noise_type", "get_desert_noise_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "desert_noise_freq", PROPERTY_HINT_RANGE, "0.00001,0.01,0.00001,or_greater"), "set_desert_noise_freq", "get_desert_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "desert_noise_octaves", PROPERTY_HINT_RANGE, "1,8"), "set_desert_noise_octaves", "get_desert_noise_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "desert_noise_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_desert_noise_gain", "get_desert_noise_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "desert_noise_lacunarity", PROPERTY_HINT_RANGE, "1.0,4.0,0.1"), "set_desert_noise_lacunarity", "get_desert_noise_lacunarity");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "desert_fractal_type", PROPERTY_HINT_ENUM, "None,FBM,Ridged,Ping Pong"), "set_desert_fractal_type", "get_desert_fractal_type");

	ADD_GROUP("Forest Noise", "forest_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "forest_noise_type", PROPERTY_HINT_ENUM, "Simplex,Simplex Smooth,Cellular,Perlin,Value Cubic,Value"), "set_forest_noise_type", "get_forest_noise_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "forest_noise_freq", PROPERTY_HINT_RANGE, "0.00001,0.01,0.00001,or_greater"), "set_forest_noise_freq", "get_forest_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "forest_noise_octaves", PROPERTY_HINT_RANGE, "1,8"), "set_forest_noise_octaves", "get_forest_noise_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "forest_noise_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_forest_noise_gain", "get_forest_noise_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "forest_noise_lacunarity", PROPERTY_HINT_RANGE, "1.0,4.0,0.1"), "set_forest_noise_lacunarity", "get_forest_noise_lacunarity");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "forest_fractal_type", PROPERTY_HINT_ENUM, "None,FBM,Ridged,Ping Pong"), "set_forest_fractal_type", "get_forest_fractal_type");

	ADD_GROUP("Tropical Noise", "tropical_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tropical_noise_type", PROPERTY_HINT_ENUM, "Simplex,Simplex Smooth,Cellular,Perlin,Value Cubic,Value"), "set_tropical_noise_type", "get_tropical_noise_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tropical_noise_freq", PROPERTY_HINT_RANGE, "0.00001,0.01,0.00001,or_greater"), "set_tropical_noise_freq", "get_tropical_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tropical_noise_octaves", PROPERTY_HINT_RANGE, "1,8"), "set_tropical_noise_octaves", "get_tropical_noise_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tropical_noise_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_tropical_noise_gain", "get_tropical_noise_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tropical_noise_lacunarity", PROPERTY_HINT_RANGE, "1.0,4.0,0.1"), "set_tropical_noise_lacunarity", "get_tropical_noise_lacunarity");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tropical_fractal_type", PROPERTY_HINT_ENUM, "None,FBM,Ridged,Ping Pong"), "set_tropical_fractal_type", "get_tropical_fractal_type");

	ADD_GROUP("Tundra Noise", "tundra_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tundra_noise_type", PROPERTY_HINT_ENUM, "Simplex,Simplex Smooth,Cellular,Perlin,Value Cubic,Value"), "set_tundra_noise_type", "get_tundra_noise_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tundra_noise_freq", PROPERTY_HINT_RANGE, "0.00001,0.01,0.00001,or_greater"), "set_tundra_noise_freq", "get_tundra_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tundra_noise_octaves", PROPERTY_HINT_RANGE, "1,8"), "set_tundra_noise_octaves", "get_tundra_noise_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tundra_noise_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_tundra_noise_gain", "get_tundra_noise_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tundra_noise_lacunarity", PROPERTY_HINT_RANGE, "1.0,4.0,0.1"), "set_tundra_noise_lacunarity", "get_tundra_noise_lacunarity");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tundra_fractal_type", PROPERTY_HINT_ENUM, "None,FBM,Ridged,Ping Pong"), "set_tundra_fractal_type", "get_tundra_fractal_type");

	ADD_GROUP("Hills & Meadows Noise", "grassland_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "grassland_noise_type", PROPERTY_HINT_ENUM, "Simplex,Simplex Smooth,Cellular,Perlin,Value Cubic,Value"), "set_grassland_noise_type", "get_grassland_noise_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grassland_noise_freq", PROPERTY_HINT_RANGE, "0.00001,0.01,0.00001,or_greater"), "set_grassland_noise_freq", "get_grassland_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "grassland_noise_octaves", PROPERTY_HINT_RANGE, "1,8"), "set_grassland_noise_octaves", "get_grassland_noise_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grassland_noise_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_grassland_noise_gain", "get_grassland_noise_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grassland_noise_lacunarity", PROPERTY_HINT_RANGE, "1.0,4.0,0.1"), "set_grassland_noise_lacunarity", "get_grassland_noise_lacunarity");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "grassland_fractal_type", PROPERTY_HINT_ENUM, "None,FBM,Ridged,Ping Pong"), "set_grassland_fractal_type", "get_grassland_fractal_type");
}

#undef BIND_FLOAT
#undef BIND_INT
#undef BIND_BOOL

// ══════════════════════════════════════════════════════════════════════════════
// SURFACE QUERY — per-position sampling for WorldDataModule (ADR-0005)
// ══════════════════════════════════════════════════════════════════════════════

bool EdenPlanetGeneratorV1::sample_surface(const Vector3 &dir, SurfaceSample &out) const {
	Parameters params;
	Ref<EdenPlanetClimateProfile> climate_profile;
	// Vector<> is copy-on-write — these copies are cheap and keep the maps alive
	// after the lock is released.
	Vector<float> tect_oceanic;
	Vector<float> tect_mountain;
	Vector<float> tect_valley;
	SampleNoises sn;
	{
		zylann::RWLockRead rlock(_parameters_lock);
		if (!_parameters.is_setup) {
			return false;
		}
		params = _parameters;
		climate_profile = _climate_profile;
		tect_oceanic = _tect_oceanic;
		tect_mountain = _tect_mountain;
		tect_valley = _tect_valley;
		sn.continent = _noise_continent;
		sn.hills = _noise_hills;
		sn.mountain = _noise_mountain;
		sn.climate = _noise_climate;
		sn.desert = _noise_desert;
		sn.forest = _noise_forest;
		sn.tropical = _noise_tropical;
		sn.tundra = _noise_tundra;
		sn.grassland = _noise_grassland;
		sn.biome_region = _noise_biome_region;
		sn.cave = _noise_cave;
	}
	_apply_climate_profile(params, climate_profile);
	sn.tect_oceanic = &tect_oceanic;
	sn.tect_mountain = &tect_mountain;
	sn.tect_valley = &tect_valley;

	Vector3 d = dir;
	if (d.length_squared() < 1e-12f) {
		return false;
	}
	d = d.normalized();

	const SampleConsts sc = _make_sample_consts(params);

	// Bisection along the radial ray: SDF is positive in air, negative in rock.
	// The shell bounds guarantee a bracket (air above alt_outer, solid below alt_inner).
	float lo = params.planet_radius + sc.alt_inner + 1.0f; // solid end
	float hi = params.planet_radius + sc.alt_outer - 1.0f; // air end
	VoxelSample vs;
	// 28 iterations over a ~2*(max_terrain_height+500) m span → sub-millimeter.
	for (int i = 0; i < 28; ++i) {
		const float mid = (lo + hi) * 0.5f;
		_sample_voxel(d.x * mid, d.y * mid, d.z * mid, params, sn, sc, false, vs);
		if (vs.sdf < 0.0f) {
			lo = mid;
		} else {
			hi = mid;
		}
	}
	const float surface_r = (lo + hi) * 0.5f;

	// Final full sample just below the surface for climate/biome fields.
	_sample_voxel(d.x * (surface_r - 0.5f), d.y * (surface_r - 0.5f), d.z * (surface_r - 0.5f),
			params, sn, sc, true, vs);

	out.height = surface_r - params.planet_radius;
	const float t_lo = MIN(params.polar_temperature, params.equator_temperature);
	const float t_hi = MAX(params.polar_temperature, params.equator_temperature);
	out.temperature01 = CLAMP((vs.temp - t_lo) / MAX(t_hi - t_lo, 0.001f), 0.0f, 1.0f);
	out.rainfall01 = CLAMP(vs.moist, 0.0f, 1.0f);
	out.biome = vs.biome;
	out.is_ocean = vs.cont_transition > 0.5f;
	return true;
}

int EdenPlanetGeneratorV1::sample_biome_at(const Vector3 &dir) const {
	SurfaceSample out;
	if (!sample_surface(dir, out)) {
		return -1;
	}
	return out.biome;
}

int EdenPlanetGeneratorV1::sample_dominant_material(const Vector3 &dir) const {
	Parameters params;
	Ref<EdenPlanetClimateProfile> climate_profile;
	Vector<float> tect_oceanic;
	Vector<float> tect_mountain;
	Vector<float> tect_valley;
	SampleNoises sn;
	{
		zylann::RWLockRead rlock(_parameters_lock);
		if (!_parameters.is_setup) {
			return -1;
		}
		params = _parameters;
		climate_profile = _climate_profile;
		tect_oceanic = _tect_oceanic;
		tect_mountain = _tect_mountain;
		tect_valley = _tect_valley;
		sn.continent = _noise_continent;
		sn.hills = _noise_hills;
		sn.mountain = _noise_mountain;
		sn.climate = _noise_climate;
		sn.desert = _noise_desert;
		sn.forest = _noise_forest;
		sn.tropical = _noise_tropical;
		sn.tundra = _noise_tundra;
		sn.grassland = _noise_grassland;
		sn.biome_region = _noise_biome_region;
		sn.cave = _noise_cave;
	}
	_apply_climate_profile(params, climate_profile);
	sn.tect_oceanic = &tect_oceanic;
	sn.tect_mountain = &tect_mountain;
	sn.tect_valley = &tect_valley;

	Vector3 d = dir;
	if (d.length_squared() < 1e-12f) {
		return -1;
	}
	d = d.normalized();

	const SampleConsts sc = _make_sample_consts(params);

	float lo = params.planet_radius + sc.alt_inner + 1.0f;
	float hi = params.planet_radius + sc.alt_outer - 1.0f;
	VoxelSample vs;
	for (int i = 0; i < 28; ++i) {
		const float mid = (lo + hi) * 0.5f;
		_sample_voxel(d.x * mid, d.y * mid, d.z * mid, params, sn, sc, false, vs);
		if (vs.sdf < 0.0f) {
			lo = mid;
		} else {
			hi = mid;
		}
	}
	const float surface_r = (lo + hi) * 0.5f;

	_sample_voxel(d.x * (surface_r - 0.5f), d.y * (surface_r - 0.5f), d.z * (surface_r - 0.5f),
			params, sn, sc, true, vs);

	int best_index = vs.indices & 0xF;
	int best_weight = vs.weights & 0xF;
	for (int slot = 1; slot < 4; ++slot) {
		const int idx = (vs.indices >> (slot * 4)) & 0xF;
		const int w = (vs.weights >> (slot * 4)) & 0xF;
		if (w > best_weight || (w == best_weight && idx < best_index)) {
			best_weight = w;
			best_index = idx;
		}
	}
	return best_index;
}
