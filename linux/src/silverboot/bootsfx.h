/*
 * The boot chooser's sounds, built with ../common/sfx.h and timed against scenes.h:
 *
 *   amb      a low, slowly breathing drone under the chooser (loops)
 *   to_l     moving to SilverOS: a dark whoosh to the left, a dry click and the emblem's
 *            metallic clink with a glint of its gem
 *   to_r     moving to Windows: a brighter whoosh to the right and a soft two-note chime
 *   tick     the auto-start countdown, once a second
 *   go       Enter: a deep thump with a rising swell
 *   trans    everything after Enter, start to finish: the fake Windows start-up bloom (an
 *            original chime, not Windows' own), a tape stop as the picture flips, a sinking,
 *            crackling drone as it melts, electric zaps on every flash, then the system check:
 *            a tick as each line appears, the dotted leader rattling in, a two-note "OK",
 *            a heartbeat under it all and a rising tone for "STARTING SILVEROS"
 *   shatter  picking Windows: glass breaking over a low boom
 */
#pragma once
#include "../common/sfx.h"

typedef struct { SfxClip amb, to_l, to_r, tick, go, trans, shatter; } BootSfx;

static SfxClip build_amb(void)
{
	const float len = 6.0f, xf = 0.5f;
	SfxMix m = sfx_mix(len + xf);
	if (!m.n) return (SfxClip){0, 0};
	for (int i = 0; i < m.n; i++) {                     /* 55 Hz and a fifth above: whole cycles in 6 s, so it loops */
		float t = (float)i / SFX_RATE, br = 0.6f + 0.4f * sfx_sin(SFX_TAU * t / 3.0f);
		float v = (sfx_sin(SFX_TAU * 55 * t) * 0.5f + sfx_sin(SFX_TAU * 82.5f * t) * 0.25f + sfx_sin(SFX_TAU * 110.5f * t) * 0.08f) * br * 0.22f;
		m.L[i] += v; m.R[i] += v;
	}
	sfx_whoosh(&m, 0.0f, len + xf, 0.05f, -0.3f, 0.3f, 0.15f);    /* air moving in the room */
	int nx = (int)(xf * SFX_RATE), nl = (int)(len * SFX_RATE);
	for (int i = 0; i < nx; i++) {                      /* crossfade the tail into the head */
		float a = (float)i / nx;
		m.L[i] = m.L[i] * a + m.L[nl + i] * (1 - a); m.R[i] = m.R[i] * a + m.R[nl + i] * (1 - a);
	}
	m.n = nl;
	SfxClip c = sfx_clip(&m, 1.0f); sfx_free_mix(&m); return c;
}

static SfxClip build_move(int right)
{
	SfxMix m = sfx_mix(0.8f);
	if (!m.n) return (SfxClip){0, 0};
	float s = right ? 1 : -1;
	sfx_whoosh(&m, 0, 0.32f, 0.35f, -0.6f * s, 0.7f * s, right ? 0.9f : 0.45f);
	sfx_noise_burst(&m, 0.10f, 0.03f, 0.004f, 0.35f, 1, 0, 0.5f * s);              /* the click as it lands */
	if (right) { sfx_ping(&m, 0.11f, 987.8f, 0.10f, 0.20f, 0.4f, 0.004f); sfx_ping(&m, 0.17f, 1480.0f, 0.07f, 0.25f, 0.5f, 0.004f); }
	else {
		sfx_thump(&m, 0.10f, 160, 70, 0.03f, 0.06f, 0.35f, 0);
		static const float part[4] = {1.0f, 2.76f, 5.40f, 8.93f}, pa[4] = {0.09f, 0.055f, 0.035f, 0.02f};
		for (int k = 0; k < 4; k++) sfx_ping(&m, 0.11f, 930.0f * part[k], pa[k], 0.30f / (1 + k * 0.6f), -0.4f, 0.0008f);   /* the emblem's clink */
		sfx_ping(&m, 0.16f, 2637.0f, 0.04f, 0.25f, -0.3f, 0.002f);                  /* and its gem */
	}
	sfx_room(&m, 0.25f, 1.0f);
	SfxClip c = sfx_clip(&m, 1.0f); sfx_free_mix(&m); return c;
}

static SfxClip build_tick(void)
{
	SfxMix m = sfx_mix(0.25f);
	if (!m.n) return (SfxClip){0, 0};
	sfx_noise_burst(&m, 0, 0.02f, 0.003f, 0.25f, 1, 0, 0);
	sfx_ping(&m, 0, 2100, 0.05f, 0.03f, 0, 0);
	SfxClip c = sfx_clip(&m, 1.0f); sfx_free_mix(&m); return c;
}

static SfxClip build_go(void)
{
	SfxMix m = sfx_mix(1.2f);
	if (!m.n) return (SfxClip){0, 0};
	sfx_thump(&m, 0, 120, 42, 0.06f, 0.25f, 0.9f, 1.5f);
	sfx_noise_burst(&m, 0, 0.05f, 0.01f, 0.4f, 1, 0, 0);
	sfx_glide(&m, 0.0f, 0.5f, 220, 880, 0.06f, 0, 0);
	sfx_room(&m, 0.3f, 1.0f);
	SfxClip c = sfx_clip(&m, 1.0f); sfx_free_mix(&m); return c;
}

/* the long track from Enter to the end of the system check */
static SfxClip build_trans(int quiet)
{
	float flick_len = 0;
	for (unsigned i = 0; i < sizeof FLICK / sizeof FLICK[0]; i++) flick_len += FLICK[i].dur;
	float t_flip = T_CLASSIC, t_melt = t_flip + T_FLIP, t_flash = t_melt + T_MELT, t_out = t_flash + flick_len, t_chk = t_out + 0.45f;
	SfxMix m = sfx_mix(t_chk + T_CHECKS + 0.6f);
	if (!m.n) return (SfxClip){0, 0};
	/* 1. a modern PC starting: a soft, original bloom as the logo appears */
	static const float bloom[5] = {440.0f, 554.37f, 659.25f, 830.61f, 1108.73f};
	for (int k = 0; k < 5; k++) {
		sfx_ping(&m, 0.32f + k * 0.07f, bloom[k], 0.07f, 1.1f, (k - 2) * 0.2f, 0.03f);
		sfx_ping(&m, 0.32f + k * 0.07f, bloom[k] * 1.003f, 0.05f, 1.1f, -(k - 2) * 0.2f, 0.03f);
	}
	sfx_whoosh(&m, 0.15f, 1.6f, 0.10f, -0.2f, 0.2f, 0.25f);
	for (float t = 1.0f; t < t_flip; t += 0.5f) sfx_ping(&m, t, 3520, 0.008f, 0.02f, 0, 0);   /* the spinner, faintly */
	/* 2. the flip: a tape stop and a stutter */
	sfx_glide(&m, t_flip, T_FLIP * 0.9f, 440, 55, 0.18f, 0, 1);
	sfx_glide(&m, t_flip, T_FLIP * 0.9f, 659, 70, 0.10f, 0, 1);
	for (int k = 0; k < 6; k++) sfx_noise_burst(&m, t_flip + k * T_FLIP / 6, 0.03f, 0.01f, 0.25f, 1, 0, (k & 1) ? 0.6f : -0.6f);
	/* 3. the melt: a drone sinking out of tune, crackle, drips */
	{
		int i0 = (int)(t_melt * SFX_RATE), len = (int)(T_MELT * SFX_RATE);
		float p1 = 0, p2 = 0;
		for (int i = 0; i < len; i++) {
			float x = (float)i / len, f1 = 98 * sfx_exp(-x * 0.9f), f2 = 147 * sfx_exp(-x * 1.25f);
			p1 += SFX_TAU * f1 / SFX_RATE; p2 += SFX_TAU * f2 / SFX_RATE;
			if (p1 > SFX_TAU) p1 -= SFX_TAU;
			if (p2 > SFX_TAU) p2 -= SFX_TAU;
			float env = sg_sat(x / 0.1f) * sg_sat((1 - x) / 0.25f);
			float v = sfx_tanh((sfx_sin(p1) + 0.7f * sfx_sin(p2)) * 1.8f) * 0.16f * env;
			sfx_put(&m, i0 + i, v, sfx_sin(x * 9) * 0.3f);
		}
		sfx_crackle(&m, t_melt, T_MELT, 140, 0.11f, 1);
		for (int k = 0; k < 14; k++) {                  /* drips: little falling bloops */
			float at = t_melt + sfx_rand01() * T_MELT * 0.9f, f = 500 + 700 * sfx_rand01();
			sfx_glide(&m, at, 0.09f, f, f * 0.45f, 0.05f, sfx_noise() * 0.7f, 0);
		}
	}
	/* 4. the flashes: a zap on every cut, the red one hardest */
	{
		float t = t_flash;
		for (unsigned i = 0; i < sizeof FLICK / sizeof FLICK[0]; i++) {
			float a = FLICK[i].inv == 2 ? 0.55f : 0.30f;
			if (!quiet || FLICK[i].inv != 2) {
				sfx_noise_burst(&m, t, 0.06f, 0.02f, a, 1, 0, 0);
				sfx_glide(&m, t, sg_min(FLICK[i].dur, 0.12f), 120, 60, a * 0.6f, 0, 1);
			}
			if (FLICK[i].logo) sfx_ping(&m, t + 0.01f, FLICK[i].inv == 1 ? 1760.0f : 1318.5f, 0.05f, 0.25f, 0, 0.002f);   /* the emblem glints */
			t += FLICK[i].dur;
		}
		if (!quiet) sfx_thump(&m, t_flash, 90, 35, 0.08f, 0.4f, 0.7f, 1.2f);
	}
	/* 5. the system check */
	for (float b = 0.07f * 0.9f; b < T_CHECKS - 0.4f; b += 0.9f) {                       /* heartbeat */
		sfx_thump(&m, t_chk + b, 75, 45, 0.03f, 0.07f, 0.45f, 0);
		sfx_thump(&m, t_chk + b + 0.21f, 70, 45, 0.03f, 0.06f, 0.30f, 0);
	}
	for (int i = 0; i < 4; i++) {
		float ti = t_chk + 0.55f + i * 0.72f;
		sfx_noise_burst(&m, ti, 0.03f, 0.006f, 0.30f, 1, 0, -0.3f);                    /* the line appears */
		sfx_ping(&m, ti, 1760, 0.05f, 0.04f, -0.3f, 0);
		for (int d = 0; d < 8; d++) sfx_ping(&m, ti + 0.1f + d * 0.05f, 2600 + d * 60, 0.025f, 0.015f, -0.2f + d * 0.06f, 0);   /* the leader */
		sfx_ping(&m, ti + 0.52f, 880, 0.10f, 0.12f, 0.3f, 0.002f);                      /* OK */
		sfx_ping(&m, ti + 0.60f, 1318.5f, 0.10f, 0.18f, 0.3f, 0.002f);
	}
	sfx_glide(&m, t_chk + 0.55f + 3 * 0.72f + 0.9f, 0.9f, 220, 660, 0.08f, 0, 0);     /* STARTING SILVEROS */
	sfx_room(&m, 0.28f, 1.0f);
	SfxClip c = sfx_clip(&m, 1.0f); sfx_free_mix(&m); return c;
}

static SfxClip build_shatter(int quiet)
{
	SfxMix m = sfx_mix(T_SHATTER + 0.8f);
	if (!m.n) return (SfxClip){0, 0};
	sfx_noise_burst(&m, 0, 0.08f, 0.015f, 0.8f, 1, 0, 0);
	if (!quiet) sfx_thump(&m, 0, 100, 38, 0.08f, 0.45f, 0.9f, 1.4f);
	for (int k = 0; k < 90; k++) {
		float u = sfx_rand01(), at = 0.01f + u * u * T_SHATTER * 0.9f;
		sfx_ping(&m, at, 2000 + 5500 * sfx_rand01(), 0.06f * (1.2f - u), 0.02f + 0.08f * sfx_rand01(), sfx_noise() * 0.8f, 0);
	}
	sfx_noise_burst(&m, 0, T_SHATTER, 0.25f, 0.18f, 1, 0, 0);
	sfx_crackle(&m, 0.05f, T_SHATTER, 120, 0.25f, 2);
	sfx_room(&m, 0.3f, 1.0f);
	SfxClip c = sfx_clip(&m, 1.0f); sfx_free_mix(&m); return c;
}

static void bootsfx_build(BootSfx *b, int with_trans, int quiet)
{
	b->amb = build_amb(); b->to_l = build_move(0); b->to_r = build_move(1);
	b->tick = build_tick(); b->go = build_go(); b->shatter = build_shatter(quiet);
	if (with_trans) b->trans = build_trans(quiet);
}
