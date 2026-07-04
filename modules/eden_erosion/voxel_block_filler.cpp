#include "voxel_block_filler.h"
#include "core/math/math_funcs.h"

VoxelBlockFiller::VoxelBlockFiller() {
	memset(grid_cells, 0, sizeof(grid_cells));
}

// ── Bind methods ──────────────────────────────────────────────────────────

void VoxelBlockFiller::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_planet_radius", "radius"), &VoxelBlockFiller::set_planet_radius);
	ClassDB::bind_method(D_METHOD("get_planet_radius"), &VoxelBlockFiller::get_planet_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "planet_radius"), "set_planet_radius", "get_planet_radius");

	ClassDB::bind_method(D_METHOD("set_micro_noises", "micro1", "micro2", "micro3"),
		&VoxelBlockFiller::set_micro_noises);

	ClassDB::bind_method(D_METHOD("set_micro_amplitudes", "amp1", "amp2", "amp3"),
		&VoxelBlockFiller::set_micro_amplitudes);

	ClassDB::bind_method(D_METHOD("set_micro_lod_cutoff", "cutoff"),
		&VoxelBlockFiller::set_micro_lod_cutoff);

	ClassDB::bind_method(D_METHOD("set_voronoi_data", "points", "voronoi_grid", "point_material",
		"warp_x", "warp_y", "warp_z", "border_warp"),
		&VoxelBlockFiller::set_voronoi_data);

	ClassDB::bind_method(D_METHOD("set_river_flow_threshold", "threshold"),
		&VoxelBlockFiller::set_river_flow_threshold);
	ClassDB::bind_method(D_METHOD("get_river_flow_threshold"),
		&VoxelBlockFiller::get_river_flow_threshold);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "river_flow_threshold"), "set_river_flow_threshold", "get_river_flow_threshold");

	ClassDB::bind_method(D_METHOD("fill_block", "buffer", "origin", "lod", "chunks", "grid_n", "max_terrain_height"),
		&VoxelBlockFiller::fill_block);
}

// ── Setters ───────────────────────────────────────────────────────────────

void VoxelBlockFiller::set_planet_radius(float p_radius) {
	planet_radius = p_radius;
}

float VoxelBlockFiller::get_planet_radius() const {
	return planet_radius;
}

void VoxelBlockFiller::set_micro_noises(
		Ref<FastNoiseLite> p_micro1,
		Ref<FastNoiseLite> p_micro2,
		Ref<FastNoiseLite> p_micro3) {
	noise_micro1 = p_micro1;
	noise_micro2 = p_micro2;
	noise_micro3 = p_micro3;
}

void VoxelBlockFiller::set_micro_amplitudes(float p_amp1, float p_amp2, float p_amp3) {
	micro1_amp = p_amp1;
	micro2_amp = p_amp2;
	micro3_amp = p_amp3;
}

void VoxelBlockFiller::set_micro_lod_cutoff(int p_cutoff) {
	micro_lod_cutoff = MAX(1, p_cutoff);
}

void VoxelBlockFiller::set_river_flow_threshold(float p_threshold) {
	river_flow_threshold = p_threshold;
}

float VoxelBlockFiller::get_river_flow_threshold() const {
	return river_flow_threshold;
}

void VoxelBlockFiller::set_voronoi_data(
		PackedVector3Array p_points,
		Array p_voronoi_grid,
		PackedByteArray p_point_material,
		Ref<FastNoiseLite> p_warp_x,
		Ref<FastNoiseLite> p_warp_y,
		Ref<FastNoiseLite> p_warp_z,
		float p_border_warp) {
	voronoi_points = p_points;
	voronoi_count = voronoi_points.size();
	point_material = p_point_material;
	noise_warp_x = p_warp_x;
	noise_warp_y = p_warp_y;
	noise_warp_z = p_warp_z;
	border_warp = p_border_warp;

	// Flatten the GDScript Voronoi grid into flat C++ structure.
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

// ── Cubemap helpers ───────────────────────────────────────────────────────

void VoxelBlockFiller::_dir_to_face_uv(const Vector3 &dir, int &r_face, float &r_u, float &r_v) {
	float ax = Math::abs(dir.x);
	float ay = Math::abs(dir.y);
	float az = Math::abs(dir.z);
	float s, t;

	if (ax >= ay && ax >= az) {
		if (dir.x > 0.0f) {
			r_face = 0; s = -dir.z / ax; t = -dir.y / ax;
		} else {
			r_face = 1; s = dir.z / ax; t = -dir.y / ax;
		}
	} else if (ay >= ax && ay >= az) {
		if (dir.y > 0.0f) {
			r_face = 2; s = dir.x / ay; t = dir.z / ay;
		} else {
			r_face = 3; s = dir.x / ay; t = -dir.z / ay;
		}
	} else {
		if (dir.z > 0.0f) {
			r_face = 4; s = dir.x / az; t = -dir.y / az;
		} else {
			r_face = 5; s = -dir.x / az; t = -dir.y / az;
		}
	}

	r_u = (s + 1.0f) * 0.5f;
	r_v = (t + 1.0f) * 0.5f;
}

void VoxelBlockFiller::_point_to_face_cell(const Vector3 &p, int &r_face, int &r_cu, int &r_cv) const {
	float u, v;
	int face;
	_dir_to_face_uv(p, face, u, v);
	r_face = face;
	r_cu = CLAMP((int)(u * GRID_RES), 0, GRID_RES - 1);
	r_cv = CLAMP((int)(v * GRID_RES), 0, GRID_RES - 1);
}

// ── Bilinear heightmap sampling ───────────────────────────────────────────

float VoxelBlockFiller::_sample_height(const float *data, int res, float u, float v) {
	float fx = CLAMP(u, 0.0f, 1.0f) * (float)(res - 1);
	float fy = CLAMP(v, 0.0f, 1.0f) * (float)(res - 1);
	int ix = (int)fx;
	int iy = (int)fy;
	float dx = fx - ix;
	float dy = fy - iy;
	int ix1 = MIN(ix + 1, res - 1);
	int iy1 = MIN(iy + 1, res - 1);
	float h00 = data[iy * res + ix];
	float h10 = data[iy * res + ix1];
	float h01 = data[iy1 * res + ix];
	float h11 = data[iy1 * res + ix1];
	return h00 * (1.0f - dx) * (1.0f - dy)
		 + h10 * dx * (1.0f - dy)
		 + h01 * (1.0f - dx) * dy
		 + h11 * dx * dy;
}

float VoxelBlockFiller::_sample_float_array(const float *data, int res, float u, float v) {
	return _sample_height(data, res, u, v);
}

// ── Voronoi / Material lookup ─────────────────────────────────────────────

Vector3 VoxelBlockFiller::_warp_dir(const Vector3 &dir) const {
	if (noise_warp_x.is_null()) {
		return dir;
	}
	float wx = noise_warp_x->get_noise_3d(dir.x, dir.y, dir.z);
	float wy = noise_warp_y->get_noise_3d(dir.x + 3.7f, dir.y + 1.3f, dir.z + 2.5f);
	float wz = noise_warp_z->get_noise_3d(dir.x - 2.1f, dir.y + 4.8f, dir.z - 0.9f);
	return (dir + Vector3(wx, wy, wz) * border_warp).normalized();
}

int VoxelBlockFiller::_get_nearest(const Vector3 &dir) const {
	int face, cu, cv;
	_point_to_face_cell(dir, face, cu, cv);

	int best_idx = 0;
	float best_dot = -2.0f;

	const Vector3 *pts = voronoi_points.ptr();
	const int *gi = grid_indices.ptr();

	// Check the 3×3 neighbourhood of cells.
	for (int dv = -1; dv <= 1; dv++) {
		int nv = CLAMP(cv + dv, 0, GRID_RES - 1);
		for (int du = -1; du <= 1; du++) {
			int nu = CLAMP(cu + du, 0, GRID_RES - 1);
			int flat_idx = face * GRID_RES * GRID_RES + nv * GRID_RES + nu;
			const GridCell &gc = grid_cells[flat_idx];
			for (int i = 0; i < gc.count; i++) {
				int idx = gi[gc.start + i];
				float d = dir.dot(pts[idx]);
				if (d > best_dot) {
					best_dot = d;
					best_idx = idx;
				}
			}
		}
	}

	return best_idx;
}

int VoxelBlockFiller::_lookup_material(const Vector3 &unit_dir, float height_m, float sediment_m, float flow_accum) const {
	// River override (highest priority)
	if (flow_accum >= river_flow_threshold && height_m > 0.0f) {
		return MAT_WATER | (MAT_WATER << 4);
	}

	Vector3 warped = _warp_dir(unit_dir);
	int idx = _get_nearest(warped);

	int mat_id = MAT_ROCK;
	if (idx < point_material.size()) {
		mat_id = point_material[idx];
	}

	// Sediment deposit bias
	if (sediment_m > 5.0f && height_m > 0.0f) {
		if (height_m < 50.0f) {
			mat_id = MAT_SAND;
		} else {
			mat_id = MAT_DIRT;
		}
	}

	// Pack as MIXEL4: indices = (mat_a << 0) | (mat_a << 4)
	return mat_id | (mat_id << 4);
}

// ── Micro-detail noise ────────────────────────────────────────────────────

float VoxelBlockFiller::_apply_micro_detail(float base_h, const Vector3 &world_pos, int lod) const {
	float lod_fade = 1.0f - (float)lod / (float)micro_lod_cutoff;

	float wx = world_pos.x;
	float wy = world_pos.y;
	float wz = world_pos.z;

	// Layer 1: broad undulations
	float m1 = 0.0f;
	if (noise_micro1.is_valid()) {
		m1 = noise_micro1->get_noise_3d(wx, wy, wz) * micro1_amp;
	}

	// Layer 2: ridged crags
	float m2 = 0.0f;
	if (noise_micro2.is_valid()) {
		m2 = noise_micro2->get_noise_3d(wx, wy, wz) * micro2_amp;
	}

	// Layer 3: fine roughness — only at LOD 0-1
	float m3 = 0.0f;
	if (lod <= 1 && noise_micro3.is_valid()) {
		float fine_fade = 1.0f - (float)lod;
		m3 = noise_micro3->get_noise_3d(wx, wy, wz) * micro3_amp * fine_fade;
	}

	// Slope-aware scaling
	float land_factor = CLAMP(base_h / 100.0f, 0.0f, 1.0f);
	float detail_scale = 0.3f + 0.7f * land_factor;

	return base_h + (m1 + m2 + m3) * lod_fade * detail_scale;
}

// ── Core fill_block method ────────────────────────────────────────────────

int VoxelBlockFiller::fill_block(
		Ref<GodotVoxelBuffer> p_buffer,
		Vector3i p_origin,
		int p_lod,
		Dictionary p_chunks,
		int p_grid_n,
		float p_max_terrain_height) {
	ERR_FAIL_COND_V_MSG(p_buffer.is_null(), 0, "VoxelBuffer is null");

	// Access the internal VoxelBuffer directly for maximum performance.
	// The GD wrapper delegates all calls to this anyway.
	using InternalVB = zylann::voxel::VoxelBuffer;
	InternalVB &buffer = p_buffer->get_buffer();

	buffer.set_channel_depth(InternalVB::CHANNEL_INDICES, InternalVB::DEPTH_16_BIT);
	buffer.set_channel_depth(InternalVB::CHANNEL_WEIGHTS, InternalVB::DEPTH_16_BIT);

	Vector3i size = buffer.get_size();
	float lod_scale = (float)(1 << p_lod);

	// ── Block-level early-out ─────────────────────────────────────────────
	float block_span = (float)(size.x - 2) * lod_scale;
	float half = block_span * 0.5f;
	Vector3 centre = Vector3(p_origin) + Vector3(half, half, half);
	float cdist = centre.length();
	float margin = p_max_terrain_height + 500.0f;

	if (cdist - half > planet_radius + margin) {
		buffer.fill_f(cdist - planet_radius, InternalVB::CHANNEL_SDF);
		buffer.fill(0x0000, InternalVB::CHANNEL_INDICES);
		buffer.fill(0x000F, InternalVB::CHANNEL_WEIGHTS);
		return 1; // early-out: outside
	}

	if (cdist + half < planet_radius - margin) {
		buffer.fill_f(planet_radius - cdist, InternalVB::CHANNEL_SDF);
		buffer.fill(0x0000, InternalVB::CHANNEL_INDICES);
		buffer.fill(0x000F, InternalVB::CHANNEL_WEIGHTS);
		return 2; // early-out: inside
	}

	// ── Per-voxel loop ────────────────────────────────────────────────────
	float inv_grid = 1.0f / (float)p_grid_n;
	bool has_material = point_material.size() > 0;
	bool do_micro = (p_lod < micro_lod_cutoff) && noise_micro1.is_valid();
	bool any_chunk_miss = false;

	for (int z = 0; z < size.z; z++) {
		for (int y = 0; y < size.y; y++) {
			for (int x = 0; x < size.x; x++) {
				float wx = (float)p_origin.x + (float)x * lod_scale;
				float wy = (float)p_origin.y + (float)y * lod_scale;
				float wz = (float)p_origin.z + (float)z * lod_scale;

				float dist = Math::sqrt(wx * wx + wy * wy + wz * wz);
				if (dist < 0.001f) {
					buffer.set_voxel_f(-1.0f, x, y, z, InternalVB::CHANNEL_SDF);
					continue;
				}

				float inv_dist = 1.0f / dist;
				float dx = wx * inv_dist;
				float dy = wy * inv_dist;
				float dz = wz * inv_dist;
				Vector3 unit_dir(dx, dy, dz);

				// ── Map direction to cubemap face + chunk grid coords ─────
				int face;
				float fu, fv;
				_dir_to_face_uv(unit_dir, face, fu, fv);

				int gx = CLAMP((int)(fu * p_grid_n), 0, p_grid_n - 1);
				int gy = CLAMP((int)(fv * p_grid_n), 0, p_grid_n - 1);
				float local_u = Math::fmod(fu * p_grid_n, 1.0f);
				float local_v = Math::fmod(fv * p_grid_n, 1.0f);
				// Clamp local UVs to avoid negative fmod results
				if (local_u < 0.0f) local_u += 1.0f;
				if (local_v < 0.0f) local_v += 1.0f;

				// ── Look up chunk from dictionary ─────────────────────────
				// Build key string — this is the main cost of the dictionary
				// approach, but it's still much faster than GDScript.
				char key_buf[32];
				snprintf(key_buf, sizeof(key_buf), "%d_%d_%d", face, gx, gy);
				String key(key_buf);

				float surface_h = 0.0f;
				float sediment = 0.0f;
				float flow = 0.0f;
				bool chunk_found = false;

				if (p_chunks.has(key)) {
					Object *chunk_obj = Object::cast_to<Object>(p_chunks[key]);
					if (chunk_obj) {
						// Check that the chunk is ready (fully generated)
						bool is_ready = chunk_obj->get("ready");
						if (is_ready) {
							// Get the height_data, sediment_data, flow_data arrays
							// and the RES constant from the chunk.
							PackedFloat32Array h_data = chunk_obj->get("height_data");
							int res = chunk_obj->get("RES");
							if (h_data.size() > 0 && res > 0) {
								surface_h = _sample_height(h_data.ptr(), res, local_u, local_v);
								chunk_found = true;

								PackedFloat32Array s_data = chunk_obj->get("sediment_data");
								if (s_data.size() > 0) {
									sediment = _sample_float_array(s_data.ptr(), res, local_u, local_v);
								}

								PackedFloat32Array f_data = chunk_obj->get("flow_data");
								if (f_data.size() > 0) {
									flow = _sample_float_array(f_data.ptr(), res, local_u, local_v);
								}
							}
						}
					}
				}

				if (!chunk_found) {
					any_chunk_miss = true;
				}

				// ── Micro-detail noise at low LODs ────────────────────────
				if (do_micro && surface_h > -50.0f) {
					Vector3 world_pos(wx, wy, wz);
					surface_h = _apply_micro_detail(surface_h, world_pos, p_lod);
				}

				// ── SDF computation ───────────────────────────────────────
				float sdf = dist - (planet_radius + surface_h);
				buffer.set_voxel_f(sdf, x, y, z, InternalVB::CHANNEL_SDF);

				// ── Material assignment ───────────────────────────────────
				if (has_material) {
					int indices_val = _lookup_material(unit_dir, surface_h, sediment, flow);
					buffer.set_voxel(indices_val, x, y, z, InternalVB::CHANNEL_INDICES);
					buffer.set_voxel(0x000F, x, y, z, InternalVB::CHANNEL_WEIGHTS);
				} else {
					buffer.set_voxel(0x0000, x, y, z, InternalVB::CHANNEL_INDICES);
					buffer.set_voxel(0x000F, x, y, z, InternalVB::CHANNEL_WEIGHTS);
				}
			}
		}
	}

	return any_chunk_miss ? 4 : 3; // 3 = filled OK, 4 = filled but some chunks missing
}
