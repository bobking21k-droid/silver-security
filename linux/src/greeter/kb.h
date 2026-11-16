/*
 * Settings > Keyboard: the layout.
 *
 * The layouts are small binary keymaps in /usr/share/keymaps (made at build time from the kbd
 * project's data, see scripts/build-keymaps.sh); layouts.txt there lists them. The privileged side
 * of the home screen loads one into the kernel with BusyBox's loadkmap when asked ("K<id>") and
 * at boot, from your choice in ~/.config/silver/keymap. The kernel keeps one keymap for all
 * consoles, so the text consoles follow too.
 *
 * Letters that this layout types as accents (a, o, e with marks...) are shown in the console but
 * not yet typed into the home screen's own boxes, which take plain ASCII.
 */
#pragma once

#define MAXKB 40
static void request_name(char c, const char *name);
static struct { char id[MAXKB][20], label[MAXKB][48]; int n, cur, sel, top; char test[40]; char note[96]; float note_t; } kb;

static void kb_file(char *out, size_t n) { snprintf(out, n, "%s/.config/silver/keymap", homedir); }

static void kb_load(void)
{
	if (kb.n) return;
	FILE *f = fopen("/usr/share/keymaps/layouts.txt", "r");
	char l[120];
	while (f && fgets(l, sizeof l, f) && kb.n < MAXKB) {
		char *bar = strchr(l, '|'); if (!bar) continue;
		*bar = 0; l[strcspn(bar + 1, "\r\n") + (size_t)(bar + 1 - l)] = 0;
		snprintf(kb.id[kb.n], sizeof kb.id[0], "%s", l); snprintf(kb.label[kb.n], sizeof kb.label[0], "%s", bar + 1);
		kb.n++;
	}
	if (f) fclose(f);
#ifdef RENDER
	if (!kb.n) { static const char *demo[][2] = {{"us", "English (US)"}, {"uk", "English (UK)"}, {"us-intl", "English (US, international)"}, {"dvorak", "Dvorak"}, {"colemak", "Colemak"}, {"de", "German"}, {"fr", "French (AZERTY)"}, {"es", "Spanish"}, {"it", "Italian"}, {"pt", "Portuguese"}, {"br", "Brazilian (ABNT2)"}, {"pl", "Polish"}};
		for (unsigned i = 0; i < sizeof demo / sizeof demo[0]; i++) { snprintf(kb.id[kb.n], sizeof kb.id[0], "%s", demo[i][0]); snprintf(kb.label[kb.n], sizeof kb.label[0], "%s", demo[i][1]); kb.n++; } }
#endif
	char id[20] = "us";
#ifndef RENDER
	char p[300]; kb_file(p, sizeof p);
	f = fopen(p, "r"); if (f) { if (fgets(id, sizeof id, f)) id[strcspn(id, "\r\n")] = 0; fclose(f); }
#endif
	kb.cur = 0;
	for (int i = 0; i < kb.n; i++) if (!strcmp(kb.id[i], id)) kb.cur = i;
}

static void kb_open(void) { kb_load(); kb.sel = kb.cur; kb.top = 0; kb.test[0] = 0; kb.note[0] = 0; }

static void kb_choose(int i)
{
	kb.cur = i;
#ifndef RENDER
	char d[300], p[300];
	snprintf(d, sizeof d, "%s/.config", homedir); mkdir(d, 0755);
	snprintf(d, sizeof d, "%s/.config/silver", homedir); mkdir(d, 0755);
	kb_file(p, sizeof p);
	FILE *f = fopen(p, "w"); if (f) { fprintf(f, "%s\n", kb.id[i]); fclose(f); }
#endif
	request_name('K', kb.id[i]);
	snprintf(kb.note, sizeof kb.note, "Layout: %.60s", kb.label[i]); kb.note_t = ui_t;
	kb.test[0] = 0;
}

static int kb_key(int k)
{
	if (k == K_ESC || k == K_LEFT) return 1;
	if (k == K_UP && kb.sel > 0) kb.sel--;
	else if (k == K_DOWN && kb.sel + 1 < kb.n) kb.sel++;
	else if (k == K_ENTER) kb_choose(kb.sel);
	else if (k == K_BACK) { size_t n = strlen(kb.test); if (n) kb.test[n - 1] = 0; }
	else if (k >= 32 && k < 127 && strlen(kb.test) + 1 < sizeof kb.test) { size_t n = strlen(kb.test); kb.test[n] = (char)k; kb.test[n + 1] = 0; }
	return 0;
}

static void draw_keyboard(Home *h, float t)
{
	(void)h;
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	title_block("KEYBOARD", "LAYOUT  TRY IT BELOW", I_TERM);
	float lx = U * 6, lw = W - U * 12, y = U * 22;
	ui_frost(lx - U * 1.2f, y - U * 1.2f, lw + U * 2.4f, U * 9.4f, U * 2.2f, 0.55f);
	sg_text(&fb, &FT, lx + U * 3, y + U * 3.4f, kb.n ? kb.label[kb.cur] : "No layouts found", U * 3.0f, WHITE, 1, 0.01f);
	sg_text(&fb, &FT, lx + U * 3, y + U * 6.6f, "Type here to try the layout:", U * 1.7f, RGB(170, 170, 178), 1, 0.01f);
	float bx = lx + U * 31, bw = lw - U * 34, by = y + U * 4.6f, bh = U * 3.4f;
	sg_rrect(&fb, bx, by, bw, bh, bh / 2, 0, WHITE, 0.95f);
	float tw = sg_text_w(&FT, kb.test, U * 2.2f, 0.04f);
	sg_text(&fb, &FT, bx + U * 1.6f, by + bh / 2 + U * 0.75f, kb.test, U * 2.2f, INK, 1, 0.04f);
	if (fmodf(t, 1.0f) < 0.6f) sg_rect(&fb, bx + U * 1.7f + tw, by + bh * 0.2f, sg_max(1, U * 0.16f), bh * 0.6f, RED, 1);
	if (kb.note[0] && ui_t - kb.note_t < 4) { float na = sg_sat((4 - (ui_t - kb.note_t)) / 0.5f), nw = sg_text_w(&FT, kb.note, U * 1.8f, 0.01f); sg_text(&fb, &FT, lx + lw - nw - U * 1.5f, y + U * 2.2f, kb.note, U * 1.8f, RGB(90, 225, 140), na, 0.01f); }
	float ly = y + U * 12, rh = U * 4.6f; int vis = (int)((H - U * 9 - ly) / (rh + U * 0.4f));
	if (vis < 1) vis = 1;
	if (kb.sel < kb.top) kb.top = kb.sel;
	if (kb.sel >= kb.top + vis) kb.top = kb.sel - vis + 1;
	int shown = kb.n - kb.top < vis ? kb.n - kb.top : vis;
	ui_frost(lx - U * 1.2f, ly - U * 0.6f, lw + U * 2.4f, shown * (rh + U * 0.4f) + U * 0.8f, U * 2.2f, 0.55f);
	for (int r = 0; r < shown; r++) {
		int i = kb.top + r; float ry = ly + r * (rh + U * 0.4f); int sel = i == kb.sel, cur = i == kb.cur;
		if (sel) { sg_rrect(&fb, lx, ry, lw, rh, U * 1.2f, 0, WHITE, 0.95f); sg_rrect(&fb, lx + U * 0.7f, ry + rh * 0.25f, U * 0.45f, rh * 0.5f, U * 0.22f, 0, RED, 1); }
		else if (r) sg_rect(&fb, lx + U * 3, ry - U * 0.2f, lw - U * 6, sg_max(1, U * 0.08f), WHITE, 0.08f);
		sg_text(&fb, &FT, lx + U * 3, ry + rh / 2 + U * 0.8f, kb.label[i], U * 2.2f, sel ? INK : WHITE, 1, 0.01f);
		if (cur) sg_text(&fb, &FC, lx + lw - U * 3 - sg_text_w(&FC, "IN USE", U * 1.1f, 0.2f), ry + rh / 2 + U * 0.5f, "IN USE", U * 1.1f, RGB(40, 190, 100), 1, 0.2f);
	}
	foot_hints("UP DOWN  MOVE      ENTER  USE THIS LAYOUT      TYPE  TRY IT      ESC  SETTINGS");
}
