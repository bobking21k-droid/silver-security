/*
 * The SilverOS emblem: nested triangles of silver mosaic around a violet gem.
 *
 *          /\            layer 0   the outer triangle, a wide band and a narrow one inside it
 *         /  \           layer 1   the middle triangle
 *        / /\ \          layer 2   the inner star: a small triangle and an upside-down one
 *       / /\/\ \         layer 3   the gem, a glowing violet triangle in the middle
 *      /________\
 *
 * Each layer turns, scales and fades on its own, so the intro and the boot chooser can spin the
 * layers apart and lock them together. Everything is computed per pixel (no images), so it is
 * sharp at any screen size: a triangle's "distance" from its centre is the largest of the three
 * edge projections, which makes bands, anti-aliased edges and the triangular mosaic tiles cheap.
 *
 * Needs only sgfx (freestanding: the boot chooser uses it before any OS exists).
 */
#pragma once
#include "sgfx.h"
#define WHITE_EMB RGB(255, 255, 255)

typedef struct {
	float rot[4], scale[4], alpha[4];   /* per layer: turn (radians), size (1 = home), opacity */
	float gem;                          /* gem glow: 0 dim .. 1 normal .. 2 flaring */
	float light_bg;                     /* 0: silver for dark backgrounds, 1: darker steel for light ones */
	float shine;                        /* a sheen sweeping across, diagonal position (-1.5 .. 1.5; outside: none) */
	float t;                            /* time, for the gem's moving light */
} Emblem;

static Emblem emblem_rest(void)
{
	Emblem e;
	for (int i = 0; i < 4; i++) { e.rot[i] = 0; e.scale[i] = 1; e.alpha[i] = 1; }
	e.gem = 1; e.light_bg = 0; e.shine = -9; e.t = 0;
	return e;
}

/* bands of each layer, in units of the outer triangle's inradius: {inner, outer} */
static const float EMB_BAND[5][2] = {
	{0.855f, 1.00f},                    /* layer 0, wide */
	{0.735f, 0.815f},                   /* layer 0, narrow */
	{0.520f, 0.625f},                   /* layer 1 */
	{0.265f, 0.345f},                   /* layer 2, up (and the same band upside down) */
	{0.000f, 0.150f},                   /* the gem */
};

/* the triangle distance of local point (x, y) for a triangle pointing up, turned by (c, s) */
static inline float emb_tdist(float x, float y, float c, float s, float *a, float *b, float *k)
{
	/* edge normals at 90, 210 and 330 degrees, turned by the layer's angle */
	float n0x = -s, n0y = c;                                       /* 90 deg: the base, below */
	float n1x = -0.8660254f * c + 0.5f * s, n1y = -0.8660254f * s - 0.5f * c;
	float n2x = 0.8660254f * c + 0.5f * s, n2y = 0.8660254f * s - 0.5f * c;
	float d0 = x * n0x + y * n0y, d1 = x * n1x + y * n1y, d2 = x * n2x + y * n2y;
	if (a) { *a = d0; *b = d1; *k = d2; }
	float m = d0 > d1 ? d0 : d1;
	return m > d2 ? m : d2;
}

/* the silver of one mosaic tile at (x, y) in a layer's frame; returns 0 where a tile is missing */
static uint32_t emb_tile(float d0, float d1, float d2, float x, float y, int layer, float px, float shade, float light_bg, float *keep)
{
	const float ts = 0.042f;                                       /* tile size, in inradius units */
	float a = d0 / ts + 0.31f, b = d1 / ts - 0.17f, k = d2 / ts - 0.14f;   /* offsets sum to 0: a true triangle lattice */
	float fa = sg_floor(a), fb = sg_floor(b), fk = sg_floor(k);
	int ia = (int)fa, ib = (int)fb, ik = (int)fk;
	uint32_t h = sg_hash(ia * 7 + ik * 131 + layer * 977, ib * 13 - ik * 17 + layer * 31);
	*keep = ((h >> 11) & 127) == 0 ? 0 : 1;                         /* a few tiles are missing */
	float ga = a - fa, gb = b - fb, gk = k - fk;
	float e = sg_min(sg_min(sg_min(ga, 1 - ga), sg_min(gb, 1 - gb)), sg_min(gk, 1 - gk));
	float grout = sg_sat((e - 0.055f) / (px / ts + 1e-4f) + 0.5f);  /* 0 in the joints between tiles */
	float g = 150 + (float)(h & 63) + (sg_noise(x * 2.6f + layer * 3.1f, y * 2.6f) - 0.5f) * 70;
	if (((h >> 7) & 7) == 0) g *= 0.72f;                           /* some darker pieces */
	g *= shade;
	if (light_bg > 0) g = sg_lerp(g, 36 + g * 0.52f, light_bg);
	g *= 0.30f + 0.70f * grout;
	float r = g * 0.96f, gg = g * 0.97f, bb = g * 1.04f + 3;
	return RGB((int)sg_clamp(r, 0, 255), (int)sg_clamp(gg, 0, 255), (int)sg_clamp(bb, 0, 255));
}

/* the gem's colour at local point (x, y) (units of its own size, -1..1 roughly) */
static uint32_t emb_gem(float x, float y, float d, float t, float glow)
{
	float swirl = 0.5f + 0.5f * sg_sin(x * 5.1f + y * 3.3f + t * 1.7f);
	float swirl2 = 0.5f + 0.5f * sg_sin(-x * 4.2f + y * 6.0f - t * 1.1f + 1.3f);
	uint32_t c = sg_mix(RGB(108, 52, 236), RGB(64, 104, 255), sg_sat(0.5f + x * 0.8f));
	c = sg_mix(c, RGB(236, 112, 246), 0.55f * swirl * swirl2);
	c = sg_mix(c, RGB(255, 236, 255), 0.65f * sg_bump(x + 0.25f, 0.45f) * sg_bump(y + 0.30f, 0.40f));   /* a highlight up-left */
	c = sg_mix(c, RGB(40, 14, 96), sg_sat(d * 1.4f - 0.15f) * 0.55f);                                     /* darker towards its edges */
	if (glow > 1) c = sg_mix(c, WHITE_EMB, sg_sat((glow - 1) * 0.6f));
	return c;
}

/* Draw the emblem centred on (cx, cy); R is the outer triangle's inradius in pixels. */
static void emblem_draw(Surf *s, float cx, float cy, float R, const Emblem *e)
{
	if (R < 2) return;
	float smax = 0;
	for (int l = 0; l < 4; l++) if (e->alpha[l] > 0.003f && e->scale[l] > smax) smax = e->scale[l];
	if (smax <= 0) return;
	/* the gem's glow behind everything, and in front once it flares */
	float ga = e->alpha[3];
	if (ga > 0.003f && e->gem > 0) {
		sg_glow(s, cx, cy, R * e->scale[3] * 1.1f * (0.8f + 0.25f * e->gem), RGB(118, 60, 255), 0.30f * ga * sg_min(e->gem, 2.0f));
		sg_glow(s, cx, cy, R * e->scale[3] * 0.45f, RGB(190, 120, 255), 0.25f * ga * sg_min(e->gem, 2.0f));
	}
	float ext = 2.05f * R * smax;                                  /* circumradius of the largest triangle */
	int x0 = (int)(cx - ext), x1 = (int)(cx + ext) + 1, y0 = (int)(cy - ext), y1 = (int)(cy + ext) + 1;
	if (x0 < s->cx0) x0 = s->cx0;
	if (y0 < s->cy0) y0 = s->cy0;
	if (x1 > s->cx1) x1 = s->cx1;
	if (y1 > s->cy1) y1 = s->cy1;
	float lc[4], ls[4], inv[4];
	for (int l = 0; l < 4; l++) { lc[l] = sg_cos(e->rot[l]); ls[l] = sg_sin(e->rot[l]); inv[l] = 1 / (R * (e->scale[l] > 0.01f ? e->scale[l] : 0.01f)); }
	float ext2 = ext * ext;
	for (int y = y0; y < y1; y++) {
		uint32_t *row = s->px + y * s->w;
		float fy = y + 0.5f - cy;
		for (int x = x0; x < x1; x++) {
			float fx = x + 0.5f - cx;
			if (fx * fx + fy * fy > ext2) continue;
			uint32_t c = row[x];
			int touched = 0;
			for (int l = 0; l < 4; l++) {
				float al = e->alpha[l];
				if (al <= 0.003f) continue;
				float lx = fx * inv[l], ly = fy * inv[l], px = inv[l];         /* local units; px = one pixel */
				float d0, d1, d2, d = emb_tdist(lx, ly, lc[l], ls[l], &d0, &d1, &d2);
				if (d > 1.05f) continue;
				float cov = 0;
				if (l == 3) {
					cov = sg_sat((EMB_BAND[4][1] - d) / px + 0.5f);
					if (cov <= 0) continue;
					/* gem-local coordinates, turned with it */
					float gx = (lx * lc[3] + ly * ls[3]) / EMB_BAND[4][1], gy = (-lx * ls[3] + ly * lc[3]) / EMB_BAND[4][1];
					c = sg_mix(c, emb_gem(gx, gy, d / EMB_BAND[4][1], e->t, e->gem), cov * al);
					touched = 1;
					continue;
				}
				int nb = l == 0 ? 2 : 1, b0 = l == 0 ? 0 : l == 1 ? 2 : 3;
				for (int b = 0; b < nb; b++) {
					float r0 = EMB_BAND[b0 + b][0], r1 = EMB_BAND[b0 + b][1];
					cov += sg_sat((r1 - d) / px + 0.5f) * sg_sat((d - r0) / px + 0.5f);
				}
				if (l == 2) {                                           /* the inner star's upside-down triangle */
					float u0, u1, u2, du = emb_tdist(-lx, -ly, lc[2], ls[2], &u0, &u1, &u2);
					float cu = sg_sat((EMB_BAND[3][1] - du) / px + 0.5f) * sg_sat((du - EMB_BAND[3][0]) / px + 0.5f);
					if (cu > cov) { cov = cu; d0 = u0; d1 = u1; d2 = u2; }
				}
				if (cov <= 0.002f) continue;
				/* light from the upper left, and the sheen */
				float shade = 0.86f - 0.22f * (lx * 0.55f + ly * 0.85f);
				if (e->shine > -2 && e->shine < 2) shade += 0.75f * sg_bump(lx * 0.62f + ly * 0.62f - e->shine, 0.22f);
				float keep;
				uint32_t tc = emb_tile(d0, d1, d2, lx, ly, l, px, shade, e->light_bg, &keep);
				cov *= keep;
				if (cov <= 0) continue;
				if (e->light_bg > 0) c = sg_mix(c, RGB(20, 20, 24), cov * al * 0.25f * e->light_bg);   /* a dark rim reads on white */
				c = sg_mix(c, tc, cov * al);
				touched = 1;
			}
			if (touched) row[x] = c;
		}
	}
}
