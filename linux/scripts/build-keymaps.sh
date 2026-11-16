#!/usr/bin/env bash
# Keyboard layouts: build kbd's loadkeys for the build machine, compile each layout in keymaps.list
# to a binary keymap (what BusyBox's loadkmap reads), and write the list the Settings page shows.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
kb="$BUILD/kbd-$KBD_VER"; out="$WORK/keymaps"
[[ -d "$kb" ]] || tar -C "$BUILD" -xf "$SOURCES/kbd-$KBD_VER.tar.xz"
if [[ ! -x "$kb/src/loadkeys" ]]; then
  (cd "$kb" && ./configure --disable-vlock --disable-tests --disable-docs --datadir="$kb/data" >/dev/null && make -j"$JOBS" >/dev/null)
fi
rm -rf "$out"; mkdir -p "$out"; : > "$out/layouts.txt"
while IFS='|' read -r id map label; do
  [[ -z "$id" || "$id" == \#* ]] && continue
  if "$kb/src/loadkeys" -b -q "$map" > "$out/$id.bkmap" 2>/dev/null && [[ -s "$out/$id.bkmap" ]]; then
    echo "$id|$label" >> "$out/layouts.txt"
  else
    rm -f "$out/$id.bkmap"; echo "   (no keymap for $id: $map)"
  fi
done < "$TOP/keymaps.list"
echo ">> $(wc -l < "$out/layouts.txt") keyboard layouts in $out"
