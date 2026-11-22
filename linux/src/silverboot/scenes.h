/*
 * silverboot scenes -- everything the boot chooser draws, independent of UEFI so the same
 * code runs in the real boot program (silverboot.c) and in the desktop preview (preview.c).
 *
 *   chooser      split screen: SilverOS (white world, the emblem turning) | Windows (colour)
 *   transition   pick SilverOS: classic Windows scene -> flips grey -> melts -> logo flicker
 *   checks       black-and-white hardware check
 *   shatter      pick Windows: the screen breaks apart
 *
 * The host provides sb_alloc() and the three font blobs.
 */
#pragma once
#include "../common/sgfx.h"
#include "../common/emblem.h"

extern void *sb_alloc(size_t bytes);
extern const uint8_t font_scrawl[], font_text[], font_caption[], font_mono[];

#define WHITE RGB(255, 255, 255)
#define BLACK RGB(0, 0, 0)
#define INK RGB(12, 12, 14)
#define TIMEOUT_S 8.0f

static Font FD, FT, FC, FM;              /* display (Silver Scrawl), text (Inter), caption (Michroma), mono (JetBrains Mono) */
#define BLOOD RGB(232, 16, 24)
static int calm;                         /* calm mode: no strobing, flashes become fades */
static int W, H;
static uint32_t *bg_a, *bg_b, *snap, *scene;   /* baked backgrounds (SilverOS / Windows selected), scratch */
static float U;                          /* layout unit: 1% of the screen height */

/* ================================================================ the seam */
static float seam_x(float y)
{
	float k = y / (H / 15.0f);
	int i = (int)sg_floor(k);
	float f = k - i;
	float a = ((sg_hash(i, 77) & 255) / 255.f - .5f), b = ((sg_hash(i + 1, 77) & 255) / 255.f - .5f);
	if (i <= 0) a = 0;
	if (i + 1 >= 15) b = 0;
	return W / 2.0f + (a + (b - a) * f) * W * 0.022f;
}

/* ================================================================ Windows world: sky, sun, hills */
static float hill(float u, int layer)
{
	switch (layer) {
	case 0: return 0.590f - 0.030f * sg_sin(u * 5.2f + 1.0f) - 0.014f * sg_sin(u * 12.3f + 0.4f);
	case 1: return 0.640f - 0.045f * sg_sin(u * 3.4f + 2.2f) - 0.012f * sg_sin(u * 9.1f);
	default: return 0.745f - 0.085f * sg_sin(u * 2.3f + 0.5f) - 0.015f * sg_sin(u * 6.7f + 1.3f);
	}
}

static void landscape(Surf *s, int x0, int x1)
{
	static const uint32_t crest[3] = {RGB(126, 178, 160), RGB(104, 184, 92), RGB(156, 224, 92)};
	static const uint32_t foot[3] = {RGB(100, 164, 132), RGB(60, 142, 64), RGB(30, 108, 36)};
	int hz = (int)(H * 0.66f);
	sg_clip(s, x0, 0, x1, H);
	sg_vgrad(s, x0, 0, x1, hz / 2, RGB(16, 78, 196), RGB(66, 140, 228));
	sg_vgrad(s, x0, hz / 2, x1, hz, RGB(66, 140, 228), RGB(188, 222, 248));
	sg_rect(s, x0, hz, x1 - x0, H - hz, foot[2], 1);
	float sx = x0 + (x1 - x0) * 0.80f, sy = H * 0.17f;              /* the sun */
	sg_glow(s, sx, sy, H * 0.55f, RGB(255, 244, 214), 0.42f);
	sg_glow(s, sx, sy, H * 0.16f, RGB(255, 250, 235), 0.75f);
	sg_disc(s, sx, sy, H * 0.035f, RGB(255, 253, 244), 1);
	for (int l = 0; l < 3; l++)
		for (int x = x0 < 0 ? 0 : x0; x < (x1 > W ? W : x1); x++) {
			float u = (float)(x - x0) / (x1 - x0), top = hill(u, l) * H;
			float sun = 0.10f * sg_sat(1 - sg_abs(u - 0.8f) * 1.6f);      /* brighter under the sun */
			for (int y = (int)top; y < H; y++) {
				float d = sg_sat((y - top) / (H * (l == 2 ? 0.30f : 0.10f)));
				uint32_t c = sg_mix(crest[l], foot[l], d);
				if (l == 2) {                                        /* grass: soft tonal bands */
					float n = sg_fbm(x * 3.2f / H, (y - top) * 14.f / H, 3);
					c = sg_mix(c, RGB(18, 84, 30), 0.34f * n);
				}
				c = sg_add(c, RGB(255, 250, 200), sun * (1 - d));
				if (y - top < 2.5f && l) c = sg_mix(RGB(214, 246, 160), c, (y - top) / 2.5f * 0.6f + 0.4f);
				float cov = sg_sat(y + 1 - top);
				uint32_t *p = &s->px[y * W + x];
				*p = cov >= 1 ? c : sg_mix(*p, c, cov);
			}
		}
	sg_noclip(s);
}

static void clouds(Surf *s, int x0, int x1, float t, float alpha)
{
	static const float lay[5][4] = {{0.10f, 0.13f, 1.0f, 9}, {0.46f, 0.24f, 0.72f, 6}, {0.78f, 0.10f, 0.85f, 12}, {0.30f, 0.37f, 0.5f, 4}, {0.92f, 0.33f, 0.6f, 7}};
	sg_clip(s, x0, 0, x1, H);
	float pw = x1 - x0, span = pw * 1.3f;
	for (int k = 0; k < 5; k++) {
		float r = H * 0.045f * lay[k][2];
		float cx = lay[k][0] * pw + t * lay[k][3] * U * 0.12f;
		cx = x0 - pw * 0.15f + (cx - sg_floor(cx / span) * span);
		float cy = H * lay[k][1];
		sg_blob(s, cx, cy + r * 0.75f, r * 3.6f, r * 0.8f, RGB(120, 160, 214), 0.30f);   /* soft underside */
		for (int b = 0; b < 6; b++) {
			float bx = cx + (b - 2.5f) * r * 0.95f, by = cy - ((b * 5 + k) % 3) * r * 0.28f;
			float br = r * (1.25f + 0.35f * ((b * 7 + k * 3) % 3) * 0.5f);
			sg_puff(s, bx, by, br * 1.2f, br * 0.86f, 2.6f, sg_mix(RGB(198, 206, 222), WHITE, alpha), 0.97f);
		}
	}
	sg_noclip(s);
}

/* four glass panes floating in the sky */
static void panes3d(Surf *s, float cx, float cy, float size, float t, float dim)
{
	static Sg3 g;
	g.cx = cx; g.cy = cy; g.focal = 30 * size; g.light = v3_norm(v3(0.5f, -0.6f, -0.62f)); g.ambient = 0.55f; g.n = 0;
	float yaw = 0.55f * sg_sin(t * 0.55f) - 0.25f, pitch = 0.16f + 0.06f * sg_sin(t * 0.8f);
	sg_glow(s, cx, cy, size * 3.4f, RGB(255, 255, 255), 0.22f * (1 - dim));
	for (int i = 0; i < 4; i++) {
		float ox = (i & 1) ? 0.07f : -1.07f, oy = (i & 2) ? 0.07f : -1.07f;
		V3 q[4] = {v3(ox, oy, 0), v3(ox + 1, oy, 0), v3(ox + 1, oy + 1, 0), v3(ox, oy + 1, 0)};
		for (int k = 0; k < 4; k++) {
			V3 p = v3_rotx(v3_roty(q[k], yaw), pitch);
			p.y += 0.07f * sg_sin(t * 0.9f); p.z += 30;
			q[k] = p;
		}
		sg3_face(&g, q, 4, RGB(176, 206, 244), WHITE, 0.96f, 1);
	}
	for (int i = 0; i < g.n; i++) g.f[i].col = sg_mix(g.f[i].col, RGB(150, 156, 170), dim * 0.5f);
	sg3_flush(&g, s, 0, 0, 1);
}

/* ================================================================ SilverOS world: white room, the emblem */
static void white_room(Surf *s, int x0, int x1)
{
	int hz = (int)(H * 0.62f);
	float vx = (x0 + x1) / 2.0f;
	sg_clip(s, x0, 0, x1, H);
	sg_vgrad(s, x0, 0, x1, hz, RGB(253, 253, 254), RGB(226, 227, 232));
	sg_vgrad(s, x0, hz, x1, H, RGB(206, 208, 215), RGB(242, 243, 246));
	sg_blob(s, vx, H * 0.36f, (x1 - x0) * 0.62f, H * 0.42f, WHITE, 0.85f);        /* a pool of light */
	for (int k = -14; k <= 14; k++) {                                           /* floor grid, fading into the distance */
		V2 a = {vx + k * (x1 - x0) * 0.022f, (float)hz}, b = {vx + k * (x1 - x0) * 0.26f, (float)H};
		for (int seg = 0; seg < 10; seg++) {
			float t0 = seg / 10.f, t1 = (seg + 1) / 10.f;
			sg_line(s, (V2){a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0}, (V2){a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1},
				1.2f, RGB(150, 154, 164), 0.10f + 0.45f * t1);
		}
	}
	for (int k = 1; k <= 9; k++) {
		float f = k / 9.f, y = hz + (H - hz) * f * f;
		sg_line(s, (V2){(float)x0, y}, (V2){(float)x1, y}, 1.2f, RGB(150, 154, 164), 0.10f + 0.45f * f);
	}
	sg_noclip(s);
}

/* The emblem over the white room. Its layers turn slowly on their own, like the emblem's own
   animation; while time crawls (nothing pressed) they barely move, and a key press (burst)
   spins them on and lets them drift apart a little. dim: the Windows side is selected. */
static void chooser_emblem(Surf *s, float cx, float cy, float R, float t, float real_t, float burst, float dim)
{
	Emblem e = emblem_rest();
	e.light_bg = 1; e.t = real_t;
	e.rot[0] = t * 0.55f; e.rot[1] = -t * 0.80f; e.rot[2] = t * 1.15f; e.rot[3] = -t * 0.40f;
	for (int l = 0; l < 3; l++) e.scale[l] = 1 + burst * (0.05f + 0.06f * (2 - l));
	for (int l = 0; l < 4; l++) e.alpha[l] = 1 - 0.40f * dim;
	e.gem = 0.9f + 0.3f * sg_heart(real_t, 0.9f) + burst * 0.8f;
	float sw = sg_fract(real_t / 5.5f) * 5.5f;                 /* a sheen every few seconds */
	if (sw < 0.8f) e.shine = sg_lerp(-1.4f, 1.4f, sw / 0.8f);
	emblem_draw(s, cx, cy + R * 0.5f, R, &e);                  /* centred on its height */
}

/* ================================================================ shards crossing the seam */
#define NCROSS 30
static struct { float y, ph, speed, size, spin, reach; int col; } cross_[NCROSS];

static void crossing(Surf *s, float t)
{
	static const uint32_t win_col[4] = {RGB(36, 116, 232), RGB(126, 190, 246), RGB(98, 196, 74), WHITE};
	static const uint32_t sil_col[4] = {RGB(8, 8, 10), WHITE, RGB(60, 60, 68), RGB(236, 236, 240)};
	for (int i = 0; i < NCROSS; i++) {
		int to_left = i & 1;                                  /* odd: pieces of Windows drifting into the white world */
		float u = sg_fract(cross_[i].ph + t * cross_[i].speed);
		float y = cross_[i].y * H + sg_sin(t * 0.6f + i) * H * 0.02f + u * H * 0.05f * (i % 3 - 1);
		float x = seam_x(y) + (to_left ? -1 : 1) * u * cross_[i].reach * W;
		float a = sg_sat(u * 9) * sg_sat((1 - u) * 2.2f);
		float ang = cross_[i].ph * 20 + t * cross_[i].spin, sz = cross_[i].size * U * (0.6f + 0.6f * (1 - u));
		V3 q[3] = {v3(-1, -0.6f, 0), v3(1.2f, -0.15f, 0), v3(-0.3f, 1.0f, 0)};
		V2 p[3];
		float nz = 0;
		for (int j = 0; j < 3; j++) {
			V3 r = v3_rotx(v3_roty(v3_rotz(q[j], ang), ang * 0.8f), ang * 0.5f);
			p[j] = (V2){x + r.x * sz, y + r.y * sz};
			if (j == 0) nz = sg_abs(sg_cos(ang * 0.8f) * sg_cos(ang * 0.5f));     /* how much it faces us */
		}
		uint32_t c = to_left ? win_col[cross_[i].col] : sil_col[cross_[i].col];
		c = sg_mix(sg_mix(c, BLACK, 0.45f), c, 0.35f + 0.65f * nz);
		if (!to_left && (cross_[i].col & 1)) sg_poly(s, p, 3, BLACK, a * 0.5f, 1.2f);   /* white needs an edge on bright sky */
		sg_poly(s, p, 3, c, a, 0);
	}
}

/* ================================================================ small UI pieces */
static void arrow(Surf *s, float cx, float cy, float r, int dir, uint32_t c, float a)
{
	float w = sg_max(1.5f, r * 0.22f);
	sg_line(s, (V2){cx - r * 0.6f, cy}, (V2){cx + r * 0.6f, cy}, w, c, a);
	sg_line(s, (V2){cx + dir * r * 0.6f, cy}, (V2){cx + dir * r * 0.1f, cy - r * 0.45f}, w, c, a);
	sg_line(s, (V2){cx + dir * r * 0.6f, cy}, (V2){cx + dir * r * 0.1f, cy + r * 0.45f}, w, c, a);
}
static float keycap(Surf *s, float x, float cy, const char *label, int arrow_dir, uint32_t c, float a)
{
	float h = U * 3.0f, sz = U * 1.12f;
	float w = label ? sg_text_w(&FC, label, sz, 0.14f) + U * 2.2f : h;
	sg_rrect(s, x, cy - h / 2, w, h, U * 0.45f, sg_max(1.2f, U * 0.16f), c, a);
	if (label) sg_text(s, &FC, x + U * 1.1f, cy + sz * 0.36f, label, sz, c, a, 0.14f);
	else arrow(s, x + w / 2, cy, h * 0.42f, arrow_dir, c, a);
	return x + w;
}
static void hints(Surf *s, float x, uint32_t c, float a)
{
	float sz = U * 1.12f, cy = H - U * 4.6f;
	x = keycap(s, x, cy, 0, -1, c, a) + U * 0.6f;
	x = keycap(s, x, cy, 0, 1, c, a) + U * 1.3f;
	x = sg_text(s, &FC, x, cy + sz * 0.36f, "CHOOSE", sz, c, a, 0.16f) + U * 3.2f;
	x = keycap(s, x, cy, "ENTER", 0, c, a) + U * 1.3f;
	sg_text(s, &FC, x, cy + sz * 0.36f, "START", sz, c, a, 0.16f);
}

/* ================================================================ chooser */
typedef struct {
	int sel, windows_found, counting, show_mouse, mx, my;
	float selblend, scene_t, real_t, burst, idle, timescale, countdown, flash;
	int last_sel;
	const char *err;
} Chooser;

static void bake_backgrounds(const Chooser *c)
{
	Surf a = sg_surf(bg_a, W, H), t = sg_surf(snap, W, H);
	int mid = W / 2, m = (int)(W * 0.03f);
	white_room(&a, 0, mid + m);
	landscape(&t, mid - m, W);                            /* composed for the right half */
	/* titles: thick, messy, with tiny wide captions underneath */
	float ts = U * 11.5f, by = H * 0.735f, ss = U * 1.08f;
	sg_text_c(&a, &FD, W * 0.25f, by, "SilverOS", ts, INK, 1, 0.0f);
	sg_text_c(&a, &FC, W * 0.25f, by + U * 4.8f, "BUILT FROM NOTHING", ss, RGB(96, 98, 108), 1, 0.30f);
	const char *sub = c->windows_found ? "WINDOWS BOOT MANAGER" : "NOT FOUND ON THIS PC";
	float tw = sg_text_w(&FD, "Windows", ts, 0.0f);
	sg_text_ex(&t, &FD, W * 0.75f - tw / 2 + U * 0.3f, by + U * 0.5f, "Windows", ts, RGB(4, 36, 12), 0.5f, 0.0f, 0.9f, U * 1.0f);   /* soft shadow */
	sg_text_c(&t, &FD, W * 0.75f, by, "Windows", ts, WHITE, 1, 0.0f);
	sg_text_c(&t, &FC, W * 0.75f, by + U * 4.8f, sub, ss, RGB(236, 248, 232), 0.95f, 0.30f);
	/* join the worlds along the seam: the white room lies on top and casts a shadow */
	for (int y = 0; y < H; y++) {
		float sx = seam_x(y + .5f);
		for (int x = mid - m; x < mid + m + 30 && x < W; x++) {
			float d = x + .5f - sx;
			uint32_t r = snap[y * W + x];
			if (d > 0 && d < U * 3.2f) { float q = 1 - d / (U * 3.2f); r = sg_mix(r, RGB(0, 10, 30), 0.55f * q * q); }
			bg_a[y * W + x] = sg_mix(r, d > -1.6f ? sg_mix(bg_a[y * W + x], RGB(40, 42, 50), sg_sat(d + 1.6f) * 0.9f) : bg_a[y * W + x], sg_sat(0.5f - d));
		}
		for (int x = mid + m + 30; x < W; x++) bg_a[y * W + x] = snap[y * W + x];
	}
	/* two versions: the half that is not selected steps back */
	for (int y = 0; y < H; y++) {
		float sx = seam_x(y + .5f);
		for (int x = 0; x < W; x++) {
			uint32_t p = bg_a[y * W + x];
			float right = sg_sat(x + .5f - sx);
			uint32_t dim_r = sg_mix(sg_mix(p, sg_gray(p), 0.42f), RGB(14, 22, 44), 0.26f);
			uint32_t dim_l = sg_mix(p, RGB(150, 153, 164), 0.40f);
			bg_b[y * W + x] = sg_mix(dim_l, p, right);
			bg_a[y * W + x] = sg_mix(p, dim_r, right);
		}
	}
}

static void chooser_init(Chooser *c)
{
	for (int i = 0; i < NCROSS; i++) {
		cross_[i].y = (i + sg_frand()) / NCROSS; cross_[i].ph = sg_frand();
		cross_[i].speed = 0.05f + sg_frand() * 0.10f; cross_[i].size = 0.7f + sg_frand() * sg_frand() * 3.0f;
		cross_[i].spin = (sg_frand() - .5f) * 5; cross_[i].reach = 0.05f + sg_frand() * 0.2f; cross_[i].col = (int)(sg_rand() & 3);
	}
	c->sel = 0; c->last_sel = 0; c->flash = 0; c->selblend = 0; c->scene_t = 0; c->real_t = 0; c->burst = 0; c->idle = 99;
	c->timescale = 0.08f; c->countdown = TIMEOUT_S; c->counting = 1; c->show_mouse = 0; c->err = 0;
	c->mx = W / 2; c->my = H / 2;
	bake_backgrounds(c);
}

static void chooser_poke(Chooser *c) { c->idle = 0; c->counting = 0; c->burst = sg_min(c->burst + 0.22f, 0.85f); }

static void chooser_update(Chooser *c, float dt)
{
	c->real_t += dt; c->idle += dt;
	float target = c->idle < 0.4f ? 1.0f : 0.07f;           /* time crawls until you move */
	c->timescale += (target - c->timescale) * sg_min(1, dt * 6);
	c->scene_t += dt * c->timescale;
	c->burst *= 1 - sg_min(1, dt * 2.2f);
	c->selblend += ((float)c->sel - c->selblend) * sg_min(1, dt * 12);
	if (c->counting) c->countdown -= dt;
	if (c->sel != c->last_sel) { c->last_sel = c->sel; c->flash = calm ? 0.35f : 1; }       /* a cut, like a strobe */
	c->flash *= 1 - sg_min(1, dt * 7);
}

static void pill(Surf *s, float cx, float cy, const char *label, uint32_t fill, uint32_t ink, float a, float pulse)
{
	float sz = U * 1.25f, w = sg_text_w(&FC, label, sz, 0.2f) + U * 5.0f, h = U * 4.2f;
	sg_glow(s, cx, cy, w * 0.9f, fill, 0.20f * a * pulse);
	sg_rrect(s, cx - w / 2, cy - h / 2, w, h, h / 2, 0, fill, a);
	sg_text_c(s, &FC, cx, cy + sz * 0.36f, label, sz, ink, a, 0.2f);
}

static void chooser_draw(Surf *s, const Chooser *c)
{
	float b = c->selblend;
	int n = W * H, mid = W / 2;
	if (b < 0.004f) sg_copy(s->px, bg_a, (size_t)n * 4);
	else if (b > 0.996f) sg_copy(s->px, bg_b, (size_t)n * 4);
	else for (int i = 0; i < n; i++) s->px[i] = sg_mix(bg_a[i], bg_b[i], b);
	float t = c->scene_t, pulse = 0.5f + 0.5f * sg_sin(c->real_t * 4.5f);

	/* SilverOS side */
	sg_clip(s, 0, 0, mid + (int)(W * 0.02f), H);
	float R = sg_min(W * 0.5f, H * 0.8f) * 0.165f, pcy = H * 0.37f;
	sg_blob(s, W * 0.25f, H * 0.655f, R * 1.6f, U * 1.9f, BLACK, 0.20f * (1 - 0.5f * b));   /* its shadow on the floor */
	chooser_emblem(s, W * 0.25f, pcy, R, t, c->real_t, c->burst, b);
	sg_noclip(s);
	/* Windows side */
	clouds(s, mid + (int)(W * 0.02f), W, t * 6 + c->real_t * 0.4f, b);
	sg_clip(s, mid, 0, W, H);
	panes3d(s, W * 0.75f, H * 0.34f, sg_min(W * 0.5f, H * 0.8f) * 0.095f, t * 2 + c->real_t * 0.15f, 1 - b);
	sg_noclip(s);

	crossing(s, t);
	float hb = sg_heart(c->real_t, 0.9f);                   /* the seam beats like a heart */
	for (int y = 0; y < H; y += H / 48) {
		V2 p0 = {seam_x((float)y), (float)y}, p1 = {seam_x((float)(y + H / 48)), (float)(y + H / 48)};
		sg_line(s, p0, p1, U * (1.2f + 1.6f * hb), BLOOD, 0.12f + 0.20f * hb);
		sg_line(s, p0, p1, U * 0.30f, BLOOD, 0.95f);
	}
	/* which side is live */
	float py = H * 0.735f + U * 9.6f;
	if (b < 0.5f) pill(s, W * 0.25f, py, "PRESS ENTER", BLOOD, WHITE, 1 - 2 * b, pulse);
	else pill(s, W * 0.75f, py, "PRESS ENTER", WHITE, BLOOD, 2 * b - 1, pulse);

	hints(s, U * 4, RGB(84, 86, 96), 0.85f * (1 - 0.4f * b));
	if (c->counting && c->countdown > 0) {                  /* auto-start countdown, under the button */
		char msg[] = "STARTS ON ITS OWN IN 0";
		msg[21] = (char)('0' + (int)(c->countdown + 0.999f));
		sg_text_c(s, &FC, W * 0.25f, py + U * 6.2f, msg, U * 0.95f, RGB(112, 114, 124), 1, 0.2f);
		float bw = U * 22, k = c->countdown / TIMEOUT_S;
		sg_rect(s, W * 0.25f - bw / 2, py + U * 7.6f, bw, sg_max(2, U * 0.28f), RGB(200, 202, 208), 1);
		sg_rect(s, W * 0.25f - bw / 2, py + U * 7.6f, bw * k, sg_max(2, U * 0.28f), BLOOD, 1);
	}
	sg_text(s, &FC, W - sg_text_w(&FC, calm ? "CALM MODE ON  /  C TO CHANGE" : "CONTAINS FLASHING LIGHTS  /  C FOR CALM MODE", U * 0.8f, 0.2f) - U * 4, H - U * 2.2f,
		calm ? "CALM MODE ON  /  C TO CHANGE" : "CONTAINS FLASHING LIGHTS  /  C FOR CALM MODE", U * 0.8f, WHITE, 0.62f, 0.2f);
	if (c->flash > 0.02f) {                                 /* selection strobe: red wash, torn bands, split colour */
		sg_rect(s, 0, 0, W, H, BLOOD, 0.34f * c->flash);
		for (int g = 0; g < 5; g++) {
			int y0 = (int)(sg_frand() * H), hh = 2 + (int)(sg_frand() * H * 0.04f);
			float sh = (sg_frand() - .5f) * W * 0.05f * c->flash;
			for (int y = y0; y < y0 + hh && y < H; y++) {
				uint32_t *row = s->px + y * W;
				for (int x = (sh > 0 ? W - 1 : 0); sh > 0 ? x >= 0 : x < W; x += (sh > 0 ? -1 : 1)) {
					int sx = x - (int)sh; sx = sx < 0 ? 0 : sx >= W ? W - 1 : sx;
					row[x] = (row[sx] & 0x00ffff) | (row[sx > 3 ? sx - 4 : sx] & 0xff0000);
				}
			}
		}
	}
	if (c->err) {
		float w = sg_text_w(&FT, c->err, U * 2.2f, 0.02f) + U * 5;
		sg_rrect(s, W / 2.0f - w / 2, U * 4, w, U * 5, U * 2.5f, 0, RGB(20, 20, 24), 0.92f);
		sg_text_c(s, &FT, W / 2.0f, U * 7.3f, c->err, U * 2.2f, WHITE, 1, 0.02f);
	}
	if (c->show_mouse) {
		float x = c->mx, y = c->my, k = U * 0.16f;
		V2 p[3] = {{x, y}, {x, y + 17 * k}, {x + 12.5f * k, y + 11.5f * k}};
		sg_poly(s, p, 3, BLACK, 1, 1.6f);
		sg_poly(s, p, 3, WHITE, 1, 0);
	}
	if (c->real_t < 0.6f) sg_rect(s, 0, 0, W, H, BLACK, 1 - sg_smooth(c->real_t / 0.6f));   /* fade up from black */
}

/* ================================================================ transition: Windows boots, flips, melts */

static uint32_t *melt_px;          /* the melting image (feedback buffer) */
static int melt_steps;
static void trans_begin(Surf *last_frame)
{
	sg_copy(snap, last_frame->px, (size_t)W * H * 4);
	melt_steps = -1;
}

/* The fake Windows boot, as a modern PC shows it: black, the four-pane logo in blue, and the
 * ring of five dots chasing round underneath. */
static void win_logo(Surf *s, float cx, float cy, float size, float al)
{
	float g = size * 0.045f, w = (size - g) / 2;
	for (int q = 0; q < 4; q++) {
		int x0 = (int)(cx - size / 2 + (q & 1) * (w + g)), y0 = (int)(cy - size / 2 + (q >> 1) * (w + g)), n = (int)w;
		for (int y = 0; y < n; y++) {
			if (y0 + y < 0 || y0 + y >= s->h) continue;
			uint32_t *row = s->px + (y0 + y) * s->w;
			for (int x = 0; x < n; x++) {
				if (x0 + x < 0 || x0 + x >= s->w) continue;
				/* light from the top left across the whole logo */
				float u = ((q & 1) * (w + g) + x + (q >> 1) * (w + g) + y) / (2 * size);
				uint32_t c = sg_mix(RGB(84, 196, 255), RGB(0, 94, 196), sg_sat(u * 1.15f));
				row[x0 + x] = al >= 1 ? c : sg_mix(row[x0 + x], c, al);
			}
		}
	}
}
static void win_spinner(Surf *s, float cx, float cy, float r, float t)
{
	const float lap = 2.15f;
	for (int i = 0; i < 5; i++) {
		float cyc = lap + 0.55f, u = t - sg_floor(t / cyc) * cyc - i * 0.105f;
		if (u < 0 || u > lap) continue;
		float x = u / lap, f = 2 * x - 0.115f * sg_sin(4 * SG_PI * x) * 1.0f;     /* two laps: slow over the top, fast underneath */
		float an = -SG_PI / 2 + f * 2 * SG_PI;
		float a = sg_sat(u / 0.08f) * sg_sat((lap - u) / 0.12f);
		sg_disc(s, cx + sg_cos(an) * r, cy + sg_sin(an) * r, r * 0.17f, WHITE, a);
	}
}
static void classic(Surf *s, float t)
{
	sg_clear(s, BLACK);
	float size = H * 0.115f, cx = W / 2.0f, cy = H * 0.42f;
	if (t > 0.30f) win_logo(s, cx, cy, size, 1);
	if (t > 0.95f) win_spinner(s, cx, H * 0.735f, H * 0.026f, t - 0.95f);
}

static void melt_step(int step)
{
	static const uint32_t pal[6] = {RGB(0, 0, 0), RGB(255, 255, 255), RGB(18, 18, 18), RGB(226, 226, 226), RGB(0, 0, 0), RGB(255, 255, 255)};
	static int16_t v[8192];
	int w = W < 8192 ? W : 8192;
	for (int x = 0; x < w; x++) {                           /* each column drips at its own, changing speed */
		float n = sg_noise(x * 22.f / W, step * 0.06f), n2 = sg_noise(x * 170.f / W + 50, step * 0.13f);
		v[x] = (int16_t)((0.6f + 13 * n * n * n + (n2 > 0.74f ? 9 : 0)) * H / 800.f + 0.5f);
	}
	for (int y = H - 1; y >= 0; y--) {
		uint32_t *row = melt_px + y * W;
		for (int x = 0; x < w; x++) { int sy = y - v[x]; row[x] = melt_px[(sy < 0 ? 0 : sy) * W + x]; }
	}
	for (int k = 0; k < 5; k++) {                           /* black and white seeps in at the top */
		int x0 = (int)(sg_frand() * W), len = (int)((0.01f + sg_frand() * sg_frand() * 0.14f) * W);
		uint32_t c = (sg_rand() & 3) ? melt_px[((int)(sg_frand() * H)) * W + (int)(sg_frand() * W)] : pal[sg_rand() % 6];
		for (int x = x0; x < x0 + len && x < W; x++) melt_px[x] = melt_px[W + x] = c;
	}
	for (int k = 0; k < 3; k++) {                           /* blocks slip sideways (datamosh) */
		int bw = (int)((0.04f + sg_frand() * 0.22f) * W), bh = (int)((0.01f + sg_frand() * 0.12f) * H);
		int sx = (int)(sg_frand() * (W - bw)), sy = (int)(sg_frand() * (H - bh)), dx = (int)((sg_frand() - .5f) * 0.12f * W);
		int tx = sx + dx; if (tx < 0) tx = 0; if (tx + bw > W) tx = W - bw;
		int inv = (sg_rand() & 7) == 0;                       /* now and then a block flips to its negative */
		for (int y = sy; y < sy + bh; y++) {
			uint32_t *row = melt_px + y * W;
			if (tx < sx) for (int x = 0; x < bw; x++) row[tx + x] = inv ? row[sx + x] ^ 0xffffff : row[sx + x];
			else for (int x = bw - 1; x >= 0; x--) row[tx + x] = inv ? row[sx + x] ^ 0xffffff : row[sx + x];
		}
	}
}

static const struct { float dur; int inv, logo; } FLICK[] = {
	{0.18f, 0, 0}, {0.50f, 0, 1}, {0.26f, 1, 1}, {0.36f, 0, 1}, {0.16f, 1, 1}, {0.09f, 2, 1}, {0.14f, 1, 1}, {0.55f, 0, 1}};
#define T_CLASSIC 3.6f
#define T_FLIP 0.62f
#define T_MELT 3.9f

/* returns 1 when the transition has finished (the screen is black) */
static int trans_frame(Surf *s, float t)
{
	if (t < T_CLASSIC) {                                    /* 1. the classic boot scene */
		classic(s, t);
		if (t < 0.22f) for (int i = 0; i < W * H; i++) s->px[i] = sg_mix(snap[i], s->px[i], sg_sat(t / 0.12f));
		return 0;
	}
	t -= T_CLASSIC;
	if (t < T_FLIP) {                                       /* 2. upside-down, drained of colour, tearing */
		classic(s, T_CLASSIC);
		sg_copy(snap, s->px, (size_t)W * H * 4);
		for (int y = 0; y < H; y++) {
			int band = (int)(y * 28.f / H);
			int off = (sg_hash(band, (int)(t * 24)) % 100 < 22) ? (int)(((sg_hash(band, 9 + (int)(t * 24)) & 255) / 255.f - .5f) * W * 0.07f) : 0;
			const uint32_t *src = snap + (H - 1 - y) * W;
			uint32_t *row = s->px + y * W;
			for (int x = 0; x < W; x++) {
				int sx = x + off; sx = sx < 0 ? 0 : sx >= W ? W - 1 : sx;
				row[x] = sg_mix(sg_gray(src[sx]), BLACK, (float)y / H * 0.3f);
			}
		}
		return 0;
	}
	t -= T_FLIP;
	if (t < T_MELT) {                                       /* 3. it melts */
		if (melt_steps < 0) {                                 /* the flipped, grey image is what melts */
			classic(s, T_CLASSIC);
			melt_px = scene;
			for (int y = 0; y < H; y++)
				for (int x = 0; x < W; x++) melt_px[y * W + x] = sg_gray(s->px[(H - 1 - y) * W + x]);
			melt_steps = 0;
		}
		int want = (int)(t * 30);                             /* the melt advances 30 steps per second */
		for (; melt_steps < want; melt_steps++) melt_step(melt_steps);
		sg_copy(s->px, melt_px, (size_t)W * H * 4);
		for (int g = 0; g < 7; g++) {                         /* torn bands, pushed sideways */
			int y0 = (int)(sg_frand() * H), hh = 2 + (int)(sg_frand() * H * 0.03f), sh = (int)((sg_frand() - .5f) * W * 0.10f);
			for (int y = y0; y < y0 + hh && y < H; y++) {
				uint32_t *row = s->px + y * W; const uint32_t *src = melt_px + y * W;
				for (int x = 0; x < W; x++) { int a = x - sh; row[x] = a >= 0 && a < W ? src[a] : 0; }
			}
		}
		float u = t / T_MELT;
		if (u > 0.62f) sg_rect(s, 0, 0, W, H, BLACK, (u - 0.62f) / 0.38f * 0.80f);   /* sinks into the dark */
		return 0;
	}
	t -= T_MELT;
	float size = H * 0.34f, zoom = 1 + t * 0.03f;
	for (unsigned i = 0; i < sizeof FLICK / sizeof FLICK[0]; i++) {       /* 4. white flash, the logo flickers */
		if (t < FLICK[i].dur) {
			int inv = calm ? 0 : FLICK[i].inv;
			uint32_t bgc = inv == 2 ? BLOOD : inv ? RGB(6, 6, 10) : RGB(250, 250, 246), fg = inv == 2 ? BLACK : inv ? RGB(248, 248, 248) : RGB(66, 68, 78);
			sg_clear(s, bgc);
			if (FLICK[i].logo) {
				float jx = (sg_frand() < 0.12f) ? (sg_frand() - .5f) * U : 0;
				if (inv == 1) sg_glow(s, W / 2.0f, H / 2.0f, size * 1.25f, WHITE, 0.22f);
				else if (inv == 2) sg_glow(s, W / 2.0f, H / 2.0f, size * 1.3f, BLACK, 0.0f);
				else if (!inv) sg_blob(s, W / 2.0f, H / 2.0f + size * 0.06f, size * 0.9f, size * 0.9f, RGB(120, 124, 140), 0.22f);
				(void)fg;
				Emblem e = emblem_rest(); float R = size * zoom * 0.42f;
				e.light_bg = inv == 1 ? 0 : 1; e.gem = inv == 2 ? 0.4f : 1.2f; e.t = t;
				emblem_draw(s, W / 2.0f + jx, H / 2.0f + R * 0.5f, R, &e);
			}
			return 0;
		}
		t -= FLICK[i].dur;
	}
	if (t < 0.45f) {                                        /* 5. and out */
		sg_clear(s, RGB(250, 250, 246));
		sg_blob(s, W / 2.0f, H / 2.0f + size * 0.06f, size * 0.9f, size * 0.9f, RGB(120, 124, 140), 0.22f);
		{ Emblem e = emblem_rest(); float R = size * zoom * 0.42f; e.light_bg = 1; e.t = t; emblem_draw(s, W / 2.0f, H / 2.0f + R * 0.5f, R, &e); }
		sg_rect(s, 0, 0, W, H, BLACK, sg_smooth(t / 0.45f));
		return 0;
	}
	sg_clear(s, BLACK);
	return 1;
}

/* ================================================================ hardware check: a boot console */
/* Black and white, like a Linux machine starting: a log scrolls past in a code font, the real
 * hardware values marked [  OK  ] (or an inverted [FAILED]), the emblem turning in the corner,
 * a thin progress bar and "Starting SilverOS" at the end. The lines and their times are one
 * table (CHECK_LINES) so the sounds (bootsfx.h) land on them. */
typedef struct { char cpu[64], gpu[48], ram[32], disk[48]; int ok[4]; } SysInfo;
#define T_CHECKS 4.6f

/* kind: 0 a log line with a timestamp, 1 a check with a status ([  OK  ] or [FAILED]); val: which
   SysInfo value follows the text (-1 none) */
typedef struct { int kind, val; const char *text; } CheckLine;
static const CheckLine CHECK_LINES[] = {
	{0, -1, "silverboot: starting SilverOS"},
	{0, -1, "efi: reading the firmware memory map"},
	{0, -1, "efi: graphics output protocol found"},
	{0, -1, "acpi: reading the firmware tables"},
	{0, -1, "cpu: probing processor"},
	{1,  0, "Processor: "},
	{0, -1, "cpu: x86-64, long mode, SSE2, AVX"},
	{0, -1, "smp: counting cores"},
	{0, -1, "rtc: reading the hardware clock"},
	{0, -1, "fb: setting up the framebuffer"},
	{1,  1, "Display: "},
	{0, -1, "fb: 32-bit colour, double buffered"},
	{0, -1, "mm: building the page allocator"},
	{0, -1, "mm: zones DMA, DMA32, Normal"},
	{1,  2, "Memory: "},
	{0, -1, "usb: host controllers found"},
	{0, -1, "blk: scanning disks"},
	{0, -1, "blk: GPT partition table"},
	{0, -1, "blk: EFI system partition found"},
	{1,  3, "Storage: "},
	{0, -1, "boot: kernel image located"},
	{0, -1, "boot: initial ramdisk located"},
	{0, -1, "random: entropy pool seeded"},
	{0, -1, "audio: output ready"},
	{0, -1, "input: keyboard and mouse ready"},
	{0, -1, "net: network interfaces found"},
	{0, -1, "boot: kernel command line set"},
	{1, -1, "Boot chooser finished"},
	{0, -1, "boot: handing over to the kernel"},
	{1, -1, "Starting SilverOS"},
};
#define N_CHECK_LINES ((int)(sizeof CHECK_LINES / sizeof CHECK_LINES[0]))
static float check_t(int i) { return 0.20f + i * 0.104f; }   /* when line i appears */

static void ck_cat(char *d, const char *s2, int cap) { int n = 0; while (d[n]) n++; while (*s2 && n < cap - 1) d[n++] = *s2++; d[n] = 0; }
/* a kernel-style timestamp: "[    0.412817]" */
static void ck_stamp(char *out, float t)
{
	int us = (int)(t * 1000000.0f * 0.37f) + 1311, sec = us / 1000000, frac = us % 1000000;
	char b[16]; int k = 0;
	b[k++] = '[';
	char sb[8]; int sn = 0; do sb[sn++] = (char)('0' + sec % 10); while ((sec /= 10) && sn < 6);
	for (int i = sn; i < 5; i++) b[k++] = ' ';
	while (sn) b[k++] = sb[--sn];
	b[k++] = '.';
	for (int d = 100000; d >= 1; d /= 10) b[k++] = (char)('0' + (frac / d) % 10);
	b[k++] = ']'; b[k] = 0;
	out[0] = 0; ck_cat(out, b, 16);
}

static int checks_frame(Surf *s, float t, const SysInfo *si)
{
	const char *val[4] = {si->cpu, si->gpu, si->ram, si->disk};
	float fade = sg_smooth(t / 0.30f) * (1 - sg_smooth((t - (T_CHECKS - 0.4f)) / 0.4f));
	sg_clear(s, BLACK);
	const uint32_t dim = RGB(112, 112, 116), mid = RGB(176, 176, 180), hi = RGB(236, 236, 238);
	float x0 = W * 0.07f;

	/* the header: the emblem turning, the name and a thin rule */
	{
		Emblem e = emblem_rest();
		e.t = t; e.rot[0] = t * 0.9f; e.rot[1] = -t * 1.3f; e.rot[2] = t * 1.8f; e.rot[3] = -t * 0.6f;
		e.gem = 0.9f + 0.3f * sg_heart(t, 0.9f);
		for (int l = 0; l < 4; l++) e.alpha[l] = fade;
		float R = U * 2.9f, ey = U * 7.0f;
		emblem_draw(s, x0 + R, ey + R * 0.5f, R, &e);
		float tx = x0 + R * 2 + U * 3.2f;
		sg_text(s, &FM, tx, ey - U * 0.2f, "SilverOS 0.1.0", U * 2.6f, hi, fade, 0);
		sg_text(s, &FM, tx, ey + U * 3.0f, "x86_64  /  hardware check", U * 1.5f, dim, fade, 0);
		sg_rect(s, x0, U * 14.0f, W - 2 * x0, sg_max(1, U * 0.12f), RGB(60, 60, 64), fade);
	}

	/* the log: lines appear on time, the newest typing in, the block scrolling up */
	float sz = U * 1.62f, lh = U * 2.55f, ytop = U * 17.5f, ybot = H - U * 11.5f;
	int vis = (int)((ybot - ytop) / lh), shown = 0;
	for (int i = 0; i < N_CHECK_LINES; i++) if (t >= check_t(i)) shown = i + 1;
	float done = 0;
	for (int i = (shown > vis ? shown - vis : 0); i < shown; i++) {
		const CheckLine *ln = &CHECK_LINES[i];
		float age = t - check_t(i), y = ytop + (vis < shown ? i - (shown - vis) : i) * lh + sz;
		char text[160]; text[0] = 0;
		ck_cat(text, ln->text, 160);
		if (ln->val >= 0) ck_cat(text, val[ln->val], 160);
		int n = 0; while (text[n]) n++;
		int reveal = (int)(n * sg_sat(age / 0.07f));                       /* typed in */
		char part[160]; for (int k = 0; k < reveal; k++) part[k] = text[k]; part[reveal] = 0;
		float x = x0;
		if (ln->kind == 0) {
			char st[24]; ck_stamp(st, check_t(i));
			x = sg_text(s, &FM, x, y, st, sz, dim, fade, 0);
			x = sg_text(s, &FM, x + sz * 0.6f, y, part, sz, mid, fade, 0);
		} else {
			int ok = ln->val < 0 ? 1 : si->ok[ln->val];
			float bw = sg_text_w(&FM, "[FAILED]", sz, 0);
			if (ok) {
				float xb = sg_text(s, &FM, x, y, "[", sz, hi, fade, 0);
				xb = sg_text(s, &FM, xb, y, "  OK  ", sz, WHITE, fade * sg_sat(age / 0.05f), 0);
				sg_text(s, &FM, xb, y, "]", sz, hi, fade, 0);
			} else {                                                         /* inverted: white box, black text */
				sg_rect(s, x - sz * 0.15f, y - sz * 0.82f, bw + sz * 0.3f, sz * 1.08f, WHITE, fade);
				sg_text(s, &FM, x, y, "[FAILED]", sz, BLACK, fade, 0);
			}
			x = sg_text(s, &FM, x + bw + sz * 0.9f, y, part, sz, ok ? hi : WHITE, fade, 0);
		}
		if (i == shown - 1 && ((int)(t * 3.2f) & 1) == 0)                  /* the cursor, blinking */
			sg_rect(s, x + sz * 0.25f, y - sz * 0.78f, sz * 0.55f, sz * 0.98f, hi, fade * 0.9f);
	}
	if (shown) done = sg_sat((t - check_t(0)) / (check_t(N_CHECK_LINES - 1) - check_t(0) + 0.2f));

	/* the progress bar and the percentage */
	{
		float by = H - U * 6.6f, bw = W - 2 * x0, bh = sg_max(2, U * 0.22f);
		sg_rect(s, x0, by, bw, bh, RGB(46, 46, 50), fade);
		sg_rect(s, x0, by, bw * done, bh, hi, fade);
		char pc[8]; int v = (int)(done * 100 + 0.5f), k = 0;
		if (v >= 100) pc[k++] = '1';
		if (v >= 10) pc[k++] = (char)('0' + (v / 10) % 10);
		pc[k++] = (char)('0' + v % 10); pc[k++] = '%'; pc[k] = 0;
		float pw = sg_text_w(&FM, pc, U * 1.4f, 0);
		sg_text(s, &FM, x0 + bw - pw, by - U * 1.3f, pc, U * 1.4f, mid, fade, 0);
		sg_text(s, &FM, x0, by - U * 1.3f, done >= 1 ? "starting silveros..." : "checking hardware...", U * 1.4f, dim, fade, 0);
	}
	for (int y = 1; y < H; y += 3) {                          /* faint scanlines */
		uint32_t *row = s->px + y * W;
		for (int x = 0; x < W; x++) row[x] = (row[x] >> 1 & 0x7f7f7f) + (row[x] >> 2 & 0x3f3f3f);
	}
	return t >= T_CHECKS;
}

/* ================================================================ pick Windows: the screen shatters */
#define SH_X 16
#define SH_Y 10
#define T_SHATTER 0.95f
static struct { V2 p[3], c; float vx, vy, spin, ph; uint32_t col; } sh[SH_X * SH_Y * 2];

static void shatter_begin(Surf *last_frame)
{
	sg_copy(snap, last_frame->px, (size_t)W * H * 4);
	float cw = (float)W / SH_X, ch = (float)H / SH_Y;
	static V2 grid[(SH_X + 1) * (SH_Y + 1)];
	for (int j = 0; j <= SH_Y; j++)
		for (int i = 0; i <= SH_X; i++) {
			float jx = (i > 0 && i < SH_X) ? (sg_frand() - .5f) * cw * 0.7f : 0, jy = (j > 0 && j < SH_Y) ? (sg_frand() - .5f) * ch * 0.7f : 0;
			grid[j * (SH_X + 1) + i] = (V2){i * cw + jx, j * ch + jy};
		}
	int n = 0;
	for (int j = 0; j < SH_Y; j++)
		for (int i = 0; i < SH_X; i++) {
			V2 a = grid[j * (SH_X + 1) + i], b = grid[j * (SH_X + 1) + i + 1], c = grid[(j + 1) * (SH_X + 1) + i + 1], d = grid[(j + 1) * (SH_X + 1) + i];
			V2 t[2][3] = {{a, b, c}, {a, c, d}};
			for (int k = 0; k < 2; k++, n++) {
				for (int v = 0; v < 3; v++) sh[n].p[v] = t[k][v];
				sh[n].c = (V2){(t[k][0].x + t[k][1].x + t[k][2].x) / 3, (t[k][0].y + t[k][1].y + t[k][2].y) / 3};
				float dx = sh[n].c.x - W / 2.0f, dy = sh[n].c.y - H / 2.0f;
				sh[n].vx = dx * (1.2f + sg_frand() * 1.6f); sh[n].vy = dy * (1.2f + sg_frand() * 1.6f) - H * 0.25f;
				sh[n].spin = (sg_frand() - .5f) * 9; sh[n].ph = sg_frand() * 6.28f;
				int px = (int)sg_clamp(sh[n].c.x, 0, W - 1), py = (int)sg_clamp(sh[n].c.y, 0, H - 1);
				sh[n].col = snap[py * W + px];
			}
		}
}

static int shatter_frame(Surf *s, float t)
{
	float u = sg_sat(t / T_SHATTER), e = u * u;
	sg_clear(s, BLACK);
	if (u < 0.12f) {                                        /* the first cracks */
		sg_copy(s->px, snap, (size_t)W * H * 4);
		for (int k = 0; k < SH_X * SH_Y * 2; k++)
			for (int v = 0; v < 3; v++) sg_line(s, sh[k].p[v], sh[k].p[(v + 1) % 3], sg_max(1.2f, U * 0.18f), WHITE, u / 0.12f * 0.8f);
		return 0;
	}
	for (int k = 0; k < SH_X * SH_Y * 2; k++) {
		float ang = sh[k].spin * e, c = sg_cos(ang), sn = sg_sin(ang), sc = 1 - 0.55f * e;
		float tilt = sg_abs(sg_cos(sh[k].ph + ang * 1.7f));    /* shards catch the light as they tumble */
		float ox = sh[k].vx * e, oy = sh[k].vy * e + H * 1.3f * e * e;
		V2 q[3];
		for (int v = 0; v < 3; v++) {
			float dx = (sh[k].p[v].x - sh[k].c.x) * sc * (0.35f + 0.65f * tilt), dy = (sh[k].p[v].y - sh[k].c.y) * sc;
			q[v] = (V2){sh[k].c.x + ox + dx * c - dy * sn, sh[k].c.y + oy + dx * sn + dy * c};
		}
		sg_poly(s, q, 3, sg_add(sg_mix(BLACK, sh[k].col, 0.45f + 0.55f * tilt), WHITE, tilt > 0.96f ? 0.5f : 0), 1 - e * 0.85f, 0.3f);
	}
	return t >= T_SHATTER;
}

/* ================================================================ setup */
static int scenes_init(int w, int h)
{
	W = w; H = h; U = H / 100.0f;
	if (sg_font_load(&FD, font_scrawl) || sg_font_load(&FT, font_text) || sg_font_load(&FC, font_caption) || sg_font_load(&FM, font_mono)) return -1;
	bg_a = sb_alloc((size_t)W * H * 4); bg_b = sb_alloc((size_t)W * H * 4);
	snap = sb_alloc((size_t)W * H * 4); scene = sb_alloc((size_t)W * H * 4);
	return bg_a && bg_b && snap && scene ? 0 : -1;
}
