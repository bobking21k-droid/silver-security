#!/usr/bin/env bash
# Build static userland against musl: BusyBox, Dropbear SSH, silver-init.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
CC="$SYSROOT/usr/bin/musl-gcc"
HARDEN="-Os -fstack-protector-strong -D_FORTIFY_SOURCE=2"
[[ -f "$SYSROOT/usr/include/linux/version.h" ]] || { echo "!! kernel headers missing; build kernel first" >&2; exit 1; }
# Let musl-gcc see the kernel headers installed into the sysroot.
export C_INCLUDE_PATH="$SYSROOT/usr/include"
mkdir -p "$BUILD" "$WORK/stage/bin" "$WORK/stage/sbin"

# ---- BusyBox: shell + core utilities ----
bb="$BUILD/busybox-$BUSYBOX_VER"
[[ -d "$bb" ]] || tar -C "$BUILD" -xf "$SOURCES/busybox-$BUSYBOX_VER.tar.bz2"
make -C "$bb" defconfig >/dev/null
cfg() { sed -i "/^#\? *CONFIG_$1[ =]/d" "$bb/.config"; echo "$2" >> "$bb/.config"; }
cfg STATIC 'CONFIG_STATIC=y'
cfg TC '# CONFIG_TC is not set'                 # broken against modern kernel headers
cfg INETD '# CONFIG_INETD is not set'
cfg TELNETD '# CONFIG_TELNETD is not set'       # plaintext remote login: never
cfg TELNET '# CONFIG_TELNET is not set'
cfg FTPD '# CONFIG_FTPD is not set'
cfg TFTPD '# CONFIG_TFTPD is not set'
cfg HTTPD '# CONFIG_HTTPD is not set'
cfg INIT '# CONFIG_INIT is not set'             # we ship our own PID 1
cfg FEATURE_DEFAULT_PASSWD_ALGO 'CONFIG_FEATURE_DEFAULT_PASSWD_ALGO="sha512"'   # not DES
cfg LINUXRC '# CONFIG_LINUXRC is not set'
{ yes '' || true; } | make -C "$bb" oldconfig >/dev/null
make -C "$bb" -j"$JOBS" CC="$CC" CONFIG_EXTRA_CFLAGS="-Os" busybox >/dev/null
cp "$bb/busybox" "$WORK/stage/bin/busybox"

# ---- Dropbear: SSH server (key-only login is enforced at runtime) ----
db="$BUILD/dropbear-$DROPBEAR_VER"
[[ -d "$db" ]] || tar -C "$BUILD" -xf "$SOURCES/dropbear-$DROPBEAR_VER.tar.bz2"
( cd "$db"
  [[ -f Makefile ]] || CC="$CC" ./configure --host=x86_64-linux-musl --enable-static \
      --disable-zlib --disable-lastlog --disable-utmp --disable-utmpx \
      --disable-wtmp --disable-wtmpx --disable-pututline --disable-pututxline >/dev/null
  make -j"$JOBS" PROGRAMS="dropbear dropbearkey" MULTI=1 STATIC=1 >/dev/null )
cp "$db/dropbearmulti" "$WORK/stage/sbin/dropbearmulti"

# ---- util-linux: sfdisk (GPT partitioning, used by silver-install) ----
ul="$BUILD/util-linux-$UTIL_LINUX_VER"
[[ -d "$ul" ]] || tar -C "$BUILD" -xf "$SOURCES/util-linux-$UTIL_LINUX_VER.tar.xz"
( cd "$ul"
  [[ -f Makefile ]] || CC="$CC" ./configure --host=x86_64-linux-musl --disable-shared --enable-static \
      --enable-static-programs=sfdisk --disable-nls --disable-liblastlog2 --disable-pam-lastlog2 \
      --without-python --without-systemd --without-udev --without-ncurses --without-ncursesw \
      --without-tinfo --without-readline --without-libmagic --without-cap-ng --without-btrfs \
      --without-cryptsetup --without-econf --without-selinux --without-audit --without-user \
      --without-utempter --disable-login --disable-su --disable-runuser --disable-chfn-chsh \
      --disable-sulogin --disable-wall --disable-write --disable-mesg --disable-pylibmount \
      --disable-makeinstall-chown --disable-makeinstall-setuid --disable-bash-completion >/dev/null
  make -j"$JOBS" sfdisk.static >/dev/null )
cp "$ul/sfdisk.static" "$WORK/stage/sbin/sfdisk"

# ---- e2fsprogs: mkfs.ext4 + e2fsck ----
e2="$BUILD/e2fsprogs-$E2FSPROGS_VER"
[[ -d "$e2" ]] || tar -C "$BUILD" -xf "$SOURCES/e2fsprogs-$E2FSPROGS_VER.tar.xz"
( cd "$e2"
  [[ -f Makefile ]] || CC="$CC" ./configure --host=x86_64-linux-musl --disable-nls --disable-elf-shlibs \
      --disable-fuse2fs --disable-e2initrd-helper --disable-uuidd --disable-defrag --disable-tls \
      --enable-libuuid --enable-libblkid --without-libarchive LDFLAGS=-static >/dev/null
  make -j"$JOBS" libs >/dev/null
  make -C misc -j"$JOBS" mke2fs >/dev/null
  make -C e2fsck -j"$JOBS" e2fsck >/dev/null )
cp "$e2/misc/mke2fs" "$WORK/stage/sbin/mke2fs"
cp "$e2/e2fsck/e2fsck" "$WORK/stage/sbin/e2fsck"

# ---- silver-efiboot: our own UEFI boot-entry manager ----
$CC $HARDEN -static -Wall -Wextra -o "$WORK/stage/sbin/silver-efiboot" "$TOP/src/tools/silver-efiboot.c"

# ---- silver-fetch: HTTPS downloads with certificate checking (BearSSL), used by the greeter's YouTube search ----
bs="$BUILD/bearssl-$BEARSSL_VER"
[[ -d "$bs" ]] || tar -C "$BUILD" -xf "$SOURCES/bearssl-$BEARSSL_VER.tar.gz"
make -C "$bs" -j"$(nproc)" CC="$CC" LD="$CC" AR=ar CFLAGS="-W -Wall -Os -fPIC -fstack-protector-strong" lib >/dev/null
$CC $HARDEN -static -DBUILD_EPOCH="$(date +%s)" -I"$bs/inc" -o "$WORK/stage/sbin/silver-fetch" "$TOP/src/tools/silver-fetch.c" "$bs/build/libbearssl.a"

# ---- Wi-Fi: libnl (netlink) + wpa_supplicant/wpa_cli (WPA2-Personal, open networks) ----
nl="$BUILD/libnl-$LIBNL_VER"
[[ -d "$nl" ]] || tar -C "$BUILD" -xf "$SOURCES/libnl-$LIBNL_VER.tar.gz"
if [[ ! -f "$SYSROOT/usr/lib/libnl-genl-3.a" ]]; then
  (cd "$nl" && CC="$CC" ./configure --host=x86_64-linux-musl --prefix=/usr --disable-shared --enable-static --disable-cli \
     --disable-debug >/dev/null && make -j"$JOBS" >/dev/null && make DESTDIR="$SYSROOT" install >/dev/null)
fi
wpa_build() {   # $1: wpa_supplicant or hostapd
  local src="$BUILD/$1-$WPA_VER" dir
  [[ -d "$src" ]] || tar -C "$BUILD" -xf "$SOURCES/$1-$WPA_VER.tar.gz"
  dir="$src/$1"
  cat > "$dir/.config" <<CFG
CONFIG_DRIVER_NL80211=y
CONFIG_LIBNL32=y
CONFIG_CTRL_IFACE=y
CONFIG_TLS=internal
CONFIG_CRYPTO=internal
CONFIG_INTERNAL_LIBTOMMATH=y
CONFIG_NO_RANDOM_POOL=y
CONFIG_GETRANDOM=y
CONFIG_ELOOP_POLL=y
CFG
  [[ "$1" == wpa_supplicant ]] && echo 'CONFIG_NO_STDOUT_DEBUG=y' >> "$dir/.config"
  [[ "$1" == hostapd ]] && printf 'CONFIG_IAPP=n\nCONFIG_NO_RADIUS=y\nCONFIG_NO_ACCOUNTING=y\nCONFIG_NO_VLAN=y\n' >> "$dir/.config"
  PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib/pkgconfig" PKG_CONFIG_SYSROOT_DIR="$SYSROOT" make -C "$dir" -j"$JOBS" CC="$CC" EXTRA_CFLAGS="-Os -fstack-protector-strong -I$SYSROOT/usr/include/libnl3" \
       LDFLAGS="-static -L$SYSROOT/usr/lib" >/dev/null
}
wpa_build wpa_supplicant
install -m 0755 "$BUILD/wpa_supplicant-$WPA_VER/wpa_supplicant/wpa_supplicant" "$BUILD/wpa_supplicant-$WPA_VER/wpa_supplicant/wpa_cli" "$WORK/stage/sbin/"
wpa_build hostapd                     # only for tests/dualboot-test.py (a pretend access point); not in the image
mkdir -p "$WORK/test"; install -m 0755 "$BUILD/hostapd-$WPA_VER/hostapd/hostapd" "$WORK/test/"

# ---- silver-greeter: graphical login on the framebuffer ----
$CC $HARDEN -O2 -fno-math-errno -static -Wall -Wextra -Wno-unused-function -DFONT_DIR="\"$TOP/src/common/fonts\"" \
    -o "$WORK/stage/sbin/silver-greeter" "$TOP/src/greeter/silver-greeter.c" -lm

# ---- silver-init: our own PID 1 ----
$CC $HARDEN -static -Wall -Wextra -o "$WORK/stage/sbin/init" "$TOP/src/init/init.c"

strip "$WORK/stage/bin/busybox" "$WORK/stage/sbin/"*
ls -l "$WORK/stage/bin" "$WORK/stage/sbin"
echo ">> userland ready"
