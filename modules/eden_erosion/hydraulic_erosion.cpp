#include "hydraulic_erosion.h"
#include "core/math/math_defs.h"
#include "core/math/math_funcs.h"
#include "core/os/os.h"
#include <cmath>
#include <cstdlib>

HydraulicErosion::HydraulicErosion() {
}

// ── Setters / Getters ─────────────────────────────────────────────────────

void HydraulicErosion::set_resolution(int p_res) {
	resolution = MAX(4, p_res);
}
int HydraulicErosion::get_resolution() const { return resolution; }

void HydraulicErosion::set_num_particles(int p_n) {
	num_particles = MAX(0, p_n);
}
int HydraulicErosion::get_num_particles() const { return num_particles; }

void HydraulicErosion::set_inertia(float p_val) { inertia = CLAMP(p_val, 0.0f, 1.0f); }
float HydraulicErosion::get_inertia() const { return inertia; }

void HydraulicErosion::set_capacity_mult(float p_val) { capacity_mult = MAX(0.01f, p_val); }
float HydraulicErosion::get_capacity_mult() const { return capacity_mult; }

void HydraulicErosion::set_deposition_rate(float p_val) { deposition_rate = CLAMP(p_val, 0.0f, 1.0f); }
float HydraulicErosion::get_deposition_rate() const { return deposition_rate; }

void HydraulicErosion::set_erosion_rate(float p_val) { erosion_rate = CLAMP(p_val, 0.0f, 1.0f); }
float HydraulicErosion::get_erosion_rate() const { return erosion_rate; }

void HydraulicErosion::set_gravity(float p_val) { gravity_val = MAX(0.01f, p_val); }
float HydraulicErosion::get_gravity() const { return gravity_val; }

void HydraulicErosion::set_evaporation_rate(float p_val) { evaporation_rate = CLAMP(p_val, 0.0f, 1.0f); }
float HydraulicErosion::get_evaporation_rate() const { return evaporation_rate; }

void HydraulicErosion::set_erosion_radius(int p_val) { erosion_radius = CLAMP(p_val, 1, 20); }
int HydraulicErosion::get_erosion_radius() const { return erosion_radius; }

void HydraulicErosion::set_max_lifetime(int p_val) { max_lifetime = MAX(1, p_val); }
int HydraulicErosion::get_max_lifetime() const { return max_lifetime; }

void HydraulicErosion::set_min_capacity(float p_val) { min_capacity = MAX(0.0001f, p_val); }
float HydraulicErosion::get_min_capacity() const { return min_capacity; }

void HydraulicErosion::set_min_height(float p_val) { min_height = p_val; }
float HydraulicErosion::get_min_height() const { return min_height; }

// ── Brush pre-computation ─────────────────────────────────────────────────

void HydraulicErosion::_build_brush() {
	brush.clear();
	float total_w = 0.0f;
	for (int dy = -erosion_radius; dy <= erosion_radius; dy++) {
		for (int dx = -erosion_radius; dx <= erosion_radius; dx++) {
			float dist = sqrtf((float)(dx * dx + dy * dy));
			if (dist <= (float)erosion_radius) {
				float w = MAX((float)erosion_radius - dist, 0.0f);
				brush.push_back({ dx, dy, w });
				total_w += w;
			}
		}
	}
	if (total_w > 0.0f) {
		for (int i = 0; i < brush.size(); i++) {
			brush.write[i].weight /= total_w;
		}
	}
}

// ── Core erosion loop ─────────────────────────────────────────────────────

Dictionary HydraulicErosion::erode(PackedFloat32Array p_height, PackedFloat32Array p_sediment, int p_seed) {
	Dictionary result;

	const int N = resolution;
	const int total = N * N;

	if (p_height.size() != total) {
		ERR_PRINT(vformat("HydraulicErosion::erode: height array size %d != expected %d", p_height.size(), total));
		result["height"] = p_height;
		result["sediment"] = p_sediment;
		result["completed"] = 0;
		return result;
	}
	if (p_sediment.size() != total) {
		p_sediment.resize(total);
		p_sediment.fill(0.0f);
	}

	_build_brush();

	// Work on raw pointers for speed — copy-on-write semantics of
	// PackedFloat32Array ensures we get our own writable copy.
	float *hdata = p_height.ptrw();
	float *sdata = p_sediment.ptrw();

	// Simple LCG for speed (avoids Godot RNG overhead per particle).
	uint32_t rng_state = (uint32_t)p_seed;
	auto next_rand = [&]() -> float {
		rng_state = rng_state * 1103515245u + 12345u;
		return (float)(rng_state & 0x7FFFFFFFu) / (float)0x7FFFFFFF;
	};

	int completed = 0;

	for (int p = 0; p < num_particles; p++) {
		float pos_x = next_rand() * (float)(N - 1);
		float pos_y = next_rand() * (float)(N - 1);
		float dir_x = 0.0f;
		float dir_y = 0.0f;
		float speed = 1.0f;
		float water = 1.0f;
		float sediment = 0.0f;

		for (int step = 0; step < max_lifetime; step++) {
			int ix = (int)pos_x;
			int iy = (int)pos_y;
			if (ix < 0 || ix >= N - 1 || iy < 0 || iy >= N - 1) {
				break;
			}

			float fx = pos_x - (float)ix;
			float fy = pos_y - (float)iy;

			// Bilinear height and gradient
			int idx00 = iy * N + ix;
			int idx10 = idx00 + 1;
			int idx01 = idx00 + N;
			int idx11 = idx01 + 1;

			float h00 = hdata[idx00];
			float h10 = hdata[idx10];
			float h01 = hdata[idx01];
			float h11 = hdata[idx11];

			float grad_x = (h10 - h00) * (1.0f - fy) + (h11 - h01) * fy;
			float grad_y = (h01 - h00) * (1.0f - fx) + (h11 - h10) * fx;

			// Update direction with inertia
			dir_x = dir_x * inertia - grad_x * (1.0f - inertia);
			dir_y = dir_y * inertia - grad_y * (1.0f - inertia);

			// Normalise direction
			float dir_len = sqrtf(dir_x * dir_x + dir_y * dir_y);
			if (dir_len < 0.0001f) {
				float angle = next_rand() * (float)Math::TAU;
				dir_x = cosf(angle);
				dir_y = sinf(angle);
			} else {
				dir_x /= dir_len;
				dir_y /= dir_len;
			}

			// Move particle
			float new_x = pos_x + dir_x;
			float new_y = pos_y + dir_y;
			if (new_x < 0.0f || new_x >= (float)(N - 1) ||
					new_y < 0.0f || new_y >= (float)(N - 1)) {
				break;
			}

			// Height at new position (bilinear)
			int nix = (int)new_x;
			int niy = (int)new_y;
			float nfx = new_x - (float)nix;
			float nfy = new_y - (float)niy;
			int nidx00 = niy * N + nix;

			float nh = hdata[nidx00] * (1.0f - nfx) * (1.0f - nfy) +
					hdata[nidx00 + 1] * nfx * (1.0f - nfy) +
					hdata[nidx00 + N] * (1.0f - nfx) * nfy +
					hdata[nidx00 + N + 1] * nfx * nfy;

			float h_old = h00 * (1.0f - fx) * (1.0f - fy) +
					h10 * fx * (1.0f - fy) +
					h01 * (1.0f - fx) * fy +
					h11 * fx * fy;

			float h_diff = nh - h_old; // negative = downhill

			// Carrying capacity
			float capacity = MAX(-h_diff, min_capacity) * speed * water * capacity_mult;

			if (sediment > capacity || h_diff > 0.0f) {
				// ── Deposit ──────────────────────────────────────────────
				float deposit_amount;
				if (h_diff > 0.0f) {
					deposit_amount = MIN(sediment, h_diff);
				} else {
					deposit_amount = (sediment - capacity) * deposition_rate;
				}
				sediment -= deposit_amount;

				float w00 = (1.0f - fx) * (1.0f - fy);
				float w10 = fx * (1.0f - fy);
				float w01 = (1.0f - fx) * fy;
				float w11 = fx * fy;

				hdata[idx00] += deposit_amount * w00;
				hdata[idx10] += deposit_amount * w10;
				hdata[idx01] += deposit_amount * w01;
				hdata[idx11] += deposit_amount * w11;

				sdata[idx00] += deposit_amount * w00;
				sdata[idx10] += deposit_amount * w10;
				sdata[idx01] += deposit_amount * w01;
				sdata[idx11] += deposit_amount * w11;
			} else {
				// ── Erode ────────────────────────────────────────────────
				float erode_amount = MIN(
						(capacity - sediment) * erosion_rate,
						-h_diff);

				for (int bi = 0; bi < brush.size(); bi++) {
					int bx = ix + brush[bi].dx;
					int by = iy + brush[bi].dy;
					if (bx >= 0 && bx < N && by >= 0 && by < N) {
						int bidx = by * N + bx;
						float w = brush[bi].weight;
						float delta = erode_amount * w;
						// Don't erode below minimum
						if (hdata[bidx] - delta < min_height) {
							delta = MAX(hdata[bidx] - min_height, 0.0f);
						}
						hdata[bidx] -= delta;
						sdata[bidx] = MAX(sdata[bidx] - delta, 0.0f);
					}
				}
				sediment += erode_amount;
			}

			// Update velocity
			speed = sqrtf(MAX(speed * speed + gravity_val * (-h_diff), 0.01f));

			// Evaporate water
			water *= (1.0f - evaporation_rate);

			// Advance
			pos_x = new_x;
			pos_y = new_y;
		}

		completed++;
	}

	result["height"] = p_height;
	result["sediment"] = p_sediment;
	result["completed"] = completed;
	return result;
}

// ── Binding ───────────────────────────────────────────────────────────────

void HydraulicErosion::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_resolution", "res"), &HydraulicErosion::set_resolution);
	ClassDB::bind_method(D_METHOD("get_resolution"), &HydraulicErosion::get_resolution);

	ClassDB::bind_method(D_METHOD("set_num_particles", "n"), &HydraulicErosion::set_num_particles);
	ClassDB::bind_method(D_METHOD("get_num_particles"), &HydraulicErosion::get_num_particles);

	ClassDB::bind_method(D_METHOD("set_inertia", "val"), &HydraulicErosion::set_inertia);
	ClassDB::bind_method(D_METHOD("get_inertia"), &HydraulicErosion::get_inertia);

	ClassDB::bind_method(D_METHOD("set_capacity_mult", "val"), &HydraulicErosion::set_capacity_mult);
	ClassDB::bind_method(D_METHOD("get_capacity_mult"), &HydraulicErosion::get_capacity_mult);

	ClassDB::bind_method(D_METHOD("set_deposition_rate", "val"), &HydraulicErosion::set_deposition_rate);
	ClassDB::bind_method(D_METHOD("get_deposition_rate"), &HydraulicErosion::get_deposition_rate);

	ClassDB::bind_method(D_METHOD("set_erosion_rate", "val"), &HydraulicErosion::set_erosion_rate);
	ClassDB::bind_method(D_METHOD("get_erosion_rate"), &HydraulicErosion::get_erosion_rate);

	ClassDB::bind_method(D_METHOD("set_gravity", "val"), &HydraulicErosion::set_gravity);
	ClassDB::bind_method(D_METHOD("get_gravity"), &HydraulicErosion::get_gravity);

	ClassDB::bind_method(D_METHOD("set_evaporation_rate", "val"), &HydraulicErosion::set_evaporation_rate);
	ClassDB::bind_method(D_METHOD("get_evaporation_rate"), &HydraulicErosion::get_evaporation_rate);

	ClassDB::bind_method(D_METHOD("set_erosion_radius", "val"), &HydraulicErosion::set_erosion_radius);
	ClassDB::bind_method(D_METHOD("get_erosion_radius"), &HydraulicErosion::get_erosion_radius);

	ClassDB::bind_method(D_METHOD("set_max_lifetime", "val"), &HydraulicErosion::set_max_lifetime);
	ClassDB::bind_method(D_METHOD("get_max_lifetime"), &HydraulicErosion::get_max_lifetime);

	ClassDB::bind_method(D_METHOD("set_min_capacity", "val"), &HydraulicErosion::set_min_capacity);
	ClassDB::bind_method(D_METHOD("get_min_capacity"), &HydraulicErosion::get_min_capacity);

	ClassDB::bind_method(D_METHOD("set_min_height", "val"), &HydraulicErosion::set_min_height);
	ClassDB::bind_method(D_METHOD("get_min_height"), &HydraulicErosion::get_min_height);

	ClassDB::bind_method(D_METHOD("erode", "height_data", "sediment_data", "seed"), &HydraulicErosion::erode);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "resolution"), "set_resolution", "get_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "num_particles"), "set_num_particles", "get_num_particles");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "inertia"), "set_inertia", "get_inertia");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "capacity_mult"), "set_capacity_mult", "get_capacity_mult");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "deposition_rate"), "set_deposition_rate", "get_deposition_rate");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "erosion_rate"), "set_erosion_rate", "get_erosion_rate");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity"), "set_gravity", "get_gravity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "evaporation_rate"), "set_evaporation_rate", "get_evaporation_rate");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "erosion_radius"), "set_erosion_radius", "get_erosion_radius");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_lifetime"), "set_max_lifetime", "get_max_lifetime");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_capacity"), "set_min_capacity", "get_min_capacity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_height"), "set_min_height", "get_min_height");
}
