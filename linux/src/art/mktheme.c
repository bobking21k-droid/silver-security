/*
 * mktheme -- draws the artwork for the live-USB GRUB menu with the same renderer and scenes
 * as the boot chooser, so the two match. Runs on the build machine.
 *
 *   mktheme <outdir> <os name> <version>
 * writes raw BGRA images that mkart.py turns into PNGs:
 *   background.raw (1280x720), tile_silveros.raw, tile_windows.raw, tile_advanced.raw (600x128,
 *   dark on white: mkart.py converts darkness to transparency)
 */
#include <stdio.h>
#include <stdlib.h>
#include "../common/fonts.h"
#include "../silverboot/scenes.h"

void *sb_alloc(size_t n) { return malloc(n); }

static void save(const char *dir, const char *name, Surf *s)
{
	char path[512];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE *f = fopen(path, "wb");
	if (!f) { perror(path); exit(1); }
	fwrite(s->px, 4, (size_t)s->w * s->h, f);
	fclose(f);
}

static void varrow(Surf *s, float cx, float cy, float r, int dir, uint32_t c, float a)
{
	float w = sg_max(1.5f, r * 0.22f);
	sg_line(s, (V2){cx, cy - r * 0.6f}, (V2){cx, cy + r * 0.6f}, w, c, a);
	sg_line(s, (V2){cx, cy + dir * r * 0.6f}, (V2){cx - r * 0.45f, cy + dir * r * 0.1f}, w, c, a);
	sg_line(s, (V2){cx, cy + dir * r * 0.6f}, (V2){cx + r * 0.45f, cy + dir * r * 0.1f}, w, c, a);
}

int main(int argc, char **argv)
{
	if (argc < 4) return 2;
	const char *dir = argv[1], *name = argv[2], *ver = argv[3];
	if (scenes_init(1280, 720)) return 1;
	Surf s = sg_surf(malloc((size_t)W * H * 4), W, H);
	Chooser c; c.windows_found = 1;
	chooser_init(&c);                                   /* sets up the pencil's shards */

	/* background: the white room, a small broken pencil, the title */
	white_room(&s, 0, W);
	sg_blob(&s, W / 2.0f, 96, 150, 9, BLACK, 0.10f);
	pencil(&s, W / 2.0f, 58, 7.2f, 2.0f, 0.12f, 0);
	sg_text_c(&s, &FD, W / 2.0f, 172, name, 66, INK, 1, 0.0f);
	sg_text_c(&s, &FC, W / 2.0f, 202, "CHOOSE YOUR SYSTEM", 10.5f, RGB(96, 98, 108), 1, 0.30f);
	uint32_t hc = RGB(84, 86, 96);
	float x = W / 2.0f - 150, cy = H - 44, sz = 13;
	sg_rrect(&s, x, cy - 11, 22, 22, 5, 1.3f, hc, 0.8f); varrow(&s, x + 11, cy, 9, -1, hc, 0.8f); x += 27;
	sg_rrect(&s, x, cy - 11, 22, 22, 5, 1.3f, hc, 0.8f); varrow(&s, x + 11, cy, 9, 1, hc, 0.8f); x += 31;
	x = sg_text(&s, &FT, x, cy + sz * 0.27f, "choose", sz, hc, 0.8f, 0.02f) + 26;
	float kw = sg_text_w(&FT, "Enter", sz, 0.04f) + 14;
	sg_rrect(&s, x, cy - 11, kw, 22, 5, 1.3f, hc, 0.8f); sg_text(&s, &FT, x + 7, cy + sz * 0.27f, "Enter", sz, hc, 0.8f, 0.04f); x += kw + 9;
	sg_text(&s, &FT, x, cy + sz * 0.27f, "start", sz, hc, 0.8f, 0.02f);
	char v[96];
	snprintf(v, sizeof v, "%s %s  -  live", name, ver);
	sg_text(&s, &FT, W - sg_text_w(&FT, v, 12, 0.03f) - 26, H - 22, v, 12, RGB(140, 142, 152), 1, 0.03f);
	save(dir, "background.raw", &s);

	/* tiles: icon + label, dark on white */
	const char *labels[3] = {name, "Windows", "More options"}, *files[3] = {"tile_silveros.raw", "tile_windows.raw", "tile_advanced.raw"};
	Surf t = sg_surf(malloc(600 * 128 * 4), 600, 128);
	for (int i = 0; i < 3; i++) {
		sg_clear(&t, WHITE);
		float ix = 86, iy = 64;
		if (i == 0) { sg_disc(&t, ix, iy, 30, BLACK, 1); sg_ring(&t, ix, iy, 17, 5, WHITE, 1); sg_disc(&t, ix, iy, 5, WHITE, 1); }
		else if (i == 1) logo4(&t, ix, iy, 56, BLACK, BLACK, BLACK, BLACK, 1);
		else for (int k = -1; k <= 1; k++) sg_disc(&t, ix + k * 20, iy, 6.5f, BLACK, 1);
		sg_text(&t, &FD, 148, 80, labels[i], 46, BLACK, 1, -0.015f);
		save(dir, files[i], &t);
	}
	return 0;
}
