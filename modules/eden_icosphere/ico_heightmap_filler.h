#ifndef ICO_HEIGHTMAP_FILLER_H
#define ICO_HEIGHTMAP_FILLER_H

#include "icosphere_mapper.h"
#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"
#include "modules/noise/fastnoise_lite.h"

/// IcoHeightmapFiller — C++ accelerated triangular heightmap tile generator.
///
/// Fills a single icosahedral tile (triangle on the sphere) with terrain height
/// data using the same multi-layer noise + tectonic pipeline as HeightmapFiller,
/// but mapped to triangular grids instead of rectangular ones.
///
/// Each tile's heightmap has (R+1)(R+2)/2 samples in a triangular grid layout.
/// R=2048 gives 2,098,177 samples per tile — about 2× the density of a 1024²
/// square grid, with no pole distortion.
///
/// Supports:
///   - OpenMP parallel fill (row-parallel)
///   - Seamless edge stitching (shared vertices along triangle edges)
///   - Batch generation of all tiles (or a subset) with work stealing
///
/// THREADING: After set_*() calls, fill_tile() is thread-safe (read-only state).

class IcoHeightmapFiller : public RefCounted {
	GDCLASS(IcoHeightmapFiller, RefCounted);

public:
	IcoHeightmapFiller();

	// ── Configuration (call before fill) ──────────────────────────────────
	void set_planet_radius(float p_radius);
	float get_planet_radius() const;

	void set_border_warp(float p_warp);
	float get_border_warp() const;

	/// Set the IcosphereMapper instance (must be build()-ed already).
	void set_mapper(Ref<IcosphereMapper> p_mapper);

	/// Set noise layers (same as HeightmapFiller).
	void set_noise_layers(
		Ref<FastNoiseLite> p_base,
		Ref<FastNoiseLite> p_continent,
		Ref<FastNoiseLite> p_plateau,
		Ref<FastNoiseLite> p_oceanic,
		Ref<FastNoiseLite> p_mountain,
		Ref<FastNoiseLite> p_trench,
		Ref<FastNoiseLite> p_rift,
		Ref<FastNoiseLite> p_detail);

	void set_warp_noises(
		Ref<FastNoiseLite> p_warp_x,
		Ref<FastNoiseLite> p_warp_y,
		Ref<FastNoiseLite> p_warp_z);

	/// Set tectonic data (same format as HeightmapFiller).
	void set_tectonic_data(
		PackedVector3Array p_voronoi_points,
		Array p_voronoi_grid,
		PackedInt32Array p_plate_id,
		PackedFloat32Array p_falloff,
		PackedByteArray p_bnd_type,
		PackedByteArray p_is_oceanic,
		PackedFloat32Array p_plate_elev_bias,
		PackedFloat32Array p_cross_plate_ang);

	void set_amplitudes(float p_base, float p_continent, float p_plateau,
		float p_oceanic, float p_mountain, float p_trench, float p_rift, float p_detail);

	void set_half_widths(float p_mtn, float p_trench, float p_rift);

	// ── Core generation methods ───────────────────────────────────────────

	/// Fill a single triangular tile's heightmap.
	/// Returns PackedFloat32Array of (R+1)(R+2)/2 height values.
	/// Thread-safe after configuration.
	PackedFloat32Array fill_tile(int p_face, int p_resolution);

	/// Fill ALL tiles in parallel. Returns Dictionary mapping face_index → PackedFloat32Array.
	/// Uses OpenMP + WorkerThreadPool for maximum throughput.
	Dictionary fill_all_tiles(int p_resolution);

	/// Fill a range of tiles [start, end). Returns Dictionary.
	Dictionary fill_tile_range(int p_start, int p_end, int p_resolution);

protected:
	static void _bind_methods();

private:
	float planet_radius = 40000.0f;
	float border_warp = 0.20f;
	Ref<IcosphereMapper> mapper;

	// Noise layers
	Ref<FastNoiseLite> noise_base;
	Ref<FastNoiseLite> noise_continent;
	Ref<FastNoiseLite> noise_plateau;
	Ref<FastNoiseLite> noise_oceanic;
	Ref<FastNoiseLite> noise_mountain;
	Ref<FastNoiseLite> noise_trench;
	Ref<FastNoiseLite> noise_rift;
	Ref<FastNoiseLite> noise_detail;

	// Warp noises
	Ref<FastNoiseLite> noise_warp_x;
	Ref<FastNoiseLite> noise_warp_y;
	Ref<FastNoiseLite> noise_warp_z;

	// Amplitudes
	float base_noise_amp = 60.0f;
	float continent_noise_amp = 120.0f;
	float plateau_noise_amp = 80.0f;
	float oceanic_noise_amp = 50.0f;
	float mountain_noise_amp = 560.0f;
	float trench_noise_amp = 360.0f;
	float rift_noise_amp = 140.0f;
	float detail_noise_amp = 20.0f;

	// Half-widths
	float mtn_half_width = 5000.0f;
	float trench_half_width = 4000.0f;
	float rift_half_width = 5000.0f;

	// ── Tectonic data ─────────────────────────────────────────────────────
	PackedVector3Array voronoi_points;
	int voronoi_count = 0;
	static const int GRID_RES = 16;
	struct GridCell {
		int start = 0;
		int count = 0;
	};
	GridCell grid_cells[6 * 16 * 16];
	Vector<int> grid_indices;
	PackedInt32Array plate_id;
	PackedFloat32Array falloff;
	PackedByteArray bnd_type;
	PackedByteArray is_oceanic_pt;
	PackedFloat32Array plate_elev_bias;
	PackedFloat32Array cross_plate_ang;

	// ── Internal helpers ──────────────────────────────────────────────────
	void _point_to_face_uv(const Vector3 &p, int &r_face, int &r_cu, int &r_cv) const;
	Vector3 _warp_dir(const Vector3 &dir) const;
	void _get_two_nearest(const Vector3 &dir, int &r_idx_a, int &r_idx_b,
		float &r_dot_a, float &r_dot_b) const;
	void _get_terrain_data(const Vector3 &unit_dir,
		float &r_falloff, float &r_oceanic, float &r_bias,
		int &r_bnd, float &r_border_dist_rad) const;
	float _compute_base_height(const Vector3 &world_pos, const Vector3 &unit_dir) const;

	static inline float _smoothstep(float edge0, float edge1, float x) {
		float t = CLAMP((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}
};

#endif // ICO_HEIGHTMAP_FILLER_H
