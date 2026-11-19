#!/usr/bin/env bash
# Build musl libc into the sysroot and provide a musl-gcc wrapper.
# Everything in SilverOS userland is statically linked against this libc.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
src="$BUILD/musl-$MUSL_VER"
mkdir -p "$BUILD"
[[ -d "$src" ]] || tar -C "$BUILD" -xf "$SOURCES/musl-$MUSL_VER.tar.gz"
cd "$src"
./configure --prefix="$SYSROOT/usr" --syslibdir="$SYSROOT/lib" --disable-shared >/dev/null
make -j"$JOBS" >/dev/null
make install >/dev/null
echo ">> musl ready: $SYSROOT/usr/bin/musl-gcc"
