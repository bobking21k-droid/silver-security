/*
 * silverboot -- the SilverOS boot chooser. A UEFI application written from scratch.
 *
 * The screen splits down the middle: SilverOS on the left (a white world with a
 * broken red low-poly pencil), Windows on the right. Time in the scene barely moves
 * until you do: press a key or move the mouse and the shards come alive.
 *
 *   <- / ->, mouse hover   choose          Enter, click    start
 *   1 / 2                  start SilverOS / Windows directly
 *
 * SilverOS hands off to \EFI\SilverOS\grubx64.efi (which loads the kernel silently);
 * Windows hands off to \EFI\Microsoft\Boot\bootmgfw.efi.
 */
#include "efi.h"

#define TIMEOUT_S 8.0f
#define SILVER_LOADER  u"\\EFI\\SilverOS\\grubx64.efi"
#define WINDOWS_LOADER u"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"

int _fltused = 0;   /* tells the MS-ABI toolchain we use floating point */

static EFI_SYSTEM_TABLE *ST;
static EFI_BOOT_SERVICES *BS;
static EFI_HANDLE Self;
static EFI_GOP *Gop;
static EFI_POINTER *Ptr;
static EFI_ABS_POINTER *Abs;
static uint32_t *fb, *snap;   /* back buffer and snapshot (0x00RRGGBB) */
static int W, H;

/* ================================================================ libc bits */
void *memset(void *d, int c, size_t n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { uint8_t *p = d; const uint8_t *q = s; while (n--) *p++ = *q++; return d; }

static float fabsf_(float x) { return x < 0 ? -x : x; }
static float fminf_(float a, float b) { return a < b ? a : b; }
static float fmaxf_(float a, float b) { return a > b ? a : b; }
static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
#define PI 3.14159265f
static float fsin(float x)
{
	x -= (float)(int)(x / (2 * PI)) * 2 * PI;     /* reduce to (-2pi, 2pi) */
	if (x > PI) x -= 2 * PI;
	if (x < -PI) x += 2 * PI;
	if (x > PI / 2) x = PI - x;                   /* fold to [-pi/2, pi/2] */
	if (x < -PI / 2) x = -PI - x;
	float x2 = x * x;
	return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72))));
}
static float fcos(float x) { return fsin(x + PI / 2); }

static uint32_t rng = 0x5117e7;
static float frand(void) { rng = rng * 1664525u + 1013904223u; return (float)(rng >> 8) / 16777216.0f; }

static inline uint64_t rdtsc(void) { uint32_t lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }
static float tsc_per_sec;

/* ================================================================ drawing */
#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

static inline uint32_t mix(uint32_t a, uint32_t b, float t)
{
	int ia = (int)(clampf(t, 0, 1) * 256);
	uint32_t rb = ((a & 0xff00ff) * (256 - ia) + (b & 0xff00ff) * ia) >> 8;
	uint32_t g = ((a & 0x00ff00) * (256 - ia) + (b & 0x00ff00) * ia) >> 8;
	return (rb & 0xff00ff) | (g & 0x00ff00);
}

/* clip rectangle for the panel currently being drawn */
static int cx0, cx1;

static void rect(float x, float y, float w, float h, uint32_t c, float a)
{
	int X0 = (int)fmaxf_(x, cx0), X1 = (int)fminf_(x + w, cx1);
	int Y0 = (int)fmaxf_(y, 0), Y1 = (int)fminf_(y + h, H);
	for (int j = Y0; j < Y1; j++) {
		uint32_t *row = fb + j * W;
		if (a >= 1) for (int i = X0; i < X1; i++) row[i] = c;
		else for (int i = X0; i < X1; i++) row[i] = mix(row[i], c, a);
	}
}

typedef struct { float x, y; } V2;

/* flat-shaded triangle, scanline with 1/2-pixel centres */
static void tri(V2 a, V2 b, V2 c, uint32_t col, float alpha)
{
	if (a.y > b.y) { V2 t = a; a = b; b = t; }
	if (a.y > c.y) { V2 t = a; a = c; c = t; }
	if (b.y > c.y) { V2 t = b; b = c; c = t; }
	int y0 = (int)fmaxf_(a.y + .5f, 0), y1 = (int)fminf_(c.y + .5f, H);
	for (int y = y0; y < y1; y++) {
		float py = y + .5f;
		float xa = a.x + (c.x - a.x) * (py - a.y) / (c.y - a.y + 1e-6f);
		float xb = py < b.y ? a.x + (b.x - a.x) * (py - a.y) / (b.y - a.y + 1e-6f)
				    : b.x + (c.x - b.x) * (py - b.y) / (c.y - b.y + 1e-6f);
		if (xa > xb) { float t = xa; xa = xb; xb = t; }
		int x0 = (int)fmaxf_(xa + .5f, cx0), x1 = (int)fminf_(xb + .5f, cx1);
		uint32_t *row = fb + y * W;
		if (alpha >= 1) for (int x = x0; x < x1; x++) row[x] = col;
		else for (int x = x0; x < x1; x++) row[x] = mix(row[x], col, alpha);
	}
}

static void quad(V2 a, V2 b, V2 c, V2 d, uint32_t col, float alpha) { tri(a, b, c, col, alpha); tri(a, c, d, col, alpha); }

static void line(V2 a, V2 b, float w, uint32_t col, float alpha)
{
	float dx = b.x - a.x, dy = b.y - a.y, len = __builtin_sqrtf(dx * dx + dy * dy);   /* sqrtss */
	if (len < .01f) return;
	float nx = -dy / len * w / 2, ny = dx / len * w / 2;
	quad((V2){a.x + nx, a.y + ny}, (V2){b.x + nx, b.y + ny}, (V2){b.x - nx, b.y - ny}, (V2){a.x - nx, a.y - ny}, col, alpha);
}

/* ================================================================ 5x7 font */
#include "../common/font5x7.h"

static int text_w(const char *s, float px) { int n = 0; while (s[n]) n++; return (int)(n * 6 * px - px); }

/* chunky letters: each font pixel is a px*px block (bold adds a sliver to the right) */
static void text(float x, float y, const char *s, float px, uint32_t col, float a)
{
	for (; *s; s++, x += 6 * px) {
		int g = 0;
		while (glyphs[g] && glyphs[g] != *s) g++;
		if (!glyphs[g]) continue;
		for (int r = 0; r < 7; r++)
			for (int c = 0; c < 5; c++)
				if (font[g][r] & (16 >> c))
					rect(x + c * px, y + r * px, px * 1.08f, px * 1.08f, col, a);
	}
}
static void text_c(float cx, float y, const char *s, float px, uint32_t col, float a) { text(cx - text_w(s, px) / 2.0f, y, s, px, col, a); }

/* ================================================================ the broken pencil */
/* Pencil laid along +x, half-thickness 1. Three facets (light / mid / dark), each
 * outlined so the low-poly shape reads crisply in black and white. */
static const float facet_y[4] = {-1, -0.33f, 0.33f, 1};
static const float zig[4] = {0.32f, -0.22f, 0.30f, -0.18f};   /* jagged break, per facet edge */

typedef struct { float cx, cy, s, ang; } XF;
static V2 xf(XF t, float x, float y)
{
	float c = fcos(t.ang), s = fsin(t.ang);
	return (V2){t.cx + (x * c - y * s) * t.s, t.cy + (x * s + y * c) * t.s};
}

static float edge_w;           /* outline width for the current pencil */
static uint32_t edge_c;

static void face(V2 a, V2 b, V2 c, V2 d, uint32_t col)
{
	quad(a, b, c, d, col, 1);
	line(a, b, edge_w, edge_c, 1); line(b, c, edge_w, edge_c, 1);
	line(c, d, edge_w, edge_c, 1); line(d, a, edge_w, edge_c, 1);
}
static void face3(V2 a, V2 b, V2 c, uint32_t col)
{
	tri(a, b, c, col, 1);
	line(a, b, edge_w, edge_c, 1); line(b, c, edge_w, edge_c, 1); line(c, a, edge_w, edge_c, 1);
}

static void facets(XF t, float x0, float x1, const float *x0zig, const float *x1zig, const uint32_t pal[3])
{
	for (int f = 0; f < 3; f++) {
		float ya = facet_y[f], yb = facet_y[f + 1];
		float sa = x0 + (x0zig ? x0zig[f] : 0), sb = x0 + (x0zig ? x0zig[f + 1] : 0);
		float ea = x1 + (x1zig ? x1zig[f] : 0), eb = x1 + (x1zig ? x1zig[f + 1] : 0);
		face(xf(t, sa, ya), xf(t, ea, ya), xf(t, eb, yb), xf(t, sb, yb), pal[f]);
	}
}

/* monochrome palettes: graphite body, grey eraser, bright ferrule and wood */
static const uint32_t BODY[3]  = {RGB(70, 70, 74), RGB(34, 34, 38), RGB(8, 8, 10)};
static const uint32_t ERASE[3] = {RGB(170, 170, 174), RGB(132, 132, 136), RGB(96, 96, 100)};
static const uint32_t METAL[3] = {RGB(255, 255, 255), RGB(214, 216, 220), RGB(168, 170, 176)};
static const uint32_t WOOD[3]  = {RGB(250, 250, 250), RGB(226, 226, 228), RGB(192, 192, 196)};
static const uint32_t LEAD[3]  = {RGB(40, 40, 44), RGB(16, 16, 18), RGB(0, 0, 0)};

static void pencil(float cx, float cy, float s, float t, float burst)
{
	float open = 0.16f + 0.05f * fsin(t * 1.3f) + burst * 0.30f;   /* bend at the break */
	float gap = (0.85f + 0.25f * fsin(t * 0.9f) + burst * 1.4f);
	float tilt = -0.42f + 0.03f * fsin(t * 0.5f);
	edge_w = fmaxf_(1.5f, s * 0.07f); edge_c = RGB(0, 0, 0);
	/* back piece: eraser end */
	XF L = { cx - fcos(tilt) * gap * s, cy - fsin(tilt) * gap * s, s, tilt - open };
	facets(L, -6.0f, -5.1f, 0, 0, ERASE);
	facets(L, -5.1f, -4.3f, 0, 0, METAL);
	facets(L, -4.3f, 0.0f, 0, zig, BODY);
	/* front piece: point end; its jagged edge fits the back piece's */
	XF R = { cx + fcos(tilt) * gap * s, cy + fsin(tilt) * gap * s, s, tilt + open };
	facets(R, 0.0f, 3.6f, zig, 0, BODY);
	for (int f = 0; f < 3; f++) {
		float ya = facet_y[f], yb = facet_y[f + 1];
		face(xf(R, 3.6f, ya), xf(R, 5.6f, ya * .28f), xf(R, 5.6f, yb * .28f), xf(R, 3.6f, yb), WOOD[f]);
		face3(xf(R, 5.6f, ya * .28f), xf(R, 6.4f, 0), xf(R, 5.6f, yb * .28f), LEAD[f]);
	}
}

/* splinters around the break: they hang in the air and drift when time flows */
#define NSHARD 22
static struct { float ox, oy, vx, vy, rot, spin, size; uint32_t col; } shard[NSHARD];

static void shards_init(void)
{
	static const uint32_t cols[] = {RGB(0, 0, 0), RGB(40, 40, 44), RGB(255, 255, 255), RGB(90, 90, 96), RGB(0, 0, 0)};
	for (int i = 0; i < NSHARD; i++) {
		shard[i].ox = (frand() - .5f) * 2.2f; shard[i].oy = (frand() - .5f) * 2.4f;
		shard[i].vx = (frand() - .5f) * 1.6f; shard[i].vy = (frand() - .5f) * 1.6f;
		shard[i].rot = frand() * 6.28f;       shard[i].spin = (frand() - .5f) * 3;
		shard[i].size = .18f + frand() * .32f; shard[i].col = cols[i % 5];
	}
}

static void shards(float cx, float cy, float s, float t, float burst)
{
	edge_w = fmaxf_(1.2f, s * 0.05f); edge_c = RGB(0, 0, 0);
	for (int i = 0; i < NSHARD; i++) {
		float k = 0.5f + 0.5f * fsin(t * 0.7f + i);
		float x = shard[i].ox + shard[i].vx * (k + burst * 3);
		float y = shard[i].oy + shard[i].vy * (k + burst * 3) - 0.6f;
		XF T = { cx + x * s, cy + y * s, s * shard[i].size, shard[i].rot + t * shard[i].spin };
		face3(xf(T, -1, -0.6f), xf(T, 1.2f, -0.2f), xf(T, -0.3f, 1.0f), shard[i].col);
	}
}

/* ================================================================ the two worlds */
static float burst;          /* extra explosion after each input */
static int windows_found;
#define WHITE RGB(255, 255, 255)
#define BLACK RGB(0, 0, 0)

/* perspective floor shared by both halves; vanishing point at the seam */
static void floor_grid(int x0, int x1, float vx, uint32_t col, float a)
{
	float horizon = H * 0.64f;
	for (int k = -16; k <= 16; k++)
		line((V2){vx + k * W * 0.012f, horizon}, (V2){vx + k * W * 0.11f, (float)H}, 1.5f, col, a);
	for (int k = 1; k < 10; k++) {
		float yy = horizon + (H - horizon) * (k * k) / 81.0f;
		line((V2){(float)x0, yy}, (V2){(float)x1, yy}, 1.5f, col, a);
	}
	line((V2){(float)x0, horizon}, (V2){(float)x1, horizon}, 2.0f, col, a);
}

/* selection frame: a crisp inset rectangle with corner ticks */
static void frame(int x0, int x1, uint32_t col, float a)
{
	float m = H * 0.04f, w = fmaxf_(2, H / 360.0f), L = H * 0.06f;
	float l = x0 + m, r = x1 - m, t = m, b = H - m * 1.8f;
	rect(l, t, r - l, w, col, a * 0.25f); rect(l, b, r - l, w, col, a * 0.25f);
	rect(l, t, w, b - t, col, a * 0.25f); rect(r - w, t, w, b - t, col, a * 0.25f);
	rect(l, t, L, w * 2, col, a); rect(l, t, w * 2, L, col, a);
	rect(r - L, t, L, w * 2, col, a); rect(r - w * 2, t, w * 2, L, col, a);
	rect(l, b - w, L, w * 2, col, a); rect(l, b - L, w * 2, L, col, a);
	rect(r - L, b - w, L, w * 2, col, a); rect(r - w * 2, b - L, w * 2, L, col, a);
}

static void silver_panel(int x0, int x1, float t, int selected, float blink)
{
	cx0 = x0; cx1 = x1;
	float pw = x1 - x0, mid = x0 + pw / 2;
	for (int j = 0; j < H; j++)
		rect(x0, j, pw, 1, mix(WHITE, RGB(222, 222, 224), (float)j / H), 1);
	floor_grid(x0, x1, (float)x1, RGB(150, 150, 154), 0.8f);
	float s = fminf_(pw, (float)H) * 0.052f;
	float py = H * 0.38f + fsin(t * 0.8f) * s * 0.15f;
	for (int k = 0; k < 6; k++)                   /* soft contact shadow */
		rect(mid - s * (7 - k), H * 0.64f + s * 1.2f + k * 2, s * (14 - 2 * k), 2, BLACK, 0.06f);
	pencil(mid, py, s, t, burst);
	shards(mid, py, s, t, burst);
	float px = fminf_(pw / 64, H / 76.0f), ly = H * 0.68f;
	text_c(mid + px * .6f, ly + px * .6f, "SILVEROS", px, RGB(170, 170, 174), 1);
	text_c(mid, ly, "SILVEROS", px, BLACK, 1);
	text_c(mid, ly + px * 9, "BUILT FROM NOTHING", px * 0.35f, RGB(90, 90, 96), 1);
	if (selected) {
		text_c(mid, ly + px * 13, "PRESS ENTER", px * 0.4f, BLACK, blink);
		frame(x0, x1, BLACK, 1);
	} else rect(x0, 0, pw, H, RGB(128, 128, 132), 0.35f);
}

/* filled ellipse (clouds) */
static void ellipse(float cx, float cy, float rx, float ry, uint32_t col, float a)
{
	for (int y = (int)fmaxf_(cy - ry, 0); y < (int)fminf_(cy + ry, H); y++) {
		float dy = (y + .5f - cy) / ry, w = 1 - dy * dy;
		if (w <= 0) continue;
		w = __builtin_sqrtf(w) * rx;
		rect(cx - w, (float)y, 2 * w, 1, col, a);
	}
}

static float hill_y(float x, float pw, float t)   /* rolling hill outline */
{
	float u = x / pw;
	return H * (0.60f - 0.10f * fsin(u * 2.6f + 0.9f) - 0.03f * fsin(u * 7.1f + 2.0f) + 0.004f * fsin(u * 23 + t));
}

static void windows_panel(int x0, int x1, float t, int selected, float blink)
{
	cx0 = x0; cx1 = x1;
	float pw = x1 - x0, mid = x0 + pw / 2;
	/* sky: deep blue overhead to pale near the horizon */
	for (int j = 0; j < H; j++)
		rect(x0, j, pw, 1, mix(RGB(18, 84, 214), RGB(150, 200, 245), fminf_(1, (float)j / (H * 0.62f))), 1);
	/* soft clouds drifting slowly */
	for (int k = 0; k < 4; k++) {
		float cxk = x0 + pw * (0.15f + 0.27f * k) + t * 12 * (1 + k % 2);
		cxk = x0 + (cxk - x0) - (float)(int)((cxk - x0) / (pw * 1.2f)) * pw * 1.2f;
		float cyk = H * (0.12f + 0.07f * (k % 3)), r = H * (0.035f + 0.01f * (k % 2));
		for (int b = 0; b < 5; b++)
			ellipse(cxk + (b - 2) * r * 0.9f, cyk + (b % 2) * r * 0.25f, r * (1.3f - 0.15f * (b == 0 || b == 4)), r * 0.75f, WHITE, 0.55f);
	}
	/* the hill: bright crest, darker as it falls away */
	for (int x = x0; x < x1; x++) {
		float top = hill_y(x - x0, pw, t);
		for (int y = (int)top; y < H; y++) {
			float d = (y - top) / (H - top + 1);
			uint32_t c = mix(RGB(120, 205, 70), RGB(36, 120, 30), fminf_(1, d * 1.4f));
			if (y - top < 3) c = mix(RGB(170, 230, 120), c, (y - top) / 3);
			fb[y * W + x] = c;
		}
	}
	/* a generic application-window glyph, white with a soft shadow */
	float s = fminf_(pw, (float)H) * 0.11f, gx = mid - s * 1.2f, gy = H * 0.30f - s * 0.9f + fsin(t * 0.8f) * s * 0.05f;
	for (int pass = 0; pass < 2; pass++) {
		float o = pass ? 0 : s * 0.08f;
		uint32_t c = pass ? WHITE : RGB(10, 50, 140);
		float al = pass ? 1 : 0.4f;
		rect(gx + o, gy + o, s * 2.4f, s * 0.42f, c, al);
		rect(gx + o, gy + o, s * 0.12f, s * 1.8f, c, al);
		rect(gx + s * 2.28f + o, gy + o, s * 0.12f, s * 1.8f, c, al);
		rect(gx + o, gy + s * 1.68f + o, s * 2.4f, s * 0.12f, c, al);
	}
	for (int k = 0; k < 3; k++) rect(gx + s * (1.95f - k * 0.3f), gy + s * 0.14f, s * 0.15f, s * 0.15f, RGB(18, 84, 214), 1);
	float px = fminf_(pw / 64, H / 76.0f), ly = H * 0.68f;
	text_c(mid + px * .6f, ly + px * .6f, "WINDOWS", px, RGB(20, 70, 20), 0.6f);
	text_c(mid, ly, "WINDOWS", px, WHITE, 1);
	text_c(mid, ly + px * 9, windows_found ? "WINDOWS BOOT MANAGER" : "NOT FOUND ON THIS PC",
	       px * 0.35f, RGB(230, 245, 225), 1);
	if (selected) {
		text_c(mid, ly + px * 13, "PRESS ENTER", px * 0.4f, WHITE, blink);
		frame(x0, x1, WHITE, 1);
	} else rect(x0, 0, pw, H, RGB(40, 40, 44), 0.35f);
}

/* ---- the seam: interlocking teeth that slide, and shards that cross and invert ---- */
#define NCROSS 26
static struct { float y, phase, speed, size, spin, rot; } cross_[NCROSS];

static void seam_init(void)
{
	for (int i = 0; i < NCROSS; i++) {
		cross_[i].y = frand(); cross_[i].phase = frand();
		cross_[i].speed = (0.05f + frand() * 0.12f) * (i & 1 ? 1 : -1);
		cross_[i].size = 0.008f + frand() * 0.016f;
		cross_[i].spin = (frand() - .5f) * 4; cross_[i].rot = frand() * 6.28f;
	}
}

/* draw something on both halves: black where it falls on white, white where it falls on black */
static void inverted(int mid, void (*fn)(void *, uint32_t), void *arg)
{
	cx0 = 0; cx1 = mid; fn(arg, BLACK);
	cx0 = mid; cx1 = W; fn(arg, WHITE);
	cx0 = 0; cx1 = W;
}

typedef struct { V2 a, b, c; } TRI;
static void tri_fn(void *p, uint32_t col) { TRI *t = p; tri(t->a, t->b, t->c, col, 1); }

static void seam(int mid, float t)
{
	cx0 = 0; cx1 = W;
	float p = H / 14.0f, depth = p * 0.42f;
	float off = (t * 0.6f - (int)(t * 0.6f)) * p;    /* teeth slide downward as time flows */
	for (float y = -p + off; y < H + p; y += p) {
		/* white tooth biting into the black half, black tooth biting into the white half */
		tri((V2){(float)mid, y}, (V2){mid + depth, y + p * 0.25f}, (V2){(float)mid, y + p * 0.5f}, WHITE, 1);
		tri((V2){(float)mid, y + p * 0.5f}, (V2){mid - depth, y + p * 0.75f}, (V2){(float)mid, y + p}, BLACK, 1);
	}
	float reach = W * 0.22f;                           /* shards drift across the seam and flip colour */
	for (int i = 0; i < NCROSS; i++) {
		float u = cross_[i].phase + t * cross_[i].speed;
		u -= (float)(int)u; if (u < 0) u += 1;
		float x = mid + (u - 0.5f) * 2 * reach;
		float y = cross_[i].y * H + fsin(t + i) * H * 0.02f;
		float sz = cross_[i].size * H, a = cross_[i].rot + t * cross_[i].spin;
		float fade = 1 - fabsf_((u - 0.5f) * 2);       /* strongest at the seam */
		if (fade < 0.15f) continue;
		XF T = {x, y, sz * (0.5f + fade), a};
		TRI tr = {xf(T, -1, -0.6f), xf(T, 1.2f, -0.2f), xf(T, -0.3f, 1.0f)};
		inverted(mid, tri_fn, &tr);
	}
}

typedef struct { float x, y, px; const char *s; } TXT;
static void text_fn(void *p, uint32_t col) { TXT *t = p; text_c(t->x, t->y, t->s, t->px, col, 1); }
typedef struct { float x, y, w, h; } RCT;
static void rect_fn(void *p, uint32_t col) { RCT *r = p; rect(r->x, r->y, r->w, r->h, col, 1); }
typedef struct { int x, y; } CUR;
static void cursor_fn(void *p, uint32_t col)
{
	CUR *c = p; float mx = c->x, my = c->y;
	uint32_t edge = col == BLACK ? WHITE : BLACK;
	tri((V2){mx - 2, my - 3}, (V2){mx + 20, my + 13}, (V2){mx + 6, my + 25}, edge, 1);
	tri((V2){mx, my}, (V2){mx + 16, my + 12}, (V2){mx + 6, my + 20}, col, 1);
}

/* ================================================================ frame + shatter */
static int sel;              /* 0 SilverOS, 1 Windows */
static float scene_t, countdown = TIMEOUT_S, timescale = 0.08f, idle = 99;
static int counting = 1, mx, my, show_mouse;
static const char *errmsg;

static void blit(void) { Gop->Blt(Gop, fb, EfiBltBufferToVideo, 0, 0, 0, 0, W, H, 0); }

static void draw(float real_t)
{
	int mid = W / 2;                              /* always split exactly down the middle */
	float blink = 0.55f + 0.45f * fsin(real_t * 5);
	silver_panel(0, mid, scene_t, sel == 0, blink);
	windows_panel(mid, W, scene_t, sel == 1, blink);
	seam(mid, scene_t);
	float px = fmaxf_(2, H / 300.0f);
	TXT hint = {W / 2.0f, H - px * 14, px, "<  >  CHOOSE      ENTER  START"};
	inverted(mid, text_fn, &hint);
	if (counting) {                               /* real-time countdown, centred, shrinking */
		float w = W * 0.5f * countdown / TIMEOUT_S;
		RCT bar = {mid - w, H - px * 4, 2 * w, px * 1.5f};
		inverted(mid, rect_fn, &bar);
	}
	if (errmsg) { TXT e = {W / 2.0f, px * 8, px * 1.2f, errmsg}; inverted(mid, text_fn, &e); }
	if (show_mouse) { CUR c = {mx, my}; inverted(mid, cursor_fn, &c); }
	blit();
}

/* The chosen screen shatters into flying triangles before the hand-off. */
static void shatter(void)
{
	memcpy(snap, fb, (size_t)W * H * 4);
	enum { GX = 10, GY = 6 };
	float cw = (float)W / GX, ch = (float)H / GY;
	static struct { V2 p[3]; V2 c; float vx, vy, spin; } t[GX * GY * 2];
	int n = 0;
	for (int j = 0; j < GY; j++)
		for (int i = 0; i < GX; i++) {
			V2 a = {i * cw, j * ch}, b = {(i + 1) * cw, j * ch}, c = {(i + 1) * cw, (j + 1) * ch}, d = {i * cw, (j + 1) * ch};
			V2 tris[2][3] = {{a, b, c}, {a, c, d}};
			for (int k = 0; k < 2; k++, n++) {
				for (int v = 0; v < 3; v++) t[n].p[v] = tris[k][v];
				t[n].c = (V2){(tris[k][0].x + tris[k][1].x + tris[k][2].x) / 3, (tris[k][0].y + tris[k][1].y + tris[k][2].y) / 3};
				t[n].vx = (t[n].c.x - W / 2.0f) * (1.5f + frand()) ; t[n].vy = (t[n].c.y - H / 2.0f) * (1.5f + frand()) - H * 0.3f;
				t[n].spin = (frand() - .5f) * 6;
			}
		}
	cx0 = 0; cx1 = W;
	for (int f = 0; f <= 24; f++) {
		float u = f / 24.0f, e = u * u;
		uint32_t bg = sel == 0 ? RGB(255, 255, 255) : RGB(0, 0, 0);
		for (int k = 0; k < W * H; k++) fb[k] = bg;
		for (int k = 0; k < n; k++) {
			float ang = t[k].spin * e, c = fcos(ang), s = fsin(ang);
			float ox = t[k].vx * e, oy = t[k].vy * e + H * 1.2f * e * e;
			V2 q[3];
			for (int v = 0; v < 3; v++) {
				float dx = t[k].p[v].x - t[k].c.x, dy = t[k].p[v].y - t[k].c.y;
				q[v] = (V2){t[k].c.x + ox + dx * c - dy * s, t[k].c.y + oy + dx * s + dy * c};
			}
			/* each shard keeps the colour of the screen under its centre */
			int sx = (int)clampf(t[k].c.x, 0, W - 1), sy = (int)clampf(t[k].c.y, 0, H - 1);
			tri(q[0], q[1], q[2], snap[sy * W + sx], 1 - u * .3f);
		}
		blit();
		BS->Stall(16000);
	}
}

/* Choosing SilverOS: the colourful Windows half melts -- columns drip and smear,
 * slices glitch sideways with split colour channels -- and spreads across the screen. */
static void melt(void)
{
	memcpy(snap, fb, (size_t)W * H * 4);
	static float delay[4096], speed[4096];
	int cols = W < 4096 ? W : 4096;
	for (int x = 0; x < cols; x++) {
		float from_right = (float)(W - x) / W;          /* Windows side goes first */
		delay[x] = from_right * 0.9f + frand() * 0.15f;
		speed[x] = H * (1.2f + frand() * 1.6f);
	}
	cx0 = 0; cx1 = W;
	uint64_t start = rdtsc();
	for (float t = 0; t < 2.0f; t = (float)(rdtsc() - start) / tsc_per_sec) {
		for (int x = 0; x < cols; x++) {
			float dt = t - delay[x];
			int off = dt > 0 ? (int)(dt * dt * speed[x]) : 0;
			for (int y = 0; y < H; y++) {
				int sy = y - off;
				/* above the dripping column: a smear of its top colour, fading to black */
				uint32_t c = sy >= 0 ? snap[sy * W + x] : mix(snap[x], BLACK, (float)(-sy) / (H * 0.6f));
				fb[y * W + x] = c;
			}
		}
		/* glitch slices: shifted rows with the red channel pulled one way */
		for (int g = 0; g < 10; g++) {
			int y0 = (int)(frand() * H), hh = 2 + (int)(frand() * H * 0.03f), sh = (int)((frand() - .5f) * W * 0.12f);
			for (int y = y0; y < y0 + hh && y < H; y++) {
				uint32_t *row = fb + y * W;
				for (int x = 0; x < W; x++) {
					int sx = x - sh, rx = x - sh - 8;
					uint32_t c = (sx >= 0 && sx < W) ? snap[y * W + sx] : 0;
					uint32_t r = (rx >= 0 && rx < W) ? snap[y * W + rx] : 0;
					row[x] = (c & 0x00ffff) | (r & 0xff0000);
				}
			}
		}
		if (t > 1.3f) rect(0, 0, W, H, BLACK, (t - 1.3f) / 0.7f);
		blit();
		BS->Stall(8000);
	}
}

/* ---- the black-and-white system check ---- */
static char cpu_name[49], gpu_line[40], ram_line[40], disk_line[40];

static void upper_clean(char *s)
{
	char *w = s;
	for (char *r = s; *r; r++) {
		char c = *r >= 'a' && *r <= 'z' ? *r - 32 : *r;
		if (c == ' ' && (w == s || w[-1] == ' ')) continue;   /* squeeze spaces */
		int ok = 0;
		for (const char *g = glyphs; *g; g++) if (*g == c) ok = 1;
		if (ok) *w++ = c;
	}
	while (w > s && w[-1] == ' ') w--;
	*w = 0;
}

static void utoa(char *out, uint64_t v) { char b[24]; int n = 0; do b[n++] = '0' + v % 10; while (v /= 10); while (n) *out++ = b[--n]; *out = 0; }
static void cat(char *d, const char *s) { while (*d) d++; while ((*d++ = *s++)); }

static int probe(void)
{
	uint32_t r[12];
	for (int i = 0; i < 3; i++)
		__asm__ volatile("cpuid" : "=a"(r[i * 4]), "=b"(r[i * 4 + 1]), "=c"(r[i * 4 + 2]), "=d"(r[i * 4 + 3]) : "a"(0x80000002 + i), "c"(0));
	memcpy(cpu_name, r, 48); cpu_name[48] = 0; upper_clean(cpu_name);
	if (!cpu_name[0]) cat(cpu_name, "X86-64");

	gpu_line[0] = 0; utoa(gpu_line, (uint64_t)W); cat(gpu_line, "X"); { char n[12]; utoa(n, (uint64_t)H); cat(gpu_line, n); } cat(gpu_line, " FRAMEBUFFER");

	static uint8_t map[64 * 1024];
	UINTN size = sizeof map, key, dsz; uint32_t ver; uint64_t pages = 0;
	if (BS->GetMemoryMap(&size, map, &key, &dsz, &ver) == EFI_SUCCESS && dsz)
		for (UINTN o = 0; o + dsz <= size; o += dsz) {
			uint32_t type = *(uint32_t *)(map + o);
			if ((type >= 1 && type <= 7) || type == 9 || type == 10) pages += *(uint64_t *)(map + o + 24);
		}
	uint64_t mb = (pages * 4096 + (128ull << 20)) >> 20;
	mb = (mb + 127) / 256 * 256;                      /* firmware reserves a little; round to 256 MB */
	ram_line[0] = 0; utoa(ram_line, mb); cat(ram_line, " MB");
	return pages > 0;
}

static void glow_text(float x, float y, const char *s, float px, float a)
{
	for (int k = 3; k >= 1; k--) {                    /* phosphor glow */
		float o = px * 0.35f * k;
		text(x - o, y, s, px, RGB(255, 255, 255), a * 0.06f);
		text(x + o, y, s, px, RGB(255, 255, 255), a * 0.06f);
		text(x, y - o, s, px, RGB(255, 255, 255), a * 0.06f);
		text(x, y + o, s, px, RGB(255, 255, 255), a * 0.06f);
	}
	text(x, y, s, px, RGB(255, 255, 255), a);
}

static void checks(int disk_ok)
{
	int ram_ok = probe();
	disk_line[0] = 0; cat(disk_line, disk_ok ? "SILVEROS VOLUME OK" : "SILVEROS VOLUME MISSING");
	const char *name[4] = {"CPU", "GPU", "RAM", "STORAGE"};
	const char *info[4] = {cpu_name, gpu_line, ram_line, disk_line};
	int ok[4] = {1, 1, ram_ok, disk_ok};
	float px = fmaxf_(2, fminf_(H / 120.0f, W / 250.0f)), x = W * 0.08f, y0 = H * 0.50f;
	cx0 = 0; cx1 = W;
	uint64_t start = rdtsc();
	for (float t = 0; t < 3.4f; t = (float)(rdtsc() - start) / tsc_per_sec) {
		for (int k = 0; k < W * H; k++) fb[k] = RGB(4, 4, 6);
		for (int y = 0; y < H; y += 3) rect(0, (float)y, (float)W, 1, BLACK, 1);   /* scanlines */
		for (int i = 0; i < 4; i++) {
			float ti = t - 0.25f - i * 0.5f;
			if (ti < 0) break;
			float y = y0 + i * px * 15;
			float jitter = (frand() < 0.08f) ? (frand() - .5f) * px * 4 : 0;   /* glitch flicker */
			glow_text(x + jitter, y, name[i], px * 1.4f, 1);
			int dots = (int)(ti * 8); if (dots > 3) dots = 3;
			const char *d3[4] = {"", ".", "..", "..."};
			glow_text(x + px * 1.4f * 6 * 8, y, d3[dots], px * 1.4f, 1);
			if (ti > 0.4f) {
				glow_text(x + px * 1.4f * 6 * 11.5f, y, ok[i] ? "*" : "!", px * 1.4f, 1);
				text(x + px * 1.4f * 6 * 13.5f, y + px * 2.5f, info[i], px * 0.7f, RGB(150, 150, 156), 1);
			}
		}
		if (frand() < 0.06f) {                         /* rolling interference band */
			int by = (int)(frand() * H);
			rect(0, (float)by, (float)W, px * 2, WHITE, 0.08f);
		}
		blit();
		BS->Stall(8000);
	}
}

/* ================================================================ loading the next stage */
static int has_file(EFI_HANDLE dev, CHAR16 *path)
{
	static EFI_GUID sfsg = SFS_GUID;
	EFI_SFS *sfs; EFI_FILE *root, *f;
	if (BS->HandleProtocol(dev, &sfsg, (void **)&sfs) != EFI_SUCCESS) return 0;
	if (sfs->OpenVolume(sfs, &root) != EFI_SUCCESS) return 0;
	int ok = root->Open(root, &f, path, EFI_FILE_MODE_READ, 0) == EFI_SUCCESS;
	if (ok) f->Close(f);
	root->Close(root);
	return ok;
}

/* find a filesystem holding `path`: our own ESP first, then every other volume */
static EFI_HANDLE find_volume(CHAR16 *path)
{
	static EFI_GUID lig = LOADED_IMAGE_GUID, sfsg = SFS_GUID;
	EFI_LOADED_IMAGE *li;
	if (BS->HandleProtocol(Self, &lig, (void **)&li) == EFI_SUCCESS && has_file(li->DeviceHandle, path))
		return li->DeviceHandle;
	UINTN n = 0; EFI_HANDLE *hs = 0;
	if (BS->LocateHandleBuffer(ByProtocol, &sfsg, 0, &n, &hs) != EFI_SUCCESS) return 0;
	for (UINTN i = 0; i < n; i++)
		if (has_file(hs[i], path)) { EFI_HANDLE h = hs[i]; BS->FreePool(hs); return h; }
	BS->FreePool(hs);
	return 0;
}

/* device path of the volume + a file-path node, so the loaded image knows where it lives */
static void *file_path(EFI_HANDLE dev, CHAR16 *path)
{
	static EFI_GUID dpg = DEVICE_PATH_GUID;
	EFI_DEVICE_PATH *dp;
	if (BS->HandleProtocol(dev, &dpg, (void **)&dp) != EFI_SUCCESS) return 0;
	UINTN len = 0;
	for (EFI_DEVICE_PATH *n = dp; !(n->Type == 0x7f && n->SubType == 0xff);
	     n = (EFI_DEVICE_PATH *)((uint8_t *)n + (n->Length[0] | n->Length[1] << 8)))
		len += n->Length[0] | n->Length[1] << 8;
	UINTN plen = 0; while (path[plen]) plen++;
	UINTN node = 4 + (plen + 1) * 2;
	uint8_t *out;
	if (BS->AllocatePool(EfiLoaderData, len + node + 4, (void **)&out) != EFI_SUCCESS) return 0;
	memcpy(out, dp, len);
	uint8_t *p = out + len;
	p[0] = 4; p[1] = 4; p[2] = (uint8_t)node; p[3] = (uint8_t)(node >> 8);
	memcpy(p + 4, path, (plen + 1) * 2);
	p += node;
	p[0] = 0x7f; p[1] = 0xff; p[2] = 4; p[3] = 0;
	return out;
}

static void launch(CHAR16 *path, int silver)
{
	EFI_HANDLE dev = find_volume(path), img;
	void *dp = dev ? file_path(dev, path) : 0;
	if (!dp) { errmsg = "COULD NOT FIND THE BOOT FILE"; return; }
	if (silver) { melt(); checks(1); }
	else shatter();
	if (BS->LoadImage(0, Self, dp, 0, 0, &img) != EFI_SUCCESS) { errmsg = "COULD NOT LOAD THE BOOT FILE"; return; }
	BS->StartImage(img, 0, 0);
	errmsg = "THAT SYSTEM RETURNED. PICK AGAIN";   /* only reached if the next stage exits */
}

/* ================================================================ main loop */
static void poke(void) { idle = 0; counting = 0; burst = fminf_(burst + 0.25f, 0.9f); }

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
	static EFI_GUID gopg = GOP_GUID, ptrg = POINTER_GUID, absg = ABSPOINTER_GUID;
	Self = image; ST = st; BS = st->BootServices;
	BS->SetWatchdogTimer(0, 0, 0, 0);
	if (BS->LocateProtocol(&gopg, 0, (void **)&Gop) != EFI_SUCCESS) {
		/* no graphics: fall straight through to SilverOS */
		launch(SILVER_LOADER, 1);
		return 1;
	}
	if (BS->LocateProtocol(&absg, 0, (void **)&Abs) != EFI_SUCCESS) Abs = 0;
	if (BS->LocateProtocol(&ptrg, 0, (void **)&Ptr) != EFI_SUCCESS) Ptr = 0;
	W = Gop->Mode->Info->HorizontalResolution;
	H = Gop->Mode->Info->VerticalResolution;
	if (BS->AllocatePool(EfiLoaderData, (UINTN)W * H * 4, (void **)&fb) != EFI_SUCCESS ||
	    BS->AllocatePool(EfiLoaderData, (UINTN)W * H * 4, (void **)&snap) != EFI_SUCCESS) {
		launch(SILVER_LOADER, 1);
		return 1;
	}
	st->ConOut->EnableCursor(st->ConOut, 0);
	windows_found = find_volume(WINDOWS_LOADER) != 0;
	mx = W / 2; my = H / 2;

	uint64_t t0 = rdtsc(); BS->Stall(50000);
	tsc_per_sec = (float)(rdtsc() - t0) * 20;
	if (tsc_per_sec < 1e6f) tsc_per_sec = 1e9f;
	shards_init();
	seam_init();
	float real_t = 0;
	uint64_t last = rdtsc();

	for (;;) {
		uint64_t now = rdtsc();
		float dt = clampf((float)(now - last) / tsc_per_sec, 0, 0.1f);
		last = now; real_t += dt; idle += dt;

		/* ---- input ---- */
		EFI_INPUT_KEY k;
		int go = -1;
		while (st->ConIn->ReadKeyStroke(st->ConIn, &k) == EFI_SUCCESS) {
			poke();
			if (k.ScanCode == SCAN_LEFT || k.ScanCode == SCAN_UP) sel = 0;
			else if (k.ScanCode == SCAN_RIGHT || k.ScanCode == SCAN_DOWN) sel = windows_found ? 1 : 0;
			else if (k.UnicodeChar == '\r' || k.UnicodeChar == ' ') go = sel;
			else if (k.UnicodeChar == '1') go = 0;
			else if (k.UnicodeChar == '2' && windows_found) go = 1;
		}
		int click = 0;
		if (Abs) {
			EFI_ABS_STATE s;
			if (Abs->GetState(Abs, &s) == EFI_SUCCESS && Abs->Mode->MaxX > Abs->Mode->MinX) {
				int nx = (int)((s.X - Abs->Mode->MinX) * W / (Abs->Mode->MaxX - Abs->Mode->MinX));
				int ny = (int)((s.Y - Abs->Mode->MinY) * H / (Abs->Mode->MaxY - Abs->Mode->MinY));
				if (nx != mx || ny != my) { mx = nx; my = ny; show_mouse = 1; poke(); }
				if (s.Buttons & 1) click = 1;
			}
		}
		if (Ptr) {
			EFI_POINTER_STATE s;
			if (Ptr->GetState(Ptr, &s) == EFI_SUCCESS) {
				float rx = Ptr->Mode->ResX ? (float)Ptr->Mode->ResX : 1, ry = Ptr->Mode->ResY ? (float)Ptr->Mode->ResY : 1;
				if (s.RelX || s.RelY) {
					mx = (int)clampf(mx + s.RelX * 6 / rx, 0, W - 1);
					my = (int)clampf(my + s.RelY * 6 / ry, 0, H - 1);
					show_mouse = 1; poke();
				}
				if (s.Left) click = 1;
			}
		}
		if (show_mouse) {
			int hover = mx < W / 2 ? 0 : 1;
			if (hover == 0 || windows_found) sel = hover;
			if (click) go = sel;
		}

		/* ---- time: crawls when you're still, flows when you move ---- */
		float target = idle < 0.35f ? 1.0f : 0.07f;
		timescale += (target - timescale) * fminf_(1, dt * 6);
		scene_t += dt * timescale;
		burst *= 1 - fminf_(1, dt * 2.5f);

		if (counting) {
			countdown -= dt;
			if (countdown <= 0) go = 0;
		}
		if (go >= 0) {
			sel = go;
			launch(go == 0 ? SILVER_LOADER : WINDOWS_LOADER, go == 0);
			counting = 0;
		}
		draw(real_t);
		BS->Stall(8000);
	}
}
