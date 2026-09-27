#ifndef EDEN_GAIT_H
#define EDEN_GAIT_H

#include "core/object/object.h"
#include "core/variant/dictionary.h"

// Pose math for the EDEN_Male rig, used by EdenAnimator and the contact-sheet tool. A pose is a Dictionary: bone name
// -> Quaternion (the bone's local rotation; the rig's rests are identity), plus "pelvis_offset" -> Vector3 (from the
// pelvis rest, skeleton space).
//
// Skeleton space: Z up, the character faces -Y, +X is its left. So:
//   pitch (about X): +deg tips bones that point up (spine, head) forward, bones that point down (legs) back:
//     a leg swings forward with -deg, a knee bends with +deg, a foot's toes drop with +deg;
//   UpperArm_L/R about Y by +deg/-deg lowers the arms from the T-pose;
//   LowerArm_L/R about Z by -deg/+deg bends the elbows forward;
//   pelvis about Z by -deg brings the left hip forward; about Y by +deg lifts the left hip.
//
// Phase u (0..1) is one gait cycle of two steps: the left heel strikes at u = 0.25, the right at 0.75.
class EdenGait : public Object {
	GDCLASS(EdenGait, Object);

public:
	static constexpr float WALK_CYCLE_DISTANCE = 1.4f; // ground distance of one cycle (two steps), walking
	static constexpr float RUN_CYCLE_DISTANCE = 3.2f; // and running: the runtime phase advances by distance over this
	static Vector3 pelvis_rest() { return Vector3(0.0f, -0.017697f, 0.831509f); }

	static Quaternion q(const Vector3 &p_axis, float p_deg);
	// Blends two poses (b over a by t): rotations slerp, the pelvis offset lerps; bones missing from one side use identity
	static Dictionary blend(const Dictionary &p_a, const Dictionary &p_b, float p_t);

	// Building blocks
	static void arms(Dictionary &p, float p_lower, float p_swing_l, float p_swing_r, float p_elbow_l, float p_elbow_r, float p_wrist_l = 0.0f, float p_wrist_r = 0.0f);
	static void hands(Dictionary &p, float p_grip = 0.0f);
	static void leg(Dictionary &p, const String &p_side, float p_thigh, float p_knee, float p_toe = 0.0f);
	static void spine(Dictionary &p, float p_lean, float p_twist = 0.0f, float p_roll = 0.0f, float p_head_pitch = 0.0f, float p_head_yaw = 0.0f);

	// Poses
	static Dictionary idle(float p_time);
	static Dictionary locomotion(float p_u, float p_gait);
	static Dictionary crouch(float p_u, float p_move, float p_time);
	static Dictionary air(float p_vertical_speed, float p_time);
	static Dictionary swim(float p_u, float p_move, float p_time);
	static Dictionary take_off(float p_squat);
	// Absorbing a landing: knees and hips give (amount 0..1, decays in the caller). Returns the pose with it added.
	static Dictionary add_landing(const Dictionary &p_pose, float p_amount);

protected:
	static void _bind_methods();
};

#endif // EDEN_GAIT_H
