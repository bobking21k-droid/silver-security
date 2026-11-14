#!/usr/bin/env bash
# Build the boot stages that silver-install puts on the EFI System Partition:
#   \EFI\SilverOS\silverboot.efi  our split-screen chooser (src/silverboot), first thing the PC runs
#   \EFI\SilverOS\grubx64.efi     standalone GRUB that loads the SilverOS kernel (theme baked in,
#                                  also used for the live USB menu)
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
  --modules="part_gpt part_msdos fat ext2 normal linux chain search search_fs_uuid search_fs_file configfile all_video efi_gop gfxterm gfxmenu png echo test reboot halt keystatus sleep" \
  "${args[@]}"
# silverboot: our own UEFI boot chooser (split screen, animated), C -> PE32+ via clang/lld
clang -target x86_64-unknown-windows -ffreestanding -fshort-wchar -mno-red-zone -fno-stack-protector \
      -mno-stack-arg-probe -fno-math-errno -O2 -Wall -Wextra \
      -c "$TOP/src/silverboot/silverboot.c" -o "$WORK/silverboot.o"
lld-link -subsystem:efi_application -entry:efi_main -nodefaultlib -out:"$OUT/silverboot.efi" "$WORK/silverboot.o"
ls -lh "$OUT/grubx64.efi" "$OUT/silverboot.efi"
echo ">> boot chooser ready"
