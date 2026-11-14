/*
 * silver-greeter -- the SilverOS login screen, drawn straight onto /dev/fb0.
 *
 * Intro (first time per boot, ~6 s, any key skips):
 *   black -> "SILVER OS" scribbles itself in white -> a pen appears under it and snaps ->
 *   ink leaks from the break and spreads until it *is* the background ->
 *   the title slides to the bottom-right corner -> the login card fades in.
 *
 * Login: type user name, Enter, password, Enter. Checked against /etc/shadow with
 * crypt(3); on success the session starts via `login -f <user>` on this tty.
 * Root may not log in here (use su). If the framebuffer is unusable we fall back to getty.
 *
 * Usage (from silver-login, as root, stdin = the tty):  silver-greeter tty1
 */
#define _GNU_SOURCE
#ifndef RENDER
#include <crypt.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <math.h>
#include <poll.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "../common/font5x7.h"

static int W, H, stride;
static uint32_t *fbmem, *buf, *ink_bg;   /* screen, back buffer, finished ink background */
static uint8_t *edge;                     /* per-pixel ink edge noise (0..255) */
static int tty_fd = 0;
static struct termios saved_tio;
static const char *tty_name = "tty1";

#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define WHITE RGB(255, 255, 255)
#define BLACK RGB(0, 0, 0)

#ifdef RENDER   /* offline render for demo videos: a fake clock that advances 1/30 s per frame */
static double fake_t;
static double now(void) { return fake_t; }
#else
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
#endif
static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float smooth(float x) { x = clampf(x, 0, 1); return x * x * (3 - 2 * x); }

static uint32_t mix(uint32_t a, uint32_t b, float t)
{
	int ia = (int)(clampf(t, 0, 1) * 256);
	uint32_t rb = ((a & 0xff00ff) * (256 - ia) + (b & 0xff00ff) * ia) >> 8;
	uint32_t g = ((a & 0x00ff00) * (256 - ia) + (b & 0x00ff00) * ia) >> 8;
	return (rb & 0xff00ff) | (g & 0x00ff00);
}

/* ---------------------------------------------------------------- screen setup */
static void restore_tty(void)
{
	ioctl(tty_fd, KDSETMODE, KD_TEXT);
	tcsetattr(tty_fd, TCSANOW, &saved_tio);
	if (write(tty_fd, "\033[2J\033[H\033[?25h", 14) < 0) { /* best effort */ }
}

static void fallback_getty(void)
{
	execl("/sbin/getty", "getty", "38400", tty_name, (char *)NULL);
	_exit(1);
}

static int fb_open(void)
{
	int fd = open("/dev/fb0", O_RDWR);
	if (fd < 0) return -1;
	struct fb_var_screeninfo v;
	struct fb_fix_screeninfo f;
	if (ioctl(fd, FBIOGET_VSCREENINFO, &v) || ioctl(fd, FBIOGET_FSCREENINFO, &f) || v.bits_per_pixel != 32) {
		close(fd);
		return -1;
	}
	W = v.xres; H = v.yres; stride = f.line_length / 4;
	fbmem = mmap(NULL, f.line_length * v.yres, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (fbmem == MAP_FAILED) return -1;
	buf = malloc((size_t)W * H * 4);
	ink_bg = malloc((size_t)W * H * 4);
	edge = malloc((size_t)W * H);
	return buf && ink_bg && edge ? 0 : -1;
}

#ifdef RENDER
static FILE *outpipe;
static void flip(void) { fwrite(buf, 4, (size_t)W * H, outpipe); fake_t += 1.0 / 30; }
#else
static void flip(void)
{
	for (int y = 0; y < H; y++) memcpy(fbmem + y * stride, buf + y * W, (size_t)W * 4);
}
#endif

/* ---------------------------------------------------------------- drawing */
static void rect(float x, float y, float w, float h, uint32_t c, float a)
{
	int x0 = (int)fmaxf(x, 0), x1 = (int)fminf(x + w, W), y0 = (int)fmaxf(y, 0), y1 = (int)fminf(y + h, H);
	for (int j = y0; j < y1; j++) {
		uint32_t *r = buf + j * W;
		if (a >= 1) for (int i = x0; i < x1; i++) r[i] = c;
		else for (int i = x0; i < x1; i++) r[i] = mix(r[i], c, a);
	}
}

static void disc(float cx, float cy, float rad, uint32_t c, float a)
{
	for (int y = (int)fmaxf(cy - rad, 0); y < (int)fminf(cy + rad + 1, H); y++)
		for (int x = (int)fmaxf(cx - rad, 0); x < (int)fminf(cx + rad + 1, W); x++) {
			float d = hypotf(x + .5f - cx, y + .5f - cy);
			if (d < rad) buf[y * W + x] = mix(buf[y * W + x], c, a * clampf(rad - d, 0, 1));
		}
}

/* thick pen stroke: discs along the segment */
static void stroke(float x0, float y0, float x1, float y1, float w, uint32_t c, float a)
{
	float len = hypotf(x1 - x0, y1 - y0);
	int n = (int)(len / (w * 0.35f)) + 1;
	for (int i = 0; i <= n; i++) {
		float t = (float)i / n;
		disc(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, w / 2, c, a);
	}
}

typedef struct { float x, y; } V2;
static void tri(V2 a, V2 b, V2 c, uint32_t col)
{
	if (a.y > b.y) { V2 t = a; a = b; b = t; }
	if (a.y > c.y) { V2 t = a; a = c; c = t; }
	if (b.y > c.y) { V2 t = b; b = c; c = t; }
	for (int y = (int)fmaxf(a.y + .5f, 0); y < (int)fminf(c.y + .5f, H); y++) {
		float py = y + .5f;
		float xa = a.x + (c.x - a.x) * (py - a.y) / (c.y - a.y + 1e-6f);
		float xb = py < b.y ? a.x + (b.x - a.x) * (py - a.y) / (b.y - a.y + 1e-6f)
				    : b.x + (c.x - b.x) * (py - b.y) / (c.y - b.y + 1e-6f);
		if (xa > xb) { float t = xa; xa = xb; xb = t; }
		for (int x = (int)fmaxf(xa + .5f, 0); x < (int)fminf(xb + .5f, W); x++) buf[y * W + x] = col;
	}
}

static int gindex(char ch)
{
	if (ch >= 'a' && ch <= 'z') ch -= 32;
	for (int g = 0; glyphs[g]; g++) if (glyphs[g] == ch) return g;
	return -1;
}
static void text(float x, float y, const char *s, float px, uint32_t c, float a)
{
	for (; *s; s++, x += 6 * px) {
		int g = gindex(*s);
		if (g < 0) continue;
		for (int r = 0; r < 7; r++)
			for (int k = 0; k < 5; k++)
				if (font[g][r] & (16 >> k)) rect(x + k * px, y + r * px, px * 1.06f, px * 1.06f, c, a);
	}
}

/* ---------------------------------------------------------------- noise (ink texture) */
static uint32_t hash2(int x, int y) { uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16); }
static float vnoise(float x, float y)
{
	int xi = (int)floorf(x), yi = (int)floorf(y);
	float fx = x - xi, fy = y - yi, u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
	float a = (hash2(xi, yi) & 1023) / 1023.f, b = (hash2(xi + 1, yi) & 1023) / 1023.f;
	float c = (hash2(xi, yi + 1) & 1023) / 1023.f, d = (hash2(xi + 1, yi + 1) & 1023) / 1023.f;
	return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
static float fbm(float x, float y) { float s = 0, a = .5f; for (int i = 0; i < 5; i++) { s += a * vnoise(x, y); x *= 2.03f; y *= 2.03f; a *= .5f; } return s; }

/* Noise is evaluated on a coarse grid and bilinearly upsampled: the marbling is smooth
 * (1/4 resolution) and the ragged ink edge only needs 1/2. About 20x cheaper than per-pixel. */
static void grid_build(float *out, int gw, int gh, int step, float freq, float (*fn)(float, float))
{
	for (int j = 0; j < gh; j++)
		for (int i = 0; i < gw; i++)
			out[j * gw + i] = fn(i * step * freq, j * step * freq);
}
static float ink_fn(float x, float y)
{
	float qx = fbm(x, y), qy = fbm(x + 5.2f, y + 1.3f);
	return fbm(x + 3 * qx, y + 3 * qy);
}
static float edge_fn(float x, float y) { return fbm(x * 4, y * 4); }
static float bilerp(const float *g, int gw, int step, int x, int y)
{
	int i = x / step, j = y / step;
	float fx = (float)(x - i * step) / step, fy = (float)(y - j * step) / step;
	const float *p = g + j * gw + i;
	return (p[0] * (1 - fx) + p[1] * fx) * (1 - fy) + (p[gw] * (1 - fx) + p[gw + 1] * fx) * fy;
}

/* silver marbled ink: domain-warped noise with dark veins */
static void build_ink(void)
{
	float sc = 3.0f / H;
	int s4 = 4, gw4 = W / s4 + 2, gh4 = H / s4 + 2, s2 = 2, gw2 = W / s2 + 2, gh2 = H / s2 + 2;
	float *g4 = malloc(sizeof(float) * gw4 * gh4), *g2 = malloc(sizeof(float) * gw2 * gh2);
	grid_build(g4, gw4, gh4, s4, sc, ink_fn);
	grid_build(g2, gw2, gh2, s2, sc, edge_fn);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float n = bilerp(g4, gw4, s4, x, y);
			float vein = fabsf(sinf(n * 18.0f));
			int g = 150 + (int)(70 * n) - (int)(45 * powf(1 - vein, 6));
			g = g < 40 ? 40 : g > 235 ? 235 : g;
			ink_bg[y * W + x] = RGB(g, g, g + 4 > 255 ? 255 : g + 4);
			edge[y * W + x] = (uint8_t)clampf(bilerp(g2, gw2, s2, x, y) * 255, 0, 255);
		}
	free(g4); free(g2);
}

/* ---------------------------------------------------------------- the scribbled title */
/* Letters as pen strokes on a 4x6 grid. -1,-1 lifts the pen. */
static const signed char LET_S[] = {4,0, 0,0, 0,3, 4,3, 4,6, 0,6, 99};
static const signed char LET_I[] = {1,0, 3,0, -1,-1, 2,0, 2,6, -1,-1, 1,6, 3,6, 99};
static const signed char LET_L[] = {0,0, 0,6, 4,6, 99};
static const signed char LET_V[] = {0,0, 2,6, 4,0, 99};
static const signed char LET_E[] = {4,0, 0,0, 0,6, 4,6, -1,-1, 0,3, 3,3, 99};
static const signed char LET_R[] = {0,6, 0,0, 4,0, 4,3, 0,3, 4,6, 99};
static const signed char LET_O[] = {0,0, 4,0, 4,6, 0,6, 0,0, 99};
static const signed char *TITLE[] = {LET_S, LET_I, LET_L, LET_V, LET_E, LET_R, 0, LET_O, LET_S};
#define TITLE_N 9

static float title_len(void)
{
	float total = 0;
	for (int l = 0; l < TITLE_N; l++) {
		const signed char *p = TITLE[l];
		if (!p) continue;
		for (int i = 0; p[i + 2] != 99; i += 2)
			if (p[i] >= 0 && p[i + 2] >= 0) total += hypotf(p[i + 2] - p[i], p[i + 3] - p[i + 1]);
	}
	return total;
}

/* Draw the first `amount` (0..1) of the title's ink. Two slightly offset passes
 * with a wobble make it read as a quick scribble rather than a font. */
static void scribble(float cx, float cy, float u, float amount, uint32_t col, float t)
{
	float total = title_len() * amount, done = 0;
	float lw = u * 0.55f, left = cx - (TITLE_N * 6 - 2) * u / 2;
	for (int pass = 0; pass < 2; pass++) {
		done = 0;
		float ox = pass ? u * 0.18f : 0, oy = pass ? -u * 0.12f : 0;
		for (int l = 0; l < TITLE_N; l++) {
			const signed char *p = TITLE[l];
			if (!p) continue;
			float bx = left + l * 6 * u + ox, by = cy - 3 * u + oy;
			for (int i = 0; p[i + 2] != 99; i += 2) {
				if (p[i] < 0 || p[i + 2] < 0) continue;
				float seg = hypotf(p[i + 2] - p[i], p[i + 3] - p[i + 1]);
				if (done >= total) return;
				float f = fminf(1, (total - done) / seg);
				float x0 = bx + p[i] * u, y0 = by + p[i + 1] * u;
				float x1 = bx + (p[i] + (p[i + 2] - p[i]) * f) * u, y1 = by + (p[i + 1] + (p[i + 3] - p[i + 1]) * f) * u;
				float wob = u * 0.10f;
				float w0 = sinf(l * 3.1f + i + pass * 1.7f) * wob, w1 = cosf(l * 1.7f + i * 2.3f + pass) * wob;
				stroke(x0 + w0, y0 - w1, x1 - w1, y1 + w0, lw * (pass ? 0.6f : 1), col, 1);
				done += seg;
			}
		}
		(void)t;
	}
}

/* ---------------------------------------------------------------- the pen */
static void pen(float cx, float cy, float L, float snap, float t)
{
	/* two halves of a sleek pen; `snap` 0..1 opens the break */
	float ang = 0.55f * smooth(snap), gap = L * 0.05f * smooth(snap);
	float th = L * 0.055f;
	for (int side = -1; side <= 1; side += 2) {
		float a = side * ang, c = cosf(a), s = sinf(a);
		float bx = cx + side * gap, by = cy + L * 0.05f * smooth(snap);
		#define P(px, py) ((V2){bx + ((px) * c - (py) * s), by + ((px) * s + (py) * c)})
		float len = L / 2;
		float x1 = side * len;
		/* body */
		V2 q0 = P(0, -th), q1 = P(x1, -th), q2 = P(x1, th), q3 = P(0, th);
		tri(q0, q1, q2, RGB(235, 235, 238)); tri(q0, q2, q3, RGB(235, 235, 238));
		V2 h0 = P(0, -th), h1 = P(x1, -th), h2 = P(x1, -th * 0.2f), h3 = P(0, -th * 0.2f);
		tri(h0, h1, h2, WHITE); tri(h0, h2, h3, WHITE);
		if (side > 0) {             /* nib */
			tri(P(x1, -th), P(x1 + L * 0.08f, 0), P(x1, th), RGB(200, 200, 205));
			tri(P(x1 + L * 0.05f, -th * 0.3f), P(x1 + L * 0.08f, 0), P(x1 + L * 0.05f, th * 0.3f), BLACK);
		} else {                    /* cap end + clip */
			tri(P(x1, -th), P(x1 - L * 0.02f, -th), P(x1 - L * 0.02f, th), RGB(120, 120, 126));
			tri(P(x1, -th), P(x1 - L * 0.02f, th), P(x1, th), RGB(120, 120, 126));
			tri(P(x1 * 0.6f, -th), P(x1 * 0.95f, -th * 1.6f), P(x1 * 0.95f, -th), RGB(170, 170, 176));
		}
		/* jagged break edge */
		for (int k = 0; k < 4; k++) {
			float yy = -th + k * th * 0.5f;
			tri(P(0, yy), P(-side * th * 0.35f, yy + th * 0.25f), P(0, yy + th * 0.5f), RGB(235, 235, 238));
		}
		#undef P
	}
	(void)t;
}

/* ---------------------------------------------------------------- intro */
static int key_waiting(void)
{
#ifdef RENDER
	return 0;
#endif
	struct pollfd p = {tty_fd, POLLIN, 0};
	return poll(&p, 1, 0) > 0;
}

static void final_frame(float title_px)
{
	memcpy(buf, ink_bg, (size_t)W * H * 4);
	/* title parked bottom-right as a crisp black mark */
	float u = title_px;
	float cx = W - (TITLE_N * 6 - 2) * u / 2 - H * 0.05f, cy = H - H * 0.06f - 3 * u;
	scribble(cx, cy, u, 1, BLACK, 0);
}

static void intro(void)
{
	float U = fminf(W / 70.0f, H / 30.0f);          /* title unit at full size */
	float u_end = fmaxf(3, H / 160.0f);
	float bx = W / 2.0f, by = H * 0.60f;              /* break point of the pen */
	float maxr = hypotf(fmaxf(bx, W - bx), fmaxf(by, H - by)) + 60;
	double t0 = now();
	struct { float x, y, vy; } drop[24];
	for (int i = 0; i < 24; i++) drop[i] = (typeof(drop[0])){ bx + (i % 5 - 2) * 3.f, by, 0 };
	for (;;) {
		float t = (float)(now() - t0);
		if (t > 6.6f || key_waiting()) break;
		/* --- background: black, with ink spreading from the break --- */
		float r = maxr * smooth((t - 3.5f) / 1.9f);
		if (r <= 0) memset(buf, 0, (size_t)W * H * 4);
		else {
			/* the edge noise moves the boundary by at most +-115 px: beyond that band a pixel is
			 * certainly ink (copy a row span) or certainly black (clear it) */
			float rin = r - 118, rout = r + 124;
			for (int y = 0; y < H; y++) {
				float dy = (y - by) * 1.15f;
				uint32_t *row = buf + y * W, *src = ink_bg + y * W;
				float oo = rout * rout - dy * dy;
				if (oo <= 0) { memset(row, 0, (size_t)W * 4); continue; }
				float wo = sqrtf(oo);
				int xo0 = (int)fmaxf(bx - wo, 0), xo1 = (int)fminf(bx + wo + 1, W), xi0 = xo0, xi1 = xo0;
				float ii = rin * rin - dy * dy;
				if (rin > 0 && ii > 0) {
					float wi = sqrtf(ii);
					xi0 = (int)fmaxf(bx - wi, xo0); xi1 = (int)fminf(bx + wi + 1, xo1);
					if (xi1 < xi0) xi1 = xi0;
				}
				if (xi1 == xi0) xi0 = xi1 = (xo0 + xo1) / 2;
				memset(row, 0, (size_t)xo0 * 4);
				for (int x = xo0; x < xo1; x++) {
					if (x == xi0 && xi1 > xi0) { memcpy(row + xi0, src + xi0, (size_t)(xi1 - xi0) * 4); x = xi1 - 1; continue; }
					float d = hypotf(x - bx, dy) + (edge[y * W + x] - 128) * 0.9f;
					row[x] = d < r ? src[x] : d < r + 6 ? RGB(20, 20, 22) : BLACK;
				}
				memset(row + xo1, 0, (size_t)(W - xo1) * 4);
			}
		}
		/* --- title: scribbles in, later glides to the corner --- */
		float move = smooth((t - 5.2f) / 0.9f);
		float u = U + (u_end - U) * move;
		float cx0 = W / 2.0f, cy0 = H * 0.36f;
		float cx1 = W - (TITLE_N * 6 - 2) * u_end / 2 - H * 0.05f, cy1 = H - H * 0.06f - 3 * u_end;
		float cx = cx0 + (cx1 - cx0) * move, cy = cy0 + (cy1 - cy0) * move;
		float inked = r > hypotf(cx - bx, cy - by) ? 1 : 0;   /* ink has reached the title */
		scribble(cx, cy, u, clampf((t - 0.3f) / 1.8f, 0, 1), inked ? BLACK : WHITE, t);
		/* --- the pen: slides up, snaps, leaks --- */
		if (t > 2.0f && t < 4.6f) {
			float rise = smooth((t - 2.0f) / 0.5f);
			float fade = 1 - smooth((t - 4.0f) / 0.6f);
			if (fade > 0.02f) {
				pen(bx, by + (1 - rise) * H * 0.2f, fminf(W * 0.5f, H * 0.8f), (t - 2.9f) / 0.35f, t);
			}
		}
		if (t > 3.05f && t < 3.9f) {               /* silver ink leaks from the break */
			float pool = H * 0.05f * smooth((t - 3.05f) / 0.5f);
			disc(bx, by + H * 0.035f, pool, ink_bg[(int)by * W + (int)bx], 1);
			for (int i = 0; i < 24; i++) {
				float dt = t - 3.05f - (i % 8) * 0.05f;
				if (dt < 0) continue;
				float y = by + H * 0.02f + dt * dt * H * 1.4f, x = drop[i].x + (i % 5 - 2) * dt * 70;
				stroke(x, y - H * 0.03f, x, y, 5 + (i % 4) * 2, RGB(205, 205, 210), 1);   /* drop with a trail */
			}
		}
		flip();
	}
}

/* ---------------------------------------------------------------- login card */
static char user[33], pass[129];
static int field;                 /* 0 user, 1 password */
static int nopass;                /* the account has an empty password: sign-in is just "press Enter" */
static const char *status = "";

static void card(float alpha, float shake)
{
	final_frame(fmaxf(3, H / 160.0f));
	float px = fmaxf(2, H / 260.0f);
	float cw = px * 6 * 34, ch = px * 72, x = (W - cw) / 2 + shake, y = (H - ch) / 2;
	rect(x + px * 3, y + px * 3, cw, ch, BLACK, 0.35f * alpha);         /* hard shadow */
	rect(x, y, cw, ch, RGB(12, 12, 14), 0.94f * alpha);
	rect(x, y, cw, px, WHITE, alpha);
	text(x + px * 8, y + px * 8, "SIGN IN", px * 1.6f, WHITE, alpha);
	const char *labels[2] = {"USER", "PASSWORD"};
	for (int f = 0; f < 2; f++) {
		float fy = y + px * (26 + f * 20);
		text(x + px * 8, fy, labels[f], px * 0.8f, RGB(150, 150, 156), alpha);
		float bx = x + px * 8, bw = cw - px * 16, byy = fy + px * 7;
		rect(bx, byy, bw, px * 9, RGB(30, 30, 34), alpha);
		rect(bx, byy + px * 9, bw, px * 0.6f, field == f ? WHITE : RGB(80, 80, 86), alpha);
		if (f == 0) text(bx + px * 2, byy + px * 1.5f, user, px * 0.9f, WHITE, alpha);
		else for (size_t k = 0; k < strlen(pass) && k < 40; k++) rect(bx + px * (2.5f + k * 5), byy + px * 3, px * 3, px * 3, WHITE, alpha);
		if (field == f && ((long)(now() * 2) & 1)) {
			float cxp = f == 0 ? bx + px * 2 + strlen(user) * 6 * px * 0.9f : bx + px * (2.5f + strlen(pass) * 5);
			rect(cxp, byy + px * 1.5f, px * 0.6f, px * 6.5f, WHITE, alpha);
		}
	}
	text(x + px * 8, y + ch - px * 8, status, px * 0.7f, RGB(230, 230, 235), alpha);
	flip();
}

#ifdef RENDER
#define crypt(a, b) ((char *)0)
#endif
/* An account is open when /etc/shadow has an empty password field for it (and it is not root). */
static int account_open(const char *u)
{
	FILE *f = fopen("/etc/shadow", "r");
	char line[512];
	int open_ = 0;
	if (!f) return 0;
	size_t ul = strlen(u);
	while (fgets(line, sizeof line, f))
		if (!strncmp(line, u, ul) && line[ul] == ':') { open_ = line[ul + 1] == ':'; break; }
	fclose(f);
	struct passwd *pw = getpwnam(u);
	return open_ && pw && pw->pw_uid != 0;
}

/* Sign-in card for a passwordless account: who you are, and "press Enter". */
static void card_simple(float alpha)
{
	final_frame(fmaxf(3, H / 160.0f));
	float px = fmaxf(2, H / 260.0f);
	float cw = px * 6 * 30, ch = px * 50, x = (W - cw) / 2, y = (H - ch) / 2;
	rect(x + px * 3, y + px * 3, cw, ch, BLACK, 0.35f * alpha);          /* hard shadow */
	rect(x, y, cw, ch, RGB(12, 12, 14), 0.94f * alpha);
	rect(x, y, cw, px, WHITE, alpha);
	float ax = x + px * 16, ay = y + ch / 2;                              /* the ring-and-dot mark */
	disc(ax, ay, px * 9, WHITE, alpha); disc(ax, ay, px * 6.2f, RGB(12, 12, 14), alpha);
	disc(ax, ay, px * 2.2f, WHITE, alpha);
	text(x + px * 32, y + px * 14, user, px * 1.8f, WHITE, alpha);
	float blink = 0.55f + 0.45f * sinf((float)now() * 5);
	text(x + px * 32, y + px * 32, "PRESS ENTER", px * 0.9f, RGB(200, 200, 206), alpha * blink);
	flip();
}

static int check_password(const char *u, const char *p)
{
	FILE *f = fopen("/etc/shadow", "r");
	char line[512];
	int ok = 0;
	if (!f) return 0;
	size_t ul = strlen(u);
	while (fgets(line, sizeof line, f)) {
		if (strncmp(line, u, ul) || line[ul] != ':') continue;
		char *hash = line + ul + 1, *end = strchr(hash, ':');
		if (end) *end = 0;
		if (hash[0] && hash[0] != '!' && hash[0] != '*') {
			const char *c = crypt(p, hash);
			ok = c && !strcmp(c, hash);
		}
		break;
	}
	fclose(f);
	memset(line, 0, sizeof line);
	return ok;
}

static void start_session(void)
{
	memset(pass, 0, sizeof pass);
	restore_tty();
	execl("/bin/login", "login", "-f", user, (char *)NULL);
	_exit(1);
}

static void simple_loop(void)
{
	double t0 = now();
	for (;;) {
		card_simple(clampf((float)(now() - t0) / 0.4f, 0, 1));
		struct pollfd p = {tty_fd, POLLIN, 0};
		if (poll(&p, 1, 40) <= 0) continue;
		char c;
		if (read(tty_fd, &c, 1) == 1 && (c == '\r' || c == '\n')) start_session();
	}
}

static void login_loop(void)
{
	double fade0 = now();
	for (;;) {
		float a = clampf((float)(now() - fade0) / 0.4f, 0, 1);
		card(a, 0);
		struct pollfd p = {tty_fd, POLLIN, 0};
		if (poll(&p, 1, a < 1 ? 16 : 250) <= 0) continue;
		char c;
		if (read(tty_fd, &c, 1) != 1) continue;
		if (c == '\033') {                      /* swallow escape sequences (arrows etc.) */
			char junk[8];
			usleep(2000);
			if (read(tty_fd, junk, sizeof junk) < 0) { /* nothing */ }
			continue;
		}
		char *dst = field ? pass : user;
		size_t cap = field ? sizeof pass - 1 : sizeof user - 1;
		size_t n = strlen(dst);
		if (c == '\t') { field ^= 1; continue; }
		if (c == 127 || c == 8) { if (n) dst[n - 1] = 0; continue; }
		if (c == 21) { dst[0] = 0; continue; }                         /* ctrl-u */
		if (c == '\r' || c == '\n') {
			if (field == 0) { if (n) field = 1; continue; }
			struct passwd *pw = getpwnam(user);
			if (pw && pw->pw_uid == 0) status = "ROOT CANNOT SIGN IN HERE - USE SU";
			else if (pw && check_password(user, pass)) start_session();
			else status = "WRONG USER OR PASSWORD";
			memset(pass, 0, sizeof pass);
			for (int k = 0; k < 12; k++) { card(1, sinf(k * 1.9f) * (12 - k) * 2); usleep(16000); }
			sleep(1);                                                  /* slow down guessing */
			tcflush(tty_fd, TCIFLUSH);
			continue;
		}
		if (c >= 32 && c < 127 && n < cap) { dst[n] = c; dst[n + 1] = 0; }
		if (field == 0 && strlen(user) > 0) status = "";
	}
}

#ifdef RENDER
/* ./silver-greeter-render out.mp4  -> the full intro + sign-in, rendered at a steady 30 fps */
int main(int argc, char **argv)
{
	W = 1280; H = 800; stride = W;
	buf = malloc((size_t)W * H * 4); ink_bg = malloc((size_t)W * H * 4); edge = malloc((size_t)W * H);
	char cmd[512];
	snprintf(cmd, sizeof cmd, "ffmpeg -v error -y -f rawvideo -pix_fmt bgr0 -s %dx%d -r 30 -i - "
		 "-c:v libx264 -crf 16 -preset medium -pix_fmt yuv420p '%s'", W, H, argc > 1 ? argv[1] : "greeter.mp4");
	outpipe = popen(cmd, "w");
	build_ink();
	intro();
	/* sign-in card: fade in, then hold */
	snprintf(user, sizeof user, "silver");
	for (int f = 0; f < 120; f++) card_simple(clampf(f / 30.0f / 0.4f, 0, 1));
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
	build_ink();
	/* the full intro plays once per boot; later logins go straight to the card */
	if (mkdir("/run/silver-greeter", 0700) == 0) intro();
	tcflush(tty_fd, TCIFLUSH);
	snprintf(user, sizeof user, "silver");
	nopass = account_open(user);
	if (nopass) simple_loop();
	user[0] = 0;
	login_loop();
	return 0;
}

#endif
