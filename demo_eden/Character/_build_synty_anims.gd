extends SceneTree
## Builds the Synty character from the pack in Character/Synty:
##   synty_anims.res       AnimationLibrary of every in-place clip ("A_Walk_F_Masc.fbx" -> "Walk_F"); locomotion,
##                         idles and falls loop
##   synty_anim_tree.tres  the state machine EdenCharacterAnim drives (see there for the parameters)
##   eden_character.tscn   the mesh + an EdenCharacterAnim playing the two
## Run after changing the pack or the tree layout (overwrites all three; edit the tree here, not in the editor):
##   godot --headless --path demo_eden -s Character/_build_synty_anims.gd

const DIR := "res://Character/Synty/"
const OUT := "res://Character/"
## Air part of each jump clip (s): from takeoff to just before the landing dip. The clips are a whole jump in place;
## the body's own velocity does the rising and falling.
const JUMPS := {"Jump_Idle": [0.0, 0.62, 1.30], "Jump_Walking": [1.45, 1.05, 1.62], "Jump_Running": [2.59, 0.42, 1.15], "Jump_Sprinting": [7.24, 0.40, 1.00]}


func _init() -> void:
	var lib := AnimationLibrary.new()
	for f: String in _fbx_files(DIR + "Animations"):
		if "RootMotion" in f:
			continue
		var scene: Node = load(f).instantiate()
		var player: AnimationPlayer = scene.find_children("*", "AnimationPlayer", true, false)[0]
		var anim: Animation = player.get_animation(player.get_animation_list()[0]).duplicate(true)
		scene.free()
		var name := f.get_file().get_basename().trim_prefix("A_").trim_suffix("_Masc").trim_suffix("_Neut")
		var loops := ("/Locomotion/" in f and not "/Turn/" in f) or "/Idle/" in f or "InAir_Fall" in name
		anim.loop_mode = Animation.LOOP_LINEAR if loops else Animation.LOOP_NONE
		lib.add_animation(name, anim)
	print("clips: ", lib.get_animation_list().size())
	_check(ResourceSaver.save(lib, OUT + "synty_anims.res", ResourceSaver.FLAG_COMPRESS))

	var tree := _tree()
	_check(ResourceSaver.save(tree, OUT + "synty_anim_tree.tres"))

	var root := Node3D.new()
	root.name = "EdenCharacter"
	var model: Node3D = load(DIR + "PolygonSyntyCharacter.fbx").instantiate()
	model.name = "Model"
	root.add_child(model)
	model.owner = root
	var anim_tree := AnimationTree.new()
	anim_tree.name = "AnimationTree"
	anim_tree.set_script(load(OUT + "eden_character_anim.gd"))
	root.add_child(anim_tree)
	anim_tree.owner = root
	anim_tree.root_node = NodePath("../Model")
	anim_tree.add_animation_library("", load(OUT + "synty_anims.res"))
	anim_tree.tree_root = load(OUT + "synty_anim_tree.tres")
	var packed := PackedScene.new()
	_check(packed.pack(root))
	_check(ResourceSaver.save(packed, OUT + "eden_character.tscn"))
	root.free()
	print("built synty_anims.res, synty_anim_tree.tres, eden_character.tscn")
	quit()


func _tree() -> AnimationNodeStateMachine:
	var sm := AnimationNodeStateMachine.new()

	# Grounded: every gait cycle is stretched to 1 s so they stay in step when blended (the pack's cycles all start
	# on the same foot, and each gait's directions share one length); `rate` plays them at the clip's cadence.
	#   stand   the direction of travel in the character's frame (x its left, y forward, m/s): idle at the centre,
	#           walk and run rings of eight directions, sprint straight ahead. Each clip sits at its own root-motion
	#           velocity (read from its RootMotion twin), so the feet match the ground speed in every direction.
	#   uphill / downhill  the 25-degree forward gaits by speed; `incline` blends them in on slopes
	#   crouch  the crouch idle and its eight directions
	var stand := _directions(["Walk", "Run"], "Idle_Standing")
	stand.add_blend_point(_cycle("Sprint_F"), _velocity("Sprint/A_Sprint_F"))
	var uphill := _slope("Up25F")
	var downhill := _slope("Down25F")
	var incline := AnimationNodeBlend3.new()
	incline.sync = true
	var crouch := _directions(["Crouch"], "Idle_Crouching")
	var mix := AnimationNodeBlend2.new()
	mix.sync = true
	var grounded := AnimationNodeBlendTree.new()
	grounded.add_node("downhill", downhill, Vector2(0, -150))
	grounded.add_node("stand", stand, Vector2(0, 0))
	grounded.add_node("uphill", uphill, Vector2(0, 150))
	grounded.add_node("incline", incline, Vector2(250, 0))
	grounded.add_node("crouch", crouch, Vector2(250, 250))
	grounded.add_node("crouch_mix", mix, Vector2(500, 100))
	grounded.add_node("rate", AnimationNodeTimeScale.new(), Vector2(700, 100))
	grounded.connect_node("incline", 0, "downhill")
	grounded.connect_node("incline", 1, "stand")
	grounded.connect_node("incline", 2, "uphill")
	grounded.connect_node("crouch_mix", 0, "incline")
	grounded.connect_node("crouch_mix", 1, "crouch")
	grounded.connect_node("rate", 0, "crouch_mix")
	grounded.connect_node("output", 0, "rate")
	grounded.set_node_position("output", Vector2(900, 100))

	# Jump: the air part of the clip for the speed the jump started at
	var jump := AnimationNodeBlendSpace1D.new()
	jump.max_space = 8.0
	for clip in JUMPS:
		var a := _clip(clip)
		a.use_custom_timeline = true
		a.start_offset = JUMPS[clip][1]
		a.timeline_length = JUMPS[clip][2] - JUMPS[clip][1]
		a.loop_mode = Animation.LOOP_NONE
		jump.add_blend_point(a, JUMPS[clip][0])
	# Fall: by downward speed; Land: by impact speed
	var fall := AnimationNodeBlendSpace1D.new()
	fall.max_space = 20.0
	fall.add_blend_point(_clip("InAir_FallShort"), 0.0)
	fall.add_blend_point(_clip("InAir_FallLarge"), 12.0)
	var land := AnimationNodeBlendSpace1D.new()
	land.max_space = 20.0
	land.add_blend_point(_clip("Land_IdleSoft"), 3.0)
	land.add_blend_point(_clip("Land_IdleMedium"), 7.0)
	land.add_blend_point(_clip("Land_IdleHard"), 12.0)

	sm.add_node("Grounded", grounded, Vector2(300, 100))
	sm.add_node("Jump", jump, Vector2(550, 0))
	sm.add_node("Fall", fall, Vector2(800, 100))
	sm.add_node("Land", land, Vector2(550, 250))
	sm.add_transition("Start", "Grounded", _go("", 0.0))
	sm.add_transition("Grounded", "Jump", _go("jump", 0.1))
	sm.add_transition("Grounded", "Fall", _go("falling", 0.3))
	sm.add_transition("Jump", "Fall", _go("falling", 0.3)) # (the jump clip holds its last frame until the descent)
	for from in ["Jump", "Fall"]:
		var hard := _go("land", 0.1)
		hard.priority = 0
		sm.add_transition(from, "Land", hard)
		var soft := _go("grounded", 0.15)
		soft.priority = 1
		sm.add_transition(from, "Grounded", soft)
	var land_done := _go("", 0.25)
	land_done.switch_mode = AnimationNodeStateMachineTransition.SWITCH_MODE_AT_END
	sm.add_transition("Land", "Grounded", land_done)
	sm.add_transition("Land", "Jump", _go("jump", 0.1))
	return sm


## Eight directions for each gait around an idle: forward, the forward diagonals and the sides from the "Fwd"
## strafes (hips facing ahead), backward and the back diagonals from the "Bck" ones
func _directions(gaits: Array, idle: String) -> AnimationNodeBlendSpace2D:
	var bs := AnimationNodeBlendSpace2D.new()
	bs.min_space = Vector2(-8, -8)
	bs.max_space = Vector2(8, 8)
	bs.x_label = "left"
	bs.y_label = "forward"
	bs.sync = true
	bs.add_blend_point(_cycle(idle), Vector2.ZERO)
	for gait: String in gaits:
		for d in ["FwdStrafeF", "FwdStrafeFL", "FwdStrafeFR", "FwdStrafeL", "FwdStrafeR", "BckStrafeB", "BckStrafeBL", "BckStrafeBR"]:
			var clip: String = gait + "_" + d
			bs.add_blend_point(_cycle(clip), _velocity(gait + "/A_" + clip))
	return bs


## A slope gait by forward speed (idle at 0)
func _slope(kind: String) -> AnimationNodeBlendSpace1D:
	var bs := AnimationNodeBlendSpace1D.new()
	bs.max_space = 8.0
	bs.sync = true
	bs.add_blend_point(_cycle("Idle_Standing"), 0.0)
	for gait: String in ["Walk", "Run", "Sprint"]:
		bs.add_blend_point(_cycle(gait + "_" + kind), _velocity(gait + "/A_" + gait + "_" + kind).y)
	return bs


## Root-motion velocity of a locomotion clip (m/s in the character's frame: x its left, y forward), from its
## RootMotion twin ("Walk/A_Walk_F" -> Locomotion/Walk/A_Walk_F_RootMotion_Masc.fbx)
func _velocity(clip: String) -> Vector2:
	var scene: Node = load(DIR + "Animations/Masculine/Locomotion/" + clip + "_RootMotion_Masc.fbx").instantiate()
	var player: AnimationPlayer = scene.find_children("*", "AnimationPlayer", true, false)[0]
	var anim := player.get_animation(player.get_animation_list()[0])
	scene.free()
	var t := anim.find_track("Skeleton3D:Root", Animation.TYPE_POSITION_3D)
	var d: Vector3 = anim.track_get_key_value(t, anim.track_get_key_count(t) - 1) - anim.track_get_key_value(t, 0)
	return Vector2(d.x, d.z) / anim.length


## A looping gait cycle, stretched to a 1 s timeline
func _cycle(clip: String) -> AnimationNodeAnimation:
	var a := _clip(clip)
	a.use_custom_timeline = true
	a.timeline_length = 1.0
	a.stretch_time_scale = true
	a.loop_mode = Animation.LOOP_LINEAR
	return a


func _clip(clip: String) -> AnimationNodeAnimation:
	var a := AnimationNodeAnimation.new()
	a.animation = clip
	return a


## An automatic transition on `condition` (none: always)
func _go(condition: String, xfade: float) -> AnimationNodeStateMachineTransition:
	var t := AnimationNodeStateMachineTransition.new()
	t.advance_mode = AnimationNodeStateMachineTransition.ADVANCE_MODE_AUTO
	t.advance_condition = condition
	t.xfade_time = xfade
	return t


func _fbx_files(dir: String) -> Array:
	var out := []
	for d in DirAccess.get_directories_at(dir):
		out.append_array(_fbx_files(dir + "/" + d))
	for f in DirAccess.get_files_at(dir):
		if f.get_extension().to_lower() == "fbx":
			out.append(dir + "/" + f)
	return out


func _check(err: int) -> void:
	if err != OK:
		push_error("save failed: %s" % error_string(err))
		quit(1)
