#!/usr/bin/env python3
"""
Bake a font into a signed-distance-field (SDF) atlas for SilverOS.

An SDF stores, for every texel, the distance to the glyph outline. The renderer
(src/common/sgfx.h) turns that into smooth, anti-aliased text at any size, plus cheap
glows and outlines, with no font library on the target.

    mkfont.py <font file> <em px> <spread px> <out.sdf> [--wght N] [--messy] [--seed N]

--messy makes "Silver Scrawl": the thick base font is fattened and its outline is roughened
with layered noise (a slow wobble, medium bumps, fine grit), dry-brush bites, random ink
splatter, and every letter gets its own tilt, size and baseline. It is a modified derivative
of the base font (see src/common/fonts/LICENSES.md).

File format (little endian):
    "SSDF"  u16 em  u16 spread  i16 ascent  i16 descent  i16 cap_height  u16 nglyphs
    nglyphs x { u32 offset; u16 w, h; i16 xoff, yoff; u16 advance_x64 }   (ASCII 32..126)
    u8 texels[]   (128 = on the outline, 255 = `spread` px inside, 0 = `spread` px outside)
Metrics are in pixels at the baked em size; yoff is the cell's top relative to the baseline.

The baked .sdf files are committed, so building SilverOS needs neither this script's
dependencies (Pillow, numpy) nor the font files.
"""
import struct, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

S = 4  # supersampling factor for the distance computation


def edt(feature, R):
    """Distance (clamped to R) from every pixel to the nearest True pixel of `feature`."""
    INF = float(R + 1)
    g = np.where(feature, 0.0, INF)
    for d in range(1, R + 1):                      # vertical pass
        up = np.zeros_like(feature); up[d:] = feature[:-d]
        dn = np.zeros_like(feature); dn[:-d] = feature[d:]
        g = np.minimum(g, np.where(up | dn, float(d), INF))
    g2 = g * g
    best = g2.copy()
    for d in range(1, R + 1):                      # horizontal pass
        l = np.full_like(g2, INF * INF); l[:, d:] = g2[:, :-d]
        r = np.full_like(g2, INF * INF); r[:, :-d] = g2[:, d:]
        best = np.minimum(best, np.minimum(l, r) + d * d)
    return np.minimum(np.sqrt(best), R)


def sdf_of(mask, R):
    return np.where(mask, edt(~mask, R) - 0.5, -(edt(mask, R) - 0.5))


def value_noise(h, w, cell, rng):
    """Smooth random field in 0..1 with features about `cell` pixels wide."""
    cell = max(2, int(cell))
    g = rng.random((h // cell + 3, w // cell + 3)).astype(np.float32)
    ys, xs = np.arange(h) / cell, np.arange(w) / cell
    y0, x0 = ys.astype(int), xs.astype(int)
    fy, fx = (ys - y0)[:, None], (xs - x0)[None, :]
    fy, fx = fy * fy * (3 - 2 * fy), fx * fx * (3 - 2 * fx)
    a, b, c, d = g[y0][:, x0], g[y0][:, x0 + 1], g[y0 + 1][:, x0], g[y0 + 1][:, x0 + 1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def messy_mask(font, ch, em_hi, rng):
    """Render one glyph, then fatten and roughen it. Returns (mask, ox, oy): the mask and the
    position of its top-left corner relative to the glyph origin (baseline), in pixels."""
    pad = int(em_hi * 0.36)
    x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
    w, h = x1 - x0 + 2 * pad, y1 - y0 + 2 * pad
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((pad - x0, pad - y0), ch, font=font, fill=255, anchor="ls")
    sc = rng.uniform(0.95, 1.05)                                  # each letter: its own size, tilt and baseline
    img = img.rotate(rng.uniform(-3.2, 3.2), resample=Image.BICUBIC)
    if abs(sc - 1) > 0.002:
        nw, nh = int(w * sc), int(h * sc)
        small = img.resize((nw, nh), Image.BICUBIC)
        img = Image.new("L", (w, h), 0); img.paste(small, ((w - nw) // 2, (h - nh) // 2))
    base = np.asarray(img) >= 128
    R = int(em_hi * 0.2)
    sd = sdf_of(base, R)
    n1 = value_noise(h, w, em_hi * 0.24, rng) - 0.5                # slow wobble
    n2 = value_noise(h, w, em_hi * 0.055, rng) - 0.5               # bumps
    n3 = value_noise(h, w, em_hi * 0.016, rng) - 0.5               # grit
    field = sd + 0.030 * em_hi + (n1 * 0.026 + n2 * 0.012 + n3 * 0.0055) * em_hi * 2
    streak = value_noise(h, w, em_hi * 0.022, rng)                 # dry-brush bites near the edge
    bite = (streak > 0.79) & (sd < em_hi * 0.03) & (sd > -em_hi * 0.055)
    mask = (field > 0) & ~bite
    yy, xx = np.ogrid[:h, :w]
    for _ in range(int(rng.integers(1, 4))):                       # ink splatter
        cx, cy = int(rng.integers(0, w)), int(rng.integers(0, h))
        r = rng.uniform(0.014, 0.042) * em_hi
        if -R * 0.2 < sd[cy, cx] < R * 0.8:
            mask |= ((yy - cy) ** 2 + (xx - cx) ** 2) < r * r
    dy = int(rng.uniform(-0.016, 0.016) * em_hi)
    return mask, x0 - pad, y0 - pad + dy


def load_font(path, size, wght):
    f = ImageFont.truetype(path, size)
    if wght:
        vals = []
        for a in f.get_variation_axes():
            nm = a["name"] if isinstance(a["name"], str) else a["name"].decode()
            vals.append(wght if "eight" in nm else a["default"])
        f.set_variation_by_axes(vals)
    return f


def bake(path, em, spread, out, wght=None, messy=False, seed=1):
    font = load_font(path, em * S, wght)
    rng = np.random.default_rng(seed)
    ascent, descent = font.getmetrics()
    cap = -font.getbbox("H", anchor="ls")[1]
    R = spread * S
    glyphs, blobs, offset = [], [], 0
    for code in range(32, 127):
        ch = chr(code)
        adv = font.getlength(ch) / S + (0.032 * em if messy and ch != " " else 0)
        bb = font.getbbox(ch, anchor="ls")
        if bb[2] <= bb[0] or bb[3] <= bb[1] or ch == " ":
            glyphs.append((0, 0, 0, 0, 0, round(adv * 64)))
            continue
        if messy:
            mask, ox, oy = messy_mask(font, ch, em * S, rng)
            # pad the hi-res mask out to the spread and snap it to the output grid
            gx0, gy0 = (ox // S) * S - R, (oy // S) * S - R
            gw, gh = -(-(ox + mask.shape[1] - gx0 + R) // S), -(-(oy + mask.shape[0] - gy0 + R) // S)
            canvas = np.zeros((gh * S, gw * S), bool)
            canvas[oy - gy0:oy - gy0 + mask.shape[0], ox - gx0:ox - gx0 + mask.shape[1]] = mask
            inside = canvas
        else:
            x0, y0, x1, y1 = bb
            gx0, gy0 = (x0 // S) * S - R, (y0 // S) * S - R
            gw, gh = -(-(x1 - gx0 + R) // S), -(-(y1 - gy0 + R) // S)
            img = Image.new("L", (gw * S, gh * S), 0)
            ImageDraw.Draw(img).text((-gx0, -gy0), ch, font=font, fill=255, anchor="ls")
            inside = np.asarray(img) >= 128
        sd = sdf_of(inside, R)
        sd = sd.reshape(gh, S, gw, S).mean(axis=(1, 3))           # downsample to the output grid
        tex = np.clip(128 + sd / R * 127, 0, 255).astype(np.uint8)
        glyphs.append((offset, gw, gh, gx0 // S, gy0 // S, round(adv * 64)))
        blobs.append(tex.tobytes()); offset += gw * gh
    with open(out, "wb") as f:
        f.write(b"SSDF" + struct.pack("<HHhhhH", em, spread, ascent // S, descent // S, cap // S, len(glyphs)))
        for g in glyphs:
            f.write(struct.pack("<IHHhhH", *g))
        f.write(b"".join(blobs))
    print(f"{out}: {len(glyphs)} glyphs, {offset} texels, cap height {cap // S}px at em {em}")


if __name__ == "__main__":
    a = sys.argv[1:]
    opts = {"wght": None, "messy": False, "seed": 1}
    pos = []
    i = 0
    while i < len(a):
        if a[i] == "--wght": opts["wght"] = int(a[i + 1]); i += 2
        elif a[i] == "--seed": opts["seed"] = int(a[i + 1]); i += 2
        elif a[i] == "--messy": opts["messy"] = True; i += 1
        else: pos.append(a[i]); i += 1
    bake(pos[0], int(pos[1]), int(pos[2]), pos[3], opts["wght"], opts["messy"], opts["seed"])
