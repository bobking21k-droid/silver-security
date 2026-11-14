#!/usr/bin/env bash
# Build the SilverOS boot chooser: a standalone GRUB EFI binary with the theme
# (drawn by src/art/mkart.py) baked into its memdisk. silver-install copies it to
# the EFI System Partition as \EFI\SilverOS\grubx64.efi.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
theme="$WORK/theme"
rm -rf "$theme"
python3 "$TOP/src/art/mkart.py" "$theme" "$OS_NAME" "$OS_VERSION" "$COLOR_BG" "$COLOR_ACCENT" "$COLOR_HILITE"

# Embedded config: locate the real menu that silver-install writes on the ESP.
stub="$WORK/grub-stub.cfg"
cat > "$stub" <<'CFG'
search --no-floppy --set=esp --file /EFI/SilverOS/grub.cfg
set prefix=($esp)/EFI/SilverOS
configfile ($esp)/EFI/SilverOS/grub.cfg
CFG
args=("boot/grub/grub.cfg=$stub")
while IFS= read -r f; do args+=("boot/grub/themes/silver/$f=$theme/$f"); done < <(cd "$theme" && find . -type f -printf '%P\n')
mkdir -p "$OUT"
grub-mkstandalone -O x86_64-efi -o "$OUT/grubx64.efi" \
  --modules="part_gpt part_msdos fat ext2 normal linux chain search search_fs_uuid search_fs_file configfile all_video efi_gop gfxterm gfxmenu png echo test reboot halt" \
  "${args[@]}"
ls -lh "$OUT/grubx64.efi"
echo ">> boot chooser ready"
