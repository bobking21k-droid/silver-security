#!/usr/bin/env bash
# The games layer: a glibc graphics stack (Mesa, Vulkan, Xorg, PulseAudio, D-Bus) built from
# source with Buildroot, for the Steam app and Proton. Two builds: 64-bit (everything) and
# 32-bit (what the Steam app itself still needs). Usage: build-games.sh [x86_64|i686]...
set -euo pipefail
source "$(dirname "$0")/../config.sh"
br="$BUILD/buildroot-$BUILDROOT_VER"
[[ -d "$br" ]] || tar -C "$BUILD" -xf "$SOURCES/buildroot-$BUILDROOT_VER.tar.xz"
# Mesa's AMD Vulkan driver (RADV), which Buildroot doesn't offer: built along with radeonsi
grep -q '+= amd$' "$br/package/mesa3d/mesa3d.mk" || \
  sed -i 's/^MESA3D_VULKAN_DRIVERS-$(BR2_PACKAGE_MESA3D_VULKAN_DRIVER_INTEL)   += intel$/&\nMESA3D_VULKAN_DRIVERS-$(BR2_PACKAGE_MESA3D_GALLIUM_DRIVER_RADEONSI) += amd/' "$br/package/mesa3d/mesa3d.mk"
grep -q '+= amd$' "$br/package/mesa3d/mesa3d.mk"
for arch in "${@:-x86_64 i686}"; do
  out="$BUILD/games-$arch"; mkdir -p "$out"
  cp "$TOP/buildroot/games-${arch}_defconfig" "$br/configs/silver-games-${arch}_defconfig"
  make -C "$br" O="$out" BR2_DL_DIR="$SOURCES/buildroot-dl" "silver-games-${arch}_defconfig" >/dev/null
  make -C "$br" O="$out" BR2_DL_DIR="$SOURCES/buildroot-dl" BR2_JLEVEL="$JOBS"
  echo ">> games layer ($arch) ready: $out/target"
done

# ---- pack both layers into one image, mounted under /usr at boot (rc.boot: silver-games-mount)
t64="$BUILD/games-x86_64/target"; t32="$BUILD/games-i686/target"
if [[ -d "$t64/usr/lib" && -d "$t32/usr/lib" ]]; then
  g="$BUILD/games-root"; rm -rf "$g"; mkdir -p "$g/usr"
  cp -a "$t64/usr/." "$g/usr/"
  mkdir -p "$g/usr/lib32"; cp -a "$t32/usr/lib/." "$g/usr/lib32/"           # the 32-bit libraries the Steam app needs
  for f in "$t32"/lib/*.so*; do [[ -e "$f" ]] && cp -a "$f" "$g/usr/lib32/"; done
  # its /etc pieces live in /usr/etc-games; SilverOS's /etc links to them
  mkdir -p "$g/usr/etc-games"
  for e in fonts X11 dbus-1 pulse udev ssl xdg; do [[ -e "$t64/etc/$e" ]] && cp -a "$t64/etc/$e" "$g/usr/etc-games/"; done
  printf '/usr/lib\n/usr/lib32\n/usr/lib32/dri\n' > "$g/usr/etc-games/ld.so.conf"
  cp -a "$TOP/games-overlay/usr/." "$g/usr/"
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
  [[ -d "$TOP/games-overlay/etc" ]] && cp -a "$TOP/games-overlay/etc/." "$g/usr/etc-games/"
  # the library cache, made here for the paths the running system will see
  mkdir -p "$g/etc"; cp "$g/usr/etc-games/ld.so.conf" "$g/etc/ld.so.conf"
  ldconfig -r "$g" -f /etc/ld.so.conf -C /usr/etc-games/ld.so.cache 2>/dev/null || ldconfig -r "$g" -f /etc/ld.so.conf -C /usr/etc-games/ld.so.cache
  rm -rf "$g/etc"
  mkdir -p "$OUT"; rm -f "$OUT/games.sqfs"
  "$BUILD/games-x86_64/host/bin/mksquashfs" "$g/usr" "$OUT/games.sqfs" -comp zstd -Xcompression-level 15 -all-root -no-progress -quiet
  ls -lh "$OUT/games.sqfs"
fi
