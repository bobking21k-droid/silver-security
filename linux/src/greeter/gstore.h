/*
 * The Game Store, in three tabs:
 *
 *   SilverOS         games that play right here (games.h)
 *   Steam            Steam's real store: top sellers and new releases, or search, read from
 *                    store.steampowered.com's public API, with art
 *   Microsoft Store  the Microsoft Store's real catalogue, read from its public storefront API
 *
 * Steam and Microsoft Store games are Windows games, and SilverOS can't run them, so those
 * tabs show art, prices, discounts and ratings, with a wishlist (W, kept in
 * ~/.config/silver/wishlist). Buy shows the game's store address as a QR code: scan it with your
 * phone and buy it there, signed in to your own account (SilverOS never sees a password or a
 * card). It lands in your library, to play when you start Windows.
 *
 * Tab switches store, typing searches it, arrows move, Enter opens (or plays), Esc goes back.
 */
#pragma once
static void request_game(const char *action, const char *app);   /* (silver-greeter.c) start Steam */
static void snd(char ev);
static float steam_launch_t = -10; static char steam_launch_what[16];   /* (the demo: Steam starting) */
/* Steam runs right here when the games layer is in this build (Xorg, Mesa, the Steam app) */
static int steam_ready(void)
{
#ifdef RENDER
	return 1;
#else
	return access("/usr/lib/silver/steam-session", X_OK) == 0;
#endif
}
/* On the live USB, the home folder is in memory: whatever Steam downloads (its own ~1 GB, then the
 * games) would fill RAM. An installed SilverOS keeps it on the disk. */
static int silver_live(void)
{
#ifdef RENDER
	return 0;
#else
	return access("/etc/silver-installed", F_OK) != 0;
#endif
}
/* The first time: get Steam's launcher from Valve (repo.steampowered.com, about 20 MB) into
 * ~/.cache/steam_latest.deb; steam-session unpacks it, and Steam updates itself from then on. */
#define STEAM_DEB_URL "https://repo.steampowered.com/steam/archive/stable/steam_latest.deb"
static struct { int state; Job job; char part[400], deb[400], action[16], id[16]; } ss;   /* state: 0 idle, 1 getting it, 2 failed */
static int steam_have_launcher(void)
{
#ifdef RENDER
	return 1;
#else
	const char *home = getenv("HOME"); char p[400]; struct stat st;
	snprintf(p, sizeof p, "%s/.local/lib/steam-launcher/bin_steam.sh", home ? home : "/home/silver");
	if (access(p, X_OK) == 0) return 1;
	snprintf(p, sizeof p, "%s/.cache/steam_latest.deb", home ? home : "/home/silver");
	return stat(p, &st) == 0 && st.st_size > (1 << 20);
#endif
}
static void steam_go(const char *action, const char *id)
{
	if (steam_have_launcher()) { request_game(action, id); return; }
#ifndef RENDER
	if (ss.state == 1) return;
	const char *home = getenv("HOME"); if (!home) home = "/home/silver";
	char d[400]; snprintf(d, sizeof d, "%s/.cache", home); mkdir(d, 0755);
	snprintf(ss.deb, sizeof ss.deb, "%s/steam_latest.deb", d); snprintf(ss.part, sizeof ss.part, "%s.part", ss.deb);
	snprintf(ss.action, sizeof ss.action, "%s", action); snprintf(ss.id, sizeof ss.id, "%s", id);
	unlink(ss.part);
	char *argv[] = {"/sbin/silver-fetch", "-q", "-t", "600", "-m", "200000000", "-o", ss.part, STEAM_DEB_URL, 0};
	job_start(&ss.job, argv, 4096);
	ss.state = ss.job.active ? 1 : 2;
#endif
}
static void steam_poll(void)
{
#ifndef RENDER
	if (ss.state != 1) return;
	job_poll(&ss.job);
	if (!ss.job.done) return;
	struct stat st;
	if (ss.job.ok && stat(ss.part, &st) == 0 && st.st_size > (1 << 20) && rename(ss.part, ss.deb) == 0) { ss.state = 0; job_free(&ss.job); snd('r'); request_game(ss.action, ss.id); }
	else { ss.state = 2; job_free(&ss.job); unlink(ss.part); snd('e'); }
#endif
}
/* the panel while it downloads, or when it couldn't */
static int steam_setup_draw(float t)
{
	if (!ss.state) return 0;
	sg_rect(&fb, 0, 0, W, H, BLACK, 0.55f);
	float pw = W * 0.5f, ph = U * 18, px = (W - pw) / 2, py = (H - ph) / 2;
	ui_frost(px, py, pw, ph, U * 2.4f, 0.78f);
	if (ss.state == 1) {
		struct stat st; long got = stat(ss.part, &st) == 0 ? (long)st.st_size : 0;
		char m[80]; snprintf(m, sizeof m, "%.1f MB", got / 1048576.0);
		sg_text(&fb, &FT, px + U * 3, py + U * 5.2f, "Getting Steam from Valve ...", U * 2.8f, WHITE, 1, 0.01f);
		float bw = pw - U * 6, f = sg_sat(got / 21e6f);
		sg_rrect(&fb, px + U * 3, py + U * 8.5f, bw, U * 1.2f, U * 0.6f, 0, WHITE, 0.15f);
		sg_rrect(&fb, px + U * 3, py + U * 8.5f, sg_max(U * 1.2f, bw * f), U * 1.2f, U * 0.6f, 0, RED, 0.95f);
		sg_text(&fb, &FT, px + U * 3, py + U * 13.4f, m, U * 1.8f, RGB(180, 180, 186), 1, 0.01f);
		sg_text(&fb, &FT, px + U * 14, py + U * 13.4f, "Only the first time. Steam keeps itself up to date.", U * 1.6f, RGB(150, 150, 156), 0.5f + 0.5f * sg_sin(t * 2), 0.01f);
		foot_hints("ONE MOMENT");
	} else {
		sg_text(&fb, &FT, px + U * 3, py + U * 5.2f, "Couldn't get Steam.", U * 2.8f, WHITE, 1, 0.01f);
		sg_text(&fb, &FT, px + U * 3, py + U * 9.4f, "Check the network in Settings, then try again.", U * 1.9f, RGB(190, 190, 196), 1, 0.01f);
		foot_hints("ENTER  try again      ESC  close");
	}
	return 1;
}
/* is this Steam game installed? Steam keeps a small manifest per installed game */
static int steam_installed(const char *appid)
{
#ifdef RENDER
	return !strcmp(appid, "1145360");                    /* (the demo pretends Hades is installed) */
#else
	char p[400]; const char *home = getenv("HOME");
	snprintf(p, sizeof p, "%s/.local/share/Steam/steamapps/appmanifest_%s.acf", home ? home : "/home/silver", appid);
	return access(p, F_OK) == 0;
#endif
}
#include "games.h"
#include "qr.h"

enum { GS_SILVER, GS_STEAM, GS_MS, GS_N };
static const char *GS_NAME[GS_N] = {"SILVEROS", "STEAM", "MICROSOFT STORE"};

typedef struct {
	char id[16], key[20], name[120], price[24], was[24], publisher[64], rating[16], tag[48], img[400], desc[400];
	int discount, win, mac, lin;
} Game;
#define MAXGAME 32
#define GSC 5                                   /* cards across */

typedef struct {
	int state;                     /* Y_IDLE, Y_LOADING, Y_RESULTS, Y_ERROR (reused from net.h) */
	Game g[MAXGAME]; int n;
	char q[64], shown_q[64], err[96];
	Job job;
	int sel, top;
	int detail; Job djob; int dstate;    /* the open game's page, and its extra details */
	int buying;                          /* the Buy panel is open */
} GStore;
static struct { int tab, focus, open; GStore s[GS_N]; float seg, wide[GS_N]; } gs;     /* open: inside one store (else the three-way front screen) */

/* ---------------------------------------------------------------- wishlist */
static int wish_has(const char *key)
{
#ifdef RENDER
	return !strcmp(key, "s1145360");
#else
	char p[300], ln[200]; snprintf(p, sizeof p, "%s/.config/silver/wishlist", homedir);
	FILE *f = fopen(p, "r"); int hit = 0;
	if (f) { size_t kl = strlen(key); while (fgets(ln, sizeof ln, f)) if (!strncmp(ln, key, kl) && (ln[kl] == ' ' || ln[kl] == '\n')) hit = 1; fclose(f); }
	return hit;
#endif
}
static void wish_toggle(const Game *g)
{
#ifndef RENDER
	char p[300], tmp[310], ln[200]; snprintf(p, sizeof p, "%s/.config/silver", homedir); mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/.config/silver/wishlist", homedir); snprintf(tmp, sizeof tmp, "%s.new", p);
	int had = wish_has(g->key);
	FILE *in = fopen(p, "r"), *out = fopen(tmp, "w");
	if (!out) { if (in) fclose(in); return; }
	size_t kl = strlen(g->key);
	if (in) { while (fgets(ln, sizeof ln, in)) if (!(had && !strncmp(ln, g->key, kl) && (ln[kl] == ' ' || ln[kl] == '\n'))) fputs(ln, out); fclose(in); }
	if (!had) fprintf(out, "%s %s\n", g->key, g->name);
	fclose(out); rename(tmp, p);
#else
	(void)g;
#endif
}

/* ---------------------------------------------------------------- reading the stores */
static long json_num(const char *obj, const char *lim, const char *key)
{
	const char *p = json_find(obj, lim, key);
	return p ? atol(p) : -1;
}
static int json_bool(const char *obj, const char *lim, const char *key)
{
	const char *p = json_find(obj, lim, key);
	return p && !strncmp(p, "true", 4);
}
static void json_s(const char *obj, const char *lim, const char *key, char *out, size_t n)
{
	out[0] = 0;
	const char *p = json_find(obj, lim, key);
	if (p) json_str(p - 1, out, n);
}

static void money(long cents, char *out, size_t n) { if (cents <= 0) snprintf(out, n, "Free"); else snprintf(out, n, "$%ld.%02ld", cents / 100, cents % 100); }

/* Steam: featuredcategories (top sellers, deals, new releases) */
static int steam_featured(const char *js, size_t len, Game *g, int max)
{
	int n = 0;
	static const char *sec[3] = {"\"top_sellers\":", "\"specials\":", "\"new_releases\":"};
	static const char *tags[3] = {"TOP SELLER", "DEAL", "NEW"};
	const char *lim = js + len;
	for (int s = 0; s < 3 && n < max; s++) {
		const char *p = json_find(js, lim, sec[s]);
		if (!p) continue;
		const char *items = json_find(p, lim, "\"items\":[");
		if (!items) continue;
		const char *end = json_end(items - 1, lim);
		for (const char *q = items; q && q < end && n < max;) {
			const char *o = memchr(q, '{', (size_t)(end - q));
			if (!o) break;
			const char *oe = json_end(o, end);
			if (!oe) break;
			Game x; memset(&x, 0, sizeof x);
			long id = json_num(o, oe, "\"id\":");
			json_s(o, oe, "\"name\":\"", x.name, sizeof x.name);
			int dup = 0; for (int i = 0; i < n; i++) if (g[i].id[0] && atol(g[i].id) == id) dup = 1;
			if (id > 0 && x.name[0] && !dup && strncmp(x.name, "Steam ", 6)) {                      /* (skip Steam's own hardware) */
				snprintf(x.id, sizeof x.id, "%ld", id); snprintf(x.key, sizeof x.key, "s%ld", id);
				long fin = json_num(o, oe, "\"final_price\":"), orig = json_num(o, oe, "\"original_price\":");
				money(fin, x.price, sizeof x.price);
				x.discount = (int)json_num(o, oe, "\"discount_percent\":");
				if (x.discount > 0 && orig > 0) money(orig, x.was, sizeof x.was);
				x.win = json_bool(o, oe, "\"windows_available\":"); x.mac = json_bool(o, oe, "\"mac_available\":"); x.lin = json_bool(o, oe, "\"linux_available\":");
				json_s(o, oe, "\"header_image\":\"", x.img, sizeof x.img);
				snprintf(x.tag, sizeof x.tag, "%s", tags[s]);
				g[n++] = x;
			}
			q = oe;
		}
	}
	return n;
}
/* Steam: storesearch */
static int steam_search(const char *js, size_t len, Game *g, int max)
{
	int n = 0;
	const char *lim = js + len, *items = json_find(js, lim, "\"items\":[");
	if (!items) return 0;
	const char *end = json_end(items - 1, lim);
	for (const char *q = items; q && end && q < end && n < max;) {
		const char *o = memchr(q, '{', (size_t)(end - q));
		if (!o) break;
		const char *oe = json_end(o, end);
		if (!oe) break;
		Game x; memset(&x, 0, sizeof x);
		long id = json_num(o, oe, "\"id\":");
		json_s(o, oe, "\"name\":\"", x.name, sizeof x.name);
		if (id > 0 && x.name[0]) {
			snprintf(x.id, sizeof x.id, "%ld", id); snprintf(x.key, sizeof x.key, "s%ld", id);
			const char *pr = json_find(o, oe, "\"price\":{");
			if (pr) {
				const char *pe = json_end(pr - 1, oe);
				long fin = json_num(pr, pe, "\"final\":"), ini = json_num(pr, pe, "\"initial\":");
				money(fin, x.price, sizeof x.price);
				if (ini > fin && ini > 0) { money(ini, x.was, sizeof x.was); x.discount = (int)(100 - fin * 100 / ini); }
			} else snprintf(x.price, sizeof x.price, "Free");
			x.win = json_bool(o, oe, "\"windows\":"); x.mac = json_bool(o, oe, "\"mac\":"); x.lin = json_bool(o, oe, "\"linux\":");
			snprintf(x.img, sizeof x.img, "https://cdn.akamai.steamstatic.com/steam/apps/%ld/header.jpg", id);
			char ms[16]; json_s(o, oe, "\"metascore\":\"", ms, sizeof ms);
			if (ms[0]) snprintf(x.rating, sizeof x.rating, "Metascore %s", ms);
			g[n++] = x;
		}
		q = oe;
	}
	return n;
}
/* Microsoft Store: storefront search results (games only, no add-ons or trailers) */
static int ms_parse(const char *js, size_t len, Game *g, int max)
{
	int n = 0;
	const char *lim = js + len, *p = js;
	while (n < max && (p = json_find(p, lim, "\"ProductId\":\""))) {
		const char *o = p; int depth = 0;                          /* back up to the start of this card */
		while (o > js) { o--; if (*o == '}') depth++; else if (*o == '{') { if (depth == 0) break; depth--; } }
		const char *oe = json_end(o, lim);
		if (!oe) break;
		Game x; memset(&x, 0, sizeof x);
		json_s(o, oe, "\"ProductId\":\"", x.id, sizeof x.id);
		json_s(o, oe, "\"Title\":\"", x.name, sizeof x.name);
		char fam[32]; json_s(o, oe, "\"ProductFamilyName\":\"", fam, sizeof fam);
		int addon = json_find(o, oe, "\"Add-on\"") != 0, dup = 0;
		for (int i = 0; i < n; i++) if (!strcmp(g[i].id, x.id)) dup = 1;
		if (x.id[0] && x.name[0] && !strcmp(fam, "Games") && !addon && !dup) {
			snprintf(x.key, sizeof x.key, "m%s", x.id);
			json_s(o, oe, "\"DisplayPrice\":\"", x.price, sizeof x.price);
			if (!strcmp(x.price, "Included")) snprintf(x.price, sizeof x.price, "Game Pass");
			json_s(o, oe, "\"PublisherName\":\"", x.publisher, sizeof x.publisher);
			const char *ar = json_find(o, oe, "\"AverageRating\":");
			if (ar && atof(ar) > 0) snprintf(x.rating, sizeof x.rating, "%.1f stars", atof(ar));
			const char *cat = json_find(o, oe, "\"Categories\":[\"");
			if (cat) json_str(cat - 1, x.tag, sizeof x.tag);
			json_s(o, oe, "\"LongDescription\":\"", x.desc, sizeof x.desc);
			/* the widest art: hero art, else box art, as a small JPEG */
			static const char *pref[3] = {"\"SuperHeroArt\"", "\"BoxArt\"", "\"Poster\""};
			for (int k = 0; k < 3 && !x.img[0]; k++) {
				const char *t = json_find(o, oe, pref[k]);
				if (!t) continue;
				const char *io = t; while (io > o && *io != '{') io--;
				const char *ie = json_end(io, oe);
				char u[300]; json_s(io, ie ? ie : oe, "\"Url\":\"", u, sizeof u);
				if (u[0]) snprintf(x.img, sizeof x.img, "%s?w=320&h=180&format=jpg&mode=crop", u);
			}
			x.win = 1;
			g[n++] = x;
		}
		p = oe;
	}
	return n;
}

#ifdef RENDER
/* the demo, without a network: a few sample entries so it still walks through every screen */
static void gs_mock(int tab)
{
	GStore *s = &gs.s[tab];
	static const char *sprice[6] = {"$24.99", "$9.99", "$14.99", "$4.99", "$14.99", "$9.99"}, *mprice[6] = {"$29.99", "Free", "Free", "$59.99", "Game Pass", "Free"};
	static const char *steam[6][3] = {{"1145360", "Hades", "Supergiant Games"}, {"620", "Portal 2", "Valve"}, {"413150", "Stardew Valley", "ConcernedApe"},
					  {"1794680", "Vampire Survivors", "poncle"}, {"367520", "Hollow Knight", "Team Cherry"}, {"105600", "Terraria", "Re-Logic"}};
	static const char *ms[6][3] = {{"9NBLGGH2JHXJ", "Minecraft for Windows", "Mojang Studios"}, {"9WZDNCRFHWD2", "Microsoft Solitaire Collection", "Xbox Game Studios"},
				       {"BT5P2X999VH2", "Fortnite", "Epic Games Inc."}, {"9NBLGGH537BL", "Forza Horizon 5", "Xbox Game Studios"},
				       {"9P2N57MC619K", "Sea of Thieves", "Xbox Game Studios"}, {"9MZ11KT5KLP6", "Halo Infinite", "Xbox Game Studios"}};
	s->n = 0;
	for (int i = 0; i < 6; i++) {
		Game *x = &s->g[s->n++]; memset(x, 0, sizeof *x);
		const char *(*e)[3] = tab == GS_STEAM ? steam : ms;
		snprintf(x->id, sizeof x->id, "%s", e[i][0]); snprintf(x->key, sizeof x->key, "%c%s", tab == GS_STEAM ? 's' : 'm', e[i][0]);
		snprintf(x->name, sizeof x->name, "%s", e[i][1]); snprintf(x->publisher, sizeof x->publisher, "%s", e[i][2]);
		snprintf(x->desc, sizeof x->desc, "A sample entry: this demo was made without a network, so the real store could not be read.");
		x->win = 1; snprintf(x->tag, sizeof x->tag, "%s", tab == GS_STEAM ? "TOP SELLER" : "Action & adventure");
		snprintf(x->price, sizeof x->price, "%s", tab == GS_STEAM ? sprice[i] : mprice[i]);
		if (tab == GS_STEAM && i == 0) { x->discount = 40; snprintf(x->price, sizeof x->price, "$14.99"); snprintf(x->was, sizeof x->was, "$24.99"); }
	}
	s->state = Y_RESULTS;
}
#endif

static void gs_poll(void);
static void gs_load(int tab)
{
	GStore *s = &gs.s[tab];
	if (tab == GS_SILVER) return;
	job_free(&s->job);
	s->n = 0; s->sel = 0; s->top = 0; s->err[0] = 0; s->detail = 0;
	memcpy(s->shown_q, s->q, sizeof s->shown_q); s->shown_q[sizeof s->shown_q - 1] = 0;
	char enc[200], url[500];
	url_encode(s->q, enc, sizeof enc);
	if (tab == GS_STEAM) {
		if (s->q[0]) snprintf(url, sizeof url, "https://store.steampowered.com/api/storesearch/?term=%s&cc=us&l=en", enc);
		else snprintf(url, sizeof url, "https://store.steampowered.com/api/featuredcategories?cc=us&l=en");
	} else snprintf(url, sizeof url, "https://storeedgefd.dsx.mp.microsoft.com/v9.0/search?market=US&locale=en-us&deviceFamily=Windows.Desktop&mediaType=games&query=%s", s->q[0] ? enc : "popular+games");
	const char *fetch = yt.fetch[0] ? yt.fetch : FETCH_BIN;
#ifdef RENDER
	if (access(fetch, X_OK)) { gs_mock(tab); return; }
#endif
	char *argv[] = {(char *)fetch, "-q", "-m", "4000000", url, 0};
	job_start(&s->job, argv, 4000000);
	s->state = s->job.active ? Y_LOADING : Y_ERROR;
#ifdef RENDER
	job_wait(&s->job);
	gs_poll();
	if (s->state != Y_RESULTS) gs_mock(tab);
#endif
}
static void gs_poll(void)
{
	for (int tab = 1; tab < GS_N; tab++) {
		GStore *s = &gs.s[tab];
		if (s->state == Y_LOADING) {
			job_poll(&s->job);
			if (s->job.done) {
				if (!s->job.ok) snprintf(s->err, sizeof s->err, "Can't reach the store. Check your network.");
				else if (tab == GS_STEAM) s->n = s->shown_q[0] ? steam_search(s->job.buf, s->job.n, s->g, MAXGAME) : steam_featured(s->job.buf, s->job.n, s->g, MAXGAME);
				else s->n = ms_parse(s->job.buf, s->job.n, s->g, MAXGAME);
				if (s->job.ok && !s->n) snprintf(s->err, sizeof s->err, "Nothing found.");
				s->state = s->n ? Y_RESULTS : Y_ERROR;
				job_free(&s->job);
				if (s->state == Y_RESULTS) snd('r');
			}
		}
		if (s->dstate == 1) {                                        /* Steam: the open game's description */
			job_poll(&s->djob);
			if (s->djob.done) {
				Game *g = &s->g[s->sel];
				if (s->djob.ok) {
					const char *lim = s->djob.buf + s->djob.n;
					json_s(s->djob.buf, lim, "\"short_description\":\"", g->desc, sizeof g->desc);
					const char *dv = json_find(s->djob.buf, lim, "\"developers\":[\"");
					if (dv) json_str(dv - 1, g->publisher, sizeof g->publisher);
					const char *gn = json_find(s->djob.buf, lim, "\"genres\":[");
					if (gn) { const char *d = json_find(gn, lim, "\"description\":\""); if (d) json_str(d - 1, g->tag, sizeof g->tag); }
				}
				s->dstate = 2; job_free(&s->djob);
			}
		}
	}
}
static void gs_open_detail(GStore *s, int tab)
{
	s->detail = 1;
	Game *g = &s->g[s->sel];
	if (tab == GS_STEAM && !g->desc[0]) {
		char url[200]; snprintf(url, sizeof url, "https://store.steampowered.com/api/appdetails?appids=%s&cc=us&l=en&filters=basic,genres,developers", g->id);
		const char *fetch = yt.fetch[0] ? yt.fetch : FETCH_BIN;
		char *argv[] = {(char *)fetch, "-q", "-m", "2000000", url, 0};
		job_start(&s->djob, argv, 2000000);
		s->dstate = s->djob.active ? 1 : 2;
#ifdef RENDER
		job_wait(&s->djob); gs_poll();
#endif
	}
}

/* ---------------------------------------------------------------- the dancer
 * A little figure dancing along the bottom of the Game Store: frames from
 * /usr/share/silver/dancer.sprite (made by scripts/make-dancer.py from a clip you supply; not in
 * git). Without the file, nobody dances. */
static struct { int w, h, n, fps, tried; uint8_t *px; } dancer;
static void dancer_load(void)
{
	if (dancer.tried) return;
	dancer.tried = 1;
	const char *path = getenv("SILVER_DANCER");
	if (!path) path = "/usr/share/silver/dancer.sprite";
	/* mapped, not copied (see clip_load): the 4 MB of frames stay on the stick or disk until used */
	int fd = open(path, O_RDONLY);
	if (fd < 0) return;
	struct stat st; void *m = MAP_FAILED;
	if (fstat(fd, &st) == 0 && st.st_size >= 12 && st.st_size < (64L << 20)) m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (m == MAP_FAILED) return;
	const unsigned char *hd = m;
	if (!memcmp(hd, "SPRT", 4)) {
		dancer.w = hd[4] | hd[5] << 8; dancer.h = hd[6] | hd[7] << 8; dancer.n = hd[8] | hd[9] << 8; dancer.fps = hd[10] | hd[11] << 8;
		size_t sz = (size_t)dancer.w * dancer.h * 4 * dancer.n;
		if (dancer.w && dancer.h && dancer.n && dancer.fps && 12 + sz <= (size_t)st.st_size) { dancer.px = (uint8_t *)m + 12; return; }
	}
	munmap(m, (size_t)st.st_size);
}
/* draw the dancer with its feet on y, centred on x, `hgt` pixels tall (blocky, like the original) */
static void dancer_draw(float x, float y, float hgt, float t)
{
	dancer_load();
	if (!dancer.px) return;
	int fi = (int)(t * dancer.fps) % dancer.n;
	const uint8_t *src = dancer.px + (size_t)fi * dancer.w * dancer.h * 4;
	float sc = hgt / dancer.h; int dw = (int)(dancer.w * sc), dh = (int)hgt, x0 = (int)(x - dw / 2), y0 = (int)(y - dh);
	sg_blob(&fb, x, y, dw * 0.35f, U * 0.9f, BLACK, 0.55f);                       /* a shadow to stand on */
	for (int j = 0; j < dh; j++) {
		int py = y0 + j; if (py < 0 || py >= H) continue;
		const uint8_t *row = src + (size_t)(j * dancer.h / dh) * dancer.w * 4;
		uint32_t *d = fb.px + py * W;
		for (int i = 0; i < dw; i++) {
			int pxx = x0 + i; if (pxx < 0 || pxx >= W) continue;
			const uint8_t *p = row + (i * dancer.w / dw) * 4;
			if (p[3] > 127) d[pxx] = RGB(p[0], p[1], p[2]);
		}
	}
}

/* ---------------------------------------------------------------- the front screen: three stores side by side */
static void draw_gs_front(float t)
{
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	title_block("GAME STORE", "PICK A STORE", I_GAME);
	for (int k = 1; k < GS_N; k++) if (gs.s[k].state == Y_IDLE) gs_load(k);       /* so their art is ready */
	float top = U * 22, bot = H - U * 22, lx = U * 6, gap = U * 2.4f, total = W - U * 12 - gap * 2, sum = 0;
	for (int i = 0; i < GS_N; i++) { gs.wide[i] += ((i == gs.tab ? 1.5f : 1.0f) - gs.wide[i]) * 0.2f; if (gs.wide[i] < 0.5f) gs.wide[i] = 1; sum += gs.wide[i]; }
	static const char *title[GS_N] = {"SILVEROS", "STEAM", "MICROSOFT"};
	static const char *sub[GS_N] = {"Games that play right here", "Top sellers, deals and new games", "Popular games from the Microsoft Store"};
	static const char *foot[GS_N] = {"PLAY", "BROWSE  -  BUY", "BROWSE  -  BUY"};
	float x = lx;
	for (int i = 0; i < GS_N; i++) {
		float pw = total * gs.wide[i] / sum, ph = bot - top; int sel = i == gs.tab;
		float k = sg_sat((gs.wide[i] - 1) / 0.5f);
		if (sel) { sg_blob(&fb, x + pw / 2, top + ph / 2 + U * 2, pw * 0.55f, ph * 0.55f, BLACK, 0.65f); sg_glow(&fb, x + pw / 2, top + ph * 0.4f, pw * 0.6f, RED, 0.12f * k); }
		ui_frost(x, top, pw, ph, U * 2.4f, sel ? 0.42f : 0.66f);
		/* art: live game previews for SilverOS, the store's own pictures for the others */
		float ax = x + U * 2, ay = top + U * 2, aw = pw - U * 4, ah = ph * 0.56f;
		if (i == GS_SILVER) {
			float cs = ah / 3.2f;
			for (int g2 = 0; g2 < G_N; g2++) {
				float cy = ay + ah * 0.18f + g2 * cs * 1.05f, cx = ax + aw / 2;
				sg_rrect(&fb, ax + U, cy - cs * 0.42f, aw - U * 2, cs * 0.9f, U * 1.2f, 0, WHITE, 0.05f);
				sg_text(&fb, &FD, ax + U * 2.5f, cy + cs * 0.15f, GAME_NAME[g2], cs * 0.42f, WHITE, 1, 0);
				if (g2 == G_SNAKE) for (int q = 0; q < 7; q++) { float a = t * 1.6f + q * 0.4f; sg_rrect(&fb, cx + aw * 0.18f + sg_cos(a) * cs * 0.45f, cy + sg_sin(a * 2) * cs * 0.2f - cs * 0.05f, cs * 0.12f, cs * 0.12f, cs * 0.03f, 0, sg_mix(RGB(90, 90, 98), WHITE, 1 - q / 7.0f), 1); }
				if (g2 == G_2048) for (int q = 0; q < 4; q++) sg_rrect(&fb, cx + aw * 0.08f + q * cs * 0.3f, cy - cs * 0.15f, cs * 0.26f, cs * 0.26f, cs * 0.04f, 0, q == 3 ? RED : sg_mix(RGB(70, 70, 76), WHITE, q / 3.0f), 1);
				if (g2 == G_SHATTER) { for (int q = 0; q < 5; q++) sg_rrect(&fb, cx + aw * 0.06f + q * cs * 0.26f, cy - cs * 0.25f, cs * 0.22f, cs * 0.1f, cs * 0.02f, 0, WHITE, 0.8f); sg_disc(&fb, cx + aw * 0.22f + sg_sin(t * 2) * cs * 0.4f, cy + cs * 0.1f, cs * 0.06f, RED, 1); }
			}
		} else {
			GStore *s = &gs.s[i];
			if (s->state == Y_RESULTS) {
				int nshow = s->n < 4 ? s->n : 4;
				float tw = (aw - U) / 2, th2 = (ah - U) / 2;
				for (int q = 0; q < nshow; q++)
					thumb_draw_url(s->g[q].key, s->g[q].img, 0, yt.fetch[0] ? yt.fetch : FETCH_BIN, ax + (q % 2) * (tw + U), ay + (q / 2) * (th2 + U), tw, th2, U * 1.0f);
				sg_rect(&fb, ax, ay, aw, ah, BLACK, sel ? 0.0f : 0.35f);
			} else {
				sg_rrect(&fb, ax, ay, aw, ah, U * 1.2f, 0, WHITE, 0.05f);
				sg_text_c(&fb, &FT, ax + aw / 2, ay + ah / 2, s->state == Y_LOADING ? "Asking the store..." : s->state == Y_ERROR ? "Offline" : "", U * 1.8f, RGB(150, 150, 156), 1, 0);
			}
		}
		float ty = top + ph * 0.56f + U * 9.5f, ts = U * (5.0f + 1.4f * k);
		sg_text(&fb, &FD, x + U * 3 + U * 0.35f, ty, title[i], ts, RED, 0.8f * k, 0);
		sg_text(&fb, &FD, x + U * 3, ty, title[i], ts, WHITE, 1, 0);
		if (i == GS_MS) sg_text(&fb, &FD, x + U * 3, ty + ts * 0.95f, "STORE", ts * 0.6f, WHITE, 1, 0);
		char sf[96]; fit(sub[i], U * 1.8f, pw - U * 6, sf, sizeof sf);
		sg_text(&fb, &FT, x + U * 3, ty + (i == GS_MS ? ts * 0.95f + U * 3.0f : U * 4.0f), sf, U * 1.8f, RGB(200, 200, 206), 1, 0.01f);
		ui_chip(x + U * 3, top + ph - U * 3.6f, foot[i], U * 0.95f, WHITE, i == GS_SILVER ? RED : WHITE, i == GS_SILVER ? 1 : 0.14f);
		if (sel) sg_rrect(&fb, x - U * 0.25f, top - U * 0.25f, pw + U * 0.5f, ph + U * 0.5f, U * 2.6f, sg_max(2, U * 0.3f), WHITE, 0.95f);
		x += pw + gap;
	}
	dancer_draw(W * 0.5f, H - U * 7.6f, U * 15, t);
	if (gs.tab == GS_STEAM && steam_ready()) foot_hints("LEFT RIGHT  choose      ENTER  browse      S  open Steam      ESC  back");
	else foot_hints("LEFT RIGHT  choose      ENTER  open      ESC  back");
}

/* ---------------------------------------------------------------- drawing */
static void gs_tabs(float x, float cy)
{
	gs.seg += ((float)gs.tab - gs.seg) * 0.25f;
	float w[GS_N], tot = 0, pad = U * 3.0f;
	for (int i = 0; i < GS_N; i++) { w[i] = sg_text_w(&FC, GS_NAME[i], U * 1.0f, 0.2f) + pad * 2; tot += w[i]; }
	float hh = U * 4.6f;
	ui_frost(x, cy - hh / 2, tot + U * 0.8f, hh, hh / 2, 0.55f);
	float hx = x + U * 0.4f;                                         /* the sliding highlight */
	{ int a = (int)gs.seg; float f = gs.seg - a; float x0 = x + U * 0.4f; for (int i = 0; i < a; i++) x0 += w[i];
	  float ww = w[a] + (a + 1 < GS_N ? (w[a + 1] - w[a]) * f : 0); x0 += (a < GS_N ? w[a] * f : 0); hx = x0;
	  sg_rrect(&fb, hx, cy - hh / 2 + U * 0.4f, ww, hh - U * 0.8f, (hh - U * 0.8f) / 2, 0, WHITE, 0.95f); }
	float tx = x + U * 0.4f;
	for (int i = 0; i < GS_N; i++) {
		float on = 1 - sg_min(1, sg_abs(gs.seg - i));
		sg_text(&fb, &FC, tx + pad, cy + U * 0.38f, GS_NAME[i], U * 1.0f, sg_mix(RGB(190, 190, 196), INK, on), 1, 0.2f);
		tx += w[i];
	}
}

static float price_tag(const Game *g, float x, float y, float size, int right)
{
	char p[48]; snprintf(p, sizeof p, "%s", g->price[0] ? g->price : "--");
	float w = sg_text_w(&FT, p, size, 0);
	float px = right ? x - w : x;
	if (g->discount > 0) {
		char d[8]; snprintf(d, sizeof d, "-%d%%", g->discount);
		float dw = sg_text_w(&FC, d, size * 0.55f, 0.1f) + size * 0.8f;
		float dx = right ? px - dw - size * 0.4f : px + w + size * 0.4f;
		sg_rrect(&fb, dx, y - size * 0.95f, dw, size * 1.25f, size * 0.25f, 0, RED, 1);
		sg_text(&fb, &FC, dx + size * 0.4f, y - size * 0.15f, d, size * 0.55f, WHITE, 1, 0.1f);
		sg_text(&fb, &FT, px, y, p, size, WHITE, 1, 0);
		return right ? px + w : dx + dw;
	}
	sg_text(&fb, &FT, px, y, p, size, WHITE, 1, 0);
	return px + w;
}

static void draw_gstore_page(Home *h, float t);
static void draw_gstore(Home *h, float t)
{
	steam_poll();
	draw_gstore_page(h, t);
	if (!steam_setup_draw(t) && ui_t - steam_launch_t < 2.4f) {          /* (the demo: no real Steam to hand over to) */
		float a = sg_sat((ui_t - steam_launch_t) / 0.3f);
		sg_rect(&fb, 0, 0, W, H, BLACK, 0.8f * a);
		const char *m = !strcmp(steam_launch_what, "play") ? "Starting the game in Steam ..." : !strcmp(steam_launch_what, "install") ? "Opening Steam to install it ..." : !strcmp(steam_launch_what, "store") ? "Opening it in the Steam store ..." : "Opening Steam ...";
		float w = sg_text_w(&FT, m, U * 3, 0.01f);
		sg_text(&fb, &FT, W / 2 - w / 2, H / 2, m, U * 3, WHITE, a, 0.01f);
	}
}
static void draw_gstore_page(Home *h, float t)
{
	gs_poll();
	if (!gs.open) { draw_gs_front(t); (void)h; return; }
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	static const char *store_title[GS_N] = {"SILVEROS GAMES", "STEAM", "MICROSOFT STORE"};
	static const char *store_cap[GS_N] = {"PLAYS RIGHT HERE", "BROWSE  -  SEARCH  -  BUY", "BROWSE  -  SEARCH  -  BUY"};
	title_block(store_title[gs.tab], store_cap[gs.tab], gs.tab == GS_SILVER ? I_GAME : I_STORE);
	GStore *s = &gs.s[gs.tab];
	float top = U * 21.5f, lx = U * 6;
	gs_tabs(lx, top + U * 2.3f);
	/* the search box (not for SilverOS's own games) */
	if (gs.tab != GS_SILVER) {
		float bx = lx + U * 52, bw = W - U * 6 - bx, bh = U * 4.6f, by = top;
		ui_frost(bx, by, bw, bh, bh / 2, 0.6f);
		if (gs.focus == 0) sg_rrect(&fb, bx, by, bw, bh, bh / 2, sg_max(1.5f, U * 0.18f), RED, 0.9f);
		icon(I_SEARCH, bx + U * 2.6f, by + bh / 2, U * 1.1f, WHITE, 1);
		char ph[64]; snprintf(ph, sizeof ph, "Search %s", gs.tab == GS_STEAM ? "Steam" : "the Microsoft Store");
		if (!s->q[0]) sg_text(&fb, &FT, bx + U * 5, by + bh / 2 + U * 0.7f, ph, U * 1.9f, RGB(130, 130, 136), 1, 0.01f);
		float qx = sg_text(&fb, &FT, bx + U * 5, by + bh / 2 + U * 0.7f, s->q, U * 1.9f, WHITE, 1, 0.01f);
		if (gs.focus == 0 && ((int)(t * 2) & 1)) sg_rect(&fb, qx + U * 0.3f, by + U * 1.1f, sg_max(2, U * 0.22f), bh - U * 2.2f, RED, 1);
	}
	float gy = top + U * 8, gw = W - U * 12, gap = U * 2.0f, cw = (gw - gap * (GSC - 1)) / GSC, th = cw * 9 / 16, ch = th + U * 8.4f, bot = H - U * 10;
	if (gs.tab == GS_SILVER) {                                        /* the games that play here */
		float bw = (gw - gap * 2) / 3, bh = bot - gy;
		for (int i = 0; i < G_N; i++) {
			float x = lx + i * (bw + gap); int sel = i == s->sel;
			if (sel) { sg_blob(&fb, x + bw / 2, gy + bh / 2 + U * 1.5f, bw * 0.55f, bh * 0.55f, BLACK, 0.6f); sg_glow(&fb, x + bw / 2, gy + bh * 0.35f, bw * 0.5f, RED, 0.12f); }
			ui_frost(x, gy, bw, bh, U * 2.2f, sel ? 0.4f : 0.6f);
			if (sel) sg_rrect(&fb, x - U * 0.2f, gy - U * 0.2f, bw + U * 0.4f, bh + U * 0.4f, U * 2.4f, sg_max(2, U * 0.28f), WHITE, 0.95f);
			/* a little live preview of each game */
			float pcx = x + bw / 2, pcy = gy + bh * 0.34f, ps = sg_min(bw, bh) * 0.28f;
			if (i == G_SNAKE) {
				for (int k = 0; k < 9; k++) { float a = t * 1.5f + k * 0.35f; sg_rrect(&fb, pcx + sg_cos(a) * ps * 0.8f - ps * 0.1f, pcy + sg_sin(a * 2) * ps * 0.4f - ps * 0.1f, ps * 0.2f, ps * 0.2f, ps * 0.06f, 0, sg_mix(RGB(90, 90, 98), WHITE, 1 - k / 9.0f), 1); }
				sg_disc(&fb, pcx + ps * 0.9f, pcy - ps * 0.5f, ps * 0.09f, RED, 1);
			} else if (i == G_2048) {
				static const int v[4] = {2, 8, 64, 2048};
				for (int k = 0; k < 4; k++) { float tx2 = pcx - ps + (k % 2) * ps * 1.04f, ty2 = pcy - ps + (k / 2) * ps * 1.04f; sg_rrect(&fb, tx2, ty2, ps * 0.96f, ps * 0.96f, ps * 0.12f, 0, k == 3 ? RED : sg_mix(RGB(70, 70, 76), WHITE, k / 3.0f), 1);
					char nb[8]; snprintf(nb, sizeof nb, "%d", v[k]); sg_text_c(&fb, &FT, tx2 + ps * 0.48f, ty2 + ps * 0.62f, nb, ps * (k == 3 ? 0.28f : 0.4f), k == 2 ? INK : WHITE, 1, 0); }
			} else {
				for (int r = 0; r < 3; r++) for (int c = 0; c < 5; c++) sg_rrect(&fb, pcx - ps * 1.25f + c * ps * 0.5f, pcy - ps * 0.9f + r * ps * 0.28f, ps * 0.46f, ps * 0.22f, ps * 0.05f, 0, r == 0 ? WHITE : RGB(150, 152, 160), 0.8f);
				float bxp = pcx + sg_sin(t * 2) * ps; sg_disc(&fb, bxp, pcy + ps * 0.3f + sg_abs(sg_sin(t * 3)) * ps * 0.2f, ps * 0.09f, RED, 1);
				sg_rrect(&fb, bxp - ps * 0.3f, pcy + ps * 0.75f, ps * 0.6f, ps * 0.1f, ps * 0.05f, 0, WHITE, 1);
			}
			sg_text(&fb, &FD, x + U * 3, gy + bh * 0.68f, GAME_NAME[i], U * 5.0f, WHITE, 1, 0);
			sg_text(&fb, &FT, x + U * 3, gy + bh * 0.68f + U * 3.6f, GAME_TAG[i], U * 1.7f, RGB(200, 200, 206), 1, 0.01f);
			{ char dl3[3][160]; int m = ui_wrap(GAME_DESC[i], U * 1.35f, bw - U * 6, 2, dl3);
			  for (int k = 0; k < m; k++) sg_text(&fb, &FT, x + U * 3, gy + bh * 0.68f + U * 6.6f + k * U * 2.0f, dl3[k], U * 1.35f, RGB(150, 150, 156), 1, 0.01f); }
			char bs[48]; snprintf(bs, sizeof bs, "BEST  %d", best[i]);
			cap_text(x + U * 3, gy + bh - U * 3, bs, U * 1.0f, RGB(170, 170, 176), 1, 0);
			ui_chip(x + bw - U * 8, gy + bh - U * 3.3f, "PLAY", U * 0.95f, WHITE, RED, 1);
		}
		dancer_draw(W * 0.5f, H - U * 8.2f, U * 13, t);
		foot_hints("TAB  store      LEFT RIGHT  choose      ENTER  play      ESC  back");
		return;
	}
	if (s->state == Y_IDLE) gs_load(gs.tab);
	if (s->state == Y_LOADING) {
		for (int i = 0; i < 2 * GSC; i++) {
			float x = lx + (i % GSC) * (cw + gap), y = gy + (i / GSC) * (ch + gap), p = 0.5f + 0.5f * sg_sin(t * 5 - i * 0.5f);
			sg_rrect(&fb, x, y, cw, th, U * 1.4f, 0, WHITE, 0.05f + 0.05f * p);
			sg_rrect(&fb, x, y + th + U * 2, cw * 0.6f, U * 1.6f, U * 0.8f, 0, WHITE, 0.07f + 0.04f * p);
		}
		cap_text(W - U * 6, H - U * 7.2f, "ASKING THE STORE", U * 0.95f, RGB(140, 140, 146), 1, 1);
	} else if (s->state == Y_ERROR) {
		ui_frost(lx, gy, W * 0.5f, U * 14, U * 2, 0.6f);
		sg_text(&fb, &FT, lx + U * 3, gy + U * 6, s->err[0] ? s->err : "Something went wrong.", U * 2.3f, WHITE, 1, 0.01f);
		sg_text(&fb, &FT, lx + U * 3, gy + U * 10.5f, "Type to search, or press Enter to try again.", U * 1.7f, RGB(170, 170, 176), 1, 0.01f);
	} else if (s->detail) {                                           /* one game's page */
		Game *g = &s->g[s->sel];
		float aw = W * 0.5f, ah = aw * 9 / 16, ax = lx, ay = gy;
		sg_blob(&fb, ax + aw / 2, ay + ah / 2 + U * 2, aw * 0.56f, ah * 0.65f, BLACK, 0.6f);
		thumb_draw_url(g->key, g->img, 1, yt.fetch[0] ? yt.fetch : FETCH_BIN, ax, ay, aw, ah, U * 1.6f);
		float rx = ax + aw + U * 4, rw = W - U * 6 - rx;
		char ln[3][160]; int n = ui_wrap(g->name, U * 3.6f, rw, 3, ln);
		for (int k = 0; k < n; k++) sg_text(&fb, &FT, rx, ay + U * 3.8f + k * U * 4.6f, ln[k], U * 3.6f, WHITE, 1, 0);
		float y = ay + U * 3.8f + n * U * 4.6f + U * 0.8f;
		if (g->publisher[0]) { sg_text(&fb, &FT, rx, y, g->publisher, U * 1.8f, RGB(190, 190, 196), 1, 0.01f); y += U * 3.4f; }
		if (g->discount > 0) { char d[16]; snprintf(d, sizeof d, "-%d%%", g->discount); ui_chip(ax + U * 1.6f, ay + U * 3, d, U * 1.1f, WHITE, RED, 1); }
		float pend = price_tag(g, rx, y + U * 2.6f, U * 3.4f, 0);
		if (g->was[0]) { float wx = pend + U * 1.4f; sg_text(&fb, &FT, wx, y + U * 2.4f, g->was, U * 1.8f, RGB(150, 150, 156), 1, 0); sg_rect(&fb, wx, y + U * 1.75f, sg_text_w(&FT, g->was, U * 1.8f, 0), sg_max(1, U * 0.15f), RGB(150, 150, 156), 1); }
		y += U * 6.0f;
		float cx = rx;
		if (g->tag[0]) cx = ui_chip(cx, y, g->tag, U * 0.9f, WHITE, WHITE, 0.12f) + U * 0.8f;
		if (g->rating[0]) cx = ui_chip(cx, y, g->rating, U * 0.9f, WHITE, WHITE, 0.12f) + U * 0.8f;
		if (g->win) cx = ui_chip(cx, y, "WINDOWS", U * 0.9f, WHITE, WHITE, 0.12f) + U * 0.8f;
		if (g->mac) cx = ui_chip(cx, y, "MAC", U * 0.9f, WHITE, WHITE, 0.12f) + U * 0.8f;
		if (g->lin) cx = ui_chip(cx, y, "LINUX", U * 0.9f, WHITE, WHITE, 0.12f) + U * 0.8f;
		y += U * 4.4f;
		const char *desc = g->desc[0] ? g->desc : (s->dstate == 1 ? "Reading the store page..." : "");
		char dl2[6][160]; int m = ui_wrap(desc, U * 1.8f, rw, 6, dl2);
		for (int k = 0; k < m; k++) sg_text(&fb, &FT, rx, y + k * U * 2.7f, dl2[k], U * 1.8f, RGB(205, 205, 210), 1, 0.01f);
		/* install / play, buy, and the wishlist */
		float boxy = bot - U * 15, boxh = U * 13;
		ui_frost(lx, boxy, gw, boxh, U * 2, 0.62f);
		int here = gs.tab == GS_STEAM && steam_ready(), inst = here && steam_installed(g->id);
		icon(here ? I_GAME : I_INFO, lx + U * 4, boxy + U * 4.4f, U * 1.6f, here ? RGB(90, 225, 140) : RGB(255, 190, 70), 1);
		const char *l1, *l2;
		if (here && inst) { l1 = "Installed. Press Enter to play it, right here in SilverOS."; l2 = "It runs through Steam and Proton. Quit the game and Steam to come back here."; }
		else if (here && silver_live()) { l1 = "You're running from the USB stick: Steam's files would fill your memory."; l2 = "Install SilverOS first (su -c silver-install), then games go on the disk. ENTER tries anyway."; }
		else if (here) { l1 = "Press Enter to install it with Steam, right here in SilverOS. B buys it in Steam."; l2 = "Steam asks you to sign in the first time. Windows games run through Proton."; }
		else if (gs.tab == GS_STEAM) { l1 = "Buy it through Steam: press Enter, scan the code, and it opens in the Steam app."; l2 = "This build has no games layer, so it plays when you start Windows."; }
		else { l1 = "Microsoft Store games only run in Windows: their copy protection needs it."; l2 = "Press Enter and scan the code to get it there, for when you start Windows."; }
		sg_text(&fb, &FT, lx + U * 7.5f, boxy + U * 5.0f, l1, U * 1.9f, WHITE, 1, 0.01f);
		sg_text(&fb, &FT, lx + U * 7.5f, boxy + U * 8.6f, l2, U * 1.7f, RGB(185, 185, 190), 1, 0.01f);
		int on = wish_has(g->key);
		float bw = U * 20, bx = lx + gw - bw - U * 3, byy = boxy + boxh / 2 - U * 2.6f, bx2 = bx - bw - U * 1.6f, bx3 = bx2 - bw - U * 1.6f;
		char bl[48]; snprintf(bl, sizeof bl, "BUY  %s", g->price[0] && strcmp(g->price, "Free") ? g->price : "");
		if (!strcmp(g->price, "Free")) snprintf(bl, sizeof bl, "GET FREE");
		if (!strcmp(g->price, "Game Pass")) snprintf(bl, sizeof bl, "GET");
		if (here) {                                                       /* PLAY / INSTALL first, then BUY */
			const char *pl = inst ? "PLAY" : "INSTALL";
			sg_rrect(&fb, bx3, byy, bw, U * 5.2f, U * 2.6f, 0, inst ? RGB(40, 170, 90) : RED, 0.95f);
			float pw2 = sg_text_w(&FC, pl, U * 1.05f, 0.2f);
			sg_text(&fb, &FC, bx3 + bw / 2 - pw2 / 2, byy + U * 3.0f, pl, U * 1.05f, WHITE, 1, 0.2f);
			sg_rrect(&fb, bx2, byy, bw, U * 5.2f, U * 2.6f, 0, WHITE, 0.14f);
		} else sg_rrect(&fb, bx2, byy, bw, U * 5.2f, U * 2.6f, 0, RED, 0.95f);
		float bww = sg_text_w(&FC, bl, U * 1.05f, 0.2f);
		sg_text(&fb, &FC, bx2 + bw / 2 - bww / 2, byy + U * 3.0f, bl, U * 1.05f, WHITE, 1, 0.2f);
		sg_rrect(&fb, bx, byy, bw, U * 5.2f, U * 2.6f, 0, WHITE, on ? 0.95f : 0.14f);       /* WISHLIST */
		const char *wl = on ? "ON WISHLIST" : "WISHLIST";
		float ww = sg_text_w(&FC, wl, U * 1.05f, 0.2f);
		sg_text(&fb, &FC, bx + bw / 2 - ww / 2, byy + U * 3.0f, wl, U * 1.05f, on ? INK : WHITE, 1, 0.2f);
		if (here) { foot_hints(on ? (inst ? "ENTER  play      B  buy      W  remove from wishlist      ESC  back" : "ENTER  install      B  buy      W  remove from wishlist      ESC  back")
		                          : (inst ? "ENTER  play      B  buy      W  add to wishlist      ESC  back" : "ENTER  install      B  buy      W  add to wishlist      ESC  back")); return; }
		if (s->buying) {                                                  /* the Buy panel: scan with your phone */
			sg_rect(&fb, 0, 0, W, H, BLACK, 0.55f);
			char url[200];
			if (gs.tab == GS_STEAM) snprintf(url, sizeof url, "https://store.steampowered.com/app/%s/", g->id);
			else snprintf(url, sizeof url, "https://apps.microsoft.com/detail/%s", g->id);
			static uint8_t qm2[QR_MAX * QR_MAX];
			int qn2 = qr_make(url, qm2);
			float pw = W * 0.70f, ph = H * 0.60f, px = (W - pw) / 2, py = (H - ph) / 2;
			ui_frost(px, py, pw, ph, U * 2.6f, 0.75f);
			float qs = ph - U * 8, qx = px + U * 4, qy = py + U * 4;
			sg_rrect(&fb, qx, qy, qs, qs, U * 1.4f, 0, WHITE, 1);
			if (qn2) {
				float cell = (qs - U * 4) / qn2, ox = qx + (qs - cell * qn2) / 2, oy = qy + (qs - cell * qn2) / 2;
				for (int yy = 0; yy < qn2; yy++) for (int xx = 0; xx < qn2; xx++)
					if (qm2[yy * qn2 + xx]) sg_rect(&fb, ox + xx * cell, oy + yy * cell, cell + 0.6f, cell + 0.6f, BLACK, 1);
			}
			float tx = qx + qs + U * 4, tw2 = px + pw - U * 4 - tx;
			cap_text(tx, qy + U * 3, gs.tab == GS_STEAM ? "BUY ON STEAM" : "BUY ON THE MICROSOFT STORE", U * 1.05f, ACCENT, 1, 0);
			char tl[3][160]; int tn = ui_wrap(g->name, U * 3.0f, tw2, 2, tl);
			for (int k2 = 0; k2 < tn; k2++) sg_text(&fb, &FT, tx, qy + U * 8 + k2 * U * 3.8f, tl[k2], U * 3.0f, WHITE, 1, 0);
			float yy2 = qy + U * 8 + tn * U * 3.8f + U * 2;
			price_tag(g, tx, yy2 + U * 2.6f, U * 3.4f, 0);
			yy2 += U * 7;
			static const char *steam_steps[3] = {"1  Scan it: it opens this game in the Steam app.", "2  Buy it in Steam, with your Steam account.", "3  It's in your Steam library, to play on Windows."};
			static const char *ms_steps[3] = {"1  Scan it: it opens this game in the Microsoft Store.", "2  Get it there, with your Microsoft account.", "3  It's in your library, to play on Windows."};
			const char *const *steps = gs.tab == GS_STEAM ? steam_steps : ms_steps;
			for (int k2 = 0; k2 < 3; k2++) { char sf2[160]; fit(steps[k2], U * 1.7f, tw2, sf2, sizeof sf2); sg_text(&fb, &FT, tx, yy2 + k2 * U * 3.3f, sf2, U * 1.7f, RGB(215, 215, 220), 1, 0.01f); }
			char uf[200]; fit(url, U * 1.4f, tw2, uf, sizeof uf);
			sg_text(&fb, &FT, tx, py + ph - U * 6, uf, U * 1.4f, RGB(160, 160, 166), 1, 0.01f);
			cap_text(tx, py + ph - U * 3, "SILVEROS NEVER SEES YOUR PASSWORD OR CARD", U * 0.85f, RGB(140, 140, 146), 1, 0);
			foot_hints("ESC  close");
			return;
		}
		foot_hints(on ? "ENTER  buy      W  remove from wishlist      ESC  back" : "ENTER  buy      W  add to wishlist      ESC  back");
		return;
	} else {                                                         /* the grid */
		int rows = (int)((bot - gy + gap) / (ch + gap)); if (rows < 1) rows = 1;
		int srow = s->sel / GSC, trow = s->top;
		if (srow < trow) trow = srow;
		if (srow >= trow + rows) trow = srow - rows + 1;
		s->top = trow;
		char hd[120];
		if (s->shown_q[0]) snprintf(hd, sizeof hd, "RESULTS FOR  \"%s\"", s->shown_q);
		else snprintf(hd, sizeof hd, gs.tab == GS_STEAM ? "TOP SELLERS  -  DEALS  -  NEW ON STEAM" : "POPULAR ON THE MICROSOFT STORE");
		cap_text(lx, gy - U * 1.4f, hd, U * 1.0f, RGB(200, 200, 206), 1, 0);
		for (int i = trow * GSC; i < s->n && i < (trow + rows) * GSC; i++) {
			Game *g = &s->g[i];
			int r = i / GSC - trow, c = i % GSC, sel = i == s->sel && gs.focus == 1;
			float in = sg_smooth((s_age - 0.1f - 0.04f * (r * GSC + c)) / 0.3f);
			float x = lx + c * (cw + gap), y = gy + U * 1.0f + r * (ch + gap) + (1 - in) * U * 3 - (sel ? U * 0.8f : 0);
			if (sel) { sg_glow(&fb, x + cw / 2, y + th / 2, cw * 0.6f, RED, 0.14f); sg_blob(&fb, x + cw / 2, y + ch / 2 + U * 1.4f, cw * 0.55f, ch * 0.6f, BLACK, 0.6f); }
			ui_frost(x, y, cw, ch, U * 1.4f, sel ? 0.45f : 0.62f);
			thumb_draw_url(g->key, g->img, 0, yt.fetch[0] ? yt.fetch : FETCH_BIN, x, y, cw, th, U * 1.4f);
			if (g->tag[0] && gs.tab == GS_STEAM && !s->shown_q[0]) ui_chip(x + U * 1.0f, y + U * 2.4f, g->tag, U * 0.8f, WHITE, BLACK, 0.7f);
			if (wish_has(g->key)) { sg_disc(&fb, x + cw - U * 2.4f, y + U * 2.4f, U * 1.3f, RED, 1); sg_text_c(&fb, &FC, x + cw - U * 2.4f, y + U * 2.85f, "W", U * 0.9f, WHITE, 1, 0); }
			if (sel) sg_rrect(&fb, x - U * 0.2f, y - U * 0.2f, cw + U * 0.4f, ch + U * 0.4f, U * 1.6f, sg_max(2, U * 0.28f), WHITE, 0.95f);
			char nm[2][160]; int n = ui_wrap(g->name, U * 1.7f, cw - U * 3, 2, nm);
			for (int k = 0; k < n; k++) sg_text(&fb, &FT, x + U * 1.5f, y + th + U * 2.9f + k * U * 2.3f, nm[k], U * 1.7f, WHITE, in, 0.01f);
			price_tag(g, x + cw - U * 1.5f, y + ch - U * 1.6f, U * 1.9f, 1);
			char sub[96]; snprintf(sub, sizeof sub, "%s", g->publisher[0] ? g->publisher : g->rating[0] ? g->rating : (g->lin ? "Windows - Linux" : "Windows"));
			char sf[96]; fit(sub, U * 1.3f, cw * 0.5f, sf, sizeof sf);
			sg_text(&fb, &FT, x + U * 1.5f, y + ch - U * 1.6f, sf, U * 1.3f, RGB(160, 160, 166), in, 0.01f);
		}
		int total_rows = (s->n + GSC - 1) / GSC;
		if (total_rows > rows) ui_scroll(W - U * 3.2f, gy, rows * (ch + gap) - gap, trow, rows, total_rows);
	}
	dancer_draw(W * 0.88f, H - U * 2.0f, U * 9, t);
	foot_hints("TAB  store      TYPE  search      ARROWS  move      ENTER  open      ESC  back");
	(void)h;
}

/* keys; returns 1 when the Game Store should close */
static int gstore_key(int k, int *playing)
{
	GStore *s = &gs.s[gs.tab];
	if (ss.state == 1) return 0;                                         /* getting Steam: wait */
	if (ss.state == 2) { if (k == K_ENTER) { ss.state = 0; steam_go(ss.action, ss.id); } else if (k == K_ESC || k == K_BACK) ss.state = 0; return 0; }
	if (!gs.open) {                                                      /* the three-way front screen */
		if (k == K_ESC || k == K_BACK) return 1;
		if (k == K_LEFT && gs.tab > 0) gs.tab--;
		else if (k == K_RIGHT && gs.tab < GS_N - 1) gs.tab++;
		else if (k == K_ENTER) { gs.open = 1; gs.focus = 1; }
		else if ((k == 's' || k == 'S') && gs.tab == GS_STEAM && steam_ready()) steam_go("open", "");   /* all of Steam: library, store, friends */
		return 0;
	}
	if (gs.tab == GS_SILVER) {
		if (k == K_ESC || k == K_BACK) { gs.open = 0; return 0; }
		if (k == 9) { gs.tab = (gs.tab + 1) % GS_N; gs.focus = 1; return 0; }
		if (k == K_LEFT && s->sel > 0) s->sel--;
		else if (k == K_RIGHT && s->sel < G_N - 1) s->sel++;
		else if (k == K_ENTER) { game_start(s->sel); *playing = 1; }
		return 0;
	}
	if (s->detail) {
		if (s->buying) { if (k == K_ESC || k == K_BACK || k == K_ENTER) s->buying = 0; return 0; }
		if (k == K_ESC || k == K_BACK || k == K_LEFT) s->detail = 0;
		else if (gs.tab == GS_STEAM && steam_ready() && s->g[s->sel].id[0]) {        /* Steam runs here: it installs, plays and sells */
			const char *id = s->g[s->sel].id;
			if (k == K_ENTER) steam_go(steam_installed(id) ? "play" : "install", id);
			else if (k == 'b' || k == 'B') steam_go("store", id);
			else if (k == 'w' || k == 'W') wish_toggle(&s->g[s->sel]);
			return 0;
		}
		else if (k == K_ENTER || k == 'b' || k == 'B') s->buying = 1;
		else if (k == 'w' || k == 'W') wish_toggle(&s->g[s->sel]);
		return 0;
	}
	if (k == 9) { gs.tab = (gs.tab + 1) % GS_N; gs.focus = gs.s[gs.tab].n ? 1 : 0; return 0; }
	if (k == K_ESC) { if (gs.focus == 0 && s->q[0]) { s->q[0] = 0; gs.focus = 1; gs_load(gs.tab); return 0; } gs.open = 0; return 0; }
	size_t n = strlen(s->q);
	if (gs.focus == 1 && s->state == Y_RESULTS) {
		if (k == K_LEFT) { if (s->sel > 0) s->sel--; return 0; }
		if (k == K_RIGHT) { if (s->sel + 1 < s->n) s->sel++; return 0; }
		if (k == K_UP) { if (s->sel >= GSC) s->sel -= GSC; else gs.focus = 0; return 0; }
		if (k == K_DOWN) { if (s->sel + GSC < s->n) s->sel += GSC; else if (s->sel / GSC < (s->n - 1) / GSC) s->sel = s->n - 1; return 0; }
		if (k == K_ENTER) { gs_open_detail(s, gs.tab); return 0; }
	}
	if (gs.focus == 0 && k == K_DOWN && s->state == Y_RESULTS) { gs.focus = 1; return 0; }
	if (k == K_BACK) { if (n) { s->q[n - 1] = 0; gs.focus = 0; } else gs.open = 0; }
	else if (k >= 32 && k < 127 && n + 1 < sizeof s->q) { s->q[n] = (char)k; s->q[n + 1] = 0; gs.focus = 0; }
	else if (k == K_ENTER) { gs_load(gs.tab); gs.focus = 1; }
	return 0;
}
