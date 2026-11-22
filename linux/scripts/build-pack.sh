#!/usr/bin/env bash
# Pack games.sqfs: everything SilverOS keeps on the USB stick / disk instead of in memory, mounted
# read-only on top of /usr at boot (rootfs-overlay/sbin/silver-games-mount):
#   - the games layer (glibc, Mesa, Xorg, PulseAudio, BlueZ, 32-bit libraries; scripts/build-games.sh)
#   - the kernel modules (Wi-Fi, Bluetooth and graphics drivers, signed) with their lookup tables
#   - the Wi-Fi, Bluetooth and graphics firmware
#   - the Search background clip, the moving home backgrounds and the dancer
# Run after the kernel is built. Needs both Buildroot layers built; otherwise it does nothing.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
t64="$BUILD/games-x86_64/target"; t32="$BUILD/games-i686/target"
if [[ -d "$t64/usr/lib" && -d "$t32/usr/lib" ]]; then
  g="$BUILD/games-root"; rm -rf "$g"; mkdir -p "$g/usr"
  cp -a "$t64/usr/." "$g/usr/"
  # Buildroot keeps glibc, bash, udev and friends in /bin /sbin /lib; they all live in /usr here
  for d in bin sbin lib libexec; do
    [[ -d "$t64/$d" ]] || continue
    mkdir -p "$g/usr/$d"; cp -a "$t64/$d/." "$g/usr/$d/"
    find "$g/usr/$d" -maxdepth 1 -type l | while read -r l; do
      t=$(readlink "$l"); case "$t" in ../usr/*) ln -sfn "../${t#../usr/}" "$l"; [[ -e "$l" ]] || ln -sfn "${t#../usr/}" "$l" ;; esac
    done
  done
  # links written for Buildroot's layout, fixed for this one: "../../../usr/share/x" under /etc becomes
  # "/usr/share/x"; "../../lib/libx.so.1" next to the libraries becomes "libx.so.1"
  find "$g/usr/etc-games" "$g/usr/lib" "$g/usr/lib32" -type l 2>/dev/null | while read -r l; do
    t=$(readlink "$l")
    case "$t" in
      ../*usr/*) ln -sfn "/usr/${t#*usr/}" "$l" ;;
      ../../lib/*) ln -sfn "${t#../../lib/}" "$l" ;;
    esac
  done || true
  # links into /usr/bin from usr/sbin etc. that now sit in the same tree stay valid; report any that don't
  find "$g/usr" -xtype l | grep -v -E '/usr/(bin|sbin)/udev|/etc-games/|^/usr/' | head -5 | sed 's/^/   (dangling) /' || true
  mkdir -p "$g/usr/lib32"; cp -a "$t32/usr/lib/." "$g/usr/lib32/"           # the 32-bit libraries the Steam app needs
  for f in "$t32"/lib/*.so*; do [[ -e "$f" ]] && cp -a "$f" "$g/usr/lib32/"; done
  # its /etc pieces live in /usr/etc-games; SilverOS's /etc links to them
  mkdir -p "$g/usr/etc-games"
  for e in fonts X11 dbus-1 pulse udev xdg bluetooth libinput; do [[ -e "$t64/etc/$e" ]] && cp -a "$t64/etc/$e" "$g/usr/etc-games/"; done
  printf '/usr/lib\n/usr/lib32\n/usr/lib32/dri\n' > "$g/usr/etc-games/ld.so.conf"
  cp -a "$TOP/games-overlay/usr/." "$g/usr/"
  cp "$BUILD/games-x86_64/host/x86_64-buildroot-linux-gnu/sysroot/usr/bin/ldd" "$g/usr/bin/ldd"      # Steam checks its 32-bit program with ldd
  cp "$BUILD/games-x86_64/host/x86_64-buildroot-linux-gnu/sysroot/sbin/ldconfig" "$g/usr/sbin/ldconfig"   # Steam's runtime check calls /sbin/ldconfig
  # graphics card and Bluetooth firmware (firmware/gpu.list), from the linux-firmware tarball
  # (older GSP versions left out), with the links its WHENCE file lists; rc.boot points the
  # kernel's firmware search here before it loads those drivers
  fwdst="$g/usr/lib/firmware"; mkdir -p "$fwdst"; lf="linux-firmware-$FIRMWARE_TAG"
  mapfile -t pats < <(grep -v '^\s*\(#\|$\)' "$TOP/firmware/gpu.list" | sed "s|^|$lf/|")
  tar -C "$fwdst" --strip-components=1 --wildcards --exclude='*gsp-535*' -xf "$SOURCES/$lf.tar.xz" "${pats[@]}"
  tar -xOf "$SOURCES/$lf.tar.xz" "$lf/WHENCE" | sed -n 's/^Link: \(.*\) -> \(.*\)$/\1 \2/p' | while read -r l t; do
    top=$(echo "$l" | cut -d/ -f1-2); [[ $l == nvidia/* ]] || top=${l%%/*}
    [[ -d "$fwdst/$top" && ! -e "$fwdst/$l" ]] || continue
    mkdir -p "$fwdst/$(dirname "$l")"; ln -s "$t" "$fwdst/$l"
    [[ -e "$fwdst/$l" ]] || rm "$fwdst/$l"                              # (points at something left out)
  done
  # kernel modules, with their lookup tables made here (the build machine's kmod depmod)
  kv=$(ls "$OUT/modules/lib/modules" | head -1)
  tm=$(mktemp -d); mkdir -p "$tm/lib/modules"; cp -a "$OUT/modules/lib/modules/$kv" "$tm/lib/modules/"
  depmod -b "$tm" "$kv"
  mkdir -p "$g/usr/lib/modules"; cp -a "$tm/lib/modules/$kv" "$g/usr/lib/modules/"; rm -rf "$tm"
  # Wi-Fi firmware (firmware/wifi.list; Intel's flattened, as the kernel asks for it)
  grep -v '^\s*\(#\|$\)' "$TOP/firmware/wifi.list" | while read -r rel; do
    case "$rel" in intel/iwlwifi/*) cp "$SOURCES/firmware/$rel" "$fwdst/" ;; *) mkdir -p "$fwdst/$(dirname "$rel")"; cp "$SOURCES/firmware/$rel" "$fwdst/$rel" ;; esac
  done
  # the Search background clip and the dancer (optional, see make-search-bg.sh / make-dancer.py)
  mkdir -p "$g/usr/share/silver"
  for a in search-bg.svid dancer.sprite; do [[ -f "$WORK/assets/$a" ]] && cp "$WORK/assets/$a" "$g/usr/share/silver/"; done
  # moving home-screen backgrounds (optional, see make-background.sh)
  if compgen -G "$WORK/assets/backgrounds/*.svic" > /dev/null; then mkdir -p "$g/usr/share/silver/backgrounds"; cp "$WORK/assets/backgrounds/"*.svic "$g/usr/share/silver/backgrounds/"; fi
  [[ -d "$TOP/games-overlay/etc" ]] && cp -a "$TOP/games-overlay/etc/." "$g/usr/etc-games/"
  # the library cache, made here for the paths the running system will see
  mkdir -p "$g/etc"; cp "$g/usr/etc-games/ld.so.conf" "$g/etc/ld.so.conf"
  ldconfig -r "$g" -f /etc/ld.so.conf -C /usr/etc-games/ld.so.cache 2>/dev/null || ldconfig -r "$g" -f /etc/ld.so.conf -C /usr/etc-games/ld.so.cache
  rm -rf "$g/etc"
  mkdir -p "$OUT"; rm -f "$OUT/games.sqfs"
  "$BUILD/games-x86_64/host/bin/mksquashfs" "$g/usr" "$OUT/games.sqfs" -comp zstd -Xcompression-level 15 -all-root -no-progress -quiet
  ls -lh "$OUT/games.sqfs"
fi
