@tool
class_name EdenCalendar
extends Node
## The planet's calendar and clock: years of 12 months, a day/night cycle, and the seasons. It drives the sky
## (EdenPlanetAtmosphere: the sun's daily turn and its seasonal height from the axial tilt, the moon's phase) and
## the seasons everywhere else (EdenAmbience.year_phase: the weather's temperature, so snow in winter, and through
## it the foliage, grass and terrain shaders).
##
## Month 1 starts at the northern spring equinox. The southern hemisphere has the opposite season at the same time,
## and near the equator seasons are hardly felt. Local time follows the sun at each place (the clock `hour` is the
## time at longitude 0).
##
## In the editor, set the date and hour in the inspector to preview any season and time of day. In game the clock
## runs (see Time); the calendar panel (K, EdenCalendarPanel) shows it and can speed it up.

signal day_changed(day_index: int)
signal season_changed(north: String, south: String)
## A player changed the time (calendar panel) while the clock follows a server: EdenNet sends it on
signal clock_set(days: float, days_per_second: float)

const MONTHS := ["Aurora", "Borea", "Cirra", "Dorea", "Estra", "Fera", "Gaia", "Hesper", "Iria", "Juno", "Kora", "Lethe"]
const SEASONS := ["Spring", "Summer", "Autumn", "Winter"]

@export_group("Date")
@export_range(1, 9999) var year := 1:
	set(v):
		year = v
		_from_fields()
@export_range(1, 12) var month := 4:
	set(v):
		month = v
		_from_fields()
@export_range(1, 31) var day := 1:
	set(v):
		day = v
		_from_fields()
## The clock at longitude 0 (local time elsewhere follows the sun)
@export_range(0.0, 24.0, 0.01) var hour := 9.0:
	set(v):
		hour = v
		_from_fields()

@export_group("Time")
## The clock runs in game (and in the editor with run_in_editor)
@export var running := true
@export var run_in_editor := false
## Real minutes per day at time_scale 1
@export_range(0.5, 240.0, 0.5, "or_greater") var day_length_minutes := 24.0
@export_range(0.0, 10000.0, 0.1, "or_greater") var time_scale := 1.0
@export_range(1, 30) var days_per_month := 4:
	set(v):
		days_per_month = v
		_from_fields()

@export_group("Sky & Seasons")
## Tilt of the spin axis: how high the summer sun climbs, how short winter days get
@export_range(0.0, 45.0, 0.1, "suffix:°") var axial_tilt := 23.4
## Days from new moon to new moon
@export_range(1.0, 60.0, 0.1) var lunar_cycle_days := 8.0
## How far temperatures swing between summer and winter toward the poles (climate units; 0.25 = ±15 °C)
@export_range(0.0, 0.6, 0.01) var season_strength := 0.25
## How far climate moisture swings between the tropics' wet and dry seasons (the rain belt follows the sun)
@export_range(0.0, 0.6, 0.01) var wet_season_strength := 0.25
## Empty: found in the scene
@export var atmosphere_path: NodePath
@export var ambience_path: NodePath

## Days since year 1, month 1, day 1, 00:00 at longitude 0
var days := 0.0
var _syncing := false
var _atmosphere: Node
var _ambience: Node
var _last_day := -1
var _last_seasons := ""
# Multiplayer (EdenNet): the server's clock, {days, at (unix seconds), rate (days per second)}; empty offline
var _server_clock := {}


func _ready() -> void:
	_from_fields()


func days_per_year() -> int:
	return 12 * days_per_month


## The year's phase: 0 northern spring equinox, 0.25 northern midsummer, 0.5 autumn equinox, 0.75 midwinter
func year_phase() -> float:
	return fposmod(days / days_per_year(), 1.0)


## The sun's declination today (degrees north of the equator)
func declination() -> float:
	return axial_tilt * sin(TAU * year_phase())


func _from_fields() -> void:
	if _syncing:
		return
	days = (year - 1) * days_per_year() + (clampi(month, 1, 12) - 1) * days_per_month + (clampi(day, 1, days_per_month) - 1) + hour / 24.0
	_apply()


func _to_fields() -> void:
	_syncing = true
	var d := int(floor(days))
	# (floored, so days before the first of Year 1 count back through Year 0 instead of giving day 0)
	year = floori(float(d) / days_per_year()) + 1
	month = posmod(d, days_per_year()) / days_per_month + 1
	day = posmod(d, days_per_month) + 1
	hour = fposmod(days, 1.0) * 24.0
	_syncing = false


## Sets the date (and hour at longitude 0)
func set_date(p_year: int, p_month: int, p_day: int, p_hour := -1.0) -> void:
	_syncing = true
	year = p_year
	month = p_month
	day = p_day
	if p_hour >= 0.0:
		hour = p_hour
	_syncing = false
	_from_fields()


## Jumps forward to the start of the next time `season` (0 spring .. 3 winter) begins in the given hemisphere
func skip_to_season(season: int, north := true) -> void:
	var target := fposmod(season * 0.25 + (0.0 if north else 0.5), 1.0)
	var ahead := fposmod(target - year_phase(), 1.0)
	days += ahead * days_per_year()
	_to_fields()
	_apply()


## Days per real second as the clock runs now
func days_per_second() -> float:
	return time_scale / (day_length_minutes * 60.0) if running else 0.0


## Multiplayer: follow the server's clock (the calendar's days at unix time `at`, running at `rate` days/second)
func follow_server(p_days: float, at: float, rate: float) -> void:
	_server_clock = {"days": p_days, "at": at, "rate": rate}
	running = rate > 0.0
	if rate > 0.0:
		time_scale = rate * day_length_minutes * 60.0


func is_synced() -> bool:
	return not _server_clock.is_empty()


## After a player changes the time (calendar panel): with a server, ask it to (everyone follows); offline it has
## already happened
func publish() -> void:
	if is_synced():
		clock_set.emit(days, days_per_second())


func _process(delta: float) -> void:
	if is_synced() and not Engine.is_editor_hint():
		days = _server_clock.days + (Time.get_unix_time_from_system() - _server_clock.at) * _server_clock.rate
		_to_fields()
		_apply()
		return
	if not running or (Engine.is_editor_hint() and not run_in_editor):
		return
	days += delta * time_scale / (day_length_minutes * 60.0)
	_to_fields()
	_apply()


# ------------------------------------------------------------------------------------------------------------
# Driving the sky and the seasons

func _find() -> void:
	if not is_inside_tree():
		return
	var scene := get_tree().edited_scene_root if Engine.is_editor_hint() else get_tree().current_scene
	if scene == null:
		return
	if _atmosphere == null or not is_instance_valid(_atmosphere):
		_atmosphere = get_node_or_null(atmosphere_path) if not atmosphere_path.is_empty() else _first(scene, "EdenPlanetAtmosphere")
	if _ambience == null or not is_instance_valid(_ambience):
		_ambience = get_node_or_null(ambience_path) if not ambience_path.is_empty() else _first(scene, "EdenAmbience")


static func _push(node: Node, prop: String, value: float, tolerance: float) -> void:
	var now = node.get(prop)
	if now == null or absf(float(now) - value) > tolerance or (tolerance == 0.0 and float(now) != value):
		node.set(prop, value)


static func _first(root: Node, cls: String) -> Node:
	for n in root.find_children("*", cls, true, false):
		return n
	return null


func _apply() -> void:
	_find()
	# Only what changed enough to matter: some of these setters redo work (the sky's lookup tables)
	if _atmosphere:
		_push(_atmosphere, "day_length_seconds", 0.0, 0.0) # this calendar turns the sun
		_push(_atmosphere, "sun_time_of_day", fposmod(days - 0.5, 1.0), 1e-5)
		_push(_atmosphere, "sun_declination_deg", declination(), 0.02)
		_push(_atmosphere, "moon_phase", fposmod(days / lunar_cycle_days, 1.0), 0.002)
	if _ambience:
		_push(_ambience, "year_phase", year_phase(), 2e-4)
		_push(_ambience, "season_strength", season_strength, 0.0)
		_push(_ambience, "wet_season_strength", wet_season_strength, 0.0)
	var d := int(floor(days))
	if d != _last_day:
		_last_day = d
		day_changed.emit(d)
	var s := season_name(true) + "/" + season_name(false)
	if s != _last_seasons:
		_last_seasons = s
		season_changed.emit(season_name(true), season_name(false))


# ------------------------------------------------------------------------------------------------------------
# Readings

## The spin axis and the equator's reference directions (the same frame as EdenPlanetAtmosphere)
func _frame() -> Array:
	var axis: Vector3 = _atmosphere.get("planet_spin_axis") if _atmosphere else Vector3.UP
	axis = axis.normalized() if axis.length_squared() > 0.5 else Vector3.UP
	var a := axis.cross(Vector3(1, 0, 0))
	if a.length_squared() < 1e-4:
		a = axis.cross(Vector3(0, 0, -1))
	a = a.normalized()
	return [axis, a, axis.cross(a).normalized()]


## Latitude in degrees (+ north) of a direction from the planet's centre
func latitude(dir: Vector3) -> float:
	return rad_to_deg(asin(clampf(dir.normalized().dot(_frame()[0]), -1.0, 1.0)))


## Local solar time (hours) at a direction from the planet's centre: noon when the sun crosses its meridian
func local_hour(dir: Vector3) -> float:
	var f := _frame()
	var lon := atan2(dir.dot(f[2]), dir.dot(f[1]))
	return fposmod(fposmod(days - 0.5, 1.0) + 0.5 - lon / TAU, 1.0) * 24.0


## Season name at a place (or a hemisphere: north = true / false)
func season_name(north: bool) -> String:
	return SEASONS[int(fposmod(year_phase() + (0.0 if north else 0.5), 1.0) * 4.0) % 4]


## The season at a place: spring .. winter outside the tropics, the wet or dry season inside them
func season_at(dir: Vector3) -> String:
	var lat := latitude(dir)
	if absf(lat) < 2.0:
		return "Equatorial (rain all year)"
	var tropical := tropical_season(dir)
	if absf(lat) < 25.0:
		return "Wet season" if tropical > 0.25 else ("Dry season" if tropical < -0.25 else ("Rains coming" if tropical_trend(dir) > 0.0 else "Rains ending"))
	return season_name(lat >= 0.0)


## Tropical wet/dry season at a direction: -1 deep dry season .. +1 the height of the wet (EdenWeatherSim)
func tropical_season(dir: Vector3) -> float:
	var s := dir.normalized().dot(_frame()[0])
	var a := absf(s)
	var tropics := smoothstep(0.03, 0.2, a) * (1.0 - smoothstep(0.38, 0.52, a))
	return tropics * sin(TAU * (year_phase() + (0.5 if s < 0.0 else 0.0) - 0.1))


func tropical_trend(dir: Vector3) -> float:
	var s := dir.normalized().dot(_frame()[0])
	return cos(TAU * (year_phase() + (0.5 if s < 0.0 else 0.0) - 0.1))


## Sunrise and sunset (local hours) and day length at a latitude today; polar day / night: [-1, -1, 24 or 0]
func sun_times(lat_deg: float) -> Array:
	var x := -tan(deg_to_rad(lat_deg)) * tan(deg_to_rad(declination()))
	if x <= -1.0:
		return [-1.0, -1.0, 24.0]
	if x >= 1.0:
		return [-1.0, -1.0, 0.0]
	var h0 := rad_to_deg(acos(x)) / 15.0
	return [12.0 - h0, 12.0 + h0, 2.0 * h0]


func date_string() -> String:
	return "%s %d, Year %d" % [MONTHS[month - 1], day, year]


static func clock(h: float) -> String:
	var m := int(floor(fposmod(h, 24.0) * 60.0))
	return "%02d:%02d" % [m / 60, m % 60]


## The season's change to the climate temperature at a direction (EdenWeatherSim::season_offset, for readings)
func season_offset(dir: Vector3) -> float:
	var s := dir.normalized().dot(_frame()[0])
	var phase := year_phase() + (0.5 if s < 0.0 else 0.0)
	var a := clampf((absf(s) - 0.05) / 0.85, 0.0, 1.0)
	return season_strength * a * a * (3.0 - 2.0 * a) * sin(TAU * (phase - 0.08))


## Climate temperature (0..1, as the generator and EdenAmbience use it; 0.3 = freezing) in degrees Celsius
static func celsius(t: float) -> float:
	return (t - 0.3) * 60.0


static func temp_text(t: float) -> String:
	return "%d °C" % roundi(celsius(t))


static func compass(dir: Vector3, up: Vector3, north: Vector3) -> String:
	var east := north.cross(up)
	var a := fposmod(rad_to_deg(atan2(dir.dot(east), dir.dot(north))), 360.0)
	return ["N", "NE", "E", "SE", "S", "SW", "W", "NW"][int((a + 22.5) / 45.0) % 8]
