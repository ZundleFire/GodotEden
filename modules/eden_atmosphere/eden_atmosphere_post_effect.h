#ifndef EDEN_ATMOSPHERE_POST_EFFECT_H
#define EDEN_ATMOSPHERE_POST_EFFECT_H

#include "core/os/mutex.h"
#include "scene/resources/compositor.h"

class RenderData;
class RenderingDevice;

// Post-transparent pass for the planetary atmosphere: exponential height fog measured by altitude
// above the curved surface, plus screen-space light rays from the sun and moon.
//
// Owned and fed by EdenPlanetAtmosphere, which attaches it to the WorldEnvironment's Compositor
// and pushes a FrameParams snapshot every frame. You should not normally create one yourself.
//
// Why a CompositorEffect subclass that re-registers its own callback: CompositorEffect exposes its
// render hook only as a GDVIRTUAL, which scripts and GDExtensions can override but a C++ subclass
// cannot. The base class merely hands a Callable to the RenderingServer, so this class hands over
// its own instead -- in its constructor, which runs after the base one. The callback type is then
// hidden from the inspector, because setting it would make the base re-register its own (empty)
// callback and silently switch the effect off.
class EdenAtmospherePostEffect : public CompositorEffect {
	GDCLASS(EdenAtmospherePostEffect, CompositorEffect);

public:
	// Everything the GPU passes need that is not camera-specific. Plain data, copied under a
	// mutex, because the render callback may run on the render thread.
	struct FrameParams {
		bool fog_enabled = true;
		bool rays_enabled = true;

		Vector3 planet_center;
		float planet_radius = 40000.0f;

		float fog_density = 0.00025f;
		float fog_scale_height = 350.0f;
		float fog_base_altitude = 0.0f;
		float fog_top = 2800.0f; // above the base; where density is negligible
		float fog_sky_affect = 0.6f;
		float fog_anisotropy = 0.6f;
		float fog_directional = 0.5f;
		Vector3 fog_albedo = Vector3(0.85f, 0.88f, 0.92f);
		Vector3 fog_ambient;

		Vector3 sun_direction = Vector3(0, 1, 0);
		Vector3 sun_fog_light;
		Vector3 moon_direction = Vector3(0, -1, 0);
		Vector3 moon_fog_light;

		// Light rays. Weight is the overall strength before the on-screen fade; 0 skips the light.
		float sun_ray_weight = 0.0f;
		float sun_ray_threshold = 1.2f;
		Vector3 sun_ray_tint = Vector3(1, 1, 1);
		float moon_ray_weight = 0.0f;
		float moon_ray_threshold = 0.3f;
		Vector3 moon_ray_tint = Vector3(1, 1, 1);
		int ray_samples = 24;
		float ray_density = 0.9f;
		float ray_decay = 0.96f;
		float ray_radius = 0.6f;
		// Extra ray weight specifically over sky/fog pixels, scaled by the local fog amount there.
		// Screen-space rays are additive on an already near-uniform bright sky, so straight shafts
		// only read as visible light/dark bands where SOMETHING breaks that uniformity -- an
		// occluder's silhouette, or a cloud edge. This gives the atmosphere itself the same kind of
		// contrast by making rays punch through haze more than they brighten clear sky, so shafts
		// become visible fanning up into the sky and through the fog, not just crossing terrain.
		float ray_sky_boost = 2.5f;
	};

	EdenAtmospherePostEffect();
	~EdenAtmospherePostEffect();

	void set_frame_params(const FrameParams &p_params);

protected:
	static void _bind_methods() {}
	void _validate_property(PropertyInfo &p_property) const;

private:
	void _render(int p_callback_type, const RenderData *p_render_data);
	bool _ensure_pipelines(RenderingDevice *p_rd);
	RID _compile(RenderingDevice *p_rd, const char *p_source, const String &p_name);
	static void _free_rids(RID p_rays_shader, RID p_emission_shader, RID p_fog_shader, RID p_ubo, RID p_linear, RID p_nearest);

	Mutex params_mutex;
	FrameParams pending;

	RID rays_shader;
	RID rays_pipeline;
	RID emission_shader;
	RID emission_pipeline;
	RID fog_shader;
	RID fog_pipeline;
	RID params_ubo;
	RID linear_sampler;
	RID nearest_sampler;
	bool pipelines_failed = false;
};

#endif // EDEN_ATMOSPHERE_POST_EFFECT_H
