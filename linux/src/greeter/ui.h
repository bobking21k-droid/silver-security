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
