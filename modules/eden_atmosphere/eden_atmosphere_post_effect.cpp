#include "eden_atmosphere_post_effect.h"

#include "eden_atmosphere_shaders.gen.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/storage/render_data.h"
#include "servers/rendering/storage/render_scene_data.h"

// Names for the half-res ray texture owned by the render buffers. Plain C strings wrapped in SNAME()
// at the use site: a file-scope static StringName is constructed before Godot's StringName table
// exists ("Condition !configured" at startup) and ends up an empty, unusable name.
static const char *CONTEXT_NAME = "eden_atmosphere";
static const char *RAYS_TEXTURE = "light_rays";
static const char *EMISSION_TEXTURE = "light_ray_emission";

// Must match the Params block in shaders/atmosphere_post_common.glsl: 2 mat4 + 15 vec4.
static const int PARAMS_FLOAT_COUNT = 16 * 2 + 4 * 16;

EdenAtmospherePostEffect::EdenAtmospherePostEffect() {
	set_effect_callback_type(EFFECT_CALLBACK_TYPE_POST_TRANSPARENT);
	// Resolved buffers: with MSAA on, the unresolved colour/depth cannot be read in compute.
	set_access_resolved_color(true);
	set_access_resolved_depth(true);

	// Replace the base class's callback (a GDVIRTUAL dispatcher a C++ subclass cannot override)
	// with ours. Must come after the base constructor and after set_effect_callback_type() above,
	// both of which register the base callback.
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs != nullptr && get_rid().is_valid()) {
		rs->compositor_effect_set_callback(get_rid(), RenderingServer::COMPOSITOR_EFFECT_CALLBACK_TYPE_POST_TRANSPARENT,
				callable_mp(this, &EdenAtmospherePostEffect::_render));
	}
}

EdenAtmospherePostEffect::~EdenAtmospherePostEffect() {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs == nullptr) {
		return;
	}
	// GPU resources belong to the render thread. Bind the RIDs by value -- `this` is going away.
	rs->call_on_render_thread(callable_mp_static(&EdenAtmospherePostEffect::_free_rids)
									  .bind(rays_shader, emission_shader, fog_shader, params_ubo, linear_sampler, nearest_sampler));
}

void EdenAtmospherePostEffect::_free_rids(RID p_rays_shader, RID p_emission_shader, RID p_fog_shader, RID p_ubo, RID p_linear, RID p_nearest) {
	RenderingServer *rs = RenderingServer::get_singleton();
	RenderingDevice *rd = rs != nullptr ? rs->get_rendering_device() : nullptr;
	if (rd == nullptr) {
		return;
	}
	// Freeing a shader also frees the pipelines and cached uniform sets built on it.
	for (const RID &rid : { p_rays_shader, p_emission_shader, p_fog_shader, p_ubo, p_linear, p_nearest }) {
		if (rid.is_valid()) {
			rd->free_rid(rid);
		}
	}
}

void EdenAtmospherePostEffect::_validate_property(PropertyInfo &p_property) const {
	// See the class comment: changing the callback type would silently disable the effect.
	if (p_property.name == "effect_callback_type") {
		p_property.usage = PROPERTY_USAGE_NONE;
	}
}

void EdenAtmospherePostEffect::set_frame_params(const FrameParams &p_params) {
	MutexLock lock(params_mutex);
	pending = p_params;
}

RID EdenAtmospherePostEffect::_compile(RenderingDevice *p_rd, const char *p_source, const String &p_name) {
	String error;
	Vector<uint8_t> spirv = p_rd->shader_compile_spirv_from_source(RenderingDevice::SHADER_STAGE_COMPUTE,
			String::utf8(p_source), RenderingDevice::SHADER_LANGUAGE_GLSL, &error);
	if (!error.is_empty() || spirv.is_empty()) {
		ERR_PRINT(vformat("EdenAtmospherePostEffect: %s failed to compile: %s", p_name, error));
		return RID();
	}
	Vector<RenderingDevice::ShaderStageSPIRVData> stages;
	RenderingDevice::ShaderStageSPIRVData stage;
	stage.shader_stage = RenderingDevice::SHADER_STAGE_COMPUTE;
	stage.spirv = spirv;
	stages.push_back(stage);
	return p_rd->shader_create_from_spirv(stages, p_name);
}

bool EdenAtmospherePostEffect::_ensure_pipelines(RenderingDevice *p_rd) {
	if (rays_pipeline.is_valid() && emission_pipeline.is_valid() && fog_pipeline.is_valid()) {
		return true;
	}
	if (pipelines_failed) {
		return false; // do not retry a failed compile every frame
	}

	rays_shader = _compile(p_rd, EDEN_RAYS_COMPUTE_CODE, "EdenAtmosphereRays");
	emission_shader = _compile(p_rd, EDEN_RAY_EMISSION_COMPUTE_CODE, "EdenAtmosphereRayEmission");
	fog_shader = _compile(p_rd, EDEN_FOG_COMPOSITE_COMPUTE_CODE, "EdenAtmosphereFogComposite");
	if (!rays_shader.is_valid() || !emission_shader.is_valid() || !fog_shader.is_valid()) {
		pipelines_failed = true;
		return false;
	}
	rays_pipeline = p_rd->compute_pipeline_create(rays_shader);
	emission_pipeline = p_rd->compute_pipeline_create(emission_shader);
	fog_pipeline = p_rd->compute_pipeline_create(fog_shader);

	params_ubo = p_rd->uniform_buffer_create(PARAMS_FLOAT_COUNT * sizeof(float));

	RenderingDevice::SamplerState linear;
	linear.mag_filter = RenderingDevice::SAMPLER_FILTER_LINEAR;
	linear.min_filter = RenderingDevice::SAMPLER_FILTER_LINEAR;
	linear_sampler = p_rd->sampler_create(linear);
	nearest_sampler = p_rd->sampler_create(RenderingDevice::SamplerState());

	pipelines_failed = !(rays_pipeline.is_valid() && emission_pipeline.is_valid() && fog_pipeline.is_valid() && params_ubo.is_valid());
	return !pipelines_failed;
}

// ---------------------------------------------------------------------------------------------
// std140 packing helpers
// ---------------------------------------------------------------------------------------------
static void put_vec4(float *r_dst, int &r_i, float p_x, float p_y, float p_z, float p_w) {
	r_dst[r_i++] = p_x;
	r_dst[r_i++] = p_y;
	r_dst[r_i++] = p_z;
	r_dst[r_i++] = p_w;
}

static void put_vec3w(float *r_dst, int &r_i, const Vector3 &p_v, float p_w) {
	put_vec4(r_dst, r_i, p_v.x, p_v.y, p_v.z, p_w);
}

// GLSL mat4 is column-major, and so is Projection's storage.
static void put_projection(float *r_dst, int &r_i, const Projection &p_m) {
	for (int c = 0; c < 4; c++) {
		for (int r = 0; r < 4; r++) {
			r_dst[r_i++] = (float)p_m.columns[c][r];
		}
	}
}

// Transform3D -> column-major mat4. Basis stores rows, so column j is (rows[0][j], rows[1][j], rows[2][j]).
static void put_transform(float *r_dst, int &r_i, const Transform3D &p_t) {
	for (int c = 0; c < 3; c++) {
		put_vec4(r_dst, r_i, (float)p_t.basis.rows[0][c], (float)p_t.basis.rows[1][c], (float)p_t.basis.rows[2][c], 0.0f);
	}
	put_vec4(r_dst, r_i, (float)p_t.origin.x, (float)p_t.origin.y, (float)p_t.origin.z, 1.0f);
}

// Screen-space position of a light at infinity. Uses the SAME depth-corrected projection the
// fog pass inverts, so the rays converge exactly on the disc the sky shader draws.
// Returns the uv in xy and an on-screen weight in z (0 when behind the camera).
static Vector3 light_screen_uv(const Projection &p_proj, const Transform3D &p_cam, const Vector3 &p_dir) {
	const Vector3 view_dir = p_cam.basis.xform_inv(p_dir);
	const Vector4 clip = p_proj.xform(Vector4(view_dir.x, view_dir.y, view_dir.z, 0.0f));
	if (clip.w <= 1e-5f) {
		return Vector3(0.5f, 0.5f, 0.0f);
	}
	const float u = clip.x / clip.w * 0.5f + 0.5f;
	const float v = clip.y / clip.w * 0.5f + 0.5f;
	// Fade out as the light leaves the frame rather than cutting at the edge: the shafts from a
	// light just off-screen are still visible and should not pop.
	const float outside = MAX(MAX(-u, u - 1.0f), MAX(-v, v - 1.0f));
	const float t = CLAMP(outside / 0.5f, 0.0f, 1.0f);
	const float weight = 1.0f - t * t * (3.0f - 2.0f * t);
	return Vector3(u, v, weight);
}

// ---------------------------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------------------------
void EdenAtmospherePostEffect::_render(int p_callback_type, const RenderData *p_render_data) {
	if (p_callback_type != RenderingServer::COMPOSITOR_EFFECT_CALLBACK_TYPE_POST_TRANSPARENT || p_render_data == nullptr) {
		return;
	}

	FrameParams fp;
	{
		MutexLock lock(params_mutex);
		fp = pending;
	}
	if (!fp.fog_enabled && !fp.rays_enabled) {
		return;
	}

	RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
	if (rd == nullptr) {
		return;
	}
	Ref<RenderSceneBuffersRD> buffers = p_render_data->get_render_scene_buffers();
	RenderSceneData *scene = p_render_data->get_render_scene_data();
	if (buffers.is_null() || scene == nullptr || !buffers->has_internal_texture()) {
		return; // e.g. reflection probe renders
	}
	if (!_ensure_pipelines(rd)) {
		return;
	}

	const Size2i size = buffers->get_internal_size();
	if (size.x < 2 || size.y < 2) {
		return;
	}
	// Quarter resolution. Rays are a smooth, low-frequency radial gradient, and at half res with 24
	// samples they measured ~5-6ms on a GTX 750 Ti whenever the sun was on screen -- about 25M
	// texture fetches a frame. Quarter res is 4x fewer pixels; the composite's 4-tap blur covers
	// the upscale.
	const Size2i half(MAX(size.x / 4, 1), MAX(size.y / 4, 1));

	// Owned by the render buffers, so it is freed and recreated automatically on resize.
	if (!buffers->has_texture(SNAME(CONTEXT_NAME), SNAME(RAYS_TEXTURE))) {
		buffers->create_texture(SNAME(CONTEXT_NAME), SNAME(RAYS_TEXTURE), RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT,
				RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice::TEXTURE_USAGE_STORAGE_BIT,
				RenderingDevice::TEXTURE_SAMPLES_1, half);
	}
	if (!buffers->has_texture(SNAME(CONTEXT_NAME), SNAME(EMISSION_TEXTURE))) {
		buffers->create_texture(SNAME(CONTEXT_NAME), SNAME(EMISSION_TEXTURE), RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT,
				RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice::TEXTURE_USAGE_STORAGE_BIT,
				RenderingDevice::TEXTURE_SAMPLES_1, half);
	}

	const Projection proj = scene->get_cam_projection();
	const Transform3D cam = scene->get_cam_transform();

	const Vector3 sun_uv = light_screen_uv(proj, cam, fp.sun_direction);
	const Vector3 moon_uv = light_screen_uv(proj, cam, fp.moon_direction);
	const float sun_w = fp.rays_enabled ? fp.sun_ray_weight * sun_uv.z : 0.0f;
	const float moon_w = fp.rays_enabled ? fp.moon_ray_weight * moon_uv.z : 0.0f;
	const bool any_rays = sun_w > 1e-4f || moon_w > 1e-4f;

	float data[PARAMS_FLOAT_COUNT];
	int i = 0;
	put_projection(data, i, proj.inverse());
	put_transform(data, i, cam);
	put_vec3w(data, i, fp.planet_center, fp.planet_radius);
	put_vec4(data, i, fp.fog_density, fp.fog_scale_height, fp.fog_base_altitude, fp.fog_enabled ? 1.0f : 0.0f);
	put_vec4(data, i, fp.fog_sky_affect, fp.fog_anisotropy, fp.fog_directional, fp.fog_top);
	put_vec3w(data, i, fp.fog_albedo, 0.0f);
	put_vec3w(data, i, fp.fog_ambient, 0.0f);
	put_vec3w(data, i, fp.sun_direction, 0.0f);
	put_vec3w(data, i, fp.sun_fog_light, 0.0f);
	put_vec3w(data, i, fp.moon_direction, 0.0f);
	put_vec3w(data, i, fp.moon_fog_light, 0.0f);
	put_vec4(data, i, (float)size.x, (float)size.y, (float)size.x / (float)size.y, any_rays ? 1.0f : 0.0f);
	put_vec4(data, i, sun_uv.x, sun_uv.y, sun_w, fp.sun_ray_threshold);
	put_vec3w(data, i, fp.sun_ray_tint, 0.0f);
	put_vec4(data, i, moon_uv.x, moon_uv.y, moon_w, fp.moon_ray_threshold);
	put_vec3w(data, i, fp.moon_ray_tint, 0.0f);
	// Floor of 1 only -- no ceiling. The rays shader's march is a dynamic loop, so a high count
	// costs GPU time and nothing else; silently capping it made the slider stop doing anything
	// past 128 with no indication why.
	put_vec4(data, i, (float)MAX(fp.ray_samples, 1), fp.ray_density, fp.ray_decay, fp.ray_radius);
	put_vec4(data, i, fp.ray_sky_boost, 0.0f, 0.0f, 0.0f);
	ERR_FAIL_COND_MSG(i != PARAMS_FLOAT_COUNT, "EdenAtmospherePostEffect: params packing out of step with the shader block.");
	rd->buffer_update(params_ubo, 0, PARAMS_FLOAT_COUNT * sizeof(float), data);

	UniformSetCacheRD *cache = UniformSetCacheRD::get_singleton();
	const RenderingDevice::Uniform u_params(RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER, 3, params_ubo);

	for (uint32_t view = 0; view < buffers->get_view_count(); view++) {
		const RID color = buffers->get_internal_texture(view);
		const RID depth = buffers->get_depth_texture(view);
		const RID rays = buffers->get_texture_slice(SNAME(CONTEXT_NAME), SNAME(RAYS_TEXTURE), view, 0);

		if (any_rays) {
			const RID emission = buffers->get_texture_slice(SNAME(CONTEXT_NAME), SNAME(EMISSION_TEXTURE), view, 0);
			{
				RenderingDevice::Uniform u_color(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ linear_sampler, color }));
				RenderingDevice::Uniform u_depth(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ nearest_sampler, depth }));
				RenderingDevice::Uniform u_emission(RenderingDevice::UNIFORM_TYPE_IMAGE, 2, emission);
				RID set = cache->get_cache(emission_shader, 0, u_color, u_depth, u_emission, u_params);

				RenderingDevice::ComputeListID list = rd->compute_list_begin();
				rd->compute_list_bind_compute_pipeline(list, emission_pipeline);
				rd->compute_list_bind_uniform_set(list, set, 0);
				rd->compute_list_dispatch_threads(list, half.x, half.y, 1);
				rd->compute_list_end();
			}
			RenderingDevice::Uniform u_emission(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ linear_sampler, emission }));
			RenderingDevice::Uniform u_rays(RenderingDevice::UNIFORM_TYPE_IMAGE, 2, rays);
			RID set = cache->get_cache(rays_shader, 0, u_emission, u_rays, u_params);

			RenderingDevice::ComputeListID list = rd->compute_list_begin();
			rd->compute_list_bind_compute_pipeline(list, rays_pipeline);
			rd->compute_list_bind_uniform_set(list, set, 0);
			rd->compute_list_dispatch_threads(list, half.x, half.y, 1);
			rd->compute_list_end();
		}

		if (fp.fog_enabled || any_rays) {
			RenderingDevice::Uniform u_color(RenderingDevice::UNIFORM_TYPE_IMAGE, 0, color);
			RenderingDevice::Uniform u_depth(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ nearest_sampler, depth }));
			RenderingDevice::Uniform u_rays(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ linear_sampler, rays }));
			RID set = cache->get_cache(fog_shader, 0, u_color, u_depth, u_rays, u_params);

			RenderingDevice::ComputeListID list = rd->compute_list_begin();
			rd->compute_list_bind_compute_pipeline(list, fog_pipeline);
			rd->compute_list_bind_uniform_set(list, set, 0);
			rd->compute_list_dispatch_threads(list, size.x, size.y, 1);
			rd->compute_list_end();
		}
	}
}
