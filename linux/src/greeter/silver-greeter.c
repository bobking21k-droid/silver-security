/*
 * silver-greeter -- the SilverOS sign-in and home screen, drawn straight onto /dev/fb0 with sgfx.
 *
 * Intro (once per boot, ~4 s, any key skips):
 *   black, a cold light and dust -> the violet gem glows and pulses -> the emblem's three
 *   silver layers spin in from outside, each locking into place -> a flash as it completes ->
 *   the emblem shrinks into the middle of the screen while the four panes open along clean
 *   lines: SETTINGS, SEARCH, APPS and FILES.
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
#include "../common/emblem.h"

#define WHITE RGB(255, 255, 255)
#define BLACK RGB(0, 0, 0)
#define INK RGB(10, 10, 12)
#define BLOOD RGB(232, 16, 24)

static Font FD, FT, FC;                       /* Silver Scrawl, Inter, Michroma */
static int W, H, scale = 1, scale_pref = 0, fb_w, fb_h, stride, calm, muted;   /* scale_pref: 0 automatic, 1..3 = your choice (Settings > Display) */
static float U;                               /* 1% of the screen height */
static uint32_t *fbmem, *canvas;
static Surf fb;
static int tty_fd = 0, req_fd = -1, back_from_game;
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
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
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
/* The privileged parent only waits for requests while the home screen (a child) runs, so it gives
 * its copies of the screen buffers back: they would otherwise sit in memory twice. The child has
 * its own. (After a Steam session a fresh child needs them again: ui_again.) */
static void ui_release(void)
{
	free(fb.px); fb.px = NULL; free(canvas); canvas = NULL; free(qmap); qmap = NULL;
	free(bg_dark); free(bg_light); free(bg_sec); bg_dark = bg_light = bg_sec = NULL;
}
static int ui_again(void)
{
	fb = sg_surf(malloc((size_t)W * H * 4), W, H);
	qmap = malloc((size_t)W * H); bg_dark = malloc((size_t)W * H * 4); bg_light = malloc((size_t)W * H * 4); bg_sec = malloc((size_t)W * H * 4);
	if (!fb.px || !qmap || !bg_dark || !bg_light || !bg_sec) return -1;
	bake_home();
	return 0;
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
#ifndef RENDER
	{ FILE *sf = fopen("/home/silver/.config/silver/scale", "r"); int sv = 0;      /* your choice, if any */
	  if (sf) { if (fscanf(sf, "%d", &sv) != 1) sv = 0; fclose(sf); }
	  if (sv >= 1 && sv <= 3 && fb_w / sv >= 640 && fb_h / sv >= 360) { scale = sv; scale_pref = sv; } }
#endif
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
			if (c3 == '2') {                               /* F12 is ESC [ 2 4 ~ */
				unsigned char c4 = 0, c5 = 0;
				if (poll(&p, 1, 20) > 0 && read(tty_fd, &c4, 1) == 1 && poll(&p, 1, 20) > 0 && read(tty_fd, &c5, 1) == 1 && c4 == '4' && c5 == '~') return K_SHOT;
				return -1;
			}
			while (c3 >= '0' && c3 <= '9' && poll(&p, 1, 20) > 0) { if (read(tty_fd, &c3, 1) != 1) break; if (c3 == '~') break; }
			return -1;
		}
		return K_ESC;
	}
	if (c == '\r' || c == '\n') return K_ENTER;
	if (c == 8 || c == 127) return K_BACK;
	if (c >= 0xC2 && c < 0xF5) {                                   /* a letter beyond ASCII, as UTF-8 */
		int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1; unsigned cp = c & (0x3F >> more);
		for (int i = 0; i < more; i++) { unsigned char b; if (poll(&p, 1, 30) <= 0 || read(tty_fd, &b, 1) != 1 || (b & 0xC0) != 0x80) return -1; cp = (cp << 6) | (b & 63); }
		return cp >= 0xA0 && cp <= 0x10FFFF ? (int)(K_UCP + cp) : -1;
	}
	return c;
}
#endif

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
	h->mute_on = muted;
}

#define T_GEM 0.25f      /* the gem appears */
#define T_FORM 0.55f     /* the layers start to come in */
#define T_LOCK 1.95f     /* the last layer locks: the emblem is complete */
#define T_OPEN 3.10f     /* the panes open */
#define T_END 3.95f
#include "sound.h"

/* the emblem during the intro at time t (seconds); R is its full size */
static Emblem intro_emblem(float t)
{
	Emblem e = emblem_rest();
	e.t = t;
	/* the three layers: when each starts, which way it spins, where it comes from */
	static const float start[3] = {T_FORM + 0.50f, T_FORM + 0.25f, T_FORM}, spin[3] = {3.6f, -3.0f, 2.4f}, from[3] = {3.2f, 2.4f, 0.15f};
	for (int l = 0; l < 3; l++) {
		float dur = T_LOCK - start[l] - (2 - l) * 0.25f;             /* inner lands first, outer last, at T_LOCK */
		float u = sg_sat((t - start[l]) / dur), eu = sg_ease_out(u);
		e.rot[l] = spin[l] * (1 - eu) * (1 - eu);
		e.scale[l] = sg_lerp(from[l], 1, eu);
		e.alpha[l] = sg_sat(u * 3.0f);
	}
	/* the gem: there first, beating, then flaring when everything locks */
	float lt = t - T_LOCK;
	e.alpha[3] = sg_smooth((t - T_GEM) / 0.35f);
	e.scale[3] = 1;
	if (lt < 0) e.gem = 0.6f + 0.7f * sg_heart(t - T_GEM, 0.82f);
	else e.gem = 1 + 1.4f * sg_sat(1 - lt / 0.7f);
	e.rot[3] = lt < 0 ? -0.6f * (1 - sg_smooth((t - T_GEM) / (T_LOCK - T_GEM))) : 0;
	/* after the lock: two sheens run across it */
	if (lt > 0.05f && lt < 0.75f) e.shine = sg_lerp(-1.4f, 1.4f, (lt - 0.05f) / 0.7f);
	else if (lt > 0.85f && lt < 1.35f) e.shine = sg_lerp(-1.4f, 1.4f, (lt - 0.85f) / 0.5f);
	return e;
}

static void intro(Home *h)
{
	float cx = W / 2.0f, cy = H / 2.0f, RB = sg_min(H * 0.20f, W * 0.17f);   /* the triangle is 3 x RB tall */
	snd('I');
	double t0 = now();
	for (;;) {
		float t = (float)(now() - t0), lt = t - T_LOCK;
		if (t > T_END) break;
		if (key_waiting()) { snd('x'); break; }               /* skipped: stop the sound too */
		sg_clear(&fb, BLACK);
		float open = sg_sat((t - T_OPEN) / 0.75f);
		/* a cold light from above, and dust hanging in it, before the panes appear */
		float lit = sg_sat((t - 0.1f) / 0.6f) * (1 - open);
		if (lit > 0) {
			sg_glow(&fb, cx, cy - H * 0.1f, H * 0.62f, RGB(38, 36, 50), lit);
			sg_glow(&fb, cx, cy, H * 0.30f, RGB(26, 24, 36), lit);
			for (int i = 0; i < 40; i++) {
				float x = sg_fract(i * 0.6180339f + t * 0.010f * (1 + i % 3)) * W, y = sg_fract(i * 0.4142135f - t * 0.018f) * H;
				float d = sg_abs(x - cx) / (W * 0.4f);
				sg_disc(&fb, x, y, U * (0.08f + 0.06f * (i % 3)), WHITE, 0.30f * lit * sg_sat(1 - d));
			}
		}
		home_draw(h, open, t, lt);                            /* the panes, once they open */

		/* the emblem: big in the middle, then shrinking into the centre of the panes */
		Emblem e = intro_emblem(t);
		float k = sg_ease_io(open), R = sg_lerp(RB, HOME_EMBLEM_R, k);
		if (open <= 0) emblem_draw(&fb, cx, cy + R * 0.5f, R, &e);   /* (centred on its height, not its middle) */
		else if (open < 1) center_mark(&e, R, k);       /* (once open, the home screen draws it) */
		if (lt > 0 && lt < 0.7f) {                             /* the lock: a flash of light and a ring going out */
			float q = lt / 0.7f;
			sg_glow(&fb, cx, cy + RB * 0.5f, RB * 1.4f * sg_sat(lt * 6), RGB(170, 120, 255), (calm ? 0.25f : 0.55f) * (1 - q));
			sg_ring(&fb, cx, cy + RB * 0.5f, H * 0.62f * sg_ease_out(q), sg_max(1, U * 0.35f * (1 - q)), WHITE, 0.45f * (1 - q));
			sg_ring(&fb, cx, cy + RB * 0.5f, H * 0.48f * sg_ease_out(q), sg_max(1, U * 0.22f * (1 - q)), RGB(150, 100, 255), 0.45f * (1 - q));
		}
		if (!calm && lt > 0) {                                  /* a small thump, and a short soft flash */
			float th = sg_sat(1 - lt / 0.30f);
			shake_frame(sg_sin(t * 97) * U * 0.5f * th * th, sg_cos(t * 83) * U * 0.5f * th * th);
			if (lt < 0.06f) sg_rect(&fb, 0, 0, W, H, WHITE, 0.35f * (1 - lt / 0.06f));
		}
		if (t < T_LOCK) sg_text_c(&fb, &FC, W / 2.0f, H - U * 3.4f, "CONTAINS FLASHING LIGHTS  /  ANY KEY SKIPS", U * 0.9f, WHITE, 0.5f * sg_sat((T_FORM + 1.0f - t) * 3) * sg_sat((t - 0.15f) * 4), 0.26f);
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

/* the glass, with a password card over it */
static void lock_frame(float alpha, float shake)
{
	memcpy(fb.px, bg_dark, (size_t)W * H * 4);
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
		if (c == K_BACK) { if (n) ucp_pop(dst); continue; }
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
		if (ucp_ok(c) && n + 4 < cap) { ucp_add(dst, cap + 1, c); status = ""; }
	}
}
#endif

/* ================================================================ home: input handling */
/* "V75" / "L40": a level for the privileged side (volume, brightness) */
static void request_val(char c, int v)
{
#ifndef RENDER
	char m[16]; int n = snprintf(m, sizeof m, "%c%d\n", c, v);
	if (req_fd >= 0 && n > 0) { if (write(req_fd, m, (size_t)n) < 0) { /* parent gone */ } }
#else
	(void)c; (void)v;
#endif
}
/* "Kde": a short name for the privileged side (a keyboard layout) */
static void request_name(char c, const char *name)
{
#ifndef RENDER
	char m[40]; int n = snprintf(m, sizeof m, "%c%s\n", c, name);
	if (req_fd >= 0 && n > 0 && n < (int)sizeof m) { if (write(req_fd, m, (size_t)n) < 0) { /* parent gone */ } }
#else
	(void)c; (void)name;
#endif
}
static void request_game(const char *action, const char *app)
{
#ifndef RENDER
	char m[48]; int n = snprintf(m, sizeof m, "G%s %s\n", action, app);
	if (req_fd >= 0 && n > 0 && n < (int)sizeof m) { if (write(req_fd, m, (size_t)n) < 0) { /* parent gone */ } }
#else
	(void)app; steam_launch_t = ui_t; snprintf(steam_launch_what, sizeof steam_launch_what, "%s", action);
#endif
}
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
	if (s == 2) { s_age = 0; sug.sel = -1; sug.hidden = 0; h->focus = yt_count(&yt) ? 1 : 0; if (!yt.fetch[0]) snprintf(yt.fetch, sizeof yt.fetch, "/sbin/silver-fetch"); if (yt.state == Y_WATCH) yt.state = Y_RESULTS; }
	h->fs = 0;
	if (s == 3) h->sub = 0;
}
/*
 * First run: on the first start of an installed SilverOS the home screen walks through Keyboard,
 * Time and Network (the pages from Settings; Esc moves to the next), then remembers it did so in
 * ~/.config/silver/welcomed.
 */
static int wel;                                                         /* 0 not running, else 1 Keyboard, 2 Time, 3 Network */
static void wel_next(Home *h)
{
	if (!wel) return;
	if (++wel == 2) { h->sub = 3; tz_open(); }
	else if (wel == 3) { h->sub = 1; wf_open(); }
	else {
		wel = 0; h->screen = 0; h->fade = 0;
#ifndef RENDER
		char p[300]; snprintf(p, sizeof p, "%s/.config", homedir); mkdir(p, 0755);
		snprintf(p, sizeof p, "%s/.config/silver", homedir); mkdir(p, 0755);
		snprintf(p, sizeof p, "%s/.config/silver/welcomed", homedir);
		FILE *f = fopen(p, "w"); if (f) { fputs("1\n", f); fclose(f); }
#endif
	}
}
static void wel_start(Home *h)
{
#ifndef RENDER
	char p[300]; snprintf(p, sizeof p, "%s/.config/silver/welcomed", homedir);
	if (access("/etc/silver-installed", F_OK) != 0 || access(p, F_OK) == 0) return;      /* the live USB, or already welcomed */
	wel = 1; open_screen(h, 1); h->sub = 4; kb_open();
#else
	(void)h;
#endif
}
static void wel_draw(void)
{
	if (!wel) return;
	char b[80]; snprintf(b, sizeof b, "WELCOME  -  STEP %d OF 3  -  ESC FOR THE NEXT", wel);
	float w = sg_text_w(&FC, b, U * 1.1f, 0.2f) + U * 4, x = W - w - U * 3, y = U * 3;
	ui_frost(x, y, w, U * 3.6f, U * 1.8f, 0.8f);
	sg_text(&fb, &FC, x + U * 2, y + U * 2.3f, b, U * 1.1f, WHITE, 1, 0.2f);
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
/* sound effects on/off: remembered in ~/.config/silver/mute, and (through the privileged parent)
 * in a firmware variable, so the boot chooser stays quiet too */
static void mute_toggle(Home *h)
{
	h->mute_on ^= 1;
#ifndef RENDER
	char p[256];
	snprintf(p, sizeof p, "%s/.config", homedir); mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/.config/silver", homedir); mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/.config/silver/mute", homedir);
	if (h->mute_on) { int fd = open(p, O_WRONLY | O_CREAT, 0644); if (fd >= 0) close(fd); } else unlink(p);
#endif
	if (h->mute_on) snd('M'); else { snd('U'); snd('o'); }
	request(h->mute_on ? 'S' : 's');
	muted = h->mute_on;
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

/* run a search: remember it, close the list, ask the site */
static void search_go(Home *h)
{
	recent_add(yt.q);
	sug.sel = -1; sug.hidden = 1;
	yt_search(&yt);
	h->focus = yt.state == Y_RESULTS ? 1 : 0;
}
#include "buttons.h"
#include "shot.h"
#include "fileops.h"

static void handle_key(Home *h, int k)
{
	if (pwr_show) {                                                          /* the "Shut down?" panel has the keys */
		if (k == K_ENTER) request('P');
		else if (k == K_ESC || k == K_BACK) pwr_show = 0;
		return;
	}
	switch (h->screen) {
	case 0:
		if (k == K_LEFT) h->sel &= ~1;
		else if (k == K_RIGHT) h->sel |= 1;
		else if (k == K_UP) h->sel &= ~2;
		else if (k == K_DOWN) h->sel |= 2;
		else if (k == K_ENTER) open_screen(h, h->sel + 1);
		break;
	case 1:                                                                     /* settings */
		if (h->sub == 1) { if (wf_key(k)) { h->sub = 0; wel_next(h); } break; }                 /* Network */
		if (h->sub == 2) { if (bt_key(k)) h->sub = 0; break; }                 /* Bluetooth */
		if (h->sub == 3) { if (tz_key(k)) { h->sub = 0; wel_next(h); } break; }                 /* Time */
		if (h->sub == 4) { if (kb_key(k)) { h->sub = 0; wel_next(h); } break; }                 /* Keyboard */
		if (h->sub == 5) { if (ins_key(k)) h->sub = 0; break; }                 /* Install */
		if ((k == K_LEFT || k == K_RIGHT) && (h->row == R_BRIGHT || h->row == R_VOL)) { slider_adjust(h, k == K_RIGHT ? 5 : -5); break; }
		if (k == K_ESC || k == K_LEFT || k == K_BACK) { h->screen = 0; h->fade = 0; }
		else if (k == K_UP && h->row > 0) { h->row--; h->confirm = 0; }
		else if (k == K_DOWN && h->row < R_N - 1) { h->row++; h->confirm = 0; }
		else if (k == K_ENTER) {
			if (h->row == R_NET) { h->sub = 1; wf_open(); }
			else if (h->row == R_BT) { h->sub = 2; bt_open(); }
			else if (h->row == R_TIME) { h->sub = 3; tz_open(); }
			else if (h->row == R_KEYB) { h->sub = 4; kb_open(); }
			else if (h->row == R_DISP) scale_cycle();
			else if (h->row == R_CALM) calm_toggle(h);
			else if (h->row == R_SOUND) mute_toggle(h);
			else if (h->row == R_WIN && ins_live()) { h->sub = 5; ins_open(); }
			else if (h->row == R_WIN) { if (win_ok()) { if (h->confirm == R_WIN) request('N'); else h->confirm = R_WIN; } }      /* Windows: asks twice */
			else if (h->row == R_RESTART || h->row == R_OFF) { if (h->confirm == h->row) request(h->row == R_RESTART ? 'R' : 'P'); else h->confirm = h->row; }
		}
		break;
	case 2: {                                                                   /* search: YouTube */
		Yt *y = &yt;
		if (y->state == Y_WATCH) {
			if (k == K_ESC || k == K_BACK || k == K_LEFT) y->state = Y_RESULTS;
			else if ((k == 'd' || k == 'D') && !y->mode) dl_start(y->web[y->watch].url);
			break;
		}
		if (h->focus == 2) { if (k == K_UP) { h->focus = yt_count(y) ? 1 : 0; break; } else if (k == K_ENTER) { open_screen(h, 1); break; } else if (k == K_ESC) { h->screen = 0; h->fade = 0; break; } else h->focus = 0; if (h->focus == 2) break; }
		if (h->focus == 0 && sug_visible(h)) {                              /* the list under the box */
			SugItem it[MAXSUG + 4 + MAXTREND]; int n = sug_items(y->q, it, (int)(sizeof it / sizeof it[0]));
			if (n) {
				if (k == K_DOWN) { if (sug.sel + 1 < n) sug.sel++; else { sug.sel = -1; sug.hidden = 1; h->focus = y->state == Y_RESULTS ? 1 : 2; } break; }
				if (k == K_UP && sug.sel >= 0) { sug.sel--; break; }
				if (k == K_ENTER && sug.sel >= 0) { snprintf(y->q, sizeof y->q, "%s", it[sug.sel].text); search_go(h); break; }
				if (k == K_BACK && sug.sel >= 0 && it[sug.sel].kind == 1) {     /* forget one of your recent searches */
					for (int r = 0; r < sug.nrecent; r++) if (!strcmp(sug.recent[r], it[sug.sel].text)) { recent_forget(r); break; }
					if (sug.sel > 0) sug.sel--;
					break;
				}
				if (k == K_ESC && (sug.sel >= 0 || y->q[0])) { sug.sel = -1; sug.hidden = 1; break; }
			}
		}
		if (k == K_ESC) { h->screen = 0; h->fade = 0; break; }
		if (k == K_DOWN && h->focus == 0 && y->state == Y_RESULTS) { h->focus = 1; break; }
		if (k == K_DOWN && h->focus == 0 && y->state != Y_RESULTS && !y->q[0]) { h->focus = 2; break; }
		if (h->focus == 1 && y->mode) {                                       /* the video grid */
			int n = yt_count(y);
			if (k == K_UP) { if (y->sel >= GRID_COLS) y->sel -= GRID_COLS; else h->focus = 0; break; }
			if (k == K_DOWN) { if (y->sel + GRID_COLS < n) y->sel += GRID_COLS; else if (y->sel / GRID_COLS < (n - 1) / GRID_COLS) y->sel = n - 1; else h->focus = 2; break; }
			if (k == K_LEFT) { if (y->sel > 0) y->sel--; break; }
			if (k == K_RIGHT) { if (y->sel + 1 < n) y->sel++; break; }
		}
		if (h->focus == 1) {
			if (k == K_UP) { if (y->sel > 0) y->sel--; else h->focus = 0; break; }
			if (k == K_DOWN) { if (y->sel + 1 < yt_count(y)) y->sel++; else h->focus = 2; break; }
			if (k == K_ENTER) { y->watch = y->sel; y->state = Y_WATCH; break; }
		}
		if (k == 9) { y->mode ^= 1; if (y->q[0] && y->state != Y_IDLE) search_go(h); else sug_typed(); break; }
		size_t n = strlen(y->q);
		if (k == K_BACK && n) { ucp_pop(y->q); h->focus = 0; sug_typed(); }
		else if (k == 21) { y->q[0] = 0; h->focus = 0; sug_typed(); }
		else if (ucp_ok(k)) { ucp_add(y->q, sizeof y->q, k); h->focus = 0; sug_typed(); }
		else if (k == K_ENTER && y->q[0]) search_go(h);
		break;
	}
	case 3:                                                                     /* apps */
		apps_key(h, k, ui_t);
		break;
	case 4:                                                                     /* files */
		if (h->fs) {
			Search *sr = &h->sr; size_t n = strlen(sr->q);
			if (k == K_ESC) { h->fs = 0; h->row = 0; h->top = 0; files_preview(h); }
			else if (k == 9) { h->fs_scope ^= 1; fs_refresh(h); }
			else if (k == K_UP && h->row > 0) h->row--;
			else if (k == K_DOWN && h->row + 1 < search_count(sr)) h->row++;
			else if (k == K_BACK && n) { ucp_pop(sr->q); fs_refresh(h); }
			else if (k == 21) { sr->q[0] = 0; fs_refresh(h); }
			else if (ucp_ok(k) && n < 38) { ucp_add(sr->q, sizeof sr->q, k); fs_refresh(h); }
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
		if (fo_key(h, k)) break;                                                 /* copy, paste, delete, pictures ... */
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

/* every key press makes a sound that fits what it did */
static void handle_key_snd(Home *h, int k)
{
	Home o = *h; Yt yo; memcpy(&yo, &yt, sizeof yo); int ssel0 = sug.sel, wsel0 = wf.sel, bsel0 = bt.sel, zsel0 = tzs.sel, wtyp0 = wf.typing; size_t wpw0 = strlen(wf.pw);
	char cwd0[256]; snprintf(cwd0, sizeof cwd0, "%s", cwd);
	char fq0[64]; snprintf(fq0, sizeof fq0, "%s", h->sr.q);
	handle_key(h, k);
	if (h->screen != o.screen) snd(h->screen ? 'o' : 'b');
	else if (yt.state != yo.state && (yt.state == Y_WATCH || yo.state == Y_WATCH)) snd(yt.state == Y_WATCH ? 'o' : 'b');
	else if (h->fs != o.fs || h->sub != o.sub) snd((h->fs > o.fs || h->sub > o.sub) ? 'o' : 'b');
	else if (strcmp(cwd, cwd0)) snd(strlen(cwd) > strlen(cwd0) ? 'o' : 'b');
	else if (wf.typing != wtyp0) snd(wf.typing ? 'o' : 'b');
	else if (strcmp(yt.q, yo.q) || strcmp(h->sr.q, fq0) || strlen(wf.pw) != wpw0) snd('k');
	else if (yt.state == Y_LOADING && yo.state != Y_LOADING) snd('o');
	else if (h->sel != o.sel || h->row != o.row || h->focus != o.focus || yt.sel != yo.sel || yt.mode != yo.mode || h->fs_scope != o.fs_scope || sug.sel != ssel0 || wf.sel != wsel0 || bt.sel != bsel0 || tzs.sel != zsel0) snd('m');
	else if (h->calm_on != o.calm_on || h->confirm != o.confirm) snd('o');
}

#include "mouse.h"

/* ---- idle: dim after a while, switch the screen off after longer, wake on any key or mouse move.
 * Times (seconds) can be changed in ~/.config/silver/idle (two numbers: dim off). ---- */
static int idle_state;                                 /* 0 awake, 1 dimmed, 2 screen off */
static int idle_saved_bri = -2;
static int idle_dim_s = 120, idle_off_s = 600;
static void idle_read_config(void)
{
	char p[300]; snprintf(p, sizeof p, "%s/.config/silver/idle", homedir);
	FILE *f = fopen(p, "r"); int a, b;
	if (f) { if (fscanf(f, "%d %d", &a, &b) == 2 && a >= 3 && b > a) { idle_dim_s = a; idle_off_s = b; } fclose(f); }
}
static void idle_set(int st)
{
	if (st == idle_state) return;
	int was = idle_state; idle_state = st;
	if (st >= 1 && was == 0) { idle_saved_bri = bl_value("max_brightness") > 0 ? bl_value("brightness") * 100 / bl_value("max_brightness") : -1; if (idle_saved_bri > 8) request_val('L', idle_saved_bri / 4 < 3 ? 3 : idle_saved_bri / 4); }
	if (st == 2) request('X');
	if (st == 0) {
		if (was == 2) request('x');
		if (idle_saved_bri > 0) request_val('L', idle_saved_bri);
		idle_saved_bri = -2;
	}
}
/* battery: the first one, and whether it is draining */
static int battery_percent(int *draining)
{
	DIR *d = opendir("/sys/class/power_supply"); struct dirent *e; int pct = -1; *draining = 0;
	while (d && (e = readdir(d))) {
		if (strncmp(e->d_name, "BAT", 3)) continue;
		char p[300], st[32] = ""; FILE *f;
		snprintf(p, sizeof p, "/sys/class/power_supply/%s/capacity", e->d_name);
		if ((f = fopen(p, "r"))) { if (fscanf(f, "%d", &pct) != 1) pct = -1; fclose(f); }
		snprintf(p, sizeof p, "/sys/class/power_supply/%s/status", e->d_name);
		if ((f = fopen(p, "r"))) { if (!fgets(st, sizeof st, f)) st[0] = 0; fclose(f); }
		*draining = !strncmp(st, "Discharging", 11);
		break;
	}
	if (d) closedir(d);
	return pct;
}
static void battery_draw(float t)
{
	static double chk; static int pct = -1, drain;
	if (now() - chk > 10) { pct = battery_percent(&drain); chk = now(); }
	if (pct < 0 || !drain || pct > 15) return;
	char m[32]; snprintf(m, sizeof m, "BATTERY LOW  %d%%", pct);
	float w = sg_text_w(&FC, m, U * 1.2f, 0.2f) + U * 4, x = W - w - U * 3, y = U * 2.2f;
	sg_rrect(&fb, x, y, w, U * 3.6f, U * 1.8f, 0, RGB(200, 40, 40), 0.55f + 0.4f * (pct <= 5 ? 0.5f + 0.5f * sg_sin(t * 6) : 1));
	sg_text(&fb, &FC, x + U * 2, y + U * 2.4f, m, U * 1.2f, WHITE, 1, 0.2f);
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
	frost_static = h->screen == 1 || h->screen == 3 || h->screen == 4;   /* these screens sit on still glass */
	{ int before = yt.state; yt_poll(&yt); if (before == Y_LOADING && yt.state == Y_RESULTS) snd('r'); else if (before == Y_LOADING && yt.state == Y_ERROR) snd('e'); }
	s_age += dt;
	ui_t = t;
	{ int was = dl.state; dl_poll(); if (was == DL_RUN && dl.state == DL_DONE) snd('r'); else if (was == DL_RUN && dl.state == DL_FAIL) snd('e'); }
	if (h->fs) search_tick(&h->sr, 0.004);
	fo_poll(h);
	switch (h->screen) {
	case 0: home_draw(h, 1, t, 99); break;
	case 1: draw_settings(h, t); break;
	case 2: draw_search(h, t); break;
	case 3: if (h->sub == 1 || h->sub == 4) sample_sys(h, t); draw_apps(h, t); break;
	case 4: if (fo.view) fo_view_draw(); else if (h->fs) draw_files_search(h, t); else draw_files(h, t); break;
	}
	if (h->screen && h->fade < 1) sg_rect(&fb, 0, 0, W, H, BLACK, 1 - h->fade);
	else if (!h->screen && h->fade < 1) sg_rect(&fb, 0, 0, W, H, BLACK, 1 - h->fade);
	battery_draw(t);
	wel_draw();
	shot_draw();
	pwr_draw(t);
}

#ifdef RENDER
/* the demo's keystrokes, by seconds after the home screen opens */
static const struct { float t; int k; } SCRIPT[] = {
	{0.8f, K_RIGHT}, {1.5f, K_DOWN}, {2.4f, K_LEFT}, {3.0f, K_UP}, {3.7f, K_RIGHT}, {4.3f, K_DOWN},          /* look around, end on Files */
	{5.0f, K_ENTER},                                                                                          /* Files */
	{6.0f, K_ENTER}, {7.0f, K_DOWN}, {7.8f, K_DOWN}, {8.6f, K_BACK}, {9.6f, K_DOWN}, {10.2f, K_DOWN},
	{11.2f, '/'}, {12.0f, 'r'}, {12.4f, 'e'}, {12.8f, 'p'}, {14.0f, K_DOWN}, {15.0f, K_ESC}, {15.8f, K_ESC},  /* search inside Files */
	{16.6f, K_UP}, {17.3f, K_ENTER},                                                                          /* Search = YouTube */
	{20.2f, 'l'}, {20.5f, 'o'}, {20.8f, 'f'}, {21.1f, 'i'}, {21.6f, K_DOWN}, {21.9f, K_ENTER},             /* suggestions, web results */
	{24.0f, K_DOWN}, {24.8f, K_DOWN}, {25.6f, K_ENTER}, {26.8f, 'd'}, {29.4f, K_ESC},                          /* a result, downloaded */
	{30.2f, 9}, {32.2f, K_DOWN}, {33.2f, K_RIGHT}, {34.2f, K_ENTER}, {37.4f, K_ESC},                          /* videos, a watch page */
	{38.4f, K_DOWN}, {39.9f, K_RIGHT}, {41.4f, K_UP}, {42.9f, K_UP}, {44.2f, K_ESC},                          /* linger: the rooms loop behind */
	{45.0f, K_LEFT}, {45.7f, K_ENTER},                                                                        /* Settings */
	{46.4f, K_ENTER}, {47.4f, K_DOWN}, {48.0f, K_UP}, {48.8f, K_ENTER},                                    /* Network: join Wi-Fi */
	{49.4f, 's'}, {49.6f, 'i'}, {49.8f, 'l'}, {50.0f, 'v'}, {50.2f, 'e'}, {50.4f, 'r'}, {50.6f, '2'}, {50.8f, '0'}, {51.0f, '2'}, {51.2f, '6'}, {51.8f, K_ENTER}, {55.2f, K_ESC},
	{55.8f, K_DOWN}, {56.4f, K_ENTER}, {57.6f, K_DOWN}, {58.2f, K_DOWN}, {58.8f, K_ENTER}, {60.2f, K_ESC},      /* Bluetooth: pair a controller */
	{60.6f, K_DOWN}, {60.9f, K_DOWN}, {61.2f, K_DOWN}, {61.5f, K_DOWN}, {62.0f, K_ENTER}, {63.2f, K_DOWN}, {63.8f, K_DOWN}, {64.5f, K_ENTER}, {66.0f, K_ESC},         /* Time: pick a zone */
	{66.4f, K_UP}, {66.8f, K_LEFT}, {67.1f, K_LEFT}, {67.4f, K_RIGHT}, {67.8f, K_DOWN}, {68.1f, K_DOWN}, {68.4f, K_DOWN}, {68.65f, K_DOWN}, {68.9f, K_ENTER}, {70.4f, K_ENTER}, {71.4f, K_ESC},     /* volume slider, sound effects off, on */
	{72.2f, K_DOWN}, {72.9f, K_ENTER},                                                                        /* Apps */
	{74.0f, K_RIGHT}, {74.8f, K_ENTER},                                                                       /* Game Store: three stores */
	{76.0f, K_RIGHT}, {77.2f, K_RIGHT}, {78.4f, K_LEFT}, {79.4f, K_LEFT}, {80.2f, K_ENTER},                  /* SilverOS games */
	{81.2f, K_RIGHT}, {82.0f, K_LEFT}, {82.8f, K_ENTER},                                                      /* play Snake */
	{83.4f, K_DOWN}, {84.2f, K_RIGHT}, {85.0f, K_UP}, {85.8f, K_RIGHT}, {86.8f, K_ESC}, {87.6f, K_ESC},
	{88.4f, K_RIGHT}, {89.2f, K_ENTER}, {90.8f, K_RIGHT}, {91.6f, K_DOWN}, {92.4f, K_ENTER}, {93.0f, 'w'}, {93.6f, K_ENTER}, {96.2f, 'p'}, {97.8f, K_ESC}, {98.5f, K_ESC}, {98.9f, K_ESC},                   /* Steam: wishlist, install with Steam */
	{100.0f, K_RIGHT}, {100.8f, K_ENTER}, {102.4f, K_RIGHT}, {103.2f, K_ENTER}, {105.4f, K_ESC}, {106.2f, K_ESC}, {107.0f, K_ESC},                   /* Microsoft Store */
	{107.8f, K_RIGHT}, {108.6f, K_ENTER}, {112.2f, K_ESC}, {113.0f, K_ESC},                                       /* Me and my PC */
};
#define SCRIPT_END 114.5f
/* where the script must have got to (-1: don't care); the demo fails if any is wrong */
static const struct { float t; int screen, sub, open, tab, mute; const char *what; } EXPECT[] = {
	{5.6f, 4, -1, -1, -1, -1, "Files opens"}, {16.2f, 0, -1, -1, -1, -1, "back home from Files"},
	{17.8f, 2, -1, -1, -1, -1, "Search opens"}, {46.2f, 1, -1, -1, -1, -1, "Settings opens"}, {47.0f, 1, 1, -1, -1, -1, "Network page"}, {55.6f, 1, 0, -1, -1, -1, "back to Settings"}, {57.0f, 1, 2, -1, -1, -1, "Bluetooth page"}, {60.6f, 1, 0, -1, -1, -1, "Settings again"},
	{64.0f, 1, 3, -1, -1, -1, "Time page"}, {69.7f, 1, -1, -1, -1, 1, "Sound effects off"}, {71.0f, 1, -1, -1, -1, 0, "Sound effects on"},
	{73.5f, 3, 0, -1, -1, -1, "Apps grid"}, {75.2f, 3, 5, 0, -1, -1, "Game Store front"},
	{80.6f, 3, 5, 1, 0, -1, "SilverOS games"}, {83.2f, 3, 6, -1, -1, -1, "playing Snake"},
	{87.2f, 3, 5, 1, 0, -1, "back from Snake"}, {88.0f, 3, 5, 0, -1, -1, "back to the front"},
	{89.6f, 3, 5, 1, 1, -1, "Steam"}, {94.6f, 3, 5, 1, 1, -1, "Steam: install"}, {99.6f, 3, 5, 0, -1, -1, "front again"},
	{101.2f, 3, 5, 1, 2, -1, "Microsoft Store"}, {107.4f, 3, 0, -1, -1, -1, "Apps grid again"},
	{109.2f, 3, 4, -1, -1, -1, "Me and my PC"}, {113.8f, 0, -1, -1, -1, -1, "home at the end"},
};
static int expect_failed;
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
		while (si < sizeof SCRIPT / sizeof SCRIPT[0] && SCRIPT[si].t <= t) handle_key_snd(h, SCRIPT[si++].k);
		static unsigned ei;
		while (ei < sizeof EXPECT / sizeof EXPECT[0] && EXPECT[ei].t <= t) {
			int ok = (EXPECT[ei].screen < 0 || h->screen == EXPECT[ei].screen) && (EXPECT[ei].sub < 0 || h->sub == EXPECT[ei].sub) &&
				 (EXPECT[ei].open < 0 || gs.open == EXPECT[ei].open) && (EXPECT[ei].tab < 0 || gs.tab == EXPECT[ei].tab) && (EXPECT[ei].mute < 0 || h->mute_on == EXPECT[ei].mute);
			if (!ok) { fprintf(stderr, "CHECK FAILED at %.1fs: %s (screen %d sub %d open %d tab %d mute %d)\n", EXPECT[ei].t, EXPECT[ei].what, h->screen, h->sub, gs.open, gs.tab, h->mute_on); expect_failed = 1; }
			ei++;
		}
		if (t > SCRIPT_END) return;
#else
		int k;
		static double idle_cfg;
		if (!idle_cfg) act_t = now();
		if (now() - idle_cfg > 5) { idle_cfg = now(); idle_read_config(); }                 /* (re-read every few seconds) */
		while ((k = read_key(0)) != -1) {
			act_t = now();
			if (k == K_SHOT) { if (!idle_state) shot_take(); continue; }
			if (idle_state) idle_set(0);                     /* the key that woke it does nothing else */
			else handle_key_snd(h, k);
		}
		double prev_act = act_t;
		mouse_poll(h);
		if (buttons_poll(h)) act_t = now();
		if (act_t != prev_act && idle_state) idle_set(0);
		{ double idle = now() - act_t; int want = idle > idle_off_s ? 2 : idle > idle_dim_s ? 1 : 0; if (want > idle_state) idle_set(want); }
		if (pwr_show) act_t = now();
		if (idle_state == 2) { sg_clear(&fb, BLACK); flip(); usleep(300000); continue; }      /* screen off: nothing to draw */
#endif
#ifdef RENDER
		static const char *prof_env; static int prof_init; if (!prof_init) { prof_env = getenv("SILVER_PROFILE"); prof_init = 1; }
		struct timespec p0, p1; if (prof_env) clock_gettime(CLOCK_MONOTONIC, &p0);
#endif
		home_frame(h, t, dt > 0.2f ? 0.2f : dt);
#ifdef RENDER
		if (prof_env) {                                   /* how long each kind of screen takes to draw */
			clock_gettime(CLOCK_MONOTONIC, &p1);
			double ms = (p1.tv_sec - p0.tv_sec) * 1e3 + (p1.tv_nsec - p0.tv_nsec) / 1e6;
			static const char *nm[] = {"home", "settings", "search", "apps", "files"};
			char key[48]; snprintf(key, sizeof key, "%s%s%d", nm[h->screen], h->screen == 3 ? "/sub" : "", h->screen == 3 ? h->sub * 10 + gs.open : 0);
			static struct { char k[48]; double sum, max; int n; } st[32]; static int ns;
			int i; for (i = 0; i < ns && strcmp(st[i].k, key); i++) {}
			if (i == ns && ns < 32) { snprintf(st[ns].k, sizeof st[ns].k, "%s", key); ns++; }
			if (i < 32) { st[i].sum += ms; st[i].n++; if (ms > st[i].max) st[i].max = ms; }
			if (t + 0.04f > SCRIPT_END) for (int k = 0; k < ns; k++) fprintf(stderr, "%-16s frames %4d  avg %6.1f ms  max %6.1f ms\n", st[k].k, st[k].n, st[k].sum / st[k].n, st[k].max);
		}
#endif
#ifndef RENDER
		if (idle_state == 1) sg_rect(&fb, 0, 0, W, H, BLACK, 0.55f);     /* (also dims screens with no backlight control) */
#endif
		mouse_draw();
		flip();
#ifndef RENDER
		if (!key_waiting()) usleep(idle_state == 1 ? 120000 : 24000);
#endif
	}
}

/* ================================================================ session */
#ifndef RENDER
#include <linux/fs.h>
#include <sys/mount.h>
/* Tell the boot chooser to stay quiet (or not): a small firmware variable it reads at start.
 * Runs in the privileged parent. Quietly does nothing on BIOS machines. */
#define EFI_GUID_STR "6c5c3c2e-6b0a-4f3c-9a7e-2f1d0c9b8a71"
#define MUTE_VAR "/sys/firmware/efi/efivars/SilverMute-" EFI_GUID_STR
#define NEXT_VAR "/sys/firmware/efi/efivars/SilverNext-" EFI_GUID_STR
/* screen brightness: every backlight (a laptop has one), 0 to 100 percent, never fully off */
static void backlight_set(int pct)
{
	DIR *d = opendir("/sys/class/backlight"); struct dirent *e;
	while (d && (e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[300]; int max = 0;
		snprintf(p, sizeof p, "/sys/class/backlight/%s/max_brightness", e->d_name);
		FILE *f = fopen(p, "r"); if (f) { if (fscanf(f, "%d", &max) != 1) max = 0; fclose(f); }
		if (max <= 0) continue;
		int v = max * pct / 100; if (v < 1) v = 1;
		snprintf(p, sizeof p, "/sys/class/backlight/%s/brightness", e->d_name);
		f = fopen(p, "w"); if (f) { fprintf(f, "%d\n", v); fclose(f); }
	}
	if (d) closedir(d);
}
/* load a keyboard layout into the kernel (all consoles share it); returns 1 if it was applied */
static int keymap_apply(const char *id)
{
	if (!*id) return 0;
	for (const char *c = id; *c; c++) if (!((*c >= 'a' && *c <= 'z') || *c == '-')) return 0;
	char path[80]; snprintf(path, sizeof path, "/usr/share/keymaps/%s.bkmap", id);
	int fd = open(path, O_RDONLY);
	if (fd < 0) return 0;
	pid_t p = fork();
	if (p == 0) { dup2(fd, 0); execl("/bin/busybox", "loadkmap", (char *)NULL); _exit(127); }
	close(fd);
	if (p > 0) { int st; waitpid(p, &st, 0); return WIFEXITED(st) && WEXITSTATUS(st) == 0; }
	return 0;
}
/* your saved layout, at start */
static void keymap_apply_saved(void)
{
	char p[300], id[24] = "";
	snprintf(p, sizeof p, "%s/.config/silver/keymap", homedir);
	FILE *f = fopen(p, "r"); if (f) { if (fgets(id, sizeof id, f)) id[strcspn(id, "\r\n")] = 0; fclose(f); }
	if (id[0] && strcmp(id, "us")) keymap_apply(id);
}
/* opt-in: ~/.config/silver/lid says "sleep" -> closing the lid puts the computer to sleep (and it wakes when you open it) */
static int lid_sleep_wanted(void)
{
	struct passwd *pw = getpwnam(user); char p[320], w[16] = "";
	if (!pw) return 0;
	snprintf(p, sizeof p, "%s/.config/silver/lid", pw->pw_dir);
	int fd = open(p, O_RDONLY | O_NOFOLLOW); if (fd < 0) return 0;
	ssize_t n = read(fd, w, sizeof w - 1); close(fd);
	return n >= 5 && !strncmp(w, "sleep", 5);
}
static void lid_go_to_sleep(void)
{
	char st[64] = ""; int fd = open("/sys/power/state", O_RDWR); if (fd < 0) return;
	ssize_t n = read(fd, st, sizeof st - 1); if (n < 0) n = 0; st[n] = 0;
	const char *mode = strstr(st, "freeze") ? "freeze" : strstr(st, "mem") ? "mem" : NULL;
	if (mode && write(fd, mode, strlen(mode)) < 0) { /* it refused: stay awake */ }
	close(fd);                                        /* (the write returns when the computer wakes up) */
}
/* the screen's backlight on or off (lid closed / opened); machines without a backlight are left alone */
static void backlight_power(int on)
{
	static int slept;
	if (on) slept = 0;
	else if (!slept && lid_sleep_wanted()) slept = 1;
	int sleep_now = !on && slept == 1; if (sleep_now) slept = 2;
	DIR *d = opendir("/sys/class/backlight"); struct dirent *e;
	while (d && (e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[300]; snprintf(p, sizeof p, "/sys/class/backlight/%s/bl_power", e->d_name);
		FILE *f = fopen(p, "w"); if (f) { fprintf(f, "%d\n", on ? 0 : 4); fclose(f); }
	}
	if (d) closedir(d);
	if (sleep_now) { lid_go_to_sleep(); backlight_power(1); }
}
/* a one-byte firmware variable the boot chooser reads: SilverMute (1 = quiet) or SilverNext (2 = start Windows once) */
static void efi_var(const char *path, int val)
{
	if (access("/sys/firmware/efi", F_OK)) return;
	if (access("/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c", F_OK) &&
	    access("/sys/firmware/efi/efivars/BootOrder-8be4df61-93ca-11d2-aa0d-00e098032b8c", F_OK))
		mount("efivarfs", "/sys/firmware/efi/efivars", "efivarfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
	int fd = open(path, O_RDONLY);
	if (fd >= 0) {                                    /* existing variables are immutable until told otherwise */
		int flags = 0;
		if (ioctl(fd, FS_IOC_GETFLAGS, &flags) == 0 && (flags & FS_IMMUTABLE_FL)) { flags &= ~FS_IMMUTABLE_FL; ioctl(fd, FS_IOC_SETFLAGS, &flags); }
		close(fd);
	}
	unsigned char rec[5] = {7, 0, 0, 0, (unsigned char)val};   /* non-volatile, boot + runtime access */
	fd = open(path, O_WRONLY | O_CREAT, 0644);
	if (fd >= 0) { if (write(fd, rec, sizeof rec) != (ssize_t)sizeof rec) { /* firmware said no: nothing more to do */ } close(fd); }
}
static void efi_mute(int on) { efi_var(MUTE_VAR, on ? 1 : 0); }
/* "Play in Windows": only on an installed SilverOS that found Windows (the installer records it) */
static int windows_installed(void)
{
	FILE *f = fopen("/etc/silver-installed", "r");
	char l[160]; int yes = 0;
	if (f) { while (fgets(l, sizeof l, f)) if (!strncmp(l, "windows=yes", 11)) yes = 1; fclose(f); }
	return yes;
}

/* Steam: hand the screen, keyboard and sound card to the games layer's session (Xorg + the
 * Steam app, /usr/lib/silver/steam-session), as the user, and come back to the home screen when
 * it ends. The request is "G<action> <appid>\n" from the interface: only these actions, digits. */
static int game_ok(const char *r)
{
	static const char *act[] = {"open", "install", "play", "store"};
	for (unsigned i = 0; i < sizeof act / sizeof act[0]; i++) {
		size_t n = strlen(act[i]);
		if (strncmp(r, act[i], n) || (r[n] != ' ' && r[n] != 0)) continue;
		const char *d = r[n] ? r + n + 1 : r + n;
		if (strlen(d) > 10) return 0;
		for (; *d; d++) if (*d < '0' || *d > '9') return 0;
		return 1;
	}
	return 0;
}
static void run_game(struct passwd *pw, char *req)
{
	if (!game_ok(req) || access("/usr/lib/silver/steam-session", X_OK)) return;
	char action[16] = "", app[16] = "";
	if (sscanf(req, "%15s %15s", action, app) < 1) return;
	snd('q'); if (snd_fd >= 0) { close(snd_fd); snd_fd = -1; }       /* games get the sound card */
	tcsetattr(tty_fd, TCSANOW, &saved_tio);
	pid_t g = fork();
	if (g == 0) {                                     /* the session script runs as root: it starts the X server, then Steam as the user */
		char *env[] = {"PATH=/usr/bin:/bin:/usr/sbin:/sbin", "LANG=en_US.UTF-8", "TERM=linux", NULL};
		execle("/usr/lib/silver/steam-session", "steam-session", pw->pw_name, action, app, (char *)NULL, env);
		_exit(127);
	}
	if (g > 0) {                                      /* Steam runs: two power-button presses within 3 s shut down */
		int st; double last = -10; buttons_open();
		for (;;) {
			if (waitpid(g, &st, WNOHANG) == g) break;
			int lid, p = buttons_read(&lid);
			if (lid) backlight_power(!bt_in.lid_closed);
			for (; p > 0; p--) { if (now() - last < 3) { kill(1, SIGUSR2); for (;;) pause(); } last = now(); }
			usleep(100000);
		}
		buttons_close();
	}
	struct termios raw = saved_tio;                                  /* back to the home screen's terminal setup */
	raw.c_lflag &= ~(ICANON | ECHO | ISIG); raw.c_iflag &= ~(ICRNL | IXON); raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
	tcsetattr(tty_fd, TCSANOW, &raw);
	ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);
	tcflush(tty_fd, TCIFLUSH);
	audio_start(calm, muted);
}

static void run_session(void)
{
	struct passwd *pw = getpwnam(user);
	for (int round = 0;; round++) {                   /* the home screen; again after each Steam session */
		int pfd[2];
		if (round && ui_again() < 0) { restore_tty(); execl("/bin/login", "login", "-f", user, (char *)NULL); _exit(1); }
		if (!pw || pipe(pfd) < 0) { restore_tty(); execl("/bin/login", "login", "-f", user, (char *)NULL); _exit(1); }
		pid_t pid = fork();
		if (pid == 0) {                                   /* the interface runs as the user, never as root */
			close(pfd[0]); req_fd = pfd[1];
			snprintf(homedir, sizeof homedir, "%s", pw->pw_dir);
			if (initgroups(pw->pw_name, pw->pw_gid) || setgid(pw->pw_gid) || setuid(pw->pw_uid)) _exit(1);   /* keeps 'netdev' (Wi-Fi settings) */
			if (chdir(homedir)) { /* fine */ }
			Home h; home_init(&h);
			mouse_open();
			buttons_open();
			if (!back_from_game) wel_start(&h);
			if (back_from_game) { open_screen(&h, 3); h.sub = 5; gs.open = 1; gs.tab = GS_STEAM; h.fade = 0; }   /* back where you were */
			home_loop(&h);
			_exit(0);
		}
		close(pfd[1]);
		ui_release();                                 /* (the child keeps its own copies) */
		char c = 'T', req[48]; int game = 0;
		for (;;) {                                        /* the privileged side only does what the user's side asks */
			ssize_t n = read(pfd[0], &c, 1);
			if (n <= 0) { c = 'T'; break; }
			if (c == 'T') break;
			if (c == 'S' || c == 's') { efi_mute(c == 'S'); muted = c == 'S'; continue; }
			if (c == 'X' || c == 'x') { backlight_power(c == 'x'); continue; }
			if (c == 'N' && windows_installed()) { efi_var(NEXT_VAR, 2); kill(1, SIGTERM); for (;;) pause(); }     /* restart straight into Windows */
			if ((c == 'I' || c == 'J') && access("/etc/silver-installed", F_OK) != 0) {     /* the installer, from the live USB only */
				static int started;                                                  /* one install per boot */
				if (c == 'J' && started) continue;
				if (c == 'J') started = 1;
				const char *cmd = c == 'I' ? "/sbin/silver-install --plan > " INS_PLAN " 2>&1; echo \"EXIT $?\" >> " INS_PLAN
				                           : "/sbin/silver-install --yes > " INS_LOG " 2>&1; echo \"EXIT $?\" >> " INS_LOG;
				if (c == 'I') { FILE *z = fopen(INS_PLAN, "w"); if (z) fclose(z); }
				pid_t a = fork();
				if (a == 0) { if (fork() == 0) { setsid(); execl("/bin/sh", "sh", "-c", cmd, (char *)NULL); } _exit(0); }
				if (a > 0) waitpid(a, NULL, 0);
				continue;
			}
			if (c == 'R') { kill(1, SIGTERM); for (;;) pause(); }
			if (c == 'P') { kill(1, SIGUSR2); for (;;) pause(); }
			if (c == 'K') {                                  /* a keyboard layout, by id (checked: letters and - only, and the file must exist) */
				char ln[24]; size_t k = 0;
				while (k + 1 < sizeof ln && read(pfd[0], &ln[k], 1) == 1 && ln[k] != '\n') k++;
				ln[k] = 0; keymap_apply(ln);
				continue;
			}
			if (c == 'V' || c == 'L') {                      /* volume / brightness, 0 to 100 */
				char ln[8]; size_t k = 0;
				while (k + 1 < sizeof ln && read(pfd[0], &ln[k], 1) == 1 && ln[k] != '\n') k++;
				ln[k] = 0; int pct = atoi(ln); if (pct < 0) pct = 0; if (pct > 100) pct = 100;
				if (c == 'V') volume_set(pct); else backlight_set(pct);
				continue;
			}
			if (c == 'G') {
				size_t k = 0;
				while (k + 1 < sizeof req && read(pfd[0], &req[k], 1) == 1 && req[k] != '\n') k++;
				req[k] = 0; game = 1; break;
			}
		}
		kill(pid, SIGKILL); waitpid(pid, NULL, 0); close(pfd[0]);
		if (game) { run_game(pw, req); back_from_game = 1; continue; }
		break;
	}
	restore_tty();
	snd('q'); if (snd_fd >= 0) close(snd_fd);
	execl("/bin/login", "login", "-f", user, (char *)NULL);
	_exit(1);
}
#endif

#ifdef RENDER
int main(int argc, char **argv)
{
	W = argc > 2 ? atoi(argv[2]) : 1280; H = argc > 3 ? atoi(argv[3]) : 800;
	if (buffers()) return 1;
	setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1); tzset(); tz_load();       /* (the demo's zone) */
	char cmd[512];
	snprintf(cmd, sizeof cmd, "ffmpeg -v error -y -f rawvideo -pix_fmt bgr0 -s %dx%d -r 30 -i - -c:v libx264 -crf 16 -preset medium -pix_fmt yuv420p '%s'",
		 W, H, argc > 1 ? argv[1] : "greeter.mp4");
	outpipe = popen(cmd, "w");
	{ const char *f = getenv("SILVER_FETCH"); if (f) snprintf(yt.fetch, sizeof yt.fetch, "%s", f); }
	bake_home();
	Home h; home_init(&h);
	if (argc > 4) {                                   /* the password card, over the glass */
		strcpy(pass, "12345678");
		for (int f = 0; f < 70; f++) lock_frame(sg_sat(f / 15.f), 0);
	}
	intro(&h);
	home_loop(&h);
	pclose(outpipe);
	if (expect_failed) { fprintf(stderr, "the demo did not do what it should: see CHECK FAILED above\n"); return 3; }
	fprintf(stderr, "all %d checks passed\n", (int)(sizeof EXPECT / sizeof EXPECT[0]));
	const char *wav = getenv("SILVER_SOUND_OUT");
	if (wav) {                                         /* the sound of the whole demo, from what was asked for */
		UiSfx ui; ui_sfx_build(&ui, 0);
		SfxClip intro = {0, 0}; { int16_t *s = 0; intro.n = intro_track(&s, INTRO_TIMES, 0); intro.s = s; }
		int n = (int)(fake_t * SR) + SR;
		int16_t *out = calloc((size_t)n, 4);
		static SfxPlayer pl;
		int at = 0;
		for (int i = 0; i <= snd_nlog && out; i++) {
			int until = i < snd_nlog ? (int)(snd_log[i].t * SR) : n;
			if (until > n) until = n;
			if (until > at) { sfx_render(&pl, out + at * 2, until - at); at = until; }
			if (i == snd_nlog) break;
			char ev = snd_log[i].ev;
			static int mu;
			if (ev == 'M') { mu = 1; sfx_stop(&pl, 0); } else if (ev == 'U') mu = 0;
			else if (ev == 'I') { if (!mu) sfx_play(&pl, &intro, 1, 0); } else if (ev == 'x') sfx_stop(&pl, &intro); else if (!mu) ui_sfx_play(&pl, &ui, ev);
		}
		if (out) wav_write(wav, out, n);
	}
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
			snprintf(p, sizeof p, "%s/.config/silver/mute", homedir);
			if (access(p, F_OK) == 0) muted = 1;
			tz_load();                                            /* your time zone */
			keymap_apply_saved();                                 /* and your keyboard layout */
		}
	}
	tcgetattr(tty_fd, &saved_tio);
	struct termios raw = saved_tio;
	raw.c_lflag &= ~(ICANON | ECHO | ISIG);
	raw.c_iflag &= ~(ICRNL | IXON);
	raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
	tcsetattr(tty_fd, TCSANOW, &raw);
	ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);            /* stop the text console drawing over us */
	signal(SIGPIPE, SIG_IGN);
	audio_start(calm, muted);                                /* the sound process: it opens the card as root, then drops root */
	sg_clear(&fb, BLACK); flip();
	bake_home();
	if (mkdir("/run/silver-greeter", 0700) == 0) { Home h; home_init(&h); intro(&h); }     /* the full intro plays once per boot */
	free(canvas); canvas = NULL;                      /* (the intro's screen-shake scratch buffer) */
	tcflush(tty_fd, TCIFLUSH);
	if (!account_open(user)) lock_loop();
	run_session();
	return 0;
}
#endif
