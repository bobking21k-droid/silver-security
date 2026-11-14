#!/usr/bin/env bash
# Produce a hybrid ISO that boots on BIOS and UEFI PCs, from CD or USB stick.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
iso="$WORK/iso"
rm -rf "$iso"; mkdir -p "$iso/boot/grub"
cp "$OUT/vmlinuz" "$OUT/rootfs.cpio.xz" "$iso/boot/"
mkdir -p "$iso/boot/grub/themes" "$iso/boot/silver"
cp -r "$WORK/theme" "$iso/boot/grub/themes/silver"
cp "$OUT/grubx64.efi" "$iso/boot/silver/grubx64.efi"     # installed to the ESP by silver-install
touch "$iso/boot/silver/SILVEROS_MEDIA"                  # lets the installer find its boot media
sed -e "s|@OS_NAME@|$OS_NAME|g" -e "s|@OS_VERSION@|$OS_VERSION|g" "$TOP/boot/grub.cfg" > "$iso/boot/grub/grub.cfg"
img="$OUT/$OS_ID-$OS_VERSION-$ARCH.iso"
grub-mkrescue -o "$img" "$iso" -- -volid "${OS_NAME^^}" 2>&1 | grep -v '^xorriso\|^Drive\|^Media\|^libisofs\|^Added\|^Writing\|^ISO image\|^Written\|^$' || true
sha256sum "$img" > "$img.sha256"
ls -lh "$img"
echo ">> image ready: $img"
echo "   write to USB:  sudo dd if=$img of=/dev/sdX bs=4M status=progress oflag=sync"
