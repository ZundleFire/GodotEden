class_name EdenRemotePlayer
extends Node3D
## Another player seen through EdenNet: the EDEN_Male model with the same procedural animator, gliding to the
## position/facing the server last reported and animated from the reported state and speed. A name tag floats
## above. Suits are tinted per player.

const MODEL := preload("res://Character/EDEN_Male.FBX")

var _planet: Node3D
var _target := Vector3.ZERO
var _yaw := 0.0
var _has_target := false
var _animator: EdenAnimator
var _label: Label3D


static func create(planet: Node3D) -> EdenRemotePlayer:
	var r := EdenRemotePlayer.new()
	r._planet = planet
	var model: Node3D = MODEL.instantiate()
	model.rotation.y = PI # the rig faces +Z; the body's forward is -Z
	r.add_child(model)
	r._animator = EdenAnimator.new()
	(model.find_children("*", "Skeleton3D", true, false)[0] as Skeleton3D).add_child(r._animator)
	r._label = Label3D.new()
	r._label.position.y = 2.1
	r._label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	r._label.fixed_size = true
	r._label.pixel_size = 0.0016
	r._label.outline_size = 8
	r._label.no_depth_test = true
	r.add_child(r._label)
	return r


func set_target(world_pos: Vector3, yaw: float, state: String, speed: float, player_name: String) -> void:
	_target = world_pos
	_yaw = yaw
	if not _has_target:
		_has_target = true
		global_position = world_pos
		var hue := float(hash(player_name) % 360) / 360.0
		EdenPlayer.apply_look(get_child(0), Color.from_hsv(hue, 0.45, 0.6), Color(0.8, 0.88, 0.95))
	_label.text = player_name
	_animator.ground_speed = speed
	_animator.crouching = state.begins_with("crouch")
	_animator.airborne = state == "jump" or state == "fall"
	_animator.vertical_speed = 3.0 if state == "jump" else -3.0
	_animator.swimming = state == "swim" or state == "tread"


func _process(delta: float) -> void:
	if not _has_target:
		return
	# Glide toward the last report (updates come ~10 per second); far off (a teleport) jump straight there
	if global_position.distance_to(_target) > 20.0:
		global_position = _target
	global_position = global_position.lerp(_target, 1.0 - exp(-delta * 10.0))
	var up := (global_position - _planet.global_position).normalized()
	var forward := EdenPlayer._north(up).rotated(up, _yaw)
	global_basis = global_basis.slerp(Basis.looking_at(forward, up), 1.0 - exp(-delta * 10.0)).orthonormalized()
