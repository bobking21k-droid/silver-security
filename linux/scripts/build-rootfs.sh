#!/usr/bin/env bash
# Assemble the root filesystem and pack it as an xz-compressed initramfs.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
rm -rf "$ROOTFS"
mkdir -p "$ROOTFS"/{bin,sbin,usr/bin,usr/sbin,dev,proc,sys,run,tmp,var/lib,var/log,etc,root,home}
cp -a "$TOP/rootfs-overlay/." "$ROOTFS/"
find "$ROOTFS" -name .keep -delete
cp "$WORK/stage/bin/busybox" "$ROOTFS/bin/busybox"
mkdir -p "$ROOTFS/etc/ssl"; cp "$SOURCES/cacert.pem" "$ROOTFS/etc/ssl/cacert.pem"
# keyboard layouts (scripts/build-keymaps.sh): small binary keymaps, loaded with BusyBox loadkmap
bash "$TOP/scripts/build-keymaps.sh" >/dev/null
mkdir -p "$ROOTFS/usr/share/keymaps"; cp "$WORK/keymaps/"* "$ROOTFS/usr/share/keymaps/"
# Wi-Fi / Bluetooth / graphics firmware and kernel modules, and the big media files, live in
# games.sqfs (scripts/build-pack.sh) so they stay on the stick or disk instead of in memory
mkdir -p "$ROOTFS/lib"
ln -s /usr/lib/modules "$ROOTFS/lib/modules"; ln -s /usr/lib/firmware "$ROOTFS/lib/firmware"
[[ -f "$OUT/games.sqfs" ]] || echo "!! no games.sqfs: this image will have no Wi-Fi, Bluetooth, graphics or Steam (run scripts/build-games.sh)"
# where the games layer's programs expect things outside /usr (dangling without the layer)
mkdir -p "$ROOTFS/lib64"
ln -s /usr/lib/ld-linux-x86-64.so.2 "$ROOTFS/lib64/ld-linux-x86-64.so.2"
ln -s /usr/lib32/ld-linux.so.2 "$ROOTFS/lib/ld-linux.so.2"
ln -s /usr/lib/udev "$ROOTFS/lib/udev"
ln -s /usr/bin/bash "$ROOTFS/bin/bash"; ln -s /usr/sbin/ldconfig "$ROOTFS/sbin/ldconfig"
mkdir -p "$ROOTFS/etc/ssl/certs"; ln -s ../cacert.pem "$ROOTFS/etc/ssl/certs/ca-certificates.crt"   # Steam's downloads check certificates
for e in fonts X11 dbus-1 pulse udev xdg bluetooth libinput ld.so.conf ld.so.cache; do ln -s "/usr/etc-games/$e" "$ROOTFS/etc/$e"; done
cp "$WORK/stage/sbin/"* "$ROOTFS/sbin/"     # init, dropbear, sfdisk, mke2fs, e2fsck, silver-efiboot
ln -sf mke2fs "$ROOTFS/sbin/mkfs.ext4"; ln -sf e2fsck "$ROOTFS/sbin/fsck.ext4"
ln -s sbin/init "$ROOTFS/init"   # the kernel runs /init from an initramfs
for a in dropbear dropbearkey; do ln -sf dropbearmulti "$ROOTFS/sbin/$a"; done

# Expand every applet into a symlink at the path BusyBox reports for it.
"$WORK/stage/bin/busybox" --list-full | while read -r p; do
  [[ "$p" == sbin/init || -e "$ROOTFS/$p" ]] && continue
  rel="$(realpath -m --relative-to="$ROOTFS/$(dirname "$p")" "$ROOTFS/bin/busybox")"
  ln -s "$rel" "$ROOTFS/$p"
done

# Branding: substitute @VARS@ from config.sh
for f in $(grep -rl '@OS_' "$ROOTFS/etc"); do
  sed -i -e "s|@OS_NAME@|$OS_NAME|g" -e "s|@OS_ID@|$OS_ID|g" -e "s|@OS_VERSION@|$OS_VERSION|g" \
         -e "s|@OS_CODENAME@|$OS_CODENAME|g" -e "s|@OS_HOSTNAME@|$OS_HOSTNAME|g" \
         -e "s|@OS_TAGLINE@|$OS_TAGLINE|g" "$f"
done

# Ownership & permissions (cpio is built with --owner root)
chmod 600 "$ROOTFS/etc/shadow"
chmod 700 "$ROOTFS/root"
chmod 4755 "$ROOTFS/bin/busybox"   # setuid for su/passwd/login; BusyBox drops it for every other applet
chmod 1777 "$ROOTFS/tmp"

mkdir -p "$OUT"
( cd "$ROOTFS" && find . -print0 | sort -z | \
  cpio --null -o -H newc --owner root:root --quiet ) | \
  xz -9 --check=crc32 > "$OUT/rootfs.cpio.xz"
ls -lh "$OUT/rootfs.cpio.xz"
echo ">> rootfs ready"
