/*
 * Web search results, read from DuckDuckGo's plain-HTML results page (no API key, no scripts).
 * Each result is: <a class="result__a" href="//duckduckgo.com/l/?uddg=ENCODED_URL&...">Title</a>
 * and a later <a class="result__snippet" ...>text with <b>marks</b></a>. We cut out those two
 * pieces, drop the tags, and decode entities and the percent-encoded address.
 * If the page changes, nothing parses and the interface says so.
 */
#pragma once
#include <string.h>
#include <stdio.h>

typedef struct { char title[160], url[200], snip[260]; } Web;
#define MAXWEB 20

static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

/* text between '>' (just after p) and the closing </a>, tags removed, entities decoded */
static void html_text(const char *p, const char *lim, char *out, size_t cap)
{
	size_t o = 0; int intag = 0;
	for (; p < lim && o + 4 < cap; p++) {
		if (intag) { if (*p == '>') intag = 0; continue; }
		if (*p == '<') { if (!strncmp(p, "</a>", 4)) break; intag = 1; continue; }
		if (*p == '&') {
			static const struct { const char *e; const char *r; } E[] = {{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#x27;", "'"}, {"&#39;", "'"}, {"&nbsp;", " "}, {"&hellip;", "\xE2\x80\xA6"}, {"&ndash;", "-"}, {"&mdash;", "-"}};
			int hit = 0;
			for (unsigned i = 0; i < sizeof E / sizeof E[0]; i++) { size_t l = strlen(E[i].e); if (!strncmp(p, E[i].e, l)) { for (const char *r = E[i].r; *r && o + 4 < cap; r++) out[o++] = *r; p += l - 1; hit = 1; break; } }
			if (hit) continue;
			if (p[1] == '#') {                                            /* numeric reference */
				unsigned cp = 0; const char *q = p + 2; int hex = *q == 'x' || *q == 'X'; if (hex) q++;
				for (; *q && *q != ';' && q - p < 10; q++) cp = cp * (hex ? 16 : 10) + (unsigned)(hexv(*q) < 0 ? 0 : hexv(*q));
				if (*q == ';' && cp) {
					if (cp < 0x80) out[o++] = (char)cp;
					else if (cp < 0x800) { out[o++] = (char)(0xC0 | cp >> 6); out[o++] = (char)(0x80 | (cp & 63)); }
					else { out[o++] = (char)(0xE0 | cp >> 12); out[o++] = (char)(0x80 | (cp >> 6 & 63)); out[o++] = (char)(0x80 | (cp & 63)); }
					p = q; continue;
				}
			}
		}
		if (*p == '\n' || *p == '\r' || *p == '\t') { if (o && out[o - 1] != ' ') out[o++] = ' '; continue; }
		if (*p == ' ' && (!o || out[o - 1] == ' ')) continue;
		out[o++] = *p;
	}
	while (o && out[o - 1] == ' ') o--;
	out[o] = 0;
}

static const char *find_str(const char *p, const char *lim, const char *k)
{
	size_t kl = strlen(k);
	for (; p + kl <= lim; p++) if (*p == k[0] && !memcmp(p, k, kl)) return p;
	return 0;
}

static int web_parse(const char *html, size_t n, Web *out, int max)
{
	int count = 0;
	const char *p = html, *lim = html + n;
	while (count < max && (p = find_str(p, lim, "class=\"result__a\""))) {
		const char *gt = memchr(p, '>', (size_t)(lim - p));
		if (!gt) break;
		Web w; memset(&w, 0, sizeof w);
		const char *u = find_str(p, gt, "uddg=");
		if (u) {                                                           /* the real address, percent-decoded */
			size_t o = 0; u += 5;
			for (; u < gt && *u != '&' && *u != '"' && o + 1 < sizeof w.url; u++) {
				if (*u == '%' && hexv(u[1]) >= 0 && hexv(u[2]) >= 0) { w.url[o++] = (char)(hexv(u[1]) * 16 + hexv(u[2])); u += 2; }
				else w.url[o++] = *u;
			}
			w.url[o] = 0;
		} else { const char *h = find_str(p, gt, "href=\""); if (h) { h += 6; size_t o = 0; while (h < gt && *h != '"' && o + 1 < sizeof w.url) w.url[o++] = *h++; w.url[o] = 0; } }
		html_text(gt + 1, lim, w.title, sizeof w.title);
		const char *next = find_str(gt, lim, "class=\"result__a\"");
		const char *sn = find_str(gt, next ? next : lim, "class=\"result__snippet\"");
		if (sn) { const char *g2 = memchr(sn, '>', (size_t)(lim - sn)); if (g2) html_text(g2 + 1, lim, w.snip, sizeof w.snip); }
		p = gt;
		if (!strncmp(w.url, "https://duckduckgo.com/y.js", 26) || strstr(w.url, "duckduckgo.com/y.js")) continue;      /* ads */
		if (w.title[0] && w.url[0]) out[count++] = w;
	}
	return count;
}

/* "https://www.example.com/a/b" -> "example.com > a > b" for display */
static void web_display_url(const char *url, char *out, size_t cap)
{
	const char *p = strstr(url, "://"); p = p ? p + 3 : url;
	if (!strncmp(p, "www.", 4)) p += 4;
	size_t o = 0;
	for (; *p && o + 4 < cap; p++) { if (*p == '/') { if (p[1]) { out[o++] = ' '; out[o++] = '>'; out[o++] = ' '; } } else if (*p == '?' || *p == '#') break; else out[o++] = *p; }
	out[o] = 0;
}
