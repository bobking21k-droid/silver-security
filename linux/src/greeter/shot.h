/*
 * F12 takes a screenshot: the picture on screen is saved as a PNG in ~/Pictures
 * (silveros-YEAR-MONTH-DAY-HOURMINUTESECOND.png). The PNG is written by hand (stored, not
 * compressed, so a bit large but dependency-free).
 */
#pragma once
#include <time.h>

static struct { char msg[160]; double t; } shot = {.t = -100};

static uint32_t shot_crc_tab[256];
static uint32_t shot_crc(uint32_t c, const uint8_t *p, size_t n)
{
	if (!shot_crc_tab[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t v = i; for (int k = 0; k < 8; k++) v = v & 1 ? 0xedb88320u ^ (v >> 1) : v >> 1; shot_crc_tab[i] = v; }
	c = ~c;
	while (n--) c = shot_crc_tab[(c ^ *p++) & 255] ^ (c >> 8);
	return ~c;
}
static void shot_be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static int shot_chunk(FILE *f, const char *type, const uint8_t *data, size_t n)
{
	uint8_t h[8]; shot_be32(h, (uint32_t)n); memcpy(h + 4, type, 4);
	uint32_t c = shot_crc(0, h + 4, 4); c = shot_crc(c, data, n);
	uint8_t t[4]; shot_be32(t, c);
	return fwrite(h, 1, 8, f) == 8 && (n == 0 || fwrite(data, 1, n, f) == n) && fwrite(t, 1, 4, f) == 4;
}

/* write the pixels (0x00RRGGBB, w x h) as an RGB PNG; returns 1 on success */
static int shot_write_png(const char *path, const uint32_t *px, int w, int h)
{
	size_t rowb = (size_t)w * 3 + 1, rawn = rowb * (size_t)h;
	uint8_t *raw = malloc(rawn);
	if (!raw) return 0;
	for (int y = 0; y < h; y++) {
		uint8_t *r = raw + rowb * (size_t)y; *r++ = 0;
		for (int x = 0; x < w; x++) { uint32_t c = px[(size_t)y * w + x]; *r++ = (uint8_t)(c >> 16); *r++ = (uint8_t)(c >> 8); *r++ = (uint8_t)c; }
	}
	FILE *f = fopen(path, "wb");
	if (!f) { free(raw); return 0; }
	static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
	int ok = fwrite(sig, 1, 8, f) == 8;
	uint8_t ihdr[13]; shot_be32(ihdr, (uint32_t)w); shot_be32(ihdr + 4, (uint32_t)h); ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
	ok = ok && shot_chunk(f, "IHDR", ihdr, 13);
	/* zlib stream made of stored blocks, in one IDAT chunk */
	size_t blocks = (rawn + 65534) / 65535, zn = 2 + rawn + blocks * 5 + 4;
	uint8_t *z = malloc(zn);
	if (!z) { fclose(f); free(raw); return 0; }
	size_t o = 0; z[o++] = 0x78; z[o++] = 0x01;
	uint32_t a = 1, b = 0;
	for (size_t i = 0; i < rawn; i += 65535) {
		size_t n = rawn - i < 65535 ? rawn - i : 65535;
		z[o++] = i + n >= rawn; z[o++] = (uint8_t)n; z[o++] = (uint8_t)(n >> 8); z[o++] = (uint8_t)~n; z[o++] = (uint8_t)(~n >> 8);
		memcpy(z + o, raw + i, n); o += n;
		for (size_t k = 0; k < n; k++) { a = (a + raw[i + k]) % 65521; b = (b + a) % 65521; }
	}
	shot_be32(z + o, (b << 16) | a); o += 4;
	ok = ok && shot_chunk(f, "IDAT", z, o) && shot_chunk(f, "IEND", NULL, 0);
	ok = fclose(f) == 0 && ok;
	free(z); free(raw);
	return ok;
}

static void shot_take(void)
{
#ifndef RENDER
	char d[300], p[400];
	snprintf(d, sizeof d, "%s/Pictures", homedir); mkdir(d, 0755);
	time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
	strftime(p, sizeof p, "silveros-%Y-%m-%d-%H%M%S.png", &tm);
	char full[420]; snprintf(full, sizeof full, "%s/%s", d, p);
	if (shot_write_png(full, fb.px, W, H)) snprintf(shot.msg, sizeof shot.msg, "Screenshot saved: Pictures/%s", p);
	else { snprintf(shot.msg, sizeof shot.msg, "Couldn't save the screenshot"); unlink(full); }
	shot.t = now();
#endif
}

static void shot_draw(void)
{
	if (now() - shot.t > 3.5) return;
	float a = sg_sat((3.5f - (float)(now() - shot.t)) / 0.6f);
	float w = sg_text_w(&FT, shot.msg, U * 1.9f, 0.01f) + U * 5, x = (W - w) / 2, y = U * 3;
	sg_rrect(&fb, x, y, w, U * 4.2f, U * 2.1f, 0, RGB(30, 30, 36), 0.9f * a);
	sg_text(&fb, &FT, x + U * 2.5f, y + U * 2.9f, shot.msg, U * 1.9f, WHITE, a, 0.01f);
}
