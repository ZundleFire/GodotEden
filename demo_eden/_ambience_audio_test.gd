extends SceneTree
## Offline check of the EdenAmbience soundscape: renders each layer solo (and a mix), prints RMS/peak and saves WAVs.
##   godot --headless --path demo_eden -s res://_ambience_audio_test.gd -- --out=<dir>

const LAYERS := ["wind", "leaves", "surf", "birds", "crickets", "rain"]


func _initialize() -> void:
	var out := "user://"
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.trim_prefix("--out=")
	var ok := true
	for i in LAYERS.size() + 2: # each layer solo, the mix, then a thunder strike alone
		var s := AudioStreamEdenAmbience.new()
		var name: String = LAYERS[i] if i < LAYERS.size() else ("mix" if i == LAYERS.size() else "thunder")
		for k in LAYERS.size():
			s.set_level(k, 1.0 if (k == i or i == LAYERS.size()) else 0.0)
		if name == "thunder":
			s.trigger_thunder(0.5, 0.2)
		var frames := s.render(12.0, 7)
		var sum := 0.0
		var peak := 0.0
		for f in frames:
			sum += f.x * f.x + f.y * f.y
			peak = maxf(peak, maxf(absf(f.x), absf(f.y)))
		var rms := sqrt(sum / (2.0 * frames.size()))
		var finite := is_finite(rms)
		ok = ok and finite and rms > 0.003 and peak < 1.0
		print("AMB_AUDIO %-8s rms=%.4f (%.1f dBFS) peak=%.3f" % [name, rms, 20.0 * log(maxf(rms, 1e-9)) / log(10.0), peak])
		var bytes := PackedByteArray()
		bytes.resize(frames.size() * 4)
		for j in frames.size():
			bytes.encode_s16(j * 4, int(clampf(frames[j].x, -1, 1) * 32767))
			bytes.encode_s16(j * 4 + 2, int(clampf(frames[j].y, -1, 1) * 32767))
		var wav := AudioStreamWAV.new()
		wav.format = AudioStreamWAV.FORMAT_16_BITS
		wav.stereo = true
		wav.mix_rate = 44100
		wav.data = bytes
		wav.save_to_wav(out.path_join("amb_%s.wav" % name))
	print("AMB_AUDIO ", "PASS" if ok else "FAIL")
	quit(0 if ok else 1)
