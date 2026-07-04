#ifndef PLANET_TECTONICS_H
#define PLANET_TECTONICS_H

#include "core/io/resource.h"
#include "core/math/random_number_generator.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "modules/noise/fastnoise_lite.h"
#include "voronoi_sphere.h"

// C++ port of plate_tectonics.gd — generates tectonic plates and provides
// per-direction terrain data queries.
//
// GDCLASS(PlanetTectonics, RefCounted) so it can be exposed to GDScript for
// debugging, but the primary consumer is EdenPlanetGenerator (C++).
//
// Thread-safe: all data is read-only after generate(). Multiple threads can
// call get_terrain_data() simultaneously.

class PlanetTectonics : public Resource {
	GDCLASS(PlanetTectonics, Resource);

public:
	// Boundary type classification.
	enum BndType {
		BND_INTERIOR = 0,
		BND_MOUNTAIN = 1,
		BND_TRENCH = 2,
		BND_RIFT = 3,
		BND_PASSIVE = 4,
		BND_ISLAND_ARC = 5,
		BND_OCEAN_INT = 6,
		BND_XFORM = 7,
	};

	// Terrain query result — matches GDScript get_terrain_data() layout.
	// No heap allocation.
	struct TerrainData {
		float falloff_s = 0.0f;       // [0] smooth blended falloff
		float oceanic_s = 0.0f;       // [1] smooth blended oceanic fraction
		float bias_s = 0.0f;          // [2] smooth blended elevation bias (m)
		int bnd_type_a = BND_INTERIOR; // [3] boundary type of nearest point
		float border_dist_rad = 1.0f; // [4] angular distance to plate border (rad)
		float falloff_a = 0.0f;       // [5] falloff of nearest point
		float falloff_b = 0.0f;       // [6] falloff of second-nearest point
		float oceanic_a = 0.0f;       // [7] is_oceanic of nearest (0/1)
		float oceanic_b = 0.0f;       // [8] is_oceanic of second-nearest (0/1)
		float bias_a = 0.0f;          // [9] elevation bias of nearest plate
		float bias_b = 0.0f;          // [10] elevation bias of second-nearest plate
		float t = 0.0f;               // [11] blend factor (0=A, 1=B)
		int bnd_type_b = BND_INTERIOR; // [12] boundary type of second-nearest
	};

	PlanetTectonics();

	void configure(const Dictionary &p_params);
	void generate(int p_seed);

	// Direct C++ query — no allocation, struct return.
	TerrainData get_terrain_data(const Vector3 &p_dir) const;

	// GDScript wrapper returning Array (for debug).
	Array get_terrain_data_gd(const Vector3 &p_dir) const;

	const VoronoiSphere &get_voronoi() const { return voronoi; }
	int get_n_plates() const { return n_plates; }
	const Vector<int> &get_plate_ids() const { return plate_id; }
	const Vector<float> &get_falloff_values() const { return falloff; }
	const Vector<uint8_t> &get_boundary_types() const { return bnd_type; }
	const Vector<uint8_t> &get_point_oceanic_flags() const { return is_oceanic_pt; }
	const Vector<float> &get_elevations() const { return elevation; }
	const Vector<Vector3> &get_plate_movements() const { return plate_movement; }
	const Vector<uint8_t> &get_plate_oceanic_flags() const { return plate_oceanic; }
	const Vector<int> &get_neighbor_offsets() const { return nbr_offsets; }
	const Vector<int> &get_neighbor_data() const { return nbr_data; }
	Vector3 warp_direction(const Vector3 &p_dir) const { return _warp_dir(p_dir); }

protected:
	static void _bind_methods();

private:
	// ── Tunables ─────────────────────────────────────────────────────────
	int n_points = 1200;
	int n_plates = 16;
	float plate_freq = 0.55f;
	int plate_octaves = 6;
	float plate_lacunarity = 2.0f;
	float plate_gain = 0.50f;
	float oceanic_fraction = 0.68f;
	int border_hops = 7;
	float collision_threshold = 0.02f;
	float border_warp = 0.20f;
	float warp_freq = 1.8f;

	// ── Voronoi ──────────────────────────────────────────────────────────
	VoronoiSphere voronoi;

	// ── Per-point data (packed arrays, read-only after generate()) ────────
	Vector<int> plate_id;              // [n_points] plate index 0..n_plates-1
	Vector<float> falloff;             // [n_points] 0=border, 1=interior
	Vector<uint8_t> bnd_type;          // [n_points] BndType value
	Vector<uint8_t> is_oceanic_pt;     // [n_points] 1=oceanic plate
	Vector<float> elevation;           // [n_points] debug elevation estimate
	Vector<float> cross_plate_ang;     // [n_points] angular dist to nearest other-plate

	// ── Per-plate data ───────────────────────────────────────────────────
	Vector<uint8_t> plate_oceanic;     // [n_plates] 1=oceanic
	Vector<Vector3> plate_movement;    // [n_plates] tangent drift vector
	Vector<float> plate_elev_bias;     // [n_plates] base elevation bias (m)

	// ── Neighbour graph ──────────────────────────────────────────────────
	// Flat array + offset table for O(1) neighbour lookup.
	Vector<int> nbr_offsets;           // [n_points + 1] start offset per point
	Vector<int> nbr_data;              // flat neighbour indices

	// ── Domain warp noises ───────────────────────────────────────────────
	Ref<FastNoiseLite> noise_warp_x;
	Ref<FastNoiseLite> noise_warp_y;
	Ref<FastNoiseLite> noise_warp_z;

	// ── Internal generation steps ────────────────────────────────────────
	void _assign_plate_ids(RandomNumberGenerator &rng);
	void _build_neighbour_graph();
	void _compute_falloff();
	void _classify_boundaries(RandomNumberGenerator &rng);
	void _bake_elevation();
	void _bake_cross_plate_dist();

	float _compression(int pa, int pb, int ia, int ib) const;
	Vector3 _warp_dir(const Vector3 &dir) const;

	static inline float _smoothstep(float edge0, float edge1, float x) {
		float t = CLAMP((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}
};

VARIANT_ENUM_CAST(PlanetTectonics::BndType);

#endif // PLANET_TECTONICS_H
