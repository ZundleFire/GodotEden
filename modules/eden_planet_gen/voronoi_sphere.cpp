#include "voronoi_sphere.h"
#include "core/math/math_funcs.h"
#include "core/math/random_number_generator.h"

// Port of voronoi_sphere.gd — Fibonacci lattice + cubemap spatial grid.

VoronoiSphere::VoronoiSphere() {
	memset(grid_cells, 0, sizeof(grid_cells));
}

// ── Generation ───────────────────────────────────────────────────────────────

void VoronoiSphere::generate(int p_num_points, int p_seed) {
	points.resize(p_num_points);

	RandomNumberGenerator rng;
	rng.set_seed(p_seed);

	// Golden angle in radians: PI * (3 - sqrt(5))
	const float PHI = 2.39996322972865332f;

	for (int i = 0; i < p_num_points; i++) {
		// Fibonacci sphere: evenly spaced in area.
		float y = 1.0f - (float(i) / float(p_num_points - 1)) * 2.0f;
		float radius = Math::sqrt(MAX(1.0f - y * y, 0.0f));
		float theta = PHI * float(i);

		// Small random jitter to break up spiral artefacts.
		float jitter = 0.4f / Math::sqrt(float(p_num_points));
		y = CLAMP(y + rng.randf_range(-jitter, jitter), -1.0f, 1.0f);
		theta += rng.randf_range(-jitter, jitter);

		float x = Math::cos(theta) * radius;
		float z = Math::sin(theta) * radius;
		points.write[i] = Vector3(x, y, z).normalized();
	}

	_build_grid();
}

int VoronoiSphere::get_num_points() const {
	return points.size();
}

// ── Cubemap face projection ──────────────────────────────────────────────────
// Maps a direction to (face, u_cell, v_cell) in the spatial grid.

void VoronoiSphere::_dir_to_face_uv(const Vector3 &p_dir, int &r_face, float &r_u, float &r_v) const {
	float ax = Math::abs(p_dir.x);
	float ay = Math::abs(p_dir.y);
	float az = Math::abs(p_dir.z);
	float s, t;

	if (ax >= ay && ax >= az) {
		if (p_dir.x > 0.0f) {
			r_face = 0;
			s = -p_dir.z / ax;
			t = -p_dir.y / ax;
		} else {
			r_face = 1;
			s = p_dir.z / ax;
			t = -p_dir.y / ax;
		}
	} else if (ay >= ax && ay >= az) {
		if (p_dir.y > 0.0f) {
			r_face = 2;
			s = p_dir.x / ay;
			t = p_dir.z / ay;
		} else {
			r_face = 3;
			s = p_dir.x / ay;
			t = -p_dir.z / ay;
		}
	} else {
		if (p_dir.z > 0.0f) {
			r_face = 4;
			s = p_dir.x / az;
			t = -p_dir.y / az;
		} else {
			r_face = 5;
			s = -p_dir.x / az;
			t = -p_dir.y / az;
		}
	}

	// s, t in [-1, 1] → [0, GRID_RES-1]
	r_u = CLAMP((s + 1.0f) * 0.5f * GRID_RES, 0.0f, float(GRID_RES - 1));
	r_v = CLAMP((t + 1.0f) * 0.5f * GRID_RES, 0.0f, float(GRID_RES - 1));
}

// ── Spatial grid build ───────────────────────────────────────────────────────

void VoronoiSphere::_build_grid() {
	// Use a flat array with start/count per cell (same pattern as voxel_block_filler).
	// First pass: count points per cell.
	memset(grid_cells, 0, sizeof(grid_cells));

	const int n = points.size();
	for (int i = 0; i < n; i++) {
		int face;
		float u, v;
		_dir_to_face_uv(points[i], face, u, v);
		int cu = int(u);
		int cv = int(v);
		int cell_idx = face * GRID_RES * GRID_RES + cv * GRID_RES + cu;
		grid_cells[cell_idx].count++;
	}

	// Compute start offsets (prefix sum).
	int total = 0;
	for (int i = 0; i < 6 * GRID_RES * GRID_RES; i++) {
		grid_cells[i].start = total;
		total += grid_cells[i].count;
		grid_cells[i].count = 0; // Reset for second pass.
	}

	// Second pass: fill indices.
	grid_indices.resize(total);
	for (int i = 0; i < n; i++) {
		int face;
		float u, v;
		_dir_to_face_uv(points[i], face, u, v);
		int cu = int(u);
		int cv = int(v);
		int cell_idx = face * GRID_RES * GRID_RES + cv * GRID_RES + cu;
		grid_indices.write[grid_cells[cell_idx].start + grid_cells[cell_idx].count] = i;
		grid_cells[cell_idx].count++;
	}
}

// ── Point access ─────────────────────────────────────────────────────────────

const Vector3 &VoronoiSphere::get_point(int p_index) const {
	return points[p_index];
}

const Vector<Vector3> &VoronoiSphere::get_points() const {
	return points;
}

// ── Nearest-point query ──────────────────────────────────────────────────────
// Checks the query cell + its 3×3 neighbourhood on the same cube face.

int VoronoiSphere::get_nearest(const Vector3 &p_dir) const {
	int face;
	float u, v;
	_dir_to_face_uv(p_dir, face, u, v);
	int cu = int(u);
	int cv = int(v);

	int best_idx = 0;
	float best_dot = -2.0f;
	bool found = false;

	for (int dv = -1; dv <= 1; dv++) {
		int nv = CLAMP(cv + dv, 0, GRID_RES - 1);
		for (int du = -1; du <= 1; du++) {
			int nu = CLAMP(cu + du, 0, GRID_RES - 1);
			int cell_idx = face * GRID_RES * GRID_RES + nv * GRID_RES + nu;
			const GridCell &cell = grid_cells[cell_idx];
			for (int k = 0; k < cell.count; k++) {
				int idx = grid_indices[cell.start + k];
				float d = p_dir.dot(points[idx]);
				if (d > best_dot) {
					best_dot = d;
					best_idx = idx;
					found = true;
				}
			}
		}
	}

	if (found) {
		return best_idx;
	}

	// Fallback: O(N) scan (should never reach here with GRID_RES >= 4).
	const int n = points.size();
	for (int i = 0; i < n; i++) {
		float d = p_dir.dot(points[i]);
		if (d > best_dot) {
			best_dot = d;
			best_idx = i;
		}
	}
	return best_idx;
}

// ── Two-nearest query ────────────────────────────────────────────────────────

void VoronoiSphere::get_two_nearest(const Vector3 &p_dir, int &r_nearest, int &r_second,
		float &r_best_dot, float &r_second_dot) const {
	int face;
	float u, v;
	_dir_to_face_uv(p_dir, face, u, v);
	int cu = int(u);
	int cv = int(v);

	int best_idx = 0;
	float best_dot = -2.0f;
	int second_idx = 0;
	float second_dot = -2.0f;
	bool found = false;

	for (int dv = -1; dv <= 1; dv++) {
		int nv = CLAMP(cv + dv, 0, GRID_RES - 1);
		for (int du = -1; du <= 1; du++) {
			int nu = CLAMP(cu + du, 0, GRID_RES - 1);
			int cell_idx = face * GRID_RES * GRID_RES + nv * GRID_RES + nu;
			const GridCell &cell = grid_cells[cell_idx];
			for (int k = 0; k < cell.count; k++) {
				int idx = grid_indices[cell.start + k];
				float d = p_dir.dot(points[idx]);
				if (d > best_dot) {
					second_dot = best_dot;
					second_idx = best_idx;
					best_dot = d;
					best_idx = idx;
					found = true;
				} else if (d > second_dot) {
					second_dot = d;
					second_idx = idx;
				}
			}
		}
	}

	if (!found) {
		// Fallback: O(N) scan.
		const int n = points.size();
		for (int i = 0; i < n; i++) {
			float d = p_dir.dot(points[i]);
			if (d > best_dot) {
				second_dot = best_dot;
				second_idx = best_idx;
				best_dot = d;
				best_idx = i;
			} else if (d > second_dot) {
				second_dot = d;
				second_idx = i;
			}
		}
	}

	r_nearest = best_idx;
	r_second = second_idx;
	r_best_dot = best_dot;
	r_second_dot = second_dot;
}
