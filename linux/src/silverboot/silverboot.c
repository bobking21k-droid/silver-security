/*
 * silverboot -- the SilverOS boot chooser. A UEFI application written from scratch.
 *
 * The screen splits down the middle: SilverOS on the left (a white world with the
 * SilverOS emblem turning), Windows on the right (in colour). Time in the scene barely moves until
 * you do: press a key or move the mouse and the shards come alive.
 *
 *   <- / ->, mouse hover   choose          Enter, click    start
 *   1 / 2                  start SilverOS / Windows directly
 *
 * SilverOS: the Windows scene melts away, a hardware check runs, then \EFI\SilverOS\grubx64.efi
 * loads the kernel. Windows: the screen shatters and \EFI\Microsoft\Boot\bootmgfw.efi takes over.
 *
 * All drawing lives in scenes.h (shared with the desktop preview); this file is the UEFI side:
 * graphics output, keyboard and pointer input, timing, and starting the next boot stage.
 */
#include "efi.h"
#include "../common/fonts.h"

#define SILVER_LOADER  u"\\EFI\\SilverOS\\grubx64.efi"
#define WINDOWS_LOADER u"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"

int _fltused = 0;   /* tells the MS-ABI toolchain we use floating point */

static EFI_SYSTEM_TABLE *ST;
static EFI_BOOT_SERVICES *BS;
static EFI_HANDLE Self;
static EFI_GOP *Gop;
static EFI_POINTER *Ptr;
static EFI_ABS_POINTER *Abs;

void *memset(void *d, int c, size_t n) { void *r = d; __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory"); return r; }
void *memcpy(void *d, const void *s, size_t n) { void *r = d; __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory"); return r; }

void *sb_alloc(size_t bytes)
{
	void *p = 0;
	return BS->AllocatePool(EfiLoaderData, bytes, &p) == EFI_SUCCESS ? p : 0;
}

#include "scenes.h"

static void *sb_zalloc(size_t n) { void *p = sb_alloc(n); if (p) memset(p, 0, n); return p; }
#define SFX_ALLOC(n) sb_zalloc(n)
#define SFX_FREE(p) BS->FreePool(p)
#include "bootsfx.h"
#include "hda.h"
static BootSfx snd;

static inline uint64_t rdtsc(void) { uint32_t lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }
static float tsc_per_sec;
static float since(uint64_t s) { return (float)(rdtsc() - s) / tsc_per_sec; }

/* ---------------------------------------------------------------- screen */
static Surf fb;                 /* what the scenes draw into */
static uint32_t *big;           /* 2x upscaled copy for very high resolution panels */
static int scale = 1, real_w, real_h;

static void present(void)
{
	if (scale == 1) { Gop->Blt(Gop, fb.px, EfiBltBufferToVideo, 0, 0, 0, 0, W, H, 0); return; }
	for (int y = 0; y < H; y++) {
		uint32_t *src = fb.px + y * W, *d0 = big + (2 * y) * (2 * W), *d1 = d0 + 2 * W;
		for (int x = 0; x < W; x++) { uint32_t c = src[x]; d0[2 * x] = d0[2 * x + 1] = d1[2 * x] = d1[2 * x + 1] = c; }
	}
	Gop->Blt(Gop, big, EfiBltBufferToVideo, 0, 0, 0, 0, 2 * W, 2 * H, 0);
}

/* ---------------------------------------------------------------- hardware facts for the check screen */
static void utoa(char *out, uint64_t v) { char b[24]; int n = 0; do b[n++] = '0' + v % 10; while (v /= 10); while (n) *out++ = b[--n]; *out = 0; }
static void cat(char *d, const char *s) { while (*d) d++; while ((*d++ = *s++)); }

static void probe(SysInfo *si, int disk_ok)
{
	uint32_t r[13];
	for (int i = 0; i < 3; i++)
		__asm__ volatile("cpuid" : "=a"(r[i * 4]), "=b"(r[i * 4 + 1]), "=c"(r[i * 4 + 2]), "=d"(r[i * 4 + 3]) : "a"(0x80000002 + i), "c"(0));
	r[12] = 0;
	char *w = si->cpu;                                  /* the CPU's own name, spaces squeezed */
	for (const char *c = (const char *)r; *c && w < si->cpu + 60; c++) {
		if (*c < 32 || *c > 126) continue;
		if (*c == ' ' && (w == si->cpu || w[-1] == ' ')) continue;
		*w++ = *c;
	}
	while (w > si->cpu && w[-1] == ' ') w--;
	*w = 0;
	if (!si->cpu[0]) cat(si->cpu, "x86-64 processor");

	si->gpu[0] = 0; utoa(si->gpu, (uint64_t)real_w); cat(si->gpu, " x ");
	{ char n[12]; utoa(n, (uint64_t)real_h); cat(si->gpu, n); }
	cat(si->gpu, " display");

	static uint8_t map[96 * 1024];
	UINTN size = sizeof map, key, dsz; uint32_t ver; uint64_t pages = 0;
	if (BS->GetMemoryMap(&size, map, &key, &dsz, &ver) == EFI_SUCCESS && dsz)
		for (UINTN o = 0; o + dsz <= size; o += dsz) {
			uint32_t type = *(uint32_t *)(map + o);
			if ((type >= 1 && type <= 7) || type == 9 || type == 10) pages += *(uint64_t *)(map + o + 24);
		}
	uint64_t mb = (pages * 4096 + (128ull << 20)) >> 20;
	mb = (mb + 127) / 256 * 256;                        /* firmware keeps a little; round to 256 MB */
	si->ram[0] = 0;
	if (mb >= 1024 && mb % 1024 == 0) { utoa(si->ram, mb / 1024); cat(si->ram, " GB"); }
	else { utoa(si->ram, mb); cat(si->ram, " MB"); }
	si->disk[0] = 0; cat(si->disk, disk_ok ? "SilverOS volume found" : "SilverOS volume missing");
	si->ok[0] = si->ok[1] = 1; si->ok[2] = pages > 0; si->ok[3] = disk_ok;
}

/* ---------------------------------------------------------------- loading the next stage */
static int has_file(EFI_HANDLE dev, CHAR16 *path)
{
	static EFI_GUID sfsg = SFS_GUID;
	EFI_SFS *sfs; EFI_FILE *root, *f;
	if (BS->HandleProtocol(dev, &sfsg, (void **)&sfs) != EFI_SUCCESS) return 0;
	if (sfs->OpenVolume(sfs, &root) != EFI_SUCCESS) return 0;
	int ok = root->Open(root, &f, path, EFI_FILE_MODE_READ, 0) == EFI_SUCCESS;
	if (ok) f->Close(f);
	root->Close(root);
	return ok;
}

/* find a filesystem holding `path`: our own ESP first, then every other volume */
static EFI_HANDLE find_volume(CHAR16 *path)
{
	static EFI_GUID lig = LOADED_IMAGE_GUID, sfsg = SFS_GUID;
	EFI_LOADED_IMAGE *li;
	if (BS->HandleProtocol(Self, &lig, (void **)&li) == EFI_SUCCESS && has_file(li->DeviceHandle, path))
		return li->DeviceHandle;
	UINTN n = 0; EFI_HANDLE *hs = 0;
	if (BS->LocateHandleBuffer(ByProtocol, &sfsg, 0, &n, &hs) != EFI_SUCCESS) return 0;
	for (UINTN i = 0; i < n; i++)
		if (has_file(hs[i], path)) { EFI_HANDLE h = hs[i]; BS->FreePool(hs); return h; }
	BS->FreePool(hs);
	return 0;
}

/* device path of the volume + a file-path node, so the loaded image knows where it lives */
static void *file_path(EFI_HANDLE dev, CHAR16 *path)
{
	static EFI_GUID dpg = DEVICE_PATH_GUID;
	EFI_DEVICE_PATH *dp;
	if (BS->HandleProtocol(dev, &dpg, (void **)&dp) != EFI_SUCCESS) return 0;
	UINTN len = 0;
	for (EFI_DEVICE_PATH *n = dp; !(n->Type == 0x7f && n->SubType == 0xff);
	     n = (EFI_DEVICE_PATH *)((uint8_t *)n + (n->Length[0] | n->Length[1] << 8)))
		len += n->Length[0] | n->Length[1] << 8;
	UINTN plen = 0; while (path[plen]) plen++;
	UINTN node = 4 + (plen + 1) * 2;
	uint8_t *out;
	if (BS->AllocatePool(EfiLoaderData, len + node + 4, (void **)&out) != EFI_SUCCESS) return 0;
	memcpy(out, dp, len);
	uint8_t *p = out + len;
	p[0] = 4; p[1] = 4; p[2] = (uint8_t)node; p[3] = (uint8_t)(node >> 8);
	memcpy(p + 4, path, (plen + 1) * 2);
	p += node;
	p[0] = 0x7f; p[1] = 0xff; p[2] = 4; p[3] = 0;
	return out;
}


/* run a scene until it reports it has finished */
static void play(int (*frame)(Surf *, float))
{
	uint64_t start = rdtsc();
	while (!frame(&fb, since(start))) { present(); hda_pump(); BS->Stall(4000); }
	present(); hda_pump();
}
static SysInfo sysinfo;
static int checks_scene(Surf *s, float t) { return checks_frame(s, t, &sysinfo); }

/* returns only if the next stage could not be started (or came back) */
static const char *launch(CHAR16 *path, int silver)
{
	EFI_HANDLE dev = find_volume(path), img;
	void *dp = dev ? file_path(dev, path) : 0;
	if (!dp) return silver ? "SilverOS boot files are missing" : "Windows Boot Manager was not found";
	sfx_stop(&hda_player, &snd.amb);
	if (silver) {
		probe(&sysinfo, 1);
		sfx_play(&hda_player, &snd.go, 1, 0); sfx_play(&hda_player, &snd.trans, 1, 0);
		trans_begin(&fb); play(trans_frame);
		play(checks_scene);
		hda_stop(0.4f);
	} else {
		sfx_play(&hda_player, &snd.shatter, 1, 0);
		shatter_begin(&fb); play(shatter_frame);
		hda_stop(0.6f);
	}
	if (BS->LoadImage(0, Self, dp, 0, 0, &img) != EFI_SUCCESS) return "That system could not be loaded";
	BS->StartImage(img, 0, 0);
	return "That system came back. Pick again";
}

/* ---------------------------------------------------------------- main */
EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
	static EFI_GUID gopg = GOP_GUID, ptrg = POINTER_GUID, absg = ABSPOINTER_GUID;
	Self = image; ST = st; BS = st->BootServices;
	BS->SetWatchdogTimer(0, 0, 0, 0);
	uint64_t t0 = rdtsc(); BS->Stall(50000);
	tsc_per_sec = (float)(rdtsc() - t0) * 20;
	if (tsc_per_sec < 1e6f) tsc_per_sec = 1e9f;

	int ok = BS->LocateProtocol(&gopg, 0, (void **)&Gop) == EFI_SUCCESS;
	if (ok) {
		real_w = Gop->Mode->Info->HorizontalResolution; real_h = Gop->Mode->Info->VerticalResolution;
		scale = real_w > 2100 ? 2 : 1;                      /* 4K panels: draw at half size, show doubled */
		ok = scenes_init(real_w / scale, real_h / scale) == 0;
		fb = sg_surf(sb_alloc((size_t)W * H * 4), W, H);
		if (scale == 2) big = sb_alloc((size_t)W * H * 16);
		ok = ok && fb.px && (scale == 1 || big);
	}
	if (!ok) {                                              /* no graphics: go straight to SilverOS */
		EFI_HANDLE dev = find_volume(SILVER_LOADER), img;
		void *dp = dev ? file_path(dev, SILVER_LOADER) : 0;
		if (dp && BS->LoadImage(0, Self, dp, 0, 0, &img) == EFI_SUCCESS) BS->StartImage(img, 0, 0);
		return 1;
	}
	if (BS->LocateProtocol(&absg, 0, (void **)&Abs) != EFI_SUCCESS) Abs = 0;
	if (BS->LocateProtocol(&ptrg, 0, (void **)&Ptr) != EFI_SUCCESS) Ptr = 0;
	st->ConOut->EnableCursor(st->ConOut, 0);
	sg_rng_state ^= (uint32_t)rdtsc();

	static Chooser c;
	c.windows_found = find_volume(WINDOWS_LOADER) != 0;
	chooser_init(&c);
	/* sound, unless "Sound effects" is off in SilverOS Settings (it leaves a firmware variable) */
	static EFI_GUID muteg = { 0x6c5c3c2e, 0x6b0a, 0x4f3c, { 0x9a, 0x7e, 0x2f, 0x1d, 0x0c, 0x9b, 0x8a, 0x71 } };
	uint8_t mv = 0; UINTN ms = 1; uint32_t mattr;
	int sound = !(((EFI_RUNTIME_SERVICES *)st->RuntimeServices)->GetVariable(u"SilverMute", &muteg, &mattr, &ms, &mv) == EFI_SUCCESS && mv);
	if (sound) {
		bootsfx_build(&snd, 1, calm);                          /* all the sounds, made now (well under a second) */
		if (hda_start()) sfx_play(&hda_player, &snd.amb, 0.7f, 1);
	}
	/* "Play in Windows" in SilverOS leaves a one-time request (the variable SilverNext = 2): start
	 * Windows right away, after a moment of the scene, and forget the request so it only happens once */
	int auto_go = -1, frames = 0;
	{
		uint8_t nv = 0; UINTN ns = 1; uint32_t nattr;
		EFI_RUNTIME_SERVICES *rt = (EFI_RUNTIME_SERVICES *)st->RuntimeServices;
		if (rt->GetVariable(u"SilverNext", &muteg, &nattr, &ns, &nv) == EFI_SUCCESS) {
			rt->SetVariable(u"SilverNext", &muteg, 0, 0, 0);           /* attributes 0, size 0: delete it */
			if (nv == 2 && c.windows_found) auto_go = 1;
		}
	}
	int last_sel = c.sel, last_count = (int)(c.countdown + 0.999f);
	uint64_t last = rdtsc();
	for (;;) {
		uint64_t now = rdtsc();
		float dt = sg_clamp((float)(now - last) / tsc_per_sec, 0, 0.1f);
		last = now;

		EFI_INPUT_KEY k;
		int go = -1, click = 0;
		if (auto_go >= 0 && ++frames > 40) { go = auto_go; auto_go = -1; }
		while (st->ConIn->ReadKeyStroke(st->ConIn, &k) == EFI_SUCCESS) {
			chooser_poke(&c);
			if (k.ScanCode == SCAN_LEFT || k.ScanCode == SCAN_UP) c.sel = 0;
			else if (k.ScanCode == SCAN_RIGHT || k.ScanCode == SCAN_DOWN) c.sel = c.windows_found ? 1 : 0;
			else if (k.UnicodeChar == '\r' || k.UnicodeChar == ' ') go = c.sel;
			else if (k.UnicodeChar == 'c' || k.UnicodeChar == 'C') calm ^= 1;
			else if (k.UnicodeChar == '1') go = 0;
			else if (k.UnicodeChar == '2' && c.windows_found) go = 1;
		}
		if (Abs) {
			EFI_ABS_STATE s;
			if (Abs->GetState(Abs, &s) == EFI_SUCCESS && Abs->Mode->MaxX > Abs->Mode->MinX) {
				int nx = (int)((s.X - Abs->Mode->MinX) * W / (Abs->Mode->MaxX - Abs->Mode->MinX));
				int ny = (int)((s.Y - Abs->Mode->MinY) * H / (Abs->Mode->MaxY - Abs->Mode->MinY));
				if (nx != c.mx || ny != c.my) { c.mx = nx; c.my = ny; c.show_mouse = 1; chooser_poke(&c); }
				if (s.Buttons & 1) click = 1;
			}
		}
		if (Ptr) {
			EFI_POINTER_STATE s;
			if (Ptr->GetState(Ptr, &s) == EFI_SUCCESS) {
				float rx = Ptr->Mode->ResX ? (float)Ptr->Mode->ResX : 1, ry = Ptr->Mode->ResY ? (float)Ptr->Mode->ResY : 1;
				if (s.RelX || s.RelY) {
					c.mx = (int)sg_clamp(c.mx + s.RelX * 6 / rx, 0, W - 1);
					c.my = (int)sg_clamp(c.my + s.RelY * 6 / ry, 0, H - 1);
					c.show_mouse = 1; chooser_poke(&c);
				}
				if (s.Left) click = 1;
			}
		}
		if (c.show_mouse) {
			int hover = c.mx < W / 2 ? 0 : 1;
			if (hover == 0 || c.windows_found) c.sel = hover;
			if (click) go = c.sel;
		}
		chooser_update(&c, dt);
		if (c.sel != last_sel) { sfx_play(&hda_player, c.sel ? &snd.to_r : &snd.to_l, 0.9f, 0); last_sel = c.sel; }
		if (c.counting) { int cnt = (int)(c.countdown + 0.999f); if (cnt != last_count && cnt > 0) sfx_play(&hda_player, &snd.tick, 0.8f, 0); last_count = cnt; }
		hda_pump();
		if (c.counting && c.countdown <= 0) go = 0;
		chooser_draw(&fb, &c);
		present();
		if (go >= 0) {
			c.sel = go; c.selblend = (float)go;
			chooser_draw(&fb, &c);
			const char *err = launch(go == 0 ? SILVER_LOADER : WINDOWS_LOADER, go == 0);
			chooser_init(&c);                                 /* we are back: show why, wait for a choice */
			if (sound && hda_start()) sfx_play(&hda_player, &snd.amb, 0.7f, 1);
			c.err = err; c.counting = 0; c.real_t = 1;
			last = rdtsc();
		}
		BS->Stall(2000);
	}
}
