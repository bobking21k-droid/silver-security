/*
 * silver-greeter -- the SilverOS login screen, drawn straight onto /dev/fb0 with sgfx.
 *
 * Intro (once per boot, ~7 s, any key skips):
 *   a marker scribbles the screen black -> "Silver OS" is written by hand in white ->
 *   a chrome pen slides in under it and snaps -> liquid silver leaks from the break and
 *   floods the screen, becoming the background -> the title settles bottom-right.
 * Then the lock screen: clock, date and a frosted sign-in card.
 *
 * Sign-in: an account with an empty password just needs Enter. Otherwise the password is
 * checked against /etc/shadow with crypt(3). Root may not sign in here (use su). On success
 * the session starts with `login -f <user>` on this tty. Without a usable framebuffer we
 * fall back to getty.
 *
 * Usage (from silver-login, as root, stdin = the tty):  silver-greeter tty1
 * Build with -DRENDER for an offline renderer:  silver-greeter-render out.mp4 [w h]
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <poll.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#ifndef RENDER
#include <crypt.h>
#endif

#include "../common/fonts.h"
#include "../common/sgfx.h"

#define WHITE RGB(255, 255, 255)
#define BLACK RGB(0, 0, 0)
#define INK RGB(14, 14, 16)
#define PAPER RGB(238, 238, 234)

static Font FD, FT;
static int W, H, scale = 1, fb_w, fb_h, stride;
static float U;                               /* 1% of the screen height */
static uint32_t *fbmem, *ink_bg, *canvas, *card_bg;
static uint8_t *edge;                         /* ragged-edge noise for the ink front */
static Surf fb;
static int tty_fd = 0;
static struct termios saved_tio;
static const char *tty_name = "tty1";

/* ---------------------------------------------------------------- time and output */
#ifdef RENDER
static double fake_t;
static FILE *outpipe;
static double now(void) { return fake_t; }
static void flip(void) { fwrite(fb.px, 4, (size_t)W * H, outpipe); fake_t += 1.0 / 30; }
static int key_waiting(void) { return 0; }
#else
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static void flip(void)
{
	if (scale == 1) { for (int y = 0; y < H; y++) memcpy(fbmem + y * stride, fb.px + y * W, (size_t)W * 4); return; }
	for (int y = 0; y < H; y++) {                 /* very high resolution panels: show doubled */
		uint32_t *src = fb.px + y * W, *d0 = fbmem + (2 * y) * stride, *d1 = d0 + stride;
		for (int x = 0; x < W; x++) { uint32_t c = src[x]; d0[2 * x] = d0[2 * x + 1] = d1[2 * x] = d1[2 * x + 1] = c; }
	}
}
static int key_waiting(void) { struct pollfd p = {tty_fd, POLLIN, 0}; return poll(&p, 1, 0) > 0; }
#endif

static void restore_tty(void)
{
	ioctl(tty_fd, KDSETMODE, KD_TEXT);
	tcsetattr(tty_fd, TCSANOW, &saved_tio);
	if (write(tty_fd, "\033[2J\033[H\033[?25h", 14) < 0) { /* best effort */ }
}
static void fallback_getty(void) { execl("/sbin/getty", "getty", "38400", tty_name, (char *)NULL); _exit(1); }

static int buffers(void)
{
	U = H / 100.0f;
	fb = sg_surf(malloc((size_t)W * H * 4), W, H);
	ink_bg = malloc((size_t)W * H * 4); canvas = malloc((size_t)W * H * 4); edge = malloc((size_t)W * H);
	return fb.px && ink_bg && canvas && edge && !sg_font_load(&FD, font_display) && !sg_font_load(&FT, font_text) ? 0 : -1;
}
static int fb_open(void)
{
	int fd = open("/dev/fb0", O_RDWR);
	if (fd < 0) return -1;
	struct fb_var_screeninfo v;
	struct fb_fix_screeninfo f;
	if (ioctl(fd, FBIOGET_VSCREENINFO, &v) || ioctl(fd, FBIOGET_FSCREENINFO, &f) || v.bits_per_pixel != 32) { close(fd); return -1; }
	fb_w = v.xres; fb_h = v.yres; stride = f.line_length / 4;
	scale = fb_w > 2100 ? 2 : 1;
	W = fb_w / scale; H = fb_h / scale;
	fbmem = mmap(NULL, (size_t)f.line_length * v.yres, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	return fbmem == MAP_FAILED ? -1 : buffers();
}

/* ================================================================ liquid silver */
static float bilerp(const float *g, int gw, int step, int x, int y)
{
	int i = x / step, j = y / step;
	float fx = (float)(x - i * step) / step, fy = (float)(y - j * step) / step;
	const float *p = g + j * gw + i;
	return (p[0] * (1 - fx) + p[1] * fx) * (1 - fy) + (p[gw] * (1 - fx) + p[gw + 1] * fx) * fy;
}

/* The background is a height field of slow, swirling metal, lit from the upper left:
 * broad soft highlights, darker troughs, and thin bright specular ridges. */
static void build_ink(void)
{
	const int st = 4, gw = W / st + 3, gh = H / st + 3;
	float *hgt = malloc(sizeof(float) * gw * gh), *eg = malloc(sizeof(float) * gw * gh);
	float sc = 1.15f / H;
	for (int j = 0; j < gh; j++)
		for (int i = 0; i < gw; i++) {
			float x = i * st * sc, y = j * st * sc;
			float qx = sg_fbm(x, y, 3), qy = sg_fbm(x + 5.2f, y + 1.3f, 3);
			float rx = sg_fbm(x + 3.0f * qx + 1.7f, y + 3.0f * qy + 9.2f, 3), ry = sg_fbm(x + 3.0f * qx + 8.3f, y + 3.0f * qy + 2.8f, 3);
			hgt[j * gw + i] = sg_fbm(x + 2.4f * rx, y + 2.4f * ry, 3);
			eg[j * gw + i] = sg_fbm(x * 10, y * 10, 4);
		}
	V3 L = v3_norm(v3(-0.45f, -0.62f, 0.64f)), Hv = v3_norm(v3(-0.45f, -0.62f, 1.64f));
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float h0 = bilerp(hgt, gw, st, x, y), hx = bilerp(hgt, gw, st, x + st, y) - h0, hy = bilerp(hgt, gw, st, x, y + st) - h0;
			V3 n = v3_norm(v3(-hx * 46, -hy * 46, 1));
			float diff = v3_dot(n, L), spec = v3_dot(n, Hv);
			spec = spec > 0 ? spec : 0; spec *= spec; spec *= spec; spec *= spec; spec *= spec;      /* ^16 */
			float band = sg_abs(sg_sin(h0 * 16));                                                     /* faint flow lines */
			float v = 0.34f + 0.46f * diff + 0.46f * spec - 0.035f * (1 - band) * (1 - band) + 0.16f * (h0 - 0.5f);
			float vig = 1 - 0.20f * (((x - W * .5f) / W) * ((x - W * .5f) / W) + ((y - H * .5f) / H) * ((y - H * .5f) / H)) * 4;
			int g = (int)(sg_sat(v * vig) * 255 + (((sg_hash(x, y) >> 9) & 3) - 1.5f));
			g = g < 0 ? 0 : g > 255 ? 255 : g;
			ink_bg[y * W + x] = RGB(g, g, g + 5 > 255 ? 255 : g + 5);
			edge[y * W + x] = (uint8_t)sg_clamp(bilerp(eg, gw, st, x, y) * 255, 0, 255);
		}
	free(hgt); free(eg);
}

/* ================================================================ handwriting */
/* Each letter is one or more pen strokes through control points (x right, y up; x-height 1,
 * capitals 2), smoothed with Catmull-Rom splines. 99 ends a stroke, 100 ends the letter. */
static const float L_S[] = {1.02f, 1.62f, 0.80f, 1.95f, 0.42f, 1.98f, 0.14f, 1.66f, 0.26f, 1.24f, 0.66f, 0.98f, 0.98f, 0.62f, 0.88f, 0.18f, 0.48f, -0.02f, 0.06f, 0.26f, 99, 100};
static const float L_i[] = {0.06f, 0.98f, 0.10f, 0.34f, 0.22f, 0.02f, 0.46f, 0.16f, 99, 0.08f, 1.46f, 0.11f, 1.52f, 99, 100};
static const float L_l[] = {0.06f, 2.02f, 0.08f, 0.36f, 0.20f, 0.02f, 0.44f, 0.16f, 99, 100};
static const float L_v[] = {0.00f, 1.00f, 0.14f, 0.52f, 0.32f, 0.02f, 0.52f, 0.52f, 0.70f, 1.02f, 99, 100};
static const float L_e[] = {0.10f, 0.50f, 0.52f, 0.56f, 0.70f, 0.80f, 0.46f, 1.02f, 0.16f, 0.82f, 0.08f, 0.40f, 0.30f, 0.03f, 0.74f, 0.20f, 99, 100};
static const float L_r[] = {0.06f, 1.02f, 0.10f, 0.50f, 0.10f, 0.00f, 99, 0.10f, 0.58f, 0.30f, 0.92f, 0.62f, 0.98f, 99, 100};
static const float L_O[] = {0.70f, 1.94f, 0.30f, 1.82f, 0.06f, 1.04f, 0.22f, 0.26f, 0.62f, 0.00f, 1.00f, 0.30f, 1.14f, 1.00f, 0.96f, 1.74f, 0.58f, 1.99f, 0.34f, 1.76f, 99, 100};
static const struct { const float *pts; float adv; } TITLE[] = {{L_S, 1.34f}, {L_i, 0.62f}, {L_l, 0.60f}, {L_v, 0.92f}, {L_e, 0.96f}, {L_r, 0.84f}, {0, 0.62f}, {L_O, 1.46f}, {L_S, 1.10f}};
#define TITLE_N 9
#define TITLE_W 8.46f
#define MAXSEG 900
static struct { V2 a, b; float len, w; } seg[MAXSEG];   /* the title as short straight pieces, in letter units */
static int nseg;
static float seg_total;

static void build_title(void)
{
	float ox = 0;
	for (int l = 0; l < TITLE_N; l++) {
		const float *p = TITLE[l].pts;
		while (p && *p != 100) {
			V2 c[16]; int n = 0;
			for (; *p != 99; p += 2) c[n++] = (V2){ox + p[0] + 0.16f * p[1], -p[1]};     /* slant; screen y points down */
			p++;
			for (int i = 0; i + 1 < n; i++) {
				V2 p0 = c[i ? i - 1 : 0], p1 = c[i], p2 = c[i + 1], p3 = c[i + 2 < n ? i + 2 : n - 1], prev = p1;
				for (int k = 1; k <= 8 && nseg < MAXSEG; k++) {
					float t = k / 8.f, t2 = t * t, t3 = t2 * t;
					V2 q = {0.5f * (2 * p1.x + (-p0.x + p2.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 + (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3),
						0.5f * (2 * p1.y + (-p0.y + p2.y) * t + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 + (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3)};
					float len = sg_sqrt((q.x - prev.x) * (q.x - prev.x) + (q.y - prev.y) * (q.y - prev.y));
					float along = (i + t) / (n - 1);                                   /* pressure: lighter at both ends of a stroke */
					seg[nseg].a = prev; seg[nseg].b = q; seg[nseg].len = len;
					seg[nseg].w = 0.72f + 0.38f * sg_sin(along * SG_PI);
					seg_total += len; nseg++; prev = q;
				}
			}
		}
		ox += TITLE[l].adv;
	}
}

/* Is this pixel under the ink yet? (r = current radius of the flood; r < 0: no ink anywhere) */
static float ink_r = -1, ink_x, ink_y;
static int inked(float x, float y)
{
	if (ink_r < 0) return 0;
	if (ink_r > 1e8f) return 1;
	int xi = (int)sg_clamp(x, 0, W - 1), yi = (int)sg_clamp(y, 0, H - 1);
	float dx = x - ink_x, dy = (y - ink_y) * 1.15f;
	return sg_sqrt(dx * dx + dy * dy) + (edge[yi * W + xi] - 128) * 0.9f < ink_r;
}

/* Draw the first `amount` (0..1) of the title centred on (cx, cy), `u` pixels per letter unit.
 * Each piece is white on black and turns dark the moment the silver reaches it. */
static void title(float cx, float cy, float u, float amount)
{
	float left = cx - TITLE_W * u / 2, top = cy + u, lw = sg_max(1.6f, u * 0.17f), want = seg_total * amount, done = 0;
	for (int i = 0; i < nseg && done < want; i++) {
		float f = sg_min(1, (want - done) / (seg[i].len + 1e-6f));
		V2 a = {left + seg[i].a.x * u, top + seg[i].a.y * u};
		V2 b = {left + (seg[i].a.x + (seg[i].b.x - seg[i].a.x) * f) * u, top + (seg[i].a.y + (seg[i].b.y - seg[i].a.y) * f) * u};
		sg_line(&fb, a, b, lw * seg[i].w, inked((a.x + b.x) / 2, (a.y + b.y) / 2) ? INK : WHITE, 1);
		done += seg[i].len;
	}
}

/* ================================================================ the pen */
static V2 rot(V2 o, float c, float s, float x, float y) { return (V2){o.x + x * c - y * s, o.y + x * s + y * c}; }

/* a tube from x0 to x1 (half-thickness t0 -> t1), shaded like polished metal */
static void tube(V2 o, float c, float s, float x0, float x1, float t0, float t1, uint32_t dark, uint32_t light, float a)
{
	enum { N = 9 };
	for (int j = 0; j < N; j++) {
		float v0 = -1 + 2.f * j / N, v1 = -1 + 2.f * (j + 1) / N, v = (v0 + v1) / 2;
		float hl = 1 - (v + 0.42f) * (v + 0.42f) / 0.10f; hl = hl > 0 ? hl : 0;                    /* highlight line */
		float rim = 1 - (v - 0.62f) * (v - 0.62f) / 0.05f; rim = rim > 0 ? rim * 0.35f : 0;         /* reflected floor */
		float sh = sg_sat(0.30f + 0.38f * (1 - v * v) + 0.75f * hl + rim - 0.22f * sg_sat(v));
		V2 q[4] = {rot(o, c, s, x0, v0 * t0), rot(o, c, s, x1, v0 * t1), rot(o, c, s, x1, v1 * t1), rot(o, c, s, x0, v1 * t0)};
		sg_poly(&fb, q, 4, sg_mix(dark, light, sh), a, 0.5f);
	}
}

/* snap: 0 = whole, 1 = broken open. (cx, cy) is the break point. */
static void pen(float cx, float cy, float L, float snap, float a)
{
	float k = sg_ease_out(snap), ang = 0.50f * k, th = L * 0.036f, half = L / 2, gap = L * 0.035f * k, sag = L * 0.07f * k;
	const uint32_t d = RGB(58, 60, 68), l = RGB(252, 252, 255), gd = RGB(18, 18, 22), gl = RGB(120, 122, 132);
	/* cap half (left) */
	V2 o = {cx - gap, cy + sag}; float c = sg_cos(ang), s = sg_sin(ang);
	tube(o, c, s, -half * 0.985f, 0, th, th, d, l, a);
	tube(o, c, s, -half, -half * 0.985f, th * 0.72f, th, d, l, a);                                  /* rounded end */
	tube(o, c, s, -half * 0.62f, -half * 0.585f, th * 1.08f, th * 1.08f, gd, gl, a);                 /* band */
	{ V2 q[4] = {rot(o, c, s, -half * 0.94f, -th * 1.55f), rot(o, c, s, -half * 0.36f, -th * 1.55f), rot(o, c, s, -half * 0.36f, -th * 1.2f), rot(o, c, s, -half * 0.94f, -th * 1.2f)};
	  sg_poly(&fb, q, 4, RGB(214, 216, 224), a, 0);                                                 /* clip */
	  V2 r[4] = {rot(o, c, s, -half * 0.94f, -th * 1.55f), rot(o, c, s, -half * 0.88f, -th * 1.55f), rot(o, c, s, -half * 0.88f, -th * 0.9f), rot(o, c, s, -half * 0.94f, -th * 0.9f)};
	  sg_poly(&fb, r, 4, RGB(214, 216, 224), a, 0); }
	for (int j = 0; j < 5 && snap > 0; j++) {                                                      /* torn metal at the break */
		float v = -1 + 0.4f * j;
		sg_tri(&fb, rot(o, c, s, 0, v * th), rot(o, c, s, th * (0.35f + 0.5f * ((j * 7) % 3) * 0.5f), (v + 0.2f) * th), rot(o, c, s, 0, (v + 0.4f) * th), sg_mix(d, l, 0.3f + 0.15f * j), a);
	}
	/* nib half (right) */
	o = (V2){cx + gap, cy + sag}; c = sg_cos(-ang); s = sg_sin(-ang);
	tube(o, c, s, 0, half * 0.70f, th, th, d, l, a);
	tube(o, c, s, half * 0.70f, half * 0.735f, th * 1.08f, th * 1.08f, gd, gl, a);                   /* grip ring */
	tube(o, c, s, half * 0.735f, half * 0.93f, th, th * 0.30f, d, l, a);                             /* taper */
	tube(o, c, s, half * 0.93f, half, th * 0.30f, th * 0.03f, gd, gl, a);                            /* tip */
	for (int j = 0; j < 5 && snap > 0; j++) {
		float v = -1 + 0.4f * j;
		sg_tri(&fb, rot(o, c, s, 0, v * th), rot(o, c, s, -th * (0.35f + 0.5f * ((j * 5 + 1) % 3) * 0.5f), (v + 0.2f) * th), rot(o, c, s, 0, (v + 0.4f) * th), sg_mix(d, l, 0.35f + 0.12f * j), a);
	}
}

/* ================================================================ intro */
static void corner_title(void) { float u = sg_max(4.2f, U * 1.55f); title(W - TITLE_W * u / 2 - U * 5.5f, H - U * 5.0f - u, u, 1); }

/* spread the silver to radius r around (ink_x, ink_y) over whatever is in the frame */
static void flood(float r)
{
	ink_r = r;
	float rin = r - 118, rout = r + 124;        /* the ragged edge moves the front by at most ~115 px */
	for (int y = 0; y < H; y++) {
		float dy = (y - ink_y) * 1.15f, oo = rout * rout - dy * dy;
		if (oo <= 0) continue;
		float wo = sg_sqrt(oo);
		int xo0 = (int)sg_max(ink_x - wo, 0), xo1 = (int)sg_min(ink_x + wo + 1, W), xi0 = xo0, xi1 = xo0;
		float ii = rin * rin - dy * dy;
		if (rin > 0 && ii > 0) {
			float wi = sg_sqrt(ii);
			xi0 = (int)sg_max(ink_x - wi, xo0); xi1 = (int)sg_min(ink_x + wi + 1, xo1);
			if (xi1 < xi0) xi1 = xi0;
		}
		uint32_t *row = fb.px + y * W, *src = ink_bg + y * W;
		for (int x = xo0; x < xo1; x++) {
			if (x == xi0 && xi1 > xi0) { memcpy(row + xi0, src + xi0, (size_t)(xi1 - xi0) * 4); x = xi1 - 1; continue; }
			float dx = x - ink_x, d = sg_sqrt(dx * dx + dy * dy) + (edge[y * W + x] - 128) * 0.9f, in = r - d;
			if (in > 0) row[x] = in < 9 ? sg_add(src[x], WHITE, 0.55f * (1 - in / 9)) : src[x];      /* bright meniscus at the front */
			else if (in > -7) row[x] = sg_mix(row[x], BLACK, 0.6f * (1 + in / 7));                   /* and its shadow */
		}
	}
}

#define T_PAPER 0.30f
#define T_SCRIB 1.35f
#define T_WRITE 2.05f
#define T_PEN 3.90f          /* pen slides in */
#define T_SNAP 4.60f
#define T_INK 4.80f
#define T_MOVE 6.65f
#define T_END 7.60f

static void intro(void)
{
	float bu = sg_min(W * 0.062f, H * 0.105f);               /* title size while it is being written */
	float eu = sg_max(4.2f, U * 1.55f);
	float tcx = W / 2.0f, tcy = H * 0.36f, px = W / 2.0f, py = H * 0.63f, PL = sg_min(W * 0.50f, H * 0.82f);
	float maxr = sg_sqrt((float)W * W / 4 + (float)H * H * 0.75f) + 140;
	static struct { float vx, vy, spin, size; } chip[14];
	for (int i = 0; i < 14; i++) chip[i] = (typeof(chip[0])){(sg_frand() - .5f) * 2.2f, -0.4f - sg_frand() * 1.5f, (sg_frand() - .5f) * 14, 0.25f + sg_frand() * 0.75f};
	Surf cv = sg_surf(canvas, W, H);
	sg_clear(&cv, PAPER);
	/* the marker's path: a fast zigzag down the page */
	enum { Z1 = 11, Z2 = 15, ZN = Z1 + Z2 + 1 };
	V2 zig[ZN + 1];
	for (int k = 0; k <= Z1; k++)                             /* first pass: side to side, working down the page */
		zig[k] = (V2){(k & 1) ? W * (1.0f + sg_frand() * 0.08f) : -W * sg_frand() * 0.08f, -H * 0.10f + k * (H * 1.2f / Z1) + (sg_frand() - .5f) * H * 0.07f};
	for (int k = 0; k <= Z2; k++)                             /* second pass: up and down, working back across */
		zig[Z1 + 1 + k] = (V2){W * 1.08f - k * (W * 1.16f / Z2) + (sg_frand() - .5f) * W * 0.03f, (k & 1) ? -H * sg_frand() * 0.08f : H * (1.0f + sg_frand() * 0.08f)};
	float drawn = 0, zw = H * 0.105f;
	ink_r = -1; ink_x = px; ink_y = py + PL * 0.07f;
	double t0 = now();
	for (;;) {
		float t = (float)(now() - t0);
		if (t > T_END || key_waiting()) break;
		if (t < T_WRITE) {                                     /* the scribble eats the page */
			float want = sg_smooth((t - T_PAPER) / (T_SCRIB)) * ZN;
			while (drawn < want) {
				float nx = sg_min(want, drawn + 0.04f);
				int k = (int)drawn; float f0 = drawn - k, f1 = nx - k; if (f1 > 1) f1 = 1;
				V2 a = {zig[k].x + (zig[k + 1].x - zig[k].x) * f0, zig[k].y + (zig[k + 1].y - zig[k].y) * f0};
				V2 b = {zig[k].x + (zig[k + 1].x - zig[k].x) * f1, zig[k].y + (zig[k + 1].y - zig[k].y) * f1};
				float wob = sg_noise(drawn * 9, 3.3f);
				a.y += (wob - .5f) * H * 0.02f; b.y += (wob - .5f) * H * 0.02f;
				sg_line(&cv, a, b, zw * (0.78f + 0.5f * wob), INK, 1);
				drawn = k + f1 >= k + 1 ? (float)(k + 1) : nx;
				if (drawn >= ZN) break;
			}
			memcpy(fb.px, canvas, (size_t)W * H * 4);
			float settle = sg_smooth((t - T_PAPER - T_SCRIB + 0.1f) / 0.3f);
			if (settle > 0) sg_rect(&fb, 0, 0, W, H, BLACK, settle);
		} else sg_clear(&fb, BLACK);

		float r = t > T_INK ? maxr * sg_ease_io((t - T_INK) / (T_MOVE - T_INK + 0.25f)) : -1;
		if (r > 0) flood(r); else ink_r = -1;

		/* the pen: slides in, snaps, falls away as the silver takes over */
		if (t > T_PEN && t < T_INK + 1.2f) {
			float in = sg_ease_out((t - T_PEN) / 0.45f), snap = (t - T_SNAP) / 0.22f, out = sg_smooth((t - T_INK - 0.5f) / 0.7f);
			float shake = (t > T_SNAP - 0.25f && t < T_SNAP) ? sg_sin(t * 90) * U * 0.35f * (t - T_SNAP + 0.25f) / 0.25f : 0;   /* it strains first */
			pen(px + (1 - in) * W * 0.5f + shake, py + out * H * 0.1f, PL, snap, (1 - out) * in);
			if (t > T_SNAP) {
				float dt = t - T_SNAP;
				for (int i = 0; i < 14; i++) {                         /* chips of metal */
					float x = px + chip[i].vx * dt * H * 0.5f, y = py + chip[i].vy * dt * H * 0.35f + dt * dt * H * 0.9f, sz = chip[i].size * U * 1.5f, a = chip[i].spin * dt;
					V2 o = {x, y}; float c = sg_cos(a), s = sg_sin(a);
					sg_tri(&fb, rot(o, c, s, -sz, -sz * 0.5f), rot(o, c, s, sz * 1.1f, -sz * 0.2f), rot(o, c, s, -sz * 0.2f, sz), RGB(226, 228, 236), sg_sat(1.6f - dt * 1.6f));
				}
				for (int i = 0; i < 18; i++) {                         /* drops of silver */
					float d2 = dt - 0.03f * (i % 6);
					if (d2 < 0) continue;
					float x = ink_x + (i % 7 - 3) * U * 0.7f + (i % 5 - 2) * d2 * U * 9, y = ink_y + d2 * d2 * H * 1.5f + d2 * H * 0.12f;
					if (r > 0 && inked(x, y)) continue;
					sg_line(&fb, (V2){x, y - U * (1.5f + d2 * 8)}, (V2){x, y}, U * (0.5f + 0.25f * (i % 3)), RGB(226, 228, 236), 1);
				}
			}
		}
		/* the title: written on, later carried to its corner */
		float mv = sg_ease_io((t - T_MOVE) / (T_END - T_MOVE - 0.1f));
		float u = bu + (eu - bu) * mv;
		title(tcx + (W - TITLE_W * eu / 2 - U * 5.5f - tcx) * mv, tcy + (H - U * 5.0f - eu - tcy) * mv, u, sg_sat((t - T_WRITE - 0.1f) / 1.55f));
		flip();
	}
	ink_r = 1e9f;
}

/* ================================================================ lock screen */
static char user[33], pass[129];
static int field = 1;             /* 0 user name, 1 password */
static const char *status = "";
static int cw, ch, cx0, cy0;

static void bake_card(int simple)
{
	cw = (int)(U * 66); ch = (int)(U * (simple ? 18.5f : 27));
	cx0 = (W - cw) / 2; cy0 = (int)(H * 0.60f);
	free(card_bg);
	card_bg = malloc((size_t)cw * ch * 4);
	uint32_t *tmp = malloc((size_t)cw * ch * 4);
	for (int y = 0; y < ch; y++) memcpy(card_bg + y * cw, ink_bg + (cy0 + y) * W + cx0, (size_t)cw * 4);
	sg_blur(card_bg, tmp, cw, ch, (int)(U * 2.4f) + 1, 3);                 /* frosted glass */
	for (int i = 0; i < cw * ch; i++) card_bg[i] = sg_mix(card_bg[i], WHITE, 0.58f);
	free(tmp);
}

static void glass(float alpha, float dx)
{
	float r = U * 3.2f, x = cx0 + dx, y = cy0;
	sg_blob(&fb, x + cw / 2.0f, y + ch + U * 1.2f, cw * 0.56f, U * 5.5f, BLACK, 0.26f * alpha);       /* shadow */
	float hx = cw / 2.0f, hy = ch / 2.0f;
	for (int j = 0; j < ch; j++) {
		int py = cy0 + j;
		if (py < 0 || py >= H) continue;
		float qy = sg_abs(j + .5f - hy) - (hy - r);
		for (int i = 0; i < cw; i++) {
			int pxx = (int)x + i;
			if (pxx < 0 || pxx >= W) continue;
			float qx = sg_abs(i + .5f - hx) - (hx - r);
			float d = (qx > 0 && qy > 0) ? sg_sqrt(qx * qx + qy * qy) - r : sg_max(qx, qy) - r;
			float cov = .5f - d;
			if (cov > 0) fb.px[py * W + pxx] = sg_mix(fb.px[py * W + pxx], card_bg[j * cw + i], alpha * (cov > 1 ? 1 : cov));
		}
	}
	sg_rrect(&fb, x, y, cw, ch, r, sg_max(1.2f, U * 0.16f), WHITE, 0.75f * alpha);                      /* light edge */
}

static void clock_text(char *hm, char *date)
{
#ifdef RENDER
	strcpy(hm, "9:41"); strcpy(date, "Saturday, 14 November");
#else
	static const char *dn[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
	static const char *mn[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
	time_t t = time(NULL);
	struct tm tm;
	gmtime_r(&t, &tm);          /* the PC clock as it is set (local time on a Windows dual-boot machine) */
	snprintf(hm, 8, "%d:%02d", tm.tm_hour, tm.tm_min);
	snprintf(date, 40, "%s, %d %s", dn[tm.tm_wday], tm.tm_mday, mn[tm.tm_mon]);
#endif
}

static void avatar(float cx, float cy, float r, float a)
{
	sg_disc(&fb, cx, cy, r, INK, a);
	sg_ring(&fb, cx, cy, r * 0.56f, r * 0.17f, WHITE, a);
	sg_disc(&fb, cx, cy, r * 0.17f, WHITE, a);
}

/* alpha: fade-in of the UI; shake: sideways offset of the card; out: fade to black on the way in */
static void lock_frame(int simple, float alpha, float shake, float out)
{
	memcpy(fb.px, ink_bg, (size_t)W * H * 4);
	corner_title();
	char hm[8], date[40];
	clock_text(hm, date);
	float rise = (1 - sg_ease_out(alpha)) * U * 2;
	sg_text_c(&fb, &FD, W / 2.0f, H * 0.300f + rise, hm, U * 19, INK, alpha * 0.92f, -0.02f);
	sg_text_c(&fb, &FT, W / 2.0f, H * 0.300f + U * 6.4f + rise, date, U * 3.3f, RGB(24, 24, 28), alpha * 0.92f, 0.02f);
	glass(alpha, shake);
	float x = cx0 + shake, pulse = 0.5f + 0.5f * sg_sin((float)now() * 4.2f);
	if (simple) {
		float ar = U * 5.4f, cy = cy0 + ch / 2.0f;
		avatar(x + U * 10.2f, cy, ar, alpha);
		sg_text(&fb, &FD, x + U * 18.6f, cy - U * 0.2f, user, U * 5.8f, INK, alpha, -0.01f);
		sg_text(&fb, &FT, x + U * 18.8f, cy + U * 4.0f, "Press Enter to sign in", U * 2.3f, RGB(60, 60, 68), alpha, 0.02f);
		float pw = sg_text_w(&FT, "Enter", U * 2.3f, 0.06f) + U * 5.4f, ph = U * 6.4f, bx = x + cw - pw - U * 6.0f;
		sg_rrect(&fb, bx, cy - ph / 2, pw, ph, ph / 2, 0, INK, alpha * (0.86f + 0.14f * pulse));
		sg_text(&fb, &FT, bx + U * 2.7f, cy + U * 0.82f, "Enter", U * 2.3f, WHITE, alpha, 0.06f);
	} else {
		float ar = U * 3.6f, top = cy0 + U * 6.8f;
		avatar(x + U * 7.6f, top, ar, alpha);
		float nx = sg_text(&fb, &FD, x + U * 13.4f, top + U * 1.5f, user[0] ? user : "user name", U * 4.2f, user[0] ? INK : RGB(120, 120, 128), alpha, -0.01f);
		if (field == 0 && ((long)(now() * 2) & 1)) sg_rect(&fb, nx + U * 0.4f, top - U * 2.2f, sg_max(2, U * 0.3f), U * 4.2f, INK, alpha);
		float fx = x + U * 5.0f, fy = top + U * 6.2f, fw = cw - U * 10.0f, fh = U * 6.4f;
		sg_rrect(&fb, fx, fy, fw, fh, fh / 2, 0, WHITE, 0.55f * alpha);
		sg_rrect(&fb, fx, fy, fw, fh, fh / 2, sg_max(1.4f, U * 0.2f), field == 1 ? INK : RGB(120, 120, 128), alpha * (field == 1 ? 0.9f : 0.45f));
		size_t n = strlen(pass);
		if (!n) sg_text(&fb, &FT, fx + U * 3.0f, fy + fh / 2 + U * 0.7f, "Password", U * 2.0f, RGB(110, 110, 118), alpha, 0.02f);
		for (size_t k = 0; k < n && k < 36; k++) sg_disc(&fb, fx + U * 3.6f + k * U * 1.9f, fy + fh / 2, U * 0.62f, INK, alpha);
		if (field == 1 && ((long)(now() * 2) & 1)) sg_rect(&fb, fx + U * 3.0f + (n > 36 ? 36 : n) * U * 1.9f, fy + U * 1.6f, sg_max(2, U * 0.3f), fh - U * 3.2f, INK, alpha);
		sg_text(&fb, &FT, fx + U * 0.6f, fy + fh + U * 3.3f, status[0] ? status : "Enter to sign in  -  Tab to change user", U * 1.75f,
			status[0] ? RGB(150, 20, 26) : RGB(74, 74, 82), alpha, 0.02f);
	}
	if (out > 0) sg_rect(&fb, 0, 0, W, H, BLACK, out);
	flip();
}

/* ================================================================ accounts */
#ifdef RENDER
#define crypt(a, b) ((char *)0)
#endif
static int shadow_field(const char *u, char *out, size_t n)
{
	FILE *f = fopen("/etc/shadow", "r");
	char line[512];
	int found = 0;
	if (!f) return 0;
	size_t ul = strlen(u);
	while (fgets(line, sizeof line, f))
		if (!strncmp(line, u, ul) && line[ul] == ':') {
			char *h = line + ul + 1, *e = strchr(h, ':');
			if (e) *e = 0;
			snprintf(out, n, "%s", h);
			found = 1;
			break;
		}
	fclose(f);
	memset(line, 0, sizeof line);
	return found;
}
static int account_open(const char *u)          /* empty password, and not root */
{
	char h[256];
	struct passwd *pw = getpwnam(u);
	return pw && pw->pw_uid != 0 && shadow_field(u, h, sizeof h) && h[0] == 0;
}
static int check_password(const char *u, const char *p)
{
	char h[256];
	if (!shadow_field(u, h, sizeof h) || !h[0] || h[0] == '!' || h[0] == '*') return 0;
	const char *c = crypt(p, h);
	int ok = c && !strcmp(c, h);
	memset(h, 0, sizeof h);
	return ok;
}

static void start_session(int simple)
{
	memset(pass, 0, sizeof pass);
	for (int f = 1; f <= 8; f++) { lock_frame(simple, 1, 0, f / 8.f); usleep(16000); }   /* fade out */
	restore_tty();
	execl("/bin/login", "login", "-f", user, (char *)NULL);
	_exit(1);
}

static void lock_loop(void)
{
	snprintf(user, sizeof user, "silver");
	int simple = account_open(user);
	bake_card(simple);
	double t0 = now();
	for (;;) {
		float a = sg_sat((float)(now() - t0) / 0.6f);
		lock_frame(simple, a, 0, 0);
		struct pollfd p = {tty_fd, POLLIN, 0};
		if (poll(&p, 1, a < 1 ? 16 : 120) <= 0) continue;
		char c;
		if (read(tty_fd, &c, 1) != 1) continue;
		if (c == '\033') { char junk[8]; usleep(2000); if (read(tty_fd, junk, sizeof junk) < 0) { /* drop arrows etc. */ } continue; }
		if (simple) { if (c == '\r' || c == '\n') start_session(1); continue; }
		char *dst = field ? pass : user;
		size_t cap = field ? sizeof pass - 1 : sizeof user - 1, n = strlen(dst);
		if (c == '\t') { field ^= 1; continue; }
		if (c == 127 || c == 8) { if (n) dst[n - 1] = 0; continue; }
		if (c == 21) { dst[0] = 0; continue; }
		if (c == '\r' || c == '\n') {
			if (field == 0) { field = 1; if (account_open(user)) { simple = 1; bake_card(1); } continue; }
			struct passwd *pw = getpwnam(user);
			if (pw && pw->pw_uid == 0) status = "root cannot sign in here - use su";
			else if (pw && check_password(user, pass)) start_session(0);
			else status = "Wrong user name or password";
			memset(pass, 0, sizeof pass);
			for (int k = 0; k < 14; k++) { lock_frame(0, 1, sg_sin(k * 1.9f) * (14 - k) * U * 0.22f, 0); usleep(16000); }
			sleep(1);                                               /* slow down guessing */
			tcflush(tty_fd, TCIFLUSH);
			continue;
		}
		if (c >= 32 && c < 127 && n < cap) { dst[n] = c; dst[n + 1] = 0; status = ""; }
	}
}

#ifdef RENDER
int main(int argc, char **argv)
{
	W = argc > 2 ? atoi(argv[2]) : 1280; H = argc > 3 ? atoi(argv[3]) : 800;
	if (buffers()) return 1;
	char cmd[512];
	snprintf(cmd, sizeof cmd, "ffmpeg -v error -y -f rawvideo -pix_fmt bgr0 -s %dx%d -r 30 -i - -c:v libx264 -crf 16 -preset medium -pix_fmt yuv420p '%s'",
		 W, H, argc > 1 ? argv[1] : "greeter.mp4");
	outpipe = popen(cmd, "w");
	build_ink(); build_title();
	intro();
	snprintf(user, sizeof user, "silver");
	int simple = !(argc > 4);
	bake_card(simple);
	if (!simple) strcpy(pass, "12345678");
	for (int f = 0; f < 110; f++) lock_frame(simple, sg_sat(f / 18.f), 0, 0);
	for (int f = 1; f <= 8; f++) lock_frame(simple, 1, 0, f / 8.f);
	pclose(outpipe);
	return 0;
}
#else
int main(int argc, char **argv)
{
	if (argc > 1) tty_name = argv[1];
	if (fb_open() < 0) fallback_getty();
	tcgetattr(tty_fd, &saved_tio);
	struct termios raw = saved_tio;
	raw.c_lflag &= ~(ICANON | ECHO | ISIG);
	raw.c_iflag &= ~(ICRNL | IXON);
	raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
	tcsetattr(tty_fd, TCSANOW, &raw);
	ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);            /* stop the text console drawing over us */
	sg_clear(&fb, BLACK); flip();
	build_ink(); build_title();
	/* the full intro plays once per boot; later sign-ins go straight to the lock screen */
	if (mkdir("/run/silver-greeter", 0700) == 0) intro();
	ink_r = 1e9f;
	tcflush(tty_fd, TCIFLUSH);
	lock_loop();
	return 0;
}
#endif
