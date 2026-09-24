#include "eden_space_environment.h"
#include "eden_material_util.h"

#include "core/io/dir_access.h"
#include "core/io/resource_loader.h"
#include "core/math/math_funcs.h"
#include "eden_planet_atmosphere.h"

EdenSpaceEnvironment::EdenSpaceEnvironment() {
	set_process(true);
}

// ===========================================================================================
// Lifecycle
// ===========================================================================================
void EdenSpaceEnvironment::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			_rebuild_index();
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

EdenPlanetAtmosphere *EdenSpaceEnvironment::_find_atmosphere() const {
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

// ===========================================================================================
// Panorama browsing -- ported from atmosphere_demo.gd's _build_space_textures()/
// _apply_space_texture(), so a project needs no script to get folder-of-panoramas browsing.
// ===========================================================================================
void EdenSpaceEnvironment::_rebuild_index() {
	panorama_paths.clear();
	panorama_names.clear();

	if (!space_texture_dir.is_empty()) {
		Error err = OK;
		Ref<DirAccess> dir = DirAccess::open(space_texture_dir, &err);
		if (dir.is_null()) {
			WARN_PRINT(vformat("EdenSpaceEnvironment: space_texture_dir '%s' could not be opened", space_texture_dir));
		} else {
			// Scanning only, never loading here: these are commonly 16K x 8K HDR panoramas, ~half
			// a gigabyte of VRAM each -- loading a folder of twenty up front asks for ~10 GB and
			// simply fails to become resident.
			PackedStringArray files = dir->get_files();
			Vector<String> sorted;
			for (int i = 0; i < files.size(); i++) {
				sorted.push_back(files[i]);
			}
			sorted.sort();
			static const char *ALLOWED_EXT[] = { "png", "jpg", "jpeg", "webp", "exr", "hdr", "ktx", "dds" };
			for (const String &f : sorted) {
				// An exported build reports the imported name; strip .import and de-duplicate so
				// both cases yield the source path exactly once.
				String name = f.trim_suffix(".import");
				String ext = name.get_extension().to_lower();
				bool allowed = false;
				for (const char *candidate : ALLOWED_EXT) {
					if (ext == candidate) {
						allowed = true;
						break;
					}
				}
				if (!allowed) {
					continue;
				}
				String path = space_texture_dir.path_join(name);
				if (panorama_paths.find(path) != -1) {
					continue;
				}
				panorama_paths.push_back(path);
				panorama_names.push_back(name);
			}
			print_line(vformat("space panoramas indexed: %d in %s", panorama_paths.size(), space_texture_dir));
		}
	}

	if (panorama_paths.is_empty() && use_placeholder_panorama) {
		if (placeholder_panorama.is_null()) {
			placeholder_panorama = _make_placeholder_panorama();
		}
	} else {
		placeholder_panorama.unref();
	}

	panorama_index = 0;
	index_signature = vformat("%s|%d", space_texture_dir, use_placeholder_panorama ? 1 : 0);
	_apply_current();
}

void EdenSpaceEnvironment::_apply_current() {
	if (panorama_paths.is_empty()) {
		// Either the generated placeholder, or nothing at all -- in which case the shader falls
		// back to the procedural star field.
		space_panorama = placeholder_panorama;
		return;
	}

	panorama_index = ((panorama_index % panorama_paths.size()) + panorama_paths.size()) % panorama_paths.size();
	const String path = panorama_paths[panorama_index];

	// Release the old one first so peak usage is one texture, not two. Without this, switching
	// between two large panoramas briefly needs both resident.
	space_panorama.unref();

	if (!ResourceLoader::exists(path)) {
		WARN_PRINT(vformat("EdenSpaceEnvironment: panorama '%s' is not an importable resource -- skipping", path));
		return;
	}
	Ref<Texture2D> tex = ResourceLoader::load(path, "Texture2D", ResourceFormatLoader::CACHE_MODE_IGNORE);
	if (tex.is_null()) {
		WARN_PRINT(vformat("EdenSpaceEnvironment: panorama '%s' failed to load", path));
		return;
	}
	space_panorama = tex;
	print_line(vformat("panorama %d/%d: %s (%dx%d)", panorama_index + 1, panorama_paths.size(),
			panorama_names[panorama_index], tex->get_width(), tex->get_height()));
}

void EdenSpaceEnvironment::rescan_panoramas() {
	_rebuild_index();
}

void EdenSpaceEnvironment::next_panorama() {
	if (panorama_paths.is_empty()) {
		return;
	}
	panorama_index++;
	_apply_current();
}

void EdenSpaceEnvironment::previous_panorama() {
	if (panorama_paths.is_empty()) {
		return;
	}
	panorama_index--;
	_apply_current();
}

String EdenSpaceEnvironment::get_panorama_label() const {
	if (panorama_paths.is_empty()) {
		return placeholder_panorama.is_valid() ? "<generated placeholder>" : "(none - procedural stars)";
	}
	return vformat("%d/%d  %s", panorama_index + 1, panorama_paths.size(), panorama_names[panorama_index]);
}

// Generated orientation and seam test panorama. Deliberately not just noise -- it makes the three
// things that actually go wrong with equirect mapping obvious at a glance: a bright GREEN meridian
// on the u=0 wrap (a broken seam shows as a line or mip-blur stripe there), RED/BLUE pole caps (a
// flipped or rotated mapping is immediately readable), and a sine "galaxy" band to judge rotation
// and distortion by.
Ref<ImageTexture> EdenSpaceEnvironment::_make_placeholder_panorama() {
	const int w = 1024;
	const int h = 512;
	Vector<uint8_t> data;
	data.resize(w * h * 3);
	uint8_t *dst = data.ptrw();

	// Cheap deterministic hash for the sparse stars -- this is a diagnostic texture, not gameplay
	// content, so bit-exact match to any particular RNG algorithm does not matter.
	auto hash01 = [](uint32_t x) -> float {
		x ^= x >> 16;
		x *= 0x7feb352dU;
		x ^= x >> 15;
		x *= 0x846ca68bU;
		x ^= x >> 16;
		return (float)(x % 100000) / 100000.0f;
	};

	for (int y = 0; y < h; y++) {
		const float v = (float(y) + 0.5f) / float(h);
		for (int x = 0; x < w; x++) {
			const float u = (float(x) + 0.5f) / float(w);
			float r = 0.02f, g = 0.02f, b = 0.04f;

			// Galaxy band: a sine ribbon with soft falloff.
			const float band_centre = 0.5f + 0.16f * Math::sin(u * Math::TAU * 1.0f);
			float band = CLAMP(1.0f - Math::abs(v - band_centre) / 0.13f, 0.0f, 1.0f);
			band = band * band;
			r += band * 0.45f;
			g += band * 0.30f;
			b += band * 0.55f;

			// 15-degree graticule.
			float grid = 0.0f;
			if (Math::fmod(u * 24.0f, 1.0f) < 0.012f || Math::fmod(v * 12.0f, 1.0f) < 0.024f) {
				grid = 0.25f;
			}
			r += grid * 0.3f;
			g += grid * 0.9f;
			b += grid * 0.9f;

			// Seam marker straight down u = 0.
			if (u < 0.004f || u > 0.996f) {
				r += 0.1f;
				g += 0.85f;
				b += 0.1f;
			}

			// Pole caps: north red, south blue.
			if (v < 0.06f) {
				r += 0.7f;
				g += 0.1f;
				b += 0.1f;
			} else if (v > 0.94f) {
				r += 0.1f;
				g += 0.1f;
				b += 0.8f;
			}

			// Sparse stars.
			if (hash01((uint32_t)(y * w + x) * 2654435761U + 20260911U) < 0.0016f) {
				const float s = 0.5f + hash01((uint32_t)(y * w + x) * 2246822519U) * 0.5f;
				r += s;
				g += s;
				b += s;
			}

			const int i = (y * w + x) * 3;
			dst[i] = (uint8_t)(CLAMP(r, 0.0f, 1.0f) * 255.0f);
			dst[i + 1] = (uint8_t)(CLAMP(g, 0.0f, 1.0f) * 255.0f);
			dst[i + 2] = (uint8_t)(CLAMP(b, 0.0f, 1.0f) * 255.0f);
		}
	}

	Ref<Image> img = Image::create_from_data(w, h, false, Image::FORMAT_RGB8, data);
	return ImageTexture::create_from_image(img);
}

// ===========================================================================================
// Uniform push
// ===========================================================================================
void EdenSpaceEnvironment::_push_uniforms() {
	// A property setter can flip space_texture_dir/use_placeholder_panorama without going
	// through rescan_panoramas() -- catch that here instead of demanding every caller remember to.
	const String sig = vformat("%s|%d", space_texture_dir, use_placeholder_panorama ? 1 : 0);
	if (sig != index_signature) {
		_rebuild_index();
	}

	EdenPlanetAtmosphere *atmo = _find_atmosphere();
	Ref<ShaderMaterial> sky_material = atmo != nullptr ? atmo->get_sky_material() : Ref<ShaderMaterial>();
	if (sky_material.is_null()) {
		return;
	}

#define EDEN_SE_U_FLOAT(m_name, m_default, m_hint, m_group) eden_set_param(sky_material, SNAME(#m_name), m_name);
#define EDEN_SE_U_VEC3(m_name, m_x, m_y, m_z, m_group) eden_set_param(sky_material, SNAME(#m_name), m_name);
#define EDEN_SE_U_COLOR(m_name, m_r, m_g, m_b, m_group) eden_set_param(sky_material, SNAME(#m_name), m_name);
#define EDEN_SE_L_FLOAT(m_name, m_default, m_hint, m_group)
#define EDEN_SE_L_BOOL(m_name, m_default, m_group)
#define EDEN_SE_L_STRING(m_name, m_default, m_group)
#include "eden_space_environment_props.inc"
#undef EDEN_SE_U_FLOAT
#undef EDEN_SE_U_VEC3
#undef EDEN_SE_U_COLOR
#undef EDEN_SE_L_FLOAT
#undef EDEN_SE_L_BOOL
#undef EDEN_SE_L_STRING

	// Deep space. The _enabled flags let the shader skip the whole panorama path and fall back to
	// procedural stars, so an empty slot costs nothing rather than sampling a black texture.
	eden_set_param(sky_material, SNAME("space_panorama"), space_panorama);
	eden_set_param(sky_material, SNAME("space_panorama_enabled"), space_panorama.is_valid());
	eden_set_param(sky_material, SNAME("space_overlay"), space_overlay);
	eden_set_param(sky_material, SNAME("space_overlay_enabled"), space_overlay.is_valid());
	// Once real space art is loaded the procedural star field is usually unwanted -- it doubles up
	// the stars and, at full strength, visually drowns a panorama. This overrides the star_intensity
	// the table just pushed.
	if (space_panorama.is_valid() || space_overlay.is_valid()) {
		eden_set_param(sky_material, SNAME("star_intensity"), star_intensity_with_panorama);
	}

	// The celestial sphere is fixed while the planet turns, so the starfield tracks the same phase
	// the sun does -- needs the sibling atmosphere's current time of day, since this node keeps no
	// day/night clock of its own. Coupling at 0 pins it; the offset orients a galaxy band.
	const float sun_time_of_day = atmo != nullptr ? atmo->get_sun_time_of_day() : 0.0f;
	const float space_angle = (float)(Math::TAU * (double)sun_time_of_day * (double)space_day_coupling) +
			Math::deg_to_rad(space_rotation_offset_deg);
	eden_set_param(sky_material, SNAME("space_rotation_angle"), space_angle);
	// Overlay rides the same day-coupled rotation, plus its own fixed offset, so a nebula can sit
	// at its own angle instead of being locked to the base starfield's orientation.
	eden_set_param(sky_material, SNAME("space_overlay_rotation_angle"), space_angle + Math::deg_to_rad(space_overlay_rotation_offset_deg));
}

// ===========================================================================================
// Generated accessors
// ===========================================================================================
#define EDEN_SE_DEF_SCALAR(m_type, m_name)                      \
	void EdenSpaceEnvironment::set_##m_name(m_type p_value) {    \
		m_name = p_value;                                        \
	}                                                             \
	m_type EdenSpaceEnvironment::get_##m_name() const {           \
		return m_name;                                            \
	}
#define EDEN_SE_DEF_REF(m_type, m_name)                          \
	void EdenSpaceEnvironment::set_##m_name(const m_type &p_value) { \
		m_name = p_value;                                          \
	}                                                               \
	m_type EdenSpaceEnvironment::get_##m_name() const {             \
		return m_name;                                              \
	}

#define EDEN_SE_U_FLOAT(m_name, m_default, m_hint, m_group) EDEN_SE_DEF_SCALAR(float, m_name)
#define EDEN_SE_U_VEC3(m_name, m_x, m_y, m_z, m_group) EDEN_SE_DEF_REF(Vector3, m_name)
#define EDEN_SE_U_COLOR(m_name, m_r, m_g, m_b, m_group) EDEN_SE_DEF_REF(Color, m_name)
#define EDEN_SE_L_FLOAT(m_name, m_default, m_hint, m_group) EDEN_SE_DEF_SCALAR(float, m_name)
#define EDEN_SE_L_BOOL(m_name, m_default, m_group) EDEN_SE_DEF_SCALAR(bool, m_name)
#define EDEN_SE_L_STRING(m_name, m_default, m_group) EDEN_SE_DEF_REF(String, m_name)
#include "eden_space_environment_props.inc"
#undef EDEN_SE_U_FLOAT
#undef EDEN_SE_U_VEC3
#undef EDEN_SE_U_COLOR
#undef EDEN_SE_L_FLOAT
#undef EDEN_SE_L_BOOL
#undef EDEN_SE_L_STRING

void EdenSpaceEnvironment::set_space_panorama(const Ref<Texture2D> &p_texture) {
	space_panorama = p_texture;
}

Ref<Texture2D> EdenSpaceEnvironment::get_space_panorama() const {
	return space_panorama;
}

void EdenSpaceEnvironment::set_space_overlay(const Ref<Texture2D> &p_texture) {
	space_overlay = p_texture;
}

Ref<Texture2D> EdenSpaceEnvironment::get_space_overlay() const {
	return space_overlay;
}

// ===========================================================================================
// Bindings
// ===========================================================================================
void EdenSpaceEnvironment::_bind_methods() {
#define EDEN_SE_BIND_ACCESSORS(m_name)                                                                    \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &EdenSpaceEnvironment::set_##m_name);          \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &EdenSpaceEnvironment::get_##m_name);

#define EDEN_SE_U_FLOAT(m_name, m_default, m_hint, m_group) EDEN_SE_BIND_ACCESSORS(m_name)
#define EDEN_SE_U_VEC3(m_name, m_x, m_y, m_z, m_group) EDEN_SE_BIND_ACCESSORS(m_name)
#define EDEN_SE_U_COLOR(m_name, m_r, m_g, m_b, m_group) EDEN_SE_BIND_ACCESSORS(m_name)
#define EDEN_SE_L_FLOAT(m_name, m_default, m_hint, m_group) EDEN_SE_BIND_ACCESSORS(m_name)
#define EDEN_SE_L_BOOL(m_name, m_default, m_group) EDEN_SE_BIND_ACCESSORS(m_name)
#define EDEN_SE_L_STRING(m_name, m_default, m_group) EDEN_SE_BIND_ACCESSORS(m_name)
#include "eden_space_environment_props.inc"
#undef EDEN_SE_U_FLOAT
#undef EDEN_SE_U_VEC3
#undef EDEN_SE_U_COLOR
#undef EDEN_SE_L_FLOAT
#undef EDEN_SE_L_BOOL
#undef EDEN_SE_L_STRING

	ClassDB::bind_method(D_METHOD("set_space_panorama", "texture"), &EdenSpaceEnvironment::set_space_panorama);
	ClassDB::bind_method(D_METHOD("get_space_panorama"), &EdenSpaceEnvironment::get_space_panorama);
	ClassDB::bind_method(D_METHOD("set_space_overlay", "texture"), &EdenSpaceEnvironment::set_space_overlay);
	ClassDB::bind_method(D_METHOD("get_space_overlay"), &EdenSpaceEnvironment::get_space_overlay);
	ClassDB::bind_method(D_METHOD("rescan_panoramas"), &EdenSpaceEnvironment::rescan_panoramas);
	ClassDB::bind_method(D_METHOD("next_panorama"), &EdenSpaceEnvironment::next_panorama);
	ClassDB::bind_method(D_METHOD("previous_panorama"), &EdenSpaceEnvironment::previous_panorama);
	ClassDB::bind_method(D_METHOD("get_panorama_label"), &EdenSpaceEnvironment::get_panorama_label);

	ADD_GROUP("Panorama", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "space_panorama", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"),
			"set_space_panorama", "get_space_panorama");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "space_overlay", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"),
			"set_space_overlay", "get_space_overlay");

#define EDEN_SE_U_FLOAT(m_name, m_default, m_hint, m_group)                                                       \
	ADD_GROUP(m_group, "");                                                                                        \
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, #m_name, PROPERTY_HINT_RANGE, m_hint), "set_" #m_name, "get_" #m_name);
#define EDEN_SE_U_VEC3(m_name, m_x, m_y, m_z, m_group)  \
	ADD_GROUP(m_group, "");                              \
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, #m_name), "set_" #m_name, "get_" #m_name);
#define EDEN_SE_U_COLOR(m_name, m_r, m_g, m_b, m_group)                                                     \
	ADD_GROUP(m_group, "");                                                                                  \
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, #m_name, PROPERTY_HINT_COLOR_NO_ALPHA), "set_" #m_name, "get_" #m_name);
#define EDEN_SE_L_FLOAT(m_name, m_default, m_hint, m_group)                                                       \
	ADD_GROUP(m_group, "");                                                                                        \
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, #m_name, PROPERTY_HINT_RANGE, m_hint), "set_" #m_name, "get_" #m_name);
#define EDEN_SE_L_BOOL(m_name, m_default, m_group) \
	ADD_GROUP(m_group, "");                         \
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, #m_name), "set_" #m_name, "get_" #m_name);
#define EDEN_SE_L_STRING(m_name, m_default, m_group)                                                          \
	ADD_GROUP(m_group, "");                                                                                    \
	ADD_PROPERTY(PropertyInfo(Variant::STRING, #m_name, PROPERTY_HINT_DIR), "set_" #m_name, "get_" #m_name);
#include "eden_space_environment_props.inc"
#undef EDEN_SE_U_FLOAT
#undef EDEN_SE_U_VEC3
#undef EDEN_SE_U_COLOR
#undef EDEN_SE_L_FLOAT
#undef EDEN_SE_L_BOOL
#undef EDEN_SE_L_STRING
}
