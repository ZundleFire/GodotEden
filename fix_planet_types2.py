"""
fix_planet_types2.py - Second pass for remaining Variant inference issues.
"""
import pathlib, re

BASE = pathlib.Path('F:/Dev/Projects/new-game-project/eden/planet')
EDEN = pathlib.Path('F:/Dev/GodotEden/eden/planet')

patches = {
    'plate_tectonics.gd': [
        # randi() % int -> int
        (r'var r := _rng\.randi\(\) % num_regions',
         'var r: int = _rng.randi() % num_regions'),
        # Packed array reads that return Variant
        (r'var plate := r_plate\[cur_r\]',
         'var plate: int = r_plate[cur_r]'),
        (r'var p1 := r_plate\[r\]',
         'var p1: int = r_plate[r]'),
        (r'var p2 := r_plate\[nb\]',
         'var p2: int = r_plate[nb]'),
        (r'var crust := r_crust\[r\]',
         'var crust: int = r_crust[r]'),
        (r'var btype := r_boundary_type\[r\]',
         'var btype: int = r_boundary_type[r]'),
        (r'var cur := queue\[qi\]; qi \+= 1',
         'var cur: int = queue[qi]; qi += 1'),
        (r'var w := elevation_weight\[cur\] \* 0\.7',
         'var w: float = elevation_weight[cur] * 0.7'),
        (r'var mv1 := Vector3\(plate_move_vec\[p1',
         'var mv1 := Vector3(plate_move_vec[p1'),  # already Vector3, skip
        (r'var bearing_score := 1\.0 - max\(0\.0,',
         'var bearing_score: float = 1.0 - max(0.0,'),
        (r'var noise_val := \(_noise\.get_noise_3d',
         'var noise_val: float = (_noise.get_noise_3d'),
        (r'var dist_score := _sphere\.region_angular_distance',
         'var dist_score: float = _sphere.region_angular_distance'),
        (r'var c := \(dist_score',
         'var c: float = (dist_score'),
        (r'var approach := mv1\.dot',
         'var approach: float = mv1.dot'),
        (r'var n := noise2\.get_noise_3d',
         'var n: float = noise2.get_noise_3d'),
    ],
    'biome_classifier.gd': [
        (r'var n := sphere\.num_regions',
         'var n: int = sphere.num_regions'),
        (r'var lat_temp := 1\.0 - abs\(pos\.y\)',
         'var lat_temp: float = 1.0 - abs(pos.y)'),
        (r'var alt_temp := 0\.0',
         'var alt_temp: float = 0.0'),
        (r'var noise_val := _noise\.get_noise_3d',
         'var noise_val: float = _noise.get_noise_3d'),
        (r'var qi := 0',
         'var qi: int = 0'),
        (r'var cur := queue\[qi\]; qi \+= 1',
         'var cur: int = queue[qi]; qi += 1'),
        (r'var spread := r_moisture\[cur\]',
         'var spread: float = r_moisture[cur]'),
        (r'var elev := _tectonics\.r_elevation\[r\]',
         'var elev: float = _tectonics.r_elevation[r]'),
        (r'var temp := r_temperature\[r\]',
         'var temp: float = r_temperature[r]'),
        (r'var mois := r_moisture\[r\]',
         'var mois: float = r_moisture[r]'),
    ],
    'river_system.gd': [
        (r'var n := sphere\.num_regions',
         'var n: int = sphere.num_regions'),
        (r'var n := _sphere\.num_regions',
         'var n: int = _sphere.num_regions'),
        (r'var elev := _tectonics\.r_elevation\[r\]',
         'var elev: float = _tectonics.r_elevation[r]'),
        (r'var nb_elev := _tectonics\.r_elevation\[nb\]',
         'var nb_elev: float = _tectonics.r_elevation[nb]'),
        (r'var noise_offset := _noise\.get_noise_3d',
         'var noise_offset: float = _noise.get_noise_3d'),
        (r'var p := r_parent\[r\]',
         'var p: int = r_parent[r]'),
        (r'var depth := RIVER_CARVE_DEPTH',
         'var depth: float = RIVER_CARVE_DEPTH'),
        (r'var qi := 0',
         'var qi: int = 0'),
        (r'var cur := queue\[qi\]; qi \+= 1',
         'var cur: int = queue[qi]; qi += 1'),
        (r'var max_flow := 1',
         'var max_flow: int = 1'),
        (r'var steps := 0',
         'var steps: int = 0'),
        (r'var best_child := -1',
         'var best_child: int = -1'),
    ],
    'voronoi_sphere.gd': [
        (r'var count := min\(k_neighbors',
         'var count: int = min(k_neighbors'),
        (r'var dot := clamp\(ax \* bx',
         'var dot: float = clamp(ax * bx'),
    ],
    'planet_pre_generator.gd': [
        (r'var total : int = data\.bucket_offsets\[bc \* br\]',
         'var total: int = data.bucket_offsets[bc * br]'),
    ],
}

for fname, rules in patches.items():
    for base in [BASE, EDEN]:
        p = base / fname
        if not p.exists():
            continue
        text = p.read_text(encoding='utf-8')
        original = text
        for pattern, replacement in rules:
            text = re.sub(pattern, replacement, text)
        if text != original:
            p.write_text(text, encoding='utf-8')
            n = sum(1 for a, b in zip(original.splitlines(), text.splitlines()) if a != b)
            print(f'  Fixed {n} lines: {p.name} in {base.name}')
        else:
            print(f'  No change: {p.name} in {base.name}')
