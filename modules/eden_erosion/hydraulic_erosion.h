#ifndef HYDRAULIC_EROSION_H
#define HYDRAULIC_EROSION_H

#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

/// High-performance particle-based hydraulic erosion.
///
/// Based on Hans Beyer / Ivo van der Veen's gradient-descent model:
///   • Drop a water particle at a random position on a heightmap grid.
///   • Move it downhill via bilinear gradient, blended with inertia.
///   • Compute carrying capacity from slope × velocity × water volume.
///   • If carrying < sediment → deposit.  If carrying > sediment → erode.
///   • Evaporate water each step.  Kill after max lifetime.
///
/// Exposed to GDScript as HydraulicErosion.  All heavy work happens in C++.
/// Typical speedup over GDScript: 50–150×.
///
/// Usage from GDScript:
///   var ero := HydraulicErosion.new()
///   ero.set_resolution(128)
///   ero.set_num_particles(5000)
///   var result: Dictionary = ero.erode(height_pfa, sediment_pfa, seed)
///   height_pfa = result["height"]
///   sediment_pfa = result["sediment"]

class HydraulicErosion : public RefCounted {
	GDCLASS(HydraulicErosion, RefCounted);

public:
	HydraulicErosion();

	// ── Parameters ────────────────────────────────────────────────────────
	void set_resolution(int p_res);
	int get_resolution() const;

	void set_num_particles(int p_n);
	int get_num_particles() const;

	void set_inertia(float p_val);
	float get_inertia() const;

	void set_capacity_mult(float p_val);
	float get_capacity_mult() const;

	void set_deposition_rate(float p_val);
	float get_deposition_rate() const;

	void set_erosion_rate(float p_val);
	float get_erosion_rate() const;

	void set_gravity(float p_val);
	float get_gravity() const;

	void set_evaporation_rate(float p_val);
	float get_evaporation_rate() const;

	void set_erosion_radius(int p_val);
	int get_erosion_radius() const;

	void set_max_lifetime(int p_val);
	int get_max_lifetime() const;

	void set_min_capacity(float p_val);
	float get_min_capacity() const;

	void set_min_height(float p_val);
	float get_min_height() const;

	// ── Core method ───────────────────────────────────────────────────────
	/// Runs particle erosion on the given heightmap and sediment arrays.
	/// Both arrays must have length == resolution * resolution.
	/// Returns a Dictionary { "height": PackedFloat32Array, "sediment": PackedFloat32Array,
	///                        "completed": int }
	Dictionary erode(PackedFloat32Array p_height, PackedFloat32Array p_sediment, int p_seed);

protected:
	static void _bind_methods();

private:
	// Parameters
	int resolution = 128;
	int num_particles = 5000;
	float inertia = 0.3f;
	float capacity_mult = 10.0f;
	float deposition_rate = 0.08f;
	float erosion_rate = 0.7f;
	float gravity_val = 9.81f;
	float evaporation_rate = 0.02f;
	int erosion_radius = 5;
	int max_lifetime = 300;
	float min_capacity = 0.01f;
	float min_height = -5000.0f;

	// Pre-computed brush
	struct BrushEntry {
		int dx;
		int dy;
		float weight;
	};
	Vector<BrushEntry> brush;
	void _build_brush();
};

#endif // HYDRAULIC_EROSION_H
