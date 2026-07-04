#ifndef GPU_EROSION_H
#define GPU_EROSION_H

#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"
#include "servers/rendering/rendering_device.h"

/// GPU-accelerated hydraulic erosion via Vulkan compute shaders.
///
/// Uses RenderingDevice to dispatch thousands of erosion particles in parallel.
/// Each particle runs independently on the GPU — the heightmap is stored in a
/// storage buffer (SSBO). At typical grid sizes (512²–4096²), the RTX 3070
/// can process 100K+ particles in under 10ms.
///
/// Usage from GDScript:
///   var gpu_ero := GPUErosion.new()
///   gpu_ero.resolution = 1024
///   gpu_ero.num_particles = 100000
///   var result: Dictionary = gpu_ero.erode(height_pfa, sediment_pfa, seed)
///   height_pfa = result["height"]
///   sediment_pfa = result["sediment"]

class GPUErosion : public RefCounted {
	GDCLASS(GPUErosion, RefCounted);

public:
	GPUErosion();
	~GPUErosion();

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

	/// Returns true if the GPU pipeline was initialised successfully.
	bool is_gpu_available() const;

	// ── Core method ───────────────────────────────────────────────────────
	Dictionary erode(PackedFloat32Array p_height, PackedFloat32Array p_sediment, int p_seed);

protected:
	static void _bind_methods();

private:
	// Parameters
	int resolution = 128;
	int num_particles = 50000;
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

	// GPU resources
	RenderingDevice *rd = nullptr;
	RID shader_rid;
	RID pipeline_rid;
	bool gpu_ready = false;

	void _init_gpu();
	void _cleanup_gpu();

	// Brush building (same as CPU version, but packed for GPU upload)
	struct BrushEntry {
		int dx;
		int dy;
		float weight;
	};
	Vector<BrushEntry> brush;
	void _build_brush();

	// Params struct — must match GLSL layout exactly (std430)
	struct GPUParams {
		int32_t resolution;
		int32_t num_particles;
		int32_t erosion_radius;
		int32_t max_lifetime;
		float inertia;
		float capacity_mult;
		float deposition_rate;
		float erosion_rate;
		float gravity;
		float evaporation_rate;
		float min_capacity;
		float min_height;
		int32_t seed;
		int32_t _pad0;
		int32_t _pad1;
		int32_t _pad2;
	};
};

#endif // GPU_EROSION_H
