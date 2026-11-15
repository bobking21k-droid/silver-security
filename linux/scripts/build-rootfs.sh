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
if [[ -f "$WORK/assets/search-bg.svid" ]]; then mkdir -p "$ROOTFS/usr/share/silver"; cp "$WORK/assets/search-bg.svid" "$ROOTFS/usr/share/silver/"; fi   # optional, see make-search-bg.sh
if [[ -f "$WORK/assets/dancer.sprite" ]]; then mkdir -p "$ROOTFS/usr/share/silver"; cp "$WORK/assets/dancer.sprite" "$ROOTFS/usr/share/silver/"; fi   # optional, see make-dancer.py
# Wi-Fi firmware (Intel's flattened, as the kernel asks for it) and the regulatory database
fwsrc="$SOURCES/firmware"; fwdst="$ROOTFS/lib/firmware"; mkdir -p "$fwdst"
grep -v '^\s*\(#\|$\)' "$TOP/firmware/wifi.list" | while read -r rel; do
  case "$rel" in intel/iwlwifi/*) cp "$fwsrc/$rel" "$fwdst/" ;; *) mkdir -p "$fwdst/$(dirname "$rel")"; cp "$fwsrc/$rel" "$fwdst/$rel" ;; esac
done
cp "$fwsrc/regulatory.db" "$fwsrc/regulatory.db.p7s" "$fwdst/"
# graphics card firmware, from the linux-firmware tarball (older GSP firmware versions left out),
# with the links its WHENCE file lists
lf="linux-firmware-$FIRMWARE_TAG"; mapfile -t pats < <(grep -v '^\s*\(#\|$\)' "$TOP/firmware/gpu.list" | sed "s|^|$lf/|")
tar -C "$fwdst" --strip-components=1 --wildcards --exclude='*gsp-535*' -xf "$SOURCES/$lf.tar.xz" "${pats[@]}"
tar -xOf "$SOURCES/$lf.tar.xz" "$lf/WHENCE" | sed -n 's/^Link: \(.*\) -> \(.*\)$/\1 \2/p' | while read -r l t; do
  top=$(echo "$l" | cut -d/ -f1-2); [[ $l == nvidia/* ]] || top=${l%%/*}
  [[ -d "$fwdst/$top" && ! -e "$fwdst/$l" ]] || continue
  mkdir -p "$fwdst/$(dirname "$l")"; ln -s "$t" "$fwdst/$l"
  [[ -e "$fwdst/$l" ]] || rm "$fwdst/$l"                                # (points at something left out)
done
# kernel modules (Wi-Fi chips, graphics cards), each signed by this build's key
rm -rf "$ROOTFS/lib/modules"; cp -a "$OUT/modules/lib/modules" "$ROOTFS/lib/"
# where the games layer's programs expect things outside /usr (dangling without the layer)
mkdir -p "$ROOTFS/lib64"
ln -s /usr/lib/ld-linux-x86-64.so.2 "$ROOTFS/lib64/ld-linux-x86-64.so.2"
ln -s /usr/lib32/ld-linux.so.2 "$ROOTFS/lib/ld-linux.so.2"
ln -s /usr/lib/udev "$ROOTFS/lib/udev"
ln -s /usr/bin/bash "$ROOTFS/bin/bash"
for e in fonts X11 dbus-1 pulse udev xdg ld.so.conf ld.so.cache; do ln -s "/usr/etc-games/$e" "$ROOTFS/etc/$e"; done
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
