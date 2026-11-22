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

# ---- pack both layers (and the kernel modules, firmware and media) into games.sqfs
bash "$TOP/scripts/build-pack.sh"
