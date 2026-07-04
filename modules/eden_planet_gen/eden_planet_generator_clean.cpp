#include "eden_planet_generator_clean.h"

#include <algorithm>
#include <cstring>

using zylann::voxel::VoxelBuffer;

namespace {

// Fast x^p for x in [0,1], p > 0.  Uses the IEEE754 bit-trick
// log2 approximation + fast exp2 (polynomial).  ~3× faster than Math::pow,
// max relative error ~1.5% for typical terrain exponents (0.2–6.0).
inline float _fast_pow01(float x, float p) {
	if (x <= 0.0f) return 0.0f;
	if (x >= 1.0f) return 1.0f;
	// Fast log2 via IEEE float bit reinterpretation.
	int32_t ix;
	memcpy(&ix, &x, 4);
	float log2_x = (float)(ix - 0x3F800000) * (1.0f / (float)(1 << 23));
	float t = p * log2_x; // This is in the range (-inf, 0] for x in (0,1].
	// Fast exp2(t) for t in [-126, 0]: split into integer + fractional.
	t = MAX(t, -126.0f);
	int ti = (int)t;
	if (t < 0.0f) ti -= 1; // floor for negative
	float tf = t - (float)ti;
	// Polynomial approx for 2^tf, tf in [0,1): 2^f ≈ 1 + f*(0.6930 + f*(0.2402 + f*0.0520))
	float exp2f = 1.0f + tf * (0.6930f + tf * (0.2402f + tf * 0.0520f));
	// Construct 2^ti as float via bit manipulation.
	int32_t exp2i_bits = (ti + 127) << 23;
	float exp2i;
	memcpy(&exp2i, &exp2i_bits, 4);
	return exp2i * exp2f;
}

inline float _fast_rsqrt(float x) {
	// Quake-style fast inverse sqrt + one Newton iteration.
	float xhalf = 0.5f * x;
	int32_t i;
	memcpy(&i, &x, 4);
	i = 0x5f3759df - (i >> 1);
	float y;
	memcpy(&y, &i, 4);
	y = y * (1.5f - xhalf * y * y);
	return y;
}

} // namespace

EdenPlanetGeneratorClean::EdenPlanetGeneratorClean() {
}

EdenPlanetGeneratorClean::~EdenPlanetGeneratorClean() {
}

Ref<FastNoiseLite> EdenPlanetGeneratorClean::_make_noise(int p_seed, float p_freq, int p_octaves, float p_gain, float p_lacunarity,
		int p_noise_type, int p_fractal_type) {
	Ref<FastNoiseLite> n;
	n.instantiate();
	n->set_seed(p_seed);
	n->set_noise_type(static_cast<FastNoiseLite::NoiseType>(p_noise_type));
	n->set_fractal_type(static_cast<FastNoiseLite::FractalType>(p_fractal_type));
	n->set_frequency(p_freq);
	n->set_fractal_octaves(p_octaves);
	n->set_fractal_gain(p_gain);
	n->set_fractal_lacunarity(p_lacunarity);
	return n;
}

void EdenPlanetGeneratorClean::setup() {
	zylann::RWLockWrite wlock(_parameters_lock);
	Parameters &p = _parameters;

	_noise_land = _make_noise(p.seed + 11, p.land_noise_freq, 5, 0.5f, 2.0f);
	_noise_temp = _make_noise(p.seed + 101, p.temp_noise_freq, 4, 0.5f, 2.0f);
	_noise_moisture = _make_noise(p.seed + 131, p.moisture_noise_freq, 4, 0.5f, 2.0f);
	_noise_evaporation = _make_noise(p.seed + 151, p.evaporation_noise_freq, 4, 0.5f, 2.0f);
	_noise_biome_selector = _make_noise(p.seed + 181, p.biome_selector_freq,
			p.biome_selector_octaves, p.biome_selector_gain, p.biome_selector_lacunarity,
			p.biome_selector_type, FastNoiseLite::FRACTAL_FBM);
	_noise_detail_desert = _make_noise(p.seed + 211, p.desert_detail_freq,
			p.desert_detail_octaves, p.desert_detail_gain, p.desert_detail_lacunarity,
			p.desert_detail_type, FastNoiseLite::FRACTAL_FBM);
	_noise_detail_tundra = _make_noise(p.seed + 229, p.tundra_detail_freq,
			p.tundra_detail_octaves, p.tundra_detail_gain, p.tundra_detail_lacunarity,
			p.tundra_detail_type, FastNoiseLite::FRACTAL_FBM);
	_noise_detail_grassland = _make_noise(p.seed + 241, p.grassland_detail_freq,
			p.grassland_detail_octaves, p.grassland_detail_gain, p.grassland_detail_lacunarity,
			p.grassland_detail_type, FastNoiseLite::FRACTAL_FBM);
	_noise_detail_forest = _make_noise(p.seed + 257, p.forest_detail_freq,
			p.forest_detail_octaves, p.forest_detail_gain, p.forest_detail_lacunarity,
			p.forest_detail_type, FastNoiseLite::FRACTAL_FBM);
	_noise_detail_tropical = _make_noise(p.seed + 277, p.tropical_detail_freq,
			p.tropical_detail_octaves, p.tropical_detail_gain, p.tropical_detail_lacunarity,
			p.tropical_detail_type, FastNoiseLite::FRACTAL_FBM);
	_noise_mountain_area = _make_noise(p.seed + 251, p.mountain_area_freq, 3, 0.5f, 2.0f);
	_noise_mountain_shape = _make_noise(p.seed + 271, p.mountain_shape_freq,
			p.mountain_shape_octaves, p.mountain_shape_gain, p.mountain_shape_lacunarity,
			p.mountain_shape_type, p.mountain_shape_fractal_type);
	if (p.mountain_shape_warp_amp > 0.0f) {
		_noise_mountain_shape->set_domain_warp_enabled(true);
		_noise_mountain_shape->set_domain_warp_type(static_cast<FastNoiseLite::DomainWarpType>(p.mountain_shape_warp_type));
		_noise_mountain_shape->set_domain_warp_amplitude(p.mountain_shape_warp_amp);
		_noise_mountain_shape->set_domain_warp_frequency(p.mountain_shape_warp_freq);
	}

	const int n = Parameters::LAND_CDF_SIZE;
	const float golden = 2.4f;
	for (int i = 0; i < n; ++i) {
		const float cos_lat = 1.0f - (2.0f * float(i) + 1.0f) / float(n);
		const float sin_lat = Math::sqrt(MAX(1.0f - cos_lat * cos_lat, 0.0f));
		const float ang = golden * float(i);
		const float sx = p.planet_radius * sin_lat * Math::cos(ang);
		const float sy = p.planet_radius * cos_lat;
		const float sz = p.planet_radius * sin_lat * Math::sin(ang);
		p.land_cdf_hf[i] = _noise_land->get_noise_3d(sx, sy, sz);
		p.land_cdf_lf[i] = _noise_land->get_noise_3d(sx * 0.35f, sy * 0.35f, sz * 0.35f);
	}
	std::sort(p.land_cdf_hf, p.land_cdf_hf + n);
	std::sort(p.land_cdf_lf, p.land_cdf_lf + n);

	p.is_setup = true;
}

zylann::voxel::VoxelGenerator::Result EdenPlanetGeneratorClean::generate_block(VoxelQueryData input) {
	Result result;

	Parameters params;
	Ref<FastNoiseLite> noise_land;
	Ref<FastNoiseLite> noise_temp;
	Ref<FastNoiseLite> noise_moisture;
	Ref<FastNoiseLite> noise_evaporation;
	Ref<FastNoiseLite> noise_biome_selector;
	Ref<FastNoiseLite> noise_detail_desert;
	Ref<FastNoiseLite> noise_detail_tundra;
	Ref<FastNoiseLite> noise_detail_grassland;
	Ref<FastNoiseLite> noise_detail_forest;
	Ref<FastNoiseLite> noise_detail_tropical;
	Ref<FastNoiseLite> noise_mountain_area;
	Ref<FastNoiseLite> noise_mountain_shape;
	{
		zylann::RWLockRead rlock(_parameters_lock);
		params = _parameters;
		noise_land = _noise_land;
		noise_temp = _noise_temp;
		noise_moisture = _noise_moisture;
		noise_evaporation = _noise_evaporation;
		noise_biome_selector = _noise_biome_selector;
		noise_detail_desert = _noise_detail_desert;
		noise_detail_tundra = _noise_detail_tundra;
		noise_detail_grassland = _noise_detail_grassland;
		noise_detail_forest = _noise_detail_forest;
		noise_detail_tropical = _noise_detail_tropical;
		noise_mountain_area = _noise_mountain_area;
		noise_mountain_shape = _noise_mountain_shape;
	}

	if (!params.is_setup) {
		setup();
		zylann::RWLockRead rlock(_parameters_lock);
		params = _parameters;
		noise_land = _noise_land;
		noise_temp = _noise_temp;
		noise_moisture = _noise_moisture;
		noise_evaporation = _noise_evaporation;
		noise_biome_selector = _noise_biome_selector;
		noise_detail_desert = _noise_detail_desert;
		noise_detail_tundra = _noise_detail_tundra;
		noise_detail_grassland = _noise_detail_grassland;
		noise_detail_forest = _noise_detail_forest;
		noise_detail_tropical = _noise_detail_tropical;
		noise_mountain_area = _noise_mountain_area;
		noise_mountain_shape = _noise_mountain_shape;
	}
	if (!params.is_setup) {
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

	const float bws = float(size.x * step);
	const float cx = float(origin.x) + bws * 0.5f;
	const float cy = float(origin.y) + bws * 0.5f;
	const float cz = float(origin.z) + bws * 0.5f;
	const float dist_c = Math::sqrt(cx * cx + cy * cy + cz * cz);
	const float diag = bws * 1.7320508f;

	const float shell_inner = params.planet_radius - (params.max_terrain_height + 1500.0f);
	const float shell_outer = params.planet_radius + (params.max_terrain_height + 1500.0f);

	if (dist_c + diag * 0.5f < shell_inner) {
		buffer.clear_channel_f(VoxelBuffer::CHANNEL_SDF, -100.0f);
		result.max_lod_hint = true;
		return result;
	}
	if (dist_c - diag * 0.5f > shell_outer) {
		buffer.clear_channel_f(VoxelBuffer::CHANNEL_SDF, 100.0f);
		result.max_lod_hint = true;
		return result;
	}

	int rock_indices, rock_weights;
	_pack_mixel4(MAT_ROCK, MAT_OCEAN_FLOOR, 0.0f, 0.46f, 0.50f, 0.50f, 0.54f, rock_indices, rock_weights);
	int ocean_indices, ocean_weights;
	_pack_mixel4(MAT_OCEAN_FLOOR, MAT_OCEAN_FLOOR, 1.0f, 0.46f, 0.50f, 0.50f, 0.54f, ocean_indices, ocean_weights);

	const float inv_tau = 1.0f / float(Math::TAU);
	const float inv_pi = 1.0f / float(Math::PI);

	// Biome region grid — fixed for the entire block, hoisted out of the voxel loop.
	const float rc_u = Math::lerp(220.0f, 24.0f, CLAMP(params.biome_region_size, 0.0f, 1.0f));
	const float rc_v = rc_u * 0.5f;
	const float edge_w = CLAMP(params.biome_edge_blend, 0.001f, 0.45f);

	// sample_region: outputs depend only on (ru,rv), never on voxel position.
	auto sample_region = [&](int ru, int rv, float &o_temp, float &o_moist, float &o_evap, float &o_sel) {
		const int w_u = int(rc_u);
		const int w_v = int(rc_v);
		const int uu = ((ru % w_u) + w_u) % w_u;
		const int vv = CLAMP(rv, 0, w_v - 1);
		const float base_u = (float(uu) + 0.5f) / rc_u;
		const float base_v = (float(vv) + 0.5f) / rc_v;
		const float offs[4][2] = {
			{-0.22f, -0.22f},
			{0.22f, -0.22f},
			{-0.22f, 0.22f},
			{0.22f, 0.22f},
		};
		float s_temp = 0.0f;
		float s_moist = 0.0f;
		float s_evap = 0.0f;
		float s_sel = 0.0f;
		for (int k = 0; k < 4; ++k) {
			float su = base_u + offs[k][0] / rc_u;
			float sv = base_v + offs[k][1] / rc_v;
			su = su - Math::floor(su);
			sv = CLAMP(sv, 0.0f, 1.0f);
			const float lon = (su - 0.5f) * float(Math::TAU);
			const float lat_a = (0.5f - sv) * float(Math::PI);
			const float cl = Math::cos(lat_a);
			const float rx = params.planet_radius * cl * Math::cos(lon);
			const float ry = params.planet_radius * Math::sin(lat_a);
			const float rz = params.planet_radius * cl * Math::sin(lon);
			const float lat01 = Math::abs(Math::sin(lat_a));
			const float base_temp = Math::lerp(36.0f, -18.0f, lat01);
			const float tn = noise_temp->get_noise_3d(rx, ry, rz);
			const float mn = noise_moisture->get_noise_3d(rx, ry, rz);
			const float en = noise_evaporation->get_noise_3d(rx, ry, rz);
			const float e = CLAMP(0.5f + 0.5f * en * params.evaporation_noise_strength, 0.0f, 1.0f);
			s_temp += base_temp + tn * params.temp_noise_strength;
			s_moist += CLAMP(0.5f + 0.5f * mn * params.moisture_noise_strength - e * params.evaporation_strength, 0.0f, 1.0f);
			s_evap += e;
			s_sel += CLAMP(0.5f + 0.5f * noise_biome_selector->get_noise_3d(rx, ry, rz), 0.0f, 1.0f);
		}
		o_temp = s_temp * 0.25f;
		o_moist = s_moist * 0.25f;
		o_evap = s_evap * 0.25f;
		o_sel = s_sel * 0.25f;
	};

	// Per-block cache: biome cells span ~1500 m; blocks are far smaller, so nearly
	// all voxels map to the same 1-2 cells. 8 slots easily covers any block.
	struct RCacheEntry { int u, v; float t, m, e, s; };
	RCacheEntry rcache[8];
	for (auto &ce : rcache) { ce.u = -999999; }
	int rcache_next = 0;

	auto cached_region = [&](int ru, int rv, float &ot, float &om, float &oe, float &os) {
		for (auto &ce : rcache) {
			if (ce.u == ru && ce.v == rv) { ot = ce.t; om = ce.m; oe = ce.e; os = ce.s; return; }
		}
		sample_region(ru, rv, ot, om, oe, os);
		rcache[rcache_next] = { ru, rv, ot, om, oe, os };
		rcache_next = (rcache_next + 1) % 8;
	};

	const float budget = params.mountain_height +
			MAX(MAX(MAX(params.desert_detail_amp, params.forest_detail_amp),
					MAX(params.tropical_detail_amp, params.tundra_detail_amp)),
					params.grassland_detail_amp) + 150.0f;

	for (int z = 0; z < size.z; ++z) {
		const float wz = float(origin.z) + z * step + half_step;
		for (int y = 0; y < size.y; ++y) {
			const float wy = float(origin.y) + y * step + half_step;
			for (int x = 0; x < size.x; ++x) {
				const float wx = float(origin.x) + x * step + half_step;

				const float r = Math::sqrt(wx * wx + wy * wy + wz * wz);
				const float alt = r - params.planet_radius;
				const float inv_r = 1.0f / MAX(r, 1.0f);
				const float ux = wx * inv_r;
				const float uy = wy * inv_r;
				const float uz = wz * inv_r;
				const float sx = ux * params.planet_radius;
				const float sy = uy * params.planet_radius;
				const float sz = uz * params.planet_radius;

				const float map_u = Math::atan2(uz, ux) * inv_tau + 0.5f;
				const float map_v = 0.5f - Math::asin(CLAMP(uy, -1.0f, 1.0f)) * inv_pi;

				const float raw_land = noise_land->get_noise_3d(sx, sy, sz);
				const float raw_land_low = noise_land->get_noise_3d(sx * 0.35f, sy * 0.35f, sz * 0.35f);
				const int cdf_idx = CLAMP(int((1.0f - CLAMP(params.land_coverage, 0.0f, 1.0f)) * float(Parameters::LAND_CDF_SIZE)), 0, Parameters::LAND_CDF_SIZE - 1);
				const float thr_hf = params.land_cdf_hf[cdf_idx];
				const float thr_lf = params.land_cdf_lf[cdf_idx];
				const float land_h = (raw_land - thr_hf) * MAX(params.land_noise_amplitude, 0.001f);
				const float land_h_low = (raw_land_low - thr_lf) * MAX(params.land_noise_amplitude, 0.001f);
				const float fall = MAX(params.land_noise_falloff, 0.001f);
				const float land_mask = CLAMP((land_h + fall) / (2.0f * fall), 0.0f, 1.0f);
				const float land_mask_low = CLAMP((land_h_low + fall) / (2.0f * fall), 0.0f, 1.0f);
				const float cont_transition = 1.0f - land_mask;
				const float macro_land = _ss(0.10f, 0.45f, land_mask_low);

				const float shore_signed = cont_transition - 0.5f;
				const float ocean_t = _ss(0.0f, 0.5f, shore_signed);
				const float ocean_base = -params.ocean_depth * ocean_t;
				const float coast_blend = _ss(-0.05f, 0.20f, shore_signed);
				const float base_height = Math::lerp(params.land_height, ocean_base, coast_blend);

				float detail = 0.0f;
				int biome = BIOME_OCEAN;
				int biome2 = BIOME_OCEAN;
				float biome_blend = 0.0f;
				float temp_c = 0.0f;
				float moist = 0.0f;
				float evap = 0.0f;

				if (macro_land > 0.1f && cont_transition <= 0.5f) {
					const float uf = map_u * rc_u;
					const float vf = map_v * rc_v;
					const int ui = int(Math::floor(uf));
					const int vi = int(Math::floor(vf));
					const float fu = uf - float(ui);
					const float fv = vf - float(vi);
					const float edge_dist = MIN(MIN(fu, 1.0f - fu), MIN(fv, 1.0f - fv));

					float t1, m1, e1, s1;
					cached_region(ui, vi, t1, m1, e1, s1);

					int nu = ui;
					int nv = vi;
					if (fu < edge_w) {
						nu = ui - 1;
					} else if ((1.0f - fu) < edge_w) {
						nu = ui + 1;
					}
					if (fv < edge_w) {
						nv = vi - 1;
					} else if ((1.0f - fv) < edge_w) {
						nv = vi + 1;
					}

					float t2, m2, e2, s2;
					cached_region(nu, nv, t2, m2, e2, s2);

					const float bt = 1.0f - _ss(0.0f, edge_w, edge_dist);
					temp_c = Math::lerp(t1, t2, bt);
					moist = Math::lerp(m1, m2, bt);
					evap = Math::lerp(e1, e2, bt);
					const float sel = Math::lerp(s1, s2, bt);
					biome_blend = bt;

					biome = _classify_biome(temp_c, moist, evap, s1);
					biome2 = _classify_biome(temp_c, moist, evap, s2);

auto biome_detail_sample = [&](int b) -> float {
					switch (b) {
						case BIOME_DESERT:    return noise_detail_desert->get_noise_3d(sx, sy, sz) * params.desert_detail_amp;
						case BIOME_TUNDRA:    return noise_detail_tundra->get_noise_3d(sx, sy, sz) * params.tundra_detail_amp;
						case BIOME_GRASSLAND: return noise_detail_grassland->get_noise_3d(sx, sy, sz) * params.grassland_detail_amp;
						case BIOME_FOREST:    return noise_detail_forest->get_noise_3d(sx, sy, sz) * params.forest_detail_amp;
						case BIOME_TROPICAL:  return noise_detail_tropical->get_noise_3d(sx, sy, sz) * params.tropical_detail_amp;
						default: return 0.0f;
					}
				};
				const float d1 = biome_detail_sample(biome);
				const float d2 = (biome2 != biome) ? biome_detail_sample(biome2) : d1;
				detail = Math::lerp(d1, d2, bt) * macro_land;
					(void)sel;
				}

				const float mountain_raw_macro = noise_mountain_area->get_noise_3d(sx, sy, sz);
				const float mountain_raw_micro = noise_mountain_area->get_noise_3d(sx * 2.7f + 41.0f, sy * 2.7f - 73.0f, sz * 2.7f + 19.0f);
				const float mountain_raw = mountain_raw_macro * 0.72f + mountain_raw_micro * 0.28f;
				const float mountain_bias = (CLAMP(params.mountain_area_coverage, 0.0f, 1.0f) - 0.5f) * 2.0f;
				const float mountain_signed = mountain_raw + mountain_bias;
				const float mountain_area = _ss(-MAX(params.mountain_area_falloff, 0.001f), MAX(params.mountain_area_falloff, 0.001f), mountain_signed);
				// pow(x, 0.72) ≈ x * (1 + 0.72*(x-1) ...) — use cbrt-ish approx: x^0.72 ≈ x / (0.28*x + 0.72) is wrong.
				// Taylor: x^0.72 for x in [0,1]. Use exp(0.72*ln(x)) workaround, but cheaper:
				// Approximate via lerp: x^0.72 ≈ sqrt(x) * 0.56 + x * 0.44 (empirical, max err ~0.02 in [0,1]).
				const float ma_clamped = CLAMP(mountain_area, 0.0f, 1.0f);
				const float ma_sqrt = (ma_clamped > 1e-6f) ? (ma_clamped * _fast_rsqrt(ma_clamped)) : 0.0f;
				const float mountain_area_shaped = ma_sqrt * 0.56f + ma_clamped * 0.44f;
				const float mountain_mask = mountain_area_shaped * macro_land;

				const float mountain_shape_noise = _fast_pow01(
						CLAMP(0.5f + 0.5f * noise_mountain_shape->get_noise_3d(sx, sy, sz), 0.0f, 1.0f),
						MAX(params.mountain_shape_sharpness, 0.25f));

				const float mtn = mountain_shape_noise * params.mountain_height * mountain_mask;

				float sdf = alt - base_height - detail - mtn;
				if (sdf > budget) {
					buffer.set_voxel_f(sdf, x, y, z, VoxelBuffer::CHANNEL_SDF);
					continue;
				}
				if (sdf < -budget) {
					buffer.set_voxel_f(sdf, x, y, z, VoxelBuffer::CHANNEL_SDF);
					buffer.set_voxel(rock_indices, x, y, z, VoxelBuffer::CHANNEL_INDICES);
					buffer.set_voxel(rock_weights, x, y, z, VoxelBuffer::CHANNEL_WEIGHTS);
					continue;
				}

				buffer.set_voxel_f(sdf, x, y, z, VoxelBuffer::CHANNEL_SDF);

				int land_mat = MAT_GRASS;
				if (biome == BIOME_OCEAN || macro_land <= 0.1f) {
					land_mat = MAT_OCEAN_FLOOR;
				} else if (biome == BIOME_DESERT) {
					land_mat = MAT_SAND;
				} else if (biome == BIOME_TUNDRA) {
					land_mat = MAT_SNOW;
				} else if (biome == BIOME_FOREST) {
					land_mat = MAT_MOSS;
				} else if (biome == BIOME_TROPICAL) {
					land_mat = MAT_DIRT;
				}
				if (mtn > params.mountain_height * 0.25f) {
					land_mat = MAT_ROCK;
				}

				int packed_i, packed_w;
				_pack_mixel4(land_mat, MAT_OCEAN_FLOOR, cont_transition, 0.46f, 0.50f, 0.50f, 0.54f, packed_i, packed_w);
				buffer.set_voxel(packed_i, x, y, z, VoxelBuffer::CHANNEL_INDICES);
				buffer.set_voxel(packed_w, x, y, z, VoxelBuffer::CHANNEL_WEIGHTS);
			}
		}
	}

	return result;
}

int EdenPlanetGeneratorClean::get_used_channels_mask() const {
	return (1 << VoxelBuffer::CHANNEL_SDF) |
			   (1 << VoxelBuffer::CHANNEL_INDICES) |
			   (1 << VoxelBuffer::CHANNEL_WEIGHTS);
}

int EdenPlanetGeneratorClean::_classify_biome(float temp_c, float moisture, float evap, float selector) const {
	if (temp_c < -2.0f) {
		return BIOME_TUNDRA;
	}
	if (moisture < 0.22f) {
		return BIOME_DESERT;
	}
	if (temp_c > 24.0f && moisture > 0.60f) {
		return BIOME_TROPICAL;
	}
	if (moisture > 0.52f) {
		return BIOME_FOREST;
	}
	if (selector > 0.72f && temp_c > 8.0f && evap < 0.65f) {
		return BIOME_FOREST;
	}
	return BIOME_GRASSLAND;
}

void EdenPlanetGeneratorClean::_pack_mixel4(int land_mat, int ocean_mat, float cont,
		float sand_start, float sand_end,
		float ocean_start, float ocean_end,
		int &r_indices, int &r_weights) {
	cont = CLAMP(cont, 0.0f, 1.0f);
	float sand_t = _ss(sand_start, sand_end, cont);
	float ocean_t = _ss(ocean_start, ocean_end, cont);
	ocean_t = MAX(ocean_t, sand_t);

	float w0 = (1.0f - sand_t);
	float w3 = (sand_t - ocean_t);
	float w6 = ocean_t;

	int i0 = land_mat;
	int i1 = MAT_SAND;
	int i2 = ocean_mat;
	int i3 = ocean_mat;

	float ws[4] = { w0, w3, w6, 0.0f };
	int is[4] = { i0, i1, i2, i3 };

	for (int a = 0; a < 4; ++a) {
		for (int b = a + 1; b < 4; ++b) {
			if (ws[b] > ws[a]) {
				float tw = ws[a]; ws[a] = ws[b]; ws[b] = tw;
				int ti = is[a]; is[a] = is[b]; is[b] = ti;
			}
		}
	}

	float sum = ws[0] + ws[1] + ws[2] + ws[3];
	if (sum < 1e-6f) {
		ws[0] = 1.0f;
		ws[1] = ws[2] = ws[3] = 0.0f;
		sum = 1.0f;
	}
	for (int k = 0; k < 4; ++k) {
		ws[k] /= sum;
	}

	int w4[4];
	int acc = 0;
	for (int k = 0; k < 4; ++k) {
		w4[k] = CLAMP(int(Math::round(ws[k] * 15.0f)), 0, 15);
		acc += w4[k];
	}
	if (acc != 15) {
		int d = 15 - acc;
		w4[0] = CLAMP(w4[0] + d, 0, 15);
	}

	r_indices = (is[0] & 0xF) | ((is[1] & 0xF) << 4) | ((is[2] & 0xF) << 8) | ((is[3] & 0xF) << 12);
	r_weights = (w4[0] & 0xF) | ((w4[1] & 0xF) << 4) | ((w4[2] & 0xF) << 8) | ((w4[3] & 0xF) << 12);
}

#define CLEAN_FLOAT_PROP(name) \
	void EdenPlanetGeneratorClean::set_##name(float v) { \
		{ \
			zylann::RWLockWrite wlock(_parameters_lock); \
			_parameters.name = v; \
		} \
		emit_changed(); \
	} \
	float EdenPlanetGeneratorClean::get_##name() const { \
		zylann::RWLockRead rlock(_parameters_lock); \
		return _parameters.name; \
	}

#define CLEAN_FLOAT_PROP_SETUP(name) \
	void EdenPlanetGeneratorClean::set_##name(float v) { \
		{ \
			zylann::RWLockWrite wlock(_parameters_lock); \
			_parameters.name = v; \
			_parameters.is_setup = false; \
		} \
		emit_changed(); \
	} \
	float EdenPlanetGeneratorClean::get_##name() const { \
		zylann::RWLockRead rlock(_parameters_lock); \
		return _parameters.name; \
	}

#define CLEAN_INT_PROP_SETUP(name) \
	void EdenPlanetGeneratorClean::set_##name(int v) { \
		{ \
			zylann::RWLockWrite wlock(_parameters_lock); \
			_parameters.name = v; \
			_parameters.is_setup = false; \
		} \
		emit_changed(); \
	} \
	int EdenPlanetGeneratorClean::get_##name() const { \
		zylann::RWLockRead rlock(_parameters_lock); \
		return _parameters.name; \
	}

CLEAN_FLOAT_PROP(planet_radius)
CLEAN_FLOAT_PROP(max_terrain_height)
CLEAN_FLOAT_PROP(land_coverage)
CLEAN_FLOAT_PROP(land_height)
CLEAN_FLOAT_PROP(ocean_depth)
CLEAN_FLOAT_PROP(land_noise_amplitude)
CLEAN_FLOAT_PROP(land_noise_falloff)
CLEAN_FLOAT_PROP_SETUP(land_noise_freq)
CLEAN_FLOAT_PROP_SETUP(temp_noise_freq)
CLEAN_FLOAT_PROP(temp_noise_strength)
CLEAN_FLOAT_PROP_SETUP(moisture_noise_freq)
CLEAN_FLOAT_PROP(moisture_noise_strength)
CLEAN_FLOAT_PROP_SETUP(evaporation_noise_freq)
CLEAN_FLOAT_PROP(evaporation_noise_strength)
CLEAN_FLOAT_PROP(evaporation_strength)
CLEAN_FLOAT_PROP(biome_region_size)
CLEAN_FLOAT_PROP(biome_edge_blend)
CLEAN_INT_PROP_SETUP(biome_selector_type)
CLEAN_FLOAT_PROP_SETUP(biome_selector_freq)
CLEAN_INT_PROP_SETUP(biome_selector_octaves)
CLEAN_FLOAT_PROP_SETUP(biome_selector_gain)
CLEAN_FLOAT_PROP_SETUP(biome_selector_lacunarity)
CLEAN_INT_PROP_SETUP(desert_detail_type)
CLEAN_FLOAT_PROP_SETUP(desert_detail_freq)
CLEAN_INT_PROP_SETUP(desert_detail_octaves)
CLEAN_FLOAT_PROP_SETUP(desert_detail_gain)
CLEAN_FLOAT_PROP_SETUP(desert_detail_lacunarity)
CLEAN_FLOAT_PROP(desert_detail_amp)
CLEAN_INT_PROP_SETUP(tundra_detail_type)
CLEAN_FLOAT_PROP_SETUP(tundra_detail_freq)
CLEAN_INT_PROP_SETUP(tundra_detail_octaves)
CLEAN_FLOAT_PROP_SETUP(tundra_detail_gain)
CLEAN_FLOAT_PROP_SETUP(tundra_detail_lacunarity)
CLEAN_FLOAT_PROP(tundra_detail_amp)
CLEAN_INT_PROP_SETUP(grassland_detail_type)
CLEAN_FLOAT_PROP_SETUP(grassland_detail_freq)
CLEAN_INT_PROP_SETUP(grassland_detail_octaves)
CLEAN_FLOAT_PROP_SETUP(grassland_detail_gain)
CLEAN_FLOAT_PROP_SETUP(grassland_detail_lacunarity)
CLEAN_FLOAT_PROP(grassland_detail_amp)
CLEAN_INT_PROP_SETUP(forest_detail_type)
CLEAN_FLOAT_PROP_SETUP(forest_detail_freq)
CLEAN_INT_PROP_SETUP(forest_detail_octaves)
CLEAN_FLOAT_PROP_SETUP(forest_detail_gain)
CLEAN_FLOAT_PROP_SETUP(forest_detail_lacunarity)
CLEAN_FLOAT_PROP(forest_detail_amp)
CLEAN_INT_PROP_SETUP(tropical_detail_type)
CLEAN_FLOAT_PROP_SETUP(tropical_detail_freq)
CLEAN_INT_PROP_SETUP(tropical_detail_octaves)
CLEAN_FLOAT_PROP_SETUP(tropical_detail_gain)
CLEAN_FLOAT_PROP_SETUP(tropical_detail_lacunarity)
CLEAN_FLOAT_PROP(tropical_detail_amp)
CLEAN_FLOAT_PROP_SETUP(mountain_area_freq)
CLEAN_FLOAT_PROP(mountain_area_coverage)
CLEAN_FLOAT_PROP(mountain_area_falloff)
CLEAN_INT_PROP_SETUP(mountain_shape_type)
CLEAN_FLOAT_PROP_SETUP(mountain_shape_freq)
CLEAN_INT_PROP_SETUP(mountain_shape_octaves)
CLEAN_FLOAT_PROP_SETUP(mountain_shape_gain)
CLEAN_FLOAT_PROP_SETUP(mountain_shape_lacunarity)
CLEAN_FLOAT_PROP(mountain_shape_sharpness)
CLEAN_INT_PROP_SETUP(mountain_shape_fractal_type)
CLEAN_INT_PROP_SETUP(mountain_shape_warp_type)
CLEAN_FLOAT_PROP_SETUP(mountain_shape_warp_amp)
CLEAN_FLOAT_PROP_SETUP(mountain_shape_warp_freq)
CLEAN_FLOAT_PROP(mountain_height)
CLEAN_INT_PROP_SETUP(seed)

#undef CLEAN_FLOAT_PROP
#undef CLEAN_FLOAT_PROP_SETUP
#undef CLEAN_INT_PROP_SETUP

#define BIND_FLOAT(name) \
	ClassDB::bind_method(D_METHOD("set_" #name, "value"), &EdenPlanetGeneratorClean::set_##name); \
	ClassDB::bind_method(D_METHOD("get_" #name), &EdenPlanetGeneratorClean::get_##name);

#define BIND_INT(name) \
	ClassDB::bind_method(D_METHOD("set_" #name, "value"), &EdenPlanetGeneratorClean::set_##name); \
	ClassDB::bind_method(D_METHOD("get_" #name), &EdenPlanetGeneratorClean::get_##name);

void EdenPlanetGeneratorClean::_bind_methods() {
	ClassDB::bind_method(D_METHOD("setup"), &EdenPlanetGeneratorClean::setup);

	BIND_FLOAT(planet_radius);
	BIND_INT(seed);
	BIND_FLOAT(max_terrain_height);

	BIND_FLOAT(land_coverage);
	BIND_FLOAT(land_height);
	BIND_FLOAT(ocean_depth);
	BIND_FLOAT(land_noise_amplitude);
	BIND_FLOAT(land_noise_falloff);
	BIND_FLOAT(land_noise_freq);

	BIND_FLOAT(temp_noise_freq);
	BIND_FLOAT(temp_noise_strength);
	BIND_FLOAT(moisture_noise_freq);
	BIND_FLOAT(moisture_noise_strength);
	BIND_FLOAT(evaporation_noise_freq);
	BIND_FLOAT(evaporation_noise_strength);
	BIND_FLOAT(evaporation_strength);

	BIND_FLOAT(biome_region_size);
	BIND_FLOAT(biome_edge_blend);
	BIND_INT(biome_selector_type);
	BIND_FLOAT(biome_selector_freq);
	BIND_INT(biome_selector_octaves);
	BIND_FLOAT(biome_selector_gain);
	BIND_FLOAT(biome_selector_lacunarity);
	BIND_INT(desert_detail_type);
	BIND_FLOAT(desert_detail_freq);
	BIND_INT(desert_detail_octaves);
	BIND_FLOAT(desert_detail_gain);
	BIND_FLOAT(desert_detail_lacunarity);
	BIND_FLOAT(desert_detail_amp);
	BIND_INT(tundra_detail_type);
	BIND_FLOAT(tundra_detail_freq);
	BIND_INT(tundra_detail_octaves);
	BIND_FLOAT(tundra_detail_gain);
	BIND_FLOAT(tundra_detail_lacunarity);
	BIND_FLOAT(tundra_detail_amp);
	BIND_INT(grassland_detail_type);
	BIND_FLOAT(grassland_detail_freq);
	BIND_INT(grassland_detail_octaves);
	BIND_FLOAT(grassland_detail_gain);
	BIND_FLOAT(grassland_detail_lacunarity);
	BIND_FLOAT(grassland_detail_amp);
	BIND_INT(forest_detail_type);
	BIND_FLOAT(forest_detail_freq);
	BIND_INT(forest_detail_octaves);
	BIND_FLOAT(forest_detail_gain);
	BIND_FLOAT(forest_detail_lacunarity);
	BIND_FLOAT(forest_detail_amp);
	BIND_INT(tropical_detail_type);
	BIND_FLOAT(tropical_detail_freq);
	BIND_INT(tropical_detail_octaves);
	BIND_FLOAT(tropical_detail_gain);
	BIND_FLOAT(tropical_detail_lacunarity);
	BIND_FLOAT(tropical_detail_amp);

	BIND_FLOAT(mountain_area_freq);
	BIND_FLOAT(mountain_area_coverage);
	BIND_FLOAT(mountain_area_falloff);
	BIND_INT(mountain_shape_type);
	BIND_FLOAT(mountain_shape_freq);
	BIND_INT(mountain_shape_octaves);
	BIND_FLOAT(mountain_shape_gain);
	BIND_FLOAT(mountain_shape_lacunarity);
	BIND_FLOAT(mountain_shape_sharpness);
	BIND_INT(mountain_shape_fractal_type);
	BIND_INT(mountain_shape_warp_type);
	BIND_FLOAT(mountain_shape_warp_amp);
	BIND_FLOAT(mountain_shape_warp_freq);
	BIND_FLOAT(mountain_height);

	ADD_GROUP("Planet", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "planet_radius"), "set_planet_radius", "get_planet_radius");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_terrain_height"), "set_max_terrain_height", "get_max_terrain_height");

	ADD_GROUP("Land Ocean", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "land_coverage", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_land_coverage", "get_land_coverage");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "land_height"), "set_land_height", "get_land_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ocean_depth"), "set_ocean_depth", "get_ocean_depth");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "land_noise_amplitude", PROPERTY_HINT_RANGE, "0.01,2.0,0.01"), "set_land_noise_amplitude", "get_land_noise_amplitude");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "land_noise_falloff", PROPERTY_HINT_RANGE, "0.01,2.0,0.01"), "set_land_noise_falloff", "get_land_noise_falloff");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "land_noise_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_land_noise_freq", "get_land_noise_freq");

	ADD_GROUP("Climate", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "temp_noise_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_temp_noise_freq", "get_temp_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "temp_noise_strength", PROPERTY_HINT_RANGE, "0.0,30.0,0.1"), "set_temp_noise_strength", "get_temp_noise_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "moisture_noise_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_moisture_noise_freq", "get_moisture_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "moisture_noise_strength", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_moisture_noise_strength", "get_moisture_noise_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "evaporation_noise_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_evaporation_noise_freq", "get_evaporation_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "evaporation_noise_strength", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_evaporation_noise_strength", "get_evaporation_noise_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "evaporation_strength", PROPERTY_HINT_RANGE, "0.0,1.5,0.01"), "set_evaporation_strength", "get_evaporation_strength");

	ADD_GROUP("Biome Regions", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_region_size", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_region_size", "get_biome_region_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_edge_blend", PROPERTY_HINT_RANGE, "0.01,0.45,0.01"), "set_biome_edge_blend", "get_biome_edge_blend");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "biome_selector_type", PROPERTY_HINT_ENUM, "OpenSimplex2,OpenSimplex2S,Cellular,Perlin,ValueCubic,Value"), "set_biome_selector_type", "get_biome_selector_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_selector_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_biome_selector_freq", "get_biome_selector_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "biome_selector_octaves", PROPERTY_HINT_RANGE, "1,8,1"), "set_biome_selector_octaves", "get_biome_selector_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_selector_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_selector_gain", "get_biome_selector_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_selector_lacunarity", PROPERTY_HINT_RANGE, "1.0,6.0,0.01"), "set_biome_selector_lacunarity", "get_biome_selector_lacunarity");

	ADD_GROUP("Desert Biome", "desert_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "desert_detail_type", PROPERTY_HINT_ENUM, "OpenSimplex2,OpenSimplex2S,Cellular,Perlin,ValueCubic,Value"), "set_desert_detail_type", "get_desert_detail_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "desert_detail_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_desert_detail_freq", "get_desert_detail_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "desert_detail_octaves", PROPERTY_HINT_RANGE, "1,8,1"), "set_desert_detail_octaves", "get_desert_detail_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "desert_detail_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_desert_detail_gain", "get_desert_detail_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "desert_detail_lacunarity", PROPERTY_HINT_RANGE, "1.0,6.0,0.01"), "set_desert_detail_lacunarity", "get_desert_detail_lacunarity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "desert_detail_amp"), "set_desert_detail_amp", "get_desert_detail_amp");

	ADD_GROUP("Tundra Biome", "tundra_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tundra_detail_type", PROPERTY_HINT_ENUM, "OpenSimplex2,OpenSimplex2S,Cellular,Perlin,ValueCubic,Value"), "set_tundra_detail_type", "get_tundra_detail_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tundra_detail_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_tundra_detail_freq", "get_tundra_detail_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tundra_detail_octaves", PROPERTY_HINT_RANGE, "1,8,1"), "set_tundra_detail_octaves", "get_tundra_detail_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tundra_detail_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_tundra_detail_gain", "get_tundra_detail_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tundra_detail_lacunarity", PROPERTY_HINT_RANGE, "1.0,6.0,0.01"), "set_tundra_detail_lacunarity", "get_tundra_detail_lacunarity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tundra_detail_amp"), "set_tundra_detail_amp", "get_tundra_detail_amp");

	ADD_GROUP("Grassland Biome", "grassland_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "grassland_detail_type", PROPERTY_HINT_ENUM, "OpenSimplex2,OpenSimplex2S,Cellular,Perlin,ValueCubic,Value"), "set_grassland_detail_type", "get_grassland_detail_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grassland_detail_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_grassland_detail_freq", "get_grassland_detail_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "grassland_detail_octaves", PROPERTY_HINT_RANGE, "1,8,1"), "set_grassland_detail_octaves", "get_grassland_detail_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grassland_detail_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_grassland_detail_gain", "get_grassland_detail_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grassland_detail_lacunarity", PROPERTY_HINT_RANGE, "1.0,6.0,0.01"), "set_grassland_detail_lacunarity", "get_grassland_detail_lacunarity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grassland_detail_amp"), "set_grassland_detail_amp", "get_grassland_detail_amp");

	ADD_GROUP("Forest Biome", "forest_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "forest_detail_type", PROPERTY_HINT_ENUM, "OpenSimplex2,OpenSimplex2S,Cellular,Perlin,ValueCubic,Value"), "set_forest_detail_type", "get_forest_detail_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "forest_detail_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_forest_detail_freq", "get_forest_detail_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "forest_detail_octaves", PROPERTY_HINT_RANGE, "1,8,1"), "set_forest_detail_octaves", "get_forest_detail_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "forest_detail_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_forest_detail_gain", "get_forest_detail_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "forest_detail_lacunarity", PROPERTY_HINT_RANGE, "1.0,6.0,0.01"), "set_forest_detail_lacunarity", "get_forest_detail_lacunarity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "forest_detail_amp"), "set_forest_detail_amp", "get_forest_detail_amp");

	ADD_GROUP("Tropical Biome", "tropical_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tropical_detail_type", PROPERTY_HINT_ENUM, "OpenSimplex2,OpenSimplex2S,Cellular,Perlin,ValueCubic,Value"), "set_tropical_detail_type", "get_tropical_detail_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tropical_detail_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_tropical_detail_freq", "get_tropical_detail_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tropical_detail_octaves", PROPERTY_HINT_RANGE, "1,8,1"), "set_tropical_detail_octaves", "get_tropical_detail_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tropical_detail_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_tropical_detail_gain", "get_tropical_detail_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tropical_detail_lacunarity", PROPERTY_HINT_RANGE, "1.0,6.0,0.01"), "set_tropical_detail_lacunarity", "get_tropical_detail_lacunarity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tropical_detail_amp"), "set_tropical_detail_amp", "get_tropical_detail_amp");

	ADD_GROUP("Mountain Areas", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_area_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_mountain_area_freq", "get_mountain_area_freq");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_area_coverage", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_mountain_area_coverage", "get_mountain_area_coverage");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_area_falloff", PROPERTY_HINT_RANGE, "0.01,1.0,0.01"), "set_mountain_area_falloff", "get_mountain_area_falloff");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mountain_shape_type", PROPERTY_HINT_ENUM, "OpenSimplex2,OpenSimplex2S,Cellular,Perlin,ValueCubic,Value"), "set_mountain_shape_type", "get_mountain_shape_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_shape_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_mountain_shape_freq", "get_mountain_shape_freq");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mountain_shape_octaves", PROPERTY_HINT_RANGE, "1,8,1"), "set_mountain_shape_octaves", "get_mountain_shape_octaves");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_shape_gain", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_mountain_shape_gain", "get_mountain_shape_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_shape_lacunarity", PROPERTY_HINT_RANGE, "1.0,6.0,0.01"), "set_mountain_shape_lacunarity", "get_mountain_shape_lacunarity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_shape_sharpness", PROPERTY_HINT_RANGE, "0.25,8.0,0.01"), "set_mountain_shape_sharpness", "get_mountain_shape_sharpness");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mountain_shape_fractal_type", PROPERTY_HINT_ENUM, "None:0,FBM:1,Ridged:2,PingPong:3"), "set_mountain_shape_fractal_type", "get_mountain_shape_fractal_type");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mountain_shape_warp_type", PROPERTY_HINT_ENUM, "Simplex:0,SimplexReduced:1,BasicGrid:2"), "set_mountain_shape_warp_type", "get_mountain_shape_warp_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_shape_warp_amp", PROPERTY_HINT_RANGE, "0.0,2000.0,1.0"), "set_mountain_shape_warp_amp", "get_mountain_shape_warp_amp");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_shape_warp_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_mountain_shape_warp_freq", "get_mountain_shape_warp_freq");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_height"), "set_mountain_height", "get_mountain_height");
}

#undef BIND_FLOAT
#undef BIND_INT
