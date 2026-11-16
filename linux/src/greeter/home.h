/*
 * SilverOS home screen: the cracked-glass launcher and its four sections.
 *
 *      +-----------+-----------+
 *      | SETTINGS  |  SEARCH   |      The glass is split by two jagged fractures that cross
 *      +-----------+-----------+      in the middle, where the impact chipped it.
 *      |   APPS    |   FILES   |      Arrow keys choose a pane, Enter opens it, Esc comes back.
 *      +-----------+-----------+
 *
 * Included by silver-greeter.c (which provides the framebuffer, fonts, clock and the privileged
 * parent) and, with -DRENDER, by the offline demo renderer, which uses a built-in sample tree
 * instead of the real file system so videos never show anything about the build machine.
 */
#pragma once
#include <dirent.h>
#include <strings.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/stat.h>
#include <sys/utsname.h>

/* atan2 without libm (accurate to ~0.005 rad, plenty for crack branches) */
static float sg_atan2f_approx(float y, float x)
{
	float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
	float a = mn / (mx + 1e-9f), s = a * a;
	float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
	if (ay > ax) r = 1.57079633f - r;
	if (x < 0) r = 3.14159265f - r;
	return y < 0 ? -r : r;
}

#define K_UP 1001
#define K_DOWN 1002
#define K_RIGHT 1003
#define K_LEFT 1004
#define K_ESC 27
#define K_ENTER 13
#define K_BACK 127

/* ================================================================ the cracks */
/* A crack is a polyline that grows from its first point at `speed` px/s, starting `t0` seconds
 * after the impact. Its width tapers from w0 to w1 along its length. */
#define CKPTS 64
typedef struct { V2 p[CKPTS]; float cum[CKPTS]; int n; float t0, speed, w0, w1; } Crack;
#define MAXCRACK 170
static Crack ck[MAXCRACK];
static int nck, nmain;                  /* the first `nmain` cracks are the four arms of the cross */
static float vtab[33], htab[33];        /* the two main fractures: x offset per y sample, y offset per x sample */
#define NGLINT 28
static V2 glint[NGLINT];                /* points along the cracks that catch the light */
static int nglint;
#define NCHIP 14
static V2 chipp[NCHIP][3];              /* little flakes knocked out around the impact */

static float vx_at(float y) { float f = sg_clamp(y / H, 0, 1) * 32, i = sg_floor(f); int k = (int)i > 31 ? 31 : (int)i; return W / 2.0f + vtab[k] + (vtab[k + 1] - vtab[k]) * (f - k); }
static float hy_at(float x) { float f = sg_clamp(x / W, 0, 1) * 32, i = sg_floor(f); int k = (int)i > 31 ? 31 : (int)i; return H / 2.0f + htab[k] + (htab[k + 1] - htab[k]) * (f - k); }

static void ck_close(Crack *c)
{
	c->cum[0] = 0;
	for (int i = 1; i < c->n; i++) { float dx = c->p[i].x - c->p[i - 1].x, dy = c->p[i].y - c->p[i - 1].y; c->cum[i] = c->cum[i - 1] + sg_sqrt(dx * dx + dy * dy); }
}
static Crack *ck_new(float t0, float speed, float w0, float w1)
{
	if (nck >= MAXCRACK) return &ck[MAXCRACK - 1];
	Crack *c = &ck[nck++]; c->n = 0; c->t0 = t0; c->speed = speed; c->w0 = w0; c->w1 = w1; return c;
}
/* make a crack jagged: split every segment and push the new point sideways (twice over) */
static void ck_roughen(Crack *c, float amt)
{
	for (int pass = 0; pass < 2; pass++) {
		if (c->n * 2 - 1 > CKPTS) break;
		V2 q[CKPTS]; int m = 0;
		for (int i = 0; i < c->n; i++) {
			q[m++] = c->p[i];
			if (i + 1 < c->n) {
				V2 a = c->p[i], b = c->p[i + 1];
				float dx = b.x - a.x, dy = b.y - a.y, l = sg_sqrt(dx * dx + dy * dy) + 1e-6f, j = (sg_frand() - .5f) * 2 * amt * l;
				float f = 0.35f + 0.3f * sg_frand();
				q[m++] = (V2){a.x + dx * f - dy / l * j, a.y + dy * f + dx / l * j};
			}
		}
		memcpy(c->p, q, sizeof(V2) * (size_t)m); c->n = m;
		amt *= 0.6f;
	}
	ck_close(c);
}

static void walk(float *tab, float step, float lim)
{
	tab[16] = 0;
	for (int dir = -1; dir <= 1; dir += 2) {
		float o = 0;
		for (int i = 16 + dir; i >= 0 && i <= 32; i += dir) {
			o += (sg_frand() - .5f) * 2 * step; o = sg_clamp(o, -lim, lim); tab[i] = o;
		}
	}
}

static void gen_cracks(void)
{
	nck = 0; nglint = 0;
	float cx = W / 2.0f, cy = H / 2.0f;
	walk(vtab, W * 0.020f, W * 0.040f);
	walk(htab, H * 0.030f, H * 0.055f);
	for (int arm = 0; arm < 4; arm++) {                        /* the cross: up, down, left, right from the centre */
		Crack *c = ck_new(0.0f, H * 1.7f, U * 0.34f, U * 0.22f);
		int dir = (arm & 1) ? 1 : -1;
		for (int i = 16; i >= 0 && i <= 32 && c->n < CKPTS; i += dir) {
			if (arm < 2) c->p[c->n++] = (V2){W / 2.0f + vtab[i], i * (float)H / 32};
			else c->p[c->n++] = (V2){i * (float)W / 32, H / 2.0f + htab[i]};
			if (c->n >= 17) break;
		}
		ck_close(c);                                           /* not roughened: the panes follow these exactly */
	}
	nmain = nck;
	enum { NR = 13 };
	float th[NR], rl[NR];
	for (int k = 0; k < NR; k++) {                               /* radial cracks, tapering, with branches */
		th[k] = (k + sg_frand() * 0.55f) * 2 * SG_PI / NR + 0.2f;
		if (sg_abs(sg_cos(th[k])) < 0.10f || sg_abs(sg_sin(th[k])) < 0.10f) th[k] += 0.2f;
		float len = H * (0.20f + 0.34f * sg_frand() * sg_frand() + 0.05f);
		rl[k] = len;
		Crack *c = ck_new(0.02f + sg_frand() * 0.06f, H * (1.1f + 0.7f * sg_frand()), U * 0.26f, U * 0.04f);
		V2 p = {cx, cy}; float a = th[k];
		c->p[c->n++] = p;
		for (int i = 1; i <= 8; i++) {
			a += (sg_frand() - .5f) * 0.30f;
			float st = len / 8 * (0.8f + 0.4f * sg_frand());
			p.x += sg_cos(a) * st; p.y += sg_sin(a) * st; c->p[c->n++] = p;
		}
		ck_roughen(c, 0.10f);
		if (nglint < NGLINT) glint[nglint++] = c->p[c->n / 3 + (int)(sg_frand() * c->n / 3)];
		Crack parent = *c;
		for (int j = 0; j < 2; j++) {                              /* branches */
			if (parent.n < 9) break;
			int v = 4 + (int)(sg_frand() * (parent.n - 6));
			float ba = sg_atan2f_approx(parent.p[v].y - parent.p[v - 1].y, parent.p[v].x - parent.p[v - 1].x) + (j ? -1 : 1) * (0.40f + 0.5f * sg_frand());
			float wf = 1 - parent.cum[v] / parent.cum[parent.n - 1];
			Crack *b = ck_new(parent.t0 + parent.cum[v] / parent.speed, parent.speed, U * 0.16f * wf + U * 0.04f, U * 0.03f);
			V2 q = parent.p[v]; b->p[b->n++] = q;
			float bl = H * (0.05f + 0.09f * sg_frand());
			for (int i = 1; i <= 4; i++) { ba += (sg_frand() - .5f) * 0.4f; q.x += sg_cos(ba) * bl / 4; q.y += sg_sin(ba) * bl / 4; b->p[b->n++] = q; }
			ck_roughen(b, 0.12f);
		}
	}
	/* a spider web: short bowed chords between neighbouring radials, at a few distances */
	const float RR[4] = {0.055f, 0.12f, 0.20f, 0.30f};
	for (int r = 0; r < 4; r++)
		for (int k = 0; k < NR; k++) {
			int k2 = (k + 1) % NR;
			float rad = H * RR[r] * (0.88f + 0.24f * sg_frand());
			if (rad > rl[k] * 0.95f || rad > rl[k2] * 0.95f || sg_frand() < 0.22f + r * 0.08f) continue;
			float a0 = th[k], a1 = th[k2] + (k2 == 0 ? 2 * SG_PI : 0), am = (a0 + a1) / 2;
			Crack *c = ck_new(0.05f + rad / (H * 1.4f), 0, U * 0.13f * (1 - r * 0.15f), U * 0.08f);
			c->p[c->n++] = (V2){cx + sg_cos(a0) * rad, cy + sg_sin(a0) * rad};
			c->p[c->n++] = (V2){cx + sg_cos(am) * rad * 0.90f, cy + sg_sin(am) * rad * 0.90f};
			c->p[c->n++] = (V2){cx + sg_cos(a1) * rad, cy + sg_sin(a1) * rad};
			ck_roughen(c, 0.14f);
			c->speed = c->cum[c->n - 1] / 0.16f;
		}
	/* hairlines crowding the impact */
	for (int k = 0; k < 26 && nck < MAXCRACK; k++) {
		float a = sg_frand() * 2 * SG_PI, r0 = U * (0.5f + 2.5f * sg_frand()), l = U * (1.0f + 4.5f * sg_frand());
		Crack *c = ck_new(0.01f + sg_frand() * 0.05f, H * 0.8f, U * 0.08f, U * 0.02f);
		c->p[c->n++] = (V2){cx + sg_cos(a) * r0, cy + sg_sin(a) * r0};
		a += (sg_frand() - .5f) * 1.2f;
		c->p[c->n++] = (V2){c->p[0].x + sg_cos(a) * l, c->p[0].y + sg_sin(a) * l};
		ck_roughen(c, 0.2f);
	}
	for (int k = 0; k < NCHIP; k++) {                             /* flakes chipped out of the centre */
		float a = k * 2 * SG_PI / NCHIP + sg_frand() * 0.4f, r = U * (0.6f + 1.8f * sg_frand()), sz = U * (0.5f + 1.1f * sg_frand());
		V2 o = {cx + sg_cos(a) * r, cy + sg_sin(a) * r};
		for (int v = 0; v < 3; v++) { float b = a + v * 2.1f + sg_frand() * 0.6f; chipp[k][v] = (V2){o.x + sg_cos(b) * sz, o.y + sg_sin(b) * sz * 0.8f}; }
	}
}

/* Draw a crack grown to time t (seconds since the impact): a soft dark gap, a pale second face
 * offset to one side (the fracture plane catching light), then the bright edge. Tapers. */
static void crack_draw(Surf *s, const Crack *c, float t, float alpha)
{
	float L = c->speed * (t - c->t0);
	if (c->speed == 0) L = t >= c->t0 ? 1e9f : 0;
	if (L <= 0 || c->n < 2) return;
	float total = c->cum[c->n - 1];
	if (L > total) L = total;
	for (int pass = 0; pass < 3; pass++)
		for (int i = 1; i < c->n && c->cum[i - 1] < L; i++) {
			V2 a = c->p[i - 1], b = c->p[i];
			float f = sg_min(1, (L - c->cum[i - 1]) / (c->cum[i] - c->cum[i - 1] + 1e-6f));
			V2 e = {a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f};
			float w = c->w0 + (c->w1 - c->w0) * (c->cum[i - 1] / (total + 1e-6f));
			if (pass == 0) sg_line(s, a, e, w * 3.4f + 1.2f, BLACK, 0.55f * alpha);
			else if (pass == 1) { float o = sg_max(1.0f, w * 1.1f); sg_line(s, (V2){a.x + o, a.y + o}, (V2){e.x + o, e.y + o}, sg_max(0.8f, w * 0.9f), RGB(170, 172, 182), 0.40f * alpha); }
			else sg_line(s, a, e, sg_max(0.9f, w * 0.85f), WHITE, alpha);
		}
}

/* the crushed spot where it was hit, and light glinting along the cracks */
static void impact_draw(Surf *s, float t, float alpha)
{
	if (t <= 0 || alpha <= 0) return;
	float cx = W / 2.0f, cy = H / 2.0f, k = sg_sat(t / 0.15f) * alpha;
	sg_glow(s, cx, cy, U * 5, WHITE, 0.10f * k);
	for (int i = 0; i < NCHIP; i++) {
		sg_tri(s, chipp[i][0], chipp[i][1], chipp[i][2], RGB(214, 216, 224), 0.55f * k);
		sg_line(s, chipp[i][0], chipp[i][1], sg_max(1, U * 0.08f), WHITE, 0.8f * k);
	}
	sg_disc(s, cx, cy, U * 0.7f, BLACK, 0.8f * k);
	sg_ring(s, cx, cy, U * 0.9f, sg_max(1, U * 0.12f), WHITE, 0.7f * k);
}
static void glints_draw(Surf *s, float t, float alpha)
{
	for (int i = 0; i < nglint; i++) {
		float ph = sg_fract(t * 0.23f + i * 0.37f), g = ph < 0.12f ? sg_sin(ph / 0.12f * SG_PI) : 0;
		if (g <= 0.01f) continue;
		V2 p = glint[i];
		sg_glow(s, p.x, p.y, U * 2.4f, WHITE, 0.35f * g * alpha);
		sg_line(s, (V2){p.x - U * 1.8f * g, p.y}, (V2){p.x + U * 1.8f * g, p.y}, sg_max(1, U * 0.08f), WHITE, 0.8f * g * alpha);
		sg_line(s, (V2){p.x, p.y - U * 1.2f * g}, (V2){p.x, p.y + U * 1.2f * g}, sg_max(1, U * 0.08f), WHITE, 0.6f * g * alpha);
	}
}

/* ================================================================ the glass */
static uint8_t *qmap;                   /* which pane every pixel belongs to: 0 TL, 1 TR, 2 BL, 3 BR */
static uint32_t *bg_dark, *bg_light, *bg_sec;

static uint32_t glass_px(int x, int y, int light)
{
	float fy = (float)y / H, s = (x * 0.55f + y) / (W * 0.55f + H);
	float streak = sg_bump(s - 0.50f, 0.20f), streak2 = 0.6f * sg_bump(s - 0.78f, 0.06f);
	int g = (int)((sg_hash(x, y) >> 10) & 7) - 3;
	int r, gg, b;
	if (!light) {
		float k = 0.045f * (streak + streak2);
		r = (int)(sg_lerp(26, 7, fy) + 255 * k + g * 0.6f); gg = (int)(sg_lerp(26, 7, fy) + 255 * k + g * 0.6f); b = (int)(sg_lerp(31, 9, fy) + 255 * k + g * 0.6f);
	} else {
		float k = 0.55f * streak + 0.35f * streak2;
		r = (int)(sg_lerp(244, 196, fy) + (255 - sg_lerp(244, 196, fy)) * k + g); gg = (int)(sg_lerp(245, 199, fy) + (255 - sg_lerp(245, 199, fy)) * k + g); b = (int)(sg_lerp(248, 208, fy) + (255 - sg_lerp(248, 208, fy)) * k + g);
	}
	r = r < 0 ? 0 : r > 255 ? 255 : r; gg = gg < 0 ? 0 : gg > 255 ? 255 : gg; b = b < 0 ? 0 : b > 255 ? 255 : b;
	return RGB(r, gg, b);
}

static void bake_home(void)
{
	float gapw = sg_max(3.0f, U * 0.85f), bevel = U * 0.45f;
	for (int y = 0; y < H; y++) {
		float vx = vx_at(y + .5f);
		for (int x = 0; x < W; x++) {
			float hy = hy_at(x + .5f);
			int i = y * W + x, q = (x + .5f > vx) + 2 * (y + .5f > hy);
			float d = sg_min(sg_abs(x + .5f - vx), sg_abs(y + .5f - hy));
			qmap[i] = (uint8_t)q;
			float cov = sg_sat(gapw / 2 - d + .5f), edge = sg_sat(1 - (d - gapw / 2) / bevel);
			uint32_t dk = glass_px(x, y, 0), lt = glass_px(x, y, 1);
			dk = sg_mix(dk, WHITE, 0.30f * edge * edge); lt = sg_mix(lt, RGB(90, 94, 104), 0.32f * edge * edge);
			bg_dark[i] = sg_mix(dk, BLACK, cov); bg_light[i] = sg_mix(lt, BLACK, cov);
		}
	}
	for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) bg_sec[y * W + x] = glass_px(x, y, 0);
	Surf s2 = sg_surf(bg_sec, W, H);
	for (int i = nmain; i < nck; i++) crack_draw(&s2, &ck[i], 99, 0.16f);       /* faint hairlines, so sections stay cracked glass */
	impact_draw(&s2, 99, 0.16f);
}

/* ================================================================ icons */
enum { I_SETTINGS, I_SEARCH, I_APPS, I_FILES, I_FOLDER, I_FILE, I_TERM, I_MONITOR, I_INFO, I_POWER,
       I_STORE, I_PC, I_CHIP, I_RAM, I_DISK, I_SCREEN, I_NET, I_BATTERY, I_SOUND, I_DOWNLOAD, I_USER, I_GAME };
static void icon(int kind, float cx, float cy, float r, uint32_t c, float a)
{
	float sw = sg_max(1.6f, r * 0.15f);
	switch (kind) {
	case I_SETTINGS:
		sg_ring(&fb, cx, cy, r * 0.56f, r * 0.30f, c, a);
		for (int i = 0; i < 8; i++) {
			float an = i * SG_PI / 4, ca = sg_cos(an), sa = sg_sin(an), w = r * 0.17f;
			V2 q[4] = {{cx + ca * r * 0.62f - sa * w, cy + sa * r * 0.62f + ca * w}, {cx + ca * r * 0.98f - sa * w * 0.8f, cy + sa * r * 0.98f + ca * w * 0.8f},
				   {cx + ca * r * 0.98f + sa * w * 0.8f, cy + sa * r * 0.98f - ca * w * 0.8f}, {cx + ca * r * 0.62f + sa * w, cy + sa * r * 0.62f - ca * w}};
			sg_poly(&fb, q, 4, c, a, 0.3f);
		}
		break;
	case I_SEARCH:
		sg_ring(&fb, cx - r * 0.12f, cy - r * 0.12f, r * 0.52f, r * 0.17f, c, a);
		sg_line(&fb, (V2){cx + r * 0.26f, cy + r * 0.26f}, (V2){cx + r * 0.84f, cy + r * 0.84f}, r * 0.21f, c, a);
		break;
	case I_APPS:
		for (int i = 0; i < 4; i++) sg_rrect(&fb, cx + ((i & 1) ? r * 0.08f : -r * 0.86f), cy + ((i & 2) ? r * 0.08f : -r * 0.86f), r * 0.78f, r * 0.78f, r * 0.16f, 0, c, a);
		break;
	case I_FILES:
	case I_FOLDER:
		sg_rrect(&fb, cx - r * 0.9f, cy - r * 0.62f, r * 0.78f, r * 0.42f, r * 0.12f, 0, c, a);
		sg_rrect(&fb, cx - r * 0.9f, cy - r * 0.40f, r * 1.8f, r * 1.1f, r * 0.14f, 0, c, a);
		break;
	case I_FILE:
		{ V2 q[5] = {{cx - r * 0.62f, cy - r * 0.85f}, {cx + r * 0.20f, cy - r * 0.85f}, {cx + r * 0.62f, cy - r * 0.43f}, {cx + r * 0.62f, cy + r * 0.85f}, {cx - r * 0.62f, cy + r * 0.85f}};
		  sg_rrect(&fb, cx - r * 0.62f, cy - r * 0.85f, r * 1.24f, r * 1.7f, r * 0.1f, sw, c, a); (void)q; }
		sg_line(&fb, (V2){cx - r * 0.3f, cy + r * 0.0f}, (V2){cx + r * 0.3f, cy + r * 0.0f}, sw * 0.9f, c, a * 0.8f);
		sg_line(&fb, (V2){cx - r * 0.3f, cy + r * 0.35f}, (V2){cx + r * 0.3f, cy + r * 0.35f}, sw * 0.9f, c, a * 0.8f);
		break;
	case I_TERM:
		sg_rrect(&fb, cx - r * 0.95f, cy - r * 0.7f, r * 1.9f, r * 1.4f, r * 0.14f, sw, c, a);
		sg_line(&fb, (V2){cx - r * 0.5f, cy - r * 0.25f}, (V2){cx - r * 0.1f, cy}, sw, c, a);
		sg_line(&fb, (V2){cx - r * 0.1f, cy}, (V2){cx - r * 0.5f, cy + r * 0.25f}, sw, c, a);
		sg_line(&fb, (V2){cx + r * 0.05f, cy + r * 0.28f}, (V2){cx + r * 0.5f, cy + r * 0.28f}, sw, c, a);
		break;
	case I_MONITOR:
		sg_line(&fb, (V2){cx - r * 0.95f, cy + r * 0.45f}, (V2){cx - r * 0.45f, cy + r * 0.45f}, sw, c, a);
		sg_line(&fb, (V2){cx - r * 0.45f, cy + r * 0.45f}, (V2){cx - r * 0.2f, cy - r * 0.55f}, sw, c, a);
		sg_line(&fb, (V2){cx - r * 0.2f, cy - r * 0.55f}, (V2){cx + r * 0.2f, cy + r * 0.7f}, sw, c, a);
		sg_line(&fb, (V2){cx + r * 0.2f, cy + r * 0.7f}, (V2){cx + r * 0.45f, cy + r * 0.0f}, sw, c, a);
		sg_line(&fb, (V2){cx + r * 0.45f, cy + r * 0.0f}, (V2){cx + r * 0.95f, cy + r * 0.0f}, sw, c, a);
		break;
	case I_INFO:
		sg_ring(&fb, cx, cy, r * 0.85f, sw, c, a);
		sg_disc(&fb, cx, cy - r * 0.42f, r * 0.11f, c, a);
		sg_line(&fb, (V2){cx, cy - r * 0.12f}, (V2){cx, cy + r * 0.48f}, sw * 1.3f, c, a);
		break;
	case I_POWER:
		sg_ring(&fb, cx, cy + r * 0.08f, r * 0.7f, sw, c, a);
		sg_rect(&fb, cx - sw * 0.9f, cy - r * 0.95f, sw * 1.8f, r * 0.9f, BLACK, 0);   /* (gap handled by the line below) */
		sg_line(&fb, (V2){cx, cy - r * 0.9f}, (V2){cx, cy - r * 0.1f}, sw * 1.2f, c, a);
		break;
	case I_STORE:                                                         /* a shopping bag with a handle */
		sg_rrect(&fb, cx - r * 0.78f, cy - r * 0.30f, r * 1.56f, r * 1.15f, r * 0.16f, 0, c, a);
		sg_ring(&fb, cx, cy - r * 0.32f, r * 0.42f, sw, c, a);
		sg_rect(&fb, cx - r * 0.5f, cy - r * 0.30f, r * 1.0f, r * 0.45f, BLACK, 0);
		sg_disc(&fb, cx - r * 0.36f, cy + r * 0.05f, r * 0.09f, BLACK, a * 0.6f); sg_disc(&fb, cx + r * 0.36f, cy + r * 0.05f, r * 0.09f, BLACK, a * 0.6f);
		break;
	case I_PC:                                                            /* a monitor on a stand */
		sg_rrect(&fb, cx - r * 0.95f, cy - r * 0.75f, r * 1.9f, r * 1.2f, r * 0.12f, sw, c, a);
		sg_rect(&fb, cx - r * 0.08f, cy + r * 0.45f, r * 0.16f, r * 0.3f, c, a);
		sg_rrect(&fb, cx - r * 0.45f, cy + r * 0.72f, r * 0.9f, r * 0.14f, r * 0.07f, 0, c, a);
		break;
	case I_CHIP:
		sg_rrect(&fb, cx - r * 0.55f, cy - r * 0.55f, r * 1.1f, r * 1.1f, r * 0.12f, sw, c, a);
		sg_rrect(&fb, cx - r * 0.25f, cy - r * 0.25f, r * 0.5f, r * 0.5f, r * 0.06f, 0, c, a);
		for (int i = 0; i < 3; i++) {
			float o = (i - 1) * r * 0.32f;
			sg_line(&fb, (V2){cx + o, cy - r * 0.9f}, (V2){cx + o, cy - r * 0.55f}, sw * 0.8f, c, a); sg_line(&fb, (V2){cx + o, cy + r * 0.55f}, (V2){cx + o, cy + r * 0.9f}, sw * 0.8f, c, a);
			sg_line(&fb, (V2){cx - r * 0.9f, cy + o}, (V2){cx - r * 0.55f, cy + o}, sw * 0.8f, c, a); sg_line(&fb, (V2){cx + r * 0.55f, cy + o}, (V2){cx + r * 0.9f, cy + o}, sw * 0.8f, c, a);
		}
		break;
	case I_RAM:
		sg_rrect(&fb, cx - r * 0.95f, cy - r * 0.42f, r * 1.9f, r * 0.7f, r * 0.08f, sw, c, a);
		for (int i = 0; i < 4; i++) sg_rect(&fb, cx - r * 0.72f + i * r * 0.4f, cy - r * 0.24f, r * 0.24f, r * 0.32f, c, a);
		for (int i = 0; i < 7; i++) sg_rect(&fb, cx - r * 0.8f + i * r * 0.26f, cy + r * 0.32f, r * 0.12f, r * 0.2f, c, a);
		break;
	case I_DISK:
		sg_rrect(&fb, cx - r * 0.95f, cy - r * 0.5f, r * 1.9f, r * 1.0f, r * 0.16f, sw, c, a);
		sg_disc(&fb, cx + r * 0.55f, cy, r * 0.12f, c, a);
		sg_line(&fb, (V2){cx - r * 0.65f, cy}, (V2){cx + r * 0.15f, cy}, sw, c, a);
		break;
	case I_SCREEN:
		sg_rrect(&fb, cx - r * 0.95f, cy - r * 0.62f, r * 1.9f, r * 1.24f, r * 0.12f, sw, c, a);
		sg_line(&fb, (V2){cx - r * 0.5f, cy + r * 0.88f}, (V2){cx + r * 0.5f, cy + r * 0.88f}, sw, c, a);
		break;
	case I_NET:                                                           /* wifi waves */
		for (int k = 0; k < 3; k++) {
			float rr = r * (0.35f + 0.3f * k);
			for (int j = 0; j < 7; j++) {
				float a0 = -SG_PI * 0.75f + j * SG_PI * 0.5f / 7, a1 = a0 + SG_PI * 0.5f / 7;
				sg_line(&fb, (V2){cx + sg_cos(a0) * rr, cy + r * 0.45f + sg_sin(a0) * rr}, (V2){cx + sg_cos(a1) * rr, cy + r * 0.45f + sg_sin(a1) * rr}, sw, c, a);
			}
		}
		sg_disc(&fb, cx, cy + r * 0.45f, r * 0.12f, c, a);
		break;
	case I_BATTERY:
		sg_rrect(&fb, cx - r * 0.95f, cy - r * 0.45f, r * 1.7f, r * 0.9f, r * 0.12f, sw, c, a);
		sg_rect(&fb, cx + r * 0.78f, cy - r * 0.2f, r * 0.17f, r * 0.4f, c, a);
		sg_rect(&fb, cx - r * 0.75f, cy - r * 0.25f, r * 0.9f, r * 0.5f, c, a);
		break;
	case I_SOUND:
		{ V2 q[4] = {{cx - r * 0.85f, cy - r * 0.28f}, {cx - r * 0.35f, cy - r * 0.28f}, {cx - r * 0.35f, cy + r * 0.28f}, {cx - r * 0.85f, cy + r * 0.28f}}; sg_poly(&fb, q, 4, c, a, 0.3f);
		  V2 cone[4] = {{cx - r * 0.35f, cy - r * 0.28f}, {cx + r * 0.1f, cy - r * 0.7f}, {cx + r * 0.1f, cy + r * 0.7f}, {cx - r * 0.35f, cy + r * 0.28f}}; sg_poly(&fb, cone, 4, c, a, 0.3f); }
		for (int k = 0; k < 2; k++) {
			float rr = r * (0.42f + 0.3f * k);
			for (int j = 0; j < 6; j++) { float a0 = -0.8f + j * 1.6f / 6, a1 = a0 + 1.6f / 6;
				sg_line(&fb, (V2){cx + r * 0.1f + sg_cos(a0) * rr, cy + sg_sin(a0) * rr}, (V2){cx + r * 0.1f + sg_cos(a1) * rr, cy + sg_sin(a1) * rr}, sw * 0.9f, c, a); }
		}
		break;
	case I_DOWNLOAD:
		sg_line(&fb, (V2){cx, cy - r * 0.85f}, (V2){cx, cy + r * 0.25f}, sw * 1.2f, c, a);
		sg_line(&fb, (V2){cx - r * 0.42f, cy - r * 0.15f}, (V2){cx, cy + r * 0.28f}, sw * 1.2f, c, a);
		sg_line(&fb, (V2){cx + r * 0.42f, cy - r * 0.15f}, (V2){cx, cy + r * 0.28f}, sw * 1.2f, c, a);
		sg_line(&fb, (V2){cx - r * 0.8f, cy + r * 0.75f}, (V2){cx + r * 0.8f, cy + r * 0.75f}, sw * 1.2f, c, a);
		break;
	case I_GAME:                                                          /* a game controller */
		sg_rrect(&fb, cx - r * 0.95f, cy - r * 0.45f, r * 1.9f, r * 0.95f, r * 0.42f, 0, c, a);
		sg_rect(&fb, cx - r * 0.62f, cy - r * 0.07f, r * 0.42f, r * 0.14f, BLACK, a * 0.7f);
		sg_rect(&fb, cx - r * 0.48f, cy - r * 0.21f, r * 0.14f, r * 0.42f, BLACK, a * 0.7f);
		sg_disc(&fb, cx + r * 0.42f, cy - r * 0.1f, r * 0.1f, BLACK, a * 0.7f); sg_disc(&fb, cx + r * 0.62f, cy + r * 0.1f, r * 0.1f, BLACK, a * 0.7f);
		break;
	case I_USER:
		sg_disc(&fb, cx, cy - r * 0.35f, r * 0.38f, c, a);
		sg_rrect(&fb, cx - r * 0.75f, cy + r * 0.15f, r * 1.5f, r * 0.75f, r * 0.37f, 0, c, a);
		break;
	}
}

/* ================================================================ data: files, network, system */
typedef struct { char name[96]; int dir; long size; } FEnt;
#define MAXENT 400
static FEnt ents[MAXENT];
static int nent;
static char homedir[160] = "/home/silver", cwd[256];

#ifdef RENDER
static const struct { const char *path; int dir; long size; const char *text; } MOCK[] = {
	{"/home/silver/Documents", 1, 0, 0}, {"/home/silver/Pictures", 1, 0, 0}, {"/home/silver/Music", 1, 0, 0}, {"/home/silver/Downloads", 1, 0, 0},
	{"/home/silver/Projects", 1, 0, 0},
	{"/home/silver/notes.txt", 0, 214, "Things to do\n\n- finish the boot animation\n- pick a wallpaper\n- back up the Windows drive\n- try the new pen\n\nBuilt from nothing."},
	{"/home/silver/todo.txt", 0, 96, "[x] install SilverOS\n[x] dual boot with Windows\n[ ] write more apps"},
	{"/home/silver/wallpaper.png", 0, 482311, 0}, {"/home/silver/readme.md", 0, 1204, "# Welcome to SilverOS\n\nThis is your home folder.\nPress Esc to go back."},
	{"/home/silver/Documents/letter.txt", 0, 880, "Dear future me,\n\nIt worked.\n"}, {"/home/silver/Documents/budget.csv", 0, 2048, "month,spent\njan,120\nfeb,98\nmar,143"},
	{"/home/silver/Documents/ideas.txt", 0, 640, "- a calendar app\n- a music player\n- a better pen"},
	{"/home/silver/Pictures/cat.jpg", 0, 912345, 0}, {"/home/silver/Pictures/sunset.png", 0, 1523342, 0},
	{"/home/silver/Projects/silveros", 1, 0, 0}, {"/home/silver/Music/mix.mp3", 0, 7340032, 0}, {"/home/silver/Downloads/installer.iso", 0, 26214400, 0},
};
static int mock_parent_eq(const char *path, const char *dir)
{
	size_t dl = strlen(dir);
	if (strncmp(path, dir, dl) || path[dl] != '/') return 0;
	return strchr(path + dl + 1, '/') == NULL;
}
#endif

static int ent_cmp(const void *a, const void *b)
{
	const FEnt *x = a, *y = b;
	if (x->dir != y->dir) return y->dir - x->dir;
	return strcasecmp(x->name, y->name);
}
static void fs_list(const char *path)
{
	nent = 0;
#ifdef RENDER
	for (unsigned i = 0; i < sizeof MOCK / sizeof MOCK[0] && nent < MAXENT; i++)
		if (mock_parent_eq(MOCK[i].path, path)) {
			FEnt *e = &ents[nent++]; snprintf(e->name, sizeof e->name, "%s", strrchr(MOCK[i].path, '/') + 1); e->dir = MOCK[i].dir; e->size = MOCK[i].size;
		}
#else
	DIR *d = opendir(path);
	if (!d) return;
	struct dirent *de;
	while ((de = readdir(d)) && nent < MAXENT) {
		if (de->d_name[0] == '.') continue;
		char full[512]; struct stat st;
		snprintf(full, sizeof full, "%s/%s", path, de->d_name);
		if (stat(full, &st)) continue;
		FEnt *e = &ents[nent++]; snprintf(e->name, sizeof e->name, "%s", de->d_name); e->dir = S_ISDIR(st.st_mode); e->size = (long)st.st_size;
	}
	closedir(d);
#endif
	qsort(ents, (size_t)nent, sizeof ents[0], ent_cmp);
}

#define MAXLINE 22
static char pv_line[MAXLINE][100];
static int pv_n, pv_binary;
static long pv_size;
static void fs_preview(const char *path, long size)
{
	pv_n = 0; pv_binary = 0; pv_size = size;
#ifdef RENDER
	for (unsigned i = 0; i < sizeof MOCK / sizeof MOCK[0]; i++)
		if (!strcmp(MOCK[i].path, path)) {
			if (!MOCK[i].text) { pv_binary = 1; return; }
			const char *t = MOCK[i].text; int c = 0;
			for (; *t && pv_n < MAXLINE; t++) { if (*t == '\n') { pv_line[pv_n][c] = 0; pv_n++; c = 0; } else if (c < 98) pv_line[pv_n][c++] = *t; }
			if (c && pv_n < MAXLINE) { pv_line[pv_n][c] = 0; pv_n++; }
			return;
		}
	pv_binary = 1;
#else
	FILE *f = fopen(path, "rb");
	char buf[2048];
	if (!f) { pv_binary = 1; return; }
	size_t n = fread(buf, 1, sizeof buf, f); fclose(f);
	int c = 0;
	for (size_t i = 0; i < n && pv_n < MAXLINE; i++) {
		unsigned char ch = (unsigned char)buf[i];
		if (ch == '\n') { pv_line[pv_n][c] = 0; pv_n++; c = 0; }
		else if (ch == '\t') { for (int k = 0; k < 4 && c < 98; k++) pv_line[pv_n][c++] = ' '; }
		else if (ch >= 32 && ch < 127) { if (c < 98) pv_line[pv_n][c++] = (char)ch; }
		else if (ch != '\r') { pv_binary = 1; return; }
	}
	if (c && pv_n < MAXLINE) { pv_line[pv_n][c] = 0; pv_n++; }
#endif
}

static void human(long b, char *out, size_t n)
{
	if (b < 1024) snprintf(out, n, "%ld B", b);
	else if (b < 1024 * 1024) snprintf(out, n, "%ld KB", b / 1024);
	else snprintf(out, n, "%ld.%ld MB", b / (1024 * 1024), (b * 10 / (1024 * 1024)) % 10);
}

static void net_text(char *out, size_t n)
{
#ifdef RENDER
	snprintf(out, n, "eth0  192.168.1.24");
#else
	struct ifaddrs *ifa, *p;
	out[0] = 0;
	if (getifaddrs(&ifa) == 0) {
		for (p = ifa; p; p = p->ifa_next)
			if (p->ifa_addr && p->ifa_addr->sa_family == AF_INET && !(p->ifa_flags & IFF_LOOPBACK)) {
				char ip[40]; inet_ntop(AF_INET, &((struct sockaddr_in *)p->ifa_addr)->sin_addr, ip, sizeof ip);
				snprintf(out, n, "%s  %s", p->ifa_name, ip); break;
			}
		freeifaddrs(ifa);
	}
	if (!out[0]) snprintf(out, n, "not connected");
#endif
}

typedef struct { float cpu; long mem_total, mem_used; long up; float load; } Sys;
static void sys_read(Sys *s, float t)
{
#ifdef RENDER
	s->cpu = 22 + 20 * sg_sin(t * 1.3f) + 8 * sg_sin(t * 4.1f); s->mem_total = 8192; s->mem_used = 2900 + (long)(120 * sg_sin(t * 0.4f)); s->up = 1384 + (long)t; s->load = 0.4f + 0.2f * sg_sin(t);
#else
	static unsigned long long pt = 0, pi = 0;
	unsigned long long u, n, sy, id, io, ir, so;
	FILE *f = fopen("/proc/stat", "r");
	if (f) {
		if (fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu", &u, &n, &sy, &id, &io, &ir, &so) == 7) {
			unsigned long long tot = u + n + sy + id + io + ir + so, idle = id + io;
			if (pt && tot > pt) s->cpu = 100.f * (float)((tot - pt) - (idle - pi)) / (float)(tot - pt);
			pt = tot; pi = idle;
		}
		fclose(f);
	}
	f = fopen("/proc/meminfo", "r");
	long tot = 0, av = 0; char k[64]; long v;
	if (f) { while (fscanf(f, "%63s %ld kB\n", k, &v) == 2) { if (!strcmp(k, "MemTotal:")) tot = v; if (!strcmp(k, "MemAvailable:")) av = v; } fclose(f); }
	s->mem_total = tot / 1024; s->mem_used = (tot - av) / 1024;
	f = fopen("/proc/uptime", "r"); double up = 0; if (f) { if (fscanf(f, "%lf", &up) == 1) { /* ok */ } fclose(f); } s->up = (long)up;
	f = fopen("/proc/loadavg", "r"); double ld = 0; if (f) { if (fscanf(f, "%lf", &ld) == 1) { /* ok */ } fclose(f); } s->load = (float)ld;
#endif
}

#include "search.h"

/* ================================================================ drawing helpers */
static void fit(const char *s, float size, float maxw, char *out, size_t n)
{
	snprintf(out, n, "%s", s);
	while (strlen(out) > 2 && sg_text_w(&FT, out, size, 0.01f) > maxw) { out[strlen(out) - 1] = 0; if (strlen(out) > 1) { out[strlen(out) - 1] = '.'; } }
}
static void cap_text(float x, float y, const char *s, float size, uint32_t c, float a, int right)
{
	float w = sg_text_w(&FC, s, size, 0.22f);
	sg_text(&fb, &FC, right ? x - w : x, y, s, size, c, a, 0.22f);
}

typedef struct {
	int sel, screen;                        /* sel: 0..3 pane; screen: 0 home, 1 settings, 2 search, 3 apps, 4 files */
	float blend[4];
	int row;                                /* current row in a list */
	int top;                                /* first visible row */
	int sub;                                /* apps: 0 grid, 1 monitor, 2 about, 3 store, 4 me and my pc */
	int sub_from;                           /* the grid tile a sub-screen was opened from */
	int mute_on;                            /* sound effects off */
	Search sr; int sr_ready;                 /* the file search engine (Files only) */
	int fs, fs_scope;                        /* Files: searching, and what in (0 names, 1 text) */
	int focus;                               /* YouTube search: 0 box, 1 results */
	int files_sel[16], depth;               /* remembered selection per directory level */
	int confirm;                            /* settings: which dangerous row awaits a second Enter */
	float cpu_hist[90]; int hn; float last_sample;
	Sys sys;
	int calm_on;
	float fade;                             /* fade-in after changing screen */
} Home;

static const uint32_t RED = RGB(232, 16, 24);

static void title_block(const char *title, const char *cap, int ic)
{
	icon(ic, U * 9.5f, U * 10.0f, U * 3.6f, WHITE, 1);
	sg_text(&fb, &FD, U * 15.5f + U * 0.35f, U * 13.2f, title, U * 7.4f, RED, 0.9f, 0);
	sg_text(&fb, &FD, U * 15.5f, U * 13.2f, title, U * 7.4f, WHITE, 1, 0);
	cap_text(U * 15.7f, U * 17.8f, cap, U * 1.2f, RGB(176, 176, 182), 1, 0);
}
static void ui_footer(float x, const char *const *kv, int n);
static void ui_frost(float x, float y, float w, float h, float r, float dark);
/* the hint bar: "KEY  meaning      KEY  meaning" becomes key caps on glass */
static void foot_hints(const char *s)
{
	static char buf[16][40];
	const char *kv[16];
	int n = 0;
	for (const char *p = s; *p && n < 8;) {
		while (*p == ' ') p++;
		const char *e = strstr(p, "      "), *end = e ? e : p + strlen(p);
		const char *m = strstr(p, "  ");
		if (!m || m > end) m = end;
		char key[40], lab[40];
		snprintf(key, sizeof key, "%.*s", (int)(m - p), p);
		const char *l = m; while (l < end && *l == ' ') l++;
		snprintf(lab, sizeof lab, "%.*s", (int)(end - l), l);
		for (char *c = lab; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);
		if (!strcmp(key, "UP DOWN")) snprintf(key, sizeof key, "\xE2\x86\x91\xE2\x86\x93");
		snprintf(buf[2 * n], 40, "%s", key); snprintf(buf[2 * n + 1], 40, "%s", lab);
		kv[2 * n] = buf[2 * n]; kv[2 * n + 1] = buf[2 * n + 1];
		n++;
		p = end;
	}
	ui_footer(U * 5, kv, n);
	cap_text(W - U * 5, H - U * 4.15f, "SILVER OS", U * 1.0f, RGB(150, 150, 156), 1, 1);
}

static void ui_card(float x, float y, float w, float h, float r, uint32_t fill, float a, float shadow);
static void row_box(float x, float y, float w, float h, int sel, float pulse)
{
	(void)pulse;
	float g = U * 0.35f, r = U * 1.1f;
	if (sel) {
		sg_blob(&fb, x + w / 2, y + h / 2 + U * 0.8f, w * 0.56f, h * 0.9f, BLACK, 0.5f);
		sg_rrect(&fb, x, y + g, w, h - 2 * g, r, 0, WHITE, 0.95f);
		sg_rrect(&fb, x, y + g, w, (h - 2 * g) * 0.5f, r, 0, WHITE, 0.25f);
		sg_rrect(&fb, x + U * 0.7f, y + h * 0.28f, U * 0.42f, h * 0.44f, U * 0.21f, 0, RED, 1);
	} else {
		sg_rrect(&fb, x, y + g, w, h - 2 * g, r, 0, WHITE, 0.045f);
		sg_rrect(&fb, x, y + g, w, h - 2 * g, r, sg_max(1, U * 0.1f), WHITE, 0.06f);
	}
}

/* ---------------------------------------------------------------- files */
static void draw_files(Home *h, float t)
{
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	title_block("FILES", "BROWSE YOUR HOME", I_FILES);
	float lx = U * 6, ly = U * 24, lw = W * 0.54f, rh = U * 5.0f;
	ui_frost(lx - U * 1.2f, ly - U * 5.0f, lw + U * 2.4f, H - U * 9 - (ly - U * 5.0f), U * 2.2f, 0.55f);
	char crumb[256]; fit(cwd, U * 1.7f, lw, crumb, sizeof crumb);
	sg_text(&fb, &FT, lx + U * 0.4f, ly - U * 1.8f, crumb, U * 1.8f, RGB(210, 210, 216), 1, 0.01f);
	int vis = (int)((H - ly - U * 9) / rh);
	if (h->row >= nent) h->row = nent ? nent - 1 : 0;
	if (h->row < h->top) h->top = h->row;
	if (h->row >= h->top + vis) h->top = h->row - vis + 1;
	if (!nent) sg_text(&fb, &FT, lx, ly + U * 5, "This folder is empty", U * 2.2f, RGB(150, 150, 156), 1, 0.01f);
	for (int i = 0; i < vis && h->top + i < nent; i++) {
		FEnt *e = &ents[h->top + i];
		float y = ly + i * rh;
		int sel = h->top + i == h->row;
		row_box(lx, y, lw, rh, sel, 0);
		uint32_t fg = sel ? INK : WHITE;
		icon(e->dir ? I_FOLDER : I_FILE, lx + U * 3.2f, y + rh / 2, U * 1.5f, e->dir ? (sel ? INK : RGB(232, 232, 236)) : (sel ? INK : RGB(170, 170, 176)), 1);
		char nm[128]; fit(e->name, U * 2.1f, lw - U * 18, nm, sizeof nm);
		sg_text(&fb, &FT, lx + U * 6.4f, y + rh / 2 + U * 0.75f, nm, U * 2.1f, fg, 1, 0.01f);
		char sz[24] = ""; if (!e->dir) human(e->size, sz, sizeof sz);
		cap_text(lx + lw - U * 1.6f, y + rh / 2 + U * 0.5f, e->dir ? "FOLDER" : sz, U * 1.0f, sel ? RGB(80, 80, 88) : RGB(130, 130, 136), 1, 1);
	}
	/* preview pane */
	float px = W * 0.62f, pw = W - px - U * 6, py = ly, ph = H - ly - U * 9;
	ui_frost(px, py, pw, ph, U * 2.0f, 0.6f);
	if (nent && !ents[h->row].dir) {
		FEnt *e = &ents[h->row];
		char nm[128]; fit(e->name, U * 2.4f, pw - U * 4, nm, sizeof nm);
		sg_text(&fb, &FT, px + U * 2, py + U * 4.2f, nm, U * 2.4f, WHITE, 1, 0.01f);
		char sz[24]; human(e->size, sz, sizeof sz);
		cap_text(px + U * 2, py + U * 6.4f, sz, U * 0.85f, RGB(150, 150, 156), 1, 0);
		if (pv_binary) sg_text(&fb, &FT, px + U * 2, py + U * 11, "No preview for this kind of file", U * 1.9f, RGB(150, 150, 156), 1, 0.01f);
		else for (int i = 0; i < pv_n && py + U * 10 + i * U * 2.5f < py + ph - U * 2; i++) {
			char ln[110]; fit(pv_line[i], U * 1.75f, pw - U * 4, ln, sizeof ln);
			sg_text(&fb, &FT, px + U * 2, py + U * 10 + i * U * 2.5f, ln, U * 1.75f, RGB(226, 226, 230), 1, 0.01f);
		}
	} else if (nent) {
		sg_text(&fb, &FT, px + U * 2, py + U * 4.2f, ents[h->row].name, U * 2.4f, WHITE, 1, 0.01f);
		cap_text(px + U * 2, py + U * 6.4f, "FOLDER  -  ENTER TO OPEN", U * 0.85f, RGB(150, 150, 156), 1, 0);
	}
	foot_hints("UP DOWN  move      ENTER  open      BACKSPACE  up a level      /  search      ESC  home");
	(void)t;
}

#include "ytui.h"

/* ---------------------------------------------------------------- apps */
static void draw_monitor(Home *h, float t)
{
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	title_block("MONITOR", "LIVE SYSTEM", I_MONITOR);
	Sys *s = &h->sys;
	float lx = U * 6, lw = W - U * 12, y = U * 26;
	sg_rect(&fb, lx, y, lw, U * 26, BLACK, 0.55f);
	cap_text(lx + U * 2, y + U * 3.6f, "CPU", U * 1.1f, WHITE, 1, 0);
	char b[48]; snprintf(b, sizeof b, "%d%%", (int)(s->cpu + 0.5f));
	sg_text(&fb, &FD, lx + lw - U * 2 - sg_text_w(&FD, b, U * 6.4f, 0), y + U * 7.6f, b, U * 6.4f, WHITE, 1, 0);
	float gx = lx + U * 2, gy = y + U * 10, gw = lw - U * 4, gh = U * 13;
	sg_rect(&fb, gx, gy + gh, gw, 1, WHITE, 0.25f);
	V2 prev = {gx, gy + gh};
	for (int i = 0; i < h->hn; i++) {
		V2 p = {gx + gw * i / 89.f, gy + gh - gh * sg_clamp(h->cpu_hist[i], 0, 100) / 100};
		sg_line(&fb, (V2){p.x, gy + gh}, (V2){p.x, p.y}, gw / 90.f + 0.5f, RED, 0.28f);
		if (i) sg_line(&fb, prev, p, sg_max(1.8f, U * 0.22f), WHITE, 1);
		prev = p;
	}
	float my = y + U * 30;
	sg_rect(&fb, lx, my, lw / 2 - U, U * 17, BLACK, 0.55f);
	cap_text(lx + U * 2, my + U * 3.6f, "MEMORY", U * 1.1f, WHITE, 1, 0);
	snprintf(b, sizeof b, "%ld / %ld MB", s->mem_used, s->mem_total);
	sg_text(&fb, &FT, lx + U * 2, my + U * 8.4f, b, U * 2.6f, WHITE, 1, 0.01f);
	float bw = lw / 2 - U * 5; sg_rect(&fb, lx + U * 2, my + U * 11.5f, bw, U * 1.0f, WHITE, 0.15f);
	sg_rect(&fb, lx + U * 2, my + U * 11.5f, bw * sg_sat((float)s->mem_used / (s->mem_total ? s->mem_total : 1)), U * 1.0f, RED, 1);
	float rx = lx + lw / 2 + U;
	sg_rect(&fb, rx, my, lw / 2 - U, U * 17, BLACK, 0.55f);
	cap_text(rx + U * 2, my + U * 3.6f, "UPTIME", U * 1.1f, WHITE, 1, 0);
	snprintf(b, sizeof b, "%ldh %02ldm %02lds", s->up / 3600, (s->up / 60) % 60, s->up % 60);
	sg_text(&fb, &FT, rx + U * 2, my + U * 8.4f, b, U * 2.6f, WHITE, 1, 0.01f);
	snprintf(b, sizeof b, "load %.2f", s->load);
	sg_text(&fb, &FT, rx + U * 2, my + U * 13.2f, b, U * 2.0f, RGB(170, 170, 176), 1, 0.01f);
	foot_hints("ESC  BACK TO APPS");
	(void)t;
}

static void draw_about(Home *h)
{
	(void)h;
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	title_block("ABOUT", "SILVER OS", I_INFO);
	struct utsname u; char hn[64] = "silver", kv[96] = "";
#ifdef RENDER
	strcpy(kv, "Linux 6.18.63-silver"); strcpy(hn, "silver");
#else
	if (!uname(&u)) { snprintf(kv, sizeof kv, "%s %s", u.sysname, u.release); snprintf(hn, sizeof hn, "%s", u.nodename); }
#endif
	float lx = U * 6, y = U * 26;
	const char *k[5] = {"VERSION", "KERNEL", "COMPUTER", "BUILT", "INSTALL"};
	char v[5][120];
	snprintf(v[0], 120, "SilverOS 0.1.0 (Genesis)"); snprintf(v[1], 120, "%s", kv); snprintf(v[2], 120, "%s", hn);
	snprintf(v[3], 120, "from nothing, from source"); snprintf(v[4], 120, "Open Terminal, then run:  su -c silver-install");
	for (int i = 0; i < 5; i++) {
		cap_text(lx, y + i * U * 8.2f, k[i], U * 1.0f, RGB(150, 150, 156), 1, 0);
		sg_text(&fb, &FT, lx, y + i * U * 8.2f + U * 3.6f, v[i], U * 2.6f, WHITE, 1, 0.01f);
	}
	foot_hints("ESC  BACK TO APPS");
}

static float ui_t;                              /* seconds on the home screen clock */
#include "apps.h"
#include "wifi.h"
#include "bt.h"
#include "tz.h"

/* ---------------------------------------------------------------- settings */
static void draw_settings(Home *h, float t)
{
	if (h->sub == 1) { draw_network(h, t); return; }
	if (h->sub == 2) { draw_bluetooth(h, t); return; }
	if (h->sub == 3) { draw_time(h, t); return; }
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	title_block("SETTINGS", "NETWORK  BLUETOOTH  TIME  SOUND  POWER", I_SETTINGS);
	char net[80]; net_text(net, sizeof net);
	if (t - wf.last_poll > 3 || !wf.last_poll) wf_poll(t);
	if (wf.ssid[0]) snprintf(net, sizeof net, "Wi-Fi  %.32s", wf.ssid);
	else if (!strcmp(net, "not connected") && wf.adapters) snprintf(net, sizeof net, "Not connected  -  ENTER to pick Wi-Fi");
	char disp[48]; snprintf(disp, sizeof disp, "%d x %d", W * scale, H * scale);
	static const char *name[10] = {"Network", "Bluetooth", "Display", "Time", "Calm mode", "Sound effects", "About", "Windows", "Restart", "Shut down"};
	static const int ic[10] = {I_NET, I_SOUND, I_SCREEN, I_INFO, I_SETTINGS, I_SOUND, I_INFO, I_PC, I_POWER, I_POWER};
	char val[10][80];
	snprintf(val[0], 80, "%s", net);
	{ int c = 0; for (int i = 0; i < bt.n; i++) c += bt.d[i].connected; if (c) snprintf(val[1], 80, "%d connected", c); else snprintf(val[1], 80, "Pair headphones, controllers ..."); }
	snprintf(val[2], 80, "%s", disp);
	snprintf(val[3], 80, "%.60s", ZONES[tzs.cur >= 0 ? tzs.cur : 0].name);
	snprintf(val[4], 80, "%s", h->calm_on ? "ON  -  no strobing or shaking" : "OFF");
	snprintf(val[5], 80, "%s", h->mute_on ? "OFF  -  muted" : "ON");
	snprintf(val[6], 80, "SilverOS 0.1.0");
	snprintf(val[7], 80, "%s", !win_ok() ? "Not installed next to SilverOS" : h->confirm == 7 ? "PRESS ENTER AGAIN TO RESTART INTO WINDOWS" : "Restart straight into Windows");
	snprintf(val[8], 80, "%s", h->confirm == 8 ? "PRESS ENTER AGAIN TO RESTART" : "Restart the computer");
	snprintf(val[9], 80, "%s", h->confirm == 9 ? "PRESS ENTER AGAIN TO SHUT DOWN" : "Turn the computer off");
	float lx = U * 6, lw = W - U * 12, ly = U * 20, rh = U * 5.2f;
	ui_frost(lx - U * 1.2f, ly - U * 1.2f, lw + U * 2.4f, 10 * (rh + U * 0.5f) + U * 1.6f, U * 2.2f, 0.55f);
	for (int i = 0; i < 10; i++) {
		float y = ly + i * (rh + U * 0.5f); int sel = i == h->row, danger = i >= 7;
		if (sel) { sg_rrect(&fb, lx, y, lw, rh, U * 1.4f, 0, WHITE, 0.95f); sg_rrect(&fb, lx + U * 0.7f, y + rh * 0.25f, U * 0.45f, rh * 0.5f, U * 0.22f, 0, RED, 1); }
		else if (i) sg_rect(&fb, lx + U * 3, y - U * 0.3f, lw - U * 6, sg_max(1, U * 0.08f), WHITE, 0.08f);
		uint32_t fg = sel ? INK : WHITE;
		if (i == 4 || i == 5) {                                              /* on/off switches */
			int on = i == 4 ? h->calm_on : !h->mute_on;
			float sw = U * 5.4f, shh = U * 2.8f, sx = lx + lw - U * 3 - sw, sy = y + rh / 2 - shh / 2;
			sg_rrect(&fb, sx, sy, sw, shh, shh / 2, 0, on ? RED : (sel ? RGB(170, 170, 176) : RGB(70, 70, 76)), 1);
			sg_disc(&fb, on ? sx + sw - shh / 2 : sx + shh / 2, sy + shh / 2, shh * 0.38f, WHITE, 1);
			icon(ic[i], lx + U * 4.2f, y + rh / 2, U * 2.0f, fg, 1);
			sg_text(&fb, &FT, lx + U * 9, y + rh / 2 + U * 0.9f, name[i], U * 2.6f, fg, 1, 0.01f);
			float vw = sg_text_w(&FT, val[i], U * 2.0f, 0.01f);
			sg_text(&fb, &FT, sx - U * 2 - vw, y + rh / 2 + U * 0.7f, val[i], U * 2.0f, sel ? RGB(70, 70, 78) : RGB(160, 160, 166), 1, 0.01f);
			continue;
		}
		icon(ic[i], lx + U * 4.2f, y + rh / 2, U * 2.0f, danger && !sel ? RGB(255, 120, 110) : fg, 1);
		sg_text(&fb, &FT, lx + U * 9, y + rh / 2 + U * 0.9f, name[i], U * 2.6f, fg, 1, 0.01f);
		uint32_t vc = sel ? RGB(70, 70, 78) : RGB(160, 160, 166);
		if (h->confirm == i && danger) vc = RED;
		float vw = sg_text_w(&FT, val[i], U * 2.0f, 0.01f);
		sg_text(&fb, &FT, lx + lw - U * 3 - vw, y + rh / 2 + U * 0.7f, val[i], U * 2.0f, vc, 1, 0.01f);
	}
	foot_hints("UP DOWN  MOVE      ENTER  CHANGE      ESC  HOME");
	(void)t;
}

/* ================================================================ the home screen itself */
static const char *TILE_NAME[4] = {"SETTINGS", "SEARCH", "APPS", "FILES"};
static const char *TILE_CAP[4] = {"NETWORK  DISPLAY  SOUND", "THE WEB  VIDEOS", "GAMES  MY PC", "BROWSE YOUR HOME"};
static const int TILE_ICON[4] = {I_SETTINGS, I_SEARCH, I_APPS, I_FILES};

static void tile_content(int q, float a, float blend, float beat)
{
	float cx = (q & 1) ? W * 0.75f : W * 0.25f, cy = (q & 2) ? H * 0.75f : H * 0.25f;
	uint32_t fg = sg_mix(WHITE, INK, blend);
	icon(TILE_ICON[q], cx, cy - U * 6.8f, U * 6.2f, fg, a);
	float sz = U * 7.6f, w = sg_text_w(&FD, TILE_NAME[q], sz, 0);
	sg_text(&fb, &FD, cx - w / 2 + sz * 0.03f, cy + U * 7.8f + sz * 0.03f, TILE_NAME[q], sz, BLACK, a * 0.35f * (1 - blend), 0);
	sg_text(&fb, &FD, cx - w / 2, cy + U * 7.8f, TILE_NAME[q], sz, fg, a, 0);
	float cw = sg_text_w(&FC, TILE_CAP[q], U * 1.2f, 0.22f);
	sg_text(&fb, &FC, cx - cw / 2, cy + U * 12.8f, TILE_CAP[q], U * 1.2f, sg_mix(RGB(170, 170, 176), RGB(64, 64, 72), blend), a, 0.22f);
	if (blend > 0.01f) {                                                  /* the chosen pane: a red tab and a beating dot */
		float cxp = (q & 1) ? W * 0.5f + U * 3 : U * 3, cyp = (q & 2) ? H * 0.5f + U * 3 : U * 3;
		(void)cxp; (void)cyp;
		sg_disc(&fb, cx, cy + U * 16.8f, U * (0.55f + 0.35f * beat), RED, a * blend);
		sg_glow(&fb, cx, cy + U * 16.8f, U * 3.4f, RED, 0.5f * a * blend * beat);
	}
}

/* open: 0 = glass not yet shown .. 1 = fully open */
static uint32_t *bk_dark, *bk_light;                /* the glass with the finished cracks baked in */
static void home_draw(const Home *h, float open, float t, float cracks_t)
{
	int n = W * H;
	float a = sg_smooth(open), beat = sg_heart(t, 0.9f);
	if (a >= 1 && cracks_t > 3) {
		/* after the intro nothing about the cracks changes: draw them once, then just copy */
		if (!bk_dark && (bk_dark = malloc((size_t)n * 4)) && (bk_light = malloc((size_t)n * 4))) {
			memcpy(bk_dark, bg_dark, (size_t)n * 4); memcpy(bk_light, bg_light, (size_t)n * 4);
			Surf sd = sg_surf(bk_dark, W, H), sl = sg_surf(bk_light, W, H);
			for (int i = 0; i < nck; i++) { crack_draw(&sd, &ck[i], 99, i < nmain ? 1.0f : 0.28f); crack_draw(&sl, &ck[i], 99, i < nmain ? 1.0f : 0.28f); }
			impact_draw(&sd, 99, 1); impact_draw(&sl, 99, 1);
		}
		if (bk_light) {
			int mode[4];                                       /* per pane: all dark, all light, or between */
			for (int q = 0; q < 4; q++) mode[q] = h->blend[q] < 0.002f ? 0 : h->blend[q] > 0.998f ? 1 : 2;
			if (mode[0] == mode[1] && mode[1] == mode[2] && mode[2] == mode[3] && mode[0] < 2) memcpy(fb.px, mode[0] ? bk_light : bk_dark, (size_t)n * 4);
			else for (int i = 0; i < n; i++) {
				int q = qmap[i];
				fb.px[i] = mode[q] == 0 ? bk_dark[i] : mode[q] == 1 ? bk_light[i] : sg_mix(bk_dark[i], bk_light[i], h->blend[q]);
			}
			goto tiles;
		}
	}
	if (a > 0.003f)
		for (int i = 0; i < n; i++) {
			uint32_t base = sg_mix(bg_dark[i], bg_light[i], h->blend[qmap[i]]);
			fb.px[i] = a >= 1 ? base : sg_mix(fb.px[i], base, a);
		}
	for (int i = 0; i < nck; i++) crack_draw(&fb, &ck[i], cracks_t, i < nmain ? 1.0f : 1 - 0.72f * a);
	impact_draw(&fb, cracks_t, 1);
tiles:;
	float ca = sg_sat((open - 0.35f) / 0.5f);
	if (ca > 0) for (int q = 0; q < 4; q++) tile_content(q, ca, h->blend[q], beat);
	glints_draw(&fb, t, sg_sat(cracks_t * 2));
	if (ca > 0.5f) {
		if (ca > 0.95f) foot_hints("ARROWS  select      ENTER  open");
	}
}
