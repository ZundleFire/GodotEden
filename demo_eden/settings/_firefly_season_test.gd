extends SceneTree
## Fireflies only on summer nights (and all year where seasons aren't felt): at the spawn (~49° N) there are
## fireflies on a warm clear summer night and none on winter or early-spring nights.
##   godot --path demo_eden --resolution 960x540 -s res://settings/_firefly_season_test.gd

var play: Node
var player: EdenPlayer
var amb: Node
var ok := true
var step := -1
var wait := 0
var results := {}
const CASES := [["summer", 5], ["winter", 11], ["spring", 2]]


func _check(cond: bool, msg: String) -> void:
	print("FIREFLY_TEST %s %s" % ["ok  " if cond else "FAIL", msg])
	ok = ok and cond


func _initialize() -> void:
	play = load("res://eden_play.tscn").instantiate()
	root.add_child(play)
	current_scene = play


func _set_month(month: int) -> void:
	var cal := player.calendar
	cal.running = false
	cal.set_date(1, month, 15, 12.0)
	cal.days -= (cal.local_hour(player.up_direction) - 23.0) / 24.0
	cal._to_fields()
	cal._apply()
	amb.set("weather_override", 1) # clear


func _process(_d: float) -> bool:
	if player == null:
		for n in play.find_children("*", "CharacterBody3D", true, false):
			if n is EdenPlayer:
				player = n
		return false
	if not player.ready_to_move:
		return false
	if step == -1:
		amb = player._planet.get_node("EdenAmbience")
		step = 0
		_set_month(CASES[0][1])
		wait = 240
		return false
	wait -= 1
	if wait > 0:
		return false
	var st: Dictionary = amb.call("get_debug_state")
	var fireflies: float = st.effects[1]
	results[CASES[step][0]] = fireflies
	print("FIREFLY_TEST info %s: fireflies %.3f night %.2f temperature %.2f (%s)" % [CASES[step][0], fireflies, st.night, st.temperature, player.calendar.date_string()])
	step += 1
	if step < CASES.size():
		_set_month(CASES[step][1])
		wait = 240
		return false
	_check(results.summer > 0.02, "fireflies on a summer night (%.3f)" % results.summer)
	_check(results.winter == 0.0, "none on a winter night (%.3f)" % results.winter)
	_check(results.spring == 0.0, "none on an early-spring night (%.3f)" % results.spring)
	print("FIREFLY_TEST %s" % ("PASS" if ok else "FAIL"))
	quit(0 if ok else 1)
	return true
