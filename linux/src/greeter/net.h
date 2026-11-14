/*
 * Background downloads and the YouTube search state, for the greeter.
 *
 * A Job runs `silver-fetch` (which checks certificates) as a child process and collects its
 * output without ever blocking the interface. Thumbnails are decoded with stb_image and
 * cached as small RGB bitmaps. Under -DRENDER no network is used: a fixed set of sample
 * results with generated thumbnails stands in, so the demo is repeatable.
 */
#pragma once
#include <errno.h>
#include "yt.h"

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
static void job_free(Job *j) { if (j->active && !j->done) { kill(j->pid, SIGKILL); waitpid(j->pid, 0, 0); if (j->fd >= 0) close(j->fd); } free(j->buf); memset(j, 0, sizeof *j); }

#define TW 160
#define TH 90
typedef struct { char id[16]; uint8_t *rgb; int w, h, state; Job job; int wide; } Thumb;       /* state 0 none 1 loading 2 ready 3 failed */
#define MAXTHUMB 40
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

static Thumb *thumb_get(const char *id, int wide)
{
	for (int i = 0; i < MAXTHUMB; i++) if (thumbs[i].state && !strcmp(thumbs[i].id, id) && thumbs[i].wide == wide) return &thumbs[i];
	return 0;
}
/* ask for a thumbnail (once); wide = the larger one for the watch page */
static Thumb *thumb_want(const char *id, int wide, const char *fetch)
{
	Thumb *t = thumb_get(id, wide);
	if (t) return t;
	int slot = -1;
	for (int i = 0; i < MAXTHUMB; i++) if (!thumbs[i].state) { slot = i; break; }
	if (slot < 0) return 0;
	t = &thumbs[slot]; memset(t, 0, sizeof *t);
	snprintf(t->id, sizeof t->id, "%s", id); t->wide = wide;
#ifdef RENDER
	(void)fetch; t->state = 3;                               /* the mock fills these itself */
#else
	char url[160]; snprintf(url, sizeof url, "https://i.ytimg.com/vi/%s/%s.jpg", id, wide ? "hqdefault" : "mqdefault");
	char *argv[] = {(char *)fetch, "-q", "-m", "400000", url, 0};
	job_start(&t->job, argv, 400000);
	t->state = t->job.active ? 1 : 3;
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
				if (t->wide && h * 16 > w * 9) { chh = w * 9 / 16; cy = (h - chh) / 2; }    /* hqdefault has black bars */
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
static void thumb_draw(const char *id, int wide, const char *fetch, float x, float y, float w, float h, float r)
{
	Thumb *t = thumb_want(id, wide, fetch);
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
	int sel, top, watch;          /* watch: index of the video whose page is open */
	Job job; char err[96];
	char fetch[64];
} Yt;
static Yt yt;

static void yt_mock(Yt *y);
static void yt_search(Yt *y)
{
	job_free(&y->job);
	y->nv = 0; y->sel = 0; y->top = 0; y->err[0] = 0;
	if (!y->q[0]) { y->state = Y_IDLE; return; }
#ifdef RENDER
	yt_mock(y); y->state = Y_RESULTS;
#else
	char enc[300], url[400];
	url_encode(y->q, enc, sizeof enc);
	snprintf(url, sizeof url, "https://www.youtube.com/results?search_query=%s&hl=en", enc);
	char *argv[] = {y->fetch, "-q", "-m", "3000000", "-H", "Cookie: SOCS=CAI", "-H", "Accept-Language: en", url, 0};
	job_start(&y->job, argv, 3000000);
	y->state = y->job.active ? Y_LOADING : Y_ERROR;
	if (y->state == Y_ERROR) snprintf(y->err, sizeof y->err, "Could not start the downloader");
#endif
}
static void yt_poll(Yt *y)
{
	thumb_poll();
	if (y->state != Y_LOADING) return;
	job_poll(&y->job);
	if (!y->job.done) return;
	if (!y->job.ok) snprintf(y->err, sizeof y->err, "Can't reach YouTube. Check your network.");
	else {
		y->nv = yt_parse(y->job.buf, y->job.n, y->v, MAXVID);
		if (!y->nv) snprintf(y->err, sizeof y->err, "No results, or YouTube changed its page.");
	}
	y->state = y->nv ? Y_RESULTS : Y_ERROR;
	job_free(&y->job);
}

static void yt_mock(Yt *y)
{
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
