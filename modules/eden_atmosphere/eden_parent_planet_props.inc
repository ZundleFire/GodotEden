// Property table for EdenParentPlanet. Same convention as eden_planet_atmosphere_props.inc:
// EDEN_PP_U_* is pushed into the sky material every frame (mirrors the shader's uniform name
// exactly, "parent_planet_*", so the push is a mechanical SNAME(#m_name) with no renaming table).
// EDEN_PP_L_* is CPU-side only.

EDEN_PP_L_BOOL(parent_planet_enabled, false, "Parent Planet")
EDEN_PP_U_VEC3(parent_planet_direction, 0.0, 0.5, 0.866, "Parent Planet")
EDEN_PP_U_FLOAT(parent_planet_angular_radius, 0.3, "0.5,90.0,0.01,radians_as_degrees", "Parent Planet")
EDEN_PP_U_VEC3(parent_planet_up, 0.0, 1.0, 0.0, "Parent Planet")
EDEN_PP_U_FLOAT(parent_planet_brightness, 1.6, "0.05,20.0,0.01", "Parent Planet")
EDEN_PP_U_FLOAT(parent_planet_limb_darkening, 0.25, "0.0,1.0,0.01", "Parent Planet")
EDEN_PP_U_FLOAT(parent_planet_earthshine, 0.02, "0.0,0.5,0.001", "Parent Planet")
// A photo or an equirect surface map; leave empty for the procedural surface below.
EDEN_PP_L_FLOAT(parent_planet_texture_rotation_deg, 0.0, "-180.0,180.0,0.1,suffix:°", "Parent Planet")

// --- Pixel-art procedural surface (ignored once parent_planet_texture is assigned) ---------
EDEN_PP_U_FLOAT(parent_planet_pixel_scale, 110.0, "0.0,400.0,1.0", "Parent Planet Surface")
EDEN_PP_U_INT(parent_planet_noise_cells, 8, "2,64,1", "Parent Planet Surface")
EDEN_PP_U_FLOAT(parent_planet_noise_strength, 1.4, "0.0,4.0,0.01", "Parent Planet Surface")
EDEN_PP_U_INT(parent_planet_detail_cells, 18, "2,128,1", "Parent Planet Surface")
EDEN_PP_U_FLOAT(parent_planet_detail_strength, 0.35, "0.0,2.0,0.01", "Parent Planet Surface")
EDEN_PP_U_FLOAT(parent_planet_land_fraction, 0.42, "0.0,1.0,0.01", "Parent Planet Surface")
EDEN_PP_U_FLOAT(parent_planet_mountain_fraction, 0.28, "0.0,1.0,0.01", "Parent Planet Surface")
EDEN_PP_U_FLOAT(parent_planet_ice_fraction, 0.06, "0.0,1.0,0.01", "Parent Planet Surface")
EDEN_PP_U_COLOR(parent_planet_ocean_deep, 0.05, 0.12, 0.28, "Parent Planet Surface")
EDEN_PP_U_COLOR(parent_planet_ocean_shallow, 0.09, 0.22, 0.38, "Parent Planet Surface")
EDEN_PP_U_COLOR(parent_planet_land_low, 0.22, 0.42, 0.20, "Parent Planet Surface")
EDEN_PP_U_COLOR(parent_planet_land_high, 0.62, 0.55, 0.38, "Parent Planet Surface")
EDEN_PP_U_COLOR(parent_planet_ice_color, 0.92, 0.94, 0.97, "Parent Planet Surface")
EDEN_PP_U_FLOAT(parent_planet_rotation, 0.0, "-360.0,360.0,0.05,radians_as_degrees", "Parent Planet Surface")
EDEN_PP_L_FLOAT(parent_planet_band_speed, 0.01, "-1.0,1.0,0.0001", "Parent Planet Surface")
EDEN_PP_U_INT(parent_planet_shade_bands, 5, "1,16,1", "Parent Planet Surface")
EDEN_PP_U_FLOAT(parent_planet_seed, 0.0, "-1000.0,1000.0,1.0", "Parent Planet Surface")

// --- Clouds --------------------------------------------------------------------------------
EDEN_PP_U_FLOAT(parent_planet_cloud_cover, 0.4, "0.0,1.0,0.01", "Parent Planet Clouds")
EDEN_PP_U_INT(parent_planet_cloud_cells, 9, "2,64,1", "Parent Planet Clouds")
EDEN_PP_U_INT(parent_planet_cloud_detail_cells, 20, "2,128,1", "Parent Planet Clouds")
EDEN_PP_U_FLOAT(parent_planet_cloud_detail_strength, 0.4, "0.0,2.0,0.01", "Parent Planet Clouds")
EDEN_PP_U_FLOAT(parent_planet_cloud_rotation, 0.6, "-4.0,4.0,0.01", "Parent Planet Clouds")
EDEN_PP_U_COLOR(parent_planet_cloud_color, 0.98, 0.98, 1.0, "Parent Planet Clouds")
EDEN_PP_U_FLOAT(parent_planet_cloud_shadow, 0.3, "0.0,1.0,0.01", "Parent Planet Clouds")

// --- Atmosphere rim + outer glow -------------------------------------------------------------
EDEN_PP_U_COLOR(parent_planet_atmosphere_color, 0.55, 0.75, 1.0, "Parent Planet Atmosphere")
EDEN_PP_U_FLOAT(parent_planet_atmosphere_power, 4.0, "0.5,16.0,0.01", "Parent Planet Atmosphere")
EDEN_PP_U_FLOAT(parent_planet_atmosphere_intensity, 1.0, "0.0,4.0,0.01", "Parent Planet Atmosphere")
EDEN_PP_U_FLOAT(parent_planet_glow_intensity, 0.15, "0.0,8.0,0.01", "Parent Planet Atmosphere")
EDEN_PP_U_FLOAT(parent_planet_glow_size, 0.05, "0.05,45.0,0.01,radians_as_degrees", "Parent Planet Atmosphere")
