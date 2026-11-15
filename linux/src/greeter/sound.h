/*
 * The intro's sound, synthesised at start-up (no sound files) and played through ALSA directly
 * (no libraries): the kernel's PCM and control interfaces, as in <sound/asound.h>.
 *
 * The track follows the intro's timeline (T_DOT, T_PEN, T_SNAP, T_OPEN in silver-greeter.c):
 *   a low heartbeat in the dark, a rising whoosh as the pencil comes up, wood creaking under
 *   strain, then the SNAP: a crack, a deep boom that sinks as time slows, glass shattering
 *   and ticking as the cracks run, and a soft shimmering chord as the panes light up.
 * A little reverb puts it all in one room. Calm mode plays it quieter, without the boom.
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

/* Render the intro track into interleaved 16-bit stereo. Returns the number of frames. */
static int intro_track(int16_t **out, float t_dot, float t_pen, float t_snap, float t_open, float t_end, int quiet)
{
	int n = (int)((t_end + 1.6f) * SR);
	float *L = calloc((size_t)n, sizeof(float)), *R = calloc((size_t)n, sizeof(float));
	int16_t *o = malloc((size_t)n * 4);
	if (!L || !R || !o) { free(L); free(R); free(o); return 0; }
	/* heartbeat: lub-dub, every 0.82 s, from the red dot until the snap */
	for (float b = t_dot + 0.82f * 0.07f - 0.02f; b < t_snap - 0.1f; b += 0.82f)
		for (int k = 0; k < 2; k++) {
			float at = b + k * 0.19f, amp = k ? 0.42f : 0.62f;
			for (int i = 0; i < (int)(0.32f * SR); i++) {
				int j = (int)(at * SR) + i; if (j < 0 || j >= n) continue;
				float tt = (float)i / SR, f = 46 + 38 * expf(-tt / 0.03f);
				float v = sinf(2 * (float)M_PI * f * tt) * expf(-tt / 0.075f) * (1 - expf(-tt / 0.004f)) * amp;
				L[j] += v; R[j] += v;
			}
		}
	/* the rise: filtered noise swelling up */
	{
		float lp = 0, lp2 = 0;
		int i0 = (int)(t_pen * SR), len = (int)(0.9f * SR);
		for (int i = 0; i < len && i0 + i < n; i++) {
			float x = (float)i / len, cut = 0.004f + 0.05f * x * x, env = sinf((float)M_PI * sg_sat(x * 1.1f)) * 0.35f;
			lp += (nrand() - lp) * cut; lp2 += (lp - lp2) * cut;
			L[i0 + i] += lp2 * 6 * env; R[i0 + i] += lp2 * 6 * env * 0.85f;
		}
	}
	/* strain: wood creaking (stick-slip clicks through a resonance) */
	{
		float y1 = 0, y2 = 0, f = 160, r = 0.995f;
		int i0 = (int)((t_snap - 0.62f) * SR), len = (int)(0.62f * SR);
		for (int i = 0; i < len && i0 + i < n; i++) {
			float x = (float)i / len, rate = 40 + 220 * x;
			float imp = (nrand() * 0.5f + 0.5f) < rate / SR * 1.0f ? nrand() * 0.9f : 0;
			float w = 2 * (float)M_PI * (f + 120 * x) / SR;
			float y = imp + 2 * r * cosf(w) * y1 - r * r * y2; y2 = y1; y1 = y;
			float v = y * 0.05f * (0.3f + x);
			L[i0 + i] += v; R[i0 + i] += v * 0.9f;
		}
	}
	/* the snap */
	{
		int i0 = (int)(t_snap * SR);
		float hp = 0, prev = 0;
		for (int i = 0; i < (int)(0.09f * SR) && i0 + i < n; i++) {               /* the crack itself */
			float tt = (float)i / SR, x = nrand(), h = x - prev; prev = x; hp = h;
			float env = expf(-tt / 0.012f) + 0.35f * expf(-tt / 0.05f) * (nrand() > 0.3f);
			L[i0 + i] += hp * env * 0.9f; R[i0 + i] += hp * env * 0.9f;
		}
		for (int k = 0; k < 6; k++) {                                              /* splintering after it */
			int j0 = i0 + (int)((0.01f + k * 0.018f + 0.01f * nrand()) * SR);
			for (int i = 0; i < (int)(0.02f * SR) && j0 + i < n; i++) { float v = nrand() * expf(-(float)i / SR / 0.004f) * 0.4f; L[j0 + i] += v * (k & 1 ? 0.6f : 1); R[j0 + i] += v * (k & 1 ? 1 : 0.6f); }
		}
		if (!quiet) for (int i = 0; i < (int)(1.6f * SR) && i0 + i < n; i++) {     /* the boom, sinking as time slows */
			float tt = (float)i / SR, f = 38 + 70 * expf(-tt / 0.10f) - 8 * sg_sat(tt / 0.6f);
			static float ph; ph += 2 * (float)M_PI * f / SR;
			float v = sinf(ph) * expf(-tt / 0.42f) * 0.95f;
			v = tanhf(v * 1.6f) * 0.75f;
			L[i0 + i] += v; R[i0 + i] += v;
		}
		{                                                                          /* the slow-motion drop: a tone sliding down */
			float ph = 0;
			for (int i = 0; i < (int)(0.75f * SR) && i0 + i < n; i++) {
				float tt = (float)i / SR, f = 340 * expf(-tt / 0.28f) + 70;
				ph += 2 * (float)M_PI * f / SR;
				float v = (sinf(ph) + 0.3f * sinf(ph * 2.01f)) * sinf((float)M_PI * sg_sat(tt / 0.75f)) * 0.10f;
				L[i0 + i] += v; R[i0 + i] += v;
			}
		}
		for (int k = 0; k < 70; k++) {                                             /* glass: a shower of tiny pings */
			float u = nrand() * 0.5f + 0.5f, at = t_snap + 0.015f + u * u * 0.95f;
			s_ping(L, R, n, at, 2200 + (nrand() * 0.5f + 0.5f) * 5200, 0.06f * (1.2f - u), 0.02f + 0.07f * (nrand() * 0.5f + 0.5f), nrand() * 0.8f, 0);
		}
		float hiss_hp = 0, hp_prev = 0;                                            /* and its hiss */
		for (int i = 0; i < (int)(0.9f * SR) && i0 + i < n; i++) {
			float tt = (float)i / SR, x = nrand(); hiss_hp = x - hp_prev; hp_prev = x;
			float v = hiss_hp * expf(-tt / 0.18f) * 0.18f;
			L[i0 + i] += v * (1 + nrand() * 0.3f); R[i0 + i] += v * (1 + nrand() * 0.3f);
		}
		for (int k = 0; k < 90; k++) {                                             /* the cracks running: dry ticks, thinning out */
			float u = nrand() * 0.5f + 0.5f, at = t_snap + 0.05f + u * u * 1.0f;
			int j0 = (int)(at * SR);
			float pan = nrand();
			for (int i = 0; i < 90 && j0 + i < n; i++) { float v = nrand() * expf(-i / 12.0f) * 0.22f * (1.1f - u); L[j0 + i] += v * (1 - pan); R[j0 + i] += v * (1 + pan); }
		}
	}
	/* the panes light up: a soft, shimmering chord */
	{
		static const float notes[5] = {220.0f, 277.18f, 329.63f, 440.0f, 659.25f};
		for (int k = 0; k < 5; k++) {
			s_ping(L, R, n, t_open - 0.05f + k * 0.03f, notes[k], 0.06f, 0.9f, (k - 2) * 0.25f, 0.12f);
			s_ping(L, R, n, t_open - 0.05f + k * 0.03f, notes[k] * 1.004f, 0.04f, 0.9f, -(k - 2) * 0.25f, 0.12f);
		}
		float lp = 0;
		int i0 = (int)((t_open - 0.35f) * SR), len = (int)(0.8f * SR);
		for (int i = 0; i < len && i0 + i < n; i++) {                              /* an airy whoosh into it */
			float x = (float)i / len; lp += (nrand() - lp) * (0.02f + 0.1f * x);
			float v = lp * sinf((float)M_PI * x) * 0.25f;
			L[i0 + i] += v; R[i0 + i] += v;
		}
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

#define INTRO_TIMES T_DOT, T_PEN, T_SNAP, T_OPEN, T_END
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
				} else if (ev[i] == 'x') sfx_stop(&pl, &intro);
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
