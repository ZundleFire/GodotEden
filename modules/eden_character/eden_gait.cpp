#include "eden_gait.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"

namespace {

const Vector3 X(1, 0, 0);
const Vector3 Y(0, 1, 0);
const Vector3 Z(0, 0, 1);

const char *FINGER_BONES_L[4][3] = { { "Index_01_L", "Index_02_L", "Index_03_L" }, { "middle_01_l", "middle_02_l", "middle_03_l" },
	{ "ring_01_L", "ring_02_L", "ring_03_L" }, { "pinky_01_l", "pinky_02_l", "pinky_03_l" } };
const char *FINGER_BONES_R[4][3] = { { "Index_01_R", "Index_05_R", "Index_03_R" }, { "middle_01_r", "middle_02_r", "middle_03_r" },
	{ "ring_01_R_001", "ring_02_R", "ring_03_R" }, { "pinky_01_r", "pinky_02_r", "pinky_03_r" } };

float bump(float p_x, float p_width = 4.0f) {
	return Math::pow(MAX(Math::cos(p_x), 0.0f), p_width);
}

Quaternion getq(const Dictionary &p, const char *p_key) {
	return p.has(p_key) ? Quaternion(p[p_key]) : Quaternion();
}

} // namespace

Quaternion EdenGait::q(const Vector3 &p_axis, float p_deg) {
	return Quaternion(p_axis, Math::deg_to_rad(p_deg));
}

Dictionary EdenGait::blend(const Dictionary &p_a, const Dictionary &p_b, float p_t) {
	if (p_t <= 0.0f) {
		return p_a;
	}
	if (p_t >= 1.0f) {
		return p_b;
	}
	Dictionary out;
	for (const Variant &k : p_a.keys()) {
		if (String(k) == "pelvis_offset") {
			out[k] = Vector3(p_a[k]).lerp(p_b.has(k) ? Vector3(p_b[k]) : Vector3(), p_t);
		} else {
			out[k] = Quaternion(p_a[k]).slerp(p_b.has(k) ? Quaternion(p_b[k]) : Quaternion(), p_t);
		}
	}
	for (const Variant &k : p_b.keys()) {
		if (!out.has(k)) {
			out[k] = String(k) == "pelvis_offset" ? Variant(Vector3().lerp(Vector3(p_b[k]), p_t)) : Variant(Quaternion().slerp(Quaternion(p_b[k]), p_t));
		}
	}
	return out;
}

void EdenGait::arms(Dictionary &p, float p_lower, float p_swing_l, float p_swing_r, float p_elbow_l, float p_elbow_r, float p_wrist_l, float p_wrist_r) {
	p["UpperArm_L"] = q(X, p_swing_l) * q(Y, p_lower);
	p["UpperArm_R"] = q(X, p_swing_r) * q(Y, -p_lower);
	p["LowerArm_L"] = q(Z, -p_elbow_l);
	p["LowerArm_R"] = q(Z, p_elbow_r);
	// Hands trail the forearm a little, and hang slightly inward
	p["Hand_L"] = q(Z, -p_wrist_l) * q(X, 8.0f);
	p["Hand_R"] = q(Z, p_wrist_r) * q(X, 8.0f);
}

void EdenGait::hands(Dictionary &p, float p_grip) {
	// Relaxed, slightly curled fingers (more toward the little finger); grip 0..1 closes them
	for (int i = 0; i < 4; i++) {
		const float base = 12.0f + 6.0f * i + 50.0f * p_grip;
		for (int k = 0; k < 3; k++) {
			const float deg = base * (1.0f + 0.3f * k);
			p[FINGER_BONES_L[i][k]] = q(Y, deg);
			p[FINGER_BONES_R[i][k]] = q(Y, -deg);
		}
	}
	p["Thumb_02_L"] = q(Y, 15.0f + 30.0f * p_grip);
	p["Thumb_02_R"] = q(Y, -15.0f - 30.0f * p_grip);
}

void EdenGait::leg(Dictionary &p, const String &p_side, float p_thigh, float p_knee, float p_toe) {
	// Thigh swing (-deg forward), knee bend (+deg), foot kept level with the ground plus toe (+deg toes down)
	p["thigh_" + p_side] = q(X, p_thigh);
	p["calf_" + p_side] = q(X, p_knee);
	p["foot_" + p_side] = q(X, -(p_thigh + p_knee) + p_toe);
}

void EdenGait::spine(Dictionary &p, float p_lean, float p_twist, float p_roll, float p_head_pitch, float p_head_yaw) {
	p["Spine_01"] = q(Z, p_twist * 0.4f) * q(Y, p_roll * 0.5f) * q(X, p_lean * 0.4f);
	p["spine_03"] = q(Z, p_twist * 0.35f) * q(Y, p_roll * 0.3f) * q(X, p_lean * 0.35f);
	p["Spine_05"] = q(Z, p_twist * 0.25f) * q(X, p_lean * 0.25f);
	// The head keeps the gaze level against the lean and roll, then turns where it is told
	p["neck_01"] = q(Z, p_head_yaw * 0.4f) * q(X, p_head_pitch * 0.4f - p_lean * 0.3f);
	p["head"] = q(Z, p_head_yaw * 0.6f) * q(Y, -p_roll * 0.6f) * q(X, p_head_pitch * 0.6f - p_lean * 0.3f);
}

// Standing: breathing, a slow weight shift from foot to foot, and small head movements (incommensurate periods, so it
// never visibly repeats)
Dictionary EdenGait::idle(float p_time) {
	const float TAU = Math::TAU;
	const float b = Math::sin(p_time * TAU / 4.2f); // breathing
	const float shift = Math::sin(p_time * TAU / 9.0f) + 0.3f * Math::sin(p_time * TAU / 3.7f);
	Dictionary p;
	arms(p, 73.0f + 1.5f * b, 3.0f + shift, 3.0f - shift, 14.0f, 14.0f, 4.0f, 4.0f);
	hands(p);
	leg(p, "l", -2.0f - 1.5f * shift, 4.0f + 2.0f * MAX(-shift, 0.0f));
	leg(p, "r", 1.0f + 1.5f * shift, 3.0f + 2.0f * MAX(shift, 0.0f));
	p["pelvis"] = q(Y, 2.5f * shift);
	const float look = 6.0f * Math::sin(p_time * TAU / 11.0f) + 3.0f * Math::sin(p_time * TAU / 5.3f);
	spine(p, 1.0f + 1.2f * b, 0.0f, -2.0f * shift, 1.5f * Math::sin(p_time * TAU / 7.1f), look);
	p["Clavicle_L"] = q(Y, 1.5f * b);
	p["Clavicle_R"] = q(Y, -1.5f * b);
	p["pelvis_offset"] = Vector3(0.012f * shift, 0.0f, -0.012f - 0.004f * b - 0.004f * Math::abs(shift));
	return p;
}

// Walking (gait 0) to running (gait 1), blended continuously
Dictionary EdenGait::locomotion(float p_u, float p_gait) {
	const float PI = Math::PI;
	const float g = CLAMP(p_gait, 0.0f, 1.0f);
	const float stride = Math::lerp(24.0f, 44.0f, g);
	const float knee_swing = Math::lerp(48.0f, 95.0f, g);
	const float knee_stance = Math::lerp(5.0f, 20.0f, g);
	const float lean = Math::lerp(4.0f, 13.0f, g);
	const float arm_swing = Math::lerp(20.0f, 40.0f, g);
	const float elbow = Math::lerp(16.0f, 82.0f, g);
	const float bob = Math::lerp(0.022f, 0.045f, g);
	const float drop = Math::lerp(0.0f, 0.04f, g);
	const float yaw = Math::lerp(6.0f, 9.0f, g);
	const float ph = p_u * Math::TAU;
	const float s = Math::sin(ph);
	const float c = Math::cos(ph);
	Dictionary p;
	// Legs: the left swings forward while c > 0 (knee lifts), strikes at ph = pi/2, then carries the weight (the knee
	// gives a little just after the strike) and pushes off (toes drop) before swinging again. The right is half a
	// cycle behind.
	const float give_l = (ph > PI * 0.5f && ph < PI) ? Math::sin(CLAMP((ph - PI * 0.5f) * 2.0f, 0.0f, PI)) : 0.0f;
	const float ph_r = Math::fposmod(ph + PI, float(Math::TAU));
	const float give_r = (ph_r > PI * 0.5f && ph_r < PI) ? Math::sin(CLAMP((ph_r - PI * 0.5f) * 2.0f, 0.0f, PI)) : 0.0f;
	const float toe_l = -12.0f * bump(ph - PI * 0.5f, 6.0f) + Math::lerp(22.0f, 30.0f, g) * bump(ph - PI * 1.35f, 5.0f);
	const float toe_r = -12.0f * bump(ph_r - PI * 0.5f, 6.0f) + Math::lerp(22.0f, 30.0f, g) * bump(ph_r - PI * 1.35f, 5.0f);
	leg(p, "l", -stride * s, knee_stance + 12.0f * give_l + knee_swing * Math::pow(MAX(c, 0.0f), 1.3f), toe_l);
	leg(p, "r", stride * s, knee_stance + 12.0f * give_r + knee_swing * Math::pow(MAX(-c, 0.0f), 1.3f), toe_r);
	// Arms swing against the legs, a little behind them; hands trail further. Running arms pump with bent elbows.
	const float sl = Math::sin(ph - 0.35f);
	arms(p, Math::lerp(73.0f, 70.0f, g), arm_swing * sl, -arm_swing * sl, elbow + 12.0f * MAX(-sl, 0.0f), elbow + 12.0f * MAX(sl, 0.0f),
			6.0f * Math::sin(ph - 0.8f), -6.0f * Math::sin(ph - 0.8f));
	hands(p, g * 0.6f);
	// Hips: turn toward the leading leg, drop on the swing side, shift over the stance leg; spine counters both
	const float roll = 4.0f * c;
	p["pelvis"] = q(Z, -yaw * s) * q(Y, roll);
	spine(p, lean, yaw * s * 1.5f, -roll * 0.8f, 0.0f, 0.0f);
	p["Clavicle_L"] = q(Z, -3.0f * sl);
	p["Clavicle_R"] = q(Z, -3.0f * sl);
	// Lowest as a foot lands (legs spread), highest as they pass; the sway toward the stance leg
	p["pelvis_offset"] = Vector3(-0.018f * c * (1.0f - g), 0.0f, -drop - bob * s * s);
	return p;
}

// Crouching, standing still (move 0) to crouch-walking (move 1)
Dictionary EdenGait::crouch(float p_u, float p_move, float p_time) {
	const float ph = p_u * Math::TAU;
	const float s = Math::sin(ph) * p_move;
	const float c = Math::cos(ph);
	const float b = Math::sin(p_time * Math::TAU / 3.3f);
	Dictionary p;
	leg(p, "l", -73.0f - 16.0f * s, 112.0f + 18.0f * MAX(c, 0.0f) * p_move);
	leg(p, "r", -70.0f + 16.0f * s, 108.0f + 18.0f * MAX(-c, 0.0f) * p_move);
	arms(p, 66.0f, -25.0f + 12.0f * s, -25.0f - 12.0f * s, 45.0f, 45.0f, 5.0f, 5.0f);
	hands(p, 0.25f);
	p["pelvis"] = q(Z, -5.0f * s);
	spine(p, 24.0f + 1.5f * b * (1.0f - p_move), 5.0f * s, 0.0f, 0.0f, 0.0f);
	p["pelvis_offset"] = Vector3(0.0f, 0.0f, -0.36f - 0.015f * s * s + 0.006f * b * (1.0f - p_move));
	return p;
}

// In the air: rising (vertical speed > 0) extended with the arms up, falling with the knees tucked and arms out
Dictionary EdenGait::air(float p_vertical_speed, float p_time) {
	const float rise = CLAMP(p_vertical_speed / 4.0f, 0.0f, 1.0f);
	const float s = Math::sin(p_time * Math::TAU / 1.2f);
	Dictionary p;
	leg(p, "l", Math::lerp(-30.0f - 6.0f * s, -10.0f, rise), Math::lerp(50.0f + 6.0f * s, 12.0f, rise), Math::lerp(10.0f, 25.0f, rise));
	leg(p, "r", Math::lerp(-18.0f + 6.0f * s, -4.0f, rise), Math::lerp(38.0f - 6.0f * s, 8.0f, rise), Math::lerp(10.0f, 25.0f, rise));
	// Arms out for balance; rising they lift a little forward and bend, as after a swing up
	arms(p, Math::lerp(45.0f + 4.0f * s, 52.0f, rise), Math::lerp(-12.0f, -22.0f, rise), Math::lerp(-8.0f, -18.0f, rise), Math::lerp(30.0f, 55.0f, rise),
			Math::lerp(30.0f, 50.0f, rise));
	hands(p, 0.1f);
	spine(p, Math::lerp(6.0f, -4.0f, rise), 0.0f, 0.0f, 0.0f, 0.0f);
	p["pelvis_offset"] = Vector3();
	return p;
}

// In water: treading (move 0: upright, legs cycling, hands sculling) to front crawl (move 1: body level, face-down, arms
// windmilling, legs fluttering). u is the stroke phase (one cycle = one stroke of each arm).
Dictionary EdenGait::swim(float p_u, float p_move, float p_time) {
	const float TAU = Math::TAU;
	const float ph = p_u * TAU;
	const float m = CLAMP(p_move, 0.0f, 1.0f);
	Dictionary p;
	// Crawl arms: each turns a full circle about the shoulder, pulling under the body, recovering over the back (the
	// right half a cycle behind); treading hands scull side to side in front of the chest
	const float crawl_l = -180.0f + 360.0f * p_u;
	const float crawl_r = crawl_l + 180.0f;
	const float scull = 18.0f * Math::sin(p_time * TAU / 1.4f);
	Dictionary tread;
	arms(tread, 62.0f, -35.0f + scull, -35.0f - scull, 50.0f, 50.0f, 10.0f * Math::sin(p_time * TAU / 1.4f), -10.0f * Math::sin(p_time * TAU / 1.4f));
	Dictionary crawl;
	arms(crawl, 82.0f, crawl_l, crawl_r, 25.0f + 20.0f * MAX(Math::sin(ph), 0.0f), 25.0f + 20.0f * MAX(-Math::sin(ph), 0.0f));
	for (const Variant &k : tread.keys()) {
		p[k] = Quaternion(tread[k]).slerp(Quaternion(crawl[k]), m);
	}
	hands(p, 0.15f);
	// Legs: slow eggbeater treading, a quick shallow flutter kick when swimming
	const float e = p_time * TAU / 1.1f;
	const float kick = Math::sin(ph * 3.0f);
	leg(p, "l", Math::lerp(-35.0f + 18.0f * Math::sin(e), 12.0f * kick, m), Math::lerp(70.0f + 25.0f * Math::cos(e), 12.0f + 10.0f * MAX(kick, 0.0f), m),
			Math::lerp(10.0f, 45.0f, m));
	leg(p, "r", Math::lerp(-35.0f - 18.0f * Math::sin(e), -12.0f * kick, m), Math::lerp(70.0f - 25.0f * Math::cos(e), 12.0f + 10.0f * MAX(-kick, 0.0f), m),
			Math::lerp(10.0f, 45.0f, m));
	// The body pitches forward about the hips to lie in the water, rolls with the stroke; the head lifts to look ahead
	const float roll = 18.0f * Math::sin(ph) * m;
	p["pelvis"] = q(X, Math::lerp(10.0f, 78.0f, m)) * q(Y, roll * 0.6f);
	spine(p, Math::lerp(4.0f, 2.0f, m), 0.0f, roll, Math::lerp(0.0f, -55.0f, m), 0.0f);
	p["pelvis_offset"] = Vector3(0.0f, 0.0f, Math::lerp(0.0f, 0.5f, m) + 0.02f * Math::sin(p_time * TAU / 1.1f)); // (level, the back just breaks the surface)
	return p;
}

// The squat before a jump (squat 0..1)
Dictionary EdenGait::take_off(float p_squat) {
	Dictionary p;
	leg(p, "l", -45.0f * p_squat, 75.0f * p_squat);
	leg(p, "r", -40.0f * p_squat, 70.0f * p_squat);
	arms(p, 70.0f, 20.0f * p_squat, 20.0f * p_squat, 20.0f, 20.0f);
	hands(p);
	spine(p, 18.0f * p_squat, 0.0f, 0.0f, 0.0f, 0.0f);
	p["pelvis_offset"] = Vector3(0.0f, 0.0f, -0.18f * p_squat);
	return p;
}

Dictionary EdenGait::add_landing(const Dictionary &p_pose, float p_amount) {
	Dictionary p = p_pose.duplicate();
	if (p_amount <= 0.0f) {
		return p;
	}
	const char *sides[2] = { "l", "r" };
	for (const char *side : sides) {
		const String sd = side;
		p["thigh_" + sd] = q(X, -22.0f * p_amount) * (p.has("thigh_" + sd) ? Quaternion(p["thigh_" + sd]) : Quaternion());
		p["calf_" + sd] = q(X, 42.0f * p_amount) * (p.has("calf_" + sd) ? Quaternion(p["calf_" + sd]) : Quaternion());
		p["foot_" + sd] = q(X, -20.0f * p_amount) * (p.has("foot_" + sd) ? Quaternion(p["foot_" + sd]) : Quaternion());
	}
	p["Spine_01"] = q(X, 10.0f * p_amount) * getq(p, "Spine_01");
	p["pelvis_offset"] = (p.has("pelvis_offset") ? Vector3(p["pelvis_offset"]) : Vector3()) + Vector3(0.0f, 0.0f, -0.1f * p_amount);
	return p;
}

void EdenGait::_bind_methods() {
	ClassDB::bind_static_method("EdenGait", D_METHOD("q", "axis", "deg"), &EdenGait::q);
	ClassDB::bind_static_method("EdenGait", D_METHOD("blend", "a", "b", "t"), &EdenGait::blend);
	ClassDB::bind_static_method("EdenGait", D_METHOD("pelvis_rest"), &EdenGait::pelvis_rest);
	ClassDB::bind_static_method("EdenGait", D_METHOD("idle", "time"), &EdenGait::idle);
	ClassDB::bind_static_method("EdenGait", D_METHOD("locomotion", "u", "gait"), &EdenGait::locomotion);
	ClassDB::bind_static_method("EdenGait", D_METHOD("crouch", "u", "move", "time"), &EdenGait::crouch);
	ClassDB::bind_static_method("EdenGait", D_METHOD("air", "vertical_speed", "time"), &EdenGait::air);
	ClassDB::bind_static_method("EdenGait", D_METHOD("swim", "u", "move", "time"), &EdenGait::swim);
	ClassDB::bind_static_method("EdenGait", D_METHOD("take_off", "squat"), &EdenGait::take_off);
	ClassDB::bind_static_method("EdenGait", D_METHOD("add_landing", "pose", "amount"), &EdenGait::add_landing);
}
