#pragma once

#include "core/math/random_pcg.h"
#include "scene/resources/mesh.h"

// A lightning strike's shape and timing, independent of any node: EdenAmbience owns the MeshInstance3D and
// light that show it.
//
// Shape: a stepped channel from the cloud base to the ground (each step heads for the strike point with a
// random sideways kick, so it zig-zags and wanders like a real leader), with branches forking off its upper
// part (and a few sub-branches) that fade out in mid-air, plus a splash billboard where it hits the ground.
// Every vertex carries its position along the channel, so the shader can draw the leader creeping down.
// Timing: the leader descends in ~40 ms, then 2-4 return strokes flash the whole channel, each decaying in
// ~35 ms, with a dim afterglow between them.
struct EdenLightningBolt {
	static constexpr int MAX_STROKES = 4;
	float strokes[MAX_STROKES] = {};
	int stroke_count = 0;
	float leader_time = 0.04f;
	float end_time = 0.5f;

	// Built in the mesh's local space: positions relative to p_top.
	static Ref<ArrayMesh> build_mesh(const Vector3 &p_top, const Vector3 &p_ground, const Vector3 &p_up, float p_width, RandomPCG &r_rng);
	void plan_strokes(RandomPCG &r_rng);
	// Channel brightness at time t since the strike (0..1), and how far down the leader has reached (0..1)
	float flash_at(float p_t) const;
	float reveal_at(float p_t) const;
};

extern const char *EDEN_LIGHTNING_SHADER;
