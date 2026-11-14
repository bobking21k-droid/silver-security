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
	float cx = W * 0.62f, cy = H * 0.42f, maxr = sg_sqrt((float)W * W + (float)H * H) * 0.62f;
	for (int j = 0; j < sh; j++)
		for (int i = 0; i < sw; i++) {
			float dx = i * 4 - cx, dy = (j * 4 - cy) * 1.05f;
			float r = sg_sqrt(dx * dx + dy * dy), a = atan2f(dy, dx);
			float wob = sg_sin(a * 5 + t * 0.7f) * 0.05f + sg_sin(a * 3 - t * 0.4f) * 0.03f;
			float ph = r / (U * 9.0f) + a * (3.0f / 6.2832f) - t * 0.22f + wob;
			float band = sg_smooth((sg_abs(sg_fract(ph) - 0.5f) * 2 - 0.30f) * 4.0f);       /* soft stripes */
			float vig = sg_sat(1 - r / maxr);
			swirl[j * sw + i] = (uint8_t)(255 * band * vig * vig);
		}
}

static void parade_figure(float x, float base, float s, float ph, float a, int eyes)
{
	float bob = sg_sin(ph * 2) * s * 0.012f, hipy = base - s * 0.40f + bob;
	uint32_t body = RGB(30, 27, 29), edge = RGB(235, 235, 240);
	for (int side = 0; side < 2; side++) {                                           /* legs: two swinging quads */
		float sw = sg_sin(ph + side * 3.14159f) * 0.42f, hx = x + (side ? 0.045f : -0.045f) * s;
		float fx = hx + sg_sin(sw) * s * 0.40f, fy = hipy + sg_cos(sw) * s * 0.40f, wd = s * 0.034f;
		V2 q[4] = {{hx - wd, hipy}, {hx + wd, hipy}, {fx + wd * 0.7f, fy}, {fx - wd * 0.7f, fy}};
		sg_poly(&fb, q, 4, RGB(44, 40, 42), a, 0);
		V2 foot[3] = {{fx - wd, fy}, {fx + wd, fy}, {fx + s * 0.07f, fy + s * 0.012f}};
		sg_poly(&fb, foot, 3, RGB(44, 40, 42), a, 0);
	}
	sg_rrect(&fb, x - s * 0.19f, base - s * 0.80f + bob, s * 0.38f, s * 0.46f, s * 0.12f, 0, body, a);   /* the oversized coat */
	sg_rrect(&fb, x - s * 0.19f, base - s * 0.80f + bob, s * 0.38f, s * 0.46f, s * 0.12f, sg_max(1, s * 0.006f), edge, a * 0.30f);
	sg_disc(&fb, x + s * 0.02f, base - s * 0.89f + bob, s * 0.115f, body, a);                              /* head with a lot of hair */
	sg_disc(&fb, x - s * 0.03f, base - s * 0.93f + bob, s * 0.105f, body, a);
	sg_disc(&fb, x + s * 0.02f, base - s * 0.89f + bob, s * 0.115f, edge, 0);
	if (eyes) { sg_disc(&fb, x + s * 0.05f, base - s * 0.875f + bob, s * 0.011f, RGB(210, 30, 36), a * 0.9f); sg_disc(&fb, x + s * 0.095f, base - s * 0.875f + bob, s * 0.011f, RGB(210, 30, 36), a * 0.9f); }
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
	static const struct { float size, speed, base, alpha; int n; } L[3] = {{0.20f, 38, 0.80f, 0.55f, 11}, {0.28f, 60, 0.90f, 0.80f, 9}, {0.38f, 92, 1.04f, 1.0f, 7}};
	for (int l = 0; l < 3; l++) {
		float s = H * L[l].size, gap = (W + s * 1.2f) / L[l].n, span = gap * L[l].n;
		for (int i = 0; i < L[l].n; i++) {
			float x = fmodf(i * gap + t * L[l].speed * U * 0.1f, span) - s * 0.6f;
			parade_figure(x, H * L[l].base, s, t * (2.6f + l * 0.35f) + i * 0.9f, L[l].alpha, l == 2 && (i % 3 == 0));
		}
	}
	sg_rect(&fb, 0, 0, W, H * 0.46f, BLACK, 0.0f);
	/* keep the top calm for the search bar, and the middle darker for reading */
	for (int y = 0; y < H * 0.62f; y++) { float f = 1 - (float)y / (H * 0.62f); sg_rect(&fb, 0, y, W, 1, BLACK, 0.55f * f * f + 0.18f * f); }
}

static void yt_meta(const Video *v, char *out, size_t n)
{
	char tmp[160] = "";
	snprintf(tmp, sizeof tmp, "%s", v->channel);
	if (v->views[0]) { strncat(tmp, "  -  ", sizeof tmp - strlen(tmp) - 1); strncat(tmp, v->views, sizeof tmp - strlen(tmp) - 1); }
	if (v->age[0]) { strncat(tmp, "  -  ", sizeof tmp - strlen(tmp) - 1); strncat(tmp, v->age, sizeof tmp - strlen(tmp) - 1); }
	snprintf(out, n, "%s", tmp);
}

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
	parade_bg(t);
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
	ui_chip(lx, H - U * 7.6f, "PLAYBACK IS NOT BUILT YET - THIS PAGE IS YOURS TO CHANGE", U * 0.85f, RGB(255, 190, 70), BLACK, 0.55f);
	foot_hints("ESC  BACK TO RESULTS");
	(void)h;
}

static void draw_page(Home *h, float t)
{
	Yt *y = &yt;
	const Web *w = &y->web[y->watch];
	parade_bg(t);
	float lx = W * 0.15f, lw = W * 0.70f, ty = U * 8;
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
	ui_chip(lx, H - U * 7.6f, "THERE IS NO WEB BROWSER YET - THIS PAGE SHOWS THE RESULT ONLY", U * 0.85f, RGB(255, 190, 70), BLACK, 0.55f);
	foot_hints("ESC  BACK TO RESULTS");
	(void)h; (void)t;
}

static float row_in(int i) { return (1 - sg_smooth((s_age - 0.30f - 0.07f * i) / 0.40f)) * U * 4; }

static void draw_search(Home *h, float t)
{
	Yt *y = &yt;
	if (y->state == Y_WATCH) { if (y->mode) draw_watch(h, t); else draw_page(h, t); return; }
	parade_bg(t);
	float op = sg_smooth(s_age / 0.55f);                       /* the bar opens out from the middle */
	float lx = W * 0.15f, lw = W * 0.70f, by = U * 5, bh = U * 7;
	float bx = W / 2 - lw * op / 2, bw = lw * op;
	ui_card(bx, by, bw, bh, bh / 2, BLACK, 0.74f, 0.5f);
	sg_rrect(&fb, bx, by, bw, bh, bh / 2, sg_max(1.5f, U * 0.18f), h->focus == 0 ? ACCENT : WHITE, h->focus == 0 ? 0.85f : 0.16f);
	if (h->focus == 0) sg_glow(&fb, W / 2, by + bh / 2, lw * 0.5f, ACCENT, 0.05f);
	if (op > 0.98f) {
	icon(I_SEARCH, lx + bh * 0.72f, by + bh / 2, U * 1.5f, WHITE, 1);
	float tx = lx + bh * 1.3f, tb = by + bh / 2 + U * 0.95f;
	if (!y->q[0]) sg_text(&fb, &FT, tx, tb, y->mode ? "Search YouTube" : "Search the internet", U * 2.5f, RGB(120, 120, 126), 1, 0.01f);
	float qx = sg_text(&fb, &FT, tx, tb, y->q, U * 2.5f, WHITE, 1, 0.01f);
	if (h->focus == 0 && ((int)(t * 2) & 1)) sg_rect(&fb, qx + U * 0.3f, by + U * 1.4f, sg_max(2, U * 0.26f), bh - U * 2.8f, WHITE, 1);
	if (y->q[0]) { ui_pill(lx + lw - U * 11.2f, by + U * 1.2f, U * 10, bh - U * 2.4f, ACCENT, h->focus == 0 ? 0.9f : 0.5f); cap_text(lx + lw - U * 6.2f - sg_text_w(&FC, "SEARCH", U * 0.95f, 0.2f) / 2, by + bh / 2 + U * 0.4f, "SEARCH", U * 0.95f, WHITE, 1, 0); }
	}
	/* the gear, bottom left: Enter opens Settings */
	{
		float gr = U * 3.4f, gx = U * 7.5f, gy = H - U * 10.5f;
		int on = h->focus == 2;
		if (on) sg_glow(&fb, gx, gy, gr * 3.2f, ACCENT, 0.16f);
		sg_disc(&fb, gx, gy, gr, on ? WHITE : BLACK, on ? 0.96f : 0.7f);
		sg_ring(&fb, gx, gy, gr, sg_max(1.5f, U * 0.16f), on ? ACCENT : WHITE, on ? 0.95f : 0.22f);
		icon(I_SETTINGS, gx, gy, gr * 0.56f, on ? INK : WHITE, 1);
		if (on) cap_text(gx + gr + U * 1.6f, gy + U * 0.4f, "SETTINGS", U * 1.0f, WHITE, 1, 0);
	}
	if (op > 0.98f) { float cx = lx; static const char *tb2[2] = {"WEB", "VIDEOS"};
	  for (int i = 0; i < 2; i++) cx = ui_chip(cx, by + bh + U * 3.4f, tb2[i], U * 0.95f, i == y->mode ? INK : RGB(190, 190, 196), WHITE, i == y->mode ? 0.96f : 0.10f) + U * 1.0f; }
	float ry0 = by + bh + U * 7.4f, rh = y->mode ? U * 14.2f : U * 12.4f;
	if (y->state == Y_IDLE) {
		static const char *ex[] = {"how operating systems boot", "linux from scratch", "weather", "lofi beats"};
		cap_text(lx, ry0 + U * 2, "TRY", U * 1.0f, RGB(176, 176, 182), 1, 0);
		float cx = lx;
		for (int i = 0; i < 4; i++) cx = ui_chip(cx, ry0 + U * 7, ex[i], U * 1.0f, WHITE, WHITE, 0.10f) + U * 1.2f;
		sg_text(&fb, &FT, lx, ry0 + U * 14, "Type anything, then press Enter. Tab switches between the web and videos.", U * 2.2f, RGB(150, 150, 156), 1, 0.01f);
	} else if (y->state == Y_LOADING) {
		for (int i = 0; i < 4; i++) {                                    /* skeleton rows */
			float ry = ry0 + i * rh, ph = sg_fract(t * 0.8f - i * 0.12f);
			sg_rrect(&fb, lx, ry + U * 0.8f, rh * 16 / 9 - U * 2, rh - U * 1.6f, U * 1.0f, 0, WHITE, 0.07f + 0.05f * sg_sin(ph * 6.28f));
			sg_rrect(&fb, lx + rh * 16 / 9 + U * 1.2f, ry + U * 2, lw * 0.5f, U * 2.2f, U * 0.5f, 0, WHITE, 0.09f);
			sg_rrect(&fb, lx + rh * 16 / 9 + U * 1.2f, ry + U * 5.6f, lw * 0.25f, U * 1.6f, U * 0.5f, 0, WHITE, 0.06f);
		}
		cap_text(W - U * 6, H - U * 7.2f, "ASKING YOUTUBE", U * 0.95f, RGB(140, 140, 146), 1, 1);
	} else if (y->state == Y_ERROR) {
		sg_text(&fb, &FT, lx, ry0 + U * 6, y->err[0] ? y->err : "Something went wrong.", U * 2.6f, WHITE, 1, 0.01f);
		sg_text(&fb, &FT, lx, ry0 + U * 11, "Press Enter to try again.", U * 2.0f, RGB(150, 150, 156), 1, 0.01f);
	} else if (!y->mode) {
		int vis = (int)((H - ry0 - U * 10) / rh), cnt = y->nw;
		if (vis < 1) vis = 1;
		if (y->sel >= cnt) y->sel = cnt - 1;
		if (y->sel < y->top) y->top = y->sel;
		if (y->sel >= y->top + vis) y->top = y->sel - vis + 1;
		for (int i = 0; i < vis && y->top + i < cnt; i++) {
			const Web *w = &y->web[y->top + i];
			float ry = ry0 + i * rh + row_in(i), cx = lx + U * 1.6f, cw = lw - U * 4.4f;
			int sel = h->focus == 1 && y->top + i == y->sel;
			if (sel) { ui_card(lx - U * 1.0f, ry, lw - U * 1.6f, rh - U * 0.6f, U * 1.4f, WHITE, 0.14f, 0.35f); sg_rrect(&fb, lx - U * 1.0f, ry, lw - U * 1.6f, rh - U * 0.6f, U * 1.4f, sg_max(1.5f, U * 0.2f), ACCENT, 0.9f); }
			char du[200], df[200]; web_display_url(w->url, du, sizeof du);
			ui_avatar(cx + U * 0.9f, ry + U * 2.4f, U * 0.95f, du);
			fit(du, U * 1.4f, cw - U * 3.4f, df, sizeof df);
			sg_text(&fb, &FT, cx + U * 3.0f, ry + U * 3.0f, df, U * 1.4f, RGB(150, 190, 150), 1, 0.01f);
			char tf[200]; fit(w->title, U * 2.3f, cw, tf, sizeof tf);
			sg_text(&fb, &FT, cx, ry + U * 6.3f, tf, U * 2.3f, WHITE, 1, 0.01f);
			char sn[2][160]; int m = ui_wrap(w->snip, U * 1.5f, cw, 2, sn);
			for (int k = 0; k < m; k++) sg_text(&fb, &FT, cx, ry + U * 9.0f + k * U * 2.0f, sn[k], U * 1.5f, RGB(160, 160, 166), 1, 0.01f);
		}
		ui_scroll(W - U * 4, ry0, vis * rh, y->top, vis, cnt);
	} else {
		int vis = (int)((H - ry0 - U * 10) / rh);
		if (vis < 1) vis = 1;
		if (y->sel >= y->nv) y->sel = y->nv - 1;
		if (y->sel < y->top) y->top = y->sel;
		if (y->sel >= y->top + vis) y->top = y->sel - vis + 1;
		float tw = (rh - U * 2.2f) * 16 / 9, th = rh - U * 2.2f;
		for (int i = 0; i < vis && y->top + i < y->nv; i++) {
			const Video *v = &y->v[y->top + i];
			float ry = ry0 + i * rh + row_in(i);
			int sel = h->focus == 1 && y->top + i == y->sel;
			if (sel) { ui_card(lx - U * 1.0f, ry, lw - U * 1.6f, rh - U * 0.6f, U * 1.4f, WHITE, 0.14f, 0.35f); sg_rrect(&fb, lx - U * 1.0f, ry, lw - U * 1.6f, rh - U * 0.6f, U * 1.4f, sg_max(1.5f, U * 0.2f), ACCENT, 0.9f); }
			float tx0 = lx + U * 0.4f, ty0 = ry + U * 1.1f;
			sg_blob(&fb, tx0 + tw / 2, ty0 + th / 2 + U * 0.6f, tw * 0.58f, th * 0.7f, BLACK, 0.55f);
			thumb_draw(v->id, 0, y->fetch, tx0, ty0, tw, th, U * 0.9f);
			length_badge(tx0 + tw - U * 0.6f, ty0 + th - U * 0.6f, v);
			float cx = tx0 + tw + U * 2.4f, cw = lw - tw - U * 6;
			char ln[2][160]; int n = ui_wrap(v->title, U * 2.3f, cw, 2, ln);
			for (int k = 0; k < n; k++) sg_text(&fb, &FT, cx, ty0 + U * 3.0f + k * U * 3.0f, ln[k], U * 2.3f, WHITE, 1, 0.01f);
			char meta[200]; yt_meta(v, meta, sizeof meta); char mf[200]; fit(meta, U * 1.5f, cw, mf, sizeof mf);
			sg_text(&fb, &FT, cx, ty0 + U * 3.0f + n * U * 3.0f + U * 0.5f, mf, U * 1.5f, RGB(170, 170, 176), 1, 0.01f);
			if (v->desc[0] && n < 2) { char df[220]; fit(v->desc, U * 1.45f, cw, df, sizeof df); sg_text(&fb, &FT, cx, ty0 + U * 3.0f + n * U * 3.0f + U * 3.6f, df, U * 1.45f, RGB(130, 130, 136), 1, 0.01f); }
		}
		ui_scroll(W - U * 4, ry0, vis * rh, y->top, vis, y->nv);
	}
	foot_hints(y->state == Y_RESULTS ? "TYPE  SEARCH      TAB  WEB OR VIDEOS      DOWN UP  PICK      ENTER  OPEN      ESC  HOME" : "TYPE  SEARCH      TAB  WEB OR VIDEOS      ENTER  GO      ESC  HOME");
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
