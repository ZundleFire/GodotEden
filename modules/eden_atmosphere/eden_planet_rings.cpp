#include "eden_planet_rings.h"

#include "eden_atmosphere_shaders.gen.h"
#include "scene/resources/surface_tool.h"

EdenPlanetRings::EdenPlanetRings() {
	set_process(true);
}

// ===========================================================================================
// Lifecycle
// ===========================================================================================
void EdenPlanetRings::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			_build_material();
			_rebuild_mesh();
			_push_uniforms();
			set_process(true);
		} break;

		case NOTIFICATION_PROCESS: {
			// Pushed every frame, like the cloud shell: cheap, and it removes a whole class of
			// bug where a property set from script without going through the setter leaves the
			// shader on stale values.
			_push_uniforms();
			_rebuild_mesh();
		} break;
	}
}

Ref<ShaderMaterial> EdenPlanetRings::get_material() {
	if (material.is_null()) {
		_build_material();
	}
	return material;
}

void EdenPlanetRings::set_ring_shader_override(const Ref<Shader> &p_shader) {
	ring_shader_override = p_shader;
	material.unref();
	_build_material();
}

Ref<Shader> EdenPlanetRings::get_ring_shader_override() const {
	return ring_shader_override;
}

void EdenPlanetRings::_build_material() {
	if (material.is_valid()) {
		return;
	}
	Ref<Shader> shader = ring_shader_override;
	if (shader.is_null()) {
		// Built in, so a game needs nothing but the engine binary.
		shader.instantiate();
		shader->set_code(String::utf8(EDEN_RINGS_SHADER_CODE));
	}
	material.instantiate();
	material->set_shader(shader);
}

// ===========================================================================================
// Mesh
// ===========================================================================================
// A flat double-sided annulus (render_mode cull_disabled handles the two faces) built directly in
// the ring's own plane from ring_normal, so no separate orientation transform is needed on the
// node itself -- move or tilt ring_normal and the geometry follows.
void EdenPlanetRings::_rebuild_mesh() {
	if (!is_inside_tree()) {
		return;
	}
	if (material.is_null()) {
		_build_material();
	}

	const float r_in = planet_radius * MAX(ring_inner_radius, 1.001f);
	const float r_out = planet_radius * MAX(ring_outer_radius, r_in / MAX(planet_radius, 1.0f) + 0.001f);
	const int segs = CLAMP(ring_segments, 8, 2048);

	const String signature = vformat("%f|%f|%d", r_in, r_out, segs);
	if (signature == mesh_signature && mesh_instance != nullptr) {
		if (mesh_instance->get_position() != planet_center) {
			mesh_instance->set_position(planet_center);
		}
		return;
	}
	mesh_signature = signature;

	Vector3 normal = ring_normal.normalized();
	if (!normal.is_finite() || normal.length_squared() < 0.5f) {
		normal = Vector3(0, 1, 0);
	}
	Vector3 tangent = Math::abs(normal.dot(Vector3(1, 0, 0))) < 0.99f ? Vector3(1, 0, 0) : Vector3(0, 0, 1);
	tangent = (tangent - normal * normal.dot(tangent)).normalized();
	const Vector3 bitangent = normal.cross(tangent);

	Ref<SurfaceTool> st;
	st.instantiate();
	st->begin(Mesh::PRIMITIVE_TRIANGLES);
	st->set_normal(normal);

	// Two rings of vertices (inner, outer) around the circle; a quad strip between them.
	for (int i = 0; i <= segs; i++) {
		const float a = (float)i / (float)segs * (float)Math::TAU;
		const float c = Math::cos(a), s = Math::sin(a);
		const Vector3 dir = tangent * c + bitangent * s;
		st->set_uv(Vector2((float)i / (float)segs, 0.0f));
		st->add_vertex(dir * r_in);
		st->set_uv(Vector2((float)i / (float)segs, 1.0f));
		st->add_vertex(dir * r_out);
	}
	for (int i = 0; i < segs; i++) {
		const int a0 = i * 2, b0 = i * 2 + 1, a1 = i * 2 + 2, b1 = i * 2 + 3;
		st->add_index(a0);
		st->add_index(a1);
		st->add_index(b0);
		st->add_index(b0);
		st->add_index(a1);
		st->add_index(b1);
	}

	Ref<ArrayMesh> mesh = st->commit();

	if (mesh_instance == nullptr) {
		mesh_instance = memnew(MeshInstance3D);
		mesh_instance->set_name("RingMesh");
		mesh_instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		mesh_instance->set_extra_cull_margin(16384.0f);
		add_child(mesh_instance, false, INTERNAL_MODE_BACK);
	}
	mesh_instance->set_mesh(mesh);
	mesh_instance->set_material_override(material);
	mesh_instance->set_position(planet_center);
}

// ===========================================================================================
// Uniform push
// ===========================================================================================
void EdenPlanetRings::_push_uniforms() {
	if (material.is_null()) {
		return;
	}
#define EDEN_RING_U_FLOAT(m_name, m_default, m_hint, m_group) material->set_shader_parameter(SNAME(#m_name), m_name);
#define EDEN_RING_U_VEC3(m_name, m_x, m_y, m_z, m_group) material->set_shader_parameter(SNAME(#m_name), m_name);
#define EDEN_RING_U_COLOR(m_name, m_r, m_g, m_b, m_group) material->set_shader_parameter(SNAME(#m_name), m_name);
#define EDEN_RING_L_FLOAT(m_name, m_default, m_hint, m_group)
#define EDEN_RING_L_VEC3(m_name, m_x, m_y, m_z, m_group)
#define EDEN_RING_L_INT(m_name, m_default, m_hint, m_group)
#include "eden_planet_rings_props.inc"
#undef EDEN_RING_U_FLOAT
#undef EDEN_RING_U_VEC3
#undef EDEN_RING_U_COLOR
#undef EDEN_RING_L_FLOAT
#undef EDEN_RING_L_VEC3
#undef EDEN_RING_L_INT

	// Fallback for standalone use (no linked EdenPlanetAtmosphere); overwritten each frame once
	// this material is linked via add_linked_material(), same as EdenCloudShell.
	material->set_shader_parameter(SNAME("planet_radius"), planet_radius);
	material->set_shader_parameter(SNAME("planet_center"), planet_center);
}

// ===========================================================================================
// Generated accessors
// ===========================================================================================
#define EDEN_RING_DEF_SCALAR(m_type, m_name)                    \
	void EdenPlanetRings::set_##m_name(m_type p_value) {         \
		m_name = p_value;                                        \
	}                                                             \
	m_type EdenPlanetRings::get_##m_name() const {                \
		return m_name;                                            \
	}
#define EDEN_RING_DEF_REF(m_type, m_name)                            \
	void EdenPlanetRings::set_##m_name(const m_type &p_value) {      \
		m_name = p_value;                                            \
	}                                                                 \
	m_type EdenPlanetRings::get_##m_name() const {                    \
		return m_name;                                                \
	}

#define EDEN_RING_U_FLOAT(m_name, m_default, m_hint, m_group) EDEN_RING_DEF_SCALAR(float, m_name)
#define EDEN_RING_U_VEC3(m_name, m_x, m_y, m_z, m_group) EDEN_RING_DEF_REF(Vector3, m_name)
#define EDEN_RING_U_COLOR(m_name, m_r, m_g, m_b, m_group) EDEN_RING_DEF_REF(Color, m_name)
#define EDEN_RING_L_FLOAT(m_name, m_default, m_hint, m_group) EDEN_RING_DEF_SCALAR(float, m_name)
#define EDEN_RING_L_VEC3(m_name, m_x, m_y, m_z, m_group) EDEN_RING_DEF_REF(Vector3, m_name)
#define EDEN_RING_L_INT(m_name, m_default, m_hint, m_group) EDEN_RING_DEF_SCALAR(int, m_name)
#include "eden_planet_rings_props.inc"
#undef EDEN_RING_U_FLOAT
#undef EDEN_RING_U_VEC3
#undef EDEN_RING_U_COLOR
#undef EDEN_RING_L_FLOAT
#undef EDEN_RING_L_VEC3
#undef EDEN_RING_L_INT

// ===========================================================================================
// Bindings
// ===========================================================================================
void EdenPlanetRings::_bind_methods() {
#define EDEN_BIND_ACCESSORS(m_name)                                                              \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &EdenPlanetRings::set_##m_name);      \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &EdenPlanetRings::get_##m_name);

#define EDEN_RING_U_FLOAT(m_name, m_default, m_hint, m_group) EDEN_BIND_ACCESSORS(m_name)
#define EDEN_RING_U_VEC3(m_name, m_x, m_y, m_z, m_group) EDEN_BIND_ACCESSORS(m_name)
#define EDEN_RING_U_COLOR(m_name, m_r, m_g, m_b, m_group) EDEN_BIND_ACCESSORS(m_name)
#define EDEN_RING_L_FLOAT(m_name, m_default, m_hint, m_group) EDEN_BIND_ACCESSORS(m_name)
#define EDEN_RING_L_VEC3(m_name, m_x, m_y, m_z, m_group) EDEN_BIND_ACCESSORS(m_name)
#define EDEN_RING_L_INT(m_name, m_default, m_hint, m_group) EDEN_BIND_ACCESSORS(m_name)
#include "eden_planet_rings_props.inc"
#undef EDEN_RING_U_FLOAT
#undef EDEN_RING_U_VEC3
#undef EDEN_RING_U_COLOR
#undef EDEN_RING_L_FLOAT
#undef EDEN_RING_L_VEC3
#undef EDEN_RING_L_INT

	ClassDB::bind_method(D_METHOD("get_material"), &EdenPlanetRings::get_material);
	ClassDB::bind_method(D_METHOD("set_ring_shader_override", "shader"), &EdenPlanetRings::set_ring_shader_override);
	ClassDB::bind_method(D_METHOD("get_ring_shader_override"), &EdenPlanetRings::get_ring_shader_override);

	ADD_GROUP("Shader", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "ring_shader_override", PROPERTY_HINT_RESOURCE_TYPE, "Shader"),
			"set_ring_shader_override", "get_ring_shader_override");

#define EDEN_RING_U_FLOAT(m_name, m_default, m_hint, m_group)                                              \
	ADD_GROUP(m_group, "");                                                                                \
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, #m_name, PROPERTY_HINT_RANGE, m_hint), "set_" #m_name, "get_" #m_name);
#define EDEN_RING_U_VEC3(m_name, m_x, m_y, m_z, m_group) \
	ADD_GROUP(m_group, "");                              \
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, #m_name), "set_" #m_name, "get_" #m_name);
#define EDEN_RING_U_COLOR(m_name, m_r, m_g, m_b, m_group)                                              \
	ADD_GROUP(m_group, "");                                                                            \
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, #m_name, PROPERTY_HINT_COLOR_NO_ALPHA), "set_" #m_name, "get_" #m_name);
#define EDEN_RING_L_FLOAT(m_name, m_default, m_hint, m_group)                                              \
	ADD_GROUP(m_group, "");                                                                                \
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, #m_name, PROPERTY_HINT_RANGE, m_hint), "set_" #m_name, "get_" #m_name);
#define EDEN_RING_L_VEC3(m_name, m_x, m_y, m_z, m_group) \
	ADD_GROUP(m_group, "");                              \
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, #m_name), "set_" #m_name, "get_" #m_name);
#define EDEN_RING_L_INT(m_name, m_default, m_hint, m_group)                                              \
	ADD_GROUP(m_group, "");                                                                              \
	ADD_PROPERTY(PropertyInfo(Variant::INT, #m_name, PROPERTY_HINT_RANGE, m_hint), "set_" #m_name, "get_" #m_name);
#include "eden_planet_rings_props.inc"
#undef EDEN_RING_U_FLOAT
#undef EDEN_RING_U_VEC3
#undef EDEN_RING_U_COLOR
#undef EDEN_RING_L_FLOAT
#undef EDEN_RING_L_VEC3
#undef EDEN_RING_L_INT
#undef EDEN_BIND_ACCESSORS
}
