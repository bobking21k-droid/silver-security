/*
 * silver-greeter -- the SilverOS login screen, drawn straight onto /dev/fb0 with sgfx.
 *
 * Intro (once per boot, ~6.5 s, any key skips):
 *   black -> a red dot beats like a heart -> "SILVER OS" slams in, letter by letter ->
 *   a chrome pen rises up the middle, strains, and SNAPS -> from the break, red spurts out in
 *   heartbeat pulses (lub-dub, lub-dub), each pulse surging the red across the screen until it
 *   is the background -> a red flash, and the title settles bottom-right.
 * Then the lock screen: clock, date and a hard-edged sign-in card.
 *
 * Sign-in: an account with an empty password just needs Enter. Otherwise the password is
 * checked against /etc/shadow with crypt(3). Root may not sign in here (use su). On success
 * the session starts with `login -f <user>` on this tty. Without a usable framebuffer we
 * fall back to getty.
 *
 * Add `silver.calm` to the kernel command line to turn off the shake and flashes.
 *
 * Usage (from silver-login, as root, stdin = the tty):  silver-greeter tty1
 * Build with -DRENDER for an offline renderer:  silver-greeter-render out.mp4 [w h [password]]
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
#define INK RGB(10, 10, 12)
#define BLOOD RGB(232, 16, 24)
#define HOT RGB(255, 62, 46)

static Font FD, FT, FC;                       /* Silver Scrawl, Inter, Michroma */
static int W, H, scale = 1, fb_w, fb_h, stride, calm;
static float U;                               /* 1% of the screen height */
static uint32_t *fbmem, *ink_bg, *canvas;
static uint8_t *edge;                         /* ragged-edge noise for the red's advancing front */
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
	return fb.px && ink_bg && canvas && edge && !sg_font_load(&FD, font_scrawl) && !sg_font_load(&FT, font_text) && !sg_font_load(&FC, font_caption) ? 0 : -1;
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

/* ================================================================ the red */
static float bilerp(const float *g, int gw, int step, int x, int y)
{
	int i = x / step, j = y / step;
	float fx = (float)(x - i * step) / step, fy = (float)(y - j * step) / step;
	const float *p = g + j * gw + i;
	return (p[0] * (1 - fx) + p[1] * fx) * (1 - fy) + (p[gw] * (1 - fx) + p[gw + 1] * fx) * fy;
}
static float step_at(float v, float th) { return sg_smooth((v - th) / 0.04f + 0.5f); }

/* The background is a height field of slow, swirling liquid, lit from the upper left, then
 * posterized into hard bands: black, dark red, red, hot red and a pale glint. */
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
			float v = 0.30f + 0.50f * diff + 0.50f * spec + 0.20f * (h0 - 0.5f);
			float dx = (x - W * .5f) / W, dy = (y - H * .5f) / H;
			v -= 0.34f * (dx * dx + dy * dy) * 4;                                                      /* dark corners */
			uint32_t c = BLACK;
			c = sg_mix(c, RGB(74, 3, 8), step_at(v, 0.24f));
			c = sg_mix(c, RGB(168, 8, 16), step_at(v, 0.44f));
			c = sg_mix(c, HOT, step_at(v, 0.70f));
			c = sg_mix(c, RGB(255, 214, 196), step_at(v, 0.96f));
			int g = (int)(((sg_hash(x, y) >> 9) & 7) - 3);                                             /* film grain */
			int r = (int)((c >> 16) & 255) + g, gg = (int)((c >> 8) & 255) + g / 2, b = (int)(c & 255) + g / 2;
			ink_bg[y * W + x] = RGB(r < 0 ? 0 : r > 255 ? 255 : r, gg < 0 ? 0 : gg > 255 ? 255 : gg, b < 0 ? 0 : b > 255 ? 255 : b);
			edge[y * W + x] = (uint8_t)sg_clamp(bilerp(eg, gw, st, x, y) * 255, 0, 255);
		}
	free(hgt); free(eg);
}

/* ================================================================ the title */
static const char *TITLE = "SILVER OS";
static float title_w(float size) { return sg_text_w(&FD, TITLE, size, 0); }

/* Draw the title with its baseline at `by`, centred on cx. `t` is seconds since the first letter
 * began to slam in (a large t draws it settled). Each letter drops in oversized, with a red echo. */
static void title(float cx, float by, float size, float t, float alpha)
{
	float left = cx - title_w(size) / 2;
	char pre[16];
	for (int i = 0; TITLE[i]; i++) {
		float k = (t - i * 0.115f) / 0.17f;
		if (k <= 0) continue;
		memcpy(pre, TITLE, (size_t)i); pre[i] = 0;
		char one[2] = {TITLE[i], 0};
		float x = left + sg_text_w(&FD, pre, size, 0) + (i ? 0 : 0), wch = sg_text_w(&FD, one, size, 0);
		float sc = k >= 1 ? 1 : 1 + 0.55f * (1 - sg_ease_out(k)), a = sg_sat(k * 5) * alpha;
		float sz = size * sc, xd = x + wch / 2 - sg_text_w(&FD, one, sz, 0) / 2, yd = by + 0.36f * size * (sc - 1);
		if (k < 1.3f && !calm) sg_text(&fb, &FD, xd - U * 0.9f, yd, one, sz, BLOOD, a * sg_sat(1.3f - k), 0);     /* red echo */
		sg_text(&fb, &FD, xd + size * 0.025f, yd + size * 0.025f, one, sz, BLACK, a * 0.55f, 0);                  /* drop shadow */
		sg_text(&fb, &FD, xd, yd, one, sz, WHITE, a, 0);
	}
}

/* ================================================================ the pen (vertical) */
static V2 rot(V2 o, float c, float s, float x, float y) { return (V2){o.x + x * c - y * s, o.y + x * s + y * c}; }

/* a tube from x0 to x1 along the local x axis (half-thickness t0 -> t1), shaded like polished metal */
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

/* Standing pen, cap up, nib down. (cx, cy) is the break point. snap: 0 whole .. 1 broken.
 * recoil: extra separation, driven by the heartbeat. */
static void pen(float cx, float cy, float L, float snap, float recoil, float a)
{
	float k = sg_ease_out(snap), ang = 0.36f * k, th = L * 0.040f, half = L / 2;
	float gap = L * (0.012f + 0.020f * recoil) * k + L * 0.004f * k;
	const uint32_t d = RGB(58, 60, 68), l = RGB(252, 252, 255), gd = RGB(18, 18, 22), gl = RGB(120, 122, 132);
	/* cap half (up) */
	float t1 = 1.5707963f + ang;
	V2 o = {cx + gap * 0.3f, cy - gap}; float c = sg_cos(t1), s = sg_sin(t1);
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
	/* nib half (down) */
	t1 = 1.5707963f - ang;
	o = (V2){cx - gap * 0.3f, cy + gap}; c = sg_cos(t1); s = sg_sin(t1);
	tube(o, c, s, 0, half * 0.70f, th, th, d, l, a);
	tube(o, c, s, half * 0.70f, half * 0.735f, th * 1.08f, th * 1.08f, gd, gl, a);                   /* grip ring */
	tube(o, c, s, half * 0.735f, half * 0.93f, th, th * 0.30f, d, l, a);                             /* taper */
	tube(o, c, s, half * 0.93f, half, th * 0.30f, th * 0.03f, gd, gl, a);                            /* tip */
	for (int j = 0; j < 5 && snap > 0; j++) {
		float v = -1 + 0.4f * j;
		sg_tri(&fb, rot(o, c, s, 0, v * th), rot(o, c, s, -th * (0.35f + 0.5f * ((j * 5 + 1) % 3) * 0.5f), (v + 0.2f) * th), rot(o, c, s, 0, (v + 0.4f) * th), sg_mix(d, l, 0.35f + 0.12f * j), a);
	}
}

/* ================================================================ the heart */
#define HB 0.62f                      /* seconds between beats */
#define NBEAT 4

/* One jet: a stream of liquid thrown from (ox, oy) at angle `ang` and speed `v`, bending under
 * gravity. Only the part between its tail (where the pulse stopped feeding it) and its head
 * (the front, tau seconds after launch) exists, so each beat sends a surge of liquid out. */
static void jet(float ox, float oy, float ang, float v, float tau, float flow, float w0, float life)
{
	const float g = H * 1.9f, ca = sg_cos(ang), sa = sg_sin(ang);
	float tt = sg_max(0, tau - flow);
	enum { N = 20 };
	V2 pt[N + 1]; float wd[N + 1];
	for (int i = 0; i <= N; i++) {
		float u = tt + (tau - tt) * i / N, f = (float)i / N;
		pt[i] = (V2){ox + ca * v * u, oy + sa * v * u + 0.5f * g * u * u};
		wd[i] = w0 * (0.20f + 0.80f * f * f * (3 - 2 * f)) * (1 - 0.28f * u);                     /* thin tail, fat head */
	}
	for (int i = 1; i <= N; i++) sg_line(&fb, pt[i - 1], pt[i], wd[i] + U * 0.6f, BLACK, 0.5f * life);   /* dark rim, so it reads on red */
	for (int i = 1; i <= N; i++) sg_line(&fb, pt[i - 1], pt[i], wd[i], sg_mix(BLOOD, HOT, (float)i / N), life);
	for (int i = 1; i <= N; i++) sg_line(&fb, pt[i - 1], pt[i], wd[i] * 0.30f, RGB(255, 200, 180), 0.5f * life * (float)i / N);   /* wet highlight */
	sg_disc(&fb, pt[N].x, pt[N].y, wd[N] * 0.62f + U * 0.5f, BLACK, 0.5f * life);                 /* the leading drop */
	sg_disc(&fb, pt[N].x, pt[N].y, wd[N] * 0.62f, HOT, life);
}

/* Spurts from (ox, oy), like a heart that has been cut: each beat (lub) throws a fan of jets
 * up and out, the second thump (dub) a smaller one. t is seconds since the first lub. */
static void spurt(float t, float ox, float oy)
{
	for (int k = 0; k < NBEAT; k++)
		for (int d = 0; d < 2; d++) {
			float tau = t - k * HB - d * 0.21f, str = d ? 0.5f : 1.0f;
			if (tau < 0 || tau > 1.1f) continue;
			int n = d ? 5 : 9;
			float life = sg_sat((1.1f - tau) * 4) * sg_sat(tau * 25);
			for (int i = 0; i < n; i++) {
				uint32_t h = sg_hash(k * 7 + d, i * 13 + 5);
				float u1 = ((h & 255) / 255.f - .5f) * 0.5f, u2 = ((h >> 8) & 255) / 255.f, u3 = ((h >> 16) & 255) / 255.f;
				float ang = -SG_PI * (0.08f + 0.84f * (i + 0.5f + u1) / n);                         /* an even fan across the top */
				float v = H * (0.62f + 0.52f * u2) * (0.65f + 0.35f * str);
				jet(ox, oy, ang, v, tau, 0.40f * (0.7f + 0.3f * str), U * (2.5f + 2.3f * u3) * (0.55f + 0.45f * str), life);
			}
			for (int i = 0; i < 22 * (d ? 1 : 2); i++) {                                            /* a fine mist around the break */
				uint32_t h = sg_hash(k * 11 + d + 90, i * 17 + 3);
				float u1 = (h & 255) / 255.f, u2 = ((h >> 8) & 255) / 255.f;
				float ang = -SG_PI * (0.02f + 0.96f * u1), v = H * (0.25f + 0.85f * u2), g = H * 1.9f;
				V2 p = {ox + sg_cos(ang) * v * tau, oy + sg_sin(ang) * v * tau + 0.5f * g * tau * tau};
				sg_disc(&fb, p.x, p.y, U * (0.25f + 0.35f * u2), BLACK, 0.4f * life);
				sg_disc(&fb, p.x, p.y, U * (0.18f + 0.30f * u2), HOT, life);
			}
		}
}

/* ================================================================ intro */
static void shake_frame(float dx, float dy)
{
	if (dx < 1 && dx > -1 && dy < 1 && dy > -1) return;
	memcpy(canvas, fb.px, (size_t)W * H * 4);
	for (int y = 0; y < H; y++) {
		int sy = y - (int)dy; sy = sy < 0 ? 0 : sy >= H ? H - 1 : sy;
		for (int x = 0; x < W; x++) { int sx = x - (int)dx; sx = sx < 0 ? 0 : sx >= W ? W - 1 : sx; fb.px[y * W + x] = canvas[sy * W + sx]; }
	}
}

static float tsize;                /* title size */
static float corner_size(void) { return sg_max(18.0f, U * 4.6f); }
static void corner_title(float alpha)
{
	float sz = corner_size(), cx = W - title_w(sz) / 2 - U * 4.5f, by = H - U * 5.2f;
	sg_text(&fb, &FD, cx - title_w(sz) / 2 - U * 0.25f, by, TITLE, sz, BLOOD, alpha * 0.9f, 0);
	sg_text(&fb, &FD, cx - title_w(sz) / 2 + sz * 0.03f, by + sz * 0.03f, TITLE, sz, BLACK, alpha * 0.6f, 0);
	sg_text(&fb, &FD, cx - title_w(sz) / 2, by, TITLE, sz, WHITE, alpha, 0);
}

/* spread the red to radius r around (ink_x, ink_y) over whatever is in the frame */
static float ink_x, ink_y;
static void flood(float r)
{
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
			if (in > 0) row[x] = in < 7 ? sg_mix(src[x], HOT, 0.8f * (1 - in / 7)) : src[x];          /* a wet, bright rim at the front */
			else if (in > -6) row[x] = sg_mix(row[x], BLACK, 0.7f * (1 + in / 6));
		}
	}
}

#define T_DOT 0.35f
#define T_TITLE 1.15f
#define T_PEN 2.55f
#define T_SNAP 3.55f
#define T_FLASH 5.95f
#define T_END 6.55f

static void intro(void)
{
	float PL = sg_min(H * 0.60f, W * 0.62f), py = H * 0.615f, px = W / 2.0f;     /* the break point */
	tsize = sg_min(U * 14.5f, W * 0.84f / (title_w(1.0f)));
	float by = H * 0.205f + tsize * 0.40f;
	float maxr = sg_sqrt((float)W * W / 4 + (float)H * H) + 160;
	ink_x = px; ink_y = py;
	static struct { float vx, vy, spin, size; } chip[14];
	for (int i = 0; i < 14; i++) chip[i] = (typeof(chip[0])){(sg_frand() - .5f) * 2.2f, -0.4f - sg_frand() * 1.5f, (sg_frand() - .5f) * 14, 0.25f + sg_frand() * 0.75f};
	double t0 = now();
	for (;;) {
		float t = (float)(now() - t0);
		if (t > T_END || key_waiting()) break;
		sg_clear(&fb, BLACK);

		/* the red, surging with each beat once the pen has snapped */
		float p = 0;
		if (t > T_SNAP) {
			for (int k = 0; k < NBEAT; k++) p += sg_smooth((t - (T_SNAP + 0.05f + k * HB)) / 0.20f);
			p /= NBEAT;
		}
		float st = t - (T_SNAP + 0.05f);                         /* time since the first lub */
		if (p > 0) flood(maxr * p);

		/* the dot, beating in the dark, then lying under the pen */
		float hb = sg_heart(t - T_DOT, 0.82f);
		if (t > T_DOT && t < T_SNAP + 0.05f) {
			float grow = sg_smooth((t - T_DOT) / 0.3f), r = U * (0.9f + 0.9f * hb) * grow;
			sg_glow(&fb, px, py, U * (6 + 5 * hb), BLOOD, 0.55f * grow * (0.5f + 0.5f * hb));
			sg_disc(&fb, px, py, r, BLOOD, grow);
		}

		/* the pen: rises up the middle, strains, snaps */
		if (t > T_PEN) {
			float in = sg_ease_out((t - T_PEN) / 0.55f), snap = (t - T_SNAP) / 0.12f;
			float strain = (t > T_SNAP - 0.5f && t < T_SNAP) ? (t - T_SNAP + 0.5f) / 0.5f : 0;
			float shk = calm ? 0 : sg_sin(t * 110) * U * 0.30f * strain;                      /* it trembles before it goes */
			float fall = t > T_SNAP + 0.9f ? sg_smooth((t - T_SNAP - 0.9f) / 1.0f) : 0;
			float rec = sg_heart(t - (T_SNAP + 0.05f), HB);                                   /* each beat shoves the halves apart */
			float ox = px + shk, oy = py + (1 - in) * H * 0.62f + fall * fall * H * 0.5f;
			pen(ox, oy, PL, snap > 0 ? sg_sat(snap) : 0, t > T_SNAP ? rec : 0, (1 - fall) * sg_sat(in * 4));
			if (t > T_SNAP) {
				float dt = t - T_SNAP;
				for (int i = 0; i < 14; i++) {                                              /* chips of metal */
					float x = px + chip[i].vx * dt * H * 0.5f, y = py + chip[i].vy * dt * H * 0.35f + dt * dt * H * 0.9f, sz = chip[i].size * U * 1.5f, a = chip[i].spin * dt;
					V2 o = {x, y}; float c = sg_cos(a), s = sg_sin(a);
					sg_tri(&fb, rot(o, c, s, -sz, -sz * 0.5f), rot(o, c, s, sz * 1.1f, -sz * 0.2f), rot(o, c, s, -sz * 0.2f, sz), RGB(226, 228, 236), sg_sat(1.6f - dt * 1.6f));
				}
			}
		}
		if (st > 0) {
			sg_glow(&fb, px, py, U * 9, HOT, 0.65f * sg_heart(st, HB));                    /* the break glows with each beat */
			spurt(st, px, py);
		}

		/* the title: slams in, later carried to the corner */
		float mv = sg_ease_io((t - (T_FLASH - 0.7f)) / 0.9f);
		float csz = corner_size(), cx = W - title_w(csz) / 2 - U * 4.5f, cby = H - U * 5.2f;
		float sz = tsize + (csz - tsize) * mv;
		title(W / 2.0f + (cx - W / 2.0f) * mv, by + (cby - by) * mv, sz, t - T_TITLE, 1);

		if (t < T_TITLE + 1.2f)                                                            /* the notice, like a game's disclaimer */
			sg_text_c(&fb, &FC, W / 2.0f, H - U * 3.4f, "CONTAINS FLASHING LIGHTS  /  ANY KEY SKIPS", U * 0.9f, WHITE, 0.55f * sg_sat((T_TITLE + 1.2f - t) * 3) * sg_sat((t - 0.15f) * 4), 0.26f);

		/* a thump on every lub, a white flash on the snap, a red flash at the end */
		if (!calm) {
			float thump = st > 0 ? sg_heart(st, HB) : 0;
			shake_frame(sg_sin(t * 97) * U * 0.9f * thump, sg_cos(t * 83) * U * 0.9f * thump);
			if (t > T_SNAP && t < T_SNAP + 0.09f) sg_rect(&fb, 0, 0, W, H, WHITE, 0.85f * (1 - (t - T_SNAP) / 0.09f));
		}
		if (t > T_FLASH) sg_rect(&fb, 0, 0, W, H, BLOOD, (calm ? 0.5f : 0.8f) * sg_sat(1 - (t - T_FLASH) / 0.6f));
		flip();
	}
}

/* ================================================================ lock screen */
static char user[33], pass[129];
static int field = 1;             /* 0 user name, 1 password */
static const char *status = "";
static int cw, ch, cx0, cy0;

static void card_geom(int simple)
{
	cw = (int)(U * 66); ch = (int)(U * (simple ? 18.5f : 28));
	cx0 = (W - cw) / 2; cy0 = (int)(H * 0.60f);
}

static void red_text(float x, float y, const char *str, float size, float alpha, int centered)
{
	if (centered) x -= sg_text_w(&FD, str, size, 0) / 2;
	sg_text(&fb, &FD, x + size * 0.035f, y + size * 0.035f, str, size, BLACK, alpha * 0.6f, 0);
	sg_text(&fb, &FD, x - size * 0.03f, y, str, size, BLOOD, alpha * 0.9f, 0);
	sg_text(&fb, &FD, x, y, str, size, WHITE, alpha, 0);
}

static void clock_text(char *hm, char *date)
{
#ifdef RENDER
	strcpy(hm, "9:41"); strcpy(date, "SATURDAY  -  14 NOVEMBER");
#else
	static const char *dn[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
	static const char *mn[] = {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
	time_t t = time(NULL);
	struct tm tm;
	gmtime_r(&t, &tm);          /* the PC clock as it is set (local time on a Windows dual-boot machine) */
	snprintf(hm, 8, "%d:%02d", tm.tm_hour, tm.tm_min);
	snprintf(date, 48, "%s  -  %d %s", dn[tm.tm_wday], tm.tm_mday, mn[tm.tm_mon]);
#endif
}

/* alpha: fade-in of the UI; shake: sideways offset of the card; out: fade to black on the way in */
static void lock_frame(int simple, float alpha, float shake, float out)
{
	memcpy(fb.px, ink_bg, (size_t)W * H * 4);
	corner_title(alpha);
	char hm[8], date[48];
	clock_text(hm, date);
	float t = (float)now(), beat = sg_heart(t, 0.9f), rise = (1 - sg_ease_out(alpha)) * U * 2;
	red_text(W / 2.0f, H * 0.315f + rise, hm, U * 19.5f, alpha, 1);
	sg_text_c(&fb, &FC, W / 2.0f + U * 0.1f, H * 0.315f + U * 6.6f + rise + 1, date, U * 1.55f, BLACK, alpha * 0.6f, 0.26f);
	sg_text_c(&fb, &FC, W / 2.0f, H * 0.315f + U * 6.6f + rise, date, U * 1.55f, WHITE, alpha, 0.26f);

	float x = cx0 + shake, y = cy0, pb = U * 0.55f;
	sg_rect(&fb, x + U * 0.9f, y + U * 0.9f, cw, ch, BLACK, 0.5f * alpha);                       /* hard shadow */
	sg_rect(&fb, x, y, cw, ch, INK, 0.96f * alpha);
	sg_rect(&fb, x, y, cw, pb, BLOOD, alpha);                                                     /* red bar */
	sg_rect(&fb, x, y + ch - sg_max(1, U * 0.12f), cw, sg_max(1, U * 0.12f), WHITE, 0.25f * alpha);
	if (simple) {
		float ar = U * 5.0f, cy = y + ch / 2.0f + pb / 2;
		sg_glow(&fb, x + U * 10.0f, cy, ar * 2.4f, BLOOD, 0.35f * alpha * beat);
		sg_disc(&fb, x + U * 10.0f, cy, ar, BLOOD, alpha);                                       /* the mark: a beating red dot in a ring */
		sg_ring(&fb, x + U * 10.0f, cy, ar * 1.32f, sg_max(1.4f, U * 0.18f), WHITE, alpha);
		sg_disc(&fb, x + U * 10.0f, cy, ar * 0.30f, BLACK, alpha);
		red_text(x + U * 18.5f, cy - U * 0.2f, "silver", U * 6.0f, alpha, 0);
		sg_text(&fb, &FC, x + U * 18.8f, cy + U * 4.2f, "NO PASSWORD NEEDED", U * 1.05f, RGB(150, 150, 156), alpha, 0.24f);
		float lw = sg_text_w(&FC, "ENTER", U * 1.25f, 0.2f) + U * 5.2f, lh = U * 5.0f, bx = x + cw - lw - U * 6.0f;
		sg_glow(&fb, bx + lw / 2, cy, lw * 0.9f, BLOOD, 0.28f * alpha * beat);
		sg_rrect(&fb, bx, cy - lh / 2, lw, lh, lh / 2, 0, BLOOD, alpha);
		sg_text(&fb, &FC, bx + U * 2.6f, cy + U * 0.5f, "ENTER", U * 1.25f, WHITE, alpha, 0.2f);
	} else {
		float top = y + U * 8.2f;
		sg_disc(&fb, x + U * 7.6f, top, U * 3.4f, BLOOD, alpha);
		sg_ring(&fb, x + U * 7.6f, top, U * 4.5f, sg_max(1.4f, U * 0.18f), WHITE, alpha);
		float nx = sg_text(&fb, &FD, x + U * 14.0f, top + U * 2.0f, user[0] ? user : "user", U * 5.2f, user[0] ? WHITE : RGB(110, 110, 116), alpha, 0);
		if (field == 0 && ((long)(now() * 2) & 1)) sg_rect(&fb, nx + U * 0.5f, top - U * 2.6f, sg_max(2, U * 0.4f), U * 5.2f, WHITE, alpha);
		float fx = x + U * 5.0f, fy = top + U * 7.4f, fw = cw - U * 10.0f, fh = U * 6.6f;
		sg_rect(&fb, fx, fy, fw, fh, BLACK, 0.6f * alpha);
		sg_rrect(&fb, fx, fy, fw, fh, U * 0.3f, sg_max(1.4f, U * 0.2f), field == 1 ? WHITE : RGB(110, 110, 116), alpha * (field == 1 ? 1 : 0.5f));
		size_t n = strlen(pass);
		if (!n) sg_text(&fb, &FC, fx + U * 2.4f, fy + fh / 2 + U * 0.4f, "PASSWORD", U * 1.15f, RGB(120, 120, 126), alpha, 0.24f);
		for (size_t k = 0; k < n && k < 36; k++) sg_disc(&fb, fx + U * 3.4f + k * U * 1.9f, fy + fh / 2, U * 0.62f, BLOOD, alpha);
		if (field == 1 && ((long)(now() * 2) & 1)) sg_rect(&fb, fx + U * 2.6f + (n > 36 ? 36 : n) * U * 1.9f, fy + U * 1.5f, sg_max(2, U * 0.35f), fh - U * 3.0f, WHITE, alpha);
		sg_text(&fb, &FC, fx, fy + fh + U * 3.6f, status[0] ? status : "ENTER TO SIGN IN  /  TAB TO CHANGE USER", U * 0.98f,
			status[0] ? HOT : RGB(170, 170, 176), alpha, 0.22f);
	}
	sg_text_c(&fb, &FC, W / 2.0f, H - U * 3.4f, "-- PRESS ANY BUTTON TO CONTINUE --", U * 0.85f, RGB(190, 190, 196), 0.55f * alpha * (0.6f + 0.4f * sg_heart(t, 1.8f)), 0.3f);
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
	card_geom(simple);
	double t0 = now();
	for (;;) {
		float a = sg_sat((float)(now() - t0) / 0.5f);
		lock_frame(simple, a, 0, 0);
		struct pollfd p = {tty_fd, POLLIN, 0};
		if (poll(&p, 1, a < 1 ? 16 : 60) <= 0) continue;
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
			if (field == 0) { field = 1; if (account_open(user)) { simple = 1; card_geom(1); } continue; }
			struct passwd *pw = getpwnam(user);
			if (pw && pw->pw_uid == 0) status = "ROOT CANNOT SIGN IN HERE - USE SU";
			else if (pw && check_password(user, pass)) start_session(0);
			else status = "WRONG USER NAME OR PASSWORD";
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
	build_ink();
	intro();
	snprintf(user, sizeof user, "silver");
	int simple = !(argc > 4);
	card_geom(simple);
	if (!simple) strcpy(pass, "12345678");
	for (int f = 0; f < 110; f++) lock_frame(simple, sg_sat(f / 15.f), 0, 0);
	for (int f = 1; f <= 8; f++) lock_frame(simple, 1, 0, f / 8.f);
	pclose(outpipe);
	return 0;
}
#else
int main(int argc, char **argv)
{
	if (argc > 1) tty_name = argv[1];
	if (fb_open() < 0) fallback_getty();
	{
		char cmd[1024] = "";
		FILE *f = fopen("/proc/cmdline", "r");
		if (f) { if (fgets(cmd, sizeof cmd, f)) { /* read */ } fclose(f); }
		calm = strstr(cmd, "silver.calm") != NULL;
	}
	tcgetattr(tty_fd, &saved_tio);
	struct termios raw = saved_tio;
	raw.c_lflag &= ~(ICANON | ECHO | ISIG);
	raw.c_iflag &= ~(ICRNL | IXON);
	raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
	tcsetattr(tty_fd, TCSANOW, &raw);
	ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);            /* stop the text console drawing over us */
	sg_clear(&fb, BLACK); flip();
	build_ink();
	/* the full intro plays once per boot; later sign-ins go straight to the lock screen */
	if (mkdir("/run/silver-greeter", 0700) == 0) intro();
	tcflush(tty_fd, TCIFLUSH);
	lock_loop();
	return 0;
}
#endif
