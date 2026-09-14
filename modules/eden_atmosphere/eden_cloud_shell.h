#ifndef EDEN_CLOUD_SHELL_H
#define EDEN_CLOUD_SHELL_H

#include "modules/noise/fastnoise_lite.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"

// Stylised planetary clouds: a small stack of flat procedural shapes parallaxed through a
// spherical slab, rather than a true volumetric march.
//
// This node owns only the carrier geometry and the baked noise; all the shape and lighting work
// happens per-fragment in the shader from an analytic ray/slab intersection. The mesh is a plain
// sphere and its tessellation affects nothing but the silhouette seen from outside.
//
// Two decisions worth knowing:
//
//  - The cloud field is sampled on normalize(planet-relative position) -- a direction vector,
//    never a UV. That makes it seamless by construction on a sphere: no wrap seam, no pole
//    pinching, verified by inspecting the poles and equator from orbit.
//  - The field is baked into a seamless 3D texture rather than evaluated procedurally, with the
//    domain warp applied at bake time. That took the clouds from ~5-9ms to ~1.2-1.7ms at 1080p
//    on a GTX 750 Ti.
//
// Link get_material() into EdenPlanetAtmosphere::add_linked_material() so the clouds receive the
// same scattering uniforms as the sky -- that is what makes them redden with the sunset.
class EdenCloudShell : public Node3D {
	GDCLASS(EdenCloudShell, Node3D);

public:
	EdenCloudShell();

	// The ShaderMaterial, for handing to EdenPlanetAtmosphere::add_linked_material().
	Ref<ShaderMaterial> get_material();

	// Supply your own cloud shader; null uses the module's built-in.
	void set_cloud_shader_override(const Ref<Shader> &p_shader);
	Ref<Shader> get_cloud_shader_override() const;

	// Force a rebake of the noise texture. Normally automatic on property change.
	void rebuild_noise();

	// --- Weather ------------------------------------------------------------------------------
	// Planet data that drives regional coverage and steers the wind. Equirectangular, in the layout
	// EdenPlanetGeneratorV3 uses for its climate/macro images (lat = (0.5 - v) * PI, lon = (u - 0.5)
	// * TAU, pole on +Y). Null runs the analytic circulation model on its own.
	void set_weather_source_image(const Ref<Image> &p_image);
	Ref<Image> get_weather_source_image() const;
	// The baked weather cubemap: RGB world-space wind (tangent), A coverage delta.
	Ref<Cubemap> get_weather_cubemap() const;
	void rebuild_weather();

	// Verification helper: a cubemap whose texels hold their own direction encoded as 0.5 + 0.5 * dir,
	// built with the exact face table the weather baker uses. Sampling it with the true view direction
	// in a shader and comparing proves the table matches the renderer's cube convention.
	static Ref<Cubemap> debug_direction_cubemap(int p_size);

	// --- Generated accessors ---------------------------------------------------------------
#define EDEN_CLOUD_U_FLOAT(m_name, m_default, m_hint, m_group) \
	void set_##m_name(float p_value);                          \
	float get_##m_name() const;
#define EDEN_CLOUD_U_INT(m_name, m_default, m_hint, m_group) \
	void set_##m_name(int p_value);                          \
	int get_##m_name() const;
#define EDEN_CLOUD_U_VEC3(m_name, m_x, m_y, m_z, m_group) \
	void set_##m_name(const Vector3 &p_value);            \
	Vector3 get_##m_name() const;
#define EDEN_CLOUD_U_COLOR(m_name, m_r, m_g, m_b, m_group) \
	void set_##m_name(const Color &p_value);               \
	Color get_##m_name() const;
#define EDEN_CLOUD_L_FLOAT(m_name, m_default, m_hint, m_group) \
	void set_##m_name(float p_value);                          \
	float get_##m_name() const;
#define EDEN_CLOUD_L_INT(m_name, m_default, m_hint, m_group) \
	void set_##m_name(int p_value);                          \
	int get_##m_name() const;
#define EDEN_CLOUD_L_VEC3(m_name, m_x, m_y, m_z, m_group) \
	void set_##m_name(const Vector3 &p_value);            \
	Vector3 get_##m_name() const;
#define EDEN_CLOUD_L_BOOL(m_name, m_default, m_group) \
	void set_##m_name(bool p_value);                  \
	bool get_##m_name() const;
#include "eden_cloud_shell_props.inc"
#undef EDEN_CLOUD_L_BOOL
#undef EDEN_CLOUD_U_FLOAT
#undef EDEN_CLOUD_U_INT
#undef EDEN_CLOUD_U_VEC3
#undef EDEN_CLOUD_U_COLOR
#undef EDEN_CLOUD_L_FLOAT
#undef EDEN_CLOUD_L_INT
#undef EDEN_CLOUD_L_VEC3

protected:
	static void _bind_methods();
	void _notification(int p_what);

private:
	void _build_material();
	void _rebuild_mesh();
	void _push_uniforms();

	struct WeatherSample {
		Vector3 wind;
		float coverage_delta = 0.0f;
	};
	WeatherSample _sample_weather(const Vector3 &p_dir) const;
	static Vector3 _cube_texel_dir(int p_face, int p_x, int p_y, int p_size);

	MeshInstance3D *mesh_instance = nullptr;
	Ref<ShaderMaterial> material;
	Ref<ImageTexture3D> noise_texture;
	Ref<Shader> cloud_shader_override;
	// Bake settings the current texture was generated from; a mismatch triggers a rebake.
	String noise_signature;
	// Geometry inputs the current mesh was built from; lets _rebuild_mesh() be called every
	// frame and early-out, instead of tracking which setters affect geometry.
	String mesh_signature;

	Ref<Image> weather_source_image;
	Ref<Cubemap> weather_cubemap;
	String weather_signature;

	// --- Generated members -----------------------------------------------------------------
#define EDEN_CLOUD_U_FLOAT(m_name, m_default, m_hint, m_group) float m_name = m_default;
#define EDEN_CLOUD_U_INT(m_name, m_default, m_hint, m_group) int m_name = m_default;
#define EDEN_CLOUD_U_VEC3(m_name, m_x, m_y, m_z, m_group) Vector3 m_name = Vector3(m_x, m_y, m_z);
#define EDEN_CLOUD_U_COLOR(m_name, m_r, m_g, m_b, m_group) Color m_name = Color(m_r, m_g, m_b);
#define EDEN_CLOUD_L_FLOAT(m_name, m_default, m_hint, m_group) float m_name = m_default;
#define EDEN_CLOUD_L_INT(m_name, m_default, m_hint, m_group) int m_name = m_default;
#define EDEN_CLOUD_L_VEC3(m_name, m_x, m_y, m_z, m_group) Vector3 m_name = Vector3(m_x, m_y, m_z);
#define EDEN_CLOUD_L_BOOL(m_name, m_default, m_group) bool m_name = m_default;
#include "eden_cloud_shell_props.inc"
#undef EDEN_CLOUD_L_BOOL
#undef EDEN_CLOUD_U_FLOAT
#undef EDEN_CLOUD_U_INT
#undef EDEN_CLOUD_U_VEC3
#undef EDEN_CLOUD_U_COLOR
#undef EDEN_CLOUD_L_FLOAT
#undef EDEN_CLOUD_L_INT
#undef EDEN_CLOUD_L_VEC3
};

#endif // EDEN_CLOUD_SHELL_H
