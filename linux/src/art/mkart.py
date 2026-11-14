#!/usr/bin/env python3
"""
SilverOS boot-screen art, drawn pixel by pixel. No image libraries, no font files:
a hand-made 5x7 bitmap font, shapes from distance functions, 4x supersampling for
smooth edges, and a minimal PNG encoder.

    mkart.py <outdir> <os_name> <version> <bg 0xRRGGBB> <accent 0xRRGGBB> <hilite 0xRRGGBB>

Writes a GRUB theme: theme.txt, background.png, icons/{silveros,windows}.png, select_*.png
"""
import math, os, struct, sys, zlib

# ---------------------------------------------------------------- 5x7 font
# Each glyph: 7 rows of 5 bits ('#' = ink). Only the characters the art uses.
FONT = {
 'A': ".###.|#...#|#...#|#####|#...#|#...#|#...#", 'B': "####.|#...#|#...#|####.|#...#|#...#|####.",
 'C': ".###.|#...#|#....|#....|#....|#...#|.###.", 'D': "####.|#...#|#...#|#...#|#...#|#...#|####.",
 'E': "#####|#....|#....|####.|#....|#....|#####", 'F': "#####|#....|#....|####.|#....|#....|#....",
 'G': ".###.|#...#|#....|#.###|#...#|#...#|.####", 'H': "#...#|#...#|#...#|#####|#...#|#...#|#...#",
 'I': ".###.|..#..|..#..|..#..|..#..|..#..|.###.", 'K': "#...#|#..#.|#.#..|##...|#.#..|#..#.|#...#",
 'L': "#....|#....|#....|#....|#....|#....|#####", 'M': "#...#|##.##|#.#.#|#.#.#|#...#|#...#|#...#",
 'N': "#...#|##..#|#.#.#|#..##|#...#|#...#|#...#", 'O': ".###.|#...#|#...#|#...#|#...#|#...#|.###.",
 'P': "####.|#...#|#...#|####.|#....|#....|#....", 'R': "####.|#...#|#...#|####.|#.#..|#..#.|#...#",
 'S': ".####|#....|#....|.###.|....#|....#|####.", 'T': "#####|..#..|..#..|..#..|..#..|..#..|..#..",
 'U': "#...#|#...#|#...#|#...#|#...#|#...#|.###.", 'V': "#...#|#...#|#...#|#...#|#...#|.#.#.|..#..",
 'W': "#...#|#...#|#...#|#.#.#|#.#.#|##.##|#...#", 'Y': "#...#|#...#|.#.#.|..#..|..#..|..#..|..#..",
 'a': ".....|.....|.###.|....#|.####|#...#|.####", 'b': "#....|#....|####.|#...#|#...#|#...#|####.",
 'c': ".....|.....|.####|#....|#....|#....|.####", 'd': "....#|....#|.####|#...#|#...#|#...#|.####",
 'e': ".....|.....|.###.|#...#|#####|#....|.###.", 'f': "..##.|.#...|.#...|####.|.#...|.#...|.#...",
 'g': ".....|.####|#...#|#...#|.####|....#|.###.", 'h': "#....|#....|####.|#...#|#...#|#...#|#...#",
 'i': "..#..|.....|.##..|..#..|..#..|..#..|.###.", 'k': "#....|#....|#..#.|#.#..|##...|#.#..|#..#.",
 'l': ".##..|..#..|..#..|..#..|..#..|..#..|.###.", 'm': ".....|.....|##.#.|#.#.#|#.#.#|#.#.#|#.#.#",
 'n': ".....|.....|####.|#...#|#...#|#...#|#...#", 'o': ".....|.....|.###.|#...#|#...#|#...#|.###.",
 'p': ".....|####.|#...#|#...#|####.|#....|#....", 'r': ".....|.....|#.##.|##..#|#....|#....|#....",
 's': ".....|.....|.####|#....|.###.|....#|####.", 't': ".#...|.#...|####.|.#...|.#...|.#..#|..##.",
 'u': ".....|.....|#...#|#...#|#...#|#..##|.##.#", 'v': ".....|.....|#...#|#...#|#...#|.#.#.|..#..",
 'w': ".....|.....|#...#|#...#|#.#.#|#.#.#|.#.#.", 'x': ".....|.....|#...#|.#.#.|..#..|.#.#.|#...#",
 'y': ".....|#...#|#...#|#...#|.####|....#|.###.", 'z': ".....|.....|#####|...#.|..#..|.#...|#####",
 '0': ".###.|#...#|#..##|#.#.#|##..#|#...#|.###.", '1': "..#..|.##..|..#..|..#..|..#..|..#..|.###.",
 '2': ".###.|#...#|....#|...#.|..#..|.#...|#####", '3': "####.|....#|....#|.###.|....#|....#|####.",
 '4': "...#.|..##.|.#.#.|#..#.|#####|...#.|...#.", '5': "#####|#....|####.|....#|....#|#...#|.###.",
 '6': ".###.|#....|#....|####.|#...#|#...#|.###.", '7': "#####|....#|...#.|..#..|.#...|.#...|.#...",
 '8': ".###.|#...#|#...#|.###.|#...#|#...#|.###.", '9': ".###.|#...#|#...#|.####|....#|....#|.###.",
 '.': ".....|.....|.....|.....|.....|.##..|.##..", '-': ".....|.....|.....|.###.|.....|.....|.....",
 '(': "...#.|..#..|.#...|.#...|.#...|..#..|...#.", ')': ".#...|..#..|...#.|...#.|...#.|..#..|.#...",
 '\u2191': "..#..|.###.|#.#.#|..#..|..#..|..#..|.....", '\u2193': "..#..|..#..|..#..|#.#.#|.###.|..#..|.....",
 ' ': ".....|.....|.....|.....|.....|.....|.....", '/': "....#|...#.|...#.|..#..|.#...|.#...|#....",
}

def glyph(c):
    rows = FONT.get(c, FONT.get(c.upper(), FONT[' '])).split('|')
    return [[ch == '#' for ch in r] for r in rows]

def text_width(s, px):           # px = size of one font pixel
    return len(s) * 6 * px - px

# ---------------------------------------------------------------- canvas
SS = 4  # supersampling factor

class Canvas:
    def __init__(s, w, h, rgba=(0, 0, 0, 0)):
        s.w, s.h = w, h
        s.px = [list(rgba) for _ in range(w * h)]

    def blend(s, x, y, rgb, a):
        if a <= 0 or not (0 <= x < s.w and 0 <= y < s.h):
            return
        p = s.px[y * s.w + x]
        oa = p[3] / 255
        na = a + oa * (1 - a)
        for i in range(3):
            p[i] = round((rgb[i] * a + p[i] * oa * (1 - a)) / na) if na else 0
        p[3] = round(na * 255)

    def shape(s, x0, y0, x1, y1, inside, rgb, alpha=1.0):
        """Fill pixels covered by inside(fx, fy) with SSxSS supersampling."""
        for y in range(max(0, int(y0)), min(s.h, int(math.ceil(y1)))):
            for x in range(max(0, int(x0)), min(s.w, int(math.ceil(x1)))):
                hit = sum(inside(x + (i + .5) / SS, y + (j + .5) / SS)
                          for i in range(SS) for j in range(SS))
                if hit:
                    s.blend(x, y, rgb, alpha * hit / (SS * SS))

    def rrect(s, x, y, w, h, r, rgb, alpha=1.0, stroke=None):
        def inside(fx, fy):
            dx = max(x + r - fx, 0, fx - (x + w - r))
            dy = max(y + r - fy, 0, fy - (y + h - r))
            d = math.hypot(dx, dy)
            return d <= r and (stroke is None or d >= r - stroke or
                               (dx == 0 and dy == 0 and min(fx - x, x + w - fx, fy - y, y + h - fy) < stroke))
        s.shape(x, y, x + w, y + h, inside, rgb, alpha)

    def text(s, x, y, string, px, rgb, alpha=1.0):
        for k, c in enumerate(string):
            g = glyph(c)
            gx = x + k * 6 * px
            for ry, row in enumerate(g):
                for rx, on in enumerate(row):
                    if on:  # slightly rounded font pixels read as smooth strokes
                        cx, cy, rad = gx + rx * px, y + ry * px, px * 0.18
                        s.rrect(cx, cy, px * 1.02, px * 1.02, rad, rgb, alpha)

    def png(s, path):
        raw = b''.join(b'\x00' + bytes(v for p in s.px[y * s.w:(y + 1) * s.w] for v in p)
                       for y in range(s.h))
        def chunk(t, d):
            return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
        with open(path, 'wb') as f:
            f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', s.w, s.h, 8, 6, 0, 0, 0))
                    + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

def hexrgb(v):
    v = int(v, 16)
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)

def mix(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))

# ---------------------------------------------------------------- art pieces
def background(path, name, version, bg, accent, hilite, W=1280, H=720):
    c = Canvas(W, H, (*bg, 255))
    # vertical gradient + soft radial glow behind the menu
    top, bot = mix(bg, (0, 0, 0), 0.35), mix(bg, hilite, 0.10)
    for y in range(H):
        for x in range(W):
            g = math.exp(-(((x - W / 2) / (W * .38)) ** 2 + ((y - H * .52) / (H * .42)) ** 2))
            col = mix(mix(top, bot, y / H), mix(bg, hilite, 0.22), g * 0.55)
            c.px[y * W + x] = [*col, 255]
    # logo: a silver ring with a notch, above the title
    cx, cy, R = W / 2, 92, 30
    def ring(fx, fy):
        d = math.hypot(fx - cx, fy - cy)
        ang = math.atan2(fy - cy, fx - cx)
        return R - 7 <= d <= R and not (-0.55 < ang < 0.05)
    c.shape(cx - R - 2, cy - R - 2, cx + R + 2, cy + R + 2, ring, accent)
    c.rrect(cx - 5, cy - 5, 10, 10, 5, hilite)
    title = name
    c.text((W - text_width(title, 5)) / 2, 140, title, 5, (235, 240, 246))
    sub = "Choose your system"
    c.text((W - text_width(sub, 3)) / 2, 192, sub, 3, accent, 0.85)
    hint = "\u2191 \u2193  to choose     Enter  to start"
    c.text((W - text_width(hint, 2)) / 2, H - 60, hint, 2, accent, 0.7)
    ver = f"{name} {version}"
    c.text(W - text_width(ver, 2) - 24, H - 30, ver, 2, accent, 0.4)
    c.png(path)

def tile(path, label, kind, accent, hilite, W=600, H=128):
    c = Canvas(W, H)            # transparent: the box comes from item_*/select_* pixmaps
    ix, iy, S = 70, (H - 64) / 2, 64
    if kind == 'silver':     # ring logo
        cx, cy, R = ix + S / 2, iy + S / 2, S / 2
        def ring(fx, fy):
            d = math.hypot(fx - cx, fy - cy)
            ang = math.atan2(fy - cy, fx - cx)
            return R - 9 <= d <= R and not (-0.55 < ang < 0.05)
        c.shape(ix - 1, iy - 1, ix + S + 1, iy + S + 1, ring, accent)
        c.rrect(cx - 7, cy - 7, 14, 14, 7, hilite)
    elif kind == 'more':     # three dots
        for k in range(3):
            c.rrect(ix + 4 + k * 22, iy + S / 2 - 7, 14, 14, 7, accent)
    else:                    # a generic application window glyph
        c.rrect(ix, iy + 4, S, S - 8, 8, (120, 180, 255), 1.0, stroke=5)
        c.rrect(ix, iy + 4, S, 16, 6, (120, 180, 255))
        for k in range(3):
            c.rrect(ix + S - 14 - k * 11, iy + 9, 6, 6, 3, (20, 30, 45))
    c.text(ix + S + 36, (H - 7 * 6) / 2, label, 6, (236, 240, 245))
    c.png(path)

def nine_slice(outdir, prefix, border_rgb, border_a, fill_rgb, fill_a, stroke):
    """Rounded box cut into 9 pieces; GRUB stretches it to each menu item."""
    B = 16
    full = Canvas(3 * B, 3 * B)
    full.rrect(1, 1, 3 * B - 2, 3 * B - 2, B - 2, fill_rgb, fill_a)
    full.rrect(1, 1, 3 * B - 2, 3 * B - 2, B - 2, border_rgb, border_a, stroke=stroke)
    parts = {'nw': (0, 0), 'n': (1, 0), 'ne': (2, 0), 'w': (0, 1), 'c': (1, 1),
             'e': (2, 1), 'sw': (0, 2), 's': (1, 2), 'se': (2, 2)}
    for name, (gx, gy) in parts.items():
        piece = Canvas(B, B)
        for y in range(B):
            for x in range(B):
                piece.px[y * B + x] = list(full.px[(gy * B + y) * 3 * B + gx * B + x])
        piece.png(os.path.join(outdir, f'{prefix}_{name}.png'))

THEME = """# SilverOS boot chooser -- generated by src/art/mkart.py
title-text: ""
desktop-image: "background.png"
desktop-image-scale-method: "stretch"
desktop-color: "{bg}"
message-color: "#c0c8d4"
message-bg-color: "{bg}"

+ boot_menu {{
    left = 50%-350
    top = 50%-128
    width = 700
    height = 380
    item_height = 140
    item_spacing = 44
    item_padding = 10
    icon_width = 600
    icon_height = 128
    item_icon_space = 0
    item_color = "{bg}"
    selected_item_color = "{bg}"
    item_pixmap_style = "item_*.png"
    selected_item_pixmap_style = "select_*.png"
    scrollbar = false
}}

+ progress_bar {{
    id = "__timeout__"
    left = 50%-200
    top = 50%+250
    width = 400
    height = 6
    show_text = false
    fg_color = "{hilite}"
    bg_color = "#1f2a38"
    border_color = "{bg}"
}}
"""

def main():
    out, name, version, bg, accent, hilite = sys.argv[1:7]
    bg, accent, hilite = hexrgb(bg), hexrgb(accent), hexrgb(hilite)
    os.makedirs(os.path.join(out, 'icons'), exist_ok=True)
    background(os.path.join(out, 'background.png'), name, version, bg, accent, hilite)
    tile(os.path.join(out, 'icons', 'silveros.png'), name, 'silver', accent, hilite)
    tile(os.path.join(out, 'icons', 'windows.png'), 'Windows', 'windows', accent, hilite)
    tile(os.path.join(out, 'icons', 'advanced.png'), 'More options', 'more', accent, hilite)
    nine_slice(out, 'item', accent, 0.25, (255, 255, 255), 0.05, 1.5)
    nine_slice(out, 'select', hilite, 1.0, hilite, 0.16, 3)
    h = lambda c: '#%02x%02x%02x' % c
    with open(os.path.join(out, 'theme.txt'), 'w') as f:
        f.write(THEME.format(bg=h(bg), hilite=h(hilite)))

if __name__ == '__main__':
    main()
