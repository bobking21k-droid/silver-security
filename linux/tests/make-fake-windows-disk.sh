#!/usr/bin/env bash
# Create a virtual disk laid out like a Windows 11 PC after "Shrink Volume":
#   p1 EFI System (FAT32, \EFI\Microsoft\Boot\bootmgfw.efi), p2 MSR, p3 "C:" (basic data),
#   then ~5 GB unallocated.  The stand-in bootmgfw.efi is a tiny GRUB that prints a banner.
set -euo pipefail
out=${1:?usage: make-fake-windows-disk.sh <disk.img> <ovmf-vars-out.fd>}
vars=${2:?}
tmp=$(mktemp -d)
cat > "$tmp/win.cfg" <<'CFG'
echo ""
echo "    ===  WINDOWS BOOT MANAGER (test stand-in)  ==="
echo ""
sleep 3600
CFG
grub-mkstandalone -O x86_64-efi -o "$tmp/bootmgfw.efi" --modules="echo sleep" "boot/grub/grub.cfg=$tmp/win.cfg"

rm -f "$out"; truncate -s 8G "$out"
sfdisk -q "$out" <<'PT'
label: gpt
start=2048, size=200MiB, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B, name="EFI system partition"
size=16MiB, type=E3C9E316-0B5C-4DB8-817D-F92DF00215AE, name="Microsoft reserved partition"
size=2GiB, type=EBD0A0A2-B9E5-4433-87C0-68B6B72699C7, name="Basic data partition"
PT
off=$((2048*512))
esp="$tmp/esp.img"; truncate -s 200M "$esp"; mkfs.vfat -F 32 -n SYSTEM "$esp" >/dev/null
mmd -i "$esp" ::/EFI ::/EFI/Microsoft ::/EFI/Microsoft/Boot ::/EFI/BOOT
mcopy -i "$esp" "$tmp/bootmgfw.efi" ::/EFI/Microsoft/Boot/bootmgfw.efi
mcopy -i "$esp" "$tmp/bootmgfw.efi" ::/EFI/BOOT/BOOTX64.EFI
dd if="$esp" of="$out" bs=1M seek=1 conv=notrunc status=none
cp /usr/share/OVMF/OVMF_VARS_4M.fd "$vars"
rm -rf "$tmp"
sfdisk -l "$out" | tail -4
