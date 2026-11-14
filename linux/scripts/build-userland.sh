#!/usr/bin/env bash
# Build static userland against musl: BusyBox, Dropbear SSH, silver-init.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
CC="$SYSROOT/usr/bin/musl-gcc"
HARDEN="-Os -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE"
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
cfg FEATURE_HAVE_RPC '# CONFIG_FEATURE_HAVE_RPC is not set'
cfg INETD '# CONFIG_INETD is not set'
cfg TELNETD '# CONFIG_TELNETD is not set'       # plaintext remote login: never
cfg TELNET '# CONFIG_TELNET is not set'
cfg FTPD '# CONFIG_FTPD is not set'
cfg TFTPD '# CONFIG_TFTPD is not set'
cfg HTTPD '# CONFIG_HTTPD is not set'
cfg INIT '# CONFIG_INIT is not set'             # we ship our own PID 1
cfg LINUXRC '# CONFIG_LINUXRC is not set'
yes '' | make -C "$bb" oldconfig >/dev/null
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

# ---- silver-init: our own PID 1 ----
$CC $HARDEN -static-pie -Wall -Wextra -o "$WORK/stage/sbin/init" "$TOP/src/init/init.c"

strip "$WORK/stage/bin/busybox" "$WORK/stage/sbin/dropbearmulti" "$WORK/stage/sbin/init"
ls -l "$WORK/stage/bin" "$WORK/stage/sbin"
echo ">> userland ready"
