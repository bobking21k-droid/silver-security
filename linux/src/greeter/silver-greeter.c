/*
 * silver-greeter -- the SilverOS sign-in and home screen, drawn straight onto /dev/fb0 with sgfx.
 *
 * Intro (once per boot, ~4 s, any key skips):
 *   black -> a red dot beats -> a chrome pen rises up the middle, strains and SNAPS ->
 *   the screen CRACKS: fractures race out from the break, and two jagged ones split the glass
 *   into four panes -> "SILVER OS" slams in where they cross -> the panes light up as
 *   SETTINGS, SEARCH, APPS and FILES.
 *
 * Home: arrow keys choose a pane, Enter opens it, Esc comes back (see home.h). Each section
 * works: browse and preview files, search files/apps/settings, a terminal and a live system
 * monitor, network and display info, calm mode, restart and shut down.
 *
 * Accounts and privileges: an account with an empty password needs no sign-in. Otherwise a
 * password card checks /etc/shadow with crypt(3). Root may not sign in here (use su). The
 * interface itself runs as the signed-in user: this process forks, the child drops root and
 * draws the home screen, and the privileged parent only does what the child asks through a
 * pipe: open the terminal (login -f), restart, or shut down. Without a usable framebuffer
 * we fall back to getty.
 *
 * Add `silver.calm` to the kernel command line (or turn on Calm mode in Settings) to turn
 * off the shake and flashes.
 *
 * Usage (from silver-login, as root, stdin = the tty):  silver-greeter tty1
 * Build with -DRENDER for an offline demo renderer:  silver-greeter-render out.mp4 [w h [password]]
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <grp.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
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

static Font FD, FT, FC;                       /* Silver Scrawl, Inter, Michroma */
static int W, H, scale = 1, fb_w, fb_h, stride, calm;
static float U;                               /* 1% of the screen height */
static uint32_t *fbmem, *canvas;
static Surf fb;
static int tty_fd = 0, req_fd = -1;
static struct termios saved_tio;
static const char *tty_name = "tty1";
static char user[33] = "silver", pass[129];

/* ---------------------------------------------------------------- time, output, input */
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

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#include "../common/stb_image.h"
#include "home.h"

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
	canvas = malloc((size_t)W * H * 4);
	qmap = malloc((size_t)W * H); bg_dark = malloc((size_t)W * H * 4); bg_light = malloc((size_t)W * H * 4); bg_sec = malloc((size_t)W * H * 4);
	return fb.px && canvas && qmap && bg_dark && bg_light && bg_sec && !sg_font_load(&FD, font_scrawl) && !sg_font_load(&FT, font_text) && !sg_font_load(&FC, font_caption) ? 0 : -1;
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

#ifndef RENDER
/* One key: letters, Enter (K_ENTER), Backspace (K_BACK), arrows (K_UP...), a lone Esc (K_ESC). */
static int read_key(int wait_ms)
{
	struct pollfd p = {tty_fd, POLLIN, 0};
	if (poll(&p, 1, wait_ms) <= 0) return -1;
	unsigned char c;
	if (read(tty_fd, &c, 1) != 1) return -1;
	if (c == 27) {
		if (poll(&p, 1, 40) <= 0) return K_ESC;
		unsigned char c2;
		if (read(tty_fd, &c2, 1) != 1) return K_ESC;
		if (c2 == '[' || c2 == 'O') {
			unsigned char c3;
			if (read(tty_fd, &c3, 1) != 1) return -1;
			if (c3 == 'A') return K_UP;
			if (c3 == 'B') return K_DOWN;
			if (c3 == 'C') return K_RIGHT;
			if (c3 == 'D') return K_LEFT;
			while (c3 >= '0' && c3 <= '9' && poll(&p, 1, 20) > 0) { if (read(tty_fd, &c3, 1) != 1) break; if (c3 == '~') break; }
			return -1;
		}
		return K_ESC;
	}
	if (c == '\r' || c == '\n') return K_ENTER;
	if (c == 8 || c == 127) return K_BACK;
	return c;
}
#endif

/* ================================================================ the title */
static const char *TITLE = "SILVER OS";
static float title_w(float size) { return sg_text_w(&FD, TITLE, size, 0); }

/* Draw the title with its baseline at `by`, centred on cx. `t` is seconds since the first letter
 * began to slam in. Each letter drops in oversized, with a red echo. */
static void title(float cx, float by, float size, float t, float alpha)
{
	float left = cx - title_w(size) / 2;
	char pre[16];
	for (int i = 0; TITLE[i]; i++) {
		float k = (t - i * 0.075f) / 0.15f;
		if (k <= 0) continue;
		memcpy(pre, TITLE, (size_t)i); pre[i] = 0;
		char one[2] = {TITLE[i], 0};
		float x = left + sg_text_w(&FD, pre, size, 0), wch = sg_text_w(&FD, one, size, 0);
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

/* Standing pen, cap up, nib down. (cx, cy) is the break point. snap: 0 whole .. 1 broken. */
static void pen(float cx, float cy, float L, float snap, float a)
{
	float k = sg_ease_out(snap), ang = 0.34f * k, th = L * 0.040f, half = L / 2, gap = L * 0.02f * k;
	const uint32_t d = RGB(58, 60, 68), l = RGB(252, 252, 255), gd = RGB(18, 18, 22), gl = RGB(120, 122, 132);
	float t1 = 1.5707963f + ang;                                                                     /* cap half (up) */
	V2 o = {cx + gap * 0.3f, cy - gap}; float c = sg_cos(t1), s = sg_sin(t1);
	tube(o, c, s, -half * 0.985f, 0, th, th, d, l, a);
	tube(o, c, s, -half, -half * 0.985f, th * 0.72f, th, d, l, a);
	tube(o, c, s, -half * 0.62f, -half * 0.585f, th * 1.08f, th * 1.08f, gd, gl, a);
	{ V2 q[4] = {rot(o, c, s, -half * 0.94f, -th * 1.55f), rot(o, c, s, -half * 0.36f, -th * 1.55f), rot(o, c, s, -half * 0.36f, -th * 1.2f), rot(o, c, s, -half * 0.94f, -th * 1.2f)};
	  sg_poly(&fb, q, 4, RGB(214, 216, 224), a, 0);
	  V2 r[4] = {rot(o, c, s, -half * 0.94f, -th * 1.55f), rot(o, c, s, -half * 0.88f, -th * 1.55f), rot(o, c, s, -half * 0.88f, -th * 0.9f), rot(o, c, s, -half * 0.94f, -th * 0.9f)};
	  sg_poly(&fb, r, 4, RGB(214, 216, 224), a, 0); }
	for (int j = 0; j < 5 && snap > 0; j++) {                                                        /* torn metal at the break */
		float v = -1 + 0.4f * j;
		sg_tri(&fb, rot(o, c, s, 0, v * th), rot(o, c, s, th * (0.35f + 0.5f * ((j * 7) % 3) * 0.5f), (v + 0.2f) * th), rot(o, c, s, 0, (v + 0.4f) * th), sg_mix(d, l, 0.3f + 0.15f * j), a);
	}
	t1 = 1.5707963f - ang;                                                                           /* nib half (down) */
	o = (V2){cx - gap * 0.3f, cy + gap}; c = sg_cos(t1); s = sg_sin(t1);
	tube(o, c, s, 0, half * 0.70f, th, th, d, l, a);
	tube(o, c, s, half * 0.70f, half * 0.735f, th * 1.08f, th * 1.08f, gd, gl, a);
	tube(o, c, s, half * 0.735f, half * 0.93f, th, th * 0.30f, d, l, a);
	tube(o, c, s, half * 0.93f, half, th * 0.30f, th * 0.03f, gd, gl, a);
	for (int j = 0; j < 5 && snap > 0; j++) {
		float v = -1 + 0.4f * j;
		sg_tri(&fb, rot(o, c, s, 0, v * th), rot(o, c, s, -th * (0.35f + 0.5f * ((j * 5 + 1) % 3) * 0.5f), (v + 0.2f) * th), rot(o, c, s, 0, (v + 0.4f) * th), sg_mix(d, l, 0.35f + 0.12f * j), a);
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

static void home_init(Home *h)
{
	memset(h, 0, sizeof *h);
	h->blend[0] = 1;
	h->calm_on = calm;
}

#define T_DOT 0.30f
#define T_PEN 0.70f
#define T_SNAP 1.85f
#define T_TITLE 2.15f
#define T_OPEN 3.00f
#define T_END 3.80f

static void intro(Home *h)
{
	float PL = sg_min(H * 0.44f, W * 0.5f), cx = W / 2.0f, cy = H / 2.0f;
	float tsz = sg_min(U * 15.0f, W * 0.82f / title_w(1.0f));
	static struct { float vx, vy, spin, size; } chip[18];
	for (int i = 0; i < 18; i++) chip[i] = (typeof(chip[0])){(sg_frand() - .5f) * 2.6f, -0.5f - sg_frand() * 1.6f, (sg_frand() - .5f) * 14, 0.25f + sg_frand() * 0.9f};
	double t0 = now();
	for (;;) {
		float t = (float)(now() - t0), ct = t - T_SNAP;
		if (t > T_END || key_waiting()) break;
		sg_clear(&fb, BLACK);
		float open = sg_sat((t - T_OPEN) / 0.75f);
		home_draw(h, open, t, ct);                            /* the glass (once open) and the cracks, growing from the break */

		float hb = sg_heart(t - T_DOT, 0.82f);                /* the dot, beating in the dark, then lying under the pen */
		if (t > T_DOT && t < T_SNAP) {
			float grow = sg_smooth((t - T_DOT) / 0.3f);
			sg_glow(&fb, cx, cy, U * (6 + 5 * hb), BLOOD, 0.55f * grow * (0.5f + 0.5f * hb));
			sg_disc(&fb, cx, cy, U * (0.9f + 0.9f * hb) * grow, BLOOD, grow);
		}
		if (t > T_PEN && ct < 1.4f) {                         /* the pen rises, strains, snaps, and falls away */
			float in = sg_ease_out((t - T_PEN) / 0.55f), snap = ct / 0.10f;
			float strain = (t > T_SNAP - 0.5f && t < T_SNAP) ? (t - T_SNAP + 0.5f) / 0.5f : 0;
			float shk = calm ? 0 : sg_sin(t * 110) * U * 0.30f * strain;
			float fall = ct > 0.35f ? sg_smooth((ct - 0.35f) / 1.0f) : 0;
			pen(cx + shk, cy + (1 - in) * H * 0.62f + fall * fall * H * 0.7f, PL, snap > 0 ? sg_sat(snap) : 0, (1 - fall) * sg_sat(in * 4));
			if (ct > 0)
				for (int i = 0; i < 18; i++) {                  /* chips of glass and metal */
					float x = cx + chip[i].vx * ct * H * 0.5f, y = cy + chip[i].vy * ct * H * 0.35f + ct * ct * H * 0.9f, sz = chip[i].size * U * 1.4f, a = chip[i].spin * ct;
					V2 o = {x, y}; float c = sg_cos(a), s = sg_sin(a);
					sg_tri(&fb, rot(o, c, s, -sz, -sz * 0.5f), rot(o, c, s, sz * 1.1f, -sz * 0.2f), rot(o, c, s, -sz * 0.2f, sz), RGB(226, 228, 236), sg_sat(1.5f - ct * 1.4f));
				}
		}
		if (ct > 0 && ct < 0.35f) sg_glow(&fb, cx, cy, U * 22 * sg_sat(ct * 6), WHITE, 0.8f * (1 - ct / 0.35f));   /* the impact */
		if (t > T_TITLE) title(cx, cy + 0.36f * tsz, tsz, t - T_TITLE, 1 - sg_sat((t - T_OPEN) / 0.5f));
		if (!calm && ct > 0) {                                  /* a thump on the snap, a white flash for one beat */
			float th = sg_sat(1 - ct / 0.45f);
			shake_frame(sg_sin(t * 97) * U * 1.3f * th * th, sg_cos(t * 83) * U * 1.3f * th * th);
			if (ct < 0.08f) sg_rect(&fb, 0, 0, W, H, WHITE, 0.85f * (1 - ct / 0.08f));
		}
		if (t < T_TITLE) sg_text_c(&fb, &FC, W / 2.0f, H - U * 3.4f, "CONTAINS FLASHING LIGHTS  /  ANY KEY SKIPS", U * 0.9f, WHITE, 0.55f * sg_sat((T_PEN + 0.9f - t) * 3) * sg_sat((t - 0.15f) * 4), 0.26f);
		flip();
	}
}

/* ================================================================ sign-in card (accounts with a password) */
static const char *status = "";
static int field = 1;             /* 0 user name, 1 password */

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

static void red_text(float x, float y, const char *str, float size, float alpha, int centered)
{
	if (centered) x -= sg_text_w(&FD, str, size, 0) / 2;
	sg_text(&fb, &FD, x + size * 0.035f, y + size * 0.035f, str, size, BLACK, alpha * 0.6f, 0);
	sg_text(&fb, &FD, x - size * 0.03f, y, str, size, BLOOD, alpha * 0.9f, 0);
	sg_text(&fb, &FD, x, y, str, size, WHITE, alpha, 0);
}

/* the cracked glass, with a password card over it */
static void lock_frame(float alpha, float shake)
{
	memcpy(fb.px, bg_dark, (size_t)W * H * 4);
	for (int i = 0; i < nck; i++) crack_draw(&fb, &ck[i], 99, 0.8f);
	char hm[8], date[48];
	clock_text(hm, date);
	float t = (float)now(), beat = sg_heart(t, 0.9f);
	red_text(W / 2.0f, H * 0.285f, hm, U * 19.5f, alpha, 1);
	sg_text_c(&fb, &FC, W / 2.0f, H * 0.285f + U * 6.6f, date, U * 1.55f, WHITE, alpha, 0.26f);
	float cw = U * 66, ch = U * 28, x = (W - cw) / 2 + shake, y = H * 0.50f, pb = U * 0.55f;
	sg_rect(&fb, x + U * 0.9f, y + U * 0.9f, cw, ch, BLACK, 0.5f * alpha);
	sg_rect(&fb, x, y, cw, ch, INK, 0.96f * alpha);
	sg_rect(&fb, x, y, cw, pb, BLOOD, alpha);
	float top = y + U * 8.2f;
	sg_glow(&fb, x + U * 7.6f, top, U * 8, BLOOD, 0.3f * alpha * beat);
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
	sg_text(&fb, &FC, fx, fy + fh + U * 3.6f, status[0] ? status : "ENTER TO SIGN IN  /  TAB TO CHANGE USER", U * 0.98f, status[0] ? RGB(255, 80, 70) : RGB(170, 170, 176), alpha, 0.22f);
	flip();
}

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
			char *hh = line + ul + 1, *e = strchr(hh, ':');
			if (e) *e = 0;
			snprintf(out, n, "%s", hh);
			found = 1;
			break;
		}
	fclose(f);
	memset(line, 0, sizeof line);
	return found;
}
static int account_open(const char *u)          /* empty password, and not root */
{
	char hh[256];
	struct passwd *pw = getpwnam(u);
	return pw && pw->pw_uid != 0 && shadow_field(u, hh, sizeof hh) && hh[0] == 0;
}
static int check_password(const char *u, const char *p)
{
	char hh[256];
	if (!shadow_field(u, hh, sizeof hh) || !hh[0] || hh[0] == '!' || hh[0] == '*') return 0;
	const char *c = crypt(p, hh);
	int ok = c && !strcmp(c, hh);
	memset(hh, 0, sizeof hh);
	return ok;
}

#ifndef RENDER
static void lock_loop(void)
{
	double t0 = now();
	for (;;) {
		float a = sg_sat((float)(now() - t0) / 0.5f);
		lock_frame(a, 0);
		int c = read_key(a < 1 ? 16 : 60);
		if (c < 0) continue;
		char *dst = field ? pass : user;
		size_t cap = field ? sizeof pass - 1 : sizeof user - 1, n = strlen(dst);
		if (c == '\t') { field ^= 1; continue; }
		if (c == K_BACK) { if (n) dst[n - 1] = 0; continue; }
		if (c == 21) { dst[0] = 0; continue; }
		if (c == K_ENTER) {
			if (field == 0) { field = 1; if (account_open(user)) return; continue; }
			struct passwd *pw = getpwnam(user);
			if (pw && pw->pw_uid == 0) status = "ROOT CANNOT SIGN IN HERE - USE SU";
			else if (pw && check_password(user, pass)) { memset(pass, 0, sizeof pass); return; }
			else status = "WRONG USER NAME OR PASSWORD";
			memset(pass, 0, sizeof pass);
			for (int k = 0; k < 14; k++) { lock_frame(1, sg_sin(k * 1.9f) * (14 - k) * U * 0.22f); usleep(16000); }
			sleep(1);                                               /* slow down guessing */
			tcflush(tty_fd, TCIFLUSH);
			continue;
		}
		if (c >= 32 && c < 127 && n < cap) { dst[n] = (char)c; dst[n + 1] = 0; status = ""; }
	}
}
#endif

/* ================================================================ home: input handling */
static void request(char c)
{
#ifndef RENDER
	if (req_fd >= 0) { if (write(req_fd, &c, 1) < 0) { /* parent gone */ } }
#else
	(void)c;
#endif
}

static void files_preview(Home *h)
{
	pv_n = 0; pv_binary = 0;
	if (h->row < nent && !ents[h->row].dir) {
		char p[512]; snprintf(p, sizeof p, "%s/%s", cwd, ents[h->row].name);
		fs_preview(p, ents[h->row].size);
	}
}
static void open_screen(Home *h, int s)
{
	h->screen = s; h->row = 0; h->top = 0; h->sub = 0; h->confirm = 0; h->fade = 0;
	if (s == 4) { snprintf(cwd, sizeof cwd, "%s", homedir); h->depth = 0; fs_list(cwd); files_preview(h); }
	if (s == 2) { h->focus = yt_count(&yt) ? 1 : 0; if (!yt.fetch[0]) snprintf(yt.fetch, sizeof yt.fetch, "/sbin/silver-fetch"); if (yt.state == Y_WATCH) yt.state = Y_RESULTS; }
	h->fs = 0;
	if (s == 3) h->sub = 0;
}
static void calm_toggle(Home *h)
{
	h->calm_on ^= 1;
#ifndef RENDER
	char p[256];
	snprintf(p, sizeof p, "%s/.config", homedir); mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/.config/silver", homedir); mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/.config/silver/calm", homedir);
	if (h->calm_on) { int fd = open(p, O_WRONLY | O_CREAT, 0644); if (fd >= 0) close(fd); } else unlink(p);
#endif
	calm = h->calm_on;
}
static void files_up(Home *h)
{
#ifdef RENDER
	if (!strcmp(cwd, homedir)) return;
#endif
	if (!strcmp(cwd, "/")) return;
	char prev[96]; char *sl = strrchr(cwd, '/');
	snprintf(prev, sizeof prev, "%s", sl ? sl + 1 : "");
	if (sl == cwd) cwd[1] = 0; else if (sl) *sl = 0;
	fs_list(cwd);
	h->row = 0;
	for (int i = 0; i < nent; i++) if (!strcmp(ents[i].name, prev)) h->row = i;
	h->top = 0; files_preview(h);
}

static void handle_key(Home *h, int k)
{
	switch (h->screen) {
	case 0:
		if (k == K_LEFT) h->sel &= ~1;
		else if (k == K_RIGHT) h->sel |= 1;
		else if (k == K_UP) h->sel &= ~2;
		else if (k == K_DOWN) h->sel |= 2;
		else if (k == K_ENTER) open_screen(h, h->sel + 1);
		break;
	case 1:                                                                     /* settings */
		if (k == K_ESC || k == K_LEFT || k == K_BACK) { h->screen = 0; h->fade = 0; }
		else if (k == K_UP && h->row > 0) { h->row--; h->confirm = 0; }
		else if (k == K_DOWN && h->row < 5) { h->row++; h->confirm = 0; }
		else if (k == K_ENTER) {
			if (h->row == 2) calm_toggle(h);
			else if (h->row == 4 || h->row == 5) { if (h->confirm == h->row) request(h->row == 4 ? 'R' : 'P'); else h->confirm = h->row; }
		}
		break;
	case 2: {                                                                   /* search: YouTube */
		Yt *y = &yt;
		if (y->state == Y_WATCH) { if (k == K_ESC || k == K_BACK || k == K_LEFT) y->state = Y_RESULTS; break; }
		if (k == K_ESC) { h->screen = 0; h->fade = 0; break; }
		if (k == K_DOWN && h->focus == 0 && y->state == Y_RESULTS) { h->focus = 1; break; }
		if (h->focus == 1) {
			if (k == K_UP) { if (y->sel > 0) y->sel--; else h->focus = 0; break; }
			if (k == K_DOWN) { if (y->sel + 1 < yt_count(y)) y->sel++; break; }
			if (k == K_ENTER) { y->watch = y->sel; y->state = Y_WATCH; break; }
		}
		if (k == 9) { y->mode ^= 1; if (y->q[0]) { yt_search(y); h->focus = y->state == Y_RESULTS ? 1 : 0; } break; }
		size_t n = strlen(y->q);
		if (k == K_BACK && n) { y->q[n - 1] = 0; h->focus = 0; }
		else if (k == 21) { y->q[0] = 0; h->focus = 0; }
		else if (k >= 32 && k < 127 && n + 1 < sizeof y->q) { y->q[n] = (char)k; y->q[n + 1] = 0; h->focus = 0; }
		else if (k == K_ENTER && y->q[0]) { yt_search(y); h->focus = y->state == Y_RESULTS ? 1 : 0; }
		break;
	}
	case 3:                                                                     /* apps */
		if (h->sub) { if (k == K_ESC || k == K_BACK || k == K_LEFT) h->sub = 0; break; }
		if (k == K_ESC || k == K_LEFT || k == K_BACK) { h->screen = 0; h->fade = 0; }
		else if (k == K_UP && h->row > 0) h->row--;
		else if (k == K_DOWN && h->row < 2) h->row++;
		else if (k == K_ENTER) { if (h->row == 0) request('T'); else h->sub = h->row; }
		break;
	case 4:                                                                     /* files */
		if (h->fs) {
			Search *sr = &h->sr; size_t n = strlen(sr->q);
			if (k == K_ESC) { h->fs = 0; h->row = 0; h->top = 0; files_preview(h); }
			else if (k == 9) { h->fs_scope ^= 1; fs_refresh(h); }
			else if (k == K_UP && h->row > 0) h->row--;
			else if (k == K_DOWN && h->row + 1 < search_count(sr)) h->row++;
			else if (k == K_BACK && n) { sr->q[n - 1] = 0; fs_refresh(h); }
			else if (k == 21) { sr->q[0] = 0; fs_refresh(h); }
			else if (k >= 32 && k < 127 && n < 40) { sr->q[n] = (char)k; sr->q[n + 1] = 0; fs_refresh(h); }
			else if (k == K_ENTER && search_count(sr)) {
				const SRes *r = search_item(sr, h->row);
				if (r->node >= 0) {
					const Node *nd = &sr->nodes[r->node];
					char dir[256]; node_path(sr, nd->kind == N_DIR ? r->node : nd->parent, dir, sizeof dir);
					snprintf(cwd, sizeof cwd, "%s", dir[0] ? dir : "/"); fs_list(cwd);
					h->row = 0; h->top = 0;
					if (nd->kind != N_DIR) for (int i = 0; i < nent; i++) if (!strcmp(ents[i].name, nd->name)) h->row = i;
					h->fs = 0; files_preview(h);
				}
			}
			break;
		}
		if (k == '/') { h->fs = 1; fs_prepare(h); h->sr.q[0] = 0; fs_refresh(h); break; }
		if (k == K_ESC) { h->screen = 0; h->fade = 0; }
		else if (k == K_UP && h->row > 0) { h->row--; files_preview(h); }
		else if (k == K_DOWN && h->row + 1 < nent) { h->row++; files_preview(h); }
		else if (k == K_BACK || k == K_LEFT) files_up(h);
		else if ((k == K_ENTER || k == K_RIGHT) && nent && ents[h->row].dir) {
			size_t n = strlen(cwd);
			snprintf(cwd + n, sizeof cwd - n, "%s%s", n > 1 ? "/" : "", ents[h->row].name);
			fs_list(cwd); h->row = 0; h->top = 0; files_preview(h);
		}
		break;
	}
}

static void sample_sys(Home *h, float t)
{
	if (t - h->last_sample < 0.5f) return;
	h->last_sample = t;
	sys_read(&h->sys, t);
	if (h->hn < 90) h->cpu_hist[h->hn++] = h->sys.cpu;
	else { memmove(h->cpu_hist, h->cpu_hist + 1, sizeof(float) * 89); h->cpu_hist[89] = h->sys.cpu; }
}

static void home_frame(Home *h, float t, float dt)
{
	for (int q = 0; q < 4; q++) h->blend[q] += (((h->screen == 0 && q == h->sel) ? 1.f : 0.f) - h->blend[q]) * sg_min(1, dt * 11);
	h->fade = sg_min(1, h->fade + dt * 6);
	yt_poll(&yt);
	if (h->fs) search_tick(&h->sr, 0.004);
	switch (h->screen) {
	case 0: home_draw(h, 1, t, 99); break;
	case 1: draw_settings(h, t); break;
	case 2: draw_search(h, t); break;
	case 3: if (h->sub == 1) sample_sys(h, t); draw_apps(h, t); break;
	case 4: if (h->fs) draw_files_search(h, t); else draw_files(h, t); break;
	}
	if (h->screen && h->fade < 1) sg_rect(&fb, 0, 0, W, H, BLACK, 1 - h->fade);
	else if (!h->screen && h->fade < 1) sg_rect(&fb, 0, 0, W, H, BLACK, 1 - h->fade);
}

#ifdef RENDER
/* the demo's keystrokes, by seconds after the home screen opens */
static const struct { float t; int k; } SCRIPT[] = {
	{0.8f, K_RIGHT}, {1.5f, K_DOWN}, {2.4f, K_LEFT}, {3.0f, K_UP}, {3.7f, K_RIGHT}, {4.3f, K_DOWN},          /* look around, end on Files */
	{5.0f, K_ENTER},                                                                                          /* Files */
	{6.0f, K_ENTER}, {7.0f, K_DOWN}, {7.8f, K_DOWN}, {8.6f, K_BACK}, {9.6f, K_DOWN}, {10.2f, K_DOWN},
	{11.2f, '/'}, {12.0f, 'r'}, {12.4f, 'e'}, {12.8f, 'p'}, {14.0f, K_DOWN}, {15.0f, K_ESC}, {15.8f, K_ESC},  /* search inside Files */
	{16.6f, K_UP}, {17.3f, K_ENTER},                                                                          /* Search = YouTube */
	{18.2f, 'l'}, {18.5f, 'i'}, {18.8f, 'n'}, {19.1f, 'u'}, {19.4f, 'x'}, {20.0f, K_ENTER},                  /* web results */
	{22.0f, K_DOWN}, {22.8f, K_DOWN}, {23.6f, K_ENTER}, {25.6f, K_ESC},                                       /* a result */
	{26.4f, 9}, {28.0f, K_DOWN}, {28.8f, K_DOWN}, {29.6f, K_ENTER}, {32.0f, K_ESC}, {32.6f, K_ESC},           /* videos, watch page */
	{33.4f, K_LEFT}, {34.1f, K_ENTER},                                                                        /* Settings */
	{35.4f, K_DOWN}, {36.0f, K_DOWN}, {36.6f, K_ENTER}, {37.6f, K_ENTER}, {38.4f, K_ESC},
	{39.2f, K_DOWN}, {39.8f, K_ENTER},                                                                        /* Apps */
	{40.6f, K_DOWN}, {41.2f, K_ENTER}, {45.5f, K_ESC}, {46.1f, K_ESC},
};
#define SCRIPT_END 48.0f
#endif

static void home_loop(Home *h)
{
	double t0 = now(), last = t0;
#ifdef RENDER
	unsigned si = 0;
#endif
	for (;;) {
		double tn = now();
		float t = (float)(tn - t0), dt = (float)(tn - last);
		last = tn;
#ifdef RENDER
		while (si < sizeof SCRIPT / sizeof SCRIPT[0] && SCRIPT[si].t <= t) handle_key(h, SCRIPT[si++].k);
		if (t > SCRIPT_END) return;
#else
		int k;
		while ((k = read_key(0)) != -1) handle_key(h, k);
#endif
		home_frame(h, t, dt > 0.2f ? 0.2f : dt);
		flip();
#ifndef RENDER
		if (!key_waiting()) usleep(24000);
#endif
	}
}

/* ================================================================ session */
#ifndef RENDER
static void run_session(void)
{
	struct passwd *pw = getpwnam(user);
	int pfd[2];
	if (!pw || pipe(pfd) < 0) { restore_tty(); execl("/bin/login", "login", "-f", user, (char *)NULL); _exit(1); }
	pid_t pid = fork();
	if (pid == 0) {                                   /* the interface runs as the user, never as root */
		close(pfd[0]); req_fd = pfd[1];
		snprintf(homedir, sizeof homedir, "%s", pw->pw_dir);
		if (setgroups(0, NULL) || setgid(pw->pw_gid) || setuid(pw->pw_uid)) _exit(1);
		if (chdir(homedir)) { /* fine */ }
		Home h; home_init(&h);
		home_loop(&h);
		_exit(0);
	}
	close(pfd[1]);
	char c = 'T';
	for (;;) {                                        /* the privileged side only does what the user's side asks */
		ssize_t n = read(pfd[0], &c, 1);
		if (n <= 0) { c = 'T'; break; }
		if (c == 'T') break;
		if (c == 'R') { kill(1, SIGTERM); for (;;) pause(); }
		if (c == 'P') { kill(1, SIGUSR2); for (;;) pause(); }
	}
	kill(pid, SIGKILL); waitpid(pid, NULL, 0);
	restore_tty();
	execl("/bin/login", "login", "-f", user, (char *)NULL);
	_exit(1);
}
#endif

#ifdef RENDER
int main(int argc, char **argv)
{
	W = argc > 2 ? atoi(argv[2]) : 1280; H = argc > 3 ? atoi(argv[3]) : 800;
	if (buffers()) return 1;
	char cmd[512];
	snprintf(cmd, sizeof cmd, "ffmpeg -v error -y -f rawvideo -pix_fmt bgr0 -s %dx%d -r 30 -i - -c:v libx264 -crf 16 -preset medium -pix_fmt yuv420p '%s'",
		 W, H, argc > 1 ? argv[1] : "greeter.mp4");
	outpipe = popen(cmd, "w");
	gen_cracks(); bake_home();
	Home h; home_init(&h);
	if (argc > 4) {                                   /* the password card, over the cracked glass */
		strcpy(pass, "12345678");
		for (int f = 0; f < 70; f++) lock_frame(sg_sat(f / 15.f), 0);
	}
	intro(&h);
	home_loop(&h);
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
		struct passwd *pw = getpwnam(user);
		if (pw) {
			snprintf(homedir, sizeof homedir, "%s", pw->pw_dir);
			char p[256]; snprintf(p, sizeof p, "%s/.config/silver/calm", homedir);
			if (access(p, F_OK) == 0) calm = 1;
		}
	}
	tcgetattr(tty_fd, &saved_tio);
	struct termios raw = saved_tio;
	raw.c_lflag &= ~(ICANON | ECHO | ISIG);
	raw.c_iflag &= ~(ICRNL | IXON);
	raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
	tcsetattr(tty_fd, TCSANOW, &raw);
	ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);            /* stop the text console drawing over us */
	sg_clear(&fb, BLACK); flip();
	gen_cracks(); bake_home();
	if (mkdir("/run/silver-greeter", 0700) == 0) { Home h; home_init(&h); intro(&h); }     /* the full intro plays once per boot */
	tcflush(tty_fd, TCIFLUSH);
	if (!account_open(user)) lock_loop();
	run_session();
	return 0;
}
#endif
