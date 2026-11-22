/*
 * A small Intel High Definition Audio driver for the boot chooser, so it can make sound before
 * any operating system has started. UEFI firmware offers no usable audio, so we drive the
 * controller ourselves through the PCI I/O protocol:
 *
 *   find the HD Audio controller (PCI class 04:03), enable memory and bus mastering, reset it;
 *   talk to the codecs through the CORB/RIRB command rings;
 *   in every codec, walk from each output pin (speaker, headphone, line out) back through
 *   mixers and selectors to a DAC, power everything up, unmute every amplifier on the way,
 *   turn the pins and external amplifiers (EAPD) on;
 *   run one output stream: a 1 second ring buffer of 48 kHz 16-bit stereo, which the mixer
 *   (sfx_render) keeps filled a little ahead of where the hardware is playing.
 *
 * hda_stop() halts the stream and puts the controller back in reset before the next boot
 * stage starts, so Linux or Windows find it as the firmware left it.
 * Anything unexpected and we give up quietly: the chooser just runs silent.
 */
#pragma once

typedef struct EFI_PCI_IO {
	void *PollMem, *PollIo;
	void *MemRead, *MemWrite, *IoRead, *IoWrite;
	EFI_STATUS (EFIAPI *PciRead)(struct EFI_PCI_IO *, uint32_t Width, uint32_t Offset, UINTN Count, void *Buffer);
	EFI_STATUS (EFIAPI *PciWrite)(struct EFI_PCI_IO *, uint32_t Width, uint32_t Offset, UINTN Count, void *Buffer);
	void *CopyMem, *Map, *Unmap, *AllocateBuffer, *FreeBuffer, *Flush, *GetLocation;
	EFI_STATUS (EFIAPI *Attributes)(struct EFI_PCI_IO *, uint32_t Operation, uint64_t Attributes, uint64_t *Result);
} EFI_PCI_IO;
#define PCI_IO_GUID { 0x4cf5b200, 0x68b8, 0x4ca5, { 0x9e, 0xec, 0xb2, 0x3e, 0x3f, 0x50, 0x02, 0x9a } }

#define HDA_RING_FRAMES SFX_RATE                    /* one second */
#define HDA_LEAD (SFX_RATE / 5)                     /* stay 200 ms ahead of the hardware */

static struct {
	volatile uint8_t *mmio;
	EFI_PCI_IO *pci;
	uint32_t *corb; uint64_t *rirb; uint64_t *bdl; int16_t *ring;
	int corb_wp, rirb_rp, sd;                         /* sd: offset of the output stream descriptor */
	int running;
	uint64_t written, played; uint32_t last_lpib;     /* frames, counted from the start */
} hda;
static SfxPlayer hda_player;

#ifdef HDA_DEBUG
static void hlog(const char *m, uint64_t v)
{
	CHAR16 b[96]; int i = 0;
	for (; *m && i < 60; m++) b[i++] = (CHAR16)*m;
	b[i++] = ' ';
	for (int k = 15; k >= 0; k--) { int d = (int)((v >> (k * 4)) & 15); b[i++] = (CHAR16)(d < 10 ? '0' + d : 'a' + d - 10); }
	b[i++] = '\r'; b[i++] = '\n'; b[i] = 0;
	ST->ConOut->OutputString(ST->ConOut, b);
}
#else
#define hlog(m, v) ((void)0)
#endif
static inline uint32_t r32(int o) { return *(volatile uint32_t *)(hda.mmio + o); }
static inline uint16_t r16(int o) { return *(volatile uint16_t *)(hda.mmio + o); }
static inline uint8_t r8(int o) { return *(volatile uint8_t *)(hda.mmio + o); }
static inline void w32(int o, uint32_t v) { *(volatile uint32_t *)(hda.mmio + o) = v; }
static inline void w16(int o, uint16_t v) { *(volatile uint16_t *)(hda.mmio + o) = v; }
static inline void w8(int o, uint8_t v) { *(volatile uint8_t *)(hda.mmio + o) = v; }

static int hda_wait8(int o, uint8_t mask, uint8_t want)
{
	for (int i = 0; i < 1000; i++) { if ((r8(o) & mask) == want) return 1; BS->Stall(100); }
	return 0;
}

static void *hda_dma(UINTN bytes)                      /* physically contiguous, below 4 GB, zeroed */
{
	uint64_t addr = 0xFFFFFFFFull;
	if (((EFI_STATUS(EFIAPI *)(uint32_t, uint32_t, UINTN, uint64_t *))BS->AllocatePages)(1, EfiLoaderData, (bytes + 4095) / 4096, &addr) != EFI_SUCCESS) return 0;
	memset((void *)(uintptr_t)addr, 0, (bytes + 4095) / 4096 * 4096);
	return (void *)(uintptr_t)addr;
}

/* send one verb, wait for its answer */
static uint32_t hda_cmd(int codec, int nid, uint32_t verb_payload)
{
	uint32_t v = (uint32_t)codec << 28 | (uint32_t)nid << 20 | verb_payload;
	hda.corb_wp = (hda.corb_wp + 1) & 255;
	hda.corb[hda.corb_wp] = v;
	__asm__ volatile("mfence" ::: "memory");
	w16(0x48, (uint16_t)hda.corb_wp);
	for (int i = 0; i < 2000; i++) {
		int wp = r16(0x58) & 255;
		if (wp != hda.rirb_rp) {
			hda.rirb_rp = (hda.rirb_rp + 1) & 255;
			uint32_t r = (uint32_t)hda.rirb[hda.rirb_rp];
			w8(0x5D, 0x05);                                   /* acknowledge, or the controller stops answering */
			return r;
		}
		BS->Stall(10);
	}
	return 0xFFFFFFFFu;
}
static uint32_t verb12(int verb, int payload) { return (uint32_t)verb << 8 | (uint32_t)(payload & 255); }
static uint32_t verb4(int verb, int payload) { return (uint32_t)verb << 16 | (uint32_t)(payload & 0xFFFF); }
static uint32_t param(int codec, int nid, int p) { return hda_cmd(codec, nid, verb12(0xF00, p)); }

/* the connection list of a widget */
static int conns(int codec, int nid, int *out, int max)
{
	uint32_t len = param(codec, nid, 0x0E);
	if (len == 0xFFFFFFFFu) return 0;
	int n = (int)(len & 0x7F), lng = (len >> 7) & 1, per = lng ? 2 : 4, got = 0;
	for (int i = 0; i < n && got < max; i += per) {
		uint32_t r = hda_cmd(codec, nid, verb12(0xF02, i));
		for (int k = 0; k < per && i + k < n && got < max; k++)
			out[got++] = lng ? (int)((r >> (16 * k)) & 0x7FFF) : (int)((r >> (8 * k)) & 0xFF);
	}
	return got;
}

static void amp_up(int codec, int nid, int output, int index)
{
	uint32_t caps = param(codec, output ? nid : nid, output ? 0x12 : 0x0D);
	int offset = (int)(caps & 0x7F);                        /* the 0 dB step */
	if (output) hda_cmd(codec, nid, verb4(0x3, 0xB000 | offset));                  /* both sides, unmuted */
	else hda_cmd(codec, nid, verb4(0x3, 0x7000 | index << 8 | offset));
}

/* from `nid`, find a way back to a DAC (depth-first, at most 5 hops); set it up on the way back */
static int route(int codec, int nid, int depth, int stream_tag)
{
	if (depth > 5) return 0;
	uint32_t wcap = param(codec, nid, 0x09);
	int type = (int)(wcap >> 20) & 15;
	hda_cmd(codec, nid, verb12(0x705, 0));                                          /* power on (D0) */
	if (type == 0) {                                                                 /* a DAC: the stream ends here */
		hda_cmd(codec, nid, verb12(0x706, stream_tag << 4));
		hda_cmd(codec, nid, verb4(0x2, 0x0011));                                      /* 48 kHz, 16 bit, 2 channels */
		if (wcap & 4) amp_up(codec, nid, 1, 0);
		return 1;
	}
	if (type == 1) return 0;                                                         /* an input converter */
	int c[32], n = conns(codec, nid, c, 32);
	for (int i = 0; i < n; i++) {
		if (!route(codec, c[i], depth + 1, stream_tag)) continue;
		if (type == 3 || type == 4) hda_cmd(codec, nid, verb12(0x701, i));           /* selector / pin: pick this input */
		if (wcap & 2) amp_up(codec, nid, 0, i);                                        /* an input amp on this path */
		if (wcap & 4) amp_up(codec, nid, 1, 0);
		return 1;
	}
	return 0;
}

static int hda_codec(int codec, int stream_tag)
{
	uint32_t sub = param(codec, 0, 0x04);
	hlog("codec sub", sub); hlog("vendor", param(codec, 0, 0));
	int fg0 = (int)(sub >> 16) & 0xFF, nfg = (int)sub & 0xFF, ok = 0;
	for (int fg = fg0; fg < fg0 + nfg; fg++) {
		if ((param(codec, fg, 0x05) & 0xFF) != 1) continue;                            /* only the audio function group */
		hda_cmd(codec, fg, verb12(0x705, 0));
		uint32_t w = param(codec, fg, 0x04);
		int n0 = (int)(w >> 16) & 0xFF, nn = (int)w & 0xFF;
		for (int nid = n0; nid < n0 + nn; nid++) {
			uint32_t wcap = param(codec, nid, 0x09);
			if (((wcap >> 20) & 15) != 4) continue;                                    /* pins only */
			uint32_t pcap = param(codec, nid, 0x0C), cfg = hda_cmd(codec, nid, verb12(0xF1C, 0));
			int conn = (int)(cfg >> 30) & 3, dev = (int)(cfg >> 20) & 15;
			if (!(pcap & 0x10) || conn == 1) continue;                                 /* not an output, or not wired up */
			if (dev != 0 && dev != 1 && dev != 2) continue;                            /* line out, speaker, headphones */
			hlog("pin", (uint64_t)nid << 32 | cfg);
			if (!route(codec, nid, 0, stream_tag)) continue;
			hda_cmd(codec, nid, verb12(0x707, dev == 2 ? 0xC0 : 0x40));               /* output on (+ headphone drive) */
			if (pcap & 0x10000) hda_cmd(codec, nid, verb12(0x70C, 2));                 /* external amplifier on */
			ok = 1;
		}
	}
	return ok;
}

/* returns 1 if sound is running */
static int hda_start(void)
{
	static EFI_GUID pg = PCI_IO_GUID;
	UINTN n = 0; EFI_HANDLE *hs = 0;
	if (BS->LocateHandleBuffer(ByProtocol, &pg, 0, &n, &hs) != EFI_SUCCESS) return 0;
	for (UINTN i = 0; i < n && !hda.mmio; i++) {
		EFI_PCI_IO *p;
		if (BS->HandleProtocol(hs[i], &pg, (void **)&p) != EFI_SUCCESS) continue;
		uint32_t cls = 0, bar0 = 0, bar1 = 0;
		p->PciRead(p, 2, 0x08, 1, &cls);
		hlog("pci class", cls);
		if ((cls >> 16) != 0x0403) continue;                                           /* multimedia / HD Audio */
		p->PciRead(p, 2, 0x10, 1, &bar0); p->PciRead(p, 2, 0x14, 1, &bar1);
		uint64_t base = bar0 & ~0xFull;
		if ((bar0 & 6) == 4) base |= (uint64_t)bar1 << 32;
		if (!base || (bar0 & 1)) continue;
		uint64_t res;
		p->Attributes(p, 2 /* enable */, 0x0200 | 0x0400 /* memory, bus master */, &res);
		hda.mmio = (volatile uint8_t *)(uintptr_t)base; hda.pci = p;
	}
	BS->FreePool(hs);
	hlog("hda mmio", (uint64_t)(uintptr_t)hda.mmio);
	if (!hda.mmio) return 0;
	/* reset */
	w32(0x08, r32(0x08) & ~1u);
	if (!hda_wait8(0x08, 1, 0)) return 0;
	BS->Stall(200);
	w32(0x08, r32(0x08) | 1u);
	if (!hda_wait8(0x08, 1, 1)) return 0;
	BS->Stall(2000);                                                                   /* codecs announce themselves */
	uint16_t codecs = r16(0x0E);
	hlog("hda codecs", codecs); hlog("hda gcap", r16(0));
	uint16_t gcap = r16(0x00);
	int iss = (gcap >> 8) & 15;
	hda.sd = 0x80 + iss * 0x20;
	/* command rings */
	hda.corb = hda_dma(1024); hda.rirb = hda_dma(2048);
	hda.bdl = hda_dma(4096); hda.ring = hda_dma(HDA_RING_FRAMES * 4);
	if (!hda.corb || !hda.rirb || !hda.bdl || !hda.ring) return 0;
	w8(0x4C, 0); w8(0x5C, 0);
	BS->Stall(100);
	w32(0x40, (uint32_t)(uintptr_t)hda.corb); w32(0x44, (uint32_t)((uint64_t)(uintptr_t)hda.corb >> 32));
	w8(0x4E, (r8(0x4E) & ~3) | 2);                                                     /* 256 entries */
	w16(0x4A, 0x8000); BS->Stall(100); w16(0x4A, 0); BS->Stall(100);                   /* reset the read pointer */
	w16(0x48, 0); hda.corb_wp = 0;
	w32(0x50, (uint32_t)(uintptr_t)hda.rirb); w32(0x54, (uint32_t)((uint64_t)(uintptr_t)hda.rirb >> 32));
	w8(0x5E, (r8(0x5E) & ~3) | 2);
	w16(0x58, 0x8000); hda.rirb_rp = 0;
	w16(0x5A, 0xFF);
	w8(0x5D, 0x05); w16(0x0E, 0x7FFF);
	w8(0x5C, 2); w8(0x4C, 2);                                                          /* run both rings */
	BS->Stall(500);
	/* codecs */
	int ok = 0;
	for (int c = 0; c < 15; c++) if (codecs & (1 << c)) ok |= hda_codec(c, 1);
	hlog("hda routed", (uint64_t)ok);
	if (!ok) { w32(0x08, r32(0x08) & ~1u); return 0; }
	/* the output stream: two buffer descriptors covering the ring */
	int sd = hda.sd;
	w8(sd, 1); hda_wait8(sd, 1, 1); w8(sd, 0); hda_wait8(sd, 1, 0);                  /* stream reset */
	uint64_t ring = (uint64_t)(uintptr_t)hda.ring; uint32_t half = HDA_RING_FRAMES * 2;
	hda.bdl[0] = ring; hda.bdl[1] = half;
	hda.bdl[2] = ring + half; hda.bdl[3] = half;
	w32(sd + 0x08, HDA_RING_FRAMES * 4);                                               /* cyclic buffer length */
	w16(sd + 0x0C, 1);                                                                 /* last valid index */
	w16(sd + 0x12, 0x0011);                                                            /* format */
	w32(sd + 0x18, (uint32_t)(uintptr_t)hda.bdl); w32(sd + 0x1C, (uint32_t)((uint64_t)(uintptr_t)hda.bdl >> 32));
	w8(sd + 2, (uint8_t)(1 << 4));                                                     /* stream tag 1 */
	hda.written = 0; hda.played = 0; hda.last_lpib = 0;
	sfx_render(&hda_player, hda.ring, HDA_LEAD);                                       /* something to start with */
	hda.written = HDA_LEAD;
	__asm__ volatile("mfence" ::: "memory");
	w8(sd, 2);                                                                         /* RUN */
	hda.running = 1;
	BS->Stall(50000); hlog("hda lpib after 50ms", r32(sd + 4)); hlog("sd ctl/sts", r32(sd));
	return 1;
}

/* keep the ring filled ahead of the hardware; call every frame */
static void hda_pump(void)
{
	if (!hda.running) return;
	uint32_t lpib = r32(hda.sd + 0x04) / 4;
	if (lpib >= HDA_RING_FRAMES) return;
	uint32_t d = lpib >= hda.last_lpib ? lpib - hda.last_lpib : lpib + HDA_RING_FRAMES - hda.last_lpib;
	hda.played += d; hda.last_lpib = lpib;
	if (hda.written < hda.played) hda.written = hda.played;                            /* we fell behind: skip ahead */
	uint64_t want = hda.played + HDA_LEAD;
	while (hda.written < want) {
		uint32_t at = (uint32_t)(hda.written % HDA_RING_FRAMES), cnt = (uint32_t)(want - hda.written);
		if (at + cnt > HDA_RING_FRAMES) cnt = HDA_RING_FRAMES - at;
		sfx_render(&hda_player, hda.ring + at * 2, (int)cnt);
		hda.written += cnt;
	}
	__asm__ volatile("mfence" ::: "memory");
}

/* let what is playing finish (up to `max_s` seconds), then stop and reset the controller */
static void hda_stop(float max_s)
{
	if (!hda.running) return;
	for (float t = 0; t < max_s; t += 0.02f) {
		int busy = 0;
		for (int i = 0; i < SFX_VOICES; i++) if (hda_player.v[i].c && !hda_player.v[i].loop) busy = 1;
		if (!busy) break;
		hda_pump(); BS->Stall(20000);
	}
	sfx_stop(&hda_player, 0);
	for (int i = 0; i < 3; i++) { hda_pump(); BS->Stall(100000); }   /* flush silence through */
	w8(hda.sd, 0); hda_wait8(hda.sd, 2, 0);
	w8(hda.sd, 1); hda_wait8(hda.sd, 1, 1); w8(hda.sd, 0);
	w8(0x4C, 0); w8(0x5C, 0);
	w32(0x08, r32(0x08) & ~1u);
	hda.running = 0;
}
