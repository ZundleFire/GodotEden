#include "eden_cloud_shell.h"
#include "eden_material_util.h"

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
			rebuild_weather();
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
	// A new material starts with none of the baked textures bound, so every bake must re-push.
	noise_signature = String();
	weather_signature = String();
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

	// Seamless, because the shader samples with repeat wrapping. Baked synchronously rather than
	// through NoiseTexture3D so the values can be equalised before upload.
	const int res = CLAMP(noise_resolution, 16, 256);
	TypedArray<Image> layers = fnl->get_seamless_image_3d(res, res, res, false, 0.1, true);
	ERR_FAIL_COND(layers.size() != res);

	// Histogram equalisation: remap every value to its rank, so the field is uniform on 0..1.
	// fbm clusters around the midpoint, which made cloud_coverage hypersensitive -- a 0.1 change swung
	// the planet from clear to overcast. Uniform, the threshold 1 - coverage covers exactly
	// `coverage` of the volume, so coverage and the weather map's deltas mean fractions of sky.
	uint64_t histogram[256] = {};
	Vector<Ref<Image>> slices;
	slices.resize(res);
	for (int z = 0; z < res; z++) {
		Ref<Image> img = layers[z];
		img->convert(Image::FORMAT_L8);
		slices.write[z] = img;
		for (const uint8_t v : img->get_data()) {
			histogram[v]++;
		}
	}
	uint8_t remap[256];
	const double total = (double)res * res * res;
	uint64_t below = 0;
	for (int v = 0; v < 256; v++) {
		// Midpoint of the bin's rank range.
		remap[v] = (uint8_t)CLAMP((int)Math::round(((double)below + histogram[v] * 0.5) / total * 255.0), 0, 255);
		below += histogram[v];
	}
	for (int z = 0; z < res; z++) {
		Vector<uint8_t> bytes = slices[z]->get_data();
		for (uint8_t &v : bytes) {
			v = remap[v];
		}
		slices.write[z] = Image::create_from_data(res, res, false, Image::FORMAT_L8, bytes);
	}
	noise_texture.instantiate();
	noise_texture->create(Image::FORMAT_L8, res, res, res, false, slices);

	eden_set_param(material, SNAME("cloud_noise"), noise_texture);
	noise_signature = signature;
}

// ===========================================================================================
// Weather: global circulation + planet data, baked to a cubemap
// ===========================================================================================
// Standard cube map face order (+X, -X, +Y, -Y, +Z, -Z) and orientation, at texel centres with image
// y pointing down. Verified against the renderer with debug_direction_cubemap().
Vector3 EdenCloudShell::_cube_texel_dir(int p_face, int p_x, int p_y, int p_size) {
	const float s = 2.0f * ((float)p_x + 0.5f) / (float)p_size - 1.0f;
	const float t = 2.0f * ((float)p_y + 0.5f) / (float)p_size - 1.0f;
	Vector3 d;
	switch (p_face) {
		case 0:
			d = Vector3(1.0f, -t, -s);
			break;
		case 1:
			d = Vector3(-1.0f, -t, s);
			break;
		case 2:
			d = Vector3(s, 1.0f, t);
			break;
		case 3:
			d = Vector3(s, -1.0f, -t);
			break;
		case 4:
			d = Vector3(s, -t, 1.0f);
			break;
		default:
			d = Vector3(-s, -t, -1.0f);
			break;
	}
	return d.normalized();
}

Ref<Cubemap> EdenCloudShell::debug_direction_cubemap(int p_size) {
	const int n = CLAMP(p_size, 2, 512);
	Vector<Ref<Image>> faces;
	for (int f = 0; f < 6; f++) {
		Ref<Image> img = Image::create_empty(n, n, false, Image::FORMAT_RGBF);
		for (int y = 0; y < n; y++) {
			for (int x = 0; x < n; x++) {
				const Vector3 d = _cube_texel_dir(f, x, y, n) * 0.5f + Vector3(0.5f, 0.5f, 0.5f);
				img->set_pixel(x, y, Color(d.x, d.y, d.z));
			}
		}
		faces.push_back(img);
	}
	Ref<Cubemap> cube;
	cube.instantiate();
	cube->create_from_images(faces);
	return cube;
}

// Bilinear lookup in EdenPlanetGeneratorV3's equirect layout: lat = (0.5 - v) * PI, lon = (u - 0.5)
// * TAU, dir = (cos lat cos lon, sin lat, cos lat sin lon). Deliberately NOT Godot's panorama
// convention -- the generator's images use their own, and sampling them any other way puts every
// continent's weather in the wrong place.
static Color _sample_v3_equirect(const Ref<Image> &p_img, const Vector3 &p_dir) {
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
		return p_img->get_pixel(x, y);
	};
	const Color top = px(x0, y0).lerp(px(x0 + 1, y0), ax);
	const Color bottom = px(x0, y0 + 1).lerp(px(x0 + 1, y0 + 1), ax);
	return top.lerp(bottom, ay);
}

EdenCloudShell::WeatherSample EdenCloudShell::_sample_weather(const Vector3 &p_dir) const {
	Vector3 axis = cloud_wind_axis.normalized();
	if (axis.length_squared() < 0.5f) {
		axis = Vector3(0, 1, 0);
	}
	const float sin_lat = CLAMP(p_dir.dot(axis), -1.0f, 1.0f);
	const float abs_lat = Math::asin(Math::abs(sin_lat));

	WeatherSample s;
	Vector3 east = axis.cross(p_dir);
	const float east_len = east.length();
	Vector3 north;
	if (east_len > 1e-4f) {
		east /= east_len;
		north = p_dir.cross(east); // toward the +axis pole
		const Vector3 poleward = north * SIGN(sin_lat);
		// Three-cell circulation. Both components follow -sin(6 * |lat|): easterly and equatorward
		// from 0-30 degrees (trade winds), westerly and poleward from 30-60 (westerlies), easterly and
		// equatorward from 60-90 (polar easterlies). It is zero at the equator (the doldrums) and at
		// the poles, which also removes the polar pinwheel the old rigid-rotation drift produced.
		const float band = -Math::sin(6.0f * abs_lat);
		s.wind = east * (band * wind_zonal_strength) + poleward * (band * wind_meridional_strength);
	}

	// Latitude cloud belts: cos(6 * |lat|) is +1 at the equator (convergence zone), -1 at 30 degrees
	// (subtropical highs -- the desert belt), +1 at 60 (storm tracks) and -1 at the poles.
	float delta = Math::cos(6.0f * abs_lat) * 0.25f * coverage_band_strength;

	if (weather_source_image.is_valid() && !weather_source_image->is_empty()) {
		const int hc = CLAMP(weather_humidity_channel, 0, 3);
		const int uc = CLAMP(weather_uplift_channel, 0, 3);
		const int rc = CLAMP(weather_rain_shadow_channel, 0, 3);
		const Color c = _sample_v3_equirect(weather_source_image, p_dir);
		const float humidity = c[hc];
		const float uplift = c[uc];
		const float rain_shadow = c[rc];

		delta += (humidity - 0.5f) * 0.5f * coverage_humidity_strength;
		delta += MAX(uplift, 0.0f) * 0.4f * coverage_uplift_strength;
		delta -= rain_shadow * 0.5f * coverage_rain_shadow_strength;

		if (wind_terrain_deflection > 0.0f && east_len > 1e-4f) {
			// Slope of the uplift field across the surface, by finite differences ~1.6 texels apart
			// at the usual 512-wide source.
			const float eps = 0.02f;
			const float ue = _sample_v3_equirect(weather_source_image, (p_dir + east * eps).normalized())[uc];
			const float un = _sample_v3_equirect(weather_source_image, (p_dir + north * eps).normalized())[uc];
			const Vector3 grad = east * ((ue - uplift) / eps) + north * ((un - uplift) / eps);
			const float grad_len = grad.length();
			if (grad_len > 1e-4f) {
				const Vector3 g = grad / grad_len;
				// Strip the uphill component so air is steered along a range instead of straight over
				// it, and slow it as the ground rises.
				const float uphill = s.wind.dot(g);
				if (uphill > 0.0f) {
					s.wind -= g * (uphill * wind_terrain_deflection);
				}
				s.wind *= 1.0f - wind_terrain_deflection * 0.5f * CLAMP(uplift, 0.0f, 1.0f);
			}
		}
	}

	s.coverage_delta = delta;
	return s;
}

void EdenCloudShell::set_weather_source_image(const Ref<Image> &p_image) {
	weather_source_image = p_image;
}

Ref<Image> EdenCloudShell::get_weather_source_image() const {
	return weather_source_image;
}

Ref<Cubemap> EdenCloudShell::get_weather_cubemap() const {
	return weather_cubemap;
}

void EdenCloudShell::rebuild_weather() {
	if (material.is_null()) {
		return;
	}
	const String signature = vformat("%d|%d|%f|%f|%f|%f|%f|%f|%f|%d|%d|%d|%s|%d",
			weather_enabled ? 1 : 0, weather_resolution, wind_zonal_strength, wind_meridional_strength,
			wind_terrain_deflection, coverage_band_strength, coverage_humidity_strength, coverage_uplift_strength,
			coverage_rain_shadow_strength, weather_humidity_channel, weather_uplift_channel, weather_rain_shadow_channel,
			String(cloud_wind_axis), weather_source_image.is_valid() ? (int64_t)weather_source_image->get_instance_id() : (int64_t)0);
	if (signature == weather_signature) {
		return;
	}
	weather_signature = signature;
	eden_set_param(material, SNAME("weather_enabled"), weather_enabled);
	if (!weather_enabled) {
		return;
	}

	const int n = CLAMP(weather_resolution, 8, 256);
	Vector<Ref<Image>> faces;
	for (int f = 0; f < 6; f++) {
		Vector<uint8_t> bytes;
		bytes.resize(n * n * 4 * sizeof(float));
		float *w = (float *)bytes.ptrw();
		int i = 0;
		for (int y = 0; y < n; y++) {
			for (int x = 0; x < n; x++) {
				const WeatherSample s = _sample_weather(_cube_texel_dir(f, x, y, n));
				w[i++] = s.wind.x;
				w[i++] = s.wind.y;
				w[i++] = s.wind.z;
				w[i++] = s.coverage_delta;
			}
		}
		// Float, because the wind components are signed.
		faces.push_back(Image::create_from_data(n, n, false, Image::FORMAT_RGBAF, bytes));
	}
	weather_cubemap.instantiate();
	weather_cubemap->create_from_images(faces);
	eden_set_param(material, SNAME("weather_map"), weather_cubemap);
}

// ===========================================================================================
// Uniform push
// ===========================================================================================
void EdenCloudShell::_push_uniforms() {
	if (material.is_null()) {
		return;
	}
#define EDEN_CLOUD_U_FLOAT(m_name, m_default, m_hint, m_group) eden_set_param(material, SNAME(#m_name), m_name);
#define EDEN_CLOUD_U_INT(m_name, m_default, m_hint, m_group) eden_set_param(material, SNAME(#m_name), m_name);
#define EDEN_CLOUD_U_VEC3(m_name, m_x, m_y, m_z, m_group) eden_set_param(material, SNAME(#m_name), m_name);
#define EDEN_CLOUD_U_COLOR(m_name, m_r, m_g, m_b, m_group) eden_set_param(material, SNAME(#m_name), m_name);
#define EDEN_CLOUD_L_FLOAT(m_name, m_default, m_hint, m_group)
#define EDEN_CLOUD_L_INT(m_name, m_default, m_hint, m_group)
#define EDEN_CLOUD_L_VEC3(m_name, m_x, m_y, m_z, m_group)
#define EDEN_CLOUD_L_BOOL(m_name, m_default, m_group)
#include "eden_cloud_shell_props.inc"
#undef EDEN_CLOUD_L_BOOL
#undef EDEN_CLOUD_U_FLOAT
#undef EDEN_CLOUD_U_INT
#undef EDEN_CLOUD_U_VEC3
#undef EDEN_CLOUD_U_COLOR
#undef EDEN_CLOUD_L_FLOAT
#undef EDEN_CLOUD_L_INT
#undef EDEN_CLOUD_L_VEC3

	// Pushed as a fallback for standalone use. When an EdenPlanetAtmosphere has this material
	// linked, it overwrites these each frame and is authoritative -- see the props table.
	eden_set_param(material, SNAME("planet_radius"), planet_radius);
	eden_set_param(material, SNAME("planet_center"), planet_center);
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
#define EDEN_CLOUD_L_BOOL(m_name, m_default, m_group) EDEN_CLOUD_DEF_SCALAR(bool, m_name)
#include "eden_cloud_shell_props.inc"
#undef EDEN_CLOUD_L_BOOL
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
#define EDEN_CLOUD_L_BOOL(m_name, m_default, m_group) EDEN_BIND_ACCESSORS(m_name)
#include "eden_cloud_shell_props.inc"
#undef EDEN_CLOUD_L_BOOL
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
	ClassDB::bind_method(D_METHOD("set_weather_source_image", "image"), &EdenCloudShell::set_weather_source_image);
	ClassDB::bind_method(D_METHOD("get_weather_source_image"), &EdenCloudShell::get_weather_source_image);
	ClassDB::bind_method(D_METHOD("get_weather_cubemap"), &EdenCloudShell::get_weather_cubemap);
	ClassDB::bind_method(D_METHOD("rebuild_weather"), &EdenCloudShell::rebuild_weather);
	ClassDB::bind_static_method("EdenCloudShell", D_METHOD("debug_direction_cubemap", "size"), &EdenCloudShell::debug_direction_cubemap);

	ADD_GROUP("Weather Source", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "weather_source_image", PROPERTY_HINT_RESOURCE_TYPE, "Image"),
			"set_weather_source_image", "get_weather_source_image");

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
#define EDEN_CLOUD_L_BOOL(m_name, m_default, m_group) \
	ADD_GROUP(m_group, "");                       \
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, #m_name), "set_" #m_name, "get_" #m_name);
#include "eden_cloud_shell_props.inc"
#undef EDEN_CLOUD_L_BOOL
#undef EDEN_CLOUD_U_FLOAT
#undef EDEN_CLOUD_U_INT
#undef EDEN_CLOUD_U_VEC3
#undef EDEN_CLOUD_U_COLOR
#undef EDEN_CLOUD_L_FLOAT
#undef EDEN_CLOUD_L_INT
#undef EDEN_CLOUD_L_VEC3
#undef EDEN_BIND_ACCESSORS
}
