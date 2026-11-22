#!/usr/bin/env bash
# Build the SilverOS kernel: x86_64_defconfig + kernel/silver.config.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
src="$BUILD/linux-$LINUX_VER"
mkdir -p "$BUILD"
[[ -d "$src" ]] || tar -C "$BUILD" -xf "$SOURCES/linux-$LINUX_VER.tar.xz"
cd "$src"
# the pretend Wi-Fi radios (for tests) are off unless asked for on the command line
sed -i 's/^static int radios = 2;/static int radios;/' drivers/net/wireless/virtual/mac80211_hwsim.c
make ARCH=x86_64 x86_64_defconfig >/dev/null
scripts/kconfig/merge_config.sh -m .config "$TOP/kernel/silver.config" >/dev/null
# the Wi-Fi regulatory database goes inside the kernel: Wi-Fi starts before the disk is mounted
scripts/config --set-str EXTRA_FIRMWARE "regulatory.db regulatory.db.p7s" --set-str EXTRA_FIRMWARE_DIR "$SOURCES/firmware"
make ARCH=x86_64 olddefconfig >/dev/null
# Report fragment options the kernel silently dropped (renamed / unmet deps).
while IFS= read -r line; do
  [[ "$line" =~ ^CONFIG_([A-Z0-9_]+)=(.*)$ ]] || continue
  name="${BASH_REMATCH[1]}"; want="${BASH_REMATCH[2]}"
  if [[ "$want" == n ]]; then
    grep -q "^CONFIG_$name=[ym]" .config && echo "   note: CONFIG_$name still enabled"
  else
    grep -qx "CONFIG_$name=$want" .config || echo "   note: CONFIG_$name!=$want ($(grep -E "CONFIG_$name[= ]" .config || echo unset))"
  fi
done < "$TOP/kernel/silver.config"
make ARCH=x86_64 -j"$JOBS" bzImage modules
rm -rf "$OUT/modules"
make ARCH=x86_64 INSTALL_MOD_PATH="$OUT/modules" INSTALL_MOD_STRIP=1 modules_install >/dev/null
rm -f "$OUT"/modules/lib/modules/*/build "$OUT"/modules/lib/modules/*/source
make ARCH=x86_64 INSTALL_HDR_PATH="$SYSROOT/usr" headers_install >/dev/null
mkdir -p "$OUT"
cp arch/x86/boot/bzImage "$OUT/vmlinuz"
cp .config "$OUT/kernel.config"
echo ">> kernel ready: $OUT/vmlinuz"
