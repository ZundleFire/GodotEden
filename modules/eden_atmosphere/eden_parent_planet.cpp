#include "eden_parent_planet.h"
#include "eden_material_util.h"

#include "eden_planet_atmosphere.h"

EdenParentPlanet::EdenParentPlanet() {
	set_process(true);
}

// ===========================================================================================
// Lifecycle
// ===========================================================================================
void EdenParentPlanet::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			_push_uniforms();
			set_process(true);
		} break;

		case NOTIFICATION_PROCESS: {
			// Pushed every frame, like EdenCloudShell/EdenPlanetRings: cheap, and it removes a
			// whole class of bug where a property set from script without going through the
			// setter leaves the shader on stale values.
			_push_uniforms();
		} break;
	}
}

EdenPlanetAtmosphere *EdenParentPlanet::_find_atmosphere() const {
	Node *sibling_scope = get_parent();
	if (sibling_scope == nullptr) {
		return nullptr;
	}
	for (int i = 0; i < sibling_scope->get_child_count(); i++) {
		EdenPlanetAtmosphere *atmo = Object::cast_to<EdenPlanetAtmosphere>(sibling_scope->get_child(i));
		if (atmo != nullptr) {
			return atmo;
		}
	}
	return nullptr;
}

void EdenParentPlanet::_push_uniforms() {
	EdenPlanetAtmosphere *atmo = _find_atmosphere();
	Ref<ShaderMaterial> sky_material = atmo != nullptr ? atmo->get_sky_material() : Ref<ShaderMaterial>();
	if (sky_material.is_null()) {
		return;
	}

	// Radians/second the surface (and, faster/slower, the clouds) spin -- a slowly "alive" planet.
	// Advanced here rather than read once, same reasoning as EdenPlanetAtmosphere's own day/moon
	// phase timers: a per-frame delta keeps it correct across any time_scale/pause state instead
	// of assuming a fixed frame rate.
	parent_planet_band_phase += parent_planet_band_speed * get_process_delta_time();

#define EDEN_PP_U_FLOAT(m_name, m_default, m_hint, m_group) eden_set_param(sky_material, SNAME(#m_name), m_name);
#define EDEN_PP_U_INT(m_name, m_default, m_hint, m_group) eden_set_param(sky_material, SNAME(#m_name), m_name);
#define EDEN_PP_U_VEC3(m_name, m_x, m_y, m_z, m_group) eden_set_param(sky_material, SNAME(#m_name), m_name);
#define EDEN_PP_U_COLOR(m_name, m_r, m_g, m_b, m_group) eden_set_param(sky_material, SNAME(#m_name), m_name);
#define EDEN_PP_L_FLOAT(m_name, m_default, m_hint, m_group)
#define EDEN_PP_L_BOOL(m_name, m_default, m_group)
#include "eden_parent_planet_props.inc"
#undef EDEN_PP_U_FLOAT
#undef EDEN_PP_U_INT
#undef EDEN_PP_U_VEC3
#undef EDEN_PP_U_COLOR
#undef EDEN_PP_L_FLOAT
#undef EDEN_PP_L_BOOL

	eden_set_param(sky_material, SNAME("parent_planet_enabled"), parent_planet_enabled);
	eden_set_param(sky_material, SNAME("parent_planet_texture"), parent_planet_texture);
	eden_set_param(sky_material, SNAME("parent_planet_texture_enabled"), parent_planet_texture.is_valid());
	eden_set_param(sky_material, SNAME("parent_planet_texture_rotation"), Math::deg_to_rad(parent_planet_texture_rotation_deg));
	// See band_phase_texture
	if (band_phase_texture.is_null()) {
		band_phase_image = Image::create_empty(1, 1, false, Image::FORMAT_RF);
		band_phase_image->set_pixel(0, 0, Color(parent_planet_band_phase, 0, 0));
		band_phase_texture = ImageTexture::create_from_image(band_phase_image);
	} else {
		band_phase_image->set_pixel(0, 0, Color(parent_planet_band_phase, 0, 0));
		band_phase_texture->update(band_phase_image);
	}
	eden_set_param(sky_material, SNAME("parent_planet_band_time_tex"), band_phase_texture);
}

// ===========================================================================================
// Generated accessors
// ===========================================================================================
#define EDEN_PP_DEF_SCALAR(m_type, m_name)                     \
	void EdenParentPlanet::set_##m_name(m_type p_value) {       \
		m_name = p_value;                                       \
	}                                                            \
	m_type EdenParentPlanet::get_##m_name() const {              \
		return m_name;                                           \
	}
#define EDEN_PP_DEF_REF(m_type, m_name)                         \
	void EdenParentPlanet::set_##m_name(const m_type &p_value) { \
		m_name = p_value;                                        \
	}                                                             \
	m_type EdenParentPlanet::get_##m_name() const {               \
		return m_name;                                            \
	}

#define EDEN_PP_U_FLOAT(m_name, m_default, m_hint, m_group) EDEN_PP_DEF_SCALAR(float, m_name)
#define EDEN_PP_U_INT(m_name, m_default, m_hint, m_group) EDEN_PP_DEF_SCALAR(int, m_name)
#define EDEN_PP_U_VEC3(m_name, m_x, m_y, m_z, m_group) EDEN_PP_DEF_REF(Vector3, m_name)
#define EDEN_PP_U_COLOR(m_name, m_r, m_g, m_b, m_group) EDEN_PP_DEF_REF(Color, m_name)
#define EDEN_PP_L_FLOAT(m_name, m_default, m_hint, m_group) EDEN_PP_DEF_SCALAR(float, m_name)
#define EDEN_PP_L_BOOL(m_name, m_default, m_group) EDEN_PP_DEF_SCALAR(bool, m_name)
#include "eden_parent_planet_props.inc"
#undef EDEN_PP_U_FLOAT
#undef EDEN_PP_U_INT
#undef EDEN_PP_U_VEC3
#undef EDEN_PP_U_COLOR
#undef EDEN_PP_L_FLOAT
#undef EDEN_PP_L_BOOL

void EdenParentPlanet::set_parent_planet_texture(const Ref<Texture2D> &p_texture) {
	parent_planet_texture = p_texture;
}

Ref<Texture2D> EdenParentPlanet::get_parent_planet_texture() const {
	return parent_planet_texture;
}

// ===========================================================================================
// Bindings
// ===========================================================================================
void EdenParentPlanet::_bind_methods() {
#define EDEN_PP_BIND_ACCESSORS(m_name)                                                                  \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &EdenParentPlanet::set_##m_name);            \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &EdenParentPlanet::get_##m_name);

#define EDEN_PP_U_FLOAT(m_name, m_default, m_hint, m_group) EDEN_PP_BIND_ACCESSORS(m_name)
#define EDEN_PP_U_INT(m_name, m_default, m_hint, m_group) EDEN_PP_BIND_ACCESSORS(m_name)
#define EDEN_PP_U_VEC3(m_name, m_x, m_y, m_z, m_group) EDEN_PP_BIND_ACCESSORS(m_name)
#define EDEN_PP_U_COLOR(m_name, m_r, m_g, m_b, m_group) EDEN_PP_BIND_ACCESSORS(m_name)
#define EDEN_PP_L_FLOAT(m_name, m_default, m_hint, m_group) EDEN_PP_BIND_ACCESSORS(m_name)
#define EDEN_PP_L_BOOL(m_name, m_default, m_group) EDEN_PP_BIND_ACCESSORS(m_name)
#include "eden_parent_planet_props.inc"
#undef EDEN_PP_U_FLOAT
#undef EDEN_PP_U_INT
#undef EDEN_PP_U_VEC3
#undef EDEN_PP_U_COLOR
#undef EDEN_PP_L_FLOAT
#undef EDEN_PP_L_BOOL

	ClassDB::bind_method(D_METHOD("set_parent_planet_texture", "texture"), &EdenParentPlanet::set_parent_planet_texture);
	ClassDB::bind_method(D_METHOD("get_parent_planet_texture"), &EdenParentPlanet::get_parent_planet_texture);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "parent_planet_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"),
			"set_parent_planet_texture", "get_parent_planet_texture");

#define EDEN_PP_U_FLOAT(m_name, m_default, m_hint, m_group)                                                       \
	ADD_GROUP(m_group, "");                                                                                        \
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, #m_name, PROPERTY_HINT_RANGE, m_hint), "set_" #m_name, "get_" #m_name);
#define EDEN_PP_U_INT(m_name, m_default, m_hint, m_group)                                                       \
	ADD_GROUP(m_group, "");                                                                                      \
	ADD_PROPERTY(PropertyInfo(Variant::INT, #m_name, PROPERTY_HINT_RANGE, m_hint), "set_" #m_name, "get_" #m_name);
#define EDEN_PP_U_VEC3(m_name, m_x, m_y, m_z, m_group)   \
	ADD_GROUP(m_group, "");                               \
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, #m_name), "set_" #m_name, "get_" #m_name);
#define EDEN_PP_U_COLOR(m_name, m_r, m_g, m_b, m_group)                                                     \
	ADD_GROUP(m_group, "");                                                                                  \
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, #m_name, PROPERTY_HINT_COLOR_NO_ALPHA), "set_" #m_name, "get_" #m_name);
#define EDEN_PP_L_FLOAT(m_name, m_default, m_hint, m_group)                                                       \
	ADD_GROUP(m_group, "");                                                                                        \
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, #m_name, PROPERTY_HINT_RANGE, m_hint), "set_" #m_name, "get_" #m_name);
#define EDEN_PP_L_BOOL(m_name, m_default, m_group) \
	ADD_GROUP(m_group, "");                         \
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, #m_name), "set_" #m_name, "get_" #m_name);
#include "eden_parent_planet_props.inc"
#undef EDEN_PP_U_FLOAT
#undef EDEN_PP_U_INT
#undef EDEN_PP_U_VEC3
#undef EDEN_PP_U_COLOR
#undef EDEN_PP_L_FLOAT
#undef EDEN_PP_L_BOOL
}
