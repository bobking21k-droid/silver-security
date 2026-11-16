/*
 * Files: what you can do with a file besides look at it.
 *
 *   C copy   X cut   V paste here   D delete (press twice)   R rename   N new folder
 *   Enter on a picture opens it full screen (Left / Right: the next picture in the folder, Esc: close)
 *
 * Copying and deleting run in the background (BusyBox cp, mv, rm) so the screen never waits, and
 * the list refreshes when they finish. Nothing here can do more than you could do in the terminal:
 * the home screen runs as you, not as the administrator.
 */
#pragma once
#include <sys/stat.h>

static struct {
	char clip[320]; int cut;                         /* what C or X picked up */
	Job job; int jobact; char jobmsg[120];
	char msg[120]; double msg_t;
	int mode;                                        /* 0 none, 1 new folder name, 2 rename, 3 delete asked */
	char buf[96];
	char del[320];                                   /* what D is asking about */
	int view; uint8_t *px; int vw, vh; char vname[96];
} fo;

static void fo_say(const char *m) { snprintf(fo.msg, sizeof fo.msg, "%s", m); fo.msg_t = now(); }
static void fo_path(const Home *h, char *out, size_t n)
{
	if (!nent) { out[0] = 0; return; }
	snprintf(out, n, "%s%s%s", cwd, strcmp(cwd, "/") ? "/" : "", ents[h->row].name);
}
static int is_image(const char *name)
{
	const char *d = strrchr(name, '.'); if (!d) return 0;
	static const char *ext[] = {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tga"};
	for (unsigned i = 0; i < sizeof ext / sizeof ext[0]; i++) if (!strcasecmp(d, ext[i])) return 1;
	return 0;
}
static int path_exists(const char *p) { struct stat st; return lstat(p, &st) == 0; }
/* never touch the very top: /, /home, your home folder itself */
static int path_safe(const char *p)
{
	if (!strcmp(p, "/") || !strcmp(p, "/home") || !strcmp(p, homedir)) return 0;
	return strchr(p + 1, '/') != NULL;
}

static void fo_run(char *const argv[], const char *doing)
{
#ifndef RENDER
	if (fo.jobact) { fo_say("Still busy with the last one"); return; }
	job_start(&fo.job, argv, 4096);
	if (fo.job.active) { fo.jobact = 1; snprintf(fo.jobmsg, sizeof fo.jobmsg, "%s", doing); fo_say(doing); }
	else fo_say("Couldn't start that");
#else
	(void)argv; fo_say(doing);
#endif
}
static void fo_poll(Home *h)
{
#ifndef RENDER
	if (!fo.jobact) return;
	job_poll(&fo.job);
	if (!fo.job.done) return;
	char m[140]; snprintf(m, sizeof m, fo.job.ok ? "Done" : "Didn't work (no permission, or no room?)");
	job_free(&fo.job); fo.jobact = 0; fo_say(m); snd(fo.job.ok ? 'r' : 'e');
	fs_list(cwd); if (h->row >= nent) h->row = nent ? nent - 1 : 0;
	files_preview(h);
#else
	(void)h;
#endif
}

static void fo_view_close(void) { free(fo.px); fo.px = NULL; fo.view = 0; }
static void fo_view_open(Home *h)
{
	char p[360]; fo_path(h, p, sizeof p);
#ifdef RENDER
	(void)p; fo_say("(the demo has no pictures)"); return;
#else
	FILE *f = fopen(p, "rb");
	if (!f) { fo_say("Can't open that picture"); return; }
	fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
	if (sz <= 0 || sz > 80L << 20) { fclose(f); fo_say("That picture is too big"); return; }
	unsigned char *file = malloc((size_t)sz);
	if (!file || fread(file, 1, (size_t)sz, f) != (size_t)sz) { free(file); fclose(f); fo_say("Can't read that picture"); return; }
	fclose(f);
	int w, hh, n; uint8_t *px = stbi_load_from_memory(file, (int)sz, &w, &hh, &n, 4);
	free(file);
	if (!px) { fo_say("Can't open that picture"); return; }
	if ((long)w * hh > 40000000L) { stbi_image_free(px); fo_say("That picture is too big"); return; }
	fo_view_close(); fo.px = px; fo.vw = w; fo.vh = hh; fo.view = 1;
	snprintf(fo.vname, sizeof fo.vname, "%s", ents[h->row].name);
#endif
}

/* returns 1 if the key was used here */
static int fo_key(Home *h, int k)
{
	if (fo.view) {                                                    /* the picture viewer */
		if (k == K_ESC || k == K_ENTER || k == K_BACK) fo_view_close();
		else if (k == K_LEFT || k == K_RIGHT) {
			int step = k == K_RIGHT ? 1 : -1;
			for (int i = h->row + step; i >= 0 && i < nent; i += step)
				if (!ents[i].dir && is_image(ents[i].name)) { h->row = i; fo_view_open(h); break; }
		}
		return 1;
	}
	if (fo.mode == 1 || fo.mode == 2) {                               /* typing a name */
		size_t n = strlen(fo.buf);
		if (k == K_ESC) fo.mode = 0;
		else if (k == K_BACK && n) fo.buf[n - 1] = 0;
		else if (k >= 32 && k < 127 && k != '/' && n + 1 < sizeof fo.buf) { fo.buf[n] = (char)k; fo.buf[n + 1] = 0; }
		else if (k == K_ENTER && n) {
			char to[420]; snprintf(to, sizeof to, "%s%s%s", cwd, strcmp(cwd, "/") ? "/" : "", fo.buf);
			if (path_exists(to)) fo_say("There is already one with that name");
			else if (fo.mode == 1) {
#ifndef RENDER
				if (mkdir(to, 0755) == 0) { fo_say("Folder made"); fs_list(cwd); } else fo_say("Couldn't make the folder");
#else
				fo_say("Folder made");
#endif
				fo.mode = 0;
			} else {
				char from[360]; fo_path(h, from, sizeof from);
#ifndef RENDER
				if (rename(from, to) == 0) { fo_say("Renamed"); fs_list(cwd); for (int i = 0; i < nent; i++) if (!strcmp(ents[i].name, fo.buf)) h->row = i; files_preview(h); }
				else fo_say("Couldn't rename it");
#else
				fo_say("Renamed");
#endif
				fo.mode = 0;
			}
		}
		return 1;
	}
	if (fo.mode == 3) {                                               /* delete: the second press */
		if (k == 'd' || k == 'D' || k == K_ENTER) {
			char *argv[] = {"/bin/rm", "-rf", fo.del, 0};
			fo_run(argv, "Deleting ...");
		} else fo_say("Kept it");
		fo.mode = 0;
		return 1;
	}
	char p[360];
	switch (k) {
	case 'c': case 'C': case 'x': case 'X':
		fo_path(h, p, sizeof p); if (!p[0]) return 1;
		snprintf(fo.clip, sizeof fo.clip, "%s", p); fo.cut = (k == 'x' || k == 'X');
		fo_say(fo.cut ? "Cut. Go to a folder and press V" : "Copied. Go to a folder and press V");
		return 1;
	case 'v': case 'V': {
		if (!fo.clip[0]) { fo_say("Nothing to paste: press C or X on a file first"); return 1; }
		const char *base = strrchr(fo.clip, '/'); base = base ? base + 1 : fo.clip;
		char to[420]; snprintf(to, sizeof to, "%s%s%s", cwd, strcmp(cwd, "/") ? "/" : "", base);
		if (path_exists(to)) { fo_say("There is already one with that name here"); return 1; }
		if (!strncmp(cwd, fo.clip, strlen(fo.clip)) && (cwd[strlen(fo.clip)] == '/' || !cwd[strlen(fo.clip)])) { fo_say("Can't put a folder inside itself"); return 1; }
		char *argv_cp[] = {"/bin/cp", "-a", fo.clip, cwd, 0}, *argv_mv[] = {"/bin/mv", fo.clip, cwd, 0};
		fo_run(fo.cut ? argv_mv : argv_cp, fo.cut ? "Moving ..." : "Copying ...");
		if (fo.cut) fo.clip[0] = 0;
		return 1; }
	case 'd': case 'D':
		fo_path(h, p, sizeof p); if (!p[0]) return 1;
		if (!path_safe(p)) { fo_say("That one is protected"); return 1; }
		snprintf(fo.del, sizeof fo.del, "%s", p); fo.mode = 3;
		return 1;
	case 'r': case 'R':
		if (!nent) return 1;
		fo.mode = 2; snprintf(fo.buf, sizeof fo.buf, "%s", ents[h->row].name);
		return 1;
	case 'n': case 'N':
		fo.mode = 1; fo.buf[0] = 0;
		return 1;
	case K_ENTER: case K_RIGHT:
		if (nent && !ents[h->row].dir && is_image(ents[h->row].name)) { fo_view_open(h); return 1; }
		return 0;
	}
	return 0;
}

/* the prompt and the message line, drawn over the Files page */
static void fo_draw_overlay(Home *h, float t)
{
	(void)h;
	float lx = U * 6, lw = W * 0.54f;
	if (fo.mode == 1 || fo.mode == 2) {
		float by = H - U * 17, bh = U * 6.5f;
		ui_frost(lx - U * 1.2f, by - U * 1.5f, lw + U * 2.4f, bh + U * 3, U * 2.2f, 0.82f);
		cap_text(lx + U * 1, by + U * 0.4f, fo.mode == 1 ? "NEW FOLDER" : "RENAME TO", U * 1.0f, RGB(110, 170, 255), 1, 0);
		sg_rrect(&fb, lx, by + U * 1.4f, lw, U * 4, U * 2, 0, WHITE, 0.96f);
		float tw = sg_text_w(&FT, fo.buf, U * 2.0f, 0.03f);
		sg_text(&fb, &FT, lx + U * 1.6f, by + U * 4.2f, fo.buf, U * 2.0f, INK, 1, 0.03f);
		if (fmodf(t, 1.0f) < 0.6f) sg_rect(&fb, lx + U * 1.7f + tw, by + U * 2.2f, sg_max(1, U * 0.16f), U * 2.4f, RED, 1);
	} else if (fo.mode == 3) {
		float by = H - U * 15;
		ui_frost(lx - U * 1.2f, by - U * 1.5f, lw + U * 2.4f, U * 7, U * 2.2f, 0.82f);
		const char *nm = strrchr(fo.del, '/'); nm = nm ? nm + 1 : fo.del;
		char m[200]; snprintf(m, sizeof m, "Delete \"%s\"? Press D again.", nm);
		char f1[200]; fit(m, U * 2.0f, lw - U * 2, f1, sizeof f1);
		sg_text(&fb, &FT, lx + U * 1, by + U * 2.6f, f1, U * 2.0f, RGB(255, 140, 130), 1, 0.01f);
		cap_text(lx + U * 1, by + U * 4.6f, "ANY OTHER KEY KEEPS IT", U * 0.9f, RGB(150, 150, 156), 1, 0);
	} else if (fo.msg[0] && now() - fo.msg_t < 4) {
		float a = sg_sat((4 - (float)(now() - fo.msg_t)) / 0.6f);
		float w = sg_text_w(&FT, fo.msg, U * 1.9f, 0.01f) + U * 5, x = lx, y = H - U * 14;
		sg_rrect(&fb, x, y, w, U * 4.2f, U * 2.1f, 0, RGB(30, 30, 36), 0.85f * a);
		sg_text(&fb, &FT, x + U * 2.5f, y + U * 2.9f, fo.msg, U * 1.9f, WHITE, a, 0.01f);
	}
}

/* the picture, as large as fits, with its name */
static void fo_view_draw(void)
{
	sg_clear(&fb, BLACK);
	if (!fo.px) return;
	float sc = sg_min((W - U * 6) / fo.vw, (H - U * 14) / fo.vh); if (sc > 4) sc = 4;
	int dw = (int)(fo.vw * sc), dh = (int)(fo.vh * sc), x0 = (W - dw) / 2, y0 = (int)(U * 3);
	for (int y = 0; y < dh; y++) {
		int py = y0 + y; if (py < 0 || py >= H) continue;
		int sy = (int)(y / sc); if (sy >= fo.vh) sy = fo.vh - 1;
		for (int x = 0; x < dw; x++) {
			int px = x0 + x; if (px < 0 || px >= W) continue;
			int sx = (int)(x / sc); if (sx >= fo.vw) sx = fo.vw - 1;
			const uint8_t *q = fo.px + ((size_t)sy * fo.vw + sx) * 4;
			float a = q[3] / 255.0f;
			fb.px[py * W + px] = RGB((int)(q[0] * a), (int)(q[1] * a), (int)(q[2] * a));
		}
	}
	char f1[120]; fit(fo.vname, U * 2.0f, W - U * 20, f1, sizeof f1);
	sg_text_c(&fb, &FT, W / 2.0f, H - U * 9, f1, U * 2.0f, WHITE, 1, 0.01f);
	foot_hints("LEFT RIGHT  other pictures      ESC  close");
}
