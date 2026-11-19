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
#define SFX_ALLOC(n) calloc(1, n)
#define SFX_FREE(p) free(p)
#include "bootsfx.h"

/* the sound, mixed alongside the frames and written as a WAV if SILVER_BOOT_WAV is set */
static SfxPlayer player;
static int16_t *audio; static size_t audio_n, audio_cap;
static void audio_frame(void)
{
	int n = SFX_RATE / 30;
	if (audio_n + (size_t)n > audio_cap) { audio_cap = audio_cap ? audio_cap * 2 : (size_t)SFX_RATE * 20; audio = realloc(audio, audio_cap * 4); }
	sfx_render(&player, audio + audio_n * 2, n);
	audio_n += (size_t)n;
}
static void audio_save(void)
{
	const char *path = getenv("SILVER_BOOT_WAV");
	FILE *f = path ? fopen(path, "wb") : 0;
	if (!f) return;
	uint32_t bytes = (uint32_t)audio_n * 4, v; uint16_t s;
	fwrite("RIFF", 1, 4, f); v = 36 + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
	v = 16; fwrite(&v, 4, 1, f); s = 1; fwrite(&s, 2, 1, f); s = 2; fwrite(&s, 2, 1, f);
	v = SFX_RATE; fwrite(&v, 4, 1, f); v = SFX_RATE * 4; fwrite(&v, 4, 1, f); s = 4; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
	fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f); fwrite(audio, 4, audio_n, f); fclose(f);
}

static Surf fb;
static void emit(void) { fwrite(fb.px, 4, (size_t)W * H, stdout); audio_frame(); }

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
	BootSfx snd; bootsfx_build(&snd, !windows, 0);
	sfx_play(&player, &snd.amb, 0.7f, 1);
	int last_sel = c.sel, last_count = (int)(c.countdown + 0.999f);
	for (int f = 0; f < 30 * 7; f++) {
		float t = f * dt;
		if (f == 75) { c.sel = 1; chooser_poke(&c); }
		if (f == 135 && !windows) { c.sel = 0; chooser_poke(&c); }
		if (f == 150 && windows) chooser_poke(&c);
		if (t > 5.2f && t < 6.2f && (f % 4) == 0) { c.show_mouse = 1; c.mx = (int)(W * (windows ? 0.62f + (t - 5.2f) * 0.1f : 0.40f - (t - 5.2f) * 0.12f)); c.my = (int)(H * (0.5f + (t - 5.2f) * 0.1f)); chooser_poke(&c); }
		chooser_update(&c, dt);
		if (c.sel != last_sel) { sfx_play(&player, c.sel ? &snd.to_r : &snd.to_l, 0.9f, 0); last_sel = c.sel; }
		if (c.counting) { int cnt = (int)(c.countdown + 0.999f); if (cnt != last_count && cnt > 0) sfx_play(&player, &snd.tick, 0.8f, 0); last_count = cnt; }
		chooser_draw(&fb, &c); emit();
	}
	sfx_stop(&player, &snd.amb);
	if (windows) {
		sfx_play(&player, &snd.shatter, 1, 0);
		shatter_begin(&fb);
		for (float t = 0; !shatter_frame(&fb, t); t += dt) emit();
		sg_clear(&fb, BLACK); for (int i = 0; i < 10; i++) emit();
		audio_save();
		return 0;
	}
	sfx_play(&player, &snd.go, 1, 0); sfx_play(&player, &snd.trans, 1, 0);
	trans_begin(&fb);
	for (float t = 0; !trans_frame(&fb, t); t += dt) emit();
	for (float t = 0; !checks_frame(&fb, t, &si); t += dt) emit();
	audio_save();
	return 0;
}
