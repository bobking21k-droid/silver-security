/*
 * SilverOS home screen: the cracked-glass launcher and its four sections.
 *
 *      +-----------+-----------+
 *      | SETTINGS  |  SEARCH   |      The glass is split by two jagged fractures that cross
 *      +-----------+-----------+      in the middle, where the SILVER OS plaque sits.
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
typedef struct { V2 p[24]; float cum[24]; int n; float t0, speed, w; } Crack;
#define MAXCRACK 90
static Crack ck[MAXCRACK];
static int nck, nmain;                  /* the first `nmain` cracks are the four arms of the cross */
static float vtab[33], htab[33];        /* the two main fractures: x offset per y sample, y offset per x sample */

static float vx_at(float y) { float f = sg_clamp(y / H, 0, 1) * 32, i = sg_floor(f); int k = (int)i > 31 ? 31 : (int)i; return W / 2.0f + vtab[k] + (vtab[k + 1] - vtab[k]) * (f - k); }
static float hy_at(float x) { float f = sg_clamp(x / W, 0, 1) * 32, i = sg_floor(f); int k = (int)i > 31 ? 31 : (int)i; return H / 2.0f + htab[k] + (htab[k + 1] - htab[k]) * (f - k); }

static void ck_close(Crack *c)
{
	c->cum[0] = 0;
	for (int i = 1; i < c->n; i++) { float dx = c->p[i].x - c->p[i - 1].x, dy = c->p[i].y - c->p[i - 1].y; c->cum[i] = c->cum[i - 1] + sg_sqrt(dx * dx + dy * dy); }
}
static Crack *ck_new(float t0, float speed, float w)
{
	if (nck >= MAXCRACK) return &ck[MAXCRACK - 1];
	Crack *c = &ck[nck++]; c->n = 0; c->t0 = t0; c->speed = speed; c->w = w; return c;
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
	nck = 0;
	float cx = W / 2.0f, cy = H / 2.0f;
	walk(vtab, W * 0.020f, W * 0.040f);
	walk(htab, H * 0.030f, H * 0.055f);
	for (int arm = 0; arm < 4; arm++) {                        /* the cross: up, down, left, right from the centre */
		Crack *c = ck_new(0.0f, H * 1.7f, U * 0.30f);
		int dir = (arm & 1) ? 1 : -1;
		for (int i = 16; i >= 0 && i <= 32 && c->n < 24; i += dir * 1) {
			if (arm < 2) c->p[c->n++] = (V2){W / 2.0f + vtab[i], i * (float)H / 32};
			else c->p[c->n++] = (V2){i * (float)W / 32, H / 2.0f + htab[i]};
			if (c->n >= 17) break;
		}
		ck_close(c);
	}
	nmain = nck;
	const int NR = 11;
	for (int k = 0; k < NR; k++) {                               /* radial cracks with branches */
		float th = (k + sg_frand() * 0.6f) * 2 * SG_PI / NR + 0.25f;
		if (sg_abs(sg_cos(th)) < 0.10f || sg_abs(sg_sin(th)) < 0.10f) th += 0.22f;
		float len = H * (0.17f + 0.33f * sg_frand() * sg_frand() + 0.06f);
		Crack *c = ck_new(0.02f + sg_frand() * 0.07f, H * (1.1f + 0.7f * sg_frand()), U * 0.20f);
		V2 p = {cx, cy}; float a = th;
		c->p[c->n++] = p;
		for (int i = 1; i <= 8; i++) {
			a += (sg_frand() - .5f) * 0.36f;
			float st = len / 8 * (0.8f + 0.4f * sg_frand());
			p.x += sg_cos(a) * st; p.y += sg_sin(a) * st; c->p[c->n++] = p;
			if (p.x < -20 || p.x > W + 20 || p.y < -20 || p.y > H + 20) break;
		}
		ck_close(c);
		Crack parent = *c;
		for (int j = 0; j < 2; j++) {                              /* branches */
			if (parent.n < 5) break;
			int v = 2 + (int)(sg_frand() * (parent.n - 3));
			float ba = sg_atan2f_approx(parent.p[v].y - parent.p[v - 1].y, parent.p[v].x - parent.p[v - 1].x) + (j ? -1 : 1) * (0.45f + 0.5f * sg_frand());
			Crack *b = ck_new(parent.t0 + parent.cum[v] / parent.speed, parent.speed, U * 0.12f);
			V2 q = parent.p[v]; b->p[b->n++] = q;
			float bl = H * (0.05f + 0.07f * sg_frand());
			for (int i = 1; i <= 3; i++) { ba += (sg_frand() - .5f) * 0.4f; q.x += sg_cos(ba) * bl / 3; q.y += sg_sin(ba) * bl / 3; b->p[b->n++] = q; }
			ck_close(b);
		}
	}
	const float RR[3] = {0.075f, 0.165f, 0.29f};
	for (int r = 0; r < 3; r++) {                                /* rings, like a spider web around the impact */
		float rad = H * RR[r];
		Crack *c = ck_new(0.06f + rad / (H * 1.5f), 0, U * 0.12f);
		int nv = 16;
		for (int i = 0; i <= nv; i++) {
			float a = (i % nv) * 2 * SG_PI / nv + (i == nv ? 0 : (sg_frand() - .5f) * 0.12f), rr = rad * (0.86f + 0.28f * sg_frand());
			if (i == nv) { c->p[c->n++] = c->p[0]; break; }
			c->p[c->n++] = (V2){cx + sg_cos(a) * rr, cy + sg_sin(a) * rr * 0.94f};
		}
		ck_close(c);
		c->speed = c->cum[c->n - 1] / 0.30f;
	}
}

/* Draw a crack grown to time t (seconds since the impact): a dark gap, then a bright edge. */
static void crack_draw(Surf *s, const Crack *c, float t, float alpha)
{
	float L = c->speed * (t - c->t0);
	if (L <= 0 || c->n < 2) return;
	if (L > c->cum[c->n - 1]) L = c->cum[c->n - 1];
	for (int pass = 0; pass < 2; pass++)
		for (int i = 1; i < c->n && c->cum[i - 1] < L; i++) {
			V2 a = c->p[i - 1], b = c->p[i];
			float f = sg_min(1, (L - c->cum[i - 1]) / (c->cum[i] - c->cum[i - 1] + 1e-6f));
			V2 e = {a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f};
			if (pass == 0) sg_line(s, a, e, c->w * 2.6f + 1.5f, BLACK, 0.8f * alpha);
			else {
				sg_line(s, a, e, sg_max(1.1f, c->w * 0.8f), WHITE, alpha);
				sg_line(s, (V2){a.x + 1.2f, a.y + 1.2f}, (V2){e.x + 1.2f, e.y + 1.2f}, 1.0f, RGB(150, 150, 160), 0.45f * alpha);   /* glass edge */
			}
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
}

/* ================================================================ icons */
enum { I_SETTINGS, I_SEARCH, I_APPS, I_FILES, I_FOLDER, I_FILE, I_TERM, I_MONITOR, I_INFO, I_POWER };
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

/* ================================================================ search index */
typedef struct { char name[64]; char path[200]; int kind; } SItem;     /* kind: 0 app, 1 setting, 2 file, 3 folder */
#define MAXITEM 3500
static SItem *sitems; static int nsit;
static void add_item(const char *name, const char *path, int kind)
{
	if (nsit >= MAXITEM) return;
	snprintf(sitems[nsit].name, sizeof sitems[0].name, "%s", name); snprintf(sitems[nsit].path, sizeof sitems[0].path, "%s", path); sitems[nsit].kind = kind; nsit++;
}
#ifndef RENDER
static void walk_dir(const char *path, int depth)
{
	if (depth > 5 || nsit >= MAXITEM) return;
	DIR *d = opendir(path);
	struct dirent *de;
	if (!d) return;
	while ((de = readdir(d)) && nsit < MAXITEM) {
		if (de->d_name[0] == '.') continue;
		char full[512]; struct stat st;
		snprintf(full, sizeof full, "%s/%s", path, de->d_name);
		if (lstat(full, &st)) continue;
		if (S_ISDIR(st.st_mode)) { add_item(de->d_name, full, 3); walk_dir(full, depth + 1); }
		else if (S_ISREG(st.st_mode)) add_item(de->d_name, full, 2);
	}
	closedir(d);
}
#endif
static void build_index(void)
{
	if (sitems) return;
	sitems = malloc(sizeof(SItem) * MAXITEM); nsit = 0;
	add_item("Terminal", "app", 0); add_item("System Monitor", "app", 0); add_item("About SilverOS", "app", 0);
	add_item("Network", "setting", 1); add_item("Display", "setting", 1); add_item("Calm mode", "setting", 1); add_item("Restart", "setting", 1); add_item("Shut down", "setting", 1);
#ifdef RENDER
	for (unsigned i = 0; i < sizeof MOCK / sizeof MOCK[0]; i++) add_item(strrchr(MOCK[i].path, '/') + 1, MOCK[i].path, MOCK[i].dir ? 3 : 2);
#else
	walk_dir(homedir, 0); walk_dir("/etc", 3);
#endif
}

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
	int sub;                                /* 0 list, 1 monitor, 2 about */
	char query[48]; int nres, res[64];
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
static void foot_hints(const char *s)
{
	cap_text(U * 6.0f, H - U * 3.4f, s, U * 1.05f, RGB(176, 176, 182), 1, 0);
	char w[] = "SILVER OS";
	cap_text(W - U * 6.0f, H - U * 3.4f, w, U * 1.05f, RGB(120, 120, 126), 1, 1);
}

static void row_box(float x, float y, float w, float h, int sel, float pulse)
{
	if (sel) {
		sg_rect(&fb, x, y, w, h, WHITE, 0.94f);
		sg_rect(&fb, x, y, U * 0.5f, h, RED, 1);
		(void)pulse;
	} else sg_rect(&fb, x, y + h - 1, w, 1, WHITE, 0.08f);
}

/* ---------------------------------------------------------------- files */
static void draw_files(Home *h, float t)
{
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	title_block("FILES", "BROWSE YOUR HOME", I_FILES);
	float lx = U * 6, ly = U * 24, lw = W * 0.54f, rh = U * 5.0f;
	char crumb[256]; fit(cwd, U * 1.7f, lw, crumb, sizeof crumb);
	sg_text(&fb, &FT, lx, ly - U * 1.6f, crumb, U * 1.8f, RGB(210, 210, 216), 1, 0.01f);
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
	sg_rect(&fb, px, py, pw, ph, BLACK, 0.55f);
	sg_rect(&fb, px, py, pw, sg_max(1.5f, U * 0.18f), WHITE, 0.35f);
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
	foot_hints("UP DOWN  MOVE      ENTER  OPEN      BACKSPACE  UP A LEVEL      ESC  HOME");
	(void)t;
}

/* ---------------------------------------------------------------- search */
static int match(const char *hay, const char *needle)
{
	if (!*needle) return 1;
	size_t n = strlen(needle);
	for (; *hay; hay++) if (!strncasecmp(hay, needle, n)) return 1;
	return 0;
}
static void do_search(Home *h)
{
	build_index();
	h->nres = 0;
	for (int i = 0; i < nsit && h->nres < 64; i++)
		if (match(sitems[i].name, h->query) && (h->query[0] || sitems[i].kind < 2)) h->res[h->nres++] = i;
	if (h->row >= h->nres) h->row = h->nres ? h->nres - 1 : 0;
}
static void draw_search(Home *h, float t)
{
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	title_block("SEARCH", "FILES  APPS  SETTINGS", I_SEARCH);
	float lx = U * 6, lw = W - U * 12, by = U * 24;
	sg_rect(&fb, lx, by, lw, U * 7.4f, BLACK, 0.6f);
	sg_rect(&fb, lx, by + U * 7.4f - sg_max(2, U * 0.3f), lw, sg_max(2, U * 0.3f), RED, 1);
	icon(I_SEARCH, lx + U * 3.6f, by + U * 3.7f, U * 1.7f, WHITE, 1);
	float tx = lx + U * 7.2f;
	if (!h->query[0]) sg_text(&fb, &FT, tx, by + U * 4.7f, "Type to search", U * 2.8f, RGB(120, 120, 126), 1, 0.01f);
	float qx = sg_text(&fb, &FT, tx, by + U * 4.7f, h->query, U * 2.8f, WHITE, 1, 0.01f);
	if ((int)(t * 2) & 1) sg_rect(&fb, qx + U * 0.3f, by + U * 1.5f, sg_max(2, U * 0.3f), U * 4.4f, WHITE, 1);
	float ly = by + U * 10.5f, rh = U * 5.4f;
	int vis = (int)((H - ly - U * 9) / rh);
	if (h->row < h->top) h->top = h->row;
	if (h->row >= h->top + vis) h->top = h->row - vis + 1;
	static const char *tag[4] = {"APP", "SETTING", "FILE", "FOLDER"};
	static const int ik[4] = {I_APPS, I_SETTINGS, I_FILE, I_FOLDER};
	if (!h->nres) sg_text(&fb, &FT, lx, ly + U * 4, "Nothing found", U * 2.2f, RGB(150, 150, 156), 1, 0.01f);
	for (int i = 0; i < vis && h->top + i < h->nres; i++) {
		SItem *it = &sitems[h->res[h->top + i]];
		float y = ly + i * rh; int sel = h->top + i == h->row;
		row_box(lx, y, lw, rh, sel, 0);
		icon(ik[it->kind], lx + U * 3.4f, y + rh / 2, U * 1.6f, sel ? INK : WHITE, 1);
		char nm[96]; fit(it->name, U * 2.2f, lw * 0.4f, nm, sizeof nm);
		sg_text(&fb, &FT, lx + U * 7, y + rh / 2 + U * 0.8f, nm, U * 2.2f, sel ? INK : WHITE, 1, 0.01f);
		if (it->kind >= 2) { char pth[200]; fit(it->path, U * 1.5f, lw * 0.42f, pth, sizeof pth); sg_text(&fb, &FT, lx + lw * 0.45f, y + rh / 2 + U * 0.6f, pth, U * 1.5f, sel ? RGB(70, 70, 78) : RGB(140, 140, 146), 1, 0.01f); }
		cap_text(lx + lw - U * 1.6f, y + rh / 2 + U * 0.5f, tag[it->kind], U * 1.0f, sel ? RGB(80, 80, 88) : RGB(130, 130, 136), 1, 1);
	}
	foot_hints("TYPE  SEARCH      UP DOWN  MOVE      ENTER  OPEN      ESC  HOME");
}

/* ---------------------------------------------------------------- apps */
static const char *APPNAME[3] = {"Terminal", "System Monitor", "About SilverOS"};
static const char *APPDESC[3] = {"A command line. Type exit to come back.", "Live CPU and memory", "Version and install notes"};
static const int APPICON[3] = {I_TERM, I_MONITOR, I_INFO};

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

static void draw_apps(Home *h, float t)
{
	if (h->sub == 1) { draw_monitor(h, t); return; }
	if (h->sub == 2) { draw_about(h); return; }
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	title_block("APPS", "TERMINAL  MONITOR  ABOUT", I_APPS);
	float lx = U * 6, lw = W - U * 12, ly = U * 25, rh = U * 12.5f;
	for (int i = 0; i < 3; i++) {
		float y = ly + i * (rh + U * 1.4f); int sel = i == h->row;
		sg_rect(&fb, lx, y, lw, rh, sel ? WHITE : BLACK, sel ? 0.94f : 0.45f);
		if (sel) sg_rect(&fb, lx, y, U * 0.5f, rh, RED, 1);
		icon(APPICON[i], lx + U * 7, y + rh / 2, U * 3.6f, sel ? INK : WHITE, 1);
		sg_text(&fb, &FD, lx + U * 14, y + rh * 0.50f, APPNAME[i], U * 4.6f, sel ? INK : WHITE, 1, 0);
		sg_text(&fb, &FT, lx + U * 14, y + rh * 0.82f, APPDESC[i], U * 2.0f, sel ? RGB(70, 70, 78) : RGB(160, 160, 166), 1, 0.01f);
	}
	foot_hints("UP DOWN  MOVE      ENTER  OPEN      ESC  HOME");
}

/* ---------------------------------------------------------------- settings */
static void draw_settings(Home *h, float t)
{
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	title_block("SETTINGS", "NETWORK  DISPLAY  POWER", I_SETTINGS);
	char net[80]; net_text(net, sizeof net);
	char disp[48]; snprintf(disp, sizeof disp, "%d x %d", W * scale, H * scale);
	static const char *name[6] = {"Network", "Display", "Calm mode", "About", "Restart", "Shut down"};
	static const int ic[6] = {I_SETTINGS, I_SETTINGS, I_SETTINGS, I_INFO, I_POWER, I_POWER};
	char val[6][80];
	snprintf(val[0], 80, "%s", net); snprintf(val[1], 80, "%s", disp); snprintf(val[2], 80, "%s", h->calm_on ? "ON  -  no strobing or shaking" : "OFF");
	snprintf(val[3], 80, "SilverOS 0.1.0"); snprintf(val[4], 80, "%s", h->confirm == 4 ? "PRESS ENTER AGAIN TO RESTART" : "Restart the computer");
	snprintf(val[5], 80, "%s", h->confirm == 5 ? "PRESS ENTER AGAIN TO SHUT DOWN" : "Turn the computer off");
	float lx = U * 6, lw = W - U * 12, ly = U * 24, rh = U * 7.4f;
	for (int i = 0; i < 6; i++) {
		float y = ly + i * (rh + U * 0.8f); int sel = i == h->row, danger = i >= 4;
		sg_rect(&fb, lx, y, lw, rh, sel ? WHITE : BLACK, sel ? 0.94f : 0.45f);
		if (sel) sg_rect(&fb, lx, y, U * 0.5f, rh, RED, 1);
		uint32_t fg = sel ? INK : WHITE;
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
static const char *TILE_CAP[4] = {"NETWORK  DISPLAY  POWER", "FIND ANYTHING", "TERMINAL  MONITOR", "BROWSE YOUR HOME"};
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

static void plaque(float a, float beat)
{
	float cx = W / 2.0f, cy = H / 2.0f, sz = U * 3.4f, tw = sg_text_w(&FD, "SILVER OS", sz, 0), w = tw + U * 11.5f, h = U * 8.4f;
	sg_rect(&fb, cx - w / 2 + U * 0.5f, cy - h / 2 + U * 0.5f, w, h, BLACK, 0.5f * a);
	sg_rect(&fb, cx - w / 2, cy - h / 2, w, h, INK, 0.97f * a);
	sg_rect(&fb, cx - w / 2, cy - h / 2, w, sg_max(2, U * 0.4f), RED, a);
	sg_disc(&fb, cx - w / 2 + U * 3.6f, cy + U * 0.3f, U * (0.7f + 0.45f * beat), RED, a);
	sg_glow(&fb, cx - w / 2 + U * 3.6f, cy + U * 0.3f, U * 4.2f, RED, 0.5f * a * beat);
	sg_text(&fb, &FD, cx - w / 2 + U * 6.6f + sz * 0.03f, cy + sz * 0.38f + sz * 0.03f, "SILVER OS", sz, BLACK, a * 0.6f, 0);
	sg_text(&fb, &FD, cx - w / 2 + U * 6.6f, cy + sz * 0.38f, "SILVER OS", sz, WHITE, a, 0);
}

/* open: 0 = glass not yet shown .. 1 = fully open */
static void home_draw(const Home *h, float open, float t, float cracks_t)
{
	int n = W * H;
	float a = sg_smooth(open), beat = sg_heart(t, 0.9f);
	if (a > 0.003f)
		for (int i = 0; i < n; i++) {
			uint32_t base = sg_mix(bg_dark[i], bg_light[i], h->blend[qmap[i]]);
			fb.px[i] = a >= 1 ? base : sg_mix(fb.px[i], base, a);
		}
	for (int i = 0; i < nck; i++) crack_draw(&fb, &ck[i], cracks_t, i < nmain ? 1.0f : 1 - 0.72f * a);
	float ca = sg_sat((open - 0.35f) / 0.5f);
	if (ca > 0) for (int q = 0; q < 4; q++) tile_content(q, ca, h->blend[q], beat);
	plaque(a, beat);
	if (ca > 0.5f) {
		cap_text(U * 6.0f, H - U * 3.4f, "ARROWS  SELECT      ENTER  OPEN", U * 1.05f, RGB(200, 200, 206), 0.8f * ca, 0);
	}
}
