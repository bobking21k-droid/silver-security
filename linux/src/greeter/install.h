/*
 * Settings > "Install SilverOS next to Windows" (only on the live USB stick).
 *
 * The same tool that `su -c silver-install` runs does the work. The home screen asks the privileged
 * side to run it ('I' = only work out the plan, 'J' = install) and reads what it writes to
 * /run/silver-install.plan and /run/silver-install.log:
 *     PLAN disk=... / PLAN windows=... / PLAN space=... / PLAN warn=... / PLAN ok
 *     [1/5] ... [5/5]   Done.   silver-install: <what went wrong>   EXIT <status>
 * Nothing is written to any disk until you press Enter on the plan page.
 */
#pragma once

#define INS_PLAN "/run/silver-install.plan"
#define INS_LOG  "/run/silver-install.log"
/* state: 0 not started, 1 working out the plan, 2 plan ready, 3 installing, 4 done, 5 problem */
static struct { int state, step; char disk[120], windows[16], space[40], warn[3][100], nwarn, err[300]; double t; } ins;

static int ins_live(void)
{
#ifdef RENDER
	return 0;                                                                       /* (the demo shows the installed system) */
#else
	return access("/etc/silver-installed", F_OK) != 0;
#endif
}

static void ins_open(void)
{
	memset(&ins, 0, sizeof ins);
#ifndef RENDER
	ins.state = 1; ins.t = now(); request('I');
#else
	ins.state = 2; snprintf(ins.disk, sizeof ins.disk, "/dev/nvme0n1 (Samsung SSD) 512.1 GB"); strcpy(ins.windows, "yes"); strcpy(ins.space, "96.0 GB");
#endif
}

#ifndef RENDER
static int ins_read(const char *path, char *buf, size_t n)
{
	FILE *f = fopen(path, "r"); if (!f) return 0;
	size_t r = fread(buf, 1, n - 1, f); buf[r] = 0; fclose(f); return 1;
}
static void ins_poll(void)
{
	if (ins.state == 1 || ins.state == 2) {
		static char b[4096];
		if (!ins_read(INS_PLAN, b, sizeof b)) return;
		ins.nwarn = 0; int ok = 0, exited = 0; ins.err[0] = 0;
		for (char *l = strtok(b, "\n"); l; l = strtok(NULL, "\n")) {
			if (!strncmp(l, "PLAN disk=", 10)) snprintf(ins.disk, sizeof ins.disk, "%s", l + 10);
			else if (!strncmp(l, "PLAN windows=", 13)) snprintf(ins.windows, sizeof ins.windows, "%s", l + 13);
			else if (!strncmp(l, "PLAN space=", 11)) snprintf(ins.space, sizeof ins.space, "%s", l + 11);
			else if (!strncmp(l, "PLAN warn=", 10) && ins.nwarn < 3) snprintf(ins.warn[ins.nwarn++], sizeof ins.warn[0], "%s", l + 10);
			else if (!strcmp(l, "PLAN ok")) ok = 1;
			else if (!strncmp(l, "EXIT ", 5)) exited = 1;
			else if (!strncmp(l, "silver-install: ", 16)) snprintf(ins.err, sizeof ins.err, "%s", l + 16);
			else if (ins.err[0] && l[0] == ' ') { size_t m = strlen(ins.err); snprintf(ins.err + m, sizeof ins.err - m, " %s", l + 2); }
		}
		if (ok) ins.state = 2;
		else if (exited) ins.state = 5;
		return;
	}
	if (ins.state == 3) {
		static char b[8192];
		if (!ins_read(INS_LOG, b, sizeof b)) return;
		int done = 0, exited = 0; ins.err[0] = 0;
		for (char *l = strtok(b, "\n"); l; l = strtok(NULL, "\n")) {
			if (l[0] == ' ' && l[1] == ' ' && l[2] == '[' && l[3] >= '1' && l[3] <= '5') ins.step = l[3] - '0';
			else if (l[0] == '[' && l[1] >= '1' && l[1] <= '5') ins.step = l[1] - '0';
			else if (strstr(l, "Done.")) done = 1;
			else if (!strncmp(l, "EXIT ", 5)) exited = atoi(l + 5) ? 2 : 1;
			else if (!strncmp(l, "silver-install: ", 16)) snprintf(ins.err, sizeof ins.err, "%s", l + 16);
		}
		if (done || exited == 1) ins.state = 4; else if (exited == 2) ins.state = 5;
	}
}
#else
static void ins_poll(void) {}
#endif

/* returns 1 to leave the page */
static int ins_key(int k)
{
	if (ins.state == 3) return 0;                                                   /* busy: nothing to press */
	if (k == K_ESC || k == K_BACK || k == K_LEFT) return 1;
	if (k == K_ENTER) {
		if (ins.state == 2 && !strcmp(ins.windows, "yes")) { ins.state = 3; ins.step = 0; ins.t = now(); request('J'); }
		else if (ins.state == 2) { ins.state = 3; ins.step = 0; ins.t = now(); request('J'); }
		else if (ins.state == 4) request('R');
	}
	return 0;
}

static void draw_install(Home *h, float t)
{
	(void)h;
	ins_poll();
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	title_block("INSTALL", "SILVEROS NEXT TO WINDOWS", I_PC);
	float lx = U * 6, lw = W - U * 12, y = U * 22, ph = U * 44;
	ui_frost(lx - U * 1.2f, y - U * 1.2f, lw + U * 2.4f, ph, U * 2.2f, 0.55f);
	const char *head = "", *l1 = "", *l2 = "", *hint = "ESC  SETTINGS"; char buf[320];
	uint32_t hc = WHITE;
	switch (ins.state) {
	case 1: head = "Looking at your disks ..."; l1 = "Nothing is changed yet."; break;
	case 2: head = "Ready to install"; hint = "ENTER  INSTALL      ESC  CANCEL"; break;
	case 3: head = "Installing ... do not turn the computer off"; hint = "PLEASE WAIT"; break;
	case 4: head = "Done"; hc = RGB(90, 225, 140); l1 = "Remove the USB stick, then press Enter to restart."; l2 = "You'll see a screen to pick SilverOS or Windows."; hint = "ENTER  RESTART"; break;
	default: head = "Couldn't install"; hc = RGB(255, 120, 110); snprintf(buf, sizeof buf, "%s", ins.err[0] ? ins.err : "Something went wrong."); l1 = buf; break;
	}
	sg_text(&fb, &FT, lx + U * 3, y + U * 4.5f, head, U * 3.4f, hc, 1, 0.01f);
	float ty = y + U * 9.5f, st = U * 3.4f;
	char line[360], fl[360];
	if (ins.state == 2 || ins.state == 3) {
		snprintf(line, sizeof line, "Disk:  %s", ins.disk); fit(line, U * 2.0f, lw - U * 6, fl, sizeof fl); sg_text(&fb, &FT, lx + U * 3, ty, fl, U * 2.0f, WHITE, 1, 0.01f); ty += st;
		snprintf(line, sizeof line, "New partition:  SilverOS, %s of free space (Windows is not touched)", ins.space); fit(line, U * 2.0f, lw - U * 6, fl, sizeof fl); sg_text(&fb, &FT, lx + U * 3, ty, fl, U * 2.0f, WHITE, 1, 0.01f); ty += st;
		snprintf(line, sizeof line, "Windows found:  %s     Boot screen:  SilverOS | Windows", ins.windows); sg_text(&fb, &FT, lx + U * 3, ty, line, U * 2.0f, WHITE, 1, 0.01f); ty += st;
		for (int i = 0; i < ins.nwarn; i++) { snprintf(line, sizeof line, "! %s", ins.warn[i]); fit(line, U * 1.8f, lw - U * 6, fl, sizeof fl); sg_text(&fb, &FT, lx + U * 3, ty, fl, U * 1.8f, RGB(255, 200, 90), 1, 0.01f); ty += st * 0.85f; }
	}
	if (ins.state == 3) {                                                           /* progress */
		float bx = lx + U * 3, bw = lw - U * 6, by = y + ph - U * 7.5f, bh = U * 2.4f;
		float f = ins.step / 5.0f + 0.2f * (0.5f + 0.5f * sg_sin(t * 3)) / 5.0f; if (f > 1) f = 1;
		sg_rrect(&fb, bx, by, bw, bh, bh / 2, 0, WHITE, 0.15f);
		sg_rrect(&fb, bx, by, sg_max(bh, bw * f), bh, bh / 2, 0, RED, 1);
		snprintf(line, sizeof line, "step %d of 5", ins.step ? ins.step : 1);
		sg_text(&fb, &FC, bx, by - U * 1.2f, line, U * 1.2f, RGB(170, 170, 178), 1, 0.2f);
	} else {
		if (l1[0]) { fit(l1, U * 2.0f, lw - U * 6, fl, sizeof fl); sg_text(&fb, &FT, lx + U * 3, ty, fl, U * 2.0f, RGB(215, 215, 220), 1, 0.01f); ty += st; }
		if (l2[0]) sg_text(&fb, &FT, lx + U * 3, ty, l2, U * 2.0f, RGB(215, 215, 220), 1, 0.01f);
	}
	foot_hints(hint);
}
