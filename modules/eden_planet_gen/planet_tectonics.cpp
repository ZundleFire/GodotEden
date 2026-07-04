#include "planet_tectonics.h"
#include "core/math/math_funcs.h"
#include "core/variant/variant.h"

// Port of plate_tectonics.gd (910 lines) — 6 generation steps + terrain query.

PlanetTectonics::PlanetTectonics() {
}

// ── Configuration ────────────────────────────────────────────────────────────

void PlanetTectonics::configure(const Dictionary &p_params) {
	if (p_params.has("n_points")) n_points = p_params["n_points"];
	if (p_params.has("n_plates")) n_plates = p_params["n_plates"];
	if (p_params.has("plate_freq")) plate_freq = p_params["plate_freq"];
	if (p_params.has("plate_octaves")) plate_octaves = p_params["plate_octaves"];
	if (p_params.has("plate_lacunarity")) plate_lacunarity = p_params["plate_lacunarity"];
	if (p_params.has("plate_gain")) plate_gain = p_params["plate_gain"];
	if (p_params.has("oceanic_fraction")) oceanic_fraction = p_params["oceanic_fraction"];
	if (p_params.has("border_hops")) border_hops = p_params["border_hops"];
	if (p_params.has("collision_threshold")) collision_threshold = p_params["collision_threshold"];
	if (p_params.has("border_warp")) border_warp = p_params["border_warp"];
	if (p_params.has("warp_freq")) warp_freq = p_params["warp_freq"];
}

// ── Generation ───────────────────────────────────────────────────────────────

void PlanetTectonics::generate(int p_seed) {
	RandomNumberGenerator rng;
	rng.set_seed(p_seed);

	voronoi.generate(n_points, p_seed);

	// Domain-warp noises.
	noise_warp_x.instantiate();
	noise_warp_x->set_noise_type(FastNoiseLite::TYPE_SIMPLEX_SMOOTH);
	noise_warp_x->set_seed(rng.randi());
	noise_warp_x->set_frequency(warp_freq);
	noise_warp_x->set_fractal_type(FastNoiseLite::FRACTAL_FBM);
	noise_warp_x->set_fractal_octaves(2);

	noise_warp_y.instantiate();
	noise_warp_y->set_noise_type(FastNoiseLite::TYPE_SIMPLEX_SMOOTH);
	noise_warp_y->set_seed(rng.randi());
	noise_warp_y->set_frequency(warp_freq);
	noise_warp_y->set_fractal_type(FastNoiseLite::FRACTAL_FBM);
	noise_warp_y->set_fractal_octaves(2);

	noise_warp_z.instantiate();
	noise_warp_z->set_noise_type(FastNoiseLite::TYPE_SIMPLEX_SMOOTH);
	noise_warp_z->set_seed(rng.randi());
	noise_warp_z->set_frequency(warp_freq);
	noise_warp_z->set_fractal_type(FastNoiseLite::FRACTAL_FBM);
	noise_warp_z->set_fractal_octaves(2);

	_build_neighbour_graph();
	_assign_plate_ids(rng);
	_bake_cross_plate_dist();
	_compute_falloff();
	_classify_boundaries(rng);
	_bake_elevation();
}

// ── Step 1: Assign plate IDs via per-plate noise competition ─────────────────

void PlanetTectonics::_assign_plate_ids(RandomNumberGenerator &rng) {
	plate_id.resize(n_points);
	plate_oceanic.resize(n_plates);
	plate_movement.resize(n_plates);
	plate_elev_bias.resize(n_plates);

	// Build one noise per plate with unique seed + random offset.
	Vector<Ref<FastNoiseLite>> plate_noises;
	Vector<Vector3> plate_offsets;
	plate_noises.resize(n_plates);
	plate_offsets.resize(n_plates);

	for (int p = 0; p < n_plates; p++) {
		Ref<FastNoiseLite> n;
		n.instantiate();
		n->set_noise_type(FastNoiseLite::TYPE_SIMPLEX_SMOOTH);
		n->set_seed(rng.randi());
		n->set_frequency(plate_freq);
		n->set_fractal_type(FastNoiseLite::FRACTAL_FBM);
		n->set_fractal_octaves(plate_octaves);
		n->set_fractal_lacunarity(plate_lacunarity);
		n->set_fractal_gain(plate_gain);
		plate_noises.write[p] = n;
		plate_offsets.write[p] = Vector3(
				rng.randf_range(-10.0f, 10.0f),
				rng.randf_range(-10.0f, 10.0f),
				rng.randf_range(-10.0f, 10.0f));
	}

	// Assign each Voronoi point to the plate whose noise is highest.
	for (int i = 0; i < n_points; i++) {
		const Vector3 &pt = voronoi.get_point(i);
		float best_val = -2.0f;
		int best_p = 0;
		for (int p = 0; p < n_plates; p++) {
			const Vector3 &op = plate_offsets[p];
			float v = plate_noises[p]->get_noise_3d(pt.x + op.x, pt.y + op.y, pt.z + op.z);
			if (v > best_val) {
				best_val = v;
				best_p = p;
			}
		}
		plate_id.write[i] = best_p;
	}

	// Assign oceanic flags — Fisher-Yates shuffle.
	int n_oceanic = int(Math::round(float(n_plates) * oceanic_fraction));
	Vector<uint8_t> flags;
	flags.resize(n_plates);
	for (int p = 0; p < n_plates; p++) {
		flags.write[p] = (p < n_oceanic) ? 1 : 0;
	}
	for (int p = n_plates - 1; p > 0; p--) {
		int q = rng.randi() % (p + 1);
		uint8_t tmp = flags[p];
		flags.write[p] = flags[q];
		flags.write[q] = tmp;
	}
	for (int p = 0; p < n_plates; p++) {
		plate_oceanic.write[p] = flags[p];
	}

	// Random tangent drift vector per plate.
	for (int p = 0; p < n_plates; p++) {
		// Centroid of this plate's Voronoi points.
		Vector3 rep_pt = Vector3();
		int count = 0;
		for (int i = 0; i < n_points; i++) {
			if (plate_id[i] == p) {
				rep_pt += voronoi.get_point(i);
				count++;
			}
		}
		if (count == 0) {
			rep_pt = Vector3(rng.randf_range(-1.0f, 1.0f),
					rng.randf_range(-1.0f, 1.0f),
					rng.randf_range(-1.0f, 1.0f));
		}
		rep_pt = rep_pt.normalized();

		// Random direction in the tangent plane at the plate centroid.
		Vector3 arb = (Math::abs(rep_pt.y) < 0.8f) ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
		Vector3 tang1 = (arb - rep_pt * arb.dot(rep_pt)).normalized();
		Vector3 tang2 = rep_pt.cross(tang1);
		float drift_angle = rng.randf() * Math::TAU;
		plate_movement.write[p] = (tang1 * Math::cos(drift_angle) + tang2 * Math::sin(drift_angle)).normalized();

		// Base elevation bias.
		if (plate_oceanic[p] == 1) {
			plate_elev_bias.write[p] = rng.randf_range(-600.0f, -150.0f);
		} else {
			plate_elev_bias.write[p] = rng.randf_range(60.0f, 200.0f);
		}
	}
}

// ── Step 2: Build adjacency graph ────────────────────────────────────────────

void PlanetTectonics::_build_neighbour_graph() {
	float cos_thresh = Math::cos(5.2f / Math::sqrt(float(n_points)));

	// First pass: count neighbours per point.
	Vector<int> counts;
	counts.resize(n_points);
	for (int i = 0; i < n_points; i++) {
		counts.write[i] = 0;
	}

	// Temporary storage: collect all edges.
	Vector<int> edges_a;
	Vector<int> edges_b;

	const Vector<Vector3> &pts = voronoi.get_points();
	for (int i = 0; i < n_points; i++) {
		for (int j = i + 1; j < n_points; j++) {
			if (pts[i].dot(pts[j]) >= cos_thresh) {
				edges_a.push_back(i);
				edges_b.push_back(j);
				counts.write[i]++;
				counts.write[j]++;
			}
		}
	}

	// Build offset table (prefix sum).
	nbr_offsets.resize(n_points + 1);
	nbr_offsets.write[0] = 0;
	for (int i = 0; i < n_points; i++) {
		nbr_offsets.write[i + 1] = nbr_offsets[i] + counts[i];
		counts.write[i] = 0; // Reset for fill pass.
	}

	// Fill neighbour data.
	int total_nbrs = nbr_offsets[n_points];
	nbr_data.resize(total_nbrs);
	for (int e = 0; e < edges_a.size(); e++) {
		int a = edges_a[e];
		int b = edges_b[e];
		nbr_data.write[nbr_offsets[a] + counts[a]] = b;
		counts.write[a]++;
		nbr_data.write[nbr_offsets[b] + counts[b]] = a;
		counts.write[b]++;
	}
}

// ── Step 3: Compute per-point falloff ────────────────────────────────────────

void PlanetTectonics::_compute_falloff() {
	falloff.resize(n_points);

	const float approx_cell_rad = 5.2f / Math::sqrt(MAX(float(n_points), 1.0f));
	const float border_width_rad = MAX(approx_cell_rad * MAX(float(border_hops), 1.0f), 0.015f);

	for (int i = 0; i < n_points; i++) {
		const float border_dist_rad = MAX(cross_plate_ang[i] - approx_cell_rad, 0.0f);
		const float raw = CLAMP(border_dist_rad / border_width_rad, 0.0f, 1.0f);
		falloff.write[i] = raw * raw * (3.0f - 2.0f * raw);
	}

	// Light neighbour relaxation to reduce Voronoi seam imprinting without
	// erasing the macro tectonic structure.
	Vector<float> smoothed = falloff;
	for (int i = 0; i < n_points; i++) {
		float acc = falloff[i] * 2.3f;
		float total_w = 2.3f;
		const int pi = plate_id[i];
		for (int k = nbr_offsets[i]; k < nbr_offsets[i + 1]; k++) {
			const int nb = nbr_data[k];
			float w = (plate_id[nb] == pi) ? 0.95f : 0.55f;
			const float falloff_delta = CLAMP(Math::abs(falloff[nb] - falloff[i]) * 2.0f, 0.0f, 1.0f);
			w *= Math::lerp(1.0f, 0.65f, falloff_delta);
			acc += falloff[nb] * w;
			total_w += w;
		}
		smoothed.write[i] = acc / MAX(total_w, 0.0001f);
	}
	falloff = smoothed;
}

// ── Step 4: Classify boundaries ──────────────────────────────────────────────

void PlanetTectonics::_classify_boundaries(RandomNumberGenerator &rng) {
	bnd_type.resize(n_points);
	is_oceanic_pt.resize(n_points);

	// Pass 1: classify immediate border ring.
	for (int i = 0; i < n_points; i++) {
		int pi = plate_id[i];
		is_oceanic_pt.write[i] = plate_oceanic[pi];
		bnd_type.write[i] = BND_INTERIOR;

		bool oc_i = (plate_oceanic[pi] == 1);
		float best_c = -1e30f;
		bool best_oc_j = false;
		bool has_cross = false;

		for (int k = nbr_offsets[i]; k < nbr_offsets[i + 1]; k++) {
			int j = nbr_data[k];
			int pj = plate_id[j];
			if (pj == pi) continue;
			has_cross = true;
			float c = _compression(pi, pj, i, j);
			if (c > best_c) {
				best_c = c;
				best_oc_j = (plate_oceanic[pj] == 1);
			}
		}

		if (!has_cross) continue;

		bool colliding = best_c > collision_threshold;
		bool rifting = best_c < -collision_threshold;

		if (!oc_i && !best_oc_j) {
			// Continental-Continental
			if (colliding) bnd_type.write[i] = BND_MOUNTAIN;
			else if (rifting) bnd_type.write[i] = BND_RIFT;
			else bnd_type.write[i] = BND_MOUNTAIN;
		} else if (oc_i && best_oc_j) {
			// Oceanic-Oceanic
			if (colliding) bnd_type.write[i] = BND_ISLAND_ARC;
			else if (rifting) bnd_type.write[i] = BND_RIFT;
			else bnd_type.write[i] = BND_RIFT;
		} else if (oc_i) {
			// OC — oceanic side
			if (colliding) bnd_type.write[i] = BND_TRENCH;
			else bnd_type.write[i] = BND_RIFT;
		} else {
			// OC — continental side
			if (colliding) bnd_type.write[i] = BND_MOUNTAIN;
			else bnd_type.write[i] = BND_PASSIVE;
		}
	}

	// Pass 2: flood-fill boundary type to all points within the falloff zone.
	bool changed = true;
	while (changed) {
		changed = false;
		for (int i = 0; i < n_points; i++) {
			if (bnd_type[i] != BND_INTERIOR) continue;
			if (falloff[i] >= 1.0f) continue;
			for (int k = nbr_offsets[i]; k < nbr_offsets[i + 1]; k++) {
				int j = nbr_data[k];
				if (bnd_type[j] != BND_INTERIOR && falloff[j] < falloff[i]) {
					bnd_type.write[i] = bnd_type[j];
					changed = true;
					break;
				}
			}
		}
	}
}

// ── Step 5: Bake elevation ───────────────────────────────────────────────────

void PlanetTectonics::_bake_elevation() {
	elevation.resize(n_points);
	for (int i = 0; i < n_points; i++) {
		int pi = plate_id[i];
		float base = plate_elev_bias[pi];
		float f = falloff[i];
		switch (bnd_type[i]) {
			case BND_MOUNTAIN: elevation.write[i] = base + Math::lerp(2000.0f, 0.0f, f); break;
			case BND_TRENCH: elevation.write[i] = base + Math::lerp(-2500.0f, 0.0f, f); break;
			case BND_RIFT: elevation.write[i] = base + Math::lerp(-500.0f, 0.0f, f); break;
			case BND_ISLAND_ARC: elevation.write[i] = base + Math::lerp(900.0f, 0.0f, f); break;
			case BND_PASSIVE: elevation.write[i] = base + Math::lerp(300.0f, 0.0f, f); break;
			default: elevation.write[i] = base; break;
		}
	}
}

// ── Step 6: Pre-bake cross-plate angular distances ───────────────────────────

void PlanetTectonics::_bake_cross_plate_dist() {
	cross_plate_ang.resize(n_points);
	const Vector<Vector3> &pts = voronoi.get_points();
	for (int i = 0; i < n_points; i++) {
		int pi = plate_id[i];
		float best_dot = -2.0f;
		for (int j = 0; j < n_points; j++) {
			if (plate_id[j] == pi) continue;
			float d = pts[i].dot(pts[j]);
			if (d > best_dot) {
				best_dot = d;
			}
		}
		if (best_dot > -2.0f) {
			cross_plate_ang.write[i] = Math::acos(CLAMP(best_dot, -1.0f, 1.0f));
		} else {
			cross_plate_ang.write[i] = Math::PI;
		}
	}
}

// ── Helpers ──────────────────────────────────────────────────────────────────

float PlanetTectonics::_compression(int pa, int pb, int ia, int ib) const {
	const float DT = 0.01f;
	const Vector3 &pos_a = voronoi.get_point(ia);
	const Vector3 &pos_b = voronoi.get_point(ib);
	return pos_a.distance_to(pos_b) -
		   (pos_a + plate_movement[pa] * DT).distance_to(pos_b + plate_movement[pb] * DT);
}

Vector3 PlanetTectonics::_warp_dir(const Vector3 &dir) const {
	float wx = noise_warp_x->get_noise_3d(dir.x, dir.y, dir.z);
	float wy = noise_warp_y->get_noise_3d(dir.x + 3.7f, dir.y + 1.3f, dir.z + 2.5f);
	float wz = noise_warp_z->get_noise_3d(dir.x - 2.1f, dir.y + 4.8f, dir.z - 0.9f);
	return (dir + Vector3(wx, wy, wz) * border_warp).normalized();
}

// ── Public query API ─────────────────────────────────────────────────────────

PlanetTectonics::TerrainData PlanetTectonics::get_terrain_data(const Vector3 &p_dir) const {
	TerrainData td;

	const Vector3 warped = _warp_dir(p_dir);
	int idx_a, idx_b;
	float dot_a, dot_b;
	voronoi.get_two_nearest(warped, idx_a, idx_b, dot_a, dot_b);

	const float ang_a = Math::acos(CLAMP(dot_a, -1.0f, 1.0f));
	const float ang_b = Math::acos(CLAMP(dot_b, -1.0f, 1.0f));
	const float SAMPLE_BLEND_RADIUS = 0.16f;
	const float BW = MAX(SAMPLE_BLEND_RADIUS * 0.8f, 0.10f);
	float t = CLAMP(1.0f - (ang_b - ang_a) / BW, 0.0f, 1.0f);
	t = t * t * (3.0f - 2.0f * t);

	const float fa = falloff[idx_a];
	const float fb = falloff[idx_b];
	const float oa = float(is_oceanic_pt[idx_a]);
	const float ob = float(is_oceanic_pt[idx_b]);
	const int pa = plate_id[idx_a];
	const int pb = plate_id[idx_b];
	const float ba = plate_elev_bias[pa];
	const float bb = plate_elev_bias[pb];

	td.falloff_s = Math::lerp(fa, fb, t);
	td.oceanic_s = Math::lerp(oa, ob, t);
	td.bias_s = Math::lerp(ba, bb, t);
	td.bnd_type_a = bnd_type[idx_a];
	td.bnd_type_b = bnd_type[idx_b];
	td.falloff_a = fa;
	td.falloff_b = fb;
	td.oceanic_a = oa;
	td.oceanic_b = ob;
	td.bias_a = ba;
	td.bias_b = bb;
	td.t = t;

	// Smooth across the nearest points and their neighbors so the tectonic
	// fields do not imprint obvious Voronoi cells into the runtime terrain.
	Vector<int> sample_indices;
	auto append_unique = [&](int p_index) {
		for (int i = 0; i < sample_indices.size(); i++) {
			if (sample_indices[i] == p_index) {
				return;
			}
		}
		sample_indices.push_back(p_index);
	};
	append_unique(idx_a);
	append_unique(idx_b);
	for (int root_i = 0; root_i < 2; root_i++) {
		const int root_idx = (root_i == 0) ? idx_a : idx_b;
		for (int k = nbr_offsets[root_idx]; k < nbr_offsets[root_idx + 1]; k++) {
			append_unique(nbr_data[k]);
		}
	}

	const float blend_radius = MAX(SAMPLE_BLEND_RADIUS, (ang_b - ang_a) * 3.0f);
	const float dot_cutoff = Math::cos(CLAMP(blend_radius, 0.03f, 0.45f));
	const float inv_dot_span = 1.0f / MAX(1.0f - dot_cutoff, 0.0001f);
	float total_w = 0.0f;
	float falloff_acc = 0.0f;
	float oceanic_acc = 0.0f;
	float bias_acc = 0.0f;
	float bnd_weights[8] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
	const Vector<Vector3> &pts = voronoi.get_points();

	for (int i = 0; i < sample_indices.size(); i++) {
		const int sample_idx = sample_indices[i];
		const float dot_v = CLAMP(warped.dot(pts[sample_idx]), -1.0f, 1.0f);
		float w = CLAMP((dot_v - dot_cutoff) * inv_dot_span, 0.0f, 1.0f);
		w = w * w * (3.0f - 2.0f * w);
		if (w <= 0.0001f) {
			continue;
		}
		total_w += w;
		falloff_acc += falloff[sample_idx] * w;
		oceanic_acc += float(is_oceanic_pt[sample_idx]) * w;
		bias_acc += plate_elev_bias[plate_id[sample_idx]] * w;
		const int bt = int(bnd_type[sample_idx]);
		if (bt >= 0 && bt < 8) {
			bnd_weights[bt] += w;
		}
	}

	if (total_w > 0.0001f) {
		td.falloff_s = falloff_acc / total_w;
		td.oceanic_s = oceanic_acc / total_w;
		td.bias_s = bias_acc / total_w;
		float best_type_weight = -1.0f;
		for (int i = 0; i < 8; i++) {
			if (bnd_weights[i] > best_type_weight) {
				best_type_weight = bnd_weights[i];
				td.bnd_type_a = i;
			}
		}
	}

	// Continuous border distance to the nearest plate boundary. Clamp at zero so
	// interpolation cannot go negative and falsely trigger inland edge masks.
	if (pa != pb) {
		td.border_dist_rad = MAX((ang_b - ang_a) * 0.5f, 0.0f);
	} else {
		const float cpa = cross_plate_ang[idx_a];
		const float cpb = cross_plate_ang[idx_b];
		const float da = MAX(cpa - ang_a, 0.0f);
		const float db = MAX(cpb - ang_b, 0.0f);
		td.border_dist_rad = MAX(Math::lerp(da, db, t) * 0.5f, 0.0f);
	}

	return td;
}

Array PlanetTectonics::get_terrain_data_gd(const Vector3 &p_dir) const {
	TerrainData td = get_terrain_data(p_dir);
	Array result;
	result.resize(13);
	result[0] = td.falloff_s;
	result[1] = td.oceanic_s;
	result[2] = td.bias_s;
	result[3] = td.bnd_type_a;
	result[4] = td.border_dist_rad;
	result[5] = td.falloff_a;
	result[6] = td.falloff_b;
	result[7] = td.oceanic_a;
	result[8] = td.oceanic_b;
	result[9] = td.bias_a;
	result[10] = td.bias_b;
	result[11] = td.t;
	result[12] = td.bnd_type_b;
	return result;
}

// ── Bindings ─────────────────────────────────────────────────────────────────

void PlanetTectonics::_bind_methods() {
	ClassDB::bind_method(D_METHOD("configure", "params"), &PlanetTectonics::configure);
	ClassDB::bind_method(D_METHOD("generate", "seed"), &PlanetTectonics::generate);
	ClassDB::bind_method(D_METHOD("get_terrain_data_gd", "direction"), &PlanetTectonics::get_terrain_data_gd);

	BIND_ENUM_CONSTANT(BND_INTERIOR);
	BIND_ENUM_CONSTANT(BND_MOUNTAIN);
	BIND_ENUM_CONSTANT(BND_TRENCH);
	BIND_ENUM_CONSTANT(BND_RIFT);
	BIND_ENUM_CONSTANT(BND_PASSIVE);
	BIND_ENUM_CONSTANT(BND_ISLAND_ARC);
	BIND_ENUM_CONSTANT(BND_OCEAN_INT);
	BIND_ENUM_CONSTANT(BND_XFORM);
}
