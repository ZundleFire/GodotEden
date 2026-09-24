#include "eden_planet_generator_v3.h"

#include <cstring>

using zylann::voxel::VoxelBuffer;

namespace {
inline float _fast_pow01(float x, float p) {
	if (x <= 0.0f) return 0.0f;
	if (x >= 1.0f) return 1.0f;
	int32_t ix;
	memcpy(&ix, &x, 4);
	float log2_x = (float)(ix - 0x3F800000) * (1.0f / (float)(1 << 23));
	float t = p * log2_x;
	t = MAX(t, -126.0f);
	int ti = (int)t;
	if (t < 0.0f) ti -= 1;
	float tf = t - (float)ti;
	float exp2f = 1.0f + tf * (0.6930f + tf * (0.2402f + tf * 0.0520f));
	int32_t exp2i_bits = (ti + 127) << 23;
	float exp2i;
	memcpy(&exp2i, &exp2i_bits, 4);
	return exp2i * exp2f;
}

inline float _fast_rsqrt(float x) {
	float xhalf = 0.5f * x;
	int32_t i;
	memcpy(&i, &x, 4);
	i = 0x5f3759df - (i >> 1);
	float y;
	memcpy(&y, &i, 4);
	y = y * (1.5f - xhalf * y * y);
	return y;
}

inline float _spawn_weight(float p_spawn) {
	const float s = CLAMP(p_spawn, 0.0f, 1.0f);
	if (s <= 0.0f) {
		return 0.0f;
	}
	return _fast_pow01(s, 2.35f);
}

inline uint8_t _pick_region_preference(const Vector3 &p_dir, float p_planet_radius, float p_continent_scale,
		const Ref<FastNoiseLite> &p_noise_continent, const Ref<FastNoiseLite> &p_noise_detail,
		RandomNumberGenerator &p_rng,
		float p_desert_spawn, float p_forest_spawn, float p_tropical_spawn,
		float p_tundra_spawn, float p_grassland_spawn, float p_alpine_spawn) {
	const float sx = p_dir.x * p_planet_radius;
	const float sy = p_dir.y * p_planet_radius;
	const float sz = p_dir.z * p_planet_radius;
	const float mx = sx / MAX(p_continent_scale, 0.35f);
	const float my = sy / MAX(p_continent_scale, 0.35f);
	const float mz = sz / MAX(p_continent_scale, 0.35f);
	const float lat01 = Math::abs(p_dir.y);
	const float climate_macro = 0.5f + 0.5f * p_noise_continent->get_noise_3d(mx * 0.26f - 131.0f, my * 0.26f + 67.0f, mz * 0.26f - 29.0f);
	const float humidity_macro = 0.5f + 0.5f * p_noise_detail->get_noise_3d(sx + 91.0f, sy - 44.0f, sz + 17.0f);
	const float warm_hint = 1.0f - CLAMP((lat01 - 0.18f) / 0.58f, 0.0f, 1.0f);
	const float cold_hint = CLAMP((lat01 - 0.58f) / 0.30f, 0.0f, 1.0f);
	const float wet_hint = CLAMP(climate_macro * 0.55f + humidity_macro * 0.45f, 0.0f, 1.0f);
	const float dry_hint = 1.0f - wet_hint;

	const float desert_spawn_w = _spawn_weight(p_desert_spawn);
	const float forest_spawn_w = _spawn_weight(p_forest_spawn);
	const float tropical_spawn_w = _spawn_weight(p_tropical_spawn);
	const float tundra_spawn_w = _spawn_weight(p_tundra_spawn);
	const float grassland_spawn_w = _spawn_weight(p_grassland_spawn);
	const float alpine_spawn_w = _spawn_weight(p_alpine_spawn);

	float weights[6] = {
		(0.10f + warm_hint * (0.24f + dry_hint * 0.96f)) * desert_spawn_w,                 // desert
		(0.18f + wet_hint * (0.58f + (1.0f - cold_hint) * 0.20f)) * forest_spawn_w,        // forest
		(0.08f + warm_hint * wet_hint * 1.18f) * tropical_spawn_w,                          // tropical
		(0.08f + cold_hint * 0.92f) * tundra_spawn_w,                                       // tundra
		(0.14f + (1.0f - Math::abs(wet_hint - 0.5f) * 1.75f) * 0.38f) * grassland_spawn_w, // grassland
		(0.04f + cold_hint * 0.18f) * alpine_spawn_w                                        // alpine
	};
	const uint8_t kinds[6] = { 1, 2, 3, 4, 5, 6 };
	float total = 0.0f;
	for (int i = 0; i < 6; ++i) {
		weights[i] = MAX(weights[i], 0.0f);
		total += weights[i];
	}
	if (total <= 0.0001f) {
		return 5;
	}
	float pick = p_rng.randf() * total;
	for (int i = 0; i < 6; ++i) {
		pick -= weights[i];
		if (pick <= 0.0f) {
			return kinds[i];
		}
	}
	return 5;
}

inline void _apply_region_preference(uint8_t p_preferred_kind, float p_strength,
		float &r_desert, float &r_forest, float &r_tropical,
		float &r_tundra, float &r_grassland, float &r_alpine) {
	const float strength = CLAMP(p_strength, 0.0f, 1.0f);
	if (strength <= 0.001f) {
		return;
	}
	const float boost = Math::lerp(1.0f, 1.28f, strength);
	const float soften = Math::lerp(1.0f, 0.94f, strength * 0.60f);
	const float suppress = Math::lerp(1.0f, 0.88f, strength * 0.65f);

	switch (p_preferred_kind) {
		case 1:
			r_desert *= boost;
			r_forest *= soften;
			r_tropical *= soften;
			r_tundra *= suppress;
			r_grassland *= 0.80f + 0.20f * soften;
			break;
		case 2:
			r_forest *= boost;
			r_tropical *= 1.0f + (boost - 1.0f) * 0.35f;
			r_desert *= suppress;
			r_grassland *= soften;
			break;
		case 3:
			r_tropical *= boost;
			r_forest *= 1.0f + (boost - 1.0f) * 0.45f;
			r_tundra *= suppress;
			r_grassland *= soften;
			break;
		case 4:
			r_tundra *= boost;
			r_desert *= suppress;
			r_tropical *= suppress;
			r_forest *= 0.82f + 0.18f * soften;
			break;
		case 6:
			r_alpine *= Math::lerp(1.0f, 2.4f, strength);
			r_grassland *= soften;
			r_forest *= soften;
			r_tropical *= suppress;
			break;
		case 5:
		default:
			r_grassland *= boost;
			r_desert *= soften;
			r_forest *= soften;
			break;
	}
}

inline void _sample_region_preference(const Ref<PlanetTectonics> &p_tectonics,
		const Vector<int> &p_point_region_id,
		const Vector<uint8_t> &p_region_kind,
		const Vector<float> &p_region_strength,
		const Vector3 &p_dir,
		int &r_kind_a, float &r_strength_a,
		int &r_kind_b, float &r_strength_b,
		float &r_blend) {
	r_kind_a = 5;
	r_strength_a = 0.0f;
	r_kind_b = 5;
	r_strength_b = 0.0f;
	r_blend = 0.0f;
	if (p_tectonics.is_null() || p_point_region_id.is_empty() || p_region_kind.is_empty()) {
		return;
	}
	const Vector3 warped_dir = p_tectonics->warp_direction(p_dir);
	int best_idx = 0;
	int second_idx = 0;
	float best_dot = -2.0f;
	float second_dot = -2.0f;
	p_tectonics->get_voronoi().get_two_nearest(warped_dir, best_idx, second_idx, best_dot, second_dot);

	auto load_region = [&](int cell_idx, int &r_kind, float &r_strength) {
		if (cell_idx < 0 || cell_idx >= p_point_region_id.size()) {
			return;
		}
		const int region_id = p_point_region_id[cell_idx];
		if (region_id < 0 || region_id >= p_region_kind.size()) {
			return;
		}
		r_kind = p_region_kind[region_id];
		r_strength = (region_id < p_region_strength.size()) ? p_region_strength[region_id] : 0.0f;
	};

	load_region(best_idx, r_kind_a, r_strength_a);
	load_region(second_idx, r_kind_b, r_strength_b);
	const float ang_a = Math::acos(CLAMP(best_dot, -1.0f, 1.0f));
	const float ang_b = Math::acos(CLAMP(MAX(second_dot, best_dot), -1.0f, 1.0f));
	float t = 1.0f - CLAMP((ang_b - ang_a) / 0.05f, 0.0f, 1.0f);
	r_blend = t * t * (3.0f - 2.0f * t);
	if (r_kind_b == r_kind_a) {
		r_blend = 0.0f;
	}
}
} // namespace

EdenPlanetGeneratorV3::EdenPlanetGeneratorV3() {
}

EdenPlanetGeneratorV3::~EdenPlanetGeneratorV3() {
}

Ref<FastNoiseLite> EdenPlanetGeneratorV3::_make_noise(int p_seed, float p_freq, int p_octaves, float p_gain, float p_lacunarity,
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

void EdenPlanetGeneratorV3::setup() {
	zylann::RWLockWrite wlock(_parameters_lock);
	Parameters &p = _parameters;

	_noise_base = _make_noise(p.seed + 11, p.base_noise_freq, 4, 0.5f, 2.0f, FastNoiseLite::TYPE_SIMPLEX_SMOOTH, FastNoiseLite::FRACTAL_FBM);
	_noise_continent = _make_noise(p.seed + 31, p.continent_noise_freq, 5, 0.52f, 2.05f, FastNoiseLite::TYPE_SIMPLEX_SMOOTH, FastNoiseLite::FRACTAL_FBM);
	_noise_plateau = _make_noise(p.seed + 41, p.plateau_noise_freq, 4, 0.5f, 2.0f, FastNoiseLite::TYPE_SIMPLEX_SMOOTH, FastNoiseLite::FRACTAL_FBM);
	_noise_oceanic = _make_noise(p.seed + 51, p.oceanic_noise_freq, 3, 0.46f, 2.0f, FastNoiseLite::TYPE_SIMPLEX_SMOOTH, FastNoiseLite::FRACTAL_FBM);
	_noise_mountain = _make_noise(p.seed + 61, p.mountain_noise_freq, 5, 0.56f, 2.1f, FastNoiseLite::TYPE_SIMPLEX_SMOOTH, FastNoiseLite::FRACTAL_RIDGED);
	_noise_detail = _make_noise(p.seed + 91, p.detail_noise_freq, 3, 0.48f, 2.0f, FastNoiseLite::TYPE_SIMPLEX_SMOOTH, FastNoiseLite::FRACTAL_FBM);

	_tectonics.instantiate();
	Dictionary tect_params;
	tect_params["n_points"] = p.n_points;
	tect_params["n_plates"] = p.n_plates;
	tect_params["oceanic_fraction"] = p.oceanic_fraction;
	tect_params["border_warp"] = p.border_warp;
	_tectonics->configure(tect_params);
	_tectonics->generate(p.seed);
	_build_biome_regions(p);

	p.is_setup = true;
}

void EdenPlanetGeneratorV3::_build_biome_regions(const Parameters &p) {
	_biome_point_region_id.clear();
	_biome_region_kind.clear();
	_biome_region_strength.clear();

	if (!_tectonics.is_valid()) {
		return;
	}

	const VoronoiSphere &voronoi = _tectonics->get_voronoi();
	const int point_count = voronoi.get_num_points();
	if (point_count <= 0) {
		return;
	}

	_biome_point_region_id.resize(point_count);
	for (int i = 0; i < point_count; ++i) {
		_biome_point_region_id.write[i] = -1;
	}

	const Vector<int> &nbr_offsets = _tectonics->get_neighbor_offsets();
	const Vector<int> &nbr_data = _tectonics->get_neighbor_data();
	const Vector<uint8_t> &oceanic_flags = _tectonics->get_point_oceanic_flags();
	if (nbr_offsets.size() != point_count + 1 || oceanic_flags.size() != point_count || !_noise_continent.is_valid() || !_noise_detail.is_valid()) {
		return;
	}

	RandomNumberGenerator rng;
	rng.set_seed(int64_t(p.seed) * 92821 + point_count * 31 + 197);

	Vector<int> land_points;
	for (int i = 0; i < point_count; ++i) {
		if (oceanic_flags[i] == 0) {
			land_points.push_back(i);
		}
	}

	for (int i = land_points.size() - 1; i > 0; --i) {
		const int j = rng.randi() % (i + 1);
		const int tmp = land_points[i];
		land_points.write[i] = land_points[j];
		land_points.write[j] = tmp;
	}

	const int min_cells = CLAMP(p.biome_region_min_cells, 1, 64);
	const int max_cells = CLAMP(MAX(p.biome_region_max_cells, min_cells), min_cells, 128);

	for (int lp = 0; lp < land_points.size(); ++lp) {
		const int seed_idx = land_points[lp];
		if (seed_idx < 0 || seed_idx >= point_count || _biome_point_region_id[seed_idx] != -1) {
			continue;
		}

		const int target_cells = CLAMP(int(rng.randi_range(min_cells, max_cells)), min_cells, max_cells);
		const Vector3 &seed_dir = voronoi.get_point(seed_idx);
		uint8_t region_kind = _pick_region_preference(seed_dir, p.planet_radius, p.continent_size_scale, _noise_continent, _noise_detail, rng,
				p.biome_spawn_desert, p.biome_spawn_forest, p.biome_spawn_tropical,
				p.biome_spawn_tundra, p.biome_spawn_grassland, p.biome_spawn_alpine);
		int neighbor_kind_hist[7] = { 0, 0, 0, 0, 0, 0, 0 };
		for (int k = nbr_offsets[seed_idx]; k < nbr_offsets[seed_idx + 1]; ++k) {
			const int nb = nbr_data[k];
			if (nb < 0 || nb >= point_count) {
				continue;
			}
			const int nb_region = _biome_point_region_id[nb];
			if (nb_region < 0 || nb_region >= _biome_region_kind.size()) {
				continue;
			}
			const int nb_kind = _biome_region_kind[nb_region];
			if (nb_kind >= 0 && nb_kind < 7) {
				neighbor_kind_hist[nb_kind] += 1;
			}
		}
		const int same_neighbor_count = (region_kind >= 0 && region_kind < 7) ? neighbor_kind_hist[region_kind] : 0;
		if (same_neighbor_count >= 1) {
			const float sx = seed_dir.x * p.planet_radius / MAX(p.continent_size_scale, 0.001f);
			const float sy = seed_dir.y * p.planet_radius / MAX(p.continent_size_scale, 0.001f);
			const float sz = seed_dir.z * p.planet_radius / MAX(p.continent_size_scale, 0.001f);
			const float wet_macro = 0.5f + 0.5f * _noise_detail->get_noise_3d(sx + 91.0f, sy - 44.0f, sz + 17.0f);
			const float climate_macro = 0.5f + 0.5f * _noise_continent->get_noise_3d(sx * 0.26f - 131.0f, sy * 0.26f + 67.0f, sz * 0.26f - 29.0f);
			const float lat01 = Math::abs(seed_dir.y);
			const float warm_macro = CLAMP(1.0f - _fast_pow01(lat01, 1.15f) + (climate_macro - 0.5f) * 0.18f, 0.0f, 1.0f);
			const float dry_macro = CLAMP((1.0f - wet_macro) * 0.72f + (0.5f - climate_macro) * 0.22f, 0.0f, 1.0f);
			const float diversify_chance = CLAMP(0.55f + 0.15f * float(same_neighbor_count), 0.0f, 0.92f);
			if (rng.randf() < diversify_chance) {
				switch (region_kind) {
					case 1:
						region_kind = (wet_macro > 0.54f) ? ((warm_macro > 0.62f) ? 3 : 2) : 5;
						break;
					case 2:
						region_kind = (dry_macro > 0.58f) ? 5 : ((warm_macro > 0.66f && wet_macro > 0.60f) ? 3 : 5);
						break;
					case 3:
						region_kind = (wet_macro < 0.50f) ? 2 : 5;
						break;
					case 4:
						region_kind = (lat01 < 0.72f) ? 5 : 6;
						break;
					case 6:
						region_kind = (lat01 < 0.68f) ? 5 : 4;
						break;
					case 5:
					default:
						region_kind = (dry_macro > 0.64f) ? 1 : ((wet_macro > 0.60f) ? ((warm_macro > 0.64f) ? 3 : 2) : 2);
						break;
				}
			}
		}
		const float region_bias = CLAMP(p.biome_region_bias_strength, 0.0f, 1.0f);
		const float region_strength = rng.randf_range(MAX(0.06f, region_bias * 0.16f), MIN(0.42f, region_bias * 0.48f + 0.04f));
		const int region_id = _biome_region_kind.size();
		_biome_region_kind.push_back(region_kind);
		_biome_region_strength.push_back(region_strength);
		_biome_point_region_id.write[seed_idx] = region_id;

		Vector<int> frontier;
		frontier.push_back(seed_idx);
		int head = 0;
		int assigned = 1;
		while (head < frontier.size() && assigned < target_cells) {
			const int current = frontier[head++];
			for (int k = nbr_offsets[current]; k < nbr_offsets[current + 1] && assigned < target_cells; ++k) {
				const int nb = nbr_data[k];
				if (nb < 0 || nb >= point_count) {
					continue;
				}
				if (oceanic_flags[nb] != 0 || _biome_point_region_id[nb] != -1) {
					continue;
				}
				_biome_point_region_id.write[nb] = region_id;
				frontier.push_back(nb);
				++assigned;
			}
		}
	}

	for (int i = 0; i < point_count; ++i) {
		if (oceanic_flags[i] != 0 || _biome_point_region_id[i] != -1) {
			continue;
		}
		int fallback_region = -1;
		for (int k = nbr_offsets[i]; k < nbr_offsets[i + 1]; ++k) {
			const int nb = nbr_data[k];
			if (nb >= 0 && nb < point_count && _biome_point_region_id[nb] >= 0) {
				fallback_region = _biome_point_region_id[nb];
				break;
			}
		}
		if (fallback_region < 0) {
			const Vector3 &seed_dir = voronoi.get_point(i);
			fallback_region = _biome_region_kind.size();
			_biome_region_kind.push_back(_pick_region_preference(seed_dir, p.planet_radius, p.continent_size_scale, _noise_continent, _noise_detail, rng,
					p.biome_spawn_desert, p.biome_spawn_forest, p.biome_spawn_tropical,
					p.biome_spawn_tundra, p.biome_spawn_grassland, p.biome_spawn_alpine));
			_biome_region_strength.push_back(CLAMP(p.biome_region_bias_strength * 0.38f + 0.05f, 0.08f, 0.42f));
		}
		_biome_point_region_id.write[i] = fallback_region;
	}

	if (_biome_region_kind.size() > 1) {
		Vector<int> region_sizes;
		region_sizes.resize(_biome_region_kind.size());
		for (int i = 0; i < region_sizes.size(); ++i) {
			region_sizes.write[i] = 0;
		}
		for (int i = 0; i < point_count; ++i) {
			const int region_id = _biome_point_region_id[i];
			if (region_id >= 0 && region_id < region_sizes.size() && oceanic_flags[i] == 0) {
				region_sizes.write[region_id] += 1;
			}
		}
		for (int i = 0; i < point_count; ++i) {
			const int region_id = _biome_point_region_id[i];
			if (oceanic_flags[i] != 0 || region_id < 0 || region_id >= region_sizes.size() || region_sizes[region_id] >= min_cells) {
				continue;
			}
			int merge_region = -1;
			for (int k = nbr_offsets[i]; k < nbr_offsets[i + 1]; ++k) {
				const int nb = nbr_data[k];
				if (nb >= 0 && nb < point_count && oceanic_flags[nb] == 0 && _biome_point_region_id[nb] >= 0 && _biome_point_region_id[nb] != region_id && region_sizes[_biome_point_region_id[nb]] < max_cells) {
					merge_region = _biome_point_region_id[nb];
					break;
				}
			}
			if (merge_region < 0) {
				for (int j = 0; j < region_sizes.size(); ++j) {
					if (j != region_id && region_sizes[j] >= min_cells && region_sizes[j] < max_cells) {
						merge_region = j;
						break;
					}
				}
			}
			if (merge_region < 0) {
				for (int j = 0; j < region_sizes.size(); ++j) {
					if (j != region_id && region_sizes[j] >= min_cells) {
						merge_region = j;
						break;
					}
				}
			}
			if (merge_region >= 0) {
				region_sizes.write[region_id] -= 1;
				region_sizes.write[merge_region] += 1;
				_biome_point_region_id.write[i] = merge_region;
			}
		}
	}
}

Dictionary EdenPlanetGeneratorV3::get_biome_region_stats() const {
	Dictionary stats;
	zylann::RWLockRead rlock(_parameters_lock);
	stats["region_count"] = _biome_region_kind.size();
	if (_biome_region_kind.is_empty() || _biome_point_region_id.is_empty() || !_tectonics.is_valid()) {
		stats["min_cells"] = 0;
		stats["max_cells"] = 0;
		stats["avg_cells"] = 0.0;
		return stats;
	}

	const Vector<uint8_t> &oceanic_flags = _tectonics->get_point_oceanic_flags();
	Vector<int> sizes;
	sizes.resize(_biome_region_kind.size());
	for (int i = 0; i < sizes.size(); ++i) {
		sizes.write[i] = 0;
	}
	for (int i = 0; i < _biome_point_region_id.size(); ++i) {
		const int region_id = _biome_point_region_id[i];
		if (region_id >= 0 && region_id < sizes.size() && (i >= oceanic_flags.size() || oceanic_flags[i] == 0)) {
			sizes.write[region_id] += 1;
		}
	}

	int min_cells_used = INT32_MAX;
	int max_cells_used = 0;
	int counted_regions = 0;
	int total_cells = 0;
	for (int i = 0; i < sizes.size(); ++i) {
		if (sizes[i] <= 0) {
			continue;
		}
		min_cells_used = MIN(min_cells_used, sizes[i]);
		max_cells_used = MAX(max_cells_used, sizes[i]);
		total_cells += sizes[i];
		++counted_regions;
	}
	stats["min_cells"] = (counted_regions > 0) ? min_cells_used : 0;
	stats["max_cells"] = max_cells_used;
	stats["avg_cells"] = (counted_regions > 0) ? float(total_cells) / float(counted_regions) : 0.0f;
	return stats;
}

Ref<Image> EdenPlanetGeneratorV3::generate_biome_debug_image(int p_width, int p_height) {
	if (p_width <= 0 || p_height <= 0) {
		return Ref<Image>();
	}

	Parameters params;
	Ref<FastNoiseLite> noise_base;
	Ref<FastNoiseLite> noise_continent;
	Ref<FastNoiseLite> noise_plateau;
	Ref<FastNoiseLite> noise_oceanic;
	Ref<FastNoiseLite> noise_mountain;
	Ref<FastNoiseLite> noise_detail;
	Ref<PlanetTectonics> tectonics;
	{
		zylann::RWLockRead rlock(_parameters_lock);
		params = _parameters;
		noise_base = _noise_base;
		noise_continent = _noise_continent;
		noise_plateau = _noise_plateau;
		noise_oceanic = _noise_oceanic;
		noise_mountain = _noise_mountain;
		noise_detail = _noise_detail;
		tectonics = _tectonics;
	}
	if (!params.is_setup) {
		setup();
		zylann::RWLockRead rlock(_parameters_lock);
		params = _parameters;
		noise_base = _noise_base;
		noise_continent = _noise_continent;
		noise_plateau = _noise_plateau;
		noise_oceanic = _noise_oceanic;
		noise_mountain = _noise_mountain;
		noise_detail = _noise_detail;
		tectonics = _tectonics;
	}
	if (!params.is_setup) {
		return Ref<Image>();
	}

	PackedByteArray pixels;
	pixels.resize(p_width * p_height * 4);
	uint8_t *pw = pixels.ptrw();
	const float max_region_kind = 6.0f;

	for (int py = 0; py < p_height; ++py) {
		const float v = (float(py) + 0.5f) / float(p_height);
		const float lat = (0.5f - v) * float(Math::PI);
		const float cos_lat = Math::cos(lat);
		const float sin_lat = Math::sin(lat);
		for (int px = 0; px < p_width; ++px) {
			const float u = (float(px) + 0.5f) / float(p_width);
			const float lon = (u - 0.5f) * float(Math::TAU);
			const float ux = cos_lat * Math::cos(lon);
			const float uy = sin_lat;
			const float uz = cos_lat * Math::sin(lon);
			const float sx = ux * params.planet_radius;
			const float sy = uy * params.planet_radius;
			const float sz = uz * params.planet_radius;
			const float continent_scale = MAX(params.continent_size_scale, 0.35f);
			const float mx = sx / continent_scale;
			const float my = sy / continent_scale;
			const float mz = sz / continent_scale;

			const float base_n = noise_base->get_noise_3d(sx, sy, sz);
			const float macro_primary = 0.5f + 0.5f * noise_continent->get_noise_3d(mx, my, mz);
			const float macro_secondary = 0.5f + 0.5f * noise_continent->get_noise_3d(mx * 0.43f + 173.4f, my * 0.43f - 91.7f, mz * 0.43f + 47.2f);
			const float macro_support = 0.5f + 0.5f * noise_plateau->get_noise_3d(mx * 0.18f - 63.1f, my * 0.18f + 24.7f, mz * 0.18f + 91.3f);
			const float plateau_n = noise_plateau->get_noise_3d(sx, sy, sz);
			const float oceanic_n = noise_oceanic->get_noise_3d(sx, sy, sz);
			const float detail_n = noise_detail->get_noise_3d(sx, sy, sz);
			const float mountain_area_noise = 0.5f + 0.5f * noise_continent->get_noise_3d(mx * 0.45f + 13.0f, my * 0.45f - 29.0f, mz * 0.45f + 47.0f);
			const float mountain_shape = _fast_pow01(CLAMP(0.5f + 0.5f * noise_mountain->get_noise_3d(sx, sy, sz), 0.0f, 1.0f), MAX(0.65f, params.mountain_shape_boost * 1.45f));
			const PlanetTectonics::TerrainData td = tectonics.is_valid() ? tectonics->get_terrain_data(Vector3(ux, uy, uz)) : PlanetTectonics::TerrainData();
			const float falloff = CLAMP(td.falloff_s, 0.0f, 1.0f);
			const float smooth_falloff = _ss(0.06f, 0.94f, falloff);
			const float oceanic = CLAMP(td.oceanic_s, 0.0f, 1.0f);
			const float plate_bias = td.bias_s;
			const float border_dist_m = MAX(td.border_dist_rad, 0.0f) * params.planet_radius;
			const float tectonic_mask = 1.0f - _ss(450.0f, 12000.0f, border_dist_m);
			const float tectonic_land_bias = (1.0f - oceanic) * Math::lerp(0.38f, 1.0f, smooth_falloff);

			float macro_shape = macro_primary * 0.78f + macro_secondary * 0.16f + macro_support * 0.06f;
			macro_shape = Math::lerp(macro_shape, _ss(0.18f, 0.82f, macro_shape), CLAMP(params.continent_cohesion, 0.0f, 1.0f) * 0.65f);
			const float land_ratio_target = CLAMP(
					params.land_coverage_target +
							(0.5f - CLAMP(params.oceanic_fraction, 0.0f, 1.0f)) * 0.32f * MAX(params.oceanic_fraction_influence, 0.0f) +
							params.continent_balance * 0.16f,
					0.08f,
					0.84f);
			const float land_threshold_center = Math::lerp(0.82f, 0.46f, land_ratio_target);
			const float land_threshold_width = Math::lerp(0.22f, 0.10f, CLAMP(params.continent_cohesion, 0.0f, 1.0f));
			const float humidity_macro = 0.5f + 0.5f * noise_detail->get_noise_3d(sx + 91.0f, sy - 44.0f, sz + 17.0f);
			const float climate_macro = 0.5f + 0.5f * noise_continent->get_noise_3d(mx * 0.26f - 131.0f, my * 0.26f + 67.0f, mz * 0.26f - 29.0f);
			const float tectonic_bias = (macro_secondary - 0.5f) * 0.04f + (climate_macro - 0.5f) * 0.03f - CLAMP(params.oceanic_fraction - 0.5f, -0.5f, 0.5f) * 0.03f;
			const float land_signal = CLAMP(macro_shape + tectonic_bias + tectonic_land_bias * 0.05f - oceanic * 0.03f, 0.0f, 1.0f);
			const float land_mask = _ss(land_threshold_center - land_threshold_width, land_threshold_center + land_threshold_width, land_signal);
			const float core_mask = _ss(land_threshold_center + land_threshold_width * 0.12f, land_threshold_center + land_threshold_width * 1.45f, land_signal);
			const float inland = CLAMP(MAX(core_mask * 0.96f, land_mask), 0.0f, 1.0f);
			const float coast_band = 1.0f - _ss(0.0f, land_threshold_width * 1.35f, Math::abs(land_signal - land_threshold_center));
			const float shore_proximity = CLAMP(MAX(1.0f - inland, coast_band * 0.72f), 0.0f, 1.0f);
			const float climate_coast_bias = CLAMP(tectonic_mask * (0.14f + 0.06f * (1.0f - oceanic)), 0.0f, 1.0f);
			const float coast_proximity = CLAMP(MAX(shore_proximity, climate_coast_bias), 0.0f, 1.0f);
			const float land_factor = CLAMP(Math::lerp(land_mask, core_mask, 0.14f) + tectonic_land_bias * 0.04f + core_mask * 0.02f - oceanic * 0.02f, 0.0f, 1.0f);
			const float continental_shelf = land_factor * (1.0f - inland * 0.82f);
			const float continental_core = land_factor * MAX(inland * 0.56f, core_mask * 0.62f) * Math::lerp(0.58f, 0.92f, smooth_falloff);
			const float interior_macro = 0.5f + 0.5f * noise_continent->get_noise_3d(mx * 0.12f + 401.0f, my * 0.12f - 187.0f, mz * 0.12f + 73.0f);
			const float basin_macro = 0.5f + 0.5f * noise_plateau->get_noise_3d(mx * 0.16f - 233.0f, my * 0.16f + 141.0f, mz * 0.16f - 59.0f);
			const float interior_plateau_mask = continental_core * (0.42f + 0.58f * smooth_falloff) * (0.30f + 0.70f * inland);
			const float basin_cut = MAX(0.0f, 0.56f - basin_macro);
			const float interior_ridge = MAX(0.0f, interior_macro - 0.54f);
			const float relief_bias = CLAMP(continental_core * 0.34f + continental_shelf * 0.18f + mountain_area_noise * 0.18f + tectonic_land_bias * 0.05f, 0.0f, 1.0f);

			float height = (land_signal - land_threshold_center) * 150.0f;
			height += Math::lerp(-params.ocean_depth * 0.82f, params.continent_height * 0.42f, land_factor);
			height += plate_bias * Math::lerp(0.05f, 0.10f, smooth_falloff);
			height += continental_shelf * (34.0f + 38.0f * shore_proximity + humidity_macro * 8.0f);
			height += continental_core * (8.0f + 18.0f * MAX(smooth_falloff, core_mask) + relief_bias * 8.0f);
			height -= interior_plateau_mask * (16.0f + 24.0f * basin_cut + 8.0f * (1.0f - interior_ridge));
			height -= (1.0f - land_factor) * (64.0f + 96.0f * (1.0f - shore_proximity));
			const float broad_plateau_n = noise_plateau->get_noise_3d(sx * 0.22f - 91.0f, sy * 0.22f + 57.0f, sz * 0.22f - 23.0f);
			const float broad_detail_n = noise_detail->get_noise_3d(sx * 0.28f + 211.0f, sy * 0.28f - 133.0f, sz * 0.28f + 71.0f);
			const float smoothed_plateau = Math::lerp(plateau_n, broad_plateau_n, 0.68f);
			const float smoothed_detail = Math::lerp(detail_n, broad_detail_n, 0.72f);
			const float spike_damping = Math::lerp(0.62f, 0.90f, relief_bias);
			const float detail_suppression = Math::lerp(1.0f, 0.58f, shore_proximity * (1.0f - relief_bias * 0.30f));
			const float interior_variation = continental_core * (0.32f + 0.68f * smooth_falloff) * (0.42f + 0.58f * (1.0f - shore_proximity));
			height += base_n * params.base_noise_amp * Math::lerp(0.22f, 0.72f, land_factor * 0.90f + 0.10f);
			height += (macro_primary - 0.5f) * params.continent_noise_amp * land_factor * 0.62f;
			height += (interior_macro - 0.5f) * params.continent_noise_amp * 0.22f * interior_variation;
			height += (basin_macro - 0.5f) * params.continent_noise_amp * 0.16f * interior_variation;
			height += smoothed_plateau * params.plateau_noise_amp * interior_variation * 0.10f * spike_damping;
			height += oceanic_n * params.oceanic_noise_amp * (1.0f - land_factor) * 0.70f;
			height -= basin_cut * params.continent_noise_amp * 0.30f * interior_plateau_mask;
			height += (smoothed_detail * 0.64f + broad_plateau_n * 0.36f) * params.continent_noise_amp * 0.26f * interior_variation;
			height -= MAX(0.0f, 0.50f - (0.5f + 0.5f * broad_detail_n)) * params.continent_noise_amp * 0.20f * interior_variation;
			height += interior_ridge * params.continent_noise_amp * 0.08f * interior_variation;
			float coast_smoothing = shore_proximity * CLAMP(params.coast_smoothing_strength, 0.0f, 1.0f);
			coast_smoothing *= 1.0f - _ss(120.0f, MAX(params.coast_smoothing_width, 121.0f), Math::abs(height));
			if (coast_smoothing > 0.001f) {
				const float coast_target = (land_signal - land_threshold_center) * 92.0f +
						continental_shelf * (42.0f + 34.0f * shore_proximity + humidity_macro * 8.0f) +
						continental_core * (10.0f + 16.0f * MAX(inland, core_mask) + relief_bias * 8.0f) -
						(1.0f - land_factor) * 14.0f;
				height = Math::lerp(height, coast_target, CLAMP(coast_smoothing, 0.0f, 0.92f));
			}
			const float mountain_area = _ss(0.63f, 0.87f, mountain_area_noise) * continental_core * Math::lerp(0.52f, 1.0f, relief_bias);
			const float mountains = mountain_shape * params.mountain_noise_amp * mountain_area * (0.68f + relief_bias * 0.16f);
			height += mountains;
			height += smoothed_detail * params.detail_noise_amp * Math::lerp(0.18f, 0.52f, inland) * detail_suppression;
			const float macro_altitude = MAX(height, 0.0f);
			const float lowland = 1.0f - _ss(140.0f, 1100.0f, macro_altitude);
			const float highland = _ss(1200.0f, 3000.0f, macro_altitude);
			const float rolling_hill = _ss(40.0f, 280.0f, macro_altitude) * (1.0f - _ss(850.0f, 1700.0f, macro_altitude));
			const float coastal_plain_mask = CLAMP(shore_proximity * lowland * land_factor * (1.0f - relief_bias * 0.72f), 0.0f, 1.0f);
			if (coastal_plain_mask > 0.001f) {
				const float rolling_noise = noise_base->get_noise_3d(sx * 0.35f + 411.0f, sy * 0.35f - 173.0f, sz * 0.35f + 97.0f);
				float meadow_target = Math::lerp(18.0f, 95.0f, rolling_noise * 0.5f + 0.5f);
				meadow_target = Math::lerp(meadow_target, 145.0f, rolling_hill * 0.35f);
				height = Math::lerp(height, meadow_target, coastal_plain_mask * 0.48f);
			}
			if (height > 0.0f) {
				height = MAX(height, 5.0f);
			}

			const float lat01 = Math::abs(uy);
			const float polar_latitude = _ss(0.34f, 0.78f, lat01);
			const float equatorial_latitude = 1.0f - _ss(0.18f, 0.54f, lat01);
			const float lat_curve = _fast_pow01(lat01, 1.35f);
			const float rain_shadow = CLAMP(relief_bias * (1.0f - coast_proximity) * (0.22f + smooth_falloff * 0.34f), 0.0f, 1.0f);
			const float climate_variance = CLAMP(Math::abs(humidity_macro - 0.5f) * 1.55f + Math::abs(climate_macro - 0.5f) * 1.35f + Math::abs(basin_macro - 0.5f) * 0.90f, 0.0f, 1.0f);
			const float inland_dryness = CLAMP(inland * 0.22f + (1.0f - coast_proximity) * 0.24f + rain_shadow * 0.28f + MAX(0.0f, 0.5f - humidity_macro) * 0.18f + basin_cut * 0.16f - MAX(climate_macro - 0.55f, 0.0f) * 0.12f, 0.0f, 1.0f);
			const float temp_c = Math::lerp(params.climate_equator_temp_c, params.climate_polar_temp_c, lat_curve) - MAX(height, 0.0f) * 0.0048f + (climate_macro - 0.5f) * 6.0f + (equatorial_latitude - polar_latitude) * 1.5f + (interior_macro - 0.5f) * 2.4f;
			const float shore_moisture = _ss(0.24f, 0.86f, coast_proximity);
			float humidity = Math::lerp(params.climate_interior_humidity, params.climate_ocean_humidity, CLAMP(shore_moisture * 0.56f + (1.0f - lat01) * 0.18f + humidity_macro * 0.16f + MAX(climate_macro - 0.48f, 0.0f) * 0.18f, 0.0f, 1.0f));
			humidity += (humidity_macro - 0.5f) * 0.40f + (climate_macro - 0.5f) * 0.20f + (basin_macro - 0.5f) * 0.18f + (interior_macro - 0.5f) * 0.10f;
			humidity -= inland_dryness * 0.22f + MAX(height, 0.0f) / 5600.0f * 0.06f;
			humidity = CLAMP(humidity, 0.0f, 1.0f);

			const float terrain_alt = MAX(height, 0.0f);
			const float biome_lowland = 1.0f - _ss(280.0f, 1850.0f, terrain_alt);
			const float biome_upland = _ss(350.0f, 2100.0f, terrain_alt);
			const float biome_highland = _ss(1250.0f, 3200.0f, terrain_alt);
			const float habitable_upland = 1.0f - _ss(2400.0f, 4300.0f, terrain_alt);
			const float ruggedness = CLAMP(relief_bias * 0.58f + biome_highland * 0.42f, 0.0f, 1.0f);
			const float warm = CLAMP((temp_c + 20.0f) / 58.0f, 0.0f, 1.0f);
			const float cold = 1.0f - warm;
			const float biome_blend = CLAMP(params.biome_terrain_blend_strength, 0.0f, 1.0f);
			const float wet_zone = CLAMP(climate_macro * 0.55f + humidity_macro * 0.45f, 0.0f, 1.0f);
			int region_pref_kind = 5;
			float region_pref_strength = 0.0f;
			int region_pref_kind_b = 5;
			float region_pref_strength_b = 0.0f;
			float region_pref_blend = 0.0f;
			_sample_region_preference(tectonics, _biome_point_region_id, _biome_region_kind, _biome_region_strength,
					Vector3(ux, uy, uz), region_pref_kind, region_pref_strength, region_pref_kind_b, region_pref_strength_b, region_pref_blend);
			const float dry_zone = 1.0f - wet_zone;
			const float aridness = CLAMP((1.0f - humidity) * 0.60f + inland_dryness * 0.30f + dry_zone * 0.22f + climate_variance * 0.08f - wet_zone * 0.12f, 0.0f, 1.0f);
			const float lushness = CLAMP(humidity * 0.68f + wet_zone * 0.28f + equatorial_latitude * 0.12f + climate_variance * 0.06f - inland_dryness * 0.10f, 0.0f, 1.0f);
			const float inland_wetness = CLAMP(humidity * (0.48f + 0.52f * habitable_upland) + wet_zone * 0.24f + MAX(0.0f, basin_macro - 0.5f) * 0.12f - inland_dryness * 0.10f, 0.0f, 1.0f);
			const float desert_spawn = CLAMP(params.biome_spawn_desert, 0.0f, 1.0f);
			const float forest_spawn = CLAMP(params.biome_spawn_forest, 0.0f, 1.0f);
			const float tropical_spawn = CLAMP(params.biome_spawn_tropical, 0.0f, 1.0f);
			const float tundra_spawn = CLAMP(params.biome_spawn_tundra, 0.0f, 1.0f);
			const float grassland_spawn = CLAMP(params.biome_spawn_grassland, 0.0f, 1.0f);
			const float alpine_spawn = CLAMP(params.biome_spawn_alpine, 0.0f, 1.0f);
			const float desert_spawn_weight = _spawn_weight(desert_spawn);
			const float forest_spawn_weight = _spawn_weight(forest_spawn);
			const float tropical_spawn_weight = _spawn_weight(tropical_spawn);
			const float tundra_spawn_weight = _spawn_weight(tundra_spawn);
			const float grassland_spawn_weight = _spawn_weight(grassland_spawn);
			const float alpine_spawn_weight = _spawn_weight(alpine_spawn);
			const float grassland_balance = CLAMP(1.0f - Math::abs(humidity - 0.46f) * 1.55f - Math::abs(warm - 0.54f) * 0.42f - Math::abs(wet_zone - 0.50f) * 0.42f, 0.0f, 1.0f);
			const float temperate_band = CLAMP(1.0f - Math::abs(warm - 0.52f) * 1.55f, 0.0f, 1.0f);
			const float biome_diversity = CLAMP(climate_variance * (0.48f + 0.52f * inland) + Math::abs(grassland_balance - 0.5f) * 0.30f, 0.0f, 1.0f);
			float grassland_score = biome_blend * grassland_spawn_weight * (0.40f + 0.60f * biome_lowland) * (0.24f + 0.76f * grassland_balance) * (0.30f + 0.30f * temperate_band + 0.24f * (1.0f - inland_dryness) + 0.16f * habitable_upland) * (0.76f + 0.24f * (1.0f - biome_diversity));
			float desert_score = biome_blend * desert_spawn_weight * (0.22f + 0.78f * biome_lowland) * (0.24f + 0.76f * aridness) * (0.42f + warm * 0.90f) * (0.34f + inland_dryness * 0.36f + dry_zone * 0.28f + biome_upland * 0.10f + biome_diversity * 0.18f);
			float tropical_score = biome_blend * tropical_spawn_weight * (0.24f + 0.76f * biome_lowland) * lushness * MAX(warm - 0.02f, 0.0f) * (0.66f + 0.48f * equatorial_latitude) * (0.54f + 0.46f * wet_zone + biome_diversity * 0.16f) * (0.66f + 0.34f * inland_wetness) * (0.76f + 0.24f * habitable_upland);
			float forest_score = biome_blend * forest_spawn_weight * (0.26f + 0.74f * (0.50f * biome_lowland + 0.50f * habitable_upland)) * lushness * (0.30f + 0.38f * (1.0f - aridness) + 0.32f * MAX(wet_zone, biome_upland) + biome_diversity * 0.12f) * (0.50f + 0.40f * (1.0f - cold)) * (0.68f + 0.32f * inland_wetness) * (1.0f - equatorial_latitude * 0.20f);
			const float polar_fade = _ss(0.78f, 0.96f, lat01) * (0.22f + 0.78f * cold);
			const float altitude_cold = _ss(1850.0f, 3400.0f, terrain_alt) * (0.35f + 0.65f * cold);
			const float tundra_gate = CLAMP(MAX(polar_fade, altitude_cold * 0.65f), 0.0f, 1.0f);
			float tundra_score = biome_blend * tundra_spawn_weight * (0.16f + 0.20f * biome_lowland + 0.64f * habitable_upland) * _ss(0.58f, 0.92f, cold) * (0.22f + 0.18f * humidity) * tundra_gate;
			float alpine_score = biome_blend * alpine_spawn_weight * (0.10f + 0.90f * biome_highland) * (0.18f + 0.24f * cold + 0.36f * relief_bias + 0.22f * ruggedness);
			float desert_score_b = desert_score;
			float forest_score_b = forest_score;
			float tropical_score_b = tropical_score;
			float tundra_score_b = tundra_score;
			float grassland_score_b = grassland_score;
			float alpine_score_b = alpine_score;
			_apply_region_preference(uint8_t(region_pref_kind), region_pref_strength, desert_score, forest_score, tropical_score, tundra_score, grassland_score, alpine_score);
			if (region_pref_blend > 0.001f) {
				_apply_region_preference(uint8_t(region_pref_kind_b), region_pref_strength_b, desert_score_b, forest_score_b, tropical_score_b, tundra_score_b, grassland_score_b, alpine_score_b);
				const float blend_t = CLAMP(region_pref_blend * 0.85f, 0.0f, 1.0f);
				desert_score = Math::lerp(desert_score, desert_score_b, blend_t);
				forest_score = Math::lerp(forest_score, forest_score_b, blend_t);
				tropical_score = Math::lerp(tropical_score, tropical_score_b, blend_t);
				tundra_score = Math::lerp(tundra_score, tundra_score_b, blend_t);
				grassland_score = Math::lerp(grassland_score, grassland_score_b, blend_t);
				alpine_score = Math::lerp(alpine_score, alpine_score_b, blend_t);
			}
			const float biome_score_total = MAX(desert_score + forest_score + tropical_score + tundra_score + grassland_score + alpine_score, 0.001f);
			const float desert_w = desert_score / biome_score_total;
			const float forest_w = forest_score / biome_score_total;
			const float tropical_w = tropical_score / biome_score_total;
			const float tundra_w = tundra_score / biome_score_total;
			const float grass_w = grassland_score / biome_score_total;
			const float alpine_w = alpine_score / biome_score_total;
			const float biome_relief_mask = land_factor * (0.24f + 0.76f * (1.0f - shore_proximity));
			const float desert_relief = (smoothed_plateau * 0.60f + broad_detail_n * 0.24f) * (18.0f + params.detail_noise_amp * 0.32f) * biome_lowland;
			const float forest_relief = (smoothed_detail * 0.42f + base_n * 0.20f) * (14.0f + params.detail_noise_amp * 0.28f) * (0.45f + 0.55f * habitable_upland);
			const float tropical_relief = (smoothed_detail * 0.46f + broad_plateau_n * 0.10f - MAX(0.0f, 0.52f - basin_macro) * 0.85f) * (18.0f + params.detail_noise_amp * 0.34f) * (0.48f + 0.52f * inland_wetness);
			const float tundra_relief = (smoothed_detail * 0.18f + broad_plateau_n * 0.12f) * (10.0f + params.detail_noise_amp * 0.18f) * (0.50f + 0.50f * biome_highland);
			const float grass_relief = (base_n * 0.26f + smoothed_detail * 0.24f) * (12.0f + params.detail_noise_amp * 0.24f) * (0.50f + 0.50f * biome_lowland);
			const float alpine_relief = mountain_shape * params.mountain_noise_amp * 0.10f * (0.25f + 0.75f * biome_highland);
			height += biome_relief_mask * (desert_w * desert_relief + forest_w * forest_relief + tropical_w * tropical_relief + tundra_w * tundra_relief + grass_w * grass_relief + alpine_w * alpine_relief);
			const float beach_height_limit = MAX(params.beach_width_m * 0.04f, 42.0f);
			const bool deep_ocean = land_factor < 0.10f;
			const bool beach = land_factor > 0.18f && land_factor < 0.48f && height > -8.0f && height < beach_height_limit && shore_proximity > 0.60f;

			const float tundra_class_ratio = Math::lerp(2.1f, 0.88f, tundra_spawn);
			const float desert_class_ratio = Math::lerp(2.4f, 0.72f, desert_spawn);
			const float tropical_class_ratio = Math::lerp(2.0f, 0.78f, tropical_spawn);
			const float forest_class_ratio = Math::lerp(1.8f, 0.78f, forest_spawn);
			const bool classify_tundra = tundra_spawn > 0.001f && tundra_gate > 0.42f && (temp_c < 7.5f || polar_fade > 0.52f || (height > 2000.0f && cold > 0.30f)) && tundra_score > MAX(desert_score, MAX(forest_score, MAX(tropical_score, grassland_score))) * tundra_class_ratio;
			const bool classify_desert = desert_spawn > 0.001f && warm > 0.60f && (humidity < 0.38f || aridness > 0.56f) && desert_score > MAX(grassland_score, MAX(forest_score, tropical_score)) * desert_class_ratio;
			const bool classify_tropical = tropical_spawn > 0.001f && warm > 0.60f && !classify_desert && (humidity > 0.44f || wet_zone > 0.58f) && tropical_score > MAX(forest_score, grassland_score) * tropical_class_ratio;
			const bool classify_forest = forest_spawn > 0.001f && !classify_desert && !classify_tropical && (humidity > 0.46f || wet_zone > 0.60f || forest_score > grassland_score * 0.90f) && forest_score > MAX(grassland_score, desert_score) * forest_class_ratio;

			int region_kind = 5;
			int mat_id = MAT_GRASS;
			if (deep_ocean) {
				region_kind = 0;
				mat_id = MAT_OCEAN_FLOOR;
			} else if (beach) {
				region_kind = (desert_score > 0.18f) ? 1 : 5;
				mat_id = MAT_SAND;
			} else if ((alpine_score > 0.20f && temp_c < 5.0f) || (height > 2850.0f && temp_c < 2.5f)) {
				region_kind = 6;
				mat_id = (temp_c < -10.0f || height > 3200.0f) ? MAT_SNOW : MAT_ROCK;
			} else if ((tundra_score > MAX(desert_score, MAX(forest_score, MAX(tropical_score, grassland_score))) * 0.88f && (temp_c < 8.0f || polar_fade > 0.34f || (height > 1600.0f && cold > 0.24f))) || classify_tundra) {
				region_kind = 4;
				mat_id = (temp_c < -4.0f || (lat01 > 0.78f && height > 500.0f)) ? MAT_SNOW : MAT_ROCK;
			} else if ((desert_score > grassland_score * 0.72f && desert_score > 0.04f) || classify_desert) {
				region_kind = 1;
				mat_id = (aridness > 0.64f || humidity < 0.20f) ? MAT_SAND : MAT_DIRT;
			} else if ((tropical_score > MAX(forest_score, grassland_score) * 0.78f && tropical_score > 0.04f) || classify_tropical) {
				region_kind = 3;
				mat_id = MAT_MOSS;
			} else if ((forest_score > MAX(grassland_score, desert_score) * 0.78f && forest_score > 0.04f) || classify_forest) {
				region_kind = 2;
				mat_id = MAT_DIRT;
			} else {
				region_kind = 5;
				mat_id = MAT_GRASS;
			}
			if (mountains > params.mountain_noise_amp * 0.31f && mat_id != MAT_SNOW && !beach) {
				mat_id = MAT_ROCK;
			}

			const int off = (py * p_width + px) * 4;
			pw[off] = uint8_t(CLAMP(int((float(mat_id) / 8.0f) * 255.0f), 0, 255));
			pw[off + 1] = uint8_t(CLAMP(int(((temp_c + 40.0f) / 80.0f) * 255.0f), 0, 255));
			pw[off + 2] = uint8_t(CLAMP(int(humidity * 255.0f), 0, 255));
			pw[off + 3] = uint8_t(CLAMP(int((float(region_kind) / max_region_kind) * 255.0f), 0, 255));
		}
	}

	Ref<Image> img;
	img.instantiate();
	img->set_data(p_width, p_height, false, Image::FORMAT_RGBA8, pixels);
	return img;
}

Ref<Image> EdenPlanetGeneratorV3::get_biome_debug_image(int p_width, int p_height) {
	return generate_biome_debug_image(p_width, p_height);
}

Ref<Image> EdenPlanetGeneratorV3::get_plate_debug_image(int p_width, int p_height) {
	if (p_width <= 0 || p_height <= 0) {
		return Ref<Image>();
	}

	Parameters params;
	Ref<PlanetTectonics> tectonics;
	{
		zylann::RWLockRead rlock(_parameters_lock);
		params = _parameters;
		tectonics = _tectonics;
	}
	if (!params.is_setup) {
		setup();
		zylann::RWLockRead rlock(_parameters_lock);
		params = _parameters;
		tectonics = _tectonics;
	}
	if (tectonics.is_null()) {
		return Ref<Image>();
	}

	Ref<Image> img = Image::create_empty(p_width, p_height, false, Image::FORMAT_RGBA8);

	const VoronoiSphere &vor = tectonics->get_voronoi();
	const Vector<int> &plate_ids = tectonics->get_plate_ids();
	const Vector<float> &falloffs = tectonics->get_falloff_values();
	const Vector<uint8_t> &boundary_types = tectonics->get_boundary_types();
	const Vector<uint8_t> &oceanic_flags = tectonics->get_point_oceanic_flags();
	const float inv_plate_count = (params.n_plates > 0) ? (1.0f / float(params.n_plates)) : 0.0f;

	for (int py = 0; py < p_height; ++py) {
		const float v = (float(py) + 0.5f) / float(p_height);
		const float lat = (0.5f - v) * float(Math::PI);
		const float cos_lat = Math::cos(lat);
		const float sin_lat = Math::sin(lat);
		for (int px = 0; px < p_width; ++px) {
			const float u = (float(px) + 0.5f) / float(p_width);
			const float lon = (u - 0.5f) * float(Math::TAU);
			const Vector3 dir(cos_lat * Math::cos(lon), sin_lat, cos_lat * Math::sin(lon));
			const Vector3 wdir = tectonics->warp_direction(dir);
			const int best_vi = vor.get_nearest(wdir);
			const int plate_id = (best_vi >= 0 && best_vi < plate_ids.size()) ? plate_ids[best_vi] : 0;
			const float falloff = (best_vi >= 0 && best_vi < falloffs.size()) ? falloffs[best_vi] : 0.0f;
			const float oceanic = (best_vi >= 0 && best_vi < oceanic_flags.size()) ? float(oceanic_flags[best_vi]) : 0.0f;
			const float bnd = (best_vi >= 0 && best_vi < boundary_types.size()) ? float(boundary_types[best_vi]) / 8.0f : 0.0f;
			img->set_pixel(px, py, Color(float(plate_id) * inv_plate_count, oceanic, bnd, falloff));
		}
	}

	return img;
}

Ref<Image> EdenPlanetGeneratorV3::get_plate_data_image(int p_width, int p_height) {
	if (p_width <= 0 || p_height <= 0) {
		return Ref<Image>();
	}

	Parameters params;
	Ref<PlanetTectonics> tectonics;
	{
		zylann::RWLockRead rlock(_parameters_lock);
		params = _parameters;
		tectonics = _tectonics;
	}
	if (!params.is_setup) {
		setup();
		zylann::RWLockRead rlock(_parameters_lock);
		params = _parameters;
		tectonics = _tectonics;
	}
	if (tectonics.is_null()) {
		return Ref<Image>();
	}

	const Vector<float> &elevations = tectonics->get_elevations();
	float elev_min = 1e20f;
	float elev_max = -1e20f;
	for (int i = 0; i < elevations.size(); ++i) {
		elev_min = MIN(elev_min, elevations[i]);
		elev_max = MAX(elev_max, elevations[i]);
	}
	const float elev_range = MAX(elev_max - elev_min, 1.0f);

	Ref<Image> img = Image::create_empty(p_width, p_height, false, Image::FORMAT_RGBA8);

	const VoronoiSphere &vor = tectonics->get_voronoi();
	const Vector<int> &plate_ids = tectonics->get_plate_ids();
	const Vector<Vector3> &movements = tectonics->get_plate_movements();

	for (int py = 0; py < p_height; ++py) {
		const float v = (float(py) + 0.5f) / float(p_height);
		const float lat = (0.5f - v) * float(Math::PI);
		const float cos_lat = Math::cos(lat);
		const float sin_lat = Math::sin(lat);
		for (int px = 0; px < p_width; ++px) {
			const float u = (float(px) + 0.5f) / float(p_width);
			const float lon = (u - 0.5f) * float(Math::TAU);
			const Vector3 dir(cos_lat * Math::cos(lon), sin_lat, cos_lat * Math::sin(lon));
			const Vector3 wdir = tectonics->warp_direction(dir);
			const int best_vi = vor.get_nearest(wdir);
			const int plate_id = (best_vi >= 0 && best_vi < plate_ids.size()) ? plate_ids[best_vi] : 0;
			const Vector3 movement = (plate_id >= 0 && plate_id < movements.size()) ? movements[plate_id] : Vector3();
			const Vector3 surf = (best_vi >= 0) ? vor.get_point(best_vi) : dir;

			const Vector3 up(0.0f, 1.0f, 0.0f);
			Vector3 east = (Math::abs(surf.y) < 0.99f) ? up.cross(surf).normalized() : Vector3(1.0f, 0.0f, 0.0f);
			const Vector3 north = surf.cross(east).normalized();
			const float mv_e = CLAMP(movement.dot(east), -1.0f, 1.0f);
			const float mv_n = CLAMP(movement.dot(north), -1.0f, 1.0f);
			const int e4 = CLAMP(int((mv_e + 1.0f) * 7.5f), 0, 15);
			const int n4 = CLAMP(int((mv_n + 1.0f) * 7.5f), 0, 15);
			const float packed_mv = float((e4 << 4) | n4) / 255.0f;

			const float elev_norm = (best_vi >= 0 && best_vi < elevations.size()) ? ((elevations[best_vi] - elev_min) / elev_range) : 0.5f;
			const float lat_norm = Math::abs(dir.y);
			const PlanetTectonics::TerrainData td = tectonics->get_terrain_data(dir);
			const float border_dist_norm = CLAMP((td.border_dist_rad * params.planet_radius) / 18000.0f, 0.0f, 1.0f);
			img->set_pixel(px, py, Color(elev_norm, lat_norm, packed_mv, border_dist_norm));
		}
	}

	return img;
}

Ref<Image> EdenPlanetGeneratorV3::get_macro_debug_image(int p_width, int p_height) {
	if (p_width <= 0 || p_height <= 0) {
		return Ref<Image>();
	}

	Parameters params;
	Ref<FastNoiseLite> noise_base;
	Ref<FastNoiseLite> noise_continent;
	Ref<FastNoiseLite> noise_plateau;
	Ref<FastNoiseLite> noise_oceanic;
	Ref<FastNoiseLite> noise_mountain;
	Ref<FastNoiseLite> noise_detail;
	Ref<PlanetTectonics> tectonics;
	{
		zylann::RWLockRead rlock(_parameters_lock);
		params = _parameters;
		noise_base = _noise_base;
		noise_continent = _noise_continent;
		noise_plateau = _noise_plateau;
		noise_oceanic = _noise_oceanic;
		noise_mountain = _noise_mountain;
		noise_detail = _noise_detail;
		tectonics = _tectonics;
	}
	if (!params.is_setup) {
		setup();
		zylann::RWLockRead rlock(_parameters_lock);
		params = _parameters;
		noise_base = _noise_base;
		noise_continent = _noise_continent;
		noise_plateau = _noise_plateau;
		noise_oceanic = _noise_oceanic;
		noise_mountain = _noise_mountain;
		noise_detail = _noise_detail;
		tectonics = _tectonics;
	}
	if (!params.is_setup) {
		return Ref<Image>();
	}

	Ref<Image> img = Image::create_empty(p_width, p_height, false, Image::FORMAT_RGBAH);

	for (int py = 0; py < p_height; ++py) {
		const float v = (float(py) + 0.5f) / float(p_height);
		const float lat = (0.5f - v) * float(Math::PI);
		const float cos_lat = Math::cos(lat);
		const float sin_lat = Math::sin(lat);
		for (int px = 0; px < p_width; ++px) {
			const float u = (float(px) + 0.5f) / float(p_width);
			const float lon = (u - 0.5f) * float(Math::TAU);
			const float ux = cos_lat * Math::cos(lon);
			const float uy = sin_lat;
			const float uz = cos_lat * Math::sin(lon);
			const float sx = ux * params.planet_radius;
			const float sy = uy * params.planet_radius;
			const float sz = uz * params.planet_radius;
			const float continent_scale = MAX(params.continent_size_scale, 0.35f);
			const float mx = sx / continent_scale;
			const float my = sy / continent_scale;
			const float mz = sz / continent_scale;

			const float macro_primary = 0.5f + 0.5f * noise_continent->get_noise_3d(mx, my, mz);
			const float macro_secondary = 0.5f + 0.5f * noise_continent->get_noise_3d(mx * 0.43f + 173.4f, my * 0.43f - 91.7f, mz * 0.43f + 47.2f);
			const float macro_support = 0.5f + 0.5f * noise_plateau->get_noise_3d(mx * 0.18f - 63.1f, my * 0.18f + 24.7f, mz * 0.18f + 91.3f);
			const float mountain_area_noise = 0.5f + 0.5f * noise_continent->get_noise_3d(mx * 0.45f + 13.0f, my * 0.45f - 29.0f, mz * 0.45f + 47.0f);
			const float humidity_macro = 0.5f + 0.5f * noise_detail->get_noise_3d(sx + 91.0f, sy - 44.0f, sz + 17.0f);
			const float climate_macro = 0.5f + 0.5f * noise_continent->get_noise_3d(mx * 0.26f - 131.0f, my * 0.26f + 67.0f, mz * 0.26f - 29.0f);

			float macro_shape = macro_primary * 0.78f + macro_secondary * 0.16f + macro_support * 0.06f;
			macro_shape = Math::lerp(macro_shape, _ss(0.18f, 0.82f, macro_shape), CLAMP(params.continent_cohesion, 0.0f, 1.0f) * 0.65f);
			const float land_ratio_target = CLAMP(
					params.land_coverage_target +
							(0.5f - CLAMP(params.oceanic_fraction, 0.0f, 1.0f)) * 0.32f * MAX(params.oceanic_fraction_influence, 0.0f) +
							params.continent_balance * 0.16f,
					0.08f,
					0.84f);
			const float land_threshold_center = Math::lerp(0.82f, 0.46f, land_ratio_target);
			const float land_threshold_width = Math::lerp(0.22f, 0.10f, CLAMP(params.continent_cohesion, 0.0f, 1.0f));
			float tectonic_bias = (macro_secondary - 0.5f) * 0.04f + (climate_macro - 0.5f) * 0.03f - CLAMP(params.oceanic_fraction - 0.5f, -0.5f, 0.5f) * 0.03f;
			float stress_hint = 0.0f;
			float coast_hint = 0.0f;
			if (tectonics.is_valid()) {
				const PlanetTectonics::TerrainData td = tectonics->get_terrain_data(Vector3(ux, uy, uz));
				tectonic_bias += CLAMP(td.bias_s / MAX(params.max_terrain_height, 1.0f), -0.35f, 0.35f) * 0.22f;
				const float border_dist_m = td.border_dist_rad * params.planet_radius;
				const float edge = 1.0f - _ss(0.0f, MAX(1800.0f, params.coast_smoothing_width * 0.80f), border_dist_m);
				switch (td.bnd_type_a) {
					case PlanetTectonics::BND_MOUNTAIN:
					case PlanetTectonics::BND_ISLAND_ARC:
						stress_hint = edge;
						break;
					case PlanetTectonics::BND_TRENCH:
					case PlanetTectonics::BND_RIFT:
						stress_hint = -edge * 0.75f;
						break;
					case PlanetTectonics::BND_PASSIVE:
						stress_hint = edge * 0.18f;
						break;
					default:
						break;
				}
				coast_hint = edge;
			}

			const float land_signal = CLAMP(macro_shape + tectonic_bias, 0.0f, 1.0f);
			const float land_mask = _ss(land_threshold_center - land_threshold_width, land_threshold_center + land_threshold_width, land_signal);
			const float core_mask = _ss(land_threshold_center + land_threshold_width * 0.12f, land_threshold_center + land_threshold_width * 1.45f, land_signal);
			const float inland = CLAMP(MAX(core_mask * 0.96f, land_mask), 0.0f, 1.0f);
			const float coast_band = 1.0f - _ss(0.0f, land_threshold_width * 1.35f, Math::abs(land_signal - land_threshold_center));
			const float coast_proximity = MAX(1.0f - inland, MAX(coast_band * 0.88f, coast_hint * 0.35f));
			const float land_factor = CLAMP(Math::lerp(land_mask, core_mask, 0.18f) + core_mask * 0.04f, 0.0f, 1.0f);
			const float continental_shelf = land_factor * (1.0f - inland * 0.70f);
			const float continental_core = land_factor * MAX(inland, core_mask * 0.96f);
			const float relief_bias = CLAMP(continental_core * 0.82f + continental_shelf * 0.34f + mountain_area_noise * 0.18f + MAX(stress_hint, 0.0f) * 0.26f, 0.0f, 1.0f);

			const float oceanic_n = noise_oceanic->get_noise_3d(sx, sy, sz);
			const float base_n = noise_base->get_noise_3d(sx, sy, sz);
			float height = (land_signal - land_threshold_center) * 210.0f;
			height += Math::lerp(-params.ocean_depth * 0.82f, params.continent_height * 0.68f, land_factor);
			height += continental_shelf * (92.0f + 112.0f * coast_proximity + humidity_macro * 18.0f);
			height += continental_core * (210.0f + 250.0f * MAX(inland, core_mask) + relief_bias * 120.0f);
			height -= (1.0f - land_factor) * (72.0f + 120.0f * (1.0f - coast_proximity));
			height += base_n * params.base_noise_amp * Math::lerp(0.22f, 0.72f, land_factor * 0.90f + 0.10f);
			height += oceanic_n * params.oceanic_noise_amp * (1.0f - land_factor) * 0.74f;
			height += MAX(stress_hint, 0.0f) * (120.0f + params.mountain_noise_amp * 0.32f);
			height -= MAX(-stress_hint, 0.0f) * (70.0f + params.ocean_depth * 0.10f);

			float humidity = Math::lerp(params.climate_interior_humidity, params.climate_ocean_humidity, CLAMP(coast_proximity * 0.75f + (1.0f - Math::abs(uy)) * 0.25f, 0.0f, 1.0f));
			humidity += (humidity_macro - 0.5f) * 0.24f - relief_bias * 0.06f;
			humidity = CLAMP(humidity, 0.0f, 1.0f);

			const float coast_distance_m = CLAMP((1.0f - coast_proximity) * 18000.0f, 0.0f, 50000.0f);
			const float climate_baseline = CLAMP((climate_macro - 0.5f) * 2.0f + (0.5f - Math::abs(uy)) * 0.20f - MAX(height, 0.0f) / MAX(params.max_terrain_height, 1.0f) * 0.14f, -1.0f, 1.0f);
			const float river_flow = CLAMP((humidity - 0.42f) * 1.6f * (1.0f - _ss(180.0f, 1400.0f, MAX(height, 0.0f))) * (0.28f + coast_proximity * 0.72f) + MAX(stress_hint, 0.0f) * 0.16f, 0.0f, 1.0f);
			img->set_pixel(px, py, Color(CLAMP(stress_hint, -1.0f, 1.0f), coast_distance_m, climate_baseline, river_flow));
		}
	}

	return img;
}

Ref<Image> EdenPlanetGeneratorV3::get_climate_debug_image(int p_width, int p_height) {
	if (p_width <= 0 || p_height <= 0) {
		return Ref<Image>();
	}

	Parameters params;
	Ref<FastNoiseLite> noise_continent;
	Ref<FastNoiseLite> noise_plateau;
	Ref<FastNoiseLite> noise_detail;
	Ref<FastNoiseLite> noise_mountain;
	Ref<PlanetTectonics> tectonics;
	{
		zylann::RWLockRead rlock(_parameters_lock);
		params = _parameters;
		noise_continent = _noise_continent;
		noise_plateau = _noise_plateau;
		noise_detail = _noise_detail;
		noise_mountain = _noise_mountain;
		tectonics = _tectonics;
	}
	if (!params.is_setup) {
		setup();
		zylann::RWLockRead rlock(_parameters_lock);
		params = _parameters;
		noise_continent = _noise_continent;
		noise_plateau = _noise_plateau;
		noise_detail = _noise_detail;
		noise_mountain = _noise_mountain;
		tectonics = _tectonics;
	}
	if (!params.is_setup) {
		return Ref<Image>();
	}

	Ref<Image> img = Image::create_empty(p_width, p_height, false, Image::FORMAT_RGBAH);

	for (int py = 0; py < p_height; ++py) {
		const float v = (float(py) + 0.5f) / float(p_height);
		const float lat = (0.5f - v) * float(Math::PI);
		const float cos_lat = Math::cos(lat);
		const float sin_lat = Math::sin(lat);
		for (int px = 0; px < p_width; ++px) {
			const float u = (float(px) + 0.5f) / float(p_width);
			const float lon = (u - 0.5f) * float(Math::TAU);
			const float ux = cos_lat * Math::cos(lon);
			const float uy = sin_lat;
			const float uz = cos_lat * Math::sin(lon);
			const float sx = ux * params.planet_radius;
			const float sy = uy * params.planet_radius;
			const float sz = uz * params.planet_radius;
			const float continent_scale = MAX(params.continent_size_scale, 0.35f);
			const float mx = sx / continent_scale;
			const float my = sy / continent_scale;
			const float mz = sz / continent_scale;

			const float climate_macro = 0.5f + 0.5f * noise_continent->get_noise_3d(mx * 0.26f - 131.0f, my * 0.26f + 67.0f, mz * 0.26f - 29.0f);
			const float humidity_macro = 0.5f + 0.5f * noise_detail->get_noise_3d(sx + 91.0f, sy - 44.0f, sz + 17.0f);
			const float plateau_macro = 0.5f + 0.5f * noise_plateau->get_noise_3d(mx * 0.18f - 63.1f, my * 0.18f + 24.7f, mz * 0.18f + 91.3f);
			const float mountain_shape = _fast_pow01(CLAMP(0.5f + 0.5f * noise_mountain->get_noise_3d(sx, sy, sz), 0.0f, 1.0f), MAX(0.65f, params.mountain_shape_boost * 1.45f));

			float stress_hint = 0.0f;
			float uplift_bias = 0.0f;
			if (tectonics.is_valid()) {
				const PlanetTectonics::TerrainData td = tectonics->get_terrain_data(Vector3(ux, uy, uz));
				const float border_dist_m = td.border_dist_rad * params.planet_radius;
				const float edge = 1.0f - _ss(0.0f, MAX(1800.0f, params.coast_smoothing_width * 0.80f), border_dist_m);
				switch (td.bnd_type_a) {
					case PlanetTectonics::BND_MOUNTAIN:
					case PlanetTectonics::BND_ISLAND_ARC:
						stress_hint = edge;
						uplift_bias = edge * (0.55f + 0.45f * td.falloff_s);
						break;
					case PlanetTectonics::BND_TRENCH:
						stress_hint = -edge * 0.85f;
						uplift_bias = -edge * 0.35f;
						break;
					case PlanetTectonics::BND_RIFT:
						stress_hint = -edge * 0.65f;
						uplift_bias = -edge * 0.20f;
						break;
					case PlanetTectonics::BND_PASSIVE:
						stress_hint = edge * 0.12f;
						break;
					default:
						break;
				}
			}

			uplift_bias = CLAMP(uplift_bias + mountain_shape * 0.28f + (plateau_macro - 0.5f) * 0.16f, -1.0f, 1.0f);
			const float lat01 = Math::abs(uy);
			const float temp_c = Math::lerp(params.climate_equator_temp_c, params.climate_polar_temp_c, lat01 * lat01) + (climate_macro - 0.5f) * 6.0f - MAX(uplift_bias, 0.0f) * 8.0f;
			const float temp_mid = (params.climate_equator_temp_c + params.climate_polar_temp_c) * 0.5f;
			const float temp_half_range = MAX((params.climate_equator_temp_c - params.climate_polar_temp_c) * 0.5f, 1.0f);
			const float temp_baseline = CLAMP((temp_c - temp_mid) / temp_half_range, -1.0f, 1.0f);
			float humidity = Math::lerp(params.climate_interior_humidity, params.climate_ocean_humidity, CLAMP((1.0f - lat01) * 0.45f + 0.35f, 0.0f, 1.0f));
			humidity += (humidity_macro - 0.5f) * 0.24f - MAX(uplift_bias, 0.0f) * 0.10f - MAX(stress_hint, 0.0f) * 0.04f;
			humidity = CLAMP(humidity, 0.0f, 1.0f);
			const float humidity_baseline = CLAMP(humidity * 2.0f - 1.0f, -1.0f, 1.0f);
			const float rain_shadow = CLAMP(MAX(uplift_bias, 0.0f) * (1.0f - humidity) * (0.55f + lat01 * 0.20f), 0.0f, 1.0f);
			img->set_pixel(px, py, Color(uplift_bias, temp_baseline, humidity_baseline, rain_shadow));
		}
	}

	return img;
}

Ref<Image> EdenPlanetGeneratorV3::get_topology_cell_debug_image(int p_width, int p_height) {
	if (p_width <= 0 || p_height <= 0) {
		return Ref<Image>();
	}

	Parameters params;
	Ref<PlanetTectonics> tectonics;
	{
		zylann::RWLockRead rlock(_parameters_lock);
		params = _parameters;
		tectonics = _tectonics;
	}
	if (!params.is_setup) {
		setup();
		zylann::RWLockRead rlock(_parameters_lock);
		params = _parameters;
		tectonics = _tectonics;
	}
	if (tectonics.is_null()) {
		return Ref<Image>();
	}

	Ref<Image> img = Image::create_empty(p_width, p_height, false, Image::FORMAT_RGBA8);

	const VoronoiSphere &vor = tectonics->get_voronoi();
	for (int py = 0; py < p_height; ++py) {
		const float v = (float(py) + 0.5f) / float(p_height);
		const float lat = (0.5f - v) * float(Math::PI);
		const float cos_lat = Math::cos(lat);
		const float sin_lat = Math::sin(lat);
		for (int px = 0; px < p_width; ++px) {
			const float u = (float(px) + 0.5f) / float(p_width);
			const float lon = (u - 0.5f) * float(Math::TAU);
			const Vector3 dir(cos_lat * Math::cos(lon), sin_lat, cos_lat * Math::sin(lon));
			const Vector3 wdir = tectonics->warp_direction(dir);
			int best_idx = 0;
			int second_idx = 0;
			float best_dot = -2.0f;
			float second_dot = -2.0f;
			vor.get_two_nearest(wdir, best_idx, second_idx, best_dot, second_dot);

			const float ang_a = Math::acos(CLAMP(best_dot, -1.0f, 1.0f));
			const float ang_b = Math::acos(CLAMP(MAX(second_dot, best_dot), -1.0f, 1.0f));
			float boundary = 1.0f - CLAMP((ang_b - ang_a) / 0.055f, 0.0f, 1.0f);
			boundary = Math::pow(boundary, 0.72f);
			const float hue = Math::fposmod(float(best_idx) * 0.61803398875f, 1.0f);
			const float sat = 0.55f + 0.20f * CLAMP(wdir.y * 0.5f + 0.5f, 0.0f, 1.0f);
			const float val = 0.70f - boundary * 0.25f;
			Color cell_col = Color::from_hsv(hue, sat, val, 1.0f);
			const float distort = CLAMP(1.0f - dir.dot(wdir), 0.0f, 1.0f);
			cell_col = cell_col.lerp(Color(0.05f, 0.04f, 0.04f, 1.0f), boundary * 0.78f);
			cell_col = cell_col.lerp(Color(0.88f, 0.94f, 1.0f, 1.0f), distort * 0.20f);
			img->set_pixel(px, py, cell_col);
		}
	}

	return img;
}

zylann::voxel::VoxelGenerator::Result EdenPlanetGeneratorV3::generate_block(VoxelQueryData input) {
	Result result;

	Parameters params;
	Ref<FastNoiseLite> noise_base;
	Ref<FastNoiseLite> noise_continent;
	Ref<FastNoiseLite> noise_plateau;
	Ref<FastNoiseLite> noise_oceanic;
	Ref<FastNoiseLite> noise_mountain;
	Ref<FastNoiseLite> noise_detail;
	Ref<PlanetTectonics> tectonics;
	{
		zylann::RWLockRead rlock(_parameters_lock);
		params = _parameters;
		noise_base = _noise_base;
		noise_continent = _noise_continent;
		noise_plateau = _noise_plateau;
		noise_oceanic = _noise_oceanic;
		noise_mountain = _noise_mountain;
		noise_detail = _noise_detail;
		tectonics = _tectonics;
	}

	if (!params.is_setup) {
		setup();
		zylann::RWLockRead rlock(_parameters_lock);
		params = _parameters;
		noise_base = _noise_base;
		noise_continent = _noise_continent;
		noise_plateau = _noise_plateau;
		noise_oceanic = _noise_oceanic;
		noise_mountain = _noise_mountain;
		noise_detail = _noise_detail;
		tectonics = _tectonics;
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

	const float block_world_size = float(size.x * step);
	const float cx = float(origin.x) + block_world_size * 0.5f;
	const float cy = float(origin.y) + block_world_size * 0.5f;
	const float cz = float(origin.z) + block_world_size * 0.5f;
	const float dist_c = Math::sqrt(cx * cx + cy * cy + cz * cz);
	const float diag = block_world_size * 1.7320508f;

	zylann::voxel::TerrainErosionParams erosion_params = zylann::voxel::make_default_terrain_erosion_params();
	erosion_params.seed = params.seed + 7919;
	erosion_params.planet_radius = params.planet_radius;
	erosion_params.tile_size = MAX(params.erosion_tile_size, 1.0f);
	erosion_params.strength = params.erosion_strength;
	erosion_params.detail = MAX(params.erosion_detail, 0.01f);
	erosion_params.octaves = CLAMP(params.erosion_octaves, 0, 12);
	const float erosion_max_height = params.use_erosion
			? zylann::voxel::get_terrain_erosion_max_height(erosion_params) * Math::abs(params.erosion_height_scale)
			: 0.0f;

	const float shell_inner = params.planet_radius - (params.max_terrain_height + 1600.0f + erosion_max_height);
	const float shell_outer = params.planet_radius + (params.max_terrain_height + 1600.0f + erosion_max_height);

	int rock_indices = 0;
	int rock_weights = 0;
	_pack_mixel4(MAT_ROCK, MAT_OCEAN_FLOOR, 0.0f, 0.42f, 0.55f, 0.56f, 0.72f, rock_indices, rock_weights);

	if (dist_c + diag * 0.5f < shell_inner) {
		buffer.clear_channel_f(VoxelBuffer::CHANNEL_SDF, -100.0f);
		buffer.clear_channel(VoxelBuffer::CHANNEL_INDICES, rock_indices);
		buffer.clear_channel(VoxelBuffer::CHANNEL_WEIGHTS, rock_weights);
		result.max_lod_hint = true;
		return result;
	}
	if (dist_c - diag * 0.5f > shell_outer) {
		buffer.clear_channel_f(VoxelBuffer::CHANNEL_SDF, 100.0f);
		result.max_lod_hint = true;
		return result;
	}

	const float budget = params.max_terrain_height + 320.0f + erosion_max_height;

	// --- Optional erosion pre-pass (use_erosion) -------------------------------------------------
	// One batched call per block (ISPC when VOXEL_ISPC_ENABLED). Relief depends on direction only,
	// sampled in the same z/y/x order as the main loop below (indexed by ispc_flat_idx).
	// ponytail: evaluates every voxel of near-surface blocks; skip voxels far from the surface if
	// profiling shows this pass dominating.
	Vector<float> erosion_out;
	if (params.use_erosion) {
		const int count = size.x * size.y * size.z;
		Vector<float> ex, ey, ez;
		ex.resize(count);
		ey.resize(count);
		ez.resize(count);
		erosion_out.resize(count);
		float *pex = ex.ptrw();
		float *pey = ey.ptrw();
		float *pez = ez.ptrw();
		int idx = 0;
		for (int z = 0; z < size.z; ++z) {
			for (int y = 0; y < size.y; ++y) {
				for (int x = 0; x < size.x; ++x) {
					pex[idx] = float(origin.x) + x * step + half_step;
					pey[idx] = float(origin.y) + y * step + half_step;
					pez[idx] = float(origin.z) + z * step + half_step;
					++idx;
				}
			}
		}
		zylann::voxel::planet_erosion_series(pex, pey, pez, erosion_out.ptrw(), nullptr, nullptr, count, erosion_params);
	}

	// --- Optional ISPC-batched noise pre-pass (use_ispc_diffusion) -----------------------------
	// Replaces the per-voxel FastNoiseLite::get_noise_3d() calls that feed the tectonic blending
	// below with a single batched call per noise field, through modules/voxel's ISPC terrain-
	// diffusion kernels (see util/noise/voxel_terrain_noise.h). The tectonic-plate/continent-blend
	// math itself (macro_primary/secondary/support, mountain_area_noise, and everything below that
	// consumes them) stays exactly as it is today -- it's plate-topology-dependent, not something
	// the generic ISPC kernels model. Only the raw fBm noise samples are replaced; the blending
	// formulas that consume them are untouched, so behavior differs only by noise implementation
	// (FastNoiseLite Simplex vs. this kernel's Perlin-derivative fBm), not by architecture.
	//
	// terrain_height_3d_series() is a full planetary-terrain-height kernel (amplitude, shaping
	// stages, etc.), not a raw-noise sampler. It's reused here as one by setting amplitude=1 and
	// leaving all shaping stages at their default no-op strengths (make_default_terrain_height_params()
	// already zeroes every blend/strength), so its output reduces to planet_radius + raw_fbm(*1),
	// i.e. subtracting planet_radius back out afterwards recovers a raw fBm sample in roughly
	// [-1, 1], the same value space FastNoiseLite::get_noise_3d() returns.
	//
	// Judgment call: the kernel always re-derives its sample direction from the input position and
	// re-projects it onto the planet_radius sphere (see FCTerrainHeight3D), which is exactly
	// correct for the 5 fields sampled at (sx, sy, sz) below (already exactly on that sphere by
	// construction). humidity_macro and climate_macro, however, sample FastNoiseLite at positions
	// slightly off that sphere (a constant small world-space offset, and a macro-scaled position,
	// respectively) -- feeding those through the kernel re-projects them back onto the sphere,
	// which is a small, deliberately-accepted distortion (the offsets are ~1-2 orders of magnitude
	// smaller than the sampled feature scale) rather than a second, unprojected batch API this
	// codebase doesn't expose.
	Vector<float> ispc_out_base, ispc_out_plateau, ispc_out_oceanic, ispc_out_detail, ispc_out_mountain;
	Vector<float> ispc_out_humidity_macro, ispc_out_climate_macro;
	const int ispc_voxel_count = params.use_ispc_diffusion ? size.x * size.y * size.z : 0;

	if (params.use_ispc_diffusion) {
		Vector<float> pos_sx, pos_sy, pos_sz; // on-sphere positions, shared by 5 of the 7 fields
		Vector<float> pos_hx, pos_hy, pos_hz; // humidity_macro's offset sample positions
		Vector<float> pos_cx, pos_cy, pos_cz; // climate_macro's macro-space sample positions
		pos_sx.resize(ispc_voxel_count);
		pos_sy.resize(ispc_voxel_count);
		pos_sz.resize(ispc_voxel_count);
		pos_hx.resize(ispc_voxel_count);
		pos_hy.resize(ispc_voxel_count);
		pos_hz.resize(ispc_voxel_count);
		pos_cx.resize(ispc_voxel_count);
		pos_cy.resize(ispc_voxel_count);
		pos_cz.resize(ispc_voxel_count);

		float *psx = pos_sx.ptrw();
		float *psy = pos_sy.ptrw();
		float *psz = pos_sz.ptrw();
		float *phx = pos_hx.ptrw();
		float *phy = pos_hy.ptrw();
		float *phz = pos_hz.ptrw();
		float *pcx = pos_cx.ptrw();
		float *pcy = pos_cy.ptrw();
		float *pcz = pos_cz.ptrw();

		size_t idx = 0;
		for (int z = 0; z < size.z; ++z) {
			const float wz = float(origin.z) + z * step + half_step;
			for (int y = 0; y < size.y; ++y) {
				const float wy = float(origin.y) + y * step + half_step;
				for (int x = 0; x < size.x; ++x) {
					const float wx = float(origin.x) + x * step + half_step;
					const float r2 = wx * wx + wy * wy + wz * wz;
					const float inv_r = (r2 > 1.0f) ? _fast_rsqrt(r2) : 1.0f;
					const float ux = wx * inv_r;
					const float uy = wy * inv_r;
					const float uz = wz * inv_r;
					const float sx = ux * params.planet_radius;
					const float sy = uy * params.planet_radius;
					const float sz = uz * params.planet_radius;
					const float continent_scale = MAX(params.continent_size_scale, 0.35f);
					const float mx = sx / continent_scale;
					const float my = sy / continent_scale;
					const float mz = sz / continent_scale;

					psx[idx] = sx;
					psy[idx] = sy;
					psz[idx] = sz;
					phx[idx] = sx + 91.0f;
					phy[idx] = sy - 44.0f;
					phz[idx] = sz + 17.0f;
					pcx[idx] = mx * 0.26f - 131.0f;
					pcy[idx] = my * 0.26f + 67.0f;
					pcz[idx] = mz * 0.26f - 29.0f;
					++idx;
				}
			}
		}

		auto make_hp = [&](int seed_offset, float freq, int octaves, float gain, float lacunarity, float aesthetic_bias) {
			zylann::voxel::TerrainHeightParams hp = zylann::voxel::make_default_terrain_height_params();
			hp.amplitude = 1.0f;
			hp.feature_scale = 1.0f / MAX(freq, 1e-9f);
			hp.lacunarity = lacunarity;
			hp.gain = gain;
			hp.aesthetic_bias = aesthetic_bias;
			hp.num_octaves = octaves;
			hp.seed = params.seed + seed_offset;
			hp.planet_radius = params.planet_radius;
			return hp;
		};

		ispc_out_base.resize(ispc_voxel_count);
		ispc_out_plateau.resize(ispc_voxel_count);
		ispc_out_oceanic.resize(ispc_voxel_count);
		ispc_out_detail.resize(ispc_voxel_count);
		ispc_out_mountain.resize(ispc_voxel_count);
		ispc_out_humidity_macro.resize(ispc_voxel_count);
		ispc_out_climate_macro.resize(ispc_voxel_count);

		// Seeds/octaves/gain/lacunarity below mirror setup()'s _make_noise() calls exactly.
		// aesthetic_bias 0 = realistic fBm (matches FRACTAL_FBM), 1 = ridged multifractal
		// (matches noise_mountain's FRACTAL_RIDGED).
		const unsigned int ispc_count_u = (unsigned int)ispc_voxel_count;
		const zylann::voxel::TerrainHeightParams hp_base = make_hp(11, params.base_noise_freq, 4, 0.5f, 2.0f, 0.0f);
		zylann::voxel::terrain_height_3d_series(psx, psy, psz, ispc_out_base.ptrw(), ispc_count_u, hp_base);

		const zylann::voxel::TerrainHeightParams hp_plateau = make_hp(41, params.plateau_noise_freq, 4, 0.5f, 2.0f, 0.0f);
		zylann::voxel::terrain_height_3d_series(psx, psy, psz, ispc_out_plateau.ptrw(), ispc_count_u, hp_plateau);

		const zylann::voxel::TerrainHeightParams hp_oceanic = make_hp(51, params.oceanic_noise_freq, 3, 0.46f, 2.0f, 0.0f);
		zylann::voxel::terrain_height_3d_series(psx, psy, psz, ispc_out_oceanic.ptrw(), ispc_count_u, hp_oceanic);

		const zylann::voxel::TerrainHeightParams hp_detail = make_hp(91, params.detail_noise_freq, 3, 0.48f, 2.0f, 0.0f);
		zylann::voxel::terrain_height_3d_series(psx, psy, psz, ispc_out_detail.ptrw(), ispc_count_u, hp_detail);

		const zylann::voxel::TerrainHeightParams hp_mountain = make_hp(61, params.mountain_noise_freq, 5, 0.56f, 2.1f, 1.0f);
		zylann::voxel::terrain_height_3d_series(psx, psy, psz, ispc_out_mountain.ptrw(), ispc_count_u, hp_mountain);

		// humidity_macro reuses noise_detail's config, sampled at an offset position.
		const zylann::voxel::TerrainHeightParams hp_humidity = hp_detail;
		zylann::voxel::terrain_height_3d_series(phx, phy, phz, ispc_out_humidity_macro.ptrw(), ispc_count_u, hp_humidity);

		// climate_macro reuses noise_continent's config, sampled in macro-scaled space.
		const zylann::voxel::TerrainHeightParams hp_climate = make_hp(31, params.continent_noise_freq, 5, 0.52f, 2.05f, 0.0f);
		zylann::voxel::terrain_height_3d_series(pcx, pcy, pcz, ispc_out_climate_macro.ptrw(), ispc_count_u, hp_climate);

		// Recover raw fBm samples in ~[-1, 1] by subtracting planet_radius back out (amplitude=1).
		float *ob = ispc_out_base.ptrw();
		float *op = ispc_out_plateau.ptrw();
		float *oo = ispc_out_oceanic.ptrw();
		float *od = ispc_out_detail.ptrw();
		float *om = ispc_out_mountain.ptrw();
		float *ohm = ispc_out_humidity_macro.ptrw();
		float *ocm = ispc_out_climate_macro.ptrw();
		for (int i = 0; i < ispc_voxel_count; ++i) {
			ob[i] -= params.planet_radius;
			op[i] -= params.planet_radius;
			oo[i] -= params.planet_radius;
			od[i] -= params.planet_radius;
			om[i] -= params.planet_radius;
			ohm[i] -= params.planet_radius;
			ocm[i] -= params.planet_radius;
		}
	}

	size_t ispc_flat_idx = 0;
	for (int z = 0; z < size.z; ++z) {
		const float wz = float(origin.z) + z * step + half_step;
		for (int y = 0; y < size.y; ++y) {
			const float wy = float(origin.y) + y * step + half_step;
			for (int x = 0; x < size.x; ++x) {
				const float wx = float(origin.x) + x * step + half_step;
				const float r2 = wx * wx + wy * wy + wz * wz;
				const float inv_r = (r2 > 1.0f) ? _fast_rsqrt(r2) : 1.0f;
				const float r = r2 * inv_r;
				const float alt = r - params.planet_radius;
				const float ux = wx * inv_r;
				const float uy = wy * inv_r;
				const float uz = wz * inv_r;

				const float sx = ux * params.planet_radius;
				const float sy = uy * params.planet_radius;
				const float sz = uz * params.planet_radius;

				const float continent_scale = MAX(params.continent_size_scale, 0.35f);
				const float mx = sx / continent_scale;
				const float my = sy / continent_scale;
				const float mz = sz / continent_scale;
				// macro_primary/secondary/support and mountain_area_noise stay scalar (tectonic
				// macro-blend, not part of the ISPC batching below -- see the pre-pass comment above).
				const float macro_primary = 0.5f + 0.5f * noise_continent->get_noise_3d(mx, my, mz);
				const float macro_secondary = 0.5f + 0.5f * noise_continent->get_noise_3d(mx * 0.43f + 173.4f, my * 0.43f - 91.7f, mz * 0.43f + 47.2f);
				const float macro_support = 0.5f + 0.5f * noise_plateau->get_noise_3d(mx * 0.18f - 63.1f, my * 0.18f + 24.7f, mz * 0.18f + 91.3f);
				const float mountain_area_noise = 0.5f + 0.5f * noise_continent->get_noise_3d(mx * 0.45f + 13.0f, my * 0.45f - 29.0f, mz * 0.45f + 47.0f);

				// Expensive per-voxel raw fBm samples -- batched through ISPC when use_ispc_diffusion
				// is on (see the pre-pass above), otherwise the original per-voxel FastNoiseLite calls.
				const int fi = (int)ispc_flat_idx;
				const float base_n = params.use_ispc_diffusion ? ispc_out_base[fi] : noise_base->get_noise_3d(sx, sy, sz);
				const float plateau_n = params.use_ispc_diffusion ? ispc_out_plateau[fi] : noise_plateau->get_noise_3d(sx, sy, sz);
				const float oceanic_n = params.use_ispc_diffusion ? ispc_out_oceanic[fi] : noise_oceanic->get_noise_3d(sx, sy, sz);
				const float detail_n = params.use_ispc_diffusion ? ispc_out_detail[fi] : noise_detail->get_noise_3d(sx, sy, sz);
				const float mountain_raw_n = params.use_ispc_diffusion ? ispc_out_mountain[fi] : noise_mountain->get_noise_3d(sx, sy, sz);
				const float mountain_shape = _fast_pow01(CLAMP(0.5f + 0.5f * mountain_raw_n, 0.0f, 1.0f), MAX(0.65f, params.mountain_shape_boost * 1.45f));
				++ispc_flat_idx;
				const float medium_lod = 1.0f - _ss(4.0f, 48.0f, float(step));
				const float detail_lod = 1.0f - _ss(2.0f, 24.0f, float(step));
				const PlanetTectonics::TerrainData td = tectonics.is_valid() ? tectonics->get_terrain_data(Vector3(ux, uy, uz)) : PlanetTectonics::TerrainData();
				const float falloff = CLAMP(td.falloff_s, 0.0f, 1.0f);
				const float smooth_falloff = _ss(0.06f, 0.94f, falloff);
				const float oceanic = CLAMP(td.oceanic_s, 0.0f, 1.0f);
				const float plate_bias = td.bias_s;
				const float border_dist_m = MAX(td.border_dist_rad, 0.0f) * params.planet_radius;
				const float tectonic_mask = 1.0f - _ss(450.0f, 12000.0f, border_dist_m);
				const float tectonic_land_bias = (1.0f - oceanic) * Math::lerp(0.38f, 1.0f, smooth_falloff);

				float macro_shape = macro_primary * 0.78f + macro_secondary * 0.16f + macro_support * 0.06f;
				macro_shape = Math::lerp(macro_shape, _ss(0.18f, 0.82f, macro_shape), CLAMP(params.continent_cohesion, 0.0f, 1.0f) * 0.65f);
				const float land_ratio_target = CLAMP(
						params.land_coverage_target +
								(0.5f - CLAMP(params.oceanic_fraction, 0.0f, 1.0f)) * 0.32f * MAX(params.oceanic_fraction_influence, 0.0f) +
								params.continent_balance * 0.16f,
						0.08f,
						0.84f);
				const float land_threshold_center = Math::lerp(0.82f, 0.46f, land_ratio_target);
				const float land_threshold_width = Math::lerp(0.22f, 0.10f, CLAMP(params.continent_cohesion, 0.0f, 1.0f));
				const float humidity_macro_n = params.use_ispc_diffusion ? ispc_out_humidity_macro[fi] : noise_detail->get_noise_3d(sx + 91.0f, sy - 44.0f, sz + 17.0f);
				const float humidity_macro = 0.5f + 0.5f * humidity_macro_n;
				const float climate_macro_n = params.use_ispc_diffusion ? ispc_out_climate_macro[fi] : noise_continent->get_noise_3d(mx * 0.26f - 131.0f, my * 0.26f + 67.0f, mz * 0.26f - 29.0f);
				const float climate_macro = 0.5f + 0.5f * climate_macro_n;
				const float tectonic_bias = (macro_secondary - 0.5f) * 0.04f + (climate_macro - 0.5f) * 0.03f - CLAMP(params.oceanic_fraction - 0.5f, -0.5f, 0.5f) * 0.03f;
				const float land_signal = CLAMP(macro_shape + tectonic_bias + tectonic_land_bias * 0.05f - oceanic * 0.03f, 0.0f, 1.0f);
				const float land_mask = _ss(land_threshold_center - land_threshold_width, land_threshold_center + land_threshold_width, land_signal);
				const float core_mask = _ss(land_threshold_center + land_threshold_width * 0.12f, land_threshold_center + land_threshold_width * 1.45f, land_signal);
				const float inland = CLAMP(MAX(core_mask * 0.96f, land_mask), 0.0f, 1.0f);
				const float coast_band = 1.0f - _ss(0.0f, land_threshold_width * 1.35f, Math::abs(land_signal - land_threshold_center));
				const float shore_proximity = CLAMP(MAX(1.0f - inland, coast_band * 0.72f), 0.0f, 1.0f);
				const float climate_coast_bias = CLAMP(tectonic_mask * (0.14f + 0.06f * (1.0f - oceanic)), 0.0f, 1.0f);
				const float coast_proximity = CLAMP(MAX(shore_proximity, climate_coast_bias), 0.0f, 1.0f);
				const float land_factor = CLAMP(Math::lerp(land_mask, core_mask, 0.14f) + tectonic_land_bias * 0.04f + core_mask * 0.02f - oceanic * 0.02f, 0.0f, 1.0f);
				const float continental_shelf = land_factor * (1.0f - inland * 0.82f);
				const float continental_core = land_factor * MAX(inland * 0.56f, core_mask * 0.62f) * Math::lerp(0.58f, 0.92f, smooth_falloff);
				const float interior_macro = 0.5f + 0.5f * noise_continent->get_noise_3d(mx * 0.12f + 401.0f, my * 0.12f - 187.0f, mz * 0.12f + 73.0f);
				const float basin_macro = 0.5f + 0.5f * noise_plateau->get_noise_3d(mx * 0.16f - 233.0f, my * 0.16f + 141.0f, mz * 0.16f - 59.0f);
				const float interior_plateau_mask = continental_core * (0.42f + 0.58f * smooth_falloff) * (0.30f + 0.70f * inland);
				const float basin_cut = MAX(0.0f, 0.56f - basin_macro);
				const float interior_ridge = MAX(0.0f, interior_macro - 0.54f);
				const float relief_bias = CLAMP(continental_core * 0.34f + continental_shelf * 0.18f + mountain_area_noise * 0.18f + tectonic_land_bias * 0.05f, 0.0f, 1.0f);

				float height = (land_signal - land_threshold_center) * 150.0f;
				height += Math::lerp(-params.ocean_depth * 0.82f, params.continent_height * 0.42f, land_factor);
				height += plate_bias * Math::lerp(0.05f, 0.10f, smooth_falloff);
				height += continental_shelf * (34.0f + 38.0f * shore_proximity + humidity_macro * 8.0f);
				height += continental_core * (8.0f + 18.0f * MAX(smooth_falloff, core_mask) + relief_bias * 8.0f);
				height -= interior_plateau_mask * (16.0f + 24.0f * basin_cut + 8.0f * (1.0f - interior_ridge));
				height -= (1.0f - land_factor) * (64.0f + 96.0f * (1.0f - shore_proximity));
				const float broad_plateau_n = noise_plateau->get_noise_3d(sx * 0.22f - 91.0f, sy * 0.22f + 57.0f, sz * 0.22f - 23.0f);
				const float broad_detail_n = noise_detail->get_noise_3d(sx * 0.28f + 211.0f, sy * 0.28f - 133.0f, sz * 0.28f + 71.0f);
				const float smoothed_plateau = Math::lerp(plateau_n, broad_plateau_n, 0.68f);
				const float smoothed_detail = Math::lerp(detail_n, broad_detail_n, 0.72f);
				const float spike_damping = Math::lerp(0.62f, 0.90f, relief_bias);
				const float detail_suppression = Math::lerp(1.0f, 0.58f, shore_proximity * (1.0f - relief_bias * 0.30f));
				const float interior_variation = continental_core * (0.32f + 0.68f * smooth_falloff) * (0.42f + 0.58f * (1.0f - shore_proximity));
				height += base_n * params.base_noise_amp * Math::lerp(0.22f, 0.72f, land_factor * 0.90f + 0.10f);
				height += (macro_primary - 0.5f) * params.continent_noise_amp * land_factor * Math::lerp(0.54f, 0.72f, medium_lod);
				height += (interior_macro - 0.5f) * params.continent_noise_amp * 0.22f * interior_variation * detail_lod;
				height += (basin_macro - 0.5f) * params.continent_noise_amp * 0.16f * interior_variation * detail_lod;
				height += smoothed_plateau * params.plateau_noise_amp * interior_variation * Math::lerp(0.08f, 0.14f, medium_lod) * spike_damping;
				height += oceanic_n * params.oceanic_noise_amp * (1.0f - land_factor) * Math::lerp(0.54f, 0.76f, medium_lod);
				height -= basin_cut * params.continent_noise_amp * 0.30f * interior_plateau_mask;
				height += (smoothed_detail * 0.64f + broad_plateau_n * 0.36f) * params.continent_noise_amp * 0.22f * interior_variation * detail_lod;
				height -= MAX(0.0f, 0.50f - (0.5f + 0.5f * broad_detail_n)) * params.continent_noise_amp * 0.18f * interior_variation;
				height += interior_ridge * params.continent_noise_amp * 0.08f * interior_variation * detail_lod;

				float coast_smoothing = shore_proximity * CLAMP(params.coast_smoothing_strength, 0.0f, 1.0f);
				coast_smoothing *= 1.0f - _ss(120.0f, MAX(params.coast_smoothing_width, 121.0f), Math::abs(height));
				if (coast_smoothing > 0.001f) {
					const float coast_target = (land_signal - land_threshold_center) * 92.0f +
							continental_shelf * (42.0f + 34.0f * shore_proximity + humidity_macro * 8.0f) +
							continental_core * (10.0f + 16.0f * MAX(inland, core_mask) + relief_bias * 8.0f) -
							(1.0f - land_factor) * 14.0f;
					height = Math::lerp(height, coast_target, CLAMP(coast_smoothing, 0.0f, 0.92f));
				}

				const float mountain_area = _ss(0.63f, 0.87f, mountain_area_noise) * continental_core * Math::lerp(0.52f, 1.0f, relief_bias);
				const float mountains = mountain_shape * params.mountain_noise_amp * mountain_area * (0.68f + relief_bias * 0.16f);
				height += mountains;
				if (params.use_erosion) {
					// Land only, fading out at the shore, strongest where V3 already builds relief
					const float erosion_mask = land_factor * (1.0f - shore_proximity * 0.7f) *
							Math::lerp(0.35f, 1.0f, CLAMP(mountain_area + relief_bias * 0.5f, 0.0f, 1.0f));
					height += erosion_out[fi] * params.erosion_height_scale * erosion_mask;
				}
				height += smoothed_detail * params.detail_noise_amp * Math::lerp(0.18f, 0.52f, inland) * detail_lod * detail_suppression;
				const float macro_altitude = MAX(height, 0.0f);
				const float lowland = 1.0f - _ss(140.0f, 1100.0f, macro_altitude);
				const float highland = _ss(1200.0f, 3000.0f, macro_altitude);
				const float rolling_hill = _ss(40.0f, 280.0f, macro_altitude) * (1.0f - _ss(850.0f, 1700.0f, macro_altitude));
				const float coastal_plain_mask = CLAMP(shore_proximity * lowland * land_factor * (1.0f - relief_bias * 0.72f), 0.0f, 1.0f);
				if (coastal_plain_mask > 0.001f) {
					const float rolling_noise = noise_base->get_noise_3d(sx * 0.35f + 411.0f, sy * 0.35f - 173.0f, sz * 0.35f + 97.0f);
					float meadow_target = Math::lerp(18.0f, 95.0f, rolling_noise * 0.5f + 0.5f);
					meadow_target = Math::lerp(meadow_target, 145.0f, rolling_hill * 0.35f);
					height = Math::lerp(height, meadow_target, coastal_plain_mask * 0.48f);
				}
				if (height > 0.0f) {
					height = MAX(height, 5.0f);
				}

				const float sdf = alt - height;

				// Bake ocean water directly into CHANNEL_DATA5 (== VoxelWaterSimulator::WATER_CHANNEL)
				// for every non-solid ocean-masked voxel at or below sea_level -- a complete,
				// gapless ocean driven by planet_radius + sea_level within the actual ocean/
				// continent mask (land_factor), matching the solid/air shape (sdf) already being
				// computed for the terrain itself. This bake-at-generation-time path is what lets
				// a VoxelWaterSimulator actually see and render ocean water, rather than the
				// ocean floor's color being the only water-related thing on the surface (ported
				// from EdenPlanetGeneratorV1's generate_block()).
				const bool bake_ocean_water =
						sdf > 0.0f && alt <= params.sea_level && land_factor < params.ocean_water_mask_threshold;

				// Local land water sources ("springs"): seeded in low-lying interior basins --
				// basin_water_signal reuses basin_cut/interior_plateau_mask, the same signal that
				// already carves basin terrain lower above (height -= interior_plateau_mask * (...
				// basin_cut ...)), so a seed only ever lands where there's a real depression for
				// it to pool into, not an arbitrary noise pick. broad_detail_n (already computed
				// for terrain detail, zero extra noise cost) breaks the basin up into scattered
				// patches instead of flooding every basin voxel uniformly. Only a thin sdf band
				// right at the surface, seeded at partial mass rather than full: this is a seed,
				// not a finished lake -- VoxelWaterSimulator's real per-tick simulation (already
				// tiered across LOD0-3) spreads/settles it into an actual pond/stream surface once
				// that area is close enough to a viewer to be actively simulated, which is cheaper
				// and more physically real than trying to detect/fill whole basins in the generator.
				const float basin_water_signal = interior_plateau_mask * basin_cut;
				const bool spring_region = land_factor >= params.ocean_water_mask_threshold &&
						basin_water_signal > params.spring_basin_threshold &&
						broad_detail_n > params.spring_noise_threshold;
				const bool bake_spring_water = !bake_ocean_water && spring_region && sdf > 0.0f && sdf < 3.0f;

				const bool bake_water = bake_ocean_water || bake_spring_water;
				const float bake_water_mass = bake_ocean_water ? 1.0f : params.spring_seed_mass;

				if (sdf > budget) {
					buffer.set_voxel_f(sdf, x, y, z, VoxelBuffer::CHANNEL_SDF);
					if (bake_water) {
						buffer.set_voxel_f(bake_water_mass, x, y, z, VoxelBuffer::CHANNEL_DATA5);
					}
					continue;
				}
				if (sdf < -budget) {
					buffer.set_voxel_f(sdf, x, y, z, VoxelBuffer::CHANNEL_SDF);
					buffer.set_voxel(rock_indices, x, y, z, VoxelBuffer::CHANNEL_INDICES);
					buffer.set_voxel(rock_weights, x, y, z, VoxelBuffer::CHANNEL_WEIGHTS);
					continue;
				}

				buffer.set_voxel_f(sdf, x, y, z, VoxelBuffer::CHANNEL_SDF);
				if (bake_water) {
					buffer.set_voxel_f(1.0f, x, y, z, VoxelBuffer::CHANNEL_DATA5);
				}

				const float beach_height_limit = MAX(params.beach_width_m * 0.035f, 36.0f);
				const bool deep_ocean = land_factor < 0.10f;
				const bool beach = land_factor > 0.18f && land_factor < 0.46f && height > -8.0f && height < beach_height_limit && shore_proximity > 0.62f;

				int land_mat = MAT_GRASS;
				if (deep_ocean) {
					land_mat = MAT_OCEAN_FLOOR;
				} else if (beach) {
					land_mat = MAT_SAND;
				} else {
					const float lat01 = Math::abs(uy);
					const float lat_curve = _fast_pow01(lat01, 1.35f);
					const float humidity_macro = 0.5f + 0.5f * noise_detail->get_noise_3d(sx + 91.0f, sy - 44.0f, sz + 17.0f);
					const float climate_macro = 0.5f + 0.5f * noise_continent->get_noise_3d(mx * 0.26f - 131.0f, my * 0.26f + 67.0f, mz * 0.26f - 29.0f);
					const float polar_latitude = _ss(0.34f, 0.78f, lat01);
					const float equatorial_latitude = 1.0f - _ss(0.18f, 0.54f, lat01);
					const float rain_shadow = CLAMP(relief_bias * (1.0f - coast_proximity) * (0.22f + smooth_falloff * 0.34f), 0.0f, 1.0f);
					const float climate_variance = CLAMP(Math::abs(humidity_macro - 0.5f) * 1.55f + Math::abs(climate_macro - 0.5f) * 1.35f + Math::abs(basin_macro - 0.5f) * 0.90f, 0.0f, 1.0f);
					const float inland_dryness = CLAMP(inland * 0.22f + (1.0f - coast_proximity) * 0.24f + rain_shadow * 0.28f + MAX(0.0f, 0.5f - humidity_macro) * 0.18f + basin_cut * 0.16f - MAX(climate_macro - 0.55f, 0.0f) * 0.12f, 0.0f, 1.0f);
					const float temp_c = Math::lerp(params.climate_equator_temp_c, params.climate_polar_temp_c, lat_curve) - MAX(height, 0.0f) * 0.0048f + (climate_macro - 0.5f) * 6.0f + (equatorial_latitude - polar_latitude) * 1.5f + (interior_macro - 0.5f) * 2.4f;
					const float shore_moisture = _ss(0.24f, 0.86f, coast_proximity);
					float humidity = Math::lerp(params.climate_interior_humidity, params.climate_ocean_humidity, CLAMP(shore_moisture * 0.56f + (1.0f - lat01) * 0.18f + humidity_macro * 0.16f + MAX(climate_macro - 0.48f, 0.0f) * 0.18f, 0.0f, 1.0f));
					humidity += (humidity_macro - 0.5f) * 0.40f + (climate_macro - 0.5f) * 0.20f + (basin_macro - 0.5f) * 0.18f + (interior_macro - 0.5f) * 0.10f;
					humidity -= inland_dryness * 0.22f + MAX(height, 0.0f) / 5600.0f * 0.06f;
					humidity = CLAMP(humidity, 0.0f, 1.0f);

					const float terrain_alt = MAX(height, 0.0f);
					const float biome_lowland = 1.0f - _ss(280.0f, 1850.0f, terrain_alt);
					const float biome_upland = _ss(350.0f, 2100.0f, terrain_alt);
					const float biome_highland = _ss(1250.0f, 3200.0f, terrain_alt);
					const float habitable_upland = 1.0f - _ss(2400.0f, 4300.0f, terrain_alt);
					const float ruggedness = CLAMP(relief_bias * 0.58f + biome_highland * 0.42f, 0.0f, 1.0f);
					const float warm = CLAMP((temp_c + 20.0f) / 58.0f, 0.0f, 1.0f);
					const float cold = 1.0f - warm;
					const float biome_blend = CLAMP(params.biome_terrain_blend_strength, 0.0f, 1.0f);
					const float wet_zone = CLAMP(climate_macro * 0.55f + humidity_macro * 0.45f, 0.0f, 1.0f);
					int region_pref_kind = 5;
					float region_pref_strength = 0.0f;
					int region_pref_kind_b = 5;
					float region_pref_strength_b = 0.0f;
					float region_pref_blend = 0.0f;
					_sample_region_preference(tectonics, _biome_point_region_id, _biome_region_kind, _biome_region_strength,
							Vector3(ux, uy, uz), region_pref_kind, region_pref_strength, region_pref_kind_b, region_pref_strength_b, region_pref_blend);
					const float dry_zone = 1.0f - wet_zone;
					const float aridness = CLAMP((1.0f - humidity) * 0.60f + inland_dryness * 0.30f + dry_zone * 0.22f + climate_variance * 0.08f - wet_zone * 0.12f, 0.0f, 1.0f);
					const float lushness = CLAMP(humidity * 0.68f + wet_zone * 0.28f + equatorial_latitude * 0.12f + climate_variance * 0.06f - inland_dryness * 0.10f, 0.0f, 1.0f);
					const float inland_wetness = CLAMP(humidity * (0.48f + 0.52f * habitable_upland) + wet_zone * 0.24f + MAX(0.0f, basin_macro - 0.5f) * 0.12f - inland_dryness * 0.10f, 0.0f, 1.0f);
					const float desert_spawn = CLAMP(params.biome_spawn_desert, 0.0f, 1.0f);
					const float forest_spawn = CLAMP(params.biome_spawn_forest, 0.0f, 1.0f);
					const float tropical_spawn = CLAMP(params.biome_spawn_tropical, 0.0f, 1.0f);
					const float tundra_spawn = CLAMP(params.biome_spawn_tundra, 0.0f, 1.0f);
					const float grassland_spawn = CLAMP(params.biome_spawn_grassland, 0.0f, 1.0f);
					const float alpine_spawn = CLAMP(params.biome_spawn_alpine, 0.0f, 1.0f);
					const float desert_spawn_weight = _spawn_weight(desert_spawn);
					const float forest_spawn_weight = _spawn_weight(forest_spawn);
					const float tropical_spawn_weight = _spawn_weight(tropical_spawn);
					const float tundra_spawn_weight = _spawn_weight(tundra_spawn);
					const float grassland_spawn_weight = _spawn_weight(grassland_spawn);
					const float alpine_spawn_weight = _spawn_weight(alpine_spawn);
					const float grassland_balance = CLAMP(1.0f - Math::abs(humidity - 0.46f) * 1.55f - Math::abs(warm - 0.54f) * 0.42f - Math::abs(wet_zone - 0.50f) * 0.42f, 0.0f, 1.0f);
					const float temperate_band = CLAMP(1.0f - Math::abs(warm - 0.52f) * 1.55f, 0.0f, 1.0f);
					const float biome_diversity = CLAMP(climate_variance * (0.48f + 0.52f * inland) + Math::abs(grassland_balance - 0.5f) * 0.30f, 0.0f, 1.0f);
					float grassland_score = biome_blend * grassland_spawn_weight * (0.40f + 0.60f * biome_lowland) * (0.24f + 0.76f * grassland_balance) * (0.30f + 0.30f * temperate_band + 0.24f * (1.0f - inland_dryness) + 0.16f * habitable_upland) * (0.76f + 0.24f * (1.0f - biome_diversity));
					float desert_score = biome_blend * desert_spawn_weight * (0.22f + 0.78f * biome_lowland) * (0.24f + 0.76f * aridness) * (0.42f + warm * 0.90f) * (0.34f + inland_dryness * 0.36f + dry_zone * 0.28f + biome_upland * 0.10f + biome_diversity * 0.18f);
					float tropical_score = biome_blend * tropical_spawn_weight * (0.24f + 0.76f * biome_lowland) * lushness * MAX(warm - 0.02f, 0.0f) * (0.66f + 0.48f * equatorial_latitude) * (0.54f + 0.46f * wet_zone + biome_diversity * 0.16f) * (0.66f + 0.34f * inland_wetness) * (0.76f + 0.24f * habitable_upland);
					float forest_score = biome_blend * forest_spawn_weight * (0.26f + 0.74f * (0.50f * biome_lowland + 0.50f * habitable_upland)) * lushness * (0.30f + 0.38f * (1.0f - aridness) + 0.32f * MAX(wet_zone, biome_upland) + biome_diversity * 0.12f) * (0.50f + 0.40f * (1.0f - cold)) * (0.68f + 0.32f * inland_wetness) * (1.0f - equatorial_latitude * 0.20f);
					const float polar_fade = _ss(0.78f, 0.96f, lat01) * (0.22f + 0.78f * cold);
					const float altitude_cold = _ss(1850.0f, 3400.0f, terrain_alt) * (0.35f + 0.65f * cold);
					const float tundra_gate = CLAMP(MAX(polar_fade, altitude_cold * 0.65f), 0.0f, 1.0f);
					float tundra_score = biome_blend * tundra_spawn_weight * (0.16f + 0.20f * biome_lowland + 0.64f * habitable_upland) * _ss(0.58f, 0.92f, cold) * (0.22f + 0.18f * humidity) * tundra_gate;
					float alpine_score = biome_blend * alpine_spawn_weight * (0.10f + 0.90f * biome_highland) * (0.18f + 0.24f * cold + 0.36f * relief_bias + 0.22f * ruggedness);
					float desert_score_b = desert_score;
					float forest_score_b = forest_score;
					float tropical_score_b = tropical_score;
					float tundra_score_b = tundra_score;
					float grassland_score_b = grassland_score;
					float alpine_score_b = alpine_score;
					_apply_region_preference(uint8_t(region_pref_kind), region_pref_strength, desert_score, forest_score, tropical_score, tundra_score, grassland_score, alpine_score);
					if (region_pref_blend > 0.001f) {
						_apply_region_preference(uint8_t(region_pref_kind_b), region_pref_strength_b, desert_score_b, forest_score_b, tropical_score_b, tundra_score_b, grassland_score_b, alpine_score_b);
						const float blend_t = CLAMP(region_pref_blend * 0.85f, 0.0f, 1.0f);
						desert_score = Math::lerp(desert_score, desert_score_b, blend_t);
						forest_score = Math::lerp(forest_score, forest_score_b, blend_t);
						tropical_score = Math::lerp(tropical_score, tropical_score_b, blend_t);
						tundra_score = Math::lerp(tundra_score, tundra_score_b, blend_t);
						grassland_score = Math::lerp(grassland_score, grassland_score_b, blend_t);
						alpine_score = Math::lerp(alpine_score, alpine_score_b, blend_t);
					}
					const float biome_score_total = MAX(desert_score + forest_score + tropical_score + tundra_score + grassland_score + alpine_score, 0.001f);
					const float desert_w = desert_score / biome_score_total;
					const float forest_w = forest_score / biome_score_total;
					const float tropical_w = tropical_score / biome_score_total;
					const float tundra_w = tundra_score / biome_score_total;
					const float grass_w = grassland_score / biome_score_total;
					const float alpine_w = alpine_score / biome_score_total;
					const float biome_relief_mask = land_factor * (0.24f + 0.76f * (1.0f - shore_proximity));
					const float desert_relief = (smoothed_plateau * 0.60f + broad_detail_n * 0.24f) * (18.0f + params.detail_noise_amp * 0.32f) * biome_lowland;
					const float forest_relief = (smoothed_detail * 0.42f + base_n * 0.20f) * (14.0f + params.detail_noise_amp * 0.28f) * (0.45f + 0.55f * habitable_upland);
					const float tropical_relief = (smoothed_detail * 0.46f + broad_plateau_n * 0.10f - MAX(0.0f, 0.52f - basin_macro) * 0.85f) * (18.0f + params.detail_noise_amp * 0.34f) * (0.48f + 0.52f * inland_wetness);
					const float tundra_relief = (smoothed_detail * 0.18f + broad_plateau_n * 0.12f) * (10.0f + params.detail_noise_amp * 0.18f) * (0.50f + 0.50f * biome_highland);
					const float grass_relief = (base_n * 0.26f + smoothed_detail * 0.24f) * (12.0f + params.detail_noise_amp * 0.24f) * (0.50f + 0.50f * biome_lowland);
					const float alpine_relief = mountain_shape * params.mountain_noise_amp * 0.10f * (0.25f + 0.75f * biome_highland);
					height += biome_relief_mask * (desert_w * desert_relief + forest_w * forest_relief + tropical_w * tropical_relief + tundra_w * tundra_relief + grass_w * grass_relief + alpine_w * alpine_relief);

					const float tundra_class_ratio = Math::lerp(2.1f, 0.88f, tundra_spawn);
					const float desert_class_ratio = Math::lerp(2.4f, 0.72f, desert_spawn);
					const float tropical_class_ratio = Math::lerp(2.0f, 0.78f, tropical_spawn);
					const float forest_class_ratio = Math::lerp(1.8f, 0.78f, forest_spawn);
					const bool classify_tundra = tundra_spawn > 0.001f && tundra_gate > 0.42f && (temp_c < 7.5f || polar_fade > 0.52f || (height > 2000.0f && cold > 0.30f)) && tundra_score > MAX(desert_score, MAX(forest_score, MAX(tropical_score, grassland_score))) * tundra_class_ratio;
					const bool classify_desert = desert_spawn > 0.001f && warm > 0.60f && (humidity < 0.38f || aridness > 0.56f) && desert_score > MAX(grassland_score, MAX(forest_score, tropical_score)) * desert_class_ratio;
					const bool classify_tropical = tropical_spawn > 0.001f && warm > 0.60f && !classify_desert && (humidity > 0.44f || wet_zone > 0.58f) && tropical_score > MAX(forest_score, grassland_score) * tropical_class_ratio;
					const bool classify_forest = forest_spawn > 0.001f && !classify_desert && !classify_tropical && (humidity > 0.46f || wet_zone > 0.60f || forest_score > grassland_score * 0.90f) && forest_score > MAX(grassland_score, desert_score) * forest_class_ratio;

					if ((alpine_score > 0.20f && temp_c < 5.0f) || (height > 2850.0f && temp_c < 2.5f)) {
						land_mat = (temp_c < -10.0f || height > 3200.0f) ? MAT_SNOW : MAT_ROCK;
					} else if ((tundra_score > MAX(desert_score, MAX(forest_score, MAX(tropical_score, grassland_score))) * 0.88f && (temp_c < 8.0f || polar_fade > 0.34f || (height > 1600.0f && cold > 0.24f))) || classify_tundra) {
						land_mat = (temp_c < -4.0f || (lat01 > 0.78f && height > 500.0f)) ? MAT_SNOW : MAT_ROCK;
					} else if ((desert_score > grassland_score * 0.72f && desert_score > 0.04f) || classify_desert) {
						land_mat = (aridness > 0.64f || humidity < 0.20f) ? MAT_SAND : MAT_DIRT;
					} else if ((tropical_score > MAX(forest_score, grassland_score) * 0.78f && tropical_score > 0.04f) || classify_tropical) {
						land_mat = MAT_MOSS;
					} else if ((forest_score > MAX(grassland_score, desert_score) * 0.78f && forest_score > 0.04f) || classify_forest) {
						land_mat = MAT_DIRT;
					} else if (params.biome_spawn_grassland > 0.0f) {
						land_mat = MAT_GRASS;
					}
				}
				if (mountains > params.mountain_noise_amp * 0.31f && land_mat != MAT_SNOW && !beach) {
					land_mat = MAT_ROCK;
				}

				int packed_i = 0;
				int packed_w = 0;
				_pack_mixel4(land_mat, MAT_OCEAN_FLOOR, 1.0f - land_mask, 0.42f, 0.55f, 0.56f, 0.72f, packed_i, packed_w);
				buffer.set_voxel(packed_i, x, y, z, VoxelBuffer::CHANNEL_INDICES);
				buffer.set_voxel(packed_w, x, y, z, VoxelBuffer::CHANNEL_WEIGHTS);
			}
		}
	}

	return result;
}

int EdenPlanetGeneratorV3::get_used_channels_mask() const {
	return (1 << VoxelBuffer::CHANNEL_SDF) |
				(1 << VoxelBuffer::CHANNEL_INDICES) |
				(1 << VoxelBuffer::CHANNEL_WEIGHTS) |
				(1 << VoxelBuffer::CHANNEL_DATA5); // baked ocean water mass, see VOXEL_FULL/bake_water above
}

void EdenPlanetGeneratorV3::_pack_mixel4(int land_mat, int ocean_mat, float transition,
		float sand_start, float sand_end,
		float ocean_start, float ocean_end,
		int &r_indices, int &r_weights) {
	transition = CLAMP(transition, 0.0f, 1.0f);
	float sand_t = _ss(sand_start, sand_end, transition);
	float ocean_t = _ss(ocean_start, ocean_end, transition);
	ocean_t = MAX(ocean_t, sand_t);

	float ws[4] = {
		1.0f - sand_t,
		sand_t - ocean_t,
		ocean_t,
		0.0f,
	};
	int is[4] = {
		land_mat,
		MAT_SAND,
		ocean_mat,
		ocean_mat,
	};

	for (int a = 0; a < 4; ++a) {
		for (int b = a + 1; b < 4; ++b) {
			if (ws[b] > ws[a]) {
				float tw = ws[a];
				ws[a] = ws[b];
				ws[b] = tw;
				int ti = is[a];
				is[a] = is[b];
				is[b] = ti;
			}
		}
	}

	float sum = ws[0] + ws[1] + ws[2] + ws[3];
	if (sum < 1e-6f) {
		ws[0] = 1.0f;
		ws[1] = ws[2] = ws[3] = 0.0f;
		sum = 1.0f;
	}
	for (int i = 0; i < 4; ++i) {
		ws[i] /= sum;
	}

	int w4[4];
	int acc = 0;
	for (int i = 0; i < 4; ++i) {
		w4[i] = CLAMP(int(Math::round(ws[i] * 15.0f)), 0, 15);
		acc += w4[i];
	}
	if (acc != 15) {
		w4[0] = CLAMP(w4[0] + (15 - acc), 0, 15);
	}

	r_indices = (is[0] & 0xF) | ((is[1] & 0xF) << 4) | ((is[2] & 0xF) << 8) | ((is[3] & 0xF) << 12);
	r_weights = (w4[0] & 0xF) | ((w4[1] & 0xF) << 4) | ((w4[2] & 0xF) << 8) | ((w4[3] & 0xF) << 12);
}

#define V6_NATIVE_FLOAT_PROP(name) \
	void EdenPlanetGeneratorV3::set_##name(float v) { \
		{ \
			zylann::RWLockWrite wlock(_parameters_lock); \
			_parameters.name = v; \
		} \
		emit_changed(); \
	} \
	float EdenPlanetGeneratorV3::get_##name() const { \
		zylann::RWLockRead rlock(_parameters_lock); \
		return _parameters.name; \
	}

#define V6_NATIVE_FLOAT_PROP_SETUP(name) \
	void EdenPlanetGeneratorV3::set_##name(float v) { \
		{ \
			zylann::RWLockWrite wlock(_parameters_lock); \
			_parameters.name = v; \
			_parameters.is_setup = false; \
		} \
		emit_changed(); \
	} \
	float EdenPlanetGeneratorV3::get_##name() const { \
		zylann::RWLockRead rlock(_parameters_lock); \
		return _parameters.name; \
	}

#define V6_NATIVE_INT_PROP_SETUP(name) \
	void EdenPlanetGeneratorV3::set_##name(int v) { \
		{ \
			zylann::RWLockWrite wlock(_parameters_lock); \
			_parameters.name = v; \
			_parameters.is_setup = false; \
		} \
		emit_changed(); \
	} \
	int EdenPlanetGeneratorV3::get_##name() const { \
		zylann::RWLockRead rlock(_parameters_lock); \
		return _parameters.name; \
	}

V6_NATIVE_FLOAT_PROP(planet_radius)
V6_NATIVE_INT_PROP_SETUP(seed)
V6_NATIVE_FLOAT_PROP(max_terrain_height)
V6_NATIVE_FLOAT_PROP(continent_height)
V6_NATIVE_FLOAT_PROP(ocean_depth)
V6_NATIVE_FLOAT_PROP(sea_level)
V6_NATIVE_FLOAT_PROP(ocean_water_mask_threshold)
V6_NATIVE_FLOAT_PROP(spring_basin_threshold)
V6_NATIVE_FLOAT_PROP(spring_noise_threshold)
V6_NATIVE_FLOAT_PROP(spring_seed_mass)
V6_NATIVE_FLOAT_PROP_SETUP(oceanic_fraction)
V6_NATIVE_INT_PROP_SETUP(n_points)
V6_NATIVE_INT_PROP_SETUP(n_plates)
V6_NATIVE_FLOAT_PROP_SETUP(border_warp)
V6_NATIVE_FLOAT_PROP(oceanic_fraction_influence)
V6_NATIVE_FLOAT_PROP(continent_cohesion)
V6_NATIVE_FLOAT_PROP(continent_balance)
V6_NATIVE_FLOAT_PROP(land_coverage_target)
V6_NATIVE_FLOAT_PROP(continent_size_scale)
V6_NATIVE_FLOAT_PROP(base_noise_amp)
V6_NATIVE_FLOAT_PROP_SETUP(base_noise_freq)
V6_NATIVE_FLOAT_PROP(continent_noise_amp)
V6_NATIVE_FLOAT_PROP_SETUP(continent_noise_freq)
V6_NATIVE_FLOAT_PROP(plateau_noise_amp)
V6_NATIVE_FLOAT_PROP_SETUP(plateau_noise_freq)
V6_NATIVE_FLOAT_PROP(oceanic_noise_amp)
V6_NATIVE_FLOAT_PROP_SETUP(oceanic_noise_freq)
V6_NATIVE_FLOAT_PROP(mountain_noise_amp)
V6_NATIVE_FLOAT_PROP_SETUP(mountain_noise_freq)
V6_NATIVE_FLOAT_PROP(detail_noise_amp)
V6_NATIVE_FLOAT_PROP_SETUP(detail_noise_freq)
V6_NATIVE_FLOAT_PROP(mountain_shape_boost)
V6_NATIVE_FLOAT_PROP(coast_smoothing_strength)
V6_NATIVE_FLOAT_PROP(coast_smoothing_width)
V6_NATIVE_FLOAT_PROP(climate_equator_temp_c)
V6_NATIVE_FLOAT_PROP(climate_polar_temp_c)
V6_NATIVE_FLOAT_PROP(climate_ocean_humidity)
V6_NATIVE_FLOAT_PROP(climate_interior_humidity)
V6_NATIVE_FLOAT_PROP_SETUP(biome_region_bias_strength)
V6_NATIVE_INT_PROP_SETUP(biome_region_min_cells)
V6_NATIVE_INT_PROP_SETUP(biome_region_max_cells)
V6_NATIVE_FLOAT_PROP(biome_terrain_blend_strength)
V6_NATIVE_FLOAT_PROP_SETUP(biome_spawn_desert)
V6_NATIVE_FLOAT_PROP_SETUP(biome_spawn_forest)
V6_NATIVE_FLOAT_PROP_SETUP(biome_spawn_tropical)
V6_NATIVE_FLOAT_PROP_SETUP(biome_spawn_tundra)
V6_NATIVE_FLOAT_PROP_SETUP(biome_spawn_grassland)
V6_NATIVE_FLOAT_PROP_SETUP(biome_spawn_alpine)
V6_NATIVE_FLOAT_PROP(beach_width_m)

void EdenPlanetGeneratorV3::set_use_ispc_diffusion(bool v) {
	{
		zylann::RWLockWrite wlock(_parameters_lock);
		_parameters.use_ispc_diffusion = v;
	}
	emit_changed();
}
bool EdenPlanetGeneratorV3::get_use_ispc_diffusion() const {
	zylann::RWLockRead rlock(_parameters_lock);
	return _parameters.use_ispc_diffusion;
}

#define V3_EROSION_PROP(type, name) \
	void EdenPlanetGeneratorV3::set_##name(type v) { \
		{ \
			zylann::RWLockWrite wlock(_parameters_lock); \
			_parameters.name = v; \
		} \
		emit_changed(); \
	} \
	type EdenPlanetGeneratorV3::get_##name() const { \
		zylann::RWLockRead rlock(_parameters_lock); \
		return _parameters.name; \
	}

V3_EROSION_PROP(bool, use_erosion)
V3_EROSION_PROP(float, erosion_height_scale)
V3_EROSION_PROP(float, erosion_tile_size)
V3_EROSION_PROP(float, erosion_strength)
V3_EROSION_PROP(float, erosion_detail)
V3_EROSION_PROP(int, erosion_octaves)
#undef V3_EROSION_PROP

#undef V6_NATIVE_FLOAT_PROP
#undef V6_NATIVE_FLOAT_PROP_SETUP
#undef V6_NATIVE_INT_PROP_SETUP

#define BIND_FLOAT(name) \
	ClassDB::bind_method(D_METHOD("set_" #name, "value"), &EdenPlanetGeneratorV3::set_##name); \
	ClassDB::bind_method(D_METHOD("get_" #name), &EdenPlanetGeneratorV3::get_##name);

#define BIND_INT(name) \
	ClassDB::bind_method(D_METHOD("set_" #name, "value"), &EdenPlanetGeneratorV3::set_##name); \
	ClassDB::bind_method(D_METHOD("get_" #name), &EdenPlanetGeneratorV3::get_##name);

#define BIND_BOOL(name) \
	ClassDB::bind_method(D_METHOD("set_" #name, "value"), &EdenPlanetGeneratorV3::set_##name); \
	ClassDB::bind_method(D_METHOD("get_" #name), &EdenPlanetGeneratorV3::get_##name);

void EdenPlanetGeneratorV3::_bind_methods() {
	ClassDB::bind_method(D_METHOD("setup"), &EdenPlanetGeneratorV3::setup);
	ClassDB::bind_method(D_METHOD("generate_biome_debug_image", "width", "height"), &EdenPlanetGeneratorV3::generate_biome_debug_image, DEFVAL(256), DEFVAL(128));
	ClassDB::bind_method(D_METHOD("get_biome_debug_image", "width", "height"), &EdenPlanetGeneratorV3::get_biome_debug_image, DEFVAL(1024), DEFVAL(512));
	ClassDB::bind_method(D_METHOD("get_plate_debug_image", "width", "height"), &EdenPlanetGeneratorV3::get_plate_debug_image, DEFVAL(1024), DEFVAL(512));
	ClassDB::bind_method(D_METHOD("get_plate_data_image", "width", "height"), &EdenPlanetGeneratorV3::get_plate_data_image, DEFVAL(1024), DEFVAL(512));
	ClassDB::bind_method(D_METHOD("get_macro_debug_image", "width", "height"), &EdenPlanetGeneratorV3::get_macro_debug_image, DEFVAL(1024), DEFVAL(512));
	ClassDB::bind_method(D_METHOD("get_climate_debug_image", "width", "height"), &EdenPlanetGeneratorV3::get_climate_debug_image, DEFVAL(1024), DEFVAL(512));
	ClassDB::bind_method(D_METHOD("get_topology_cell_debug_image", "width", "height"), &EdenPlanetGeneratorV3::get_topology_cell_debug_image, DEFVAL(1024), DEFVAL(512));
	ClassDB::bind_method(D_METHOD("get_biome_region_stats"), &EdenPlanetGeneratorV3::get_biome_region_stats);

	BIND_FLOAT(planet_radius);
	BIND_INT(seed);
	BIND_FLOAT(max_terrain_height);
	BIND_FLOAT(continent_height);
	BIND_FLOAT(ocean_depth);
	BIND_FLOAT(sea_level);
	BIND_FLOAT(ocean_water_mask_threshold);
	BIND_FLOAT(spring_basin_threshold);
	BIND_FLOAT(spring_noise_threshold);
	BIND_FLOAT(spring_seed_mass);
	BIND_FLOAT(oceanic_fraction);
	BIND_INT(n_points);
	BIND_INT(n_plates);
	BIND_FLOAT(border_warp);
	BIND_FLOAT(oceanic_fraction_influence);
	BIND_FLOAT(continent_cohesion);
	BIND_FLOAT(continent_balance);
	BIND_FLOAT(land_coverage_target);
	BIND_FLOAT(continent_size_scale);
	BIND_FLOAT(base_noise_amp);
	BIND_FLOAT(base_noise_freq);
	BIND_FLOAT(continent_noise_amp);
	BIND_FLOAT(continent_noise_freq);
	BIND_FLOAT(plateau_noise_amp);
	BIND_FLOAT(plateau_noise_freq);
	BIND_FLOAT(oceanic_noise_amp);
	BIND_FLOAT(oceanic_noise_freq);
	BIND_FLOAT(mountain_noise_amp);
	BIND_FLOAT(mountain_noise_freq);
	BIND_FLOAT(detail_noise_amp);
	BIND_FLOAT(detail_noise_freq);
	BIND_FLOAT(mountain_shape_boost);
	BIND_FLOAT(coast_smoothing_strength);
	BIND_FLOAT(coast_smoothing_width);
	BIND_FLOAT(climate_equator_temp_c);
	BIND_FLOAT(climate_polar_temp_c);
	BIND_FLOAT(climate_ocean_humidity);
	BIND_FLOAT(climate_interior_humidity);
	BIND_FLOAT(biome_region_bias_strength);
	BIND_INT(biome_region_min_cells);
	BIND_INT(biome_region_max_cells);
	BIND_FLOAT(biome_terrain_blend_strength);
	BIND_FLOAT(biome_spawn_desert);
	BIND_FLOAT(biome_spawn_forest);
	BIND_FLOAT(biome_spawn_tropical);
	BIND_FLOAT(biome_spawn_tundra);
	BIND_FLOAT(biome_spawn_grassland);
	BIND_FLOAT(biome_spawn_alpine);
	BIND_FLOAT(beach_width_m);
	BIND_BOOL(use_ispc_diffusion);
	BIND_BOOL(use_erosion);
	BIND_FLOAT(erosion_height_scale);
	BIND_FLOAT(erosion_tile_size);
	BIND_FLOAT(erosion_strength);
	BIND_FLOAT(erosion_detail);
	BIND_INT(erosion_octaves);

	ADD_GROUP("Planet", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "planet_radius"), "set_planet_radius", "get_planet_radius");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_terrain_height"), "set_max_terrain_height", "get_max_terrain_height");

	ADD_GROUP("Macro Shape", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continent_height"), "set_continent_height", "get_continent_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ocean_depth"), "set_ocean_depth", "get_ocean_depth");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sea_level"), "set_sea_level", "get_sea_level");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ocean_water_mask_threshold", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_ocean_water_mask_threshold", "get_ocean_water_mask_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spring_basin_threshold", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_spring_basin_threshold", "get_spring_basin_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spring_noise_threshold", PROPERTY_HINT_RANGE, "-1.0,1.0,0.01"), "set_spring_noise_threshold", "get_spring_noise_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spring_seed_mass", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_spring_seed_mass", "get_spring_seed_mass");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "oceanic_fraction", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_oceanic_fraction", "get_oceanic_fraction");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "n_points", PROPERTY_HINT_RANGE, "256,12000,1"), "set_n_points", "get_n_points");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "n_plates", PROPERTY_HINT_RANGE, "2,64,1"), "set_n_plates", "get_n_plates");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "border_warp", PROPERTY_HINT_RANGE, "0.0,0.35,0.005"), "set_border_warp", "get_border_warp");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "oceanic_fraction_influence", PROPERTY_HINT_RANGE, "0.0,1.5,0.05"), "set_oceanic_fraction_influence", "get_oceanic_fraction_influence");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continent_cohesion", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_continent_cohesion", "get_continent_cohesion");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continent_balance", PROPERTY_HINT_RANGE, "-0.25,0.25,0.01"), "set_continent_balance", "get_continent_balance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "land_coverage_target", PROPERTY_HINT_RANGE, "0.15,0.85,0.01"), "set_land_coverage_target", "get_land_coverage_target");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continent_size_scale", PROPERTY_HINT_RANGE, "0.35,3.0,0.05"), "set_continent_size_scale", "get_continent_size_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_shape_boost", PROPERTY_HINT_RANGE, "0.5,2.5,0.05"), "set_mountain_shape_boost", "get_mountain_shape_boost");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "coast_smoothing_strength", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_coast_smoothing_strength", "get_coast_smoothing_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "coast_smoothing_width", PROPERTY_HINT_RANGE, "500.0,12000.0,50.0"), "set_coast_smoothing_width", "get_coast_smoothing_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "beach_width_m", PROPERTY_HINT_RANGE, "100.0,8000.0,10.0"), "set_beach_width_m", "get_beach_width_m");

	ADD_GROUP("Climate", "climate_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "climate_equator_temp_c", PROPERTY_HINT_RANGE, "-20.0,60.0,0.5"), "set_climate_equator_temp_c", "get_climate_equator_temp_c");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "climate_polar_temp_c", PROPERTY_HINT_RANGE, "-60.0,30.0,0.5"), "set_climate_polar_temp_c", "get_climate_polar_temp_c");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "climate_ocean_humidity", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_climate_ocean_humidity", "get_climate_ocean_humidity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "climate_interior_humidity", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_climate_interior_humidity", "get_climate_interior_humidity");

	ADD_GROUP("Biomes", "biome_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_region_bias_strength", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_region_bias_strength", "get_biome_region_bias_strength");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "biome_region_min_cells", PROPERTY_HINT_RANGE, "1,32,1"), "set_biome_region_min_cells", "get_biome_region_min_cells");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "biome_region_max_cells", PROPERTY_HINT_RANGE, "1,64,1"), "set_biome_region_max_cells", "get_biome_region_max_cells");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_terrain_blend_strength", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_terrain_blend_strength", "get_biome_terrain_blend_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_spawn_desert", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_spawn_desert", "get_biome_spawn_desert");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_spawn_forest", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_spawn_forest", "get_biome_spawn_forest");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_spawn_tropical", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_spawn_tropical", "get_biome_spawn_tropical");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_spawn_tundra", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_spawn_tundra", "get_biome_spawn_tundra");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_spawn_grassland", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_spawn_grassland", "get_biome_spawn_grassland");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "biome_spawn_alpine", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_biome_spawn_alpine", "get_biome_spawn_alpine");

	ADD_GROUP("Noise", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "base_noise_amp", PROPERTY_HINT_RANGE, "0.0,2000.0,1.0"), "set_base_noise_amp", "get_base_noise_amp");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "base_noise_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_base_noise_freq", "get_base_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continent_noise_amp", PROPERTY_HINT_RANGE, "0.0,3000.0,1.0"), "set_continent_noise_amp", "get_continent_noise_amp");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "continent_noise_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_continent_noise_freq", "get_continent_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "plateau_noise_amp", PROPERTY_HINT_RANGE, "0.0,2000.0,1.0"), "set_plateau_noise_amp", "get_plateau_noise_amp");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "plateau_noise_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_plateau_noise_freq", "get_plateau_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "oceanic_noise_amp", PROPERTY_HINT_RANGE, "0.0,2000.0,1.0"), "set_oceanic_noise_amp", "get_oceanic_noise_amp");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "oceanic_noise_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_oceanic_noise_freq", "get_oceanic_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_noise_amp", PROPERTY_HINT_RANGE, "0.0,6000.0,1.0"), "set_mountain_noise_amp", "get_mountain_noise_amp");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mountain_noise_freq", PROPERTY_HINT_RANGE, "0.000001,0.01,0.000001,or_greater"), "set_mountain_noise_freq", "get_mountain_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "detail_noise_amp", PROPERTY_HINT_RANGE, "0.0,500.0,1.0"), "set_detail_noise_amp", "get_detail_noise_amp");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "detail_noise_freq", PROPERTY_HINT_RANGE, "0.000001,0.02,0.000001,or_greater"), "set_detail_noise_freq", "get_detail_noise_freq");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_ispc_diffusion"), "set_use_ispc_diffusion", "get_use_ispc_diffusion");

	ADD_GROUP("Erosion", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_erosion"), "set_use_erosion", "get_use_erosion");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "erosion_height_scale", PROPERTY_HINT_RANGE, "0.0,4.0,0.01"), "set_erosion_height_scale", "get_erosion_height_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "erosion_tile_size", PROPERTY_HINT_RANGE, "100.0,100000.0,1.0,or_greater"), "set_erosion_tile_size", "get_erosion_tile_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "erosion_strength", PROPERTY_HINT_RANGE, "0.0,1.0,0.001"), "set_erosion_strength", "get_erosion_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "erosion_detail", PROPERTY_HINT_RANGE, "0.01,4.0,0.01"), "set_erosion_detail", "get_erosion_detail");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "erosion_octaves", PROPERTY_HINT_RANGE, "0,12,1"), "set_erosion_octaves", "get_erosion_octaves");
}

#undef BIND_FLOAT
#undef BIND_INT
#undef BIND_BOOL
