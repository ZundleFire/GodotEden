#pragma once

#include "servers/audio/audio_stream.h"

#include <atomic>

// Procedural outdoor soundscape: wind, rustling leaves, surf, birds and crickets, synthesized
// live (no audio assets). EdenAmbience sets each layer's level from where the camera is and what
// time it is; the playback eases toward those levels on the audio thread.
class AudioStreamEdenAmbience : public AudioStream {
	GDCLASS(AudioStreamEdenAmbience, AudioStream);

public:
	enum Layer {
		LAYER_WIND,
		LAYER_LEAVES,
		LAYER_SURF,
		LAYER_BIRDS,
		LAYER_CRICKETS,
		LAYER_RAIN,
		LAYER_MAX,
	};

	std::atomic<float> levels[LAYER_MAX];
	std::atomic<float> gustiness{ 0.5f };
	// Thunder requests: bumping the serial queues one strike, heard after its delay
	std::atomic<uint32_t> thunder_serial{ 0 };
	std::atomic<float> thunder_delay{ 0.0f };
	std::atomic<float> thunder_distance{ 0.5f }; // 0 overhead .. 1 far away
	std::atomic<float> thunder_gain{ 1.0f };

protected:
	static void _bind_methods();

public:
	void set_level(Layer p_layer, float p_level);
	float get_level(Layer p_layer) const;

	// Mixes `p_seconds` offline into a fresh playback (tests/previews; levels jump straight to target).
	PackedVector2Array render(float p_seconds, int p_seed = 1);
	void trigger_thunder(float p_delay, float p_distance);

	virtual Ref<AudioStreamPlayback> instantiate_playback() override;
	virtual String get_stream_name() const override { return "EdenAmbience"; }
	virtual double get_length() const override { return 0.0; }
	virtual bool is_monophonic() const override { return true; }

	AudioStreamEdenAmbience();
};

VARIANT_ENUM_CAST(AudioStreamEdenAmbience::Layer);

class AudioStreamPlaybackEdenAmbience : public AudioStreamPlaybackResampled {
	GDCLASS(AudioStreamPlaybackEdenAmbience, AudioStreamPlaybackResampled);
	friend class AudioStreamEdenAmbience;

	static constexpr float RATE = 44100.0f;
	static constexpr int MAX_VOICES = 8;
	static constexpr int CRICKETS = 10;

	struct Voice {
		int kind = -1; // -1 idle; 0 whistle, 1 trill, 2 chirp series
		float t = 0, dur = 0, f0 = 0, f1 = 0, phase = 0, gain = 0, pan = 0, notes = 1, rate = 0, lp = 0, lps = 0;
	};
	struct Cricket {
		float freq = 4500, rate = 2, offset = 0, gain = 0, pan = 0, phase = 0, clock = 0;
	};
	struct Wave {
		float t = 0, period = 9, gain = 0, pan = 0, lp = 0, lp2 = 0;
	};

	Ref<AudioStreamEdenAmbience> stream;
	bool active = false;
	bool snap_levels = false;
	uint32_t rng = 0x9E3779B9u;
	float level[AudioStreamEdenAmbience::LAYER_MAX] = {};

	// wind
	float gust = 0.5f, gust_target = 0.5f, gust_timer = 0;
	float wl[2] = {}, wl2[2] = {}, wbp_low[2] = {}, wbp_band[2] = {}, whiss[2] = {};
	// leaves
	float leaf_hp[2] = {}, leaf_lp[2] = {}, leaf_lp2[2] = {}, leaf_flutter = 0, leaf_flutter_target = 0;
	// surf
	Wave waves[2];
	// birds
	Voice voices[MAX_VOICES];
	float bird_timer = 0;
	// crickets
	Cricket crickets[CRICKETS];
	// rain
	float rain_hp[2] = {}, rain_lp[2] = {}, rain_lp2[2] = {}, rain_body[2] = {}, rain_swell = 0.5f, rain_swell_target = 0.5f;
	// thunder
	uint32_t thunder_seen = 0;
	float thunder_t = -1e9f, thunder_dist = 0.5f, thunder_lp = 0, thunder_lp2 = 0, thunder_swell = 0, thunder_swell_target = 0, thunder_clp = 0;
	float echo[2][8192] = {};
	int echo_pos = 0;

	float _rand() { // [0, 1)
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		return (rng >> 8) * (1.0f / 16777216.0f);
	}
	float _noise() { return _rand() * 2.0f - 1.0f; }
	float _range(float a, float b) { return a + (b - a) * _rand(); }

	void _spawn_bird();
	void _reset_crickets();
	void _frame(float &r_l, float &r_r);

protected:
	static void _bind_methods() {}
	virtual int _mix_internal(AudioFrame *p_buffer, int p_frames) override;
	virtual float get_stream_sampling_rate() override { return RATE; }

public:
	virtual void start(double p_from_pos = 0.0) override;
	virtual void stop() override { active = false; }
	virtual bool is_playing() const override { return active; }
	virtual int get_loop_count() const override { return 0; }
	virtual double get_playback_position() const override { return 0.0; }
	virtual void seek(double p_time) override {}
};
