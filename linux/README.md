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
2. **Pick SilverOS:** a fake modern Windows boot appears (black, the blue four-pane logo, the
   ring of dots chasing round), then flips upside-down in grey and melts, in pure black and white: columns drip at their own speeds, blocks slip
   sideways and flip to negative. A white flash, the four-pane logo flickers (with one red cut),
   and then the system check runs (CPU, GPU, RAM, STORAGE) with values read from the real
   hardware, each confirmed by a beating red dot.
3. **The greeter** (`src/greeter`): a red dot beats in the dark. A white hexagonal pencil rises
   up the middle, bows and snaps (time slows for a beat, splinters fly), and the screen
   **cracks**: fractures race out from the break, and two jagged ones split the glass into four
   panes that light up as **Settings, Search, Apps and Files**. The intro plays once per boot;
   any key skips it. Add `silver.calm` to the kernel command line (or turn on Calm mode in
   Settings) to turn off its shake and flashes.
   **Sound:** everything has sound effects, all made by code at boot (no sound files):
   - the chooser: a low breathing drone, a whoosh and click (or a soft chime) as you move
     between the sides, a tick each second of the countdown, a deep thump on Enter;
   - the fake Windows: a soft start-up bloom (an original chime), a tape stop as it flips, a
     sinking, crackling drone as it melts, electric zaps on the flashes; the system check
     ticks, rattles and beeps "OK" over a heartbeat; picking Windows sounds like glass breaking;
   - the intro: a heartbeat, a rising whoosh, wood creaking, the snap with a deep boom, glass
     shattering, cracks ticking and a soft chord as the panes light up;
   - the home screen and every section: clicks as you move, whooshes as you open and go back,
     key taps as you type, a two-note chime when search results arrive, a low buzz on errors.

   The chooser runs before any operating system, so silverboot has its own small Intel HD Audio
   driver (`src/silverboot/hda.h`); it resets the controller before Linux or Windows starts.
   In Linux the greeter plays through the kernel's ALSA interface directly
   (`src/greeter/sound.h`, sounds in `src/common/sfx.h`). It needs Intel HD Audio with a
   common codec (Realtek, Conexant, Analog, Cirrus, HDMI) or, in Linux only, USB audio.
   Calm mode plays the big moments quieter and without the booms.
4. **The home screen** (`src/greeter/home.h`): arrow keys choose a pane (the chosen one turns
   to white glass), Enter opens it, Esc comes back. Every section works:
   - **Files**: browse your home folder (Enter opens a folder, Backspace goes up) with a text
     preview of the selected file.
   - **Search** works like Google's front page: a big search bar, and under it **your recent
     searches** (kept in `~/.config/silver/search-history`, Backspace on one forgets it) and
     **what's trending today** (Google Trends' public feed). As you type, Google's suggestions
     appear under the bar (the part you typed dim, the rest bright); arrows pick one, Enter
     searches. Tab switches between **Web** and **Videos**. Web results come from DuckDuckGo's
     plain-HTML page; Videos from YouTube, with thumbnails. Enter on a video opens a watch page
     (title, channel, description, up-next list); **playback is not built yet**. D on a web
     result downloads the file to `~/Downloads`. Everything is fetched by `silver-fetch` (our
     own small HTTPS client on BearSSL, checking certificates against the bundled Mozilla
     roots). The result scraping is unofficial, can break whenever those sites change their
     pages, and is against their terms for automated use. What you type goes to Google (for
     suggestions) and to DuckDuckGo or YouTube (for results). There is no web browser, so a web
     result opens a page showing its title, snippet and address.
   - Behind Search a video clip of your choice loops, tinted black and red. Make it with
     `scripts/make-search-bg.sh <clip> <start> <end> [<start> <end> ...]` before building. The
     clip is kept in `work/` (not in git); only use footage you have the right to use. Without
     it, a turning dark-red swirl plays instead.
   - **Files** also searches: press `/`, type part of a name (letters may be spread out) or
     switch with Tab to search inside text files; Enter shows the result in its folder.
   - **Settings > Network** joins Wi-Fi: the networks nearby (signal, padlock, 2.4/5/6 GHz,
     which ones are saved), Enter to join, a password box (Tab shows what you typed), F forgets
     a saved network, R scans again. Joined networks are saved and joined by themselves at the
     next boot; the status line shows the network, your address, and a cable if one is plugged
     in. Behind it, `/sbin/silver-wifi` runs **wpa_supplicant 2.11** (built from source, with
     libnl) on every wireless adapter, including USB ones plugged in later, and fetches an
     address with DHCP when it connects. Supported adapters (drivers and firmware in the image):
     Intel Wi-Fi (7265 up to Wi-Fi 7 BE200), Realtek (rtw88/rtw89), MediaTek MT7921/MT7922/MT7925,
     Qualcomm Atheros (ath9k, ath10k QCA6174, ath11k WCN6855, ath12k WCN7850). Not yet: WPA3-only
     networks (WPA2 and WPA2/WPA3 mixed ones work), work/school sign-in networks (802.1X), and
     Broadcom chips. The firmware is Intel's, Realtek's, MediaTek's and Qualcomm's own (kept
     under their licences in `/lib/firmware`, listed in `firmware/wifi.list`).
   - **Settings > Bluetooth** pairs headphones, speakers, game controllers (Xbox, PlayStation
     DualSense, Switch Pro, Steam Controller), keyboards and mice: devices nearby, battery
     level, Enter pairs / connects / disconnects, F forgets, R looks again. BlueZ 5.79 does the
     work (built from source in the games layer, with PulseAudio's Bluetooth audio); the
     adapter's firmware (Intel, Realtek, MediaTek, Qualcomm, Broadcom USB) is in the image.
     Not done: Bluetooth audio in the home screen's own sounds (games and Steam use it).
     Tested against real BlueZ only for the "no adapter" state (a virtual machine has no
     Bluetooth hardware, and BlueZ's virtual controller would not run on this kernel); pairing
     itself is untested, and the page parses `bluetoothctl`'s documented output.
   - **Apps**: a grid of apps: Terminal (a shell on the console; `exit` returns here), the
     **Game Store**, **Me and my PC**, a live System Monitor and About.
     - **Game Store** (`src/greeter/gstore.h`): opens on three panels side by side, picked with
       the arrows: **SilverOS** (games that play right here: Snake, 2048 and Shatter, a brick
       breaker made of glass, written for SilverOS in `games.h`, best scores kept), **Steam**
       (top sellers and new releases, or search, from Steam's public store API, with art) and **Microsoft Store** (its catalogue and search, from the public
       storefront API), with **prices, discounts and deals**. **Steam runs right here**:
       on a Steam game's page, Enter **installs** it (or **plays** it once installed) and **B**
       buys it: SilverOS hands the screen to the real Steam app (Big Picture), as your user, and
       brings the home screen back when you quit Steam. No restart. The first time, SilverOS
       fetches Steam's launcher from Valve (about 20 MB; Steam updates itself after that) and
       Steam asks you to sign in to your own Steam account: SilverOS never sees your password
       or card. Windows games run through Steam's Proton (Valve's, downloaded by Steam). See
       "The games layer" below. **Microsoft Store games cannot run outside Windows** (their
       copy protection needs it), so that tab is look-only, with a code to get them in Windows.
       A wishlist (W) is kept in `~/.config/silver/wishlist`.
       A little figure dances along the bottom, from a clip you
       supply: `scripts/make-dancer.py <clip> <start> <end> <x> <y> <w> <h>` (kept in `work/`,
       not in git).
     - **Me and my PC**: you, and the computer: maker and model, processor (cores, threads,
       speed, how busy), memory, storage, graphics and display, network, battery, temperature,
       kernel, uptime, UEFI/BIOS and Secure Boot. All read live from `/proc` and `/sys`.
   - **Downloads**: on a web result, D downloads it into your Downloads folder (never
     overwriting: a second copy is saved as `name (2).ext`). Big files are fine.
   - **Settings**: network address, display size, Calm mode, **Sound effects** (on/off: also
     silences the boot chooser, through a small firmware variable), About, Restart and Shut down
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
| Kernel | Linux 6.18 LTS, hardened; only Wi-Fi, Bluetooth and graphics drivers are modules, and every module must be signed with this build's own key | `kernel/silver.config` |
| C library | musl, everything statically linked | `scripts/build-musl.sh` |
| PID 1 | **silver-init**, written from scratch: mounts, sysctl, service supervision, shutdown | `src/init/init.c` |
| Userland | BusyBox (shell + core utils; telnet/ftp/http daemons removed) | `scripts/build-userland.sh` |
| Remote access | Dropbear SSH: key-only, no root, off unless a key exists | `rootfs-overlay/etc/rc.boot` |
| Config & branding | users, sysctl hardening, issue/motd, os-release | `rootfs-overlay/`, `config.sh` |
| Renderer | **sgfx**: anti-aliased 2D/3D software renderer + SDF fonts, one header | `src/common/` |
| Boot chooser | **silverboot**: split-screen animated UEFI app, written from scratch | `src/silverboot/` |
| Login | **silver-greeter**: framebuffer intro + "press Enter" sign-in (asks for a password only if the account has one) | `src/greeter/` |
| Installer | **silver-install** (dual boot) + **silver-efiboot** (UEFI boot entries) | `rootfs-overlay/sbin/`, `src/tools/` |
| Wi-Fi | wpa_supplicant 2.11 + libnl 3.11, static; firmware from linux-firmware; regulatory database | `scripts/build-userland.sh`, `firmware/wifi.list` |
| Games layer | glibc, Mesa (AMD radeonsi + RADV, Intel iris/ANV, Nvidia nouveau, virtual and software GPUs), Vulkan, Xorg, PulseAudio, D-Bus, BlueZ, 32-bit libraries; built from source with Buildroot, packed as `games.sqfs`, mounted under `/usr` | `buildroot/`, `scripts/build-games.sh`, `games-overlay/` |
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

`scripts/render-demo.sh` also checks the home screen: its scripted walk through every screen
must land where it should at each step, or it fails. `tests/dualboot-test.py` runs the whole dual-boot story in QEMU against a fake Windows 11
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

## Memory

SilverOS keeps almost nothing in RAM. On the live USB the in-memory system is **7 MB** (the
programs and scripts). The drivers (kernel modules), the Wi-Fi / Bluetooth / graphics firmware,
the Search background clip and the dancer all live in `games.sqfs` on the stick or disk and are
read only when used (the clip and dancer are memory-mapped, so their pages can be dropped).
Measured in a virtual PC at the home screen: the whole system uses about **140 MB**, of which
the home-screen program is about 22 MB (at 1280x800; screen buffers grow with the resolution).
It frees what it doesn't need: the privileged parent hands its screen buffers back while the
home screen runs, two background buffers are dropped once the cracked glass is baked, and the
frosted-glass cache is capped at 1.5 screens. An installed SilverOS runs from its own disk.
Steam is the exception: on the live USB its files (1 GB of its own, then your games) would sit
in RAM, so the Game Store tells you to install SilverOS first.

## The games layer

SilverOS itself is small and static (musl). Steam and Windows games need the opposite: glibc,
graphics drivers, a display server. So those live in a second layer, `games.sqfs` (about 280 MB
compressed, with the GPU and Bluetooth firmware), built from source with Buildroot
(`scripts/build-games.sh`, 64-bit and 32-bit halves; plan on a few hours on 4 cores and 30 GB of
disk) and mounted read-only on top of `/usr` at boot. Without it SilverOS works as before and
the Game Store says Steam isn't available.

What works and what is untested, plainly:

- Verified in the virtual PC: the layer mounts, drivers load, Xorg starts with glamor and DRI3,
  OpenGL 4.5 and the Vulkan loader work (software rendering there), Steam's launcher unpacks
  and runs, Steam downloads and installs its own client and runtime (about 1 GB; it updates
  itself after that), and the home screen comes back when Steam exits.
- **Not working in my tests, and unresolved:** Steam's own window. In the virtual PC (no real
  GPU, so everything is drawn by the CPU) Steam's browser engine starts but its graphics
  process crashes, so no Steam window appears. SilverOS then starts Steam with its
  software-rendering flags and hides software Vulkan from it, which did not fix it there. A
  real AMD or Intel GPU takes a different path (glamor, hardware OpenGL) that I could not test.
  Extra Steam flags can go on one line in `~/.config/silver/steam-args`.
- **Not verified, because it needs a real GPU and a Steam account:** sign-in, installing a game,
  Proton, and speed on real graphics hardware. Expect rough edges; the session log is
  `~/.local/state/silver/steam-session.log`, Xorg's is `Xorg.log` beside it, and Steam's own
  logs are in `~/.local/share/Steam/logs/`.
- **Graphics cards:** AMD (Radeon HD 7000 and newer) and Intel (Ivy Bridge and newer) are the
  good case. **Nvidia uses the open nouveau driver**: it works for the desktop, but is slow for
  games on most cards, and Vulkan games (Proton) need NVK, which this build does not include.
  Nvidia's own driver can't be included in an all-from-source system. Plan for AMD or Intel.
- **Needs a PC from about 2009 on** (the layer is built for x86-64-v2), and **at least 4 GB of
  RAM on the live USB**, which keeps its system in memory. An installed SilverOS boots from its
  own partition. Steam games are big: shrink C: in Windows to leave 60 GB or more.
- **Microsoft Store games and games with anti-cheat that needs Windows cannot run here.**
- Security trade-offs this adds: the X server runs as root (there is no login manager to hand it
  the screen and keyboard; it listens only on its local socket and only for your user's cookie),
  the kernel's 32-bit program support is on (Steam's client is 32-bit), and unprivileged user
  namespaces are on (Steam's game sandbox needs them).

## Security defaults

- Kernel: KASLR, lockdown (integrity), signed-only modules (only the Wi-Fi, Bluetooth and
  graphics drivers are modules), no `/dev/mem`, no kexec, no
  hibernation, 32-bit programs allowed only because Steam needs them, zeroed allocations and stacks, hardened usercopy,
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
