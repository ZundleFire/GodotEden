#ifndef EDEN_FOLIAGE_MESHES_H
#define EDEN_FOLIAGE_MESHES_H

#include "core/object/object.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"

class EdenFoliageLayer;
class RandomNumberGenerator;

// Mesh builders for EdenFoliage's MultiMesh layers. Every result is ONE vertex-coloured surface with a shared
// material, so a layer draws in one call per chunk: procedural trees (EdenTreeGenerator), bushes harvested from
// EdenBushInstance, faceted rocks built here, fallen wood built with EdenTreeMesher, and grass tufts. Mesh origins sit
// at the ground contact point, +Y up. Rocks are ~1 m across at scale 1. Results are cached (clear_cache() frees them).
class EdenFoliageMeshes : public Object {
	GDCLASS(EdenFoliageMeshes, Object);

public:
	// Plants sway (the embedded tree shader); rocks and wood use the same shader without sway, so they take snow too
	static Ref<ShaderMaterial> get_material(bool p_wind = true);
	static Ref<Shader> get_tree_shader();
	static Ref<Shader> get_grass_shader();

	// {mesh, shape: [Shape3D, Transform3D] or []} for one variant of a layer (not grass). Cached.
	static Dictionary build(const Ref<EdenFoliageLayer> &p_layer, int p_variant);
	// Far copy: voxelized at `resolution` cells across its width (0: the layer's far_resolution); rocks use their
	// 20-facet version instead. Cached.
	static Ref<Mesh> build_far(const Ref<EdenFoliageLayer> &p_layer, int p_variant, int p_resolution = 0);
	// Middle-distance copy with about `ratio` of the triangles (Godot's mesh simplifier). Cached.
	static Ref<Mesh> build_simplified(const Ref<EdenFoliageLayer> &p_layer, int p_variant, float p_ratio, int p_min_tris = 400);
	// Blocky low-poly copy of a vertex-coloured mesh, faces greedily merged per colour
	static Ref<ArrayMesh> voxelize(const Ref<Mesh> &p_mesh, int p_resolution);
	// Placeholder for "nothing here": the instancer needs a non-null mesh for every LOD slot
	static Ref<ArrayMesh> empty_mesh();

	// {mesh, trunk_radius, trunk_height}; tree_type is EdenTreeShape.TREE_*
	static Dictionary tree(int p_tree_type, int p_season, int p_variant);
	static Ref<ArrayMesh> bush(int p_bush_type, int p_season, int p_variant);
	static Dictionary rock(const Ref<EdenFoliageLayer> &p_layer, int p_variant);
	static Ref<ArrayMesh> wood(bool p_is_log, int p_variant);
	// Grass tuft: `blades` flat triangles, UV.y 0 at the base .. 1 at the tip
	static Ref<ArrayMesh> tuft(int p_blades, const Ref<Material> &p_material);
	// One surface from [[Mesh, Color], ...]
	static Ref<ArrayMesh> merge(const Array &p_parts, bool p_wind);

	static void clear_cache();

protected:
	static void _bind_methods();
};

#endif // EDEN_FOLIAGE_MESHES_H
