#ifndef EDEN_PARENT_PLANET_H
#define EDEN_PARENT_PLANET_H

#include "scene/3d/node_3d.h"
#include "scene/resources/material.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/texture.h"

// A large body fixed in the sky, for a "standing on a moon" view of the planet you orbit.
// Shaded like the moon (a real lit-sphere phase), with a pixel-art procedural Earth-like surface
// (ocean/land/ice, drifting clouds, posterised lighting) when no texture is supplied -- ported
// from the "Animated Pixel Art Planet" shader (godotshaders.com/shader/animated-pixel-art-planet).
// See planet_sky.gdshader's pp_* functions for the port notes.
//
// This node owns no geometry or material of its own: the disc is drawn as part of the shared sky
// shader (Godot's Environment takes exactly one Sky resource, so the rendering itself cannot be
// split out). What this node owns is the property surface and the per-frame push of its own
// "parent_planet_*" uniforms into whichever sibling EdenPlanetAtmosphere owns that sky material --
// split out of EdenPlanetAtmosphere because the property list had grown to ~50 entries doing a
// visually and logically independent job (a background sky object, not the local atmosphere).
//
// Drop this as a sibling of an EdenPlanetAtmosphere node; it finds it automatically, the same way
// EdenCloudShell/EdenPlanetRings are auto-linked, just in the opposite push direction.
class EdenPlanetAtmosphere;

class EdenParentPlanet : public Node3D {
	GDCLASS(EdenParentPlanet, Node3D);

public:
	EdenParentPlanet();

	// Equirectangular surface map. Null uses the procedural gas-giant/pixel-art surface below.
	void set_parent_planet_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_parent_planet_texture() const;

	// --- Generated accessors ---------------------------------------------------------------
#define EDEN_PP_U_FLOAT(m_name, m_default, m_hint, m_group) \
	void set_##m_name(float p_value);                        \
	float get_##m_name() const;
#define EDEN_PP_U_INT(m_name, m_default, m_hint, m_group) \
	void set_##m_name(int p_value);                        \
	int get_##m_name() const;
#define EDEN_PP_U_VEC3(m_name, m_x, m_y, m_z, m_group) \
	void set_##m_name(const Vector3 &p_value);          \
	Vector3 get_##m_name() const;
#define EDEN_PP_U_COLOR(m_name, m_r, m_g, m_b, m_group) \
	void set_##m_name(const Color &p_value);             \
	Color get_##m_name() const;
#define EDEN_PP_L_FLOAT(m_name, m_default, m_hint, m_group) \
	void set_##m_name(float p_value);                        \
	float get_##m_name() const;
#define EDEN_PP_L_BOOL(m_name, m_default, m_group) \
	void set_##m_name(bool p_value);                \
	bool get_##m_name() const;
#include "eden_parent_planet_props.inc"
#undef EDEN_PP_U_FLOAT
#undef EDEN_PP_U_INT
#undef EDEN_PP_U_VEC3
#undef EDEN_PP_U_COLOR
#undef EDEN_PP_L_FLOAT
#undef EDEN_PP_L_BOOL

protected:
	static void _bind_methods();
	void _notification(int p_what);

private:
	void _push_uniforms();
	// Sibling search, not cached across frames: this node has no lifecycle hook that fires when a
	// sibling atmosphere is added/removed/reparented later, and the search itself is a handful of
	// pointer comparisons -- not worth the staleness risk to save.
	EdenPlanetAtmosphere *_find_atmosphere() const;

	Ref<Texture2D> parent_planet_texture;
	float parent_planet_band_phase = 0.0f;
	// The band phase reaches the sky shader as the one texel of this texture rather than as a uniform: a uniform
	// changing every frame made Godot re-render the sky's whole radiance cubemap every frame, while updating a
	// texture's data leaves the material untouched. (The planet is not drawn in the cubemap pass anyway.)
	Ref<Image> band_phase_image;
	Ref<ImageTexture> band_phase_texture;

	// --- Generated members -----------------------------------------------------------------
#define EDEN_PP_U_FLOAT(m_name, m_default, m_hint, m_group) float m_name = m_default;
#define EDEN_PP_U_INT(m_name, m_default, m_hint, m_group) int m_name = m_default;
#define EDEN_PP_U_VEC3(m_name, m_x, m_y, m_z, m_group) Vector3 m_name = Vector3(m_x, m_y, m_z);
#define EDEN_PP_U_COLOR(m_name, m_r, m_g, m_b, m_group) Color m_name = Color(m_r, m_g, m_b);
#define EDEN_PP_L_FLOAT(m_name, m_default, m_hint, m_group) float m_name = m_default;
#define EDEN_PP_L_BOOL(m_name, m_default, m_group) bool m_name = m_default;
#include "eden_parent_planet_props.inc"
#undef EDEN_PP_U_FLOAT
#undef EDEN_PP_U_INT
#undef EDEN_PP_U_VEC3
#undef EDEN_PP_U_COLOR
#undef EDEN_PP_L_FLOAT
#undef EDEN_PP_L_BOOL
};

#endif // EDEN_PARENT_PLANET_H
