/*
 * silver-fetch -- HTTPS GET for SilverOS, with real certificate checking.
 *
 *   silver-fetch [-o file] [-m maxbytes] [-t seconds] [-H "Name: value"]... https://host/path
 *
 * Writes the response body to stdout (or -o file). TLS is BearSSL; the server's certificate
 * chain must lead to one of the root certificates in /etc/ssl/cacert.pem (or $SILVER_CA_FILE)
 * and match the host name. Follows up to 5 redirects. Understands chunked transfer encoding.
 * Honours $HTTPS_PROXY (http://[user:pass@]host:port) with CONNECT.
 *
 * Exit status: 0 ok, 2 usage, 3 cannot connect, 4 TLS/certificate failure, 5 HTTP error
 * (the status code is printed on stderr as "HTTP 403"), 6 too slow or too big.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "bearssl.h"

#ifndef BUILD_EPOCH
#define BUILD_EPOCH 1760000000L
#endif

static int quiet;
static void die(int code, const char *fmt, const char *a)
{
	if (!quiet) { fprintf(stderr, "silver-fetch: "); fprintf(stderr, fmt, a); fprintf(stderr, "\n"); }
	exit(code);
}

/* ---------------------------------------------------------------- trust anchors from a PEM bundle */
typedef struct { unsigned char *p; size_t n, cap; } Vec;
static void vec_add(Vec *v, const void *d, size_t n)
{
	if (v->n + n > v->cap) { v->cap = (v->n + n) * 2 + 256; v->p = realloc(v->p, v->cap); if (!v->p) die(3, "out of memory%s", ""); }
	memcpy(v->p + v->n, d, n); v->n += n;
}
static void vec_cb(void *ctx, const void *d, size_t n) { vec_add(ctx, d, n); }

static br_x509_trust_anchor *anchors;
static size_t nanchors;

static void anchor_add(const unsigned char *der, size_t len)
{
	br_x509_decoder_context dc;
	Vec dn = {0};
	br_x509_decoder_init(&dc, vec_cb, &dn);
	br_x509_decoder_push(&dc, der, len);
	br_x509_pkey *pk = br_x509_decoder_get_pkey(&dc);
	if (!pk) { free(dn.p); return; }
	anchors = realloc(anchors, (nanchors + 1) * sizeof *anchors);
	br_x509_trust_anchor *ta = &anchors[nanchors++];
	memset(ta, 0, sizeof *ta);
	ta->dn.data = dn.p; ta->dn.len = dn.n;
	if (br_x509_decoder_isCA(&dc)) ta->flags |= BR_X509_TA_CA;
	ta->pkey.key_type = pk->key_type;
	if (pk->key_type == BR_KEYTYPE_RSA) {
		ta->pkey.key.rsa.n = memcpy(malloc(pk->key.rsa.nlen), pk->key.rsa.n, pk->key.rsa.nlen); ta->pkey.key.rsa.nlen = pk->key.rsa.nlen;
		ta->pkey.key.rsa.e = memcpy(malloc(pk->key.rsa.elen), pk->key.rsa.e, pk->key.rsa.elen); ta->pkey.key.rsa.elen = pk->key.rsa.elen;
	} else if (pk->key_type == BR_KEYTYPE_EC) {
		ta->pkey.key.ec.curve = pk->key.ec.curve;
		ta->pkey.key.ec.q = memcpy(malloc(pk->key.ec.qlen), pk->key.ec.q, pk->key.ec.qlen); ta->pkey.key.ec.qlen = pk->key.ec.qlen;
	} else { nanchors--; free(dn.p); }
}

static void load_anchors(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) die(4, "cannot read the root certificates (%s)", path);
	br_pem_decoder_context pc;
	Vec der = {0};
	unsigned char buf[4096];
	int in_cert = 0;
	br_pem_decoder_init(&pc);
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
		size_t off = 0;
		while (off < n) {
			off += br_pem_decoder_push(&pc, buf + off, n - off);
			switch (br_pem_decoder_event(&pc)) {
			case BR_PEM_BEGIN_OBJ:
				in_cert = !strcmp(br_pem_decoder_name(&pc), "CERTIFICATE");
				if (in_cert) { der.n = 0; br_pem_decoder_setdest(&pc, vec_cb, &der); } else br_pem_decoder_setdest(&pc, 0, 0);
				break;
			case BR_PEM_END_OBJ:
				if (in_cert && der.n) anchor_add(der.p, der.n);
				in_cert = 0;
				break;
			case BR_PEM_ERROR: fclose(f); die(4, "bad certificate file %s", path);
			}
		}
	}
	fclose(f); free(der.p);
	if (!nanchors) die(4, "no certificates in %s", path);
}

/* ---------------------------------------------------------------- sockets */
static int tcp_connect(const char *host, const char *port)
{
	struct addrinfo hints = {0}, *res, *p;
	hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, port, &hints, &res)) die(3, "cannot find %s", host);
	int fd = -1;
	for (p = res; p; p = p->ai_next) {
		fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (fd < 0) continue;
		struct timeval tv = {15, 0};
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
		if (!connect(fd, p->ai_addr, p->ai_addrlen)) break;
		close(fd); fd = -1;
	}
	freeaddrinfo(res);
	if (fd < 0) die(3, "cannot connect to %s", host);
	return fd;
}
static int sock_read(void *ctx, unsigned char *buf, size_t len)
{
	for (;;) { ssize_t r = read(*(int *)ctx, buf, len); if (r <= 0) { if (r < 0 && errno == EINTR) continue; return -1; } return (int)r; }
}
static int sock_write(void *ctx, const unsigned char *buf, size_t len)
{
	for (;;) { ssize_t r = write(*(int *)ctx, buf, len); if (r <= 0) { if (r < 0 && errno == EINTR) continue; return -1; } return (int)r; }
}

static void b64(const char *in, char *out)
{
	static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t n = strlen(in), i, o = 0;
	for (i = 0; i + 2 < n; i += 3) { out[o++] = t[in[i] >> 2]; out[o++] = t[(in[i] & 3) << 4 | (in[i + 1] >> 4 & 15)]; out[o++] = t[(in[i + 1] & 15) << 2 | (in[i + 2] >> 6 & 3)]; out[o++] = t[in[i + 2] & 63]; }
	if (n - i == 1) { out[o++] = t[in[i] >> 2]; out[o++] = t[(in[i] & 3) << 4]; out[o++] = '='; out[o++] = '='; }
	else if (n - i == 2) { out[o++] = t[in[i] >> 2]; out[o++] = t[(in[i] & 3) << 4 | (in[i + 1] >> 4 & 15)]; out[o++] = t[(in[i + 1] & 15) << 2]; out[o++] = '='; }
	out[o] = 0;
}

/* connect through an HTTP proxy with CONNECT, if $HTTPS_PROXY is set */
static int open_connection(const char *host, const char *port)
{
	const char *px = getenv("HTTPS_PROXY"); if (!px) px = getenv("https_proxy");
	if (!px || !*px) return tcp_connect(host, port);
	char tmp[400]; snprintf(tmp, sizeof tmp, "%s", px);
	char *s = strstr(tmp, "://"); s = s ? s + 3 : tmp;
	char *at = strrchr(s, '@'), auth[300] = "", b[420] = "";
	if (at) { *at = 0; snprintf(auth, sizeof auth, "%s", s); b64(auth, b); s = at + 1; }
	char *sl = strchr(s, '/'); if (sl) *sl = 0;
	char *colon = strrchr(s, ':'), *pport = "3128";
	if (colon) { *colon = 0; pport = colon + 1; }
	int fd = tcp_connect(s, pport);
	char req[900];
	int n = snprintf(req, sizeof req, "CONNECT %s:%s HTTP/1.1\r\nHost: %s:%s\r\n%s%s%s\r\n", host, port, host, port, b[0] ? "Proxy-Authorization: Basic " : "", b, b[0] ? "\r\n" : "");
	if (sock_write(&fd, (unsigned char *)req, (size_t)n) < 0) die(3, "proxy refused the connection%s", "");
	char resp[2048]; size_t got = 0;
	while (got < sizeof resp - 1) {
		ssize_t r = read(fd, resp + got, 1);
		if (r <= 0) die(3, "proxy closed the connection%s", "");
		got++; resp[got] = 0;
		if (got >= 4 && !memcmp(resp + got - 4, "\r\n\r\n", 4)) break;
	}
	if (!strstr(resp, " 200")) die(3, "the proxy would not connect (%.40s)", resp);
	return fd;
}

/* ---------------------------------------------------------------- one HTTPS request */
typedef struct { char host[256], port[8], path[2048]; } Url;
static void url_parse(const char *u, Url *o)
{
	if (strncmp(u, "https://", 8)) die(2, "only https:// URLs are allowed (%s)", u);
	const char *h = u + 8, *sl = strchr(h, '/');
	size_t hl = sl ? (size_t)(sl - h) : strlen(h);
	if (hl >= sizeof o->host) die(2, "host name too long%s", "");
	char hp[256]; memcpy(hp, h, hl); hp[hl] = 0;
	char *c = strrchr(hp, ':');
	snprintf(o->port, sizeof o->port, "443");
	if (c) { *c = 0; snprintf(o->port, sizeof o->port, "%s", c + 1); }
	snprintf(o->host, sizeof o->host, "%s", hp);
	snprintf(o->path, sizeof o->path, "%s", sl ? sl : "/");
}

static char *headers[8]; static int nheaders;
static long maxbytes = 6 * 1024 * 1024;
static int outfd = 1;

static int fetch_one(const Url *u, char *location, size_t locn)
{
	static br_ssl_client_context sc;
	static br_x509_minimal_context xc;
	static unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
	static br_sslio_context ioc;
	int fd = open_connection(u->host, u->port);
	br_ssl_client_init_full(&sc, &xc, anchors, nanchors);
	time_t now = time(NULL); if (now < BUILD_EPOCH) now = BUILD_EPOCH;          /* a PC with an unset clock */
	br_x509_minimal_set_time(&xc, (uint32_t)(now / 86400 + 719528), (uint32_t)(now % 86400));
	br_ssl_engine_set_buffer(&sc.eng, iobuf, sizeof iobuf, 1);
	br_ssl_client_reset(&sc, u->host, 0);
	br_sslio_init(&ioc, &sc.eng, sock_read, &fd, sock_write, &fd);

	char req[4096];
	int n = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: SilverOS/0.1\r\nAccept: */*\r\nAccept-Language: en\r\n", u->path, u->host);
	for (int i = 0; i < nheaders; i++) n += snprintf(req + n, sizeof req - (size_t)n, "%s\r\n", headers[i]);
	n += snprintf(req + n, sizeof req - (size_t)n, "Connection: close\r\n\r\n");
	if (br_sslio_write_all(&ioc, req, (size_t)n) < 0 || br_sslio_flush(&ioc) < 0) {
		int e = br_ssl_engine_last_error(&sc.eng);
		die(4, e >= BR_ERR_X509_OK && e < BR_ERR_X509_OK + 64 ? "the server's certificate was rejected (code %s)" : "secure connection failed (code %s)", e ? (snprintf(req, 16, "%d", e), req) : "0");
	}
	/* headers */
	static char hd[16384]; size_t hn = 0;
	memset(hd, 0, sizeof hd);
	while (hn < sizeof hd - 1) {
		unsigned char c;
		if (br_sslio_read(&ioc, &c, 1) != 1) die(3, "connection ended during the headers%s", "");
		hd[hn++] = (char)c; hd[hn] = 0;
		if (hn >= 4 && !memcmp(hd + hn - 4, "\r\n\r\n", 4)) break;
	}
	int status = atoi(hd + 9);
	long clen = -1; int chunked = 0;
	location[0] = 0;
	for (char *line = hd; (line = strchr(line, '\n')) && line[1];) {            /* stop at the end: never read old headers */
		line++;
		if (!strncasecmp(line, "content-length:", 15)) clen = atol(line + 15);
		else if (!strncasecmp(line, "transfer-encoding:", 18) && strcasestr(line, "chunked")) chunked = 1;
		else if (!strncasecmp(line, "location:", 9)) { char *v = line + 9; while (*v == ' ') v++; size_t l = strcspn(v, "\r\n"); if (l >= locn) l = locn - 1; memcpy(location, v, l); location[l] = 0; }
	}
	if (status >= 300 && status < 400 && location[0]) { close(fd); return status; }
	if (status < 200 || status >= 300) { if (!quiet) fprintf(stderr, "HTTP %d\n", status); exit(5); }
	/* body */
	unsigned char buf[8192]; long total = 0;
	if (chunked) {
		for (;;) {
			char ln[32]; int k = 0; unsigned char c;
			while (k < 30) { if (br_sslio_read(&ioc, &c, 1) != 1) goto done; if (c == '\n') break; if (c != '\r') ln[k++] = (char)c; }
			ln[k] = 0;
			long cs = strtol(ln, NULL, 16);
			if (cs <= 0) break;
			while (cs > 0) {
				int r = br_sslio_read(&ioc, buf, (size_t)(cs < (long)sizeof buf ? cs : (long)sizeof buf));
				if (r <= 0) goto done;
				if (write(outfd, buf, (size_t)r) < 0) exit(6);
				cs -= r; total += r; if (total > maxbytes) exit(6);
			}
			if (br_sslio_read(&ioc, &c, 1) != 1 || (c == '\r' && br_sslio_read(&ioc, &c, 1) != 1)) goto done;
		}
	} else {
		for (;;) {
			long want = clen >= 0 ? clen - total : (long)sizeof buf;
			if (want <= 0) break;
			int r = br_sslio_read(&ioc, buf, (size_t)(want < (long)sizeof buf ? want : (long)sizeof buf));
			if (r <= 0) break;
			if (write(outfd, buf, (size_t)r) < 0) exit(6);
			total += r; if (total > maxbytes) exit(6);
		}
	}
done:
	/* No TLS goodbye: br_sslio_close() spins when the server has already hung up, and the body is
	 * framed by its length or its chunk markers, so we know it is complete. */
	close(fd);
	if (clen >= 0 && !chunked && total < clen) exit(6);                               /* cut short */
	return 200;
}

static void on_alarm(int s) { (void)s; _exit(6); }

int main(int argc, char **argv)
{
	int timeout = 40;
	const char *url = NULL, *out = NULL;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
		else if (!strcmp(argv[i], "-m") && i + 1 < argc) maxbytes = atol(argv[++i]);
		else if (!strcmp(argv[i], "-H") && i + 1 < argc && nheaders < 8) headers[nheaders++] = argv[++i];
		else if (!strcmp(argv[i], "-q")) quiet = 1;
		else if (!strcmp(argv[i], "-t") && i + 1 < argc) timeout = atoi(argv[++i]);
		else if (argv[i][0] != '-') url = argv[i];
		else die(2, "unknown option %s", argv[i]);
	}
	if (!url) die(2, "usage: silver-fetch [-o file] [-m bytes] [-t seconds] [-H header] https://host/path%s", "");
	signal(SIGALRM, on_alarm); signal(SIGPIPE, SIG_IGN);
	alarm(timeout > 0 ? (unsigned)timeout : 40);
	if (out) { outfd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0600); if (outfd < 0) die(2, "cannot write %s", out); }
	const char *ca = getenv("SILVER_CA_FILE");
	load_anchors(ca && *ca ? ca : "/etc/ssl/cacert.pem");
	Url u; url_parse(url, &u);
	for (int hops = 0; hops < 6; hops++) {
		char loc[2048];
		int st = fetch_one(&u, loc, sizeof loc);
		if (st == 200) return 0;
		if (loc[0] == '/') snprintf(u.path, sizeof u.path, "%s", loc);                      /* same host */
		else url_parse(loc, &u);
	}
	die(5, "too many redirects%s", "");
	return 5;
}
