#ifndef GPU_HEIGHTMAP_FILLER_H
#define GPU_HEIGHTMAP_FILLER_H

#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"
#include "servers/rendering/rendering_device.h"

/// Tier 2+3: GPU-accelerated heightmap generation + grid-based erosion.
///
/// Combines three passes into a single GPU pipeline:
///   Pass 1 — Heightmap fill (1024² cells in parallel, ~5-10ms)
///   Pass 2 — Grid-based shallow-water erosion (soillib-inspired, ~20-50ms)
///   Pass 3 — Thermal cascading / talus angle relaxation (~5ms)
///
/// All passes run on Vulkan compute via Godot's RenderingDevice.
/// This replaces both HeightmapFiller (CPU) and GPUErosion (particle-based).
///
/// Usage from GDScript:
///   var gpu_fill := GPUHeightmapFiller.new()
///   gpu_fill.planet_radius = 40000.0
///   gpu_fill.set_tectonic_ssbo(packed_tectonic_data)
///   var result: Dictionary = gpu_fill.generate_chunk(face, gx, gy, grid_n, resolution, seed)
///   var height = result["height"]       # PackedFloat32Array
///   var sediment = result["sediment"]   # PackedFloat32Array

class GPUHeightmapFiller : public RefCounted {
	GDCLASS(GPUHeightmapFiller, RefCounted);

public:
	GPUHeightmapFiller();
	~GPUHeightmapFiller();

	// ── Parameters ────────────────────────────────────────────────────────
	void set_planet_radius(float p_radius);
	float get_planet_radius() const;

	void set_border_warp(float p_warp);
	float get_border_warp() const;

	/// Upload packed tectonic data as a single flat buffer for GPU access.
	/// Format: [N_POINTS, voronoi_xyz × N, plate_id × N, falloff × N,
	///          bnd_type × N, is_oceanic × N, elev_bias × N_PLATES,
	///          cross_plate_ang × N, grid_data...]
	void set_tectonic_data(PackedFloat32Array p_packed_data);

	/// Set noise seeds — the GPU generates its own noise procedurally.
	/// 8 seed ints (one per noise layer) + amplitudes + half-widths.
	void set_noise_params(PackedFloat32Array p_noise_config);

	// ── Erosion parameters ────────────────────────────────────────────────
	void set_erosion_iterations(int p_iters);
	int get_erosion_iterations() const;

	void set_erosion_rate(float p_rate);
	float get_erosion_rate() const;

	void set_deposition_rate(float p_rate);
	float get_deposition_rate() const;

	void set_thermal_iterations(int p_iters);
	int get_thermal_iterations() const;

	void set_talus_angle(float p_angle);
	float get_talus_angle() const;

	/// Returns true if the GPU pipeline initialised successfully.
	bool is_gpu_available() const;

	// ── Core method ───────────────────────────────────────────────────────
	/// Generate a chunk: fill heightmap + erode + thermal cascade, all on GPU.
	/// Returns Dictionary with "height", "sediment", "flow" PackedFloat32Arrays.
	Dictionary generate_chunk(int p_face, int p_gx, int p_gy, int p_grid_n,
			int p_resolution, int p_seed);

protected:
	static void _bind_methods();

private:
	// Parameters
	float planet_radius = 40000.0f;
	float border_warp = 0.20f;
	int erosion_iterations = 64;
	float erosion_rate_val = 0.5f;
	float deposition_rate_val = 0.3f;
	int thermal_iterations = 8;
	float talus_angle = 0.6f; // tangent of max stable slope

	// GPU resources
	RenderingDevice *rd = nullptr;
	RID fill_shader_rid;
	RID fill_pipeline_rid;
	RID erode_shader_rid;
	RID erode_pipeline_rid;
	RID thermal_shader_rid;
	RID thermal_pipeline_rid;
	RID flow_shader_rid;
	RID flow_pipeline_rid;
	bool gpu_ready = false;

	// Tectonic SSBO (uploaded once, reused for all chunks)
	RID tectonic_ssbo_rid;
	bool tectonic_uploaded = false;

	// Noise config SSBO
	RID noise_config_rid;
	bool noise_uploaded = false;

	void _init_gpu();
	void _cleanup_gpu();
	RID _compile_shader(const char *glsl_source, const char *name);

	// ── Params struct for chunk dispatch ──────────────────────────────────
	struct ChunkParams {
		int32_t face;
		int32_t gx;
		int32_t gy;
		int32_t grid_n;
		int32_t resolution;
		int32_t seed;
		float planet_radius;
		float border_warp;
		// Erosion params
		int32_t erosion_iters;
		float erosion_rate;
		float deposition_rate;
		float talus_angle_tan;
		int32_t thermal_iters;
		int32_t _pad0;
		int32_t _pad1;
		int32_t _pad2;
	};
};

#endif // GPU_HEIGHTMAP_FILLER_H
