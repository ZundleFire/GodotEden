#include "eden_planet_ocean.h"

#include "eden_ocean_shaders.gen.h"

#include "modules/noise/fastnoise_lite.h"
#include "modules/noise/noise_texture_2d.h"
#include "core/config/project_settings.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/visual_instance_3d.h"
#include "scene/main/canvas_item.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/main/viewport.h"
#include "scene/resources/gradient.h"
#include "scene/resources/mesh.h"
#include "core/templates/local_vector.h"
#include "servers/rendering/rendering_server.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_interface.h"
#endif

// [normal, axis_u, axis_v] per cube face, axis_u x axis_v == normal. Same table as ocean_shell.gd.
static const Vector3 FACE_BASES[6][3] = {
	{ Vector3(1, 0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0) },
	{ Vector3(-1, 0, 0), Vector3(0, 0, 1), Vector3(0, 1, 0) },
	{ Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, -1) },
	{ Vector3(0, -1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1) },
	{ Vector3(0, 0, 1), Vector3(1, 0, 0), Vector3(0, 1, 0) },
	{ Vector3(0, 0, -1), Vector3(-1, 0, 0), Vector3(0, 1, 0) },
};

static const int MAX_DEPTH = 20;

// Equirect convention copied verbatim from EdenCloudShell::_sample_v3_equirect: lat = asin(y),
// lon = atan2(z, x), x = lon/TAU + 0.5. Deliberately NOT Godot's panorama convention -- the
// generator's images use their own, and sampling them any other way puts every continent in the
// wrong place (and would put the coastlines the currents steer around in the wrong place too).
static float _sample_equirect_channel(const Ref<Image> &p_img, const Vector3 &p_dir, int p_channel) {
	const int w = p_img->get_width();
	const int h = p_img->get_height();
	const float lat = Math::asin(CLAMP(p_dir.y, -1.0f, 1.0f));
	const float lon = Math::atan2(p_dir.z, p_dir.x);
	const float fx = (lon / (float)Math::TAU + 0.5f) * (float)w - 0.5f;
	const float fy = (0.5f - lat / (float)Math::PI) * (float)h - 0.5f;
	const int x0 = (int)Math::floor(fx);
	const int y0 = (int)Math::floor(fy);
	const float ax = fx - (float)x0;
	const float ay = fy - (float)y0;
	auto px = [&](int x, int y) {
		x = ((x % w) + w) % w; // longitude wraps
		y = CLAMP(y, 0, h - 1); // latitude clamps at the poles
		return p_img->get_pixel(x, y)[p_channel];
	};
	const float top = Math::lerp(px(x0, y0), px(x0 + 1, y0), ax);
	const float bottom = Math::lerp(px(x0, y0 + 1), px(x0 + 1, y0 + 1), ax);
	return Math::lerp(top, bottom, ay);
}

EdenPlanetOcean::~EdenPlanetOcean() {
	// Leaf MeshInstance3Ds are children and die with the node; only the tree structs are ours.
	for (QuadNode *root : roots) {
		if (root) {
			_free_node(root);
		}
	}
}

void EdenPlanetOcean::_free_node(QuadNode *p_node) {
	for (QuadNode *c : p_node->children) {
		if (c) {
			_free_node(c);
		}
	}
	memdelete(p_node);
}

void EdenPlanetOcean::_free_leaf_mesh(QuadNode *p_node) {
	if (p_node->mesh_instance) {
		p_node->mesh_instance->queue_free();
		p_node->mesh_instance = nullptr;
	}
}

void EdenPlanetOcean::_merge(QuadNode *p_node) {
	if (p_node->is_leaf()) {
		return;
	}
	for (QuadNode *&c : p_node->children) {
		_merge(c);
		_free_leaf_mesh(c);
		memdelete(c);
		c = nullptr;
	}
}

void EdenPlanetOcean::_clear_tree() {
	for (QuadNode *&root : roots) {
		if (root) {
			if (!root->is_leaf()) {
				_merge(root);
			}
			_free_leaf_mesh(root);
			memdelete(root);
			root = nullptr;
		}
	}
	rebuild_accum = rebuild_interval; // rebuild on the next process tick
}

Vector3 EdenPlanetOcean::_sphere_dir(int p_face, float p_u, float p_v) const {
	return (FACE_BASES[p_face][0] + FACE_BASES[p_face][1] * p_u + FACE_BASES[p_face][2] * p_v).normalized();
}

// Every viewpoint the ocean should refine around. LOD is driven by ALL of them, not just one:
// the editor can show up to four 3D views at once, each with its own camera, and a scene can run
// several viewports (split screen, a security-monitor SubViewport). With a single camera the other
// views showed whatever LOD the one tracked camera happened to earn, which from a second editor
// view looks like the ocean is permanently stuck at its coarsest level.
void EdenPlanetOcean::_camera_positions(LocalVector<Vector3> &r_out) const {
	r_out.clear();
	Viewport *vp = get_viewport();
	if (vp && vp->get_camera_3d()) {
		r_out.push_back(vp->get_camera_3d()->get_global_position());
	}
#ifdef TOOLS_ENABLED
	// With the scene not running the only cameras are the editor's own. All four viewports, but
	// only the ones actually on screen -- a hidden split view would otherwise pin full detail
	// around wherever its camera was last left.
	if (Engine::get_singleton()->is_editor_hint() && EditorInterface::get_singleton()) {
		for (int i = 0; i < 4; i++) {
			SubViewport *editor_vp = EditorInterface::get_singleton()->get_editor_viewport_3d(i);
			if (editor_vp == nullptr || editor_vp->get_camera_3d() == nullptr) {
				continue;
			}
			// A SubViewport is a plain Node with no visibility of its own; what gets hidden when a
			// split view is off is the CanvasItem chain holding it, so test that instead.
			const CanvasItem *host = Object::cast_to<CanvasItem>(editor_vp->get_parent());
			if (host != nullptr && !host->is_visible_in_tree()) {
				continue;
			}
			r_out.push_back(editor_vp->get_camera_3d()->get_global_position());
		}
	}
#endif
	if (r_out.is_empty()) {
		// No camera anywhere: keep the old stand-in so the tree still builds something sane.
		r_out.push_back(get_global_position() + get_global_basis().get_column(1) * (planet_radius + 250.0f));
	}
}

// Single viewpoint, for the near-field ripple pass -- that one is a small fixed budget of nearby
// objects and only makes sense around one viewer.
Vector3 EdenPlanetOcean::_camera_position() const {
	LocalVector<Vector3> cams;
	_camera_positions(cams);
	return cams[0];
}

void EdenPlanetOcean::update_lod() {
	if (!is_inside_tree()) {
		return;
	}
	LocalVector<Vector3> cams_world;
	_camera_positions(cams_world);
	last_lod_cams = cams_world;
	LocalVector<Vector3> cams_local;
	cams_local.resize(cams_world.size());
	for (uint32_t i = 0; i < cams_world.size(); i++) {
		cams_local[i] = to_local(cams_world[i]);
	}
	for (int i = 0; i < 6; i++) {
		if (!roots[i]) {
			roots[i] = memnew(QuadNode);
			roots[i]->face = i;
		}
		_update_node(roots[i], cams_local);
	}
	if (current_dirty) {
		bake_currents();
	}
	_push_runtime_params();
}

// Runtime-only shader inputs. The current cubemap goes straight to the RenderingServer rather than
// through ShaderMaterial::set_shader_parameter, so it is not serialised into the scene with the
// material (it is ~100 KB and always rebakes from the seed anyway). Re-pushed every LOD pass, which
// also covers a swapped material or an edited shader.
void EdenPlanetOcean::_push_runtime_params() {
	Ref<ShaderMaterial> sm = material;
	if (sm.is_null()) {
		return;
	}
	sm->set_shader_parameter("planet_center", get_global_position());
	sm->set_shader_parameter("planet_radius", planet_radius);
	// Lets the shader turn a patch's baked lod_range back into its vertex spacing, which is what it
	// band limits the wave set against (see the shader's lod_vertex_spacing).
	sm->set_shader_parameter("lod_vertex_spacing", 1.0f / (2.0f * MAX(split_distance_factor, 0.01f) * (float)grid_resolution));
	RenderingServer *rs = RenderingServer::get_singleton();
	const bool have_map = current_enabled && current_map.is_valid();
	rs->material_set_param(sm->get_rid(), "current_enabled", have_map);
	if (have_map) {
		rs->material_set_param(sm->get_rid(), "current_map", current_map->get_rid());
	}
}

void EdenPlanetOcean::_update_node(QuadNode *p_node, const LocalVector<Vector3> &p_cams_local) {
	// World size is approximate: cube-face extent scaled linearly to arc length (as ocean_shell.gd).
	const float world_size = p_node->half_size * 2.0f * planet_radius;
	const Vector3 center = _sphere_dir(p_node->face, p_node->center.x, p_node->center.y) * planet_radius;
	// Nearest of all viewpoints. Taking the minimum is what makes the tree refine around every
	// camera at once: a patch is only allowed to stay coarse if it is far from ALL of them.
	float dist = FLT_MAX;
	for (const Vector3 &c : p_cams_local) {
		dist = MIN(dist, c.distance_to(center));
	}
	// Split on the patch's NEAREST point (centre distance minus a conservative radius: half-diagonal
	// in face-uv bounds the angle, x1.5 for margin, +10 m for wave crests), not its centre. That makes
	// the split test a true "any point within range" test, which is what the shader's per-vertex
	// geomorph relies on: an unsplit patch then has every point at least `range` away, so the finer
	// neighbour's vertices along the shared edge are always fully morphed onto it -- no seam.
	const float bound = p_node->half_size * 1.5f * planet_radius + 10.0f;
	const bool split = dist - bound < world_size * split_distance_factor && world_size > min_leaf_world_size && p_node->depth < MAX_DEPTH;

	if (split) {
		if (p_node->is_leaf()) {
			_free_leaf_mesh(p_node);
			const float h = p_node->half_size * 0.5f;
			for (int i = 0; i < 4; i++) {
				QuadNode *c = memnew(QuadNode);
				c->face = p_node->face;
				c->center = p_node->center + Vector2((i & 1) ? h : -h, (i & 2) ? h : -h);
				c->half_size = h;
				c->depth = p_node->depth + 1;
				p_node->children[i] = c;
			}
		}
		for (QuadNode *c : p_node->children) {
			_update_node(c, p_cams_local);
		}
		return;
	}

	if (!p_node->is_leaf()) {
		_merge(p_node);
	}

	// Near-field handoff (e.g. to VoxelWaterSimulator): hide leaves close to the camera. Uses the
	// nearest camera, so anything inside the handoff radius of ANY viewer is handed off.
	bool visible = dist >= near_field_radius;
	// Horizon culling: from above the sea, a patch whose angular distance from the point under the
	// camera exceeds the horizon angle plus its own angular radius can't be seen. (Its half-diagonal
	// in face-uv units bounds its angular radius in radians; +0.01 rad covers wave crests.)
	// Visible if ANY camera can see it -- unlike the split test this cannot use a single reduced
	// distance, since each viewpoint has its own horizon circle centred on its own sub-point.
	if (visible && horizon_culling) {
		bool any_sees = false;
		for (const Vector3 &cam : p_cams_local) {
			const float cam_r = cam.length();
			if (cam_r <= planet_radius) {
				any_sees = true; // at or below sea level: no horizon to cull against
				break;
			}
			const float horizon = Math::acos(planet_radius / cam_r);
			const float angle = Math::acos(CLAMP(cam.dot(center) / (cam_r * planet_radius), -1.0f, 1.0f));
			if (angle < horizon + p_node->half_size * 1.5f + 0.01f) {
				any_sees = true;
				break;
			}
		}
		visible = any_sees;
	}
	if (!visible) {
		if (p_node->mesh_instance) {
			p_node->mesh_instance->set_visible(false);
		}
		return;
	}
	if (!p_node->mesh_instance) {
		_build_leaf_mesh(p_node);
	} else {
		p_node->mesh_instance->set_visible(true);
	}
}

void EdenPlanetOcean::_build_leaf_mesh(QuadNode *p_node) {
	const int res = grid_resolution;
	const int row = res + 1;
	const Vector3 origin = _sphere_dir(p_node->face, p_node->center.x, p_node->center.y) * planet_radius;
	// Capped so an unsplit face-sized patch does not grow kilometre-deep skirts, and floored because
	// a LOD0 leaf is only ~10 m across -- 3% of that is thinner than the waves it has to hide behind.
	const float skirt_depth = MAX(MIN(p_node->half_size * 2.0f * planet_radius, 64.0f) * skirt_depth_ratio, 2.0f);

	PackedVector3Array positions;
	PackedVector3Array normals;
	PackedVector2Array uvs;
	PackedInt32Array indices;
	PackedFloat32Array morph_a; // CUSTOM0/CUSTOM1: offsets to the two parent-grid vertices this
	PackedFloat32Array morph_b; // vertex lies between (zero where the parent has it too)
	// This level's morph range: the parent's split distance, beyond which a vertex is fully morphed
	// onto the parent's grid (see _update_node). Roots have no parent and never morph.
	const float lod_range = p_node->depth > 0 ? p_node->half_size * 4.0f * planet_radius * split_distance_factor : 0.0f;
	positions.resize(row * row + 4 * row);
	morph_a.resize(positions.size() * 4);
	morph_b.resize(positions.size() * 4);
	morph_a.fill(0.0f);
	morph_b.fill(0.0f);
	float *ma = morph_a.ptrw();
	float *mb = morph_b.ptrw();
	normals.resize(positions.size());
	uvs.resize(positions.size());
	Vector3 *pw = positions.ptrw();
	Vector3 *nw = normals.ptrw();
	Vector2 *uw = uvs.ptrw();

	for (int j = 0; j <= res; j++) {
		for (int i = 0; i <= res; i++) {
			const float u = p_node->center.x - p_node->half_size + 2.0f * p_node->half_size * i / res;
			const float v = p_node->center.y - p_node->half_size + 2.0f * p_node->half_size * j / res;
			const Vector3 n = _sphere_dir(p_node->face, u, v);
			const int k = j * row + i;
			pw[k] = n * planet_radius - origin;
			nw[k] = n;
			uw[k] = Vector2(float(i) / res, float(j) / res);
			mb[k * 4 + 3] = lod_range; // CUSTOM1.w; the skirt loop below copies it along
		}
	}
	// Geomorph targets. The parent covers this patch with res/2 cells, so its vertices are our even
	// ones; an odd vertex lies on the parent edge between its two even neighbours, and an odd/odd one
	// on the parent quad's (i,j)-(i+1,j+1) diagonal -- the diagonal the triangles below split along.
	for (int j = 0; j <= res; j++) {
		for (int i = 0; i <= res; i++) {
			if (!((i | j) & 1)) {
				continue;
			}
			int a;
			int b;
			if ((i & 1) && (j & 1)) {
				a = (j - 1) * row + (i - 1);
				b = (j + 1) * row + (i + 1);
			} else if (i & 1) {
				a = j * row + i - 1;
				b = j * row + i + 1;
			} else {
				a = (j - 1) * row + i;
				b = (j + 1) * row + i;
			}
			const int k = j * row + i;
			const Vector3 da = pw[a] - pw[k];
			const Vector3 db = pw[b] - pw[k];
			ma[k * 4 + 0] = da.x;
			ma[k * 4 + 1] = da.y;
			ma[k * 4 + 2] = da.z;
			mb[k * 4 + 0] = db.x;
			mb[k * 4 + 1] = db.y;
			mb[k * 4 + 2] = db.z;
		}
	}
	// Winding as in ocean_shell.gd: front faces point away from the planet (re-verified with a
	// back-face-culled StandardMaterial3D lit from behind the camera).
	//
	// The two triangles of a quad deliberately start at OPPOSITE corners (a and c). The shader's
	// low-poly mode flat-shades from the provoking vertex, which is the first one, so listing both
	// triangles from `a` would hand them the same normal and the sea would facet into squares
	// instead of triangles. (c, a, d) is a rotation of (a, d, c), so the winding is unchanged.
	for (int j = 0; j < res; j++) {
		for (int i = 0; i < res; i++) {
			const int a = j * row + i, b = a + 1, c = a + row + 1, d = a + row;
			indices.append(a);
			indices.append(c);
			indices.append(b);
			indices.append(c);
			indices.append(a);
			indices.append(d);
		}
	}
	// Skirts: a curtain dropped toward the planet centre along every edge hides T-junction cracks
	// against any differently-leveled neighbour without neighbour lookups.
	int next = row * row;
	for (int e = 0; e < 4; e++) {
		int prev_edge = -1, prev_skirt = -1;
		for (int s = 0; s <= res; s++) {
			const int fixed = (e & 1) ? res : 0;
			const int edge = (e < 2) ? s * row + fixed : fixed * row + s;
			pw[next] = pw[edge] - nw[edge] * skirt_depth;
			nw[next] = nw[edge];
			uw[next] = Vector2();
			for (int c = 0; c < 4; c++) { // a skirt vertex morphs with its edge vertex
				ma[next * 4 + c] = ma[edge * 4 + c];
				mb[next * 4 + c] = mb[edge * 4 + c];
			}
			ma[next * 4 + 3] = 1.0f; // CUSTOM0.w: skirt flag, shaded on the shader's cheap path
			if (prev_skirt >= 0) {
				indices.append(prev_edge);
				indices.append(next);
				indices.append(edge);
				indices.append(prev_edge);
				indices.append(prev_skirt);
				indices.append(next);
			}
			prev_edge = edge;
			prev_skirt = next++;
		}
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = positions;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_INDEX] = indices;
	arrays[Mesh::ARRAY_CUSTOM0] = morph_a;
	arrays[Mesh::ARRAY_CUSTOM1] = morph_b;
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(),
			(Mesh::ARRAY_CUSTOM_RGBA_FLOAT << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT) |
					(Mesh::ARRAY_CUSTOM_RGBA_FLOAT << Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT));

	MeshInstance3D *mi = memnew(MeshInstance3D);
	mi->set_mesh(mesh);
	mi->set_material_override(material);
	mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	mi->set_extra_cull_margin(32.0f); // vertex waves move the surface outside the mesh AABB
	mi->set_position(origin);
	add_child(mi, false, INTERNAL_MODE_BACK);
	p_node->mesh_instance = mi;
}

// p_lo_until: ramp stays at p_lo up to this offset, so only the top of the noise range shows.
static Ref<NoiseTexture2D> _make_noise(FastNoiseLite::NoiseType p_type, float p_freq, const Color &p_lo, const Color &p_hi, int p_seed, float p_lo_until = 0.0f) {
	Ref<FastNoiseLite> noise;
	noise.instantiate();
	noise->set_noise_type(p_type);
	noise->set_frequency(p_freq);
	noise->set_seed(p_seed);
	noise->set_fractal_type(FastNoiseLite::FRACTAL_FBM);
	noise->set_fractal_octaves(3);
	Ref<Gradient> ramp;
	ramp.instantiate();
	ramp->set_color(0, p_lo);
	ramp->set_color(1, p_hi);
	if (p_lo_until > 0.0f) {
		ramp->add_point(p_lo_until, p_lo);
	}
	Ref<NoiseTexture2D> tex;
	tex.instantiate();
	tex->set_width(512);
	tex->set_height(512);
	tex->set_seamless(true);
	tex->set_generate_mipmaps(true);
	tex->set_color_ramp(ramp);
	tex->set_noise(noise);
	return tex;
}

static PackedFloat32Array _floats(std::initializer_list<float> p_values) {
	PackedFloat32Array a;
	for (float v : p_values) {
		a.push_back(v);
	}
	return a;
}

// The built-in material. Textures are procedural stand-ins for upstream's PNGs (albedo caustics,
// refraction noise, foam with alpha); wave arrays are upstream's deep_ocean_material.tres preset.
Ref<ShaderMaterial> EdenPlanetOcean::make_default_material() {
	Ref<ShaderMaterial> default_material;
	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(String::utf8(EDEN_OCEAN_SHADER_CODE));
	default_material.instantiate();
	default_material->set_shader(shader);
	// Before other transparents: the ocean writes depth, so clouds drawn after it are correctly
	// hidden below the horizon and correctly drawn over the water. With the default priority,
	// back-to-front sorting by object centre put the cloud shell (centred on the planet) first and
	// the near ocean patches painted over it.
	default_material->set_render_priority(-1);

	default_material->set_shader_parameter("texture_albedo", _make_noise(FastNoiseLite::TYPE_CELLULAR, 0.02f, Color(0.75, 0.8, 0.85), Color(1, 1, 1), 1));
	default_material->set_shader_parameter("texture_refraction", _make_noise(FastNoiseLite::TYPE_SIMPLEX_SMOOTH, 0.01f, Color(0, 0, 0), Color(1, 1, 1), 2));
	default_material->set_shader_parameter("texture_foam", _make_noise(FastNoiseLite::TYPE_CELLULAR, 0.03f, Color(1, 1, 1, 0), Color(1, 1, 1, 1), 3, 0.65f));

	// Wave spectrum. Upstream's four waves were two near-parallel 16 m ripples plus one 50 m swell
	// 2.5 m high and one 0.5 m ripple: nothing at all between 16 m and 0.5 m, so the sea had no
	// feature anywhere near the size of a person or a boat and read as either huge or tiny depending
	// on what you compared it to. This is a wind sea instead -- eight components on a roughly
	// geometric wavelength ladder (140 m down to 3.4 m, each ~1.7x the next), amplitudes falling with
	// wavelength for a significant wave height of ~1.4 m, and directions fanned +-72 degrees about
	// the mean so crests are short and interfere instead of marching in ranks.
	//
	// The ladder stops at 3.4 m on purpose: LOD0 vertices are ~1 m apart, so that is the shortest
	// wave the finest patches can carry without the shader's band limiting eating it (see
	// lod_vertex_spacing). Shorter waves want a smaller min_leaf_world_size to go with them.
	//
	// WaveSteepnesses is wave height * WaveCount (the shader averages over the set), WaveFrequencies
	// is 1 / wavelength, and WaveSpeeds is deep-water dispersion, TAU * 1.25 / sqrt(wavelength):
	// long swell rolls through short chop at three times its speed, which is most of what makes a
	// sea look alive. WaveAmplitudes is the Gerstner steepness Q (0.85: sharpened crests, flat
	// troughs, no self-intersection). Phases are scattered so the whole set does not line up at t=0.
	default_material->set_shader_parameter("WaveCount", 8);
	default_material->set_shader_parameter("WaveSteepnesses", _floats({ 2.688, 2.112, 1.536, 1.075, 0.691, 0.422, 0.269, 0.166 }));
	default_material->set_shader_parameter("WaveAmplitudes", _floats({ 0.85, 0.85, 0.85, 0.85, 0.85, 0.85, 0.85, 0.85 }));
	default_material->set_shader_parameter("WaveDirectionsDegrees", _floats({ 0, 18, 345, 40, 312, 68, 288, 100 }));
	default_material->set_shader_parameter("WaveFrequencies", _floats({ 0.007143, 0.0125, 0.022222, 0.038462, 0.066667, 0.111111, 0.181818, 0.294118 }));
	default_material->set_shader_parameter("WaveSpeeds", _floats({ 0.664, 0.878, 1.171, 1.540, 2.028, 2.618, 3.349, 4.259 }));
	default_material->set_shader_parameter("WavePhases", _floats({ 0, 41, 137, 213, 79, 301, 167, 23 }));

	default_material->set_shader_parameter("UVWaveCount", 2);
	default_material->set_shader_parameter("UVWaveSteepnesses", _floats({ 0.09, 0.08 }));
	default_material->set_shader_parameter("UVWaveAmplitudes", _floats({ 0.7, 0.5 }));
	default_material->set_shader_parameter("UVWaveDirectionsDegrees", _floats({ 315, 90 }));
	default_material->set_shader_parameter("UVWaveFrequencies", _floats({ 0.9, 0.6 }));
	default_material->set_shader_parameter("UVWaveSpeeds", _floats({ 0.75, 0.375 }));
	default_material->set_shader_parameter("UVWavePhases", _floats({ 0, 0 }));
	return default_material;
}

void EdenPlanetOcean::_apply_material(QuadNode *p_node, const Ref<Material> &p_mat) {
	if (!p_node) {
		return;
	}
	if (p_node->mesh_instance) {
		p_node->mesh_instance->set_material_override(p_mat);
	}
	for (QuadNode *c : p_node->children) {
		_apply_material(c, p_mat);
	}
}

int EdenPlanetOcean::_count_leaves(const QuadNode *p_node) const {
	if (!p_node) {
		return 0;
	}
	if (p_node->is_leaf()) {
		return p_node->mesh_instance ? 1 : 0;
	}
	int n = 0;
	for (const QuadNode *c : p_node->children) {
		n += _count_leaves(c);
	}
	return n;
}

int EdenPlanetOcean::get_leaf_count() const {
	int n = 0;
	for (const QuadNode *root : roots) {
		n += _count_leaves(root);
	}
	return n;
}

void EdenPlanetOcean::set_planet_radius(float p_value) {
	planet_radius = MAX(p_value, 1.0f);
	_derive_mesh_density(); // the patch ladder moved, so the mesh that hits the target triangle did
	_clear_tree();
}

// Script-level override: the patch size is normally derived from lod0_triangle_size, so setting it
// by hand re-derives the grid and the triangle size to match rather than the other way round.
void EdenPlanetOcean::set_min_leaf_world_size(float p_value) {
	min_leaf_world_size = MAX(p_value, 1.0f);
	grid_resolution = CLAMP((int)Math::round(min_leaf_world_size / MAX(lod0_triangle_size, 0.01f) * 0.5f) * 2, 2, 128);
	lod0_triangle_size = min_leaf_world_size / (float)grid_resolution;
	_clear_tree();
}

// Patch size and cell count that together put a LOD0 triangle closest to lod0_triangle_size.
// The patch is a power-of-two division of the planet, so it is chosen first (nearest to
// NOMINAL_CELLS triangles across), and the cell count then divides it -- even, because the geomorph
// needs the parent's grid to land on this one's vertices.
void EdenPlanetOcean::_derive_mesh_density() {
	const float t = MAX(lod0_triangle_size, 0.01f);
	const float want = t * (float)NOMINAL_CELLS;
	float leaf = 2.0f * planet_radius;
	for (int d = 0; d < MAX_DEPTH && leaf > want * 1.5f; d++) {
		leaf *= 0.5f;
	}
	min_leaf_world_size = leaf;
	grid_resolution = CLAMP((int)Math::round(leaf / t * 0.5f) * 2, 2, 128);
	lod0_triangle_size = leaf / (float)grid_resolution;
}

void EdenPlanetOcean::set_lod0_triangle_size(float p_value) {
	lod0_triangle_size = MAX(p_value, 0.01f);
	_derive_mesh_density();
	_clear_tree();
}

void EdenPlanetOcean::set_skirt_depth_ratio(float p_value) {
	skirt_depth_ratio = p_value;
	_clear_tree();
}

// Kept for scripts that want the grid directly; lod0_triangle_size follows so the two never
// disagree about what the mesh is.
void EdenPlanetOcean::set_grid_resolution(int p_value) {
	grid_resolution = CLAMP(p_value, 2, 128) & ~1; // even: the parent grid must land on our vertices
	lod0_triangle_size = min_leaf_world_size / (float)grid_resolution;
	_clear_tree();
}

void EdenPlanetOcean::set_material(const Ref<Material> &p_material) {
	// Never empty in the tree: clearing the slot in the inspector resets to a fresh built-in material.
	material = (p_material.is_null() && is_inside_tree()) ? Ref<Material>(make_default_material()) : p_material;
	for (QuadNode *root : roots) {
		_apply_material(root, material);
	}
}

// Standard cube map face order (+X, -X, +Y, -Y, +Z, -Z) at texel centres, image y down. Same table
// as EdenCloudShell::_cube_texel_dir, which is verified against the renderer.
static Vector3 _cube_texel_dir(int p_face, int p_x, int p_y, int p_size) {
	const float s = 2.0f * ((float)p_x + 0.5f) / (float)p_size - 1.0f;
	const float t = 2.0f * ((float)p_y + 0.5f) / (float)p_size - 1.0f;
	switch (p_face) {
		case 0:
			return Vector3(1.0f, -t, -s).normalized();
		case 1:
			return Vector3(-1.0f, -t, s).normalized();
		case 2:
			return Vector3(s, 1.0f, t).normalized();
		case 3:
			return Vector3(s, -1.0f, -t).normalized();
		case 4:
			return Vector3(s, -t, 1.0f).normalized();
		default:
			return Vector3(-s, -t, -1.0f).normalized();
	}
}

// Flow = cross(up, grad psi): tangent to the sphere and divergence-free, so it circulates in gyres
// around the stream function's highs and lows instead of piling up or draining anywhere. psi is
// domain-warped fBm (swirly, uneven gyres) plus sin(3 * latitude) bands (alternating east/west
// "trade" belts).
// The stream function psi. Flow is d x grad(psi), so flow runs ALONG contours of psi and is
// divergence-free for any psi whatever -- water circulates in closed gyres instead of piling up.
// Two consequences used below:
//   - a psi that varies only with latitude gives purely east-west flow (the wind bands),
//   - a psi held CONSTANT over a region makes that region's boundary a contour, i.e. a streamline:
//     flow runs exactly along the coast and never crosses it, with zero flow inland. That is the
//     textbook no-normal-flow boundary condition, and it costs one mix().
float EdenPlanetOcean::_current_psi(const Vector3 &p_dir, FastNoiseLite *p_noise) const {
	const Vector3 p = p_dir * current_frequency;
	float psi = p_noise->get_noise_3d(p.x, p.y, p.z);

	// Three-cell bands, matched to EdenCloudShell's wind so surface currents run with the winds
	// that drive them. Its east wind is -sin(6*|lat|); for a latitude-only psi, d x grad(psi)
	// works out to flow_east = -dpsi/dlat, so psi = -cos(6*|lat|)/6 reproduces exactly that.
	// (The old sin(3*lat) was antisymmetric about the equator, which ran the northern and southern
	// belts in opposite directions -- real trade winds are easterly in both hemispheres.)
	Vector3 axis = current_wind_axis.normalized();
	if (axis.length_squared() < 0.5f) {
		axis = Vector3(0, 1, 0);
	}
	const float abs_lat = Math::asin(Math::abs(CLAMP(p_dir.dot(axis), -1.0f, 1.0f)));
	psi += current_zonal * -Math::cos(6.0f * abs_lat) / 6.0f;

	// Land: blend psi toward a constant so coastlines become streamlines and continents bound the
	// gyres. The constant is 0 (the field's own mean), which keeps land near the open-ocean value
	// and so keeps the coastal gradient -- and the along-shore current it implies -- modest.
	// ponytail: one constant for ALL landmasses. Strictly each disconnected one wants its own
	// (Godfrey's island rule); indistinguishable by eye, revisit only if island wakes look wrong.
	if (current_land_image.is_valid() && !current_land_image->is_empty() && current_land_coast_band > 0.0f) {
		const int ch = CLAMP(current_land_channel, 0, 3);
		const float height = _sample_equirect_channel(current_land_image, p_dir, ch);
		const float landness = CLAMP((height - current_land_sea_level) / current_land_coast_band, 0.0f, 1.0f);
		psi *= 1.0f - landness * landness * (3.0f - 2.0f * landness); // smoothstep toward psi = 0
	}
	return psi;
}

void EdenPlanetOcean::bake_currents() {
	current_dirty = false;
	if (!current_enabled) {
		current_map.unref();
		current_flow.clear();
		return;
	}
	Ref<FastNoiseLite> noise;
	noise.instantiate();
	noise->set_seed(current_seed);
	noise->set_noise_type(FastNoiseLite::TYPE_SIMPLEX_SMOOTH);
	noise->set_frequency(1.0f);
	noise->set_fractal_type(FastNoiseLite::FRACTAL_FBM);
	noise->set_fractal_octaves(3);
	noise->set_domain_warp_enabled(current_warp > 0.0f);
	noise->set_domain_warp_type(FastNoiseLite::DOMAIN_WARP_SIMPLEX);
	noise->set_domain_warp_amplitude(current_warp);
	noise->set_domain_warp_frequency(1.0f);

	auto psi = [&](const Vector3 &d) -> float {
		return _current_psi(d, noise.ptr());
	};

	const int n = current_resolution;
	const float eps = 0.5f / n;
	LocalVector<Vector3> flow;
	flow.resize(6 * n * n);
	float max_len = 1e-6f;
	for (int f = 0; f < 6; f++) {
		for (int y = 0; y < n; y++) {
			for (int x = 0; x < n; x++) {
				const Vector3 d = _cube_texel_dir(f, x, y, n);
				const Vector3 t1 = (Math::abs(d.y) < 0.99f ? Vector3(0, 1, 0) : Vector3(1, 0, 0)).cross(d).normalized();
				const Vector3 t2 = d.cross(t1);
				const Vector3 grad = t1 * (psi((d + t1 * eps).normalized()) - psi((d - t1 * eps).normalized())) +
						t2 * (psi((d + t2 * eps).normalized()) - psi((d - t2 * eps).normalized()));
				const Vector3 v = d.cross(grad) / (2.0f * eps);
				flow[(f * n + y) * n + x] = v;
				max_len = MAX(max_len, v.length());
			}
		}
	}

	Vector<Ref<Image>> faces;
	for (int f = 0; f < 6; f++) {
		Ref<Image> img = Image::create_empty(n, n, false, Image::FORMAT_RGBA8);
		for (int y = 0; y < n; y++) {
			for (int x = 0; x < n; x++) {
				const Vector3 v = flow[(f * n + y) * n + x];
				const float len = v.length();
				const Vector3 dir = len > 1e-9f ? v / len : Vector3();
				// sqrt: spreads the strength range so only true stagnation points read as calm.
				img->set_pixel(x, y, Color(dir.x * 0.5f + 0.5f, dir.y * 0.5f + 0.5f, dir.z * 0.5f + 0.5f, Math::sqrt(len / max_len)));
			}
		}
		faces.push_back(img);
	}
	current_map.instantiate();
	current_map->create_from_images(faces);

	current_flow.resize(flow.size());
	for (uint32_t i = 0; i < flow.size(); i++) {
		const float len = flow[i].length();
		current_flow[i] = len > 1e-9f ? flow[i] / len * Math::sqrt(len / max_len) : Vector3();
	}
}

// Inverse of _cube_texel_dir: major axis picks the face, the other two components give (s, t).
Vector3 EdenPlanetOcean::get_current_at(const Vector3 &p_world_position) const {
	const int n = current_resolution;
	if (current_flow.size() != (uint32_t)(6 * n * n)) {
		return Vector3();
	}
	const Vector3 d = (to_local(p_world_position)).normalized();
	const Vector3 a = d.abs();
	int face;
	float s;
	float t;
	if (a.x >= a.y && a.x >= a.z) {
		face = d.x > 0.0f ? 0 : 1;
		s = (d.x > 0.0f ? -d.z : d.z) / a.x;
		t = -d.y / a.x;
	} else if (a.y >= a.z) {
		face = d.y > 0.0f ? 2 : 3;
		s = d.x / a.y;
		t = (d.y > 0.0f ? d.z : -d.z) / a.y;
	} else {
		face = d.z > 0.0f ? 4 : 5;
		s = (d.z > 0.0f ? d.x : -d.x) / a.z;
		t = -d.y / a.z;
	}
	const int x = CLAMP((int)((s + 1.0f) * 0.5f * n), 0, n - 1);
	const int y = CLAMP((int)((t + 1.0f) * 0.5f * n), 0, n - 1);
	return get_global_basis().xform(current_flow[(face * n + y) * n + x]);
}

void EdenPlanetOcean::set_current_enabled(bool p_value) {
	current_enabled = p_value;
	current_dirty = true;
}

void EdenPlanetOcean::set_current_seed(int p_value) {
	current_seed = p_value;
	current_dirty = true;
}

void EdenPlanetOcean::set_current_frequency(float p_value) {
	current_frequency = MAX(p_value, 0.01f);
	current_dirty = true;
}

void EdenPlanetOcean::set_current_warp(float p_value) {
	current_warp = MAX(p_value, 0.0f);
	current_dirty = true;
}

void EdenPlanetOcean::set_current_zonal(float p_value) {
	current_zonal = p_value;
	current_dirty = true;
}

void EdenPlanetOcean::set_current_resolution(int p_value) {
	current_resolution = CLAMP(p_value, 8, 512);
	current_dirty = true;
}

void EdenPlanetOcean::set_current_wind_axis(const Vector3 &p_value) {
	current_wind_axis = p_value;
	current_dirty = true;
}

void EdenPlanetOcean::set_current_land_image(const Ref<Image> &p_value) {
	current_land_image = p_value;
	current_dirty = true;
}

void EdenPlanetOcean::set_current_land_channel(int p_value) {
	current_land_channel = CLAMP(p_value, 0, 3);
	current_dirty = true;
}

void EdenPlanetOcean::set_current_land_sea_level(float p_value) {
	current_land_sea_level = p_value;
	current_dirty = true;
}

void EdenPlanetOcean::set_current_land_coast_band(float p_value) {
	// Zero would be a step discontinuity in psi, i.e. an infinite coastal jet.
	current_land_coast_band = MAX(p_value, 0.001f);
	current_dirty = true;
}

void EdenPlanetOcean::set_screen_space_reflections(bool p_value) {
	screen_space_reflections = p_value;
	_upgrade_builtin_shader();
}

void EdenPlanetOcean::set_lighting_mode(LightingMode p_mode) {
	lighting_mode = p_mode;
	_upgrade_builtin_shader();
}

void EdenPlanetOcean::set_split_distance_factor(float p_value) {
	split_distance_factor = p_value;
	_clear_tree(); // each patch's baked morph distance depends on it
}

// A scene saves the built-in material with an embedded copy of the shader, which would otherwise
// stay frozen at whatever version it was saved with. An embedded (non-file) shader that is this
// module's -- recognised by its first line -- is refreshed to the current code. A shader saved to
// its own .gdshader file is the user's and is never touched.
void EdenPlanetOcean::_upgrade_builtin_shader() {
	Ref<ShaderMaterial> sm = material;
	if (sm.is_null() || sm->get_shader().is_null()) {
		return;
	}
	Ref<Shader> sh = sm->get_shader();
	const String base = String::utf8(EDEN_OCEAN_SHADER_CODE).strip_edges();
	const String header = base.get_slice("\n", 0);
	if (header.is_empty() || sh->get_path().is_resource_file() || !sh->get_code().strip_edges().begins_with(header)) {
		return;
	}
	// Quality variants are preprocessor defines, placed after the header line so the check above
	// still recognises the shader as the built-in one.
	String code = base;
	String defines;
	if (lighting_mode == LIGHTING_FAST_SKY || lighting_mode == LIGHTING_FAST) {
		defines += "#define EDEN_OCEAN_FAST_SKY_LIGHT\n";
	}
	if (lighting_mode == LIGHTING_FAST) {
		defines += "#define EDEN_OCEAN_FAST_LIGHT\n";
	}
	if (screen_space_reflections) {
		defines += "#define EDEN_OCEAN_SSR\n";
	}
	if (!defines.is_empty()) {
		code = header + "\n" + defines + base.substr(header.length() + 1);
	}
	if (sh->get_code() != code) {
		sh->set_code(code);
		print_verbose("EdenPlanetOcean: refreshed the built-in ocean shader saved with this scene.");
	}
	sm->set_render_priority(-1);
}

// Candidate tracking. Every MeshInstance3D in this World3D except the ocean's own patches; kept
// current by the tree's node_added / node_removed signals, so nothing is rescanned per frame.
void EdenPlanetOcean::_track_ripple_node(Node *p_node) {
	MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(p_node);
	if (mi != nullptr && !is_ancestor_of(mi)) {
		ripple_candidates.insert(mi->get_instance_id());
	}
}

void EdenPlanetOcean::_untrack_ripple_node(Node *p_node) {
	ripple_candidates.erase(p_node->get_instance_id());
}

// Waterline foam, every frame, for the nearest 16 candidates within ripple_range of the camera whose
// bounds reach the water band (sea level +-15 m; wave crests never get further), skipping anything
// bigger than ripple_max_object_size (terrain -- the depth-based shore foam covers coastlines) or
// denser than ripple_max_triangles, and anything with an "ocean_ripple_ignore" meta.
//
// Each is sliced at RIPPLE_SLICES heights across its vertical extent, in a frame tangent to the
// planet at the object: triangle/plane intersections give the outline a water surface at that height
// would make. The shader measures the true 2D distance to the slices bracketing each water pixel's
// own (wave-displaced) height. Segments go into a small float texture, through the RenderingServer so
// nothing is saved with the scene.
void EdenPlanetOcean::_update_ripples(double p_delta) {
	Ref<ShaderMaterial> sm = material;
	if (sm.is_null() || !is_inside_tree()) {
		return;
	}
	const int MAX_OBJECTS = 16;
	const int SLICES = 10; // RIPPLE_SLICES in the shader
	const int MAX_SEGMENTS = 24; // per slice; texel 0 of each row holds the count
	const float WAVE_BAND = 15.0f;
	const Vector3 center = get_global_position();
	const Vector3 cam = _camera_position();
	Ref<World3D> world = get_world_3d();

	struct Cand {
		float dist;
		MeshInstance3D *mi;
	};
	LocalVector<Cand> cands;
	LocalVector<ObjectID> dead;
	for (const ObjectID &id : ripple_candidates) {
		MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(ObjectDB::get_instance(id));
		if (mi == nullptr) {
			dead.push_back(id);
			continue;
		}
		if (!mi->is_inside_tree() || !mi->is_visible_in_tree() || mi->get_mesh().is_null() || mi->get_world_3d() != world || mi->has_meta("ocean_ripple_ignore")) {
			continue;
		}
		const AABB wb = mi->get_global_transform().xform(mi->get_aabb());
		if (wb.get_longest_axis_size() > ripple_max_object_size) {
			continue;
		}
		const Vector3 c = wb.get_center();
		const float dist = c.distance_to(cam);
		if (dist > ripple_range) {
			continue;
		}
		if (Math::abs(c.distance_to(center) - planet_radius) > wb.size.length() * 0.5f + WAVE_BAND) {
			continue;
		}
		cands.push_back({ dist, mi });
	}
	for (const ObjectID &id : dead) {
		ripple_candidates.erase(id);
	}
	struct ByDistance {
		bool operator()(const Cand &a, const Cand &b) const { return a.dist < b.dist; }
	};
	cands.sort_custom<ByDistance>();

	if (ripple_image.is_null()) {
		ripple_image = Image::create_empty(MAX_SEGMENTS + 1, MAX_OBJECTS * SLICES, false, Image::FORMAT_RGBAF);
	}
	PackedVector4Array origins, ups, easts;
	PackedFloat32Array zmaxs;
	origins.resize(MAX_OBJECTS);
	ups.resize(MAX_OBJECTS);
	easts.resize(MAX_OBJECTS);
	zmaxs.resize(MAX_OBJECTS);
	int count = 0;
	LocalVector<Vector3> verts;
	LocalVector<Vector4> segs;
	for (const Cand &cand : cands) {
		if (count >= MAX_OBJECTS) {
			break;
		}
		MeshInstance3D *mi = cand.mi;
		Ref<Mesh> mesh = mi->get_mesh();
		const RID mesh_rid = mesh->get_rid();
		if (!ripple_face_cache.has(mesh_rid)) {
			ripple_face_cache[mesh_rid] = mesh->get_faces();
		}
		const Vector<Face3> &faces = ripple_face_cache[mesh_rid];
		if (faces.is_empty() || faces.size() > ripple_max_triangles) {
			continue;
		}

		// Frame tangent to the planet at the object's bounds centre.
		const Transform3D xf = mi->get_global_transform();
		const Vector3 o = xf.xform(mi->get_aabb().get_center());
		const Vector3 up = (o - center).normalized();
		const Vector3 ref = Math::abs(up.y) < 0.9f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
		const Vector3 east = ref.cross(up).normalized();
		const Vector3 north = up.cross(east);

		verts.resize(faces.size() * 3);
		float zmin = 1e30f, zmax = -1e30f, bound = 0.0f;
		for (int f = 0; f < faces.size(); f++) {
			for (int k = 0; k < 3; k++) {
				const Vector3 p = xf.xform(faces[f].vertex[k]) - o;
				const Vector3 q(p.dot(east), p.dot(north), p.dot(up));
				verts[f * 3 + k] = q;
				zmin = MIN(zmin, q.z);
				zmax = MAX(zmax, q.z);
				bound = MAX(bound, Vector2(q.x, q.y).length());
			}
		}
		// Slices strictly inside [zmin, zmax] (at the very bottom/top a plane only grazes a vertex or a
		// flat face), evenly spaced; the shader indexes them from the first and last heights.
		const float span = MAX(zmax - zmin, 1e-3f);
		const float z_first = zmin + span * 0.5f / SLICES;
		const float z_last = zmax - span * 0.5f / SLICES;
		for (int sl = 0; sl < SLICES; sl++) {
			const float z = z_first + (z_last - z_first) * sl / (SLICES - 1);
			segs.clear();
			for (int f = 0; f < faces.size(); f++) {
				const Vector3 *v = &verts[f * 3];
				Vector2 pts[2];
				int n = 0;
				for (int e = 0; e < 3 && n < 2; e++) {
					const Vector3 &a = v[e];
					const Vector3 &b = v[(e + 1) % 3];
					const float da = a.z - z;
					const float db = b.z - z;
					if ((da > 0.0f) != (db > 0.0f)) {
						const float t = da / (da - db);
						pts[n++] = Vector2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
					}
				}
				if (n == 2) {
					segs.push_back(Vector4(pts[0].x, pts[0].y, pts[1].x, pts[1].y));
				}
			}
			// Too many segments (a dense mesh): keep an even spread. Neighbouring pieces of a curve
			// are short, so the gaps change the distance by at most about half a segment.
			const int n_out = MIN((int)segs.size(), MAX_SEGMENTS);
			const int row = count * SLICES + sl;
			// Outline size: half the diagonal of its bounding box in the frame.
			Vector2 lo(1e30f, 1e30f), hi(-1e30f, -1e30f);
			for (const Vector4 &sg : segs) {
				lo = lo.min(Vector2(MIN(sg.x, sg.z), MIN(sg.y, sg.w)));
				hi = hi.max(Vector2(MAX(sg.x, sg.z), MAX(sg.y, sg.w)));
			}
			const float outline_radius = segs.is_empty() ? 0.0f : (hi - lo).length() * 0.5f;
			ripple_image->set_pixel(0, row, Color(n_out, outline_radius, 0, 0));
			for (int k = 0; k < n_out; k++) {
				const Vector4 sg = segs[(int)((int64_t)k * segs.size() / n_out)];
				ripple_image->set_pixel(k + 1, row, Color(sg.x, sg.y, sg.z, sg.w));
			}
		}
		origins.set(count, Vector4(o.x, o.y, o.z, 1.0f));
		ups.set(count, Vector4(up.x, up.y, up.z, bound));
		easts.set(count, Vector4(east.x, east.y, east.z, z_first));
		zmaxs.set(count, z_last);
		count++;
	}

	RenderingServer *rs = RenderingServer::get_singleton();
	const RID rid = sm->get_rid();
	rs->material_set_param(rid, "ripple_count", count);
	if (count > 0) {
		if (ripple_texture.is_null()) {
			ripple_texture = ImageTexture::create_from_image(ripple_image);
		} else {
			ripple_texture->update(ripple_image);
		}
		rs->material_set_param(rid, "ripple_segments", ripple_texture->get_rid());
		rs->material_set_param(rid, "ripple_origin", origins);
		rs->material_set_param(rid, "ripple_up", ups);
		rs->material_set_param(rid, "ripple_east", easts);
		rs->material_set_param(rid, "ripple_zmax", zmaxs);
	}
}

// LIGHTING_FAST lights the water itself, so it needs the scene's directional lights: the two
// brightest visible ones, in the form Godot builds them (linear colour * energy * PI, or * intensity
// with physical light units). The scene is rescanned every 2 s (lights come and go rarely); their
// direction/colour/visibility are read every frame.
void EdenPlanetOcean::_update_fast_lights(double p_delta) {
	Ref<ShaderMaterial> sm = material;
	if (lighting_mode != LIGHTING_FAST || sm.is_null() || !is_inside_tree()) {
		return;
	}
	fast_light_rescan -= p_delta;
	if (fast_light_rescan <= 0.0) {
		fast_light_rescan = 2.0;
		fast_lights.clear();
		// owned = false: the atmosphere's auto-created sun/moon are internal, unowned children.
		TypedArray<Node> found = get_tree()->get_root()->find_children("*", "DirectionalLight3D", true, false);
		for (int i = 0; i < found.size(); i++) {
			fast_lights.push_back(Object::cast_to<Node>(found[i])->get_instance_id());
		}
	}
	const bool physical = GLOBAL_GET("rendering/lights_and_shadows/use_physical_light_units");
	struct Lit {
		float energy;
		Vector3 dir;
		Vector3 col;
		float spec;
		float size;
	};
	LocalVector<Lit> lit;
	// Godot widens directional highlights by the light's angular size, but only while soft directional shadows are on
	// for the frame (any visible directional light with a shadow, a shadow blur and a size: LightStorage's
	// update_light_buffers). Mirrored so Fast shades a sized sun's glint like Fast Sky.
	bool soft_shadows = false;
	for (const ObjectID &id : fast_lights) {
		DirectionalLight3D *l = Object::cast_to<DirectionalLight3D>(ObjectDB::get_instance(id));
		if (l == nullptr || !l->is_visible_in_tree()) {
			continue;
		}
		if (l->has_shadow() && l->get_param(Light3D::PARAM_SHADOW_BLUR) > 0.0f && l->get_param(Light3D::PARAM_SIZE) > 0.0f) {
			soft_shadows = true;
		}
		if (l->is_negative()) {
			continue;
		}
		float energy = l->get_param(Light3D::PARAM_ENERGY) * (physical ? l->get_param(Light3D::PARAM_INTENSITY) : (float)Math::PI);
		if (energy <= 0.0f) {
			continue;
		}
		const Color c = l->get_color().srgb_to_linear();
		// Godot's DirectionalLightData::size: angle to cosine offset
		const float size = 1.0f - Math::cos(Math::deg_to_rad(l->get_param(Light3D::PARAM_SIZE)));
		lit.push_back({ energy, l->get_global_basis().get_column(2).normalized(), Vector3(c.r, c.g, c.b) * energy,
				l->get_param(Light3D::PARAM_SPECULAR), size });
	}
	struct ByEnergy {
		bool operator()(const Lit &a, const Lit &b) const { return a.energy > b.energy; }
	};
	lit.sort_custom<ByEnergy>();
	const int count = MIN((int)lit.size(), 2);
	PackedVector3Array dirs;
	PackedVector3Array cols;
	PackedFloat32Array specs;
	PackedFloat32Array sizes;
	dirs.resize(2);
	cols.resize(2);
	specs.resize(2);
	sizes.resize(2);
	for (int i = 0; i < count; i++) {
		dirs.set(i, lit[i].dir);
		cols.set(i, lit[i].col);
		specs.set(i, lit[i].spec);
		sizes.set(i, soft_shadows ? lit[i].size : 0.0f);
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	rs->material_set_param(sm->get_rid(), "fast_light_count", count);
	rs->material_set_param(sm->get_rid(), "fast_light_direction", dirs);
	rs->material_set_param(sm->get_rid(), "fast_light_color", cols);
	rs->material_set_param(sm->get_rid(), "fast_light_specular", specs);
	rs->material_set_param(sm->get_rid(), "fast_light_size", sizes);
	// Godot's screen-space roughness limiter, which Fast (unshaded) applies itself; see the shader
	const bool limiter = GLOBAL_GET("rendering/anti_aliasing/screen_space_roughness_limiter/enabled");
	rs->material_set_param(sm->get_rid(), "fast_roughness_limiter_amount",
			limiter ? (float)GLOBAL_GET("rendering/anti_aliasing/screen_space_roughness_limiter/amount") : 0.0f);
	rs->material_set_param(sm->get_rid(), "fast_roughness_limiter_limit",
			(float)GLOBAL_GET("rendering/anti_aliasing/screen_space_roughness_limiter/limit"));
}

void EdenPlanetOcean::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			// Created here, not in the constructor: ClassDB keeps a constructed instance for property
			// defaults, and a material made there outlives the RenderingServer (leaks at exit).
			if (material.is_null()) {
				material = make_default_material();
			}
			_upgrade_builtin_shader();
			// Waterline-foam candidates: one scan now, then kept current by the tree's signals.
			ripple_candidates.clear();
			{
				TypedArray<Node> found = get_tree()->get_root()->find_children("*", "MeshInstance3D", true, false);
				for (int i = 0; i < found.size(); i++) {
					_track_ripple_node(Object::cast_to<Node>(found[i]));
				}
			}
			get_tree()->connect("node_added", callable_mp(this, &EdenPlanetOcean::_track_ripple_node));
			get_tree()->connect("node_removed", callable_mp(this, &EdenPlanetOcean::_untrack_ripple_node));
		} break;
		case NOTIFICATION_EXIT_TREE: {
			get_tree()->disconnect("node_added", callable_mp(this, &EdenPlanetOcean::_track_ripple_node));
			get_tree()->disconnect("node_removed", callable_mp(this, &EdenPlanetOcean::_untrack_ripple_node));
			ripple_candidates.clear();
		} break;
		case NOTIFICATION_READY: {
			set_process(true);
			update_lod();
		} break;
		case NOTIFICATION_PROCESS: {
			_update_ripples(get_process_delta_time());
			_update_fast_lights(get_process_delta_time());
			rebuild_accum += get_process_delta_time();
			// A teleport (a --capture pose step, a respawn, a cut to another camera) moves the
			// viewpoint further in one frame than walking does in many, and waiting out
			// rebuild_interval leaves it sitting over water still subdivided for where it used to
			// be -- i.e. no patches at all nearby, and the sky's analytic ground visible straight
			// through where the sea should be. Cheap to detect, and the rebuild is the same one the
			// timer would have done a moment later.
			bool jumped = false;
			{
				LocalVector<Vector3> cams;
				_camera_positions(cams);
				if (cams.size() != last_lod_cams.size()) {
					jumped = true;
				} else {
					for (uint32_t i = 0; i < cams.size(); i++) {
						// A teleport, not a walk: move less than this and the existing tree still
						// covers the viewpoint at roughly the right depth, so the timer can handle it.
						// Floored well above a leaf, which is now metres wide -- scaled to the leaf it
						// made walking pace force a full rebuild every few steps.
						const float jump = MAX(min_leaf_world_size, 50.0f);
						if (cams[i].distance_squared_to(last_lod_cams[i]) > jump * jump) {
							jumped = true;
							break;
						}
					}
				}
			}
			if (jumped || rebuild_accum >= rebuild_interval) {
				rebuild_accum = 0.0;
				update_lod();
			}
		} break;
	}
}

void EdenPlanetOcean::_bind_methods() {
	ClassDB::bind_method(D_METHOD("update_lod"), &EdenPlanetOcean::update_lod);
	ClassDB::bind_method(D_METHOD("get_leaf_count"), &EdenPlanetOcean::get_leaf_count);
	ClassDB::bind_static_method("EdenPlanetOcean", D_METHOD("make_default_material"), &EdenPlanetOcean::make_default_material);

	ClassDB::bind_method(D_METHOD("set_planet_radius", "value"), &EdenPlanetOcean::set_planet_radius);
	ClassDB::bind_method(D_METHOD("get_planet_radius"), &EdenPlanetOcean::get_planet_radius);
	ClassDB::bind_method(D_METHOD("set_min_leaf_world_size", "value"), &EdenPlanetOcean::set_min_leaf_world_size);
	ClassDB::bind_method(D_METHOD("get_min_leaf_world_size"), &EdenPlanetOcean::get_min_leaf_world_size);
	ClassDB::bind_method(D_METHOD("set_split_distance_factor", "value"), &EdenPlanetOcean::set_split_distance_factor);
	ClassDB::bind_method(D_METHOD("get_split_distance_factor"), &EdenPlanetOcean::get_split_distance_factor);
	ClassDB::bind_method(D_METHOD("set_near_field_radius", "value"), &EdenPlanetOcean::set_near_field_radius);
	ClassDB::bind_method(D_METHOD("get_near_field_radius"), &EdenPlanetOcean::get_near_field_radius);
	ClassDB::bind_method(D_METHOD("set_rebuild_interval", "value"), &EdenPlanetOcean::set_rebuild_interval);
	ClassDB::bind_method(D_METHOD("get_rebuild_interval"), &EdenPlanetOcean::get_rebuild_interval);
	ClassDB::bind_method(D_METHOD("set_skirt_depth_ratio", "value"), &EdenPlanetOcean::set_skirt_depth_ratio);
	ClassDB::bind_method(D_METHOD("get_skirt_depth_ratio"), &EdenPlanetOcean::get_skirt_depth_ratio);
	ClassDB::bind_method(D_METHOD("set_grid_resolution", "value"), &EdenPlanetOcean::set_grid_resolution);
	ClassDB::bind_method(D_METHOD("get_grid_resolution"), &EdenPlanetOcean::get_grid_resolution);
	ClassDB::bind_method(D_METHOD("set_lod0_triangle_size", "value"), &EdenPlanetOcean::set_lod0_triangle_size);
	ClassDB::bind_method(D_METHOD("get_lod0_triangle_size"), &EdenPlanetOcean::get_lod0_triangle_size);
	ClassDB::bind_method(D_METHOD("set_screen_space_reflections", "value"), &EdenPlanetOcean::set_screen_space_reflections);
	ClassDB::bind_method(D_METHOD("get_screen_space_reflections"), &EdenPlanetOcean::get_screen_space_reflections);
	ClassDB::bind_method(D_METHOD("set_lighting_mode", "mode"), &EdenPlanetOcean::set_lighting_mode);
	ClassDB::bind_method(D_METHOD("get_lighting_mode"), &EdenPlanetOcean::get_lighting_mode);
	ClassDB::bind_method(D_METHOD("set_ripple_range", "value"), &EdenPlanetOcean::set_ripple_range);
	ClassDB::bind_method(D_METHOD("get_ripple_range"), &EdenPlanetOcean::get_ripple_range);
	ClassDB::bind_method(D_METHOD("set_ripple_max_object_size", "value"), &EdenPlanetOcean::set_ripple_max_object_size);
	ClassDB::bind_method(D_METHOD("get_ripple_max_object_size"), &EdenPlanetOcean::get_ripple_max_object_size);
	ClassDB::bind_method(D_METHOD("set_ripple_max_triangles", "value"), &EdenPlanetOcean::set_ripple_max_triangles);
	ClassDB::bind_method(D_METHOD("get_ripple_max_triangles"), &EdenPlanetOcean::get_ripple_max_triangles);
	BIND_ENUM_CONSTANT(LIGHTING_GODOT);
	BIND_ENUM_CONSTANT(LIGHTING_FAST_SKY);
	BIND_ENUM_CONSTANT(LIGHTING_FAST);
	ClassDB::bind_method(D_METHOD("set_horizon_culling", "value"), &EdenPlanetOcean::set_horizon_culling);
	ClassDB::bind_method(D_METHOD("get_horizon_culling"), &EdenPlanetOcean::get_horizon_culling);
	ClassDB::bind_method(D_METHOD("bake_currents"), &EdenPlanetOcean::bake_currents);
	ClassDB::bind_method(D_METHOD("get_current_map"), &EdenPlanetOcean::get_current_map);
	ClassDB::bind_method(D_METHOD("get_current_at", "world_position"), &EdenPlanetOcean::get_current_at);
	ClassDB::bind_method(D_METHOD("set_current_enabled", "value"), &EdenPlanetOcean::set_current_enabled);
	ClassDB::bind_method(D_METHOD("get_current_enabled"), &EdenPlanetOcean::get_current_enabled);
	ClassDB::bind_method(D_METHOD("set_current_seed", "value"), &EdenPlanetOcean::set_current_seed);
	ClassDB::bind_method(D_METHOD("get_current_seed"), &EdenPlanetOcean::get_current_seed);
	ClassDB::bind_method(D_METHOD("set_current_frequency", "value"), &EdenPlanetOcean::set_current_frequency);
	ClassDB::bind_method(D_METHOD("get_current_frequency"), &EdenPlanetOcean::get_current_frequency);
	ClassDB::bind_method(D_METHOD("set_current_warp", "value"), &EdenPlanetOcean::set_current_warp);
	ClassDB::bind_method(D_METHOD("get_current_warp"), &EdenPlanetOcean::get_current_warp);
	ClassDB::bind_method(D_METHOD("set_current_zonal", "value"), &EdenPlanetOcean::set_current_zonal);
	ClassDB::bind_method(D_METHOD("get_current_zonal"), &EdenPlanetOcean::get_current_zonal);
	ClassDB::bind_method(D_METHOD("set_current_resolution", "value"), &EdenPlanetOcean::set_current_resolution);
	ClassDB::bind_method(D_METHOD("get_current_resolution"), &EdenPlanetOcean::get_current_resolution);
	ClassDB::bind_method(D_METHOD("set_current_wind_axis", "value"), &EdenPlanetOcean::set_current_wind_axis);
	ClassDB::bind_method(D_METHOD("get_current_wind_axis"), &EdenPlanetOcean::get_current_wind_axis);
	ClassDB::bind_method(D_METHOD("set_current_land_image", "value"), &EdenPlanetOcean::set_current_land_image);
	ClassDB::bind_method(D_METHOD("get_current_land_image"), &EdenPlanetOcean::get_current_land_image);
	ClassDB::bind_method(D_METHOD("set_current_land_channel", "value"), &EdenPlanetOcean::set_current_land_channel);
	ClassDB::bind_method(D_METHOD("get_current_land_channel"), &EdenPlanetOcean::get_current_land_channel);
	ClassDB::bind_method(D_METHOD("set_current_land_sea_level", "value"), &EdenPlanetOcean::set_current_land_sea_level);
	ClassDB::bind_method(D_METHOD("get_current_land_sea_level"), &EdenPlanetOcean::get_current_land_sea_level);
	ClassDB::bind_method(D_METHOD("set_current_land_coast_band", "value"), &EdenPlanetOcean::set_current_land_coast_band);
	ClassDB::bind_method(D_METHOD("get_current_land_coast_band"), &EdenPlanetOcean::get_current_land_coast_band);
	ClassDB::bind_method(D_METHOD("set_material", "material"), &EdenPlanetOcean::set_material);
	ClassDB::bind_method(D_METHOD("get_material"), &EdenPlanetOcean::get_material);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "planet_radius", PROPERTY_HINT_RANGE, "1,10000000,1,or_greater,suffix:m"), "set_planet_radius", "get_planet_radius");
	ADD_GROUP("LOD", "");
	// Read-only like grid_resolution: both are derived from lod0_triangle_size, which is registered
	// after them so a scene load applies them in that order and the derived values win.
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_leaf_world_size", PROPERTY_HINT_RANGE, "1,10000,1,suffix:m",
						 PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_READ_ONLY),
			"set_min_leaf_world_size", "get_min_leaf_world_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "split_distance_factor", PROPERTY_HINT_RANGE, "0.5,8,0.05"), "set_split_distance_factor", "get_split_distance_factor");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "grid_resolution", PROPERTY_HINT_RANGE, "1,128,1",
						 PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_READ_ONLY),
			"set_grid_resolution", "get_grid_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod0_triangle_size", PROPERTY_HINT_RANGE, "0.05,32,0.01,or_greater,suffix:m"), "set_lod0_triangle_size", "get_lod0_triangle_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "skirt_depth_ratio", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_skirt_depth_ratio", "get_skirt_depth_ratio");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "near_field_radius", PROPERTY_HINT_RANGE, "0,100000,1,suffix:m"), "set_near_field_radius", "get_near_field_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rebuild_interval", PROPERTY_HINT_RANGE, "0,5,0.01,suffix:s"), "set_rebuild_interval", "get_rebuild_interval");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "horizon_culling"), "set_horizon_culling", "get_horizon_culling");
	ADD_GROUP("Currents", "current_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "current_enabled"), "set_current_enabled", "get_current_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "current_seed"), "set_current_seed", "get_current_seed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "current_frequency", PROPERTY_HINT_RANGE, "0.1,16,0.05"), "set_current_frequency", "get_current_frequency");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "current_warp", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_current_warp", "get_current_warp");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "current_zonal", PROPERTY_HINT_RANGE, "-2,2,0.01"), "set_current_zonal", "get_current_zonal");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "current_resolution", PROPERTY_HINT_RANGE, "8,512,1"), "set_current_resolution", "get_current_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "current_wind_axis"), "set_current_wind_axis", "get_current_wind_axis");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "current_land_image", PROPERTY_HINT_RESOURCE_TYPE, "Image"), "set_current_land_image", "get_current_land_image");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "current_land_channel", PROPERTY_HINT_RANGE, "0,3,1"), "set_current_land_channel", "get_current_land_channel");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "current_land_sea_level", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_current_land_sea_level", "get_current_land_sea_level");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "current_land_coast_band", PROPERTY_HINT_RANGE, "0.001,0.5,0.001"), "set_current_land_coast_band", "get_current_land_coast_band");
	ADD_GROUP("", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "lighting_mode", PROPERTY_HINT_ENUM, "Godot,Fast Sky,Fast"), "set_lighting_mode", "get_lighting_mode");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "screen_space_reflections"), "set_screen_space_reflections", "get_screen_space_reflections");
	ADD_GROUP("Waterline Foam", "ripple_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ripple_range", PROPERTY_HINT_RANGE, "0,5000,1,suffix:m"), "set_ripple_range", "get_ripple_range");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ripple_max_object_size", PROPERTY_HINT_RANGE, "0.1,1000,0.1,suffix:m"), "set_ripple_max_object_size", "get_ripple_max_object_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ripple_max_triangles", PROPERTY_HINT_RANGE, "12,1000000,1"), "set_ripple_max_triangles", "get_ripple_max_triangles");
	ADD_GROUP("", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "ShaderMaterial,StandardMaterial3D"), "set_material", "get_material");
}
