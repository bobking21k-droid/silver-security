/*
 * SilverOS home screen: cosmic colours pouring out of the cracked emblem, and a row of four buttons.
 *
 *      SilverOS  9:41                                   /\  <- the emblem, cracked, spinning;
 *                                                      /__\    the colours burst out of it
 *      +----------+  +----------+  +----------+  +----------+
 *      | SETTINGS |  |  SEARCH  |  |   APPS   |  |  FILES   |    Left/Right choose, Enter opens,
 *      +----------+  +----------+  +----------+  +----------+    Esc comes back.
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

#define K_UP 1001
#define K_DOWN 1002
#define K_RIGHT 1003
#define K_LEFT 1004
#define K_ESC 27
#define K_ENTER 13
#define K_BACK 127
/* letters beyond ASCII (accents, Cyrillic, Greek ...) arrive as K_UCP + the Unicode code point */
#define K_UCP 0x10000
static int ucp_ok(int k) { return (k >= 32 && k < 127) || k >= K_UCP + 0xA0; }
static void ucp_add(char *b, size_t cap, int k)         /* append the letter k to b (cap = size of b) if it fits */
{
	size_t n = strlen(b); unsigned char t[4]; int l;
	if (k < K_UCP) { t[0] = (unsigned char)k; l = 1; }
	else { unsigned cp = (unsigned)(k - K_UCP);
		if (cp < 0x800) { t[0] = 0xC0 | (cp >> 6); t[1] = 0x80 | (cp & 63); l = 2; }
		else if (cp < 0x10000) { t[0] = 0xE0 | (cp >> 12); t[1] = 0x80 | ((cp >> 6) & 63); t[2] = 0x80 | (cp & 63); l = 3; }
		else { t[0] = 0xF0 | (cp >> 18); t[1] = 0x80 | ((cp >> 12) & 63); t[2] = 0x80 | ((cp >> 6) & 63); t[3] = 0x80 | (cp & 63); l = 4; } }
	if (n + (size_t)l + 1 <= cap) { memcpy(b + n, t, (size_t)l); b[n + (size_t)l] = 0; }
}
static void ucp_pop(char *b)                             /* remove the last letter (all its bytes) */
{
	size_t n = strlen(b); if (!n) return;
	n--; while (n > 0 && ((unsigned char)b[n] & 0xC0) == 0x80) n--;
	b[n] = 0;
}
#define K_SHOT 1010                      /* F12: screenshot */

/* ================================================================ the glass */
static uint8_t *qmap;                   /* the cosmic wave's ragged edge, per pixel (used while it spreads out) */
static uint32_t *bg_dark, *bg_sec;      /* the cosmic background of the home screen; plain dark glass for the sections */

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

/* atan2 without libm (to ~0.005 rad) */
static float hm_atan2(float y, float x)
{
	float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
	float a = mn / (mx + 1e-9f), q = a * a;
	float r = ((-0.0464964749f * q + 0.15931422f) * q - 0.327622764f) * q * a + a;
	if (ay > ax) r = 1.57079633f - r;
	if (x < 0) r = 3.14159265f - r;
	return y < 0 ? -r : r;
}

/* where the emblem lives: the top-right corner (its visual centre) and its size */
#define EM_R (U * 5.8f)
static float em_x(void) { return W - U * 14.5f; }
static float em_y(void) { return U * 14.0f; }
static float tri_wave(float x) { x = x - sg_floor(x); return x < 0.5f ? x * 2 : 2 - x * 2; }

/* The cosmos, baked once: deep space lit from the emblem's corner, nebula clouds in the gem's
   own colours (violet, blue, pink) thinning out towards the far corner, and stars. */
static void bake_cosmic(uint32_t *dst)
{
	float ox = em_x(), oy = em_y() + EM_R * 0.3f, far = sg_sqrt(ox * ox + (H - oy) * (H - oy));
	float f = 3.2f / H, cell = U * 2.2f;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float dx = x + .5f - ox, dy = y + .5f - oy, d = sg_sqrt(dx * dx + dy * dy), dn = d / far;
			/* along the diagonal the clouds stretch from the corner */
			float n1 = sg_fbm(x * f + 3.1f, y * f * 1.15f + 7.7f, 5), n2 = sg_fbm(x * f * 1.7f + 11.3f, y * f * 1.7f + 2.9f, 4);
			float n3 = sg_fbm(x * f * 0.6f - 4.0f, y * f * 0.6f + 9.0f, 3);
			qmap[y * W + x] = (uint8_t)(sg_clamp(n3, 0, 1) * 255);                            /* the wave's ragged edge, for later */
			float fall = 1 - 0.72f * sg_smooth(dn);
			/* deep space: a little warmer near the corner, near black far away */
			uint32_t c = sg_mix(RGB(34, 18, 66), RGB(3, 3, 12), sg_smooth(dn * 1.1f));
			/* nebula */
			float dens = sg_smooth((n1 - 0.38f) / 0.40f) * fall;
			uint32_t neb = sg_mix(RGB(108, 52, 236), RGB(54, 96, 255), sg_sat(n2 * 1.4f - 0.2f));
			neb = sg_mix(neb, RGB(236, 112, 246), sg_smooth((n2 - 0.55f) / 0.25f) * 0.7f);
			c = sg_add(c, neb, dens * 0.85f);
			float wisp = sg_smooth((n2 - 0.62f) / 0.18f) * dens;                              /* bright filaments */
			c = sg_add(c, RGB(255, 210, 255), wisp * 0.35f);
			float dust = sg_smooth((0.45f - n3) / 0.25f) * 0.55f;                             /* dark dust lanes */
			c = sg_mix(c, RGB(2, 2, 8), dust * (1 - 0.5f * dens));
			/* the glow of the corner, where it all comes from */
			c = sg_add(c, RGB(170, 120, 255), 0.55f * sg_bump(dn, 0.38f));
			c = sg_add(c, RGB(255, 235, 255), 0.35f * sg_bump(dn, 0.12f));
			/* stars: one in some cells, a soft point */
			int gx = (int)(x / cell), gy = (int)(y / cell);
			uint32_t hs = sg_hash(gx * 7 + 13, gy * 11 + 5);
			if ((hs & 15) < 3) {
				float sx = (gx + ((hs >> 4) & 255) / 255.f) * cell, sy = (gy + ((hs >> 12) & 255) / 255.f) * cell;
				float sd = (x + .5f - sx) * (x + .5f - sx) + (y + .5f - sy) * (y + .5f - sy);
				float b0 = ((hs >> 20) & 255) / 255.f, br = 0.15f + 0.85f * b0 * b0 * b0, rr = U * (0.05f + 0.10f * br);
				float st = br * sg_sat(1 - sd / (rr * rr * 4));
				c = sg_add(c, ((hs >> 3) & 1) ? RGB(220, 230, 255) : RGB(255, 236, 250), st);
			}
			dst[y * W + x] = c;
		}
}

/* Moving backgrounds: clips made by scripts/make-background.sh, found in
   /usr/share/silver/backgrounds/ (or $SILVER_BG_DIR). Each file is mapped, not copied, so a clip
   costs no memory until it plays. "SVIC" u16 w, h, frames, fps, 32-byte name, then YUV 4:2:0 frames. */
#define VCLIP_MAX 6
typedef struct { int w, h, n, fps; char name[33], file[64]; const uint8_t *frames; } VClip;
static VClip vclip[VCLIP_MAX];
static int nvclip, vclip_tried;
static void vclip_scan(void)
{
	if (vclip_tried) return;
	vclip_tried = 1;
	const char *dir = getenv("SILVER_BG_DIR"); if (!dir) dir = "/usr/share/silver/backgrounds";
	DIR *d = opendir(dir); if (!d) return;
	char names[VCLIP_MAX * 2][64]; int nn = 0;
	struct dirent *e;
	while ((e = readdir(d)) && nn < VCLIP_MAX * 2) { size_t l = strlen(e->d_name); if (l > 5 && l < 64 && !strcmp(e->d_name + l - 5, ".svic")) snprintf(names[nn++], 64, "%s", e->d_name); }
	closedir(d);
	for (int i = 1; i < nn; i++) for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) { char tmp[64]; memcpy(tmp, names[j], 64); memcpy(names[j], names[j - 1], 64); memcpy(names[j - 1], tmp, 64); }
	for (int i = 0; i < nn && nvclip < VCLIP_MAX; i++) {
		char p[400]; snprintf(p, sizeof p, "%s/%s", dir, names[i]);
		int fd = open(p, O_RDONLY); if (fd < 0) continue;
		struct stat st; void *m = MAP_FAILED;
		if (fstat(fd, &st) == 0 && st.st_size >= 44 && st.st_size <= (96L << 20)) m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		close(fd);
		if (m == MAP_FAILED) continue;
		const uint8_t *b = m; VClip *v = &vclip[nvclip];
		v->w = b[4] | b[5] << 8; v->h = b[6] | b[7] << 8; v->n = b[8] | b[9] << 8; v->fps = b[10] | b[11] << 8;
		if (memcmp(b, "SVIC", 4) || v->w < 4 || v->h < 4 || (v->w | v->h) & 1 || !v->n || !v->fps || 44 + (long)v->w * v->h * 3 / 2 * v->n > st.st_size) { munmap(m, (size_t)st.st_size); continue; }
		memcpy(v->name, b + 12, 32); v->name[32] = 0;
		for (char *c = v->name; *c; c++) if (*c < ' ' || *c > '~') *c = ' ';
		if (!v->name[0]) snprintf(v->name, sizeof v->name, "Clip %d", nvclip + 1);
		snprintf(v->file, sizeof v->file, "%s", names[i]);
		v->frames = b + 44;
		nvclip++;
	}
}
/* draw clip v at time t over the whole screen (filling it), dimmed so the buttons and the clock
   read on top, the top calmer still; sampled at half resolution, each sample doubled */
static void vclip_draw(const VClip *v, float t)
{
	float len = (float)v->n / v->fps, u = t - len * sg_floor(t / len);
	int fi = (int)(u * v->fps) % v->n;
	float dip = sg_min(sg_sat(u / 0.15f), sg_sat((len - u) / 0.15f));         /* a quick dip to dark where it loops */
	if (t < 0.15f) dip = 1;
	const uint8_t *Y = v->frames + (size_t)fi * v->w * v->h * 3 / 2, *Ub = Y + v->w * v->h, *Vb = Ub + (v->w / 2) * (v->h / 2);
	float sc = sg_max((float)W / v->w, (float)H / v->h);
	float ox = (v->w - W / sc) / 2, oy = (v->h - H / sc) / 2;
	int step = (int)(65536 * 2 / sc), sx0 = (int)(ox * 65536);
	for (int y = 0; y < H; y += 2) {
		float top = y < H * 0.30f ? 1 - (float)y / (H * 0.30f) : 0;
		int k = (int)(256 * 0.82f * (1 - 0.40f * top) * (0.35f + 0.65f * dip));
		float fy = oy + (y + 1.0f) / sc - 0.5f;
		int y0 = (int)fy; if (fy < 0) { y0 = 0; fy = 0; }
		if (y0 >= v->h - 1) { y0 = v->h - 2; fy = (float)(v->h - 1); }
		int wy = (int)((fy - y0) * 256); if (wy > 255) wy = 255;
		const uint8_t *r0 = Y + y0 * v->w, *r1 = r0 + v->w, *ur = Ub + (y0 / 2) * (v->w / 2), *vr = Vb + (y0 / 2) * (v->w / 2);
		uint32_t *d0 = fb.px + y * W, *d1 = y + 1 < H ? d0 + W : d0;
		int sx = sx0 + step / 2 - 32768;
		for (int x = 0; x < W; x += 2, sx += step) {
			int x0 = sx >> 16, wx = (sx >> 8) & 255;
			if (sx < 0) { x0 = 0; wx = 0; }
			if (x0 >= v->w - 1) { x0 = v->w - 2; wx = 255; }
			int l = ((r0[x0] * (256 - wx) + r0[x0 + 1] * wx) * (256 - wy) + (r1[x0] * (256 - wx) + r1[x0 + 1] * wx) * wy) >> 16;
			int C = 298 * (l - 16), D = ur[x0 / 2] - 128, E = vr[x0 / 2] - 128;
			int R = (C + 409 * E + 128) >> 8, G = (C - 100 * D - 208 * E + 128) >> 8, B = (C + 516 * D + 128) >> 8;
			R = R < 0 ? 0 : R > 255 ? 255 : R; G = G < 0 ? 0 : G > 255 ? 255 : G; B = B < 0 ? 0 : B > 255 ? 255 : B;
			uint32_t c = (uint32_t)((R * k >> 8) << 16 | (G * k >> 8) << 8 | (B * k >> 8));
			d0[x] = c; d1[x] = c;
			if (x + 1 < W) { d0[x + 1] = c; d1[x + 1] = c; }
		}
	}
}

/* which background: 0 cosmic (the default), 1 the Search pane's moving clip (see rooms.h), then
   each moving background found; your choice lives in ~/.config/silver/background */
enum { BG_COSMIC, BG_SEARCH, BG_FIXED };
static int bg_n(void) { return BG_FIXED + nvclip; }
static const char *bg_name(int st) { return st == BG_COSMIC ? "Cosmic" : st == BG_SEARCH ? "Search" : vclip[st - BG_FIXED].name; }
static void bg_key_of(int st, char *out, size_t n) { if (st >= BG_FIXED) snprintf(out, n, "clip:%s", vclip[st - BG_FIXED].file); else snprintf(out, n, "%s", st == BG_SEARCH ? "search" : "cosmic"); }
static int bg_style;
static char homedir[160];                                                 /* (set below) */
static void bg_style_load(void)
{
	vclip_scan();
	char v[96] = "";
#ifndef RENDER
	char p[300]; snprintf(p, sizeof p, "%s/.config/silver/background", homedir);
	FILE *f = fopen(p, "r"); if (f) { if (!fgets(v, sizeof v, f)) v[0] = 0; fclose(f); }
	v[strcspn(v, "\n")] = 0;
#endif
	bg_style = BG_COSMIC;
	for (int st = 1; st < bg_n(); st++) { char k[96]; bg_key_of(st, k, sizeof k); if (!strcmp(v, k)) bg_style = st; }
}
static void bake_home(void)
{
	bg_style_load();
	bake_cosmic(bg_dark);                 /* (also with the clip chosen: it fills qmap, the splash's edge noise) */
	for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) bg_sec[y * W + x] = glass_px(x, y, 0);
}

/* cracks across the emblem, in its own units (R), turning with it; grow: 0..1 how far they have run */
static void emblem_cracks(float cx, float cy, float R, float rot, float grow, float a)
{
	if (grow <= 0 || a <= 0) return;
	float c = sg_cos(rot), s2 = sg_sin(rot);
	for (int k = 0; k < 5; k++) {
		float ang = k * 1.2566f + 0.4f + ((sg_hash(k, 3) & 255) / 255.f - 0.5f) * 0.5f, len = 0.75f + ((sg_hash(k, 9) & 255) / 255.f) * 0.45f;
		V2 prev = {0, 0};
		for (int j = 1; j <= 6; j++) {
			float f = j / 6.f;
			if (f > grow) break;
			float jit = ((sg_hash(k * 13 + j, 5) & 255) / 255.f - 0.5f) * 0.32f;
			float r = 0.10f + f * len, aa = ang + jit;
			V2 lp = {sg_cos(aa) * r, sg_sin(aa) * r};
			V2 p0 = {cx + (prev.x * c - prev.y * s2) * R, cy + (prev.x * s2 + prev.y * c) * R}, p1 = {cx + (lp.x * c - lp.y * s2) * R, cy + (lp.x * s2 + lp.y * c) * R};
			if (j > 1) {
				float w = R * (0.075f - 0.008f * j);
				sg_line(&fb, p0, p1, sg_max(2.0f, w * 3.0f), RGB(150, 90, 255), 0.35f * a);
				sg_line(&fb, p0, p1, sg_max(1.0f, w * 0.6f), RGB(246, 226, 255), a);
			}
			prev = lp;
		}
	}
	sg_glow(&fb, cx, cy, R * 1.1f, RGB(170, 110, 255), 0.45f * a * sg_sat(grow * 2));          /* light leaking out of the gem */
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
static void fo_draw_overlay(Home *h, float t);          /* (fileops.h) */
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
	fo_draw_overlay(h, t);
	foot_hints("ENTER  open      C  copy      X  cut      V  paste      D  delete      R  rename      N  new folder      ESC  home");
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
#include "kb.h"
#include "install.h"

/* ---------------------------------------------------------------- settings */
/* the rows of Settings, in order (the keys, the mouse and the drawing all use these) */
static int scale_next;                                  /* the size chosen in Settings > Display, used from the next start */
static void scale_cycle(void)
{
#ifndef RENDER
	int c = scale_next ? scale_next : scale;
	c = c % 3 + 1;
	if (fb_w / c < 640 || fb_h / c < 360) c = 1;
	scale_next = c;
	char p[300]; snprintf(p, sizeof p, "%s/.config", homedir); mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/.config/silver", homedir); mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/.config/silver/scale", homedir);
	FILE *f = fopen(p, "w"); if (f) { fprintf(f, "%d\n", c); fclose(f); }
#endif
}
enum { R_NET, R_BT, R_DISP, R_BRIGHT, R_VOL, R_TIME, R_KEYB, R_CALM, R_SOUND, R_ABOUT, R_WIN, R_RESTART, R_OFF, R_N };
#define SET_LY (U * 19.0f)
#define SET_RH (U * 4.5f)
#define SET_STEP (U * 4.95f)

#ifndef RENDER
static int volume_get(void);                  /* (sound.h) */
#endif
/* screen brightness: the first backlight there is (laptop panels; desktop monitors have none) */
static int bl_value(const char *file)
{
#ifdef RENDER
	return !strcmp(file, "max_brightness") ? 100 : 70;
#else
	DIR *d = opendir("/sys/class/backlight"); struct dirent *e; int v = -1;
	while (d && (e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[300]; snprintf(p, sizeof p, "/sys/class/backlight/%s/%s", e->d_name, file);
		FILE *f = fopen(p, "r"); if (f) { if (fscanf(f, "%d", &v) != 1) v = -1; fclose(f); }
		if (v >= 0) break;
	}
	if (d) closedir(d);
	return v;
#endif
}
static struct { int bri, vol; double read; } dsp = {-2, -2, -100};
static void dsp_refresh(float t)                                     /* every couple of seconds, and when Settings opens */
{
	if ((double)t - dsp.read < 2.0 && dsp.bri != -2) return;
	dsp.read = t;
#ifdef RENDER
	if (dsp.bri == -2) { dsp.bri = 70; dsp.vol = 65; }
#else
	int m = bl_value("max_brightness"), b = bl_value("brightness");
	dsp.bri = (m > 0 && b >= 0) ? b * 100 / m : -1;
	dsp.vol = volume_get();
#endif
}
static void request_val(char c, int v);
static void slider_adjust(Home *h, int delta)
{
	(void)h;
	if (h->row == R_BRIGHT && dsp.bri >= 0) { dsp.bri = dsp.bri + delta < 5 ? 5 : dsp.bri + delta > 100 ? 100 : dsp.bri + delta; request_val('L', dsp.bri); }
	else if (h->row == R_VOL && dsp.vol >= 0) { dsp.vol = dsp.vol + delta < 0 ? 0 : dsp.vol + delta > 100 ? 100 : dsp.vol + delta; request_val('V', dsp.vol); }
}

static void draw_settings(Home *h, float t)
{
	if (h->sub == 1) { draw_network(h, t); return; }
	if (h->sub == 2) { draw_bluetooth(h, t); return; }
	if (h->sub == 3) { draw_time(h, t); return; }
	if (h->sub == 4 && h->screen == 1) { draw_keyboard(h, t); return; }
	if (h->sub == 5 && h->screen == 1) { draw_install(h, t); return; }
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	title_block("SETTINGS", "NETWORK  BLUETOOTH  DISPLAY  SOUND  TIME  KEYBOARD  POWER", I_SETTINGS);
	char net[80]; net_text(net, sizeof net);
	if (t - wf.last_poll > 3 || !wf.last_poll) wf_poll(t);
	dsp_refresh(t);
	if (wf.ssid[0]) snprintf(net, sizeof net, "Wi-Fi  %.32s", wf.ssid);
	else if (!strcmp(net, "not connected") && wf.adapters) snprintf(net, sizeof net, "Not connected  -  ENTER to pick Wi-Fi");
	char disp[80]; snprintf(disp, sizeof disp, "%d x %d  size x%d  -  ENTER: change", W * scale, H * scale, scale);
	if (scale_next) snprintf(disp, sizeof disp, "size x%d after restart  -  ENTER: change", scale_next);
	const char *name[R_N] = {"Network", "Bluetooth", "Display", "Brightness", "Volume", "Time", "Keyboard", "Calm mode", "Sound effects", "About", "Windows", "Restart", "Shut down"};
	if (ins_live()) name[R_WIN] = "Install SilverOS";
	static const int ic[R_N] = {I_NET, I_SOUND, I_SCREEN, I_SCREEN, I_SOUND, I_INFO, I_TERM, I_SETTINGS, I_SOUND, I_INFO, I_PC, I_POWER, I_POWER};
	char val[R_N][80];
	snprintf(val[R_NET], 80, "%s", net);
	{ int c = 0; for (int i = 0; i < bt.n; i++) c += bt.d[i].connected; if (c) snprintf(val[R_BT], 80, "%d connected", c); else snprintf(val[R_BT], 80, "Pair headphones, controllers ..."); }
	snprintf(val[R_DISP], 80, "%s", disp);
	snprintf(val[R_BRIGHT], 80, dsp.bri >= 0 ? "%d%%" : "Not adjustable on this screen", dsp.bri);
	snprintf(val[R_VOL], 80, dsp.vol >= 0 ? "%d%%" : "No sound device", dsp.vol);
	snprintf(val[R_TIME], 80, "%.60s", ZONES[tzs.cur >= 0 ? tzs.cur : 0].name);
	kb_load();
	snprintf(val[R_KEYB], 80, "%.60s", kb.n ? kb.label[kb.cur] : "English (US)");
	snprintf(val[R_CALM], 80, "%s", h->calm_on ? "ON  -  no strobing or shaking" : "OFF");
	snprintf(val[R_SOUND], 80, "%s", h->mute_on ? "OFF  -  muted" : "ON");
	snprintf(val[R_ABOUT], 80, "SilverOS 0.1.0");
	snprintf(val[R_WIN], 80, "%s", ins_live() ? "Put SilverOS on this computer, next to Windows" : !win_ok() ? "Not installed next to SilverOS" : h->confirm == R_WIN ? "PRESS ENTER AGAIN TO RESTART INTO WINDOWS" : "Restart straight into Windows");
	snprintf(val[R_RESTART], 80, "%s", h->confirm == R_RESTART ? "PRESS ENTER AGAIN TO RESTART" : "Restart the computer");
	snprintf(val[R_OFF], 80, "%s", h->confirm == R_OFF ? "PRESS ENTER AGAIN TO SHUT DOWN" : "Turn the computer off");
	float lx = U * 6, lw = W - U * 12, ly = SET_LY, rh = SET_RH;
	ui_frost(lx - U * 1.2f, ly - U * 1.0f, lw + U * 2.4f, R_N * SET_STEP + U * 1.2f, U * 2.2f, 0.55f);
	for (int i = 0; i < R_N; i++) {
		float y = ly + i * SET_STEP; int sel = i == h->row, danger = i >= R_WIN;
		if (sel) { sg_rrect(&fb, lx, y, lw, rh, U * 1.4f, 0, WHITE, 0.95f); sg_rrect(&fb, lx + U * 0.7f, y + rh * 0.25f, U * 0.45f, rh * 0.5f, U * 0.22f, 0, RED, 1); }
		else if (i) sg_rect(&fb, lx + U * 3, y - U * 0.25f, lw - U * 6, sg_max(1, U * 0.08f), WHITE, 0.08f);
		uint32_t fg = sel ? INK : WHITE, sub = sel ? RGB(70, 70, 78) : RGB(160, 160, 166);
		if (i == R_CALM || i == R_SOUND) {                                   /* on/off switches */
			int on = i == R_CALM ? h->calm_on : !h->mute_on;
			float sw = U * 5.4f, shh = U * 2.6f, sx = lx + lw - U * 3 - sw, sy = y + rh / 2 - shh / 2;
			sg_rrect(&fb, sx, sy, sw, shh, shh / 2, 0, on ? RED : (sel ? RGB(170, 170, 176) : RGB(70, 70, 76)), 1);
			sg_disc(&fb, on ? sx + sw - shh / 2 : sx + shh / 2, sy + shh / 2, shh * 0.38f, WHITE, 1);
			icon(ic[i], lx + U * 4.2f, y + rh / 2, U * 1.9f, fg, 1);
			sg_text(&fb, &FT, lx + U * 9, y + rh / 2 + U * 0.85f, name[i], U * 2.4f, fg, 1, 0.01f);
			float vw = sg_text_w(&FT, val[i], U * 1.9f, 0.01f);
			sg_text(&fb, &FT, sx - U * 2 - vw, y + rh / 2 + U * 0.65f, val[i], U * 1.9f, sub, 1, 0.01f);
			continue;
		}
		if ((i == R_BRIGHT && dsp.bri >= 0) || (i == R_VOL && dsp.vol >= 0)) {   /* sliders: Left / Right */
			int v = i == R_BRIGHT ? dsp.bri : dsp.vol;
			float sw = U * 26, shh = U * 1.0f, sx = lx + lw - U * 3 - sw - U * 7, sy = y + rh / 2 - shh / 2;
			sg_rrect(&fb, sx, sy, sw, shh, shh / 2, 0, sel ? RGB(170, 170, 176) : RGB(70, 70, 76), 1);
			sg_rrect(&fb, sx, sy, sg_max(shh, sw * v / 100), shh, shh / 2, 0, RED, 1);
			sg_disc(&fb, sx + sw * v / 100, sy + shh / 2, U * 1.2f, sel ? RED : WHITE, 1);
			icon(ic[i], lx + U * 4.2f, y + rh / 2, U * 1.9f, fg, 1);
			sg_text(&fb, &FT, lx + U * 9, y + rh / 2 + U * 0.85f, name[i], U * 2.4f, fg, 1, 0.01f);
			float vw = sg_text_w(&FT, val[i], U * 1.9f, 0.01f);
			sg_text(&fb, &FT, lx + lw - U * 3 - vw, y + rh / 2 + U * 0.65f, val[i], U * 1.9f, sub, 1, 0.01f);
			continue;
		}
		icon(ic[i], lx + U * 4.2f, y + rh / 2, U * 1.9f, danger && !sel ? RGB(255, 120, 110) : fg, 1);
		sg_text(&fb, &FT, lx + U * 9, y + rh / 2 + U * 0.85f, name[i], U * 2.4f, fg, 1, 0.01f);
		uint32_t vc = sub;
		if (h->confirm == i && danger) vc = RED;
		float vw = sg_text_w(&FT, val[i], U * 1.9f, 0.01f);
		sg_text(&fb, &FT, lx + lw - U * 3 - vw, y + rh / 2 + U * 0.65f, val[i], U * 1.9f, vc, 1, 0.01f);
	}
	foot_hints(h->row == R_BRIGHT || h->row == R_VOL ? "LEFT RIGHT  ADJUST      UP DOWN  MOVE      ESC  HOME" : "UP DOWN  MOVE      ENTER  CHANGE      ESC  HOME");
	(void)t;
}

/* ================================================================ the home screen itself */
static const char *TILE_NAME[4] = {"SETTINGS", "SEARCH", "APPS", "FILES"};
static const char *TILE_CAP[4] = {"NETWORK  DISPLAY  SOUND", "THE WEB  VIDEOS", "GAMES  MY PC", "BROWSE YOUR HOME"};
static const int TILE_ICON[4] = {I_SETTINGS, I_SEARCH, I_APPS, I_FILES};

/* the four buttons, in a row across the lower half */
static void button_rect(int q, float *x, float *y, float *w, float *h)
{
	float x0 = W * 0.065f, gap = U * 3.0f, bw = (W - 2 * x0 - 3 * gap) / 4;
	*x = x0 + q * (bw + gap); *w = bw; *h = U * 24; *y = H * 0.60f - *h / 2;
}
static int button_at(float mx) { float x0 = W * 0.065f; int q = (int)((mx - x0) / ((W - 2 * x0) / 4)); return q < 0 ? 0 : q > 3 ? 3 : q; }

/* a dark glass card with a fine light edge; the chosen one lifts, its edge and a glow behind it
   turn violet. pop: 0..1 how far it has appeared */
static void draw_button(int q, float blend, float pop, float beat)
{
	if (pop <= 0) return;
	float x, y, w, h; button_rect(q, &x, &y, &w, &h);
	float e = sg_ease_out(pop), lift = U * 1.4f * blend;
	y += (1 - e) * U * 6 - lift;
	float a = sg_sat(pop * 2.5f), r = U * 1.8f;
	if (blend > 0.01f) sg_glow(&fb, x + w / 2, y + h / 2, w * 0.75f, RGB(130, 80, 255), 0.45f * a * blend * (0.8f + 0.2f * beat));
	ui_frost(x, y, w, h, r, 0.62f - 0.18f * blend);
	sg_rrect(&fb, x, y, w, h, r, 0, RGB(16, 10, 36), 0.35f * a);
	sg_rrect(&fb, x, y, w, h, r, sg_max(1.2f, U * 0.14f), sg_mix(RGB(150, 150, 170), RGB(196, 160, 255), blend), (0.45f + 0.5f * blend) * a);
	uint32_t ink = WHITE;
	float cxi = x + w / 2;
	icon(TILE_ICON[q], cxi, y + h * 0.30f, U * 4.4f, ink, a);
	float sz = U * 5.0f, tw = sg_text_w(&FD, TILE_NAME[q], sz, 0);
	if (tw > w - U * 3) { sz *= (w - U * 3) / tw; tw = w - U * 3; }
	sg_text(&fb, &FD, cxi - tw / 2, y + h * 0.70f, TILE_NAME[q], sz, ink, a, 0);
	float cs = U * 1.05f, cw = sg_text_w(&FC, TILE_CAP[q], cs, 0.2f);
	if (cw > w - U * 2) { cs *= (w - U * 2) / cw; cw = w - U * 2; }
	sg_text(&fb, &FC, cxi - cw / 2, y + h * 0.86f, TILE_CAP[q], cs, sg_mix(RGB(160, 160, 176), RGB(220, 205, 255), blend), a, 0.2f);
	if (blend > 0.5f) {                                                                 /* a beating dot under the chosen one */
		float k2 = (blend - 0.5f) * 2;
		sg_glow(&fb, cxi, y + h + U * 3.2f, U * 2.6f, RGB(150, 100, 255), 0.6f * a * k2 * beat);
		sg_disc(&fb, cxi, y + h + U * 3.2f, U * (0.5f + 0.25f * beat), WHITE, a * k2);
	}
}

/* white text with a soft dark shadow and a faint violet glow */
static void glow_text(const Font *f, float x, float y, const char *str, float size, float a, float track)
{
	sg_text(&fb, f, x + size * 0.03f, y + size * 0.04f, str, size, BLACK, a * 0.55f, track);
	sg_text(&fb, f, x, y, str, size, WHITE, a, track);
}

static double spin_t0;                                   /* when the emblem cracked and started spinning */
static void clock_text(char *hm, char *date);

/* The emblem in the corner. sc: seconds since it started to crack (negative: not yet; 99: long ago).
   0 .. CRACK_BURST   the pressure builds: it trembles harder and harder, swells, cracks spread
                      across it in jumps and light forces its way out of the seams
   CRACK_BURST        it bursts open: the layers are blown apart for a moment
   after              they snap back together, cracked, and it spins on, each layer at its own speed */
#define CRACK_BURST 0.95f
static void home_emblem(float t, float sc)
{
	float bt = sc - CRACK_BURST;                                 /* seconds since the burst */
	if (bt >= 0 && sc < 50) spin_t0 = now() - bt;               /* (so it carries on smoothly after the intro) */
	else if (spin_t0 == 0) spin_t0 = now();
	float rt = sc < 50 ? (bt > 0 ? bt : 0) : (float)(now() - spin_t0), ramp = rt * sg_sat(rt / 1.2f);
	Emblem e = emblem_rest();
	e.t = t; e.rot[0] = ramp * 0.45f; e.rot[1] = -ramp * 0.70f; e.rot[2] = ramp * 1.05f; e.rot[3] = -ramp * 0.35f;
	float cx = em_x(), cy = em_y(), grow = 1, leak = 0.3f;
	e.gem = 0.9f + 0.35f * sg_heart(t, 0.9f);
	if (sc >= 0 && sc < CRACK_BURST) {                           /* the pressure */
		float k = sc / CRACK_BURST, k2 = k * k;
		cx += sg_sin(t * 93) * U * 1.1f * k2; cy += sg_cos(t * 77) * U * 1.1f * k2;
		float swell = 1 + 0.07f * k2 + 0.03f * k * sg_sin(t * 40);
		for (int l = 0; l < 4; l++) e.scale[l] = swell;
		e.gem = 1 + 2.2f * k2 * (0.6f + 0.4f * sg_sin(t * (14 + 30 * k)));
		grow = sg_floor(k * 7.0f) / 7.0f + 0.04f;                 /* the cracks run in jumps */
		leak = 0.3f + 1.6f * k2;
	} else if (bt >= 0 && bt < 0.75f) {                          /* the burst: blown apart, snapping back */
		float u = bt / 0.75f, out = sg_sin(SG_PI * sg_sat(u)) * (1 - 0.4f * u);
		for (int l = 0; l < 3; l++) { e.scale[l] = 1 + (0.30f + 0.22f * (2 - l)) * out; e.rot[l] += (l & 1 ? -1 : 1) * 1.4f * out; }
		e.gem = 1 + 2.5f * (1 - u);
		leak = 2.0f * (1 - u) + 0.3f;
	}
	if (sc < 0) grow = 0;
	float sw = sg_fract(t / 6.0f) * 6.0f;
	if (sw < 0.8f && bt > 1) e.shine = sg_lerp(-1.4f, 1.4f, sw / 0.8f);
	if (sc >= 0 && sc < CRACK_BURST) {                           /* beams of light forcing out through the cracks */
		float k = sc / CRACK_BURST, k2 = k * k, ccx = cx, ccy = cy + EM_R * 0.5f;
		for (int j = 0; j < 5; j++) {
			float ang = j * 1.2566f + 0.4f + ((sg_hash(j, 3) & 255) / 255.f - 0.5f) * 0.5f + e.rot[0];
			if (j / 5.0f > grow + 0.1f) continue;
			float len = EM_R * (1.5f + 5.5f * k2) * (0.8f + 0.2f * sg_sin(t * 31 + j));
			V2 a = {ccx, ccy}, b = {ccx + sg_cos(ang) * len, ccy + sg_sin(ang) * len};
			sg_line(&fb, a, b, EM_R * (0.10f + 0.30f * k2), RGB(150, 90, 255), 0.30f * k);
			sg_line(&fb, a, b, sg_max(1.2f, EM_R * (0.03f + 0.08f * k2)), RGB(245, 230, 255), 0.75f * k);
		}
	}
	emblem_draw(&fb, cx, cy + EM_R * 0.5f, EM_R, &e);
	if (grow > 0) emblem_cracks(cx, cy + EM_R * 0.5f, EM_R * e.scale[0], e.rot[0], grow, 1);
	if (leak > 0.31f) {                                          /* light forcing its way out */
		sg_glow(&fb, cx, cy + EM_R * 0.5f, EM_R * (1.0f + 0.8f * leak), RGB(160, 100, 255), sg_min(1, 0.30f * leak));
		sg_glow(&fb, cx, cy + EM_R * 0.5f, EM_R * 0.6f, RGB(250, 230, 255), sg_min(1, 0.25f * leak));
	}
}

/* reveal: 0..1 how far the cosmic colours have spread (1 = whole screen); t: time; sc: seconds
   since the emblem cracked (99 once settled) */
/* the Search pane's clip as the home background, timed by the home screen's own clock */
static void home_clip_bg(float t) { float keep = s_age; s_age = t; scene_focus = 0.5f; scene_bg(t); scene_focus = 0.74f; s_age = keep; }   /* centred: off-centre, the clip runs out on the left and its edge smears */
/* draw background st full screen into fb */
static void bg_render(int st, float t)
{
	if (st == BG_SEARCH) home_clip_bg(t);
	else if (st >= BG_FIXED) vclip_draw(&vclip[st - BG_FIXED], t);
	else memcpy(fb.px, bg_dark, (size_t)W * H * 4);
}
static uint32_t *bg_live;                       /* the splash's source when a moving background is chosen */

static void home_draw(const Home *h, float reveal, float t, float sc)
{
	int n = W * H;
	float beat = sg_heart(t, 0.9f);
	const uint32_t *src = bg_dark;
	if (bg_style != BG_COSMIC && reveal > 0 && reveal < 1 && canvas) {         /* the splash reveals the clip's current frame */
		if (!bg_live) bg_live = malloc((size_t)n * 4);
		if (bg_live) { memcpy(canvas, fb.px, (size_t)n * 4); bg_render(bg_style, t); memcpy(bg_live, fb.px, (size_t)n * 4); memcpy(fb.px, canvas, (size_t)n * 4); src = bg_live; }
	} else if (bg_live && reveal >= 1) { free(bg_live); bg_live = NULL; }
	if (reveal >= 1) bg_render(bg_style, t);
	else if (reveal > 0) {
		/* The splash: what was packed into the gem bursts out through the crack. Long tendrils shoot
		   out first, mostly down towards the far corner, droplets fly ahead of them, then it all
		   floods together until the whole screen is filled. A glowing wet edge, no circle. */
		float ox = em_x(), oy = em_y() + EM_R * 0.5f, far = sg_sqrt(ox * ox + (H - oy) * (H - oy));
		float p = reveal, ep = sg_ease_out(p), reach = far * (0.12f + 2.9f * ep * ep), wob = U * 10, rim = U * 2.2f;   /* violent at first */
		float tdir = hm_atan2((float)H - oy, -ox);                                    /* towards the bottom-left corner */
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++) {
				float dx = x + .5f - ox, dy = y + .5f - oy, d = sg_sqrt(dx * dx + dy * dy);
				if (d > reach * 1.6f + rim) continue;
				float th = hm_atan2(dy, dx), cd = sg_cos(th - tdir);
				float dirk = 0.32f + 0.68f * sg_sat(cd) * sg_sat(cd);                     /* faster towards the far corner */
				float lobe = 0.38f * sg_sin(5 * th + 1.3f) + 0.24f * sg_sin(9 * th + 0.4f) + 0.16f * sg_sin(17 * th + 2.1f) + 0.10f * sg_sin(29 * th + 0.7f);
				lobe = lobe > 0 ? lobe * 1.5f : lobe * 0.6f;                              /* sharp tendrils, round valleys */
				float edge = reach * dirk * (1 + lobe) - wob * (qmap[y * W + x] / 255.f);
				int i = y * W + x;
				if (d < edge - rim) fb.px[i] = src[i];
				else if (d < edge) fb.px[i] = sg_add(src[i], RGB(210, 170, 255), 0.9f * (d - (edge - rim)) / rim);   /* the wet, glowing edge */
				else if (d < edge + rim * 0.7f) fb.px[i] = sg_add(fb.px[i], RGB(130, 80, 255), 0.6f * (1 - (d - edge) / (rim * 0.7f)));
			}
		/* droplets flung out ahead, with short trails */
		for (int k = 0; k < 70; k++) {
			uint32_t hk = sg_hash(k, 77);
			float spread = ((hk & 1023) / 1023.f - 0.5f) * (k < 44 ? 1.3f : 4.0f), ang = tdir + spread;
			float sp = 0.55f + ((hk >> 10) & 255) / 255.f * 0.9f, sz = U * (0.25f + ((hk >> 18) & 255) / 255.f * 0.9f);
			float q = p * sp * 1.25f;
			if (q <= 0.02f || p > 0.92f) continue;
			float dd = far * q, px2 = ox + sg_cos(ang) * dd, py2 = oy + sg_sin(ang) * dd + far * 0.10f * q * q;
			float al = sg_sat((0.92f - p) * 5) * sg_sat(q * 8);
			V2 tail = {ox + sg_cos(ang) * dd * 0.86f, oy + sg_sin(ang) * dd * 0.86f + far * 0.10f * q * q * 0.74f};
			sg_line(&fb, tail, (V2){px2, py2}, sz * 0.9f, RGB(150, 100, 255), 0.45f * al);
			sg_glow(&fb, px2, py2, sz * 3.2f, RGB(150, 100, 255), 0.5f * al);
			sg_disc(&fb, px2, py2, sz, (hk >> 26) & 1 ? RGB(236, 112, 246) : RGB(120, 140, 255), al);
			sg_disc(&fb, px2 - sz * 0.3f, py2 - sz * 0.3f, sz * 0.35f, WHITE, 0.8f * al);
		}
	}
	/* the name and the clock, top left */
	float ha = sc < 99 ? sg_sat((sc - CRACK_BURST - 0.75f) / 0.3f) : 1;
	if (ha > 0) {
		glow_text(&FD, U * 6, U * 12.5f, "SilverOS", U * 7.0f, ha, 0);
		char hm[8], date[48]; clock_text(hm, date);
		glow_text(&FT, U * 6.4f, U * 17.8f, hm, U * 2.6f, ha, 0.01f);
		glow_text(&FC, U * 6.4f + sg_text_w(&FT, hm, U * 2.6f, 0.01f) + U * 1.6f, U * 17.6f, date, U * 1.1f, ha, 0.25f);
	}
	/* the buttons, popping in one after another as the colours pass them */
	for (int q = 0; q < 4; q++) {
		float pop = sc >= 99 ? 1 : sg_sat((sc - CRACK_BURST - 0.60f - q * 0.09f) / 0.32f);
		draw_button(q, h->blend[q], pop, beat);
	}
	if (sc >= -0.15f) home_emblem(t, sc);
	if (sc >= 99 || sc > CRACK_BURST + 1.0f) foot_hints("LEFT RIGHT  select      ENTER  open      B  backgrounds");
}

/* ================================================================ backgrounds (click the emblem) */
/* A row of previews: the background it lands on (Cosmic, the default), the Search pane's clip,
   and each moving background. Left/Right or the mouse choose, Enter or a click uses it, Esc goes back. */
static uint32_t *bg_thumb[BG_FIXED + VCLIP_MAX];
static int tb_w, tb_h, tb_n;
static int em_hit(float x, float y) { float dx = x - em_x(), dy = y - em_y(); return dx * dx + dy * dy < EM_R * EM_R * 4.4f; }
static float bg_gap(void) { return W * 0.025f; }

static void bg_thumbs_make(void)
{
	int n = bg_n();
	int w = (int)sg_min(W * 0.36f, (W * 0.90f - bg_gap() * (n - 1)) / n);
	if (w != tb_w) { for (int i = 0; i < BG_FIXED + VCLIP_MAX; i++) { free(bg_thumb[i]); bg_thumb[i] = NULL; } tb_w = w; tb_h = tb_w * H / W; }
	tb_n = n;
	for (int st = 0; st < n; st++) {
		if (!bg_thumb[st]) bg_thumb[st] = malloc((size_t)tb_w * tb_h * 4);
		if (!bg_thumb[st]) return;
		bg_render(st, st >= BG_FIXED ? 1.5f : 2.0f);                  /* a frame of it into fb (fb is redrawn next frame) */
		const uint32_t *src = fb.px;
		for (int y = 0; y < tb_h; y++)                                        /* shrink: average a little block */
			for (int x = 0; x < tb_w; x++) {
				int sx0 = x * W / tb_w, sy0 = y * H / tb_h, sx1 = (x + 1) * W / tb_w, sy1 = (y + 1) * H / tb_h;
				unsigned r = 0, g = 0, b = 0, c = 0;
				for (int yy = sy0; yy < sy1; yy += 2) for (int xx = sx0; xx < sx1; xx += 2) { uint32_t p = src[yy * W + xx]; r += p >> 16 & 255; g += p >> 8 & 255; b += p & 255; c++; }
				bg_thumb[st][y * tb_w + x] = c ? RGB(r / c, g / c, b / c) : 0;
			}
	}
}
static void bg_open(Home *h) { h->row = bg_style; bg_thumbs_make(); }
static void bg_choose(int st)
{
	if (st == bg_style) return;
	bg_style = st;
#ifndef RENDER
	char p[300], k[96]; snprintf(p, sizeof p, "%s/.config", homedir); mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/.config/silver", homedir); mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/.config/silver/background", homedir);
	bg_key_of(st, k, sizeof k);
	FILE *f = fopen(p, "w"); if (f) { fprintf(f, "%s\n", k); fclose(f); }
#endif
}
static void bg_rect(int st, float *x, float *y) { float g = bg_gap(); *x = (W - tb_n * tb_w - (tb_n - 1) * g) / 2 + st * (tb_w + g); *y = H * 0.5f - tb_h * 0.62f; }
static int bg_at(float mx) { float x0, y0; bg_rect(0, &x0, &y0); int q = (int)((mx - x0 + bg_gap() / 2) / (tb_w + bg_gap())); return q < 0 ? 0 : q >= tb_n ? tb_n - 1 : q; }

static void draw_backgrounds(Home *h, float t)
{
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	title_block("BACKGROUNDS", "HOME SCREEN", I_SCREEN);
	float ls = tb_n > 2 ? U * 2.1f : U * 2.6f;
	for (int st = 0; st < tb_n; st++) {
		if (!bg_thumb[st]) continue;
		float x, y; bg_rect(st, &x, &y);
		int sel = h->row == st, cur = bg_style == st;
		if (sel) sg_glow(&fb, x + tb_w / 2.0f, y + tb_h / 2.0f, tb_w * 0.7f, RGB(130, 80, 255), 0.45f * (0.8f + 0.2f * sg_heart(t, 0.9f)));
		for (int yy = 0; yy < tb_h; yy++) {
			int py = (int)y + yy; if (py < 0 || py >= H) continue;
			memcpy(fb.px + py * W + (int)x, bg_thumb[st] + yy * tb_w, (size_t)tb_w * 4);
		}
		float bw = sel ? sg_max(2, U * 0.45f) : sg_max(1.2f, U * 0.14f);
		sg_rrect(&fb, x - bw, y - bw, tb_w + 2 * bw, tb_h + 2 * bw, U * 0.8f, bw, sel ? RGB(196, 160, 255) : RGB(120, 120, 136), 1);
		sg_text(&fb, &FT, x, y + tb_h + ls * 1.7f, bg_name(st), ls, WHITE, 1, 0.01f);
		const char *tag = cur ? "IN USE" : st == BG_COSMIC ? "DEFAULT" : st >= BG_FIXED ? "MOVING" : "";
		if (*tag) sg_text(&fb, &FC, x + tb_w - sg_text_w(&FC, tag, U * 1.1f, 0.2f), y + tb_h + ls * 1.55f, tag, U * 1.1f, cur ? RGB(150, 230, 170) : RGB(150, 150, 166), 1, 0.2f);
	}
	foot_hints("LEFT RIGHT  choose      ENTER  use it      ESC  home");
}
static int bg_key(Home *h, int k)                       /* returns 1 to go back home */
{
	if (k == K_ESC || k == K_BACK) return 1;
	if (k == K_LEFT && h->row > 0) h->row--;
	else if (k == K_RIGHT && h->row < tb_n - 1) h->row++;
	else if (k == K_ENTER) bg_choose(h->row);
	return 0;
}
