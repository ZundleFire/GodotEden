#ifndef TERRAIN_STAMP_PLACER_H
#define TERRAIN_STAMP_PLACER_H

#include "core/object/ref_counted.h"
#include "core/variant/variant.h"
#include "core/math/transform_3d.h"
#include "core/math/aabb.h"
#include "terrain_stamp.h"
#include "modules/voxel/storage/voxel_buffer_gd.h"

// Alias for the GDScript-facing VoxelBuffer (RefCounted wrapper).
using GodotVoxelBuffer = zylann::voxel::godot::VoxelBuffer;

/// TerrainStampPlacer — Runtime system that blends TerrainStamp patches
/// into the voxel terrain SDF during block generation.
///
/// Designed to be called from VoxelGeneratorScript._generate_block() or
/// as a post-process after the base generator fills SDF data.
///
/// Usage from GDScript:
///   var placer := TerrainStampPlacer.new()
///   placer.set_planet_radius(40000.0)
///   placer.add_stamp(stamp_res, transform)   # place stamp at world position
///   placer.add_stamp(stamp_res2, transform2)
///
///   # In _generate_block():
///   placer.apply_stamps(out_buffer, origin_in_voxels, lod)
///
/// Each stamp is stored with its world Transform3D.  The "up" direction
/// of the transform points away from the planet surface.  The XZ plane
/// of the transform is the stamp's horizontal footprint.
///
/// PERFORMANCE: For each block, only stamps whose AABB intersects the
/// block AABB are evaluated.  Within those stamps, per-voxel SDF is
/// blended using the stamp's blend_mode (smooth union, subtract, replace).

class TerrainStampPlacer : public RefCounted {
	GDCLASS(TerrainStampPlacer, RefCounted);

public:
	TerrainStampPlacer();

	// ── Planet parameters ────────────────────────────────────────────────
	void set_planet_radius(float p_radius);
	float get_planet_radius() const;

	// ── Stamp management ─────────────────────────────────────────────────
	/// Add a stamp at the given world-space transform.
	/// The transform's origin is the stamp centre on the terrain surface.
	/// The transform's Y axis should point "up" (away from planet centre).
	/// Returns the stamp's index for later removal.
	int add_stamp(const Ref<TerrainStamp> &p_stamp, const Transform3D &p_transform);

	/// Remove a stamp by index.
	void remove_stamp(int p_index);

	/// Remove all stamps.
	void clear_stamps();

	/// Get the number of placed stamps.
	int get_stamp_count() const;

	/// Update the transform of an existing stamp.
	void set_stamp_transform(int p_index, const Transform3D &p_transform);
	Transform3D get_stamp_transform(int p_index) const;

	/// Get the stamp resource at an index.
	Ref<TerrainStamp> get_stamp(int p_index) const;

	// ── SDF evaluation ───────────────────────────────────────────────────
	/// Evaluate the combined SDF contribution of all nearby stamps at a
	/// single world-space position.  Returns a pair: SDF value and
	/// material index (-1 if no stamp contributes at this point).
	///
	/// base_sdf: the existing terrain SDF at this point (for blending).
	float evaluate_sdf(const Vector3 &p_world_pos, float p_base_sdf) const;

	/// Get the material index from the dominant stamp at a world position.
	/// Returns -1 if no stamp contributes here.
	int evaluate_material(const Vector3 &p_world_pos) const;

	// ── Block-level application ──────────────────────────────────────────
	/// Apply all relevant stamps to a VoxelBuffer block.
	/// This reads the existing SDF from the buffer, blends stamp contributions,
	/// and writes back.  Also writes material channel if stamps provide one.
	///
	/// p_buffer: VoxelBuffer to modify (must already contain base SDF).
	/// p_origin: block origin in voxel coordinates.
	/// p_lod: LOD level (voxel size = 1 << p_lod).
	/// Returns the number of stamps that affected this block.
	int apply_stamps(Ref<GodotVoxelBuffer> p_buffer, Vector3i p_origin, int p_lod);

	// ── Utility ──────────────────────────────────────────────────────────
	/// Create a stamp transform for placing a stamp on a spherical planet
	/// at the given surface point.  The Y axis will point radially outward.
	/// p_surface_point: point on the planet surface (world coords).
	/// p_rotation_deg: rotation around the local Y axis (degrees).
	static Transform3D make_planet_transform(const Vector3 &p_surface_point, float p_rotation_deg = 0.0f);

protected:
	static void _bind_methods();

private:
	struct PlacedStamp {
		Ref<TerrainStamp> stamp;
		Transform3D transform;
		Transform3D inverse_transform;
		AABB world_aabb;   // Pre-computed world-space bounding box
		bool active = true;
	};

	Vector<PlacedStamp> stamps;
	float planet_radius = 40000.0f;

	// Pre-compute the world AABB for a placed stamp
	AABB _compute_world_aabb(const Ref<TerrainStamp> &p_stamp, const Transform3D &p_transform) const;

	// Smooth polynomial union (same as voxel module's sdf_smooth_union)
	static float _sdf_smooth_union(float a, float b, float k);
	// Smooth polynomial subtraction
	static float _sdf_smooth_subtract(float a, float b, float k);
};

#endif // TERRAIN_STAMP_PLACER_H
