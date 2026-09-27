#[compute]
#version 450

// Precomputes the FFT "butterfly texture": for every (stage, index) pair in a radix-2
// decimation-in-time FFT of size N, which two input samples combine at that stage and what
// twiddle factor multiplies the second one. Run once at startup (and again if N changes) --
// every later per-frame IFFT pass just looks this up instead of recomputing bit-reversal and
// trig each frame. Standard technique; the exact structure (bit-reversed indices at stage 0,
// index/index+span pairing at every other stage) matches the reference implementations this
// was checked against (Tessendorf's original ocean paper's GPU ports).
//
// Output: RGBA32F image, size (log2(N), N). Texel (stage, index) = (twiddle.re, twiddle.im,
// input_index_1, input_index_2).

layout(local_size_x = 1, local_size_y = 64, local_size_z = 1) in;

layout(rgba32f, set = 0, binding = 0) uniform restrict writeonly image2D butterfly_image;

layout(push_constant, std430) uniform Params {
	uint n;
	uint log2n;
	uint pad0;
	uint pad1;
}
params;

const float PI = 3.14159265359;

uint bit_reverse(uint v, uint bits) {
	uint r = 0u;
	for (uint i = 0u; i < bits; i++) {
		r = (r << 1u) | (v & 1u);
		v >>= 1u;
	}
	return r;
}

void main() {
	uint stage = gl_GlobalInvocationID.x;
	uint index = gl_GlobalInvocationID.y;
	if (stage >= params.log2n || index >= params.n) {
		return;
	}

	float span_f = pow(2.0, float(stage));
	float pair_span_f = span_f * 2.0;
	uint span = uint(span_f);

	float k = mod(float(index) * (float(params.n) / pair_span_f), float(params.n));
	float angle = 2.0 * PI * k / float(params.n);
	vec2 twiddle = vec2(cos(angle), sin(angle));

	bool top_wing = mod(float(index), pair_span_f) < span_f;

	uint idx1;
	uint idx2;
	if (stage == 0u) {
		// First stage reads directly from the (unordered) input, so the pairing must land on
		// bit-reversed positions -- every later stage just walks index/index+span in natural order.
		if (top_wing) {
			idx1 = bit_reverse(index, params.log2n);
			idx2 = bit_reverse(index + 1u, params.log2n);
		} else {
			idx1 = bit_reverse(index - 1u, params.log2n);
			idx2 = bit_reverse(index, params.log2n);
		}
	} else {
		if (top_wing) {
			idx1 = index;
			idx2 = index + span;
		} else {
			idx1 = index - span;
			idx2 = index;
		}
	}

	imageStore(butterfly_image, ivec2(stage, index), vec4(twiddle, float(idx1), float(idx2)));
}
