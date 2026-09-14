#ifndef EDEN_PLANET_RINGS_H
#define EDEN_PLANET_RINGS_H

#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/material.h"

// A flat, tilted ring around a planet -- Saturn-style -- built from a deterministic banded pattern
// so no texture asset is required. Link get_material() into EdenPlanetAtmosphere::add_linked_material()
// so the rings pick up the same sun direction/colour as the sky, clouds and terrain; unlinked, it
// still renders using atmosphere_common.gdshaderinc's own default sun direction.
//
// The mesh is a plain double-sided annulus in the ring's own plane (render_mode cull_disabled), so
// both faces show without doubling the geometry. All the banding, lighting and self-shadowing
// happen per-fragment in the shader.
class EdenPlanetRings : public Node3D {
	GDCLASS(EdenPlanetRings, Node3D);

public:
	EdenPlanetRings();

	Ref<ShaderMaterial> get_material();
	void set_ring_shader_override(const Ref<Shader> &p_shader);
	Ref<Shader> get_ring_shader_override() const;

#define EDEN_RING_U_FLOAT(m_name, m_default, m_hint, m_group) \
	void set_##m_name(float p_value);                         \
	float get_##m_name() const;
#define EDEN_RING_U_VEC3(m_name, m_x, m_y, m_z, m_group) \
	void set_##m_name(const Vector3 &p_value);           \
	Vector3 get_##m_name() const;
#define EDEN_RING_U_COLOR(m_name, m_r, m_g, m_b, m_group) \
	void set_##m_name(const Color &p_value);              \
	Color get_##m_name() const;
#define EDEN_RING_L_FLOAT(m_name, m_default, m_hint, m_group) \
	void set_##m_name(float p_value);                         \
	float get_##m_name() const;
#define EDEN_RING_L_VEC3(m_name, m_x, m_y, m_z, m_group) \
	void set_##m_name(const Vector3 &p_value);           \
	Vector3 get_##m_name() const;
#define EDEN_RING_L_INT(m_name, m_default, m_hint, m_group) \
	void set_##m_name(int p_value);                         \
	int get_##m_name() const;
#include "eden_planet_rings_props.inc"
#undef EDEN_RING_U_FLOAT
#undef EDEN_RING_U_VEC3
#undef EDEN_RING_U_COLOR
#undef EDEN_RING_L_FLOAT
#undef EDEN_RING_L_VEC3
#undef EDEN_RING_L_INT

protected:
	static void _bind_methods();
	void _notification(int p_what);

private:
	void _build_material();
	void _rebuild_mesh();
	void _push_uniforms();

	MeshInstance3D *mesh_instance = nullptr;
	Ref<ShaderMaterial> material;
	Ref<Shader> ring_shader_override;
	String mesh_signature;

#define EDEN_RING_U_FLOAT(m_name, m_default, m_hint, m_group) float m_name = m_default;
#define EDEN_RING_U_VEC3(m_name, m_x, m_y, m_z, m_group) Vector3 m_name = Vector3(m_x, m_y, m_z);
#define EDEN_RING_U_COLOR(m_name, m_r, m_g, m_b, m_group) Color m_name = Color(m_r, m_g, m_b);
#define EDEN_RING_L_FLOAT(m_name, m_default, m_hint, m_group) float m_name = m_default;
#define EDEN_RING_L_VEC3(m_name, m_x, m_y, m_z, m_group) Vector3 m_name = Vector3(m_x, m_y, m_z);
#define EDEN_RING_L_INT(m_name, m_default, m_hint, m_group) int m_name = m_default;
#include "eden_planet_rings_props.inc"
#undef EDEN_RING_U_FLOAT
#undef EDEN_RING_U_VEC3
#undef EDEN_RING_U_COLOR
#undef EDEN_RING_L_FLOAT
#undef EDEN_RING_L_VEC3
#undef EDEN_RING_L_INT
};

#endif // EDEN_PLANET_RINGS_H
