/*
 * A small QR code encoder (byte mode, error correction level M, versions 1 to 10: up to 213
 * bytes), so a web address on screen can be opened by pointing a phone at it. Follows the
 * structure of Project Nayuki's reference encoder (MIT licence), written down from the standard.
 *
 *   int n = qr_make("https://example.com", m);   // m: QR_MAX * QR_MAX bytes; n x n modules, 1 = dark
 */
#pragma once
#include <stdint.h>
#include <string.h>

#define QR_MAX (17 + 4 * 10)

static const int8_t QR_ECC_M[11] = {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26};   /* ECC codewords per block */
static const int8_t QR_BLK_M[11] = {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5};              /* number of blocks */

static int qr_raw_modules(int ver)
{
	int r = (16 * ver + 128) * ver + 64;
	if (ver >= 2) { int na = ver / 7 + 2; r -= (25 * na - 10) * na - 55; if (ver >= 7) r -= 36; }
	return r;
}
static uint8_t qr_gmul(uint8_t x, uint8_t y)
{
	int z = 0;
	for (int i = 7; i >= 0; i--) { z = (z << 1) ^ ((z >> 7) * 0x11D); z ^= ((y >> i) & 1) * x; }
	return (uint8_t)z;
}
static void qr_rs(const uint8_t *data, int n, int deg, uint8_t *out)
{
	uint8_t div[32] = {0}, root = 1;
	div[deg - 1] = 1;
	for (int i = 0; i < deg; i++) {
		for (int j = 0; j < deg; j++) { div[j] = qr_gmul(div[j], root); if (j + 1 < deg) div[j] ^= div[j + 1]; }
		root = qr_gmul(root, 0x02);
	}
	memset(out, 0, (size_t)deg);
	for (int i = 0; i < n; i++) {
		uint8_t f = data[i] ^ out[0];
		memmove(out, out + 1, (size_t)deg - 1); out[deg - 1] = 0;
		for (int j = 0; j < deg; j++) out[j] ^= qr_gmul(div[j], f);
	}
}

static uint8_t *qm, *qf; static int qn;                         /* modules, "is a function pattern", size */
static void qset(int x, int y, int dark) { qm[y * QR_MAX + x] = (uint8_t)dark; qf[y * QR_MAX + x] = 1; }
static void q_finder(int cx, int cy)
{
	for (int dy = -4; dy <= 4; dy++) for (int dx = -4; dx <= 4; dx++) {
		int x = cx + dx, y = cy + dy, d = dx < 0 ? -dx : dx; if ((dy < 0 ? -dy : dy) > d) d = dy < 0 ? -dy : dy;
		if (x >= 0 && x < qn && y >= 0 && y < qn) qset(x, y, d != 2 && d != 4);
	}
}
static void q_align(int cx, int cy)
{
	for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) { int d = dx < 0 ? -dx : dx; if ((dy < 0 ? -dy : dy) > d) d = dy < 0 ? -dy : dy; qset(cx + dx, cy + dy, d != 1); }
}
static void q_format(int mask)
{
	int data = (0 << 3) | mask, rem = data;                         /* level M is 00 */
	for (int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
	int bits = ((data << 10) | rem) ^ 0x5412;
	for (int i = 0; i <= 5; i++) qset(8, i, (bits >> i) & 1);
	qset(8, 7, (bits >> 6) & 1); qset(8, 8, (bits >> 7) & 1); qset(7, 8, (bits >> 8) & 1);
	for (int i = 9; i < 15; i++) qset(14 - i, 8, (bits >> i) & 1);
	for (int i = 0; i < 8; i++) qset(qn - 1 - i, 8, (bits >> i) & 1);
	for (int i = 8; i < 15; i++) qset(8, qn - 15 + i, (bits >> i) & 1);
	qset(8, qn - 8, 1);                                             /* the dark module */
}

static int qr_make(const char *text, uint8_t *out)
{
	static uint8_t mods[QR_MAX * QR_MAX], func[QR_MAX * QR_MAX];
	int len = (int)strlen(text), ver = 0;
	for (int v = 1; v <= 10; v++) {
		int cap = qr_raw_modules(v) / 8 - QR_ECC_M[v] * QR_BLK_M[v];
		if (4 + 8 + len * 8 <= cap * 8) { ver = v; break; }
	}
	if (!ver) return 0;
	qn = ver * 4 + 17; qm = mods; qf = func;
	memset(mods, 0, sizeof mods); memset(func, 0, sizeof func);
	/* the data: mode, length, bytes, terminator, padding */
	int datacap = qr_raw_modules(ver) / 8 - QR_ECC_M[ver] * QR_BLK_M[ver];
	uint8_t data[400] = {0}; int bit = 0;
#define PUT(v, nb) for (int _i = (nb) - 1; _i >= 0; _i--, bit++) data[bit >> 3] |= (uint8_t)((((v) >> _i) & 1) << (7 - (bit & 7)))
	PUT(4, 4); PUT(len, 8);
	for (int i = 0; i < len; i++) PUT((uint8_t)text[i], 8);
	int term = datacap * 8 - bit; if (term > 4) term = 4; PUT(0, term);
	bit = (bit + 7) & ~7;
	for (int pad = 0xEC; bit < datacap * 8; pad ^= 0xEC ^ 0x11) PUT(pad, 8);
#undef PUT
	/* split into blocks, add error correction, interleave */
	int nb = QR_BLK_M[ver], ecl = QR_ECC_M[ver], raw = qr_raw_modules(ver) / 8, nshort = nb - raw % nb, slen = raw / nb;
	uint8_t blocks[8][160], all[400]; int k = 0;
	for (int i = 0; i < nb; i++) {
		int dl = slen - ecl + (i < nshort ? 0 : 1);
		uint8_t ecc[32];
		qr_rs(data + k, dl, ecl, ecc);
		memcpy(blocks[i], data + k, (size_t)dl); k += dl;
		if (i < nshort) { blocks[i][dl] = 0; memcpy(blocks[i] + dl + 1, ecc, (size_t)ecl); }
		else memcpy(blocks[i] + dl, ecc, (size_t)ecl);
	}
	int na = 0;
	for (int i = 0; i <= slen; i++) for (int j = 0; j < nb; j++) if (i != slen - ecl || j >= nshort) all[na++] = blocks[j][i];
	/* function patterns */
	for (int i = 0; i < qn; i++) { qset(6, i, i % 2 == 0); qset(i, 6, i % 2 == 0); }
	q_finder(3, 3); q_finder(qn - 4, 3); q_finder(3, qn - 4);
	if (ver >= 2) {
		int nal = ver / 7 + 2, step = (ver * 8 + nal * 3 + 5) / (nal * 4 - 4) * 2, pos[8];
		for (int i = nal - 1, p = qn - 7; i >= 1; i--, p -= step) pos[i] = p;
		pos[0] = 6;
		for (int i = 0; i < nal; i++) for (int j = 0; j < nal; j++)
			if (!((i == 0 && j == 0) || (i == 0 && j == nal - 1) || (i == nal - 1 && j == 0))) q_align(pos[i], pos[j]);
	}
	q_format(0);                                                    /* reserve the format areas */
	if (ver >= 7) {
		int rem = ver; for (int i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
		long bits = (long)ver << 12 | rem;
		for (int i = 0; i < 18; i++) { int b = (bits >> i) & 1, a = qn - 11 + i % 3, c = i / 3; qset(a, c, b); qset(c, a, b); }
	}
	/* the codewords, in the zigzag */
	int bi = 0;
	for (int right = qn - 1; right >= 1; right -= 2) {
		if (right == 6) right = 5;
		for (int vert = 0; vert < qn; vert++)
			for (int j = 0; j < 2; j++) {
				int x = right - j, up = ((right + 1) & 2) == 0, y = up ? qn - 1 - vert : vert;
				if (!func[y * QR_MAX + x] && bi < na * 8) { mods[y * QR_MAX + x] = (all[bi >> 3] >> (7 - (bi & 7))) & 1; bi++; }
			}
	}
	/* mask 0 ((x + y) % 2 == 0) over the data, then the real format bits */
	for (int y = 0; y < qn; y++) for (int x = 0; x < qn; x++) if (!func[y * QR_MAX + x] && (x + y) % 2 == 0) mods[y * QR_MAX + x] ^= 1;
	q_format(0);
	for (int y = 0; y < qn; y++) memcpy(out + y * qn, mods + y * QR_MAX, (size_t)qn);
	return qn;
}
