/*
 * The intro's sound, synthesised at start-up (no sound files) and played through ALSA directly
 * (no libraries): the kernel's PCM and control interfaces, as in <sound/asound.h>.
 *
 * The track follows the intro's timeline (T_GEM, T_FORM, T_LOCK, T_OPEN, T_CRACK in silver-greeter.c),
 * in two styles mixed: digital ("code-y": square-wave blips, data chatter, bit-crushed glitches)
 * and JoJo-like drama (a menacing "gogogo" rumble building up, heavy impact hits, reverse swells):
 *   the gem boots up with three square blips and the rumble begins; each layer spins in on a
 *   chirping data sweep and lands with a heavy hit; a reverse swell into the LOCK: a huge hit, a
 *   glitch stutter and a short stab; a digital zip to the corner; then the PRESSURE: the rumble
 *   comes back harder, a sharp digital tick on every jump of the cracks, a reverse swell, and the
 *   BURST: the biggest hit, a bit-crushed explosion falling away, data spraying out and droplets
 *   blipping as they land; four rising blips as the buttons appear.
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

/* a pad: detuned saw-ish voices through a low-pass that opens and closes */
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

/* a square wave sliding from f0 to f1 (log), with a fast attack and an exponential fade */
static void s_square(float *L, float *R, int n, float at, float dur, float f0, float f1, float amp, float pan, float duty)
{
	int i0 = (int)(at * SR), len = (int)(dur * SR);
	float ph = 0;
	for (int i = 0; i < len && i0 + i < n; i++) {
		if (i0 + i < 0) continue;
		float x = (float)i / len, f = f0 * powf(f1 / f0, x);
		ph += f / SR; ph -= (int)ph;
		float v = (ph < duty ? 1.0f : -1.0f) * amp * sg_sat(i / (0.002f * SR)) * expf(-x * 3.0f);
		L[i0 + i] += v * (1 - pan); R[i0 + i] += v * (1 + pan);
	}
}

/* bit-crush a stretch: fewer levels and held samples, the sound of cheap digital */
static void s_crush(float *L, float *R, int n, float at, float dur, float levels, int hold)
{
	int i0 = (int)(at * SR), len = (int)(dur * SR);
	float hl = 0, hr = 0;
	for (int i = 0; i < len && i0 + i < n; i++) {
		if (i0 + i < 0) continue;
		if (i % hold == 0) { hl = L[i0 + i]; hr = R[i0 + i]; }
		L[i0 + i] = floorf(hl * levels + 0.5f) / levels; R[i0 + i] = floorf(hr * levels + 0.5f) / levels;
	}
}

/* data chatter: a stream of tiny random square blips */
static void s_data(float *L, float *R, int n, float at, float dur, float amp, float pan0, float pan1)
{
	for (float t = 0; t < dur; t += 0.018f + 0.02f * (nrand() * 0.5f + 0.5f)) {
		float x = t / dur, f = 700 + 2600 * (nrand() * 0.5f + 0.5f);
		s_square(L, R, n, at + t, 0.016f, f, f * (nrand() > 0 ? 1.3f : 0.8f), amp * (0.5f + 0.5f * (nrand() * 0.5f + 0.5f)), pan0 + (pan1 - pan0) * x, 0.5f);
	}
}

/* the menacing rumble ("gogogo"): a low growl, distorted, pulsing like heavy footsteps, growing */
static void s_menace(float *L, float *R, int n, float at, float dur, float amp)
{
	int i0 = (int)(at * SR), len = (int)(dur * SR);
	float p1 = 0, p2 = 0, lp = 0;
	for (int i = 0; i < len && i0 + i < n; i++) {
		if (i0 + i < 0) continue;
		float x = (float)i / len, tt = (float)i / SR, f = 41 + 8 * x;
		p1 += f / SR; p1 -= (int)p1; p2 += f * 2.01f / SR; p2 -= (int)p2;
		float raw = (2 * p1 - 1) * 0.6f + (2 * p2 - 1) * 0.4f + nrand() * 0.15f;
		lp += (raw - lp) * 0.06f;
		float go = 0.55f + 0.45f * sinf(2 * (float)M_PI * (5.5f + 4 * x) * tt);          /* go-go-go */
		float v = tanhf(lp * 3.0f) * go * amp * sg_sat(x * 3) * (0.4f + 0.6f * x) * sg_sat((1 - x) * 12);
		L[i0 + i] += v; R[i0 + i] += v * 0.96f;
	}
}

/* a heavy dramatic hit: a sub dropping hard, driven into distortion, a crack, and a low metal clang */
static void s_impact(float *L, float *R, int n, float at, float amp, int quiet)
{
	int i0 = (int)(at * SR);
	float ph = 0, prev = 0;
	for (int i = 0; i < (int)(1.1f * SR) && i0 + i < n; i++) {
		if (i0 + i < 0) continue;
		float tt = (float)i / SR, f = 34 + 110 * expf(-tt / 0.05f);
		ph += 2 * (float)M_PI * f / SR;
		float sub = quiet ? 0 : tanhf(sinf(ph) * expf(-tt / 0.32f) * 3.0f) * 0.75f;
		float x = nrand(), hp = x - prev; prev = x;
		float crack = hp * expf(-tt / 0.012f) * 0.9f;
		float v = (sub + crack) * amp;
		L[i0 + i] += v; R[i0 + i] += v;
	}
	static const float part[4] = {1.0f, 2.76f, 5.40f, 8.93f};
	for (int k = 0; k < 4; k++) s_ping(L, R, n, at, 196.0f * part[k], amp * 0.12f / (1 + k), 0.5f / (1 + k * 0.5f), (k & 1) ? 0.3f : -0.3f, 0.001f);
}

/* a reverse swell: noise rising and brightening, cut off at `end` */
static void s_revswell(float *L, float *R, int n, float end, float dur, float amp)
{
	int i0 = (int)((end - dur) * SR), len = (int)(dur * SR);
	float lp = 0;
	for (int i = 0; i < len && i0 + i < n; i++) {
		if (i0 + i < 0) continue;
		float x = (float)i / len; lp += (nrand() - lp) * (0.02f + 0.5f * x * x);
		float v = lp * x * x * x * amp;
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
	float t_burst = t_crack + CRACK_BURST;
	/* the gem boots: three square blips going up, and the rumble begins underneath */
	static const float boot[3] = {1046.5f, 1318.5f, 1568.0f};
	for (int k = 0; k < 3; k++) s_square(L, R, n, t_gem + k * 0.07f, 0.09f, boot[k], boot[k], 0.05f, 0, 0.25f);
	s_menace(L, R, n, t_gem + 0.1f, t_lock - t_gem - 0.1f, 0.22f);
	/* its beat: a short low square "bip" */
	for (float b = t_gem + 0.82f * 0.07f - 0.02f; b < t_lock - 0.1f; b += 0.82f) s_square(L, R, n, b, 0.06f, 196, 180, 0.06f, 0, 0.5f);
	/* the layers: a chirping data sweep each, landing with a heavy hit */
	{
		const float start[3] = {t_form, t_form + 0.25f, t_form + 0.50f}, land[3] = {t_lock - 0.50f, t_lock - 0.25f, t_lock};
		const float pan0[3] = {0.0f, -0.8f, 0.8f};
		for (int l = 0; l < 3; l++) {
			s_data(L, R, n, start[l], land[l] - start[l] - 0.05f, 0.022f, pan0[l], 0);
			s_square(L, R, n, start[l], land[l] - start[l], 160 + 60 * l, 900 + 300 * l, 0.03f, pan0[l] * 0.5f, 0.3f);
			if (l < 2) s_impact(L, R, n, land[l], 0.35f, quiet);
		}
	}
	/* the LOCK: a reverse swell into a huge hit, a glitch stutter and a short stab */
	s_revswell(L, R, n, t_lock, 0.8f, 0.6f);
	s_impact(L, R, n, t_lock, 0.75f, quiet);
	for (int k = 0; k < 6; k++) s_square(L, R, n, t_lock + 0.08f + k * 0.045f, 0.03f, 880 - k * 90, 880 - k * 90, 0.05f * (1 - k / 7.f), (k & 1) ? 0.4f : -0.4f, 0.5f);
	{
		static const float stab[4] = {146.83f, 220.0f, 293.66f, 349.23f};                 /* D minor, short and sharp */
		s_pad(L, R, n, t_lock, 0.55f, stab, 4, 0.07f, 0.35f, 0.05f, 0.005f, 0.35f);
	}
	/* a digital zip up into the corner */
	s_square(L, R, n, t_open, 0.45f, 220, 1760, 0.04f, 0.5f, 0.5f);
	s_data(L, R, n, t_open, 0.4f, 0.015f, 0, 0.7f);
	/* the PRESSURE: the rumble back, harder; a digital tick on every jump of the cracks */
	s_menace(L, R, n, t_crack - 0.1f, CRACK_BURST + 0.1f, 0.42f);
	for (int k = 1; k <= 7; k++) {
		float at = t_crack + k / 7.0f * CRACK_BURST - 0.02f;
		if (at >= t_burst) break;
		s_square(L, R, n, at, 0.025f, 3200 - k * 200, 2400 - k * 150, 0.05f + 0.01f * k, 0.6f, 0.5f);
		int i0 = (int)(at * SR);
		for (int i = 0; i < (int)(0.01f * SR) && i0 + i < n; i++) { float v = nrand() * expf(-(float)i / (0.0015f * SR)) * (0.2f + 0.04f * k); L[i0 + i] += v * 0.7f; R[i0 + i] += v * 1.2f; }
	}
	s_revswell(L, R, n, t_burst, 0.7f, 0.75f);
	/* the BURST: the biggest hit, a bit-crushed explosion falling away, data spraying, droplets blipping */
	s_impact(L, R, n, t_burst, 1.0f, quiet);
	{
		int i0 = (int)(t_burst * SR), len = (int)(0.9f * SR);
		float lp = 0;
		for (int i = 0; i < len && i0 + i < n; i++) {
			float x = (float)i / len; lp += (nrand() - lp) * (0.5f * (1 - x) + 0.02f);
			float v = lp * expf(-x * 3.5f) * 0.9f, pan = 0.6f - 1.2f * x;
			L[i0 + i] += v * (1 - pan); R[i0 + i] += v * (1 + pan);
		}
		s_crush(L, R, n, t_burst + 0.01f, 0.5f, 10, 6);
	}
	s_data(L, R, n, t_burst + 0.05f, 0.8f, 0.025f, 0.6f, -0.7f);
	for (int k = 0; k < 16; k++) {
		float u = nrand() * 0.5f + 0.5f, f = 600 + 900 * (nrand() * 0.5f + 0.5f);
		s_square(L, R, n, t_burst + 0.25f + u * 0.7f, 0.04f, f, f * 1.8f, 0.025f, 0.6f - 1.3f * u, 0.25f);
	}
	/* the buttons appear: four rising blips */
	static const float bl[4] = {523.25f, 659.25f, 783.99f, 1046.5f}, bp[4] = {-0.6f, -0.2f, 0.2f, 0.6f};
	for (int k = 0; k < 4; k++) s_square(L, R, n, t_burst + 0.62f + k * 0.09f, 0.10f, bl[k], bl[k] * 1.02f, 0.045f, bp[k], 0.25f);
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
