/*
 * The computer's own buttons: the power button and the laptop lid.
 *
 * The kernel turns the ACPI power button and lid switch into input devices ("Power Button",
 * "Lid Switch"). We read their events directly:
 *
 *   power button   a panel asks "Shut down?" (Enter confirms, Esc cancels); pressing the button
 *                  again confirms. During a Steam session (the home screen isn't running) two
 *                  presses within three seconds shut the computer down.
 *   lid closed     the screen goes dark; it comes back when the lid opens.
 *
 * (Sleep when the lid closes is not done: resuming works differently on every laptop.)
 */
#pragma once
#include <linux/input.h>

#ifdef RENDER
static int  pwr_show;
static void buttons_open(void) {}
static int  buttons_poll(Home *h) { (void)h; return 0; }
static void pwr_draw(float t) { (void)t; }
#else
#define MAXBTN 6
static struct { int fd[MAXBTN], n; int lid_closed; } bt_in;
static int pwr_show; static double pwr_t;

/* every input device named "... Power Button", "... Sleep Button" or "Lid Switch" */
static void buttons_open(void)
{
	for (int i = 0; i < MAXBTN; i++) bt_in.fd[i] = -1;
	bt_in.n = 0;
	DIR *d = opendir("/sys/class/input"); struct dirent *e;
	while (d && (e = readdir(d)) && bt_in.n < MAXBTN) {
		if (strncmp(e->d_name, "input", 5)) continue;
		char p[300], nm[80] = "";
		snprintf(p, sizeof p, "/sys/class/input/%s/name", e->d_name);
		FILE *f = fopen(p, "r"); if (f) { if (!fgets(nm, sizeof nm, f)) nm[0] = 0; fclose(f); }
		if (!strstr(nm, "Power Button") && !strstr(nm, "Sleep Button") && !strstr(nm, "Lid Switch")) continue;
		char dir[300]; snprintf(dir, sizeof dir, "/sys/class/input/%s", e->d_name);
		DIR *dd = opendir(dir); struct dirent *ee;
		while (dd && (ee = readdir(dd))) {
			if (strncmp(ee->d_name, "event", 5)) continue;
			snprintf(p, sizeof p, "/dev/input/%s", ee->d_name);
			int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
			if (fd >= 0) bt_in.fd[bt_in.n++] = fd;
			break;
		}
		if (dd) closedir(dd);
	}
	if (d) closedir(d);
}
static void buttons_close(void) { for (int i = 0; i < bt_in.n; i++) close(bt_in.fd[i]); bt_in.n = 0; }

/* read what happened: returns the number of power-button presses; updates the lid state */
static int buttons_read(int *lid_changed)
{
	int presses = 0; struct input_event ev;
	*lid_changed = 0;
	for (int i = 0; i < bt_in.n; i++)
		while (read(bt_in.fd[i], &ev, sizeof ev) == (ssize_t)sizeof ev) {
			if (ev.type == EV_KEY && ev.value == 1 && (ev.code == KEY_POWER || ev.code == KEY_SLEEP)) presses++;
			else if (ev.type == EV_SW && ev.code == SW_LID) { int c = ev.value != 0; if (c != bt_in.lid_closed) { bt_in.lid_closed = c; *lid_changed = 1; } }
		}
	return presses;
}

static void request(char c);
static int buttons_poll(Home *h)
{
	(void)h;
	int lid; int p = buttons_read(&lid);
	if (lid) request(bt_in.lid_closed ? 'X' : 'x');                   /* screen off / on */
	if (pwr_show && now() - pwr_t > 20) pwr_show = 0;                 /* forgotten: go away */
	for (; p > 0; p--) {
		if (pwr_show) { request('P'); return 1; }                      /* pressed again: shut down */
		pwr_show = 1; pwr_t = now(); snd('o');
	}
	return 0;
}

/* the "Shut down?" panel, over whatever is on screen */
static void pwr_draw(float t)
{
	if (!pwr_show) return;
	sg_rect(&fb, 0, 0, W, H, BLACK, 0.6f);
	float pw = W * 0.46f, ph = U * 27, px = (W - pw) / 2, py = (H - ph) / 2;
	ui_frost(px, py, pw, ph, U * 2.6f, 0.8f);
	icon(I_POWER, px + U * 8, py + ph * 0.5f, U * 4.2f, RGB(255, 120, 110), 1);
	sg_text(&fb, &FT, px + U * 15, py + U * 10.5f, "Shut down?", U * 4.4f, WHITE, 1, 0.01f);
	sg_text(&fb, &FT, px + U * 15, py + U * 15.5f, "Enter shuts the computer down.", U * 1.9f, RGB(215, 215, 220), 1, 0.01f);
	sg_text(&fb, &FT, px + U * 15, py + U * 18.7f, "Esc keeps it on. The power button again shuts down now.", U * 1.6f, RGB(160, 160, 168), 1, 0.01f);
	float a = 0.6f + 0.4f * sg_sin(t * 4);
	sg_rrect(&fb, px + U * 15, py + ph - U * 7, U * 20, U * 4.6f, U * 2.3f, 0, RGB(220, 40, 40), a);
	sg_text(&fb, &FC, px + U * 25 - sg_text_w(&FC, "SHUT DOWN", U * 1.05f, 0.2f) / 2, py + ph - U * 4.1f, "SHUT DOWN", U * 1.05f, WHITE, 1, 0.2f);
	foot_hints("ENTER  shut down      ESC  cancel");
}
#endif
