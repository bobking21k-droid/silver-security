#!/usr/bin/env bash
# Build SilverOS end to end.  Usage: ./build.sh [stage...]
#   stages: fetch musl kernel userland pack rootfs bootloader image   (default: all, in order)
#   games (the hours-long Buildroot build of the games layer) is run by hand: scripts/build-games.sh
set -euo pipefail
cd "$(dirname "$0")"
stages=("${@:-fetch musl kernel userland pack rootfs bootloader image}")
for s in ${stages[*]}; do
  echo "==================== $s ===================="
  bash "scripts/$( [[ $s == fetch ]] && echo fetch || echo build-$s ).sh"
done
