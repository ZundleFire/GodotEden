#include "eden_cloud_shell.h"

#include "eden_atmosphere_shaders.gen.h"

EdenCloudShell::EdenCloudShell() {
	set_process(true);
}

// ===========================================================================================
// Lifecycle
// ===========================================================================================
void EdenCloudShell::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			_build_material();
			_rebuild_mesh();
			rebuild_noise();
			_push_uniforms();
			set_process(true);
		} break;

		case NOTIFICATION_PROCESS: {
			// Uniforms are pushed every frame rather than on change. It is a few dozen writes
			// and it removes a whole class of bug where a property set from script without
			// going through the setter leaves the shader on stale values.
			_push_uniforms();
			_rebuild_mesh();
			rebuild_noise();
		} break;
	}
}

Ref<ShaderMaterial> EdenCloudShell::get_material() {
	if (material.is_null()) {
		_build_material();
	}
	return material;
}

void EdenCloudShell::set_cloud_shader_override(const Ref<Shader> &p_shader) {
	cloud_shader_override = p_shader;
	material.unref();
	_build_material();
	_rebuild_mesh();
	rebuild_noise();
}

Ref<Shader> EdenCloudShell::get_cloud_shader_override() const {
	return cloud_shader_override;
}

void EdenCloudShell::_build_material() {
	if (material.is_valid()) {
		return;
	}
	Ref<Shader> shader = cloud_shader_override;
	if (shader.is_null()) {
		// Built in, so a game needs nothing but the engine binary.
		shader.instantiate();
		shader->set_code(String::utf8(EDEN_CLOUD_SHADER_CODE));
	}
	material.instantiate();
	material->set_shader(shader);
}

void EdenCloudShell::_rebuild_mesh() {
	if (!is_inside_tree()) {
		return;
	}
	if (material.is_null()) {
		_build_material();
	}

	// The carrier mesh must reach the OUTER shell radius, not some inner midpoint: Godot only
	// runs the fragment shader where the mesh itself covers screen pixels, so from outside the
	// shell (i.e. from space) a smaller sphere's own silhouette horizon falls short of the
	// slab's true outer horizon and silently clips off the last sliver of visible cloud before
	// the analytic ray/slab math ever gets a chance to draw it. Sized at cloud_top, the mesh's
	// silhouette always matches (or exceeds) what the shader can actually light up.
	const float r_out = planet_radius + MAX(cloud_top, cloud_bottom + 1.0f);

	// Guarded so this can safely be called every frame: rebuilding a SphereMesh per frame would
	// be pointless churn, and keeping a "does this setter affect geometry" flag correct across
	// ~30 properties is worse.
	const String signature = vformat("%f|%d", r_out, mesh_segments);
	if (signature == mesh_signature && mesh_instance != nullptr) {
		if (mesh_instance->get_position() != planet_center) {
			mesh_instance->set_position(planet_center);
		}
		return;
	}
	mesh_signature = signature;

	if (mesh_instance == nullptr) {
		mesh_instance = memnew(MeshInstance3D);
		mesh_instance->set_name("CloudShellMesh");
		mesh_instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		// The shader raymarches from the camera, so Godot's frustum reasoning about this sphere
		// is unhelpful -- keep it drawn.
		mesh_instance->set_extra_cull_margin(16384.0f);
		add_child(mesh_instance, false, INTERNAL_MODE_BACK);
	}

	Ref<SphereMesh> sphere;
	sphere.instantiate();
	sphere->set_radius(r_out);
	sphere->set_height(r_out * 2.0f);
	sphere->set_radial_segments(MAX(mesh_segments, 8));
	sphere->set_rings(MAX(mesh_segments / 2, 4));

	mesh_instance->set_mesh(sphere);
	mesh_instance->set_material_override(material);
	mesh_instance->set_position(planet_center);
}

// ===========================================================================================
// Baked noise
// ===========================================================================================
void EdenCloudShell::rebuild_noise() {
	if (material.is_null()) {
		_build_material();
	}
	if (material.is_null()) {
		return;
	}

	const String signature = vformat("%d|%f|%d|%f|%f|%f|%d|%d", noise_resolution, noise_frequency,
			noise_octaves, noise_gain, noise_lacunarity, warp_amplitude, warp_octaves, noise_seed);
	if (signature == noise_signature && noise_texture.is_valid()) {
		return;
	}

	Ref<FastNoiseLite> fnl;
	fnl.instantiate();
	fnl->set_noise_type(FastNoiseLite::TYPE_SIMPLEX_SMOOTH);
	fnl->set_seed(noise_seed);
	fnl->set_frequency(noise_frequency);
	fnl->set_fractal_type(FastNoiseLite::FRACTAL_FBM);
	fnl->set_fractal_octaves(noise_octaves);
	fnl->set_fractal_gain(noise_gain);
	fnl->set_fractal_lacunarity(noise_lacunarity);
	if (warp_amplitude > 0.0f) {
		// Baked here rather than in the shader: it is the most expensive part of a procedural
		// cloud field and completely static, so paying once at startup is free quality.
		fnl->set_domain_warp_enabled(true);
		fnl->set_domain_warp_type(FastNoiseLite::DOMAIN_WARP_SIMPLEX);
		fnl->set_domain_warp_amplitude(warp_amplitude);
		fnl->set_domain_warp_fractal_type(FastNoiseLite::DOMAIN_WARP_FRACTAL_INDEPENDENT);
		fnl->set_domain_warp_fractal_octaves(warp_octaves);
	}

	noise_texture.instantiate();
	const int res = CLAMP(noise_resolution, 16, 256);
	noise_texture->set_width(res);
	noise_texture->set_height(res);
	noise_texture->set_depth(res);
	// Required: the shader samples with repeat wrapping, so a non-seamless texture would show
	// hard discontinuities wherever the coordinate wraps.
	noise_texture->set_seamless(true);
	// Without normalising, fbm output clusters around the midpoint and the coverage threshold
	// becomes hypersensitive -- a 0.01 change would swing clear to overcast.
	noise_texture->set_normalize(true);
	noise_texture->set_noise(fnl);

	material->set_shader_parameter(SNAME("cloud_noise"), noise_texture);
	noise_signature = signature;
}

// ===========================================================================================
// Uniform push
// ===========================================================================================
void EdenCloudShell::_push_uniforms() {
	if (material.is_null()) {
		return;
	}
#define EDEN_CLOUD_U_FLOAT(m_name, m_default, m_hint, m_group) material->set_shader_parameter(SNAME(#m_name), m_name);
#define EDEN_CLOUD_U_INT(m_name, m_default, m_hint, m_group) material->set_shader_parameter(SNAME(#m_name), m_name);
#define EDEN_CLOUD_U_VEC3(m_name, m_x, m_y, m_z, m_group) material->set_shader_parameter(SNAME(#m_name), m_name);
#define EDEN_CLOUD_U_COLOR(m_name, m_r, m_g, m_b, m_group) material->set_shader_parameter(SNAME(#m_name), m_name);
#define EDEN_CLOUD_L_FLOAT(m_name, m_default, m_hint, m_group)
#define EDEN_CLOUD_L_INT(m_name, m_default, m_hint, m_group)
#define EDEN_CLOUD_L_VEC3(m_name, m_x, m_y, m_z, m_group)
#include "eden_cloud_shell_props.inc"
#undef EDEN_CLOUD_U_FLOAT
#undef EDEN_CLOUD_U_INT
#undef EDEN_CLOUD_U_VEC3
#undef EDEN_CLOUD_U_COLOR
#undef EDEN_CLOUD_L_FLOAT
#undef EDEN_CLOUD_L_INT
#undef EDEN_CLOUD_L_VEC3

	// Pushed as a fallback for standalone use. When an EdenPlanetAtmosphere has this material
	// linked, it overwrites these each frame and is authoritative -- see the props table.
	material->set_shader_parameter(SNAME("planet_radius"), planet_radius);
	material->set_shader_parameter(SNAME("planet_center"), planet_center);
}

// ===========================================================================================
// Generated accessors
// ===========================================================================================
// Setters only assign. Which properties affect geometry is decided by _rebuild_mesh() itself,
// which early-outs unless its inputs actually changed -- that avoids having to keep a
// "does this one need a rebuild" flag correct across ~30 properties.
#define EDEN_CLOUD_DEF_SCALAR(m_type, m_name)           \
	void EdenCloudShell::set_##m_name(m_type p_value) { \
		m_name = p_value;                               \
	}                                                   \
	m_type EdenCloudShell::get_##m_name() const {       \
		return m_name;                                  \
	}
#define EDEN_CLOUD_DEF_REF(m_type, m_name)                     \
	void EdenCloudShell::set_##m_name(const m_type &p_value) {  \
		m_name = p_value;                                       \
	}                                                           \
	m_type EdenCloudShell::get_##m_name() const {               \
		return m_name;                                          \
	}

#define EDEN_CLOUD_U_FLOAT(m_name, m_default, m_hint, m_group) EDEN_CLOUD_DEF_SCALAR(float, m_name)
#define EDEN_CLOUD_U_INT(m_name, m_default, m_hint, m_group) EDEN_CLOUD_DEF_SCALAR(int, m_name)
#define EDEN_CLOUD_U_VEC3(m_name, m_x, m_y, m_z, m_group) EDEN_CLOUD_DEF_REF(Vector3, m_name)
#define EDEN_CLOUD_U_COLOR(m_name, m_r, m_g, m_b, m_group) EDEN_CLOUD_DEF_REF(Color, m_name)
#define EDEN_CLOUD_L_FLOAT(m_name, m_default, m_hint, m_group) EDEN_CLOUD_DEF_SCALAR(float, m_name)
#define EDEN_CLOUD_L_INT(m_name, m_default, m_hint, m_group) EDEN_CLOUD_DEF_SCALAR(int, m_name)
#define EDEN_CLOUD_L_VEC3(m_name, m_x, m_y, m_z, m_group) EDEN_CLOUD_DEF_REF(Vector3, m_name)
#include "eden_cloud_shell_props.inc"
#undef EDEN_CLOUD_U_FLOAT
#undef EDEN_CLOUD_U_INT
#undef EDEN_CLOUD_U_VEC3
#undef EDEN_CLOUD_U_COLOR
#undef EDEN_CLOUD_L_FLOAT
#undef EDEN_CLOUD_L_INT
#undef EDEN_CLOUD_L_VEC3

// ===========================================================================================
// Bindings
// ===========================================================================================
void EdenCloudShell::_bind_methods() {
#define EDEN_BIND_ACCESSORS(m_name)                                                            \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &EdenCloudShell::set_##m_name);    \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &EdenCloudShell::get_##m_name);

#define EDEN_CLOUD_U_FLOAT(m_name, m_default, m_hint, m_group) EDEN_BIND_ACCESSORS(m_name)
#define EDEN_CLOUD_U_INT(m_name, m_default, m_hint, m_group) EDEN_BIND_ACCESSORS(m_name)
#define EDEN_CLOUD_U_VEC3(m_name, m_x, m_y, m_z, m_group) EDEN_BIND_ACCESSORS(m_name)
#define EDEN_CLOUD_U_COLOR(m_name, m_r, m_g, m_b, m_group) EDEN_BIND_ACCESSORS(m_name)
#define EDEN_CLOUD_L_FLOAT(m_name, m_default, m_hint, m_group) EDEN_BIND_ACCESSORS(m_name)
#define EDEN_CLOUD_L_INT(m_name, m_default, m_hint, m_group) EDEN_BIND_ACCESSORS(m_name)
#define EDEN_CLOUD_L_VEC3(m_name, m_x, m_y, m_z, m_group) EDEN_BIND_ACCESSORS(m_name)
#include "eden_cloud_shell_props.inc"
#undef EDEN_CLOUD_U_FLOAT
#undef EDEN_CLOUD_U_INT
#undef EDEN_CLOUD_U_VEC3
#undef EDEN_CLOUD_U_COLOR
#undef EDEN_CLOUD_L_FLOAT
#undef EDEN_CLOUD_L_INT
#undef EDEN_CLOUD_L_VEC3

	ClassDB::bind_method(D_METHOD("get_material"), &EdenCloudShell::get_material);
	ClassDB::bind_method(D_METHOD("set_cloud_shader_override", "shader"), &EdenCloudShell::set_cloud_shader_override);
	ClassDB::bind_method(D_METHOD("get_cloud_shader_override"), &EdenCloudShell::get_cloud_shader_override);
	ClassDB::bind_method(D_METHOD("rebuild_noise"), &EdenCloudShell::rebuild_noise);

	ADD_GROUP("Shader", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "cloud_shader_override", PROPERTY_HINT_RESOURCE_TYPE, "Shader"),
			"set_cloud_shader_override", "get_cloud_shader_override");

#define EDEN_CLOUD_U_FLOAT(m_name, m_default, m_hint, m_group)                                              \
	ADD_GROUP(m_group, "");                                                                                 \
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, #m_name, PROPERTY_HINT_RANGE, m_hint), "set_" #m_name, "get_" #m_name);
#define EDEN_CLOUD_U_INT(m_name, m_default, m_hint, m_group)                                              \
	ADD_GROUP(m_group, "");                                                                               \
	ADD_PROPERTY(PropertyInfo(Variant::INT, #m_name, PROPERTY_HINT_RANGE, m_hint), "set_" #m_name, "get_" #m_name);
#define EDEN_CLOUD_U_VEC3(m_name, m_x, m_y, m_z, m_group) \
	ADD_GROUP(m_group, "");                               \
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, #m_name), "set_" #m_name, "get_" #m_name);
#define EDEN_CLOUD_U_COLOR(m_name, m_r, m_g, m_b, m_group)                                              \
	ADD_GROUP(m_group, "");                                                                             \
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, #m_name, PROPERTY_HINT_COLOR_NO_ALPHA), "set_" #m_name, "get_" #m_name);
#define EDEN_CLOUD_L_FLOAT(m_name, m_default, m_hint, m_group)                                              \
	ADD_GROUP(m_group, "");                                                                                 \
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, #m_name, PROPERTY_HINT_RANGE, m_hint), "set_" #m_name, "get_" #m_name);
#define EDEN_CLOUD_L_INT(m_name, m_default, m_hint, m_group)                                              \
	ADD_GROUP(m_group, "");                                                                               \
	ADD_PROPERTY(PropertyInfo(Variant::INT, #m_name, PROPERTY_HINT_RANGE, m_hint), "set_" #m_name, "get_" #m_name);
#define EDEN_CLOUD_L_VEC3(m_name, m_x, m_y, m_z, m_group) \
	ADD_GROUP(m_group, "");                               \
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, #m_name), "set_" #m_name, "get_" #m_name);
#include "eden_cloud_shell_props.inc"
#undef EDEN_CLOUD_U_FLOAT
#undef EDEN_CLOUD_U_INT
#undef EDEN_CLOUD_U_VEC3
#undef EDEN_CLOUD_U_COLOR
#undef EDEN_CLOUD_L_FLOAT
#undef EDEN_CLOUD_L_INT
#undef EDEN_CLOUD_L_VEC3
#undef EDEN_BIND_ACCESSORS
}
