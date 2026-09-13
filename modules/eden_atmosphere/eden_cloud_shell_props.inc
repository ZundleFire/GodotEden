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
EDEN_CLOUD_U_FLOAT(cloud_coverage, 0.50, "0.0,1.0,0.001", "Shape")
// Narrow = crisp cutout silhouettes (stylised). Wide = soft fluffy edges (realistic).
EDEN_CLOUD_U_FLOAT(cloud_edge_hardness, 0.16, "0.01,1.0,0.001", "Shape")
// Cel-steps the SHADING (not the density -- banding density bands the alpha and draws
// topographic contour rings). 32 is effectively smooth.
EDEN_CLOUD_U_FLOAT(cloud_facet_levels, 5.0, "1.0,32.0,1.0", "Shape")
// Parallax separation between layers: too little reads as one flat sheet, too much and the
// layers visibly detach.
EDEN_CLOUD_U_FLOAT(cloud_layer_offset, 0.18, "0.0,2.0,0.01", "Shape")
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
// Motion
// =========================================================================================
// Clouds drift by rotating the sample direction about this axis, which keeps motion seamless
// everywhere including over the poles. Consequence: wind speed goes as cos(latitude), so clouds
// are motionless exactly at the poles and fastest at the equator.
EDEN_CLOUD_U_VEC3(cloud_wind_axis, 0.0, 1.0, 0.0, "Motion")
EDEN_CLOUD_U_FLOAT(cloud_drift_speed, 0.004, "0.0,0.05,0.0001", "Motion")

// =========================================================================================
// Lighting
// =========================================================================================
EDEN_CLOUD_U_COLOR(cloud_albedo, 1.0, 0.99, 0.97, "Lighting")
// Surface irradiance. Deliberately separate from the atmosphere's sun_intensity: that is a
// coefficient inside the scattering integral, not a surface brightness, and reusing it here
// overdrives cloud by an order of magnitude and clips everything to flat white.
EDEN_CLOUD_U_FLOAT(cloud_sun_intensity, 2.4, "0.0,12.0,0.01", "Lighting")
EDEN_CLOUD_U_FLOAT(cloud_ambient, 0.12, "0.0,1.0,0.001", "Lighting")
EDEN_CLOUD_U_FLOAT(cloud_light_gain, 1.9, "0.0,4.0,0.01", "Lighting")
// How much direct light a back-lit face keeps. 0 = black silhouettes, 1 = no front/back
// distinction, which makes every cloud read as uniformly bright paper.
EDEN_CLOUD_U_FLOAT(cloud_backlit_floor, 0.30, "0.0,1.0,0.01", "Lighting")
// Beer-Powder absorption; the powder term is what stops flat alpha-blended cloud reading as fog.
EDEN_CLOUD_U_FLOAT(cloud_absorption, 2.2, "0.1,8.0,0.01", "Lighting")
EDEN_CLOUD_U_FLOAT(cloud_density_scale, 0.004, "0.0,0.05,0.0001", "Lighting")
// Multiplier on alpha/opacity accumulation: higher values make clouds more opaque and solid,
// completely occluding background stars and space panoramas.
EDEN_CLOUD_U_FLOAT(cloud_opacity, 1.5, "0.0,8.0,0.01", "Lighting")

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
EDEN_CLOUD_U_FLOAT(cloud_glow_strength, 1.6, "0.0,8.0,0.01", "Edge Glow")
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
