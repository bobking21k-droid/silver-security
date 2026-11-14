#!/usr/bin/env bash
# Assemble the root filesystem and pack it as an xz-compressed initramfs.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
rm -rf "$ROOTFS"
mkdir -p "$ROOTFS"/{bin,sbin,usr/bin,usr/sbin,dev,proc,sys,run,tmp,var/lib,var/log,etc,root,home}
cp -a "$TOP/rootfs-overlay/." "$ROOTFS/"
find "$ROOTFS" -name .keep -delete
cp "$WORK/stage/bin/busybox" "$ROOTFS/bin/busybox"
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
