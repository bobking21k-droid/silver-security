#!/usr/bin/env bash
# Turn a video clip into the Search pane's looping background (needs ffmpeg on the build machine).
#   scripts/make-search-bg.sh <clip> <start seconds> <end seconds>   -> work/assets/search-bg.svid
# The clip is stored as small greyscale frames; the greeter tints them black-and-red as it plays.
# The file lives in work/ (not in git): only use clips you have the right to use. build-rootfs.sh
# puts it in the image at /usr/share/silver/search-bg.svid when it exists; without it Search
# falls back to the drawn march.
#
# Format "SVID": "SVID" u16 width, u16 height, u16 frames, u16 fps, then frames of width*height bytes.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
clip=${1:?usage: make-search-bg.sh <clip> <start> <end>}; ss=${2:?}; to=${3:?}
w=384 h=216 fps=15
mkdir -p "$WORK/assets"
raw="$WORK/assets/search-bg.gray"
ffmpeg -v error -y -ss "$ss" -to "$to" -i "$clip" -vf "fps=$fps,scale=${w}:${h}:flags=lanczos,format=gray" -f rawvideo -pix_fmt gray "$raw"
n=$(( $(stat -c %s "$raw") / (w * h) ))
{ printf 'SVID'; python3 -c "import struct,sys; sys.stdout.buffer.write(struct.pack('<HHHH', $w, $h, $n, $fps))"; cat "$raw"; } > "$WORK/assets/search-bg.svid"
rm -f "$raw"
echo "$WORK/assets/search-bg.svid: $n frames, ${w}x${h} @ ${fps} fps"
