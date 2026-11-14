#!/usr/bin/env bash
# Download pinned upstream sources and verify them against checksums.sha256.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
mkdir -p "$SOURCES"
SUMS="$TOP/checksums.sha256"
touch "$SUMS"

for url in "$LINUX_URL" "$MUSL_URL" "$BUSYBOX_URL" "$DROPBEAR_URL"; do
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
echo ">> sources ok"
