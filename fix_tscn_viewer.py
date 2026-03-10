import pathlib

p = pathlib.Path('F:/Dev/Projects/new-game-project/scenes/terrain_test.tscn')
text = p.read_text(encoding='utf-8')

# Ensure VoxelViewer has requires_visuals = true
# The node line is: [node name="VoxelViewer" type="VoxelViewer" parent="Player" ...]
# followed by nothing (no properties). Add requires_visuals = true after it.
if 'requires_visuals' not in text:
    text = text.replace(
        '[node name="VoxelViewer" type="VoxelViewer" parent="Player" unique_id=458615776]',
        '[node name="VoxelViewer" type="VoxelViewer" parent="Player" unique_id=458615776]\nrequires_visuals = true'
    )
    print('Added requires_visuals = true')
else:
    print('requires_visuals already present')

p.write_text(text, encoding='utf-8')
print('Done')
