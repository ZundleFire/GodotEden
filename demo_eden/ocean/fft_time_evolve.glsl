#[compute]
#version 450

// Per-frame: evolves the static h0(k) spectrum forward to time t, producing THREE complex
// spectra packed as layers of one RG32F array texture -- height, and the two horizontal
// "choppy wave" displacement components (Tessendorf's D(x,t)) -- so the same butterfly IFFT
// pass can transform all three in one dispatch instead of three separate ones.
//
// h(k,t)  = h0(k)*exp(i*omega*t) + conj(h0(-k))*exp(-i*omega*t)     -- height
// D(k,t)  = i * (k/|k|) * h(k,t)                                    -- horizontal displacement,
//           one component per axis, magnitude/direction from k itself
//
// omega(k) = sqrt(g*|k|) is the deep-water dispersion relation -- same physics as
// planet_water.gdshader's Gerstner stack, just applied per-frequency-bin instead of to a
// handful of hand-picked waves.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(rg32f, set = 0, binding = 0) uniform restrict readonly image2D h0_image;
layout(rg32f, set = 0, binding = 1) uniform restrict writeonly image2DArray spectra_image;

layout(push_constant, std430) uniform Params {
	uint n;
	float patch_size;
	float time;
	float pad0;
}
params;

const float PI = 3.14159265359;
const float GRAVITY = 9.81;

vec2 complex_mul(vec2 a, vec2 b) {
	return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

void main() {
	uint x = gl_GlobalInvocationID.x;
	uint y = gl_GlobalInvocationID.y;
	if (x >= params.n || y >= params.n) {
		return;
	}

	vec2 k = vec2(2.0 * PI * (float(x) - float(params.n) * 0.5) / params.patch_size,
			2.0 * PI * (float(y) - float(params.n) * 0.5) / params.patch_size);
	float k_len = length(k);

	vec2 h0_k = imageLoad(h0_image, ivec2(x, y)).xy;
	ivec2 neg_coord = ivec2((int(params.n) - int(x)) % int(params.n), (int(params.n) - int(y)) % int(params.n));
	vec2 h0_neg_k = imageLoad(h0_image, neg_coord).xy;
	vec2 h0_conj_neg_k = vec2(h0_neg_k.x, -h0_neg_k.y);

	float omega = sqrt(GRAVITY * max(k_len, 1e-6));
	float phase = omega * params.time;
	vec2 fwd = vec2(cos(phase), sin(phase));
	vec2 bwd = vec2(cos(phase), -sin(phase));

	vec2 h = complex_mul(h0_k, fwd) + complex_mul(h0_conj_neg_k, bwd);

	vec2 dx_spec = vec2(0.0);
	vec2 dz_spec = vec2(0.0);
	if (k_len > 1e-6) {
		vec2 k_hat = k / k_len;
		// i * s * h, s real: (a+bi)->(-b*s, a*s)
		dx_spec = vec2(-h.y * k_hat.x, h.x * k_hat.x);
		dz_spec = vec2(-h.y * k_hat.y, h.x * k_hat.y);
	}

	imageStore(spectra_image, ivec3(x, y, 0), vec4(h, 0.0, 0.0));
	imageStore(spectra_image, ivec3(x, y, 1), vec4(dx_spec, 0.0, 0.0));
	imageStore(spectra_image, ivec3(x, y, 2), vec4(dz_spec, 0.0, 0.0));
}
