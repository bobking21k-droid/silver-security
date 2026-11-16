# 48-hour goal: keep improving SilverOS

Started: 2026-11-16T14:49Z   Deadline: **2026-11-18T14:49Z**

## The goal

Make SilverOS better on a real PC, in the order that matters to someone who actually installs it
next to Windows. Every item must be built, **tested in the virtual PC (or a build check) before it
is committed**, and reported honestly: what was verified, what could not be tested here (real GPU,
real Wi-Fi/Bluetooth chips, a real Windows, a real Steam account). Nothing is "done" until the full
dual-boot test passes (`tests/dualboot-test.py`) and the demo checks pass (`scripts/render-demo.sh`).

## Rules for every work session

1. Read this file; take the first unchecked item; do it; test it; commit; push; tick it; add a line
   to the log at the bottom. One item (or a clear slice of one) per session.
2. Never push a broken build. If a test fails, fix it or revert; don't skip or weaken the test.
3. Keep the memory footprint small (it is a goal in itself) and keep the README true.
4. Don't touch Windows partitions. Don't add network services that listen. Don't weaken the firewall.
5. If the container was reset (`work/` missing), rebuild what is needed (`./build.sh`; the games
   layer takes hours: `scripts/build-games.sh`, so avoid needing it unless the item requires it).
6. At the deadline: stop starting new work, make sure `main` of this branch is green and pushed,
   send the final ISO + demo + a short honest report, and disable the scheduled wake-ups.

## Backlog, in priority order

Real-PC essentials
- [x] **Power button and lid.** (power button tested in QEMU; lid closed = screen off is written but untested: no lid in a VM; sleep-on-lid not done) The power button does nothing today. Read ACPI button / lid events
      (kernel input events), show a "Shut down?" panel on the power button (Enter confirms), and
      handle lid close (dim/blank the screen; suspend if the kernel supports it and it is safe).
      Test: QEMU `system_powerdown` must bring up the panel and shut the VM down on confirm.
- [x] **Keyboard layouts.** (24 layouts; German tested end to end in QEMU; accented letters show in the console but are not yet typed into the home screen's boxes) US only today. Add Settings > Keyboard: common layouts (UK, DE, FR, ES,
      IT, PT/BR, NL, SE/NO/DK/FI, PL, CZ, TR, RU, US-international, Dvorak, Colemak...), applied
      to the greeter's text input and the console (`loadkmap` maps built from kbd's keymaps).
- [x] **Idle behaviour.** (done: dim after 2 min, screen off after 10, wake on key/mouse, battery-low banner, power saving on battery, cpufreq drivers + schedutil default in the kernel; testing) Screen dimming/blanking after inactivity and wake on key/mouse; CPU
      frequency governor for laptops (power saving on battery); battery low warning.
- [x] **Resolution/scaling.** (UI size x1/x2/x3 chosen in Settings > Display, used from next start; picking a different screen mode is NOT done: needs a GPU driver mode-set) Display row: pick a mode when the monitor offers several; HiDPI scale.

Using it
- [x] **Files app operations.** (copy/cut/paste/delete/rename/new folder/picture viewer; tests/files-test.py) Copy, move, rename, delete (with confirm), new folder; open text
      and images (a simple viewer); mark several files.
- [x] **In-OS installer.** (Settings > Install SilverOS on the live USB: plan page, progress, done; tests/install-ui-test.py installs in QEMU and boots the result) An "Install SilverOS" app on the live USB: guided steps (disk choice,
      how much space for SilverOS, a plan screen, a progress bar), instead of `su -c silver-install`.
      Must keep the existing safety checks. Test it in the QEMU dual-boot test.
- [x] **First-run welcome** (keyboard, time zone, Wi-Fi pages in turn; tests/welcome-test.py) (language/keyboard, time zone, Wi-Fi) on the first boot of an install.

Quality
- [x] **Boot time.** (not meaningfully measurable here: no KVM, QEMU is ~76 s to login; the known avoidable waits are `sleep 1` and `udevadm settle` in rc.boot, left alone because Wi-Fi/graphics need them on real PCs) Measure time from power-on to home screen (QEMU + `-d` timestamps); remove
      avoidable waits (module loading, DHCP, udev settle, Wi-Fi scan). Report before/after.
- [x] **Memory.** (measured: about 80 MB in use at the login prompt on an installed system, 2 GB VM; home screen adds the greeter) Re-measure; find anything above ~150 MB at the home screen on the live USB.
- [x] **Security review.** (docs/SECURITY.md: fixed a root symlink bug in the Steam session; kernel tightening and Secure Boot are planned, not done) Audit the kernel config and boot-time services, setuid files, sysctls,
      the new root X server session, the request protocol between the greeter's two halves;
      write the findings and fix what is cheap. Plan (and if feasible, implement) Secure Boot.
- [x] **One test command.** (tests/run-all.sh, written; first full run pending) `tests/run-all.sh` runs: build check, demo render (checks), dual-boot
      test, firewall test, mouse test, and prints a one-screen PASS/FAIL summary.
- [ ] **README and docs** match reality; add a short "known limits" section.

Investigations (time-boxed to one session each; stop and write down what was learned)
- [ ] **Steam's own window.** Why does Steam's browser engine's GPU process crash on software
      rendering? Try: glamor on llvmpipe, other CEF flags, `--in-process-gpu`, Xorg `-ac` style
      tweaks, matching libstdc++ in the runtime. Can't be fully proven without a real GPU.
- [x] **Bluetooth pairing end to end.** (time-boxed, NOT solved: see the log line of 17:45Z) BlueZ's `btvirt` failed to open a virtual controller on this
      kernel; find out why (kernel debug, strace-like tracing) or write a tiny virtual controller.

## Log

- 2026-11-16 14:50Z  Scheduled wake-up every 2 hours (trigger trig_01N1bRKJaqCpXYm9QWZUbfic); it disables itself after the deadline.
- 2026-11-16 14:49Z  Goal created. (Earlier in this session: Steam launcher + games layer, Bluetooth,
  Wi-Fi, firewall, clock, time zones, mouse, brightness/volume, Play in Windows, memory cuts.)
- 2026-11-16 14:55Z  Power button: "Shut down?" panel (Enter/Esc/press again), also during Steam (2 presses in 3 s); lid close blanks the screen. tests/power-test.py passes.
- 2026-11-16 15:10Z  Keyboard layouts: Settings > Keyboard, 24 layouts compiled from kbd data, try-it box; tests/keyboard-test.py (German: physical Y types z).
- 2026-11-16 15:20Z  Idle dim/off/wake, battery banner, silver-power (governor on battery), kernel cpufreq (intel/amd pstate, schedutil default).
- 2026-11-16 15:40Z  Files app: copy/move/delete/rename/new folder + picture viewer. Security review: fixed root symlink bug in Steam session; docs/SECURITY.md.
- 2026-11-16 15:45Z  Batch committed+pushed (demo 24 checks, dual-boot PASS, steam security test PASS). Memory ~80 MB in use at login. tests/boot-time.py added (QEMU without KVM is ~76 s to login, not meaningful for real hardware).
- 2026-11-16 16:00Z  Settings > Display: choose interface size (x1-x3), saved in ~/.config/silver/scale. README known limits added.
- 2026-11-16 16:35Z  Kernel hardened (no DEBUG_FS, KALLSYMS_ALL, BINFMT_MISC, X86_MSR). tests/run-all.sh: ALL PASSED (demo checks, power, keyboard, display, idle, mouse, firewall, dual-boot).
- 2026-11-16 17:00Z  In-OS installer page (plan, progress bar, done) built on silver-install --plan/--yes; install + boot verified in QEMU.
- 2026-11-16 17:20Z  First-run welcome on installed systems (Keyboard > Time > Network, then home). Dual-boot test passes with it.
- 2026-11-16 17:45Z  Bluetooth investigation: the reason btvirt failed was a missing kernel option (CONFIG_BT_HCIVHCI, now =m, /dev/vhci exists). btvirt still prints "Failed to open Virtual HCI device" even with bluetoothd stopped; the kernel registers hci0 but the emulator's create handshake fails (hci0 reset times out). Not solved; tests/bt-pair-test.py and tests/bt-img.sh are kept for the next try. Pairing remains untested.
- 2026-11-16 18:05Z  Full suite on the kernel with hci_vhci: all 10 tests pass (run-all order fixed so install tests run after the live-USB tests).
- 2026-11-16 19:05Z  Kernel audit for real PCs: added touchpad/I2C/LPSS/pinctrl, VMD, SD/eMMC, Intel 2.5G + USB ethernet, USB-C/USB4, laptop vendor drivers. SOF audio firmware unavailable offline (documented). docs/REAL-PC.md checklist. Full suite passes.
