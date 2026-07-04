#ifndef VOXEL_BLOCK_FILLER_H
#define VOXEL_BLOCK_FILLER_H

#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"
#include "modules/noise/fastnoise_lite.h"
#include "modules/voxel/storage/voxel_buffer_gd.h"

// Alias for the GDScript-facing VoxelBuffer (RefCounted wrapper).
using GodotVoxelBuffer = zylann::voxel::godot::VoxelBuffer;

/// C++ accelerated per-voxel block filler for the heightmap-chunk pipeline.
///
/// Replaces the GDScript _generate_block() triple-nested loop which is the
/// #1 performance bottleneck.  A 34³-voxel block requires ~39K iterations
/// of height sampling + micro-detail noise + material lookup — moving this
/// to C++ gives 50-100× speedup.
///
/// The class holds references to:
///   - Heightmap chunk data (height, sediment, flow arrays per chunk)
///   - Micro-detail noise layers (for sub-cell LOD resolution)
///   - Voronoi + biome data (for material assignment)
///
/// Usage from GDScript:
///   var filler := VoxelBlockFiller.new()
///   filler.planet_radius = 40000.0
///   filler.set_micro_noises(micro1, micro2, micro3)
///   filler.set_voronoi_data(points, grid, point_material, warp_x, warp_y, warp_z, border_warp)
///   # In _generate_block():
///   filler.fill_block(out_buffer, origin, lod, chunk_dict, grid_n, max_terrain_height)
///
/// THREADING: This class is designed to be called from voxel worker threads.
/// All referenced data (noise objects, arrays) must be read-only after setup.

class VoxelBlockFiller : public RefCounted {
	GDCLASS(VoxelBlockFiller, RefCounted);

public:
	VoxelBlockFiller();

	// ── Planet parameters ─────────────────────────────────────────────────
	void set_planet_radius(float p_radius);
	float get_planet_radius() const;

	// ── Micro-detail noise layers ─────────────────────────────────────────
	void set_micro_noises(
		Ref<FastNoiseLite> p_micro1,
		Ref<FastNoiseLite> p_micro2,
		Ref<FastNoiseLite> p_micro3);

	void set_micro_amplitudes(float p_amp1, float p_amp2, float p_amp3);
	void set_micro_lod_cutoff(int p_cutoff);

	// ── Voronoi / material data ───────────────────────────────────────────
	/// Set up Voronoi spatial lookup for material assignment.
	/// point_material: PackedByteArray, per-Voronoi-point material ID.
	/// warp noises: for domain-warped nearest-point lookup.
	void set_voronoi_data(
		PackedVector3Array p_points,
		Array p_voronoi_grid,
		PackedByteArray p_point_material,
		Ref<FastNoiseLite> p_warp_x,
		Ref<FastNoiseLite> p_warp_y,
		Ref<FastNoiseLite> p_warp_z,
		float p_border_warp);

	// ── Core method: fill an entire VoxelBuffer block in C++ ──────────────
	/// Reads height/sediment/flow from the chunk dictionary, computes SDF
	/// and material for every voxel, writes directly into the VoxelBuffer.
	/// Returns true if the block was filled (chunk was available), false for
	/// early-out (entirely inside or outside the surface).
	///
	/// chunk_data: Dictionary mapping "face_gx_gy" keys to RefCounted chunk objects
	///   that have: height_data (PackedFloat32Array), sediment_data, flow_data,
	///   plus RES, CHUNK_SIZE_M constants and ready flag.
	///
	/// This replaces the GDScript triple-nested loop in _generate_block().
	int fill_block(
		Ref<GodotVoxelBuffer> p_buffer,
		Vector3i p_origin,
		int p_lod,
		Dictionary p_chunks,
		int p_grid_n,
		float p_max_terrain_height);

	/// River flow threshold for water material assignment.
	void set_river_flow_threshold(float p_threshold);
	float get_river_flow_threshold() const;

protected:
	static void _bind_methods();

private:
	// Planet params
	float planet_radius = 40000.0f;

	// Micro-detail noises
	Ref<FastNoiseLite> noise_micro1;
	Ref<FastNoiseLite> noise_micro2;
	Ref<FastNoiseLite> noise_micro3;
	float micro1_amp = 14.0f;
	float micro2_amp = 7.0f;
	float micro3_amp = 2.5f;
	int micro_lod_cutoff = 4;

	// Material constants
	static const int MAT_GRASS = 0;
	static const int MAT_ROCK = 1;
	static const int MAT_SNOW = 2;
	static const int MAT_SAND = 3;
	static const int MAT_DIRT = 4;
	static const int MAT_MOSS = 5;
	static const int MAT_WATER = 6;
	float river_flow_threshold = 500.0f;

	// Voronoi data for material assignment
	PackedVector3Array voronoi_points;
	int voronoi_count = 0;
	PackedByteArray point_material;

	// Voronoi spatial grid (same structure as HeightmapFiller)
	static const int GRID_RES = 16;
	struct GridCell {
		int start = 0;
		int count = 0;
	};
	GridCell grid_cells[6 * 16 * 16];
	Vector<int> grid_indices;

	// Warp noises for material domain warp
	Ref<FastNoiseLite> noise_warp_x;
	Ref<FastNoiseLite> noise_warp_y;
	Ref<FastNoiseLite> noise_warp_z;
	float border_warp = 0.20f;

	// ── Internal helpers ──────────────────────────────────────────────────

	/// Apply micro-detail noise to a base height. Thread-safe.
	float _apply_micro_detail(float base_h, const Vector3 &world_pos, int lod) const;

	/// Cubemap direction → (face, u, v).
	static void _dir_to_face_uv(const Vector3 &dir, int &r_face, float &r_u, float &r_v);

	/// Sample a heightmap chunk bilinearly at (u, v) in [0..1]².
	static float _sample_height(const float *data, int res, float u, float v);
	static float _sample_float_array(const float *data, int res, float u, float v);

	/// Voronoi nearest-point lookup on the spatial grid.
	void _point_to_face_cell(const Vector3 &p, int &r_face, int &r_cu, int &r_cv) const;
	int _get_nearest(const Vector3 &dir) const;

	/// Warp a direction using the 3 warp noises.
	Vector3 _warp_dir(const Vector3 &dir) const;

	/// Look up material ID for a surface point.
	int _lookup_material(const Vector3 &unit_dir, float height_m, float sediment_m, float flow_accum) const;

	static inline float _smoothstep(float edge0, float edge1, float x) {
		float t = CLAMP((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}
};

#endif // VOXEL_BLOCK_FILLER_H
