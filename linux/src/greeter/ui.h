/*
 * SilverOS interface kit: the small set of components every screen is built from, so the
 * whole interface looks like one thing. Cards with soft shadows and a lit edge, icon badges,
 * pills, avatars, text wrapping, scroll bars.
 *
 * Needs (from silver-greeter.c): fb, W, H, U, fonts FD FT FC, and icon() from home.h.
 */
#pragma once

static const uint32_t ACCENT = RGB(232, 16, 24);

/* a pleasant colour derived from a string (channel avatars, placeholder thumbnails) */
static uint32_t hash_color(const char *s, float sat, float val)
{
	uint32_t h = 2166136261u;
	for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
	float hue = (float)(h % 360) / 60.f, c = val * sat, x = c * (1 - sg_abs(sg_fract(hue / 2) * 2 - 1)), m = val - c;
	float r = 0, g = 0, b = 0;
	int k = (int)hue;
	if (k == 0) { r = c; g = x; } else if (k == 1) { r = x; g = c; } else if (k == 2) { g = c; b = x; }
	else if (k == 3) { g = x; b = c; } else if (k == 4) { r = x; b = c; } else { r = c; b = x; }
	return RGB((int)((r + m) * 255), (int)((g + m) * 255), (int)((b + m) * 255));
}

/* glass card: soft shadow, a faint top-lit fill, a bright edge */
static void ui_card(float x, float y, float w, float h, float r, uint32_t fill, float a, float shadow)
{
	if (shadow > 0) sg_blob(&fb, x + w / 2, y + h / 2 + U * 1.0f, w * 0.60f + U * 2, h * 0.80f + U * 2, BLACK, shadow);
	sg_rrect(&fb, x, y, w, h, r, 0, fill, a);
	sg_rrect(&fb, x + r * 0.3f, y + sg_max(1, U * 0.12f), w - r * 0.6f, sg_max(1, U * 0.1f), U * 0.05f, 0, WHITE, 0.10f);   /* a lit top edge */
	sg_rrect(&fb, x, y, w, h, r, sg_max(1.0f, U * 0.12f), WHITE, 0.11f);          /* the lit edge */
}

static void ui_pill(float x, float y, float w, float h, uint32_t fill, float a)
{
	sg_rrect(&fb, x, y, w, h, h / 2, 0, fill, a);
}

/* an icon inside a round badge */
static void ui_badge(int kind, float cx, float cy, float r, uint32_t bg, float bga, uint32_t fg)
{
	sg_disc(&fb, cx, cy, r, bg, bga);
	sg_ring(&fb, cx, cy, r, sg_max(1, U * 0.12f), WHITE, 0.14f);
	icon(kind, cx, cy, r * 0.56f, fg, 1);
}

static void ui_avatar(float cx, float cy, float r, const char *name)
{
	uint32_t c = hash_color(name, 0.55f, 0.62f);
	sg_disc(&fb, cx, cy, r, c, 1);
	char one[8] = {0};
	const char *p = name;
	if (*p) { uint32_t cp = sg_utf8(&p); (void)cp; size_t n = (size_t)(p - name); if (n > 7) n = 7; memcpy(one, name, n); }
	for (char *q = one; *q; q++) if (*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);
	float sz = r * 1.15f;
	sg_text_c(&fb, &FT, cx, cy + sz * 0.36f, one, sz, WHITE, 1, 0);
}

/* Break s into at most maxlines lines no wider than maxw. The last line ends with an ellipsis if
 * text was left over. Returns the number of lines. */
static int ui_wrap(const char *s, float size, float maxw, int maxlines, char out[][160])
{
	int nl = 0;
	size_t pos = 0, len = strlen(s);
	while (pos < len && nl < maxlines) {
		while (s[pos] == ' ') pos++;
		if (pos >= len) break;
		char *ln = out[nl]; ln[0] = 0;
		size_t used = 0, last_space = 0, i = pos;
		int last = nl == maxlines - 1;
		for (;;) {
			size_t j = i;                                        /* the next codepoint */
			if (j < len) { const char *q = s + j; sg_utf8(&q); j = (size_t)(q - s); }
			char tmp[160]; size_t l = i - pos < 150 ? i - pos : 150;
			memcpy(tmp, s + pos, l); tmp[l] = 0;
			if (i >= len) { used = l; memcpy(ln, tmp, l + 1); i = len; break; }
			if (s[i] == ' ') last_space = i;
			char t2[160]; size_t l2 = j - pos < 150 ? j - pos : 150; memcpy(t2, s + pos, l2); t2[l2] = 0;
			if (sg_text_w(&FT, t2, size, 0.0f) > maxw) {
				size_t cut = last_space > pos ? last_space : i;       /* break at a space if there was one */
				if (last) {                                          /* final line: trim and add an ellipsis */
					size_t l3 = cut - pos < 140 ? cut - pos : 140; memcpy(ln, s + pos, l3); ln[l3] = 0;
					while (sg_text_w(&FT, ln, size, 0) > maxw - size && l3 > 1) { l3--; while (l3 > 1 && ((unsigned char)ln[l3] & 0xC0) == 0x80) l3--; ln[l3] = 0; }
					strcat(ln, "\xE2\x80\xA6");
					i = len;
				} else { size_t l3 = cut - pos < 150 ? cut - pos : 150; memcpy(ln, s + pos, l3); ln[l3] = 0; i = cut; }
				break;
			}
			i = j;
			(void)used;
		}
		pos = i; nl++;
	}
	return nl;
}

static void ui_scroll(float x, float y, float h, int top, int vis, int n)
{
	if (n <= vis) return;
	sg_rrect(&fb, x, y, U * 0.5f, h, U * 0.25f, 0, WHITE, 0.10f);
	float th = h * vis / n, ty = y + (h - th) * top / (n - vis > 0 ? n - vis : 1);
	sg_rrect(&fb, x, ty, U * 0.5f, th, U * 0.25f, 0, WHITE, 0.55f);
}

/* a small caption chip, e.g. a key hint or a tag */
static float ui_chip(float x, float cy, const char *s, float size, uint32_t fg, uint32_t bg, float bga)
{
	float w = sg_text_w(&FC, s, size, 0.18f) + size * 2.2f, h = size * 2.5f;
	sg_rrect(&fb, x, cy - h / 2, w, h, h / 2, 0, bg, bga);
	sg_text(&fb, &FC, x + size * 1.1f, cy + size * 0.36f, s, size, fg, 1, 0.18f);
	return x + w;
}

/* Frosted glass: blur what is behind a rounded rectangle and darken it, so text on top reads
 * over a busy, moving background. The blur runs at half resolution (four times less work, and
 * at this radius it looks the same). On screens whose background doesn't move (frost_static),
 * finished panels are kept and simply copied back the next frame. */
static uint32_t *frost_buf, *frost_tmp;
static size_t frost_cap;
static int frost_static;                                /* set per frame: the background under panels is still */
#define FROST_CACHE 48
static struct { int x0, y0, bw, bh, key; uint32_t *px; unsigned used; } frost_cache[FROST_CACHE];
static size_t frost_bytes;                              /* what the cache holds; capped at 1.5 screens */
static void frost_drop(int i) { if (frost_cache[i].px) { frost_bytes -= (size_t)frost_cache[i].bw * frost_cache[i].bh * 4; free(frost_cache[i].px); frost_cache[i].px = 0; } }
static unsigned frost_clock;

static void ui_frost(float x, float y, float w, float h, float r, float dark)
{
	int x0 = (int)sg_max(0, x - 1), y0 = (int)sg_max(0, y - 1), x1 = (int)sg_min(W, x + w + 2), y1 = (int)sg_min(H, y + h + 2);
	int bw = x1 - x0, bh = y1 - y0;
	if (bw < 4 || bh < 4) return;
	int key = (int)(r * 16) * 1000 + (int)(dark * 1000) + (int)((x - (int)x) * 7) * 100000 + (int)((y - (int)y) * 7) * 1000000;
	frost_clock++;
	if (frost_static)
		for (int i = 0; i < FROST_CACHE; i++)
			if (frost_cache[i].px && frost_cache[i].x0 == x0 && frost_cache[i].y0 == y0 && frost_cache[i].bw == bw && frost_cache[i].bh == bh && frost_cache[i].key == key) {
				for (int j = 0; j < bh; j++) memcpy(fb.px + (y0 + j) * W + x0, frost_cache[i].px + j * bw, (size_t)bw * 4);
				frost_cache[i].used = frost_clock;
				return;
			}
	int hw = (bw + 1) / 2, hh = (bh + 1) / 2;
	if ((size_t)hw * hh > frost_cap) {
		free(frost_buf); free(frost_tmp);
		frost_cap = (size_t)hw * hh;
		frost_buf = malloc(frost_cap * 4); frost_tmp = malloc(frost_cap * 4);
		if (!frost_buf || !frost_tmp) { frost_cap = 0; return; }
	}
	for (int j = 0; j < hh; j++) {                                        /* shrink 2x2 -> 1 */
		const uint32_t *r0 = fb.px + (y0 + 2 * j) * W + x0, *r1 = (y0 + 2 * j + 1 < y1) ? r0 + W : r0;
		uint32_t *o = frost_buf + j * hw;
		for (int i = 0; i < hw; i++) {
			int a0 = 2 * i, a1 = 2 * i + 1 < bw ? 2 * i + 1 : 2 * i;
			uint32_t p = r0[a0], q = r0[a1], u = r1[a0], v = r1[a1];
			o[i] = RGB((int)((((p >> 16) & 255) + ((q >> 16) & 255) + ((u >> 16) & 255) + ((v >> 16) & 255)) >> 2),
				   (int)((((p >> 8) & 255) + ((q >> 8) & 255) + ((u >> 8) & 255) + ((v >> 8) & 255)) >> 2),
				   (int)(((p & 255) + (q & 255) + (u & 255) + (v & 255)) >> 2));
		}
	}
	sg_blur(frost_buf, frost_tmp, hw, hh, (int)(U * 0.65f) + 1, 2);
	uint8_t lut[3][256];                                                   /* darkening, as a table */
	float k = 1 - dark;
	for (int v = 0; v < 256; v++) { int a2 = (int)(v * k); lut[0][v] = (uint8_t)sg_min(255, a2 + 4); lut[1][v] = (uint8_t)sg_min(255, a2 + 3); lut[2][v] = (uint8_t)sg_min(255, a2 + 4); }
	float cx = x + w / 2, cy = y + h / 2, hx = w / 2 - r, hy2 = h / 2 - r;
	for (int j = 0; j < bh; j++) {
		uint32_t *d = fb.px + (y0 + j) * W + x0;
		const uint32_t *srow = frost_buf + (j >> 1) * hw;
		float qy = sg_abs(y0 + j + 0.5f - cy) - hy2;
		for (int i = 0; i < bw; i++) {
			float qx = sg_abs(x0 + i + 0.5f - cx) - hx, dd;
			if (qx <= 0 && qy <= 0) dd = sg_max(qx, qy) - r;
			else if (qx > 0 && qy > 0) dd = sg_sqrt(qx * qx + qy * qy) - r;
			else dd = sg_max(qx, qy) - r;
			float cov = 0.5f - dd;
			if (cov <= 0) continue;
			uint32_t c = srow[i >> 1];
			c = RGB(lut[0][(c >> 16) & 255], lut[1][(c >> 8) & 255], lut[2][c & 255]);
			d[i] = cov >= 1 ? c : sg_mix(d[i], c, cov);
		}
	}
	sg_rrect(&fb, x, y, w, h, r, sg_max(1, U * 0.11f), WHITE, 0.12f);                                     /* glass edge */
	sg_rrect(&fb, x + r * 0.5f, y + sg_max(1, U * 0.1f), w - r, sg_max(1, U * 0.1f), U * 0.05f, 0, WHITE, 0.10f);   /* lit top */
	if (frost_static) {                                                    /* keep it for next time (oldest out) */
		int slot = 0;
		for (int i = 0; i < FROST_CACHE; i++) { if (!frost_cache[i].px) { slot = i; break; } if (frost_cache[i].used < frost_cache[slot].used) slot = i; }
		if (frost_cache[slot].px && (frost_cache[slot].bw != bw || frost_cache[slot].bh != bh)) frost_drop(slot);   /* (the bytes are counted by size) */
		size_t cap = (size_t)W * H * 6, need = (size_t)bw * bh * 4;      /* 1.5 screens of 4-byte pixels */
		while (!frost_cache[slot].px && frost_bytes + need > cap) {       /* make room: drop the least recently used */
			int old = -1;
			for (int i = 0; i < FROST_CACHE; i++) if (frost_cache[i].px && (old < 0 || frost_cache[i].used < frost_cache[old].used)) old = i;
			if (old < 0) break;
			frost_drop(old);
		}
		if (!frost_cache[slot].px && frost_bytes + need <= cap && (frost_cache[slot].px = malloc(need))) frost_bytes += need;
		if (frost_cache[slot].px) {
			frost_cache[slot].x0 = x0; frost_cache[slot].y0 = y0; frost_cache[slot].bw = bw; frost_cache[slot].bh = bh; frost_cache[slot].key = key; frost_cache[slot].used = frost_clock;
			for (int j = 0; j < bh; j++) memcpy(frost_cache[slot].px + j * bw, fb.px + (y0 + j) * W + x0, (size_t)bw * 4);
		}
	}
}

/* a key cap with its meaning beside it; returns where the next one starts */
static float ui_key(float x, float cy, const char *key, const char *label, float a)
{
	int wide = 0; for (const char *p = key; *p; p++) if ((unsigned char)*p >= 0x80) wide = 1;
	const Font *kf = wide ? &FT : &FC;                    /* the caption font has no arrows */
	float ks = wide ? U * 1.5f : U * 0.85f, kw = sg_text_w(kf, key, ks, 0.14f) + U * 1.8f, kh = U * 2.7f;
	sg_rrect(&fb, x, cy - kh / 2, kw, kh, U * 0.6f, 0, WHITE, 0.10f * a);
	sg_rrect(&fb, x, cy - kh / 2, kw, kh, U * 0.6f, sg_max(1, U * 0.1f), WHITE, 0.32f * a);
	sg_text(&fb, kf, x + U * 0.9f, cy + ks * 0.36f, key, ks, WHITE, a, 0.14f);
	float lx = x + kw + U * 0.9f;
	return sg_text(&fb, &FT, lx, cy + U * 0.55f, label, U * 1.5f, RGB(196, 196, 202), a, 0.01f) + U * 2.4f;
}

/* the hint bar along the bottom: pairs of key and meaning, on frosted glass */
static void ui_footer(float x, const char *const *kv, int n)
{
	float w = U * 2.4f;
	for (int i = 0; i < n; i++) w += ((unsigned char)kv[2 * i][0] >= 0x80 ? sg_text_w(&FT, kv[2 * i], U * 1.5f, 0.14f) : sg_text_w(&FC, kv[2 * i], U * 0.85f, 0.14f)) + U * 1.8f + U * 0.9f + sg_text_w(&FT, kv[2 * i + 1], U * 1.5f, 0.01f) + U * 2.4f;
	float h = U * 4.8f, y = H - U * 7.0f;
	ui_frost(x, y, w - U * 1.2f, h, h / 2, 0.55f);
	float cx = x + U * 1.8f;
	for (int i = 0; i < n; i++) cx = ui_key(cx, y + h / 2, kv[2 * i], kv[2 * i + 1], 1);
}
