#!/usr/bin/env bash
# Render demo videos of the boot experience on the build machine, using the exact drawing
# code that ships (src/silverboot/scenes.h and src/greeter). Handy for iterating on the look.
#   scripts/render-demo.sh [width height]      -> work/out/screens/*.mp4   (needs ffmpeg)
set -euo pipefail
source "$(dirname "$0")/../config.sh"
w="${1:-1920}"; h="${2:-1080}"
out="$OUT/screens"; tmp="$WORK/demo"; mkdir -p "$out" "$tmp"
FONTS="-DFONT_DIR=\"$TOP/src/common/fonts\""
cc -O2 -fno-math-errno "$FONTS" -o "$tmp/preview" "$TOP/src/silverboot/preview.c" -lm
cc -O2 -fno-math-errno -DRENDER "$FONTS" -o "$tmp/greeter-render" "$TOP/src/greeter/silver-greeter.c" -lm
enc=(-c:v libx264 -crf 17 -preset medium -pix_fmt yuv420p -movflags +faststart)
"$tmp/preview" "$w" "$h" silver  | ffmpeg -v error -y -f rawvideo -pix_fmt bgr0 -s "${w}x${h}" -r 30 -i - "${enc[@]}" "$tmp/chooser.mp4"
"$tmp/preview" "$w" "$h" windows | ffmpeg -v error -y -f rawvideo -pix_fmt bgr0 -s "${w}x${h}" -r 30 -i - "${enc[@]}" "$out/silveros-pick-windows.mp4"
# live search results and thumbnails when the downloader is built and the network is reachable
SILVER_FETCH="$WORK/stage/sbin/silver-fetch" SILVER_CA_FILE="$SOURCES/cacert.pem" SILVER_SEARCH_BG="$WORK/assets/search-bg.svid" \
  "$tmp/greeter-render" "$tmp/greeter.mp4" "$w" "$h"
# chooser -> transition -> check, a beat of black (the kernel starting), then the login
ffmpeg -v error -y -i "$tmp/chooser.mp4" -i "$tmp/greeter.mp4" -filter_complex \
  "[0:v]tpad=stop_mode=add:stop_duration=0.8:color=black[a];[a][1:v]concat=n=2:v=1:a=0[v]" -map "[v]" "${enc[@]}" "$out/silveros-boot-demo.mp4"
ls -lh "$out"/*.mp4
