#include "eden_lightning.h"

#include <cmath>

// Vertex layout per channel segment: 4 vertices (a quad expanded to face the camera in the shader).
//   VERTEX = segment end point, NORMAL = segment direction, UV = (side -1..1, progress along the channel 0..1),
//   COLOR = (core width m, brightness, 0 = channel / 1 = ground splash, 1).
namespace {

struct Builder {
	PackedVector3Array verts, normals;
	PackedVector2Array uvs;
	PackedColorArray colors;
	PackedInt32Array indices;
	Vector3 origin;

	void segment(const Vector3 &p_a, const Vector3 &p_b, float p_prog_a, float p_prog_b, float p_width, float p_bright) {
		const Vector3 dir = (p_b - p_a).normalized();
		const int base = verts.size();
		const Vector3 pts[4] = { p_a, p_a, p_b, p_b };
		const Vector2 uv[4] = { Vector2(-1, p_prog_a), Vector2(1, p_prog_a), Vector2(-1, p_prog_b), Vector2(1, p_prog_b) };
		for (int i = 0; i < 4; i++) {
			verts.push_back(pts[i] - origin);
			normals.push_back(dir);
			uvs.push_back(uv[i]);
			colors.push_back(Color(p_width, p_bright, 0, 1));
		}
		const int idx[6] = { 0, 1, 2, 1, 3, 2 };
		for (int i : idx) {
			indices.push_back(base + i);
		}
	}

	void splash(const Vector3 &p_at, float p_size, float p_bright) {
		const int base = verts.size();
		const Vector2 uv[4] = { Vector2(-1, -1), Vector2(1, -1), Vector2(-1, 1), Vector2(1, 1) };
		for (int i = 0; i < 4; i++) {
			verts.push_back(p_at - origin);
			normals.push_back(Vector3(0, 1, 0));
			uvs.push_back(uv[i]);
			colors.push_back(Color(p_size, p_bright, 1, 1));
		}
		const int idx[6] = { 0, 1, 2, 1, 3, 2 };
		for (int i : idx) {
			indices.push_back(base + i);
		}
	}
};

Vector3 _random_perp(const Vector3 &p_dir, RandomPCG &r_rng) {
	Vector3 v(r_rng.randf() * 2.0f - 1.0f, r_rng.randf() * 2.0f - 1.0f, r_rng.randf() * 2.0f - 1.0f);
	v -= p_dir * v.dot(p_dir);
	return v.length_squared() > 1e-6f ? v.normalized() : p_dir.get_any_perpendicular();
}

// A wandering branch from p_from heading roughly along p_dir, drawn to p_depth levels of sub-branches
void _branch(Builder &r_b, const Vector3 &p_from, Vector3 p_dir, const Vector3 &p_down, float p_seg, int p_steps, float p_prog,
		float p_prog_step, float p_width, float p_bright, int p_depth, RandomPCG &r_rng) {
	Vector3 p = p_from;
	float prog = p_prog;
	for (int i = 0; i < p_steps; i++) {
		// Keep heading onward and a little downward, kicked sideways every step
		p_dir = (p_dir + _random_perp(p_dir, r_rng) * 0.55f + p_down * 0.15f).normalized();
		const Vector3 q = p + p_dir * p_seg * (0.6f + 0.8f * r_rng.randf());
		const float fade = 1.0f - float(i) / p_steps; // thins and dims toward its tip
		r_b.segment(p, q, prog, prog + p_prog_step, p_width * (0.4f + 0.6f * fade), p_bright * fade);
		if (p_depth > 0 && r_rng.randf() < 0.1f) {
			const Vector3 d2 = (p_dir + _random_perp(p_dir, r_rng) * 1.2f).normalized();
			_branch(r_b, q, d2, p_down, p_seg * 0.8f, p_steps / 2, prog, p_prog_step, p_width * 0.5f, p_bright * 0.6f, p_depth - 1, r_rng);
		}
		p = q;
		prog += p_prog_step;
	}
}

} // namespace

Ref<ArrayMesh> EdenLightningBolt::build_mesh(const Vector3 &p_top, const Vector3 &p_ground, const Vector3 &p_up, float p_width, RandomPCG &r_rng) {
	Builder b;
	b.origin = p_top;
	const float length = p_top.distance_to(p_ground);
	const int steps = CLAMP(int(length / 45.0f), 24, 64);
	const float seg = length / steps;
	const Vector3 down = -p_up;
	Vector3 p = p_top;
	for (int i = 1; i <= steps; i++) {
		const float prog0 = float(i - 1) / steps, prog1 = float(i) / steps;
		const Vector3 to_target = p_ground - p;
		Vector3 dir = to_target.normalized();
		// Tortuous: a strong sideways kick each step, pulled back toward the target (harder near the end,
		// so the channel lands exactly on the strike point)
		const float pull = 0.6f + 1.4f * prog1 * prog1;
		const Vector3 ahead = dir;
		dir = (dir * pull + _random_perp(dir, r_rng) * 0.7f).normalized();
		// Step so the progress toward the target (not the zig-zag length) covers what remains in the steps
		// left: a fixed step length ran out of steps well above the ground and finished in one long straight
		// drop
		const float remaining = to_target.length();
		const float forward = MAX(dir.dot(ahead), 0.35f);
		Vector3 q = p + dir * (remaining / (steps - i + 1)) * (0.85f + 0.3f * r_rng.randf()) / forward;
		if (i == steps || q.distance_to(p_ground) < seg * 0.3f) {
			q = p_ground;
		}
		b.segment(p, q, prog0, prog1, p_width, 1.0f);
		// Branches fork off the upper three quarters
		if (prog1 < 0.75f && r_rng.randf() < 0.16f) {
			const Vector3 bdir = (dir + _random_perp(dir, r_rng) * (0.6f + r_rng.randf())).normalized();
			const int bsteps = int(steps * (0.15f + 0.3f * r_rng.randf()));
			_branch(b, q, bdir, down, seg * 0.8f, MAX(bsteps, 3), prog1, 1.0f / steps, p_width * 0.45f, 0.55f, 1, r_rng);
		}
		p = q;
		if (p == p_ground) {
			break;
		}
	}
	b.splash(p_ground, p_width * 14.0f, 1.0f);

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = b.verts;
	arrays[Mesh::ARRAY_NORMAL] = b.normals;
	arrays[Mesh::ARRAY_TEX_UV] = b.uvs;
	arrays[Mesh::ARRAY_COLOR] = b.colors;
	arrays[Mesh::ARRAY_INDEX] = b.indices;
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	// The shader moves vertices sideways by up to a few metres (or pixels): keep it from being culled
	AABB aabb = mesh->get_aabb();
	mesh->set_custom_aabb(aabb.grow(50.0f));
	return mesh;
}

void EdenLightningBolt::plan_strokes(RandomPCG &r_rng) {
	leader_time = 0.03f + 0.03f * r_rng.randf();
	stroke_count = 2 + int(r_rng.randf() * 3.0f);
	stroke_count = MIN(stroke_count, MAX_STROKES);
	float t = leader_time;
	for (int i = 0; i < stroke_count; i++) {
		strokes[i] = t;
		t += 0.05f + 0.1f * r_rng.randf();
	}
	end_time = strokes[stroke_count - 1] + 0.3f;
}

float EdenLightningBolt::flash_at(float p_t) const {
	if (p_t < 0.0f || p_t > end_time) {
		return 0.0f;
	}
	float f = p_t < leader_time ? 0.15f : 0.0f; // the dim leader
	for (int i = 0; i < stroke_count; i++) {
		if (p_t >= strokes[i]) {
			f = MAX(f, std::exp(-(p_t - strokes[i]) / 0.035f));
		}
	}
	// Afterglow between strokes, fading out after the last
	const float last = strokes[stroke_count - 1];
	const float glow = p_t < last ? 0.12f : 0.12f * MAX(0.0f, 1.0f - (p_t - last) / (end_time - last));
	return MAX(f, p_t >= leader_time ? glow : 0.0f);
}

float EdenLightningBolt::reveal_at(float p_t) const {
	return CLAMP(p_t / leader_time, 0.0f, 1.0f);
}

const char *EDEN_LIGHTNING_SHADER = R"(
shader_type spatial;
render_mode skip_vertex_transform, unshaded, blend_add, depth_draw_never, cull_disabled, shadows_disabled;

uniform float flash = 0.0;
uniform float reveal = 1.0;
uniform vec3 bolt_color : source_color = vec3(0.65, 0.75, 1.0);
uniform float intensity = 25.0;
// Far strikes stay at least this many pixels wide (a 1.5 m channel is sub-pixel a few km away)
uniform float min_pixels = 1.6;

varying float v_side;
varying float v_bright;
varying float v_prog;
varying float v_splash;
varying vec2 v_quad;

void vertex() {
	vec3 wp = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	vec3 cam = INV_VIEW_MATRIX[3].xyz;
	float dist = length(wp - cam);
	float metres_per_px = dist * 2.0 / (abs(PROJECTION_MATRIX[1][1]) * VIEWPORT_SIZE.y);
	v_bright = COLOR.g;
	v_prog = UV.y;
	v_splash = COLOR.b;
	if (COLOR.b > 0.5) {
		// Ground splash: a camera-facing glow at the strike point
		float s = max(COLOR.r, metres_per_px * 10.0);
		wp += (INV_VIEW_MATRIX[0].xyz * UV.x + INV_VIEW_MATRIX[1].xyz * UV.y) * s;
		v_quad = UV;
		v_prog = 1.0;
	} else {
		// Channel segment: a ribbon facing the camera, three times the core width for the halo
		vec3 dir = normalize(mat3(MODEL_MATRIX) * NORMAL);
		vec3 side = normalize(cross(dir, normalize(wp - cam)));
		float w = max(COLOR.r, metres_per_px * min_pixels) * 3.0;
		wp += side * UV.x * w;
		v_side = UV.x;
	}
	VERTEX = (VIEW_MATRIX * vec4(wp, 1.0)).xyz;
}

void fragment() {
	if (v_prog > reveal) {
		discard;
	}
	float core, halo;
	if (v_splash > 0.5) {
		float r2 = dot(v_quad, v_quad);
		core = exp(-r2 * 30.0);
		halo = exp(-r2 * 5.0) * 0.5;
	} else {
		float e = v_side * v_side;
		core = exp(-e * 30.0);
		halo = exp(-e * 4.0) * 0.3;
	}
	// White-hot core, blue glow around it
	vec3 c = mix(bolt_color, vec3(1.0), core) * (core + halo);
	ALBEDO = c * intensity * flash * v_bright;
	ALPHA = 1.0;
}
)";
