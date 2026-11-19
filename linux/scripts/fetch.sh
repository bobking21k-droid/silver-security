#!/usr/bin/env bash
# Download pinned upstream sources and verify them against checksums.sha256.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
mkdir -p "$SOURCES"
SUMS="$TOP/checksums.sha256"
touch "$SUMS"

for url in "$LINUX_URL" "$MUSL_URL" "$BUSYBOX_URL" "$DROPBEAR_URL" "$UTIL_LINUX_URL" "$E2FSPROGS_URL" "$BEARSSL_URL" "$CACERT_URL" "$LIBNL_URL" "$KBD_URL" "$LIBMNL_URL" "$LIBNFTNL_URL" "$NFT_URL" "$WPA_URL" "$HOSTAPD_URL" "$BUILDROOT_URL" "$FIRMWARE_TARBALL_URL"; do
  f="$(basename "$url")"
  if [[ ! -f "$SOURCES/$f" ]]; then
    echo ">> fetching $f"
    curl -fL --retry 4 -o "$SOURCES/$f.part" "$url"
    mv "$SOURCES/$f.part" "$SOURCES/$f"
  fi
  sum="$(sha256sum "$SOURCES/$f" | cut -d' ' -f1)"
  if grep -q "  $f\$" "$SUMS"; then
    grep "  $f\$" "$SUMS" | (cd "$SOURCES" && sha256sum -c --quiet -) \
      || { echo "!! checksum mismatch for $f" >&2; exit 1; }
  else
    echo "$sum  $f" >> "$SUMS"   # trust-on-first-use; review and commit
    echo ">> recorded checksum for $f"
  fi
done
# Wi-Fi firmware and the regulatory database: one file at a time, each checked like the tarballs
fw="$SOURCES/firmware"; mkdir -p "$fw"
{ grep -v '^\s*\(#\|$\)' "$TOP/firmware/wifi.list" | sed "s|^|$FIRMWARE_BASE/|; s|\$|?h=$FIRMWARE_TAG|"
  echo "$REGDB_BASE/regulatory.db"; echo "$REGDB_BASE/regulatory.db.p7s"; } | while read -r url; do
  rel="${url#$FIRMWARE_BASE/}"; rel="${rel#$REGDB_BASE/}"; rel="${rel%%\?*}"
  if [[ ! -f "$fw/$rel" ]]; then
    mkdir -p "$(dirname "$fw/$rel")"
    curl -fsL --retry 4 -o "$fw/$rel.part" "$url" && mv "$fw/$rel.part" "$fw/$rel" || { echo "!! could not fetch $rel" >&2; exit 1; }
  fi
  key="firmware/$rel"
  if grep -q "  $key\$" "$SUMS"; then
    grep "  $key\$" "$SUMS" | (cd "$SOURCES" && sha256sum -c --quiet -) || { echo "!! checksum mismatch for $key" >&2; exit 1; }
  else
    (cd "$SOURCES" && sha256sum "$key") >> "$SUMS"
  fi
done
echo ">> sources ok"
