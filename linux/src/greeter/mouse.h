/*
 * Mouse and touchpad.
 *
 * /dev/input/mice is the kernel's mouse emulation: every mouse and touchpad (USB, PS/2, laptop)
 * feeds it, in the classic PS/2 packet format. We ask it for the wheel extension (IntelliMouse),
 * move a cursor, and turn what you do into the keys the interface already understands:
 *
 *   move         select what is under the cursor (home buttons; rows of Settings, Network,
 *                Bluetooth and Time), with the same little sound as the arrow keys
 *   left click   Enter        right click   Esc (back)        wheel   Up / Down
 *
 * The cursor shows once the mouse moves and fades after a few quiet seconds. Without a mouse
 * nothing changes. (The demo video is made without one.)
 */
#pragma once

#ifdef RENDER
static double act_t;
static void mouse_open(void) {}
static void mouse_poll(Home *h) { (void)h; }
static void mouse_draw(void) {}
#else
static double act_t;                                  /* when you last touched anything (idle dimming) */
static struct { int fd, pkt, have; unsigned char buf[64]; float x, y; int btn, shown; double last; } ms = {.fd = -1, .pkt = 3};

static void mouse_open(void)
{
	ms.fd = open("/dev/input/mice", O_RDWR | O_NONBLOCK);
	if (ms.fd < 0) ms.fd = open("/dev/input/mice", O_RDONLY | O_NONBLOCK);
	if (ms.fd < 0) return;
	/* the IntelliMouse handshake: a magic sequence, then ask for the device id (3 = it has a wheel) */
	static const unsigned char magic[] = {0xf3, 200, 0xf3, 100, 0xf3, 80, 0xf2};
	if (write(ms.fd, magic, sizeof magic) == (ssize_t)sizeof magic) {
		usleep(30000);
		unsigned char r[32]; ssize_t n = read(ms.fd, r, sizeof r);
		if (n > 0 && r[n - 1] == 3) ms.pkt = 4;
	}
	ms.x = W / 2.0f; ms.y = H / 2.0f;
}

/* which row of a list is at screen height y (rows start at ly, each step tall); -1 if none */
static int row_at(float y, float ly, float step, int n)
{
	if (y < ly) return -1;
	int r = (int)((y - ly) / step);
	return r < n ? r : -1;
}

/* select what's under the cursor; returns 1 if the selection changed */
static int mouse_hover(Home *h)
{
	int changed = 0;
	switch (h->screen) {
	case 0: {                                                                       /* the row of buttons: by column */
		int q = button_at(ms.x); look_wake = now();
		if (q != h->sel) { h->sel = q; changed = 1; }
		break; }
	case 1:
		if (h->sub == 0) {                                                          /* Settings rows (see draw_settings) */
			int r = row_at(ms.y, SET_LY, SET_STEP, R_N);
			if (r >= 0 && r != h->row) { h->row = r; h->confirm = 0; changed = 1; }
		} else if (h->sub == 1 && !wf.typing) {                                     /* Network */
			int r = row_at(ms.y, U * 34, U * 6.1f, wf.nap - wf.top);
			if (r >= 0 && wf.top + r != wf.sel) { wf.sel = wf.top + r; changed = 1; }
		} else if (h->sub == 2) {                                                   /* Bluetooth */
			int r = row_at(ms.y, U * 34, U * 6.1f, bt.n - bt.top);
			if (r >= 0 && bt.top + r != bt.sel) { bt.sel = bt.top + r; changed = 1; }
		} else if (h->sub == 3) {                                                   /* Time */
			int r = row_at(ms.y, U * 34, U * 5.0f, NZONES - tzs.top);
			if (r >= 0 && tzs.top + r != tzs.sel) { tzs.sel = tzs.top + r; changed = 1; }
		}
		break;
	case 5: {                                                                       /* backgrounds: a preview, or a Customize row */
		float px, py; bg_rect(0, &px, &py);
		if (bg_grab) break;                                                         /* (moving a button: keys only) */
		if (ms.y >= py - U && ms.y <= py + tb_h + U * 5) {
			int q = bg_at(ms.x);
			if (q != h->row || bg_area != 0) { h->row = q; bg_area = 0; changed = 1; }
		} else {
			int r = lo_row_at(ms.y);
			if (r >= 0) {
				int c = r == LO_ORDER ? lo_chip_at(ms.x) : bg_chip;
				if (r != bg_opt || bg_area != 1 || c != bg_chip) { bg_opt = r; bg_area = 1; bg_chip = c; changed = 1; }
			}
		}
		break; }
	}
	return changed;
}

static void mouse_poll(Home *h)
{
	if (ms.fd < 0) return;
	ssize_t n;
	while ((n = read(ms.fd, ms.buf + ms.have, sizeof ms.buf - (size_t)ms.have)) > 0) {
		ms.have += (int)n;
		int off = 0;
		while (ms.have - off >= ms.pkt) {
			const unsigned char *p = ms.buf + off;
			if (!(p[0] & 8)) { off++; continue; }                                   /* not the start of a packet: resync */
			int dx = (signed char)p[1], dy = -(signed char)p[2], wheel = ms.pkt == 4 ? (signed char)p[3] : 0, btn = p[0] & 7;
			off += ms.pkt;
			float sp = W / 1280.0f * 1.4f;
			if (dx || dy) { ms.x = sg_clamp(ms.x + dx * sp, 0, (float)W - 1); ms.y = sg_clamp(ms.y + dy * sp, 0, (float)H - 1); ms.shown = 1; ms.last = act_t = now(); if (mouse_hover(h)) snd('m'); }
			if (wheel) { ms.shown = 1; ms.last = act_t = now(); handle_key_snd(h, wheel < 0 ? K_DOWN : K_UP); }
			if ((btn & 1) && !(ms.btn & 1)) { ms.shown = 1; ms.last = act_t = now(); if (h->screen == 0 && em_hit(ms.x, ms.y)) handle_key_snd(h, 'b'); else { mouse_hover(h); handle_key_snd(h, K_ENTER); } }   /* left: Enter (the emblem: backgrounds) */
			if ((btn & 2) && !(ms.btn & 2)) { ms.shown = 1; ms.last = act_t = now(); handle_key_snd(h, K_ESC); }                      /* right: back */
			ms.btn = btn;
		}
		if (off) { memmove(ms.buf, ms.buf + off, (size_t)(ms.have - off)); ms.have -= off; }
		if (ms.have >= (int)sizeof ms.buf) ms.have = 0;
	}
}

/* a small arrow, drawn over everything, fading out when the mouse has been still */
static void mouse_draw(void)
{
	if (ms.fd < 0 || !ms.shown) return;
	float idle = (float)(now() - ms.last), a = idle < 4 ? 1 : sg_sat(1 - (idle - 4) / 1.5f);
	if (a <= 0) return;
	float u = U * 1.25f, x = ms.x, y = ms.y;
	V2 o[3] = {{x - u * 0.15f, y - u * 0.25f}, {x - u * 0.15f, y + u * 2.1f}, {x + u * 1.5f, y + u * 1.55f}};
	V2 i[3] = {{x + u * 0.15f, y + u * 0.35f}, {x + u * 0.15f, y + u * 1.45f}, {x + u * 0.95f, y + u * 1.2f}};
	sg_tri(&fb, o[0], o[1], o[2], BLACK, 0.85f * a);
	sg_tri(&fb, i[0], i[1], i[2], WHITE, a);
}
#endif
