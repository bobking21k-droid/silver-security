/*
 * The Apps pane: a grid of apps, the Game Store (gstore.h) and "Me and my PC".
 *
 * Me and my PC: who you are and what you are running on, read live from /proc and /sys: the
 * computer's maker and model, processor, memory, storage, graphics and display, network, power
 * and battery, temperature, and the system itself.
 */
#pragma once
#include <sys/statvfs.h>

static void request(char c);
#define FETCH_BIN "/sbin/silver-fetch"

/* ================================================================ Me and my PC */
typedef struct {
	char model[96], maker[64], host[64];
	char cpu[96], cpu_sub[96], gpu[96], gpu_sub[96], disk[96], disk_sub[96], net[96], net_sub[96];
	char power[64], power_sub[96], temp[48], sys[96], sys_sub[96], boot[48];
	float disk_frac, bat_frac; int has_bat;
	float last;
} PcInfo;
static PcInfo pc;

static int read_line(const char *path, char *out, size_t n)
{
	out[0] = 0;
	FILE *f = fopen(path, "r");
	if (!f) return 0;
	if (!fgets(out, (int)n, f)) out[0] = 0;
	fclose(f);
	size_t l = strlen(out); while (l && (out[l - 1] == '\n' || out[l - 1] == ' ')) out[--l] = 0;
	return out[0] != 0;
}
static void human_bytes(double b, char *out, size_t n)
{
	const char *u[] = {"B", "KB", "MB", "GB", "TB"}; int k = 0;
	while (b >= 1000 && k < 4) { b /= 1000; k++; }
	snprintf(out, n, k >= 3 ? "%.1f %s" : "%.0f %s", b, u[k]);
}

static void pc_read(PcInfo *p, const Sys *sy)
{
#ifdef RENDER
	(void)sy;
	snprintf(p->model, sizeof p->model, "Standard PC (Q35 + ICH9, 2009)"); snprintf(p->maker, sizeof p->maker, "QEMU");
	snprintf(p->host, sizeof p->host, "silver");
	snprintf(p->cpu, sizeof p->cpu, "Intel Core i7-1260P"); snprintf(p->cpu_sub, sizeof p->cpu_sub, "12 cores  -  16 threads  -  up to 4.7 GHz");
	snprintf(p->gpu, sizeof p->gpu, "Intel Iris Xe Graphics"); snprintf(p->gpu_sub, sizeof p->gpu_sub, "%d x %d display", W * scale, H * scale);
	snprintf(p->disk, sizeof p->disk, "Samsung SSD 980 PRO  1.0 TB"); snprintf(p->disk_sub, sizeof p->disk_sub, "SilverOS uses 1.4 GB of 64 GB"); p->disk_frac = 0.022f;
	snprintf(p->net, sizeof p->net, "Wi-Fi  -  192.168.1.24"); snprintf(p->net_sub, sizeof p->net_sub, "wlan0  -  3c:58:c2:1f:9a:04");
	snprintf(p->power, sizeof p->power, "Battery 84%%"); snprintf(p->power_sub, sizeof p->power_sub, "Charging"); p->bat_frac = 0.84f; p->has_bat = 1;
	snprintf(p->temp, sizeof p->temp, "46\xC2\xB0" "C");
	snprintf(p->sys, sizeof p->sys, "SilverOS 0.1.0"); snprintf(p->sys_sub, sizeof p->sys_sub, "Linux 6.18.63-silver");
	snprintf(p->boot, sizeof p->boot, "UEFI  -  Secure Boot off");
#else
	char b[256];
	read_line("/sys/class/dmi/id/product_name", p->model, sizeof p->model);
	read_line("/sys/class/dmi/id/sys_vendor", p->maker, sizeof p->maker);
	if (!p->model[0] || !strcmp(p->model, "To Be Filled By O.E.M.")) snprintf(p->model, sizeof p->model, "This PC");
	gethostname(p->host, sizeof p->host);
	/* processor */
	FILE *f = fopen("/proc/cpuinfo", "r");
	int threads = 0, cores = 0; float mhz = 0;
	p->cpu[0] = 0;
	if (f) {
		while (fgets(b, sizeof b, f)) {
			char *v = strchr(b, ':'); if (!v) continue; v += 2;
			size_t l = strlen(v); while (l && (v[l - 1] == '\n' || v[l - 1] == ' ')) v[--l] = 0;
			if (!strncmp(b, "processor", 9)) threads++;
			else if (!strncmp(b, "model name", 10) && !p->cpu[0]) snprintf(p->cpu, sizeof p->cpu, "%s", v);
			else if (!strncmp(b, "cpu cores", 9)) cores = atoi(v);
			else if (!strncmp(b, "cpu MHz", 7) && !mhz) mhz = (float)atof(v);
		}
		fclose(f);
	}
	if (read_line("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", b, sizeof b)) mhz = (float)atof(b) / 1000;
	{ char *s = p->cpu, *d = p->cpu; for (; *s; s++) if (!(*s == ' ' && (d == p->cpu || d[-1] == ' '))) *d++ = *s; *d = 0; }
	if (!p->cpu[0]) snprintf(p->cpu, sizeof p->cpu, "x86-64 processor");
	snprintf(p->cpu_sub, sizeof p->cpu_sub, "%d cores  -  %d threads%s", cores ? cores : threads, threads, "");
	if (mhz > 0) { size_t l = strlen(p->cpu_sub); snprintf(p->cpu_sub + l, sizeof p->cpu_sub - l, "  -  %.1f GHz", mhz / 1000); }
	/* graphics: the first display controller on the PCI bus */
	p->gpu[0] = 0;
	DIR *dd = opendir("/sys/bus/pci/devices");
	if (dd) {
		struct dirent *e;
		while ((e = readdir(dd)) && !p->gpu[0]) {
			char path[300], cls[32], ven[32], dev[32];
			snprintf(path, sizeof path, "/sys/bus/pci/devices/%s/class", e->d_name);
			if (!read_line(path, cls, sizeof cls) || strncmp(cls, "0x03", 4)) continue;
			snprintf(path, sizeof path, "/sys/bus/pci/devices/%s/vendor", e->d_name); read_line(path, ven, sizeof ven);
			snprintf(path, sizeof path, "/sys/bus/pci/devices/%s/device", e->d_name); read_line(path, dev, sizeof dev);
			const char *vn = !strcmp(ven, "0x8086") ? "Intel" : !strcmp(ven, "0x10de") ? "NVIDIA" : !strcmp(ven, "0x1002") ? "AMD" :
					 !strcmp(ven, "0x1234") ? "QEMU standard VGA" : !strcmp(ven, "0x1af4") ? "Virtio GPU" : !strcmp(ven, "0x15ad") ? "VMware SVGA" : 0;
			if (vn) snprintf(p->gpu, sizeof p->gpu, "%s%s", vn, strstr(vn, "QEMU") || strstr(vn, "V") == vn ? "" : " graphics");
			else snprintf(p->gpu, sizeof p->gpu, "Graphics %s:%s", ven + 2, dev + 2);
		}
		closedir(dd);
	}
	if (!p->gpu[0]) snprintf(p->gpu, sizeof p->gpu, "Firmware display");
	char fbn[64] = ""; read_line("/sys/class/graphics/fb0/name", fbn, sizeof fbn);
	snprintf(p->gpu_sub, sizeof p->gpu_sub, "%d x %d display%s%s", W * scale, H * scale, fbn[0] ? "  -  " : "", fbn);
	/* storage: the first real disk, and how full the system is */
	p->disk[0] = 0;
	static const char *disks[] = {"nvme0n1", "sda", "vda", "mmcblk0", "hda"};
	for (unsigned k = 0; k < sizeof disks / sizeof disks[0] && !p->disk[0]; k++) {
		char path[128], model[96], sz[32];
		snprintf(path, sizeof path, "/sys/block/%s/size", disks[k]);
		if (!read_line(path, sz, sizeof sz)) continue;
		snprintf(path, sizeof path, "/sys/block/%s/device/model", disks[k]);
		if (!read_line(path, model, sizeof model)) snprintf(model, sizeof model, "%s", disks[k]);
		char hb[32]; human_bytes(atof(sz) * 512, hb, sizeof hb);
		snprintf(p->disk, sizeof p->disk, "%s  %s", model, hb);
	}
	if (!p->disk[0]) snprintf(p->disk, sizeof p->disk, "No disk found");
	struct statvfs vf;
	if (statvfs(homedir, &vf) == 0 && vf.f_blocks) {
		double tot = (double)vf.f_blocks * vf.f_frsize, fre = (double)vf.f_bavail * vf.f_frsize;
		char a[32], c[32]; human_bytes(tot - fre, a, sizeof a); human_bytes(tot, c, sizeof c);
		snprintf(p->disk_sub, sizeof p->disk_sub, "Your files: %s used of %s", a, c);
		p->disk_frac = (float)((tot - fre) / tot);
	}
	/* network */
	p->net[0] = 0; p->net_sub[0] = 0;
	struct ifaddrs *ifs, *a;
	if (getifaddrs(&ifs) == 0) {
		for (a = ifs; a; a = a->ifa_next) {
			if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || (a->ifa_flags & IFF_LOOPBACK)) continue;
			char ip[32]; inet_ntop(AF_INET, &((struct sockaddr_in *)a->ifa_addr)->sin_addr, ip, sizeof ip);
			char path[128], mac[32] = ""; snprintf(path, sizeof path, "/sys/class/net/%s/address", a->ifa_name); read_line(path, mac, sizeof mac);
			snprintf(path, sizeof path, "/sys/class/net/%s/wireless", a->ifa_name);
			snprintf(p->net, sizeof p->net, "%s  -  %s", access(path, F_OK) == 0 ? "Wi-Fi" : "Wired", ip);
			snprintf(p->net_sub, sizeof p->net_sub, "%s  -  %s", a->ifa_name, mac);
			break;
		}
		freeifaddrs(ifs);
	}
	if (!p->net[0]) snprintf(p->net, sizeof p->net, "Not connected");
	/* power */
	p->has_bat = 0; snprintf(p->power, sizeof p->power, "Plugged in"); snprintf(p->power_sub, sizeof p->power_sub, "No battery");
	if ((dd = opendir("/sys/class/power_supply"))) {
		struct dirent *e;
		while ((e = readdir(dd))) {
			char path[300], ty[32], cap[16], st[32];
			snprintf(path, sizeof path, "/sys/class/power_supply/%s/type", e->d_name);
			if (!read_line(path, ty, sizeof ty) || strcmp(ty, "Battery")) continue;
			snprintf(path, sizeof path, "/sys/class/power_supply/%s/capacity", e->d_name); read_line(path, cap, sizeof cap);
			snprintf(path, sizeof path, "/sys/class/power_supply/%s/status", e->d_name); read_line(path, st, sizeof st);
			p->has_bat = 1; p->bat_frac = (float)atoi(cap) / 100;
			snprintf(p->power, sizeof p->power, "Battery %s%%", cap); snprintf(p->power_sub, sizeof p->power_sub, "%s", st);
			break;
		}
		closedir(dd);
	}
	/* temperature: the warmest sensor */
	int best = -1;
	for (int z = 0; z < 16; z++) { char path[96], v[32]; snprintf(path, sizeof path, "/sys/class/thermal/thermal_zone%d/temp", z); if (read_line(path, v, sizeof v) && atoi(v) > best) best = atoi(v); }
	if (best > 0) snprintf(p->temp, sizeof p->temp, "%d\xC2\xB0" "C", best / 1000); else snprintf(p->temp, sizeof p->temp, "--");
	/* the system */
	struct utsname u;
	snprintf(p->sys, sizeof p->sys, "SilverOS 0.1.0");
	if (!uname(&u)) snprintf(p->sys_sub, sizeof p->sys_sub, "Linux %s", u.release);
	int uefi = access("/sys/firmware/efi", F_OK) == 0, sb = 0;
	f = fopen("/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c", "rb");
	if (f) { unsigned char v[5]; if (fread(v, 1, 5, f) == 5) sb = v[4]; fclose(f); }
	snprintf(p->boot, sizeof p->boot, "%s%s", uefi ? "UEFI" : "BIOS", uefi ? (sb ? "  -  Secure Boot on" : "  -  Secure Boot off") : "");
	(void)sy;
#endif
}

/* ================================================================ drawing */
typedef struct { const char *name, *desc; int icon, action; } AppTile;    /* action: 0 terminal 2 pc 3 monitor 4 about 5 game store */
static int app_tiles(AppTile *t, int max)
{
	static const AppTile fixed[5] = {
		{"Terminal", "A command line. Type exit to come back.", I_TERM, 0},
		{"Game Store", "Play here, or browse Steam and the Microsoft Store.", I_GAME, 5},
		{"Me and my PC", "You, and everything inside this computer.", I_PC, 2},
		{"System Monitor", "Live CPU and memory.", I_MONITOR, 3},
		{"About SilverOS", "Version and install notes.", I_INFO, 4}};
	int n = 0;
	for (int i = 0; i < 5 && n < max; i++) t[n++] = fixed[i];
	return n;
}

#define GRID_C 4
static void draw_app_grid(Home *h, float t)
{
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	title_block("APPS", "TERMINAL  -  GAMES  -  YOUR PC", I_APPS);
	AppTile tl[5]; int n = app_tiles(tl, 5);
	if (h->row >= n) h->row = n - 1;
	float lx = U * 6, top = U * 24, gap = U * 2.2f, tw = (W - U * 12 - gap * (GRID_C - 1)) / GRID_C, th = U * 19;
	int rows = (int)((H - U * 10 - top + gap) / (th + gap)), srow = h->row / GRID_C;
	if (srow < h->top) h->top = srow;
	if (srow >= h->top + rows) h->top = srow - rows + 1;
	for (int i = h->top * GRID_C; i < n && i < (h->top + rows) * GRID_C; i++) {
		int r = i / GRID_C - h->top, c = i % GRID_C, sel = i == h->row;
		float x = lx + c * (tw + gap), y = top + r * (th + gap) - (sel ? U * 0.6f : 0);
		if (sel) { sg_blob(&fb, x + tw / 2, y + th / 2 + U * 1.4f, tw * 0.55f, th * 0.6f, BLACK, 0.6f); sg_rrect(&fb, x, y, tw, th, U * 1.8f, 0, WHITE, 0.95f); }
		else ui_frost(x, y, tw, th, U * 1.8f, 0.55f);
		uint32_t fg = sel ? INK : WHITE;
		if (tl[i].icon >= 0) {
			sg_disc(&fb, x + U * 5.2f, y + U * 5.6f, U * 3.4f, sel ? INK : WHITE, sel ? 1 : 0.10f);
			icon(tl[i].icon, x + U * 5.2f, y + U * 5.6f, U * 1.9f, sel ? WHITE : WHITE, 1);
		} else ui_avatar(x + U * 5.2f, y + U * 5.6f, U * 3.4f, tl[i].name);
		if (sel) sg_rrect(&fb, x + tw - U * 3.2f, y + U * 2.4f, U * 1.2f, U * 1.2f, U * 0.6f, 0, RED, 1);
		char nm[64]; fit(tl[i].name, U * 2.6f, tw - U * 4, nm, sizeof nm);
		sg_text(&fb, &FT, x + U * 2.4f, y + U * 13.0f, nm, U * 2.6f, fg, 1, 0.0f);
		char ln[2][160]; int m = ui_wrap(tl[i].desc, U * 1.45f, tw - U * 4.8f, 2, ln);
		for (int k = 0; k < m; k++) sg_text(&fb, &FT, x + U * 2.4f, y + U * 15.6f + k * U * 2.0f, ln[k], U * 1.45f, sel ? RGB(80, 80, 88) : RGB(160, 160, 166), 1, 0.01f);
	}
	foot_hints("ARROWS  move      ENTER  open      ESC  home");
	(void)t;
}

static void pc_card(float x, float y, float w, float h, int ic, const char *title, const char *big, const char *sub, float bar, const char *stat)
{
	ui_frost(x, y, w, h, U * 1.8f, 0.58f);
	sg_disc(&fb, x + U * 3.6f, y + U * 3.8f, U * 2.0f, WHITE, 0.10f);
	icon(ic, x + U * 3.6f, y + U * 3.8f, U * 1.2f, WHITE, 1);
	cap_text(x + U * 6.6f, y + U * 4.2f, title, U * 0.95f, RGB(170, 170, 176), 1, 0);
	char ln[3][160]; int m = ui_wrap(big, U * 2.3f, w - U * 4.4f, 2, ln);
	for (int k = 0; k < m; k++) sg_text(&fb, &FT, x + U * 2.2f, y + U * 10.0f + k * U * 3.0f, ln[k], U * 2.3f, WHITE, 1, 0.0f);
	char sf[160]; fit(sub, U * 1.45f, w - U * 4.4f, sf, sizeof sf);
	sg_text(&fb, &FT, x + U * 2.2f, y + U * 10.0f + m * U * 3.0f + U * 0.6f, sf, U * 1.45f, RGB(170, 170, 176), 1, 0.01f);
	if (stat && *stat) {                                                  /* the headline number, big */
		float ss = U * 5.4f;
		while (sg_text_w(&FT, stat, ss, 0) > w - U * 4.4f && ss > U * 2) ss *= 0.92f;
		sg_text(&fb, &FT, x + U * 2.2f, y + h - (bar >= 0 ? U * 4.6f : U * 2.6f), stat, ss, WHITE, 1, -0.01f);
	}
	if (bar >= 0) {
		float bw = w - U * 4.4f, byy = y + h - U * 2.6f;
		sg_rrect(&fb, x + U * 2.2f, byy, bw, U * 0.8f, U * 0.4f, 0, WHITE, 0.12f);
		sg_rrect(&fb, x + U * 2.2f, byy, sg_max(U * 0.8f, bw * sg_sat(bar)), U * 0.8f, U * 0.4f, 0, bar > 0.85f ? RED : WHITE, 0.9f);
	}
}

static void draw_mypc(Home *h, float t)
{
	if (t - pc.last > 1.0f || pc.last == 0) { pc_read(&pc, &h->sys); pc.last = t > 0 ? t : 0.001f; }
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	/* you */
	float ax = U * 11, ay = U * 12.5f;
	sg_glow(&fb, ax, ay, U * 10, RED, 0.12f);
	ui_avatar(ax, ay, U * 6.0f, user);
	sg_text(&fb, &FD, U * 20, U * 12.0f, user, U * 6.8f, WHITE, 1, 0);
	char on[200]; snprintf(on, sizeof on, "on %s%s%s", pc.maker[0] ? pc.maker : "", pc.maker[0] ? " " : "", pc.model);
	char of[200]; fit(on, U * 2.2f, W - U * 30, of, sizeof of);
	sg_text(&fb, &FT, U * 20.2f, U * 16.6f, of, U * 2.2f, RGB(200, 200, 206), 1, 0.01f);
	char tag[160]; snprintf(tag, sizeof tag, "COMPUTER NAME  %s      %s", pc.host, pc.boot);
	for (char *q = tag; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
	cap_text(U * 20.2f, U * 20.0f, tag, U * 0.95f, RGB(150, 150, 156), 1, 0);
	/* the computer, in eight cards */
	Sys *s = &h->sys;
	float lx = U * 6, top = U * 25, gap = U * 2, cw = (W - U * 12 - gap * 3) / 4, ch = (H - U * 10 - top - gap) / 2;
	char mem[96], mems[96], cpu_sub[160], up[64];
	snprintf(mem, sizeof mem, "%.1f GB memory", s->mem_total / 1024.0);
	snprintf(mems, sizeof mems, "%.1f GB in use", s->mem_used / 1024.0);
	snprintf(cpu_sub, sizeof cpu_sub, "%s  -  %d%% busy", pc.cpu_sub, (int)(s->cpu + 0.5f));
	snprintf(up, sizeof up, "On for %ldh %02ldm  -  load %.2f", s->up / 3600, (s->up / 60) % 60, s->load);
	char sys2[160]; snprintf(sys2, sizeof sys2, "%s  -  %s", pc.sys_sub, up);
	char st_cpu[16], st_mem[32], st_disk[16], st_up[32], st_bat[16];
	snprintf(st_cpu, sizeof st_cpu, "%d%%", (int)(s->cpu + 0.5f));
	snprintf(st_mem, sizeof st_mem, "%.1f / %.0f GB", s->mem_used / 1024.0, s->mem_total / 1024.0 + 0.49);
	snprintf(st_disk, sizeof st_disk, "%d%% used", (int)(pc.disk_frac * 100 + 0.5f));
	snprintf(st_up, sizeof st_up, "%ldh %02ldm", s->up / 3600, (s->up / 60) % 60);
	snprintf(st_bat, sizeof st_bat, "%d%%", (int)(pc.bat_frac * 100 + 0.5f));
	char res[32]; snprintf(res, sizeof res, "%d x %d", W * scale, H * scale);
	pc_card(lx + 0 * (cw + gap), top, cw, ch, I_CHIP, "PROCESSOR", pc.cpu, pc.cpu_sub, s->cpu / 100, st_cpu);
	pc_card(lx + 1 * (cw + gap), top, cw, ch, I_RAM, "MEMORY", mem, mems, (float)s->mem_used / (s->mem_total ? s->mem_total : 1), st_mem);
	pc_card(lx + 2 * (cw + gap), top, cw, ch, I_DISK, "STORAGE", pc.disk, pc.disk_sub, pc.disk_frac, st_disk);
	pc_card(lx + 3 * (cw + gap), top, cw, ch, I_SCREEN, "GRAPHICS AND DISPLAY", pc.gpu, pc.gpu_sub, -1, res);
	float y2 = top + ch + gap;
	pc_card(lx + 0 * (cw + gap), y2, cw, ch, I_NET, "NETWORK", pc.net, pc.net_sub, -1, strcmp(pc.net, "Not connected") ? "Online" : "Offline");
	pc_card(lx + 1 * (cw + gap), y2, cw, ch, I_BATTERY, "POWER", pc.power, pc.power_sub, pc.has_bat ? pc.bat_frac : -1, pc.has_bat ? st_bat : "AC");
	pc_card(lx + 2 * (cw + gap), y2, cw, ch, I_MONITOR, "TEMPERATURE", "The warmest sensor", "in the computer, right now", -1, pc.temp);
	pc_card(lx + 3 * (cw + gap), y2, cw, ch, I_INFO, "SYSTEM", pc.sys, pc.sys_sub, -1, st_up);
	(void)cpu_sub; (void)sys2;
	foot_hints("ESC  back to apps");
}

#include "gstore.h"

/* keys for the whole Apps pane; returns 1 if it used the key */
static int apps_key(Home *h, int k, float t)
{
	(void)t;
	if (h->sub == 6) { if (game_key(k)) h->sub = 5; return 1; }            /* playing a SilverOS game */
	if (h->sub == 5) { int play = 0; if (gstore_key(k, &play)) { h->sub = 0; h->row = h->sub_from; } else if (play) h->sub = 6; return 1; }
	if (h->sub == 1 || h->sub == 2 || h->sub == 4) { if (k == K_ESC || k == K_BACK || k == K_LEFT) { h->sub = 0; h->row = h->sub_from; } return 1; }
	AppTile tl[5]; int n = app_tiles(tl, 5);
	if (k == K_ESC || k == K_BACK) { h->screen = 0; h->fade = 0; return 1; }
	if (k == K_LEFT && h->row > 0) h->row--;
	else if (k == K_RIGHT && h->row + 1 < n) h->row++;
	else if (k == K_UP && h->row >= GRID_C) h->row -= GRID_C;
	else if (k == K_DOWN && h->row + GRID_C < n) h->row += GRID_C;
	else if (k == K_DOWN && h->row / GRID_C < (n - 1) / GRID_C) h->row = n - 1;
	else if (k == K_ENTER) {
		int act = tl[h->row].action;
		h->sub_from = h->row;
		if (act == 0) request('T');
		else if (act == 2) { h->sub = 4; pc.last = 0; }
		else if (act == 3) h->sub = 1;
		else if (act == 4) h->sub = 2;
		else if (act == 5) { h->sub = 5; scores_load(); gs.open = 0; gs.focus = 1; }
	}
	return 1;
}

static void draw_apps(Home *h, float t)
{
	switch (h->sub) {
	case 1: draw_monitor(h, t); break;
	case 2: draw_about(h); break;
	case 4: draw_mypc(h, t); break;
	case 5: draw_gstore(h, t); break;
	case 6: { static float last; float dt = last > 0 && t > last ? t - last : 0; if (dt > 0.1f) dt = 0.1f; last = t; game_update(dt); game_draw(); break; }
	default: draw_app_grid(h, t); break;
	}
}
