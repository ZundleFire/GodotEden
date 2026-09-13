// Property table for EdenPlanetAtmosphere. Included several times with different macro
// definitions to generate the members, the accessors, the ClassDB bindings, the inspector
// properties and the shader uniform push -- so a new knob is one line here, not five edits
// across two files that can drift apart.
//
// EDEN_ATMO_U_*  : pushed to the sky material (and every linked material) each frame. The
//                  property name MUST match the shader uniform name exactly.
// EDEN_ATMO_L_*  : CPU-side only, never pushed.
//
// Any macro not defined by the includer is defined empty by the includer's prologue.

// =========================================================================================
// Planet geometry
// =========================================================================================
EDEN_ATMO_U_FLOAT(planet_radius, 40000.0, "100.0,1000000.0,1.0,or_greater,suffix:m", "Planet")
EDEN_ATMO_U_VEC3(planet_center, 0.0, 0.0, 0.0, "Planet")
EDEN_ATMO_U_FLOAT(atmosphere_height, 4000.0, "500.0,20000.0,1.0,suffix:m", "Planet")

// =========================================================================================
// Scattering
// =========================================================================================
EDEN_ATMO_U_FLOAT(rayleigh_scale_height, 800.0, "50.0,8000.0,1.0,suffix:m", "Scattering")
EDEN_ATMO_U_FLOAT(mie_scale_height, 120.0, "10.0,2000.0,1.0,suffix:m", "Scattering")
// See the shader: these are multipliers on Earth-equivalent scattering, auto-renormalised for
// the chosen scale heights, so 1.0 means Earth-like at any planet size. Defaults are not 1.0
// because a small planet's short grazing path needs extra Rayleigh to keep sunset colour.
EDEN_ATMO_U_FLOAT(rayleigh_strength, 2.5, "0.0,8.0,0.01", "Scattering")
EDEN_ATMO_U_FLOAT(mie_strength, 0.35, "0.0,8.0,0.01", "Scattering")
EDEN_ATMO_U_FLOAT(mie_anisotropy, 0.76, "0.0,0.999,0.001", "Scattering")
// RGB wavelengths in nm. Rayleigh goes as 1/lambda^4, so this is the physical control for an
// alien sky -- shift it and gradient, sunset and sun colour all move together consistently.
EDEN_ATMO_U_VEC3(rayleigh_wavelengths, 680.0, 550.0, 440.0, "Scattering")

// =========================================================================================
// Sun
// =========================================================================================
EDEN_ATMO_U_COLOR(sun_tint, 1.0, 1.0, 1.0, "Sun")
EDEN_ATMO_U_FLOAT(sun_intensity, 22.0, "0.0,100.0,0.1", "Sun")
EDEN_ATMO_U_FLOAT(sun_angular_radius, 0.02, "0.001,0.2,0.0005,radians_as_degrees", "Sun")
// Kept separate from sun_intensity: that is a coefficient inside the scattering integral, not
// a surface brightness. Sharing one value makes the disc vanish into the horizon glow at sunset.
EDEN_ATMO_U_FLOAT(sun_disc_brightness, 30.0, "1.0,200.0,0.5", "Sun")
EDEN_ATMO_U_FLOAT(sun_limb_darkening, 0.4, "0.0,1.0,0.01", "Sun")
// Half-width in metres of the soft band at the planet's shadow edge. A hard edge quantises
// against the raymarch step count and prints arc-shaped bands into the sunset sky.
EDEN_ATMO_U_FLOAT(shadow_softness, 600.0, "0.0,4000.0,1.0,suffix:m", "Sun")

// =========================================================================================
// Sky appearance
// =========================================================================================
EDEN_ATMO_U_COLOR(ground_albedo, 0.11, 0.10, 0.09, "Sky")
EDEN_ATMO_U_FLOAT(ground_light_scale, 0.15, "0.0,2.0,0.01", "Sky")
EDEN_ATMO_U_COLOR(space_color, 0.0, 0.0, 0.0, "Sky")
EDEN_ATMO_U_FLOAT(exposure, 1.0, "0.0,8.0,0.01", "Sky")

// =========================================================================================
// Stars
// =========================================================================================
EDEN_ATMO_U_FLOAT(star_intensity, 1.0, "0.0,4.0,0.01", "Stars")
EDEN_ATMO_U_FLOAT(star_density, 0.02, "0.0,0.2,0.001", "Stars")
EDEN_ATMO_U_FLOAT(star_scale, 400.0, "50.0,2000.0,1.0", "Stars")
// Fakes the eye adaptation the pipeline does not have; without it the daytime sky is speckled.
EDEN_ATMO_U_FLOAT(star_daylight_fade, 80.0, "0.0,400.0,1.0", "Stars")
// Procedural star strength used INSTEAD of star_intensity while a space panorama is set. 0 lets
// the art stand alone; a little adds sparkle over a soft, downscaled panorama.
EDEN_ATMO_L_FLOAT(star_intensity_with_panorama, 0.0, "0.0,4.0,0.01", "Stars")

// =========================================================================================
// Deep space panorama
// =========================================================================================
// The Texture2D slots themselves are bound by hand (they are not scalars); these are the
// shaping controls that go with them.
EDEN_ATMO_U_COLOR(space_tint, 1.0, 1.0, 1.0, "Space")
// Default is 8, not 1. Photographed/rendered HDR space panoramas are authored at physical-ish
// levels -- the supplied set averages ~0.023 luminance -- so at 1.0 they are too faint to survive
// tonemapping and read as "the panorama is not loading". Measured, not guessed.
EDEN_ATMO_U_FLOAT(space_intensity, 8.0, "0.0,64.0,0.01", "Space")
EDEN_ATMO_U_FLOAT(space_overlay_intensity, 8.0, "0.0,64.0,0.01", "Space")
// Same trick as star_daylight_fade: with no eye adaptation in the pipeline, a nebula/starfield
// panorama would otherwise stay just as visible at noon as at midnight. 0 disables fading
// (panorama always fully visible); the two slots fade independently so, e.g., a faint nebula
// overlay can vanish well before the base starfield does.
EDEN_ATMO_U_FLOAT(space_daylight_fade, 60.0, "0.0,400.0,1.0", "Space")
EDEN_ATMO_U_FLOAT(space_overlay_daylight_fade, 60.0, "0.0,400.0,1.0", "Space")
// Axis the star sphere turns about. Separate from planet_spin_axis so the galactic plane can be
// tilted relative to the planet's rotation, which is the normal case in reality.
EDEN_ATMO_U_VEC3(space_rotation_axis, 0.0, 1.0, 0.0, "Space")
// How strongly the starfield tracks the day. 1 is physically correct -- the celestial sphere is
// fixed while the planet turns, so stars wheel overhead at the same rate the sun moves. 0 pins
// them, which looks wrong at night but is occasionally wanted for a static backdrop.
EDEN_ATMO_L_FLOAT(space_day_coupling, 1.0, "0.0,1.0,0.01", "Space")
// Fixed offset, for orienting a galaxy band where you want it.
EDEN_ATMO_L_FLOAT(space_rotation_offset_deg, 0.0, "-360.0,360.0,0.1,suffix:°", "Space")
// Extra offset on top of the above, for the overlay slot alone -- lets a nebula overlay sit at
// its own angle relative to the base starfield instead of being locked to the same orientation.
EDEN_ATMO_L_FLOAT(space_overlay_rotation_offset_deg, 0.0, "-360.0,360.0,0.1,suffix:°", "Space")

// =========================================================================================
// Time of day (CPU-side: these drive sun_direction, they are not uniforms themselves)
// =========================================================================================
EDEN_ATMO_L_FLOAT(day_length_seconds, 0.0, "0.0,3600.0,0.1,or_greater,suffix:s", "Time")
EDEN_ATMO_L_FLOAT(sun_time_of_day, 0.30, "0.0,1.0,0.001", "Time")
// Seasonal declination. Constant over one day, which is why latitude alone decides whether the
// sun rises steeply or merely grazes the horizon.
EDEN_ATMO_L_FLOAT(sun_declination_deg, 12.0, "-89.0,89.0,0.1,suffix:°", "Time")
EDEN_ATMO_L_VEC3(planet_spin_axis, 0.0, 1.0, 0.0, "Time")

// =========================================================================================
// Sun light driving
// =========================================================================================
EDEN_ATMO_L_FLOAT(sun_light_energy, 3.2, "0.0,16.0,0.01", "Sun Light")
EDEN_ATMO_L_FLOAT(night_light_floor, 0.02, "0.0,1.0,0.001", "Sun Light")
EDEN_ATMO_L_BOOL(drive_sun_light, true, "Sun Light")
EDEN_ATMO_L_BOOL(drive_sun_rotation, true, "Sun Light")

// =========================================================================================
// Moon / Night sky light
// =========================================================================================
// Disc colour multiplier. Neutral by default because a moon texture brings its own colour;
// the cool night cast belongs to moon_light_color, not to the body itself.
EDEN_ATMO_U_COLOR(moon_tint, 1.0, 0.98, 0.95, "Moon")
EDEN_ATMO_U_FLOAT(moon_intensity, 1.0, "0.0,10.0,0.01", "Moon")
EDEN_ATMO_U_FLOAT(moon_angular_radius, 0.025, "0.001,0.2,0.0005,radians_as_degrees", "Moon")
// 2.5, not the old 15: that value was tuned for a flat untextured disc. With texture albedo and
// Lommel-Seeliger reflectance 15 clips the whole moon to white and erases the maria; 2.5 was
// verified by render to keep surface detail at full moon.
EDEN_ATMO_U_FLOAT(moon_disc_brightness, 2.5, "0.1,200.0,0.01", "Moon")
EDEN_ATMO_U_FLOAT(moon_limb_darkening, 0.3, "0.0,1.0,0.01", "Moon")
EDEN_ATMO_U_FLOAT(moon_daylight_fade, 50.0, "0.0,400.0,1.0", "Moon")
EDEN_ATMO_U_VEC3(moon_direction, 0.0, -1.0, 0.0, "Moon")
// Light on the moon's night side -- sunlight bounced off the planet. Keeps the unlit part of a
// crescent faintly visible, as on Earth, instead of a hard black cut-out.
EDEN_ATMO_U_FLOAT(moon_earthshine, 0.03, "0.0,0.5,0.001", "Moon")
// Spins the texture on the disc (disc mode) or offsets surface longitude (equirect mode).
EDEN_ATMO_L_FLOAT(moon_texture_rotation_deg, 0.0, "-180.0,180.0,0.1,suffix:°", "Moon")
// false: moon_texture is a photo of the moon filling a square image. true: it is an
// equirectangular surface map, wrapped on the sphere with its centre facing the planet.
EDEN_ATMO_L_BOOL(moon_texture_equirect, false, "Moon")

// --- Lunar cycle -------------------------------------------------------------------------
// Position round the orbit measured from the sun: 0 new, 0.25 first quarter, 0.5 full,
// 0.75 last quarter. Advances on its own while the day cycle runs.
EDEN_ATMO_L_FLOAT(moon_phase, 0.5, "0.0,1.0,0.001", "Moon Cycle")
// Days for one full phase cycle. Earth is 29.53; the default is shortened so a play session
// actually sees the phases change.
EDEN_ATMO_L_FLOAT(lunar_cycle_days, 8.0, "0.1,365.0,0.01,or_greater", "Moon Cycle")
EDEN_ATMO_L_BOOL(advance_moon_phase, true, "Moon Cycle")
// Tilt of the orbit from the equator. Earth's moon is 5.14 degrees, which is why the moon
// rides high some months and low others, and why eclipses are rare rather than monthly.
EDEN_ATMO_L_FLOAT(moon_orbit_inclination_deg, 5.14, "-89.0,89.0,0.01,suffix:°", "Moon Cycle")
// Where the tilted orbit crosses the equator.
EDEN_ATMO_L_FLOAT(moon_node_longitude_deg, 0.0, "-360.0,360.0,0.1,suffix:°", "Moon Cycle")

// --- Moon light ----------------------------------------------------------------------------
// Also pushed as a uniform so moonlit clouds use the same colour as the DirectionalLight3D.
EDEN_ATMO_U_COLOR(moon_light_color, 0.62, 0.70, 0.95, "Moon Light")
// Energy at full moon, overhead. Scaled down by phase, altitude and atmospheric extinction.
EDEN_ATMO_L_FLOAT(moon_light_energy, 0.4, "0.0,16.0,0.01", "Moon Light")
// Brightness vs illuminated fraction. Earth's half moon is ~10% of full, not 50%.
EDEN_ATMO_L_FLOAT(moon_light_phase_exponent, 2.0, "0.0,8.0,0.01", "Moon Light")
// Hide the light entirely when it contributes nothing, skipping its shadow pass.
EDEN_ATMO_L_BOOL(moon_light_hide_when_dark, true, "Moon Light")
EDEN_ATMO_L_BOOL(drive_moon_light, true, "Moon Light")
EDEN_ATMO_L_BOOL(drive_moon_rotation, true, "Moon Light")

// =========================================================================================
// Sun & moon glow (sky shader)
// =========================================================================================
// Soft falloff around the disc. Lives in the sky shader, so it is extinguished and reddened along
// the view ray exactly like the disc -- a setting sun's glow goes orange with it.
EDEN_ATMO_U_FLOAT(sun_glow_intensity, 0.6, "0.0,8.0,0.01", "Sun Glow")
EDEN_ATMO_U_FLOAT(sun_glow_size, 0.12, "0.005,1.0,0.001,radians_as_degrees", "Sun Glow")
// Tight bright rim hugging the disc. Pushes past the bloom threshold, which is what makes the sun
// read as blinding rather than as a flat white circle.
EDEN_ATMO_U_FLOAT(sun_corona_intensity, 1.5, "0.0,16.0,0.01", "Sun Glow")
// 22-degree ice-crystal halo. Off by default for the sun -- on Earth it needs a cirrus layer.
EDEN_ATMO_U_FLOAT(sun_halo_intensity, 0.0, "0.0,4.0,0.01", "Sun Glow")
EDEN_ATMO_U_FLOAT(moon_glow_intensity, 0.5, "0.0,8.0,0.01", "Moon Glow")
EDEN_ATMO_U_FLOAT(moon_glow_size, 0.08, "0.005,1.0,0.001,radians_as_degrees", "Moon Glow")
EDEN_ATMO_U_FLOAT(moon_halo_intensity, 0.05, "0.0,4.0,0.01", "Moon Glow")
// Shared halo geometry. 22 degrees is the minimum-deviation angle of hexagonal ice plates.
EDEN_ATMO_U_FLOAT(halo_radius, 0.3840, "0.05,1.5,0.0001,radians_as_degrees", "Halo")
EDEN_ATMO_U_FLOAT(halo_width, 0.018, "0.001,0.2,0.0001,radians_as_degrees", "Halo")

// =========================================================================================
// Planetary height fog (post-process)
// =========================================================================================
// Density falls off with altitude ABOVE THE SURFACE, not with world Y -- Godot's own height fog is
// planar, which on a sphere becomes a vertical wall at the equator.
EDEN_ATMO_L_BOOL(fog_enabled, true, "Fog")
// Extinction per metre at the base altitude. 0.00012 is a clear-day haze (~30km visibility).
// Thicker defaults were tried: from orbit you look through the layer at grazing angles, and
// 0.00025 turned the whole planet an opaque white.
EDEN_ATMO_L_FLOAT(fog_density, 0.00012, "0.0,0.02,0.000001", "Fog")
// Scale height: altitude over which density drops by e. Small = a thin valley mist, large = haze.
EDEN_ATMO_L_FLOAT(fog_height_falloff, 250.0, "5.0,8000.0,1.0,suffix:m", "Fog")
// Altitude the falloff starts from; below it density is uniform. Raise to fill valleys on a
// planet whose sea level is not where the fog should sit.
EDEN_ATMO_L_FLOAT(fog_base_altitude, 0.0, "-5000.0,20000.0,1.0,suffix:m", "Fog")
// How much fog is laid over the sky. The atmosphere already scatters light there; full fog would
// double it, and zero leaves a visible seam where fogged terrain meets the horizon.
EDEN_ATMO_L_FLOAT(fog_sky_affect, 0.6, "0.0,1.0,0.01", "Fog")
EDEN_ATMO_U_COLOR(fog_albedo, 0.85, 0.88, 0.92, "Fog")
EDEN_ATMO_U_COLOR(fog_ambient_color, 0.36, 0.44, 0.58, "Fog")
EDEN_ATMO_L_FLOAT(fog_ambient_day, 0.6, "0.0,4.0,0.01", "Fog")
EDEN_ATMO_L_FLOAT(fog_ambient_night, 0.03, "0.0,1.0,0.001", "Fog")
// Sunlight and moonlight in the fog, after atmospheric extinction -- so fog turns gold at sunset
// and silver under a full moon without any extra authoring.
EDEN_ATMO_L_FLOAT(fog_sun_intensity, 0.9, "0.0,8.0,0.01", "Fog")
EDEN_ATMO_L_FLOAT(fog_moon_intensity, 0.2, "0.0,8.0,0.01", "Fog")
// Forward scattering: fog glows toward the light. 0 = even, 0.9 = a tight bright lobe.
EDEN_ATMO_L_FLOAT(fog_anisotropy, 0.6, "0.0,0.95,0.01", "Fog")
EDEN_ATMO_L_FLOAT(fog_sun_scatter, 0.5, "0.0,1.0,0.01", "Fog")

// =========================================================================================
// Light rays (post-process)
// =========================================================================================
EDEN_ATMO_L_BOOL(light_rays_enabled, true, "Light Rays")
// Samples per pixel per light, at quarter resolution. The main cost dial.
EDEN_ATMO_L_INT(light_ray_samples, 16, "4,64,1", "Light Rays")
// How far toward the light each pixel marches (1 = all the way). Longer = longer shafts.
EDEN_ATMO_L_FLOAT(light_ray_density, 0.9, "0.05,1.0,0.01", "Light Rays")
// Per-sample falloff. Closer to 1 = shafts carry further from the source.
EDEN_ATMO_L_FLOAT(light_ray_decay, 0.965, "0.8,1.0,0.001", "Light Rays")
// Screen-space radius (uv) around the light that is allowed to emit. Stops the whole bright
// daytime sky from casting rays.
EDEN_ATMO_L_FLOAT(light_ray_radius, 0.55, "0.02,2.0,0.01", "Light Rays")
// 0.45: at 0.9 the shafts painted solid beams across the whole foreground at sunset.
EDEN_ATMO_L_FLOAT(sun_ray_intensity, 0.45, "0.0,8.0,0.01", "Light Rays")
// Only sky brighter than this emits. Above normal sky brightness, so only the disc, its glow and
// sunlit cloud edges produce shafts.
EDEN_ATMO_L_FLOAT(sun_ray_threshold, 1.2, "0.0,50.0,0.01", "Light Rays")
EDEN_ATMO_U_COLOR(sun_ray_tint, 1.0, 0.95, 0.85, "Light Rays")
EDEN_ATMO_L_FLOAT(moon_ray_intensity, 0.6, "0.0,8.0,0.01", "Light Rays")
EDEN_ATMO_L_FLOAT(moon_ray_threshold, 0.35, "0.0,50.0,0.01", "Light Rays")
EDEN_ATMO_U_COLOR(moon_ray_tint, 0.75, 0.82, 1.0, "Light Rays")

// =========================================================================================
// Bloom (Godot Environment glow)
// =========================================================================================
// Native, cheap, and additive above an HDR threshold, so only the sun, moon and their glow bloom
// -- not the whole sky. Turn manage_glow off to own the Environment's glow settings yourself.
EDEN_ATMO_L_BOOL(manage_glow, true, "Bloom")
// Default 0 (glow disabled): measured at ~3ms on a GTX 750 Ti at 1080p -- more than fog and
// light rays combined. The sky shader's analytic corona and glow already make the sun and moon
// read as bright for ~0.2ms. Raise this to opt in; 0.35 is a good starting point.
EDEN_ATMO_L_FLOAT(bloom_intensity, 0.0, "0.0,4.0,0.01", "Bloom")
EDEN_ATMO_L_FLOAT(bloom_threshold, 1.1, "0.0,16.0,0.01", "Bloom")
EDEN_ATMO_L_FLOAT(bloom_strength, 1.0, "0.0,2.0,0.01", "Bloom")

// =========================================================================================
// Transmittance LUT
// =========================================================================================
// Replaces the per-sample sun march with one texture fetch. Off falls back to marching, which
// is correct but markedly slower.
EDEN_ATMO_L_BOOL(lut_enabled, true, "LUT")
// The sun-angle axis needs the resolution: optical depth changes by orders of magnitude within
// a couple of degrees of the horizon.
EDEN_ATMO_L_INT(lut_width, 128, "16,512,1", "LUT")
EDEN_ATMO_L_INT(lut_height, 48, "8,256,1", "LUT")
EDEN_ATMO_L_INT(lut_steps, 24, "4,128,1", "LUT")

// =========================================================================================
// Environment management
// =========================================================================================
EDEN_ATMO_L_BOOL(manage_environment, true, "Environment")
EDEN_ATMO_L_BOOL(manage_ambient, true, "Environment")

// =========================================================================================
// Auto-setup (showcase/debug)
// =========================================================================================
EDEN_ATMO_L_BOOL(auto_create_planet_mesh, true, "Auto Setup")
EDEN_ATMO_L_BOOL(auto_create_lights, true, "Auto Setup")
EDEN_ATMO_L_BOOL(auto_create_environment, true, "Auto Setup")
EDEN_ATMO_L_BOOL(auto_create_cloud_shell, false, "Auto Setup")
EDEN_ATMO_L_BOOL(debug_camera_hud, false, "Auto Setup")
EDEN_ATMO_L_FLOAT(planet_mesh_segments, 512, "8,2048,1", "Auto Setup")
EDEN_ATMO_L_FLOAT(planet_mesh_rings, 256, "4,1024,1", "Auto Setup")
EDEN_ATMO_L_FLOAT(start_altitude, 60.0, "1.0,10000.0,1.0,suffix:m", "Auto Setup")
EDEN_ATMO_L_FLOAT(fly_speed, 800.0, "10.0,10000.0,1.0,suffix:m/s", "Auto Setup")
EDEN_ATMO_L_FLOAT(time_scrub_step, 0.005, "0.0001,0.1,0.0001", "Auto Setup")
EDEN_ATMO_L_BOOL(uncap_fps, true, "Auto Setup")
EDEN_ATMO_L_STRING(space_texture_dir, "", "Auto Setup")
EDEN_ATMO_L_BOOL(use_placeholder_panorama, true, "Auto Setup")
EDEN_ATMO_L_FLOAT(cloud_bottom, 1200.0, "100.0,12000.0,1.0,suffix:m", "Auto Setup")
EDEN_ATMO_L_FLOAT(cloud_top, 1900.0, "200.0,16000.0,1.0,suffix:m", "Auto Setup")
