/*
 * Settings > Bluetooth: pair and connect headphones, speakers, game controllers, keyboards
 * and mice.
 *
 * BlueZ's bluetoothd (started by rc.boot from the games layer) does the work; we drive its
 * bluetoothctl tool in the background (one short command at a time, never blocking the screen)
 * and read what it prints. Sound goes to Bluetooth headphones in games (PulseAudio); the home
 * screen's own little sounds stay on the computer's speakers.
 */
#pragma once

#define BTCTL "/usr/bin/bluetoothctl"
#define MAXBT 24
typedef struct { char mac[18], name[64], icon[24]; int paired, connected, battery; } BtDev;
static struct {
	int have;                         /* bluetoothctl and an adapter */
	int powered, scanning;
	BtDev d[MAXBT]; int n;
	int sel, top;
	Job job; int step;                /* the command running now (see bt_next) */
	char busy_mac[18]; int busy_what; /* 1 pairing+connecting, 2 connecting, 3 disconnecting, 4 forgetting */
	float busy_t, last_poll, scan_until;
	char note[96]; float note_t;
	int info_i;                       /* which device's details to read next */
} bt;

static void bt_note(const char *s) { snprintf(bt.note, sizeof bt.note, "%s", s); bt.note_t = ui_t; }

static const char *bt_kind(const BtDev *d)
{
	if (strstr(d->icon, "headset") || strstr(d->icon, "headphone")) return "HEADPHONES";
	if (strstr(d->icon, "audio")) return "SPEAKER";
	if (strstr(d->icon, "gaming")) return "CONTROLLER";
	if (strstr(d->icon, "keyboard")) return "KEYBOARD";
	if (strstr(d->icon, "mouse")) return "MOUSE";
	if (strstr(d->icon, "phone")) return "PHONE";
	if (strstr(d->icon, "computer")) return "COMPUTER";
	return "";
}

#ifndef RENDER
static void bt_run(const char *a1, const char *a2, const char *a3)
{
	char *argv[6] = {BTCTL, 0, 0, 0, 0, 0}; int k = 1;
	if (a1) argv[k++] = (char *)a1;
	if (a2) argv[k++] = (char *)a2;
	if (a3) argv[k++] = (char *)a3;
	job_start(&bt.job, argv, 65536);
}
static BtDev *bt_find(const char *mac) { for (int i = 0; i < bt.n; i++) if (!strcmp(bt.d[i].mac, mac)) return &bt.d[i]; return NULL; }
/* "Device AA:BB:CC:DD:EE:FF Name" lines */
static void bt_parse_devices(const char *out)
{
	for (const char *l = out; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
		if (strncmp(l, "Device ", 7)) continue;
		char mac[18] = ""; memcpy(mac, l + 7, 17); mac[17] = 0;
		const char *nm = l + 25; const char *e = strchr(nm, '\n'); size_t len = e ? (size_t)(e - nm) : strlen(nm);
		BtDev *d = bt_find(mac);
		if (!d && bt.n < MAXBT) { d = &bt.d[bt.n++]; memset(d, 0, sizeof *d); snprintf(d->mac, sizeof d->mac, "%s", mac); d->battery = -1; }
		if (d) { if (len > 63) len = 63; memcpy(d->name, nm, len); d->name[len] = 0; }
	}
}
static void bt_parse_info(const char *out)
{
	char mac[18] = ""; const char *p = strstr(out, "Device ");
	if (!p) return;
	memcpy(mac, p + 7, 17); mac[17] = 0;
	BtDev *d = bt_find(mac); if (!d) return;
	d->paired = strstr(out, "Paired: yes") != NULL;
	d->connected = strstr(out, "Connected: yes") != NULL;
	const char *ic = strstr(out, "Icon: "); if (ic) { sscanf(ic + 6, "%23s", d->icon); }
	const char *ba = strstr(out, "Battery Percentage:"); if (ba) { const char *q = strchr(ba, '('); if (q) d->battery = atoi(q + 1); }
}
#endif

/* what to ask next; one command at a time */
static void bt_next(float t)
{
#ifdef RENDER
	(void)t;
#else
	if (bt.job.active) {
		job_poll(&bt.job);
		if (!bt.job.done) return;
		const char *out = bt.job.buf ? bt.job.buf : "";
		switch (bt.step) {
		case 1: bt.powered = strstr(out, "Powered: yes") != NULL; bt.have = strstr(out, "Controller ") != NULL; bt.scanning = strstr(out, "Discovering: yes") != NULL; break;
		case 2: bt_parse_devices(out); break;
		case 3: bt_parse_info(out); break;
		case 5: {                                                       /* a pair/connect/disconnect/remove finished */
			BtDev *d = bt_find(bt.busy_mac); char m[96];
			const char *nm = d && d->name[0] ? d->name : bt.busy_mac;
			int ok = bt.job.ok && !strstr(out, "Failed") && !strstr(out, "not available");
			if (bt.busy_what == 4) { snprintf(m, sizeof m, "Forgot %.40s.", nm); if (d) { *d = bt.d[--bt.n]; } }
			else if (bt.busy_what == 3) snprintf(m, sizeof m, "Disconnected %.40s.", nm);
			else snprintf(m, sizeof m, ok ? "Connected to %.40s." : "Couldn't connect to %.40s. Is it in pairing mode?", nm);
			bt_note(m); snd(ok ? 'r' : 'e'); bt.busy_mac[0] = 0; bt.busy_what = 0; bt.last_poll = 0;
			break; }
		}
		job_free(&bt.job); bt.step = 0;
	}
	if (bt.busy_what && !bt.step) {                                       /* do what was asked */
		char cmd[200];
		if (bt.busy_what == 1) snprintf(cmd, sizeof cmd, BTCTL " --timeout 25 pair %s; " BTCTL " trust %s; " BTCTL " --timeout 15 connect %s", bt.busy_mac, bt.busy_mac, bt.busy_mac);
		else if (bt.busy_what == 2) snprintf(cmd, sizeof cmd, BTCTL " --timeout 15 connect %s", bt.busy_mac);
		else if (bt.busy_what == 3) snprintf(cmd, sizeof cmd, BTCTL " disconnect %s", bt.busy_mac);
		else snprintf(cmd, sizeof cmd, BTCTL " remove %s", bt.busy_mac);
		char *argv[] = {"/bin/sh", "-c", cmd, 0};
		job_start(&bt.job, argv, 65536); bt.step = 5; return;
	}
	if (t - bt.last_poll < 1.2f && bt.last_poll) return;
	bt.last_poll = t;
	static int round;
	switch (round++ % 3) {
	case 0: bt_run("show", 0, 0); bt.step = 1; break;
	case 1: bt_run("devices", 0, 0); bt.step = 2; break;
	case 2: if (bt.n) { if (bt.info_i >= bt.n) bt.info_i = 0; bt_run("info", bt.d[bt.info_i++].mac, 0); bt.step = 3; } break;
	}
#endif
}

static void bt_mock(void)
{
	static int init;
	if (init) return;
	init = 1; bt.have = 1; bt.powered = 1;
	static const struct { const char *mac, *name, *icon; int paired, conn, bat; } M[] = {
		{"A0:11:22:33:44:55", "Xbox Wireless Controller", "input-gaming", 1, 1, 80},
		{"B4:22:33:44:55:66", "WH-1000XM5", "audio-headset", 1, 0, -1},
		{"C8:33:44:55:66:77", "DualSense Wireless Controller", "input-gaming", 0, 0, -1},
		{"D2:44:55:66:77:88", "JBL Flip 6", "audio-card", 0, 0, -1},
		{"E6:55:66:77:88:99", "MX Keys", "input-keyboard", 0, 0, -1},
	};
	for (unsigned i = 0; i < sizeof M / sizeof M[0]; i++) {
		BtDev *d = &bt.d[bt.n++]; memset(d, 0, sizeof *d);
		snprintf(d->mac, sizeof d->mac, "%s", M[i].mac); snprintf(d->name, sizeof d->name, "%s", M[i].name); snprintf(d->icon, sizeof d->icon, "%s", M[i].icon);
		d->paired = M[i].paired; d->connected = M[i].conn; d->battery = M[i].bat;
	}
}

static int bt_cmp(const void *a, const void *b)
{
	const BtDev *x = a, *y = b;
	if (x->connected != y->connected) return y->connected - x->connected;
	if (x->paired != y->paired) return y->paired - x->paired;
	return strcmp(x->name, y->name);
}

static void bt_open(void)
{
	bt.sel = 0; bt.top = 0; bt.note[0] = 0; bt.last_poll = 0;
#ifdef RENDER
	bt_mock();
#else
	bt.have = access(BTCTL, X_OK) == 0;
	if (bt.have && !bt.job.active) {                                  /* turn it on and look around for a while */
		char *argv[] = {"/bin/sh", "-c", BTCTL " power on >/dev/null; " BTCTL " --timeout 20 scan on >/dev/null 2>&1 &", 0};
		Job j; memset(&j, 0, sizeof j); job_start(&j, argv, 4096); job_wait(&j); job_free(&j);
		bt.scan_until = ui_t + 20;
	}
#endif
}

static int bt_key(int k)
{
	if (k == K_ESC || k == K_LEFT || k == K_BACK) return 1;
	if (k == K_UP && bt.sel > 0) bt.sel--;
	else if (k == K_DOWN && bt.sel + 1 < bt.n) bt.sel++;
	else if ((k == 'r' || k == 'R') && bt.have) {
#ifndef RENDER
		char *argv[] = {"/bin/sh", "-c", BTCTL " --timeout 20 scan on >/dev/null 2>&1 &", 0};
		Job j; memset(&j, 0, sizeof j); job_start(&j, argv, 4096); job_wait(&j); job_free(&j);
#endif
		bt.scan_until = ui_t + 20; bt_note("Looking for devices. Put yours in pairing mode.");
	}
	else if (bt.sel < bt.n && !bt.busy_what) {
		BtDev *d = &bt.d[bt.sel];
		if (k == K_ENTER) {
			bt.busy_what = d->connected ? 3 : d->paired ? 2 : 1; snprintf(bt.busy_mac, sizeof bt.busy_mac, "%s", d->mac); bt.busy_t = ui_t;
			char m[96]; snprintf(m, sizeof m, d->connected ? "Disconnecting %.40s ..." : d->paired ? "Connecting to %.40s ..." : "Pairing with %.40s ...", d->name[0] ? d->name : d->mac); bt_note(m);
#ifdef RENDER
			d->connected = !d->connected; if (d->connected) d->paired = 1; bt.busy_what = 0;
			snprintf(m, sizeof m, d->connected ? "Connected to %.40s." : "Disconnected %.40s.", d->name); bt_note(m);
#endif
		} else if ((k == 'f' || k == 'F') && d->paired) {
			bt.busy_what = 4; snprintf(bt.busy_mac, sizeof bt.busy_mac, "%s", d->mac); bt.busy_t = ui_t;
#ifdef RENDER
			bt.busy_what = 0; d->paired = d->connected = 0;
#endif
		}
	}
	return 0;
}

static void draw_bluetooth(Home *h, float t)
{
	(void)h;
	bt_next(t);
	qsort(bt.d, (size_t)bt.n, sizeof bt.d[0], bt_cmp);
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	title_block("BLUETOOTH", "HEADPHONES  CONTROLLERS  KEYBOARDS", I_SOUND);
	float lx = U * 6, lw = W - U * 12, y = U * 22;
	ui_frost(lx - U * 1.2f, y - U * 1.2f, lw + U * 2.4f, U * 9.4f, U * 2.2f, 0.55f);
	int conn = 0; for (int i = 0; i < bt.n; i++) conn += bt.d[i].connected;
	char big[96], small[128]; uint32_t dot = RGB(110, 110, 118);
	int looking = ui_t < bt.scan_until;
	if (!bt.have) { snprintf(big, sizeof big, "No Bluetooth adapter found"); snprintf(small, sizeof small, "This computer has none SilverOS can use, or its games layer isn't installed. A USB Bluetooth adapter works."); }
	else if (!bt.powered) { snprintf(big, sizeof big, "Bluetooth is starting ..."); snprintf(small, sizeof small, "One moment"); }
	else { snprintf(big, sizeof big, conn ? "%d device%s connected" : "Bluetooth is on", conn, conn == 1 ? "" : "s"); snprintf(small, sizeof small, looking ? "Looking for devices nearby. Put yours in pairing mode." : "R looks for new devices"); dot = RGB(80, 150, 255); }
	sg_disc(&fb, lx + U * 3.2f, y + U * 2.6f, U * 0.75f, dot, 1);
	if (bt.have && bt.powered) sg_glow(&fb, lx + U * 3.2f, y + U * 2.6f, U * 2.6f, dot, 0.35f + 0.15f * sg_sin(t * 3));
	char f1[128]; fit(big, U * 3.0f, lw - U * 8, f1, sizeof f1);
	sg_text(&fb, &FT, lx + U * 6, y + U * 3.6f, f1, U * 3.0f, WHITE, 1, 0.01f);
	fit(small, U * 1.9f, lw - U * 8, f1, sizeof f1);
	sg_text(&fb, &FT, lx + U * 6, y + U * 6.6f, f1, U * 1.9f, RGB(170, 170, 178), looking ? 0.65f + 0.35f * sg_sin(t * 3) : 1, 0.01f);
	if (bt.note[0] && t - bt.note_t < 7) {
		float na = sg_sat((7 - (t - bt.note_t)) / 0.6f), nw = sg_text_w(&FT, bt.note, U * 1.8f, 0.01f);
		uint32_t nc = strncmp(bt.note, "Couldn't", 8) ? RGB(130, 190, 255) : RGB(255, 140, 130);
		sg_text(&fb, &FT, lx + lw - nw - U * 1.5f, y + U * 3.4f, bt.note, U * 1.8f, nc, na, 0.01f);
	}
	float ly = y + U * 12, rh = U * 5.6f; int vis = (int)((H - U * 9 - ly) / (rh + U * 0.5f)); if (vis < 1) vis = 1;
	if (bt.sel >= bt.n) bt.sel = bt.n ? bt.n - 1 : 0;
	if (bt.sel < bt.top) bt.top = bt.sel;
	if (bt.sel >= bt.top + vis) bt.top = bt.sel - vis + 1;
	int shown = bt.n - bt.top < vis ? bt.n - bt.top : vis;
	cap_text(lx, ly - U * 1.2f, bt.n ? "DEVICES" : "", U * 1.2f, RGB(176, 176, 182), 1, 0);
	if (shown > 0) ui_frost(lx - U * 1.2f, ly - U * 0.6f, lw + U * 2.4f, shown * (rh + U * 0.5f) + U * 0.8f, U * 2.2f, 0.55f);
	for (int r = 0; r < shown; r++) {
		int i = bt.top + r; const BtDev *d = &bt.d[i]; float ry = ly + r * (rh + U * 0.5f);
		int sel = i == bt.sel, busy = bt.busy_what && !strcmp(bt.busy_mac, d->mac);
		if (sel) { sg_rrect(&fb, lx, ry, lw, rh, U * 1.3f, 0, WHITE, 0.95f); sg_rrect(&fb, lx + U * 0.7f, ry + rh * 0.25f, U * 0.45f, rh * 0.5f, U * 0.22f, 0, RED, 1); }
		else if (r) sg_rect(&fb, lx + U * 3, ry - U * 0.25f, lw - U * 6, sg_max(1, U * 0.08f), WHITE, 0.08f);
		uint32_t fg = sel ? INK : WHITE, sub = sel ? RGB(80, 80, 88) : RGB(150, 150, 158), blue = sel ? RGB(30, 100, 220) : RGB(110, 170, 255);
		const char *kind = bt_kind(d);
		icon(!strcmp(kind, "CONTROLLER") ? I_GAME : (!strcmp(kind, "HEADPHONES") || !strcmp(kind, "SPEAKER")) ? I_SOUND : I_PC, lx + U * 3.6f, ry + rh / 2, U * 1.5f, d->connected ? blue : fg, 1);
		char nm[48]; fit(d->name[0] ? d->name : d->mac, U * 2.4f, lw * 0.5f, nm, sizeof nm);
		sg_text(&fb, &FT, lx + U * 7.4f, ry + rh / 2 + U * 0.85f, nm, U * 2.4f, fg, 1, 0.01f);
		char right[64];
		const char *st = busy ? (bt.busy_what == 1 ? "PAIRING" : bt.busy_what == 2 ? "CONNECTING" : bt.busy_what == 3 ? "DISCONNECTING" : "FORGETTING") : d->connected ? "CONNECTED" : d->paired ? "PAIRED" : "";
		if (d->battery >= 0) snprintf(right, sizeof right, "%s%s%s%sBATTERY %d%%", st, st[0] ? "     " : "", kind, kind[0] ? "     " : "", d->battery);
		else snprintf(right, sizeof right, "%s%s%s", st, st[0] && kind[0] ? "     " : "", kind);
		float rw = sg_text_w(&FC, right, U * 1.2f, 0.22f);
		sg_text(&fb, &FC, lx + lw - U * 3 - rw, ry + rh / 2 + U * 0.5f, right, U * 1.2f, d->connected ? blue : sub, busy ? 0.5f + 0.5f * sg_sin(t * 6) : 1, 0.22f);
	}
	if (!bt.n && bt.have) { float a = 0.5f + 0.5f * sg_sin(t * 3); sg_text(&fb, &FT, lx + U * 2, ly + U * 3, "Looking for devices ... put yours in pairing mode.", U * 2.2f, WHITE, a, 0.01f); }
	if (!bt.have) foot_hints("ESC  SETTINGS");
	else if (bt.sel < bt.n && bt.d[bt.sel].paired) foot_hints(bt.d[bt.sel].connected ? "UP DOWN  MOVE      ENTER  DISCONNECT      F  FORGET      R  LOOK AGAIN      ESC  SETTINGS" : "UP DOWN  MOVE      ENTER  CONNECT      F  FORGET      R  LOOK AGAIN      ESC  SETTINGS");
	else foot_hints("UP DOWN  MOVE      ENTER  PAIR      R  LOOK AGAIN      ESC  SETTINGS");
}
