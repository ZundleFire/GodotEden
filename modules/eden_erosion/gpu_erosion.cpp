#include "gpu_erosion.h"
#include "core/math/math_defs.h"
#include "core/os/os.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server.h"
#include <cmath>
#include <cstring>

// ── Embedded GLSL compute shader source ──────────────────────────────────
// This is compiled to SPIR-V at runtime by Godot's RenderingDevice.

// Fixed-point scale: 1 float metre = 1024 integer units.
// This gives ~0.001m precision with int32 atomics, and ±2 million metre range.
static const float FP_SCALE = 1024.0f;
static const float FP_INV = 1.0f / 1024.0f;

static const char *EROSION_GLSL = R"(
#version 450

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// Height and sediment stored as fixed-point int32 for atomic operations.
// CPU converts float→int before upload and int→float after readback.
// Scale: 1 metre = 1024 units (~0.001m precision).
layout(set = 0, binding = 0, std430) coherent buffer HeightBuffer {
    int height_fp[];
};

layout(set = 0, binding = 1, std430) coherent buffer SedimentBuffer {
    int sediment_fp[];
};

layout(set = 0, binding = 2, std430) restrict readonly buffer ParamsBuffer {
    int u_resolution;
    int u_num_particles;
    int u_erosion_radius;
    int u_max_lifetime;
    float u_inertia;
    float u_capacity_mult;
    float u_deposition_rate;
    float u_erosion_rate;
    float u_gravity;
    float u_evaporation_rate;
    float u_min_capacity;
    float u_min_height;
    int u_seed;
    int _pad0;
    int _pad1;
    int _pad2;
};

layout(set = 0, binding = 3, std430) restrict readonly buffer BrushBuffer {
    int brush_count;
    int brush_data[];  // [dx, dy, weight_as_int] × brush_count
};

const float FP_INV = 1.0 / 1024.0;
const int FP_SCALE = 1024;

// ── Read height as float from fixed-point buffer ─────────────────────────
float read_h(int idx) {
    return float(height_fp[idx]) * FP_INV;
}

// ── LCG RNG ──────────────────────────────────────────────────────────────
uint rng_state;

float next_rand() {
    rng_state = rng_state * 1103515245u + 12345u;
    return float(rng_state & 0x7FFFFFFFu) / float(0x7FFFFFFF);
}

void main() {
    uint particle_id = gl_GlobalInvocationID.x;
    if (particle_id >= uint(u_num_particles)) {
        return;
    }

    int N = u_resolution;

    // Seed RNG per particle — large prime multiplier for good spread
    rng_state = uint(u_seed) ^ (particle_id * 2654435761u);
    next_rand(); next_rand(); // warm up

    float pos_x = next_rand() * float(N - 1);
    float pos_y = next_rand() * float(N - 1);
    float dir_x = 0.0;
    float dir_y = 0.0;
    float speed = 1.0;
    float water = 1.0;
    float sediment = 0.0;

    for (int step = 0; step < u_max_lifetime; step++) {
        int ix = int(pos_x);
        int iy = int(pos_y);
        if (ix < 0 || ix >= N - 1 || iy < 0 || iy >= N - 1) break;

        float fx = pos_x - float(ix);
        float fy = pos_y - float(iy);

        int idx00 = iy * N + ix;
        int idx10 = idx00 + 1;
        int idx01 = idx00 + N;
        int idx11 = idx01 + 1;

        float h00 = read_h(idx00);
        float h10 = read_h(idx10);
        float h01 = read_h(idx01);
        float h11 = read_h(idx11);

        float grad_x = (h10 - h00) * (1.0 - fy) + (h11 - h01) * fy;
        float grad_y = (h01 - h00) * (1.0 - fx) + (h11 - h10) * fx;

        dir_x = dir_x * u_inertia - grad_x * (1.0 - u_inertia);
        dir_y = dir_y * u_inertia - grad_y * (1.0 - u_inertia);

        float dir_len = sqrt(dir_x * dir_x + dir_y * dir_y);
        if (dir_len < 0.0001) {
            float angle = next_rand() * 6.28318530718;
            dir_x = cos(angle);
            dir_y = sin(angle);
        } else {
            dir_x /= dir_len;
            dir_y /= dir_len;
        }

        float new_x = pos_x + dir_x;
        float new_y = pos_y + dir_y;
        if (new_x < 0.0 || new_x >= float(N - 1) ||
                new_y < 0.0 || new_y >= float(N - 1)) break;

        int nix = int(new_x);
        int niy = int(new_y);
        float nfx = new_x - float(nix);
        float nfy = new_y - float(niy);
        int nidx00 = niy * N + nix;

        float nh = read_h(nidx00) * (1.0 - nfx) * (1.0 - nfy) +
                read_h(nidx00 + 1) * nfx * (1.0 - nfy) +
                read_h(nidx00 + N) * (1.0 - nfx) * nfy +
                read_h(nidx00 + N + 1) * nfx * nfy;

        float h_old = h00 * (1.0 - fx) * (1.0 - fy) +
                h10 * fx * (1.0 - fy) +
                h01 * (1.0 - fx) * fy +
                h11 * fx * fy;

        float h_diff = nh - h_old;
        float capacity = max(-h_diff, u_min_capacity) * speed * water * u_capacity_mult;

        if (sediment > capacity || h_diff > 0.0) {
            // ── Deposit (atomicAdd positive deltas) ─────────────────────
            float deposit_amount;
            if (h_diff > 0.0) {
                deposit_amount = min(sediment, h_diff);
            } else {
                deposit_amount = (sediment - capacity) * u_deposition_rate;
            }
            sediment -= deposit_amount;

            float w00 = (1.0 - fx) * (1.0 - fy);
            float w10 = fx * (1.0 - fy);
            float w01 = (1.0 - fx) * fy;
            float w11 = fx * fy;

            atomicAdd(height_fp[idx00], int(deposit_amount * w00 * FP_SCALE));
            atomicAdd(height_fp[idx10], int(deposit_amount * w10 * FP_SCALE));
            atomicAdd(height_fp[idx01], int(deposit_amount * w01 * FP_SCALE));
            atomicAdd(height_fp[idx11], int(deposit_amount * w11 * FP_SCALE));

            atomicAdd(sediment_fp[idx00], int(deposit_amount * w00 * FP_SCALE));
            atomicAdd(sediment_fp[idx10], int(deposit_amount * w10 * FP_SCALE));
            atomicAdd(sediment_fp[idx01], int(deposit_amount * w01 * FP_SCALE));
            atomicAdd(sediment_fp[idx11], int(deposit_amount * w11 * FP_SCALE));
        } else {
            // ── Erode (atomicAdd negative deltas) ───────────────────────
            float erode_amount = min(
                    (capacity - sediment) * u_erosion_rate,
                    -h_diff);

            for (int bi = 0; bi < brush_count; bi++) {
                int bx = ix + brush_data[bi * 3 + 0];
                int by = iy + brush_data[bi * 3 + 1];
                float w = intBitsToFloat(brush_data[bi * 3 + 2]);
                if (bx >= 0 && bx < N && by >= 0 && by < N) {
                    int bidx = by * N + bx;
                    float delta = erode_amount * w;
                    // Approximate min_height check (non-atomic read is OK —
                    // worst case we slightly over-erode, which is fine)
                    float cur_h = read_h(bidx);
                    if (cur_h - delta < u_min_height) {
                        delta = max(cur_h - u_min_height, 0.0);
                    }
                    int idelta = int(delta * FP_SCALE);
                    atomicAdd(height_fp[bidx], -idelta);
                    atomicAdd(sediment_fp[bidx], -idelta);
                }
            }
            sediment += erode_amount;
        }

        speed = sqrt(max(speed * speed + u_gravity * (-h_diff), 0.01));
        water *= (1.0 - u_evaporation_rate);
        pos_x = new_x;
        pos_y = new_y;
    }
}
)";

// ── Constructor / Destructor ──────────────────────────────────────────────

GPUErosion::GPUErosion() {
	// Defer GPU init until first use (RenderingServer may not be ready in _init)
}

GPUErosion::~GPUErosion() {
	_cleanup_gpu();
}

// ── Parameter accessors ───────────────────────────────────────────────────

void GPUErosion::set_resolution(int p_res) { resolution = MAX(4, p_res); }
int GPUErosion::get_resolution() const { return resolution; }

void GPUErosion::set_num_particles(int p_n) { num_particles = MAX(0, p_n); }
int GPUErosion::get_num_particles() const { return num_particles; }

void GPUErosion::set_inertia(float p_val) { inertia = CLAMP(p_val, 0.0f, 1.0f); }
float GPUErosion::get_inertia() const { return inertia; }

void GPUErosion::set_capacity_mult(float p_val) { capacity_mult = MAX(0.01f, p_val); }
float GPUErosion::get_capacity_mult() const { return capacity_mult; }

void GPUErosion::set_deposition_rate(float p_val) { deposition_rate = CLAMP(p_val, 0.0f, 1.0f); }
float GPUErosion::get_deposition_rate() const { return deposition_rate; }

void GPUErosion::set_erosion_rate(float p_val) { erosion_rate = CLAMP(p_val, 0.0f, 1.0f); }
float GPUErosion::get_erosion_rate() const { return erosion_rate; }

void GPUErosion::set_gravity(float p_val) { gravity_val = MAX(0.01f, p_val); }
float GPUErosion::get_gravity() const { return gravity_val; }

void GPUErosion::set_evaporation_rate(float p_val) { evaporation_rate = CLAMP(p_val, 0.0f, 1.0f); }
float GPUErosion::get_evaporation_rate() const { return evaporation_rate; }

void GPUErosion::set_erosion_radius(int p_val) { erosion_radius = CLAMP(p_val, 1, 20); }
int GPUErosion::get_erosion_radius() const { return erosion_radius; }

void GPUErosion::set_max_lifetime(int p_val) { max_lifetime = MAX(1, p_val); }
int GPUErosion::get_max_lifetime() const { return max_lifetime; }

void GPUErosion::set_min_capacity(float p_val) { min_capacity = MAX(0.0001f, p_val); }
float GPUErosion::get_min_capacity() const { return min_capacity; }

void GPUErosion::set_min_height(float p_val) { min_height = p_val; }
float GPUErosion::get_min_height() const { return min_height; }

bool GPUErosion::is_gpu_available() const { return gpu_ready; }

// ── Brush ─────────────────────────────────────────────────────────────────

void GPUErosion::_build_brush() {
	brush.clear();
	float total_w = 0.0f;
	for (int dy = -erosion_radius; dy <= erosion_radius; dy++) {
		for (int dx = -erosion_radius; dx <= erosion_radius; dx++) {
			float dist = sqrtf((float)(dx * dx + dy * dy));
			if (dist <= (float)erosion_radius) {
				float w = MAX((float)erosion_radius - dist, 0.0f);
				brush.push_back({ dx, dy, w });
				total_w += w;
			}
		}
	}
	if (total_w > 0.0f) {
		for (int i = 0; i < brush.size(); i++) {
			brush.write[i].weight /= total_w;
		}
	}
}

// ── GPU initialisation ───────────────────────────────────────────────────

void GPUErosion::_init_gpu() {
	if (gpu_ready) {
		return;
	}

	// Get a local RenderingDevice for compute work.
	rd = RenderingServer::get_singleton()->create_local_rendering_device();
	if (!rd) {
		ERR_PRINT("GPUErosion: Failed to create local RenderingDevice. GPU erosion unavailable.");
		return;
	}

	// Compile GLSL compute stage to SPIR-V.
	String compile_error;
	Vector<uint8_t> spirv_bytes = rd->shader_compile_spirv_from_source(
			RenderingDevice::SHADER_STAGE_COMPUTE,
			String(EROSION_GLSL),
			RenderingDevice::SHADER_LANGUAGE_GLSL,
			&compile_error);

	if (!compile_error.is_empty()) {
		ERR_PRINT(vformat("GPUErosion: Shader compile error: %s", compile_error));
		memdelete(rd);
		rd = nullptr;
		return;
	}

	if (spirv_bytes.is_empty()) {
		ERR_PRINT("GPUErosion: Shader compilation returned empty SPIR-V.");
		memdelete(rd);
		rd = nullptr;
		return;
	}

	// Build ShaderStageSPIRVData for the compute stage.
	Vector<RenderingDevice::ShaderStageSPIRVData> stages;
	RenderingDevice::ShaderStageSPIRVData compute_stage;
	compute_stage.shader_stage = RenderingDevice::SHADER_STAGE_COMPUTE;
	compute_stage.spirv = spirv_bytes;
	stages.push_back(compute_stage);

	shader_rid = rd->shader_create_from_spirv(stages, "GPUErosion");
	if (!shader_rid.is_valid()) {
		ERR_PRINT("GPUErosion: Failed to create shader from SPIR-V.");
		memdelete(rd);
		rd = nullptr;
		return;
	}

	pipeline_rid = rd->compute_pipeline_create(shader_rid);
	if (!pipeline_rid.is_valid()) {
		ERR_PRINT("GPUErosion: Failed to create compute pipeline.");
		rd->free_rid(shader_rid);
		memdelete(rd);
		rd = nullptr;
		return;
	}

	gpu_ready = true;
	print_line("GPUErosion: Vulkan compute pipeline ready.");
}

void GPUErosion::_cleanup_gpu() {
	if (rd) {
		if (pipeline_rid.is_valid()) {
			rd->free_rid(pipeline_rid);
		}
		if (shader_rid.is_valid()) {
			rd->free_rid(shader_rid);
		}
		memdelete(rd);
		rd = nullptr;
	}
	gpu_ready = false;
}

// ── Core erosion ─────────────────────────────────────────────────────────

Dictionary GPUErosion::erode(PackedFloat32Array p_height, PackedFloat32Array p_sediment, int p_seed) {
	Dictionary result;
	const int N = resolution;
	const int total = N * N;

	if (p_height.size() != total) {
		ERR_PRINT(vformat("GPUErosion::erode: height array size %d != expected %d", p_height.size(), total));
		result["height"] = p_height;
		result["sediment"] = p_sediment;
		result["completed"] = 0;
		return result;
	}
	if (p_sediment.size() != total) {
		p_sediment.resize(total);
		p_sediment.fill(0.0f);
	}

	// Lazy init GPU
	if (!gpu_ready) {
		_init_gpu();
	}
	if (!gpu_ready) {
		// GPU not available — return unchanged
		ERR_PRINT("GPUErosion: GPU not available, returning unchanged data.");
		result["height"] = p_height;
		result["sediment"] = p_sediment;
		result["completed"] = 0;
		return result;
	}

	_build_brush();

	// ── Prepare GPU buffers ──────────────────────────────────────────────

	// Convert float heightmap to fixed-point int32 for GPU atomic safety.
	// Scale: 1 metre = FP_SCALE (1024) integer units.
	uint32_t fp_bytes = total * sizeof(int32_t);

	Vector<uint8_t> height_buf;
	height_buf.resize(fp_bytes);
	{
		int32_t *dst = (int32_t *)height_buf.ptrw();
		const float *src = p_height.ptr();
		for (int i = 0; i < total; i++) {
			dst[i] = (int32_t)(src[i] * FP_SCALE);
		}
	}
	RID height_rid = rd->storage_buffer_create(fp_bytes, height_buf);

	// Sediment buffer (also fixed-point)
	Vector<uint8_t> sediment_buf;
	sediment_buf.resize(fp_bytes);
	{
		int32_t *dst = (int32_t *)sediment_buf.ptrw();
		const float *src = p_sediment.ptr();
		for (int i = 0; i < total; i++) {
			dst[i] = (int32_t)(src[i] * FP_SCALE);
		}
	}
	RID sediment_rid = rd->storage_buffer_create(fp_bytes, sediment_buf);

	// Params buffer
	GPUParams params;
	params.resolution = N;
	params.num_particles = num_particles;
	params.erosion_radius = erosion_radius;
	params.max_lifetime = max_lifetime;
	params.inertia = inertia;
	params.capacity_mult = capacity_mult;
	params.deposition_rate = deposition_rate;
	params.erosion_rate = erosion_rate;
	params.gravity = gravity_val;
	params.evaporation_rate = evaporation_rate;
	params.min_capacity = min_capacity;
	params.min_height = min_height;
	params.seed = p_seed;
	params._pad0 = 0;
	params._pad1 = 0;
	params._pad2 = 0;

	Vector<uint8_t> params_buf;
	params_buf.resize(sizeof(GPUParams));
	memcpy(params_buf.ptrw(), &params, sizeof(GPUParams));
	RID params_rid = rd->storage_buffer_create(sizeof(GPUParams), params_buf);

	// Brush buffer: [brush_count, dx0, dy0, weight_as_int0, dx1, ...]
	int brush_entry_count = brush.size();
	int brush_buf_ints = 1 + brush_entry_count * 3;
	Vector<uint8_t> brush_buf;
	brush_buf.resize(brush_buf_ints * sizeof(int32_t));
	int32_t *brush_ptr = (int32_t *)brush_buf.ptrw();
	brush_ptr[0] = brush_entry_count;
	for (int i = 0; i < brush_entry_count; i++) {
		brush_ptr[1 + i * 3 + 0] = brush[i].dx;
		brush_ptr[1 + i * 3 + 1] = brush[i].dy;
		float w = brush[i].weight;
		int32_t w_bits;
		memcpy(&w_bits, &w, sizeof(int32_t));
		brush_ptr[1 + i * 3 + 2] = w_bits;
	}
	RID brush_rid = rd->storage_buffer_create(brush_buf_ints * sizeof(int32_t), brush_buf);

	// ── Create uniform set ───────────────────────────────────────────────

	RenderingDevice::Uniform u_height;
	u_height.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
	u_height.binding = 0;
	u_height.append_id(height_rid);

	RenderingDevice::Uniform u_sediment;
	u_sediment.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
	u_sediment.binding = 1;
	u_sediment.append_id(sediment_rid);

	RenderingDevice::Uniform u_params;
	u_params.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
	u_params.binding = 2;
	u_params.append_id(params_rid);

	RenderingDevice::Uniform u_brush;
	u_brush.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
	u_brush.binding = 3;
	u_brush.append_id(brush_rid);

	Vector<RenderingDevice::Uniform> uniforms;
	uniforms.push_back(u_height);
	uniforms.push_back(u_sediment);
	uniforms.push_back(u_params);
	uniforms.push_back(u_brush);

	RID uniform_set = rd->uniform_set_create(uniforms, shader_rid, 0);

	// ── Dispatch compute ─────────────────────────────────────────────────

	int workgroup_size = 64;
	int num_groups_x = (num_particles + workgroup_size - 1) / workgroup_size;

	RenderingDevice::ComputeListID compute_list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(compute_list, pipeline_rid);
	rd->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
	rd->compute_list_dispatch(compute_list, num_groups_x, 1, 1);
	rd->compute_list_end();

	// Submit and wait for completion
	rd->submit();
	rd->sync();

	// ── Read back results (fixed-point → float) ─────────────────────────

	Vector<uint8_t> height_result = rd->buffer_get_data(height_rid);
	Vector<uint8_t> sediment_result = rd->buffer_get_data(sediment_rid);

	{
		float *dst = p_height.ptrw();
		const int32_t *src = (const int32_t *)height_result.ptr();
		for (int i = 0; i < total; i++) {
			dst[i] = (float)src[i] * FP_INV;
		}
	}
	{
		float *dst = p_sediment.ptrw();
		const int32_t *src = (const int32_t *)sediment_result.ptr();
		for (int i = 0; i < total; i++) {
			dst[i] = (float)src[i] * FP_INV;
		}
	}

	// ── Cleanup per-dispatch resources ───────────────────────────────────
	rd->free_rid(uniform_set);
	rd->free_rid(brush_rid);
	rd->free_rid(params_rid);
	rd->free_rid(sediment_rid);
	rd->free_rid(height_rid);

	result["height"] = p_height;
	result["sediment"] = p_sediment;
	result["completed"] = num_particles;
	return result;
}

// ── Binding ──────────────────────────────────────────────────────────────

void GPUErosion::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_resolution", "res"), &GPUErosion::set_resolution);
	ClassDB::bind_method(D_METHOD("get_resolution"), &GPUErosion::get_resolution);

	ClassDB::bind_method(D_METHOD("set_num_particles", "n"), &GPUErosion::set_num_particles);
	ClassDB::bind_method(D_METHOD("get_num_particles"), &GPUErosion::get_num_particles);

	ClassDB::bind_method(D_METHOD("set_inertia", "val"), &GPUErosion::set_inertia);
	ClassDB::bind_method(D_METHOD("get_inertia"), &GPUErosion::get_inertia);

	ClassDB::bind_method(D_METHOD("set_capacity_mult", "val"), &GPUErosion::set_capacity_mult);
	ClassDB::bind_method(D_METHOD("get_capacity_mult"), &GPUErosion::get_capacity_mult);

	ClassDB::bind_method(D_METHOD("set_deposition_rate", "val"), &GPUErosion::set_deposition_rate);
	ClassDB::bind_method(D_METHOD("get_deposition_rate"), &GPUErosion::get_deposition_rate);

	ClassDB::bind_method(D_METHOD("set_erosion_rate", "val"), &GPUErosion::set_erosion_rate);
	ClassDB::bind_method(D_METHOD("get_erosion_rate"), &GPUErosion::get_erosion_rate);

	ClassDB::bind_method(D_METHOD("set_gravity", "val"), &GPUErosion::set_gravity);
	ClassDB::bind_method(D_METHOD("get_gravity"), &GPUErosion::get_gravity);

	ClassDB::bind_method(D_METHOD("set_evaporation_rate", "val"), &GPUErosion::set_evaporation_rate);
	ClassDB::bind_method(D_METHOD("get_evaporation_rate"), &GPUErosion::get_evaporation_rate);

	ClassDB::bind_method(D_METHOD("set_erosion_radius", "val"), &GPUErosion::set_erosion_radius);
	ClassDB::bind_method(D_METHOD("get_erosion_radius"), &GPUErosion::get_erosion_radius);

	ClassDB::bind_method(D_METHOD("set_max_lifetime", "val"), &GPUErosion::set_max_lifetime);
	ClassDB::bind_method(D_METHOD("get_max_lifetime"), &GPUErosion::get_max_lifetime);

	ClassDB::bind_method(D_METHOD("set_min_capacity", "val"), &GPUErosion::set_min_capacity);
	ClassDB::bind_method(D_METHOD("get_min_capacity"), &GPUErosion::get_min_capacity);

	ClassDB::bind_method(D_METHOD("set_min_height", "val"), &GPUErosion::set_min_height);
	ClassDB::bind_method(D_METHOD("get_min_height"), &GPUErosion::get_min_height);

	ClassDB::bind_method(D_METHOD("is_gpu_available"), &GPUErosion::is_gpu_available);

	ClassDB::bind_method(D_METHOD("erode", "height_data", "sediment_data", "seed"), &GPUErosion::erode);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "resolution"), "set_resolution", "get_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "num_particles"), "set_num_particles", "get_num_particles");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "inertia"), "set_inertia", "get_inertia");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "capacity_mult"), "set_capacity_mult", "get_capacity_mult");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "deposition_rate"), "set_deposition_rate", "get_deposition_rate");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "erosion_rate"), "set_erosion_rate", "get_erosion_rate");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity"), "set_gravity", "get_gravity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "evaporation_rate"), "set_evaporation_rate", "get_evaporation_rate");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "erosion_radius"), "set_erosion_radius", "get_erosion_radius");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_lifetime"), "set_max_lifetime", "get_max_lifetime");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_capacity"), "set_min_capacity", "get_min_capacity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_height"), "set_min_height", "get_min_height");
}
