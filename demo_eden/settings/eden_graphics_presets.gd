@tool
class_name EdenGraphicsPresets
extends Resource
## The Low / Medium / High / Ultra levels shared by every EdenGraphics node (settings/eden_graphics_presets.tres).
## Open the .tres in the inspector to tune what each level does.

@export var presets: Array[EdenGraphicsPreset] = []
