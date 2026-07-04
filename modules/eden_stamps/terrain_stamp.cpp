#include "terrain_stamp.h"
#include "core/math/math_funcs.h"

TerrainStamp::TerrainStamp() {
}

// ── Heightmap ────────────────────────────────────────────────────────────────

void TerrainStamp::set_heightmap(const Ref<Image> &p_image) {
	heightmap = p_image;
}

Ref<Image> TerrainStamp::get_heightmap() const {
	return heightmap;
}

// ── World-space footprint ────────────────────────────────────────────────────

void TerrainStamp::set_world_size(float p_size) {
	world_size = MAX(1.0f, p_size);
}

float TerrainStamp::get_world_size() const {
	return world_size;
}

void TerrainStamp::set_height_scale(float p_scale) {
	height_scale = p_scale;
}

float TerrainStamp::get_height_scale() const {
	return height_scale;
}

void TerrainStamp::set_base_offset(float p_offset) {
	base_offset = p_offset;
}

float TerrainStamp::get_base_offset() const {
	return base_offset;
}

// ── Blending ─────────────────────────────────────────────────────────────────

void TerrainStamp::set_blend_radius(float p_radius) {
	blend_radius = MAX(0.0f, p_radius);
}

float TerrainStamp::get_blend_radius() const {
	return blend_radius;
}

void TerrainStamp::set_blend_mode(BlendMode p_mode) {
	blend_mode = p_mode;
}

TerrainStamp::BlendMode TerrainStamp::get_blend_mode() const {
	return blend_mode;
}

// ── Material ─────────────────────────────────────────────────────────────────

void TerrainStamp::set_material_index(int p_index) {
	material_index = p_index;
}

int TerrainStamp::get_material_index() const {
	return material_index;
}

// ── Bilinear heightmap sampling ──────────────────────────────────────────────

float TerrainStamp::sample_height_bilinear(float px, float py) const {
	if (heightmap.is_null() || heightmap->is_empty()) {
		return 0.0f;
	}

	const int w = heightmap->get_width();
	const int h = heightmap->get_height();

	// Clamp to valid range
	px = CLAMP(px, 0.0f, (float)(w - 1));
	py = CLAMP(py, 0.0f, (float)(h - 1));

	const int x0 = (int)Math::floor(px);
	const int y0 = (int)Math::floor(py);
	const int x1 = MIN(x0 + 1, w - 1);
	const int y1 = MIN(y0 + 1, h - 1);

	const float fx = px - x0;
	const float fy = py - y0;

	// Read red channel (works for RF, RH, L8, RGBA8 etc.)
	const float h00 = heightmap->get_pixel(x0, y0).r;
	const float h10 = heightmap->get_pixel(x1, y0).r;
	const float h01 = heightmap->get_pixel(x0, y1).r;
	const float h11 = heightmap->get_pixel(x1, y1).r;

	// Bilinear interpolation
	return Math::lerp(Math::lerp(h00, h10, fx), Math::lerp(h01, h11, fx), fy);
}

float TerrainStamp::sample_height_uv(float u, float v) const {
	if (heightmap.is_null() || heightmap->is_empty()) {
		return base_offset;
	}
	const float px = u * (heightmap->get_width() - 1);
	const float py = v * (heightmap->get_height() - 1);
	return sample_height_bilinear(px, py) * height_scale + base_offset;
}

// ── SDF evaluation ───────────────────────────────────────────────────────────
//
// The stamp exists in a local coordinate system where:
//   X, Z ∈ [-world_size/2, +world_size/2]  (horizontal footprint)
//   Y = up (height above/below the placement surface)
//
// The SDF at a local point is computed as:
//   1. Map X,Z to UV in [0,1]
//   2. Sample the heightmap to get surface_y
//   3. SDF = distance from point to stamp surface, with edge fade
//
// Edge fade: outside the stamp footprint, the SDF smoothly returns a large
// positive value (exterior) so it doesn't affect terrain beyond the footprint.

float TerrainStamp::sample_sdf(const Vector3 &p_local_pos) const {
	const float half = world_size * 0.5f;

	// UV mapping: stamp centre = (0.5, 0.5)
	const float u = (p_local_pos.x + half) / world_size;
	const float v = (p_local_pos.z + half) / world_size;

	// Distance from footprint edge (negative = inside, positive = outside)
	const float dx = MAX(0.0f, Math::abs(p_local_pos.x) - half);
	const float dz = MAX(0.0f, Math::abs(p_local_pos.z) - half);
	const float edge_dist = Math::sqrt(dx * dx + dz * dz);

	if (edge_dist > blend_radius) {
		// Too far from the stamp footprint — return large positive SDF
		return edge_dist;
	}

	// Sample the height at this XZ position
	float surface_y;
	if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f) {
		surface_y = sample_height_uv(u, v);
	} else {
		// Outside footprint: use nearest edge height, fading to zero
		const float cu = CLAMP(u, 0.0f, 1.0f);
		const float cv = CLAMP(v, 0.0f, 1.0f);
		surface_y = sample_height_uv(cu, cv);
	}

	// Vertical signed distance to the stamp surface
	const float sdf_vertical = p_local_pos.y - surface_y;

	// If outside the footprint, blend with edge distance
	if (edge_dist > 0.0f) {
		// Combine vertical and horizontal distance for smooth fade
		const float blend_factor = 1.0f - CLAMP(edge_dist / blend_radius, 0.0f, 1.0f);
		// Smooth hermite for nicer transition
		const float smooth = blend_factor * blend_factor * (3.0f - 2.0f * blend_factor);
		// Outside the footprint, SDF grows with edge distance
		return Math::lerp(edge_dist + Math::abs(sdf_vertical), sdf_vertical, smooth);
	}

	return sdf_vertical;
}

// ── Bindings ─────────────────────────────────────────────────────────────────

void TerrainStamp::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_heightmap", "image"), &TerrainStamp::set_heightmap);
	ClassDB::bind_method(D_METHOD("get_heightmap"), &TerrainStamp::get_heightmap);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "heightmap", PROPERTY_HINT_RESOURCE_TYPE, "Image"), "set_heightmap", "get_heightmap");

	ClassDB::bind_method(D_METHOD("set_world_size", "size"), &TerrainStamp::set_world_size);
	ClassDB::bind_method(D_METHOD("get_world_size"), &TerrainStamp::get_world_size);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "world_size", PROPERTY_HINT_RANGE, "1,100000,1"), "set_world_size", "get_world_size");

	ClassDB::bind_method(D_METHOD("set_height_scale", "scale"), &TerrainStamp::set_height_scale);
	ClassDB::bind_method(D_METHOD("get_height_scale"), &TerrainStamp::get_height_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height_scale", PROPERTY_HINT_RANGE, "-10000,10000,0.1"), "set_height_scale", "get_height_scale");

	ClassDB::bind_method(D_METHOD("set_base_offset", "offset"), &TerrainStamp::set_base_offset);
	ClassDB::bind_method(D_METHOD("get_base_offset"), &TerrainStamp::get_base_offset);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "base_offset", PROPERTY_HINT_RANGE, "-10000,10000,0.1"), "set_base_offset", "get_base_offset");

	ClassDB::bind_method(D_METHOD("set_blend_radius", "radius"), &TerrainStamp::set_blend_radius);
	ClassDB::bind_method(D_METHOD("get_blend_radius"), &TerrainStamp::get_blend_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blend_radius", PROPERTY_HINT_RANGE, "0,10000,1"), "set_blend_radius", "get_blend_radius");

	ClassDB::bind_method(D_METHOD("set_blend_mode", "mode"), &TerrainStamp::set_blend_mode);
	ClassDB::bind_method(D_METHOD("get_blend_mode"), &TerrainStamp::get_blend_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "blend_mode", PROPERTY_HINT_ENUM, "Union,Subtract,Replace"), "set_blend_mode", "get_blend_mode");

	ClassDB::bind_method(D_METHOD("set_material_index", "index"), &TerrainStamp::set_material_index);
	ClassDB::bind_method(D_METHOD("get_material_index"), &TerrainStamp::get_material_index);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "material_index", PROPERTY_HINT_RANGE, "-1,255,1"), "set_material_index", "get_material_index");

	ClassDB::bind_method(D_METHOD("sample_sdf", "local_pos"), &TerrainStamp::sample_sdf);
	ClassDB::bind_method(D_METHOD("sample_height_uv", "u", "v"), &TerrainStamp::sample_height_uv);

	BIND_ENUM_CONSTANT(BLEND_UNION);
	BIND_ENUM_CONSTANT(BLEND_SUBTRACT);
	BIND_ENUM_CONSTANT(BLEND_REPLACE);
}
