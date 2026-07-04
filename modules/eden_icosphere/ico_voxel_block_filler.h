#ifndef ICO_VOXEL_BLOCK_FILLER_H
#define ICO_VOXEL_BLOCK_FILLER_H

#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"
#include "modules/noise/fastnoise_lite.h"
#include "modules/voxel/storage/voxel_buffer_gd.h"

using GodotVoxelBuffer = zylann::voxel::godot::VoxelBuffer;

/// C++ accelerated per-voxel material filler for the on-the-fly icosphere pipeline.
///
/// Works with VoxelLodTerrain's GPU SDF generation: after the GPU fills the
/// SDF channel, this class fills CHANNEL_INDICES and CHANNEL_WEIGHTS in C++
/// via the _generate_materials() callback. It skips voxels far from the surface
/// (reads SDF to decide) and only computes materials for near-surface voxels.
///
/// Uses domain-warped Voronoi lookup for biome-driven material assignment,
/// identical to VoxelBlockFiller but without heightmap chunk dependency.
/// All data is passed during setup and is read-only from worker threads.
///
/// THREADING: After set_*() calls, fill_materials() is thread-safe.

class IcoVoxelBlockFiller : public RefCounted {
	GDCLASS(IcoVoxelBlockFiller, RefCounted);

public:
	IcoVoxelBlockFiller();

	// ── Planet parameters ─────────────────────────────────────────────────
	void set_planet_radius(float p_radius);
	float get_planet_radius() const;

	void set_max_terrain_height(float p_height);
	float get_max_terrain_height() const;

	// ── Voronoi / material data ───────────────────────────────────────────
	void set_voronoi_data(
		PackedVector3Array p_points,
		Array p_voronoi_grid,
		PackedByteArray p_point_material,
		Ref<FastNoiseLite> p_warp_x,
		Ref<FastNoiseLite> p_warp_y,
		Ref<FastNoiseLite> p_warp_z,
		float p_border_warp);

	// ── Core method: fill material channels in a VoxelBuffer ──────────────
	/// Reads SDF from CHANNEL_SDF to identify near-surface voxels.
	/// For each near-surface voxel, looks up the biome material via
	/// domain-warped Voronoi and writes CHANNEL_INDICES + CHANNEL_WEIGHTS.
	///
	/// This replaces the GDScript _generate_materials() triple loop.
	void fill_materials(
		Ref<GodotVoxelBuffer> p_buffer,
		Vector3i p_origin,
		int p_lod);

	/// River flow threshold for water material assignment.
	void set_river_flow_threshold(float p_threshold);
	float get_river_flow_threshold() const;

protected:
	static void _bind_methods();

private:
	float planet_radius = 40000.0f;
	float max_terrain_height = 5000.0f;

	// Material constants
	static const int MAT_GRASS = 0;
	static const int MAT_ROCK = 1;
	static const int MAT_SNOW = 2;
	static const int MAT_SAND = 3;
	static const int MAT_DIRT = 4;
	static const int MAT_MOSS = 5;
	static const int MAT_WATER = 6;
	float river_flow_threshold = 500.0f;

	// Voronoi data
	PackedVector3Array voronoi_points;
	int voronoi_count = 0;
	PackedByteArray point_material;

	// Cubemap spatial grid
	static const int GRID_RES = 16;
	struct GridCell {
		int start = 0;
		int count = 0;
	};
	GridCell grid_cells[6 * 16 * 16];
	Vector<int> grid_indices;

	// Domain warp noises
	Ref<FastNoiseLite> noise_warp_x;
	Ref<FastNoiseLite> noise_warp_y;
	Ref<FastNoiseLite> noise_warp_z;
	float border_warp = 0.20f;

	// ── Internal helpers ──────────────────────────────────────────────────
	static void _point_to_face_cell(const Vector3 &p, int &r_face, int &r_cu, int &r_cv);
	int _get_nearest(const Vector3 &dir) const;
	Vector3 _warp_dir(const Vector3 &dir) const;
	int _lookup_material(const Vector3 &unit_dir) const;
};

#endif // ICO_VOXEL_BLOCK_FILLER_H
