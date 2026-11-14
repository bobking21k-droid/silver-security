#!/usr/bin/env bash
# Build SilverOS end to end.  Usage: ./build.sh [stage...]
#   stages: fetch musl kernel userland rootfs bootloader image   (default: all, in order)
set -euo pipefail
cd "$(dirname "$0")"
stages=("${@:-fetch musl kernel userland rootfs bootloader image}")
for s in ${stages[*]}; do
  echo "==================== $s ===================="
  bash "scripts/$( [[ $s == fetch ]] && echo fetch || echo build-$s ).sh"
done
