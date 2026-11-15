/*
 * Settings > Network: join Wi-Fi.
 *
 * /sbin/silver-wifi runs wpa_supplicant on every wireless adapter; we talk to it over its control
 * socket (/run/wpa_supplicant/<adapter>, open to the "netdev" group the home screen runs in).
 * Each request is a short text command answered at once (SCAN, SCAN_RESULTS, STATUS,
 * ADD_NETWORK, SET_NETWORK, SELECT_NETWORK, REMOVE_NETWORK, LIST_NETWORKS, SAVE_CONFIG).
 * Networks you join are saved and joined again by themselves next time.
 *
 * What it can't do (yet): WPA3-only networks, and work or school (802.1X / "enterprise") ones.
 */
#pragma once
#include <sys/socket.h>
#include <sys/un.h>
#include <dirent.h>

#define MAXAP 24
typedef struct { char ssid[33], bssid[18]; int sig, freq, sec, saved, id; } Ap;   /* sec: 0 open, 1 WPA/WPA2, 2 WPA3 only, 3 other (WEP, enterprise) */
static struct {
	char ifn[32];                    /* the adapter we control, "" = none */
	int adapters;                    /* wireless adapters the kernel knows about */
	int hardoff;                     /* turned off by a switch or key on the computer */
	Ap ap[MAXAP]; int nap;
	char state[32], ssid[33], ip[40]; /* STATUS */
	char wired[64];                  /* "eth0  192.168.1.24" or "" */
	int sel, top;
	int typing; char pw[64]; int show;  /* the password box */
	char joining[33]; float join_t; int join_new, join_id;
	char note[96]; float note_t;     /* a line of news under the status */
	float last_poll, last_scan;
	int sock;
} wf = {.sock = -1};

#ifndef RENDER
static int wpa_req(const char *cmd, char *out, size_t n)
{
	if (!wf.ifn[0]) return -1;
	if (wf.sock < 0) {
		wf.sock = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
		if (wf.sock < 0) return -1;
		struct sockaddr_un me = {.sun_family = AF_UNIX};                 /* an abstract name: nothing to clean up */
		snprintf(me.sun_path + 1, sizeof me.sun_path - 1, "silver-wifi-%d", (int)getpid());
		if (bind(wf.sock, (struct sockaddr *)&me, sizeof me)) { close(wf.sock); wf.sock = -1; return -1; }
		struct timeval tv = {0, 400000}; setsockopt(wf.sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
	}
	struct sockaddr_un to = {.sun_family = AF_UNIX};
	snprintf(to.sun_path, sizeof to.sun_path, "/run/wpa_supplicant/%s", wf.ifn);
	if (sendto(wf.sock, cmd, strlen(cmd), 0, (struct sockaddr *)&to, sizeof to) < 0) return -1;
	for (int tries = 0; tries < 4; tries++) {
		ssize_t r = recv(wf.sock, out, n - 1, 0);
		if (r < 0) return -1;
		out[r] = 0;
		if (out[0] != '<') return (int)r;                               /* skip any event messages */
	}
	return -1;
}
#endif

#ifndef RENDER
static void hexs(const char *s, char *out) { for (; *s; s++, out += 2) sprintf(out, "%02x", (unsigned char)*s); *out = 0; }
#endif

static void wf_note(const char *s) { snprintf(wf.note, sizeof wf.note, "%s", s); wf.note_t = ui_t; }

static void wf_find(void)
{
	wf.adapters = 0; wf.hardoff = 0;
#ifdef RENDER
	wf.adapters = 1; snprintf(wf.ifn, sizeof wf.ifn, "wlan0");
#else
	DIR *d = opendir("/sys/class/net"); struct dirent *e; char p[300], first[32] = "";
	if (d) {
		while ((e = readdir(d))) {
			if (e->d_name[0] == '.') continue;
			snprintf(p, sizeof p, "/sys/class/net/%s/wireless", e->d_name);
			struct stat st;
			if (stat(p, &st)) { snprintf(p, sizeof p, "/sys/class/net/%s/phy80211", e->d_name); if (stat(p, &st)) continue; }
			wf.adapters++;
			snprintf(p, sizeof p, "/run/wpa_supplicant/%s", e->d_name);
			if (!stat(p, &st) && !first[0]) snprintf(first, sizeof first, "%.31s", e->d_name);
		}
		closedir(d);
	}
	if (strcmp(first, wf.ifn)) { snprintf(wf.ifn, sizeof wf.ifn, "%s", first); if (wf.sock >= 0) { close(wf.sock); wf.sock = -1; } }
	if ((d = opendir("/sys/class/rfkill"))) {
		while ((e = readdir(d))) {
			if (e->d_name[0] == '.') continue;
			char ty[16] = "", hd[4] = ""; FILE *f;
			snprintf(p, sizeof p, "/sys/class/rfkill/%s/type", e->d_name); if ((f = fopen(p, "r"))) { if (!fgets(ty, sizeof ty, f)) ty[0] = 0; fclose(f); }
			snprintf(p, sizeof p, "/sys/class/rfkill/%s/hard", e->d_name); if ((f = fopen(p, "r"))) { if (!fgets(hd, sizeof hd, f)) hd[0] = 0; fclose(f); }
			if (!strncmp(ty, "wlan", 4) && hd[0] == '1') wf.hardoff = 1;
		}
		closedir(d);
	}
#endif
}

static void wf_wired(void)
{
	wf.wired[0] = 0;
#ifdef RENDER
	return;
#else
	struct ifaddrs *ifa, *p;
	if (getifaddrs(&ifa)) return;
	for (p = ifa; p; p = p->ifa_next)
		if (p->ifa_addr && p->ifa_addr->sa_family == AF_INET && !(p->ifa_flags & IFF_LOOPBACK) && strcmp(p->ifa_name, wf.ifn)) {
			char ip[40]; inet_ntop(AF_INET, &((struct sockaddr_in *)p->ifa_addr)->sin_addr, ip, sizeof ip);
			snprintf(wf.wired, sizeof wf.wired, "%.20s  %s", p->ifa_name, ip); break;
		}
	freeifaddrs(ifa);
#endif
}

static int ap_cmp(const void *a, const void *b)
{
	const Ap *x = a, *y = b;
	int cx = !strcmp(x->ssid, wf.ssid) && wf.ssid[0], cy = !strcmp(y->ssid, wf.ssid) && wf.ssid[0];
	if (cx != cy) return cy - cx;                                       /* the one you're on first, then the strongest */
	return y->sig - x->sig;
}

#ifdef RENDER
static void wf_mock(void)
{
	static const struct { const char *s; int sig, sec; } M[] = {
		{"Kings Den", -41, 1}, {"Kings Den 5G", -48, 1}, {"NETGEAR-Guest", -63, 0}, {"Pixel_7731", -66, 1},
		{"xfinitywifi", -71, 0}, {"DIRECT-HP OfficeJet", -74, 1}, {"Fiber-Home-2F9A", -79, 2}, {"SchoolNet", -83, 3},
	};
	wf.nap = 0;
	for (unsigned i = 0; i < sizeof M / sizeof M[0]; i++) {
		Ap *a = &wf.ap[wf.nap++]; memset(a, 0, sizeof *a);
		snprintf(a->ssid, sizeof a->ssid, "%s", M[i].s); a->sig = M[i].sig; a->sec = M[i].sec; a->freq = strstr(M[i].s, "5G") ? 5180 : 2437; a->id = -1;
		if (wf.ssid[0] && !strcmp(a->ssid, wf.ssid)) { a->saved = 1; a->id = 0; }
	}
	if (wf.joining[0] && ui_t - wf.join_t > 1.6f) {                    /* pretend it worked */
		snprintf(wf.ssid, sizeof wf.ssid, "%s", wf.joining); snprintf(wf.state, sizeof wf.state, "COMPLETED");
		snprintf(wf.ip, sizeof wf.ip, "192.168.1.37"); wf.joining[0] = 0; wf_note("Connected. SilverOS will join it by itself next time.");
	} else if (wf.joining[0]) snprintf(wf.state, sizeof wf.state, "ASSOCIATING");
	else if (!wf.ssid[0]) snprintf(wf.state, sizeof wf.state, "DISCONNECTED");
	qsort(wf.ap, (size_t)wf.nap, sizeof wf.ap[0], ap_cmp);
}
#endif

/* the networks you have saved: id and name */
#ifndef RENDER
static void wf_saved(void)
{
	char buf[4096];
	for (int i = 0; i < wf.nap; i++) { wf.ap[i].saved = 0; wf.ap[i].id = -1; }
	if (wpa_req("LIST_NETWORKS", buf, sizeof buf) <= 0) return;
	char *line = strchr(buf, '\n');                                      /* first line: the column names */
	while (line && *++line) {
		char *end = strchr(line, '\n'); if (end) *end = 0;
		int id = atoi(line); char *t1 = strchr(line, '\t');
		if (t1) {
			char *t2 = strchr(t1 + 1, '\t'); if (t2) *t2 = 0;
			for (int i = 0; i < wf.nap; i++) if (!strcmp(wf.ap[i].ssid, t1 + 1)) { wf.ap[i].saved = 1; wf.ap[i].id = id; }
			if (t2) *t2 = '\t';
			if (wf.joining[0] && id == wf.join_id && strstr(t2 ? t2 : "", "TEMP-DISABLED")) {   /* it tried and gave up: almost always the password */
				char cmd[48];
				if (wf.join_new) { snprintf(cmd, sizeof cmd, "REMOVE_NETWORK %d", id); wpa_req(cmd, buf, 64); wpa_req("SAVE_CONFIG", buf, 64); }
				else { snprintf(cmd, sizeof cmd, "ENABLE_NETWORK %d", id); wpa_req(cmd, buf, 64); }
				char m[96]; snprintf(m, sizeof m, "Couldn't join %.32s. Wrong password?", wf.joining); wf_note(m);
				snd('e'); wf.joining[0] = 0;
				return;
			}
		}
		if (!end) break;
		line = end;
	}
}
#endif

static void wf_poll(float t)
{
	if (t - wf.last_poll < 1.0f && wf.last_poll) return;
	wf.last_poll = t;
	wf_find(); wf_wired();
#ifdef RENDER
	wf_mock(); return;
#else
	char buf[8192];
	wf.state[0] = wf.ssid[0] = wf.ip[0] = 0;
	if (!wf.ifn[0]) { wf.nap = 0; return; }
	if (wpa_req("STATUS", buf, sizeof buf) > 0)
		for (char *l = strtok(buf, "\n"); l; l = strtok(NULL, "\n")) {
			if (!strncmp(l, "wpa_state=", 10)) snprintf(wf.state, sizeof wf.state, "%s", l + 10);
			else if (!strncmp(l, "ssid=", 5)) snprintf(wf.ssid, sizeof wf.ssid, "%s", l + 5);
			else if (!strncmp(l, "ip_address=", 11)) snprintf(wf.ip, sizeof wf.ip, "%s", l + 11);
		}
	if (strcmp(wf.state, "COMPLETED")) wf.ssid[0] = 0;
	if (!wf.ip[0] && wf.ssid[0]) {                                       /* STATUS only knows the address once DHCP sets it; ask the kernel */
		struct ifaddrs *ifa, *p;
		if (!getifaddrs(&ifa)) { for (p = ifa; p; p = p->ifa_next) if (p->ifa_addr && p->ifa_addr->sa_family == AF_INET && !strcmp(p->ifa_name, wf.ifn)) inet_ntop(AF_INET, &((struct sockaddr_in *)p->ifa_addr)->sin_addr, wf.ip, sizeof wf.ip); freeifaddrs(ifa); }
	}
	if (wf.joining[0] && wf.ssid[0] && !strcmp(wf.ssid, wf.joining)) {
		wf.joining[0] = 0; wpa_req("SAVE_CONFIG", buf, 64); wf_note("Connected. SilverOS will join it by itself next time."); snd('r');
	} else if (wf.joining[0] && t - wf.join_t > 30) { char m[96]; snprintf(m, sizeof m, "%.32s didn't answer. Try again closer to it?", wf.joining); wf_note(m); wf.joining[0] = 0; snd('e'); }
	if (!wf.last_scan || t - wf.last_scan > 12) { wpa_req("SCAN", buf, 64); wf.last_scan = t; }
	if (wpa_req("SCAN_RESULTS", buf, sizeof buf) <= 0) return;
	wf.nap = 0;
	char *line = strchr(buf, '\n');
	while (line && *++line) {
		char *end = strchr(line, '\n'); if (end) *end = 0;
		char *f[5] = {line}; int nf = 1;
		for (char *c = line; *c && nf < 5; c++) if (*c == '\t') { *c = 0; f[nf++] = c + 1; }
		if (nf == 5 && f[4][0] && !strstr(f[4], "\\x00")) {
			int sig = atoi(f[2]), dup = -1;
			for (int i = 0; i < wf.nap; i++) if (!strcmp(wf.ap[i].ssid, f[4])) dup = i;
			if (dup >= 0) { if (sig > wf.ap[dup].sig) wf.ap[dup].sig = sig; }
			else if (wf.nap < MAXAP) {
				Ap *a = &wf.ap[wf.nap++]; memset(a, 0, sizeof *a);
				snprintf(a->ssid, sizeof a->ssid, "%s", f[4]); snprintf(a->bssid, sizeof a->bssid, "%s", f[0]);
				a->sig = sig; a->freq = atoi(f[1]); a->id = -1;
				const char *fl = f[3];
				int psk = strstr(fl, "PSK") != NULL, sae = strstr(fl, "SAE") != NULL, eap = strstr(fl, "EAP") != NULL;
				a->sec = psk ? 1 : sae ? 2 : (eap || strstr(fl, "WEP")) ? 3 : 0;
			}
		}
		if (!end) break;
		line = end;
	}
	wf_saved();
	qsort(wf.ap, (size_t)wf.nap, sizeof wf.ap[0], ap_cmp);
#endif
}

static void wf_open(void) { wf.sel = 0; wf.top = 0; wf.typing = 0; wf.last_poll = 0; wf.last_scan = 0; wf.note[0] = 0; }

/* join a network: a saved one, an open one, or a new one with the password just typed */
static void wf_join(const Ap *a, const char *pw)
{
	snprintf(wf.joining, sizeof wf.joining, "%s", a->ssid); wf.join_t = ui_t; wf.join_new = 0;
	char m[96]; snprintf(m, sizeof m, "Joining %.32s ...", a->ssid); wf_note(m);
#ifndef RENDER
	char buf[256], cmd[200], hx[70];
	if (a->saved && !pw) { snprintf(cmd, sizeof cmd, "SELECT_NETWORK %d", a->id); wf.join_id = a->id; wpa_req(cmd, buf, sizeof buf); return; }
	if (a->saved) { snprintf(cmd, sizeof cmd, "REMOVE_NETWORK %d", a->id); wpa_req(cmd, buf, sizeof buf); }
	if (wpa_req("ADD_NETWORK", buf, sizeof buf) <= 0 || buf[0] < '0' || buf[0] > '9') { wf_note("Wi-Fi didn't answer. Try again in a moment."); wf.joining[0] = 0; return; }
	int id = atoi(buf); wf.join_id = id; wf.join_new = 1;
	hexs(a->ssid, hx); snprintf(cmd, sizeof cmd, "SET_NETWORK %d ssid %s", id, hx); wpa_req(cmd, buf, sizeof buf);
	if (pw) {
		snprintf(cmd, sizeof cmd, "SET_NETWORK %d psk \"%s\"", id, pw); wpa_req(cmd, buf, sizeof buf);
		snprintf(cmd, sizeof cmd, "SET_NETWORK %d key_mgmt WPA-PSK", id); wpa_req(cmd, buf, sizeof buf);
		snprintf(cmd, sizeof cmd, "SET_NETWORK %d ieee80211w 1", id); wpa_req(cmd, buf, sizeof buf);   /* protected management frames when offered */
	} else { snprintf(cmd, sizeof cmd, "SET_NETWORK %d key_mgmt NONE", id); wpa_req(cmd, buf, sizeof buf); }
	snprintf(cmd, sizeof cmd, "SELECT_NETWORK %d", id); wpa_req(cmd, buf, sizeof buf);
#else
	(void)pw;
#endif
}

static void wf_forget(const Ap *a)
{
	char m[96]; snprintf(m, sizeof m, "Forgot %.32s.", a->ssid); wf_note(m);
#ifndef RENDER
	char buf[64], cmd[48];
	snprintf(cmd, sizeof cmd, "REMOVE_NETWORK %d", a->id); wpa_req(cmd, buf, sizeof buf); wpa_req("SAVE_CONFIG", buf, sizeof buf);
#endif
	if (!strcmp(a->ssid, wf.ssid)) { wf.ssid[0] = 0; wf.ip[0] = 0; }
	wf.last_poll = 0;
}

/* keys on the Network page; returns 1 to go back to Settings */
static int wf_key(int k)
{
	if (wf.typing) {
		size_t n = strlen(wf.pw);
		if (k == K_ESC) { wf.typing = 0; memset(wf.pw, 0, sizeof wf.pw); }
		else if (k == 9) wf.show ^= 1;
		else if (k == K_BACK && n) wf.pw[n - 1] = 0;
		else if (k == 21) wf.pw[0] = 0;
		else if (k >= 32 && k < 127 && n + 1 < sizeof wf.pw) { wf.pw[n] = (char)k; wf.pw[n + 1] = 0; }
		else if (k == K_ENTER) {
			if (n < 8) wf_note("Wi-Fi passwords are at least 8 characters.");
			else if (wf.sel < wf.nap) { wf_join(&wf.ap[wf.sel], wf.pw); wf.typing = 0; memset(wf.pw, 0, sizeof wf.pw); }
		}
		return 0;
	}
	if (k == K_ESC || k == K_LEFT || k == K_BACK) return 1;
	if (k == K_UP && wf.sel > 0) wf.sel--;
	else if (k == K_DOWN && wf.sel + 1 < wf.nap) wf.sel++;
	else if ((k == 'r' || k == 'R') && wf.ifn[0]) { wf.last_scan = 0; wf.last_poll = 0; wf_note("Looking for networks ..."); }
	else if ((k == 'f' || k == 'F') && wf.sel < wf.nap && wf.ap[wf.sel].saved) wf_forget(&wf.ap[wf.sel]);
	else if (k == K_ENTER && wf.sel < wf.nap) {
		Ap *a = &wf.ap[wf.sel];
		if (!strcmp(a->ssid, wf.ssid)) wf_note("You're on this one. F forgets it.");
		else if (a->sec == 2) wf_note("This network is WPA3-only, which SilverOS can't join yet.");
		else if (a->sec == 3) wf_note("Work and school sign-in networks aren't supported yet.");
		else if (a->sec == 1 && !a->saved) { wf.typing = 1; wf.pw[0] = 0; wf.show = 0; }
		else wf_join(a, NULL);
	}
	return 0;
}

static void wf_bars(float x, float y, float s, int sig, uint32_t c, uint32_t dim)
{
	int lv = sig >= -55 ? 4 : sig >= -65 ? 3 : sig >= -75 ? 2 : 1;
	for (int i = 0; i < 4; i++) { float bh = s * (0.3f + 0.233f * i); sg_rrect(&fb, x + i * s * 0.32f, y + s - bh, s * 0.22f, bh, s * 0.06f, 0, i < lv ? c : dim, 1); }
}
static void wf_lock(float x, float y, float s, uint32_t c)
{
	sg_rrect(&fb, x + s * 0.18f, y, s * 0.64f, s * 0.75f, s * 0.32f, s * 0.12f, c, 1);
	sg_rrect(&fb, x, y + s * 0.38f, s, s * 0.66f, s * 0.14f, 0, c, 1);
}

static void draw_network(Home *h, float t)
{
	(void)h;
	wf_poll(t);
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	title_block("NETWORK", "WI-FI  WIRED", I_NET);
	float lx = U * 6, lw = W - U * 12, y = U * 22;
	/* the status card */
	ui_frost(lx - U * 1.2f, y - U * 1.2f, lw + U * 2.4f, U * 9.4f, U * 2.2f, 0.55f);
	char big[96], small[128]; uint32_t dot = RGB(110, 110, 118);
	if (wf.ssid[0]) { snprintf(big, sizeof big, "Connected to %s", wf.ssid); snprintf(small, sizeof small, "%s   %s", wf.ifn, wf.ip[0] ? wf.ip : "getting an address ..."); dot = RGB(70, 220, 120); }
	else if (wf.joining[0]) { snprintf(big, sizeof big, "Joining %s", wf.joining); snprintf(small, sizeof small, "%s", wf.ifn); dot = RGB(250, 190, 60); }
	else if (wf.hardoff) { snprintf(big, sizeof big, "Wi-Fi is switched off"); snprintf(small, sizeof small, "Turn it on with the Wi-Fi key or switch on your computer"); }
	else if (!wf.adapters) { snprintf(big, sizeof big, "No Wi-Fi adapter found"); snprintf(small, sizeof small, "This computer's Wi-Fi isn't supported, or there isn't one. A USB Wi-Fi stick or a cable works."); }
	else if (!wf.ifn[0]) { snprintf(big, sizeof big, "Starting Wi-Fi ..."); snprintf(small, sizeof small, "One moment"); }
	else { snprintf(big, sizeof big, "Not connected to Wi-Fi"); snprintf(small, sizeof small, "%s   pick a network below", wf.ifn); }
	if (wf.wired[0]) { size_t l = strlen(small); snprintf(small + l, sizeof small - l, "%sCable: %s", l ? "      " : "", wf.wired); if (!wf.ssid[0]) dot = RGB(70, 220, 120); }
	sg_disc(&fb, lx + U * 3.2f, y + U * 2.6f, U * 0.75f, dot, 1);
	if (dot != RGB(110, 110, 118)) sg_glow(&fb, lx + U * 3.2f, y + U * 2.6f, U * 2.6f, dot, 0.35f + 0.15f * sg_sin(t * 3));
	char fb1[128]; fit(big, U * 3.0f, lw - U * 8, fb1, sizeof fb1);
	sg_text(&fb, &FT, lx + U * 6, y + U * 3.6f, fb1, U * 3.0f, WHITE, 1, 0.01f);
	fit(small, U * 1.9f, lw - U * 8, fb1, sizeof fb1);
	sg_text(&fb, &FT, lx + U * 6, y + U * 6.6f, fb1, U * 1.9f, RGB(170, 170, 178), 1, 0.01f);
	if (wf.note[0] && t - wf.note_t < 7) { float na = sg_sat((7 - (t - wf.note_t)) / 0.6f); float nw = sg_text_w(&FT, wf.note, U * 1.8f, 0.01f); sg_text(&fb, &FT, lx + lw - nw - U * 1.5f, y + U * 3.4f, wf.note, U * 1.8f, RGB(255, 140, 130), na, 0.01f); }
	/* the networks */
	float ly = y + U * 12, rh = U * 5.6f; int vis = (int)((H - U * 9 - ly) / (rh + U * 0.5f));
	if (vis < 1) vis = 1;
	if (wf.sel < wf.top) wf.top = wf.sel;
	if (wf.sel >= wf.top + vis) wf.top = wf.sel - vis + 1;
	int shown = wf.nap - wf.top < vis ? wf.nap - wf.top : vis;
	cap_text(lx, ly - U * 1.2f, wf.nap ? "NETWORKS NEARBY" : "", U * 1.2f, RGB(176, 176, 182), 1, 0);
	if (shown > 0) ui_frost(lx - U * 1.2f, ly - U * 0.6f, lw + U * 2.4f, shown * (rh + U * 0.5f) + U * 0.8f, U * 2.2f, 0.55f);
	else if (wf.ifn[0]) { float a = 0.5f + 0.5f * sg_sin(t * 3); sg_text(&fb, &FT, lx + U * 2, ly + U * 3, "Looking for networks ...", U * 2.2f, WHITE, a, 0.01f); }
	for (int r = 0; r < shown; r++) {
		int i = wf.top + r; const Ap *a = &wf.ap[i]; float ry = ly + r * (rh + U * 0.5f);
		int sel = i == wf.sel && !wf.typing, on = wf.ssid[0] && !strcmp(a->ssid, wf.ssid);
		if (sel) { sg_rrect(&fb, lx, ry, lw, rh, U * 1.3f, 0, WHITE, 0.95f); sg_rrect(&fb, lx + U * 0.7f, ry + rh * 0.25f, U * 0.45f, rh * 0.5f, U * 0.22f, 0, RED, 1); }
		else if (r) sg_rect(&fb, lx + U * 3, ry - U * 0.25f, lw - U * 6, sg_max(1, U * 0.08f), WHITE, 0.08f);
		uint32_t fg = sel ? INK : WHITE, sub = sel ? RGB(80, 80, 88) : RGB(150, 150, 158);
		wf_bars(lx + U * 2.6f, ry + rh / 2 - U * 1.1f, U * 2.2f, a->sig, on ? RGB(40, 190, 100) : fg, sel ? RGB(190, 190, 196) : RGB(70, 70, 78));
		char nm[40]; fit(a->ssid, U * 2.4f, lw * 0.5f, nm, sizeof nm);
		sg_text(&fb, &FT, lx + U * 7.4f, ry + rh / 2 + U * 0.85f, nm, U * 2.4f, fg, 1, 0.01f);
		float nx = lx + U * 7.4f + sg_text_w(&FT, nm, U * 2.4f, 0.01f) + U * 1.4f;
		if (a->sec) wf_lock(nx, ry + rh / 2 - U * 0.9f, U * 1.5f, sub);
		const char *tag = on ? "CONNECTED" : (wf.joining[0] && !strcmp(wf.joining, a->ssid)) ? "JOINING" : a->saved ? "SAVED" : a->sec == 2 ? "WPA3 ONLY" : a->sec == 3 ? "SIGN-IN" : a->sec ? "" : "OPEN";
		char right[48]; snprintf(right, sizeof right, "%s%s%s", tag, tag[0] ? "     " : "", a->freq >= 5900 ? "6 GHz" : a->freq >= 5000 ? "5 GHz" : "2.4 GHz");
		float rw = sg_text_w(&FC, right, U * 1.2f, 0.22f);
		sg_text(&fb, &FC, lx + lw - U * 3 - rw, ry + rh / 2 + U * 0.5f, right, U * 1.2f, on ? RGB(40, 190, 100) : sub, 1, 0.22f);
	}
	/* the password box */
	if (wf.typing && wf.sel < wf.nap) {
		sg_rect(&fb, 0, 0, W, H, BLACK, 0.45f);
		float pw = W * 0.52f, ph = U * 20, px = (W - pw) / 2, py = (H - ph) / 2;
		ui_frost(px, py, pw, ph, U * 2.4f, 0.75f);
		char tl0[80], tl[80]; snprintf(tl0, sizeof tl0, "Password for %s", wf.ap[wf.sel].ssid); fit(tl0, U * 2.8f, pw - U * 6, tl, sizeof tl);
		sg_text(&fb, &FT, px + U * 3, py + U * 5, tl, U * 2.8f, WHITE, 1, 0.01f);
		float bx = px + U * 3, by = py + U * 7.6f, bw = pw - U * 6, bh = U * 5.2f;
		sg_rrect(&fb, bx, by, bw, bh, bh / 2, 0, WHITE, 0.96f);
		char shown_pw[64]; size_t n = strlen(wf.pw);
		if (wf.show) snprintf(shown_pw, sizeof shown_pw, "%s", wf.pw); else { for (size_t i = 0; i < n && i < 63; i++) shown_pw[i] = '*'; shown_pw[n < 63 ? n : 63] = 0; }
		float tw = sg_text_w(&FT, shown_pw, U * 2.4f, 0.04f);
		sg_text(&fb, &FT, bx + U * 2.4f, by + bh / 2 + U * 0.85f, shown_pw, U * 2.4f, INK, 1, 0.04f);
		if (fmodf(t, 1.0f) < 0.6f) sg_rect(&fb, bx + U * 2.5f + tw, by + bh * 0.25f, sg_max(1, U * 0.18f), bh * 0.5f, RED, 1);
		if (!n) sg_text(&fb, &FT, bx + U * 2.4f, by + bh / 2 + U * 0.85f, "Type the Wi-Fi password", U * 2.2f, RGB(150, 150, 158), 1, 0.01f);
		sg_text(&fb, &FT, px + U * 3, py + U * 16.4f, "Saved on this computer only, so it joins by itself next time.", U * 1.7f, RGB(165, 165, 172), 1, 0.01f);
		foot_hints("ENTER  JOIN      TAB  SHOW PASSWORD      ESC  CANCEL");
		return;
	}
	if (wf.sel < wf.nap && wf.ap[wf.sel].saved) foot_hints("UP DOWN  MOVE      ENTER  JOIN      F  FORGET      R  RESCAN      ESC  SETTINGS");
	else foot_hints("UP DOWN  MOVE      ENTER  JOIN      R  RESCAN      ESC  SETTINGS");
}
