#!/usr/bin/env bash
# Make the small disk image the Bluetooth test needs: BlueZ's virtual controller emulator (btvirt).
# Usage: tests/bt-img.sh <out.img>     (needs work/build/btvirt from the BlueZ build)
set -eu
out=$1; d=$(mktemp -d)
cp "$(dirname "$0")/../work/build/btvirt/bluez-5.79/emulator/btvirt" "$d/btvirt"; cc -shared -fPIC -o "$d/vhci-blocking.so" "$(dirname "$0")/vhci-blocking.c" -ldl
truncate -s 8M "$out"; mke2fs -q -F -t ext2 -L BTTEST -d "$d" "$out"; rm -r "${d:?}"
echo "$out"
