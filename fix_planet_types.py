"""
fix_planet_types.py
Patches all five planet GDScript files to add explicit type annotations
on variables whose values would otherwise be inferred as Variant.

Rules applied:
  - PackedFloat32Array[i]  -> : float
  - PackedInt32Array[i]    -> : int
  - PackedByteArray[i]     -> : int
  - clamp(int_expr, ...)   -> clampi(...)  and : int
  - clamp(float_expr, ...) -> stays clamp, : float
  - %  (modulo on ints)    -> : int
  - min/max(int, int)      -> : int
  - queue[qi]              -> : int  (queues hold ints in these files)
  - Array subscripts that are int-typed -> : int
"""

import pathlib, re

BASE = pathlib.Path('F:/Dev/Projects/new-game-project/eden/planet')
EDEN = pathlib.Path('F:/Dev/GodotEden/eden/planet')

def fix(src: str, fname: str) -> str:
    lines = src.splitlines()
    out = []
    for line in lines:
        line = fix_line(line, fname)
        out.append(line)
    return '\n'.join(out)

def fix_line(line: str, fname: str) -> str:
    s = line

    # ------------------------------------------------------------------ #
    # 1.  clamp(int_expr, int, int) -> clampi(...)   (int clamp = no Variant)
    # ------------------------------------------------------------------ #
    # Only replace clamp where all args look like integer expressions:
    # ti := clamp(int(...), 0, N-1)
    # row := clamp(row0 + dr, 0, N-1)
    s = re.sub(
        r'\bclamp\((\s*int\([^)]+\)\s*),\s*0\s*,\s*([^)]+)\)',
        lambda m: f'clampi({m.group(1)}, 0, {m.group(2)})',
        s
    )
    # clamp(row0 + dr, 0, N) style
    s = re.sub(
        r'\bclamp\((\s*\w+\s*[+\-]\s*\w+\s*),\s*0\s*,\s*([^)]+)\)',
        lambda m: f'clampi({m.group(1)}, 0, {m.group(2)})',
        s
    )

    # ------------------------------------------------------------------ #
    # 2.  var X :=  ->  var X: TYPE =
    # ------------------------------------------------------------------ #

    # Pattern: leading whitespace, "var NAME :=" then the RHS
    m = re.match(r'^(\s*)(var\s+(\w+)\s*):=\s*(.+)$', s)
    if m:
        indent, var_decl, name, rhs = m.group(1), m.group(2), m.group(3), m.group(4)
        typ = infer_type(name, rhs, fname)
        if typ:
            s = f'{indent}{var_decl}: {typ} = {rhs}'

    return s

FLOAT_ARRAYS = {'region_pos', 'r_elevation', 'r_temperature', 'r_moisture',
                'r_flow_norm', 'plate_move_vec', 'elevation_weight'}
INT_ARRAYS   = {'r_plate', 'r_crust', 'r_boundary_type', 'r_biome', 'r_tex_slot',
                'bucket_offsets', 'bucket_region_ids', 'r_parent', 'r_flow',
                'r_is_river', 'queue', 'bucket_ids'}

def infer_type(name: str, rhs: str, fname: str) -> str:
    rhs = rhs.strip().rstrip('  # .*')

    # Array subscript
    for arr in FLOAT_ARRAYS:
        if re.search(rf'\b{arr}\s*\[', rhs):
            return 'float'
    for arr in INT_ARRAYS:
        if re.search(rf'\b{arr}\s*\[', rhs):
            return 'int'

    # clampi(...) -> int
    if rhs.startswith('clampi('):
        return 'int'

    # clamp(float, ...) -> float
    if rhs.startswith('clamp(') and not rhs.startswith('clampi('):
        # if contains known float arrays or acos/asin, it's float
        for arr in FLOAT_ARRAYS:
            if arr in rhs:
                return 'float'
        if any(k in rhs for k in ['dir.y', '.x', '.y', '.z', 'asin', 'acos', '1.0', '0.0']):
            return 'float'

    # modulo on ints -> int
    if '%' in rhs and 'float' not in rhs and '.' not in rhs.split('%')[0].split()[-1]:
        return 'int'

    # min/max of integer expressions
    if re.match(r'min\(|max\(', rhs.strip()):
        # if any operand looks like a float, it's float; else int
        if '.' in rhs or 'float' in rhs:
            return 'float'
        return 'int'

    # queue[qi] -> int  (these queues hold region ints)
    if re.match(r'queue\[', rhs):
        return 'int'

    # r_plate[...], r_crust[...] etc direct access without prefix
    if re.match(r'r_plate\[|r_crust\[|r_biome\[|r_tex_slot\[|r_boundary_type\[', rhs):
        return 'int'
    if re.match(r'r_elevation\[|r_temperature\[|r_moisture\[|r_flow_norm\[', rhs):
        return 'float'

    # spread := r_moisture[cur] * 0.72 — float * float
    if re.search(r'r_moisture\[|r_elevation\[|r_temperature\[', rhs):
        return 'float'

    # _tectonics.r_elevation[r] etc
    if re.search(r'\._elevation\[|\._temperature\[|\._moisture\[', rhs):
        return 'float'
    if re.search(r'\._plate\[|\._crust\[|\._biome\[', rhs):
        return 'int'

    # planet_pre_generator bucket calcs
    if name in ('col', 'bcol'):
        if '%' in rhs or 'int(' in rhs:
            return 'int'
    if name in ('row', 'brow'):
        if 'clamp' in rhs or 'clampi' in rhs:
            return 'int'
    if name == 'bi':
        return 'int'
    if name in ('bstart', 'bend', 'total', 'idx', 'cursor'):
        return 'int'

    return ''


files = [
    'voronoi_sphere.gd',
    'plate_tectonics.gd',
    'biome_classifier.gd',
    'river_system.gd',
    'planet_pre_generator.gd',
]

for fname in files:
    for base in [BASE, EDEN]:
        p = base / fname
        if not p.exists():
            print(f'MISSING: {p}')
            continue
        original = p.read_text(encoding='utf-8')
        patched  = fix(original, fname)
        if patched != original:
            p.write_text(patched, encoding='utf-8')
            n = sum(1 for a, b in zip(original.splitlines(), patched.splitlines()) if a != b)
            print(f'Fixed {n} lines: {p}')
        else:
            print(f'No changes: {p}')
