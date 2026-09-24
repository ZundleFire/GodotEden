#ifndef EDEN_MATERIAL_UTIL_H
#define EDEN_MATERIAL_UTIL_H

#include "scene/resources/material.h"

// Sets a shader parameter only when the material holds a different value. The atmosphere nodes push every uniform
// every frame, so that nothing can go stale (a dirty-flag version once left the sky on an old sun direction), and
// Godot treats any set as a change: every linked material re-uploaded each frame, and the sky re-rendered its whole
// radiance cubemap each frame (0.55 ms at 1080p on a GTX 750 Ti). Comparing against the material's own state keeps
// the push-everything safety without the cost.
inline void eden_set_param(const Ref<ShaderMaterial> &p_mat, const StringName &p_param, const Variant &p_value) {
	const Variant current = p_mat->get_shader_parameter(p_param);
	// An unset texture is pushed as a null Object but reads back as NIL, and the two never compare equal: that alone
	// kept the sky's cubemap re-rendering every frame.
	const bool both_empty = (current.get_type() == Variant::NIL || (current.get_type() == Variant::OBJECT && current.is_null())) &&
			(p_value.get_type() == Variant::NIL || (p_value.get_type() == Variant::OBJECT && p_value.is_null()));
	if (!both_empty && current != p_value) {
		p_mat->set_shader_parameter(p_param, p_value);
	}
}

#endif // EDEN_MATERIAL_UTIL_H
