#ifndef EDEN_PLANET_ATMOSPHERE_H
#define EDEN_PLANET_ATMOSPHERE_H

#include "core/templates/local_vector.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/sky.h"
#include "scene/resources/texture.h"

#include "eden_atmosphere_post_effect.h"

// Planetary atmosphere driver: owns the sky material, the sun, and the link between them.
//
// The problem it solves: a sky shader can compute correct scattering, but Godot will still
// light the terrain with whatever flat DirectionalLight3D is configured, so at sunset the sky
// goes orange and the ground stays midday-blue. This node closes that loop -- it evaluates the
// same sun_transmittance() the shader uses, on the CPU, at the camera's planet-relative
// position, and feeds the result to the DirectionalLight3D. Sky and terrain then agree by
// construction, and the sun's colour tracks where the player is on the planet.
//
// Ambient is handled for free: the sky renders into Godot's radiance cubemap, so setting the
// Environment to AMBIENT_SOURCE_SKY (this node does that) makes ambient follow the sky.
//
// Link cloud/terrain materials that include the same shader header via add_linked_material();
// they receive identical uniform values, which is what keeps clouds reddening with the sunset.
class EdenPlanetAtmosphere : public Node3D {
	GDCLASS(EdenPlanetAtmosphere, Node3D);

public:
	EdenPlanetAtmosphere();

	// --- Wiring ---------------------------------------------------------------------------
	void set_sun_light_path(const NodePath &p_path);
	NodePath get_sun_light_path() const;
	void set_moon_light_path(const NodePath &p_path);
	NodePath get_moon_light_path() const;
	void set_environment_path(const NodePath &p_path);
	NodePath get_environment_path() const;

	void set_linked_materials(const TypedArray<ShaderMaterial> &p_materials);
	TypedArray<ShaderMaterial> get_linked_materials() const;
	void add_linked_material(const Ref<ShaderMaterial> &p_material);
	void remove_linked_material(const Ref<ShaderMaterial> &p_material);

	// Supply your own sky shader; null uses the module's built-in.
	void set_sky_shader_override(const Ref<Shader> &p_shader);
	Ref<Shader> get_sky_shader_override() const;

	// --- Deep space -------------------------------------------------------------------------
	// Equirectangular panoramas, in Godot's own panorama convention (u = atan2(x,-z)/2pi,
	// v = acos(y)/pi), so anything authored against PanoramaSkyMaterial drops in unchanged.
	// Two slots so a starfield and a nebula can be layered and cross-faded independently.
	// Both null -> the procedural star field is used, so the sky works with no art at all.
	void set_space_panorama(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_space_panorama() const;
	void set_space_overlay(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_space_overlay() const;

	// --- Moon ------------------------------------------------------------------------------
	// Either a photo of the moon filling a square image (moon_texture_equirect = false) or an
	// equirectangular surface map (true). Null draws a plain grey sphere -- phases still work,
	// because they come from lighting the sphere, not from the texture.
	void set_moon_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_moon_texture() const;

	// --- Queries (also useful from script and tools) ---------------------------------------
	Vector3 get_sun_direction() const;
	// Illuminated fraction of the moon as seen from the planet: 0 new, 1 full.
	float get_moon_illumination() const;
	// Colour of direct sunlight at a planet-relative point, after atmospheric extinction.
	Vector3 sun_transmittance_at(const Vector3 &p_planet_relative) const;
	// Time-of-day phase putting the sun at a given elevation for an observer with this local
	// up. Returns -1 when unreachable (e.g. any elevation at the pole, where the sun's height
	// equals the declination all day and never varies with phase).
	float phase_for_sun_elevation(const Vector3 &p_up, float p_elevation_deg, bool p_rising = true) const;

	Ref<ShaderMaterial> get_sky_material() const;
	// The fog + light-ray post-process this node manages; null until first used.
	Ref<EdenAtmospherePostEffect> get_post_effect() const;
	Ref<Sky> get_sky() const;

	// --- Generated accessors ---------------------------------------------------------------
#define EDEN_ATMO_U_FLOAT(m_name, m_default, m_hint, m_group) \
	void set_##m_name(float p_value);                         \
	float get_##m_name() const;
#define EDEN_ATMO_U_VEC3(m_name, m_x, m_y, m_z, m_group) \
	void set_##m_name(const Vector3 &p_value);           \
	Vector3 get_##m_name() const;
#define EDEN_ATMO_U_COLOR(m_name, m_r, m_g, m_b, m_group) \
	void set_##m_name(const Color &p_value);              \
	Color get_##m_name() const;
#define EDEN_ATMO_L_FLOAT(m_name, m_default, m_hint, m_group) \
	void set_##m_name(float p_value);                         \
	float get_##m_name() const;
#define EDEN_ATMO_L_VEC3(m_name, m_x, m_y, m_z, m_group) \
	void set_##m_name(const Vector3 &p_value);           \
	Vector3 get_##m_name() const;
#define EDEN_ATMO_L_BOOL(m_name, m_default, m_group) \
	void set_##m_name(bool p_value);                 \
	bool get_##m_name() const;
#define EDEN_ATMO_L_INT(m_name, m_default, m_hint, m_group) \
	void set_##m_name(int p_value);                         \
	int get_##m_name() const;
#define EDEN_ATMO_L_STRING(m_name, m_default, m_group) \
	void set_##m_name(const String &p_value);           \
	String get_##m_name() const;
#include "eden_planet_atmosphere_props.inc"
#undef EDEN_ATMO_L_STRING
#undef EDEN_ATMO_U_FLOAT
#undef EDEN_ATMO_U_VEC3
#undef EDEN_ATMO_U_COLOR
#undef EDEN_ATMO_L_FLOAT
#undef EDEN_ATMO_L_VEC3
#undef EDEN_ATMO_L_BOOL
#undef EDEN_ATMO_L_INT

protected:
	static void _bind_methods();
	void _notification(int p_what);

private:
	void _resolve_nodes();
	void _build_sky();
	// Kept separate from _build_sky() and retried each frame because add_child() runs _ready()
	// immediately: a caller that adds this node and only then assigns environment_path would
	// otherwise leave the sky permanently unattached.
	void _try_configure_environment();
	void _update_sun_direction();
	void _update_moon_direction();
	void _push_uniforms();
	void _update_sun_light();
	void _update_moon_light();
	void _update_post_effect();
	void _attach_post_effect();
	void _detach_post_effect();
	void _update_bloom();
	void _set_on_all(const StringName &p_param, const Variant &p_value);

	// CPU mirror of the shader's scattering helpers. These must stay in step with
	// shaders/atmosphere_common.gdshaderinc; if they drift, the symptom is sky and terrain
	// lighting disagreeing at sunset.
	Vector3 _beta_rayleigh() const;
	float _beta_mie() const;
	Vector2 _ray_sphere(const Vector3 &p_ro, const Vector3 &p_rd, float p_radius) const;
	// Direction-generic, so the moon light gets the same extinction and horizon occlusion as
	// the sun. The _sun wrappers keep existing call sites unchanged.
	Vector2 _optical_depth_along(const Vector3 &p_point, const Vector3 &p_dir) const;
	float _planet_shadow_dir(const Vector3 &p_point, const Vector3 &p_dir) const;
	Vector3 _transmittance_toward(const Vector3 &p_point, const Vector3 &p_dir) const;
	Vector2 _optical_depth_to_sun(const Vector3 &p_point) const;
	float _planet_shadow(const Vector3 &p_point) const;
	Vector3 _sun_transmittance(const Vector3 &p_point) const;
	Vector3 _camera_planet_relative() const;
	void _equatorial_basis(Vector3 &r_axis, Vector3 &r_a, Vector3 &r_b) const;

	void _bake_transmittance_lut();
	static float _lut_cos_to_u(float p_cos);
	static float _lut_u_to_cos(float p_u);

	NodePath sun_light_path;
	NodePath moon_light_path;
	NodePath environment_path;
	TypedArray<ShaderMaterial> linked_materials;
	Ref<Shader> sky_shader_override;
	Ref<Texture2D> space_panorama;
	Ref<Texture2D> space_overlay;
	Ref<Texture2D> moon_texture;

	Ref<ShaderMaterial> sky_material;
	Ref<Sky> sky;
	Ref<ImageTexture> lut_texture;
	String lut_signature;
	bool env_configured = false;
	Ref<EdenAtmospherePostEffect> post_effect;
	String bloom_signature;

	DirectionalLight3D *sun_light = nullptr;
	DirectionalLight3D *moon_light = nullptr;
	WorldEnvironment *world_env = nullptr;
	Vector3 sun_direction = Vector3(0, 1, 0);
	// Normal of the moon's orbital plane in the planet-fixed frame. Doubles as the moon's "up"
	// for texture orientation: a tidally locked moon keeps a fixed attitude relative to it.
	Vector3 moon_orbit_normal = Vector3(0, 1, 0);

	// --- Generated members -----------------------------------------------------------------
#define EDEN_ATMO_U_FLOAT(m_name, m_default, m_hint, m_group) float m_name = m_default;
#define EDEN_ATMO_U_VEC3(m_name, m_x, m_y, m_z, m_group) Vector3 m_name = Vector3(m_x, m_y, m_z);
#define EDEN_ATMO_U_COLOR(m_name, m_r, m_g, m_b, m_group) Color m_name = Color(m_r, m_g, m_b);
#define EDEN_ATMO_L_FLOAT(m_name, m_default, m_hint, m_group) float m_name = m_default;
#define EDEN_ATMO_L_VEC3(m_name, m_x, m_y, m_z, m_group) Vector3 m_name = Vector3(m_x, m_y, m_z);
#define EDEN_ATMO_L_BOOL(m_name, m_default, m_group) bool m_name = m_default;
#define EDEN_ATMO_L_INT(m_name, m_default, m_hint, m_group) int m_name = m_default;
#define EDEN_ATMO_L_STRING(m_name, m_default, m_group) String m_name = m_default;
#include "eden_planet_atmosphere_props.inc"
#undef EDEN_ATMO_L_STRING
#undef EDEN_ATMO_U_FLOAT
#undef EDEN_ATMO_U_VEC3
#undef EDEN_ATMO_U_COLOR
#undef EDEN_ATMO_L_FLOAT
#undef EDEN_ATMO_L_VEC3
#undef EDEN_ATMO_L_BOOL
#undef EDEN_ATMO_L_INT
};

#endif // EDEN_PLANET_ATMOSPHERE_H
