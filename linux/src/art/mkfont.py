#!/usr/bin/env python3
"""
Bake a TrueType/OpenType font into a signed-distance-field (SDF) atlas for SilverOS.

An SDF stores, for every texel, the distance to the glyph outline. The renderer
(src/common/sgfx.h) turns that into smooth, anti-aliased text at any size, plus
cheap glows -- with no font library on the target.

    mkfont.py <font file> <em px> <spread px> <out.sdf>

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

S = 6  # supersampling factor for the distance computation


def edt(feature, R):
    """Distance (clamped to R) from every pixel to the nearest True pixel of `feature`."""
    h, w = feature.shape
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


def bake(path, em, spread, out):
    font = ImageFont.truetype(path, em * S)
    ascent, descent = font.getmetrics()
    cap = -font.getbbox("H", anchor="ls")[1]
    R = spread * S
    glyphs, blobs, offset = [], [], 0
    for code in range(32, 127):
        ch = chr(code)
        x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
        adv = font.getlength(ch) / S
        if x1 <= x0 or y1 <= y0 or ch == " ":
            glyphs.append((0, 0, 0, 0, 0, round(adv * 64)))
            continue
        # snap the hi-res box to the output grid, then pad by the spread
        gx0, gy0 = (x0 // S) * S - R, (y0 // S) * S - R
        gw = -(-(x1 - gx0 + R) // S)
        gh = -(-(y1 - gy0 + R) // S)
        img = Image.new("L", (gw * S, gh * S), 0)
        ImageDraw.Draw(img).text((-gx0, -gy0), ch, font=font, fill=255, anchor="ls")
        inside = np.asarray(img) >= 128
        sd = np.where(inside, edt(~inside, R) - 0.5, -(edt(inside, R) - 0.5))
        sd = sd.reshape(gh, S, gw, S).mean(axis=(1, 3))           # downsample
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
    bake(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4])
