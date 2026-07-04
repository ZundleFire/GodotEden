#include "terrain_stamp_placer.h"
#include "core/math/math_funcs.h"

TerrainStampPlacer::TerrainStampPlacer() {
}

// ── Planet parameters ────────────────────────────────────────────────────────

void TerrainStampPlacer::set_planet_radius(float p_radius) {
	planet_radius = MAX(1.0f, p_radius);
}

float TerrainStampPlacer::get_planet_radius() const {
	return planet_radius;
}

// ── Stamp management ─────────────────────────────────────────────────────────

int TerrainStampPlacer::add_stamp(const Ref<TerrainStamp> &p_stamp, const Transform3D &p_transform) {
	ERR_FAIL_COND_V(p_stamp.is_null(), -1);

	PlacedStamp ps;
	ps.stamp = p_stamp;
	ps.transform = p_transform;
	ps.inverse_transform = p_transform.affine_inverse();
	ps.world_aabb = _compute_world_aabb(p_stamp, p_transform);
	ps.active = true;

	// Find an inactive slot to reuse
	for (int i = 0; i < stamps.size(); i++) {
		if (!stamps[i].active) {
			stamps.write[i] = ps;
			return i;
		}
	}

	// Append new
	int idx = stamps.size();
	stamps.push_back(ps);
	return idx;
}

void TerrainStampPlacer::remove_stamp(int p_index) {
	ERR_FAIL_INDEX(p_index, stamps.size());
	stamps.write[p_index].active = false;
	stamps.write[p_index].stamp = Ref<TerrainStamp>();
}

void TerrainStampPlacer::clear_stamps() {
	stamps.clear();
}

int TerrainStampPlacer::get_stamp_count() const {
	int count = 0;
	for (int i = 0; i < stamps.size(); i++) {
		if (stamps[i].active) {
			count++;
		}
	}
	return count;
}

void TerrainStampPlacer::set_stamp_transform(int p_index, const Transform3D &p_transform) {
	ERR_FAIL_INDEX(p_index, stamps.size());
	ERR_FAIL_COND(!stamps[p_index].active);
	stamps.write[p_index].transform = p_transform;
	stamps.write[p_index].inverse_transform = p_transform.affine_inverse();
	stamps.write[p_index].world_aabb = _compute_world_aabb(stamps[p_index].stamp, p_transform);
}

Transform3D TerrainStampPlacer::get_stamp_transform(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, stamps.size(), Transform3D());
	return stamps[p_index].transform;
}

Ref<TerrainStamp> TerrainStampPlacer::get_stamp(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, stamps.size(), Ref<TerrainStamp>());
	return stamps[p_index].stamp;
}

// ── AABB computation ─────────────────────────────────────────────────────────

AABB TerrainStampPlacer::_compute_world_aabb(const Ref<TerrainStamp> &p_stamp, const Transform3D &p_transform) const {
	const float half_size = p_stamp->get_world_size() * 0.5f;
	const float max_h = p_stamp->get_height_scale() + p_stamp->get_base_offset();
	const float min_h = p_stamp->get_base_offset();
	const float blend = p_stamp->get_blend_radius();

	// Local AABB of the stamp (centred at origin, Y = up)
	AABB local;
	local.position = Vector3(-half_size - blend, min_h - blend, -half_size - blend);
	local.size = Vector3(
		(half_size + blend) * 2.0f,
		(max_h - min_h) + blend * 2.0f,
		(half_size + blend) * 2.0f
	);

	return p_transform.xform(local);
}

// ── SDF math ─────────────────────────────────────────────────────────────────

float TerrainStampPlacer::_sdf_smooth_union(float a, float b, float k) {
	if (k <= 0.0f) {
		return MIN(a, b);
	}
	const float h = CLAMP(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
	return Math::lerp(b, a, h) - k * h * (1.0f - h);
}

float TerrainStampPlacer::_sdf_smooth_subtract(float a, float b, float k) {
	// a = base SDF, b = subtractor SDF
	// smooth subtraction: max(a, -b) with smoothing
	if (k <= 0.0f) {
		return MAX(a, -b);
	}
	const float h = CLAMP(0.5f - 0.5f * (a + b) / k, 0.0f, 1.0f);
	return Math::lerp(a, -b, h) + k * h * (1.0f - h);
}

// ── SDF evaluation ───────────────────────────────────────────────────────────

float TerrainStampPlacer::evaluate_sdf(const Vector3 &p_world_pos, float p_base_sdf) const {
	float result = p_base_sdf;

	for (int i = 0; i < stamps.size(); i++) {
		if (!stamps[i].active) {
			continue;
		}

		const PlacedStamp &ps = stamps[i];

		// Quick AABB rejection
		if (!ps.world_aabb.has_point(p_world_pos)) {
			continue;
		}

		// Transform world pos to stamp-local space
		const Vector3 local = ps.inverse_transform.xform(p_world_pos);

		// Evaluate stamp SDF in local space
		const float stamp_sdf = ps.stamp->sample_sdf(local);

		// Blend with current result based on blend mode
		const float k = ps.stamp->get_blend_radius();
		switch (ps.stamp->get_blend_mode()) {
			case TerrainStamp::BLEND_UNION:
				result = _sdf_smooth_union(result, stamp_sdf, k);
				break;
			case TerrainStamp::BLEND_SUBTRACT:
				result = _sdf_smooth_subtract(result, stamp_sdf, k);
				break;
			case TerrainStamp::BLEND_REPLACE: {
				// Hard replacement within the stamp footprint
				const float half = ps.stamp->get_world_size() * 0.5f;
				if (Math::abs(local.x) <= half && Math::abs(local.z) <= half) {
					result = stamp_sdf;
				}
			} break;
		}
	}

	return result;
}

int TerrainStampPlacer::evaluate_material(const Vector3 &p_world_pos) const {
	float best_sdf = 1e20f;
	int best_mat = -1;

	for (int i = 0; i < stamps.size(); i++) {
		if (!stamps[i].active) {
			continue;
		}

		const PlacedStamp &ps = stamps[i];
		if (ps.stamp->get_material_index() < 0) {
			continue;
		}

		if (!ps.world_aabb.has_point(p_world_pos)) {
			continue;
		}

		const Vector3 local = ps.inverse_transform.xform(p_world_pos);
		const float sdf = ps.stamp->sample_sdf(local);

		if (sdf < best_sdf) {
			best_sdf = sdf;
			best_mat = ps.stamp->get_material_index();
		}
	}

	// Only override material if we're actually inside/near a stamp surface
	if (best_sdf < 2.0f) {
		return best_mat;
	}
	return -1;
}

// ── Block-level application ──────────────────────────────────────────────────

int TerrainStampPlacer::apply_stamps(Ref<GodotVoxelBuffer> p_buffer, Vector3i p_origin, int p_lod) {
	ERR_FAIL_COND_V(p_buffer.is_null(), 0);

	const int voxel_step = 1 << p_lod;
	const Vector3i size = p_buffer->get_size();

	// Compute this block's world AABB
	const AABB block_aabb(
		Vector3(p_origin.x, p_origin.y, p_origin.z),
		Vector3(size.x * voxel_step, size.y * voxel_step, size.z * voxel_step)
	);

	// Collect stamps that intersect this block
	Vector<int> relevant_stamps;
	for (int i = 0; i < stamps.size(); i++) {
		if (!stamps[i].active) {
			continue;
		}
		if (stamps[i].world_aabb.intersects(block_aabb)) {
			relevant_stamps.push_back(i);
		}
	}

	if (relevant_stamps.is_empty()) {
		return 0;
	}

	// Iterate every voxel in the block
	for (int z = 0; z < size.z; z++) {
		for (int y = 0; y < size.y; y++) {
			for (int x = 0; x < size.x; x++) {
				const Vector3 world_pos(
					p_origin.x + x * voxel_step + voxel_step * 0.5f,
					p_origin.y + y * voxel_step + voxel_step * 0.5f,
					p_origin.z + z * voxel_step + voxel_step * 0.5f
				);

				// Read existing SDF
				const float base_sdf = p_buffer->get_voxel_f(x, y, z, GodotVoxelBuffer::CHANNEL_SDF);

				float blended_sdf = base_sdf;
				int best_mat = -1;
				float best_stamp_sdf = 1e20f;

				// Evaluate each relevant stamp
				for (int si = 0; si < relevant_stamps.size(); si++) {
					const PlacedStamp &ps = stamps[relevant_stamps[si]];

					// Quick AABB check per voxel (cheaper than full stamp eval)
					if (!ps.world_aabb.has_point(world_pos)) {
						continue;
					}

					const Vector3 local = ps.inverse_transform.xform(world_pos);
					const float stamp_sdf = ps.stamp->sample_sdf(local);
					const float k = ps.stamp->get_blend_radius();

					switch (ps.stamp->get_blend_mode()) {
						case TerrainStamp::BLEND_UNION:
							blended_sdf = _sdf_smooth_union(blended_sdf, stamp_sdf, k);
							break;
						case TerrainStamp::BLEND_SUBTRACT:
							blended_sdf = _sdf_smooth_subtract(blended_sdf, stamp_sdf, k);
							break;
						case TerrainStamp::BLEND_REPLACE: {
							const float half = ps.stamp->get_world_size() * 0.5f;
							if (Math::abs(local.x) <= half && Math::abs(local.z) <= half) {
								blended_sdf = stamp_sdf;
							}
						} break;
					}

					// Track best stamp for material assignment
					if (ps.stamp->get_material_index() >= 0 && stamp_sdf < best_stamp_sdf) {
						best_stamp_sdf = stamp_sdf;
						best_mat = ps.stamp->get_material_index();
					}
				}

				// Write blended SDF back
				p_buffer->set_voxel_f(blended_sdf, x, y, z, GodotVoxelBuffer::CHANNEL_SDF);

				// Write material if a stamp provides one and we're near its surface
				if (best_mat >= 0 && best_stamp_sdf < 2.0f * voxel_step) {
					p_buffer->set_voxel(best_mat, x, y, z, GodotVoxelBuffer::CHANNEL_INDICES);
				}
			}
		}
	}

	return relevant_stamps.size();
}

// ── Utility ──────────────────────────────────────────────────────────────────

Transform3D TerrainStampPlacer::make_planet_transform(const Vector3 &p_surface_point, float p_rotation_deg) {
	// Y axis = radial outward (planet normal)
	Vector3 up = p_surface_point.normalized();

	// Pick a reference direction that isn't parallel to up
	Vector3 ref = Vector3(0, 1, 0);
	if (Math::abs(up.dot(ref)) > 0.99f) {
		ref = Vector3(1, 0, 0);
	}

	// Build orthonormal basis
	Vector3 right = up.cross(ref).normalized();
	Vector3 forward = right.cross(up).normalized();

	// Apply rotation around up axis
	if (Math::abs(p_rotation_deg) > 0.001f) {
		const float rad = Math::deg_to_rad(p_rotation_deg);
		const float c = Math::cos(rad);
		const float s = Math::sin(rad);
		Vector3 new_right = right * c + forward * s;
		Vector3 new_forward = -right * s + forward * c;
		right = new_right;
		forward = new_forward;
	}

	Basis basis;
	basis.set_column(0, right);     // X = right
	basis.set_column(1, up);        // Y = up (radial)
	basis.set_column(2, forward);   // Z = forward

	return Transform3D(basis, p_surface_point);
}

// ── Bindings ─────────────────────────────────────────────────────────────────

void TerrainStampPlacer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_planet_radius", "radius"), &TerrainStampPlacer::set_planet_radius);
	ClassDB::bind_method(D_METHOD("get_planet_radius"), &TerrainStampPlacer::get_planet_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "planet_radius"), "set_planet_radius", "get_planet_radius");

	ClassDB::bind_method(D_METHOD("add_stamp", "stamp", "transform"), &TerrainStampPlacer::add_stamp);
	ClassDB::bind_method(D_METHOD("remove_stamp", "index"), &TerrainStampPlacer::remove_stamp);
	ClassDB::bind_method(D_METHOD("clear_stamps"), &TerrainStampPlacer::clear_stamps);
	ClassDB::bind_method(D_METHOD("get_stamp_count"), &TerrainStampPlacer::get_stamp_count);

	ClassDB::bind_method(D_METHOD("set_stamp_transform", "index", "transform"), &TerrainStampPlacer::set_stamp_transform);
	ClassDB::bind_method(D_METHOD("get_stamp_transform", "index"), &TerrainStampPlacer::get_stamp_transform);
	ClassDB::bind_method(D_METHOD("get_stamp", "index"), &TerrainStampPlacer::get_stamp);

	ClassDB::bind_method(D_METHOD("evaluate_sdf", "world_pos", "base_sdf"), &TerrainStampPlacer::evaluate_sdf);
	ClassDB::bind_method(D_METHOD("evaluate_material", "world_pos"), &TerrainStampPlacer::evaluate_material);
	ClassDB::bind_method(D_METHOD("apply_stamps", "buffer", "origin", "lod"), &TerrainStampPlacer::apply_stamps);

	ClassDB::bind_static_method("TerrainStampPlacer", D_METHOD("make_planet_transform", "surface_point", "rotation_deg"), &TerrainStampPlacer::make_planet_transform, DEFVAL(0.0f));
}
