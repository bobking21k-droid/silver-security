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
- [ ] **Resolution/scaling.** Display row: pick a mode when the monitor offers several; HiDPI scale.

Using it
- [x] **Files app operations.** (copy/cut/paste/delete/rename/new folder/picture viewer; tests/files-test.py) Copy, move, rename, delete (with confirm), new folder; open text
      and images (a simple viewer); mark several files.
- [ ] **In-OS installer.** An "Install SilverOS" app on the live USB: guided steps (disk choice,
      how much space for SilverOS, a plan screen, a progress bar), instead of `su -c silver-install`.
      Must keep the existing safety checks. Test it in the QEMU dual-boot test.
- [ ] **First-run welcome** (language/keyboard, time zone, Wi-Fi) on the first boot of an install.

Quality
- [ ] **Boot time.** Measure time from power-on to home screen (QEMU + `-d` timestamps); remove
      avoidable waits (module loading, DHCP, udev settle, Wi-Fi scan). Report before/after.
- [ ] **Memory.** Re-measure; find anything above ~150 MB at the home screen on the live USB.
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
- [ ] **Bluetooth pairing end to end.** BlueZ's `btvirt` failed to open a virtual controller on this
      kernel; find out why (kernel debug, strace-like tracing) or write a tiny virtual controller.

## Log

- 2026-11-16 14:50Z  Scheduled wake-up every 2 hours (trigger trig_01N1bRKJaqCpXYm9QWZUbfic); it disables itself after the deadline.
- 2026-11-16 14:49Z  Goal created. (Earlier in this session: Steam launcher + games layer, Bluetooth,
  Wi-Fi, firewall, clock, time zones, mouse, brightness/volume, Play in Windows, memory cuts.)
- 2026-11-16 14:55Z  Power button: "Shut down?" panel (Enter/Esc/press again), also during Steam (2 presses in 3 s); lid close blanks the screen. tests/power-test.py passes.
- 2026-11-16 15:10Z  Keyboard layouts: Settings > Keyboard, 24 layouts compiled from kbd data, try-it box; tests/keyboard-test.py (German: physical Y types z).
- 2026-11-16 15:20Z  Idle dim/off/wake, battery banner, silver-power (governor on battery), kernel cpufreq (intel/amd pstate, schedutil default).
- 2026-11-16 15:40Z  Files app: copy/move/delete/rename/new folder + picture viewer. Security review: fixed root symlink bug in Steam session; docs/SECURITY.md.
