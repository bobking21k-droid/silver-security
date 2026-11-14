/*
 * YouTube search results, read from YouTube's public search page (no API key).
 *
 * The page embeds its results as JSON ("ytInitialData"). We do not parse the whole document:
 * each video is a {"videoRenderer":{...}} object, so we cut those out with a brace matcher that
 * respects strings, then pull the few fields we show. If YouTube changes its page, parsing finds
 * nothing and the interface says so instead of showing garbage.
 *
 * Requests go through `silver-fetch`, which checks the server's certificate.
 */
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>

typedef struct {
	char id[16];
	char title[160], channel[64], views[40], length[16], age[32], desc[220];
	int live;
} Video;
#define MAXVID 24

/* Read a JSON string starting at the opening quote of p; unescape into out (UTF-8). Returns a
 * pointer just past the closing quote. */
static const char *json_str(const char *p, char *out, size_t cap)
{
	size_t o = 0;
	if (*p != '"') { if (cap) out[0] = 0; return p; }
	p++;
	while (*p && *p != '"') {
		unsigned c = (unsigned char)*p++;
		if (c == '\\') {
			c = (unsigned char)*p++;
			if (c == 'n' || c == 't' || c == 'r') c = ' ';
			else if (c == 'u') {
				unsigned cp = 0;
				for (int i = 0; i < 4 && *p; i++, p++) cp = cp * 16 + (unsigned)((*p >= 'a') ? *p - 'a' + 10 : (*p >= 'A') ? *p - 'A' + 10 : *p - '0');
				if (cp >= 0xD800 && cp < 0xDC00 && p[0] == '\\' && p[1] == 'u') {              /* a surrogate pair */
					unsigned lo = 0; p += 2;
					for (int i = 0; i < 4 && *p; i++, p++) lo = lo * 16 + (unsigned)((*p >= 'a') ? *p - 'a' + 10 : (*p >= 'A') ? *p - 'A' + 10 : *p - '0');
					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
				}
				if (o + 4 < cap) {
					if (cp < 0x80) out[o++] = (char)cp;
					else if (cp < 0x800) { out[o++] = (char)(0xC0 | cp >> 6); out[o++] = (char)(0x80 | (cp & 63)); }
					else if (cp < 0x10000) { out[o++] = (char)(0xE0 | cp >> 12); out[o++] = (char)(0x80 | (cp >> 6 & 63)); out[o++] = (char)(0x80 | (cp & 63)); }
					else { out[o++] = (char)(0xF0 | cp >> 18); out[o++] = (char)(0x80 | (cp >> 12 & 63)); out[o++] = (char)(0x80 | (cp >> 6 & 63)); out[o++] = (char)(0x80 | (cp & 63)); }
				}
				continue;
			}
		}
		if (o + 1 < cap) out[o++] = (char)c;
	}
	if (cap) out[o < cap ? o : cap - 1] = 0;
	if (cap) out[o < cap ? o : cap - 1] = 0;
	return *p == '"' ? p + 1 : p;
}

/* end of the JSON value (object or array) that starts at p[0] = '{' or '[' */
static const char *json_end(const char *p, const char *lim)
{
	int depth = 0;
	for (; p < lim; p++) {
		if (*p == '"') { for (p++; p < lim && *p != '"'; p++) if (*p == '\\') p++; continue; }
		if (*p == '{' || *p == '[') depth++;
		else if (*p == '}' || *p == ']') { if (--depth == 0) return p + 1; }
	}
	return 0;
}

/* find `key` (like "\"videoId\":") in [p, lim) */
static const char *json_find(const char *p, const char *lim, const char *key)
{
	size_t kl = strlen(key);
	for (; p + kl <= lim; p++) if (*p == key[0] && !memcmp(p, key, kl)) return p + kl;
	return 0;
}

/* all the "text":"..." strings of the runs array that starts right at p, joined */
static void join_runs(const char *p, const char *lim, char *out, size_t cap)
{
	size_t o = 0;
	out[0] = 0;
	if (*p != '[') return;
	const char *e = json_end(p, lim);
	if (!e) return;
	for (const char *q = p; (q = json_find(q, e, "\"text\":\"")) && o + 2 < cap;) {
		char tmp[200];
		q = json_str(q - 1, tmp, sizeof tmp);
		size_t l = strlen(tmp);
		if (o + l >= cap) l = cap - o - 1;
		memcpy(out + o, tmp, l); o += l; out[o] = 0;
	}
}

/* a text object: {"simpleText":"..."} or {"runs":[...]} (also skips an "accessibility" block) */
static void text_obj(const char *obj, const char *lim, const char *key, char *out, size_t cap)
{
	out[0] = 0;
	const char *p = json_find(obj, lim, key);
	if (!p || *p != '{') return;
	const char *e = json_end(p, lim);
	if (!e) return;
	const char *s = json_find(p, e, "\"simpleText\":\"");
	if (s) { json_str(s - 1, out, cap); return; }
	const char *r = json_find(p, e, "\"runs\":");
	if (r) join_runs(r, e, out, cap);
}

/* Parse a results page. Returns how many videos were found. */
static int yt_parse(const char *html, size_t n, Video *out, int max)
{
	int count = 0;
	const char *p = html, *lim = html + n;
	const char *data = json_find(html, lim, "ytInitialData");
	if (data) p = data;
	while (count < max && (p = json_find(p, lim, "\"videoRenderer\":{"))) {
		const char *obj = p - 1, *e = json_end(obj, lim);
		if (!e) break;
		Video v; memset(&v, 0, sizeof v);
		const char *q = json_find(obj, e, "\"videoId\":\"");
		if (!q) { p = e; continue; }
		json_str(q - 1, v.id, sizeof v.id);
		text_obj(obj, e, "\"title\":", v.title, sizeof v.title);
		text_obj(obj, e, "\"ownerText\":", v.channel, sizeof v.channel);
		text_obj(obj, e, "\"viewCountText\":", v.views, sizeof v.views);
		text_obj(obj, e, "\"publishedTimeText\":", v.age, sizeof v.age);
		text_obj(obj, e, "\"lengthText\":", v.length, sizeof v.length);
		const char *d = json_find(obj, e, "\"detailedMetadataSnippets\":[");
		if (d) { const char *r = json_find(d, e, "\"snippetText\":{\"runs\":"); if (r) join_runs(r, e, v.desc, sizeof v.desc); }
		v.live = json_find(obj, e, "BADGE_STYLE_TYPE_LIVE_NOW") != 0;
		int dup = 0;
		for (int i = 0; i < count; i++) if (!strcmp(out[i].id, v.id)) dup = 1;
		if (v.id[0] && v.title[0] && !dup) out[count++] = v;
		p = e;
	}
	return count;
}

/* percent-encode a query for a URL */
static void url_encode(const char *s, char *out, size_t cap)
{
	size_t o = 0;
	for (; *s && o + 4 < cap; s++) {
		unsigned char c = (unsigned char)*s;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') out[o++] = (char)c;
		else if (c == ' ') out[o++] = '+';
		else o += (size_t)snprintf(out + o, cap - o, "%%%02X", c);
	}
	out[o] = 0;
}
