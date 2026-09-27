#[compute]
#version 450

// Initial Phillips-spectrum wave amplitudes h0(k) for a Tessendorf ocean. Run once at startup
// (and again if wind/patch parameters change) -- the per-frame pass only evolves these in time,
// it never regenerates them, so the same random "seed field" of waves persists across the whole
// run instead of reshuffling every frame.
//
// Output: RG32F image, size (N, N). Texel (x,y) = h0(k) as (real, imag), where k is the
// wavevector for FFT bin (x,y) on an N x N grid tiling an L x L world-space patch.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(rg32f, set = 0, binding = 0) uniform restrict writeonly image2D h0_image;

layout(push_constant, std430) uniform Params {
	uint n;
	float patch_size;
	float wind_speed;
	float wind_angle; // radians
	float amplitude; // Phillips spectrum's "A" constant
	float seed;
	// Wavelengths shorter than this are damped out of the spectrum before it ever reaches the
	// IFFT -- should track whatever mesh actually displays the result (its own vertex spacing,
	// not this FFT texture's much finer texel spacing). Undamped, energy piles up at the
	// shortest wavelengths the spectrum can represent; a mesh far coarser than the FFT grid
	// cannot display those and the result aliases into sharp, chaotic, self-overlapping spikes
	// (screenshot-confirmed this session -- root cause was the OLD damping term derived from
	// wind_speed^2/g, a length scale ~24x smaller than this grid's own Nyquist wavelength, so it
	// was suppressing nothing any mesh here would ever have been able to alias on in the first
	// place).
	float min_wavelength;
	float pad1;
}
params;

const float PI = 3.14159265359;
const float GRAVITY = 9.81;

// Cheap deterministic hash -> [0,1). Two calls with different salts give the independent uniform
// pair Box-Muller needs; a texel's random values are then repeatable run to run for the same seed,
// which matters for A/B comparing parameter changes.
float hash(vec2 p, float salt) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031 + salt);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

vec2 gaussian_pair(vec2 texel, float seed) {
	float u1 = max(hash(texel, seed), 1e-6);
	float u2 = hash(texel, seed + 17.0);
	float r = sqrt(-2.0 * log(u1));
	float theta = 2.0 * PI * u2;
	return vec2(r * cos(theta), r * sin(theta));
}

void main() {
	uint x = gl_GlobalInvocationID.x;
	uint y = gl_GlobalInvocationID.y;
	if (x >= params.n || y >= params.n) {
		return;
	}

	// Wavevector for this FFT bin, centred so bin N/2 is k=0 -- standard FFT frequency layout.
	vec2 k = vec2(2.0 * PI * (float(x) - float(params.n) * 0.5) / params.patch_size,
			2.0 * PI * (float(y) - float(params.n) * 0.5) / params.patch_size);
	float k_len = length(k);

	if (k_len < 1e-6) {
		imageStore(h0_image, ivec2(x, y), vec4(0.0));
		return;
	}

	vec2 wind_dir = vec2(cos(params.wind_angle), sin(params.wind_angle));
	float wind_speed2 = max(params.wind_speed, 0.001) * max(params.wind_speed, 0.001);
	float L = wind_speed2 / GRAVITY; // largest wave a steady wind of this speed can sustain

	float k_len2 = k_len * k_len;
	float k_dot_w = dot(k / k_len, wind_dir);

	// Classic Phillips spectrum: energy peaks around wavelength L in the wind direction, falls
	// off as k^-4 elsewhere, and a small-wavelength damping term suppresses wavelengths the
	// DISPLAYING MESH cannot represent (see min_wavelength's own comment above) instead of
	// letting that energy alias into sharp, chaotic geometry.
	float l_small = params.min_wavelength / (2.0 * PI);
	float phillips = params.amplitude
			* exp(-1.0 / (k_len2 * L * L)) / (k_len2 * k_len2)
			* pow(max(k_dot_w, 0.0), 2.0)
			* exp(-k_len2 * l_small * l_small);

	vec2 gauss = gaussian_pair(vec2(x, y), params.seed);
	vec2 h0 = gauss * sqrt(max(phillips, 0.0) * 0.5);

	imageStore(h0_image, ivec2(x, y), vec4(h0, 0.0, 0.0));
}
