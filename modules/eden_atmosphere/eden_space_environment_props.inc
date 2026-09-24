// Property table for EdenSpaceEnvironment. Same convention as eden_planet_atmosphere_props.inc:
// EDEN_SE_U_* is pushed into the sky material every frame (mirrors the shader's uniform name
// exactly, so the push is a mechanical SNAME(#m_name) with no renaming table).
// EDEN_SE_L_* is CPU-side only.

// =========================================================================================
// Stars
// =========================================================================================
EDEN_SE_U_FLOAT(star_intensity, 1.0, "0.0,4.0,0.01", "Stars")
EDEN_SE_U_FLOAT(star_density, 0.02, "0.0,0.2,0.001", "Stars")
EDEN_SE_U_FLOAT(star_scale, 400.0, "50.0,2000.0,1.0", "Stars")
// Radius of each star as a fraction of its lattice cell.
EDEN_SE_U_FLOAT(star_size, 0.4, "0.02,0.5,0.001", "Stars")
EDEN_SE_U_FLOAT(star_brightness_variation, 0.75, "0.0,1.0,0.01", "Stars")
// 0 all white, 1 full blue-white to orange spread.
EDEN_SE_U_FLOAT(star_color_variation, 0.3, "0.0,1.0,0.01", "Stars")
EDEN_SE_U_FLOAT(star_twinkle, 0.0, "0.0,1.0,0.01", "Stars")
EDEN_SE_U_FLOAT(star_twinkle_speed, 3.0, "0.0,20.0,0.01", "Stars")
// Fakes the eye adaptation the pipeline does not have; without it the daytime sky is speckled.
EDEN_SE_U_FLOAT(star_daylight_fade, 80.0, "0.0,400.0,1.0", "Stars")
// Procedural star strength used INSTEAD of star_intensity while a space panorama is set. 0 lets
// the art stand alone; nonzero adds procedural sparkle on top so faint/low-res panoramas still
// read as a proper starfield instead of just a dim nebula wash.
EDEN_SE_L_FLOAT(star_intensity_with_panorama, 0.5, "0.0,4.0,0.01", "Stars")

// =========================================================================================
// Deep space panorama
// =========================================================================================
EDEN_SE_U_COLOR(space_tint, 1.0, 1.0, 1.0, "Space")
// Default is 8, not 1. Photographed/rendered HDR space panoramas are authored at physical-ish
// levels -- the supplied set averages ~0.023 luminance -- so at 1.0 they are too faint to survive
// tonemapping and read as "the panorama is not loading". Measured, not guessed.
EDEN_SE_U_FLOAT(space_intensity, 1.6, "0.0,64.0,0.01", "Space")
EDEN_SE_U_FLOAT(space_overlay_intensity, 1.6, "0.0,64.0,0.01", "Space")
// Grading, applied to both slots after intensity. Black point crushes the faint noise floor of a
// downscaled HDR; gamma (pivot 1.0) above 1 sinks the dim wash while bright stars stay; contrast
// pivots at mid-grey 0.18; saturation 0 is greyscale.
EDEN_SE_U_FLOAT(space_black_point, 0.05, "0.0,1.0,0.001", "Space")
EDEN_SE_U_FLOAT(space_gamma, 1.6, "0.2,4.0,0.01", "Space")
EDEN_SE_U_FLOAT(space_contrast, 1.0, "0.2,4.0,0.01", "Space")
EDEN_SE_U_FLOAT(space_saturation, 1.0, "0.0,3.0,0.01", "Space")
EDEN_SE_U_FLOAT(space_opacity, 1.0, "0.0,1.0,0.01", "Space")
// Same trick as star_daylight_fade: with no eye adaptation in the pipeline, a nebula/starfield
// panorama would otherwise stay just as visible at noon as at midnight. 0 disables fading
// (panorama always fully visible); the two slots fade independently so, e.g., a faint nebula
// overlay can vanish well before the base starfield does.
EDEN_SE_U_FLOAT(space_daylight_fade, 60.0, "0.0,400.0,1.0", "Space")
EDEN_SE_U_FLOAT(space_overlay_daylight_fade, 60.0, "0.0,400.0,1.0", "Space")
// Axis the star sphere turns about. Separate from planet_spin_axis so the galactic plane can be
// tilted relative to the planet's rotation, which is the normal case in reality.
EDEN_SE_U_VEC3(space_rotation_axis, 0.0, 1.0, 0.0, "Space")
// How strongly the starfield tracks the day. 1 is physically correct -- the celestial sphere is
// fixed while the planet turns, so stars wheel overhead at the same rate the sun moves. 0 pins
// them, which looks wrong at night but is occasionally wanted for a static backdrop. Needs the
// sibling EdenPlanetAtmosphere's sun_time_of_day, since this node has no day/night clock of its own.
EDEN_SE_L_FLOAT(space_day_coupling, 1.0, "0.0,1.0,0.01", "Space")
// Fixed offset, for orienting a galaxy band where you want it.
EDEN_SE_L_FLOAT(space_rotation_offset_deg, 0.0, "-360.0,360.0,0.1,suffix:°", "Space")
// Extra offset on top of the above, for the overlay slot alone -- lets a nebula overlay sit at
// its own angle relative to the base starfield instead of being locked to the same orientation.
EDEN_SE_L_FLOAT(space_overlay_rotation_offset_deg, 0.0, "-360.0,360.0,0.1,suffix:°", "Space")

// =========================================================================================
// Panorama browsing (folder of equirectangular space panoramas -- previously demo-only GDScript)
// =========================================================================================
EDEN_SE_L_STRING(space_texture_dir, "", "Panorama Browsing")
// Generated orientation/seam-test panorama, used when space_texture_dir is empty (or unreadable)
// and no panorama has been assigned directly via set_space_panorama().
EDEN_SE_L_BOOL(use_placeholder_panorama, true, "Panorama Browsing")
