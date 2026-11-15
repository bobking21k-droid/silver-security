/*
 * The Apps pane: a grid of apps, the App Store, and "Me and my PC".
 *
 * App Store: every app in catalog.h is one statically linked program published by its own
 * project. Get downloads the release archive with silver-fetch into ~/.cache/silver, checks its
 * SHA-256 against the catalog (anything else is thrown away), unpacks it into
 * ~/.local/share/silver-apps/<id> and links the program into ~/.local/bin. No root needed, and
 * nothing outside your home folder changes. Opening an app runs it full screen in the terminal;
 * when it quits you are back here. U removes an installed app.
 *
 * Me and my PC: who you are and what you are running on, read live from /proc and /sys: the
 * computer's maker and model, processor, memory, storage, graphics and display, network, power
 * and battery, temperature, and the system itself.
 */
#pragma once
#include <sys/statvfs.h>
#include "catalog.h"

static void request(char c);
#define FETCH_BIN "/sbin/silver-fetch"

/* ================================================================ installing */
enum { IN_NONE, IN_DOWNLOAD, IN_UNPACK, IN_DONE, IN_FAIL };
typedef struct { int state; Job job; char msg[96]; float t0; } Inst;
static Inst inst[NCAT];
#ifdef RENDER
static int mock_installed[NCAT] = {[0] = 0, [2] = 1};
#endif

static void app_paths(int i, char *dl, size_t n1, char *dir, size_t n2)
{
	if (dl) snprintf(dl, n1, "%s/.cache/silver/%s.download", homedir, CATALOG[i].id);
	if (dir) snprintf(dir, n2, "%s/.local/share/silver-apps/%s", homedir, CATALOG[i].id);
}
static int app_installed(int i)
{
#ifdef RENDER
	return mock_installed[i];
#else
	char p[400]; snprintf(p, sizeof p, "%s/.local/bin/%s", homedir, CATALOG[i].cmd);
	return access(p, X_OK) == 0;
#endif
}
static void mkdirs(const char *path)
{
	char p[400]; snprintf(p, sizeof p, "%s", path);
	for (char *s = p + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
	mkdir(p, 0755);
}

/* the unpack step: a fixed script, the catalog's values passed as arguments (never pasted in) */
static const char *UNPACK_SH =
	"set -e\n"
	"dl=\"$1\"; dir=\"$2\"; sum=\"$3\"; kind=\"$4\"; prog=\"$5\"; cmd=\"$6\"\n"
	"[ \"$(sha256sum \"$dl\" | cut -d' ' -f1)\" = \"$sum\" ] || { rm -f \"$dl\"; echo checksum; exit 3; }\n"
	"rm -rf \"$dir.new\"; mkdir -p \"$dir.new\"\n"
	"case \"$kind\" in\n"
	"  tbz) bunzip2 -c \"$dl\" | tar -x -C \"$dir.new\" ;;\n"
	"  tgz) gunzip -c \"$dl\" | tar -x -C \"$dir.new\" ;;\n"
	"  zip) unzip -q -o \"$dl\" -d \"$dir.new\" ;;\n"
	"esac\n"
	"[ -f \"$dir.new/$prog\" ] || { echo missing; exit 4; }\n"
	"chmod 755 \"$dir.new/$prog\"\n"
	"rm -rf \"$dir\"; mv \"$dir.new\" \"$dir\"\n"
	"mkdir -p \"$HOME/.local/bin\"; ln -sf \"$dir/$prog\" \"$HOME/.local/bin/$cmd\"\n"
	"rm -f \"$dl\"\n";

static void app_install(int i, float t)
{
	Inst *s = &inst[i];
	if (s->state == IN_DOWNLOAD || s->state == IN_UNPACK) return;
	memset(s, 0, sizeof *s); s->t0 = t;
#ifdef RENDER
	s->state = IN_DOWNLOAD;
#else
	char dl[400], dir[400], cache[400];
	app_paths(i, dl, sizeof dl, dir, sizeof dir);
	snprintf(cache, sizeof cache, "%s/.cache/silver", homedir); mkdirs(cache);
	char lim[32]; snprintf(lim, sizeof lim, "%ld", CATALOG[i].size + 4096);
	char *argv[] = {FETCH_BIN, "-q", "-t", "900", "-m", lim, "-o", dl, (char *)CATALOG[i].url, 0};
	job_start(&s->job, argv, 4096);
	s->state = s->job.active ? IN_DOWNLOAD : IN_FAIL;
	if (s->state == IN_FAIL) snprintf(s->msg, sizeof s->msg, "Could not start the download");
#endif
}
static void app_remove(int i)
{
#ifdef RENDER
	mock_installed[i] = 0;
#else
	char dir[400], link[400]; app_paths(i, 0, 0, dir, sizeof dir);
	snprintf(link, sizeof link, "%s/.local/bin/%s", homedir, CATALOG[i].cmd);
	unlink(link);
	char *argv[] = {"/bin/rm", "-rf", dir, 0};
	Job j; job_start(&j, argv, 64); while (j.active && !j.done) { job_poll(&j); usleep(20000); } job_free(&j);
#endif
	memset(&inst[i], 0, sizeof inst[i]);
}
/* run an installed app full screen: the terminal session reads this file and starts it */
static void app_launch(const char *cmd)
{
#ifndef RENDER
	char p[400]; snprintf(p, sizeof p, "%s/.config/silver", homedir); mkdirs(p);
	snprintf(p, sizeof p, "%s/.config/silver/launch", homedir);
	FILE *f = fopen(p, "w");
	if (f) { fprintf(f, "%s\n", cmd); fclose(f); }
#endif
	(void)cmd;
	request('T');
}
static float app_progress(int i, float t)
{
	Inst *s = &inst[i];
#ifdef RENDER
	return sg_sat((t - s->t0) / 2.2f);
#else
	(void)t;
	char dl[400]; app_paths(i, dl, sizeof dl, 0, 0);
	struct stat st;
	return stat(dl, &st) == 0 ? sg_sat((float)st.st_size / (float)CATALOG[i].size) : 0;
#endif
}
static void inst_poll(float t)
{
	for (int i = 0; i < NCAT; i++) {
		Inst *s = &inst[i];
#ifdef RENDER
		if (s->state == IN_DOWNLOAD && t - s->t0 > 2.2f) s->state = IN_UNPACK;
		else if (s->state == IN_UNPACK && t - s->t0 > 3.0f) { s->state = IN_DONE; mock_installed[i] = 1; }
		continue;
#else
		(void)t;
		if (s->state != IN_DOWNLOAD && s->state != IN_UNPACK) continue;
		job_poll(&s->job);
		if (!s->job.done) continue;
		int ok = s->job.ok;
		char out[64]; snprintf(out, sizeof out, "%s", s->job.buf ? s->job.buf : "");
		job_free(&s->job);
		if (s->state == IN_DOWNLOAD) {
			if (!ok) { s->state = IN_FAIL; snprintf(s->msg, sizeof s->msg, "Download failed. Check your network."); continue; }
			char dl[400], dir[400]; app_paths(i, dl, sizeof dl, dir, sizeof dir);
			char *argv[] = {"/bin/sh", "-c", (char *)UNPACK_SH, "unpack", dl, dir, (char *)CATALOG[i].sha256, (char *)CATALOG[i].archive, (char *)CATALOG[i].path, (char *)CATALOG[i].cmd, 0};
			job_start(&s->job, argv, 4096);
			s->state = s->job.active ? IN_UNPACK : IN_FAIL;
		} else {
			s->state = ok ? IN_DONE : IN_FAIL;
			if (!ok) snprintf(s->msg, sizeof s->msg, strstr(out, "checksum") ? "The download did not match its checksum, so it was thrown away." : "Could not unpack the app.");
		}
#endif
	}
}

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
typedef struct { const char *name, *desc; int icon, action; } AppTile;    /* action: 0 terminal 1 store 2 pc 3 monitor 4 about, 100+i catalog app */
static int app_tiles(AppTile *t, int max)
{
	static const AppTile fixed[5] = {
		{"Terminal", "A command line. Type exit to come back.", I_TERM, 0},
		{"App Store", "Get apps. Checked before they install.", I_STORE, 1},
		{"Me and my PC", "You, and everything inside this computer.", I_PC, 2},
		{"System Monitor", "Live CPU and memory.", I_MONITOR, 3},
		{"About SilverOS", "Version and install notes.", I_INFO, 4}};
	int n = 0;
	for (int i = 0; i < 5 && n < max; i++) t[n++] = fixed[i];
	for (int i = 0; i < NCAT && n < max; i++) if (app_installed(i)) t[n++] = (AppTile){CATALOG[i].name, CATALOG[i].tag, -1, 100 + i};
	return n;
}

#define GRID_C 4
static void draw_app_grid(Home *h, float t)
{
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	title_block("APPS", "YOUR APPS  -  GET MORE IN THE APP STORE", I_APPS);
	AppTile tl[5 + NCAT]; int n = app_tiles(tl, 5 + NCAT);
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

static void host_of(const char *url, char *out, size_t n)
{
	const char *p = strstr(url, "://"); p = p ? p + 3 : url;
	size_t o = 0; int slashes = 0;
	for (; *p && o + 1 < n; p++) { if (*p == '/' && ++slashes > 2) break; out[o++] = *p; }
	out[o] = 0;
}

static void draw_store(Home *h, float t)
{
	inst_poll(t);
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	title_block("APP STORE", "FREE APPS  -  EVERY DOWNLOAD CHECKED", I_STORE);
	if (h->row >= NCAT) h->row = NCAT - 1;
	float lx = U * 6, top = U * 23, lw = W * 0.38f, rh = U * 8.2f, bot = H - U * 10;
	int vis = (int)((bot - top) / rh);
	if (h->row < h->top) h->top = h->row;
	if (h->row >= h->top + vis) h->top = h->row - vis + 1;
	ui_frost(lx - U * 1.2f, top - U * 1.2f, lw + U * 2.4f, vis * rh + U * 2.0f, U * 2.0f, 0.55f);
	for (int i = 0; i < vis && h->top + i < NCAT; i++) {
		int idx = h->top + i, sel = idx == h->row;
		const CatApp *a = &CATALOG[idx];
		float y = top + i * rh;
		if (sel) { sg_rrect(&fb, lx, y + U * 0.3f, lw, rh - U * 0.6f, U * 1.3f, 0, WHITE, 0.95f); sg_rrect(&fb, lx + U * 0.6f, y + rh * 0.28f, U * 0.42f, rh * 0.44f, U * 0.21f, 0, RED, 1); }
		ui_avatar(lx + U * 4.0f, y + rh / 2, U * 2.4f, a->name);
		sg_text(&fb, &FT, lx + U * 7.8f, y + rh * 0.46f, a->name, U * 2.2f, sel ? INK : WHITE, 1, 0.0f);
		char tg[96]; fit(a->tag, U * 1.4f, lw - U * 20, tg, sizeof tg);
		sg_text(&fb, &FT, lx + U * 7.8f, y + rh * 0.46f + U * 2.4f, tg, U * 1.4f, sel ? RGB(80, 80, 88) : RGB(160, 160, 166), 1, 0.01f);
		char pct[16]; snprintf(pct, sizeof pct, "%d%%", (int)(app_progress(idx, t) * 100));
		const char *chip = inst[idx].state == IN_DOWNLOAD ? pct : inst[idx].state == IN_UNPACK ? "CHECK" : app_installed(idx) ? "OPEN" : "GET";
		float cw = sg_text_w(&FC, chip, U * 0.95f, 0.2f) + U * 3.0f;
		sg_rrect(&fb, lx + lw - cw - U * 1.6f, y + rh / 2 - U * 1.6f, cw, U * 3.2f, U * 1.6f, 0, app_installed(idx) ? (sel ? INK : WHITE) : RED, app_installed(idx) ? (sel ? 0.9f : 0.14f) : 0.95f);
		sg_text(&fb, &FC, lx + lw - cw - U * 0.1f, y + rh / 2 + U * 0.36f, chip, U * 0.95f, WHITE, 1, 0.2f);
	}
	ui_scroll(lx + lw + U * 0.4f, top, vis * rh, h->top, vis, NCAT);
	/* the chosen app */
	const CatApp *a = &CATALOG[h->row];
	Inst *s = &inst[h->row];
	float px = lx + lw + U * 4, pw = W - U * 6 - px, py = top - U * 1.2f, ph = bot - py + U * 0.8f;
	ui_frost(px, py, pw, ph, U * 2.4f, 0.62f);
	ui_avatar(px + U * 7, py + U * 7.5f, U * 4.4f, a->name);
	sg_text(&fb, &FD, px + U * 13.5f, py + U * 9.2f, a->name, U * 5.4f, WHITE, 1, 0);
	float cx = ui_chip(px + U * 13.6f, py + U * 12.6f, a->kind, U * 0.9f, WHITE, WHITE, 0.12f);
	char sz[32]; human_bytes((double)a->size, sz, sizeof sz);
	char dsz[48]; snprintf(dsz, sizeof dsz, "%s DOWNLOAD", sz); for (char *q = dsz; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
	ui_chip(cx + U * 1.0f, py + U * 12.6f, dsz, U * 0.9f, WHITE, WHITE, 0.12f);
	sg_text(&fb, &FT, px + U * 3, py + U * 20, a->tag, U * 2.6f, WHITE, 1, 0.0f);
	char ln[5][160]; int m = ui_wrap(a->desc, U * 1.9f, pw - U * 6, 5, ln);
	for (int k = 0; k < m; k++) sg_text(&fb, &FT, px + U * 3, py + U * 24.6f + k * U * 2.8f, ln[k], U * 1.9f, RGB(200, 200, 206), 1, 0.01f);
	char host[160]; host_of(a->url, host, sizeof host);
	float iy = py + U * 26 + m * U * 2.8f + U * 3;
	cap_text(px + U * 3, iy, "FROM", U * 0.95f, RGB(150, 150, 156), 1, 0);
	sg_text(&fb, &FT, px + U * 12.5f, iy + U * 0.3f, host, U * 1.6f, WHITE, 1, 0.01f);
	cap_text(px + U * 3, iy + U * 3.4f, "CHECK", U * 0.95f, RGB(150, 150, 156), 1, 0);
	char sh[64]; snprintf(sh, sizeof sh, "SHA-256 %.16s...", a->sha256);
	sg_text(&fb, &FT, px + U * 12.5f, iy + U * 3.7f, sh, U * 1.6f, WHITE, 1, 0.01f);
	cap_text(px + U * 3, iy + U * 6.8f, "RUNS AS", U * 0.95f, RGB(150, 150, 156), 1, 0);
	sg_text(&fb, &FT, px + U * 12.5f, iy + U * 7.1f, a->cmd, U * 1.6f, WHITE, 1, 0.01f);
	/* the big button */
	float bx = px + U * 3, by = py + ph - U * 9.4f, bw = pw - U * 6, bh = U * 6;
	if (s->state == IN_DOWNLOAD || s->state == IN_UNPACK) {
		float pr = s->state == IN_UNPACK ? 1 : app_progress(h->row, t);
		sg_rrect(&fb, bx, by, bw, bh, bh / 2, 0, WHITE, 0.10f);
		sg_rrect(&fb, bx, by, sg_max(bh, bw * pr), bh, bh / 2, 0, RED, 0.9f);
		char lab[48];
		if (s->state == IN_UNPACK) snprintf(lab, sizeof lab, "CHECKING AND INSTALLING");
		else snprintf(lab, sizeof lab, "DOWNLOADING  %d%%", (int)(pr * 100));
		float lw2 = sg_text_w(&FC, lab, U * 1.1f, 0.22f);
		sg_text(&fb, &FC, bx + bw / 2 - lw2 / 2, by + bh / 2 + U * 0.4f, lab, U * 1.1f, WHITE, 1, 0.22f);
	} else {
		int on = app_installed(h->row);
		sg_rrect(&fb, bx, by, bw, bh, bh / 2, 0, on ? WHITE : RED, 0.95f);
		const char *lab = on ? "OPEN" : "GET";
		float lw2 = sg_text_w(&FC, lab, U * 1.3f, 0.22f);
		sg_text(&fb, &FC, bx + bw / 2 - lw2 / 2, by + bh / 2 + U * 0.48f, lab, U * 1.3f, on ? INK : WHITE, 1, 0.22f);
		if (s->state == IN_FAIL) sg_text(&fb, &FT, bx, by - U * 1.6f, s->msg, U * 1.6f, RGB(255, 120, 110), 1, 0.01f);
		else if (s->state == IN_DONE && on) sg_text(&fb, &FT, bx, by - U * 1.6f, "Installed and checked. Press Enter to open it.", U * 1.6f, RGB(170, 220, 170), 1, 0.01f);
	}
	foot_hints(app_installed(h->row) ? "UP DOWN  choose      ENTER  open      U  remove      ESC  back" : "UP DOWN  choose      ENTER  get      ESC  back");
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

/* keys for the whole Apps pane; returns 1 if it used the key */
static int apps_key(Home *h, int k, float t)
{
	if (h->sub == 1 || h->sub == 2 || h->sub == 4) { if (k == K_ESC || k == K_BACK || k == K_LEFT) { h->sub = 0; h->row = h->sub_from; } return 1; }
	if (h->sub == 3) {                                                     /* the App Store */
		if (k == K_ESC || k == K_BACK) { h->sub = 0; h->row = h->sub_from; return 1; }
		if (k == K_UP && h->row > 0) h->row--;
		else if (k == K_DOWN && h->row + 1 < NCAT) h->row++;
		else if (k == K_ENTER) {
			if (app_installed(h->row)) app_launch(CATALOG[h->row].cmd);
			else app_install(h->row, t);
		} else if ((k == 'u' || k == 'U') && app_installed(h->row)) app_remove(h->row);
		return 1;
	}
	AppTile tl[5 + NCAT]; int n = app_tiles(tl, 5 + NCAT);
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
		else if (act == 1) { h->sub = 3; h->row = 0; h->top = 0; }
		else if (act == 2) { h->sub = 4; pc.last = 0; }
		else if (act == 3) h->sub = 1;
		else if (act == 4) h->sub = 2;
		else app_launch(CATALOG[act - 100].cmd);
	}
	return 1;
}

static void draw_apps(Home *h, float t)
{
	switch (h->sub) {
	case 1: draw_monitor(h, t); break;
	case 2: draw_about(h); break;
	case 3: draw_store(h, t); break;
	case 4: draw_mypc(h, t); break;
	default: draw_app_grid(h, t); break;
	}
}
