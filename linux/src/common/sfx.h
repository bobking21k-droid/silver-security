/*
 * SilverOS sound effects: a tiny synthesiser, shared by the boot chooser (UEFI, no C library)
 * and the greeter (Linux). Every sound is made from code: sine pings, pitch-dropping thumps,
 * filtered noise, glides, a small room reverb. Nothing is sampled from anywhere.
 *
 * Needs sgfx.h (sg_sin, sg_sat, sg_floor), SFX_ALLOC(bytes) returning zeroed memory (or 0) and
 * SFX_FREE(ptr).
 * Clips are 48 kHz, interleaved 16-bit stereo.
 */
#pragma once
#include <stdint.h>

#define SFX_RATE 48000

typedef struct { float *L, *R; int n; } SfxMix;
typedef struct { int16_t *s; int n; } SfxClip;      /* n frames */

static uint32_t sfx_seed = 0x2545F491u;
static float sfx_noise(void) { sfx_seed ^= sfx_seed << 13; sfx_seed ^= sfx_seed >> 17; sfx_seed ^= sfx_seed << 5; return (float)(sfx_seed >> 8) / 8388608.0f - 1.0f; }
static float sfx_rand01(void) { return sfx_noise() * 0.5f + 0.5f; }

/* e^x, good to ~0.1% over the range used here */
static float sfx_exp(float x)
{
	if (x < -40) return 0;
	if (x > 20) x = 20;
	float y = x * 1.4426950409f, fl = sg_floor(y), f = y - fl;
	float p = 1 + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
	union { float f; int32_t i; } u = {p};
	u.i += (int32_t)fl * (1 << 23);                             /* (a multiply: shifting a negative number is undefined) */
	return u.f;
}
static float sfx_ln(float x)
{
	if (x <= 0) return -40;
	union { float f; int32_t i; } u = {x};
	int e = ((u.i >> 23) & 255) - 127;
	u.i = (u.i & 0x007fffff) | 0x3f800000;
	float m = u.f, z = (m - 1) / (m + 1), z2 = z * z;
	return e * 0.6931472f + 2 * z * (1 + z2 * (1.0f / 3 + z2 * (0.2f + z2 / 7)));
}
static float sfx_tanh(float x) { if (x > 4) return 1; if (x < -4) return -1; float x2 = x * x; return x * (27 + x2) / (27 + 9 * x2); }
static float sfx_sin(float ph) { return sg_sin(ph); }         /* ph in radians */
#define SFX_TAU 6.2831853f

static SfxMix sfx_mix(float seconds)
{
	SfxMix m; m.n = (int)(seconds * SFX_RATE);
	m.L = SFX_ALLOC((size_t)m.n * sizeof(float)); m.R = SFX_ALLOC((size_t)m.n * sizeof(float));
	if (!m.L || !m.R) m.n = 0;
	return m;
}
static void sfx_free_mix(SfxMix *m) { SFX_FREE(m->L); SFX_FREE(m->R); m->L = m->R = 0; m->n = 0; }
static void sfx_put(SfxMix *m, int i, float v, float pan) { if (i >= 0 && i < m->n) { m->L[i] += v * (1 - pan); m->R[i] += v * (1 + pan); } }

/* a decaying sine: glass, bells, chord notes */
static void sfx_ping(SfxMix *m, float at, float f, float amp, float decay, float pan, float attack)
{
	int i0 = (int)(at * SFX_RATE), len = (int)(decay * 7 * SFX_RATE);
	float ph = 0, dph = SFX_TAU * f / SFX_RATE;
	for (int i = 0; i < len; i++, ph += dph) {
		if (ph > SFX_TAU) ph -= SFX_TAU;
		float t = (float)i / SFX_RATE, env = sfx_exp(-t / decay) * (attack > 0 ? 1 - sfx_exp(-t / attack) : 1);
		sfx_put(m, i0 + i, sfx_sin(ph) * amp * env, pan);
	}
}
/* a thump: a sine falling from f0 to f1, for kicks, booms and heartbeats */
static void sfx_thump(SfxMix *m, float at, float f0, float f1, float fall, float decay, float amp, float drive)
{
	int i0 = (int)(at * SFX_RATE), len = (int)(decay * 7 * SFX_RATE);
	float ph = 0;
	for (int i = 0; i < len; i++) {
		float t = (float)i / SFX_RATE, f = f1 + (f0 - f1) * sfx_exp(-t / fall);
		ph += SFX_TAU * f / SFX_RATE; if (ph > SFX_TAU) ph -= SFX_TAU;
		float v = sfx_sin(ph) * sfx_exp(-t / decay) * (1 - sfx_exp(-t / 0.003f)) * amp;
		if (drive > 0) v = sfx_tanh(v * drive) / sfx_tanh(drive);
		sfx_put(m, i0 + i, v, 0);
	}
}
/* noise: hp > 0 makes it bright (a click, a hiss), lp in 0..1 makes it dark (wind, a whoosh) */
static void sfx_noise_burst(SfxMix *m, float at, float dur, float decay, float amp, int hp, float lp, float pan)
{
	int i0 = (int)(at * SFX_RATE), len = (int)(dur * SFX_RATE);
	float prev = 0, low = 0;
	for (int i = 0; i < len; i++) {
		float t = (float)i / SFX_RATE, x = sfx_noise(), v;
		if (hp) { v = x - prev; prev = x; }
		else { low += (x - low) * lp; v = low * 3; }
		float env = (decay > 0 ? sfx_exp(-t / decay) : 1) * sg_sat((dur - t) / 0.01f);
		sfx_put(m, i0 + i, v * env * amp, pan);
	}
}
/* a whoosh: noise through a low-pass that opens and closes, panning across */
static void sfx_whoosh(SfxMix *m, float at, float dur, float amp, float pan0, float pan1, float bright)
{
	int i0 = (int)(at * SFX_RATE), len = (int)(dur * SFX_RATE);
	float l1 = 0, l2 = 0;
	for (int i = 0; i < len; i++) {
		float x = (float)i / len, env = sg_sin(3.14159f * x); env *= env;
		float cut = 0.01f + bright * 0.18f * env;
		l1 += (sfx_noise() - l1) * cut; l2 += (l1 - l2) * cut;
		sfx_put(m, i0 + i, l2 * env * amp * 5, pan0 + (pan1 - pan0) * x);
	}
}
/* a gliding tone (with a little of its octave): risers, falls, tape stops */
static void sfx_glide(SfxMix *m, float at, float dur, float f0, float f1, float amp, float pan, int saw)
{
	int i0 = (int)(at * SFX_RATE), len = (int)(dur * SFX_RATE);
	float ph = 0, k = sfx_ln(f1 / f0);
	for (int i = 0; i < len; i++) {
		float x = (float)i / len, f = f0 * sfx_exp(x * k);                       /* an even glide, in pitch */
		ph += SFX_TAU * f / SFX_RATE; if (ph > SFX_TAU * 64) ph -= SFX_TAU * 64;
		float v = saw ? (sg_fract(ph / SFX_TAU) * 2 - 1) * 0.6f : sfx_sin(ph) + 0.25f * sfx_sin(ph * 2);
		float env = sg_sat(x / 0.05f) * sg_sat((1 - x) / 0.1f);
		sfx_put(m, i0 + i, v * env * amp, pan);
	}
}
/* scattered short ticks: cracks running, static, crackle */
static void sfx_crackle(SfxMix *m, float at, float dur, int count, float amp, float fade_pow)
{
	for (int k = 0; k < count; k++) {
		float u = sfx_rand01(), t = at + (fade_pow > 1 ? u * u : u) * dur, pan = sfx_noise() * 0.8f, a = amp * (0.4f + 0.6f * sfx_rand01()) * (fade_pow > 1 ? 1.1f - u : 1);
		int i0 = (int)(t * SFX_RATE);
		for (int i = 0; i < 80; i++) sfx_put(m, i0 + i, sfx_noise() * sfx_exp(-i / 10.0f) * a, pan);
	}
}
/* a small room: four combs and two all-passes per side */
static void sfx_room(SfxMix *m, float wet_amt, float size)
{
	static const int comb[4] = {1557, 1617, 1491, 1422}, ap[2] = {225, 556};
	float *buf[2] = {m->L, m->R};
	float *wet = SFX_ALLOC((size_t)m->n * sizeof(float)), *line = SFX_ALLOC(4096 * sizeof(float));
	if (!wet || !line) { if (wet) SFX_FREE(wet); if (line) SFX_FREE(line); return; }
	for (int ch = 0; ch < 2; ch++) {
		float *x = buf[ch];
		for (int i = 0; i < m->n; i++) wet[i] = 0;
		for (int c = 0; c < 4; c++) {
			int d = (int)((comb[c] + ch * 23) * size); if (d > 4096) d = 4096;
			for (int i = 0; i < d; i++) line[i] = 0;
			int p = 0; float lp = 0;
			for (int i = 0; i < m->n; i++) { float y = line[p]; lp = y * 0.6f + lp * 0.4f; line[p] = x[i] + lp * 0.80f; if (++p == d) p = 0; wet[i] += y * 0.25f; }
		}
		for (int a = 0; a < 2; a++) {
			int d = ap[a] + ch * 11;
			for (int i = 0; i < d; i++) line[i] = 0;
			int p = 0;
			for (int i = 0; i < m->n; i++) { float b = line[p], y = -wet[i] + b; line[p] = wet[i] + b * 0.5f; if (++p == d) p = 0; wet[i] = y; }
		}
		for (int i = 0; i < m->n; i++) x[i] += wet[i] * wet_amt;
	}
	SFX_FREE(wet); SFX_FREE(line);
}
/* finish: soft-limit into a 16-bit clip (the float buffers are left for the caller to free) */
static SfxClip sfx_clip(SfxMix *m, float gain)
{
	SfxClip c; c.n = m->n; c.s = SFX_ALLOC((size_t)m->n * 4);
	if (!c.s) { c.n = 0; return c; }
	for (int i = 0; i < m->n; i++) {
		float fade = sg_sat((m->n - i) / (0.02f * SFX_RATE));
		c.s[2 * i] = (int16_t)(sfx_tanh(m->L[i] * gain) * fade * 30000);
		c.s[2 * i + 1] = (int16_t)(sfx_tanh(m->R[i] * gain) * fade * 30000);
	}
	return c;
}

/* ================================================================ a voice mixer for live playback */
#define SFX_VOICES 16
typedef struct { const SfxClip *c; int pos; float gain; int loop; } SfxVoice;
typedef struct { SfxVoice v[SFX_VOICES]; } SfxPlayer;

static void sfx_play(SfxPlayer *p, const SfxClip *c, float gain, int loop)
{
	if (!c || !c->n) return;
	for (int i = 0; i < SFX_VOICES; i++) if (!p->v[i].c) { p->v[i] = (SfxVoice){c, 0, gain, loop}; return; }
	p->v[0] = (SfxVoice){c, 0, gain, loop};                   /* all busy: take the oldest slot */
}
static void sfx_stop(SfxPlayer *p, const SfxClip *c) { for (int i = 0; i < SFX_VOICES; i++) if (!c || p->v[i].c == c) p->v[i].c = 0; }
/* mix `frames` frames into out (interleaved 16-bit stereo) */
static void sfx_render(SfxPlayer *p, int16_t *out, int frames)
{
	for (int f = 0; f < frames; f++) {
		int32_t l = 0, r = 0;
		for (int i = 0; i < SFX_VOICES; i++) {
			SfxVoice *v = &p->v[i];
			if (!v->c) continue;
			if (v->pos >= v->c->n) { if (v->loop) v->pos = 0; else { v->c = 0; continue; } }
			l += (int32_t)(v->c->s[2 * v->pos] * v->gain); r += (int32_t)(v->c->s[2 * v->pos + 1] * v->gain);
			v->pos++;
		}
		out[2 * f] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
		out[2 * f + 1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
	}
}
