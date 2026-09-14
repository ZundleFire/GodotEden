// Property table for EdenCloudShell. Same X-macro scheme as the atmosphere: included with
// different macro definitions to generate members, accessors, bindings, inspector entries and
// the uniform push, so a new knob is one line here rather than five scattered edits.
//
// EDEN_CLOUD_U_* : pushed to the cloud material. Name MUST match the shader uniform.
// EDEN_CLOUD_L_* : CPU-side only (mesh sizing, noise bake settings).
//
// NOTE on planet_radius / planet_center: EdenPlanetAtmosphere also pushes these to every linked
// material and is authoritative for what the SHADER sees. The copies here size the carrier mesh.
// Keep them equal; they are separate only so a CloudShell works standalone without an
// atmosphere node.

// =========================================================================================
// Slab geometry
// =========================================================================================
EDEN_CLOUD_L_FLOAT(planet_radius, 40000.0, "100.0,1000000.0,1.0,or_greater,suffix:m", "Planet")
EDEN_CLOUD_L_VEC3(planet_center, 0.0, 0.0, 0.0, "Planet")
EDEN_CLOUD_U_FLOAT(cloud_bottom, 1200.0, "100.0,12000.0,1.0,suffix:m", "Slab")
EDEN_CLOUD_U_FLOAT(cloud_top, 1900.0, "200.0,16000.0,1.0,suffix:m", "Slab")
// Only affects the silhouette seen from outside; cloud shapes are computed per-fragment from an
// analytic ray/slab intersection, so mesh density does not change them.
EDEN_CLOUD_L_INT(mesh_segments, 96, "8,512,1", "Slab")

// =========================================================================================
// Shape
// =========================================================================================
// Feature size, in "features per radian"-ish units. Multiplies a unit direction vector, so it
// is independent of planet radius.
EDEN_CLOUD_U_FLOAT(cloud_scale, 3.5, "0.5,40.0,0.01", "Shape")
EDEN_CLOUD_U_FLOAT(cloud_coverage, 0.40, "0.0,1.0,0.001", "Shape")
// 0 = crisp, antialiased polygon edges at any distance (stylised). Higher = soft fluffy edges.
EDEN_CLOUD_U_FLOAT(cloud_edge_hardness, 0.14, "0.0,1.0,0.001", "Shape")
// Cel-steps the SHADING (not the density -- banding density bands the alpha and draws
// topographic contour rings). 32 is effectively smooth.
EDEN_CLOUD_U_FLOAT(cloud_facet_levels, 5.0, "1.0,32.0,1.0", "Shape")
// Parallax separation between layers: too little reads as one flat sheet, too much and the
// layers visibly detach.
EDEN_CLOUD_U_FLOAT(cloud_layer_offset, 0.0, "0.0,2.0,0.01", "Shape")
// Volumetric layering: with cloud_layer_offset 0 all layers share one shape, and layers away from the
// slab middle are shrunk inward by this much, so the rim is covered by one layer (translucent, glowing)
// and the core by all of them (solid).
EDEN_CLOUD_U_FLOAT(cloud_layer_erosion, 0.22, "0.0,0.5,0.001", "Shape")
// Opacity of the thinnest rim relative to the core. Low = soft translucent edges, 1 = uniformly solid.
EDEN_CLOUD_U_FLOAT(cloud_edge_opacity, 0.12, "0.0,1.0,0.01", "Shape")
// Second octave. Non-integer on purpose -- an integer ratio makes both octaves repeat on the
// same period and the texture tiling becomes obvious.
EDEN_CLOUD_U_FLOAT(cloud_detail_scale, 2.37, "1.1,8.0,0.01", "Shape")
EDEN_CLOUD_U_FLOAT(cloud_detail_weight, 0.25, "0.0,1.0,0.01", "Shape")
// Main quality/cost dial: 1 is a flat sheet, 3 reads as volume, 5+ smoother but linearly dearer.
EDEN_CLOUD_U_INT(cloud_layers, 3, "1,8,1", "Shape")
// Fraction of slab height, from the floor, over which density ramps up from zero. Avoids a
// flat horizontal cutoff at the bottom of the slab.
EDEN_CLOUD_U_FLOAT(cloud_profile_bottom_fade, 0.22, "0.0,0.5,0.001", "Shape")
// Height (0..1 of the slab) above which density ramps back down to zero at the ceiling. A ray
// from outside the shell looking near its own horizon -- the normal way clouds are seen from
// orbit -- travels almost entirely at height close to 1.0, so a low value here (the old 0.70)
// fades exactly that view to nothing and makes the whole cloud layer read as invisible from
// space. Close to 1.0 keeps just enough fade to avoid a razor-flat ceiling seen edge-on from
// inside the slab, without erasing the view from outside it.
EDEN_CLOUD_U_FLOAT(cloud_profile_top_fade, 0.96, "0.5,1.0,0.001", "Shape")

// =========================================================================================
// Style
// =========================================================================================
// 0 = soft noise-contour clouds, 1 = the field triangulated on a tetrahedral lattice: straight-edged
// polygon silhouettes with one flat shade per facet. In between, soft bodies with faceted edges.
EDEN_CLOUD_U_FLOAT(cloud_facet_mix, 0.12, "0.0,1.0,0.01", "Style")
// Lattice cells per unit of noise space. Higher = smaller polygons.
EDEN_CLOUD_U_FLOAT(cloud_facet_scale, 12.0, "1.0,48.0,0.01", "Style")
// How strongly each facet's slope tilts its lighting normal. 0 lights every facet the same.
EDEN_CLOUD_U_FLOAT(cloud_facet_relief, 0.1, "0.0,1.0,0.001", "Style")

// =========================================================================================
// Cloud types: regional shape variety
// =========================================================================================
// Rounds the vertical profile into a bulging dome (cumulus) instead of a flat slab (stratus).
EDEN_CLOUD_U_FLOAT(cloud_puffiness, 0.55, "0.0,1.0,0.01", "Style")
// Stretches noise along the wind direction into thin streaky wisps (cirrus).
EDEN_CLOUD_U_FLOAT(cloud_wispiness, 0.25, "0.0,1.0,0.01", "Style")
// How much puffiness/wispiness vary from one region of sky to another, so puffy and wispy patches
// both appear instead of the whole sky reading as one cloud type.
EDEN_CLOUD_U_FLOAT(cloud_type_variation, 0.6, "0.0,1.0,0.01", "Style")
EDEN_CLOUD_U_FLOAT(cloud_type_scale, 0.35, "0.01,2.0,0.001", "Style")
// Shadowed faces take the sky's own colour -- blue by day, violet at dusk -- tinted by this, instead
// of being a darker copy of the sunlight.
EDEN_CLOUD_U_COLOR(cloud_shadow_color, 0.72, 0.78, 0.95, "Style")
EDEN_CLOUD_U_FLOAT(cloud_sky_ambient_mix, 0.7, "0.0,1.0,0.01", "Style")
EDEN_CLOUD_U_FLOAT(cloud_sky_ambient_gain, 1.2, "0.0,8.0,0.01", "Style")

// =========================================================================================
// Weather (flow map + coverage map)
// =========================================================================================
// A cubemap baked from a global circulation model and, optionally, planet data. Clouds are
// advected along its wind field and their coverage is raised or lowered by region -- so they pile
// up over humid ground and windward mountains and thin out over deserts and rain shadows.
EDEN_CLOUD_L_BOOL(weather_enabled, true, "Weather")
// Texels per cube face. Weather is very low frequency; 64 is plenty.
EDEN_CLOUD_L_INT(weather_resolution, 64, "8,256,1", "Weather")
// Trade winds / westerlies / polar easterlies (east-west component).
EDEN_CLOUD_L_FLOAT(wind_zonal_strength, 1.0, "0.0,4.0,0.01", "Weather")
// Poleward / equatorward component of the same three-cell circulation.
EDEN_CLOUD_L_FLOAT(wind_meridional_strength, 0.35, "0.0,4.0,0.01", "Weather")
// How strongly winds are steered along mountain ranges instead of straight over them.
EDEN_CLOUD_L_FLOAT(wind_terrain_deflection, 0.6, "0.0,1.0,0.01", "Weather")
// Latitude cloud belts: the equatorial convergence zone, the dry subtropical highs, the storm tracks.
EDEN_CLOUD_L_FLOAT(coverage_band_strength, 0.6, "0.0,2.0,0.01", "Weather")
EDEN_CLOUD_L_FLOAT(coverage_humidity_strength, 0.8, "0.0,2.0,0.01", "Weather")
// Orographic cloud: moist air forced up over high ground condenses.
EDEN_CLOUD_L_FLOAT(coverage_uplift_strength, 0.5, "0.0,2.0,0.01", "Weather")
EDEN_CLOUD_L_FLOAT(coverage_rain_shadow_strength, 0.7, "0.0,2.0,0.01", "Weather")
// Channel layout of weather_source_image. Defaults match EdenPlanetGeneratorV3.get_climate_debug_image():
// R uplift bias, G temperature, B humidity, A rain shadow.
EDEN_CLOUD_L_INT(weather_humidity_channel, 2, "0,3,1", "Weather")
EDEN_CLOUD_L_INT(weather_uplift_channel, 0, "0,3,1", "Weather")
EDEN_CLOUD_L_INT(weather_rain_shadow_channel, 3, "0,3,1", "Weather")
// Flow-map cycles per second. This sets how fast clouds FORM AND DISSOLVE: each cycle crossfades
// between two advected patterns, so a cloud fades in or out over about 1 / (2 * flow_speed) seconds.
// Keep it low. Regional flow speed on screen is flow_speed x flow_strength.
EDEN_CLOUD_U_FLOAT(flow_speed, 0.002, "0.0,0.2,0.0001", "Weather")
// Advection distance per cycle, in unit-sphere radians. Keep well under a cloud's size or shapes
// visibly smear along the wind before each phase resets.
EDEN_CLOUD_U_FLOAT(flow_strength, 0.2, "0.0,1.0,0.001", "Weather")
// How much the regional coverage map shifts cloud_coverage. 0 ignores the map.
EDEN_CLOUD_U_FLOAT(coverage_map_strength, 1.0, "0.0,2.0,0.01", "Weather")

// =========================================================================================
// Motion
// =========================================================================================
// Clouds drift by rotating the sample direction about this axis, which keeps motion seamless
// everywhere including over the poles. Consequence: wind speed goes as cos(latitude), so clouds
// are motionless exactly at the poles and fastest at the equator.
EDEN_CLOUD_U_VEC3(cloud_wind_axis, 0.0, 1.0, 0.0, "Motion")
// Radians per second. Transport only -- shapes glide intact. Ground speed is this x planet radius:
// 0.001 at 40 km is ~40 m/s, which from under the deck is clearly visible flow.
EDEN_CLOUD_U_FLOAT(cloud_drift_speed, 0.001, "0.0,0.05,0.00001", "Motion")

// =========================================================================================
// Lighting
// =========================================================================================
EDEN_CLOUD_U_COLOR(cloud_albedo, 1.0, 0.99, 0.97, "Lighting")
// Surface irradiance. Deliberately separate from the atmosphere's sun_intensity: that is a
// coefficient inside the scattering integral, not a surface brightness, and reusing it here
// overdrives cloud by an order of magnitude and clips everything to flat white.
EDEN_CLOUD_U_FLOAT(cloud_sun_intensity, 2.4, "0.0,12.0,0.01", "Lighting")
EDEN_CLOUD_U_FLOAT(cloud_ambient, 0.12, "0.0,1.0,0.001", "Lighting")
EDEN_CLOUD_U_FLOAT(cloud_light_gain, 1.0, "0.0,4.0,0.01", "Lighting")
// How much direct light a back-lit face keeps. 0 = black silhouettes, 1 = no front/back
// distinction, which makes every cloud read as uniformly bright paper.
EDEN_CLOUD_U_FLOAT(cloud_backlit_floor, 0.30, "0.0,1.0,0.01", "Lighting")
// Sunlight skimming under the cloud base while the sun is low: what turns sunset clouds orange and
// pink from below instead of leaving them as grey silhouettes against a bright sky.
EDEN_CLOUD_U_FLOAT(cloud_sunset_underlight, 0.5, "0.0,4.0,0.01", "Lighting")
// Beer-Powder absorption; the powder term is what stops flat alpha-blended cloud reading as fog.
EDEN_CLOUD_U_FLOAT(cloud_absorption, 2.0, "0.1,8.0,0.01", "Lighting")
EDEN_CLOUD_U_FLOAT(cloud_density_scale, 0.004, "0.0,0.05,0.0001", "Lighting")
// Multiplier on alpha/opacity accumulation: higher values make clouds more opaque and solid,
// completely occluding background stars and space panoramas.
EDEN_CLOUD_U_FLOAT(cloud_opacity, 3.0, "0.0,8.0,0.01", "Lighting")

// =========================================================================================
// Night lighting
// =========================================================================================
// Ambient starlight/airglow illumination at night, so clouds stay visible and have form in the dark.
EDEN_CLOUD_U_FLOAT(cloud_night_ambient, 0.05, "0.0,1.0,0.001", "Night Lighting")
EDEN_CLOUD_U_COLOR(cloud_night_color, 0.15, 0.18, 0.28, "Night Lighting")
EDEN_CLOUD_U_FLOAT(cloud_night_rim, 0.25, "0.0,2.0,0.01", "Night Lighting")

// =========================================================================================
// Edge glow
// =========================================================================================
EDEN_CLOUD_U_FLOAT(cloud_glow_strength, 2.5, "0.0,8.0,0.01", "Edge Glow")
// A normalised pow() lobe peaking at exactly 1.0, not a Mie phase -- that peaks near 26 at
// these g values and blew the sky out around the sun.
EDEN_CLOUD_U_FLOAT(cloud_glow_sharpness, 16.0, "1.0,64.0,0.1", "Edge Glow")
EDEN_CLOUD_U_FLOAT(cloud_edge_power, 3.5, "0.5,12.0,0.01", "Edge Glow")

// =========================================================================================
// Sampling
// =========================================================================================
// Off by default: with few layers, jitter moves samples between facet shading steps and reads
// as a heavy stipple rather than smooth dithering.
EDEN_CLOUD_U_FLOAT(cloud_jitter, 0.0, "0.0,1.0,0.01", "Sampling")

// =========================================================================================
// Baked noise
// =========================================================================================
// 64 is 262k texels and plenty at these feature sizes; 128 costs 8x memory and bake time.
EDEN_CLOUD_L_INT(noise_resolution, 64, "16,256,1", "Baked Noise")
EDEN_CLOUD_L_FLOAT(noise_frequency, 0.035, "0.001,0.5,0.0001", "Baked Noise")
EDEN_CLOUD_L_INT(noise_octaves, 4, "1,8,1", "Baked Noise")
EDEN_CLOUD_L_FLOAT(noise_gain, 0.5, "0.0,1.0,0.01", "Baked Noise")
EDEN_CLOUD_L_FLOAT(noise_lacunarity, 2.0, "1.0,4.0,0.01", "Baked Noise")
// Domain warp, baked at generation time. This gives the torn, angular silhouettes; it used to
// be ~75% of the shader's runtime cost and is now free.
EDEN_CLOUD_L_FLOAT(warp_amplitude, 28.0, "0.0,200.0,0.1", "Baked Noise")
EDEN_CLOUD_L_INT(warp_octaves, 2, "1,6,1", "Baked Noise")
EDEN_CLOUD_L_INT(noise_seed, 1337, "-100000,100000,1", "Baked Noise")
