#include "eden_ambience_audio.h"

#include <cmath>

static constexpr float TAU_F = 6.28318530718f;

// One-pole low-pass coefficient for a cutoff in Hz
static inline float _lp_coef(float p_hz, float p_rate) {
	return 1.0f - std::exp(-TAU_F * p_hz / p_rate);
}

static inline float _smoothstep(float a, float b, float x) {
	const float t = CLAMP((x - a) / (b - a), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

// Chamberlin state-variable filter; returns the band-pass output
static inline float _svf_band(float p_in, float p_f, float p_q, float &r_low, float &r_band) {
	r_low += p_f * r_band;
	const float high = p_in - r_low - p_q * r_band;
	r_band += p_f * high;
	return r_band;
}

AudioStreamEdenAmbience::AudioStreamEdenAmbience() {
	for (int i = 0; i < LAYER_MAX; i++) {
		levels[i].store(0.0f);
	}
}

void AudioStreamEdenAmbience::set_level(Layer p_layer, float p_level) {
	ERR_FAIL_INDEX(p_layer, LAYER_MAX);
	levels[p_layer].store(CLAMP(p_level, 0.0f, 2.0f), std::memory_order_relaxed);
}

float AudioStreamEdenAmbience::get_level(Layer p_layer) const {
	ERR_FAIL_INDEX_V(p_layer, LAYER_MAX, 0.0f);
	return levels[p_layer].load(std::memory_order_relaxed);
}

Ref<AudioStreamPlayback> AudioStreamEdenAmbience::instantiate_playback() {
	Ref<AudioStreamPlaybackEdenAmbience> pb;
	pb.instantiate();
	pb->stream = Ref<AudioStreamEdenAmbience>(this);
	return pb;
}

PackedVector2Array AudioStreamEdenAmbience::render(float p_seconds, int p_seed) {
	Ref<AudioStreamPlaybackEdenAmbience> pb = instantiate_playback();
	pb->rng = 0x9E3779B9u ^ (uint32_t)p_seed * 2654435761u;
	pb->start();
	pb->snap_levels = true;
	PackedVector2Array out;
	const int n = (int)(p_seconds * AudioStreamPlaybackEdenAmbience::RATE);
	out.resize(n);
	Vector2 *w = out.ptrw();
	for (int i = 0; i < n; i++) {
		float l, r;
		pb->_frame(l, r);
		w[i] = Vector2(l, r);
	}
	return out;
}

void AudioStreamEdenAmbience::trigger_thunder(float p_delay, float p_distance) {
	thunder_delay.store(MAX(p_delay, 0.0f), std::memory_order_relaxed);
	thunder_distance.store(p_distance, std::memory_order_relaxed);
	thunder_serial.fetch_add(1, std::memory_order_release);
}

void AudioStreamEdenAmbience::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_level", "layer", "level"), &AudioStreamEdenAmbience::set_level);
	ClassDB::bind_method(D_METHOD("get_level", "layer"), &AudioStreamEdenAmbience::get_level);
	ClassDB::bind_method(D_METHOD("render", "seconds", "seed"), &AudioStreamEdenAmbience::render, DEFVAL(1));
	ClassDB::bind_method(D_METHOD("trigger_thunder", "delay", "distance"), &AudioStreamEdenAmbience::trigger_thunder);

	BIND_ENUM_CONSTANT(LAYER_WIND);
	BIND_ENUM_CONSTANT(LAYER_LEAVES);
	BIND_ENUM_CONSTANT(LAYER_SURF);
	BIND_ENUM_CONSTANT(LAYER_BIRDS);
	BIND_ENUM_CONSTANT(LAYER_CRICKETS);
	BIND_ENUM_CONSTANT(LAYER_RAIN);
	BIND_ENUM_CONSTANT(LAYER_MAX);
}

// ---------------------------------------------------------------------------------------------

void AudioStreamPlaybackEdenAmbience::start(double p_from_pos) {
	begin_resample();
	_reset_crickets();
	for (int i = 0; i < 2; i++) {
		waves[i].period = _range(7.0f, 12.0f);
		waves[i].t = waves[i].period * (0.1f + 0.5f * i);
		waves[i].gain = _range(0.6f, 1.0f);
		waves[i].pan = i == 0 ? -0.35f : 0.35f;
	}
	active = true;
}

void AudioStreamPlaybackEdenAmbience::_reset_crickets() {
	for (Cricket &c : crickets) {
		c.freq = _range(3800.0f, 5200.0f);
		c.rate = _range(1.2f, 3.2f);
		c.offset = _rand();
		c.gain = _range(0.04f, 0.6f);
		c.pan = _range(-0.9f, 0.9f);
	}
}

void AudioStreamPlaybackEdenAmbience::_spawn_bird() {
	for (Voice &v : voices) {
		if (v.kind >= 0) {
			continue;
		}
		v.kind = (int)(_rand() * 3.0f);
		v.t = 0;
		v.phase = 0;
		v.lps = 0;
		v.pan = _range(-0.9f, 0.9f);
		// Distance: far birds are quieter and duller
		const float dist = _rand();
		v.gain = Math::lerp(0.55f, 0.1f, dist);
		v.lp = _lp_coef(Math::lerp(9000.0f, 2500.0f, dist), RATE);
		switch (v.kind) {
			case 0: // whistled notes
				v.notes = (float)(int)_range(2.0f, 6.0f);
				v.dur = v.notes * _range(0.12f, 0.28f);
				v.f0 = _range(1900.0f, 3800.0f);
				v.f1 = v.f0 * _range(0.7f, 1.5f);
				break;
			case 1: // trill
				v.dur = _range(0.5f, 1.4f);
				v.f0 = _range(3800.0f, 6500.0f);
				v.f1 = v.f0 * 0.85f;
				v.rate = _range(16.0f, 32.0f);
				break;
			default: // chirp series
				v.notes = (float)(int)_range(3.0f, 9.0f);
				v.rate = _range(6.0f, 12.0f);
				v.dur = v.notes / v.rate;
				v.f0 = _range(5000.0f, 7200.0f);
				v.f1 = _range(2600.0f, 3600.0f);
				break;
		}
		return;
	}
}

void AudioStreamPlaybackEdenAmbience::_frame(float &r_l, float &r_r) {
	constexpr float dt = 1.0f / RATE;
	const AudioStreamEdenAmbience *s = stream.ptr();
	const float ease = snap_levels ? 1.0f : dt / 1.5f;
	for (int i = 0; i < AudioStreamEdenAmbience::LAYER_MAX; i++) {
		level[i] += (s->levels[i].load(std::memory_order_relaxed) - level[i]) * ease;
	}
	float l = 0, r = 0;

	// Gusts: a slowly eased random target, shared by wind and leaves
	gust_timer -= dt;
	if (gust_timer <= 0) {
		const float g = s->gustiness.load(std::memory_order_relaxed);
		gust_target = CLAMP(0.15f + _rand() * (0.35f + g), 0.0f, 1.0f);
		gust_timer = _range(1.5f, 5.0f);
	}
	gust += (gust_target - gust) * (dt / 1.2f);

	// Wind: low rumble plus a band-passed hiss whose pitch rises with the gust
	if (level[AudioStreamEdenAmbience::LAYER_WIND] > 1e-4f) {
		const float a = _lp_coef(70.0f + 180.0f * gust, RATE);
		const float f = 2.0f * std::sin(Math::PI * (240.0f + 560.0f * gust) / RATE);
		const float a_hiss = _lp_coef(1100.0f, RATE);
		const float amp = level[AudioStreamEdenAmbience::LAYER_WIND] * (0.3f + 0.7f * gust);
		float out[2];
		for (int c = 0; c < 2; c++) {
			const float n = _noise();
			wl[c] += (n - wl[c]) * a;
			wl2[c] += (wl[c] - wl2[c]) * a;
			const float rumble = wl2[c] * 2.2f / std::sqrt(a);
			whiss[c] += (_svf_band(n, f, 0.3f, wbp_low[c], wbp_band[c]) - whiss[c]) * a_hiss;
			out[c] = amp * (rumble * 0.35f + whiss[c] * (0.15f + 0.3f * gust));
		}
		l += out[0];
		r += out[1];
	}

	// Leaves: a soft rustle that swells with the gusts and drifts slowly in between -- a fast random
	// flutter here read as a choppy, rushed hiss
	if (level[AudioStreamEdenAmbience::LAYER_LEAVES] > 1e-4f) {
		if (_rand() < 1.5f * dt) {
			leaf_flutter_target = _rand();
		}
		leaf_flutter += (leaf_flutter_target - leaf_flutter) * (dt * 2.0f);
		const float amp = level[AudioStreamEdenAmbience::LAYER_LEAVES] * (0.05f + 0.7f * gust * gust) * (0.6f + 0.4f * leaf_flutter);
		const float a_hp = _lp_coef(700.0f, RATE);
		const float a_lp = _lp_coef(3200.0f, RATE);
		float out[2];
		for (int c = 0; c < 2; c++) {
			const float n = _noise();
			leaf_hp[c] += (n - leaf_hp[c]) * a_hp;
			leaf_lp[c] += ((n - leaf_hp[c]) - leaf_lp[c]) * a_lp;
			leaf_lp2[c] += (leaf_lp[c] - leaf_lp2[c]) * a_lp;
			out[c] = leaf_lp2[c] * amp * 1.1f;
		}
		l += out[0];
		r += out[1];
	}

	// Surf: two staggered breakers; each crashes then washes out, getting duller as it fades
	if (level[AudioStreamEdenAmbience::LAYER_SURF] > 1e-4f) {
		for (Wave &w : waves) {
			w.t += dt;
			if (w.t >= w.period) {
				w.t = 0;
				w.period = _range(7.0f, 12.0f);
				w.gain = _range(0.55f, 1.0f);
			}
			const float x = w.t / w.period;
			// Swell, crash, long wash, over a constant distant roar
			const float crash = x < 0.25f ? _smoothstep(0.0f, 0.25f, x) : std::exp(-(x - 0.25f) * 3.5f);
			const float env = 0.2f + 0.8f * crash;
			const float a = _lp_coef(200.0f + 1600.0f * crash * crash, RATE);
			w.lp += (_noise() - w.lp) * a;
			w.lp2 += (w.lp - w.lp2) * a;
			const float v = w.lp2 / std::sqrt(a) * env * w.gain * level[AudioStreamEdenAmbience::LAYER_SURF] * 0.28f;
			l += v * std::sqrt(0.5f * (1.0f - w.pan));
			r += v * std::sqrt(0.5f * (1.0f + w.pan));
		}
	}

	float wet_l = 0, wet_r = 0;

	// Birds
	const float birds = level[AudioStreamEdenAmbience::LAYER_BIRDS];
	bird_timer -= dt;
	if (bird_timer <= 0) {
		bird_timer = _range(0.2f, 1.3f);
		if (_rand() < birds) {
			_spawn_bird();
		}
	}
	for (Voice &v : voices) {
		if (v.kind < 0) {
			continue;
		}
		v.t += dt;
		if (v.t >= v.dur) {
			v.kind = -1;
			continue;
		}
		float freq = 0, env = 0;
		if (v.kind == 0) {
			const float slot = v.dur / v.notes;
			const float note = std::floor(v.t / slot);
			const float u = (v.t - note * slot) / (slot * 0.8f);
			const float glide = ((int)note & 1) ? 1.0f - u : u;
			freq = Math::lerp(v.f0, v.f1, CLAMP(glide, 0.0f, 1.0f)) * (1.0f + 0.012f * std::sin(TAU_F * 28.0f * v.t));
			env = u < 1.0f ? std::sin(Math::PI * u) : 0.0f;
			env *= env;
		} else if (v.kind == 1) {
			const float u = v.t / v.dur;
			freq = Math::lerp(v.f0, v.f1, u);
			const float am = MAX(std::sin(TAU_F * v.rate * v.t), 0.0f);
			env = std::sin(Math::PI * u) * am * am;
		} else {
			const float ct = std::fmod(v.t, 1.0f / v.rate);
			const float u = ct / 0.035f;
			freq = Math::lerp(v.f0, v.f1, MIN(u, 1.0f));
			env = u < 1.0f ? std::sin(Math::PI * u) : 0.0f;
		}
		v.phase += freq * dt;
		v.phase -= std::floor(v.phase);
		const float tone = (std::sin(TAU_F * v.phase) + 0.12f * std::sin(2.0f * TAU_F * v.phase)) * env * v.gain;
		v.lps += (tone - v.lps) * v.lp; // distance low-pass
		const float filtered = v.lps;
		const float gl = std::sqrt(0.5f * (1.0f - v.pan)), gr = std::sqrt(0.5f * (1.0f + v.pan));
		const float dry = filtered * birds * 0.3f;
		l += dry * gl;
		r += dry * gr;
		wet_l += dry * gl;
		wet_r += dry * gr;
	}

	// Crickets: each a pure tone pulsed three times per chirp
	const float crick = level[AudioStreamEdenAmbience::LAYER_CRICKETS];
	if (crick > 1e-4f) {
		for (Cricket &c : crickets) {
			c.clock += dt;
			const float period = 1.0f / c.rate;
			const float ct = std::fmod(c.clock + c.offset * period, period);
			const int pulse = (int)(ct / 0.034f);
			const float pt = ct - pulse * 0.034f;
			c.phase += c.freq * dt;
			c.phase -= std::floor(c.phase);
			if (pulse < 3 && pt < 0.02f) {
				const float v = std::sin(TAU_F * c.phase) * std::sin(Math::PI * pt / 0.02f) * c.gain * crick * 0.18f;
				const float gl = std::sqrt(0.5f * (1.0f - c.pan)), gr = std::sqrt(0.5f * (1.0f + c.pan));
				l += v * gl;
				r += v * gr;
				wet_l += v * gl;
				wet_r += v * gr;
			}
		}
	}

	// Rain: a steady hiss with a low roar underneath
	const float rain = level[AudioStreamEdenAmbience::LAYER_RAIN];
	if (rain > 1e-4f) {
		// Soft, darker hiss (two poles at 3.5 kHz) whose loudness drifts as the shower thickens and eases
		const float a_hp = _lp_coef(500.0f, RATE), a_lp = _lp_coef(3500.0f, RATE), a_body = _lp_coef(350.0f, RATE);
		if (_rand() < 3.0f * dt) {
			rain_swell_target = _rand();
		}
		rain_swell += (rain_swell_target - rain_swell) * dt * 1.5f;
		const float amp = rain * (0.7f + 0.3f * rain_swell);
		for (int c = 0; c < 2; c++) {
			const float n = _noise();
			rain_hp[c] += (n - rain_hp[c]) * a_hp;
			rain_lp[c] += ((n - rain_hp[c]) - rain_lp[c]) * a_lp;
			rain_lp2[c] += (rain_lp[c] - rain_lp2[c]) * a_lp;
			rain_body[c] += (n - rain_body[c]) * a_body;
			(c == 0 ? l : r) += amp * (rain_lp2[c] * 0.45f + rain_body[c] / std::sqrt(a_body) * 0.07f);
		}
	}

	// Thunder: a crack for close strikes, then a rolling low rumble that swells and fades
	const uint32_t serial = s->thunder_serial.load(std::memory_order_relaxed);
	if (serial != thunder_seen) {
		thunder_seen = serial;
		thunder_t = -s->thunder_delay.load(std::memory_order_relaxed);
		thunder_dist = CLAMP(s->thunder_distance.load(std::memory_order_relaxed), 0.0f, 1.0f);
		thunder_swell = 0.5f;
	}
	if (thunder_t > -1e8f) {
		thunder_t += dt;
		const float T = thunder_t;
		const float dur = Math::lerp(5.0f, 9.0f, thunder_dist);
		if (T > dur) {
			thunder_t = -1e9f;
		} else if (T >= 0.0f) {
			if (_rand() < 6.0f * dt) {
				thunder_swell_target = _rand();
			}
			thunder_swell += (thunder_swell_target - thunder_swell) * dt * 6.0f;
			const float near = (1.0f - thunder_dist) * (1.0f - thunder_dist);
			const float crack = T < 0.5f ? near * std::exp(-T * 10.0f) : 0.0f;
			// Fades out over the last third instead of stopping dead
			const float env = _smoothstep(0.0f, 0.25f + 0.5f * thunder_dist, T) * std::exp(-T / (1.5f + 2.0f * thunder_dist)) *
					(0.5f + 0.8f * thunder_swell) * (1.0f - _smoothstep(dur * 0.6f, dur, T));
			const float a = _lp_coef(Math::lerp(260.0f, 90.0f, thunder_dist), RATE);
			const float n = _noise();
			thunder_lp += (n - thunder_lp) * a;
			thunder_lp2 += (thunder_lp - thunder_lp2) * a;
			thunder_clp += (n - thunder_clp) * _lp_coef(2500.0f, RATE); // the crack: bright, not hissy
			const float v = (thunder_lp2 / std::sqrt(a) * env * Math::lerp(1.0f, 0.45f, thunder_dist) * 1.3f + thunder_clp * crack * 2.0f) *
					s->thunder_gain.load(std::memory_order_relaxed);
			l += v;
			r += v * 0.95f;
		}
	}

	// Slapback echo for the calls: reads as open air instead of a dry studio take
	const int dl = (echo_pos - 3300) & 8191, dr = (echo_pos - 4700) & 8191;
	const float el = echo[0][dl], er = echo[1][dr];
	echo[0][echo_pos] = wet_l + er * 0.35f;
	echo[1][echo_pos] = wet_r + el * 0.35f;
	echo_pos = (echo_pos + 1) & 8191;
	l += el * 0.3f;
	r += er * 0.3f;

	r_l = std::tanh(l);
	r_r = std::tanh(r);
}

int AudioStreamPlaybackEdenAmbience::_mix_internal(AudioFrame *p_buffer, int p_frames) {
	if (!active || stream.is_null()) {
		return 0;
	}
	for (int i = 0; i < p_frames; i++) {
		_frame(p_buffer[i].left, p_buffer[i].right);
	}
	return p_frames;
}
