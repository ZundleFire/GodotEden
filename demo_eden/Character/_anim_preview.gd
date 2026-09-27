extends SceneTree
## Contact sheet of the EDEN_Male procedural poses (EdenGait, as EdenAnimator plays them): one row per pose, a few
## phases each, side view (plus a front row for the walk). Saves one image, anim_sheet.png.
##   godot --path demo_eden --resolution 320x400 -s res://Character/_anim_preview.gd -- --out=<dir>

const PHASES := [0.0, 0.125, 0.25, 0.375, 0.5, 0.625]

var skel: Skeleton3D
var cam: Camera3D
var rows := []
var out := "user://"
var frame := 0
var sheet: Image


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.trim_prefix("--out=")
	var model: Node3D = load("res://Character/EDEN_Male.FBX").instantiate()
	root.add_child(model)
	skel = model.find_children("*", "Skeleton3D", true, false)[0]
	cam = Camera3D.new()
	cam.current = true
	cam.fov = 40.0
	root.add_child(cam)
	var light := DirectionalLight3D.new()
	root.add_child(light)
	light.transform = Transform3D(Basis.looking_at(Vector3(-0.5, -1, -0.8)), Vector3.ZERO)
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.55, 0.6, 0.65)
	env.ambient_light_color = Color(0.6, 0.6, 0.6)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	var we := WorldEnvironment.new()
	we.environment = env
	root.add_child(we)
	var ground := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(6, 6)
	ground.mesh = pm
	root.add_child(ground)
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.8, 0.8, 0.8)
	for mi in model.find_children("*", "MeshInstance3D", true, false):
		mi.material_override = mat
	rows = [
		["idle", "side", func(u): return EdenGait.idle(u * 9.0)],
		["walk", "side", func(u): return EdenGait.locomotion(u, 0.0)],
		["walk", "front", func(u): return EdenGait.locomotion(u, 0.0)],
		["jog", "side", func(u): return EdenGait.locomotion(u, 0.5)],
		["run", "side", func(u): return EdenGait.locomotion(u, 1.0)],
		["crouch", "side", func(u): return EdenGait.crouch(u, 1.0, 0.0)],
		["air", "side", func(u): return EdenGait.air(lerpf(5.0, -6.0, u), u)],
		["tread", "side", func(u): return EdenGait.swim(u, 0.0, u * 1.4)],
		["crawl", "side", func(u): return EdenGait.swim(u, 1.0, u)],
		["crawl", "front", func(u): return EdenGait.swim(u, 1.0, u)],
		["land", "side", func(u):
			var p := EdenGait.idle(0.0)
			p = EdenGait.add_landing(p, 1.0 - u * 1.4)
			return p],
	]


func _process(_d: float) -> bool:
	# Pose, give the renderer two frames, grab into the sheet
	var n := rows.size() * PHASES.size()
	var j: int = frame / 3
	var step: int = frame % 3
	frame += 1
	if j >= n:
		sheet.save_png(out.path_join("anim_sheet.png"))
		print("saved ", out.path_join("anim_sheet.png"))
		return true
	var row: Array = rows[j / PHASES.size()]
	var col: int = j % PHASES.size()
	if step == 0:
		for i in skel.get_bone_count():
			skel.set_bone_pose_rotation(i, Quaternion.IDENTITY)
		var p: Dictionary = row[2].call(PHASES[col])
		for k in p:
			if k == "pelvis_offset":
				skel.set_bone_pose_position(skel.find_bone("pelvis"), EdenGait.pelvis_rest() + p[k])
			elif skel.find_bone(k) >= 0:
				skel.set_bone_pose_rotation(skel.find_bone(k), p[k])
		# The model faces +Z: side view from +X, front view from +Z
		var eye := Vector3(3.6, 1.0, 0.0) if row[1] == "side" else Vector3(0.0, 1.0, 3.6)
		cam.transform = Transform3D(Basis.looking_at(Vector3(0, 0.85, 0) - eye), eye)
	elif step == 2:
		var img := root.get_viewport().get_texture().get_image()
		if sheet == null:
			sheet = Image.create(img.get_width() * PHASES.size(), img.get_height() * rows.size(), false, img.get_format())
		sheet.blit_rect(img, Rect2i(Vector2i.ZERO, img.get_size()), Vector2i(col * img.get_width(), (j / PHASES.size()) * img.get_height()))
	return false
