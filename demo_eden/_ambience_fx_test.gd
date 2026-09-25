extends SceneTree
## Fast EdenAmbience check without terrain: a stub planet (flat climate from args), a camera on the surface,
## a plain sky. Saves a screenshot after a few seconds and prints the ambience state.
##   godot --path demo_eden --resolution 960x540 -s res://_ambience_fx_test.gd -- --temp=0.0 --moist=0.5 --out=<png> [--amb_<export>=v]

const R := 1000.0


class StubGenerator:
	extends RefCounted
	var planet_radius := R
	var temperature := 0.5
	var moisture := 0.5

	func sample_surface(_dir: Vector3) -> Dictionary:
		return {"height": 10.0, "temperature": temperature, "moisture": moisture}


class StubPlanet:
	extends Node3D
	var generator := StubGenerator.new()


var args := {}
var frames := 0


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.trim_prefix("--").split("=")
		args[kv[0]] = kv[1] if kv.size() > 1 else "1"
	var planet := StubPlanet.new()
	planet.generator.temperature = float(args.get("temp", "0.5"))
	planet.generator.moisture = float(args.get("moist", "0.5"))
	root.add_child(planet)

	var ground := MeshInstance3D.new()
	var sphere := SphereMesh.new()
	sphere.radius = R + 10.0
	sphere.height = 2.0 * (R + 10.0)
	sphere.radial_segments = 256
	sphere.rings = 128
	ground.mesh = sphere
	var gm := StandardMaterial3D.new()
	gm.albedo_color = Color(0.25, 0.35, 0.2)
	ground.material_override = gm
	planet.add_child(ground)

	var amb := EdenAmbience.new()
	amb.audio_enabled = args.has("audio")
	if args.has("audio"): # measure what reaches the master bus
		AudioServer.add_bus_effect(0, AudioEffectCapture.new())
	for k in args:
		if k.begins_with("amb_"):
			amb.set(k.substr(4), str_to_var(args[k]))
	planet.add_child(amb)

	var cam := Camera3D.new()
	cam.current = true
	root.add_child(cam)
	cam.transform = Transform3D(Basis.looking_at(Vector3(0, 0.05, -1)), Vector3(0, R + 12.0, 0))

	var light := DirectionalLight3D.new()
	root.add_child(light)
	light.transform = Transform3D(Basis.looking_at(Vector3(-0.3, -0.6, 0.4)), Vector3.ZERO)
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.35, 0.45, 0.6) if args.get("night", "0") == "0" else Color(0.02, 0.02, 0.05)
	var we := WorldEnvironment.new()
	we.environment = env
	root.add_child(we)


func _process(_d: float) -> bool:
	frames += 1
	if args.has("audio") and frames == int(args.get("frames", "240")) - 20:
		(AudioServer.get_bus_effect(0, 0) as AudioEffectCapture).clear_buffer()
	if frames < int(args.get("frames", "240")):
		return false
	var amb: EdenAmbience = root.get_child(0).get_child(1)
	print("AMB_FX state: ", amb.get_debug_state())
	for i in EdenAmbience.FX_MAX:
		var p := amb.get_effect(i)
		print("AMB_FX effect %d %s: emitting=%s visible=%s amount=%d ratio=%.2f pos=%s" % [i, p.name, p.emitting, p.visible, p.amount, p.amount_ratio, p.global_position])
	if args.has("audio"):
		var cap: AudioEffectCapture = AudioServer.get_bus_effect(0, 0)
		var buf := cap.get_buffer(cap.get_frames_available())
		var sum := 0.0
		for f in buf:
			sum += f.x * f.x
		print("AMB_FX audio: frames=%d rms=%.4f playing=%s" % [buf.size(), sqrt(sum / maxf(buf.size(), 1)), amb.get_child(amb.get_child_count(true) - 1, true).playing])
	root.get_viewport().get_texture().get_image().save_png(args.get("out", "res://_ambience_fx.png"))
	return true
