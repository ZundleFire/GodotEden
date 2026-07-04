#ifndef TERRAIN_STAMP_H
#define TERRAIN_STAMP_H

#include "core/io/resource.h"
#include "core/variant/variant.h"
#include "core/io/image.h"

/// TerrainStamp — A placeable heightmap patch resource.
///
/// Holds a 2D heightmap (Image, single-channel float or RF), plus
/// parameters that control how it blends into the planet's SDF.
///
/// Workflow:
///   1. Generate or paint a heightmap in an Image (e.g. 256×256).
///   2. Optionally run HydraulicErosion on it for realistic detail.
///   3. Wrap it in a TerrainStamp resource, set world_size and height_scale.
///   4. Place it on the planet via TerrainStampPlacer.
///
/// The stamp is projected downward along a configurable "up" direction
/// (default: radial from planet centre).  The blend_radius creates a
/// smooth transition zone around the edges where the stamp fades into
/// the existing terrain.
///
/// The heightmap is sampled with bilinear filtering, normalised to [0,1],
/// then scaled by height_scale to produce a local height offset in metres.

class TerrainStamp : public Resource {
	GDCLASS(TerrainStamp, Resource);

public:
	TerrainStamp();

	// ── Heightmap data ───────────────────────────────────────────────────
	void set_heightmap(const Ref<Image> &p_image);
	Ref<Image> get_heightmap() const;

	// ── World-space footprint ────────────────────────────────────────────
	/// Side length (in metres) of the square footprint on the terrain.
	void set_world_size(float p_size);
	float get_world_size() const;

	/// Vertical scale multiplier applied to the normalised [0,1] heightmap.
	/// e.g. height_scale=500 → stamp peak is 500m above the base.
	void set_height_scale(float p_scale);
	float get_height_scale() const;

	/// Base height offset (metres).  The stamp's "zero" level is shifted
	/// by this amount relative to the placement surface.  Negative values
	/// carve below the surface; positive values raise the base.
	void set_base_offset(float p_offset);
	float get_base_offset() const;

	// ── Blending ─────────────────────────────────────────────────────────
	/// Width of the smooth-blend zone (in metres) around the stamp edge.
	/// Uses polynomial smooth-min to merge stamp SDF into terrain SDF.
	void set_blend_radius(float p_radius);
	float get_blend_radius() const;

	/// Blend mode: 0 = smooth union (additive), 1 = smooth subtract (carve),
	/// 2 = replace (hard stamp).
	enum BlendMode {
		BLEND_UNION = 0,
		BLEND_SUBTRACT = 1,
		BLEND_REPLACE = 2,
	};
	void set_blend_mode(BlendMode p_mode);
	BlendMode get_blend_mode() const;

	// ── Material override ────────────────────────────────────────────────
	/// If >= 0, voxels created by this stamp get this material index.
	/// If < 0, material is inherited from existing terrain.
	void set_material_index(int p_index);
	int get_material_index() const;

	// ── SDF evaluation ───────────────────────────────────────────────────
	/// Sample the stamp's SDF contribution at a local position.
	/// local_pos is relative to the stamp centre (stamp-space), where:
	///   X,Z are horizontal, Y is up.
	///   The stamp footprint spans [-world_size/2, +world_size/2] in X and Z.
	/// Returns a signed distance value (negative = inside stamp surface).
	float sample_sdf(const Vector3 &p_local_pos) const;

	/// Sample the raw height at normalised UV (0-1 range).
	/// Returns height in metres (heightmap value × height_scale + base_offset).
	float sample_height_uv(float u, float v) const;

	/// Get the heightmap value at pixel coords with bilinear filtering.
	float sample_height_bilinear(float px, float py) const;

protected:
	static void _bind_methods();

private:
	Ref<Image> heightmap;
	float world_size = 1000.0f;    // metres
	float height_scale = 500.0f;   // metres
	float base_offset = 0.0f;      // metres
	float blend_radius = 100.0f;   // metres
	BlendMode blend_mode = BLEND_UNION;
	int material_index = -1;
};

VARIANT_ENUM_CAST(TerrainStamp::BlendMode);

#endif // TERRAIN_STAMP_H
