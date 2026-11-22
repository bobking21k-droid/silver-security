/*
 * sgfx -- the SilverOS software renderer. One header, no libc, shared by the UEFI boot
 * chooser (silverboot) and the Linux login screen (silver-greeter).
 *
 *   - anti-aliased primitives from signed distances: rounded rects, discs, rings, capsule
 *     lines, convex polygons
 *   - text from baked signed-distance-field fonts (src/art/mkfont.py): smooth at any size,
 *     with optional glow
 *   - dithered gradients, soft blobs, value noise / fbm, box blur
 *   - a tiny 3D pipeline: rotate, project, flat-shade, depth-sort, toon outline
 *
 * Pixels are 0x00RRGGBB. Every draw call respects the surface's clip rectangle.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct { uint32_t *px; int w, h; int cx0, cy0, cx1, cy1; } Surf;
typedef struct { float x, y; } V2;
typedef struct { float x, y, z; } V3;

#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define SG_PI 3.14159265f

/* ------------------------------------------------------------------ math */
static inline float sg_sqrt(float x) { return __builtin_sqrtf(x); }
static inline float sg_abs(float x) { return x < 0 ? -x : x; }
static inline float sg_min(float a, float b) { return a < b ? a : b; }
static inline float sg_max(float a, float b) { return a > b ? a : b; }
static inline float sg_clamp(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float sg_sat(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static inline float sg_floor(float x) { int i = (int)x; return (float)(i - (x < (float)i)); }
static inline float sg_fract(float x) { return x - sg_floor(x); }
static inline float sg_lerp(float a, float b, float t) { return a + (b - a) * t; }
static inline float sg_smooth(float x) { x = sg_sat(x); return x * x * (3 - 2 * x); }
static inline float sg_ease_out(float x) { x = 1 - sg_sat(x); return 1 - x * x * x; }
static inline float sg_ease_io(float x) { x = sg_sat(x); return x < .5f ? 4 * x * x * x : 1 - (2 - 2 * x) * (2 - 2 * x) * (2 - 2 * x) / 2; }
/* a heartbeat: "lub-dub" once per `period` seconds. 0..1, sharp attack, quick fall. */
static inline float sg_bump(float x, float w) { x = x < 0 ? -x : x; if (x >= w) return 0; x = 1 - (x / w) * (x / w); return x * x; }
static float sg_heart(float t, float period)
{
	float ph = t / period; ph -= (float)(int)ph;
	float a = sg_bump(ph - 0.07f, 0.10f), b = 0.62f * sg_bump(ph - 0.30f, 0.085f);
	return a > b ? a : b;
}
static float sg_sin(float x)
{
	x -= sg_floor(x / (2 * SG_PI)) * 2 * SG_PI;
	if (x > SG_PI) x -= 2 * SG_PI;
	if (x > SG_PI / 2) x = SG_PI - x;
	if (x < -SG_PI / 2) x = -SG_PI - x;
	float x2 = x * x;
	return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72))));
}
static inline float sg_cos(float x) { return sg_sin(x + SG_PI / 2); }

static uint32_t sg_rng_state = 0x51173eedu;
static inline uint32_t sg_rand(void) { sg_rng_state = sg_rng_state * 1664525u + 1013904223u; return sg_rng_state >> 8; }
static inline float sg_frand(void) { return (float)sg_rand() / 16777216.0f; }

static inline uint32_t sg_hash(int x, int y)
{
	uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}
static float sg_noise(float x, float y)                 /* smooth value noise, 0..1 */
{
	float fx0 = sg_floor(x), fy0 = sg_floor(y);
	int xi = (int)fx0, yi = (int)fy0;
	float fx = x - fx0, fy = y - fy0, u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
	float a = (sg_hash(xi, yi) & 1023) / 1023.f, b = (sg_hash(xi + 1, yi) & 1023) / 1023.f;
	float c = (sg_hash(xi, yi + 1) & 1023) / 1023.f, d = (sg_hash(xi + 1, yi + 1) & 1023) / 1023.f;
	return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
static float sg_fbm(float x, float y, int oct)
{
	float s = 0, a = .5f;
	for (int i = 0; i < oct; i++) { s += a * sg_noise(x, y); x = x * 2.03f + 17.1f; y = y * 2.03f + 9.7f; a *= .5f; }
	return s;
}

/* ------------------------------------------------------------------ colour */
static inline uint32_t sg_mix(uint32_t a, uint32_t b, float t)
{
	int ia = (int)(sg_sat(t) * 256);
	uint32_t rb = ((a & 0xff00ff) * (256 - ia) + (b & 0xff00ff) * ia) >> 8;
	uint32_t g = ((a & 0x00ff00) * (256 - ia) + (b & 0x00ff00) * ia) >> 8;
	return (rb & 0xff00ff) | (g & 0x00ff00);
}
static inline uint32_t sg_gray(uint32_t c)
{
	uint32_t g = (((c >> 16) & 255) * 30 + ((c >> 8) & 255) * 59 + (c & 255) * 11) / 100;
	return RGB(g, g, g);
}
static inline uint32_t sg_add(uint32_t a, uint32_t b, float t)        /* additive light */
{
	int k = (int)(sg_sat(t) * 256);
	int r = (int)((a >> 16) & 255) + ((int)((b >> 16) & 255) * k >> 8);
	int g = (int)((a >> 8) & 255) + ((int)((b >> 8) & 255) * k >> 8);
	int bl = (int)(a & 255) + ((int)(b & 255) * k >> 8);
	return RGB(r > 255 ? 255 : r, g > 255 ? 255 : g, bl > 255 ? 255 : bl);
}

/* ------------------------------------------------------------------ surface */
static void *sg_copy(void *d, const void *s, size_t n)
{
	uint8_t *p = d; const uint8_t *q = s;
	while (n >= 8) { *(uint64_t *)p = *(const uint64_t *)q; p += 8; q += 8; n -= 8; }
	while (n--) *p++ = *q++;
	return d;
}
static inline Surf sg_surf(uint32_t *px, int w, int h) { return (Surf){px, w, h, 0, 0, w, h}; }
static inline void sg_clip(Surf *s, int x0, int y0, int x1, int y1)
{
	s->cx0 = x0 < 0 ? 0 : x0; s->cy0 = y0 < 0 ? 0 : y0;
	s->cx1 = x1 > s->w ? s->w : x1; s->cy1 = y1 > s->h ? s->h : y1;
}
static inline void sg_noclip(Surf *s) { s->cx0 = s->cy0 = 0; s->cx1 = s->w; s->cy1 = s->h; }
static void sg_clear(Surf *s, uint32_t c) { for (int i = 0; i < s->w * s->h; i++) s->px[i] = c; }

/* integer-aligned rectangle (fast path for overlays and fills) */
static void sg_rect(Surf *s, float x, float y, float w, float h, uint32_t c, float a)
{
	int x0 = (int)sg_max(x, s->cx0), x1 = (int)sg_min(x + w, s->cx1);
	int y0 = (int)sg_max(y, s->cy0), y1 = (int)sg_min(y + h, s->cy1);
	if (a <= 0) return;
	for (int j = y0; j < y1; j++) {
		uint32_t *r = s->px + j * s->w;
		if (a >= 1) for (int i = x0; i < x1; i++) r[i] = c;
		else for (int i = x0; i < x1; i++) r[i] = sg_mix(r[i], c, a);
	}
}

/* rounded rectangle; stroke > 0 draws only an inner border of that width */
static void sg_rrect(Surf *s, float x, float y, float w, float h, float r, float stroke, uint32_t c, float a)
{
	int x0 = (int)sg_max(x - 1, s->cx0), x1 = (int)sg_min(x + w + 2, s->cx1);
	int y0 = (int)sg_max(y - 1, s->cy0), y1 = (int)sg_min(y + h + 2, s->cy1);
	float hx = w / 2, hy = h / 2, cx = x + hx, cy = y + hy;
	if (r > hx) r = hx;
	if (r > hy) r = hy;
	if (a <= 0) return;
	for (int j = y0; j < y1; j++) {
		uint32_t *row = s->px + j * s->w;
		float qy = sg_abs(j + .5f - cy) - (hy - r);
		/* In the middle of a row (|qx| small) the distance doesn't change along x: d = qy - r.
		 * Do that span in one go (or skip it, for an outline) and work out only the two ends. */
		float half = hx - r + (qy < 0 ? qy : 0) - 1.0f;
		int i0 = x0, i1 = x1;
		if (half > 0) {
			i0 = (int)(cx - half); i1 = (int)(cx + half) + 1;
			if (i0 < x0) i0 = x0;
			if (i1 > x1) i1 = x1;
			if (i1 > i0) {
				float d = qy - r;
				if (stroke > 0) d = sg_abs(d + stroke / 2) - stroke / 2;
				float cov = .5f - d;
				if (cov > 0) {
					float aa = cov >= 1 ? a : a * cov;
					if (aa >= 1) for (int i = i0; i < i1; i++) row[i] = c;
					else for (int i = i0; i < i1; i++) row[i] = sg_mix(row[i], c, aa);
				}
			} else { i0 = x1; i1 = x1; }
		} else { i0 = x1; i1 = x1; }
		for (int pass = 0; pass < 2; pass++) {
			int a0 = pass ? i1 : x0, a1 = pass ? x1 : i0;
			if (i0 >= x1) { if (pass) break; a0 = x0; a1 = x1; }
			for (int i = a0; i < a1; i++) {
				float qx = sg_abs(i + .5f - cx) - (hx - r), d;
				if (qx <= 0 && qy <= 0) d = sg_max(qx, qy) - r;
				else if (qx > 0 && qy > 0) d = sg_sqrt(qx * qx + qy * qy) - r;
				else d = sg_max(qx, qy) - r;
				if (stroke > 0) d = sg_abs(d + stroke / 2) - stroke / 2;
				float cov = .5f - d;
				if (cov <= 0) continue;
				row[i] = sg_mix(row[i], c, cov >= 1 ? a : a * cov);
			}
		}
	}
}
static void sg_disc(Surf *s, float cx, float cy, float r, uint32_t c, float a) { sg_rrect(s, cx - r, cy - r, 2 * r, 2 * r, r, 0, c, a); }
static void sg_ring(Surf *s, float cx, float cy, float r, float w, uint32_t c, float a) { sg_rrect(s, cx - r, cy - r, 2 * r, 2 * r, r, w, c, a); }

/* soft elliptical blob: smooth falloff to the edge (shadows, clouds, glows) */
static void sg_blob(Surf *s, float cx, float cy, float rx, float ry, uint32_t c, float a)
{
	int x0 = (int)sg_max(cx - rx, s->cx0), x1 = (int)sg_min(cx + rx + 1, s->cx1);
	int y0 = (int)sg_max(cy - ry, s->cy0), y1 = (int)sg_min(cy + ry + 1, s->cy1);
	for (int j = y0; j < y1; j++) {
		uint32_t *row = s->px + j * s->w;
		float dy = (j + .5f - cy) / ry, dy2 = dy * dy;
		for (int i = x0; i < x1; i++) {
			float dx = (i + .5f - cx) / rx, q = 1 - dx * dx - dy2;
			if (q > 0) row[i] = sg_mix(row[i], c, a * q * q);
		}
	}
}
/* like sg_blob but with a firmer body: `hard` 1 = soft, 3 = mostly solid with a soft rim */
static void sg_puff(Surf *s, float cx, float cy, float rx, float ry, float hard, uint32_t c, float a)
{
	int x0 = (int)sg_max(cx - rx, s->cx0), x1 = (int)sg_min(cx + rx + 1, s->cx1);
	int y0 = (int)sg_max(cy - ry, s->cy0), y1 = (int)sg_min(cy + ry + 1, s->cy1);
	for (int j = y0; j < y1; j++) {
		uint32_t *row = s->px + j * s->w;
		float dy = (j + .5f - cy) / ry, dy2 = dy * dy;
		for (int i = x0; i < x1; i++) {
			float dx = (i + .5f - cx) / rx, q = (1 - dx * dx - dy2) * hard;
			if (q > 0) row[i] = sg_mix(row[i], c, q >= 1 ? a : a * q * q * (3 - 2 * q));
		}
	}
}
static void sg_glow(Surf *s, float cx, float cy, float r, uint32_t c, float a)   /* additive blob */
{
	int x0 = (int)sg_max(cx - r, s->cx0), x1 = (int)sg_min(cx + r + 1, s->cx1);
	int y0 = (int)sg_max(cy - r, s->cy0), y1 = (int)sg_min(cy + r + 1, s->cy1);
	for (int j = y0; j < y1; j++) {
		uint32_t *row = s->px + j * s->w;
		float dy = (j + .5f - cy) / r, dy2 = dy * dy;
		for (int i = x0; i < x1; i++) {
			float dx = (i + .5f - cx) / r, q = 1 - dx * dx - dy2;
			if (q > 0) row[i] = sg_add(row[i], c, a * q * q);
		}
	}
}

/* line with round caps */
static void sg_line(Surf *s, V2 a, V2 b, float w, uint32_t c, float al)
{
	float hw = w / 2, m = hw + 1;
	int x0 = (int)sg_max(sg_min(a.x, b.x) - m, s->cx0), x1 = (int)sg_min(sg_max(a.x, b.x) + m + 1, s->cx1);
	int y0 = (int)sg_max(sg_min(a.y, b.y) - m, s->cy0), y1 = (int)sg_min(sg_max(a.y, b.y) + m + 1, s->cy1);
	float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy, inv = l2 > 1e-6f ? 1 / l2 : 0;
	for (int j = y0; j < y1; j++) {
		uint32_t *row = s->px + j * s->w;
		float py = j + .5f - a.y;
		for (int i = x0; i < x1; i++) {
			float px = i + .5f - a.x, t = sg_sat((px * dx + py * dy) * inv);
			float ex = px - dx * t, ey = py - dy * t, cov = hw + .5f - sg_sqrt(ex * ex + ey * ey);
			if (cov > 0) row[i] = sg_mix(row[i], c, cov >= 1 ? al : al * cov);
		}
	}
}

/* convex polygon, anti-aliased. `grow` pushes every edge outward by that many pixels:
 * 0 for a lone shape; 0.5 for faces of a mesh, so neighbours overlap and no background
 * shows through shared edges. */
static void sg_poly(Surf *s, const V2 *p, int n, uint32_t c, float a, float grow)
{
	float nx[8], ny[8], nd[8], minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f, area = 0;
	if (n < 3 || n > 8 || a <= 0) return;
	for (int i = 0; i < n; i++) {
		V2 q = p[(i + 1) % n];
		area += p[i].x * q.y - q.x * p[i].y;
		minx = sg_min(minx, p[i].x); maxx = sg_max(maxx, p[i].x);
		miny = sg_min(miny, p[i].y); maxy = sg_max(maxy, p[i].y);
	}
	float sgn = area < 0 ? -1.f : 1.f;
	for (int i = 0; i < n; i++) {
		V2 q = p[(i + 1) % n];
		float ex = q.x - p[i].x, ey = q.y - p[i].y, l = sg_sqrt(ex * ex + ey * ey);
		if (l < 1e-5f) { nx[i] = ny[i] = 0; nd[i] = 1e9f; continue; }
		nx[i] = -ey / l * sgn; ny[i] = ex / l * sgn;           /* inward normal */
		nd[i] = -(nx[i] * p[i].x + ny[i] * p[i].y) + grow + .5f;
	}
	int x0 = (int)sg_max(minx - grow - 1, s->cx0), x1 = (int)sg_min(maxx + grow + 2, s->cx1);
	int y0 = (int)sg_max(miny - grow - 1, s->cy0), y1 = (int)sg_min(maxy + grow + 2, s->cy1);
	for (int j = y0; j < y1; j++) {
		uint32_t *row = s->px + j * s->w;
		float py = j + .5f;
		for (int i = x0; i < x1; i++) {
			float px = i + .5f, cov = 1;
			for (int k = 0; k < n; k++) {
				float d = nx[k] * px + ny[k] * py + nd[k];
				if (d < cov) { cov = d; if (cov <= 0) break; }
			}
			if (cov > 0) row[i] = sg_mix(row[i], c, cov >= 1 ? a : a * cov);
		}
	}
}
static inline void sg_tri(Surf *s, V2 a, V2 b, V2 c, uint32_t col, float al) { V2 p[3] = {a, b, c}; sg_poly(s, p, 3, col, al, 0); }

/* vertical gradient with ordered dithering (no banding on 8-bit displays) */
static void sg_vgrad(Surf *s, int x0, int y0, int x1, int y1, uint32_t top, uint32_t bot)
{
	static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
	int cx0 = x0 < s->cx0 ? s->cx0 : x0, cx1 = x1 > s->cx1 ? s->cx1 : x1;
	for (int j = y0 < s->cy0 ? s->cy0 : y0; j < (y1 > s->cy1 ? s->cy1 : y1); j++) {
		float t = (float)(j - y0) / (float)(y1 - y0 > 1 ? y1 - y0 - 1 : 1);
		float r = sg_lerp((top >> 16) & 255, (bot >> 16) & 255, t), g = sg_lerp((top >> 8) & 255, (bot >> 8) & 255, t), b = sg_lerp(top & 255, bot & 255, t);
		uint32_t *row = s->px + j * s->w;
		for (int i = cx0; i < cx1; i++) {
			float d = (bayer[j & 3][i & 3] + .5f) / 16.f;
			row[i] = RGB((int)(r + d), (int)(g + d), (int)(b + d));
		}
	}
}

/* box blur of a w x h image, radius r, run `passes` times (3 passes ~ gaussian). tmp: w*h. */
static void sg_blur(uint32_t *img, uint32_t *tmp, int w, int h, int r, int passes)
{
	for (int p = 0; p < passes; p++) {
		for (int pass = 0; pass < 2; pass++) {
			uint32_t *src = pass ? tmp : img, *dst = pass ? img : tmp;
			int len = pass ? h : w, lines = pass ? w : h, step = pass ? w : 1, lstep = pass ? 1 : w;
			for (int l = 0; l < lines; l++) {
				uint32_t *sp = src + l * lstep, *dp = dst + l * lstep;
				int sr = 0, sg = 0, sb = 0, cnt = 2 * r + 1;
				for (int k = -r; k <= r; k++) {
					uint32_t c = sp[(k < 0 ? 0 : k >= len ? len - 1 : k) * step];
					sr += (c >> 16) & 255; sg += (c >> 8) & 255; sb += c & 255;
				}
				for (int i = 0; i < len; i++) {
					dp[i * step] = RGB(sr / cnt, sg / cnt, sb / cnt);
					int o = i - r, n = i + r + 1;
					uint32_t co = sp[(o < 0 ? 0 : o) * step], cn = sp[(n >= len ? len - 1 : n) * step];
					sr += (int)((cn >> 16) & 255) - (int)((co >> 16) & 255);
					sg += (int)((cn >> 8) & 255) - (int)((co >> 8) & 255);
					sb += (int)(cn & 255) - (int)(co & 255);
				}
			}
		}
	}
}

/* ------------------------------------------------------------------ text (SDF fonts) */
typedef struct { const uint8_t *glyphs, *tex; int em, spread, ascent, descent, cap, n, nr; uint32_t rs[16], re[16]; int ri[16]; } Font;

static inline int sg_rd16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
static inline uint32_t sg_rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int sg_font_load(Font *f, const uint8_t *blob)
{
	if (blob[0] != 'S' || blob[1] != 'S' || blob[2] != 'D' || blob[3] != '2') return -1;
	f->em = sg_rd16(blob + 4); f->spread = sg_rd16(blob + 6); f->ascent = sg_rd16(blob + 8);
	f->descent = sg_rd16(blob + 10); f->cap = sg_rd16(blob + 12); f->nr = sg_rd16(blob + 14);
	if (f->nr > 16) return -1;
	const uint8_t *p = blob + 16;
	int idx = 0;
	for (int i = 0; i < f->nr; i++, p += 8) { f->rs[i] = sg_rd32(p); f->re[i] = sg_rd32(p + 4); f->ri[i] = idx; idx += (int)(f->re[i] - f->rs[i] + 1); }
	f->n = idx; f->glyphs = p; f->tex = p + idx * 14;
	return 0;
}
/* next code point of a UTF-8 string; invalid bytes come back as U+FFFD */
static uint32_t sg_utf8(const char **sp)
{
	const uint8_t *s = (const uint8_t *)*sp;
	uint32_t c = *s++;
	if (c < 0x80) { *sp = (const char *)s; return c; }
	int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
	if (!n) { *sp = (const char *)s; return 0xFFFD; }
	c &= (0x3F >> n);
	while (n-- && (*s & 0xC0) == 0x80) c = (c << 6) | (*s++ & 0x3F);
	*sp = (const char *)s;
	return c;
}
/* glyph record for a code point, or NULL if the font does not have it */
static const uint8_t *sg_glyph(const Font *f, uint32_t cp)
{
	for (int i = 0; i < f->nr; i++) if (cp >= f->rs[i] && cp <= f->re[i]) return f->glyphs + (f->ri[i] + (int)(cp - f->rs[i])) * 14;
	return 0;
}
/* width of a string at `size` (em height in px); tracking is extra space per glyph, in em */
static float sg_text_w(const Font *f, const char *str, float size, float tracking)
{
	float sc = size / f->em, w = 0; int n = 0;
	while (*str) {
		uint32_t cp = sg_utf8(&str);
		const uint8_t *g = sg_glyph(f, cp);
		if (!g) continue;                                   /* symbols the font lacks (emoji...) take no room */
		w += (uint16_t)sg_rd16(g + 12) / 64.f * sc + tracking * size; n++;
	}
	return n ? w - tracking * size : 0;
}
/* Draw text with its baseline at y. glow > 0 adds a soft halo of `glow_px` pixels (limited by
 * the font's baked spread). Returns the x position after the last glyph. */
static float sg_text_ex(Surf *s, const Font *f, float x, float y, const char *str, float size,
			uint32_t col, float a, float tracking, float glow, float glow_px)
{
	float sc = size / f->em, inv = 1 / sc, dist = (float)f->spread * sc / 127.f;
	if (glow_px > f->spread * sc * 0.92f) glow_px = f->spread * sc * 0.92f;   /* the halo must end inside the glyph cell */
	while (*str) {
		uint32_t cp = sg_utf8(&str);
		const uint8_t *g = sg_glyph(f, cp);
		if (!g) continue;
		int gw = (uint16_t)sg_rd16(g + 4), gh = (uint16_t)sg_rd16(g + 6);
		float adv = (uint16_t)sg_rd16(g + 12) / 64.f * sc;
		if (gw && gh) {
			const uint8_t *tex = f->tex + sg_rd32(g);
			float dx = x + sg_rd16(g + 8) * sc, dy = y + sg_rd16(g + 10) * sc;
			int x0 = (int)sg_max(dx, s->cx0), x1 = (int)sg_min(dx + gw * sc + 1, s->cx1);
			int y0 = (int)sg_max(dy, s->cy0), y1 = (int)sg_min(dy + gh * sc + 1, s->cy1);
			for (int j = y0; j < y1; j++) {
				float v = (j + .5f - dy) * inv - .5f;
				int vi = (int)sg_floor(v);
				float fv = v - vi;
				if (vi < -1 || vi >= gh) continue;
				const uint8_t *r0 = vi >= 0 ? tex + vi * gw : 0, *r1 = vi + 1 < gh ? tex + (vi + 1) * gw : 0;
				uint32_t *row = s->px + j * s->w;
				for (int i = x0; i < x1; i++) {
					float u = (i + .5f - dx) * inv - .5f;
					int ui = (int)sg_floor(u);
					float fu = u - ui;
					if (ui < -1 || ui >= gw) continue;
					int u0ok = ui >= 0, u1ok = ui + 1 < gw;
					float t00 = r0 && u0ok ? r0[ui] : 0, t10 = r0 && u1ok ? r0[ui + 1] : 0;
					float t01 = r1 && u0ok ? r1[ui] : 0, t11 = r1 && u1ok ? r1[ui + 1] : 0;
					float t = (t00 * (1 - fu) + t10 * fu) * (1 - fv) + (t01 * (1 - fu) + t11 * fu) * fv;
					float sd = (t - 128) * dist, cov = sd + .5f;      /* signed distance in screen px */
					if (cov >= 1) { row[i] = sg_mix(row[i], col, a); continue; }
					if (cov < 0) cov = 0;
					if (glow > 0 && sd < 0) {
						float gl = 1 + sd / glow_px;
						if (gl > 0) { gl = gl * gl * glow; if (gl > cov) cov = gl; }
					}
					if (cov > 0) row[i] = sg_mix(row[i], col, a * cov);
				}
			}
		}
		x += adv + tracking * size;
	}
	return x;
}
static inline float sg_text(Surf *s, const Font *f, float x, float y, const char *str, float size, uint32_t col, float a, float tracking)
{ return sg_text_ex(s, f, x, y, str, size, col, a, tracking, 0, 1); }
static inline void sg_text_c(Surf *s, const Font *f, float cx, float y, const char *str, float size, uint32_t col, float a, float tracking)
{ sg_text_ex(s, f, cx - sg_text_w(f, str, size, tracking) / 2, y, str, size, col, a, tracking, 0, 1); }

/* ------------------------------------------------------------------ 3D */
static inline V3 v3(float x, float y, float z) { return (V3){x, y, z}; }
static inline V3 v3_add(V3 a, V3 b) { return (V3){a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline V3 v3_sub(V3 a, V3 b) { return (V3){a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline V3 v3_mul(V3 a, float k) { return (V3){a.x * k, a.y * k, a.z * k}; }
static inline float v3_dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 v3_cross(V3 a, V3 b) { return (V3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static inline V3 v3_norm(V3 a) { float l = sg_sqrt(v3_dot(a, a)); return l > 1e-9f ? v3_mul(a, 1 / l) : a; }
static inline V3 v3_rotx(V3 p, float a) { float c = sg_cos(a), s = sg_sin(a); return (V3){p.x, p.y * c - p.z * s, p.y * s + p.z * c}; }
static inline V3 v3_roty(V3 p, float a) { float c = sg_cos(a), s = sg_sin(a); return (V3){p.x * c + p.z * s, p.y, -p.x * s + p.z * c}; }
static inline V3 v3_rotz(V3 p, float a) { float c = sg_cos(a), s = sg_sin(a); return (V3){p.x * c - p.y * s, p.x * s + p.y * c, p.z}; }

/* Scene: camera at the origin looking down +z, screen y down. Faces are queued, then drawn
 * far-to-near with flat shading and an optional outline around the whole silhouette. */
#define SG_MAXFACE 420
typedef struct { V2 p[6]; int n; float z; uint32_t col; float a; } SgFace;
typedef struct {
	float cx, cy, focal;         /* projection centre and focal length in px */
	V3 light;                    /* unit vector pointing towards the light */
	float ambient;
	SgFace f[SG_MAXFACE];
	int n;
} Sg3;

static inline V2 sg3_proj(const Sg3 *g, V3 p) { float k = g->focal / p.z; return (V2){g->cx + p.x * k, g->cy + p.y * k}; }

/* Queue a convex face given in camera space, counter-clockwise seen from outside.
 * dark/light: the colour in full shadow and in full light. two_sided faces are never culled. */
static void sg3_face(Sg3 *g, const V3 *v, int n, uint32_t dark, uint32_t light, float alpha, int two_sided)
{
	if (g->n >= SG_MAXFACE || n < 3 || n > 6) return;
	V3 nrm = v3_norm(v3_cross(v3_sub(v[1], v[0]), v3_sub(v[2], v[0])));
	V3 c = v3(0, 0, 0);
	for (int i = 0; i < n; i++) { if (v[i].z < 0.05f) return; c = v3_add(c, v[i]); }
	c = v3_mul(c, 1.f / n);
	float facing = -v3_dot(nrm, c);                       /* > 0: faces the camera */
	if (facing <= 0) { if (!two_sided) return; nrm = v3_mul(nrm, -1); }
	float sh = g->ambient + (1 - g->ambient) * sg_sat(v3_dot(nrm, g->light));
	SgFace *f = &g->f[g->n++];
	for (int i = 0; i < n; i++) f->p[i] = sg3_proj(g, v[i]);
	f->n = n; f->z = c.z; f->col = sg_mix(dark, light, sh); f->a = alpha;
}

static void sg3_flush(Sg3 *g, Surf *s, float outline, uint32_t outline_col, float alpha)
{
	static uint16_t order[SG_MAXFACE];
	for (int i = 0; i < g->n; i++) {                       /* insertion sort, far first */
		int j = i;
		while (j > 0 && g->f[order[j - 1]].z < g->f[i].z) { order[j] = order[j - 1]; j--; }
		order[j] = (uint16_t)i;
	}
	if (outline > 0)
		for (int i = 0; i < g->n; i++) { SgFace *f = &g->f[order[i]]; sg_poly(s, f->p, f->n, outline_col, alpha * f->a, outline); }
	for (int i = 0; i < g->n; i++) { SgFace *f = &g->f[order[i]]; sg_poly(s, f->p, f->n, f->col, alpha * f->a, 0.5f); }
	g->n = 0;
}
