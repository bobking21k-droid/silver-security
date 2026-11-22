#!/usr/bin/env bash
# Turn a video clip into the Search pane's looping background (needs ffmpeg on the build machine).
#   scripts/make-search-bg.sh [--hide x,y,w,h ...] <clip> <start> <end> [<start> <end> ...]   -> work/assets/search-bg.svid
# --hide paints out a box of the source video (in its own pixels), e.g. a watermark.
# Several start/end pairs are joined in order, and the result loops.
# The clip is stored as small greyscale frames; the greeter tints them black-and-red as it plays.
# The file lives in work/ (not in git): only use clips you have the right to use. build-rootfs.sh
# puts it in the image at /usr/share/silver/search-bg.svid when it exists; without it Search
# falls back to the drawn march.
#
# Format "SVID": "SVID" u16 width, u16 height, u16 frames, u16 fps, then frames of width*height bytes.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
hide=""
while [[ ${1:-} == --hide ]]; do IFS=, read -r hx hy hw hh <<< "$2"; hide+="delogo=x=$hx:y=$hy:w=$hw:h=$hh,"; shift 2; done
clip=${1:?usage: make-search-bg.sh <clip> <start> <end> [<start> <end> ...]}; shift
(( $# >= 2 && $# % 2 == 0 )) || { echo "give start/end pairs" >&2; exit 2; }
sel=""; while (( $# )); do sel+="${sel:++}between(t\,$1\,$2)"; shift 2; done
w=352 h=198 fps=12
mkdir -p "$WORK/assets"
raw="$WORK/assets/search-bg.gray"
ffmpeg -v error -y -i "$clip" -vf "${hide}select=$sel,setpts=N/FRAME_RATE/TB,fps=$fps,scale=${w}:${h}:flags=lanczos,format=gray" -f rawvideo -pix_fmt gray "$raw"
n=$(( $(stat -c %s "$raw") / (w * h) ))
{ printf 'SVID'; python3 -c "import struct,sys; sys.stdout.buffer.write(struct.pack('<HHHH', $w, $h, $n, $fps))"; cat "$raw"; } > "$WORK/assets/search-bg.svid"
rm -f "$raw"
echo "$WORK/assets/search-bg.svid: $n frames, ${w}x${h} @ ${fps} fps"
