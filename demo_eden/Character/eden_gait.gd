class_name EdenGait
extends RefCounted
## Pose math for the EDEN_Male rig, used by the runtime animator (eden_animator.gd) and the contact sheet
## (_anim_preview.gd). A pose is a Dictionary: bone name -> Quaternion (the bone's local rotation; the
## rig's rests are identity), plus "pelvis_offset" -> Vector3 (from the pelvis rest, skeleton space).
##
## Skeleton space: Z up, the character faces -Y, +X is its left. So:
##   pitch (about X): +deg tips bones that point up (spine, head) forward, bones that point down (legs) back:
##     a leg swings forward with -deg, a knee bends with +deg, a foot's toes drop with +deg;
##   UpperArm_L/R about Y by +deg/-deg lowers the arms from the T-pose;
##   LowerArm_L/R about Z by -deg/+deg bends the elbows forward;
##   pelvis about Z by -deg brings the left hip forward; about Y by +deg lifts the left hip.
##
## Phase u (0..1) is one gait cycle of two steps: the left heel strikes at u = 0.25, the right at 0.75.

const X := Vector3(1, 0, 0)
const Y := Vector3(0, 1, 0)
const Z := Vector3(0, 0, 1)
const PELVIS_REST := Vector3(0.0, -0.017697, 0.831509)
const FINGERS := ["Index", "middle", "ring", "pinky"]
const FINGER_BONES_L := {"Index": ["Index_01_L", "Index_02_L", "Index_03_L"], "middle": ["middle_01_l", "middle_02_l", "middle_03_l"],
		"ring": ["ring_01_L", "ring_02_L", "ring_03_L"], "pinky": ["pinky_01_l", "pinky_02_l", "pinky_03_l"]}
const FINGER_BONES_R := {"Index": ["Index_01_R", "Index_05_R", "Index_03_R"], "middle": ["middle_01_r", "middle_02_r", "middle_03_r"],
		"ring": ["ring_01_R_001", "ring_02_R", "ring_03_R"], "pinky": ["pinky_01_r", "pinky_02_r", "pinky_03_r"]}
## Ground distance of one cycle (two steps), walking and running: the runtime phase advances by distance over this
const WALK_CYCLE_DISTANCE := 1.4
const RUN_CYCLE_DISTANCE := 3.2


static func q(axis: Vector3, deg: float) -> Quaternion:
	return Quaternion(axis, deg_to_rad(deg))


## Blends two poses (b over a by t): rotations slerp, the pelvis offset lerps; bones missing from one side use identity
static func blend(a: Dictionary, b: Dictionary, t: float) -> Dictionary:
	if t <= 0.0:
		return a
	if t >= 1.0:
		return b
	var out := {}
	for k in a:
		if k == "pelvis_offset":
			out[k] = a[k].lerp(b.get(k, Vector3.ZERO), t)
		else:
			out[k] = (a[k] as Quaternion).slerp(b.get(k, Quaternion.IDENTITY), t)
	for k in b:
		if not out.has(k):
			out[k] = Vector3.ZERO.lerp(b[k], t) if k == "pelvis_offset" else Quaternion.IDENTITY.slerp(b[k], t)
	return out


# ------------------------------------------------------------------------------------------------------------
# Building blocks

static func arms(p: Dictionary, lower: float, swing_l: float, swing_r: float, elbow_l: float, elbow_r: float, wrist_l := 0.0, wrist_r := 0.0) -> void:
	p["UpperArm_L"] = q(X, swing_l) * q(Y, lower)
	p["UpperArm_R"] = q(X, swing_r) * q(Y, -lower)
	p["LowerArm_L"] = q(Z, -elbow_l)
	p["LowerArm_R"] = q(Z, elbow_r)
	# Hands trail the forearm a little, and hang slightly inward
	p["Hand_L"] = q(Z, -wrist_l) * q(X, 8.0)
	p["Hand_R"] = q(Z, wrist_r) * q(X, 8.0)


## Relaxed, slightly curled fingers (more toward the little finger); `grip` 0..1 closes them
static func hands(p: Dictionary, grip := 0.0) -> void:
	var i := 0
	for f in FINGERS:
		var base := 12.0 + 6.0 * i + 50.0 * grip
		for k in 3:
			var deg := base * (1.0 + 0.3 * k)
			p[FINGER_BONES_L[f][k]] = q(Y, deg)
			p[FINGER_BONES_R[f][k]] = q(Y, -deg)
		i += 1
	p["Thumb_02_L"] = q(Y, 15.0 + 30.0 * grip)
	p["Thumb_02_R"] = q(Y, -15.0 - 30.0 * grip)


## A leg: thigh swing (-deg forward), knee bend (+deg), foot kept level with the ground plus `toe` (+deg toes down)
static func leg(p: Dictionary, side: String, thigh: float, knee: float, toe := 0.0) -> void:
	p["thigh_" + side] = q(X, thigh)
	p["calf_" + side] = q(X, knee)
	p["foot_" + side] = q(X, -(thigh + knee) + toe)


static func spine(p: Dictionary, lean: float, twist := 0.0, roll := 0.0, head_pitch := 0.0, head_yaw := 0.0) -> void:
	p["Spine_01"] = q(Z, twist * 0.4) * q(Y, roll * 0.5) * q(X, lean * 0.4)
	p["spine_03"] = q(Z, twist * 0.35) * q(Y, roll * 0.3) * q(X, lean * 0.35)
	p["Spine_05"] = q(Z, twist * 0.25) * q(X, lean * 0.25)
	# The head keeps the gaze level against the lean and roll, then turns where it is told
	p["neck_01"] = q(Z, head_yaw * 0.4) * q(X, head_pitch * 0.4 - lean * 0.3)
	p["head"] = q(Z, head_yaw * 0.6) * q(Y, -roll * 0.6) * q(X, head_pitch * 0.6 - lean * 0.3)


static func _bump(x: float, width := 4.0) -> float:
	return pow(maxf(cos(x), 0.0), width)


# ------------------------------------------------------------------------------------------------------------
# Poses

## Standing: breathing, a slow weight shift from foot to foot, and small head movements (incommensurate periods,
## so it never visibly repeats)
static func idle(time: float) -> Dictionary:
	var b := sin(time * TAU / 4.2) # breathing
	var shift := sin(time * TAU / 9.0) + 0.3 * sin(time * TAU / 3.7)
	var p := {}
	arms(p, 73.0 + 1.5 * b, 3.0 + shift, 3.0 - shift, 14.0, 14.0, 4.0, 4.0)
	hands(p)
	leg(p, "l", -2.0 - 1.5 * shift, 4.0 + 2.0 * maxf(-shift, 0.0))
	leg(p, "r", 1.0 + 1.5 * shift, 3.0 + 2.0 * maxf(shift, 0.0))
	p["pelvis"] = q(Y, 2.5 * shift)
	var look := 6.0 * sin(time * TAU / 11.0) + 3.0 * sin(time * TAU / 5.3)
	spine(p, 1.0 + 1.2 * b, 0.0, -2.0 * shift, 1.5 * sin(time * TAU / 7.1), look)
	p["Clavicle_L"] = q(Y, 1.5 * b)
	p["Clavicle_R"] = q(Y, -1.5 * b)
	p["pelvis_offset"] = Vector3(0.012 * shift, 0.0, -0.012 - 0.004 * b - 0.004 * absf(shift))
	return p


## Walking (gait 0) to running (gait 1), blended continuously
static func locomotion(u: float, gait: float) -> Dictionary:
	var g := clampf(gait, 0.0, 1.0)
	var stride := lerpf(24.0, 44.0, g)
	var knee_swing := lerpf(48.0, 95.0, g)
	var knee_stance := lerpf(5.0, 20.0, g)
	var lean := lerpf(4.0, 13.0, g)
	var arm_swing := lerpf(20.0, 40.0, g)
	var elbow := lerpf(16.0, 82.0, g)
	var bob := lerpf(0.022, 0.045, g)
	var drop := lerpf(0.0, 0.04, g)
	var yaw := lerpf(6.0, 9.0, g)
	var ph := u * TAU
	var s := sin(ph)
	var c := cos(ph)
	var p := {}
	# Legs: the left swings forward while c > 0 (knee lifts), strikes at ph = pi/2, then carries the weight (the knee
	# gives a little just after the strike) and pushes off (toes drop) before swinging again. The right is half a
	# cycle behind.
	var give_l := sin(clampf((ph - PI * 0.5) * 2.0, 0.0, PI)) if ph > PI * 0.5 and ph < PI else 0.0
	var ph_r := fposmod(ph + PI, TAU)
	var give_r := sin(clampf((ph_r - PI * 0.5) * 2.0, 0.0, PI)) if ph_r > PI * 0.5 and ph_r < PI else 0.0
	var toe_l := -12.0 * _bump(ph - PI * 0.5, 6.0) + lerpf(22.0, 30.0, g) * _bump(ph - PI * 1.35, 5.0)
	var toe_r := -12.0 * _bump(ph_r - PI * 0.5, 6.0) + lerpf(22.0, 30.0, g) * _bump(ph_r - PI * 1.35, 5.0)
	leg(p, "l", -stride * s, knee_stance + 12.0 * give_l + knee_swing * pow(maxf(c, 0.0), 1.3), toe_l)
	leg(p, "r", stride * s, knee_stance + 12.0 * give_r + knee_swing * pow(maxf(-c, 0.0), 1.3), toe_r)
	# Arms swing against the legs, a little behind them; hands trail further. Running arms pump with bent elbows.
	var sl := sin(ph - 0.35)
	arms(p, lerpf(73.0, 70.0, g), arm_swing * sl, -arm_swing * sl, elbow + 12.0 * maxf(-sl, 0.0), elbow + 12.0 * maxf(sl, 0.0),
			6.0 * sin(ph - 0.8), -6.0 * sin(ph - 0.8))
	hands(p, g * 0.6)
	# Hips: turn toward the leading leg, drop on the swing side, shift over the stance leg; spine counters both
	var roll := 4.0 * c
	p["pelvis"] = q(Z, -yaw * s) * q(Y, roll)
	spine(p, lean, yaw * s * 1.5, -roll * 0.8, 0.0, 0.0)
	p["Clavicle_L"] = q(Z, -3.0 * sl)
	p["Clavicle_R"] = q(Z, -3.0 * sl)
	# Lowest as a foot lands (legs spread), highest as they pass; the sway toward the stance leg
	p["pelvis_offset"] = Vector3(-0.018 * c * (1.0 - g), 0.0, -drop - bob * s * s)
	return p


## Crouching, standing still (move 0) to crouch-walking (move 1)
static func crouch(u: float, move: float, time: float) -> Dictionary:
	var ph := u * TAU
	var s := sin(ph) * move
	var c := cos(ph)
	var b := sin(time * TAU / 3.3)
	var p := {}
	leg(p, "l", -73.0 - 16.0 * s, 112.0 + 18.0 * maxf(c, 0.0) * move)
	leg(p, "r", -70.0 + 16.0 * s, 108.0 + 18.0 * maxf(-c, 0.0) * move)
	arms(p, 66.0, -25.0 + 12.0 * s, -25.0 - 12.0 * s, 45.0, 45.0, 5.0, 5.0)
	hands(p, 0.25)
	p["pelvis"] = q(Z, -5.0 * s)
	spine(p, 24.0 + 1.5 * b * (1.0 - move), 5.0 * s, 0.0, 0.0, 0.0)
	p["pelvis_offset"] = Vector3(0.0, 0.0, -0.36 - 0.015 * s * s + 0.006 * b * (1.0 - move))
	return p


## In the air: rising (vertical speed > 0) extended with the arms up, falling with the knees tucked and arms out
static func air(vertical_speed: float, time: float) -> Dictionary:
	var rise := clampf(vertical_speed / 4.0, 0.0, 1.0)
	var s := sin(time * TAU / 1.2)
	var p := {}
	leg(p, "l", lerpf(-30.0 - 6.0 * s, -10.0, rise), lerpf(50.0 + 6.0 * s, 12.0, rise), lerpf(10.0, 25.0, rise))
	leg(p, "r", lerpf(-18.0 + 6.0 * s, -4.0, rise), lerpf(38.0 - 6.0 * s, 8.0, rise), lerpf(10.0, 25.0, rise))
	# Arms out for balance; rising they lift a little forward and bend, as after a swing up
	arms(p, lerpf(45.0 + 4.0 * s, 52.0, rise), lerpf(-12.0, -22.0, rise), lerpf(-8.0, -18.0, rise), lerpf(30.0, 55.0, rise), lerpf(30.0, 50.0, rise))
	hands(p, 0.1)
	spine(p, lerpf(6.0, -4.0, rise), 0.0, 0.0, 0.0, 0.0)
	p["pelvis_offset"] = Vector3.ZERO
	return p


## In water: treading (move 0: upright, legs cycling, hands sculling) to front crawl (move 1: body level, face-down,
## arms windmilling, legs fluttering). u is the stroke phase (one cycle = one stroke of each arm).
static func swim(u: float, move: float, time: float) -> Dictionary:
	var ph := u * TAU
	var m := clampf(move, 0.0, 1.0)
	var p := {}
	# Crawl arms: each turns a full circle about the shoulder, pulling under the body, recovering over the back
	# (the right half a cycle behind); treading hands scull side to side in front of the chest
	var crawl_l := -180.0 + 360.0 * u
	var crawl_r := crawl_l + 180.0
	var scull := 18.0 * sin(time * TAU / 1.4)
	var tread := {}
	arms(tread, 62.0, -35.0 + scull, -35.0 - scull, 50.0, 50.0, 10.0 * sin(time * TAU / 1.4), -10.0 * sin(time * TAU / 1.4))
	var crawl := {}
	arms(crawl, 82.0, crawl_l, crawl_r, 25.0 + 20.0 * maxf(sin(ph), 0.0), 25.0 + 20.0 * maxf(-sin(ph), 0.0))
	for k in tread:
		p[k] = (tread[k] as Quaternion).slerp(crawl[k], m)
	hands(p, 0.15)
	# Legs: slow eggbeater treading, a quick shallow flutter kick when swimming
	var e := time * TAU / 1.1
	var kick := sin(ph * 3.0)
	leg(p, "l", lerpf(-35.0 + 18.0 * sin(e), 12.0 * kick, m), lerpf(70.0 + 25.0 * cos(e), 12.0 + 10.0 * maxf(kick, 0.0), m), lerpf(10.0, 45.0, m))
	leg(p, "r", lerpf(-35.0 - 18.0 * sin(e), -12.0 * kick, m), lerpf(70.0 - 25.0 * cos(e), 12.0 + 10.0 * maxf(-kick, 0.0), m), lerpf(10.0, 45.0, m))
	# The body pitches forward about the hips to lie in the water, rolls with the stroke; the head lifts to look ahead
	var roll := 18.0 * sin(ph) * m
	p["pelvis"] = q(X, lerpf(10.0, 78.0, m)) * q(Y, roll * 0.6)
	spine(p, lerpf(4.0, 2.0, m), 0.0, roll, lerpf(0.0, -55.0, m), 0.0)
	p["pelvis_offset"] = Vector3(0.0, 0.0, lerpf(0.0, 0.5, m) + 0.02 * sin(time * TAU / 1.1)) # (level, the back just breaks the surface)
	return p


## The squat before a jump (squat 0..1)
static func take_off(squat: float) -> Dictionary:
	var p := {}
	leg(p, "l", -45.0 * squat, 75.0 * squat)
	leg(p, "r", -40.0 * squat, 70.0 * squat)
	arms(p, 70.0, 20.0 * squat, 20.0 * squat, 20.0, 20.0)
	hands(p)
	spine(p, 18.0 * squat, 0.0, 0.0, 0.0, 0.0)
	p["pelvis_offset"] = Vector3(0.0, 0.0, -0.18 * squat)
	return p


## Absorbing a landing: knees and hips give, then recover (amount 0..1, decays in the caller)
static func add_landing(p: Dictionary, amount: float) -> void:
	if amount <= 0.0:
		return
	for side in ["l", "r"]:
		p["thigh_" + side] = q(X, -22.0 * amount) * p.get("thigh_" + side, Quaternion.IDENTITY)
		p["calf_" + side] = q(X, 42.0 * amount) * p.get("calf_" + side, Quaternion.IDENTITY)
		p["foot_" + side] = q(X, -20.0 * amount) * p.get("foot_" + side, Quaternion.IDENTITY)
	p["Spine_01"] = q(X, 10.0 * amount) * p.get("Spine_01", Quaternion.IDENTITY)
	p["pelvis_offset"] = p.get("pelvis_offset", Vector3.ZERO) + Vector3(0.0, 0.0, -0.1 * amount)
