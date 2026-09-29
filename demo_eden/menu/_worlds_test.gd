extends SceneTree
## Worlds and saving, end to end, on a private SpacetimeDB (its own port and data folder, stopped at the end):
## hosts a world with a seed, finds it in the server's list, joins it (the planet uses the seed), walks and changes
## the inventory, goes back to the main menu, rejoins and checks the player is back where they left with the same
## inventory, then deletes the world. Needs the spacetime CLI and a window.
##   godot --audio-driver Dummy --path demo_eden --resolution 1280x720 -s res://menu/_worlds_test.gd --
##       --stdb-port=3191 --stdb-data=<scratch dir> [--out=<dir>]

const SEED := 4242
var ok := true
var out := "user://"
var worlds: EdenWorlds


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.trim_prefix("--out=")
	_run()


func _run() -> void:
	await process_frame
	var app := root.get_node_or_null("EdenApp")
	_check(app != null, "EdenApp autoload present")
	worlds = EdenWorlds.new()
	root.add_child(worlds)
	var server := EdenWorlds.local_url()
	_check(server.ends_with(":3191"), "private server port (%s)" % server)
	EdenOptions.player_name = "Tester"

	# Host
	var t0 := Time.get_ticks_msec()
	var r := await worlds.host_world("Test World", SEED, "Tester")
	_check(r.has("database"), "hosted a world %s in %.1f s" % [r, (Time.get_ticks_msec() - t0) / 1000.0])
	if not r.has("database"):
		return _finish()
	var db: String = r.database
	var list := await worlds.list_worlds(server)
	var found := list.filter(func(w): return w.database == db)
	_check(found.size() == 1 and found[0].seed == SEED and found[0].name == "Test World", "listed with its name and seed: %s" % [found])
	var meta := await worlds.world_meta(server, db)
	_check(meta.get("seed", -1) == SEED, "world_meta seed %s" % [meta])

	# Join: walk a bit, set the inventory
	var player := await _join(server, db)
	if player == null:
		return _finish()
	var gen = player._planet.generator
	_check(int(gen.seed) == SEED, "the planet uses the world's seed (%d)" % gen.seed)
	_shot("joined")
	Input.action_press("move_forward")
	await _seconds(3.0)
	Input.action_release("move_forward")
	await _seconds(1.0)
	player.miner.counts[0] = 7
	player.miner.counts[1] = 3
	player.miner.refresh()
	await _seconds(2.5) # an inventory save is at most once a second; positions go ~10 a second
	var left_at := player.global_position - player._center()
	var rows = await worlds.sql(server, db, "SELECT * FROM player_inventory")
	_check(rows is Array and rows.size() == 1 and rows[0][1][0] == 7, "inventory saved on the server: %s" % [rows])

	# Back to the menu, then rejoin
	root.get_node("EdenApp").main_menu()
	await _seconds(3.0)
	_check(current_scene is EdenMainMenu, "back at the main menu")
	player = await _join(server, db)
	if player == null:
		return _finish()
	await _seconds(2.0)
	var net: EdenNet = player.net
	_check(net.restored.has("position") and net.restored.has("inventory"), "joining restored %s" % [net.restored.keys()])
	var back_at := player.global_position - player._center()
	_check(back_at.distance_to(left_at) < 3.0, "back where they left (%.2f m away)" % back_at.distance_to(left_at))
	_check(player.miner.counts[0] == 7 and player.miner.counts[1] == 3, "same inventory (%s)" % [player.miner.counts])
	_shot("rejoined")

	# Clean up: back to the menu, delete the world
	root.get_node("EdenApp").main_menu()
	await _seconds(2.0)
	_check(await worlds.delete_world(db), "deleted the world")
	list = await worlds.list_worlds(server)
	_check(list.filter(func(w): return w.database == db).is_empty(), "no longer listed")
	_finish()


## Sets the session and loads the play scene; returns the player once it is online and on the ground (or null)
func _join(server: String, db: String) -> EdenPlayer:
	var meta := await worlds.world_meta(server, db)
	EdenSession.play_online(server, db, meta.name, meta.seed)
	change_scene_to_file("res://eden_play.tscn")
	var t0 := Time.get_ticks_msec()
	while Time.get_ticks_msec() - t0 < 90000:
		await process_frame
		if current_scene == null:
			continue
		for n in current_scene.find_children("*", "CharacterBody3D", true, false):
			if n is EdenPlayer and n.ready_to_move and n.net and n.net.status == "online" and n.net._joined:
				await _seconds(1.0)
				if n.ready_to_move:
					print("WORLDS_TEST info joined in %.1f s" % ((Time.get_ticks_msec() - t0) / 1000.0))
					return n
	_check(false, "joined the world (online and on the ground) within 90 s")
	return null


func _finish() -> void:
	EdenWorlds.stop_local_server()
	print("WORLDS_TEST ", "PASS" if ok else "FAIL")
	quit(0 if ok else 1)


func _check(cond: bool, msg: String) -> void:
	print("WORLDS_TEST %s %s" % ["ok  " if cond else "FAIL", msg])
	ok = ok and cond


func _shot(name: String) -> void:
	root.get_viewport().get_texture().get_image().save_png(out.path_join("worlds_%s.png" % name))


func _seconds(s: float) -> void:
	var end := Time.get_ticks_msec() + int(s * 1000.0)
	while Time.get_ticks_msec() < end:
		await process_frame
