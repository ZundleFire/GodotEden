class_name EdenAnimator
extends SkeletonModifier3D
## Procedural animation for the EDEN_Male rig (a child of its Skeleton3D). Every frame it blends EdenGait poses from
## what the body is doing, instead of cross-fading fixed clips:
##   idle <-> walk <-> run by the (eased) ground speed, the stride phase advanced by distance travelled so feet
##   don't slide; crouch and air poses faded in and out; a knee-and-hip dip on landing scaled by the impact; the head
##   turning toward where the camera looks.
## The owner (EdenPlayer) feeds the inputs below from its physics step. `step` fires as each foot strikes.

signal step(foot: int, strength: float)

## Inputs (set by the owner)
var ground_speed := 0.0
var airborne := false
var crouching := false
var swimming := false
var vertical_speed := 0.0
## Where to look, relative to the body: radians, + left / + up
var look_yaw := 0.0
var look_pitch := 0.0
## Speeds that mean "walking" and "running" (gait 0 and 1)
var walk_speed := 1.6
var run_speed := 5.5

## What it is showing, for tests and the HUD: idle, walk, run, crouch_idle, crouch_walk, jump, fall
var state := "idle"

var _bones := {}
var _time := 0.0
var _phase := 0.0
var _move := 0.0
var _gait := 0.0
var _crouch := 0.0
var _air := 0.0
var _swim := 0.0
var _stroke := 0.0
var _land_t := 10.0
var _land_amount := 0.0
var _look := Vector2.ZERO


## The body just touched down after falling at `impact` m/s
func land(impact: float) -> void:
	_land_amount = clampf(impact / 7.0, 0.15, 1.0)
	_land_t = 0.0


func _bone(skel: Skeleton3D, name: String) -> int:
	var i: int = _bones.get(name, -2)
	if i == -2:
		i = skel.find_bone(name)
		_bones[name] = i
	return i


func _process_modification_with_delta(delta: float) -> void:
	var skel := get_skeleton()
	if skel == null:
		return
	delta = minf(delta, 0.1)
	_time += delta
	# Eased weights: each follows its target at its own rate (speed-ups snappier than slow-downs feel natural)
	var moving := 0.0 if airborne else clampf(ground_speed / 0.6, 0.0, 1.0)
	_move = move_toward(_move, moving, delta * (5.0 if moving > _move else 3.5))
	var gait_target := clampf((ground_speed - walk_speed) / (run_speed - walk_speed), 0.0, 1.0)
	_gait = lerpf(_gait, gait_target, 1.0 - exp(-delta * 6.0))
	_crouch = move_toward(_crouch, 1.0 if crouching else 0.0, delta * 5.0)
	_air = move_toward(_air, 1.0 if airborne and not swimming else 0.0, delta * (8.0 if airborne else 12.0))
	_swim = move_toward(_swim, 1.0 if swimming else 0.0, delta * 2.5)
	_land_t += delta
	var look_target := Vector2(look_yaw, look_pitch)
	if absf(look_yaw) > deg_to_rad(110.0): # looking back over the shoulder: face forward rather than twist round
		look_target = Vector2.ZERO
	_look = _look.lerp(look_target, 1.0 - exp(-delta * 5.0))

	# Stride phase: one cycle per cycle-distance travelled (crouch steps are shorter)
	var cycle := lerpf(EdenGait.WALK_CYCLE_DISTANCE, EdenGait.RUN_CYCLE_DISTANCE, _gait)
	cycle = lerpf(cycle, 1.0, _crouch)
	var before := _phase
	_phase = fposmod(_phase + ground_speed * delta / cycle, 1.0)
	# Strokes: faster when swimming than treading; each arm entering the water is a splash (the step signal)
	var stroke_before := _stroke
	_stroke = fposmod(_stroke + delta * lerpf(0.5, 0.75, clampf(ground_speed / 2.0, 0.0, 1.0)), 1.0)
	if swimming and ground_speed > 0.4:
		for entry in [0.0, 0.5]:
			if _crossed(stroke_before, _stroke, entry):
				step.emit(0 if entry == 0.0 else 1, 0.6)
	if not airborne and not swimming and _move > 0.3:
		for strike in [0.25, 0.75]:
			if _crossed(before, _phase, strike):
				step.emit(0 if strike == 0.25 else 1, clampf(ground_speed / run_speed, 0.25, 1.0) * lerpf(1.0, 0.5, _crouch))

	# Standing: idle -> locomotion; crouched: the crouch pose (which has its own still -> moving blend)
	# (only the poses with weight are built: ~25 us each in GDScript)
	var stand := {}
	if _crouch < 1.0:
		if _move <= 0.0:
			stand = EdenGait.idle(_time)
		elif _move >= 1.0:
			stand = EdenGait.locomotion(_phase, _gait)
		else:
			stand = EdenGait.blend(EdenGait.idle(_time), EdenGait.locomotion(_phase, _gait), _smooth(_move))
	var low := EdenGait.crouch(_phase, _move, _time) if _crouch > 0.0 else {}
	var p := EdenGait.blend(stand, low, _smooth(_crouch))
	if _air > 0.0:
		p = EdenGait.blend(p, EdenGait.air(vertical_speed, _time), _smooth(_air))
	if _swim > 0.0:
		p = EdenGait.blend(p, EdenGait.swim(_stroke, clampf(ground_speed / 1.5, 0.0, 1.0), _time), _smooth(_swim))
	# Landing: dips fast, recovers over a third of a second
	if _land_t < 0.6:
		EdenGait.add_landing(p, _land_amount * minf(_land_t / 0.06, 1.0) * exp(-_land_t / 0.14))
	_add_look(p)
	_apply(skel, p)

	if _swim > 0.5:
		state = "swim" if ground_speed > 0.4 else "tread"
	elif _air > 0.5:
		state = "jump" if vertical_speed > 0.5 else "fall"
	elif crouching:
		state = "crouch_walk" if ground_speed > 0.15 else "crouch_idle"
	elif ground_speed > (walk_speed + run_speed) * 0.5:
		state = "run"
	elif ground_speed > 0.15:
		state = "walk"
	else:
		state = "idle"


static func _smooth(t: float) -> float:
	return t * t * (3.0 - 2.0 * t)


static func _crossed(a: float, b: float, x: float) -> bool:
	return (a < x and b >= x) if b >= a else (a < x or b >= x)


# The head and neck turn toward the look direction on top of the pose (limited: a head turns ~70 deg, tips ~35)
func _add_look(p: Dictionary) -> void:
	var yaw := rad_to_deg(clampf(_look.x, -1.2, 1.2))
	var pitch := -rad_to_deg(clampf(_look.y, -0.6, 0.5))
	p["neck_01"] = EdenGait.q(EdenGait.Z, yaw * 0.35) * EdenGait.q(EdenGait.X, pitch * 0.4) * p.get("neck_01", Quaternion.IDENTITY)
	p["head"] = EdenGait.q(EdenGait.Z, yaw * 0.5) * EdenGait.q(EdenGait.X, pitch * 0.6) * p.get("head", Quaternion.IDENTITY)
	p["spine_03"] = EdenGait.q(EdenGait.Z, yaw * 0.15) * p.get("spine_03", Quaternion.IDENTITY)


func _apply(skel: Skeleton3D, p: Dictionary) -> void:
	for k in p:
		if k == "pelvis_offset":
			var i := _bone(skel, "pelvis")
			if i >= 0:
				skel.set_bone_pose_position(i, EdenGait.PELVIS_REST + p[k])
		else:
			var i := _bone(skel, k)
			if i >= 0:
				skel.set_bone_pose_rotation(i, p[k])
	# Bones a pose leaves out (clavicles when crouched, say) go back to rest rather than keeping a stale angle
	for k in _bones:
		if not p.has(k) and _bones[k] >= 0:
			skel.set_bone_pose_rotation(_bones[k], Quaternion.IDENTITY)
