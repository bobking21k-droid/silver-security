/*
 * Desktop preview of the silverboot scenes: renders frames with the exact code the boot
 * program uses and writes them as raw BGRA to stdout (pipe into ffmpeg).
 *
 *   preview <w> <h> silver   | ffmpeg -f rawvideo -pix_fmt bgr0 -s WxH -r 30 -i - out.mp4
 *   preview <w> <h> windows  | ...
 *   preview <w> <h> still <seconds> <sel>       one chooser frame
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/fonts.h"
#include "scenes.h"

void *sb_alloc(size_t n) { return malloc(n); }

static Surf fb;
static void emit(void) { fwrite(fb.px, 4, (size_t)W * H, stdout); }

int main(int argc, char **argv)
{
	int w = argc > 1 ? atoi(argv[1]) : 1280, h = argc > 2 ? atoi(argv[2]) : 800;
	const char *mode = argc > 3 ? argv[3] : "silver";
	const float dt = 1 / 30.f;
	if (scenes_init(w, h)) return 1;
	fb = sg_surf(malloc((size_t)w * h * 4), w, h);
	Chooser c; c.windows_found = 1;
	chooser_init(&c);
	SysInfo si = {"Intel Core i7-1260P", "1920x1080 framebuffer", "16384 MB", "SilverOS volume OK", {1, 1, 1, 1}};

	if (!strcmp(mode, "still")) {
		float until = argc > 4 ? atof(argv[4]) : 2;
		c.sel = argc > 5 ? atoi(argv[5]) : 0;
		c.selblend = c.sel;
		for (float t = 0; t < until; t += dt) { if (t > until - 0.5f && argc > 6) chooser_poke(&c); chooser_update(&c, dt); }
		chooser_draw(&fb, &c); emit();
		return 0;
	}
	/* scripted chooser: look, move right, move back (or stay), press Enter */
	int windows = !strcmp(mode, "windows");
	for (int f = 0; f < 30 * 7; f++) {
		float t = f * dt;
		if (f == 75) { c.sel = 1; chooser_poke(&c); }
		if (f == 135 && !windows) { c.sel = 0; chooser_poke(&c); }
		if (f == 150 && windows) chooser_poke(&c);
		if (t > 5.2f && t < 6.2f && (f % 4) == 0) { c.show_mouse = 1; c.mx = (int)(W * (windows ? 0.62f + (t - 5.2f) * 0.1f : 0.40f - (t - 5.2f) * 0.12f)); c.my = (int)(H * (0.5f + (t - 5.2f) * 0.1f)); chooser_poke(&c); }
		chooser_update(&c, dt); chooser_draw(&fb, &c); emit();
	}
	if (windows) {
		shatter_begin(&fb);
		for (float t = 0; !shatter_frame(&fb, t); t += dt) emit();
		sg_clear(&fb, BLACK); for (int i = 0; i < 10; i++) emit();
		return 0;
	}
	trans_begin(&fb);
	for (float t = 0; !trans_frame(&fb, t); t += dt) emit();
	for (float t = 0; !checks_frame(&fb, t, &si); t += dt) emit();
	return 0;
}
