#!/usr/bin/env bash
# Boot the built ISO in QEMU.  ./run-qemu.sh [bios|uefi] [-nographic]
set -euo pipefail
source "$(dirname "$0")/config.sh"
mode="${1:-bios}"; shift || true
img="$OUT/$OS_ID-$OS_VERSION-$ARCH.iso"
fw=()
if [[ $mode == uefi ]]; then
  ovmf=$(ls /usr/share/ovmf/OVMF.fd /usr/share/qemu/OVMF.fd 2>/dev/null | head -1)
  fw=(-bios "$ovmf")
fi
accel=(); [[ -w /dev/kvm ]] && accel=(-enable-kvm -cpu host)
exec qemu-system-x86_64 "${accel[@]}" "${fw[@]}" -m 1024 -smp 2 \
  -cdrom "$img" -boot d -nic user,model=e1000,hostfwd=tcp::2222-:22 "$@"
