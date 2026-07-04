#include "ico_voxel_block_filler.h"
#include "core/math/math_funcs.h"
#include "modules/voxel/constants/voxel_constants.h"

IcoVoxelBlockFiller::IcoVoxelBlockFiller() {
	memset(grid_cells, 0, sizeof(grid_cells));
}

void IcoVoxelBlockFiller::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_planet_radius", "radius"), &IcoVoxelBlockFiller::set_planet_radius);
	ClassDB::bind_method(D_METHOD("get_planet_radius"), &IcoVoxelBlockFiller::get_planet_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "planet_radius"), "set_planet_radius", "get_planet_radius");

	ClassDB::bind_method(D_METHOD("set_max_terrain_height", "height"), &IcoVoxelBlockFiller::set_max_terrain_height);
	ClassDB::bind_method(D_METHOD("get_max_terrain_height"), &IcoVoxelBlockFiller::get_max_terrain_height);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_terrain_height"), "set_max_terrain_height", "get_max_terrain_height");

	ClassDB::bind_method(D_METHOD("set_voronoi_data", "points", "grid", "point_material",
								  "warp_x", "warp_y", "warp_z", "border_warp"),
		&IcoVoxelBlockFiller::set_voronoi_data);

	ClassDB::bind_method(D_METHOD("fill_materials", "buffer", "origin", "lod"),
		&IcoVoxelBlockFiller::fill_materials);

	ClassDB::bind_method(D_METHOD("set_river_flow_threshold", "threshold"), &IcoVoxelBlockFiller::set_river_flow_threshold);
	ClassDB::bind_method(D_METHOD("get_river_flow_threshold"), &IcoVoxelBlockFiller::get_river_flow_threshold);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "river_flow_threshold"), "set_river_flow_threshold", "get_river_flow_threshold");
}

// ── Setters ───────────────────────────────────────────────────────────────────

void IcoVoxelBlockFiller::set_planet_radius(float p_radius) { planet_radius = p_radius; }
float IcoVoxelBlockFiller::get_planet_radius() const { return planet_radius; }

void IcoVoxelBlockFiller::set_max_terrain_height(float p_height) { max_terrain_height = p_height; }
float IcoVoxelBlockFiller::get_max_terrain_height() const { return max_terrain_height; }

void IcoVoxelBlockFiller::set_river_flow_threshold(float p_threshold) { river_flow_threshold = p_threshold; }
float IcoVoxelBlockFiller::get_river_flow_threshold() const { return river_flow_threshold; }

// ── Voronoi data ──────────────────────────────────────────────────────────────

void IcoVoxelBlockFiller::set_voronoi_data(
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

	// Build the cubemap spatial grid
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

// ── Cubemap helpers ───────────────────────────────────────────────────────────

void IcoVoxelBlockFiller::_point_to_face_cell(const Vector3 &p, int &r_face, int &r_cu, int &r_cv) {
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

Vector3 IcoVoxelBlockFiller::_warp_dir(const Vector3 &dir) const {
	if (noise_warp_x.is_null()) return dir;
	float wx = noise_warp_x->get_noise_3d(dir.x, dir.y, dir.z);
	float wy = noise_warp_y->get_noise_3d(dir.x + 3.7f, dir.y + 1.3f, dir.z + 2.5f);
	float wz = noise_warp_z->get_noise_3d(dir.x - 2.1f, dir.y + 4.8f, dir.z - 0.9f);
	return (dir + Vector3(wx, wy, wz) * border_warp).normalized();
}

int IcoVoxelBlockFiller::_get_nearest(const Vector3 &dir) const {
	int face, cu, cv;
	_point_to_face_cell(dir, face, cu, cv);

	int best_idx = 0;
	float best_dot = -2.0f;

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
				if (d > best_dot) {
					best_dot = d;
					best_idx = idx;
				}
			}
		}
	}

	return best_idx;
}

int IcoVoxelBlockFiller::_lookup_material(const Vector3 &unit_dir) const {
	Vector3 warped = _warp_dir(unit_dir);
	int idx = _get_nearest(warped);
	int mat_id = MAT_ROCK;
	if (idx < point_material.size()) {
		mat_id = point_material[idx];
	}
	return mat_id | (mat_id << 4);
}

// ── Core: fill materials ──────────────────────────────────────────────────────

void IcoVoxelBlockFiller::fill_materials(
		Ref<GodotVoxelBuffer> p_buffer,
		Vector3i p_origin,
		int p_lod) {
	ERR_FAIL_COND(p_buffer.is_null());

	using namespace zylann::voxel;

	godot::VoxelBuffer *buf = p_buffer.ptr();
	buf->set_channel_depth(
		VoxelBuffer::CHANNEL_INDICES, GodotVoxelBuffer::DEPTH_16_BIT);
	buf->set_channel_depth(
		VoxelBuffer::CHANNEL_WEIGHTS, GodotVoxelBuffer::DEPTH_16_BIT);

	Vector3i size = buf->get_size();
	float lod_scale = (float)(1 << p_lod);

	// Block-level early-out
	float block_span = (float)(size.x - 2) * lod_scale;
	float half = block_span * 0.5f;
	Vector3 centre = Vector3(p_origin) + Vector3(half, half, half);
	float cdist = centre.length();
	float margin = max_terrain_height + 500.0f;

	if (cdist - half > planet_radius + margin) {
		buf->fill(0x0000, VoxelBuffer::CHANNEL_INDICES);
		buf->fill(0x000F, VoxelBuffer::CHANNEL_WEIGHTS);
		return;
	}

	if (cdist + half < planet_radius - margin) {
		buf->fill(0x0000, VoxelBuffer::CHANNEL_INDICES);
		buf->fill(0x000F, VoxelBuffer::CHANNEL_WEIGHTS);
		return;
	}

	bool has_materials = point_material.size() > 0;
	float surface_margin = 2.0f * lod_scale;

	for (int z = 0; z < size.z; z++) {
		for (int y = 0; y < size.y; y++) {
			for (int x = 0; x < size.x; x++) {
				// Read SDF — skip voxels far from the surface
				float sdf = buf->get_voxel_f(x, y, z, VoxelBuffer::CHANNEL_SDF);

				if (Math::abs(sdf) > surface_margin) {
					buf->set_voxel(0x0000, x, y, z, VoxelBuffer::CHANNEL_INDICES);
					buf->set_voxel(0x000F, x, y, z, VoxelBuffer::CHANNEL_WEIGHTS);
					continue;
				}

				if (has_materials) {
					Vector3 world_pos(
						p_origin.x + x * lod_scale,
						p_origin.y + y * lod_scale,
						p_origin.z + z * lod_scale);
					float dist = world_pos.length();
					if (dist < 0.001f) {
						continue;
					}
					Vector3 unit_dir = world_pos / dist;
					int indices_val = _lookup_material(unit_dir);
					buf->set_voxel(indices_val, x, y, z, VoxelBuffer::CHANNEL_INDICES);
					buf->set_voxel(0x000F, x, y, z, VoxelBuffer::CHANNEL_WEIGHTS);
				} else {
					buf->set_voxel(0x0000, x, y, z, VoxelBuffer::CHANNEL_INDICES);
					buf->set_voxel(0x000F, x, y, z, VoxelBuffer::CHANNEL_WEIGHTS);
				}
			}
		}
	}
}
