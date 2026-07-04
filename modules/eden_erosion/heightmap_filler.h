#ifndef HEIGHTMAP_FILLER_H
#define HEIGHTMAP_FILLER_H

#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"
#include "modules/noise/fastnoise_lite.h"

/// C++ accelerated heightmap chunk filler.
///
/// Replaces the GDScript _compute_base_height() loop which takes ~4 seconds
/// per 512² chunk. Moving the tight loop (262K iterations × 8 noise samples
/// + Voronoi lookup + tectonic blending) into C++ gives ~50-100× speedup.
///
/// Usage from GDScript:
///   var filler := HeightmapFiller.new()
///   filler.set_planet_radius(40000.0)
///   filler.set_noise_layers(base, continent, plateau, oceanic, mountain, trench, rift, detail)
///   filler.set_warp_noises(warp_x, warp_y, warp_z)
///   filler.set_tectonic_data(voronoi_points, voronoi_grid, plate_id, falloff, bnd_type,
///                            is_oceanic, plate_elev_bias, cross_plate_ang)
///   var height_data: PackedFloat32Array = filler.fill_chunk(face, gx, gy, grid_n, resolution)

class HeightmapFiller : public RefCounted {
	GDCLASS(HeightmapFiller, RefCounted);

public:
	HeightmapFiller();

	// ── Planet parameters ─────────────────────────────────────────────────
	void set_planet_radius(float p_radius);
	float get_planet_radius() const;

	// ── Noise layers (set once, reused for all chunks) ────────────────────
	void set_noise_layers(
		Ref<FastNoiseLite> p_base,
		Ref<FastNoiseLite> p_continent,
		Ref<FastNoiseLite> p_plateau,
		Ref<FastNoiseLite> p_oceanic,
		Ref<FastNoiseLite> p_mountain,
		Ref<FastNoiseLite> p_trench,
		Ref<FastNoiseLite> p_rift,
		Ref<FastNoiseLite> p_detail);

	// ── Warp noises (for tectonic border warping) ─────────────────────────
	void set_warp_noises(
		Ref<FastNoiseLite> p_warp_x,
		Ref<FastNoiseLite> p_warp_y,
		Ref<FastNoiseLite> p_warp_z);

	void set_border_warp(float p_warp);
	float get_border_warp() const;

	// ── Tectonic data arrays (baked once by PlateTectonics) ───────────────
	/// voronoi_points: PackedVector3Array of all Voronoi seed points on unit sphere.
	/// voronoi_grid: Array of 6 faces, each face is Array of GRID_RES² PackedInt32Arrays.
	/// plate_id, falloff, bnd_type, is_oceanic: per-Voronoi-point arrays.
	/// plate_elev_bias: per-plate elevation bias (N_PLATES entries).
	/// cross_plate_ang: per-point angular distance to nearest cross-plate point.
	void set_tectonic_data(
		PackedVector3Array p_voronoi_points,
		Array p_voronoi_grid,
		PackedInt32Array p_plate_id,
		PackedFloat32Array p_falloff,
		PackedByteArray p_bnd_type,
		PackedByteArray p_is_oceanic,
		PackedFloat32Array p_plate_elev_bias,
		PackedFloat32Array p_cross_plate_ang);

	// ── Amplitude parameters ──────────────────────────────────────────────
	void set_amplitudes(float p_base, float p_continent, float p_plateau,
		float p_oceanic, float p_mountain, float p_trench, float p_rift, float p_detail);

	void set_half_widths(float p_mtn, float p_trench, float p_rift);

	// ── Core method ───────────────────────────────────────────────────────
	/// Fills a chunk heightmap. Returns PackedFloat32Array of resolution² floats.
	PackedFloat32Array fill_chunk(int p_face, int p_gx, int p_gy, int p_grid_n, int p_resolution);

protected:
	static void _bind_methods();

private:
	// Planet params
	float planet_radius = 40000.0f;
	float border_warp = 0.20f;

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

	// Amplitude constants
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

	// ── Tectonic data (read-only after set) ───────────────────────────────
	PackedVector3Array voronoi_points;
	int voronoi_count = 0;

	// Flat spatial grid: 6 faces × GRID_RES² cells.
	// Each cell stores start index + count into voronoi_grid_indices.
	static const int GRID_RES = 16;
	struct GridCell {
		int start = 0;
		int count = 0;
	};
	GridCell grid_cells[6 * 16 * 16]; // 6 faces × 16 × 16
	Vector<int> grid_indices;          // flat packed point indices

	PackedInt32Array plate_id;
	PackedFloat32Array falloff;
	PackedByteArray bnd_type;
	PackedByteArray is_oceanic_pt;
	PackedFloat32Array plate_elev_bias;
	PackedFloat32Array cross_plate_ang;

	// ── Internal helpers ──────────────────────────────────────────────────
	void _point_to_face_uv(const Vector3 &p, int &r_face, int &r_cu, int &r_cv) const;

	/// Warp a direction using the 3 warp noises + border_warp strength.
	Vector3 _warp_dir(const Vector3 &dir) const;

	/// Two-nearest Voronoi query on the spatial grid.
	void _get_two_nearest(const Vector3 &dir, int &r_idx_a, int &r_idx_b,
		float &r_dot_a, float &r_dot_b) const;

	/// Full terrain data query (reimplements PlateTectonics.get_terrain_data).
	/// Writes falloff, oceanic, bias, bnd, border_dist_rad.
	void _get_terrain_data(const Vector3 &unit_dir,
		float &r_falloff, float &r_oceanic, float &r_bias,
		int &r_bnd, float &r_border_dist_rad) const;

	/// Compute base height at a world position (reimplements _compute_base_height).
	float _compute_base_height(const Vector3 &world_pos, const Vector3 &unit_dir) const;

	/// Face UV to direction (reimplements _face_uv_to_dir).
	static Vector3 _face_uv_to_dir(int face, float u, float v);

	/// Smoothstep helper.
	static inline float _smoothstep(float edge0, float edge1, float x) {
		float t = CLAMP((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}
};

#endif // HEIGHTMAP_FILLER_H
