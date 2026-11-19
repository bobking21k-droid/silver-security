/*
 * Settings > Time: the time zone.
 *
 * The clock itself is set from the internet at boot (rc.boot runs ntpd once when the network is
 * up) and the computer's hardware clock is left alone, because Windows keeps it in local time and
 * would be wrong afterwards. The zone is a POSIX rule such as "EST5EDT,M3.2.0,M11.1.0" (the C
 * library understands these directly, so no zone database is needed). Your choice lives in
 * ~/.config/silver/tz; /etc/profile and the home screen read it.
 *
 * The rules are today's; countries do change them now and then.
 */
#pragma once
#include <time.h>

typedef struct { const char *name, *tz; } Zone;
static const Zone ZONES[] = {
	{"UTC", "UTC0"},
	{"Honolulu", "HST10"},
	{"Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"},
	{"Los Angeles, Vancouver (Pacific)", "PST8PDT,M3.2.0,M11.1.0"},
	{"Denver, Calgary (Mountain)", "MST7MDT,M3.2.0,M11.1.0"},
	{"Phoenix (Arizona)", "MST7"},
	{"Chicago, Winnipeg (Central)", "CST6CDT,M3.2.0,M11.1.0"},
	{"New York, Toronto (Eastern)", "EST5EDT,M3.2.0,M11.1.0"},
	{"Halifax (Atlantic)", "AST4ADT,M3.2.0,M11.1.0"},
	{"St. John's (Newfoundland)", "NST3:30NDT,M3.2.0,M11.1.0"},
	{"Mexico City", "CST6"},
	{"Bogota, Lima, Quito", "<-05>5"},
	{"Santiago", "<-04>4<-03>,M9.1.6/24,M4.1.6/24"},
	{"Caracas, La Paz", "<-04>4"},
	{"Buenos Aires, Sao Paulo", "<-03>3"},
	{"Reykjavik", "GMT0"},
	{"London, Dublin", "GMT0BST,M3.5.0/1,M10.5.0"},
	{"Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
	{"Paris, Berlin, Madrid, Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
	{"Athens, Helsinki, Kyiv", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
	{"Istanbul, Minsk", "<+03>-3"},
	{"Moscow", "MSK-3"},
	{"Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24"},
	{"Lagos, Kinshasa", "WAT-1"},
	{"Johannesburg", "SAST-2"},
	{"Nairobi, Addis Ababa", "EAT-3"},
	{"Jerusalem", "IST-2IDT,M3.4.4/26,M10.5.0"},
	{"Riyadh, Baghdad, Kuwait", "<+03>-3"},
	{"Tehran", "<+0330>-3:30"},
	{"Dubai, Muscat", "<+04>-4"},
	{"Karachi", "PKT-5"},
	{"Delhi, Mumbai, Kolkata", "IST-5:30"},
	{"Kathmandu", "<+0545>-5:45"},
	{"Dhaka", "<+06>-6"},
	{"Bangkok, Jakarta, Hanoi", "<+07>-7"},
	{"Singapore, Kuala Lumpur", "<+08>-8"},
	{"Beijing, Shanghai, Taipei", "CST-8"},
	{"Hong Kong", "HKT-8"},
	{"Manila", "PST-8"},
	{"Perth", "AWST-8"},
	{"Seoul", "KST-9"},
	{"Tokyo, Osaka", "JST-9"},
	{"Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
	{"Darwin", "ACST-9:30"},
	{"Brisbane", "AEST-10"},
	{"Sydney, Melbourne, Canberra", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
	{"Auckland, Wellington", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
	{"Fiji", "<+12>-12"},
	{"Samoa", "<+13>-13"},
};
#define NZONES ((int)(sizeof ZONES / sizeof ZONES[0]))

static struct { int cur, sel, top; char note[96]; float note_t; } tzs = {.cur = -1};

static int tz_index(const char *tz)
{
	for (int i = 0; i < NZONES; i++) if (!strcmp(ZONES[i].tz, tz)) return i;
	return -1;
}
static void tz_file(char *out, size_t n) { snprintf(out, n, "%s/.config/silver/tz", homedir); }

/* at start: your saved zone, if any */
static void tz_load(void)
{
#ifndef RENDER
	char p[300], tz[96] = "";
	tz_file(p, sizeof p);
	FILE *f = fopen(p, "r");
	if (f) { if (fgets(tz, sizeof tz, f)) tz[strcspn(tz, "\r\n")] = 0; fclose(f); }
	if (tz[0] && tz_index(tz) >= 0) { setenv("TZ", tz, 1); tzset(); }
#endif
	const char *e = getenv("TZ");
	tzs.cur = e ? tz_index(e) : -1;
	if (tzs.cur < 0) tzs.cur = 0;
}

static void tz_choose(int i)
{
	tzs.cur = i;
	setenv("TZ", ZONES[i].tz, 1); tzset();
#ifndef RENDER
	char d[300], p[300];
	snprintf(d, sizeof d, "%s/.config", homedir); mkdir(d, 0755);
	snprintf(d, sizeof d, "%s/.config/silver", homedir); mkdir(d, 0755);
	tz_file(p, sizeof p);
	FILE *f = fopen(p, "w");
	if (f) { fprintf(f, "%s\n", ZONES[i].tz); fclose(f); }
#endif
	snprintf(tzs.note, sizeof tzs.note, "Time zone: %.60s", ZONES[i].name); tzs.note_t = ui_t;
}

/* the time in zone i, such as "14:32  Sun" */
static void tz_time_in(int i, char *out, size_t n)
{
	char *old = getenv("TZ"); char keep[96] = ""; int had = old != NULL;
	if (had) snprintf(keep, sizeof keep, "%s", old);
	setenv("TZ", ZONES[i].tz, 1); tzset();
#ifdef RENDER
	time_t t = 1763300000 + (time_t)(ui_t);                 /* the demo's pretend clock */
#else
	time_t t = time(NULL);
#endif
	struct tm tm; localtime_r(&t, &tm);
	strftime(out, n, "%H:%M  %a", &tm);
	if (had) setenv("TZ", keep, 1); else unsetenv("TZ");
	tzset();
}

static void tz_open(void) { tzs.sel = tzs.cur >= 0 ? tzs.cur : 0; tzs.top = 0; tzs.note[0] = 0; }

static int tz_key(int k)
{
	if (k == K_ESC || k == K_LEFT || k == K_BACK) return 1;
	if (k == K_UP && tzs.sel > 0) tzs.sel--;
	else if (k == K_DOWN && tzs.sel + 1 < NZONES) tzs.sel++;
	else if (k == K_ENTER) tz_choose(tzs.sel);
	return 0;
}

static void draw_time(Home *h, float t)
{
	(void)h; (void)t;
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	title_block("TIME", "TIME ZONE  CLOCK", I_INFO);
	float lx = U * 6, lw = W - U * 12, y = U * 22;
	ui_frost(lx - U * 1.2f, y - U * 1.2f, lw + U * 2.4f, U * 9.4f, U * 2.2f, 0.55f);
	char now[40]; tz_time_in(tzs.cur, now, sizeof now);
	char big[120], small[160];
	snprintf(big, sizeof big, "%s", ZONES[tzs.cur].name);
	snprintf(small, sizeof small, "%s   The clock is set from the internet when you're online. The computer's own clock is left alone, so Windows isn't disturbed.", now);
	sg_disc(&fb, lx + U * 3.2f, y + U * 2.6f, U * 0.75f, RGB(80, 150, 255), 1);
	char f1[200]; fit(big, U * 3.0f, lw - U * 30, f1, sizeof f1);
	sg_text(&fb, &FT, lx + U * 6, y + U * 3.6f, f1, U * 3.0f, WHITE, 1, 0.01f);
	fit(small, U * 1.7f, lw - U * 8, f1, sizeof f1);
	sg_text(&fb, &FT, lx + U * 6, y + U * 6.6f, f1, U * 1.7f, RGB(170, 170, 178), 1, 0.01f);
	if (tzs.note[0] && ui_t - tzs.note_t < 4) { float na = sg_sat((4 - (ui_t - tzs.note_t)) / 0.5f), nw = sg_text_w(&FT, tzs.note, U * 1.8f, 0.01f); sg_text(&fb, &FT, lx + lw - nw - U * 1.5f, y + U * 3.4f, tzs.note, U * 1.8f, RGB(90, 225, 140), na, 0.01f); }
	float ly = y + U * 12, rh = U * 4.6f; int vis = (int)((H - U * 9 - ly) / (rh + U * 0.4f));
	if (vis < 1) vis = 1;
	if (tzs.sel < tzs.top) tzs.top = tzs.sel;
	if (tzs.sel >= tzs.top + vis) tzs.top = tzs.sel - vis + 1;
	int shown = NZONES - tzs.top < vis ? NZONES - tzs.top : vis;
	ui_frost(lx - U * 1.2f, ly - U * 0.6f, lw + U * 2.4f, shown * (rh + U * 0.4f) + U * 0.8f, U * 2.2f, 0.55f);
	for (int r = 0; r < shown; r++) {
		int i = tzs.top + r; float ry = ly + r * (rh + U * 0.4f);
		int sel = i == tzs.sel, cur = i == tzs.cur;
		if (sel) { sg_rrect(&fb, lx, ry, lw, rh, U * 1.2f, 0, WHITE, 0.95f); sg_rrect(&fb, lx + U * 0.7f, ry + rh * 0.25f, U * 0.45f, rh * 0.5f, U * 0.22f, 0, RED, 1); }
		else if (r) sg_rect(&fb, lx + U * 3, ry - U * 0.2f, lw - U * 6, sg_max(1, U * 0.08f), WHITE, 0.08f);
		uint32_t fg = sel ? INK : WHITE, sub = sel ? RGB(80, 80, 88) : RGB(150, 150, 158);
		char nm[80]; fit(ZONES[i].name, U * 2.2f, lw * 0.6f, nm, sizeof nm);
		sg_text(&fb, &FT, lx + U * 3, ry + rh / 2 + U * 0.8f, nm, U * 2.2f, fg, 1, 0.01f);
		char tm[40]; tz_time_in(i, tm, sizeof tm);
		float tw = sg_text_w(&FC, tm, U * 1.3f, 0.2f);
		sg_text(&fb, &FC, lx + lw - U * 3 - tw, ry + rh / 2 + U * 0.5f, tm, U * 1.3f, cur ? RGB(40, 190, 100) : sub, 1, 0.2f);
		if (cur) sg_text(&fb, &FC, lx + lw - U * 3 - tw - U * 8, ry + rh / 2 + U * 0.5f, "YOURS", U * 1.0f, RGB(40, 190, 100), 1, 0.2f);
	}
	foot_hints("UP DOWN  MOVE      ENTER  USE THIS ZONE      ESC  SETTINGS");
}
