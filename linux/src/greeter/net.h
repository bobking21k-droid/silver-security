/*
 * Background downloads and the YouTube search state, for the greeter.
 *
 * A Job runs `silver-fetch` (which checks certificates) as a child process and collects its
 * output without ever blocking the interface. Thumbnails are decoded with stb_image and
 * cached as small RGB bitmaps. Under -DRENDER (the demo renderer, which runs on a fake clock)
 * downloads run to completion before the frame continues; with no downloader available a fixed
 * set of sample results stands in.
 */
#pragma once
#include <errno.h>
#include "yt.h"
#include "web.h"

typedef struct { int fd; pid_t pid; char *buf; size_t n, cap; int active, done, ok; } Job;

static void job_start(Job *j, char *const argv[], size_t maxbytes)
{
	memset(j, 0, sizeof *j);
	int p[2];
	if (pipe(p) < 0) { j->done = 1; return; }
	pid_t pid = fork();
	if (pid < 0) { close(p[0]); close(p[1]); j->done = 1; return; }
	if (pid == 0) {
		dup2(p[1], 1); close(p[0]); close(p[1]);
		int dn = open("/dev/null", O_RDWR); if (dn >= 0) { dup2(dn, 0); dup2(dn, 2); }
		execv(argv[0], argv); _exit(127);
	}
	close(p[1]);
	fcntl(p[0], F_SETFL, fcntl(p[0], F_GETFL) | O_NONBLOCK);
	j->fd = p[0]; j->pid = pid; j->active = 1; j->cap = maxbytes + 1;
	j->buf = malloc(j->cap);
	if (!j->buf) { j->done = 1; j->active = 0; close(p[0]); }
}
/* read what has arrived; sets done/ok when the child has finished */
static void job_poll(Job *j)
{
	if (!j->active || j->done) return;
	for (;;) {
		ssize_t r = read(j->fd, j->buf + j->n, j->cap - 1 - j->n);
		if (r > 0) { j->n += (size_t)r; if (j->n >= j->cap - 1) break; continue; }
		if (r < 0 && (errno == EAGAIN || errno == EINTR)) return;
		break;                                                    /* EOF, error or full */
	}
	int st = 0;
	close(j->fd); j->fd = -1;
	if (j->n >= j->cap - 1) kill(j->pid, SIGKILL);
	waitpid(j->pid, &st, 0);
	j->buf[j->n] = 0;
	j->ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
	j->done = 1;
}
static void job_wait(Job *j) { while (j->active && !j->done) { job_poll(j); if (!j->done) usleep(3000); } }
static void job_free(Job *j) { if (j->active && !j->done) { kill(j->pid, SIGKILL); waitpid(j->pid, 0, 0); if (j->fd >= 0) close(j->fd); } free(j->buf); memset(j, 0, sizeof *j); }

#define TW 160
#define TH 90
typedef struct { char id[16]; uint8_t *rgb; int w, h, state; Job job; int wide; } Thumb;       /* state 0 none 1 loading 2 ready 3 failed */
#define MAXTHUMB 96
static Thumb thumbs[MAXTHUMB];

static uint8_t *scale_rgb(const uint8_t *src, int sw, int sh, int cx, int cy, int cw, int ch, int dw, int dh)
{
	uint8_t *d = malloc((size_t)dw * dh * 3);
	if (!d) return 0;
	for (int y = 0; y < dh; y++)
		for (int x = 0; x < dw; x++) {
			float fx = cx + (x + 0.5f) * cw / dw - 0.5f, fy = cy + (y + 0.5f) * ch / dh - 0.5f;   /* bilinear / box-ish */
			int x0 = (int)fx, y0 = (int)fy; if (fx < 0) x0 = 0; if (fy < 0) y0 = 0;
			int x1 = x0 + 1 < sw ? x0 + 1 : x0, y1 = y0 + 1 < sh ? y0 + 1 : y0;
			float ax = fx - x0, ay = fy - y0; if (ax < 0) ax = 0; if (ay < 0) ay = 0;
			for (int c = 0; c < 3; c++) {
				float v = (src[(y0 * sw + x0) * 3 + c] * (1 - ax) + src[(y0 * sw + x1) * 3 + c] * ax) * (1 - ay) +
					  (src[(y1 * sw + x0) * 3 + c] * (1 - ax) + src[(y1 * sw + x1) * 3 + c] * ax) * ay;
				d[(y * dw + x) * 3 + c] = (uint8_t)v;
			}
		}
	return d;
}

static void thumb_poll(void);
static Thumb *thumb_get(const char *id, int wide)
{
	for (int i = 0; i < MAXTHUMB; i++) if (thumbs[i].state && !strcmp(thumbs[i].id, id) && thumbs[i].wide == wide) return &thumbs[i];
	return 0;
}
/* ask for a thumbnail (once); wide = the larger one for the watch page */
static Thumb *thumb_want_url(const char *id, int wide, const char *fetch, const char *src);
static Thumb *thumb_want(const char *id, int wide, const char *fetch) { return thumb_want_url(id, wide, fetch, 0); }
/* any picture by address (game art), cached under `id` */
static Thumb *thumb_want_url(const char *id, int wide, const char *fetch, const char *src)
{
	Thumb *t = thumb_get(id, wide);
	if (t) return t;
	int slot = -1;
	for (int i = 0; i < MAXTHUMB; i++) if (!thumbs[i].state) { slot = i; break; }
	if (slot < 0) return 0;
	t = &thumbs[slot]; memset(t, 0, sizeof *t);
	snprintf(t->id, sizeof t->id, "%s", id); t->wide = wide;
	if (!fetch || !fetch[0]) { t->state = 3; return t; }
	char url[400];
	if (src) snprintf(url, sizeof url, "%s", src);
	else snprintf(url, sizeof url, "https://i.ytimg.com/vi/%s/%s.jpg", id, wide ? "hqdefault" : "mqdefault");
	char *argv[] = {(char *)fetch, "-q", "-m", "400000", url, 0};
	job_start(&t->job, argv, 400000);
	t->state = t->job.active ? 1 : 3;
#ifdef RENDER
	job_wait(&t->job); thumb_poll();                         /* the demo clock waits for the download */
#endif
	return t;
}
static void thumb_poll(void)
{
	for (int i = 0; i < MAXTHUMB; i++) {
		Thumb *t = &thumbs[i];
		if (t->state != 1) continue;
		job_poll(&t->job);
		if (!t->job.done) continue;
		t->state = 3;
		if (t->job.ok) {
			int w, h, c;
			uint8_t *px = stbi_load_from_memory((const uint8_t *)t->job.buf, (int)t->job.n, &w, &h, &c, 3);
			if (px) {
				int cx = 0, cy = 0, cw = w, chh = h;
				if (h * 16 > w * 9) { chh = w * 9 / 16; cy = (h - chh) / 2; }                  /* cut to 16:9: hqdefault's black bars, square box art */
				else if (w * 9 > h * 16) { cw = h * 16 / 9; cx = (w - cw) / 2; }                /* Steam's wide headers */
				t->rgb = scale_rgb(px, w, h, cx, cy, cw, chh, t->wide ? 320 : TW, t->wide ? 180 : TH);
				t->w = t->wide ? 320 : TW; t->h = t->wide ? 180 : TH;
				if (t->rgb) t->state = 2;
				stbi_image_free(px);
			}
		}
		job_free(&t->job);
	}
}

/* draw a thumbnail (or a tinted placeholder) with rounded corners */
static void thumb_paint(Thumb *t, const char *id, float x, float y, float w, float h, float r);
static void thumb_draw(const char *id, int wide, const char *fetch, float x, float y, float w, float h, float r) { thumb_paint(thumb_want(id, wide, fetch), id, x, y, w, h, r); }
static void thumb_draw_url(const char *id, const char *url, int wide, const char *fetch, float x, float y, float w, float h, float r) { thumb_paint(thumb_want_url(id, wide, fetch, url), id, x, y, w, h, r); }
static void thumb_paint(Thumb *t, const char *id, float x, float y, float w, float h, float r)
{
	if (t && t->state == 2) {
		for (int py = 0; py < (int)h; py++) {
			int sy = fb.cy0 > (int)y + py ? -1 : (int)y + py;
			if (sy < 0 || sy >= fb.h) continue;
			for (int px = 0; px < (int)w; px++) {
				int sx = (int)x + px;
				if (sx < 0 || sx >= fb.w) continue;
				float dx = sg_abs(px + 0.5f - w / 2) - (w / 2 - r), dy = sg_abs(py + 0.5f - h / 2) - (h / 2 - r);
				float a = 1;
				if (dx > 0 && dy > 0) { float d = sg_sqrt(dx * dx + dy * dy) - r; a = sg_clamp(0.5f - d, 0, 1); }
				if (a <= 0) continue;
				int tx = px * t->w / (int)w, ty = py * t->h / (int)h;
				const uint8_t *s = t->rgb + (ty * t->w + tx) * 3;
				uint32_t *d = &fb.px[sy * fb.w + sx];
				uint32_t c = RGB(s[0], s[1], s[2]);
				if (a >= 1) *d = c;
				else { uint32_t o = *d; *d = RGB((int)(((o >> 16) & 255) * (1 - a) + s[0] * a), (int)(((o >> 8) & 255) * (1 - a) + s[1] * a), (int)((o & 255) * (1 - a) + s[2] * a)); }
			}
		}
		return;
	}
	uint32_t c = hash_color(id, 0.45f, 0.30f);                                 /* a placeholder, tinted per video */
	sg_rrect(&fb, x, y, w, h, r, 0, c, 1);
	sg_rrect(&fb, x, y, w, h * 0.5f, r, 0, WHITE, 0.05f);
	if (t && t->state == 1) {                                                   /* loading: a sweeping highlight */
		float ph = sg_fract((float)now() * 0.9f);
		sg_rrect(&fb, x + (w + h) * ph - h, y, h * 0.8f, h, r, 0, WHITE, 0.08f);
	}
	sg_rrect(&fb, x, y + h * 0.62f, w, h * 0.38f, r, 0, BLACK, 0.25f);
}

/* ---------------------------------------------------------------- the YouTube state */
enum { Y_IDLE, Y_LOADING, Y_RESULTS, Y_ERROR, Y_WATCH };
typedef struct {
	char q[96]; int state;
	Video v[MAXVID]; int nv;
	int mode;                     /* 0 the web, 1 YouTube videos */
	Web web[MAXWEB]; int nw;
	int sel, top, watch;          /* watch: index of the video whose page is open */
	Job job; char err[96];
	char fetch[64];
} Yt;
static Yt yt;

static void yt_mock(Yt *y);
static void yt_poll(Yt *y);
static int yt_count(const Yt *y) { return y->mode ? y->nv : y->nw; }
static void yt_search(Yt *y)
{
	job_free(&y->job);
	y->nv = 0; y->nw = 0; y->sel = 0; y->top = 0; y->err[0] = 0;
	if (!y->q[0]) { y->state = Y_IDLE; return; }
#ifdef RENDER
	if (!y->fetch[0] || access(y->fetch, X_OK)) { yt_mock(y); y->state = yt_count(y) ? Y_RESULTS : Y_ERROR; return; }
#endif
	char enc[300], url[400];
	url_encode(y->q, enc, sizeof enc);
	if (y->mode) {
		snprintf(url, sizeof url, "https://www.youtube.com/results?search_query=%s&hl=en", enc);
		char *argv[] = {y->fetch, "-q", "-m", "3000000", "-H", "Cookie: SOCS=CAI", url, 0};
		job_start(&y->job, argv, 3000000);
	} else {
		snprintf(url, sizeof url, "https://html.duckduckgo.com/html/?q=%s", enc);
		char *argv[] = {y->fetch, "-q", "-m", "1000000", url, 0};
		job_start(&y->job, argv, 1000000);
	}
	y->state = y->job.active ? Y_LOADING : Y_ERROR;
	if (y->state == Y_ERROR) snprintf(y->err, sizeof y->err, "Could not start the downloader");
#ifdef RENDER
	job_wait(&y->job); yt_poll(y);
	if (y->state != Y_RESULTS) { yt_mock(y); y->state = yt_count(y) ? Y_RESULTS : Y_ERROR; }
#endif
}
static void yt_poll(Yt *y)
{
	thumb_poll();
	if (y->state != Y_LOADING) return;
	job_poll(&y->job);
	if (!y->job.done) return;
	if (!y->job.ok) snprintf(y->err, sizeof y->err, "Can't reach the internet. Check your network.");
	else {
		if (y->mode) y->nv = yt_parse(y->job.buf, y->job.n, y->v, MAXVID);
		else y->nw = web_parse(y->job.buf, y->job.n, y->web, MAXWEB);
		if (!yt_count(y)) snprintf(y->err, sizeof y->err, "No results, or the search site changed its page.");
	}
	y->state = yt_count(y) ? Y_RESULTS : Y_ERROR;
	job_free(&y->job);
}

static void yt_mock(Yt *y)
{
	if (!y->mode) {
		static const struct { const char *t, *u, *s; } M[] = {
			{"What Happens When We Turn on Computer? - GeeksforGeeks", "https://www.geeksforgeeks.org/operating-systems/what-happens-when-we-turn-on-computer/", "The job of the boot loader is to load the actual operating system kernel into memory. After the boot loader runs, the kernel is loaded into RAM and starts the init process."},
			{"Booting - Wikipedia", "https://en.wikipedia.org/wiki/Booting", "Booting is the process of starting a computer. It can be initiated by hardware, such as a button press, or by a software command. Booting is complete when the runtime system is ready."},
			{"The Boot Process: 6 Stages from Power-On to Operating System", "https://www.thecoreitech.com/computer-basics/boot-process/", "The ordered sequence a computer runs from the moment you press the power button until the operating system is ready to use, in six stages."},
			{"Linux From Scratch", "https://www.linuxfromscratch.org/", "Linux From Scratch is a project that provides step-by-step instructions for building your own custom Linux system, entirely from source code."},
			{"How Does a Computer Boot? | Explained", "https://www.howtogeek.com/how-does-a-computer-boot/", "From firmware to bootloader to kernel to login screen: what really happens in the seconds after you press the power button."},
			{"UEFI vs BIOS: what is the difference?", "https://www.example.org/uefi-vs-bios", "UEFI replaces the legacy BIOS with a faster, more secure firmware interface that supports large disks and Secure Boot."},
		};
		y->nw = 0;
		for (unsigned i = 0; i < sizeof M / sizeof M[0]; i++) { Web *w = &y->web[y->nw++]; memset(w, 0, sizeof *w); snprintf(w->title, sizeof w->title, "%s", M[i].t); snprintf(w->url, sizeof w->url, "%s", M[i].u); snprintf(w->snip, sizeof w->snip, "%s", M[i].s); }
		return;
	}
	static const struct { const char *id, *title, *ch, *views, *len, *age, *desc; } M[] = {
		{"mock0001", "lofi hip hop radio - beats to relax/study to", "Lofi Girl", "12M views", "", "Streamed live", "Thank you for listening, I hope you have a good time here"},
		{"mock0002", "Chill Lofi Beats for Deep Focus & Coding", "Mellow Studio", "2.4M views", "3:12:08", "8 months ago", "Two hours of calm instrumentals made for long nights of work"},
		{"mock0003", "Rainy Night in Tokyo - Lofi Jazz Mix", "Quiet Hours", "880K views", "1:04:51", "2 years ago", "A soft mix of piano and vinyl crackle with rain on the window"},
		{"mock0004", "How an Operating System Boots, Explained", "Low Level Learning", "1.1M views", "14:26", "1 year ago", "From the BIOS to your login screen, step by step, with diagrams"},
		{"mock0005", "Building a Kernel From Scratch (Part 1)", "Bare Metal", "340K views", "28:40", "3 years ago", "We set up a cross compiler and print our first character"},
		{"mock0006", "Synthwave Drive - Neon Highway Mix", "Night Run", "5.2M views", "58:20", "5 years ago", "Retro synths for the road at night"},
		{"mock0007", "The Art of the Glitch", "Pixel Theory", "96K views", "9:03", "4 months ago", "How broken signals became a visual style"},
		{"mock0008", "Piano Study Music - Deep Concentration", "Calm Keys", "7.7M views", "2:00:00", "6 years ago", "Gentle piano for reading and focus"},
	};
	y->nv = 0;
	for (unsigned i = 0; i < sizeof M / sizeof M[0]; i++) {
		Video *v = &y->v[y->nv++]; memset(v, 0, sizeof *v);
		snprintf(v->id, sizeof v->id, "%s", M[i].id); snprintf(v->title, sizeof v->title, "%s", M[i].title);
		snprintf(v->channel, sizeof v->channel, "%s", M[i].ch); snprintf(v->views, sizeof v->views, "%s", M[i].views);
		snprintf(v->length, sizeof v->length, "%s", M[i].len); snprintf(v->age, sizeof v->age, "%s", M[i].age);
		snprintf(v->desc, sizeof v->desc, "%s", M[i].desc); v->live = !M[i].len[0];
	}
}

/* ---------------------------------------------------------------- downloading a file to ~/Downloads */
enum { DL_IDLE, DL_RUN, DL_DONE, DL_FAIL };
typedef struct { int state; Job job; char path[400], name[128]; double t0; } Dl;
static Dl dl;

/* a safe file name from the last part of a web address */
static void dl_name(const char *url, char *out, size_t n)
{
	const char *p = strstr(url, "://"); p = p ? p + 3 : url;
	const char *host = p, *slash = strchr(p, '/'), *last = slash ? slash : 0;
	for (const char *q = p; *q && *q != '?' && *q != '#'; q++) if (*q == '/' && q[1] && q[1] != '/' && q[1] != '?' && q[1] != '#') last = q;   /* the last non-empty part */
	size_t o = 0;
	if (last && last[1] && last[1] != '?' && last[1] != '#')
		for (const char *q = last + 1; *q && *q != '/' && *q != '?' && *q != '#' && o + 1 < n; q++) {
			char c = *q;
			out[o++] = (char)(((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_') ? c : '_');
		}
	if (!o) for (const char *q = host; *q && *q != '/' && o + 6 < n; q++) out[o++] = (char)(*q == '.' || *q == '-' ? *q : ((*q >= 'a' && *q <= 'z') || (*q >= '0' && *q <= '9') ? *q : '_'));
	out[o] = 0;
	if (!strchr(out, '.') || out[0] == '.') { if (o + 6 < n) strcat(out, ".html"); }
	if (out[0] == '.') out[0] = '_';
}
static void dl_start(const char *url)
{
	if (dl.state == DL_RUN) return;
	memset(&dl, 0, sizeof dl);
	dl.t0 = now();
	dl_name(url, dl.name, sizeof dl.name);
#ifdef RENDER
	dl.state = DL_RUN;
	snprintf(dl.path, sizeof dl.path, "%s/Downloads/%s", homedir, dl.name);
#else
	char dir[300]; snprintf(dir, sizeof dir, "%s/Downloads", homedir); mkdir(dir, 0755);
	snprintf(dl.path, sizeof dl.path, "%s/%s", dir, dl.name);
	for (int k = 1; access(dl.path, F_OK) == 0 && k < 100; k++) {        /* never overwrite: name (2).ext */
		char base[128]; snprintf(base, sizeof base, "%s", dl.name);
		char *dot = strrchr(base, '.'); char ext[32] = "";
		if (dot) { snprintf(ext, sizeof ext, "%s", dot); *dot = 0; }
		snprintf(dl.path, sizeof dl.path, "%s/%s (%d)%s", dir, base, k + 1, ext);
	}
	const char *sl = strrchr(dl.path, '/'); snprintf(dl.name, sizeof dl.name, "%s", sl ? sl + 1 : dl.path);
	char *argv[] = {"/sbin/silver-fetch", "-q", "-t", "1800", "-m", "2147483647", "-o", dl.path, (char *)url, 0};
	job_start(&dl.job, argv, 4096);
	dl.state = dl.job.active ? DL_RUN : DL_FAIL;
#endif
}
static long dl_bytes(void)
{
#ifdef RENDER
	return (long)((now() - dl.t0) * 180000);
#else
	struct stat st; return stat(dl.path, &st) == 0 ? (long)st.st_size : 0;
#endif
}
static void dl_poll(void)
{
	if (dl.state != DL_RUN) return;
#ifdef RENDER
	if (now() - dl.t0 > 1.6) dl.state = DL_DONE;
#else
	job_poll(&dl.job);
	if (!dl.job.done) return;
	dl.state = dl.job.ok ? DL_DONE : DL_FAIL;
	if (!dl.job.ok) unlink(dl.path);
	job_free(&dl.job);
#endif
}

/* ---------------------------------------------------------------- suggestions, trending, recent
 * Like a search engine's home page: as you type, suggestions come from Google's public suggest
 * service (the same one browsers use); before you type, today's trending searches (Google Trends'
 * public feed) and your own recent searches (kept in ~/.config/silver/search-history). */
#define MAXSUG 8
#define MAXTREND 10
#define MAXRECENT 12
typedef struct { char text[96], extra[24]; int kind; } SugItem;      /* kind: 0 suggestion, 1 recent, 2 trending */
static struct {
	char sug[MAXSUG][96]; int nsug; char for_q[96];
	char trend[MAXTREND][80], traffic[MAXTREND][16]; int ntrend, trend_state;
	char recent[MAXRECENT][96]; int nrecent, recent_loaded;
	Job sjob, tjob; double typed; int want, sel, hidden;
} sug;

static void recent_load(void)
{
	if (sug.recent_loaded) return;
	sug.recent_loaded = 1;
#ifdef RENDER
	static const char *demo[3] = {"linux from scratch", "how operating systems boot", "lofi beats"};
	for (int i = 0; i < 3; i++) snprintf(sug.recent[sug.nrecent++], 96, "%s", demo[i]);
#else
	char p[300], ln[128]; snprintf(p, sizeof p, "%s/.config/silver/search-history", homedir);
	FILE *f = fopen(p, "r");
	if (!f) return;
	while (sug.nrecent < MAXRECENT && fgets(ln, sizeof ln, f)) {
		size_t l = strlen(ln); while (l && (ln[l - 1] == '\n' || ln[l - 1] == '\r')) ln[--l] = 0;
		if (l) snprintf(sug.recent[sug.nrecent++], 96, "%s", ln);
	}
	fclose(f);
#endif
}
static void recent_add(const char *q)
{
	recent_load();
	int at = -1;
	for (int i = 0; i < sug.nrecent; i++) if (!strcasecmp(sug.recent[i], q)) at = i;
	if (at < 0) at = sug.nrecent < MAXRECENT ? sug.nrecent++ : MAXRECENT - 1;
	for (int i = at; i > 0; i--) memcpy(sug.recent[i], sug.recent[i - 1], 96);
	snprintf(sug.recent[0], 96, "%s", q);
#ifndef RENDER
	char p[300]; snprintf(p, sizeof p, "%s/.config", homedir); mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/.config/silver", homedir); mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/.config/silver/search-history", homedir);
	FILE *f = fopen(p, "w");
	if (f) { for (int i = 0; i < sug.nrecent; i++) fprintf(f, "%s\n", sug.recent[i]); fclose(f); }
#endif
}
static void recent_forget(int i)
{
	if (i < 0 || i >= sug.nrecent) return;
	for (int k = i; k + 1 < sug.nrecent; k++) memcpy(sug.recent[k], sug.recent[k + 1], 96);
	sug.nrecent--;
#ifndef RENDER
	char p[300]; snprintf(p, sizeof p, "%s/.config/silver/search-history", homedir);
	FILE *f = fopen(p, "w");
	if (f) { for (int k = 0; k < sug.nrecent; k++) fprintf(f, "%s\n", sug.recent[k]); fclose(f); }
#endif
}

/* the typed text changed: ask for suggestions a moment later (not on every key) */
static void sug_typed(void) { sug.typed = now(); sug.want = 1; sug.sel = -1; sug.hidden = 0; }

static void sug_parse(const char *js, size_t n)
{
	sug.nsug = 0;
	const char *lim = js + n, *p = memchr(js, '[', n);                        /* ["q",["s1","s2",...],...] */
	if (!p) return;
	p = memchr(p + 1, '[', (size_t)(lim - p - 1));
	if (!p) return;
	const char *e = json_end(p, lim);
	for (const char *q = p + 1; q && e && q < e && sug.nsug < MAXSUG;) {
		q = memchr(q, '"', (size_t)(e - q));
		if (!q) break;
		q = json_str(q, sug.sug[sug.nsug], 96);
		if (sug.sug[sug.nsug][0]) sug.nsug++;
	}
}
static void trend_parse(const char *x, size_t n)
{
	sug.ntrend = 0;
	const char *lim = x + n, *p = x;
	while (sug.ntrend < MAXTREND && (p = json_find(p, lim, "<item>"))) {
		const char *t = json_find(p, lim, "<title>"), *te = t ? json_find(t, lim, "</title>") : 0;
		if (!t || !te) break;
		size_t l = (size_t)(te - 8 - t); if (l > 79) l = 79;
		memcpy(sug.trend[sug.ntrend], t, l); sug.trend[sug.ntrend][l] = 0;
		const char *a = json_find(p, lim, "<ht:approx_traffic>"), *nx = json_find(p, lim, "</item>");
		sug.traffic[sug.ntrend][0] = 0;
		if (a && (!nx || a < nx)) { size_t k = 0; while (a[k] && a[k] != '<' && k < 15) { sug.traffic[sug.ntrend][k] = a[k]; k++; } sug.traffic[sug.ntrend][k] = 0; }
		sug.ntrend++;
		p = te;
	}
}
static void sug_poll(const char *q, const char *fetch, int tab_videos)
{
	if (sug.trend_state == 0 && fetch && fetch[0]) {                           /* trending: once */
		char *argv[] = {(char *)fetch, "-q", "-m", "400000", "https://trends.google.com/trending/rss?geo=US", 0};
		job_start(&sug.tjob, argv, 400000);
		sug.trend_state = sug.tjob.active ? 1 : 3;
#ifdef RENDER
		job_wait(&sug.tjob);
#endif
	}
	if (sug.trend_state == 1) { job_poll(&sug.tjob); if (sug.tjob.done) { if (sug.tjob.ok) trend_parse(sug.tjob.buf, sug.tjob.n); sug.trend_state = 2; job_free(&sug.tjob); } }
	if (sug.want && now() - sug.typed > 0.12 && !(sug.sjob.active && !sug.sjob.done)) {
		sug.want = 0;
		job_free(&sug.sjob);
		snprintf(sug.for_q, sizeof sug.for_q, "%s", q);
		if (!q[0]) { sug.nsug = 0; return; }
		if (!fetch || !fetch[0]) return;
		char enc[300], url[400]; url_encode(q, enc, sizeof enc);
		snprintf(url, sizeof url, "https://suggestqueries.google.com/complete/search?client=firefox&hl=en%s&q=%s", tab_videos ? "&ds=yt" : "", enc);
		char *argv[] = {(char *)fetch, "-q", "-m", "100000", url, 0};
		job_start(&sug.sjob, argv, 100000);
#ifdef RENDER
		job_wait(&sug.sjob);
#endif
	}
	if (sug.sjob.active) { job_poll(&sug.sjob); if (sug.sjob.done) { if (sug.sjob.ok && !strcmp(sug.for_q, q)) sug_parse(sug.sjob.buf, sug.sjob.n); job_free(&sug.sjob); } }
}
/* what the list under the box shows right now */
static int sug_items(const char *q, SugItem *out, int max)
{
	int n = 0;
	recent_load();
	if (q[0]) {
		for (int i = 0; i < sug.nrecent && n < 3; i++)                          /* your own matching searches first */
			if (!strncasecmp(sug.recent[i], q, strlen(q)) && strcasecmp(sug.recent[i], q)) { snprintf(out[n].text, 96, "%s", sug.recent[i]); out[n].extra[0] = 0; out[n].kind = 1; n++; }
		for (int i = 0; i < sug.nsug && n < max; i++) {
			int dup = 0; for (int k = 0; k < n; k++) if (!strcasecmp(out[k].text, sug.sug[i])) dup = 1;
			if (!dup && strcasecmp(sug.sug[i], q)) { snprintf(out[n].text, 96, "%s", sug.sug[i]); out[n].extra[0] = 0; out[n].kind = 0; n++; }
		}
	} else {
		for (int i = 0; i < sug.nrecent && n < 4; i++) { snprintf(out[n].text, 96, "%s", sug.recent[i]); out[n].extra[0] = 0; out[n].kind = 1; n++; }
		for (int i = 0; i < sug.ntrend && n < max; i++) { snprintf(out[n].text, 96, "%s", sug.trend[i]); snprintf(out[n].extra, 24, "%s", sug.traffic[i]); out[n].kind = 2; n++; }
	}
	return n;
}
