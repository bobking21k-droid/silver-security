#!/usr/bin/env python3
"""
Cut a small dancing figure out of a video clip, for the bottom of the Game Store.

    scripts/make-dancer.py <clip> <start s> <end s> <x> <y> <w> <h>   -> work/assets/dancer.sprite

The box (x, y, w, h, in the clip's pixels) should hold the figure and plain background only.
The background (dark blue-green) is keyed out, stray sparkles are dropped (small shapes are
thrown away) and the frames are trimmed to the figure, halved and saved:

    "SPRT" u16 w, u16 h, u16 frames, u16 fps, then frames of w*h RGBA

Like the Search background, the result lives in work/ (not in git): use clips you may use.
Needs ffmpeg and Pillow on the build machine.
"""
import glob, os, struct, subprocess, sys, tempfile
from PIL import Image

clip, ss, to, x, y, w, h = sys.argv[1], sys.argv[2], sys.argv[3], *map(int, sys.argv[4:8])
FPS = 15
tmp = tempfile.mkdtemp()
subprocess.run(["ffmpeg", "-v", "error", "-ss", ss, "-to", to, "-i", clip, "-vf", f"fps={FPS},crop={w}:{h}:{x}:{y}", f"{tmp}/f%04d.png"], check=True)
frames = []
for f in sorted(glob.glob(f"{tmp}/f*.png")):
    im = Image.open(f).convert("RGB"); px = im.load()
    a = [[0] * w for _ in range(h)]
    for j in range(h):
        for i in range(w):
            r, g, b = px[i, j]
            bg = (b > r + 10 and g > r + 4 and max(r, g, b) < 110) or (r > 90 and g > 100 and b < r and abs(r - g) < 30 and b > 80 and max(r, g, b) < 170)   # teal background, olive divider
            a[j][i] = 0 if bg else 255
    # keep the dancer (every sizeable shape: the body, the hat); drop sparkles and specks
    seen = [[0] * w for _ in range(h)]; best = []
    for j in range(h):
        for i in range(w):
            if a[j][i] and not seen[j][i]:
                comp, stack = [], [(i, j)]; seen[j][i] = 1
                while stack:
                    ci, cj = stack.pop(); comp.append((ci, cj))
                    for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        ni, nj = ci + di, cj + dj
                        if 0 <= ni < w and 0 <= nj < h and a[nj][ni] and not seen[nj][ni]: seen[nj][ni] = 1; stack.append((ni, nj))
                if len(comp) >= 80: best += comp
    keep = set(best)
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0)); op = out.load()
    for (i, j) in keep:
        r, g, b = px[i, j]; op[i, j] = (r, g, b, 255)
    frames.append(out)
# trim to the box that holds the figure in every frame, then halve
bx = [f.getbbox() for f in frames if f.getbbox()]
l, t, r_, b_ = min(q[0] for q in bx), min(q[1] for q in bx), max(q[2] for q in bx), max(q[3] for q in bx)
frames = [f.crop((l, t, r_, b_)) for f in frames]
fw, fh = frames[0].size
frames = [f.resize((fw // 2, fh // 2), Image.NEAREST) for f in frames]
fw, fh = frames[0].size
top = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
os.makedirs(os.path.join(top, "work", "assets"), exist_ok=True)
dst = os.path.join(top, "work", "assets", "dancer.sprite")
with open(dst, "wb") as f:
    f.write(b"SPRT" + struct.pack("<HHHH", fw, fh, len(frames), FPS))
    for fr in frames: f.write(fr.tobytes())
print(f"{dst}: {len(frames)} frames, {fw}x{fh} @ {FPS} fps")
