/*
 * The Search pane: YouTube search. Type, Enter, pick a video, Enter opens its page.
 * The Files pane has its own search (draw_files_search below).
 */
#pragma once
#include "ui.h"
#include "net.h"


/* ---------------------------------------------------------------- the background: a parade */
/* A hypnotic loop: a slowly turning dark-red swirl, and rows of identical hooded figures walking
 * across it at different depths. Black first; the red stays low. */
static float s_age;                                   /* seconds since the Search pane opened */
static uint8_t *swirl; static int swirl_w, swirl_h;

static void swirl_frame(float t)
{
	int sw = W / 4 + 2, sh = H / 4 + 2;
	if (swirl_w != sw || swirl_h != sh) { free(swirl); swirl = malloc((size_t)sw * sh); swirl_w = sw; swirl_h = sh; }
	if (!swirl) return;
	float cx = W * 0.56f, cy = H * 0.76f, maxr = sg_sqrt((float)W * W + (float)H * H) * 0.62f;
	for (int j = 0; j < sh; j++)
		for (int i = 0; i < sw; i++) {
			float dx = i * 4 - cx, dy = (j * 4 - cy) * 1.05f;
			float r = sg_sqrt(dx * dx + dy * dy), a = atan2f(dy, dx);
			float wob = sg_sin(a * 5 + t * 0.7f) * 0.05f + sg_sin(a * 3 - t * 0.4f) * 0.03f;
			float ph = r / (U * 9.0f) + a * (3.0f / 6.2832f) - t * 0.22f + wob;
			float band = sg_smooth((sg_abs(sg_fract(ph) - 0.5f) * 2 - 0.30f) * 4.0f);       /* soft stripes */
			float vig = sg_sat(1 - r / maxr);
			float yy = (j * 4) / (float)H, mask = sg_smooth((yy - 0.52f) / 0.10f) * sg_smooth((0.995f - yy) / 0.03f);   /* a band across the lower half, like a film strip */
			swirl[j * sw + i] = (uint8_t)(255 * band * sg_sat(vig * 1.6f) * mask);
		}
}

static void parade_bg(float t)
{
	swirl_frame(t);
	int sw = swirl_w;
	for (int y = 0; y < H; y++) {
		float fy = y / 4.0f; int j = (int)fy; float ty = fy - j;
		uint32_t *row = fb.px + y * W;
		for (int x = 0; x < W; x++) {
			float fx = x / 4.0f; int i = (int)fx; float tx = fx - i;
			const uint8_t *p = swirl + j * sw + i;
			float v = ((p[0] * (1 - tx) + p[1] * tx) * (1 - ty) + (p[sw] * (1 - tx) + p[sw + 1] * tx) * ty) * (1.0f / 255);
			int r = (int)(7 + v * 34), g = (int)(6 + v * 3), b = (int)(8 + v * 5);
			row[x] = RGB(r, g, b);
		}
	}
	sg_rect(&fb, 0, 0, W, H * 0.46f, BLACK, 0.0f);
	/* keep the top calm for the search bar, and the middle darker for reading */
	for (int y = 0; y < H * 0.62f; y++) { float f = 1 - (float)y / (H * 0.62f); sg_rect(&fb, 0, y, W, 1, BLACK, 0.50f * f * f + 0.20f * f); }
}

#include "rooms.h"

static void search_footer(Home *h, const char *const *kv, int n);

static void length_badge(float x, float y, const Video *v)         /* bottom-right of a thumbnail at (x, y) */
{
	const char *s = v->live ? "LIVE" : v->length;
	if (!*s) return;
	float sz = U * 0.9f, w = sg_text_w(&FC, s, sz, 0.12f) + sz * 1.6f, hh = sz * 2.4f;
	sg_rrect(&fb, x - w, y - hh, w, hh, hh * 0.28f, 0, v->live ? ACCENT : BLACK, v->live ? 1 : 0.78f);
	sg_text(&fb, &FC, x - w + sz * 0.8f, y - hh / 2 + sz * 0.36f, s, sz, WHITE, 1, 0.12f);
}

static void draw_watch(Home *h, float t)
{
	Yt *y = &yt;
	const Video *v = &y->v[y->watch];
	scene_bg(t);
	float lx = U * 6, top = U * 7, pw = W * 0.56f, ph = pw * 9 / 16;
	/* player */
	sg_blob(&fb, lx + pw / 2, top + ph / 2 + U * 2, pw * 0.6f, ph * 0.7f, BLACK, 0.7f);
	thumb_draw(v->id, 1, y->fetch, lx, top, pw, ph, U * 1.2f);
	sg_rrect(&fb, lx, top + ph * 0.55f, pw, ph * 0.45f, U * 1.2f, 0, BLACK, 0.35f);
	float pr = U * 4.6f, pcx = lx + pw / 2, pcy = top + ph / 2;
	sg_disc(&fb, pcx, pcy, pr * (1 + 0.04f * sg_sin(t * 4)), ACCENT, 0.95f);
	V2 tri[3] = {{pcx - pr * 0.28f, pcy - pr * 0.42f}, {pcx - pr * 0.28f, pcy + pr * 0.42f}, {pcx + pr * 0.46f, pcy}};
	sg_poly(&fb, tri, 3, WHITE, 1, 0);
	length_badge(lx + pw - U * 1.2f, top + ph - U * 1.2f, v);
	sg_rrect(&fb, lx + U * 2, top + ph - U * 2.4f, pw - U * 4, U * 0.45f, U * 0.2f, 0, WHITE, 0.25f);
	sg_rrect(&fb, lx + U * 2, top + ph - U * 2.4f, (pw - U * 4) * 0.0f + U * 0.5f, U * 0.45f, U * 0.2f, 0, ACCENT, 1);
	/* title and info */
	float ty = top + ph + U * 4.6f;
	char ln[4][160];
	int n = ui_wrap(v->title, U * 2.6f, pw, 2, ln);
	for (int i = 0; i < n; i++) sg_text(&fb, &FT, lx, ty + i * U * 3.4f, ln[i], U * 2.6f, WHITE, 1, 0.01f);
	ty += n * U * 3.4f + U * 1.2f;
	char meta[160]; snprintf(meta, sizeof meta, "%s%s%s", v->views, v->views[0] && v->age[0] ? "  -  " : "", v->age);
	sg_text(&fb, &FT, lx, ty, meta, U * 1.7f, RGB(170, 170, 176), 1, 0.01f);
	ty += U * 3.6f;
	ui_avatar(lx + U * 2.2f, ty + U * 1.4f, U * 2.2f, v->channel);
	sg_text(&fb, &FT, lx + U * 5.4f, ty + U * 2.0f, v->channel, U * 2.0f, WHITE, 1, 0.01f);
	ty += U * 6.2f;
	n = ui_wrap(v->desc[0] ? v->desc : "No description.", U * 1.8f, pw, 3, ln);
	ui_card(lx, ty - U * 2, pw, n * U * 2.7f + U * 3, U * 1.2f, BLACK, 0.45f, 0);
	for (int i = 0; i < n; i++) sg_text(&fb, &FT, lx + U * 2, ty + U * 0.8f + i * U * 2.7f, ln[i], U * 1.8f, RGB(210, 210, 216), 1, 0.01f);
	/* up next */
	float rx = lx + pw + U * 4, rw = W - rx - U * 6, rh = U * 9.4f, ry = top;
	cap_text(rx, ry + U * 1, "UP NEXT", U * 1.0f, RGB(176, 176, 182), 1, 0);
	ry += U * 3;
	int shown = 0;
	for (int i = 0; i < y->nv && shown < 7; i++) {
		if (i == y->watch) continue;
		const Video *o = &y->v[i];
		float tw = rh * 16 / 9;
		thumb_draw(o->id, 0, y->fetch, rx, ry, tw, rh, U * 0.7f);
		length_badge(rx + tw - U * 0.5f, ry + rh - U * 0.5f, o);
		char t2[2][160]; int m = ui_wrap(o->title, U * 1.5f, rw - tw - U * 1.6f, 2, t2);
		for (int k = 0; k < m; k++) sg_text(&fb, &FT, rx + tw + U * 1.4f, ry + U * 2.6f + k * U * 2.2f, t2[k], U * 1.5f, WHITE, 1, 0.01f);
		char f[96]; fit(o->channel, U * 1.3f, rw - tw - U * 1.6f, f, sizeof f);
		sg_text(&fb, &FT, rx + tw + U * 1.4f, ry + U * 2.6f + m * U * 2.2f + U * 0.4f, f, U * 1.3f, RGB(150, 150, 156), 1, 0.01f);
		ry += rh + U * 1.0f; shown++;
	}
	/* the player is not built: say so plainly */
	ui_chip(lx, H - U * 10.6f, "PLAYBACK IS NOT BUILT YET - THIS PAGE IS YOURS TO CHANGE", U * 0.85f, RGB(255, 190, 70), BLACK, 0.55f);
	{ static const char *kv[] = {"ESC", "back to results"}; search_footer(h, kv, 1); }
	(void)h;
}

static void draw_page(Home *h, float t)
{
	Yt *y = &yt;
	const Web *w = &y->web[y->watch];
	scene_bg(t);
	float lx = W * 0.05f, lw = W * 0.56f, ty = U * 8;
	char du[200]; web_display_url(w->url, du, sizeof du);
	ui_card(lx, ty, lw, H - ty - U * 14, U * 1.8f, BLACK, 0.78f, 0.5f);
	float cx = lx + U * 4, cw = lw - U * 8;
	ui_avatar(cx + U * 1.4f, ty + U * 5, U * 1.4f, du);
	sg_text(&fb, &FT, cx + U * 4.2f, ty + U * 5.8f, du, U * 1.8f, RGB(150, 190, 150), 1, 0.01f);
	char ln[3][160]; int n = ui_wrap(w->title, U * 3.4f, cw, 3, ln);
	for (int i = 0; i < n; i++) sg_text(&fb, &FT, cx, ty + U * 12.5f + i * U * 4.4f, ln[i], U * 3.4f, WHITE, 1, 0.01f);
	float sy = ty + U * 12.5f + n * U * 4.4f + U * 2;
	char sn[5][160]; int m = ui_wrap(w->snip, U * 2.2f, cw, 5, sn);
	for (int i = 0; i < m; i++) sg_text(&fb, &FT, cx, sy + i * U * 3.2f, sn[i], U * 2.2f, RGB(206, 206, 212), 1, 0.01f);
	cap_text(cx, sy + m * U * 3.2f + U * 4, "ADDRESS", U * 0.95f, RGB(150, 150, 156), 1, 0);
	char uf[200]; fit(w->url, U * 2.0f, cw, uf, sizeof uf);
	sg_text(&fb, &FT, cx, sy + m * U * 3.2f + U * 7.6f, uf, U * 2.0f, WHITE, 1, 0.01f);
	/* downloading this page (or file) into Downloads */
	{
		float dy = H - U * 22.5f;
		if (dl.state == DL_RUN || dl.state == DL_DONE || dl.state == DL_FAIL) {
			char sz[32], line[300];
			long b = dl_bytes();
			if (b >= 1000000) snprintf(sz, sizeof sz, "%.1f MB", b / 1e6); else snprintf(sz, sizeof sz, "%ld KB", b / 1000);
			if (dl.state == DL_RUN) snprintf(line, sizeof line, "Downloading %s  -  %s", dl.name, sz);
			else if (dl.state == DL_DONE) snprintf(line, sizeof line, "Saved to Downloads:  %s  (%s)", dl.name, sz);
			else snprintf(line, sizeof line, "That download did not work. Check your network.");
			icon(dl.state == DL_FAIL ? I_INFO : I_DOWNLOAD, cx + U * 1.4f, dy - U * 0.6f, U * 1.3f, dl.state == DL_FAIL ? RGB(255, 120, 110) : dl.state == DL_DONE ? RGB(170, 220, 170) : WHITE, 1);
			char lf[300]; fit(line, U * 1.8f, cw - U * 4, lf, sizeof lf);
			sg_text(&fb, &FT, cx + U * 4, dy, lf, U * 1.8f, WHITE, 1, 0.01f);
			if (dl.state == DL_RUN) { float ph = sg_fract((float)now() * 0.8f); sg_rrect(&fb, cx, dy + U * 2.0f, cw, U * 0.5f, U * 0.25f, 0, WHITE, 0.12f); sg_rrect(&fb, cx + (cw - cw * 0.25f) * ph, dy + U * 2.0f, cw * 0.25f, U * 0.5f, U * 0.25f, 0, RED, 1); }
		}
	}
	ui_chip(lx, H - U * 10.6f, "NO WEB BROWSER YET  -  PRESS D TO DOWNLOAD IT TO YOUR DOWNLOADS FOLDER", U * 0.85f, RGB(255, 190, 70), BLACK, 0.55f);
	{ static const char *kv[] = {"D", "download", "ESC", "back to results"}; search_footer(h, kv, 2); }
	(void)h; (void)t;
}

static float row_in(int i) { return (1 - sg_smooth((s_age - 0.30f - 0.07f * i) / 0.40f)) * U * 4; }

/* the gear and the hint bar along the bottom of every Search screen */
static void search_footer(Home *h, const char *const *kv, int n)
{
	float gr = U * 2.4f, gx = U * 5 + gr, gy = H - U * 4.6f;
	int on = h->focus == 2 && yt.state != Y_WATCH;
	if (on) sg_glow(&fb, gx, gy, gr * 3.0f, ACCENT, 0.20f);
	ui_frost(gx - gr, gy - gr, gr * 2, gr * 2, gr, 0.5f);
	if (on) { sg_disc(&fb, gx, gy, gr, WHITE, 0.95f); sg_ring(&fb, gx, gy, gr, sg_max(1.5f, U * 0.18f), ACCENT, 1); }
	icon(I_SETTINGS, gx, gy, gr * 0.55f, on ? INK : WHITE, 1);
	float fx = gx + gr + U * 1.6f;
	if (on) { ui_frost(fx, gy - U * 2.4f, U * 15, U * 4.8f, U * 2.4f, 0.55f); cap_text(fx + U * 2, gy + U * 0.45f, "OPEN SETTINGS", U * 1.0f, WHITE, 1, 0); fx += U * 16.6f; }
	ui_footer(fx, kv, n);
	cap_text(W - U * 5, gy + U * 0.45f, "SILVER OS", U * 1.0f, RGB(150, 150, 156), 1, 1);
}

static float seg_pos;                                         /* where the Web/Videos highlight sits, eased */
#define GRID_COLS 4

/* the header row shared by the results screens: the word SEARCH, then the bar */
static void search_header(Home *h, float t, float op)
{
	Yt *y = &yt;
	float tx = U * 5, ty = U * 10.6f, tsz = U * 6.2f;
	sg_text(&fb, &FD, tx + U * 0.4f, ty, "SEARCH", tsz, ACCENT, 0.85f * op, 0);
	sg_text(&fb, &FD, tx, ty, "SEARCH", tsz, WHITE, op, 0);
	float bx0 = tx + sg_text_w(&FD, "SEARCH", tsz, 0) + U * 3.5f, by = U * 4.0f, bh = U * 7.6f, full = W - U * 5 - bx0;
	float bw = sg_max(bh, full * op);
	if (h->focus == 0) sg_glow(&fb, bx0 + bw / 2, by + bh / 2, bw * 0.4f, ACCENT, 0.08f);
	ui_frost(bx0, by, bw, bh, bh / 2, 0.6f);
	sg_rrect(&fb, bx0, by, bw, bh, bh / 2, sg_max(1.5f, U * 0.2f), h->focus == 0 ? ACCENT : WHITE, h->focus == 0 ? 0.95f : 0.18f);
	if (op < 0.98f) return;
	icon(I_SEARCH, bx0 + bh * 0.62f, by + bh / 2, U * 1.6f, h->focus == 0 ? WHITE : RGB(170, 170, 176), 1);
	float sw = U * 21, sh = bh - U * 2.0f, sx = bx0 + full - sw - U * 1.0f, sy = by + U * 1.0f;
	seg_pos += ((float)y->mode - seg_pos) * 0.25f;
	sg_rrect(&fb, sx, sy, sw, sh, sh / 2, 0, WHITE, 0.07f);
	sg_rrect(&fb, sx + U * 0.4f + seg_pos * (sw / 2), sy + U * 0.4f, sw / 2 - U * 0.8f, sh - U * 0.8f, (sh - U * 0.8f) / 2, 0, WHITE, 0.95f);
	static const char *seg[2] = {"WEB", "VIDEOS"};
	for (int i = 0; i < 2; i++) {
		float on = 1 - sg_abs(seg_pos - i), w = sg_text_w(&FC, seg[i], U * 1.0f, 0.2f);
		sg_text(&fb, &FC, sx + sw / 4 + i * sw / 2 - w / 2, sy + sh / 2 + U * 0.38f, seg[i], U * 1.0f, sg_mix(RGB(180, 180, 186), INK, on), 1, 0.2f);
	}
	float qx0 = bx0 + bh * 1.2f, tb = by + bh / 2 + U * 1.0f;
	if (!y->q[0]) sg_text(&fb, &FT, qx0, tb, y->mode ? "Search YouTube" : "Search the internet", U * 2.7f, RGB(130, 130, 136), 1, 0.01f);
	float qx = sg_text(&fb, &FT, qx0, tb, y->q, U * 2.7f, WHITE, 1, 0.01f);
	if (h->focus == 0 && ((int)(t * 2) & 1)) sg_rect(&fb, qx + U * 0.3f, by + U * 1.7f, sg_max(2, U * 0.26f), bh - U * 3.4f, ACCENT, 1);
	if (y->q[0] && h->focus == 0) ui_key(sx - U * 13.5f, by + bh / 2, "ENTER", "search", 0.85f);
}

/* web results: titles down the left, the chosen one large on the right */
static void draw_web(Home *h, float t, float top, float bot)
{
	Yt *y = &yt;
	float lx = U * 5, lw = W * 0.36f, rx = lx + lw + U * 2.5f, rw = W - U * 5 - rx, rh = U * 8.4f;
	int cnt = y->nw, vis = (int)((bot - top) / rh);
	if (vis < 1) vis = 1;
	if (y->sel >= cnt) y->sel = cnt - 1;
	if (y->sel < y->top) y->top = y->sel;
	if (y->sel >= y->top + vis) y->top = y->sel - vis + 1;
	ui_frost(lx, top, lw, vis * rh + U * 1.2f, U * 2.0f, 0.62f);
	for (int i = 0; i < vis && y->top + i < cnt; i++) {
		int idx = y->top + i, sel = idx == y->sel, foc = sel && h->focus == 1;
		const Web *w = &y->web[idx];
		float ry = top + U * 0.6f + i * rh + row_in(i), ix = lx + U * 0.8f, iw = lw - U * 1.6f;
		if (sel) {
			sg_rrect(&fb, ix, ry + U * 0.3f, iw, rh - U * 0.6f, U * 1.3f, 0, WHITE, foc ? 0.95f : 0.16f);
			if (foc) sg_rrect(&fb, ix + U * 0.6f, ry + rh * 0.27f, U * 0.45f, rh * 0.46f, U * 0.22f, 0, ACCENT, 1);
		}
		char du[200]; web_display_url(w->url, du, sizeof du);
		char dom[64]; { const char *g = strstr(du, " > "); size_t l = g ? (size_t)(g - du) : strlen(du); if (l > 63) l = 63; memcpy(dom, du, l); dom[l] = 0; }
		ui_avatar(ix + U * 2.8f, ry + rh / 2, U * 1.5f, dom);
		float cx = ix + U * 5.4f, cw = iw - U * 6.6f;
		char tf[200]; fit(w->title, U * 1.9f, cw, tf, sizeof tf);
		sg_text(&fb, &FT, cx, ry + rh * 0.47f, tf, U * 1.9f, foc ? INK : WHITE, 1, 0.01f);
		char df[80]; fit(dom, U * 1.35f, cw, df, sizeof df);
		sg_text(&fb, &FT, cx, ry + rh * 0.47f + U * 2.4f, df, U * 1.35f, foc ? RGB(90, 90, 98) : RGB(160, 160, 166), 1, 0.01f);
	}
	ui_scroll(lx + lw - U * 1.0f, top + U * 1.0f, vis * rh - U * 0.8f, y->top, vis, cnt);
	/* the preview */
	const Web *w = &y->web[y->sel];
	float ph = bot - top, pin = sg_smooth((s_age - 0.35f) / 0.4f);
	ui_frost(rx, top, rw, ph, U * 2.4f, 0.66f);
	sg_rect(&fb, rx + U * 3, top + U * 3.4f, U * 6, sg_max(2, U * 0.35f), ACCENT, pin);
	char du[200]; web_display_url(w->url, du, sizeof du);
	char dom[64]; { const char *g = strstr(du, " > "); size_t l = g ? (size_t)(g - du) : strlen(du); if (l > 63) l = 63; memcpy(dom, du, l); dom[l] = 0; }
	ui_avatar(rx + U * 4.6f, top + U * 7.8f, U * 1.6f, dom);
	sg_text(&fb, &FT, rx + U * 7.2f, top + U * 8.5f, dom, U * 1.8f, RGB(200, 200, 206), pin, 0.01f);
	char ln[4][160]; int n = ui_wrap(w->title, U * 3.6f, rw - U * 6, 3, ln);
	for (int i = 0; i < n; i++) sg_text(&fb, &FT, rx + U * 3, top + U * 15.5f + i * U * 4.6f, ln[i], U * 3.6f, WHITE, pin, 0.0f);
	float sy = top + U * 15.5f + n * U * 4.6f + U * 1.6f;
	char sn[7][160]; int m = ui_wrap(w->snip[0] ? w->snip : "No summary for this page.", U * 2.0f, rw - U * 6, 6, sn);
	for (int i = 0; i < m && sy + i * U * 3.0f < bot - U * 9; i++) sg_text(&fb, &FT, rx + U * 3, sy + i * U * 3.0f, sn[i], U * 2.0f, RGB(196, 196, 202), pin, 0.01f);
	char uf[200]; fit(w->url, U * 1.5f, rw - U * 6, uf, sizeof uf);
	sg_rect(&fb, rx + U * 3, bot - U * 8.2f, rw - U * 6, sg_max(1, U * 0.1f), WHITE, 0.12f);
	sg_text(&fb, &FT, rx + U * 3, bot - U * 4.4f, uf, U * 1.5f, RGB(150, 150, 156), pin, 0.01f);
	(void)t;
}

/* video results: a grid of big thumbnails */
static void draw_grid(Home *h, float t, float top, float bot)
{
	Yt *y = &yt;
	float gx = U * 5, gw = W - U * 10, gap = U * 2.2f, cw = (gw - gap * (GRID_COLS - 1)) / GRID_COLS, th = cw * 9 / 16, ch = th + U * 9.6f;
	int rows = (int)((bot - top + gap) / (ch + gap));
	if (rows < 1) rows = 1;
	int cnt = y->nv, srow = y->sel / GRID_COLS, trow = y->top / GRID_COLS;
	if (srow < trow) trow = srow;
	if (srow >= trow + rows) trow = srow - rows + 1;
	y->top = trow * GRID_COLS;
	for (int r = 0; r < rows; r++)
		for (int c = 0; c < GRID_COLS; c++) {
			int idx = (trow + r) * GRID_COLS + c;
			if (idx >= cnt) continue;
			const Video *v = &y->v[idx];
			int sel = idx == y->sel && h->focus == 1;
			float in = sg_smooth((s_age - 0.25f - 0.05f * (r * GRID_COLS + c)) / 0.35f);
			float x = gx + c * (cw + gap), yy = top + r * (ch + gap) + (1 - in) * U * 4 - (sel ? U * 0.8f : 0);
			if (sel) { sg_glow(&fb, x + cw / 2, yy + th / 2, cw * 0.62f, ACCENT, 0.16f); sg_blob(&fb, x + cw / 2, yy + ch / 2 + U * 1.5f, cw * 0.55f, ch * 0.6f, BLACK, 0.6f); }
			ui_frost(x, yy, cw, ch, U * 1.4f, sel ? 0.45f : 0.62f);
			thumb_draw(v->id, 0, y->fetch, x, yy, cw, th, U * 1.4f);
			length_badge(x + cw - U * 0.8f, yy + th - U * 0.8f, v);
			if (sel) sg_rrect(&fb, x - U * 0.2f, yy - U * 0.2f, cw + U * 0.4f, ch + U * 0.4f, U * 1.6f, sg_max(2, U * 0.28f), WHITE, 0.95f);
			ui_avatar(x + U * 2.4f, yy + th + U * 3.0f, U * 1.3f, v->channel);
			char ln[2][160]; int n = ui_wrap(v->title, U * 1.65f, cw - U * 5.6f, 2, ln);
			for (int k = 0; k < n; k++) sg_text(&fb, &FT, x + U * 4.6f, yy + th + U * 3.4f + k * U * 2.3f, ln[k], U * 1.65f, WHITE, in, 0.01f);
			char meta[160]; snprintf(meta, sizeof meta, "%s%s%s", v->channel, v->views[0] ? "  \xC2\xB7  " : "", v->views);
			char mf[160]; fit(meta, U * 1.3f, cw - U * 5.6f, mf, sizeof mf);
			sg_text(&fb, &FT, x + U * 4.6f, yy + th + U * 3.4f + n * U * 2.3f + U * 0.5f, mf, U * 1.3f, RGB(160, 160, 166), in, 0.01f);
		}
	int total_rows = (cnt + GRID_COLS - 1) / GRID_COLS;
	if (total_rows > rows) ui_scroll(W - U * 3.2f, top, rows * (ch + gap) - gap, trow, rows, total_rows);
	(void)t;
}

static void draw_search(Home *h, float t)
{
	Yt *y = &yt;
	if (y->state == Y_WATCH) { if (y->mode) draw_watch(h, t); else draw_page(h, t); return; }
	scene_bg(t);
	float op = sg_smooth(s_age / 0.55f);
	search_header(h, t, op);
	float top = U * 15.5f, bot = H - U * 10.0f, lx = U * 5;
	if (y->state == Y_IDLE) {
		float w0 = W * 0.48f;
		ui_frost(lx, top, w0, U * 21, U * 2.2f, 0.62f);
		cap_text(lx + U * 3, top + U * 4.4f, "TRY", U * 1.0f, ACCENT, 1, 0);
		static const char *ex[] = {"how operating systems boot", "linux from scratch", "lofi beats", "weather"};
		float cx = lx + U * 3, cy = top + U * 9.4f;
		for (int i = 0; i < 4; i++) {
			float w = sg_text_w(&FT, ex[i], U * 1.8f, 0.01f) + U * 4.0f;
			if (cx + w > lx + w0 - U * 2) { cx = lx + U * 3; cy += U * 5.2f; }
			sg_rrect(&fb, cx, cy - U * 2.0f, w, U * 4.0f, U * 2.0f, 0, WHITE, 0.08f);
			sg_rrect(&fb, cx, cy - U * 2.0f, w, U * 4.0f, U * 2.0f, sg_max(1, U * 0.1f), WHITE, 0.2f);
			sg_text(&fb, &FT, cx + U * 2.0f, cy + U * 0.65f, ex[i], U * 1.8f, WHITE, 1, 0.01f);
			cx += w + U * 1.2f;
		}
		sg_text(&fb, &FT, lx + U * 3, top + U * 18.2f, "Type anything, press Enter. Tab switches between the web and videos.", U * 1.6f, RGB(170, 170, 176), 1, 0.01f);
	} else if (y->state == Y_LOADING) {
		float ph = 0.5f + 0.5f * sg_sin(t * 5);
		if (y->mode) {
			float gw = W - U * 10, gap = U * 2.2f, cw = (gw - gap * 3) / 4, th = cw * 9 / 16;
			for (int i = 0; i < 8; i++) {
				float x = lx + (i % 4) * (cw + gap), yy = top + (i / 4) * (th + U * 9.6f + gap), p = 0.5f + 0.5f * sg_sin(t * 5 - i * 0.5f);
				sg_rrect(&fb, x, yy, cw, th, U * 1.4f, 0, WHITE, 0.06f + 0.05f * p);
				sg_rrect(&fb, x + U * 4.6f, yy + th + U * 2.2f, cw * 0.6f, U * 1.6f, U * 0.8f, 0, WHITE, 0.07f + 0.04f * p);
				sg_rrect(&fb, x + U * 4.6f, yy + th + U * 5.0f, cw * 0.35f, U * 1.2f, U * 0.6f, 0, WHITE, 0.05f + 0.03f * p);
			}
		} else {
			ui_frost(lx, top, W * 0.36f, bot - top, U * 2.0f, 0.62f);
			for (int i = 0; i < 7; i++) {
				float p = 0.5f + 0.5f * sg_sin(t * 5 - i * 0.6f);
				sg_disc(&fb, lx + U * 3.6f, top + U * 4.8f + i * U * 8.4f, U * 1.5f, WHITE, 0.06f + 0.04f * p);
				sg_rrect(&fb, lx + U * 6.2f, top + U * 3.2f + i * U * 8.4f, W * 0.22f, U * 1.8f, U * 0.9f, 0, WHITE, 0.07f + 0.04f * p);
				sg_rrect(&fb, lx + U * 6.2f, top + U * 6.0f + i * U * 8.4f, W * 0.10f, U * 1.2f, U * 0.6f, 0, WHITE, 0.05f + 0.03f * p);
			}
			ui_frost(lx + W * 0.36f + U * 2.5f, top, W - U * 7.5f - W * 0.36f - U * 2.5f + U * 2.5f, bot - top, U * 2.4f, 0.66f);
			(void)ph;
		}
	} else if (y->state == Y_ERROR) {
		ui_frost(lx, top, W * 0.5f, U * 16, U * 2.0f, 0.62f);
		sg_disc(&fb, lx + U * 4.6f, top + U * 5.6f, U * 1.8f, ACCENT, 0.95f);
		sg_text_c(&fb, &FT, lx + U * 4.6f, top + U * 6.4f, "!", U * 2.2f, WHITE, 1, 0);
		sg_text(&fb, &FT, lx + U * 8, top + U * 6.6f, y->err[0] ? y->err : "Something went wrong.", U * 2.3f, WHITE, 1, 0.01f);
		sg_text(&fb, &FT, lx + U * 8, top + U * 11.0f, "Press Enter to try again.", U * 1.7f, RGB(170, 170, 176), 1, 0.01f);
	} else if (y->mode) draw_grid(h, t, top, bot);
	else draw_web(h, t, top, bot);
	static const char *k_web[] = {"TAB", "web / videos", "\xE2\x86\x91\xE2\x86\x93", "pick", "ENTER", "open", "ESC", "home"};
	static const char *k_vid[] = {"TAB", "web / videos", "\xE2\x86\x90\xE2\x86\x91\xE2\x86\x92\xE2\x86\x93", "pick", "ENTER", "watch", "ESC", "home"};
	static const char *k_idle[] = {"TAB", "web / videos", "ENTER", "search", "\xE2\x86\x93", "settings", "ESC", "home"};
	search_footer(h, y->state != Y_RESULTS ? k_idle : y->mode ? k_vid : k_web, 4);
}

/* ---------------------------------------------------------------- files search */
static void fs_prepare(Home *h)
{
	if (!h->sr_ready) { search_init(&h->sr); h->sr_ready = 1; }
}
static void fs_refresh(Home *h)
{
	fs_prepare(h);
	search_set(&h->sr, h->sr.q, h->fs_scope ? SC_TEXT : SC_FILES);
	h->row = 0; h->top = 0;
}

static float draw_hl(float x, float y, const char *nm, uint64_t mask, float size, uint32_t fg, uint32_t hl, float maxx)
{
	for (int i = 0; nm[i] && x < maxx; i++) {
		char one[2] = {nm[i], 0};
		x = sg_text(&fb, &FT, x, y, one, size, (i < 64 && ((mask >> i) & 1)) ? hl : fg, 1, 0.01f);
	}
	return x;
}

static void draw_files_search(Home *h, float t)
{
	fs_prepare(h);
	Search *s = &h->sr;
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	title_block("FILES", h->fs_scope ? "SEARCHING INSIDE FILES" : "SEARCHING FILE NAMES", I_FILES);
	float lx = U * 6, lw = W - U * 12, by = U * 22, bh = U * 6.6f;
	ui_card(lx, by, lw, bh, bh / 2, BLACK, 0.62f, 0.3f);
	sg_rrect(&fb, lx, by, lw, bh, bh / 2, sg_max(1.5f, U * 0.2f), ACCENT, 0.95f);
	icon(I_SEARCH, lx + bh * 0.72f, by + bh / 2, U * 1.5f, WHITE, 1);
	float tx = lx + bh * 1.3f, tb = by + bh / 2 + U * 0.95f;
	if (!s->q[0]) sg_text(&fb, &FT, tx, tb, "Find a file or folder", U * 2.5f, RGB(120, 120, 126), 1, 0.01f);
	float qx = sg_text(&fb, &FT, tx, tb, s->q, U * 2.5f, WHITE, 1, 0.01f);
	if ((int)(t * 2) & 1) sg_rect(&fb, qx + U * 0.3f, by + U * 1.4f, sg_max(2, U * 0.26f), bh - U * 2.8f, WHITE, 1);
	float cx = lx, cy = by + bh + U * 3.4f;
	static const char *tabs[2] = {"NAMES", "TEXT INSIDE FILES"};
	for (int i = 0; i < 2; i++) cx = ui_chip(cx, cy, tabs[i], U * 0.95f, i == h->fs_scope ? INK : RGB(190, 190, 196), WHITE, i == h->fs_scope ? 0.96f : 0.10f) + U * 1.0f;
	float y0 = cy + U * 4, rh = U * 5.2f;
	int n = search_count(s), vis = (int)((H - y0 - U * 10) / rh);
	if (vis < 1) vis = 1;
	if (h->row >= n) h->row = n ? n - 1 : 0;
	if (h->row < h->top) h->top = h->row;
	if (h->row >= h->top + vis) h->top = h->row - vis + 1;
	if (!s->q[0]) sg_text(&fb, &FT, lx, y0 + U * 5, "Type part of a name. Letters can be spread out: rprt finds report.", U * 2.0f, RGB(150, 150, 156), 1, 0.01f);
	else if (!n) sg_text(&fb, &FT, lx, y0 + U * 5, (s->scanning || s->indexing) ? "Searching..." : "Nothing found", U * 2.2f, RGB(150, 150, 156), 1, 0.01f);
	for (int i = 0; i < vis && h->top + i < n; i++) {
		const SRes *r = search_item(s, h->top + i);
		if (r->node < 0) continue;
		const Node *nd = &s->nodes[r->node];
		float ry = y0 + i * rh; int sel = h->top + i == h->row;
		row_box(lx, ry, lw, rh, sel, 0);
		uint32_t fg = sel ? INK : WHITE, dim = sel ? RGB(70, 70, 78) : RGB(150, 150, 156), hl = sel ? RGB(200, 10, 20) : RGB(255, 96, 86);
		float base = ry + rh / 2 + U * 0.8f;
		icon(nd->kind == N_DIR ? I_FOLDER : I_FILE, lx + U * 3.4f, ry + rh / 2, U * 1.6f, fg, 1);
		char nm[96]; fit(nd->name, U * 2.2f, lw * 0.34f, nm, sizeof nm);
		float ex = draw_hl(lx + U * 7, base, nm, r->type == R_TEXT ? 0 : r->mask, U * 2.2f, fg, hl, lx + lw);
		float sx = sg_max(ex + U * 3, lx + lw * 0.38f);
		if (r->type == R_TEXT) {
			char hd[24]; snprintf(hd, sizeof hd, "line %d", r->line);
			cap_text(sx, base - U * 0.1f, hd, U * 0.95f, dim, 1, 0);
			draw_hl(sx + U * 7, base, r->text, r->mask, U * 1.8f, dim, hl, lx + lw - U * 4);
		} else {
			char path[300]; node_path(s, nd->parent, path, sizeof path);
			char pf[300]; fit(path, U * 1.6f, lx + lw - U * 4 - sx, pf, sizeof pf);
			sg_text(&fb, &FT, sx, base - U * 0.1f, pf, U * 1.6f, dim, 1, 0.01f);
		}
	}
	ui_scroll(W - U * 4, y0, vis * rh, h->top, vis, n);
	char st[96];
	if (s->indexing) snprintf(st, sizeof st, "INDEXING  %d ITEMS", s->n);
	else if (s->scanning) snprintf(st, sizeof st, "SEARCHING INSIDE FILES  %d / %d", s->cpos, s->ncand);
	else snprintf(st, sizeof st, "%d ITEMS INDEXED", s->n);
	cap_text(W - U * 6, H - U * 7.2f, st, U * 0.95f, RGB(140, 140, 146), 1, 1);
	foot_hints("TYPE  FIND      TAB  NAMES OR TEXT      UP DOWN  PICK      ENTER  SHOW IN FILES      ESC  BACK");
}
