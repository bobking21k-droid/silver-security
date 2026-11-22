/*
 * The intro's sound, synthesised at start-up (no sound files) and played through ALSA directly
 * (no libraries): the kernel's PCM and control interfaces, as in <sound/asound.h>.
 *
 * The track follows the intro's timeline (T_GEM, T_FORM, T_LOCK, T_OPEN in silver-greeter.c):
 *   the gem fades in on a warm synth pad and beats with a soft bending "bloop"; each silver
 *   layer spins in on a rising, wobbling synth tone (panned to where it comes from) and lands
 *   with a two-part magnetic "ch-chk" over a deep tom; a reverse swell sucks in towards the
 *   lock; the LOCK: a sub drop, a big filtered chord opening up, a bell and a quick rising
 *   bell arpeggio; two airy "zips" follow the sheens; a rising zip as the emblem flies into the
 *   corner; the CRACK (a sharp snap, glassy splinters, a low thoom) and the cosmic colours
 *   splashing out (a deep falling whoosh over a sub, droplets plinking as they land, a shimmer
 *   of high bells drifting across); then a plucked note climbing
 *   as each button pops in, and the pad settling into a calm chord.
 * A little reverb puts it all in one room. Calm mode plays it quieter, without the deep hit.
 *
 * Playback runs in a child process so drawing never waits for audio. If there is no sound
 * card, or it cannot be opened, the intro simply plays in silence.
 */
#pragma once
#include <math.h>

#define SR 48000

static uint32_t snd_rng = 0x9E3779B9u;
static float nrand(void) { snd_rng ^= snd_rng << 13; snd_rng ^= snd_rng >> 17; snd_rng ^= snd_rng << 5; return (float)(snd_rng >> 8) / 8388608.0f - 1.0f; }

/* add a decaying sine "ping" (glass, chord notes) */
static void s_ping(float *L, float *R, int n, float at, float f, float amp, float decay, float pan, float attack)
{
	int i0 = (int)(at * SR);
	for (int i = 0; i < (int)(decay * 6 * SR) && i0 + i < n; i++) {
		if (i0 + i < 0) continue;
		float tt = (float)i / SR, env = expf(-tt / decay) * (attack > 0 ? 1 - expf(-tt / attack) : 1);
		float v = sinf(2 * (float)M_PI * f * tt) * amp * env;
		L[i0 + i] += v * (1 - pan); R[i0 + i] += v * (1 + pan);
	}
}

/* a warm pad: detuned saw-ish voices through a low-pass that opens and closes */
static void s_pad(float *L, float *R, int n, float at, float dur, const float *f, int nf, float amp, float cut0, float cut1, float attack, float release)
{
	int i0 = (int)(at * SR), len = (int)(dur * SR);
	for (int v = 0; v < nf; v++)
		for (int side = 0; side < 2; side++) {
			float ph = side * 0.37f + v * 0.11f, det = side ? 1.004f : 0.996f, lp = 0, lp2 = 0;
			float *out = side ? R : L;
			for (int i = 0; i < len && i0 + i < n; i++) {
				if (i0 + i < 0) continue;
				float tt = (float)i / SR, x = (float)i / len;
				ph += f[v] * det / SR; ph -= (int)ph;
				float saw = 2 * ph - 1, cut = cut0 + (cut1 - cut0) * sg_smooth(x);
				lp += (saw - lp) * cut; lp2 += (lp - lp2) * cut;
				float env = sg_sat(tt / attack) * sg_sat((dur - tt) / release);
				out[i0 + i] += lp2 * env * amp;
			}
		}
}

/* an FM bell: carrier f, modulator at f * ratio; the brightness fades with the note */
static void s_bell(float *L, float *R, int n, float at, float f, float amp, float decay, float ratio, float index, float pan)
{
	int i0 = (int)(at * SR);
	float pc = 0, pm = 0;
	for (int i = 0; i < (int)(decay * 5 * SR) && i0 + i < n; i++) {
		if (i0 + i < 0) continue;
		float tt = (float)i / SR, env = expf(-tt / decay) * (1 - expf(-tt / 0.002f));
		pm += 2 * (float)M_PI * f * ratio / SR; pc += 2 * (float)M_PI * f / SR;
		float v = sinf(pc + index * expf(-tt / (decay * 0.5f)) * sinf(pm)) * env * amp;
		L[i0 + i] += v * (1 - pan); R[i0 + i] += v * (1 + pan);
	}
}

/* a plucked string (Karplus-Strong) */
static void s_pluck(float *L, float *R, int n, float at, float f, float amp, float pan)
{
	int d = (int)(SR / f), i0 = (int)(at * SR);
	if (d < 2) return;
	float *line = calloc((size_t)d, sizeof(float));
	if (!line) return;
	for (int k = 0; k < d; k++) line[k] = nrand();
	int p = 0; float prev = 0;
	for (int i = 0; i < (int)(1.6f * SR) && i0 + i < n; i++) {
		float y = line[p], nv = 0.4985f * (y + prev); prev = y; line[p] = nv; p = (p + 1) % d;
		float v = y * amp * (1 - expf(-(float)i / (0.001f * SR)));
		if (i0 + i >= 0) { L[i0 + i] += v * (1 - pan); R[i0 + i] += v * (1 + pan); }
	}
	free(line);
}

/* a rising synth tone that wobbles faster as it spins, sliding into place */
static void s_spin(float *L, float *R, int n, float at, float dur, float f0, float f1, float amp, float pan0, float pan1, float wob0, float wob1)
{
	int i0 = (int)(at * SR), len = (int)(dur * SR);
	float ph = 0, wp = 0;
	for (int i = 0; i < len && i0 + i < n; i++) {
		if (i0 + i < 0) continue;
		float x = (float)i / len, f = f0 * powf(f1 / f0, sg_ease_out(x)), wob = wob0 + (wob1 - wob0) * x;
		wp += 2 * (float)M_PI * wob / SR;
		ph += 2 * (float)M_PI * f * (1 + 0.025f * sinf(wp)) / SR;
		float tone = sinf(ph) * 0.7f + sinf(ph * 2) * 0.2f + sinf(ph * 3.0f) * 0.1f;
		float env = sg_sat(x * 6) * (0.35f + 0.65f * x) * (0.75f + 0.25f * sinf(wp * 0.5f)), pan = pan0 + (pan1 - pan0) * x;
		float v = tone * env * amp;
		L[i0 + i] += v * (1 - pan); R[i0 + i] += v * (1 + pan);
	}
}

/* the magnetic "ch-chk" of a part locking, over a deep tom */
static void s_lock(float *L, float *R, int n, float at, float tom, float amp, float pan)
{
	for (int k = 0; k < 2; k++) {
		int i0 = (int)((at + k * 0.045f) * SR);
		float lp = 0;
		for (int i = 0; i < (int)(0.05f * SR) && i0 + i < n; i++) {               /* filtered clicks */
			float x = nrand(); lp += (x - lp) * (k ? 0.5f : 0.25f);
			float v = (x - lp) * expf(-(float)i / (0.006f * SR)) * amp * (k ? 1.0f : 0.6f);
			L[i0 + i] += v * (1 - pan); R[i0 + i] += v * (1 + pan);
		}
	}
	int i0 = (int)(at * SR); float ph = 0;
	for (int i = 0; i < (int)(0.5f * SR) && i0 + i < n; i++) {                     /* the tom */
		float tt = (float)i / SR, f = tom * (1 + 0.8f * expf(-tt / 0.03f));
		ph += 2 * (float)M_PI * f / SR;
		float v = sinf(ph) * expf(-tt / 0.14f) * amp * 1.6f;
		L[i0 + i] += v; R[i0 + i] += v;
	}
}

/* Render the intro track into interleaved 16-bit stereo. Returns the number of frames. */
static int intro_track(int16_t **out, float t_gem, float t_form, float t_lock, float t_open, float t_crack, float t_end, int quiet)
{
	int n = (int)((t_end + 1.6f) * SR);
	float *L = calloc((size_t)n, sizeof(float)), *R = calloc((size_t)n, sizeof(float));
	int16_t *o = malloc((size_t)n * 4);
	if (!L || !R || !o) { free(L); free(R); free(o); return 0; }
	/* the gem fades in on a warm pad (D minor, low), opening slowly */
	{
		static const float dm[3] = {73.42f, 110.0f, 174.61f};
		s_pad(L, R, n, t_gem, t_lock - t_gem + 0.3f, dm, 3, 0.10f, 0.004f, 0.05f, 0.6f, 0.3f);
	}
	/* its beat: a soft "bloop" that bends up */
	for (float b = t_gem + 0.82f * 0.07f - 0.02f; b < t_lock - 0.1f; b += 0.82f) {
		float ph = 0;
		for (int i = 0; i < (int)(0.22f * SR); i++) {
			int j = (int)(b * SR) + i; if (j < 0 || j >= n) continue;
			float tt = (float)i / SR, f = 140 + 220 * (1 - expf(-tt / 0.05f));
			ph += 2 * (float)M_PI * f / SR;
			float v = sinf(ph) * expf(-tt / 0.06f) * (1 - expf(-tt / 0.003f)) * 0.30f;
			L[j] += v; R[j] += v;
		}
	}
	/* the three layers: a rising wobbling tone each, then the lock */
	{
		const float start[3] = {t_form, t_form + 0.25f, t_form + 0.50f};                 /* inner, middle, outer */
		const float land[3] = {t_lock - 0.50f, t_lock - 0.25f, t_lock};
		const float pan0[3] = {0.0f, -0.8f, 0.8f}, f0[3] = {330, 220, 147}, f1[3] = {880, 587, 392}, tom[3] = {110, 87, 65};
		for (int l = 0; l < 3; l++) {
			s_spin(L, R, n, start[l], land[l] - start[l], f0[l], f1[l], 0.07f, pan0[l], 0, 26 - 5 * l, 4);
			s_lock(L, R, n, land[l], tom[l], 0.22f, l == 0 ? 0 : (l == 1 ? -0.35f : 0.35f));
		}
	}
	/* a reverse swell, sucking in towards the lock */
	{
		int i0 = (int)((t_lock - 0.9f) * SR), len = (int)(0.9f * SR);
		float lp = 0;
		for (int i = 0; i < len && i0 + i < n; i++) {
			float x = (float)i / len; lp += (nrand() - lp) * (0.02f + 0.3f * x * x);
			float v = lp * x * x * x * 0.55f;
			L[i0 + i] += v; R[i0 + i] += v;
		}
	}
	/* the lock */
	{
		int i0 = (int)(t_lock * SR);
		if (!quiet) {                                                              /* a sub drop */
			float ph = 0;
			for (int i = 0; i < (int)(1.4f * SR) && i0 + i < n; i++) {
				float tt = (float)i / SR, f = 30 + 90 * expf(-tt / 0.12f);
				ph += 2 * (float)M_PI * f / SR;
				float v = tanhf(sinf(ph) * expf(-tt / 0.45f) * 1.4f) * 0.65f;
				L[i0 + i] += v; R[i0 + i] += v;
			}
		}
		static const float chord[5] = {146.83f, 220.0f, 293.66f, 369.99f, 440.0f};   /* D major, opening up */
		s_pad(L, R, n, t_lock, t_crack - t_lock + 0.3f, chord, 5, 0.06f, 0.30f, 0.02f, 0.01f, 0.7f);
		s_bell(L, R, n, t_lock, 587.33f, 0.16f, 1.2f, 3.5f, 2.2f, 0);              /* a bell */
		static const float arp[6] = {587.33f, 739.99f, 880.0f, 1174.66f, 1479.98f, 1760.0f};
		for (int k = 0; k < 6; k++) s_bell(L, R, n, t_lock + 0.10f + k * 0.07f, arp[k], 0.07f, 0.45f, 2.0f, 1.5f, (k - 2.5f) * 0.25f);
	}
	/* two airy "zips" with the sheens */
	for (int z = 0; z < 2; z++) {
		float at = t_lock + (z ? 0.85f : 0.05f), dur = z ? 0.5f : 0.7f, ph = 0;
		int i0 = (int)(at * SR), len = (int)(dur * SR);
		for (int i = 0; i < len && i0 + i < n; i++) {
			float x = (float)i / len, f = 1200 + 3600 * x;
			ph += 2 * (float)M_PI * f / SR;
			float v = (sinf(ph) * 0.5f + nrand() * 0.3f) * sinf((float)M_PI * x) * 0.035f, pan = -0.6f + 1.2f * x;
			L[i0 + i] += v * (1 - pan); R[i0 + i] += v * (1 + pan);
		}
	}
	/* the emblem flies up into the corner: a rising zip, moving right */
	s_spin(L, R, n, t_open, 0.55f, 260, 1300, 0.05f, 0, 0.7f, 30, 8);
	/* the CRACK: a sharp snap, glassy splinters and a low thoom */
	{
		int i0 = (int)(t_crack * SR);
		float prev = 0;
		for (int i = 0; i < (int)(0.06f * SR) && i0 + i < n; i++) {
			float x = nrand(), hp = x - prev; prev = x;
			float v = hp * (expf(-(float)i / (0.004f * SR)) + 0.3f * expf(-(float)i / (0.02f * SR))) * 0.9f;
			L[i0 + i] += v * 0.7f; R[i0 + i] += v * 1.2f;
		}
		for (int k = 0; k < 14; k++) s_bell(L, R, n, t_crack + 0.005f + k * 0.012f, 2600 + (nrand() * 0.5f + 0.5f) * 3400, 0.03f, 0.06f, 1.41f, 1.0f, 0.3f + nrand() * 0.4f);
		float ph = 0;
		for (int i = 0; i < (int)(0.6f * SR) && i0 + i < n; i++) {
			float tt = (float)i / SR; ph += 2 * (float)M_PI * (55 + 40 * expf(-tt / 0.05f)) / SR;
			float v = sinf(ph) * expf(-tt / 0.18f) * (quiet ? 0.2f : 0.5f);
			L[i0 + i] += v; R[i0 + i] += v;
		}
	}
	/* the cosmic colours pour out: a deep whoosh falling from bright to dark, from the corner across, and a shimmer */
	{
		int i0 = (int)((t_crack + 0.08f) * SR), len = (int)(1.0f * SR);
		float lp = 0, lp2 = 0;
		for (int i = 0; i < len && i0 + i < n; i++) {
			float x = (float)i / len, cut = 0.35f * (1 - x) * (1 - x) + 0.01f;
			lp += (nrand() - lp) * cut; lp2 += (lp - lp2) * cut;
			float v = lp2 * 3.2f * expf(-x * 3.0f) * sg_sat(x * 30), pan = 0.7f - 1.4f * x;
			L[i0 + i] += v * (1 - pan); R[i0 + i] += v * (1 + pan);
		}
		if (!quiet) {
			float ph = 0;
			for (int i = 0; i < (int)(0.8f * SR) && i0 + i < n; i++) {
				float tt = (float)i / SR; ph += 2 * (float)M_PI * (38 + 60 * expf(-tt / 0.08f)) / SR;
				float v = tanhf(sinf(ph) * expf(-tt / 0.28f) * 1.6f) * 0.55f;
				L[i0 + i] += v; R[i0 + i] += v;
			}
		}
	}
	for (int k = 0; k < 22; k++) {                                                 /* droplets landing: little wet plinks, spreading left */
		float u = (nrand() * 0.5f + 0.5f), at = t_crack + 0.2f + u * 0.7f, f = 900 + 1400 * (nrand() * 0.5f + 0.5f);
		float ph = 0; int i0 = (int)(at * SR);
		for (int i = 0; i < (int)(0.05f * SR) && i0 + i < n; i++) {
			float tt = (float)i / SR; ph += 2 * (float)M_PI * f * (1 + 2.5f * tt / 0.05f) / SR;      /* a rising "plip" */
			float v = sinf(ph) * expf(-tt / 0.012f) * 0.05f, pan = 0.6f - 1.3f * u;
			L[i0 + i] += v * (1 - pan); R[i0 + i] += v * (1 + pan);
		}
	}
	for (int k = 0; k < 18; k++) {                                                 /* the shimmer: high bells drifting across, right to left */
		float u = k / 17.f;
		s_bell(L, R, n, t_crack + 0.15f + u * 0.9f + 0.03f * nrand(), 1760.0f * (1 + 0.5f * (nrand() * 0.5f + 0.5f)), 0.025f * (1.1f - u), 0.5f, 2.0f, 0.8f, 0.7f - 1.4f * u);
	}
	/* the buttons pop in: a plucked note climbing for each, and a calm chord */
	{
		static const float pl[4] = {293.66f, 440.0f, 587.33f, 880.0f}, tp[4] = {-0.6f, -0.2f, 0.2f, 0.6f};
		for (int k = 0; k < 4; k++) s_pluck(L, R, n, t_crack + 0.62f + k * 0.09f, pl[k], 0.22f, tp[k]);
		static const float calm_c[4] = {146.83f, 220.0f, 277.18f, 329.63f};
		s_pad(L, R, n, t_crack + 0.6f, t_end - t_crack + 0.6f, calm_c, 4, 0.05f, 0.01f, 0.03f, 0.25f, 0.9f);
	}
	/* a small room: four comb filters into two all-passes, per side */
	{
		static const int comb[4] = {1557, 1617, 1491, 1422}, ap[2] = {225, 556};
		float *buf[2] = {L, R};
		for (int ch = 0; ch < 2; ch++) {
			float *x = buf[ch], *wet = calloc((size_t)n, sizeof(float));
			if (!wet) continue;
			for (int c = 0; c < 4; c++) {
				int d = comb[c] + ch * 23; float *line = calloc((size_t)d, sizeof(float)); int p = 0; float lp = 0;
				if (!line) continue;
				for (int i = 0; i < n; i++) { float y = line[p]; lp = y * 0.6f + lp * 0.4f; line[p] = x[i] + lp * 0.80f; p = (p + 1) % d; wet[i] += y * 0.25f; }
				free(line);
			}
			for (int a = 0; a < 2; a++) {
				int d = ap[a] + ch * 11; float *line = calloc((size_t)d, sizeof(float)); int p = 0;
				if (!line) continue;
				for (int i = 0; i < n; i++) { float b = line[p], y = -wet[i] + b; line[p] = wet[i] + b * 0.5f; p = (p + 1) % d; wet[i] = y; }
				free(line);
			}
			for (int i = 0; i < n; i++) x[i] += wet[i] * 0.22f;
			free(wet);
		}
	}
	float gain = quiet ? 0.35f : 0.8f;
	for (int i = 0; i < n; i++) {
		float fade = sg_sat((n - i) / (0.3f * SR));
		o[2 * i] = (int16_t)(tanhf(L[i] * gain) * fade * 30000);
		o[2 * i + 1] = (int16_t)(tanhf(R[i] * gain) * fade * 30000);
	}
	free(L); free(R);
	*out = o;
	return n;
}

/* ================================================================ interface sounds */
#define SFX_ALLOC(n) calloc(1, n)
#define SFX_FREE(p) free(p)
#include "../common/sfx.h"

typedef struct { SfxClip move, open, back, key, results, error; } UiSfx;
static SfxClip ui_clip(int kind)
{
	SfxMix m = sfx_mix(kind == 1 || kind == 2 ? 0.7f : 0.45f);
	if (!m.n) return (SfxClip){0, 0};
	switch (kind) {
	case 0: sfx_noise_burst(&m, 0, 0.015f, 0.002f, 0.18f, 1, 0, 0); sfx_ping(&m, 0, 1500, 0.035f, 0.025f, 0, 0); break;           /* move */
	case 1: sfx_whoosh(&m, 0, 0.26f, 0.22f, -0.3f, 0.3f, 0.9f); sfx_thump(&m, 0.05f, 140, 70, 0.03f, 0.05f, 0.25f, 0);
		sfx_ping(&m, 0.06f, 659.25f, 0.06f, 0.15f, -0.2f, 0.003f); sfx_ping(&m, 0.10f, 987.8f, 0.05f, 0.20f, 0.2f, 0.003f); break;   /* open */
	case 2: sfx_whoosh(&m, 0, 0.24f, 0.20f, 0.3f, -0.3f, 0.45f); sfx_ping(&m, 0.05f, 392.0f, 0.05f, 0.12f, 0, 0.003f); break;      /* back */
	case 3: sfx_noise_burst(&m, 0, 0.012f, 0.0018f, 0.11f, 1, 0, sfx_noise() * 0.3f); break;                                         /* a key */
	case 4: sfx_ping(&m, 0, 880, 0.06f, 0.14f, -0.2f, 0.002f); sfx_ping(&m, 0.07f, 1318.5f, 0.06f, 0.2f, 0.2f, 0.002f); break;      /* results */
	case 5: sfx_glide(&m, 0, 0.22f, 110, 82, 0.10f, 0, 1); break;                                                                   /* error */
	}
	sfx_room(&m, 0.2f, 0.8f);
	SfxClip c = sfx_clip(&m, 1.0f); sfx_free_mix(&m); return c;
}
static void ui_sfx_build(UiSfx *u, int quiet)
{
	(void)quiet;
	u->move = ui_clip(0); u->open = ui_clip(1); u->back = ui_clip(2); u->key = ui_clip(3); u->results = ui_clip(4); u->error = ui_clip(5);
}
static void ui_sfx_play(SfxPlayer *p, UiSfx *u, char ev)
{
	switch (ev) {
	case 'm': sfx_play(p, &u->move, 0.8f, 0); break;
	case 'o': sfx_play(p, &u->open, 0.9f, 0); break;
	case 'b': sfx_play(p, &u->back, 0.8f, 0); break;
	case 'k': sfx_play(p, &u->key, 0.9f, 0); break;
	case 'r': sfx_play(p, &u->results, 0.8f, 0); break;
	case 'e': sfx_play(p, &u->error, 0.8f, 0); break;
	}
}

#define INTRO_TIMES T_GEM, T_FORM, T_LOCK, T_OPEN, T_CRACK, T_END
static int snd_fd = -1;

#ifdef RENDER
/* the demo renderer records requests against its clock and mixes them at the end */
static struct { double t; char ev; } snd_log[4096];
static int snd_nlog;
static double now(void);
static void snd(char ev) { if (snd_nlog < 4096) { snd_log[snd_nlog].t = now(); snd_log[snd_nlog].ev = ev; snd_nlog++; } }
#else
static void snd(char ev) { if (snd_fd >= 0 && write(snd_fd, &ev, 1) < 0) { /* the sound process is gone: fine */ } }
#endif

#ifndef RENDER
#include <sound/asound.h>
#include <dirent.h>

/* turn up and unmute every playback control of a card (HDA codecs often start muted) */
static void mixer_up(int card)
{
	char p[64]; snprintf(p, sizeof p, "/dev/snd/controlC%d", card);
	int fd = open(p, O_RDWR | O_CLOEXEC);
	if (fd < 0) return;
	struct snd_ctl_elem_list list; memset(&list, 0, sizeof list);
	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) < 0 || !list.count) { close(fd); return; }
	struct snd_ctl_elem_id *ids = calloc(list.count, sizeof *ids);
	if (!ids) { close(fd); return; }
	list.space = list.count; list.pids = ids;
	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) == 0)
		for (unsigned i = 0; i < list.used; i++) {
			const char *nm = (const char *)ids[i].name;
			if (!strstr(nm, "Playback")) continue;
			struct snd_ctl_elem_info info; memset(&info, 0, sizeof info); info.id = ids[i];
			if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info) < 0) continue;
			struct snd_ctl_elem_value v; memset(&v, 0, sizeof v); v.id = ids[i];
			unsigned cnt = info.count > 8 ? 8 : info.count;
			if (info.type == SNDRV_CTL_ELEM_TYPE_BOOLEAN) for (unsigned k = 0; k < cnt; k++) v.value.integer.value[k] = 1;
			else if (info.type == SNDRV_CTL_ELEM_TYPE_INTEGER) {
				long lo = info.value.integer.min, hi = info.value.integer.max;
				for (unsigned k = 0; k < cnt; k++) v.value.integer.value[k] = lo + (hi - lo) * 85 / 100;
			} else continue;
			ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &v);
		}
	free(ids); close(fd);
}

/* The main playback volume of a card, as a percentage: the "Master" control if there is one, else
 * "PCM", else the first volume control. set < 0 only reads it; otherwise it is set (and every
 * playback switch is unmuted). Returns the level, or -1 if the card has no volume control. */
static int mixer_level(int card, int set)
{
	char p[64]; snprintf(p, sizeof p, "/dev/snd/controlC%d", card);
	int fd = open(p, O_RDWR | O_CLOEXEC);
	if (fd < 0 && set < 0) fd = open(p, O_RDONLY | O_CLOEXEC);
	if (fd < 0) return -1;
	struct snd_ctl_elem_list list; memset(&list, 0, sizeof list);
	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) < 0 || !list.count) { close(fd); return -1; }
	struct snd_ctl_elem_id *ids = calloc(list.count, sizeof *ids);
	if (!ids) { close(fd); return -1; }
	list.space = list.count; list.pids = ids;
	int best = -1, rank = 0, level = -1;
	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) == 0)
		for (unsigned i = 0; i < list.used; i++) {
			const char *nm = (const char *)ids[i].name;
			if (!strstr(nm, "Playback Volume")) continue;
			int r = !strncmp(nm, "Master", 6) ? 3 : !strncmp(nm, "PCM", 3) ? 2 : 1;
			if (r > rank) { rank = r; best = (int)i; }
		}
	if (best >= 0) {
		struct snd_ctl_elem_info info; memset(&info, 0, sizeof info); info.id = ids[best];
		if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info) == 0 && info.type == SNDRV_CTL_ELEM_TYPE_INTEGER) {
			long lo = info.value.integer.min, hi = info.value.integer.max;
			struct snd_ctl_elem_value v; memset(&v, 0, sizeof v); v.id = ids[best];
			unsigned cnt = info.count > 8 ? 8 : info.count;
			if (set >= 0) {
				for (unsigned k = 0; k < cnt; k++) v.value.integer.value[k] = lo + (hi - lo) * set / 100;
				ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &v);
				for (unsigned i = 0; i < list.used; i++) {                          /* and make sure nothing is muted */
					if (!strstr((const char *)ids[i].name, "Playback Switch")) continue;
					struct snd_ctl_elem_info in2; memset(&in2, 0, sizeof in2); in2.id = ids[i];
					if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &in2) < 0 || in2.type != SNDRV_CTL_ELEM_TYPE_BOOLEAN) continue;
					struct snd_ctl_elem_value v2; memset(&v2, 0, sizeof v2); v2.id = ids[i];
					for (unsigned k = 0; k < (in2.count > 8 ? 8 : in2.count); k++) v2.value.integer.value[k] = 1;
					ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &v2);
				}
			}
			if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &v) == 0 && hi > lo) level = (int)((v.value.integer.value[0] - lo) * 100 / (hi - lo));
		}
	}
	free(ids); close(fd);
	return level;
}
/* the first card that has one */
static int volume_get(void) { for (int c = 0; c < 8; c++) { int l = mixer_level(c, -1); if (l >= 0) return l; } return -1; }
static void volume_set(int pct) { for (int c = 0; c < 8; c++) mixer_level(c, pct); }

static void hw_mask(struct snd_pcm_hw_params *p, int n, unsigned v)
{
	struct snd_mask *m = &p->masks[n - SNDRV_PCM_HW_PARAM_FIRST_MASK];
	memset(m, 0, sizeof *m); m->bits[v >> 5] |= 1u << (v & 31);
}
static void hw_int(struct snd_pcm_hw_params *p, int n, unsigned v)
{
	struct snd_interval *i = &p->intervals[n - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
	i->min = i->max = v; i->integer = 1;
}

/* open the first playback device that takes 48 kHz 16-bit stereo */
static int pcm_open(void)
{
	for (int card = 0; card < 8; card++)
		for (int dev = 0; dev < 4; dev++) {
			char p[64]; snprintf(p, sizeof p, "/dev/snd/pcmC%dD%dp", card, dev);
			int fd = open(p, O_RDWR | O_CLOEXEC);
			if (fd < 0) continue;
			struct snd_pcm_hw_params hp; memset(&hp, 0, sizeof hp);
			for (int k = 0; k <= SNDRV_PCM_HW_PARAM_LAST_MASK - SNDRV_PCM_HW_PARAM_FIRST_MASK; k++) memset(&hp.masks[k], 0xff, sizeof hp.masks[k]);
			for (int k = 0; k <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; k++) { hp.intervals[k].min = 0; hp.intervals[k].max = ~0u; }
			hp.rmask = ~0u; hp.info = ~0u;
			hw_mask(&hp, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
			hw_mask(&hp, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S16_LE);
			hw_mask(&hp, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
			hw_int(&hp, SNDRV_PCM_HW_PARAM_SAMPLE_BITS, 16);
			hw_int(&hp, SNDRV_PCM_HW_PARAM_FRAME_BITS, 32);
			hw_int(&hp, SNDRV_PCM_HW_PARAM_CHANNELS, 2);
			hw_int(&hp, SNDRV_PCM_HW_PARAM_RATE, SR);
			hw_int(&hp, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 1024);
			hw_int(&hp, SNDRV_PCM_HW_PARAM_PERIODS, 4);
			if (ioctl(fd, SNDRV_PCM_IOCTL_HW_PARAMS, &hp) == 0 && ioctl(fd, SNDRV_PCM_IOCTL_PREPARE) == 0) { mixer_up(card); return fd; }
			close(fd);
		}
	return -1;
}

/* ================================================================ the sound process
 * Forked once, as root, before anything else: it opens the sound card, makes every sound, drops
 * root, then mixes and plays forever, taking one-byte requests from a pipe (see snd()). Both the
 * intro and the unprivileged home screen send requests; nobody waits for it. */
static pid_t audio_start(int quiet, int muted)
{
	int p[2];
	if (pipe(p) < 0) return -1;
	pid_t pid = fork();
	if (pid < 0) { close(p[0]); close(p[1]); return -1; }
	if (pid > 0) { close(p[0]); snd_fd = p[1]; fcntl(snd_fd, F_SETFL, fcntl(snd_fd, F_GETFL) | O_NONBLOCK); return pid; }
	close(p[1]);
	signal(SIGTERM, SIG_DFL);
	int fd = pcm_open();
	if (fd < 0) _exit(0);
	if (getuid() == 0 && (setgroups(0, NULL) || setgid(65534) || setuid(65534))) _exit(0);   /* nothing more needs root */
	UiSfx ui; ui_sfx_build(&ui, quiet);
	SfxClip intro = {0, 0};
	fcntl(p[0], F_SETFL, fcntl(p[0], F_GETFL) | O_NONBLOCK);
	static SfxPlayer pl;
	int16_t buf[1024 * 2];
	int open_pipe = 1;
	for (;;) {
		char ev[32];
		ssize_t r;
		while (open_pipe && (r = read(p[0], ev, sizeof ev)) != 0) {
			if (r < 0) break;
			for (ssize_t i = 0; i < r; i++) {
				if (ev[i] == 'I') {
					if (!intro.n) { int16_t *s = 0; intro.n = intro_track(&s, INTRO_TIMES, quiet); intro.s = s; }
					if (!muted) sfx_play(&pl, &intro, 1, 0);
				} else if (ev[i] == 'x') { sfx_stop(&pl, &intro); free(intro.s); intro.s = 0; intro.n = 0; }   /* skipped: the track's memory goes back */
				else if (ev[i] == 'q') open_pipe = 0;
				else if (ev[i] == 'M') { muted = 1; sfx_stop(&pl, 0); }
				else if (ev[i] == 'U') muted = 0;
				else if (!muted) ui_sfx_play(&pl, &ui, ev[i]);
			}
		}
		if (r == 0) open_pipe = 0;
		if (!open_pipe) { int busy = 0; for (int i = 0; i < SFX_VOICES; i++) if (pl.v[i].c) busy = 1; if (!busy) break; }
		sfx_render(&pl, buf, 1024);
		struct snd_xferi x = {0, buf, 1024};
		if (ioctl(fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x) < 0) {
			if (errno == EPIPE) { ioctl(fd, SNDRV_PCM_IOCTL_PREPARE); continue; }   /* an underrun: carry on */
			break;
		}
	}
	ioctl(fd, SNDRV_PCM_IOCTL_DRAIN);
	_exit(0);
}
#endif

/* write interleaved 16-bit stereo as a WAV file (for the demo video) */
static void wav_write(const char *path, const int16_t *buf, int n)
{
	FILE *f = fopen(path, "wb");
	if (!f) return;
	uint32_t bytes = (uint32_t)n * 4, v;
	fwrite("RIFF", 1, 4, f); v = 36 + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
	v = 16; fwrite(&v, 4, 1, f); uint16_t s = 1; fwrite(&s, 2, 1, f); s = 2; fwrite(&s, 2, 1, f);
	v = SR; fwrite(&v, 4, 1, f); v = SR * 4; fwrite(&v, 4, 1, f); s = 4; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
	fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f); fwrite(buf, 4, (size_t)n, f);
	fclose(f);
}
