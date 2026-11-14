/*
 * The Search pane's moving background, on a loop:
 *
 *   the march   a packed crowd of identical hooded figures crossing a turning swirl (drawn, ytui.h)
 *   the rooms   a video clip, played as-is but tinted black and red
 *
 * The clip comes from /usr/share/silver/search-bg.svid, made on the build machine by
 * scripts/make-search-bg.sh from a video you supply (it is not part of the source tree).
 * Frames are stored as small greyscale pictures; we scale them up and map grey to
 * black -> dark red -> pale as they play. Without the file only the march plays.
 * Scenes change with a short glitch.
 */
#pragma once

typedef struct { int w, h, n, fps; const uint8_t *frames; uint8_t *blob; int tried; } Clip;
static Clip clip;
static uint32_t clip_lut[256];

static void clip_load(void)
{
	if (clip.tried) return;
	clip.tried = 1;
	const char *path = getenv("SILVER_SEARCH_BG");
	if (!path) path = "/usr/share/silver/search-bg.svid";
	FILE *f = fopen(path, "rb");
	if (!f) return;
	fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
	if (n < 12 || n > 64L << 20 || !(clip.blob = malloc((size_t)n))) { fclose(f); return; }
	if (fread(clip.blob, 1, (size_t)n, f) != (size_t)n || memcmp(clip.blob, "SVID", 4)) { fclose(f); free(clip.blob); clip.blob = 0; return; }
	fclose(f);
	clip.w = clip.blob[4] | clip.blob[5] << 8; clip.h = clip.blob[6] | clip.blob[7] << 8;
	clip.n = clip.blob[8] | clip.blob[9] << 8; clip.fps = clip.blob[10] | clip.blob[11] << 8;
	if (clip.w < 2 || clip.h < 2 || !clip.n || !clip.fps || 12 + (long)clip.w * clip.h * clip.n > n) { free(clip.blob); clip.blob = 0; clip.n = 0; return; }
	clip.frames = clip.blob + 12;
	for (int v = 0; v < 256; v++) {                         /* grey -> black, a held-back red, then pale highlights */
		float x = v / 255.0f, hi = x * x * x;
		clip_lut[v] = RGB((int)(255 * sg_sat(x * 0.86f + hi * 0.16f)), (int)(255 * sg_sat(x * 0.30f + hi * 0.60f)), (int)(255 * sg_sat(x * 0.32f + hi * 0.58f)));
	}
}
static float clip_len(void) { return clip.n ? (float)clip.n / clip.fps : 0; }

/* draw frame `fi` over the whole screen, filled and pushed in by `zoom`, with the clip's centre
 * placed at `focus` across the screen (so it shows beside the results, not under them); bilinear */
static void clip_draw(int fi, float zoom, float focus)
{
	const uint8_t *src = clip.frames + (size_t)(fi % clip.n) * clip.w * clip.h;
	float sc = sg_max((float)W / clip.w, (float)H / clip.h) * zoom;
	float ox = clip.w * 0.5f - W * focus / sc, oy = (clip.h - H / sc) / 2;
	int step = (int)(65536 / sc), sx0 = (int)(ox * 65536);
	for (int y = 0; y < H; y++) {
		float fy = oy + (y + 0.5f) / sc - 0.5f;
		int y0 = (int)fy; if (fy < 0) { y0 = 0; fy = 0; }
		if (y0 >= clip.h - 1) { y0 = clip.h - 2; fy = (float)(clip.h - 1); }
		int wy = (int)((fy - y0) * 256); if (wy > 255) wy = 255;
		const uint8_t *r0 = src + y0 * clip.w, *r1 = r0 + clip.w;
		uint32_t *d = fb.px + y * W;
		int sx = sx0 + step / 2 - 32768;
		for (int x = 0; x < W; x++, sx += step) {
			int x0 = sx >> 16, wx = (sx >> 8) & 255;
			if (sx < 0) { x0 = 0; wx = 0; }
			if (x0 >= clip.w - 1) { x0 = clip.w - 2; wx = 255; }
			int top = r0[x0] * (256 - wx) + r0[x0 + 1] * wx, bot = r1[x0] * (256 - wx) + r1[x0 + 1] * wx;
			d[x] = clip_lut[(top * (256 - wy) + bot * wy) >> 16];
		}
	}
}

/* a short glitch between scenes: rows torn sideways, a dark-red tint */
static void glitch(float amt, float seed)
{
	if (amt <= 0) return;
	uint32_t *tmp = malloc((size_t)W * 4);
	if (!tmp) return;
	int band = (int)(U * 2.5f) + 1;
	for (int y = 0; y < H; y += band) {
		float n = sg_fract(sg_sin(y * 12.9898f + seed * 78.233f) * 43758.5453f);
		if (n > amt) continue;
		int off = (int)((n - 0.5f) * W * 0.25f * amt);
		for (int yy = y; yy < y + band && yy < H; yy++) {
			uint32_t *row = fb.px + yy * W;
			for (int x = 0; x < W; x++) { int sx = x - off; tmp[x] = row[sx < 0 ? 0 : sx >= W ? W - 1 : sx]; }
			memcpy(row, tmp, (size_t)W * 4);
		}
	}
	free(tmp);
	sg_rect(&fb, 0, 0, W, H, RGB(120, 10, 16), 0.12f * amt);
}

#define MARCH_LEN 7.0f
static void parade_bg(float t);
static void scene_bg(float t)
{
	clip_load();
	float rooms = clip.n ? clip_len() * 2 : 0;                  /* the clip plays twice, then the march again */
	float loop = MARCH_LEN + rooms, u = rooms > 0 ? fmodf(s_age, loop) : s_age;
	int k = u < MARCH_LEN || rooms <= 0 ? 0 : 1;
	float into = k ? u - MARCH_LEN : u, left = (k ? rooms : MARCH_LEN) - into;
	if (!k) parade_bg(t);
	else {
		clip_draw((int)(into * clip.fps), 1.22f, 0.74f);
		sg_rect(&fb, 0, 0, W, H, BLACK, 0.18f);                   /* a little darker, for reading */
	}
	if (rooms > 0) {                                                /* tear the picture as one scene hands over to the next */
		if (s_age > 1.0f && into < 0.22f) glitch(1 - into / 0.22f, (float)k + into);
		else if (left < 0.12f) glitch(1 - left / 0.12f, (float)k * 3 + left);
	}
	/* keep the top calm for the search bar */
	for (int y = 0; y < H * 0.30f; y++) { float f = 1 - (float)y / (H * 0.30f); sg_rect(&fb, 0, y, W, 1, BLACK, 0.55f * f); }
}
