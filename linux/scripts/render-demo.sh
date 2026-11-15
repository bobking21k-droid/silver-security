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
SILVER_BOOT_WAV="$tmp/boot.wav" "$tmp/preview" "$w" "$h" silver  | ffmpeg -v error -y -f rawvideo -pix_fmt bgr0 -s "${w}x${h}" -r 30 -i - "${enc[@]}" "$tmp/chooser.mp4"
SILVER_BOOT_WAV="$tmp/win.wav" "$tmp/preview" "$w" "$h" windows | ffmpeg -v error -y -f rawvideo -pix_fmt bgr0 -s "${w}x${h}" -r 30 -i - "${enc[@]}" "$tmp/pick-windows.mp4"
ffmpeg -v error -y -i "$tmp/pick-windows.mp4" -i "$tmp/win.wav" -map 0:v -map 1:a -c:v copy -c:a aac -b:a 192k -shortest "$out/silveros-pick-windows.mp4"
# live search results and thumbnails when the downloader is built and the network is reachable
SILVER_SOUND_OUT="$tmp/greeter.wav" SILVER_FETCH="$WORK/stage/sbin/silver-fetch" SILVER_CA_FILE="$SOURCES/cacert.pem" SILVER_SEARCH_BG="$WORK/assets/search-bg.svid" SILVER_DANCER="$WORK/assets/dancer.sprite" \
  "$tmp/greeter-render" "$tmp/greeter.mp4" "$w" "$h"
# chooser -> transition -> check, a beat of black (the kernel starting), then the login,
# with the chooser's sound under it and the greeter's sound placed where the greeter starts
lead=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$tmp/chooser.mp4")
delay=$(python3 -c "print(int(($lead + 0.8) * 1000))")
total=$(python3 -c "print($lead + 0.8 + $(ffprobe -v error -show_entries format=duration -of csv=p=0 "$tmp/greeter.mp4"))")
ffmpeg -v error -y -i "$tmp/chooser.mp4" -i "$tmp/greeter.mp4" -i "$tmp/boot.wav" -i "$tmp/greeter.wav" -filter_complex \
  "[0:v]tpad=stop_mode=add:stop_duration=0.8:color=black[a];[a][1:v]concat=n=2:v=1:a=0[v];[3:a]adelay=${delay}|${delay}[g];[2:a][g]amix=inputs=2:normalize=0:duration=longest,apad=whole_dur=${total}[s]" \
  -map "[v]" -map "[s]" -t "$total" "${enc[@]}" -c:a aac -b:a 192k "$out/silveros-boot-demo.mp4"
ls -lh "$out"/*.mp4
