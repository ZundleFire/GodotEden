#[compute]
#version 450

// Final pass: takes the raw IFFT output (still needing the standard per-texel sign correction
// and 1/N^2 normalisation every one of these GPU FFT implementations needs -- the transform was
// run as a disguised forward FFT, not a true inverse, which is faster but leaves those two
// corrections to the caller) and turns it into what the water shader actually samples:
//   displacement_image (RGBA16F): world-space (Dx, height, Dz, jacobian_fold)
//   normal_image       (RGBA16F): (normal.xyz, unused) from finite differences of the
//                                  DISPLACED position at neighbouring texels, so choppy
//                                  horizontal displacement bends the normal too, not just height.
// jacobian_fold is Tessendorf's wave-folding indicator: where the horizontal displacement
// compresses an area of surface instead of stretching it (Jacobian of the displacement map < a
// threshold), that patch is folding onto itself -- a breaking wave in reality, foam by convention
// here. It replaces the old shader's "steepness" foam entirely, which (see this session's own
// banding investigation) accumulated independent of wave amplitude and produced uniform stripes.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(rg32f, set = 0, binding = 0) uniform restrict readonly image2DArray ifft_image;
layout(rgba16f, set = 0, binding = 1) uniform restrict writeonly image2D displacement_image;
layout(rgba16f, set = 0, binding = 2) uniform restrict writeonly image2D normal_image;

layout(push_constant, std430) uniform Params {
	uint n;
	float patch_size;
	float choppiness;
	float height_scale;
}
params;

float sign_correct(uint x, uint y) {
	return (((x + y) & 1u) == 0u) ? 1.0 : -1.0;
}

// Sign-corrected, normalised (Dx, height, Dz) at a wrapped texel -- the patch tiles seamlessly,
// so out-of-range neighbours wrap rather than clamp.
vec3 sample_field(ivec2 coord) {
	ivec2 c = ivec2(mod(vec2(coord), float(params.n)));
	float s = sign_correct(uint(c.x), uint(c.y)) / (float(params.n) * float(params.n));
	float h = imageLoad(ifft_image, ivec3(c, 0)).x * s;
	float dx = imageLoad(ifft_image, ivec3(c, 1)).x * s;
	float dz = imageLoad(ifft_image, ivec3(c, 2)).x * s;
	return vec3(dx, h, dz);
}

void main() {
	uint x = gl_GlobalInvocationID.x;
	uint y = gl_GlobalInvocationID.y;
	if (x >= params.n || y >= params.n) {
		return;
	}

	vec3 here = sample_field(ivec2(x, y));
	float dx = here.x * params.choppiness;
	float dz = here.z * params.choppiness;
	float height = here.y * params.height_scale;
	imageStore(displacement_image, ivec2(x, y), vec4(dx, height, dz, 0.0));

	// World-space texel spacing, for turning texel-index differences into real slopes.
	float texel_size = params.patch_size / float(params.n);

	vec3 l = sample_field(ivec2(x - 1u, y));
	vec3 r = sample_field(ivec2(x + 1u, y));
	vec3 d = sample_field(ivec2(x, y - 1u));
	vec3 u = sample_field(ivec2(x, y + 1u));

	vec3 pos_l = vec3(-texel_size + l.x * params.choppiness, l.y * params.height_scale, l.z * params.choppiness);
	vec3 pos_r = vec3(texel_size + r.x * params.choppiness, r.y * params.height_scale, r.z * params.choppiness);
	vec3 pos_d = vec3(d.x * params.choppiness, d.y * params.height_scale, -texel_size + d.z * params.choppiness);
	vec3 pos_u = vec3(u.x * params.choppiness, u.y * params.height_scale, texel_size + u.z * params.choppiness);

	vec3 tangent_x = pos_r - pos_l;
	vec3 tangent_z = pos_u - pos_d;
	vec3 normal = normalize(cross(tangent_z, tangent_x));

	// Jacobian of the horizontal displacement map: how much a texel-sized patch of the UNDISPLACED
	// grid has been stretched (>0) or folded over itself (<=0, cannot happen physically -- a
	// breaking wave/foam) by the choppy displacement. Ddx/Ddx_x and Ddz/Ddz_z are the diagonal
	// terms; the off-diagonal cross terms are dropped (Tessendorf's own simplified foam estimate)
	// since they are a second-order refinement on top of what already reads correctly as foam.
	float ddx_dx = (r.x - l.x) * params.choppiness / (2.0 * texel_size);
	float ddz_dz = (u.z - d.z) * params.choppiness / (2.0 * texel_size);
	float jacobian = (1.0 + ddx_dx) * (1.0 + ddz_dz);
	float fold = clamp(1.0 - jacobian, 0.0, 1.0);

	imageStore(normal_image, ivec2(x, y), vec4(normal, fold));
}
