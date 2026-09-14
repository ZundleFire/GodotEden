// Property table for EdenPlanetRings. Same X-macro scheme as the cloud shell and atmosphere:
// included with different macro definitions to generate members, accessors, bindings, inspector
// entries and the uniform push.
//
// EDEN_RING_U_* : pushed to the ring material. Name MUST match the shader uniform.
// EDEN_RING_L_* : CPU-side only (mesh sizing, bake settings).

// =========================================================================================
// Planet linkage
// =========================================================================================
// Copies here size the ring mesh; EdenPlanetAtmosphere overwrites the shader's own copies each
// frame when linked via add_linked_material(), the same as EdenCloudShell. Keep them equal so
// the mesh sits where the shading expects it, and so rings work standalone with no atmosphere.
EDEN_RING_L_FLOAT(planet_radius, 40000.0, "100.0,1000000.0,1.0,or_greater,suffix:m", "Planet")
EDEN_RING_L_VEC3(planet_center, 0.0, 0.0, 0.0, "Planet")

// =========================================================================================
// Shape
// =========================================================================================
// Distance from the planet centre, as a multiple of planet_radius -- keeps the ring sized
// sensibly if the planet is resized, the way real rings scale with their parent body.
EDEN_RING_U_FLOAT(ring_inner_radius, 1.5, "1.01,10.0,0.001", "Shape")
EDEN_RING_U_FLOAT(ring_outer_radius, 2.4, "1.02,12.0,0.001", "Shape")
// The ring plane's normal. Tilted off the planet's spin axis by default -- a perfectly flush ring
// is the visually rare case, not the norm.
EDEN_RING_U_VEC3(ring_normal, 0.15, 0.97, 0.05, "Shape")
EDEN_RING_L_INT(ring_segments, 256, "32,1024,1", "Shape")

// =========================================================================================
// Style
// =========================================================================================
EDEN_RING_U_COLOR(ring_tint, 0.85, 0.80, 0.72, "Style")
EDEN_RING_U_FLOAT(ring_opacity, 0.85, "0.0,1.0,0.01", "Style")
// How many concentric bands across the ring's width. Higher = finer banding, like Saturn's.
EDEN_RING_U_FLOAT(ring_band_scale, 40.0, "1.0,400.0,0.1", "Style")
// 0 = a smooth gradient of bands, 1 = crisp Saturn-like divisions (gaps, bright/dark rings).
EDEN_RING_U_FLOAT(ring_band_contrast, 0.6, "0.0,1.0,0.01", "Style")
// Overall brightness variation between the darkest and brightest band.
EDEN_RING_U_FLOAT(ring_band_variation, 0.5, "0.0,1.0,0.01", "Style")
// Icy/dusty forward-scatter: the ring glows when the sun is behind it, the way real ring ice
// backlights. 0 = no glow, matte dust look.
EDEN_RING_U_FLOAT(ring_translucency, 0.6, "0.0,1.0,0.01", "Style")
// Darkens the half of the ring sitting in the planet's own shadow.
EDEN_RING_U_FLOAT(ring_self_shadow, 0.8, "0.0,1.0,0.01", "Style")
// Mixed into the band hash, same convention as EdenPlanetAtmosphere's random_seed: a different
// seed gives a different (but repeatable) band pattern.
EDEN_RING_U_FLOAT(ring_seed, 0.0, "-100000.0,100000.0,1.0", "Style")

// NOTE: sun_direction / sun_tint / planet_radius / planet_center are NOT declared here -- they
// come from atmosphere_common.gdshaderinc (included by the shader) and its own defaults cover
// standalone use. EdenPlanetAtmosphere overwrites them each frame once this material is linked
// via add_linked_material(), exactly as it does for EdenCloudShell's material.
