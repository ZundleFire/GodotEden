shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

// Building pieces (EdenBuildPieces): wooden planks or coursed stone blocks drawn from the local position, so
// neighbouring pieces continue each other's pattern. `tint` (per instance) overlays the support colour in build mode.

uniform bool u_stone = false;
uniform vec3 u_color : source_color = vec3(0.55, 0.38, 0.22);
instance uniform vec4 tint = vec4(0.0);

varying vec3 v_local;
varying vec3 v_normal_local;

float hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void vertex() {
	v_local = VERTEX;
	v_normal_local = NORMAL;
}

void fragment() {
	// Face coordinates: the two local axes across this face
	vec3 an = abs(v_normal_local);
	vec2 uv = an.x > an.y && an.x > an.z ? v_local.zy : (an.y > an.z ? v_local.xz : v_local.xy);
	vec3 col = u_color;
	if (u_stone) {
		// Courses 0.5 m tall of 1 m blocks, every other course offset by half a block
		float row = floor(uv.y / 0.5);
		vec2 cell = vec2(floor(uv.x / 1.0 + row * 0.5), row);
		vec2 f = vec2(fract(uv.x / 1.0 + row * 0.5), fract(uv.y / 0.5));
		float mortar = step(f.x, 0.03) + step(f.y, 0.06);
		col *= 0.85 + 0.3 * hash(cell);
		col = mix(col, vec3(0.3, 0.29, 0.27), clamp(mortar, 0.0, 1.0));
	} else {
		// Planks 0.25 m wide across the face's second axis, each a slightly different shade, with dark seams
		float p = uv.y / 0.25;
		float plank = floor(p);
		col *= 0.82 + 0.3 * hash(vec2(plank, 7.0));
		col *= 0.92 + 0.08 * sin(uv.x * 9.0 + plank * 3.1); // grain
		col = mix(col, vec3(0.18, 0.11, 0.06), step(fract(p), 0.05));
	}
	ALBEDO = mix(col, tint.rgb, tint.a);
	ROUGHNESS = u_stone ? 0.9 : 0.8;
}
