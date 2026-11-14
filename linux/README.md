# SilverOS

A Linux system built from source, from nothing, where every part is chosen and owned:
the kernel config, the C library, the init system (written here, in C), the boot menu,
and soon the desktop down to the pixel.

> Status: **Phase 2, dual boot.** It boots live from a USB stick (BIOS + UEFI) and installs next
> to Windows with its own animated boot chooser and graphical login.

## Power-on experience

The look is stark and gritty: black, vivid red and white, thick messy lettering, tiny wide
captions, and a heartbeat that runs through everything. (It takes its mood from a game-intro
reference, not its content.)

1. **The chooser** (`src/silverboot`, our own UEFI program in C): the screen splits exactly in
   half along a torn seam that glows red and beats like a heart. On the left is SilverOS: a
   bright white room where a broken, flat-shaded 3D pencil turns slowly in the air among its
   splinters, and time crawls until you press a key or move the mouse. On the right is
   Windows, in full colour: sky, sun, clouds, hills and four glass panes. Shards of each world
   drift across the seam into the other, and every change of selection cuts with a brief red
   strobe. Use <- -> or the mouse, then press Enter or click; `1` and `2` jump straight to a
   system; `C` turns on calm mode (no strobing). It starts SilverOS after 8 s if untouched.
2. **Pick SilverOS:** a full-screen classic Windows boot scene appears, flips upside-down in
   grey, then melts, in pure black and white: columns drip at their own speeds, blocks slip
   sideways and flip to negative. A white flash, the four-pane logo flickers (with one red cut),
   and then the system check runs (CPU, GPU, RAM, STORAGE) with values read from the real
   hardware, each confirmed by a beating red dot.
3. **The greeter** (`src/greeter`): a red dot beats in the dark. A chrome pen rises up the
   middle, strains and snaps, and the screen **cracks**: fractures race out from the break,
   and two jagged ones split the glass into four panes. "SILVER OS" slams in where they cross,
   and the panes light up as **Settings, Search, Apps and Files**. The intro plays once per
   boot; any key skips it. Add `silver.calm` to the kernel command line (or turn on Calm mode
   in Settings) to turn off its shake and flashes.
4. **The home screen** (`src/greeter/home.h`): arrow keys choose a pane (the chosen one turns
   to white glass), Enter opens it, Esc comes back. Every section works:
   - **Files**: browse your home folder (Enter opens a folder, Backspace goes up) with a text
     preview of the selected file.
   - **Search**: Internet search. Type, Enter, pick a result (Tab switches between **Web** and **Videos**). Web results come from DuckDuckGo's plain-HTML page; Videos from YouTube. Enter on a video opens a watch page (title,
     channel, description, up-next list). **Playback is not built yet**, and the page is a
     placeholder for you to change. Results are read from those sites' public pages through
     `silver-fetch` (our own small HTTPS client on BearSSL, checking certificates against the
     bundled Mozilla roots). That scraping is unofficial, can break whenever YouTube changes its
     page, and is against those sites' terms for automated use. What you search for goes to DuckDuckGo or YouTube. There is no web browser, so a web result opens a page showing its title, snippet and address.
   - Behind Search a background loops: a drawn crowd marching over a dark-red swirl, then a
     video clip of your choice tinted black and red. Make it with
     `scripts/make-search-bg.sh <clip> <start> <end>` before building. The clip is kept in
     `work/` (not in git); only use footage you have the right to use. Without it, only the
     march plays.
   - **Files** also searches: press `/`, type part of a name (letters may be spread out) or
     switch with Tab to search inside text files; Enter shows the result in its folder.
   - **Apps**: a Terminal (a shell on the console; `exit` returns here), a live System Monitor
     (CPU graph, memory, uptime) and an About page.
   - **Settings**: network address, display size, Calm mode, About, Restart and Shut down
     (Enter twice to confirm).

   Signing in is passwordless by default; an account with a password gets a card over the
   cracked glass first. The home screen runs as the signed-in user, not root: the greeter forks,
   the child drops privileges, and the privileged parent only does what the child asks through
   a pipe (open the terminal, restart, shut down).
5. **Pick Windows:** the screen shatters like glass and Windows Boot Manager starts as normal.

Both the chooser and the intro contain flashing lights; they say so on screen.

### How it is drawn

Everything is software-rendered by one small header, `src/common/sgfx.h`, with no libraries:
anti-aliased shapes from signed distances, text from signed-distance-field fonts (smooth at any
size), dithered gradients, noise, blur, and a tiny 3D pipeline (project, flat-shade, depth-sort,
toon outline). The boot chooser and the login screen share it, and so does the live-USB menu
artwork. Screens above 2100 px wide are drawn at half size and shown doubled, to stay smooth.

`scripts/render-demo.sh` renders videos of the whole experience on the build machine with the
exact code that ships, so the look can be changed and reviewed without rebooting anything.

Fonts: **Silver Scrawl**, a thick, messy display face made for SilverOS by `src/art/mkfont.py
--messy` (it fattens Bowlby One and roughens its outline: wobble, bumps, grit, dry-brush bites,
splatter, and a different tilt, size and baseline for every letter); Michroma for the tiny wide
captions; Inter for readable sentences. All under the SIL Open Font License 1.1 (see
`src/common/fonts/LICENSES.md`). They are baked into `src/common/fonts/*.sdf` and committed, so
building needs neither the font files nor that script's dependencies.

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
| Renderer | **sgfx**: anti-aliased 2D/3D software renderer + SDF fonts, one header | `src/common/` |
| Boot chooser | **silverboot**: split-screen animated UEFI app, written from scratch | `src/silverboot/` |
| Login | **silver-greeter**: framebuffer intro + "press Enter" sign-in (asks for a password only if the account has one) | `src/greeter/` |
| Installer | **silver-install** (dual boot) + **silver-efiboot** (UEFI boot entries) | `rootfs-overlay/sbin/`, `src/tools/` |
| Disk tools | sfdisk (util-linux), mkfs.ext4/e2fsck (e2fsprogs), static | `scripts/build-userland.sh` |

Rebrand everything by editing `config.sh` (`OS_NAME`, `OS_HOSTNAME`, colours, ...).

## Build

On Ubuntu/Debian:

```sh
sudo apt-get install build-essential rsync bison flex bc libelf-dev libssl-dev cpio xz-utils \
     clang lld python3 xorriso grub-pc-bin grub-efi-amd64-bin mtools qemu-system-x86 ovmf fdisk dosfstools
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
4. **Silver Desktop**: windows. The home screen and its sections exist; next is a window
   manager and compositor so several apps can run at once.
5. **Self-hosting toolchain**: build GCC/binutils for the target so SilverOS can rebuild
   itself, and drop the dependency on the host compiler.
6. **Secure Boot**: sign the bootloader and kernel with your own keys.
