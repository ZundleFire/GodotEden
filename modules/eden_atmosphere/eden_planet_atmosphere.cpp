#include "eden_planet_atmosphere.h"
#include "eden_material_util.h"

#include "core/config/engine.h"
#include "eden_atmosphere_shaders.gen.h"
#include "eden_cloud_shell.h"
#include "eden_planet_rings.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "scene/resources/compositor.h"
#include "scene/resources/environment.h"
#ifdef TOOLS_ENABLED
#include "editor/editor_interface.h"
#endif

// Earth reference values, duplicated from shaders/atmosphere_common.gdshaderinc. The two copies
// must stay in step -- that is the price of evaluating one model on both CPU and GPU.
static const Vector3 BETA_RAYLEIGH_EARTH = Vector3(5.8e-6, 13.5e-6, 33.1e-6); // m^-1, sea level
static const Vector3 EARTH_WAVELENGTHS = Vector3(680.0, 550.0, 440.0); // nm
static const double BETA_MIE_EARTH = 21e-6;
static const double EARTH_RAYLEIGH_SCALE_H = 8000.0;
static const double EARTH_MIE_SCALE_H = 1200.0;
static const double MIE_EXTINCTION_FACTOR = 1.1;
static const int SUN_STEPS = 4;

EdenPlanetAtmosphere::EdenPlanetAtmosphere() {
	set_process(true);
}

// ===========================================================================================
// Wiring
// ===========================================================================================
void EdenPlanetAtmosphere::set_sun_light_path(const NodePath &p_path) {
	sun_light_path = p_path;
	sun_light = nullptr;
	_resolve_nodes();
}

NodePath EdenPlanetAtmosphere::get_sun_light_path() const {
	return sun_light_path;
}

void EdenPlanetAtmosphere::set_moon_light_path(const NodePath &p_path) {
	moon_light_path = p_path;
	moon_light = nullptr;
	_resolve_nodes();
}

NodePath EdenPlanetAtmosphere::get_moon_light_path() const {
	return moon_light_path;
}

void EdenPlanetAtmosphere::set_sun2_light_path(const NodePath &p_path) {
	sun2_light_path = p_path;
	sun2_light = nullptr;
	_resolve_nodes();
}

NodePath EdenPlanetAtmosphere::get_sun2_light_path() const {
	return sun2_light_path;
}

void EdenPlanetAtmosphere::set_moonb_light_path(const NodePath &p_path) {
	moonb_light_path = p_path;
	moonb_light = nullptr;
	_resolve_nodes();
}

NodePath EdenPlanetAtmosphere::get_moonb_light_path() const {
	return moonb_light_path;
}

void EdenPlanetAtmosphere::set_environment_path(const NodePath &p_path) {
	// If this instance auto-created the WorldEnvironment it is about to stop using, free it rather
	// than leaving it alive with the post-process compositor effect still attached -- two live
	// WorldEnvironments both holding that effect is exactly what crashed the renderer before
	// auto-setup was moved off NOTIFICATION_READY (see the comment there); this is the same
	// protection for a path reassigned later, by a script or the inspector, instead of at ready.
	if (auto_created_environment && world_env != nullptr) {
		_detach_post_effect();
		world_env->queue_free();
	}
	auto_created_environment = false;
	environment_path = p_path;
	world_env = nullptr;
	env_configured = false;
	_resolve_nodes();
	_try_configure_environment();
}

NodePath EdenPlanetAtmosphere::get_environment_path() const {
	return environment_path;
}

void EdenPlanetAtmosphere::set_linked_materials(const TypedArray<ShaderMaterial> &p_materials) {
	linked_materials = p_materials;
}

TypedArray<ShaderMaterial> EdenPlanetAtmosphere::get_linked_materials() const {
	return linked_materials;
}

void EdenPlanetAtmosphere::add_linked_material(const Ref<ShaderMaterial> &p_material) {
	if (p_material.is_null()) {
		return;
	}
	if (linked_materials.find(p_material) == -1) {
		linked_materials.push_back(p_material);
	}
}

void EdenPlanetAtmosphere::remove_linked_material(const Ref<ShaderMaterial> &p_material) {
	const int idx = linked_materials.find(p_material);
	if (idx != -1) {
		linked_materials.remove_at(idx);
	}
}

// Materials of sibling clouds/rings found at runtime. Kept out of the exported linked_materials: added there, the
// editor saved each session's runtime materials into the scene, and they piled up (45 in _ocean_editor_probe.tscn,
// every one a full shader copy, all receiving the whole uniform push every frame).
void EdenPlanetAtmosphere::_auto_link(const Ref<ShaderMaterial> &p_material) {
	if (p_material.is_valid() && !auto_linked_materials.has(p_material) && linked_materials.find(p_material) == -1) {
		auto_linked_materials.push_back(p_material);
	}
}

void EdenPlanetAtmosphere::set_sky_shader_override(const Ref<Shader> &p_shader) {
	sky_shader_override = p_shader;
	// Force a rebuild so the swap takes effect immediately in the editor.
	sky_material.unref();
	sky.unref();
	env_configured = false;
	_build_sky();
	_try_configure_environment();
}

Ref<Shader> EdenPlanetAtmosphere::get_sky_shader_override() const {
	return sky_shader_override;
}

void EdenPlanetAtmosphere::set_moon_texture(const Ref<Texture2D> &p_texture) {
	moon_texture = p_texture;
}

Ref<Texture2D> EdenPlanetAtmosphere::get_moon_texture() const {
	return moon_texture;
}

void EdenPlanetAtmosphere::set_moonb_texture(const Ref<Texture2D> &p_texture) {
	moonb_texture = p_texture;
}

Ref<Texture2D> EdenPlanetAtmosphere::get_moonb_texture() const {
	return moonb_texture;
}

static float _luma(const Vector3 &p_v) {
	return p_v.x * 0.2126f + p_v.y * 0.7152f + p_v.z * 0.0722f;
}

Ref<EdenAtmospherePostEffect> EdenPlanetAtmosphere::get_post_effect() const {
	return post_effect;
}

void EdenPlanetAtmosphere::_attach_post_effect() {
	if (world_env == nullptr || post_effect.is_null()) {
		return;
	}
	Ref<Compositor> compositor = world_env->get_compositor();
	if (compositor.is_null()) {
		compositor.instantiate();
		world_env->set_compositor(compositor);
	}
	// Append rather than replace, so a project's own compositor effects keep running.
	TypedArray<CompositorEffect> effects = compositor->get_compositor_effects();
	if (effects.find(post_effect) == -1) {
		effects.push_back(post_effect);
		compositor->set_compositor_effects(effects);
	}
}

void EdenPlanetAtmosphere::_detach_post_effect() {
	if (world_env == nullptr || post_effect.is_null()) {
		return;
	}
	Ref<Compositor> compositor = world_env->get_compositor();
	if (compositor.is_null()) {
		return;
	}
	TypedArray<CompositorEffect> effects = compositor->get_compositor_effects();
	const int idx = effects.find(post_effect);
	if (idx != -1) {
		effects.remove_at(idx);
		compositor->set_compositor_effects(effects);
	}
}

void EdenPlanetAtmosphere::_update_post_effect() {
	if (!fog_enabled && !light_rays_enabled) {
		if (post_effect.is_valid()) {
			post_effect->set_enabled(false);
		}
		return;
	}
	if (post_effect.is_null()) {
		post_effect.instantiate();
	}
	post_effect->set_enabled(true);
	_attach_post_effect();

	EdenAtmospherePostEffect::FrameParams fp;
	fp.fog_enabled = fog_enabled;
	fp.rays_enabled = light_rays_enabled;
	fp.planet_center = planet_center;
	fp.planet_radius = planet_radius;

	fp.fog_density = fog_density;
	fp.fog_scale_height = MAX(fog_height_falloff, 1.0f);
	fp.fog_base_altitude = fog_base_altitude;
	// Eight scale heights up, density is e^-8 (~0.03%) of the base. Integrating any higher only
	// spreads the fixed sample budget over empty air.
	fp.fog_top = fp.fog_scale_height * 8.0f;
	fp.fog_sky_affect = fog_sky_affect;
	fp.fog_anisotropy = fog_anisotropy;
	fp.fog_directional = fog_sun_scatter;
	fp.fog_albedo = Vector3(fog_albedo.r, fog_albedo.g, fog_albedo.b);

	// Evaluated at the camera, like the DirectionalLight3Ds: the fog in view is lit by the same
	// extinguished sun and moon colour as the terrain it sits on.
	const Vector3 cam = _camera_planet_relative();
	const Vector3 t_sun = _transmittance_toward(cam, sun_direction);
	const Vector3 t_moon = _transmittance_toward(cam, moon_direction);
	const float sun_vis = _luma(t_sun);
	const float moon_bright = Math::pow(get_moon_illumination(), MAX(moon_light_phase_exponent, 0.0f));
	const Vector3 moon_col(moon_light_color.r, moon_light_color.g, moon_light_color.b);

	fp.sun_direction = sun_direction;
	fp.moon_direction = moon_direction;
	fp.sun_fog_light = t_sun * fog_sun_intensity;
	fp.moon_fog_light = Vector3(moon_col.x * t_moon.x, moon_col.y * t_moon.y, moon_col.z * t_moon.z) * (moon_bright * fog_moon_intensity);

	// Ambient tracks the sky in HUE as well as brightness. Previously fog_ambient_color was the
	// finished colour and only its brightness moved with the sun, so fog stayed the same authored
	// blue at noon, at sunset and from orbit -- the one time fog most obviously should not be blue
	// is the one time the old form could not express. Sampling the real sky the fog sits under
	// makes it go gold at sunrise/sunset, deep blue at dusk, and pale at altitude, for five CPU
	// scattering evaluations a frame (_push_ocean_sky already does 290 for the same integral).
	// fog_ambient_color is now a TINT on that, so the authored value still shapes the result.
	// Sampled at the camera, so it also tracks where the camera is, not just what time it is.
	const Vector3 up = cam.normalized();
	Vector3 horiz_a = up.cross(Vector3(0, 1, 0));
	if (horiz_a.length_squared() < 1e-6f) {
		horiz_a = up.cross(Vector3(1, 0, 0));
	}
	horiz_a.normalize();
	const Vector3 horiz_b = up.cross(horiz_a).normalized();
	// Up plus the four horizon quadrants: enough to pick up the sun's side of the sky being
	// brighter and warmer than the opposite side, which is the whole point at a low sun.
	Vector3 sky_amb = _sky_radiance(cam, up);
	sky_amb += _sky_radiance(cam, (up * 0.35f + horiz_a).normalized());
	sky_amb += _sky_radiance(cam, (up * 0.35f - horiz_a).normalized());
	sky_amb += _sky_radiance(cam, (up * 0.35f + horiz_b).normalized());
	sky_amb += _sky_radiance(cam, (up * 0.35f - horiz_b).normalized());
	sky_amb /= 5.0f;
	// Only the HUE comes from the sky; the brightness stays on the existing, already-tuned
	// day/night curve. Letting the sky drive brightness too made fog several times brighter than
	// before (sky radiance is post-sun_intensity, so it runs to a few units where the old constant
	// was ~0.4) and washed the whole frame milky -- confirmed against a --no-fog capture.
	const Vector3 tint(fog_ambient_color.r, fog_ambient_color.g, fog_ambient_color.b);
	const float amb_luma = _luma(sky_amb);
	const Vector3 sky_hue = amb_luma > 1e-5f ? sky_amb / amb_luma : Vector3(1, 1, 1);
	Vector3 hue(tint.x * sky_hue.x, tint.y * sky_hue.y, tint.z * sky_hue.z);
	hue /= MAX(_luma(hue), 1e-5f); // pure chromaticity, so the line below sets the level alone
	const float ambient = fog_ambient_night + fog_ambient_day * sun_vis;
	fp.fog_ambient = hue * (_luma(tint) * ambient);

	// Rays fade with the light's own visibility, so a set sun casts none. Moon rays additionally
	// wait for the sun to leave the sky -- by day the moon's surroundings are just bright sky.
	// Light shafts are light scattered by air, so they fade out as the camera climbs out of the
	// atmosphere. Without this, from orbit any bright panorama near the sun gets smeared across the
	// planet as fake rays.
	const float cam_altitude = cam.length() - planet_radius;
	const float air_t = CLAMP((cam_altitude - atmosphere_height * 0.35f) / MAX(atmosphere_height * 0.65f, 1.0f), 0.0f, 1.0f);
	const float air = 1.0f - air_t * air_t * (3.0f - 2.0f * air_t);
	fp.sun_ray_weight = sun_ray_intensity * sun_vis * air;
	fp.sun_ray_threshold = sun_ray_threshold;
	fp.sun_ray_tint = Vector3(sun_ray_tint.r, sun_ray_tint.g, sun_ray_tint.b);
	fp.moon_ray_weight = moon_ray_intensity * moon_bright * _luma(t_moon) * CLAMP(1.0f - sun_vis * 4.0f, 0.0f, 1.0f) * air;
	fp.moon_ray_threshold = moon_ray_threshold;
	fp.moon_ray_tint = Vector3(moon_ray_tint.r, moon_ray_tint.g, moon_ray_tint.b);
	fp.ray_samples = light_ray_samples;
	fp.ray_density = light_ray_density;
	fp.ray_decay = light_ray_decay;
	fp.ray_radius = light_ray_radius;
	fp.ray_sky_boost = light_ray_sky_boost;

	post_effect->set_frame_params(fp);
}

void EdenPlanetAtmosphere::_update_bloom() {
	if (!manage_glow || world_env == nullptr) {
		return;
	}
	Ref<Environment> env = world_env->get_environment();
	if (env.is_null()) {
		return;
	}
	// Only touch the Environment when something changed; its setters push to the RenderingServer.
	const String signature = vformat("%d|%f|%f|%f", (int64_t)env->get_instance_id(), bloom_intensity, bloom_threshold, bloom_strength);
	if (signature == bloom_signature) {
		return;
	}
	bloom_signature = signature;
	env->set_glow_enabled(bloom_intensity > 0.0f);
	env->set_glow_intensity(bloom_intensity);
	env->set_glow_strength(bloom_strength);
	env->set_glow_hdr_bleed_threshold(bloom_threshold);
	// Additive above the threshold. Godot's default soft-light mode brightens the whole frame,
	// which washes out the night sky instead of making the sun and moon bloom.
	env->set_glow_blend_mode(Environment::GLOW_BLEND_MODE_ADDITIVE);
}

Ref<ShaderMaterial> EdenPlanetAtmosphere::get_sky_material() const {
	return sky_material;
}

Ref<Sky> EdenPlanetAtmosphere::get_sky() const {
	return sky;
}

// ===========================================================================================
// Lifecycle
// ===========================================================================================
void EdenPlanetAtmosphere::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			_resolve_nodes();
			// Deliberately NOT called here: children finish _ready() before their parent does, so
			// for a node placed directly in a scene (rather than created by a script), this fires
			// before a parent's own _ready() has had a chance to set sun_light_path etc. itself.
			// Auto-creating here and then having the parent immediately repoint those paths left
			// two WorldEnvironments alive at once, each with the post-process effect attached --
			// harmless-looking but it crashed the renderer. The first NOTIFICATION_PROCESS tick,
			// below, runs after the WHOLE ready cascade (parents included) has finished, so by then
			// a script that wants to own the wiring already has -- auto-setup only fills in what is
			// still empty at that point.
			_build_sky();
			_try_configure_environment();
			set_process(true);
		} break;

		case NOTIFICATION_PROCESS: {
			const double delta = get_process_delta_time();
			if (day_length_seconds > 0.0) {
				// Advance the exported phase itself rather than a separate clock, so freezing
				// the cycle leaves the sun where it was and the inspector shows the true time.
				sun_time_of_day = Math::fposmod(sun_time_of_day + (float)(delta / day_length_seconds), 1.0f);
				if (advance_moon_phase && lunar_cycle_days > 0.0f) {
					// moon_phase is measured against the SUN (synodic), so one full cycle per
					// lunar_cycle_days days is exactly a phase cycle -- and the moon consequently
					// rises later each day by day_length / lunar_cycle_days, as Earth's does.
					moon_phase = Math::fposmod(moon_phase + (float)(delta / (day_length_seconds * lunar_cycle_days)), 1.0f);
				}
				if (moonb_advance_phase && moonb_lunar_cycle_days > 0.0f) {
					moonb_phase = Math::fposmod(moonb_phase + (float)(delta / (day_length_seconds * moonb_lunar_cycle_days)), 1.0f);
				}
			}
			if (sun_light == nullptr || world_env == nullptr || (moon_light == nullptr && !moon_light_path.is_empty()) ||
					(sun2_light == nullptr && !sun2_light_path.is_empty()) ||
					(moonb_light == nullptr && !moonb_light_path.is_empty())) {
				_resolve_nodes();
			}
			_collect_linked_nodes(delta);
			_auto_setup();
			_try_configure_environment();
			_update_sun_direction();
			_update_sun2_direction();
			_update_moon_direction();
			_update_moonb_direction();
			_push_uniforms();
			_update_sun_light();
			_update_sun2_light();
			_update_moon_light();
			_update_moonb_light();
			_update_post_effect();
			_update_bloom();
		} break;

		case NOTIFICATION_EXIT_TREE: {
			// Leaving the effect in the Compositor would keep fogging the scene after this node
			// is gone, driven by a parameter snapshot nobody updates any more.
			_detach_post_effect();
		} break;
	}
}

void EdenPlanetAtmosphere::_resolve_nodes() {
	if (!is_inside_tree()) {
		return;
	}
	if (!sun_light_path.is_empty()) {
		sun_light = Object::cast_to<DirectionalLight3D>(get_node_or_null(sun_light_path));
	}
	if (!moon_light_path.is_empty()) {
		moon_light = Object::cast_to<DirectionalLight3D>(get_node_or_null(moon_light_path));
	}
	if (!sun2_light_path.is_empty()) {
		sun2_light = Object::cast_to<DirectionalLight3D>(get_node_or_null(sun2_light_path));
	}
	if (!moonb_light_path.is_empty()) {
		moonb_light = Object::cast_to<DirectionalLight3D>(get_node_or_null(moonb_light_path));
	}
	if (!environment_path.is_empty()) {
		world_env = Object::cast_to<WorldEnvironment>(get_node_or_null(environment_path));
	}
}

// One helper per body so a light already resolved (non-empty path, or a path that has not
// resolved yet because its target has not entered the tree) is never second-guessed.
// Deliberately does NOT write r_path. The light this creates is an internal, unowned child --
// invisible to the scene tree dock and never saved with the scene -- but the exported NodePath
// property IS saved regardless of what it points to. Writing r_path here once produced a scene
// saved with e.g. sun_light_path = NodePath("Sun") pointing at a "Sun" that was never actually
// part of the saved scene: on the next load the path was non-empty (so auto-setup would not
// recreate anything) but dangling (so it resolved to nothing) -- silent, and it crashed the
// renderer. Leaving the property empty means auto-setup keeps recreating the same internal light
// every load, exactly as if nothing had ever been auto-created, however many times the scene is
// saved in between.
static DirectionalLight3D *_ensure_light(Node *p_owner, DirectionalLight3D *&r_light, const NodePath &r_path, const String &p_name) {
	// Keyed on the resolved POINTER, not the path string: a path that is set but does not resolve
	// (a stale reference to a node that no longer exists, e.g. from an old scene saved before this
	// fix) must not permanently block auto-setup from providing a working light -- see the crash
	// this caused when sun_light_path and moon_light_path were BOTH left dangling like that.
	if (r_light != nullptr) {
		return r_light;
	}
	DirectionalLight3D *light = memnew(DirectionalLight3D);
	light->set_name(p_name);
	p_owner->add_child(light, false, Node::INTERNAL_MODE_BACK);
	r_light = light;
	return light;
}

void EdenPlanetAtmosphere::_auto_setup() {
	if (!is_inside_tree()) {
		return;
	}

	if (auto_create_lights) {
		_ensure_light(this, sun_light, sun_light_path, "Sun");
		// Shadow set once at creation -- this runs every frame, and re-asserting it here used to
		// override anyone turning the moon's shadow off (and _update_moon_light's daylight switch).
		const bool had_moon = moon_light != nullptr;
		DirectionalLight3D *moon = _ensure_light(this, moon_light, moon_light_path, "Moon");
		if (!had_moon) {
			moon->set_shadow(true);
		}
		if (sun2_enabled) {
			_ensure_light(this, sun2_light, sun2_light_path, "Sun2");
		}
		if (moonb_enabled) {
			_ensure_light(this, moonb_light, moonb_light_path, "MoonB");
		}
	}

	if (auto_create_environment && world_env == nullptr) {
		// Also deliberately leaves environment_path empty -- see _ensure_light()'s comment above;
		// the same dangling-NodePath-survives-a-save problem applies here.
		WorldEnvironment *we = memnew(WorldEnvironment);
		we->set_name("Environment");
		add_child(we, false, INTERNAL_MODE_BACK);
		world_env = we;
		env_configured = false;
		auto_created_environment = true;
	}

	if (auto_create_planet_mesh) {
		const String sig = vformat("%f|%d|%d", planet_radius, (int)planet_mesh_segments, (int)planet_mesh_rings);
		if (auto_planet_mesh == nullptr) {
			auto_planet_mesh = memnew(MeshInstance3D);
			auto_planet_mesh->set_name("PlanetMesh");
			add_child(auto_planet_mesh, false, INTERNAL_MODE_BACK);
		}
		if (sig != auto_planet_mesh_signature) {
			auto_planet_mesh_signature = sig;
			Ref<SphereMesh> sphere;
			sphere.instantiate();
			sphere->set_radius(planet_radius);
			sphere->set_height(planet_radius * 2.0f);
			sphere->set_radial_segments(MAX((int)planet_mesh_segments, 8));
			sphere->set_rings(MAX((int)planet_mesh_rings, 4));
			auto_planet_mesh->set_mesh(sphere);
			if (auto_planet_mesh->get_material_override().is_null()) {
				// A simple lit placeholder -- enough to see the atmosphere's terminator, horizon and
				// ground colour against; swap in real terrain by pointing auto_create_planet_mesh off
				// and adding your own MeshInstance3D.
				Ref<StandardMaterial3D> mat;
				mat.instantiate();
				mat->set_albedo(Color(ground_albedo.r, ground_albedo.g, ground_albedo.b));
				mat->set_roughness(1.0f);
				auto_planet_mesh->set_material_override(mat);
			}
		}
		if (auto_planet_mesh->get_position() != planet_center) {
			auto_planet_mesh->set_position(planet_center);
		}
	}

	if (auto_create_cloud_shell && auto_cloud_shell == nullptr) {
		EdenCloudShell *cs = memnew(EdenCloudShell);
		cs->set_name("CloudShell");
		cs->set_planet_radius(planet_radius);
		cs->set_planet_center(planet_center);
		add_child(cs, false, INTERNAL_MODE_BACK);
		auto_cloud_shell = cs;
		_auto_link(cs->get_material());
	}

	// Auto-link any sibling EdenCloudShell / EdenPlanetRings -- clouds/rings placed as siblings
	// (the normal, script-free way to combine them with the atmosphere, exactly as the sample
	// scenes do) otherwise never receive sun_direction and the rest of the per-frame uniform push
	// at all, and fall back to their shader defaults -- including sun_direction = straight up,
	// which reads as "the northern hemisphere is permanently lit, the southern permanently dark,
	// and time of day does nothing" once you notice it. A script normally does this linking by
	// hand (add_linked_material()); without one -- in the editor, or any runtime scene that never
	// got that glue code written -- it silently never happened. Idempotent (add_linked_material()
	// already no-ops on a material already linked), so running this every frame just picks up
	// anything added to the scene later for free.
	// Also: the placeholder planet mesh sits exactly at sea level, so next to an EdenPlanetOcean it
	// z-fights the water and hides everything beneath it. Hidden while an ocean sibling exists
	// (looked up by class name so this module doesn't depend on eden_ocean).
	bool ocean_found = false;
	for (const ObjectID &id : linked_nodes) {
		Node *n = Object::cast_to<Node>(ObjectDB::get_instance(id));
		if (n == nullptr || !n->is_inside_tree()) {
			continue;
		}
		if (n->is_class("EdenPlanetOcean")) {
			ocean_found = true;
			_push_ocean_sky(n->call("get_material"));
		}
		EdenCloudShell *cs2 = Object::cast_to<EdenCloudShell>(n);
		if (cs2 != nullptr) {
			_auto_link(cs2->get_material());
		}
		EdenPlanetRings *pr = Object::cast_to<EdenPlanetRings>(n);
		if (pr != nullptr) {
			_auto_link(pr->get_material());
		}
	}
	if (auto_planet_mesh != nullptr && auto_planet_mesh->is_visible() == ocean_found) {
		auto_planet_mesh->set_visible(!ocean_found);
	}
}

// Finds the nodes that need this atmosphere's per-frame state but are not its children: an
// EdenPlanetOcean (which needs the baked sky radiance map and ambient cube for its own lighting),
// an EdenCloudShell and EdenPlanetRings (which need sun_direction and the scattering set).
//
// Deliberately the WHOLE tree, not just this node's siblings. The siblings-only version was the
// documented cause of the clouds' "north always lit" bug, and it had exactly the same effect one
// node type over: an ocean placed anywhere but directly beside the atmosphere never received
// sky_radiance_map at all, so sky_map_valid stayed false and the water fell back to the CONSTANT
// sky_fallback_color. Its direct sunlight still tracked the sun (that comes from the
// DirectionalLight3D, which is correct -- verified against sun_direction across declination and
// spin axis), but its sky reflection and ambient did not move at all, so the two disagreed more
// and more the further the sun got from wherever the fallback colour had been authored.
//
// Throttled rather than per-frame: find_children walks the entire scene, which is far too much to
// do every frame, while the cached ObjectIDs are pushed to every frame as before.
void EdenPlanetAtmosphere::_collect_linked_nodes(double p_delta) {
	linked_rescan -= p_delta;
	bool stale = linked_rescan <= 0.0;
	// Also rescan immediately if anything cached has died, so removing and re-adding a node does
	// not leave the scene unlit for up to the full interval.
	for (const ObjectID &id : linked_nodes) {
		Node *n = Object::cast_to<Node>(ObjectDB::get_instance(id));
		if (n == nullptr || !n->is_inside_tree()) {
			stale = true;
			break;
		}
	}
	if (!stale) {
		return;
	}
	linked_rescan = 2.0;
	linked_nodes.clear();
	// Relinked from linked_nodes every frame (see _auto_setup), so dropping them here forgets freed shells
	auto_linked_materials.clear();
	if (EdenCloudShell *cs = Object::cast_to<EdenCloudShell>(auto_cloud_shell)) {
		_auto_link(cs->get_material());
	}
	if (!is_inside_tree()) {
		return;
	}
	// owned = false: auto-created nodes are internal and unowned, and a node instanced at runtime
	// may have no owner either.
	static const char *classes[3] = { "EdenPlanetOcean", "EdenCloudShell", "EdenPlanetRings" };
	for (int c = 0; c < 3; c++) {
		TypedArray<Node> found = get_tree()->get_root()->find_children("*", classes[c], true, false);
		for (int i = 0; i < found.size(); i++) {
			Node *n = Object::cast_to<Node>(found[i]);
			if (n != nullptr) {
				linked_nodes.push_back(n->get_instance_id());
			}
		}
	}
}

// EdenPlanetOcean's fast sky lighting (ambient_light_disabled + its own sky term) needs what Godot's
// radiance cubemap would have given it: sky radiance seen from the camera. Evaluated here with the
// same CPU scattering integral that drives the clouds' sky_ambient_color, into a 24x12 world-space
// equirect (~290 samples, a few hundred microseconds), plus an ambient cube: cosine-weighted mean
// radiance around +-X/Y/Z. Pushed through the RenderingServer so none of it is saved into the scene
// with the ocean's material.
void EdenPlanetAtmosphere::_push_ocean_sky(const Ref<ShaderMaterial> &p_material) {
	if (p_material.is_null()) {
		return;
	}
	const int w = 24;
	const int h = 12;
	if (ocean_sky_image.is_null()) {
		ocean_sky_image = Image::create_empty(w, h, false, Image::FORMAT_RGBAH);
	}
	const Vector3 cam = _camera_planet_relative();
	static const Vector3 axes[6] = { Vector3(1, 0, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0), Vector3(0, -1, 0), Vector3(0, 0, 1), Vector3(0, 0, -1) };
	Vector3 amb[6];
	float amb_w[6] = {};
	for (int y = 0; y < h; y++) {
		const float theta = Math::PI * (y + 0.5f) / h; // from +Y, matching acos(d.y) / PI in the shader
		const float solid = Math::sin(theta); // equirect texel solid angle, up to a constant
		for (int x = 0; x < w; x++) {
			const float phi = Math::TAU * ((x + 0.5f) / w - 0.5f); // matching atan(d.z, d.x) / TAU + 0.5
			const Vector3 d(Math::sin(theta) * Math::cos(phi), Math::cos(theta), Math::sin(theta) * Math::sin(phi));
			Vector3 rad;
			if (d.dot(sun_direction) > 0.9f) {
				// A texel is ~15 degrees across. Point-sampled at its centre, the one holding the
				// sun takes the narrow forward-scattering peak's full value for its whole area and
				// reflects as a bright blob; box-filter the few texels near the sun instead.
				for (int sy = -1; sy <= 1; sy++) {
					for (int sx = -1; sx <= 1; sx++) {
						const float t2 = theta + sy * (Math::PI / h) / 3.0f;
						const float p2 = phi + sx * (Math::TAU / w) / 3.0f;
						rad += _sky_radiance(cam, Vector3(Math::sin(t2) * Math::cos(p2), Math::cos(t2), Math::sin(t2) * Math::sin(p2)));
					}
				}
				rad /= 9.0f;
			} else {
				rad = _sky_radiance(cam, d);
			}
			ocean_sky_image->set_pixel(x, y, Color(rad.x, rad.y, rad.z));
			for (int k = 0; k < 6; k++) {
				const float c = MAX(d.dot(axes[k]), 0.0f) * solid;
				amb[k] += rad * c;
				amb_w[k] += c;
			}
		}
	}
	if (ocean_sky_texture.is_null()) {
		ocean_sky_texture = ImageTexture::create_from_image(ocean_sky_image);
	} else {
		ocean_sky_texture->update(ocean_sky_image);
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	const RID rid = p_material->get_rid();
	rs->material_set_param(rid, "sky_radiance_map", ocean_sky_texture->get_rid());
	rs->material_set_param(rid, "sky_map_valid", true);
	static const char *names[6] = { "sky_ambient_px", "sky_ambient_nx", "sky_ambient_py", "sky_ambient_ny", "sky_ambient_pz", "sky_ambient_nz" };
	for (int k = 0; k < 6; k++) {
		rs->material_set_param(rid, names[k], amb[k] / MAX(amb_w[k], 1e-6f));
	}
}

void EdenPlanetAtmosphere::_build_sky() {
	if (sky_material.is_valid()) {
		return;
	}

	Ref<Shader> shader = sky_shader_override;
	if (shader.is_null()) {
		// Built in, so a game needs nothing but the engine binary -- no shader files to copy.
		shader.instantiate();
		shader->set_code(String::utf8(EDEN_SKY_SHADER_CODE));
	}

	sky_material.instantiate();
	sky_material->set_shader(shader);

	sky.instantiate();
	sky->set_material(sky_material);
	sky->set_radiance_size(Sky::RADIANCE_SIZE_128);
	// The scattering integral is not cheap and refreshing the radiance cubemap every frame is
	// wasted work when the sun barely moves. REALTIME only earns its cost with a running cycle.
	//
	// In the editor, always use REALTIME regardless of day_length_seconds: this demo runs frozen
	// (day_length_seconds == 0) by default, so INCREMENTAL is the normal case there, and its
	// gradual convergence takes far more real frames than a quick look in the editor's static
	// viewport gets -- confirmed this session via --capture: ambient-lit reflective surfaces
	// (the ocean's near-mirror material) read with wildly wrong day/night brightness for several
	// seconds after any sun_time_of_day change (including scene load) until the cubemap catches
	// up. Editor responsiveness matters more than the perf cost of a realtime sky there.
	bool editor_realtime = false;
#ifdef TOOLS_ENABLED
	editor_realtime = Engine::get_singleton()->is_editor_hint();
#endif
	sky->set_process_mode((day_length_seconds > 0.0 || editor_realtime) ? Sky::PROCESS_MODE_REALTIME : Sky::PROCESS_MODE_INCREMENTAL);
}

void EdenPlanetAtmosphere::_try_configure_environment() {
	if (env_configured || !manage_environment) {
		return;
	}
	if (world_env == nullptr || sky.is_null()) {
		return;
	}

	Ref<Environment> env = world_env->get_environment();
	if (env.is_null()) {
		env.instantiate();
		world_env->set_environment(env);
	}
	env->set_background(Environment::BG_SKY);
	env->set_sky(sky);
	if (manage_ambient) {
		// This is what makes ambient light follow the sky instead of a hand-picked constant.
		env->set_ambient_source(Environment::AMBIENT_SOURCE_SKY);
		env->set_ambient_light_sky_contribution(1.0);
		env->set_reflection_source(Environment::REFLECTION_SOURCE_SKY);
	}
	if (env->get_tonemapper() == Environment::TONE_MAPPER_LINEAR) {
		// Scattering output is HDR; linear tonemapping clips the sun and the horizon.
		// Filmic rather than ACES: the scattering integral hands the tonemapper exactly the case
		// ACES is worst at -- very bright, very saturated colour. ACES' RRT skews saturated reds
		// and oranges toward yellow and desaturates them toward white as luminance rises, so a
		// sunset sky at a luminance of a few units arrives on screen as pale cream and the sun disc
		// (sun_intensity * sun_disc_brightness, ~660) as a flat white blob with a bleached
		// surround. That is the "sunsets aren't coloured / the sun is overpowered" failure, and no
		// amount of tuning sun_intensity fixes it because the hue skew is in the transform, not the
		// input. Measured over the demo's capture poses, Filmic held the most saturation at the
		// horizon and lost the least of the night sky; AGX was a close second, ACES clearly last.
		//
		// Only a fallback: a scene that has already chosen a tonemapper keeps it, and the choice
		// matters less than Environment::tonemap_exposure, which this deliberately does not touch
		// -- exposure is a per-project art decision, and at the 1.0 default every curve here reads
		// flat and washed out.
		env->set_tonemapper(Environment::TONE_MAPPER_FILMIC);
	}
	env_configured = true;
}

// ===========================================================================================
// Sun
// ===========================================================================================
void EdenPlanetAtmosphere::_equatorial_basis(Vector3 &r_axis, Vector3 &r_a, Vector3 &r_b) const {
	r_axis = planet_spin_axis.normalized();
	if (!r_axis.is_finite() || r_axis.length_squared() < 0.5) {
		r_axis = Vector3(0, 1, 0);
	}
	r_a = r_axis.cross(Vector3(1, 0, 0));
	if (r_a.length_squared() < 1e-4) {
		r_a = r_axis.cross(Vector3(0, 0, -1));
	}
	r_a.normalize();
	r_b = r_axis.cross(r_a).normalized();
}

void EdenPlanetAtmosphere::_update_sun_direction() {
	// Rotating the sun about the spin axis is exactly what a planetary day is, which is why
	// this gives correct sunrise/sunset behaviour at every latitude with no special cases.
	Vector3 axis, ea, eb;
	_equatorial_basis(axis, ea, eb);
	const double dec = Math::deg_to_rad((double)sun_declination_deg);
	const double ang = Math::TAU * (double)sun_time_of_day;
	sun_direction = (ea * (float)(Math::cos(dec) * Math::cos(ang)) +
			eb * (float)(Math::cos(dec) * Math::sin(ang)) +
			axis * (float)Math::sin(dec))
							.normalized();
}

// Built in the planet's NON-rotating frame, then carried through the same daily rotation the sun
// gets. That split is what makes an orbit behave like Earth's moon:
//
//  - the orbit is a great circle whose plane is tilted from the equator by the inclination about
//    the line of nodes, so the body's height in the sky wanders month to month;
//  - phase is the angle round that orbit measured from the sun, so phase 0 is new (beside the
//    sun), 0.5 is full (opposite it), and eclipses only line up occasionally because the inclined
//    plane rarely passes exactly through the sun;
//  - the daily rotation is shared with the sun, so the body rises and sets like everything else in
//    the sky, while its slow phase advance makes it rise later each day.
//
// Shared by both moons so "two moons" is two real independent orbits, not a hand-copied second
// body -- give Moon B its own inclination/node/phase and it behaves exactly like a second Earth
// moon, on its own schedule.
void EdenPlanetAtmosphere::_compute_moon_orbit(float p_inclination_deg, float p_node_longitude_deg,
		float p_phase, Vector3 &r_direction, Vector3 &r_orbit_normal) const {
	Vector3 axis, ea, eb;
	_equatorial_basis(axis, ea, eb);

	const double dec = Math::deg_to_rad((double)sun_declination_deg);
	const Vector3 sun_inertial = (ea * (float)Math::cos(dec) + axis * (float)Math::sin(dec)).normalized();

	const double node = Math::deg_to_rad((double)p_node_longitude_deg);
	const Vector3 node_line = (ea * (float)Math::cos(node) + eb * (float)Math::sin(node)).normalized();
	const Vector3 normal_inertial = axis.rotated(node_line, Math::deg_to_rad(p_inclination_deg)).normalized();

	// Reference direction: the sun projected into the orbital plane. Rotating that within the
	// plane keeps the body on a true great circle; rotating the raw sun vector about the normal
	// would trace a cone instead and "full" would never be opposite the sun.
	Vector3 ref = sun_inertial - normal_inertial * sun_inertial.dot(normal_inertial);
	if (ref.length_squared() < 1e-8) {
		ref = node_line; // sun exactly on the orbit pole; any in-plane direction will do
	}
	ref.normalize();
	const Vector3 body_inertial = ref.rotated(normal_inertial, (float)(Math::TAU * (double)p_phase));

	const float day_angle = (float)(Math::TAU * (double)sun_time_of_day);
	r_direction = body_inertial.rotated(axis, day_angle).normalized();
	r_orbit_normal = normal_inertial.rotated(axis, day_angle).normalized();
}

void EdenPlanetAtmosphere::_update_moon_direction() {
	_compute_moon_orbit(moon_orbit_inclination_deg, moon_node_longitude_deg, moon_phase, moon_direction, moon_orbit_normal);
}

void EdenPlanetAtmosphere::_update_moonb_direction() {
	_compute_moon_orbit(moonb_orbit_inclination_deg, moonb_node_longitude_deg, moonb_phase, moonb_direction, moonb_orbit_normal);
}

Vector3 EdenPlanetAtmosphere::get_sun_direction() const {
	return sun_direction;
}

// Offset from the primary sun by its own azimuth/elevation, in a basis built from the primary
// direction -- "parked" near the main sun rather than needing a separate orbit.
void EdenPlanetAtmosphere::_update_sun2_direction() {
	Vector3 up_ref = planet_spin_axis.normalized();
	if (!up_ref.is_finite() || Math::abs(sun_direction.dot(up_ref)) > 0.99f) {
		up_ref = Vector3(1, 0, 0);
	}
	Vector3 right = up_ref.cross(sun_direction).normalized();
	Vector3 up = sun_direction.cross(right).normalized();
	sun2_direction = sun_direction
							 .rotated(right, Math::deg_to_rad(sun2_offset_elevation_deg))
							 .rotated(up, Math::deg_to_rad(sun2_offset_azimuth_deg))
							 .normalized();
}

Vector3 EdenPlanetAtmosphere::get_sun2_direction() const {
	return sun2_direction;
}

float EdenPlanetAtmosphere::get_moon_illumination() const {
	// Half the cosine of the sun-moon elongation: 0 when the moon sits beside the sun, 1 when
	// opposite. Rotation-invariant, so either frame gives the same answer.
	return CLAMP(0.5f * (1.0f - sun_direction.dot(moon_direction)), 0.0f, 1.0f);
}

float EdenPlanetAtmosphere::get_moonb_illumination() const {
	return CLAMP(0.5f * (1.0f - sun_direction.dot(moonb_direction)), 0.0f, 1.0f);
}

float EdenPlanetAtmosphere::phase_for_sun_elevation(const Vector3 &p_up, float p_elevation_deg, bool p_rising) const {
	Vector3 axis, ea, eb;
	_equatorial_basis(axis, ea, eb);
	const Vector3 up = p_up.normalized();
	const double dec = Math::deg_to_rad((double)sun_declination_deg);

	// sin(elevation) = dot(up, sun) = A*cos(ang) + B*sin(ang) + C, a single sinusoid
	// R*cos(ang - phi) + C, so it inverts in closed form.
	const double a = Math::cos(dec) * up.dot(ea);
	const double b = Math::cos(dec) * up.dot(eb);
	const double c = Math::sin(dec) * up.dot(axis);
	const double r = Math::sqrt(a * a + b * b);
	if (r < 1e-6) {
		// Degenerate at the poles: elevation equals the declination all day and never varies
		// with phase, so no phase produces a requested elevation.
		return -1.0f;
	}
	const double ratio = (Math::sin(Math::deg_to_rad((double)p_elevation_deg)) - c) / r;
	if (Math::abs(ratio) > 1.0) {
		return -1.0f;
	}
	const double phi = Math::atan2(b, a);
	const double offset = Math::acos(CLAMP(ratio, -1.0, 1.0));
	const double ang = p_rising ? (phi - offset) : (phi + offset);
	return (float)Math::fposmod(ang / Math::TAU, 1.0);
}

void EdenPlanetAtmosphere::_update_sun_light() {
	if (sun_light == nullptr || !drive_sun_light) {
		return;
	}

	if (drive_sun_rotation) {
		// DirectionalLight3D shines along its local -Z, so it looks toward the anti-sun point.
		Vector3 up_ref = planet_spin_axis.normalized();
		if (Math::abs(sun_direction.dot(up_ref)) > 0.99) {
			up_ref = Vector3(1, 0, 0);
		}
		const Vector3 origin = sun_light->get_global_position();
		sun_light->look_at_from_position(origin, origin - sun_direction, up_ref);
	}

	// Removed this session: this used to drive the light's colour/energy from CPU-computed
	// atmospheric transmittance (sunset reddening, night dimming) at some single reference point.
	// That was never supposed to touch terrain lighting at all -- the reddening/dimming effect
	// belongs entirely to the atmosphere itself (sky, clouds, fog), which already computes it
	// correctly and independently, per-pixel, via their own sun_transmittance() calls in the GPU
	// shaders (see atmosphere_common.gdshaderinc) -- based on how far that specific ray's light
	// actually travels through the atmosphere, exactly the real physical mechanism. Piping a
	// second, CPU-side, single-point version of the same idea into the one shared terrain light
	// was redundant with that, and outright wrong for it: no single reference point can represent
	// "how lit is the terrain in this view" for anything but a camera standing right on the
	// ground, which is exactly the bug repeatedly root-caused (and re-broken) this session. Plain
	// Lambertian N.L plus real-time shadows already light terrain correctly on their own, with a
	// constant, undistorted sun colour and energy -- no CPU help needed or wanted.
	// Atmospheric extinction on the terrain/water light. The removal noted above was right that a
	// single CPU reference point cannot describe terrain lighting for an ORBITAL camera -- but
	// pinning the light to full-strength white instead meant the ground and the ocean were lit at
	// noon intensity and noon colour at sunset, under an orange sky. A --capture pass showed it
	// plainly: sun_rgb=(1,1,1) energy=1.00 at all sixteen poses, midnight included, with the water
	// blown to white beneath a gold horizon.
	//
	// So: hue and level from the real transmittance at the camera, NORMALISED against the
	// transmittance straight up at the same altitude. That reference is what keeps this honest --
	// at a high sun the ratio is 1 and the light is exactly the unattenuated white it is today, so
	// nothing that was already tuned moves; it only departs from white as the sun nears the
	// horizon, which is the case the old code could not express at all.
	const Vector3 cam = _camera_planet_relative();
	const Vector3 up = cam.normalized();
	const Vector3 t_sun = _transmittance_toward(cam, sun_direction);
	const Vector3 t_ref = _transmittance_toward(cam, up, false); // sun overhead, no shadow term
	const float ref_luma = MAX(_luma(t_ref), 1e-4f);
	// Above the atmosphere there is no single ground point this can stand for (the camera sees day
	// and night at once), so fade back to plain white -- the documented orbital case.
	const float cam_altitude = cam.length() - planet_radius;
	const float a = CLAMP((cam_altitude - atmosphere_height * 0.35f) / MAX(atmosphere_height * 0.65f, 1.0f), 0.0f, 1.0f);
	const float in_air = 1.0f - a * a * (3.0f - 2.0f * a);

	const float scale = CLAMP(_luma(t_sun) / ref_luma, 0.0f, 1.0f);
	const float peak = MAX(t_sun.x, MAX(t_sun.y, t_sun.z));
	// Pure hue: divided by its own brightest channel, so `scale` alone sets the level.
	const Vector3 hue = peak > 1e-5f ? t_sun / peak : Vector3(1, 1, 1);
	sun_light->set_color(Color(
			Math::lerp(1.0f, hue.x, in_air),
			Math::lerp(1.0f, hue.y, in_air),
			Math::lerp(1.0f, hue.z, in_air)));
	sun_light->set_param(Light3D::PARAM_ENERGY, sun_light_energy * Math::lerp(1.0f, scale, in_air));
}

void EdenPlanetAtmosphere::_update_sun2_light() {
	if (sun2_light == nullptr || !sun2_enabled || !drive_sun2_light) {
		return;
	}

	if (drive_sun2_rotation) {
		Vector3 up_ref = planet_spin_axis.normalized();
		if (Math::abs(sun2_direction.dot(up_ref)) > 0.99f) {
			up_ref = Vector3(1, 0, 0);
		}
		const Vector3 origin = sun2_light->get_global_position();
		sun2_light->look_at_from_position(origin, origin - sun2_direction, up_ref);
	}

	// See _update_sun_light()'s comment: atmospheric reddening/dimming belongs to the atmosphere
	// shaders alone, not this terrain-lighting light. Plain constant colour/energy.
	sun2_light->set_color(Color(sun2_tint.r, sun2_tint.g, sun2_tint.b));
	sun2_light->set_param(Light3D::PARAM_ENERGY, sun2_light_energy);
	if (sun2_light->is_visible() != sun2_enabled) {
		sun2_light->set_visible(sun2_enabled);
	}
}

void EdenPlanetAtmosphere::_update_moon_light() {
	if (moon_light == nullptr || !drive_moon_light) {
		return;
	}

	if (drive_moon_rotation) {
		// Shines along its local -Z, so it looks toward the anti-moon point.
		Vector3 up_ref = moon_orbit_normal;
		if (Math::abs(moon_direction.dot(up_ref)) > 0.99f) {
			up_ref = Vector3(1, 0, 0);
		}
		const Vector3 origin = moon_light->get_global_position();
		moon_light->look_at_from_position(origin, origin - moon_direction, up_ref);
	}

	// Reflected light scales steeply with phase. Half moon is only ~10% of full on Earth (the
	// opposition surge), which a linear falloff badly overstates; the exponent is the knob. This
	// is real physics about the MOON itself (is it lit by the sun at all right now) -- unlike the
	// atmospheric extinction this function used to also apply (see _update_sun_light()'s comment
	// for why that was removed), phase is not an atmosphere effect and stays.
	const float phase_brightness = Math::pow(get_moon_illumination(), MAX(moon_light_phase_exponent, 0.0f));
	const float energy = moon_light_energy * moon_intensity * phase_brightness;

	moon_light->set_color(moon_light_color);
	moon_light->set_param(Light3D::PARAM_ENERGY, energy);

	if (moon_light_hide_when_dark) {
		// A second shadowed directional light is a whole extra shadow pass. Below the horizon, at
		// new moon or in daylight it contributes nothing, so switch it off rather than pay for it.
		bool lit = energy > 1e-3f;
		// Also off in daylight while the camera is inside the atmosphere: next to the sun it adds
		// nothing visible, but a second directional light is still evaluated on every lit pixel
		// (~0.8 ms at 1080p on a GTX 750 Ti). From orbit it stays on -- the night side is in view.
		// Auto-created moon only.
		if (moon_light_path.is_empty()) {
			const Vector3 cam = _camera_planet_relative();
			const bool day = sun_direction.dot(cam.normalized()) > 0.05f;
			if (day && cam.length() < planet_radius + atmosphere_height) {
				lit = false;
			}
		}
		if (moon_light->is_visible() != lit) {
			moon_light->set_visible(lit);
		}
		// In daylight moon shadows are invisible under the sun's, but still cost a whole extra
		// shadow pass plus a cascade lookup on every lit pixel (measured ~2.8 ms at 1080p on a GTX
		// 750 Ti). The light stays on -- from orbit it still lights the night side -- only its
		// shadow follows day/night at the camera. Auto-created moon only; a moon light you supplied
		// keeps whatever shadow setting you gave it.
		if (moon_light_path.is_empty()) {
			const float sun_elevation = sun_direction.dot(_camera_planet_relative().normalized());
			const bool night = sun_elevation < 0.05f;
			if (moon_light->has_shadow() != night) {
				moon_light->set_shadow(night);
			}
		}
	}
}

void EdenPlanetAtmosphere::_update_moonb_light() {
	if (moonb_light == nullptr || !moonb_enabled || !drive_moonb_light) {
		return;
	}

	if (drive_moonb_rotation) {
		Vector3 up_ref = moonb_orbit_normal;
		if (Math::abs(moonb_direction.dot(up_ref)) > 0.99f) {
			up_ref = Vector3(1, 0, 0);
		}
		const Vector3 origin = moonb_light->get_global_position();
		moonb_light->look_at_from_position(origin, origin - moonb_direction, up_ref);
	}

	// See _update_moon_light()'s comment: phase is real physics and stays; atmospheric extinction
	// does not belong to this terrain-lighting light and was removed.
	const float phase_brightness = Math::pow(get_moonb_illumination(), MAX(moonb_light_phase_exponent, 0.0f));
	const float energy = moonb_light_energy * moonb_intensity * phase_brightness;

	moonb_light->set_color(moonb_light_color);
	moonb_light->set_param(Light3D::PARAM_ENERGY, energy);

	if (moonb_light_hide_when_dark) {
		const bool lit = energy > 1e-3f;
		if (moonb_light->is_visible() != lit) {
			moonb_light->set_visible(lit);
		}
	} else if (moonb_light->is_visible() != moonb_enabled) {
		moonb_light->set_visible(moonb_enabled);
	}
}

Vector3 EdenPlanetAtmosphere::_camera_planet_relative() const {
	Vector3 world_pos = get_global_position();
	if (is_inside_tree()) {
		Viewport *vp = get_viewport();
		if (vp != nullptr) {
			Camera3D *cam = vp->get_camera_3d();
			if (cam != nullptr) {
				world_pos = cam->get_global_position();
			}
		}
	}
#ifdef TOOLS_ENABLED
	// Root-caused this session: a scene's own Camera3D (the FlyCamera atmosphere_demo.gd builds
	// at runtime, say) only exists while the scene is actually PLAYING. While just navigating the
	// editor's own static 3D viewport -- no Play, no running game -- get_viewport()->get_camera_3d()
	// above finds nothing, and this fell back to get_global_position(), this NODE's own static
	// position, completely disconnected from wherever the editor camera is actually looking. Every
	// lighting/shadow calculation driven from this function was therefore looking at the wrong
	// point the entire time someone was just orbiting the editor view -- not a bug in any of that
	// downstream math, a bug in what point it was ever being asked about. Falls back to the
	// editor's own 3D viewport camera specifically when the scene isn't running.
	if (Engine::get_singleton()->is_editor_hint()) {
		EditorInterface *ei = EditorInterface::get_singleton();
		if (ei != nullptr) {
			SubViewport *editor_vp = ei->get_editor_viewport_3d(0);
			if (editor_vp != nullptr) {
				Camera3D *editor_cam = editor_vp->get_camera_3d();
				if (editor_cam != nullptr) {
					world_pos = editor_cam->get_global_position();
				}
			}
		}
	}
#endif
	Vector3 p = world_pos - planet_center;
	// Guard the exactly-on-the-surface case: the shadow test degenerates when |p| == radius.
	const float r = p.length();
	if (r < planet_radius + 1.0f) {
		p = (p / MAX(r, 1e-3f)) * (planet_radius + 1.0f);
	}
	return p;
}

// ===========================================================================================
// CPU mirror of the shader scattering model
// ===========================================================================================
Vector3 EdenPlanetAtmosphere::_beta_rayleigh() const {
	// Rayleigh scattering goes as 1/lambda^4. Expressed as a ratio against the wavelengths the
	// Earth betas were measured at, so the default wavelengths reproduce them exactly.
	const Vector3 wl = rayleigh_wavelengths;
	const Vector3 ratio(EARTH_WAVELENGTHS.x / MAX(wl.x, 1.0f),
			EARTH_WAVELENGTHS.y / MAX(wl.y, 1.0f),
			EARTH_WAVELENGTHS.z / MAX(wl.z, 1.0f));
	const Vector3 spectral(ratio.x * ratio.x * ratio.x * ratio.x,
			ratio.y * ratio.y * ratio.y * ratio.y,
			ratio.z * ratio.z * ratio.z * ratio.z);
	const double scale = EARTH_RAYLEIGH_SCALE_H / MAX((double)rayleigh_scale_height, 1.0);
	return Vector3(BETA_RAYLEIGH_EARTH.x * spectral.x, BETA_RAYLEIGH_EARTH.y * spectral.y,
				   BETA_RAYLEIGH_EARTH.z * spectral.z) *
			(float)scale * rayleigh_strength;
}

float EdenPlanetAtmosphere::_beta_mie() const {
	return (float)(BETA_MIE_EARTH * (EARTH_MIE_SCALE_H / MAX((double)mie_scale_height, 1.0))) * mie_strength;
}

// Mirrors beta_ozone() / ozone_density() in atmosphere_common.gdshaderinc. Ozone absorbs and does
// not scatter, so it only ever appears in extinction, never in an in-scatter sum. See the shader
// for why the coefficients peak in green and what that fixes.
Vector3 EdenPlanetAtmosphere::_beta_ozone() const {
	static const Vector3 BETA_OZONE_EARTH(0.650e-6f, 1.881e-6f, 0.085e-6f);
	const float scale = (float)(EARTH_RAYLEIGH_SCALE_H / MAX((double)rayleigh_scale_height, 1.0));
	// rayleigh_strength included deliberately -- see beta_ozone() in atmosphere_common.gdshaderinc.
	return BETA_OZONE_EARTH * scale * rayleigh_strength * ozone_strength;
}

float EdenPlanetAtmosphere::_ozone_density(float p_altitude) const {
	const float peak = 3.125f * rayleigh_scale_height;
	const float width = 1.875f * MAX(rayleigh_scale_height, 1.0f);
	return MAX(0.0f, 1.0f - Math::abs(p_altitude - peak) / width);
}

Vector2 EdenPlanetAtmosphere::_ray_sphere(const Vector3 &p_ro, const Vector3 &p_rd, float p_radius) const {
	const float b = p_ro.dot(p_rd);
	const float c = p_ro.dot(p_ro) - p_radius * p_radius;
	float d = b * b - c;
	if (d < 0.0f) {
		return Vector2(1.0f, -1.0f); // miss
	}
	d = Math::sqrt(d);
	return Vector2(-b - d, -b + d);
}

float EdenPlanetAtmosphere::_planet_shadow_dir(const Vector3 &p_point, const Vector3 &p_dir) const {
	const float b = p_point.dot(p_dir);
	if (b >= 0.0f) {
		return 1.0f;
	}
	const float perp = Math::sqrt(MAX(p_point.dot(p_point) - b * b, 0.0f));
	const float w = MAX(shadow_softness, 1.0f);
	// Smooth, not binary: on the GPU that kills sunset banding, and here it makes lights fade
	// through the terminator instead of snapping off in one frame.
	const float lo = planet_radius - w;
	const float hi = planet_radius + w;
	const float t = CLAMP((perp - lo) / MAX(hi - lo, 1e-5f), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

Vector3 EdenPlanetAtmosphere::_optical_depth_along(const Vector3 &p_point, const Vector3 &p_dir) const {
	const Vector2 atm = _ray_sphere(p_point, p_dir, planet_radius + atmosphere_height);
	const float t_end = MAX(atm.y, 0.0f);
	if (t_end <= 0.0f) {
		return Vector3();
	}
	const float step = t_end / (float)SUN_STEPS;
	Vector3 depth; // (rayleigh, mie, ozone)
	float t = step * 0.5f;
	for (int i = 0; i < SUN_STEPS; i++) {
		const float h = MAX((p_point + p_dir * t).length() - planet_radius, 0.0f);
		depth.x += Math::exp(-h / rayleigh_scale_height) * step;
		depth.y += Math::exp(-h / mie_scale_height) * step;
		depth.z += _ozone_density(h) * step;
		t += step;
	}
	return depth;
}

Vector3 EdenPlanetAtmosphere::_transmittance_toward(const Vector3 &p_point, const Vector3 &p_dir, bool p_apply_shadow) const {
	const Vector3 depth = _optical_depth_along(p_point, p_dir);
	const Vector3 br = _beta_rayleigh();
	const float bm = _beta_mie() * (float)MIE_EXTINCTION_FACTOR;
	const Vector3 bo = _beta_ozone();
	const float shadow = p_apply_shadow ? _planet_shadow_dir(p_point, p_dir) : 1.0f;
	return Vector3(
				   Math::exp(-(br.x * depth.x + bm * depth.y + bo.x * depth.z)),
				   Math::exp(-(br.y * depth.x + bm * depth.y + bo.y * depth.z)),
				   Math::exp(-(br.z * depth.x + bm * depth.y + bo.z * depth.z))) *
			shadow;
}

Vector3 EdenPlanetAtmosphere::_sky_radiance(const Vector3 &p_point, const Vector3 &p_dir) const {
	// Coarse on purpose: this feeds one ambient colour per frame, not pixels.
	const Vector2 atm = _ray_sphere(p_point, p_dir, planet_radius + atmosphere_height);
	const float t_start = MAX(atm.x, 0.0f);
	float t_end = atm.y;
	if (t_end <= t_start) {
		return Vector3();
	}
	const Vector2 ground = _ray_sphere(p_point, p_dir, planet_radius);
	if (ground.y > ground.x && ground.x > 0.0f) {
		t_end = MIN(t_end, ground.x);
	}

	const Vector3 br = _beta_rayleigh();
	const float bm = _beta_mie();
	const float mu = p_dir.dot(sun_direction);
	const float ph_r = 0.0596831036f * (1.0f + mu * mu);
	const float g = mie_anisotropy;
	const float g2 = g * g;
	const float ph_m = (3.0f * (1.0f - g2)) / (2.0f * (2.0f + g2)) * (1.0f + mu * mu) /
			MAX(Math::pow(1.0f + g2 - 2.0f * g * mu, 1.5f), 1e-4f);

	const Vector3 bo = _beta_ozone();
	const int steps = 8;
	const float step = (t_end - t_start) / (float)steps;
	Vector3 view_depth; // (rayleigh, mie, ozone)
	Vector3 sum_r, sum_m;
	float t = t_start + step * 0.5f;
	for (int i = 0; i < steps; i++) {
		const Vector3 p = p_point + p_dir * t;
		const float h = MAX(p.length() - planet_radius, 0.0f);
		const float d_r = Math::exp(-h / rayleigh_scale_height) * step;
		const float d_m = Math::exp(-h / mie_scale_height) * step;
		// Ozone enters tau only -- no sum_* term, because it absorbs rather than scatters.
		view_depth += Vector3(d_r, d_m, _ozone_density(h) * step);
		const Vector3 sun_depth = _optical_depth_along(p, sun_direction);
		const Vector3 tau = br * (view_depth.x + sun_depth.x) +
				Vector3(1, 1, 1) * (bm * (float)MIE_EXTINCTION_FACTOR * (view_depth.y + sun_depth.y)) +
				bo * (view_depth.z + sun_depth.z);
		const Vector3 att = Vector3(Math::exp(-tau.x), Math::exp(-tau.y), Math::exp(-tau.z)) * _planet_shadow_dir(p, sun_direction);
		sum_r += att * d_r;
		sum_m += att * d_m;
		t += step;
	}
	const Vector3 rgb = Vector3(sum_r.x * br.x, sum_r.y * br.y, sum_r.z * br.z) * ph_r + sum_m * (bm * ph_m);
	return Vector3(rgb.x * sun_tint.r, rgb.y * sun_tint.g, rgb.z * sun_tint.b) * (sun_intensity * exposure);
}

float EdenPlanetAtmosphere::_planet_shadow(const Vector3 &p_point) const {
	return _planet_shadow_dir(p_point, sun_direction);
}

Vector3 EdenPlanetAtmosphere::_optical_depth_to_sun(const Vector3 &p_point) const {
	return _optical_depth_along(p_point, sun_direction);
}

Vector3 EdenPlanetAtmosphere::_sun_transmittance(const Vector3 &p_point) const {
	return _transmittance_toward(p_point, sun_direction);
}

Vector3 EdenPlanetAtmosphere::sun_transmittance_at(const Vector3 &p_planet_relative) const {
	return _sun_transmittance(p_planet_relative);
}

// ===========================================================================================
// Transmittance LUT
// ===========================================================================================
float EdenPlanetAtmosphere::_lut_cos_to_u(float p_cos) {
	// The sqrt concentrates texels near the horizon, where optical depth changes by orders of
	// magnitude over a couple of degrees. Mirrored by lut_cos_to_u() in the shader.
	return 0.5f + 0.5f * SIGN(p_cos) * Math::sqrt(Math::abs(p_cos));
}

float EdenPlanetAtmosphere::_lut_u_to_cos(float p_u) {
	const float t = 2.0f * p_u - 1.0f;
	return SIGN(t) * t * t;
}

void EdenPlanetAtmosphere::_bake_transmittance_lut() {
	if (!lut_enabled) {
		return;
	}
	// Valid because the atmosphere is spherically symmetric: the sun-ward integral depends only
	// on altitude and the sun's angle to local up, never on where the point is.
	// ozone_strength is deliberately NOT in the signature: the LUT stores raw densities, and the
	// beta multiply (which is where ozone_strength lands) happens at sample time.
	const String signature = vformat("%f|%f|%f|%f|%d|%d|%d", planet_radius, atmosphere_height,
			rayleigh_scale_height, mie_scale_height, lut_width, lut_height, lut_steps);
	if (signature == lut_signature && lut_texture.is_valid()) {
		return;
	}

	const int w = MAX(lut_width, 8);
	const int h = MAX(lut_height, 4);
	const int steps = MAX(lut_steps, 2);
	const float r_atm = planet_radius + atmosphere_height;

	// RGBH = four half floats; was RGH until ozone needed a third density channel. Depths reach
	// ~1.4e4 m, well inside half-float range, and its ~0.1% relative precision is far below
	// anything visible after the beta multiply.
	Ref<Image> img = Image::create_empty(w, h, false, Image::FORMAT_RGBAH);

	for (int yi = 0; yi < h; yi++) {
		const float alt = ((float)yi + 0.5f) / (float)h * atmosphere_height;
		const Vector3 p(0.0f, planet_radius + alt, 0.0f);
		for (int xi = 0; xi < w; xi++) {
			const float u = ((float)xi + 0.5f) / (float)w;
			const float c = _lut_u_to_cos(u);
			// Any sun direction with this cosine against local up will do; symmetry makes the
			// azimuth irrelevant.
			const Vector3 s(Math::sqrt(MAX(1.0f - c * c, 0.0f)), c, 0.0f);

			const Vector2 hit = _ray_sphere(p, s, r_atm);
			const float t_end = MAX(hit.y, 0.0f);
			Vector3 depth; // (rayleigh, mie, ozone)
			if (t_end > 0.0f) {
				const float step = t_end / (float)steps;
				float t = step * 0.5f;
				for (int i = 0; i < steps; i++) {
					const float alt_s = MAX((p + s * t).length() - planet_radius, 0.0f);
					depth.x += Math::exp(-alt_s / rayleigh_scale_height) * step;
					depth.y += Math::exp(-alt_s / mie_scale_height) * step;
					depth.z += _ozone_density(alt_s) * step;
					t += step;
				}
			}
			img->set_pixel(xi, yi, Color(depth.x, depth.y, depth.z, 1.0f));
		}
	}

	if (lut_texture.is_valid()) {
		lut_texture->update(img);
	} else {
		lut_texture = ImageTexture::create_from_image(img);
	}
	lut_signature = signature;
}

// ===========================================================================================
// Uniform push
// ===========================================================================================
void EdenPlanetAtmosphere::_set_on_all(const StringName &p_param, const Variant &p_value) {
	if (sky_material.is_valid()) {
		eden_set_param(sky_material, p_param, p_value);
	}
	for (int i = 0; i < linked_materials.size(); i++) {
		Ref<ShaderMaterial> mat = linked_materials[i];
		if (mat.is_valid()) {
			eden_set_param(mat, p_param, p_value);
		}
	}
	// Auto-linked clouds/rings push their own planet geometry every frame, after this node. Pushing ours too made the
	// two fight over it (39900 vs 40000 in _ocean_editor_probe.tscn), re-uploading both materials every frame; the
	// node's own value is what always ended up rendered, so leaving it to them changes nothing on screen.
	if (p_param == SNAME("planet_radius") || p_param == SNAME("planet_center")) {
		return;
	}
	for (const Ref<ShaderMaterial> &mat : auto_linked_materials) {
		eden_set_param(mat, p_param, p_value);
	}
}

void EdenPlanetAtmosphere::_push_uniforms() {
	// Pushed unconditionally. There was a dirty-flag fast path here in the prototype; it saved
	// about a dozen writes and cost a real bug -- any property changed without going through a
	// setter left the sky frozen on a stale sun direction while the CPU light kept updating, so
	// sky and terrain lighting silently disagreed.
#define EDEN_ATMO_U_FLOAT(m_name, m_default, m_hint, m_group) _set_on_all(SNAME(#m_name), m_name);
#define EDEN_ATMO_U_VEC3(m_name, m_x, m_y, m_z, m_group) _set_on_all(SNAME(#m_name), m_name);
#define EDEN_ATMO_U_COLOR(m_name, m_r, m_g, m_b, m_group) _set_on_all(SNAME(#m_name), m_name);
#define EDEN_ATMO_U_INT(m_name, m_default, m_hint, m_group) _set_on_all(SNAME(#m_name), m_name);
#define EDEN_ATMO_L_FLOAT(m_name, m_default, m_hint, m_group)
#define EDEN_ATMO_L_VEC3(m_name, m_x, m_y, m_z, m_group)
#define EDEN_ATMO_L_BOOL(m_name, m_default, m_group)
#define EDEN_ATMO_L_INT(m_name, m_default, m_hint, m_group)
#define EDEN_ATMO_L_STRING(m_name, m_default, m_group)
#include "eden_planet_atmosphere_props.inc"
#undef EDEN_ATMO_L_STRING
#undef EDEN_ATMO_U_FLOAT
#undef EDEN_ATMO_U_VEC3
#undef EDEN_ATMO_U_COLOR
#undef EDEN_ATMO_U_INT
#undef EDEN_ATMO_L_FLOAT
#undef EDEN_ATMO_L_VEC3
#undef EDEN_ATMO_L_BOOL
#undef EDEN_ATMO_L_INT

	_set_on_all(SNAME("sun_direction"), sun_direction);
	_set_on_all(SNAME("sun2_enabled"), sun2_enabled);
	_set_on_all(SNAME("sun2_direction"), sun2_direction);
	_set_on_all(SNAME("moon_orbit_normal"), moon_orbit_normal);
	// Sky colour straight up from the camera, so clouds can shade their shadowed faces with the
	// sky actually above them -- blue at noon, violet at dusk, black at night.
	{
		const Vector3 cam = _camera_planet_relative();
		_set_on_all(SNAME("sky_ambient_color"), _sky_radiance(cam, cam.normalized()));
	}
	_set_on_all(SNAME("moon_illumination"), get_moon_illumination());
	// Brightness with the phase curve applied, so moonlit clouds follow the same falloff as the
	// moon DirectionalLight3D rather than a hardcoded copy of it.
	_set_on_all(SNAME("moon_phase_brightness"), Math::pow(get_moon_illumination(), MAX(moon_light_phase_exponent, 0.0f)));
	_set_on_all(SNAME("moon_texture"), moon_texture);
	_set_on_all(SNAME("moon_texture_equirect"), moon_texture_equirect);
	_set_on_all(SNAME("moon_texture_rotation"), Math::deg_to_rad(moon_texture_rotation_deg));

	_set_on_all(SNAME("moonb_direction"), moonb_direction);
	_set_on_all(SNAME("moonb_orbit_normal"), moonb_orbit_normal);
	_set_on_all(SNAME("moonb_illumination"), get_moonb_illumination());
	_set_on_all(SNAME("moonb_phase_brightness"), Math::pow(get_moonb_illumination(), MAX(moonb_light_phase_exponent, 0.0f)));
	_set_on_all(SNAME("moonb_texture"), moonb_texture);
	_set_on_all(SNAME("moonb_texture_equirect"), moonb_texture_equirect);
	_set_on_all(SNAME("moonb_texture_rotation"), Math::deg_to_rad(moonb_texture_rotation_deg));
	_set_on_all(SNAME("moonb_enabled"), moonb_enabled);

	// (dither_resolution used to be pushed here for the sky's raymarch jitter. Removed: the sky
	// shader now reads FRAGCOORD directly, which Godot's sky shader stage actually provides -- no
	// CPU-guessed viewport size needed, and no risk of it disagreeing with whatever the sky is
	// really rendering into (an editor sub-viewport, a differently-sized game window, ...), which
	// was producing visible banding instead of the fine dither the jitter is meant to give.)

	_bake_transmittance_lut();
	_set_on_all(SNAME("transmittance_lut"), lut_texture);
	_set_on_all(SNAME("use_transmittance_lut"), lut_enabled && lut_texture.is_valid());
}

// ===========================================================================================
// Generated accessors
// ===========================================================================================
#define EDEN_ATMO_DEF_SCALAR(m_type, m_name)                        \
	void EdenPlanetAtmosphere::set_##m_name(m_type p_value) {       \
		m_name = p_value;                                           \
	}                                                               \
	m_type EdenPlanetAtmosphere::get_##m_name() const {             \
		return m_name;                                              \
	}
#define EDEN_ATMO_DEF_REF(m_type, m_name)                            \
	void EdenPlanetAtmosphere::set_##m_name(const m_type &p_value) { \
		m_name = p_value;                                            \
	}                                                                \
	m_type EdenPlanetAtmosphere::get_##m_name() const {              \
		return m_name;                                               \
	}

#define EDEN_ATMO_U_FLOAT(m_name, m_default, m_hint, m_group) EDEN_ATMO_DEF_SCALAR(float, m_name)
#define EDEN_ATMO_U_VEC3(m_name, m_x, m_y, m_z, m_group) EDEN_ATMO_DEF_REF(Vector3, m_name)
#define EDEN_ATMO_U_COLOR(m_name, m_r, m_g, m_b, m_group) EDEN_ATMO_DEF_REF(Color, m_name)
#define EDEN_ATMO_U_INT(m_name, m_default, m_hint, m_group) EDEN_ATMO_DEF_SCALAR(int, m_name)
#define EDEN_ATMO_L_FLOAT(m_name, m_default, m_hint, m_group) EDEN_ATMO_DEF_SCALAR(float, m_name)
#define EDEN_ATMO_L_VEC3(m_name, m_x, m_y, m_z, m_group) EDEN_ATMO_DEF_REF(Vector3, m_name)
#define EDEN_ATMO_L_BOOL(m_name, m_default, m_group) EDEN_ATMO_DEF_SCALAR(bool, m_name)
#define EDEN_ATMO_L_INT(m_name, m_default, m_hint, m_group) EDEN_ATMO_DEF_SCALAR(int, m_name)
#define EDEN_ATMO_L_STRING(m_name, m_default, m_group) EDEN_ATMO_DEF_REF(String, m_name)
#include "eden_planet_atmosphere_props.inc"
#undef EDEN_ATMO_L_STRING
#undef EDEN_ATMO_U_FLOAT
#undef EDEN_ATMO_U_VEC3
#undef EDEN_ATMO_U_COLOR
#undef EDEN_ATMO_U_INT
#undef EDEN_ATMO_L_FLOAT
#undef EDEN_ATMO_L_VEC3
#undef EDEN_ATMO_L_BOOL
#undef EDEN_ATMO_L_INT

// ===========================================================================================
// Bindings
// ===========================================================================================
void EdenPlanetAtmosphere::_bind_methods() {
#define EDEN_BIND_ACCESSORS(m_name)                                                                                 \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &EdenPlanetAtmosphere::set_##m_name);                   \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &EdenPlanetAtmosphere::get_##m_name);

#define EDEN_ATMO_U_FLOAT(m_name, m_default, m_hint, m_group) EDEN_BIND_ACCESSORS(m_name)
#define EDEN_ATMO_U_VEC3(m_name, m_x, m_y, m_z, m_group) EDEN_BIND_ACCESSORS(m_name)
#define EDEN_ATMO_U_COLOR(m_name, m_r, m_g, m_b, m_group) EDEN_BIND_ACCESSORS(m_name)
#define EDEN_ATMO_U_INT(m_name, m_default, m_hint, m_group) EDEN_BIND_ACCESSORS(m_name)
#define EDEN_ATMO_L_FLOAT(m_name, m_default, m_hint, m_group) EDEN_BIND_ACCESSORS(m_name)
#define EDEN_ATMO_L_VEC3(m_name, m_x, m_y, m_z, m_group) EDEN_BIND_ACCESSORS(m_name)
#define EDEN_ATMO_L_BOOL(m_name, m_default, m_group) EDEN_BIND_ACCESSORS(m_name)
#define EDEN_ATMO_L_INT(m_name, m_default, m_hint, m_group) EDEN_BIND_ACCESSORS(m_name)
#define EDEN_ATMO_L_STRING(m_name, m_default, m_group) EDEN_BIND_ACCESSORS(m_name)
#include "eden_planet_atmosphere_props.inc"
#undef EDEN_ATMO_L_STRING
#undef EDEN_ATMO_U_FLOAT
#undef EDEN_ATMO_U_VEC3
#undef EDEN_ATMO_U_COLOR
#undef EDEN_ATMO_U_INT
#undef EDEN_ATMO_L_FLOAT
#undef EDEN_ATMO_L_VEC3
#undef EDEN_ATMO_L_BOOL
#undef EDEN_ATMO_L_INT

	ClassDB::bind_method(D_METHOD("set_sun_light_path", "path"), &EdenPlanetAtmosphere::set_sun_light_path);
	ClassDB::bind_method(D_METHOD("get_sun_light_path"), &EdenPlanetAtmosphere::get_sun_light_path);
	ClassDB::bind_method(D_METHOD("set_moon_light_path", "path"), &EdenPlanetAtmosphere::set_moon_light_path);
	ClassDB::bind_method(D_METHOD("get_moon_light_path"), &EdenPlanetAtmosphere::get_moon_light_path);
	ClassDB::bind_method(D_METHOD("set_sun2_light_path", "path"), &EdenPlanetAtmosphere::set_sun2_light_path);
	ClassDB::bind_method(D_METHOD("get_sun2_light_path"), &EdenPlanetAtmosphere::get_sun2_light_path);
	ClassDB::bind_method(D_METHOD("set_environment_path", "path"), &EdenPlanetAtmosphere::set_environment_path);
	ClassDB::bind_method(D_METHOD("get_environment_path"), &EdenPlanetAtmosphere::get_environment_path);
	ClassDB::bind_method(D_METHOD("set_linked_materials", "materials"), &EdenPlanetAtmosphere::set_linked_materials);
	ClassDB::bind_method(D_METHOD("get_linked_materials"), &EdenPlanetAtmosphere::get_linked_materials);
	ClassDB::bind_method(D_METHOD("add_linked_material", "material"), &EdenPlanetAtmosphere::add_linked_material);
	ClassDB::bind_method(D_METHOD("remove_linked_material", "material"), &EdenPlanetAtmosphere::remove_linked_material);
	ClassDB::bind_method(D_METHOD("set_sky_shader_override", "shader"), &EdenPlanetAtmosphere::set_sky_shader_override);
	ClassDB::bind_method(D_METHOD("get_sky_shader_override"), &EdenPlanetAtmosphere::get_sky_shader_override);
	ClassDB::bind_method(D_METHOD("set_moon_texture", "texture"), &EdenPlanetAtmosphere::set_moon_texture);
	ClassDB::bind_method(D_METHOD("get_moon_texture"), &EdenPlanetAtmosphere::get_moon_texture);
	ClassDB::bind_method(D_METHOD("get_moon_illumination"), &EdenPlanetAtmosphere::get_moon_illumination);
	ClassDB::bind_method(D_METHOD("set_moonb_light_path", "path"), &EdenPlanetAtmosphere::set_moonb_light_path);
	ClassDB::bind_method(D_METHOD("get_moonb_light_path"), &EdenPlanetAtmosphere::get_moonb_light_path);
	ClassDB::bind_method(D_METHOD("set_moonb_texture", "texture"), &EdenPlanetAtmosphere::set_moonb_texture);
	ClassDB::bind_method(D_METHOD("get_moonb_texture"), &EdenPlanetAtmosphere::get_moonb_texture);
	ClassDB::bind_method(D_METHOD("get_moonb_illumination"), &EdenPlanetAtmosphere::get_moonb_illumination);

	ClassDB::bind_method(D_METHOD("get_sun_direction"), &EdenPlanetAtmosphere::get_sun_direction);
	ClassDB::bind_method(D_METHOD("get_sun2_direction"), &EdenPlanetAtmosphere::get_sun2_direction);
	ClassDB::bind_method(D_METHOD("sun_transmittance_at", "planet_relative_position"), &EdenPlanetAtmosphere::sun_transmittance_at);
	ClassDB::bind_method(D_METHOD("phase_for_sun_elevation", "up", "elevation_deg", "rising"),
			&EdenPlanetAtmosphere::phase_for_sun_elevation, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("get_sky_material"), &EdenPlanetAtmosphere::get_sky_material);
	ClassDB::bind_method(D_METHOD("get_post_effect"), &EdenPlanetAtmosphere::get_post_effect);
	ClassDB::bind_method(D_METHOD("get_sky"), &EdenPlanetAtmosphere::get_sky);

	ADD_GROUP("Wiring", "");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sun_light_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "DirectionalLight3D"),
			"set_sun_light_path", "get_sun_light_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "moon_light_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "DirectionalLight3D"),
			"set_moon_light_path", "get_moon_light_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sun2_light_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "DirectionalLight3D"),
			"set_sun2_light_path", "get_sun2_light_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "environment_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "WorldEnvironment"),
			"set_environment_path", "get_environment_path");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "linked_materials", PROPERTY_HINT_ARRAY_TYPE,
						 vformat("%s/%s:%s", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "ShaderMaterial")),
			"set_linked_materials", "get_linked_materials");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sky_shader_override", PROPERTY_HINT_RESOURCE_TYPE, "Shader"),
			"set_sky_shader_override", "get_sky_shader_override");

	ADD_GROUP("Moon", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "moon_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"),
			"set_moon_texture", "get_moon_texture");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "moonb_light_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "DirectionalLight3D"),
			"set_moonb_light_path", "get_moonb_light_path");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "moonb_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"),
			"set_moonb_texture", "get_moonb_texture");

	// Inspector properties, grouped as declared in the table.
#define EDEN_ATMO_U_FLOAT(m_name, m_default, m_hint, m_group)                                              \
	ADD_GROUP(m_group, "");                                                                                \
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, #m_name, PROPERTY_HINT_RANGE, m_hint), "set_" #m_name, "get_" #m_name);
#define EDEN_ATMO_U_VEC3(m_name, m_x, m_y, m_z, m_group) \
	ADD_GROUP(m_group, "");                              \
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, #m_name), "set_" #m_name, "get_" #m_name);
#define EDEN_ATMO_U_COLOR(m_name, m_r, m_g, m_b, m_group)                                              \
	ADD_GROUP(m_group, "");                                                                            \
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, #m_name, PROPERTY_HINT_COLOR_NO_ALPHA), "set_" #m_name, "get_" #m_name);
#define EDEN_ATMO_U_INT(m_name, m_default, m_hint, m_group)                                              \
	ADD_GROUP(m_group, "");                                                                              \
	ADD_PROPERTY(PropertyInfo(Variant::INT, #m_name, PROPERTY_HINT_RANGE, m_hint), "set_" #m_name, "get_" #m_name);
#define EDEN_ATMO_L_FLOAT(m_name, m_default, m_hint, m_group)                                              \
	ADD_GROUP(m_group, "");                                                                                \
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, #m_name, PROPERTY_HINT_RANGE, m_hint), "set_" #m_name, "get_" #m_name);
#define EDEN_ATMO_L_VEC3(m_name, m_x, m_y, m_z, m_group) \
	ADD_GROUP(m_group, "");                              \
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, #m_name), "set_" #m_name, "get_" #m_name);
#define EDEN_ATMO_L_BOOL(m_name, m_default, m_group) \
	ADD_GROUP(m_group, "");                          \
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, #m_name), "set_" #m_name, "get_" #m_name);
#define EDEN_ATMO_L_INT(m_name, m_default, m_hint, m_group)                                              \
	ADD_GROUP(m_group, "");                                                                              \
	ADD_PROPERTY(PropertyInfo(Variant::INT, #m_name, PROPERTY_HINT_RANGE, m_hint), "set_" #m_name, "get_" #m_name);
#define EDEN_ATMO_L_STRING(m_name, m_default, m_group) \
	ADD_GROUP(m_group, "");                        \
	ADD_PROPERTY(PropertyInfo(Variant::STRING, #m_name), "set_" #m_name, "get_" #m_name);
#include "eden_planet_atmosphere_props.inc"
#undef EDEN_ATMO_L_STRING
#undef EDEN_ATMO_U_FLOAT
#undef EDEN_ATMO_U_VEC3
#undef EDEN_ATMO_U_COLOR
#undef EDEN_ATMO_U_INT
#undef EDEN_ATMO_L_FLOAT
#undef EDEN_ATMO_L_VEC3
#undef EDEN_ATMO_L_BOOL
#undef EDEN_ATMO_L_INT
#undef EDEN_BIND_ACCESSORS
}
