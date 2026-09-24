#pragma once

#include "core/math/face3.h"
#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/material.h"
#include "scene/resources/image_texture.h"

// Planet-wide ocean surface: the cube-sphere quadtree from demo_eden/ocean_shell.gd (6 per-face
// quadtrees, flat-cube-subdivide-then-normalize, Planetar's `dist < size * factor` split rule,
// unconditional skirts for crack hiding), ported to C++, plus a built-in material ported from
// boujie_water_shader (Gerstner waves, foam, refraction, depth fog, Snell's window) adapted to a
// sphere -- see shaders/planet_ocean.gdshader.
//
// The planet centre is this node's origin. Leaves are MeshInstance3D children, rebuilt only on
// split/merge; wave motion is entirely vertex-shader driven.
class EdenPlanetOcean : public Node3D {
	GDCLASS(EdenPlanetOcean, Node3D);

public:
	// How the water is lit; each step trades generality for GPU time (GTX 750 Ti, 1080p, surface).
	enum LightingMode {
		LIGHTING_GODOT, // Godot's full lighting: every light, sky ambient + radiance reflections.
		LIGHTING_FAST_SKY, // Godot direct lights; sky ambient/reflections from a small sky map (~-2.5 ms).
		LIGHTING_FAST, // Unshaded + Godot's BRDF for the two brightest directional lights (more again).
	};

private:

	struct QuadNode {
		int face = 0;
		Vector2 center; // cube-face-local, each axis in [-1, 1]
		float half_size = 1.0f;
		int depth = 0;
		QuadNode *children[4] = {};
		MeshInstance3D *mesh_instance = nullptr;
		bool is_leaf() const { return children[0] == nullptr; }
	};

	QuadNode *roots[6] = {};
	double rebuild_accum = 0.0;
	// Last viewpoint the tree was built for, so a teleport can force an immediate rebuild instead
	// of leaving the camera over water that has not been subdivided yet.
	LocalVector<Vector3> last_lod_cams;

	float planet_radius = 20000.0f;
	// Biggest a leaf patch is allowed to be; derived from lod0_triangle_size, and shown read-only.
	// The quadtree only ever halves the whole planet, so this picks which power-of-two division the
	// finest patch lands on.
	float min_leaf_world_size = 39.0f;
	float split_distance_factor = 2.0f;
	float near_field_radius = 0.0f;
	float rebuild_interval = 0.25f;
	// Small: geomorphing already closes LOD seams exactly, skirts only cover the gap while an LOD
	// update lags the camera -- and deep skirts were measured as the ocean's single biggest cost.
	float skirt_depth_ratio = 0.03f;
	// Target edge length of a LOD0 triangle -- which, with low_poly_normals on, is the facet size.
	// This is the one knob for mesh density: min_leaf_world_size and grid_resolution are both derived
	// from it and shown read-only. Snapped on assignment to what the mesh can actually build, so the
	// value stored is the value you get.
	float lod0_triangle_size = 4.88f;
	int grid_resolution = 8; // cells per leaf edge; derived, see lod0_triangle_size
	// Cells a patch is aimed at. It is not only the LOD0 triangle count: vertex spacing at EVERY
	// level is patch / cells, so this also decides how much of the wave spectrum a coarse patch can
	// still carry (see the shader's band limiting). Deriving the triangle size purely by shrinking
	// the grid drove it to 2, which left the sea beyond a few hundred metres sampled at ~180 m and
	// therefore perfectly flat. The patch size moves instead, and this stays put.
	static const int NOMINAL_CELLS = 10;
	void _derive_mesh_density();
	LightingMode lighting_mode = LIGHTING_FAST;
	bool screen_space_reflections = false;

	// Waterline foam: every MeshInstance3D in the scene is a candidate (tracked through the tree's
	// node_added/node_removed signals, not rescanned); each frame the nearest ones actually reaching
	// the water are sliced (see _update_ripples).
	HashSet<ObjectID> ripple_candidates;
	HashMap<RID, Vector<Face3>> ripple_face_cache;
	Ref<Image> ripple_image;
	Ref<ImageTexture> ripple_texture;
	float ripple_range = 300.0f;
	float ripple_max_object_size = 80.0f;
	int ripple_max_triangles = 20000;
	void _track_ripple_node(Node *p_node);
	void _untrack_ripple_node(Node *p_node);
	// Directional lights for LIGHTING_FAST, rescanned every couple of seconds.
	LocalVector<ObjectID> fast_lights;
	double fast_light_rescan = 0.0;
	void _update_ripples(double p_delta);
	void _update_fast_lights(double p_delta);
	bool horizon_culling = true;
	Ref<Material> material;

	// Ocean currents: a stream function (domain-warped noise + latitude bands) baked once into a
	// cubemap of divergence-free tangent flow. The shader only samples it -- nothing runs per frame.
	bool current_enabled = true;
	int current_seed = 0;
	float current_frequency = 2.0f;
	float current_warp = 0.6f;
	float current_zonal = 0.35f;
	int current_resolution = 64;
	// Shared with EdenCloudShell::cloud_wind_axis: both the three-cell wind bands and the ocean's
	// zonal bands are defined about it, so tilting one without the other desyncs sea from sky.
	Vector3 current_wind_axis = Vector3(0, 1, 0);
	// Land mask, same decoupled pattern as EdenCloudShell::weather_source_image: a plain equirect
	// Image (EdenPlanetGeneratorV4 heights, or anything hand-painted), never a module dependency.
	Ref<Image> current_land_image;
	int current_land_channel = 0;
	float current_land_sea_level = 0.5f;
	float current_land_coast_band = 0.05f;
	Ref<Cubemap> current_map;
	LocalVector<Vector3> current_flow; // CPU copy: direction * strength per cubemap texel
	bool current_dirty = true;
	float _current_psi(const Vector3 &p_dir, class FastNoiseLite *p_noise) const;

	void _clear_tree();
	void _free_node(QuadNode *p_node);
	void _free_leaf_mesh(QuadNode *p_node);
	void _merge(QuadNode *p_node);
	void _update_node(QuadNode *p_node, const LocalVector<Vector3> &p_cams_local);
	void _build_leaf_mesh(QuadNode *p_node);
	Vector3 _sphere_dir(int p_face, float p_u, float p_v) const;
	Vector3 _camera_position() const;
	void _camera_positions(LocalVector<Vector3> &r_out) const;
	void _apply_material(QuadNode *p_node, const Ref<Material> &p_mat);
	int _count_leaves(const QuadNode *p_node) const;
	void _push_runtime_params();
	void _upgrade_builtin_shader();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void update_lod();
	int get_leaf_count() const;
	// The built-in ocean material (boujie-derived shader, procedural textures, deep-ocean waves).
	// An empty `material` becomes one of these on entering the tree, so it shows in the inspector.
	static Ref<ShaderMaterial> make_default_material();

	void set_planet_radius(float p_value);
	float get_planet_radius() const { return planet_radius; }
	void set_min_leaf_world_size(float p_value);
	float get_min_leaf_world_size() const { return min_leaf_world_size; }
	void set_split_distance_factor(float p_value);
	float get_split_distance_factor() const { return split_distance_factor; }
	void set_near_field_radius(float p_value) { near_field_radius = p_value; }
	float get_near_field_radius() const { return near_field_radius; }
	void set_rebuild_interval(float p_value) { rebuild_interval = p_value; }
	float get_rebuild_interval() const { return rebuild_interval; }
	void set_skirt_depth_ratio(float p_value);
	float get_skirt_depth_ratio() const { return skirt_depth_ratio; }
	void set_grid_resolution(int p_value);
	int get_grid_resolution() const { return grid_resolution; }
	void set_lod0_triangle_size(float p_value);
	float get_lod0_triangle_size() const { return lod0_triangle_size; }
	// On (default): the built-in shader replaces Godot's sky ambient/reflections with a sky
	// gradient a sibling EdenPlanetAtmosphere feeds it -- ~3 ms cheaper (see the shader).
	// Reflect scene geometry (not just the sky) by ray-marching the depth buffer. Fast / Fast Sky
	// lighting modes only; see the shader's ScreenSpaceReflections group.
	void set_screen_space_reflections(bool p_value);
	bool get_screen_space_reflections() const { return screen_space_reflections; }
	void set_lighting_mode(LightingMode p_mode);
	LightingMode get_lighting_mode() const { return lighting_mode; }
	void set_ripple_range(float p_value) { ripple_range = p_value; }
	float get_ripple_range() const { return ripple_range; }
	void set_ripple_max_object_size(float p_value) { ripple_max_object_size = p_value; }
	float get_ripple_max_object_size() const { return ripple_max_object_size; }
	void set_ripple_max_triangles(int p_value) { ripple_max_triangles = p_value; }
	int get_ripple_max_triangles() const { return ripple_max_triangles; }
	void set_horizon_culling(bool p_value) { horizon_culling = p_value; }
	bool get_horizon_culling() const { return horizon_culling; }

	// Rebakes the current cubemap now (normally automatic on the next LOD pass after a change).
	void bake_currents();
	Ref<Cubemap> get_current_map() const { return current_map; }
	// The baked current at a world position: a world-space vector tangent to the sphere, length =
	// strength in 0..1 (what the waves follow). Nearest texel; for gameplay (drift, boats, debris).
	Vector3 get_current_at(const Vector3 &p_world_position) const;
	void set_current_enabled(bool p_value);
	bool get_current_enabled() const { return current_enabled; }
	void set_current_seed(int p_value);
	int get_current_seed() const { return current_seed; }
	void set_current_frequency(float p_value);
	float get_current_frequency() const { return current_frequency; }
	void set_current_warp(float p_value);
	float get_current_warp() const { return current_warp; }
	void set_current_zonal(float p_value);
	float get_current_zonal() const { return current_zonal; }
	void set_current_resolution(int p_value);
	int get_current_resolution() const { return current_resolution; }
	void set_current_wind_axis(const Vector3 &p_value);
	Vector3 get_current_wind_axis() const { return current_wind_axis; }
	void set_current_land_image(const Ref<Image> &p_value);
	Ref<Image> get_current_land_image() const { return current_land_image; }
	void set_current_land_channel(int p_value);
	int get_current_land_channel() const { return current_land_channel; }
	void set_current_land_sea_level(float p_value);
	float get_current_land_sea_level() const { return current_land_sea_level; }
	void set_current_land_coast_band(float p_value);
	float get_current_land_coast_band() const { return current_land_coast_band; }

	void set_material(const Ref<Material> &p_material);
	Ref<Material> get_material() const { return material; }


	~EdenPlanetOcean();
};

VARIANT_ENUM_CAST(EdenPlanetOcean::LightingMode);
