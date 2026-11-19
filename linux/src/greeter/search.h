/*
 * Silver Search: SilverOS's own search engine. No external libraries.
 *
 * It finds anything:
 *   - names of files and folders, apps, settings and commands (ranked fuzzy matching: exact,
 *     prefix, word-start, substring, then letters-in-order; every word of the query must match,
 *     either the name or a folder above it; the letters that matched are reported for highlighting)
 *   - text INSIDE files (time-sliced, so the screen never stalls), with the matching line
 *   - running processes
 *   - instant answers: calculator, percentages, unit conversions, time and date, and system
 *     facts (ip, memory, disk, kernel, uptime, ...)
 *   - "> command" to run something in the terminal
 * It learns: things you open are ranked higher next time (~/.config/silver/search-history).
 *
 * The index is built a little at a time between frames, so it never blocks the interface.
 * Included by home.h; needs now() and, with -DRENDER, the sample tree MOCK.
 */
#pragma once
#include <math.h>
#include <sys/statvfs.h>

enum { N_APP, N_SET, N_FILE, N_DIR, N_CMD };
enum { R_NODE, R_TEXT, R_PROC, R_RUN };
enum { SC_ALL, SC_FILES, SC_TEXT, SC_APPS, SC_SETTINGS, SC_RUNNING, SC_COMMANDS, SC_N };
static const char *SCOPE_NAME[SC_N] = {"ALL", "FILES", "TEXT", "APPS", "SETTINGS", "RUNNING", "COMMANDS"};

typedef struct { char name[56]; int parent; uint8_t kind; uint32_t size; } Node;      /* size: file size, or the Settings row for N_SET */
typedef struct { int type, node, score, line, pid; uint64_t mask; char text[100]; } SRes;

#define NODE_CAP 24000
#define MAXRES 80
#define MAXHITS 40
#define MAXHIST 64

typedef struct {
	Node *nodes; int n;
	int *queue, qh, qt, indexing, ready;
	double rebuilt;
	char q[64]; int scope;
	SRes res[MAXRES]; int nres;
	SRes hits[MAXHITS]; int nhits;
	int *cand, ncand, cpos, scanning;
	int has_ans; char ans_cap[40], ans_big[72], ans_sub[120];
	struct { char key[200]; int count; } hist[MAXHIST]; int nhist;
	struct { int pid; char name[32]; long rss; } procs[96]; int nprocs;
} Search;

/* ================================================================ nodes */
static int node_add(Search *s, const char *name, int parent, int kind, uint32_t size)
{
	if (s->n >= NODE_CAP) return -1;
	Node *nd = &s->nodes[s->n];
	snprintf(nd->name, sizeof nd->name, "%s", name); nd->parent = parent; nd->kind = (uint8_t)kind; nd->size = size;
	return s->n++;
}
static void node_path(const Search *s, int i, char *out, size_t n)
{
	int chain[16], d = 0;
	for (; i >= 0 && d < 16; i = s->nodes[i].parent) chain[d++] = i;
	out[0] = 0;
	for (int k = d - 1; k >= 0; k--) {
		const char *nm = s->nodes[chain[k]].name;
		size_t l = strlen(out);
		if (!strcmp(nm, "/")) { snprintf(out + l, n - l, "/"); continue; }
		snprintf(out + l, n - l, "%s%s", (l && out[l - 1] != '/') ? "/" : "", nm);
	}
}
static void node_key(const Search *s, int i, char *out, size_t n)
{
	const Node *nd = &s->nodes[i];
	if (nd->kind == N_APP) snprintf(out, n, "app:%s", nd->name);
	else if (nd->kind == N_SET) snprintf(out, n, "set:%s", nd->name);
	else if (nd->kind == N_CMD) snprintf(out, n, "cmd:%s", nd->name);
	else node_path(s, i, out, n);
}

/* ================================================================ history (frecency) */
static void hist_path(char *out, size_t n) { snprintf(out, n, "%s/.config/silver/search-history", homedir); }
static void hist_load(Search *s)
{
	s->nhist = 0;
#ifdef RENDER
	static const struct { const char *k; int c; } fake[] = {{"/home/silver/notes.txt", 5}, {"app:Terminal", 4}, {"set:Calm mode", 2}, {"/home/silver/Documents/ideas.txt", 3}};
	for (unsigned i = 0; i < sizeof fake / sizeof fake[0]; i++) { snprintf(s->hist[s->nhist].key, 200, "%s", fake[i].k); s->hist[s->nhist++].count = fake[i].c; }
#else
	char p[256]; hist_path(p, sizeof p);
	FILE *f = fopen(p, "r");
	if (!f) return;
	char line[260];
	while (s->nhist < MAXHIST && fgets(line, sizeof line, f)) {
		int c = atoi(line); char *sp = strchr(line, ' ');
		if (!sp || c <= 0) continue;
		sp++; sp[strcspn(sp, "\n")] = 0;
		snprintf(s->hist[s->nhist].key, 200, "%s", sp); s->hist[s->nhist++].count = c;
	}
	fclose(f);
#endif
}
static int hist_count(const Search *s, const char *key)
{
	for (int i = 0; i < s->nhist; i++) if (!strcmp(s->hist[i].key, key)) return s->hist[i].count;
	return 0;
}
static void hist_bump(Search *s, const char *key)
{
	int i;
	for (i = 0; i < s->nhist; i++) if (!strcmp(s->hist[i].key, key)) break;
	if (i == s->nhist) {
		if (s->nhist < MAXHIST) s->nhist++;
		else { i = 0; for (int k = 1; k < MAXHIST; k++) if (s->hist[k].count < s->hist[i].count) i = k; }   /* forget the weakest */
		snprintf(s->hist[i].key, 200, "%s", key); s->hist[i].count = 0;
	}
	s->hist[i].count++;
#ifndef RENDER
	char p[256]; snprintf(p, sizeof p, "%s/.config", homedir); mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/.config/silver", homedir); mkdir(p, 0755);
	hist_path(p, sizeof p);
	FILE *f = fopen(p, "w");
	if (f) { for (int k = 0; k < s->nhist; k++) fprintf(f, "%d %s\n", s->hist[k].count, s->hist[k].key); fclose(f); }
#endif
}

/* ================================================================ matching */
static char lc(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
static int is_word_start(const char *nm, int i) { return i == 0 || !((nm[i - 1] >= 'a' && nm[i - 1] <= 'z') || (nm[i - 1] >= 'A' && nm[i - 1] <= 'Z') || (nm[i - 1] >= '0' && nm[i - 1] <= '9')) || (nm[i] >= 'A' && nm[i] <= 'Z' && nm[i - 1] >= 'a' && nm[i - 1] <= 'z'); }

/* How well `tok` (lower case) matches `nm`. 0 = not at all. mask gets the matched letter positions. */
static int match_name(const char *nm, const char *tok, uint64_t *mask)
{
	int nl = (int)strlen(nm), tl = (int)strlen(tok);
	if (!tl || tl > nl) return 0;
	int exact = nl == tl;
	for (int i = 0; exact && i < nl; i++) if (lc(nm[i]) != tok[i]) exact = 0;
	int best = -1, bscore = 0;
	for (int i = 0; i + tl <= nl; i++) {                       /* a plain substring: prefer the earliest word start */
		int ok = 1;
		for (int j = 0; j < tl; j++) if (lc(nm[i + j]) != tok[j]) { ok = 0; break; }
		if (!ok) continue;
		int sc = i == 0 ? 800 : is_word_start(nm, i) ? 600 : 400;
		if (sc > bscore) { bscore = sc; best = i; }
	}
	if (best >= 0) {
		if (mask) for (int j = 0; j < tl && best + j < 64; j++) *mask |= 1ull << (best + j);
		return exact ? 1000 : bscore;
	}
	int pos[24], p = 0, last = -2, score = 150;                /* letters in order, e.g. "slvr" in "silver" */
	if (tl > 24) return 0;
	for (int i = 0, j = 0; i < nl && j < tl; i++)
		if (lc(nm[i]) == tok[j]) {
			pos[p++] = i; score += (i == last + 1) ? 14 : 0; score += is_word_start(nm, i) ? 10 : 0; last = i; j++;
		}
	if (p < tl) return 0;
	score -= (pos[p - 1] - pos[0] - tl) * 2;                    /* scattered matches rank lower */
	if (score < 60) return 0;
	if (mask) for (int k = 0; k < p; k++) if (pos[k] < 64) *mask |= 1ull << pos[k];
	return score;
}

/* ================================================================ quick answers */
static const char *cp;
static int calc_err;
static double calc_expr(void);
static void sp_skip(void) { while (*cp == ' ') cp++; }
static double calc_primary(void)
{
	sp_skip();
	if (*cp == '(') { cp++; double v = calc_expr(); sp_skip(); if (*cp == ')') cp++; else calc_err = 1; return v; }
	if ((*cp >= '0' && *cp <= '9') || *cp == '.') {
		char *e; double v = strtod(cp, &e); cp = e; return v;
	}
	if ((*cp >= 'a' && *cp <= 'z')) {
		char id[12]; int n = 0;
		while (*cp >= 'a' && *cp <= 'z' && n < 11) id[n++] = *cp++;
		id[n] = 0;
		if (!strcmp(id, "pi")) return 3.14159265358979;
		if (!strcmp(id, "e")) return 2.71828182845905;
		sp_skip();
		if (*cp != '(') { calc_err = 1; return 0; }
		cp++; double v = calc_expr(); sp_skip(); if (*cp == ')') cp++; else calc_err = 1;
		if (!strcmp(id, "sqrt")) return sqrt(v);
		if (!strcmp(id, "abs")) return fabs(v);
		if (!strcmp(id, "sin")) return sin(v);
		if (!strcmp(id, "cos")) return cos(v);
		if (!strcmp(id, "tan")) return tan(v);
		if (!strcmp(id, "ln")) return log(v);
		if (!strcmp(id, "log")) return log10(v);
		if (!strcmp(id, "round")) return floor(v + 0.5);
		calc_err = 1; return 0;
	}
	calc_err = 1; return 0;
}
static double calc_unary(void)
{
	sp_skip();
	if (*cp == '-') { cp++; return -calc_unary(); }
	if (*cp == '+') { cp++; return calc_unary(); }
	double v = calc_primary();
	sp_skip();
	if (*cp == '^') { cp++; return pow(v, calc_unary()); }
	return v;
}
static double calc_term(void)
{
	double v = calc_unary();
	for (;;) {
		sp_skip();
		if (*cp == '*' || *cp == 'x') { cp++; v *= calc_unary(); }
		else if (*cp == '/') { cp++; double d = calc_unary(); if (d == 0) calc_err = 1; else v /= d; }
		else if (*cp == '%') { cp++; double d = calc_unary(); if (d == 0) calc_err = 1; else v = fmod(v, d); }
		else return v;
	}
}
static double calc_expr(void)
{
	double v = calc_term();
	for (;;) {
		sp_skip();
		if (*cp == '+') { cp++; v += calc_term(); }
		else if (*cp == '-') { cp++; v -= calc_term(); }
		else return v;
	}
}
static void fmt_num(double v, char *out, size_t n)
{
	if (fabs(v) < 1e15 && v == floor(v)) snprintf(out, n, "%.0f", v);
	else { snprintf(out, n, "%.10g", v); }
}

static const struct { const char *name; int cat; double f; } UNITS[] = {
	{"mm", 0, 0.001}, {"cm", 0, 0.01}, {"m", 0, 1}, {"km", 0, 1000}, {"in", 0, 0.0254}, {"inch", 0, 0.0254}, {"ft", 0, 0.3048}, {"feet", 0, 0.3048}, {"yd", 0, 0.9144}, {"mi", 0, 1609.344}, {"miles", 0, 1609.344}, {"mile", 0, 1609.344},
	{"mg", 1, 1e-6}, {"g", 1, 0.001}, {"kg", 1, 1}, {"lb", 1, 0.45359237}, {"lbs", 1, 0.45359237}, {"oz", 1, 0.0283495231},
	{"ml", 2, 0.001}, {"l", 2, 1}, {"gal", 2, 3.785411784}, {"cup", 2, 0.2365882365},
	{"b", 3, 1}, {"kb", 3, 1e3}, {"mb", 3, 1e6}, {"gb", 3, 1e9}, {"tb", 3, 1e12}, {"kib", 3, 1024}, {"mib", 3, 1048576}, {"gib", 3, 1073741824.0},
	{"ms", 4, 0.001}, {"s", 4, 1}, {"sec", 4, 1}, {"min", 4, 60}, {"h", 4, 3600}, {"hr", 4, 3600}, {"d", 4, 86400}, {"w", 4, 604800},
	{"c", 5, 1}, {"f", 5, 1}, {"k", 5, 1},
};
static int unit_find(const char *u) { for (unsigned i = 0; i < sizeof UNITS / sizeof UNITS[0]; i++) if (!strcmp(UNITS[i].name, u)) return (int)i; return -1; }

static int answer_units(const char *q, Search *s)
{
	char a[64]; char u1[16], u2[16], w[8];
	snprintf(a, sizeof a, "%s", q);
	double v; int n = 0;
	if (sscanf(a, "%lf %15s %7s %15s%n", &v, u1, w, u2, &n) != 4) return 0;
	if (strcmp(w, "in") && strcmp(w, "to") && strcmp(w, "as")) return 0;
	int i = unit_find(u1), j = unit_find(u2);
	if (i < 0 || j < 0 || UNITS[i].cat != UNITS[j].cat) return 0;
	double r;
	if (UNITS[i].cat == 5) {                                         /* temperature */
		double c = u1[0] == 'f' ? (v - 32) / 1.8 : u1[0] == 'k' ? v - 273.15 : v;
		r = u2[0] == 'f' ? c * 1.8 + 32 : u2[0] == 'k' ? c + 273.15 : c;
	} else r = v * UNITS[i].f / UNITS[j].f;
	char nb[40], vb[40]; fmt_num(r, nb, sizeof nb); fmt_num(v, vb, sizeof vb);
	snprintf(s->ans_cap, sizeof s->ans_cap, "CONVERSION");
	snprintf(s->ans_big, sizeof s->ans_big, "%s %s", nb, u2);
	snprintf(s->ans_sub, sizeof s->ans_sub, "%s %s", vb, u1);
	return 1;
}

static int answer_fact(const char *q, Search *s)
{
	char big[72] = "", sub[120] = "", cap[40] = "";
	char ql[64]; snprintf(ql, sizeof ql, "%s", q);
	for (char *c = ql; *c; c++) *c = lc(*c);
	if (!strcmp(ql, "time") || !strcmp(ql, "clock") || !strcmp(ql, "date") || !strcmp(ql, "today") || !strcmp(ql, "day")) {
		static const char *dn[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
		static const char *mn[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
#ifdef RENDER
		snprintf(big, sizeof big, "9:41"); snprintf(sub, sizeof sub, "Saturday 14 November 2026");
#else
		time_t t = time(NULL); struct tm tm; gmtime_r(&t, &tm);
		snprintf(big, sizeof big, "%d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
		snprintf(sub, sizeof sub, "%s %d %s %d", dn[tm.tm_wday], tm.tm_mday, mn[tm.tm_mon], tm.tm_year + 1900);
#endif
		snprintf(cap, sizeof cap, "TIME AND DATE");
	} else if (!strcmp(ql, "ip") || !strcmp(ql, "network") || !strcmp(ql, "wifi") || !strcmp(ql, "internet")) {
		net_text(sub, sizeof sub); snprintf(big, sizeof big, "%s", strchr(sub, ' ') ? strrchr(sub, ' ') + 1 : sub);
		snprintf(cap, sizeof cap, "NETWORK"); snprintf(sub, sizeof sub, "your address");
	} else if (!strcmp(ql, "host") || !strcmp(ql, "hostname") || !strcmp(ql, "computer")) {
		struct utsname u;
#ifdef RENDER
		snprintf(big, sizeof big, "silver");
#else
		if (!uname(&u)) snprintf(big, sizeof big, "%s", u.nodename);
#endif
		(void)u; snprintf(cap, sizeof cap, "COMPUTER NAME"); snprintf(sub, sizeof sub, "hostname");
	} else if (!strcmp(ql, "kernel") || !strcmp(ql, "version") || !strcmp(ql, "os")) {
#ifdef RENDER
		snprintf(big, sizeof big, "6.18.63"); snprintf(sub, sizeof sub, "Linux, SilverOS 0.1.0");
#else
		struct utsname u;
		if (!uname(&u)) { snprintf(big, sizeof big, "%s", u.release); snprintf(sub, sizeof sub, "%s, SilverOS 0.1.0", u.sysname); }
#endif
		snprintf(cap, sizeof cap, "KERNEL");
	} else if (!strcmp(ql, "uptime") || !strcmp(ql, "memory") || !strcmp(ql, "ram") || !strcmp(ql, "mem") || !strcmp(ql, "cpu") || !strcmp(ql, "load")) {
		Sys sy; memset(&sy, 0, sizeof sy); sys_read(&sy, 1.0f); sys_read(&sy, 1.5f);
		if (ql[0] == 'u') { snprintf(big, sizeof big, "%ldh %02ldm", sy.up / 3600, (sy.up / 60) % 60); snprintf(sub, sizeof sub, "since the computer started"); snprintf(cap, sizeof cap, "UPTIME"); }
		else if (ql[0] == 'c' || ql[0] == 'l') { snprintf(big, sizeof big, "%d%%", (int)(sy.cpu + 0.5f)); snprintf(sub, sizeof sub, "load %.2f", sy.load); snprintf(cap, sizeof cap, "PROCESSOR"); }
		else { snprintf(big, sizeof big, "%ld MB", sy.mem_used); snprintf(sub, sizeof sub, "used of %ld MB", sy.mem_total); snprintf(cap, sizeof cap, "MEMORY"); }
	} else if (!strcmp(ql, "disk") || !strcmp(ql, "storage") || !strcmp(ql, "space") || !strcmp(ql, "free")) {
#ifdef RENDER
		snprintf(big, sizeof big, "412 GB free"); snprintf(sub, sizeof sub, "of 512 GB");
#else
		struct statvfs sv;
		if (!statvfs("/", &sv)) {
			double fr = (double)sv.f_bavail * sv.f_frsize, tot = (double)sv.f_blocks * sv.f_frsize;
			if (tot > 1e9) { snprintf(big, sizeof big, "%.1f GB free", fr / 1e9); snprintf(sub, sizeof sub, "of %.1f GB", tot / 1e9); }
			else { snprintf(big, sizeof big, "%.0f MB free", fr / 1e6); snprintf(sub, sizeof sub, "of %.0f MB", tot / 1e6); }
		}
#endif
		snprintf(cap, sizeof cap, "STORAGE");
	} else if (!strcmp(ql, "whoami") || !strcmp(ql, "user") || !strcmp(ql, "me")) {
		snprintf(big, sizeof big, "%s", strrchr(homedir, '/') ? strrchr(homedir, '/') + 1 : "silver"); snprintf(cap, sizeof cap, "SIGNED IN AS"); snprintf(sub, sizeof sub, "%s", homedir);
	} else return 0;
	if (!big[0]) return 0;
	snprintf(s->ans_cap, sizeof s->ans_cap, "%s", cap); snprintf(s->ans_big, sizeof s->ans_big, "%s", big); snprintf(s->ans_sub, sizeof s->ans_sub, "%s", sub);
	return 1;
}

static void compute_answer(Search *s)
{
	s->has_ans = 0;
	const char *q = s->q;
	if (!q[0] || q[0] == '>') return;
	if (answer_fact(q, s) || answer_units(q, s)) { s->has_ans = 1; return; }
	double a, b;                                                    /* "15% of 80" */
	char rest[48];
	if (sscanf(q, "%lf%% of %lf", &a, &b) == 2) {
		char nb[40]; fmt_num(a * b / 100, nb, sizeof nb);
		snprintf(s->ans_cap, sizeof s->ans_cap, "PERCENTAGE"); snprintf(s->ans_big, sizeof s->ans_big, "%s", nb); snprintf(s->ans_sub, sizeof s->ans_sub, "%g%% of %g", a, b);
		s->has_ans = 1; return;
	}
	int has_digit = 0, has_op = 0;
	for (const char *c = q; *c; c++) { if ((*c >= '0' && *c <= '9')) has_digit = 1; if (strchr("+-*/^()%x", *c) || (*c >= 'a' && *c <= 'z')) has_op = 1; }
	if (q[0] == '=') q++;
	if (!has_digit && strncmp(q, "pi", 2) && strncmp(s->q, "=", 1)) return;
	snprintf(rest, sizeof rest, "%s", q);
	for (char *c = rest; *c; c++) *c = lc(*c);
	cp = rest; calc_err = 0;
	double v = calc_expr();
	sp_skip();
	int plain_number = 1;
	for (const char *c = rest; *c; c++) if (!((*c >= '0' && *c <= '9') || *c == '.' || *c == ' ')) plain_number = 0;
	if (calc_err || *cp || !has_op || (plain_number && s->q[0] != '=') || v != v) return;
	char nb[48]; fmt_num(v, nb, sizeof nb);
	snprintf(s->ans_cap, sizeof s->ans_cap, "CALCULATOR"); snprintf(s->ans_big, sizeof s->ans_big, "%s", nb); snprintf(s->ans_sub, sizeof s->ans_sub, "%s", s->q[0] == '=' ? s->q + 1 : s->q);
	s->has_ans = 1;
}

/* ================================================================ the index */
static const char *APPS_N[] = {"Terminal", "System Monitor", "About SilverOS", "Files", "Settings"};
static const char *SETS_N[] = {"Network", "Display", "Calm mode", "About", "Restart", "Shut down"};

static void index_seed(Search *s)
{
	s->n = 0;
	node_add(s, "/", -1, N_DIR, 0);
	for (unsigned i = 0; i < sizeof APPS_N / sizeof APPS_N[0]; i++) node_add(s, APPS_N[i], -1, N_APP, (uint32_t)i);
	for (unsigned i = 0; i < sizeof SETS_N / sizeof SETS_N[0]; i++) node_add(s, SETS_N[i], -1, N_SET, (uint32_t)i);
#ifdef RENDER
	static const char *cmds[] = {"ls", "cat", "grep", "ps", "top", "vi", "ping", "date", "free", "df"};
	for (unsigned i = 0; i < sizeof cmds / sizeof cmds[0]; i++) node_add(s, cmds[i], 0, N_CMD, 0);
	for (unsigned i = 0; i < sizeof MOCK / sizeof MOCK[0]; i++) {          /* the sample tree */
		char tmp[200], *part[8]; int np = 0;
		snprintf(tmp, sizeof tmp, "%s", MOCK[i].path);
		for (char *t = strtok(tmp, "/"); t && np < 8; t = strtok(NULL, "/")) part[np++] = t;
		int parent = 0;
		for (int c = 0; c < np; c++) {
			int found = -1, last = c == np - 1;
			for (int k = 0; k < s->n; k++) if (s->nodes[k].parent == parent && s->nodes[k].kind >= N_FILE && !strcmp(s->nodes[k].name, part[c])) { found = k; break; }
			if (found < 0) found = node_add(s, part[c], parent, last && !MOCK[i].dir ? N_FILE : N_DIR, last ? (uint32_t)MOCK[i].size : 0);
			parent = found;
		}
	}
	s->indexing = 0; s->ready = 1;
#else
	s->queue[0] = 0; s->qh = 0; s->qt = 1; s->indexing = 1;
#endif
}

#ifndef RENDER
static int skip_dir(int parent, const char *name)
{
	if (parent != 0) return 0;
	return !strcmp(name, "proc") || !strcmp(name, "sys") || !strcmp(name, "dev") || !strcmp(name, "run") || !strcmp(name, "tmp") || !strcmp(name, "lost+found");
}
static void index_step(Search *s, double budget)
{
	double t0 = now();
	while (s->qh < s->qt && s->n < NODE_CAP - 300) {
		int d = s->queue[s->qh++];
		char path[400]; node_path(s, d, path, sizeof path);
		int depth = 0; for (const char *c = path; *c; c++) if (*c == '/') depth++;
		if (depth > 8) continue;
		DIR *dir = opendir(path);
		if (!dir) continue;
		const char *dn = s->nodes[d].name;
		int in_bin = !strcmp(dn, "bin") || !strcmp(dn, "sbin");
		struct dirent *de;
		while ((de = readdir(dir)) && s->n < NODE_CAP) {
			if (de->d_name[0] == '.') continue;
			if (skip_dir(d, de->d_name)) continue;
			char full[600]; struct stat st;
			snprintf(full, sizeof full, "%s/%s", path, de->d_name);
			if (lstat(full, &st)) continue;
			if (S_ISDIR(st.st_mode)) { int ni = node_add(s, de->d_name, d, N_DIR, 0); if (ni >= 0 && s->qt < NODE_CAP / 2) s->queue[s->qt++] = ni; }
			else if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))
				node_add(s, de->d_name, d, (in_bin && (st.st_mode & 0111)) ? N_CMD : N_FILE, (uint32_t)st.st_size);
		}
		closedir(dir);
		if (now() - t0 > budget) break;
	}
	if (s->qh >= s->qt) { s->indexing = 0; s->ready = 1; }
}
#endif

/* ================================================================ files: text */
/* Read up to cap bytes of a text file. Returns the length, or -1 if it is not text. */
static int file_text(const char *path, char *buf, int cap)
{
#ifdef RENDER
	for (unsigned i = 0; i < sizeof MOCK / sizeof MOCK[0]; i++)
		if (!strcmp(MOCK[i].path, path)) { if (!MOCK[i].text) return -1; snprintf(buf, (size_t)cap, "%s", MOCK[i].text); return (int)strlen(buf); }
	return -1;
#else
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	int n = (int)fread(buf, 1, (size_t)cap - 1, f); fclose(f);
	for (int i = 0; i < n && i < 1024; i++) if (buf[i] == 0) return -1;
	buf[n] = 0;
	return n;
#endif
}

static int find_ci(const char *hay, int hn, const char *needle, int nn)
{
	for (int i = 0; i + nn <= hn; i++) {
		int j = 0;
		while (j < nn && lc(hay[i + j]) == needle[j]) j++;
		if (j == nn) return i;
	}
	return -1;
}

static void cands_build(Search *s)
{
	if (!s->cand) s->cand = malloc(sizeof(int) * 9000);
	s->ncand = 0;
	char p[400];
	for (int i = 0; i < s->n && s->ncand < 9000; i++) {
		const Node *nd = &s->nodes[i];
		if (nd->kind != N_FILE || nd->size == 0 || nd->size > 262144) continue;
		node_path(s, i, p, sizeof p);
		if (strncmp(p, homedir, strlen(homedir)) && strncmp(p, "/etc/", 5) && strncmp(p, "/var/", 5) && strncmp(p, "/usr/share/", 11)) continue;
		s->cand[s->ncand++] = i;
	}
}

static void content_step(Search *s, double budget)
{
	static char buf[65536];
	double t0 = now();
	int nn = (int)strlen(s->q);
	char needle[64]; for (int i = 0; i <= nn; i++) needle[i] = lc(s->q[i]);
	while (s->cpos < s->ncand && s->nhits < MAXHITS) {
		int ni = s->cand[s->cpos++];
		char p[400]; node_path(s, ni, p, sizeof p);
		int n = file_text(p, buf, sizeof buf);
		if (n > 0) {
			int at = find_ci(buf, n, needle, nn);
			if (at >= 0) {
				int ls = at; while (ls > 0 && buf[ls - 1] != '\n') ls--;
				int le = at; while (le < n && buf[le] != '\n') le++;
				int line = 1; for (int i = 0; i < ls; i++) if (buf[i] == '\n') line++;
				int st = at - 22 > ls ? at - 22 : ls, en = st + 70 < le ? st + 70 : le;
				SRes *r = &s->hits[s->nhits++];
				r->type = R_TEXT; r->node = ni; r->line = line; r->pid = 0; r->mask = 0;
				r->score = 300 + (s->scope == SC_TEXT ? 300 : 0) - s->cpos / 40;
				int k = 0;
				for (int i = st; i < en && k < 98; i++, k++) { r->text[k] = (unsigned char)buf[i] >= 32 && (unsigned char)buf[i] < 127 ? buf[i] : ' '; if (i >= at && i < at + nn && k < 64) r->mask |= 1ull << k; }
				r->text[k] = 0;
			}
		}
		if (now() - t0 > budget) return;
	}
	if (s->cpos >= s->ncand || s->nhits >= MAXHITS) s->scanning = 0;
}

/* ================================================================ processes */
static void procs_read(Search *s)
{
	s->nprocs = 0;
#ifdef RENDER
	static const struct { int pid; const char *n; long r; } fake[] = {{1, "init", 640}, {42, "silver-greeter", 9120}, {57, "sh", 1210}, {61, "dropbear", 880}, {73, "udhcpc", 540}};
	for (unsigned i = 0; i < sizeof fake / sizeof fake[0]; i++) { s->procs[s->nprocs].pid = fake[i].pid; snprintf(s->procs[s->nprocs].name, 32, "%s", fake[i].n); s->procs[s->nprocs++].rss = fake[i].r; }
#else
	DIR *d = opendir("/proc");
	struct dirent *de;
	if (!d) return;
	while ((de = readdir(d)) && s->nprocs < 96) {
		int pid = atoi(de->d_name);
		if (pid <= 0) continue;
		char p[64], nm[32] = ""; long rss = 0;
		snprintf(p, sizeof p, "/proc/%d/comm", pid);
		FILE *f = fopen(p, "r"); if (!f) continue;
		if (fgets(nm, sizeof nm, f)) nm[strcspn(nm, "\n")] = 0;
		fclose(f);
		snprintf(p, sizeof p, "/proc/%d/statm", pid);
		f = fopen(p, "r"); if (f) { long a, b; if (fscanf(f, "%ld %ld", &a, &b) == 2) rss = b * 4; fclose(f); }
		s->procs[s->nprocs].pid = pid; snprintf(s->procs[s->nprocs].name, 32, "%s", nm); s->procs[s->nprocs++].rss = rss;
	}
	closedir(d);
#endif
}

/* ================================================================ the query */
static int res_cmp(const void *a, const void *b) { return ((const SRes *)b)->score - ((const SRes *)a)->score; }

static void search_run(Search *s)
{
	s->nres = 0;
	if (!s->q[0]) return;
	if (s->q[0] == '>') {                                           /* run a command in the terminal */
		const char *cmd = s->q + 1; while (*cmd == ' ') cmd++;
		if (*cmd) { SRes *r = &s->res[s->nres++]; r->type = R_RUN; r->node = -1; r->score = 9999; r->line = 0; r->pid = 0; r->mask = 0; snprintf(r->text, sizeof r->text, "%s", cmd); }
		return;
	}
	char tok[4][40]; int nt = 0;
	{ char tmp[64]; snprintf(tmp, sizeof tmp, "%s", s->q);
	  for (char *t = strtok(tmp, " "); t && nt < 4; t = strtok(NULL, " ")) { snprintf(tok[nt], 40, "%s", t); for (char *c = tok[nt]; *c; c++) *c = lc(*c); nt++; } }
	if (!nt) return;
	if (s->scope != SC_TEXT && s->scope != SC_RUNNING) {
		for (int i = 0; i < s->n; i++) {
			Node *nd = &s->nodes[i];
			if (i == 0) continue;
			int k = nd->kind;
			if (s->scope == SC_FILES && k != N_FILE && k != N_DIR) continue;
			if (s->scope == SC_APPS && k != N_APP) continue;
			if (s->scope == SC_SETTINGS && k != N_SET) continue;
			if (s->scope == SC_COMMANDS && k != N_CMD) continue;
			int total = 0, ok = 1; uint64_t mask = 0;
			for (int t = 0; t < nt && ok; t++) {
				int sc = match_name(nd->name, tok[t], &mask);
				if (!sc) {                                         /* maybe a folder above matches: "docs letter" */
					int p = nd->parent;
					for (int d = 0; d < 3 && p > 0 && !sc; d++, p = s->nodes[p].parent) sc = match_name(s->nodes[p].name, tok[t], NULL) / 4;
				}
				if (!sc) ok = 0; else total += sc;
			}
			if (!ok) continue;
			total += k == N_APP ? 300 : k == N_SET ? 250 : k == N_DIR ? 60 : k == N_CMD ? -40 : 0;
			int len = (int)strlen(nd->name); total += len < 40 ? 40 - len : 0;
			char key[300]; node_key(s, i, key, sizeof key);
			int hc = hist_count(s, key); total += hc > 10 ? 300 : hc * 30;
			if (s->nres == MAXRES) { int w = 0; for (int r = 1; r < MAXRES; r++) if (s->res[r].score < s->res[w].score) w = r; if (s->res[w].score >= total) continue; s->nres--; s->res[w] = s->res[s->nres]; }
			SRes *r = &s->res[s->nres++]; r->type = R_NODE; r->node = i; r->score = total; r->line = 0; r->pid = 0; r->mask = mask; r->text[0] = 0;
		}
	}
	if (s->scope == SC_RUNNING || s->scope == SC_ALL) {
		procs_read(s);
		for (int i = 0; i < s->nprocs && s->nres < MAXRES; i++) {
			uint64_t m = 0; int ok = 1, total = 0;
			for (int t = 0; t < nt && ok; t++) { int sc = match_name(s->procs[i].name, tok[t], &m); if (!sc) ok = 0; else total += sc; }
			if (!ok || (s->scope == SC_ALL && total < 700)) continue;
			SRes *r = &s->res[s->nres++]; r->type = R_PROC; r->node = -1; r->pid = s->procs[i].pid; r->score = total + (s->scope == SC_RUNNING ? 400 : 0); r->mask = m; r->line = 0;
			snprintf(r->text, sizeof r->text, "pid %d  -  %ld KB", s->procs[i].pid, s->procs[i].rss);
		}
	}
	qsort(s->res, (size_t)s->nres, sizeof s->res[0], res_cmp);
}

static void search_set(Search *s, const char *q, int scope)
{
	if (q != s->q) snprintf(s->q, sizeof s->q, "%s", q);          /* (callers may pass s->q itself) */
	s->scope = scope;
	compute_answer(s);
	search_run(s);
	s->nhits = 0; s->cpos = 0; s->scanning = 0;                      /* restart the text search */
	if ((scope == SC_ALL || scope == SC_TEXT) && strlen(q) >= 2 && q[0] != '>' && !s->has_ans) {
		cands_build(s);
		s->scanning = s->ncand > 0;
	}
}

static void search_init(Search *s)
{
	memset(s, 0, sizeof *s);
	s->nodes = malloc(sizeof(Node) * NODE_CAP); s->queue = malloc(sizeof(int) * (NODE_CAP / 2 + 8));
	hist_load(s);
	index_seed(s);
}

/* Called every frame: grow the index and walk file contents, a few milliseconds at a time. */
static void search_tick(Search *s, double budget)
{
#ifndef RENDER
	if (s->indexing) {
		index_step(s, budget);
		if (s->q[0] && now() - s->rebuilt > 0.4) { s->rebuilt = now(); search_set(s, s->q, s->scope); }
		else if (!s->indexing && s->q[0]) search_set(s, s->q, s->scope);
		return;
	}
#endif
	if (s->scanning) content_step(s, budget);
}

static int search_count(const Search *s) { return s->nres + s->nhits; }
static const SRes *search_item(const Search *s, int i) { return i < s->nres ? &s->res[i] : &s->hits[i - s->nres]; }
