"""fix_plate_and_pregenerator.py"""
import pathlib

# ── plate_tectonics.gd ──────────────────────────────────────────────────────
for base in ['F:/Dev/Projects/new-game-project', 'F:/Dev/GodotEden']:
    p = pathlib.Path(f'{base}/eden/planet/plate_tectonics.gd')
    if not p.exists():
        print(f'MISSING: {p}'); continue

    text = p.read_text(encoding='utf-8')
    original = text

    # The automated pass incorrectly annotated Vector3 constructors as : float
    # because plate_move_vec is PackedFloat32Array (elements are float).
    # The RESULT of Vector3(...) is Vector3, not float.
    text = text.replace(
        '\t\tvar mv : float = Vector3(',
        '\t\tvar mv: Vector3 = Vector3('
    )
    text = text.replace(
        '\t\t\t\tvar mv1 : float = Vector3(',
        '\t\t\t\tvar mv1: Vector3 = Vector3('
    )
    text = text.replace(
        '\t\t\t\tvar mv2 : float = Vector3(',
        '\t\t\t\tvar mv2: Vector3 = Vector3('
    )

    # pop_front() on untyped Array returns Variant
    text = text.replace(
        '\t\tvar item  := queue.pop_front()',
        '\t\tvar item: Array = queue.pop_front() as Array'
    )

    if text != original:
        p.write_text(text, encoding='utf-8')
        print(f'Fixed plate_tectonics.gd -> {p}')
    else:
        # Show what the lines actually look like so we can match them
        for i, l in enumerate(text.splitlines(), 1):
            if 'mv' in l and ('float' in l or 'Vector3' in l) and 'plate_move_vec' in l:
                print(f'  LINE {i}: {repr(l)}')
        print(f'NO CHANGE: {p}')

# ── planet_pre_generator.gd ─────────────────────────────────────────────────
for base in ['F:/Dev/Projects/new-game-project', 'F:/Dev/GodotEden']:
    p = pathlib.Path(f'{base}/eden/planet/planet_pre_generator.gd')
    if not p.exists():
        print(f'MISSING: {p}'); continue

    text = p.read_text(encoding='utf-8')
    original = text

    # bc and br are bucket_cols/bucket_rows — plain ints on VoronoiSphere.
    # := can't infer from a property on a custom class (returns Variant in some GDScript versions).
    # Fix: explicit : int annotation.
    text = text.replace(
        '\tvar bc := sphere.bucket_cols',
        '\tvar bc: int = sphere.bucket_cols'
    )
    text = text.replace(
        '\tvar br := sphere.bucket_rows',
        '\tvar br: int = sphere.bucket_rows'
    )

    if text != original:
        p.write_text(text, encoding='utf-8')
        print(f'Fixed planet_pre_generator.gd -> {p}')
    else:
        for i, l in enumerate(text.splitlines(), 1):
            if 'bc' in l or 'br' in l:
                if 'bucket' in l:
                    print(f'  LINE {i}: {repr(l)}')
        print(f'NO CHANGE: {p}')
