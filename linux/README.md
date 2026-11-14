# SilverOS

A Linux system built from source, from nothing, where every part is chosen and owned:
the kernel config, the C library, the init system (written here, in C), the boot menu,
and soon the desktop down to the pixel.

> Status: **Phase 2, dual boot.** It boots live from a USB stick (BIOS + UEFI) and installs next
> to Windows with its own animated boot chooser and graphical login.

## Power-on experience

1. **The chooser** (`src/silverboot`, our own UEFI program in C): the screen splits exactly in
   half. On the left is SilverOS: a black-and-white world where a broken low-poly pencil hangs in
   the air, and time crawls until you press a key or move the mouse. On the right is Windows,
   in full colour: blue sky, clouds, a rolling green hill. A sliding sawtooth seam divides them,
   and shards drift across it, flipping colour as they cross. Use ← → or the mouse, then press
   Enter or click; `1` and `2` jump straight to SilverOS or Windows. It boots SilverOS after
   8 s if you don't touch anything.
2. **Pick SilverOS:** the chooser cuts straight to a full-screen classic Windows boot scene (blue
   sky, green hill, four-pane mark). It flips upside-down in grey, then melts in glitching colour
   streaks, flashes white, and the four-pane logo flickers between light and dark. Then a
   black-and-white system check runs (CPU, GPU, RAM, STORAGE) with values read from the real hardware.
3. **The greeter** (`src/greeter`): "SILVER OS" scribbles itself onto black, a pen appears and
   snaps, silver ink leaks from the break and spreads until it becomes the background, and the
   title slides into the bottom-right corner. Then a card with your name appears and
   you press Enter: no password. The full intro plays once per boot; any key skips it.
4. **Pick Windows:** the screen shatters and Windows Boot Manager starts as normal.

## What's in it

| Layer | What | Source |
|---|---|---|
| Bootloader | GRUB, branded menu, BIOS + UEFI hybrid image | `boot/grub.cfg` |
| Kernel | Linux 6.18 LTS, hardened, no loadable modules | `kernel/silver.config` |
| C library | musl, everything statically linked | `scripts/build-musl.sh` |
| PID 1 | **silver-init**, written from scratch: mounts, sysctl, service supervision, shutdown | `src/init/init.c` |
| Userland | BusyBox (shell + core utils; telnet/ftp/http daemons removed) | `scripts/build-userland.sh` |
| Remote access | Dropbear SSH: key-only, no root, off unless a key exists | `rootfs-overlay/etc/rc.boot` |
| Config & branding | users, sysctl hardening, issue/motd, os-release | `rootfs-overlay/`, `config.sh` |
| Boot chooser | **silverboot**: split-screen animated UEFI app, written from scratch | `src/silverboot/` |
| Login | **silver-greeter**: framebuffer intro + "press Enter" sign-in (asks for a password only if the account has one) | `src/greeter/` |
| Installer | **silver-install** (dual boot) + **silver-efiboot** (UEFI boot entries) | `rootfs-overlay/sbin/`, `src/tools/` |
| Disk tools | sfdisk (util-linux), mkfs.ext4/e2fsck (e2fsprogs), static | `scripts/build-userland.sh` |

Rebrand everything by editing `config.sh` (`OS_NAME`, `OS_HOSTNAME`, colours, ...).

## Build

On Ubuntu/Debian:

```sh
sudo apt-get install build-essential rsync bison flex bc libelf-dev libssl-dev cpio xz-utils \
     xorriso grub-pc-bin grub-efi-amd64-bin mtools qemu-system-x86 ovmf fdisk dosfstools
./build.sh                 # all stages: fetch musl kernel userland rootfs image
./build.sh userland rootfs image   # or rebuild individual stages
```

Output: `work/out/silveros-<version>-x86_64.iso`. Sources are pinned in `config.sh` and
verified against `checksums.sha256`.

## Try it

```sh
./run-qemu.sh bios         # or: ./run-qemu.sh uefi
tests/boot-test.py work/out/silveros-0.1.0-x86_64.iso   # automated boot + security checks
```

`tests/dualboot-test.py` runs the whole dual-boot story in QEMU against a fake Windows 11
disk (`tests/make-fake-windows-disk.sh`): install, chooser, melt, system check, greeter
intro, graphical login, then a reboot into "Windows". It saves screenshots of each step.

Verified in QEMU on both BIOS and UEFI: it boots to the login prompt, runs first-boot
admin-password setup, signs in without a password, refuses direct root login, gets DHCP
networking, and `su` works. Kernel
lockdown reports `[integrity]`. Runtime sysctls and `hidepid` are applied, and `/dev/mem`
is absent. Size: 26 MB ISO, 0.8 MB root filesystem.

Note: the live USB keeps everything in RAM, so the admin password you set is lost at
reboot. The install makes it persistent.

## Put it on a USB stick

```sh
sudo dd if=work/out/silveros-0.1.0-x86_64.iso of=/dev/sdX bs=4M status=progress oflag=sync
```

Then boot the PC from USB. You may need to turn off Secure Boot, because this image isn't
signed yet. On the first boot, press Enter on any console (screen or serial) to choose an admin (`root`)
password, or type `skip` to look around without one. Signing in as `silver` needs no password.

## Install next to Windows (dual boot)

Before you start, in Windows:

1. **Back up anything important.** The installer only writes to free space, but this is your disk.
2. If BitLocker is on, have your recovery key ready (https://aka.ms/myrecoverykey).
3. Turn off **Fast Startup**: Control Panel > Power Options > Choose what the power buttons do.
4. Make room: right-click Start > **Disk Management** > right-click C: > **Shrink Volume**.
   Shrink by at least 4 GB (20+ GB is comfortable) and leave the space **Unallocated**.

Then boot the USB stick in **UEFI** mode (Secure Boot off), finish first-boot setup, log in and run:

```sh
su -c silver-install
```

It shows the plan (disk, new partition, what it won't touch) and only proceeds when you type
`install`. Remove the stick and restart: the chooser appears.

### PC skips the chooser and goes straight to Windows

Some firmware (often HP and some Lenovo models) ignores the boot order. In Windows, open
**Command Prompt as administrator** and run:

```bat
bcdedit /set {bootmgr} path \EFI\SilverOS\silverboot.efi
```

Undo it with `bcdedit /set {bootmgr} path \EFI\Microsoft\Boot\bootmgfw.efi`.

### Uninstall

1. In Windows (admin Command Prompt): `mountvol S: /S`, then `rmdir /S /Q S:\EFI\SilverOS`,
   then `mountvol S: /D`.
2. Disk Management: delete the **SilverOS** partition, then extend C: into the free space.
3. Optionally, remove the leftover "SilverOS" entry in your firmware's boot menu.

### Recovery

Hold **Shift** while SilverOS starts loading (after you pick it in the chooser) to get a plain
text menu: SilverOS, SilverOS (verbose boot), Windows, Firmware setup.

## Security defaults

- Kernel: KASLR, lockdown (integrity), no modules, no `/dev/mem`, no kexec, no
  hibernation, no 32-bit syscall ABI, zeroed allocations and stacks, hardened usercopy,
  freelist hardening, panic on oops.
- Runtime: kptr/dmesg restricted, ptrace restricted (Yama 2), unprivileged BPF off,
  SysRq off, protected links/fifos, network anti-spoofing / no redirects.
- **No sign-in password.** The `silver` account has an empty password, so the greeter just asks you
  to press Enter, and the text consoles (tty2, serial) let `silver` in without one. That is a
  convenience trade: anyone at the keyboard gets the `silver` user (not root). Remote access is
  unaffected: SSH never accepts passwords. To turn the password back on, run `passwd` as
  `silver`; the greeter then asks for it.
- Root stays locked until you set the admin password, can't log in directly (empty `securetty`,
  refused by the greeter), and is reached with `su`. Passwords are SHA-512 crypt hashes.
- No plaintext network services. SSH accepts keys only and runs only when
  `authorized_keys` exists.
- `/proc` is mounted with `hidepid=2`.

## Roadmap

1. **Groundwork** ✅: toolchain, kernel, init, userland, bootable USB.
2. **Dual boot with Windows** ✅: installer, own boot chooser, graphical login, persistent install.
3. **Firewall**: build nftables (libmnl + libnftnl) from source, default-deny inbound.
4. **Silver Desktop**: our own framebuffer/DRM compositor in C. We draw the window
   manager, font rendering, cursor and widgets ourselves, pixel by pixel.
5. **Self-hosting toolchain**: build GCC/binutils for the target so SilverOS can rebuild
   itself, and drop the dependency on the host compiler.
6. **Secure Boot**: sign the bootloader and kernel with your own keys.
