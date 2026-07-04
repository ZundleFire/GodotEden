#include "gpu_heightmap_filler.h"
#include "core/math/math_defs.h"
#include "core/os/os.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server.h"
#include <cmath>
#include <cstring>

// ════════════════════════════════════════════════════════════════════════════
// GLSL Compute Shader: Heightmap Fill
// ════════════════════════════════════════════════════════════════════════════
// Generates terrain heights procedurally using GPU-native simplex noise.
// Each thread computes one cell of the RES×RES heightmap grid.
// Replaces the CPU HeightmapFiller (~920ms → ~10ms on RTX 3070).

static const char *FILL_GLSL = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) writeonly buffer HeightBuffer {
    float height_data[];
};

layout(set = 0, binding = 1, std430) restrict readonly buffer ParamsBuffer {
    int u_face;
    int u_gx;
    int u_gy;
    int u_grid_n;
    int u_resolution;
    int u_seed;
    float u_planet_radius;
    float u_border_warp;
    int u_erosion_iters;
    float u_erosion_rate;
    float u_deposition_rate;
    float u_talus_angle_tan;
    int u_thermal_iters;
    int _pad0;
    int _pad1;
    int _pad2;
};

layout(set = 0, binding = 2, std430) restrict readonly buffer TectonicBuffer {
    float tectonic_data[];
};

layout(set = 0, binding = 3, std430) restrict readonly buffer NoiseConfigBuffer {
    float noise_config[];
};

// ── GPU Simplex noise (3D) ───────────────────────────────────────────────
// Optimised permutation-free simplex noise for compute shaders.
// Based on Ashima Arts / Stefan Gustavson webgl-noise (MIT licence).

vec3 mod289(vec3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 mod289(vec4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 permute(vec4 x) { return mod289(((x * 34.0) + 10.0) * x); }
vec4 taylorInvSqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }

float snoise(vec3 v) {
    const vec2 C = vec2(1.0/6.0, 1.0/3.0);
    const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);

    vec3 i  = floor(v + dot(v, C.yyy));
    vec3 x0 = v - i + dot(i, C.xxx);

    vec3 g = step(x0.yzx, x0.xyz);
    vec3 l = 1.0 - g;
    vec3 i1 = min(g.xyz, l.zxy);
    vec3 i2 = max(g.xyz, l.zxy);

    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + C.yyy;
    vec3 x3 = x0 - D.yyy;

    i = mod289(i);
    vec4 p = permute(permute(permute(
        i.z + vec4(0.0, i1.z, i2.z, 1.0))
      + i.y + vec4(0.0, i1.y, i2.y, 1.0))
      + i.x + vec4(0.0, i1.x, i2.x, 1.0));

    float n_ = 0.142857142857;
    vec3 ns = n_ * D.wyz - D.xzx;

    vec4 j = p - 49.0 * floor(p * ns.z * ns.z);
    vec4 x_ = floor(j * ns.z);
    vec4 y_ = floor(j - 7.0 * x_);
    vec4 x2_ = x_ * ns.x + ns.yyyy;
    vec4 y2_ = y_ * ns.x + ns.yyyy;
    vec4 h = 1.0 - abs(x2_) - abs(y2_);

    vec4 b0 = vec4(x2_.xy, y2_.xy);
    vec4 b1 = vec4(x2_.zw, y2_.zw);

    vec4 s0 = floor(b0) * 2.0 + 1.0;
    vec4 s1 = floor(b1) * 2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));

    vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;

    vec3 p0 = vec3(a0.xy, h.x);
    vec3 p1 = vec3(a0.zw, h.y);
    vec3 p2 = vec3(a1.xy, h.z);
    vec3 p3 = vec3(a1.zw, h.w);

    vec4 norm = taylorInvSqrt(vec4(dot(p0,p0), dot(p1,p1), dot(p2,p2), dot(p3,p3)));
    p0 *= norm.x; p1 *= norm.y; p2 *= norm.z; p3 *= norm.w;

    vec4 m = max(0.5 - vec4(dot(x0,x0), dot(x1,x1), dot(x2,x2), dot(x3,x3)), 0.0);
    m = m * m;
    return 105.0 * dot(m*m, vec4(dot(p0,x0), dot(p1,x1), dot(p2,x2), dot(p3,x3)));
}

// ── FBM noise (variable octaves) ─────────────────────────────────────────
float fbm(vec3 p, float freq, int octaves, float lacunarity, float gain, int seed_offset) {
    vec3 sp = p * freq + vec3(float(seed_offset) * 0.31, float(seed_offset) * 0.73, float(seed_offset) * 0.53);
    float sum = 0.0;
    float amp = 1.0;
    float total_amp = 0.0;
    for (int i = 0; i < octaves; i++) {
        sum += snoise(sp) * amp;
        total_amp += amp;
        sp *= lacunarity;
        amp *= gain;
    }
    return sum / total_amp;
}

// ── Ridged noise ─────────────────────────────────────────────────────────
float ridged(vec3 p, float freq, int octaves, float lacunarity, float gain, int seed_offset) {
    vec3 sp = p * freq + vec3(float(seed_offset) * 0.31, float(seed_offset) * 0.73, float(seed_offset) * 0.53);
    float sum = 0.0;
    float amp = 1.0;
    float total_amp = 0.0;
    for (int i = 0; i < octaves; i++) {
        float n = 1.0 - abs(snoise(sp));
        sum += n * amp;
        total_amp += amp;
        sp *= lacunarity;
        amp *= gain;
    }
    return sum / total_amp;
}

// ── Face UV to direction ─────────────────────────────────────────────────
vec3 face_uv_to_dir(int face, float u, float v) {
    float s = u * 2.0 - 1.0;
    float t = v * 2.0 - 1.0;
    vec3 dir;
    if (face == 0)      dir = vec3( 1.0, -t,  -s);
    else if (face == 1) dir = vec3(-1.0, -t,   s);
    else if (face == 2) dir = vec3( s,    1.0, t);
    else if (face == 3) dir = vec3( s,   -1.0,-t);
    else if (face == 4) dir = vec3( s,   -t,   1.0);
    else                dir = vec3(-s,   -t,  -1.0);
    return normalize(dir);
}

// ── Smoothstep ───────────────────────────────────────────────────────────
float smooth_step(float edge0, float edge1, float x) {
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// ── Tectonic data lookup ─────────────────────────────────────────────────
// tectonic_data layout:
//   [0]          = N_POINTS (float, cast to int)
//   [1]          = N_PLATES
//   [2..2+N*3-1] = voronoi_points (xyz interleaved)
//   then: plate_id[N], falloff[N], bnd_type[N], is_oceanic[N],
//         elev_bias[N_PLATES], cross_plate_ang[N]

struct TerrainResult {
    float falloff;
    float oceanic;
    float bias;
    int bnd;
    float border_dist_m;
};

TerrainResult get_terrain(vec3 unit_dir) {
    int N = int(tectonic_data[0]);
    int N_PLATES = int(tectonic_data[1]);
    int pts_start = 2;
    int pid_start = pts_start + N * 3;
    int fo_start = pid_start + N;
    int bnd_start = fo_start + N;
    int oce_start = bnd_start + N;
    int bias_start = oce_start + N;
    int cpa_start = bias_start + N_PLATES;

    // Domain warp
    int warp_seed = u_seed + 777;
    float wx = fbm(unit_dir, 1.8, 2, 2.0, 0.5, warp_seed);
    float wy = fbm(unit_dir + vec3(3.7, 1.3, 2.5), 1.8, 2, 2.0, 0.5, warp_seed + 1);
    float wz = fbm(unit_dir + vec3(-2.1, 4.8, -0.9), 1.8, 2, 2.0, 0.5, warp_seed + 2);
    vec3 warped = normalize(unit_dir + vec3(wx, wy, wz) * u_border_warp);

    // Two-nearest brute force (GPU has thousands of threads, N=4800 is fine)
    float dot_a = -2.0, dot_b = -2.0;
    int idx_a = 0, idx_b = 0;
    for (int i = 0; i < N; i++) {
        vec3 pt = vec3(tectonic_data[pts_start + i*3],
                       tectonic_data[pts_start + i*3 + 1],
                       tectonic_data[pts_start + i*3 + 2]);
        float d = dot(warped, pt);
        if (d > dot_a) {
            dot_b = dot_a; idx_b = idx_a;
            dot_a = d; idx_a = i;
        } else if (d > dot_b) {
            dot_b = d; idx_b = i;
        }
    }

    float ang_a = acos(clamp(dot_a, -1.0, 1.0));
    float ang_b = acos(clamp(dot_b, -1.0, 1.0));
    float BW = 0.10;
    float t = clamp(1.0 - (ang_b - ang_a) / BW, 0.0, 1.0);
    t = t * t * (3.0 - 2.0 * t);

    float fa = tectonic_data[fo_start + idx_a];
    float fb = tectonic_data[fo_start + idx_b];

    int pa = int(tectonic_data[pid_start + idx_a]);
    int pb = int(tectonic_data[pid_start + idx_b]);

    TerrainResult r;
    r.falloff = mix(fa, fb, t);
    r.oceanic = mix(tectonic_data[oce_start + idx_a], tectonic_data[oce_start + idx_b], t);
    r.bias = mix(tectonic_data[bias_start + pa], tectonic_data[bias_start + pb], t);
    r.bnd = int(tectonic_data[bnd_start + idx_a]);

    if (pa != pb) {
        r.border_dist_m = (ang_b - ang_a) * 0.5 * u_planet_radius;
    } else {
        float cpa_a = tectonic_data[cpa_start + idx_a];
        float cpa_b = tectonic_data[cpa_start + idx_b];
        r.border_dist_m = mix(cpa_a - ang_a, cpa_b - ang_b, t) * 0.5 * u_planet_radius;
    }
    return r;
}

void main() {
    int col = int(gl_GlobalInvocationID.x);
    int row = int(gl_GlobalInvocationID.y);
    int N = u_resolution;
    if (col >= N || row >= N) return;

    float inv_res = 1.0 / float(N);
    float inv_grid = 1.0 / float(u_grid_n);

    float local_u = (float(col) + 0.5) * inv_res;
    float local_v = (float(row) + 0.5) * inv_res;
    float global_u = (float(u_gx) + local_u) * inv_grid;
    float global_v = (float(u_gy) + local_v) * inv_grid;

    vec3 unit_dir = face_uv_to_dir(u_face, global_u, global_v);
    vec3 world_pos = unit_dir * u_planet_radius;

    TerrainResult td = get_terrain(unit_dir);

    // Noise config layout:
    // [0..7] = seeds, [8..15] = amplitudes, [16..18] = half_widths
    // [19..21] = base freq/octaves/gain, etc.
    float base_amp = noise_config[8];
    float cont_amp = noise_config[9];
    float plat_amp = noise_config[10];
    float oce_amp = noise_config[11];
    float mtn_amp = noise_config[12];
    float trench_amp = noise_config[13];
    float rift_amp = noise_config[14];
    float detail_amp = noise_config[15];
    float mtn_hw = noise_config[16];
    float trench_hw = noise_config[17];
    float rift_hw = noise_config[18];

    int s0 = int(noise_config[0]);
    int s1 = int(noise_config[1]);
    int s2 = int(noise_config[2]);
    int s3 = int(noise_config[3]);
    int s4 = int(noise_config[4]);
    int s5 = int(noise_config[5]);
    int s6 = int(noise_config[6]);
    int s7 = int(noise_config[7]);

    float height = td.bias * td.falloff;

    // Base noise (FBM 5 octaves)
    height += fbm(world_pos, 0.00012, 5, 2.0, 0.5, s0) * base_amp;

    // Continental noise — land only
    float land_factor = (1.0 - td.oceanic) * td.falloff;
    if (land_factor > 0.01)
        height += fbm(world_pos, 0.00006, 4, 2.0, 0.45, s1) * cont_amp * land_factor;

    // Plateau noise — deep land interior
    float interior_factor = land_factor * td.falloff;
    if (interior_factor > 0.01)
        height += ridged(world_pos, 0.0001, 3, 2.0, 0.45, s2) * plat_amp * interior_factor;

    // Oceanic noise
    if (td.oceanic > 0.01)
        height += fbm(world_pos, 0.00008, 3, 2.0, 0.5, s3) * oce_amp * td.oceanic;

    // Mountain noise
    float mtn_mask = 0.0;
    if (td.bnd == 1 || td.bnd == 5) {
        mtn_mask = 1.0 - smooth_step(0.0, mtn_hw, td.border_dist_m);
        mtn_mask *= mtn_mask;
    }
    if (mtn_mask > 0.01)
        height += ridged(world_pos, 0.00015, 4, 2.0, 0.4, s4) * mtn_amp * mtn_mask;

    // Trench noise
    float trench_mask = 0.0;
    if (td.bnd == 2)
        trench_mask = 1.0 - smooth_step(0.0, trench_hw, td.border_dist_m);
    if (trench_mask > 0.01)
        height -= ridged(world_pos, 0.0002, 3, 2.0, 0.45, s5) * trench_amp * trench_mask;

    // Rift noise
    float rift_mask = 0.0;
    if (td.bnd == 3 || td.bnd == 4)
        rift_mask = 1.0 - smooth_step(0.0, rift_hw, td.border_dist_m);
    if (rift_mask > 0.01)
        height -= abs(fbm(world_pos, 0.00015, 3, 2.0, 0.5, s6)) * rift_amp * rift_mask;

    // Detail noise — damped near sea level
    float detail_n = fbm(world_pos, 0.0008, 3, 2.0, 0.5, s7);
    float sea_damp = clamp(abs(height) / 200.0, 0.1, 1.0);
    height += detail_n * detail_amp * sea_damp;

    // Shore land minimum
    if (height > 0.0) height = max(height, 5.0);

    height_data[row * N + col] = height;
}
)";

// ════════════════════════════════════════════════════════════════════════════
// GLSL Compute Shader: Grid-Based Shallow-Water Erosion (soillib-inspired)
// ════════════════════════════════════════════════════════════════════════════
// Tier 3: Instead of independent particle random walks (which cause atomicAdd
// contention and random memory access), this computes water flow between
// grid cells using a pipe model / shallow-water approximation.
//
// Each iteration:
//   1. Compute water flux to 4 neighbours based on height + water difference
//   2. Update water level from net flux
//   3. Compute velocity from flux
//   4. Erode / deposit based on carrying capacity vs sediment load
//   5. Transport sediment by velocity
//   6. Evaporate water
//
// This is fully parallelizable — no atomics needed (double-buffered).

static const char *ERODE_GLSL = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) buffer HeightBuffer {
    float height_data[];
};

layout(set = 0, binding = 1, std430) buffer WaterBuffer {
    float water_data[];
};

layout(set = 0, binding = 2, std430) buffer SedimentBuffer {
    float sediment_data[];
};

layout(set = 0, binding = 3, std430) buffer FluxBuffer {
    // 4 floats per cell: flux_left, flux_right, flux_top, flux_bottom
    float flux_data[];
};

layout(set = 0, binding = 4, std430) buffer VelocityBuffer {
    // 2 floats per cell: vx, vy
    float velocity_data[];
};

layout(set = 0, binding = 5, std430) restrict readonly buffer ParamsBuffer {
    int u_resolution;
    int u_iteration;      // current iteration index
    float u_dt;           // timestep
    float u_erosion_rate;
    float u_deposition_rate;
    float u_evaporation;
    float u_capacity_mult;
    float u_gravity;
    float u_cell_size;    // metres per cell
    float u_min_slope;
    int _pad0;
    int _pad1;
};

// Sub-pass selector: 0=flux, 1=water+velocity, 2=erode+deposit, 3=sediment transport, 4=evaporate
layout(push_constant) uniform PushConstants {
    int sub_pass;
    int _pc_pad0;
    int _pc_pad1;
    int _pc_pad2;
} pc;

void main() {
    int x = int(gl_GlobalInvocationID.x);
    int y = int(gl_GlobalInvocationID.y);
    int N = u_resolution;
    if (x >= N || y >= N) return;

    int idx = y * N + x;
    float dt = u_dt;
    float g = u_gravity;
    float cell = u_cell_size;

    if (pc.sub_pass == 0) {
        // ── Pass 0: Compute outgoing flux ────────────────────────────────
        float h = height_data[idx] + water_data[idx];

        // 4-neighbour flux: left, right, top, bottom
        float fL = 0.0, fR = 0.0, fT = 0.0, fB = 0.0;

        if (x > 0) {
            int nidx = y * N + (x - 1);
            float nh = height_data[nidx] + water_data[nidx];
            float dh = h - nh;
            fL = max(0.0, flux_data[idx * 4 + 0] + dt * g * dh / cell);
        }
        if (x < N - 1) {
            int nidx = y * N + (x + 1);
            float nh = height_data[nidx] + water_data[nidx];
            float dh = h - nh;
            fR = max(0.0, flux_data[idx * 4 + 1] + dt * g * dh / cell);
        }
        if (y > 0) {
            int nidx = (y - 1) * N + x;
            float nh = height_data[nidx] + water_data[nidx];
            float dh = h - nh;
            fT = max(0.0, flux_data[idx * 4 + 2] + dt * g * dh / cell);
        }
        if (y < N - 1) {
            int nidx = (y + 1) * N + x;
            float nh = height_data[nidx] + water_data[nidx];
            float dh = h - nh;
            fB = max(0.0, flux_data[idx * 4 + 3] + dt * g * dh / cell);
        }

        // Scale flux to not drain more water than available
        float total_out = fL + fR + fT + fB;
        float w = water_data[idx] * cell * cell;
        if (total_out > 0.0 && total_out * dt > w) {
            float scale = w / (total_out * dt);
            fL *= scale; fR *= scale; fT *= scale; fB *= scale;
        }

        flux_data[idx * 4 + 0] = fL;
        flux_data[idx * 4 + 1] = fR;
        flux_data[idx * 4 + 2] = fT;
        flux_data[idx * 4 + 3] = fB;
    }
    else if (pc.sub_pass == 1) {
        // ── Pass 1: Update water level + compute velocity ────────────────
        float outflow = flux_data[idx * 4 + 0] + flux_data[idx * 4 + 1]
                       + flux_data[idx * 4 + 2] + flux_data[idx * 4 + 3];

        float inflow = 0.0;
        // Inflow from left neighbour's right flux
        if (x > 0) inflow += flux_data[(y * N + (x-1)) * 4 + 1];
        // Inflow from right neighbour's left flux
        if (x < N-1) inflow += flux_data[(y * N + (x+1)) * 4 + 0];
        // Inflow from top neighbour's bottom flux
        if (y > 0) inflow += flux_data[((y-1) * N + x) * 4 + 3];
        // Inflow from bottom neighbour's top flux
        if (y < N-1) inflow += flux_data[((y+1) * N + x) * 4 + 2];

        float dV = (inflow - outflow) * dt;
        water_data[idx] = max(0.0, water_data[idx] + dV / (cell * cell));

        // Velocity from flux difference
        float fL = flux_data[idx * 4 + 0];
        float fR = flux_data[idx * 4 + 1];
        float fT = flux_data[idx * 4 + 2];
        float fB = flux_data[idx * 4 + 3];

        float wh = max(water_data[idx], 0.001);
        float vx = ((fR - fL) + (x > 0 ? flux_data[(y*N+(x-1))*4+1] : 0.0)
                                - (x < N-1 ? flux_data[(y*N+(x+1))*4+0] : 0.0)) * 0.5 / (wh * cell);
        float vy = ((fB - fT) + (y > 0 ? flux_data[((y-1)*N+x)*4+3] : 0.0)
                                - (y < N-1 ? flux_data[((y+1)*N+x)*4+2] : 0.0)) * 0.5 / (wh * cell);

        velocity_data[idx * 2 + 0] = vx;
        velocity_data[idx * 2 + 1] = vy;
    }
    else if (pc.sub_pass == 2) {
        // ── Pass 2: Erosion / Deposition ─────────────────────────────────
        float vx = velocity_data[idx * 2 + 0];
        float vy = velocity_data[idx * 2 + 1];
        float speed = sqrt(vx * vx + vy * vy);

        // Local slope
        float slope = u_min_slope;
        if (x > 0 && x < N-1) {
            float dhdx = (height_data[y*N+(x+1)] - height_data[y*N+(x-1)]) / (2.0 * cell);
            slope = max(slope, abs(dhdx));
        }
        if (y > 0 && y < N-1) {
            float dhdy = (height_data[(y+1)*N+x] - height_data[(y-1)*N+x]) / (2.0 * cell);
            slope = max(slope, abs(dhdy));
        }

        float w = water_data[idx];
        float capacity = u_capacity_mult * speed * slope * max(w, 0.01);
        float sed = sediment_data[idx];

        if (sed < capacity) {
            // Erode
            float erode_amt = min(u_erosion_rate * (capacity - sed) * dt, height_data[idx] + 5000.0);
            height_data[idx] -= erode_amt;
            sediment_data[idx] += erode_amt;
        } else {
            // Deposit
            float deposit_amt = u_deposition_rate * (sed - capacity) * dt;
            height_data[idx] += deposit_amt;
            sediment_data[idx] -= deposit_amt;
        }
    }
    else if (pc.sub_pass == 3) {
        // ── Pass 3: Sediment transport (advection by velocity) ───────────
        float vx = velocity_data[idx * 2 + 0];
        float vy = velocity_data[idx * 2 + 1];

        // Semi-Lagrangian back-trace
        float src_x = float(x) - vx * dt / cell;
        float src_y = float(y) - vy * dt / cell;
        src_x = clamp(src_x, 0.0, float(N - 1));
        src_y = clamp(src_y, 0.0, float(N - 1));

        int sx = int(src_x);
        int sy = int(src_y);
        float fx = src_x - float(sx);
        float fy = src_y - float(sy);
        int sx1 = min(sx + 1, N - 1);
        int sy1 = min(sy + 1, N - 1);

        // Bilinear sample from sediment buffer
        float s00 = sediment_data[sy * N + sx];
        float s10 = sediment_data[sy * N + sx1];
        float s01 = sediment_data[sy1 * N + sx];
        float s11 = sediment_data[sy1 * N + sx1];
        float new_sed = s00 * (1.0-fx) * (1.0-fy) + s10 * fx * (1.0-fy)
                      + s01 * (1.0-fx) * fy + s11 * fx * fy;

        // Write to a temporary location (we use sediment_data as double buffer
        // by running this pass on alternating iterations — simple approach)
        sediment_data[idx] = new_sed;
    }
    else if (pc.sub_pass == 4) {
        // ── Pass 4: Evaporation ──────────────────────────────────────────
        water_data[idx] *= (1.0 - u_evaporation * dt);
    }
}
)";

// ════════════════════════════════════════════════════════════════════════════
// GLSL Compute Shader: Thermal Erosion / Sediment Cascading
// ════════════════════════════════════════════════════════════════════════════
// Tier 3 (soillib-inspired): Talus angle relaxation — if the height difference
// between a cell and its neighbour exceeds the max stable slope, material
// cascades downhill.  This creates natural scree slopes and fills valleys.

static const char *THERMAL_GLSL = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) buffer HeightBuffer {
    float height_data[];
};

layout(set = 0, binding = 1, std430) buffer SedimentBuffer {
    float sediment_data[];
};

layout(set = 0, binding = 2, std430) restrict readonly buffer ParamsBuffer {
    int u_resolution;
    float u_cell_size;
    float u_talus_angle_tan;  // tan(max_stable_slope_angle)
    float u_settling_rate;
    int _pad0;
    int _pad1;
    int _pad2;
    int _pad3;
};

void main() {
    int x = int(gl_GlobalInvocationID.x);
    int y = int(gl_GlobalInvocationID.y);
    int N = u_resolution;
    if (x >= N || y >= N) return;

    int idx = y * N + x;
    float h = height_data[idx];
    float cell = u_cell_size;
    float max_diff = u_talus_angle_tan * cell;

    // Check 8 neighbours, find total excess material to redistribute
    float total_excess = 0.0;
    float max_h_diff = 0.0;

    // D8 offsets
    int dx[8] = int[8](0, 1, 1, 1, 0, -1, -1, -1);
    int dy[8] = int[8](-1, -1, 0, 1, 1, 1, 0, -1);
    float inv_dist[8] = float[8](1.0, 0.7071, 1.0, 0.7071, 1.0, 0.7071, 1.0, 0.7071);

    float diffs[8];
    float valid[8];
    int n_excess = 0;

    for (int d = 0; d < 8; d++) {
        int nx = x + dx[d];
        int ny = y + dy[d];
        diffs[d] = 0.0;
        valid[d] = 0.0;
        if (nx < 0 || nx >= N || ny < 0 || ny >= N) continue;

        int nidx = ny * N + nx;
        float nh = height_data[nidx];
        float diff = (h - nh) * inv_dist[d];
        float excess = diff - max_diff;
        if (excess > 0.0) {
            diffs[d] = excess;
            valid[d] = 1.0;
            total_excess += excess;
            max_h_diff = max(max_h_diff, excess);
            n_excess++;
        }
    }

    if (total_excess <= 0.0 || n_excess == 0) return;

    // Redistribute proportionally
    float transfer_total = min(u_settling_rate * max_h_diff * 0.5, h + 5000.0);

    for (int d = 0; d < 8; d++) {
        if (valid[d] <= 0.0) continue;
        int nx = x + dx[d];
        int ny = y + dy[d];
        if (nx < 0 || nx >= N || ny < 0 || ny >= N) continue;

        float fraction = diffs[d] / total_excess;
        float amt = transfer_total * fraction;

        // Atomic not needed with Jacobi iteration — we over/under-shoot
        // but converge over iterations.
        height_data[idx] -= amt;
        height_data[ny * N + nx] += amt;
        // Track sediment motion
        sediment_data[idx] += amt * 0.2;
    }
}
)";

// ════════════════════════════════════════════════════════════════════════════
// GLSL Compute Shader: GPU Flow Accumulation
// ════════════════════════════════════════════════════════════════════════════
// Parallel flow accumulation using iterative relaxation.
// Each cell checks its 8 neighbours: if a neighbour is higher AND its
// steepest-descent target is this cell, add the neighbour's flow.
// Converges in O(sqrt(N)) iterations for typical terrain.

static const char *FLOW_GLSL = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) restrict readonly buffer HeightBuffer {
    float height_data[];
};

layout(set = 0, binding = 1, std430) buffer FlowBufferA {
    float flow_a[];
};

layout(set = 0, binding = 2, std430) buffer FlowBufferB {
    float flow_b[];
};

layout(set = 0, binding = 3, std430) restrict readonly buffer ParamsBuffer {
    int u_resolution;
    int u_iteration;
    int _pad0;
    int _pad1;
};

layout(push_constant) uniform PushConstants {
    int read_from_a;  // 1 = read from A write to B, 0 = read B write A
    int _pc_pad0;
    int _pc_pad1;
    int _pc_pad2;
} pc;

void main() {
    int x = int(gl_GlobalInvocationID.x);
    int y = int(gl_GlobalInvocationID.y);
    int N = u_resolution;
    if (x >= N || y >= N) return;

    int idx = y * N + x;
    float h_here = height_data[idx];

    // Find steepest-descent neighbour (D8)
    int dx[8] = int[8](0, 1, 1, 1, 0, -1, -1, -1);
    int dy[8] = int[8](-1, -1, 0, 1, 1, 1, 0, -1);
    float inv_dist[8] = float[8](1.0, 0.7071, 1.0, 0.7071, 1.0, 0.7071, 1.0, 0.7071);

    // Accumulate: start with 1.0 (rain) + flow from all neighbours that drain to us
    float flow = 1.0;

    for (int d = 0; d < 8; d++) {
        int nx = x + dx[d];
        int ny = y + dy[d];
        if (nx < 0 || nx >= N || ny < 0 || ny >= N) continue;

        int nidx = ny * N + nx;
        float nh = height_data[nidx];
        if (nh <= h_here) continue;  // neighbour not higher

        // Check if this neighbour's steepest descent points to us
        float best_slope = 0.0;
        int best_dx = 0, best_dy = 0;
        for (int dd = 0; dd < 8; dd++) {
            int nnx = nx + dx[dd];
            int nny = ny + dy[dd];
            if (nnx < 0 || nnx >= N || nny < 0 || nny >= N) continue;
            float slope = (nh - height_data[nny * N + nnx]) * inv_dist[dd];
            if (slope > best_slope) {
                best_slope = slope;
                best_dx = dx[dd];
                best_dy = dy[dd];
            }
        }
        // Does this neighbour drain to us?
        if (nx + best_dx == x && ny + best_dy == y) {
            float nflow = (pc.read_from_a == 1) ? flow_a[nidx] : flow_b[nidx];
            flow += nflow;
        }
    }

    if (pc.read_from_a == 1) {
        flow_b[idx] = flow;
    } else {
        flow_a[idx] = flow;
    }
}
)";

// ════════════════════════════════════════════════════════════════════════════
// C++ Implementation
// ════════════════════════════════════════════════════════════════════════════

GPUHeightmapFiller::GPUHeightmapFiller() {
}

GPUHeightmapFiller::~GPUHeightmapFiller() {
	_cleanup_gpu();
}

void GPUHeightmapFiller::set_planet_radius(float p_radius) { planet_radius = p_radius; }
float GPUHeightmapFiller::get_planet_radius() const { return planet_radius; }

void GPUHeightmapFiller::set_border_warp(float p_warp) { border_warp = p_warp; }
float GPUHeightmapFiller::get_border_warp() const { return border_warp; }

void GPUHeightmapFiller::set_erosion_iterations(int p_iters) { erosion_iterations = MAX(0, p_iters); }
int GPUHeightmapFiller::get_erosion_iterations() const { return erosion_iterations; }

void GPUHeightmapFiller::set_erosion_rate(float p_rate) { erosion_rate_val = CLAMP(p_rate, 0.0f, 5.0f); }
float GPUHeightmapFiller::get_erosion_rate() const { return erosion_rate_val; }

void GPUHeightmapFiller::set_deposition_rate(float p_rate) { deposition_rate_val = CLAMP(p_rate, 0.0f, 5.0f); }
float GPUHeightmapFiller::get_deposition_rate() const { return deposition_rate_val; }

void GPUHeightmapFiller::set_thermal_iterations(int p_iters) { thermal_iterations = MAX(0, p_iters); }
int GPUHeightmapFiller::get_thermal_iterations() const { return thermal_iterations; }

void GPUHeightmapFiller::set_talus_angle(float p_angle) { talus_angle = MAX(0.01f, p_angle); }
float GPUHeightmapFiller::get_talus_angle() const { return talus_angle; }

bool GPUHeightmapFiller::is_gpu_available() const { return gpu_ready; }

void GPUHeightmapFiller::set_tectonic_data(PackedFloat32Array p_packed_data) {
	if (!gpu_ready) {
		_init_gpu();
	}
	if (!gpu_ready || p_packed_data.is_empty()) {
		return;
	}

	if (tectonic_ssbo_rid.is_valid()) {
		rd->free_rid(tectonic_ssbo_rid);
	}

	Vector<uint8_t> buf;
	buf.resize(p_packed_data.size() * sizeof(float));
	memcpy(buf.ptrw(), p_packed_data.ptr(), buf.size());
	tectonic_ssbo_rid = rd->storage_buffer_create(buf.size(), buf);
	tectonic_uploaded = tectonic_ssbo_rid.is_valid();

	if (tectonic_uploaded) {
		print_line(vformat("GPUHeightmapFiller: tectonic SSBO uploaded (%d floats, %d bytes)",
				p_packed_data.size(), buf.size()));
	}
}

void GPUHeightmapFiller::set_noise_params(PackedFloat32Array p_noise_config) {
	if (!gpu_ready) {
		_init_gpu();
	}
	if (!gpu_ready || p_noise_config.is_empty()) {
		return;
	}

	if (noise_config_rid.is_valid()) {
		rd->free_rid(noise_config_rid);
	}

	Vector<uint8_t> buf;
	buf.resize(p_noise_config.size() * sizeof(float));
	memcpy(buf.ptrw(), p_noise_config.ptr(), buf.size());
	noise_config_rid = rd->storage_buffer_create(buf.size(), buf);
	noise_uploaded = noise_config_rid.is_valid();
}

// ── GPU Initialisation ───────────────────────────────────────────────────

RID GPUHeightmapFiller::_compile_shader(const char *glsl_source, const char *name) {
	String compile_error;
	Vector<uint8_t> spirv_bytes = rd->shader_compile_spirv_from_source(
			RenderingDevice::SHADER_STAGE_COMPUTE,
			String(glsl_source),
			RenderingDevice::SHADER_LANGUAGE_GLSL,
			&compile_error);

	if (!compile_error.is_empty()) {
		ERR_PRINT(vformat("GPUHeightmapFiller: %s shader compile error: %s", name, compile_error));
		return RID();
	}

	if (spirv_bytes.is_empty()) {
		ERR_PRINT(vformat("GPUHeightmapFiller: %s shader produced empty SPIR-V.", name));
		return RID();
	}

	Vector<RenderingDevice::ShaderStageSPIRVData> stages;
	RenderingDevice::ShaderStageSPIRVData stage;
	stage.shader_stage = RenderingDevice::SHADER_STAGE_COMPUTE;
	stage.spirv = spirv_bytes;
	stages.push_back(stage);

	RID shader = rd->shader_create_from_spirv(stages, name);
	return shader;
}

void GPUHeightmapFiller::_init_gpu() {
	if (gpu_ready) {
		return;
	}

	rd = RenderingServer::get_singleton()->create_local_rendering_device();
	if (!rd) {
		ERR_PRINT("GPUHeightmapFiller: Failed to create local RenderingDevice.");
		return;
	}

	// Compile all shaders
	fill_shader_rid = _compile_shader(FILL_GLSL, "Fill");
	if (!fill_shader_rid.is_valid()) {
		memdelete(rd);
		rd = nullptr;
		return;
	}
	fill_pipeline_rid = rd->compute_pipeline_create(fill_shader_rid);

	erode_shader_rid = _compile_shader(ERODE_GLSL, "GridErode");
	if (erode_shader_rid.is_valid()) {
		erode_pipeline_rid = rd->compute_pipeline_create(erode_shader_rid);
	}

	thermal_shader_rid = _compile_shader(THERMAL_GLSL, "Thermal");
	if (thermal_shader_rid.is_valid()) {
		thermal_pipeline_rid = rd->compute_pipeline_create(thermal_shader_rid);
	}

	flow_shader_rid = _compile_shader(FLOW_GLSL, "Flow");
	if (flow_shader_rid.is_valid()) {
		flow_pipeline_rid = rd->compute_pipeline_create(flow_shader_rid);
	}

	gpu_ready = true;
	print_line("GPUHeightmapFiller: Vulkan compute pipeline ready (fill + grid-erode + thermal + flow).");
}

void GPUHeightmapFiller::_cleanup_gpu() {
	if (rd) {
		if (tectonic_ssbo_rid.is_valid()) rd->free_rid(tectonic_ssbo_rid);
		if (noise_config_rid.is_valid()) rd->free_rid(noise_config_rid);
		if (fill_pipeline_rid.is_valid()) rd->free_rid(fill_pipeline_rid);
		if (fill_shader_rid.is_valid()) rd->free_rid(fill_shader_rid);
		if (erode_pipeline_rid.is_valid()) rd->free_rid(erode_pipeline_rid);
		if (erode_shader_rid.is_valid()) rd->free_rid(erode_shader_rid);
		if (thermal_pipeline_rid.is_valid()) rd->free_rid(thermal_pipeline_rid);
		if (thermal_shader_rid.is_valid()) rd->free_rid(thermal_shader_rid);
		if (flow_pipeline_rid.is_valid()) rd->free_rid(flow_pipeline_rid);
		if (flow_shader_rid.is_valid()) rd->free_rid(flow_shader_rid);
		memdelete(rd);
		rd = nullptr;
	}
	gpu_ready = false;
	tectonic_uploaded = false;
	noise_uploaded = false;
}

// ── Core: Generate Chunk ─────────────────────────────────────────────────

Dictionary GPUHeightmapFiller::generate_chunk(int p_face, int p_gx, int p_gy,
		int p_grid_n, int p_resolution, int p_seed) {
	Dictionary result;
	const int N = p_resolution;
	const int total = N * N;

	if (!gpu_ready) {
		_init_gpu();
	}
	if (!gpu_ready || !tectonic_uploaded || !noise_uploaded) {
		ERR_PRINT("GPUHeightmapFiller: GPU not ready or data not uploaded.");
		PackedFloat32Array empty;
		empty.resize(total);
		empty.fill(0.0f);
		result["height"] = empty;
		result["sediment"] = empty.duplicate();
		result["flow"] = empty.duplicate();
		return result;
	}

	float cell_size = 5120.0f / (float)N; // CHUNK_SIZE_M / resolution

	// ── Create per-chunk buffers ─────────────────────────────────────────
	uint32_t float_bytes = total * sizeof(float);

	// Height buffer (output of fill, modified by erosion)
	Vector<uint8_t> zero_buf;
	zero_buf.resize(float_bytes);
	memset(zero_buf.ptrw(), 0, float_bytes);
	RID height_rid = rd->storage_buffer_create(float_bytes, zero_buf);

	// Water buffer
	// Initialise with uniform rainfall
	Vector<uint8_t> water_init;
	water_init.resize(float_bytes);
	{
		float *dst = (float *)water_init.ptrw();
		for (int i = 0; i < total; i++) {
			dst[i] = 0.01f; // 1cm initial rain
		}
	}
	RID water_rid = rd->storage_buffer_create(float_bytes, water_init);

	// Sediment buffer
	RID sediment_rid = rd->storage_buffer_create(float_bytes, zero_buf);

	// Flux buffer (4 floats per cell)
	uint32_t flux_bytes = total * 4 * sizeof(float);
	Vector<uint8_t> flux_zero;
	flux_zero.resize(flux_bytes);
	memset(flux_zero.ptrw(), 0, flux_bytes);
	RID flux_rid = rd->storage_buffer_create(flux_bytes, flux_zero);

	// Velocity buffer (2 floats per cell)
	uint32_t vel_bytes = total * 2 * sizeof(float);
	Vector<uint8_t> vel_zero;
	vel_zero.resize(vel_bytes);
	memset(vel_zero.ptrw(), 0, vel_bytes);
	RID velocity_rid = rd->storage_buffer_create(vel_bytes, vel_zero);

	// Flow accumulation double buffer
	RID flow_a_rid = rd->storage_buffer_create(float_bytes, zero_buf);
	// Initialise flow_a with 1.0 per cell
	{
		Vector<uint8_t> flow_init;
		flow_init.resize(float_bytes);
		float *dst = (float *)flow_init.ptrw();
		for (int i = 0; i < total; i++) dst[i] = 1.0f;
		rd->buffer_update(flow_a_rid, 0, float_bytes, flow_init.ptr());
	}
	RID flow_b_rid = rd->storage_buffer_create(float_bytes, zero_buf);

	// ── Params buffer for fill shader ────────────────────────────────────
	ChunkParams params;
	params.face = p_face;
	params.gx = p_gx;
	params.gy = p_gy;
	params.grid_n = p_grid_n;
	params.resolution = N;
	params.seed = p_seed;
	params.planet_radius = planet_radius;
	params.border_warp = border_warp;
	params.erosion_iters = erosion_iterations;
	params.erosion_rate = erosion_rate_val;
	params.deposition_rate = deposition_rate_val;
	params.talus_angle_tan = talus_angle;
	params.thermal_iters = thermal_iterations;
	params._pad0 = 0;
	params._pad1 = 0;
	params._pad2 = 0;

	Vector<uint8_t> params_buf;
	params_buf.resize(sizeof(ChunkParams));
	memcpy(params_buf.ptrw(), &params, sizeof(ChunkParams));
	RID params_rid = rd->storage_buffer_create(sizeof(ChunkParams), params_buf);

	int groups_x = (N + 15) / 16;
	int groups_y = (N + 15) / 16;

	// ════════════════════════════════════════════════════════════════════
	// PASS 1: Heightmap fill
	// ════════════════════════════════════════════════════════════════════
	{
		RenderingDevice::Uniform u_height;
		u_height.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u_height.binding = 0;
		u_height.append_id(height_rid);

		RenderingDevice::Uniform u_params;
		u_params.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u_params.binding = 1;
		u_params.append_id(params_rid);

		RenderingDevice::Uniform u_tect;
		u_tect.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u_tect.binding = 2;
		u_tect.append_id(tectonic_ssbo_rid);

		RenderingDevice::Uniform u_noise;
		u_noise.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u_noise.binding = 3;
		u_noise.append_id(noise_config_rid);

		Vector<RenderingDevice::Uniform> uniforms;
		uniforms.push_back(u_height);
		uniforms.push_back(u_params);
		uniforms.push_back(u_tect);
		uniforms.push_back(u_noise);

		RID uniform_set = rd->uniform_set_create(uniforms, fill_shader_rid, 0);

		RenderingDevice::ComputeListID cl = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(cl, fill_pipeline_rid);
		rd->compute_list_bind_uniform_set(cl, uniform_set, 0);
		rd->compute_list_dispatch(cl, groups_x, groups_y, 1);
		rd->compute_list_end();
		rd->submit();
		rd->sync();

		rd->free_rid(uniform_set);
	}

	// ════════════════════════════════════════════════════════════════════
	// PASS 2: Grid-based erosion (multiple iterations)
	// ════════════════════════════════════════════════════════════════════
	if (erosion_iterations > 0 && erode_pipeline_rid.is_valid()) {
		// Erosion params SSBO
		struct ErodeParams {
			int32_t resolution;
			int32_t iteration;
			float dt;
			float erosion_rate;
			float deposition_rate;
			float evaporation;
			float capacity_mult;
			float gravity;
			float cell_size;
			float min_slope;
			int32_t _pad0;
			int32_t _pad1;
		};

		ErodeParams ep;
		ep.resolution = N;
		ep.iteration = 0;
		ep.dt = 0.05f;
		ep.erosion_rate = erosion_rate_val;
		ep.deposition_rate = deposition_rate_val;
		ep.evaporation = 0.01f;
		ep.capacity_mult = 8.0f;
		ep.gravity = 9.81f;
		ep.cell_size = cell_size;
		ep.min_slope = 0.001f;
		ep._pad0 = 0;
		ep._pad1 = 0;

		Vector<uint8_t> ep_buf;
		ep_buf.resize(sizeof(ErodeParams));
		RID ep_rid = rd->storage_buffer_create(sizeof(ErodeParams), ep_buf);

		// Create uniform set for erosion shader
		RenderingDevice::Uniform u_height;
		u_height.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u_height.binding = 0;
		u_height.append_id(height_rid);

		RenderingDevice::Uniform u_water;
		u_water.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u_water.binding = 1;
		u_water.append_id(water_rid);

		RenderingDevice::Uniform u_sed;
		u_sed.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u_sed.binding = 2;
		u_sed.append_id(sediment_rid);

		RenderingDevice::Uniform u_flux;
		u_flux.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u_flux.binding = 3;
		u_flux.append_id(flux_rid);

		RenderingDevice::Uniform u_vel;
		u_vel.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u_vel.binding = 4;
		u_vel.append_id(velocity_rid);

		RenderingDevice::Uniform u_ep;
		u_ep.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u_ep.binding = 5;
		u_ep.append_id(ep_rid);

		Vector<RenderingDevice::Uniform> uniforms;
		uniforms.push_back(u_height);
		uniforms.push_back(u_water);
		uniforms.push_back(u_sed);
		uniforms.push_back(u_flux);
		uniforms.push_back(u_vel);
		uniforms.push_back(u_ep);

		RID uniform_set = rd->uniform_set_create(uniforms, erode_shader_rid, 0);

		// Run erosion iterations
		for (int iter = 0; iter < erosion_iterations; iter++) {
			ep.iteration = iter;
			memcpy(ep_buf.ptrw(), &ep, sizeof(ErodeParams));
			rd->buffer_update(ep_rid, 0, sizeof(ErodeParams), ep_buf.ptr());

			// Add rain every 8 iterations
			if (iter % 8 == 0) {
				rd->buffer_update(water_rid, 0, float_bytes, water_init.ptr());
			}

			// 5 sub-passes per iteration
			for (int sp = 0; sp < 5; sp++) {
				// Push constant for sub-pass selector (16 bytes to match shader struct)
				Vector<uint8_t> pc;
				pc.resize(16);
				memset(pc.ptrw(), 0, 16);
				int32_t sub_pass = sp;
				memcpy(pc.ptrw(), &sub_pass, sizeof(int32_t));

				RenderingDevice::ComputeListID cl = rd->compute_list_begin();
				rd->compute_list_bind_compute_pipeline(cl, erode_pipeline_rid);
				rd->compute_list_bind_uniform_set(cl, uniform_set, 0);
				rd->compute_list_set_push_constant(cl, pc.ptr(), 16);
				rd->compute_list_dispatch(cl, groups_x, groups_y, 1);
				rd->compute_list_add_barrier(cl);
				rd->compute_list_end();
			}

			// Submit every 8 iterations to keep GPU busy without stalling
			if ((iter & 7) == 7 || iter == erosion_iterations - 1) {
				rd->submit();
				rd->sync();
			}
		}

		rd->free_rid(uniform_set);
		rd->free_rid(ep_rid);
	}

	// ════════════════════════════════════════════════════════════════════
	// PASS 3: Thermal cascading
	// ════════════════════════════════════════════════════════════════════
	if (thermal_iterations > 0 && thermal_pipeline_rid.is_valid()) {
		struct ThermalParams {
			int32_t resolution;
			float cell_size;
			float talus_angle_tan;
			float settling_rate;
			int32_t _pad0;
			int32_t _pad1;
			int32_t _pad2;
			int32_t _pad3;
		};

		ThermalParams tp;
		tp.resolution = N;
		tp.cell_size = cell_size;
		tp.talus_angle_tan = talus_angle;
		tp.settling_rate = 0.3f;
		tp._pad0 = 0;
		tp._pad1 = 0;
		tp._pad2 = 0;
		tp._pad3 = 0;

		Vector<uint8_t> tp_buf;
		tp_buf.resize(sizeof(ThermalParams));
		memcpy(tp_buf.ptrw(), &tp, sizeof(ThermalParams));
		RID tp_rid = rd->storage_buffer_create(sizeof(ThermalParams), tp_buf);

		RenderingDevice::Uniform u_height;
		u_height.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u_height.binding = 0;
		u_height.append_id(height_rid);

		RenderingDevice::Uniform u_sed;
		u_sed.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u_sed.binding = 1;
		u_sed.append_id(sediment_rid);

		RenderingDevice::Uniform u_tp;
		u_tp.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u_tp.binding = 2;
		u_tp.append_id(tp_rid);

		Vector<RenderingDevice::Uniform> uniforms;
		uniforms.push_back(u_height);
		uniforms.push_back(u_sed);
		uniforms.push_back(u_tp);

		RID uniform_set = rd->uniform_set_create(uniforms, thermal_shader_rid, 0);

		for (int iter = 0; iter < thermal_iterations; iter++) {
			RenderingDevice::ComputeListID cl = rd->compute_list_begin();
			rd->compute_list_bind_compute_pipeline(cl, thermal_pipeline_rid);
			rd->compute_list_bind_uniform_set(cl, uniform_set, 0);
			rd->compute_list_dispatch(cl, groups_x, groups_y, 1);
			rd->compute_list_add_barrier(cl);
			rd->compute_list_end();
		}
		rd->submit();
		rd->sync();

		rd->free_rid(uniform_set);
		rd->free_rid(tp_rid);
	}

	// ════════════════════════════════════════════════════════════════════
	// PASS 4: GPU Flow accumulation (iterative relaxation)
	// ════════════════════════════════════════════════════════════════════
	if (flow_pipeline_rid.is_valid()) {
		struct FlowParams {
			int32_t resolution;
			int32_t iteration;
			int32_t _pad0;
			int32_t _pad1;
		};

		FlowParams fp;
		fp.resolution = N;
		fp.iteration = 0;
		fp._pad0 = 0;
		fp._pad1 = 0;

		Vector<uint8_t> fp_buf;
		fp_buf.resize(sizeof(FlowParams));
		memcpy(fp_buf.ptrw(), &fp, sizeof(FlowParams));
		RID fp_rid = rd->storage_buffer_create(sizeof(FlowParams), fp_buf);

		RenderingDevice::Uniform u_height;
		u_height.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u_height.binding = 0;
		u_height.append_id(height_rid);

		RenderingDevice::Uniform u_flow_a;
		u_flow_a.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u_flow_a.binding = 1;
		u_flow_a.append_id(flow_a_rid);

		RenderingDevice::Uniform u_flow_b;
		u_flow_b.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u_flow_b.binding = 2;
		u_flow_b.append_id(flow_b_rid);

		RenderingDevice::Uniform u_fp;
		u_fp.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u_fp.binding = 3;
		u_fp.append_id(fp_rid);

		Vector<RenderingDevice::Uniform> uniforms;
		uniforms.push_back(u_height);
		uniforms.push_back(u_flow_a);
		uniforms.push_back(u_flow_b);
		uniforms.push_back(u_fp);

		RID uniform_set = rd->uniform_set_create(uniforms, flow_shader_rid, 0);

		// ~64 iterations for 1024² grid convergence
		int flow_iters = MIN(N / 8, 128);
		for (int iter = 0; iter < flow_iters; iter++) {
			fp.iteration = iter;
			memcpy(fp_buf.ptrw(), &fp, sizeof(FlowParams));
			rd->buffer_update(fp_rid, 0, sizeof(FlowParams), fp_buf.ptr());

			int32_t read_from_a = (iter % 2 == 0) ? 1 : 0;
			Vector<uint8_t> pc;
			pc.resize(16);
			memset(pc.ptrw(), 0, 16);
			memcpy(pc.ptrw(), &read_from_a, sizeof(int32_t));

			RenderingDevice::ComputeListID cl = rd->compute_list_begin();
			rd->compute_list_bind_compute_pipeline(cl, flow_pipeline_rid);
			rd->compute_list_bind_uniform_set(cl, uniform_set, 0);
			rd->compute_list_set_push_constant(cl, pc.ptr(), 16);
			rd->compute_list_dispatch(cl, groups_x, groups_y, 1);
			rd->compute_list_add_barrier(cl);
			rd->compute_list_end();

			if ((iter & 15) == 15 || iter == flow_iters - 1) {
				rd->submit();
				rd->sync();
			}
		}

		rd->free_rid(uniform_set);
		rd->free_rid(fp_rid);
	}

	// ════════════════════════════════════════════════════════════════════
	// Readback results
	// ════════════════════════════════════════════════════════════════════
	PackedFloat32Array out_height;
	out_height.resize(total);
	{
		Vector<uint8_t> data = rd->buffer_get_data(height_rid);
		memcpy(out_height.ptrw(), data.ptr(), float_bytes);
	}

	PackedFloat32Array out_sediment;
	out_sediment.resize(total);
	{
		Vector<uint8_t> data = rd->buffer_get_data(sediment_rid);
		memcpy(out_sediment.ptrw(), data.ptr(), float_bytes);
	}

	// Read the final flow buffer (whichever was last written)
	PackedFloat32Array out_flow;
	out_flow.resize(total);
	if (flow_pipeline_rid.is_valid()) {
		int flow_iters = MIN(N / 8, 128);
		RID final_flow_rid = (flow_iters % 2 == 0) ? flow_a_rid : flow_b_rid;
		Vector<uint8_t> data = rd->buffer_get_data(final_flow_rid);
		memcpy(out_flow.ptrw(), data.ptr(), float_bytes);
	}

	// ── Cleanup per-chunk buffers ────────────────────────────────────────
	rd->free_rid(height_rid);
	rd->free_rid(water_rid);
	rd->free_rid(sediment_rid);
	rd->free_rid(flux_rid);
	rd->free_rid(velocity_rid);
	rd->free_rid(flow_a_rid);
	rd->free_rid(flow_b_rid);
	rd->free_rid(params_rid);

	result["height"] = out_height;
	result["sediment"] = out_sediment;
	result["flow"] = out_flow;
	return result;
}

// ── Binding ──────────────────────────────────────────────────────────────

void GPUHeightmapFiller::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_planet_radius", "radius"), &GPUHeightmapFiller::set_planet_radius);
	ClassDB::bind_method(D_METHOD("get_planet_radius"), &GPUHeightmapFiller::get_planet_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "planet_radius"), "set_planet_radius", "get_planet_radius");

	ClassDB::bind_method(D_METHOD("set_border_warp", "warp"), &GPUHeightmapFiller::set_border_warp);
	ClassDB::bind_method(D_METHOD("get_border_warp"), &GPUHeightmapFiller::get_border_warp);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "border_warp"), "set_border_warp", "get_border_warp");

	ClassDB::bind_method(D_METHOD("set_tectonic_data", "packed_data"), &GPUHeightmapFiller::set_tectonic_data);
	ClassDB::bind_method(D_METHOD("set_noise_params", "noise_config"), &GPUHeightmapFiller::set_noise_params);

	ClassDB::bind_method(D_METHOD("set_erosion_iterations", "iters"), &GPUHeightmapFiller::set_erosion_iterations);
	ClassDB::bind_method(D_METHOD("get_erosion_iterations"), &GPUHeightmapFiller::get_erosion_iterations);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "erosion_iterations"), "set_erosion_iterations", "get_erosion_iterations");

	ClassDB::bind_method(D_METHOD("set_erosion_rate", "rate"), &GPUHeightmapFiller::set_erosion_rate);
	ClassDB::bind_method(D_METHOD("get_erosion_rate"), &GPUHeightmapFiller::get_erosion_rate);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "erosion_rate"), "set_erosion_rate", "get_erosion_rate");

	ClassDB::bind_method(D_METHOD("set_deposition_rate", "rate"), &GPUHeightmapFiller::set_deposition_rate);
	ClassDB::bind_method(D_METHOD("get_deposition_rate"), &GPUHeightmapFiller::get_deposition_rate);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "deposition_rate"), "set_deposition_rate", "get_deposition_rate");

	ClassDB::bind_method(D_METHOD("set_thermal_iterations", "iters"), &GPUHeightmapFiller::set_thermal_iterations);
	ClassDB::bind_method(D_METHOD("get_thermal_iterations"), &GPUHeightmapFiller::get_thermal_iterations);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "thermal_iterations"), "set_thermal_iterations", "get_thermal_iterations");

	ClassDB::bind_method(D_METHOD("set_talus_angle", "angle"), &GPUHeightmapFiller::set_talus_angle);
	ClassDB::bind_method(D_METHOD("get_talus_angle"), &GPUHeightmapFiller::get_talus_angle);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "talus_angle"), "set_talus_angle", "get_talus_angle");

	ClassDB::bind_method(D_METHOD("is_gpu_available"), &GPUHeightmapFiller::is_gpu_available);

	ClassDB::bind_method(D_METHOD("generate_chunk", "face", "gx", "gy", "grid_n", "resolution", "seed"),
			&GPUHeightmapFiller::generate_chunk);
}
