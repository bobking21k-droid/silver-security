#!/usr/bin/env bash
# Turn a video clip into a moving home-screen background (needs ffmpeg on the build machine).
#   scripts/make-background.sh <clip> <name> [crop]    -> work/assets/backgrounds/<name>.svic
# <name> is what the Backgrounds screen calls it (letters, digits, spaces). [crop] is w:h:x:y in
# the source's own pixels: cut out a screen recording's borders and pick the 16:9 part to show.
# Without it the clip is cut to 16:9 around its centre.
# The file lives in work/ (not in git): only use clips you have the right to use. build-pack.sh
# puts every one in the image under /usr/share/silver/backgrounds/, and each becomes a choice.
#
# Format "SVIC": "SVIC" u16 width, u16 height, u16 frames, u16 fps, 32-byte name,
# then frames in YUV 4:2:0 (width*height grey, then quarter-size U and V).
set -euo pipefail
source "$(dirname "$0")/../config.sh"
clip=${1:?usage: make-background.sh <clip> <name> [w:h:x:y]}; name=${2:?give a name}; crop=${3:-}
[[ $name =~ ^[A-Za-z0-9\ ]{1,31}$ ]] || { echo "name: letters, digits and spaces, up to 31" >&2; exit 2; }
w=384 h=216 fps=12
if [[ -n $crop ]]; then vf="crop=$crop"; else vf="crop='min(iw,ih*16/9)':'min(ih,iw*9/16)'"; fi
mkdir -p "$WORK/assets/backgrounds"
slug=$(tr 'A-Z ' 'a-z-' <<< "$name")
raw="$WORK/assets/backgrounds/$slug.yuv"; out="$WORK/assets/backgrounds/$slug.svic"
ffmpeg -v error -y -i "$clip" -an -vf "$vf,fps=$fps,scale=${w}:${h}:flags=lanczos,format=yuv420p" -f rawvideo -pix_fmt yuv420p "$raw"
n=$(( $(stat -c %s "$raw") / (w * h * 3 / 2) ))
{ printf 'SVIC'; python3 -c "import struct,sys; sys.stdout.buffer.write(struct.pack('<HHHH32s', $w, $h, $n, $fps, '''$name'''.encode()))"; cat "$raw"; } > "$out"
rm -f "$raw"
echo "$out: \"$name\", $n frames, ${w}x${h} @ ${fps} fps"
