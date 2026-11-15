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
BEARSSL_VER="0.6"          # TLS (silver-fetch: HTTPS with certificate checking)
LIBNL_VER="3.11.0"         # netlink library for wpa_supplicant
WPA_VER="2.11"             # wpa_supplicant (Wi-Fi: WPA2/WPA3) and hostapd (only for the Wi-Fi test)
FIRMWARE_TAG="20261019"    # linux-firmware release the Wi-Fi firmware comes from (firmware/wifi.list)

LINUX_URL="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${LINUX_VER}.tar.xz"
MUSL_URL="https://musl.libc.org/releases/musl-${MUSL_VER}.tar.gz"
BUSYBOX_URL="https://busybox.net/downloads/busybox-${BUSYBOX_VER}.tar.bz2"
DROPBEAR_URL="https://matt.ucc.asn.au/dropbear/releases/dropbear-${DROPBEAR_VER}.tar.bz2"
UTIL_LINUX_URL="https://mirrors.edge.kernel.org/pub/linux/utils/util-linux/v${UTIL_LINUX_VER%.*}/util-linux-${UTIL_LINUX_VER}.tar.xz"
BEARSSL_URL="https://bearssl.org/bearssl-${BEARSSL_VER}.tar.gz"
CACERT_URL="https://curl.se/ca/cacert.pem"     # Mozilla root certificates, as published by curl
LIBNL_URL="https://github.com/thom311/libnl/releases/download/libnl${LIBNL_VER//./_}/libnl-${LIBNL_VER}.tar.gz"
WPA_URL="https://w1.fi/releases/wpa_supplicant-${WPA_VER}.tar.gz"
HOSTAPD_URL="https://w1.fi/releases/hostapd-${WPA_VER}.tar.gz"
FIRMWARE_BASE="https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain"
REGDB_BASE="https://git.kernel.org/pub/scm/linux/kernel/git/wens/wireless-regdb.git/plain"
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
