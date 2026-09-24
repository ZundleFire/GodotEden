#ifndef EDEN_SPACE_ENVIRONMENT_H
#define EDEN_SPACE_ENVIRONMENT_H

#include "core/templates/vector.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/texture.h"

// Deep-space background: the procedural star field plus an optional pair of equirectangular
// panoramas (a starfield and a nebula/overlay, cross-fadeable independently), and the folder
// browsing that used to live as GDScript in atmosphere_demo.gd (_build_space_textures /
// _apply_space_texture / _make_placeholder_panorama) -- ported here so a project needs no script
// at all to get panorama browsing, the same way EdenPlanetAtmosphere and EdenCloudShell need none.
//
// Like EdenParentPlanet, this owns no geometry or material of its own: the sky itself can only
// ever be one Sky resource (that is Godot's Environment API, not a limitation of this module), so
// what this node owns is the property surface and the per-frame push of its own "star_*"/"space_*"
// uniforms into whichever sibling EdenPlanetAtmosphere owns that sky material.
//
// Drop this as a sibling of an EdenPlanetAtmosphere node; it finds it automatically, the same way
// EdenCloudShell/EdenPlanetRings are auto-linked, just in the opposite push direction.
class EdenPlanetAtmosphere;

class EdenSpaceEnvironment : public Node3D {
	GDCLASS(EdenSpaceEnvironment, Node3D);

public:
	EdenSpaceEnvironment();

	// Equirectangular panoramas (Godot's own panorama convention: u = atan2(x,-z), v = acos(y)),
	// so anything authored against PanoramaSkyMaterial drops in unchanged. Two slots so a
	// starfield and a nebula can be layered and cross-faded independently. Both null falls back
	// to the procedural star field (or the generated placeholder, see use_placeholder_panorama).
	void set_space_panorama(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_space_panorama() const;
	void set_space_overlay(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_space_overlay() const;

	// Re-scans space_texture_dir and applies the first (or current index's) panorama. Normally
	// automatic on property change; exposed for a manual refresh after adding files at runtime.
	void rescan_panoramas();
	// Cycles through the panoramas found in space_texture_dir (or does nothing if none were
	// found and only the generated placeholder is showing).
	void next_panorama();
	void previous_panorama();
	// For a HUD: "3/20  nebula_04.hdr" or "(none - procedural stars)".
	String get_panorama_label() const;

	// --- Generated accessors ---------------------------------------------------------------
#define EDEN_SE_U_FLOAT(m_name, m_default, m_hint, m_group) \
	void set_##m_name(float p_value);                        \
	float get_##m_name() const;
#define EDEN_SE_U_VEC3(m_name, m_x, m_y, m_z, m_group) \
	void set_##m_name(const Vector3 &p_value);          \
	Vector3 get_##m_name() const;
#define EDEN_SE_U_COLOR(m_name, m_r, m_g, m_b, m_group) \
	void set_##m_name(const Color &p_value);             \
	Color get_##m_name() const;
#define EDEN_SE_L_FLOAT(m_name, m_default, m_hint, m_group) \
	void set_##m_name(float p_value);                        \
	float get_##m_name() const;
#define EDEN_SE_L_BOOL(m_name, m_default, m_group) \
	void set_##m_name(bool p_value);                \
	bool get_##m_name() const;
#define EDEN_SE_L_STRING(m_name, m_default, m_group) \
	void set_##m_name(const String &p_value);         \
	String get_##m_name() const;
#include "eden_space_environment_props.inc"
#undef EDEN_SE_U_FLOAT
#undef EDEN_SE_U_VEC3
#undef EDEN_SE_U_COLOR
#undef EDEN_SE_L_FLOAT
#undef EDEN_SE_L_BOOL
#undef EDEN_SE_L_STRING

protected:
	static void _bind_methods();
	void _notification(int p_what);

private:
	void _push_uniforms();
	EdenPlanetAtmosphere *_find_atmosphere() const;
	void _rebuild_index();
	void _apply_current();
	static Ref<ImageTexture> _make_placeholder_panorama();

	Ref<Texture2D> space_panorama;
	Ref<Texture2D> space_overlay;
	Ref<ImageTexture> placeholder_panorama;
	Vector<String> panorama_paths;
	Vector<String> panorama_names;
	int panorama_index = 0;
	// Signature the index was last built from (space_texture_dir + use_placeholder_panorama), so
	// a property-change setter can request a rebuild cheaply instead of unconditionally rescanning
	// the directory every frame.
	String index_signature;

	// --- Generated members -----------------------------------------------------------------
#define EDEN_SE_U_FLOAT(m_name, m_default, m_hint, m_group) float m_name = m_default;
#define EDEN_SE_U_VEC3(m_name, m_x, m_y, m_z, m_group) Vector3 m_name = Vector3(m_x, m_y, m_z);
#define EDEN_SE_U_COLOR(m_name, m_r, m_g, m_b, m_group) Color m_name = Color(m_r, m_g, m_b);
#define EDEN_SE_L_FLOAT(m_name, m_default, m_hint, m_group) float m_name = m_default;
#define EDEN_SE_L_BOOL(m_name, m_default, m_group) bool m_name = m_default;
#define EDEN_SE_L_STRING(m_name, m_default, m_group) String m_name = m_default;
#include "eden_space_environment_props.inc"
#undef EDEN_SE_U_FLOAT
#undef EDEN_SE_U_VEC3
#undef EDEN_SE_U_COLOR
#undef EDEN_SE_L_FLOAT
#undef EDEN_SE_L_BOOL
#undef EDEN_SE_L_STRING
};

#endif // EDEN_SPACE_ENVIRONMENT_H
