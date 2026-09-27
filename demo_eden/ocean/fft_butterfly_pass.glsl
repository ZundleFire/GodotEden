#[compute]
#version 450

// One stage of a radix-2 IFFT, applied to all 3 layers (height, Dx, Dz) of the spectra array at
// once. Dispatched log2(N) times for the horizontal pass, then log2(N) times again for the
// vertical pass (a 2D FFT is separable into a 1D FFT along each axis) -- direction and stage are
// both push constants so the same compiled shader drives every one of those dispatches, just
// ping-ponging between two array textures so a pass never reads and writes the same texel.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(rgba32f, set = 0, binding = 0) uniform restrict readonly image2D butterfly_image;
layout(rg32f, set = 0, binding = 1) uniform restrict readonly image2DArray src_image;
layout(rg32f, set = 0, binding = 2) uniform restrict writeonly image2DArray dst_image;

layout(push_constant, std430) uniform Params {
	uint n;
	uint stage;
	uint direction; // 0 = horizontal (along x), 1 = vertical (along y)
	uint pad0;
}
params;

vec2 complex_mul(vec2 a, vec2 b) {
	return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

void main() {
	uint x = gl_GlobalInvocationID.x;
	uint y = gl_GlobalInvocationID.y;
	uint layer = gl_GlobalInvocationID.z;
	if (x >= params.n || y >= params.n || layer >= 3u) {
		return;
	}

	uint along = (params.direction == 0u) ? x : y;
	vec4 bf = imageLoad(butterfly_image, ivec2(int(params.stage), int(along)));
	vec2 twiddle = bf.xy;
	uint idx1 = uint(bf.z);
	uint idx2 = uint(bf.w);

	vec2 p, q;
	if (params.direction == 0u) {
		p = imageLoad(src_image, ivec3(int(idx1), int(y), int(layer))).xy;
		q = imageLoad(src_image, ivec3(int(idx2), int(y), int(layer))).xy;
	} else {
		p = imageLoad(src_image, ivec3(int(x), int(idx1), int(layer))).xy;
		q = imageLoad(src_image, ivec3(int(x), int(idx2), int(layer))).xy;
	}

	vec2 result = p + complex_mul(twiddle, q);
	imageStore(dst_image, ivec3(x, y, layer), vec4(result, 0.0, 0.0));
}
