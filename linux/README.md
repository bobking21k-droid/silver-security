# SilverOS

A Linux system built from source, from nothing, where every part is chosen and owned:
the kernel config, the C library, the init system (written here, in C), the boot menu,
and soon the desktop down to the pixel.

> Status: **Phase 1, the groundwork.** It's a live system that boots from a USB stick on
> real BIOS and UEFI PCs. Installing it for dual boot is Phase 2.

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

Verified in QEMU on both BIOS and UEFI: it boots to the login prompt, runs first-boot
password setup, refuses direct root login, gets DHCP networking, and `su` works. Kernel
lockdown reports `[integrity]`. Runtime sysctls and `hidepid` are applied, and `/dev/mem`
is absent. Size: 26 MB ISO, 0.8 MB root filesystem.

Note: the live USB keeps everything in RAM, so the passwords you set are lost at
reboot. The Phase 2 install makes them persistent.

## Put it on a USB stick

```sh
sudo dd if=work/out/silveros-0.1.0-x86_64.iso of=/dev/sdX bs=4M status=progress oflag=sync
```

Then boot the PC from USB. You may need to turn off Secure Boot, because this image isn't
signed yet. On the first boot, press Enter on any console (screen or serial) to set passwords for
`silver` and `root`.

## Security defaults

- Kernel: KASLR, lockdown (integrity), no modules, no `/dev/mem`, no kexec, no
  hibernation, no 32-bit syscall ABI, zeroed allocations and stacks, hardened usercopy,
  freelist hardening, panic on oops.
- Runtime: kptr/dmesg restricted, ptrace restricted (Yama 2), unprivileged BPF off,
  SysRq off, protected links/fifos, network anti-spoofing / no redirects.
- Accounts start locked. Root can't log in directly (empty `securetty`), so you use `su`.
- No plaintext network services. SSH accepts keys only and runs only when
  `authorized_keys` exists.
- `/proc` is mounted with `hidepid=2`.

## Roadmap

1. **Groundwork** ✅: toolchain, kernel, init, userland, bootable USB.
2. **Dual-boot install.** An installer (`silver-install`) that:
   - shrinks nothing on its own. It installs into free space or a partition you pick,
     and shows a clear plan before it writes anything.
   - adds a SilverOS root partition (ext4) and reuses the existing EFI System Partition.
   - installs GRUB with a menu entry for SilverOS **and** one for the other OS
     (Windows Boot Manager chainload / other Linux via os-prober-style detection).
   - makes `/etc`, `/home` and `/var` persistent (live USB keeps them in RAM today).
3. **Firewall**: build nftables (libmnl + libnftnl) from source, default-deny inbound.
4. **Silver Desktop**: our own framebuffer/DRM compositor in C. We draw the window
   manager, font rendering, cursor and widgets ourselves, pixel by pixel.
5. **Self-hosting toolchain**: build GCC/binutils for the target so SilverOS can rebuild
   itself, and drop the dependency on the host compiler.
6. **Secure Boot**: sign the bootloader and kernel with your own keys.
