#include "eden_animator.h"

#include "eden_gait.h"

#include "core/math/math_funcs.h"
#include "scene/3d/skeleton_3d.h"

namespace {

float smooth(float t) {
	return t * t * (3.0f - 2.0f * t);
}

bool crossed(float a, float b, float x) {
	return b >= a ? (a < x && b >= x) : (a < x || b >= x);
}

} // namespace

void EdenAnimator::land(float p_impact) {
	land_amount = CLAMP(p_impact / 7.0f, 0.15f, 1.0f);
	land_t = 0.0f;
}

int EdenAnimator::_bone(Skeleton3D *p_skel, const String &p_name) {
	const int *found = bones.getptr(p_name);
	if (found != nullptr) {
		return *found;
	}
	const int i = p_skel->find_bone(p_name);
	bones[p_name] = i;
	return i;
}

void EdenAnimator::_process_modification(double p_delta) {
	Skeleton3D *skel = get_skeleton();
	if (skel == nullptr) {
		return;
	}
	const float delta = MIN(float(p_delta), 0.1f);
	time += delta;
	// Eased weights: each follows its target at its own rate (speed-ups snappier than slow-downs feel natural)
	const float moving = airborne ? 0.0f : CLAMP(ground_speed / 0.6f, 0.0f, 1.0f);
	move = Math::move_toward(move, moving, delta * (moving > move ? 5.0f : 3.5f));
	const float gait_target = CLAMP((ground_speed - walk_speed) / MAX(run_speed - walk_speed, 0.01f), 0.0f, 1.0f);
	gait = Math::lerp(gait, gait_target, 1.0f - Math::exp(-delta * 6.0f));
	crouch = Math::move_toward(crouch, crouching ? 1.0f : 0.0f, delta * 5.0f);
	air = Math::move_toward(air, (airborne && !swimming) ? 1.0f : 0.0f, delta * (airborne ? 8.0f : 12.0f));
	swim = Math::move_toward(swim, swimming ? 1.0f : 0.0f, delta * 2.5f);
	land_t += delta;
	Vector2 look_target(look_yaw, look_pitch);
	if (Math::abs(look_yaw) > Math::deg_to_rad(110.0f)) { // looking back over the shoulder: face forward rather than twist round
		look_target = Vector2();
	}
	look = look.lerp(look_target, 1.0f - Math::exp(-delta * 5.0f));

	// Stride phase: one cycle per cycle-distance travelled (crouch steps are shorter)
	float cycle = Math::lerp(EdenGait::WALK_CYCLE_DISTANCE, EdenGait::RUN_CYCLE_DISTANCE, gait);
	cycle = Math::lerp(cycle, 1.0f, crouch);
	const float before = phase;
	phase = Math::fposmod(phase + ground_speed * delta / cycle, 1.0f);
	// Strokes: faster when swimming than treading; each arm entering the water is a splash (the step signal)
	const float stroke_before = stroke;
	stroke = Math::fposmod(stroke + delta * Math::lerp(0.5f, 0.75f, CLAMP(ground_speed / 2.0f, 0.0f, 1.0f)), 1.0f);
	if (swimming && ground_speed > 0.4f) {
		if (crossed(stroke_before, stroke, 0.0f)) {
			emit_signal(SNAME("step"), 0, 0.6f);
		}
		if (crossed(stroke_before, stroke, 0.5f)) {
			emit_signal(SNAME("step"), 1, 0.6f);
		}
	}
	if (!airborne && !swimming && move > 0.3f) {
		const float strength = CLAMP(ground_speed / run_speed, 0.25f, 1.0f) * Math::lerp(1.0f, 0.5f, crouch);
		if (crossed(before, phase, 0.25f)) {
			emit_signal(SNAME("step"), 0, strength);
		}
		if (crossed(before, phase, 0.75f)) {
			emit_signal(SNAME("step"), 1, strength);
		}
	}

	// Standing: idle -> locomotion; crouched: the crouch pose (which has its own still -> moving blend). Only the poses
	// with weight are built.
	Dictionary stand;
	if (crouch < 1.0f) {
		if (move <= 0.0f) {
			stand = EdenGait::idle(time);
		} else if (move >= 1.0f) {
			stand = EdenGait::locomotion(phase, gait);
		} else {
			stand = EdenGait::blend(EdenGait::idle(time), EdenGait::locomotion(phase, gait), smooth(move));
		}
	}
	const Dictionary low = crouch > 0.0f ? EdenGait::crouch(phase, move, time) : Dictionary();
	Dictionary p = EdenGait::blend(stand, low, smooth(crouch));
	if (air > 0.0f) {
		p = EdenGait::blend(p, EdenGait::air(vertical_speed, time), smooth(air));
	}
	if (swim > 0.0f) {
		p = EdenGait::blend(p, EdenGait::swim(stroke, CLAMP(ground_speed / 1.5f, 0.0f, 1.0f), time), smooth(swim));
	}
	// Landing: dips fast, recovers over a third of a second
	if (land_t < 0.6f) {
		p = EdenGait::add_landing(p, land_amount * MIN(land_t / 0.06f, 1.0f) * Math::exp(-land_t / 0.14f));
	}
	// The head and neck turn toward the look direction on top of the pose (a head turns ~70 deg, tips ~35)
	const float yaw = Math::rad_to_deg(CLAMP(look.x, -1.2f, 1.2f));
	const float pitch = -Math::rad_to_deg(CLAMP(look.y, -0.6f, 0.5f));
	const Vector3 X(1, 0, 0), Z(0, 0, 1);
	p["neck_01"] = EdenGait::q(Z, yaw * 0.35f) * EdenGait::q(X, pitch * 0.4f) * (p.has("neck_01") ? Quaternion(p["neck_01"]) : Quaternion());
	p["head"] = EdenGait::q(Z, yaw * 0.5f) * EdenGait::q(X, pitch * 0.6f) * (p.has("head") ? Quaternion(p["head"]) : Quaternion());
	p["spine_03"] = EdenGait::q(Z, yaw * 0.15f) * (p.has("spine_03") ? Quaternion(p["spine_03"]) : Quaternion());
	_apply(skel, p);

	if (swim > 0.5f) {
		state = ground_speed > 0.4f ? "swim" : "tread";
	} else if (air > 0.5f) {
		state = vertical_speed > 0.5f ? "jump" : "fall";
	} else if (crouching) {
		state = ground_speed > 0.15f ? "crouch_walk" : "crouch_idle";
	} else if (ground_speed > (walk_speed + run_speed) * 0.5f) {
		state = "run";
	} else if (ground_speed > 0.15f) {
		state = "walk";
	} else {
		state = "idle";
	}
}

void EdenAnimator::_apply(Skeleton3D *p_skel, const Dictionary &p_pose) {
	for (const Variant &kv : p_pose.keys()) {
		const String k = kv;
		if (k == "pelvis_offset") {
			const int i = _bone(p_skel, "pelvis");
			if (i >= 0) {
				p_skel->set_bone_pose_position(i, EdenGait::pelvis_rest() + Vector3(p_pose[kv]));
			}
		} else {
			const int i = _bone(p_skel, k);
			if (i >= 0) {
				p_skel->set_bone_pose_rotation(i, p_pose[kv]);
			}
		}
	}
	// Bones a pose leaves out (clavicles when crouched, say) go back to rest rather than keeping a stale angle
	for (const KeyValue<String, int> &e : bones) {
		if (e.value >= 0 && !p_pose.has(e.key)) {
			p_skel->set_bone_pose_rotation(e.value, Quaternion());
		}
	}
}

void EdenAnimator::_bind_methods() {
	ClassDB::bind_method(D_METHOD("land", "impact"), &EdenAnimator::land);
	ClassDB::bind_method(D_METHOD("get_state"), &EdenAnimator::get_state);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "state", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_state");
#define EDEN_ANIM_BIND(m_vtype, m_name)                                                          \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &EdenAnimator::set_##m_name);       \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &EdenAnimator::get_##m_name);                \
	ADD_PROPERTY(PropertyInfo(Variant::m_vtype, #m_name), "set_" #m_name, "get_" #m_name);
	EDEN_ANIM_BIND(FLOAT, ground_speed)
	EDEN_ANIM_BIND(BOOL, airborne)
	EDEN_ANIM_BIND(BOOL, crouching)
	EDEN_ANIM_BIND(BOOL, swimming)
	EDEN_ANIM_BIND(FLOAT, vertical_speed)
	EDEN_ANIM_BIND(FLOAT, look_yaw)
	EDEN_ANIM_BIND(FLOAT, look_pitch)
	EDEN_ANIM_BIND(FLOAT, walk_speed)
	EDEN_ANIM_BIND(FLOAT, run_speed)
#undef EDEN_ANIM_BIND
	ADD_SIGNAL(MethodInfo("step", PropertyInfo(Variant::INT, "foot"), PropertyInfo(Variant::FLOAT, "strength")));
}
