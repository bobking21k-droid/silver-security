# SilverOS build configuration -- every knob that shapes the OS lives here.
# Change OS_NAME and friends to rebrand the whole system.

OS_NAME="SilverOS"
OS_ID="silveros"
OS_VERSION="0.1.0"
OS_CODENAME="Genesis"
OS_HOSTNAME="silver"
OS_TAGLINE="Built from nothing. Every line, every pixel."

# Upstream sources (pinned). Checksums are recorded in checksums.sha256.
LINUX_VER="6.18.63"
MUSL_VER="1.2.6"
BUSYBOX_VER="1.38.0"
DROPBEAR_VER="2026.94"
UTIL_LINUX_VER="2.42.4"    # sfdisk (GPT partitioning for the installer)
E2FSPROGS_VER="1.47.4"     # mkfs.ext4

LINUX_URL="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${LINUX_VER}.tar.xz"
MUSL_URL="https://musl.libc.org/releases/musl-${MUSL_VER}.tar.gz"
BUSYBOX_URL="https://busybox.net/downloads/busybox-${BUSYBOX_VER}.tar.bz2"
DROPBEAR_URL="https://matt.ucc.asn.au/dropbear/releases/dropbear-${DROPBEAR_VER}.tar.bz2"
UTIL_LINUX_URL="https://mirrors.edge.kernel.org/pub/linux/utils/util-linux/v${UTIL_LINUX_VER%.*}/util-linux-${UTIL_LINUX_VER}.tar.xz"
E2FSPROGS_URL="https://mirrors.edge.kernel.org/pub/linux/kernel/people/tytso/e2fsprogs/v${E2FSPROGS_VER}/e2fsprogs-${E2FSPROGS_VER}.tar.xz"

ARCH="x86_64"
JOBS="${JOBS:-$(nproc)}"

TOP="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="${WORK:-$TOP/work}"          # all build output (git-ignored)
SOURCES="$WORK/sources"            # downloaded tarballs
BUILD="$WORK/build"                # unpacked + compiled trees
SYSROOT="$WORK/sysroot"            # musl + kernel headers (the toolchain sysroot)
ROOTFS="$WORK/rootfs"              # the final root filesystem
OUT="$WORK/out"                    # ISO / USB image
