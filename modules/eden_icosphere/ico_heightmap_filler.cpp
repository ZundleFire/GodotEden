#include "ico_heightmap_filler.h"
#include "core/math/math_funcs.h"
#include "core/templates/hash_map.h"

#ifdef _OPENMP
#include <omp.h>
#endif

// ══════════════════════════════════════════════════════════════════════════════
// IcoHeightmapFiller implementation
// ══════════════════════════════════════════════════════════════════════════════

IcoHeightmapFiller::IcoHeightmapFiller() {
	memset(grid_cells, 0, sizeof(grid_cells));
}

void IcoHeightmapFiller::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_planet_radius", "radius"), &IcoHeightmapFiller::set_planet_radius);
	ClassDB::bind_method(D_METHOD("get_planet_radius"), &IcoHeightmapFiller::get_planet_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "planet_radius"), "set_planet_radius", "get_planet_radius");

	ClassDB::bind_method(D_METHOD("set_border_warp", "warp"), &IcoHeightmapFiller::set_border_warp);
	ClassDB::bind_method(D_METHOD("get_border_warp"), &IcoHeightmapFiller::get_border_warp);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "border_warp"), "set_border_warp", "get_border_warp");

	ClassDB::bind_method(D_METHOD("set_mapper", "mapper"), &IcoHeightmapFiller::set_mapper);
	ClassDB::bind_method(D_METHOD("set_noise_layers", "base", "continent", "plateau", "oceanic", "mountain", "trench", "rift", "detail"),
		&IcoHeightmapFiller::set_noise_layers);
	ClassDB::bind_method(D_METHOD("set_warp_noises", "warp_x", "warp_y", "warp_z"),
		&IcoHeightmapFiller::set_warp_noises);
	ClassDB::bind_method(D_METHOD("set_tectonic_data", "voronoi_points", "voronoi_grid", "plate_id", "falloff", "bnd_type", "is_oceanic", "plate_elev_bias", "cross_plate_ang"),
		&IcoHeightmapFiller::set_tectonic_data);
	ClassDB::bind_method(D_METHOD("set_amplitudes", "base", "continent", "plateau", "oceanic", "mountain", "trench", "rift", "detail"),
		&IcoHeightmapFiller::set_amplitudes);
	ClassDB::bind_method(D_METHOD("set_half_widths", "mtn", "trench", "rift"),
		&IcoHeightmapFiller::set_half_widths);

	ClassDB::bind_method(D_METHOD("fill_tile", "face", "resolution"), &IcoHeightmapFiller::fill_tile);
	ClassDB::bind_method(D_METHOD("fill_all_tiles", "resolution"), &IcoHeightmapFiller::fill_all_tiles);
	ClassDB::bind_method(D_METHOD("fill_tile_range", "start", "end", "resolution"), &IcoHeightmapFiller::fill_tile_range);
}

// ── Setters ───────────────────────────────────────────────────────────────────

void IcoHeightmapFiller::set_planet_radius(float p_radius) { planet_radius = p_radius; }
float IcoHeightmapFiller::get_planet_radius() const { return planet_radius; }

void IcoHeightmapFiller::set_border_warp(float p_warp) { border_warp = p_warp; }
float IcoHeightmapFiller::get_border_warp() const { return border_warp; }

void IcoHeightmapFiller::set_mapper(Ref<IcosphereMapper> p_mapper) { mapper = p_mapper; }

void IcoHeightmapFiller::set_noise_layers(
		Ref<FastNoiseLite> p_base, Ref<FastNoiseLite> p_continent,
		Ref<FastNoiseLite> p_plateau, Ref<FastNoiseLite> p_oceanic,
		Ref<FastNoiseLite> p_mountain, Ref<FastNoiseLite> p_trench,
		Ref<FastNoiseLite> p_rift, Ref<FastNoiseLite> p_detail) {
	noise_base = p_base; noise_continent = p_continent;
	noise_plateau = p_plateau; noise_oceanic = p_oceanic;
	noise_mountain = p_mountain; noise_trench = p_trench;
	noise_rift = p_rift; noise_detail = p_detail;
}

void IcoHeightmapFiller::set_warp_noises(
		Ref<FastNoiseLite> p_warp_x, Ref<FastNoiseLite> p_warp_y,
		Ref<FastNoiseLite> p_warp_z) {
	noise_warp_x = p_warp_x; noise_warp_y = p_warp_y; noise_warp_z = p_warp_z;
}

void IcoHeightmapFiller::set_amplitudes(float p_base, float p_continent, float p_plateau,
		float p_oceanic, float p_mountain, float p_trench, float p_rift, float p_detail) {
	base_noise_amp = p_base; continent_noise_amp = p_continent;
	plateau_noise_amp = p_plateau; oceanic_noise_amp = p_oceanic;
	mountain_noise_amp = p_mountain; trench_noise_amp = p_trench;
	rift_noise_amp = p_rift; detail_noise_amp = p_detail;
}

void IcoHeightmapFiller::set_half_widths(float p_mtn, float p_trench, float p_rift) {
	mtn_half_width = p_mtn; trench_half_width = p_trench; rift_half_width = p_rift;
}

// ── Tectonic data ─────────────────────────────────────────────────────────────

void IcoHeightmapFiller::set_tectonic_data(
		PackedVector3Array p_voronoi_points,
		Array p_voronoi_grid,
		PackedInt32Array p_plate_id,
		PackedFloat32Array p_falloff,
		PackedByteArray p_bnd_type,
		PackedByteArray p_is_oceanic,
		PackedFloat32Array p_plate_elev_bias,
		PackedFloat32Array p_cross_plate_ang) {
	voronoi_points = p_voronoi_points;
	voronoi_count = voronoi_points.size();
	plate_id = p_plate_id;
	falloff = p_falloff;
	bnd_type = p_bnd_type;
	is_oceanic_pt = p_is_oceanic;
	plate_elev_bias = p_plate_elev_bias;
	cross_plate_ang = p_cross_plate_ang;

	grid_indices.clear();
	memset(grid_cells, 0, sizeof(grid_cells));

	ERR_FAIL_COND_MSG(p_voronoi_grid.size() != 6, "voronoi_grid must have 6 faces");

	for (int face = 0; face < 6; face++) {
		Array face_arr = p_voronoi_grid[face];
		ERR_FAIL_COND_MSG(face_arr.size() != GRID_RES * GRID_RES,
			vformat("voronoi_grid face %d must have %d cells", face, GRID_RES * GRID_RES));

		for (int cell = 0; cell < GRID_RES * GRID_RES; cell++) {
			PackedInt32Array cell_arr = face_arr[cell];
			int flat_idx = face * GRID_RES * GRID_RES + cell;
			grid_cells[flat_idx].start = grid_indices.size();
			grid_cells[flat_idx].count = cell_arr.size();
			const int *src = cell_arr.ptr();
			for (int i = 0; i < cell_arr.size(); i++) {
				grid_indices.push_back(src[i]);
			}
		}
	}
}

// ── Cubemap helpers (for Voronoi lookup — uses cube-map spatial grid) ─────────

void IcoHeightmapFiller::_point_to_face_uv(const Vector3 &p, int &r_face, int &r_cu, int &r_cv) const {
	float ax = Math::abs(p.x);
	float ay = Math::abs(p.y);
	float az = Math::abs(p.z);
	float s, t;

	if (ax >= ay && ax >= az) {
		if (p.x > 0.0f) { r_face = 0; s = -p.z / ax; t = -p.y / ax; }
		else { r_face = 1; s = p.z / ax; t = -p.y / ax; }
	} else if (ay >= ax && ay >= az) {
		if (p.y > 0.0f) { r_face = 2; s = p.x / ay; t = p.z / ay; }
		else { r_face = 3; s = p.x / ay; t = -p.z / ay; }
	} else {
		if (p.z > 0.0f) { r_face = 4; s = p.x / az; t = -p.y / az; }
		else { r_face = 5; s = -p.x / az; t = -p.y / az; }
	}

	r_cu = CLAMP((int)((s + 1.0f) * 0.5f * GRID_RES), 0, GRID_RES - 1);
	r_cv = CLAMP((int)((t + 1.0f) * 0.5f * GRID_RES), 0, GRID_RES - 1);
}

Vector3 IcoHeightmapFiller::_warp_dir(const Vector3 &dir) const {
	if (noise_warp_x.is_null()) return dir;
	float wx = noise_warp_x->get_noise_3d(dir.x, dir.y, dir.z);
	float wy = noise_warp_y->get_noise_3d(dir.x + 3.7f, dir.y + 1.3f, dir.z + 2.5f);
	float wz = noise_warp_z->get_noise_3d(dir.x - 2.1f, dir.y + 4.8f, dir.z - 0.9f);
	return (dir + Vector3(wx, wy, wz) * border_warp).normalized();
}

void IcoHeightmapFiller::_get_two_nearest(const Vector3 &dir, int &r_idx_a, int &r_idx_b,
		float &r_dot_a, float &r_dot_b) const {
	int face, cu, cv;
	_point_to_face_uv(dir, face, cu, cv);

	r_idx_a = 0; r_idx_b = 0;
	r_dot_a = -2.0f; r_dot_b = -2.0f;
	bool found = false;

	const Vector3 *pts = voronoi_points.ptr();
	const int *gi = grid_indices.ptr();

	for (int dv = -1; dv <= 1; dv++) {
		int nv = CLAMP(cv + dv, 0, GRID_RES - 1);
		for (int du = -1; du <= 1; du++) {
			int nu = CLAMP(cu + du, 0, GRID_RES - 1);
			int flat_idx = face * GRID_RES * GRID_RES + nv * GRID_RES + nu;
			const GridCell &gc = grid_cells[flat_idx];
			for (int i = 0; i < gc.count; i++) {
				int idx = gi[gc.start + i];
				float d = dir.dot(pts[idx]);
				if (d > r_dot_a) {
					r_dot_b = r_dot_a; r_idx_b = r_idx_a;
					r_dot_a = d; r_idx_a = idx;
					found = true;
				} else if (d > r_dot_b) {
					r_dot_b = d; r_idx_b = idx;
				}
			}
		}
	}

	if (found) return;

	for (int i = 0; i < voronoi_count; i++) {
		float d = dir.dot(pts[i]);
		if (d > r_dot_a) {
			r_dot_b = r_dot_a; r_idx_b = r_idx_a;
			r_dot_a = d; r_idx_a = i;
		} else if (d > r_dot_b) {
			r_dot_b = d; r_idx_b = i;
		}
	}
}

void IcoHeightmapFiller::_get_terrain_data(const Vector3 &unit_dir,
		float &r_falloff, float &r_oceanic, float &r_bias,
		int &r_bnd, float &r_border_dist_rad) const {
	Vector3 warped = _warp_dir(unit_dir);

	int idx_a, idx_b;
	float dot_a, dot_b;
	_get_two_nearest(warped, idx_a, idx_b, dot_a, dot_b);

	float ang_a = Math::acos(CLAMP(dot_a, -1.0f, 1.0f));
	float ang_b = Math::acos(CLAMP(dot_b, -1.0f, 1.0f));

	const float BW = 0.10f;
	float t = CLAMP(1.0f - (ang_b - ang_a) / BW, 0.0f, 1.0f);
	t = t * t * (3.0f - 2.0f * t);

	const float *fo = falloff.ptr();
	const uint8_t *oce = is_oceanic_pt.ptr();
	const int *pid = plate_id.ptr();
	const float *bias_arr = plate_elev_bias.ptr();
	const uint8_t *bnd_arr = bnd_type.ptr();
	const float *cpa = cross_plate_ang.ptr();

	r_falloff = Math::lerp(fo[idx_a], fo[idx_b], t);
	r_oceanic = Math::lerp((float)oce[idx_a], (float)oce[idx_b], t);

	int pa = pid[idx_a], pb = pid[idx_b];
	r_bias = Math::lerp(bias_arr[pa], bias_arr[pb], t);
	r_bnd = bnd_arr[idx_a];

	if (pa != pb) {
		r_border_dist_rad = (ang_b - ang_a) * 0.5f;
	} else {
		r_border_dist_rad = Math::lerp(cpa[idx_a] - ang_a, cpa[idx_b] - ang_b, t) * 0.5f;
	}
}

float IcoHeightmapFiller::_compute_base_height(const Vector3 &world_pos, const Vector3 &unit_dir) const {
	float td_falloff, td_oceanic, td_bias;
	int td_bnd;
	float td_border_dist_rad;
	_get_terrain_data(unit_dir, td_falloff, td_oceanic, td_bias, td_bnd, td_border_dist_rad);

	float border_dist_m = td_border_dist_rad * planet_radius;
	float height = td_bias * td_falloff;

	height += noise_base->get_noise_3d(world_pos.x, world_pos.y, world_pos.z) * base_noise_amp;

	float land_factor = (1.0f - td_oceanic) * td_falloff;
	if (land_factor > 0.01f) {
		height += noise_continent->get_noise_3d(world_pos.x, world_pos.y, world_pos.z) * continent_noise_amp * land_factor;
	}

	float interior_factor = land_factor * td_falloff;
	if (interior_factor > 0.01f) {
		height += noise_plateau->get_noise_3d(world_pos.x, world_pos.y, world_pos.z) * plateau_noise_amp * interior_factor;
	}

	if (td_oceanic > 0.01f) {
		height += noise_oceanic->get_noise_3d(world_pos.x, world_pos.y, world_pos.z) * oceanic_noise_amp * td_oceanic;
	}

	float mtn_mask = 0.0f;
	if (td_bnd == 1 || td_bnd == 5) {
		mtn_mask = 1.0f - _smoothstep(0.0f, mtn_half_width, border_dist_m);
		mtn_mask = mtn_mask * mtn_mask * mtn_mask;
	}
	if (mtn_mask > 0.01f) {
		height += noise_mountain->get_noise_3d(world_pos.x, world_pos.y, world_pos.z) * mountain_noise_amp * mtn_mask;
	}

	float trench_mask = 0.0f;
	if (td_bnd == 2) {
		trench_mask = 1.0f - _smoothstep(0.0f, trench_half_width, border_dist_m);
	}
	if (trench_mask > 0.01f) {
		height -= noise_trench->get_noise_3d(world_pos.x, world_pos.y, world_pos.z) * trench_noise_amp * trench_mask;
	}

	float rift_mask = 0.0f;
	if (td_bnd == 3 || td_bnd == 4) {
		rift_mask = 1.0f - _smoothstep(0.0f, rift_half_width, border_dist_m);
	}
	if (rift_mask > 0.01f) {
		height -= Math::abs(noise_rift->get_noise_3d(world_pos.x, world_pos.y, world_pos.z)) * rift_noise_amp * rift_mask;
	}

	float detail_n = noise_detail->get_noise_3d(world_pos.x, world_pos.y, world_pos.z);
	float sea_damp = CLAMP(Math::abs(height) / 200.0f, 0.1f, 1.0f);
	height += detail_n * detail_noise_amp * sea_damp;

	if (height > 0.0f) {
		height = MAX(height, 5.0f);
	}

	return height;
}

// ── Core: fill a single triangular tile ───────────────────────────────────────

PackedFloat32Array IcoHeightmapFiller::fill_tile(int p_face, int p_resolution) {
	ERR_FAIL_COND_V_MSG(mapper.is_null(), PackedFloat32Array(), "Mapper not set. Call set_mapper() first.");
	ERR_FAIL_COND_V_MSG(noise_base.is_null(), PackedFloat32Array(), "Noise layers not set.");
	ERR_FAIL_COND_V_MSG(voronoi_count == 0, PackedFloat32Array(), "Tectonic data not set.");

	int R = p_resolution;
	int total_samples = (R + 1) * (R + 2) / 2;
	PackedFloat32Array result;
	result.resize(total_samples);
	float *dst = result.ptrw();

	// Iterate over the triangular grid: row i has (R+1-i) samples.
	// Each sample (i,j) corresponds to barycentric coords (u=i/R, v=j/R, w=1-u-v).
	//
	// The key insight: vertices along edges are shared with neighbouring tiles,
	// providing automatic seamless stitching when both tiles compute the SAME
	// world position for shared edge samples.

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 4) num_threads(8)
#endif
	for (int i = 0; i <= R; i++) {
		int row_start = i * (R + 1) - i * (i - 1) / 2;
		int row_count = R + 1 - i;

		for (int j = 0; j < row_count; j++) {
			// Barycentric coordinates within this tile.
			float u = (float)i / (float)R;
			float v = (float)j / (float)R;

			// Map to unit-sphere direction via the mapper.
			Vector3 unit_dir = mapper->face_uv_to_dir(p_face, u, v);
			Vector3 world_pos = unit_dir * planet_radius;

			dst[row_start + j] = _compute_base_height(world_pos, unit_dir);
		}
	}

	return result;
}

// ── Batch fill all tiles ──────────────────────────────────────────────────────

Dictionary IcoHeightmapFiller::fill_all_tiles(int p_resolution) {
	ERR_FAIL_COND_V_MSG(mapper.is_null(), Dictionary(), "Mapper not set.");

	int n_faces = mapper->get_face_count();
	Dictionary result;

	// Fill tiles in parallel at the tile level (each tile's internal loop
	// is also parallelised, so we do tile-level parallelism only if we have
	// more cores than needed for the inner loop).
	for (int f = 0; f < n_faces; f++) {
		result[f] = fill_tile(f, p_resolution);
	}

	return result;
}

Dictionary IcoHeightmapFiller::fill_tile_range(int p_start, int p_end, int p_resolution) {
	ERR_FAIL_COND_V_MSG(mapper.is_null(), Dictionary(), "Mapper not set.");

	int n_faces = mapper->get_face_count();
	p_end = MIN(p_end, n_faces);
	Dictionary result;

	for (int f = p_start; f < p_end; f++) {
		result[f] = fill_tile(f, p_resolution);
	}

	return result;
}
