#!/usr/bin/env python3
"""
End-to-end dual-boot test in QEMU (UEFI with persistent NVRAM):
  1. boot the live ISO next to a fake Windows disk, do first-boot setup, run silver-install
  2. power on without the ISO -> the SilverOS chooser must appear (screenshot)
  3. pick SilverOS -> installed system boots from disk, admin password persisted
  4. power on again, pick Windows -> Windows Boot Manager stand-in starts (screenshot)

Usage: tests/dualboot-test.py <iso> <disk.img> <ovmf_vars.fd> <outdir>
"""
import os, re, socket, struct, subprocess, sys, time, zlib

iso, disk, vars_fd, outdir = sys.argv[1:5]
os.makedirs(outdir, exist_ok=True)
CODE = "/usr/share/OVMF/OVMF_CODE_4M.fd"

# Wi-Fi: the installed system boots with two simulated radios (mac80211_hwsim); a small extra
# disk carries a test access point (hostapd, WPA2 "SilverTest") for the second one
here = os.path.dirname(os.path.abspath(__file__))
apimg = os.path.join(outdir, "testap.img")
subprocess.run(["rm", "-f", apimg]); subprocess.run(["truncate", "-s", "16M", apimg], check=True)
subprocess.run(["mkfs.vfat", "-n", "TESTAP", apimg], check=True, stdout=subprocess.DEVNULL)
for f in [os.path.join(here, "..", "work", "test", "hostapd")] + [os.path.join(here, "wifi-ap", n) for n in ("ap.sh", "hostapd.conf", "udhcpd.conf")]:
    subprocess.run(["mcopy", "-i", apimg, f, "::"], check=True)

class VM:
    runs = 0
    shots = {}
    def __init__(self, with_iso, testap=False):
        VM.runs += 1
        self.mon = os.path.join(outdir, "mon.sock")
        if os.path.exists(self.mon): os.unlink(self.mon)
        args = ["qemu-system-x86_64", "-m", "2048", "-smp", "2", "-machine", "q35", "-cpu", "max",
                "-drive", f"if=pflash,format=raw,readonly=on,file={CODE}",
                "-drive", f"if=pflash,format=raw,file={vars_fd}",
                "-drive", f"file={disk},format=raw,if=none,id=d0", "-device", "nvme,drive=d0,serial=WIN11",
                "-nic", "user,model=e1000", "-vga", "std", "-display", "none",
                "-serial", "stdio", "-monitor", f"unix:{self.mon},server,nowait"]
        if testap:
            args += ["-drive", f"file={apimg},format=raw,if=none,id=t0,readonly=on", "-device", "nvme,drive=t0,serial=TESTAP"]
        if with_iso:
            args += ["-drive", f"file={iso},media=cdrom,readonly=on", "-boot", "order=d"]
        # a sound card whose output is recorded, so the intro's sound can be checked
        args += ["-audiodev", f"wav,id=snd0,path={os.path.join(outdir, f'sound-{VM.runs}.wav')}",
                 "-device", "intel-hda", "-device", "hda-duplex,audiodev=snd0"]
        self.p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        os.set_blocking(self.p.stdout.fileno(), False)
        self.buf = b""; self.log = b""
        time.sleep(1)
        self.m = socket.socket(socket.AF_UNIX); self.m.connect(self.mon)

    def pump(self):
        try: d = self.p.stdout.read()
        except Exception: d = None
        if d: self.buf += d; self.log += d

    def expect(self, pat, t=90):
        end = time.time() + t
        while time.time() < end:
            self.pump()
            m = re.search(pat.encode(), self.buf)
            if m:
                out = self.buf[:m.end()]; self.buf = self.buf[m.end():]
                return out.decode(errors="replace")
            time.sleep(0.2)
        self.fail(f"timeout waiting for {pat!r}")

    def send(self, s): self.p.stdin.write(s.encode() + b"\r"); self.p.stdin.flush(); time.sleep(0.4)
    def monitor(self, cmd): self.m.send(cmd.encode() + b"\n"); time.sleep(0.6)
    def key(self, k): self.monitor(f"sendkey {k}")

    def shot(self, name):
        ppm = os.path.join(outdir, name + ".ppm")
        self.monitor(f"screendump {ppm}"); time.sleep(1.5)
        d = open(ppm, "rb").read().split(b"\n", 3); w, h = map(int, d[1].split()); px = d[3]
        raw = b"".join(b"\x00" + px[y*w*3:(y+1)*w*3] for y in range(h))
        c = lambda t, x: struct.pack(">I", len(x)) + t + x + struct.pack(">I", zlib.crc32(t + x))
        png = os.path.join(outdir, name + ".png")
        open(png, "wb").write(b"\x89PNG\r\n\x1a\n" + c(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                              + c(b"IDAT", zlib.compress(raw)) + c(b"IEND", b""))
        os.unlink(ppm); print("   screenshot:", png)
        VM.shots[name] = px

    def changed(self, a, b, minimum=0.04):
        """fail unless screenshot b differs from screenshot a: the key press must have done something"""
        pa, pb = VM.shots[a], VM.shots[b]
        n = min(len(pa), len(pb)); step = 97
        diff = sum(1 for i in range(0, n, step) if abs(pa[i] - pb[i]) > 24) / max(1, n // step)
        print(f"   {a} -> {b}: {diff:.0%} of the screen changed")
        if diff < minimum: self.fail(f"{b} looks the same as {a}: the key press did nothing")

    def off(self):
        try: self.monitor("quit")
        except OSError: pass          # already powered itself off
        self.p.wait(15)

    def fail(self, msg):
        print("FAIL:", msg); print(re.sub(rb"\x1b\[[0-9;=?]*[a-zA-Z]", b"", self.log[-3000:]).decode(errors="replace"))
        self.p.kill(); sys.exit(1)

def strip(s): return re.sub(r"\x1b\[[0-9;=?]*[a-zA-Z]", "", s).replace("\r", "")

print("== 1. live USB: first boot + silver-install")
vm = VM(with_iso=True)
vm.expect("Press Enter", 120); vm.send("")
vm.expect("New password:"); vm.send("Admin#Test12"); vm.expect("Retype"); vm.send("Admin#Test12")
vm.expect("login:"); vm.send("silver"); vm.expect(r"~\$ ")        # no password prompt
vm.send("su -c silver-install"); vm.expect("Password:"); vm.send("Admin#Test12")
plan = vm.expect("Type  install", 60); print(strip(plan))
vm.send("install")
done = vm.expect(r"Done\. Remove the USB stick|silver-install: .*", 300); print(strip(done))
if "Done" not in done: vm.fail("installer failed")
vm.expect(r"~\$ "); vm.send("su -c 'silver-efiboot list'"); vm.expect("Password:"); vm.send("Admin#Test12")
print(strip(vm.expect(r"~\$ ")))
# (test only) the installed system gets two simulated Wi-Fi radios
vm.send("su -c 'mkdir -p /run/e; mount -t vfat $(findfs LABEL=SYSTEM) /run/e && sed -i \"s/console=tty1\\$/console=tty1 mac80211_hwsim.radios=2/\" /run/e/EFI/SilverOS/grub.cfg; grep -c hwsim /run/e/EFI/SilverOS/grub.cfg; umount /run/e'"); vm.expect("Password:"); vm.send("Admin#Test12")
print(strip(vm.expect(r"~\$ ")))
vm.send("su -c poweroff"); vm.expect("Password:"); vm.send("Admin#Test12"); vm.p.wait(60); print("   powered off cleanly")

print("== 2. power on from disk: chooser must appear")
vm = VM(with_iso=False, testap=True)
vm.expect("silverboot.efi", 60); time.sleep(4); vm.shot("chooser")
vm.key("ret"); time.sleep(0.9); vm.shot("melt"); time.sleep(3.2); vm.shot("checks")
print("== 3. SilverOS boots from disk: emblem intro, four-pane home, sections, terminal as the user")
vm.expect("login:", 120)
t0 = time.time()
for at, name in [(2.0, "intro-1"), (6.0, "intro-2"), (9.0, "intro-3"), (12.0, "intro-4"), (15.0, "intro-5"), (22.0, "home")]:
    time.sleep(max(0, t0 + at - time.time())); vm.shot(name)
for _ in range(3): vm.key("esc"); time.sleep(2.5)                      # first-run welcome: Keyboard, Time, Network, then home
time.sleep(2); vm.shot("home")
for _ in range(3): vm.key("right"); time.sleep(0.5)       # the buttons are a row: Files is the 4th
time.sleep(1); vm.shot("home-files-selected")
vm.key("ret"); time.sleep(2.5); vm.shot("files")
vm.key("down"); time.sleep(1.5); vm.shot("files-2"); vm.key("esc"); time.sleep(1.5)
vm.key("left"); time.sleep(0.5); vm.key("left"); time.sleep(0.8); vm.key("ret"); time.sleep(2); vm.key("l"); time.sleep(0.5); vm.key("o"); time.sleep(0.5); vm.key("f"); time.sleep(0.5); vm.key("i"); time.sleep(0.5); vm.key("ret"); time.sleep(6); vm.shot("search")
vm.key("esc"); time.sleep(1.5); vm.key("left"); time.sleep(0.8); vm.key("ret"); time.sleep(2.5); vm.shot("settings")
for k in ["down"] * 8: vm.key(k); time.sleep(0.5)          # Sound effects is the 9th row
vm.key("ret"); time.sleep(1.5); vm.shot("settings-sound-off")                      # Sound effects off (also for the boot chooser)
vm.key("esc"); time.sleep(1.5); vm.key("right"); time.sleep(0.5); vm.key("right"); time.sleep(0.8); vm.key("ret"); time.sleep(2.5); vm.shot("apps")
vm.key("right"); time.sleep(0.5); vm.key("ret"); time.sleep(2); vm.shot("game-store")     # three stores
vm.key("ret"); time.sleep(1.5); vm.key("ret"); time.sleep(2); vm.key("down"); time.sleep(1); vm.shot("game-snake")         # play Snake
vm.key("esc"); time.sleep(1); vm.key("esc"); time.sleep(1); vm.key("right"); time.sleep(0.5); vm.key("ret"); time.sleep(8); vm.shot("game-steam")
vm.key("esc"); time.sleep(1); vm.key("esc"); time.sleep(1)
vm.key("right"); time.sleep(0.5); vm.key("ret"); time.sleep(3); vm.shot("me-and-my-pc")
vm.key("esc"); time.sleep(1); vm.key("left"); time.sleep(0.5); vm.key("left"); time.sleep(0.8)
vm.changed("home", "home-files-selected"); vm.changed("home-files-selected", "files"); vm.changed("files", "search")
vm.changed("settings", "settings-sound-off", 0.002); vm.changed("apps", "game-store"); vm.changed("game-store", "game-snake")
vm.changed("game-snake", "game-steam"); vm.changed("apps", "me-and-my-pc")
vm.key("ret"); time.sleep(5); vm.shot("terminal")                                  # Terminal: a shell on tty1, as the user
vm.send("silver"); vm.expect(r"~\$ ")                                              # text console: no password either
for c in ["mount | grep -E 'games|/usr ' ; ls -la /boot/games.sqfs; ls /usr/lib/firmware | head -3; ls /dev/dri", "od -An -tx1 /sys/firmware/efi/efivars/SilverMute-6c5c3c2e-6b0a-4f3c-9a7e-2f1d0c9b8a71", "ls ~/.config/silver", "cat /etc/silver-installed", "mount | grep ' / '", "echo persist-check > ~/note.txt; sync", "ps | grep -c '[s]ilver-greeter' || true", "id -un"]:
    vm.send(c); print(strip(vm.expect(r"~\$ ")))
print("== 3b. Wi-Fi: join the test access point from Settings > Network")
vm.send("su -c 'mkdir -p /run/ap; mount -o ro $(findfs LABEL=TESTAP) /run/ap && /run/ap/ap.sh'"); vm.expect("Password:"); vm.send("Admin#Test12")
print(strip(vm.expect(r"~\$ ", 30)))
for k in "exit": vm.key(k)
vm.key("ret"); time.sleep(6); vm.key("spc"); time.sleep(4); vm.shot("home-again")          # leave the on-screen terminal: back to the home screen (intro skipped)
vm.key("ret"); time.sleep(2); vm.key("ret"); time.sleep(10); vm.shot("network")            # Settings > Network: SilverTest listed
vm.key("ret"); time.sleep(0.3)
for ch in "silverpass1": vm.key(ch); time.sleep(0.15)
time.sleep(1); vm.shot("network-password")
vm.key("ret"); time.sleep(15); vm.shot("network-joined")
vm.changed("network", "network-password"); vm.changed("network-password", "network-joined")
vm.key("esc"); time.sleep(1); vm.key("esc"); time.sleep(1.5); vm.key("right"); time.sleep(0.5); vm.key("right"); time.sleep(0.8); vm.key("ret"); time.sleep(2); vm.key("ret"); time.sleep(5)   # Terminal again, for the reboot
for c in ["ip -4 addr show wlan0 | grep inet", "ping -c 1 -W 3 10.42.0.1 | grep -c 'bytes from'", "id"]:
    vm.send(c); out = strip(vm.expect(r"~\$ ")); print(out)
    if c.startswith("ip ") and "inet 10.42.0." not in out: vm.fail("Wi-Fi: no address from the test access point")
    if c.startswith("ip "): print("   Wi-Fi joined: got an address from the test access point")
vm.send("su -c 'grep -c SilverTest /etc/wpa_supplicant/wpa_supplicant.conf'"); vm.expect("Password:"); vm.send("Admin#Test12"); print(strip(vm.expect(r"~\$ ")))
vm.send("su -c reboot"); vm.expect("Password:"); vm.send("Admin#Test12")
print("== 4. 'Play in Windows': leave the one-time request, restart, Windows must start by itself")
# (what SilverOS's Settings > Windows does: the variable SilverNext = 2, read and deleted by the boot chooser)
vm.send("su -c 'mountpoint -q /sys/firmware/efi/efivars || mount -t efivarfs efivarfs /sys/firmware/efi/efivars; printf \"\\\\007\\\\000\\\\000\\\\000\\\\002\" > /sys/firmware/efi/efivars/SilverNext-6c5c3c2e-6b0a-4f3c-9a7e-2f1d0c9b8a71; od -An -tx1 /sys/firmware/efi/efivars/SilverNext-6c5c3c2e-6b0a-4f3c-9a7e-2f1d0c9b8a71; reboot'")
vm.expect("Password:"); vm.send("Admin#Test12")
print(strip(vm.expect(r"07 00 00 00 02", 30)))
vm.expect("silverboot.efi", 60)
vm.expect("test stand-in", 90); time.sleep(1); vm.shot("windows-direct")           # no key was pressed
print("   Windows started on its own")
vm.off()
print("== 4b. the request was used up: the chooser waits for a choice again")
vm = VM(with_iso=False)
vm.expect("silverboot.efi", 60); time.sleep(3); vm.shot("chooser-again")
if "test stand-in" in vm.log.decode(errors="replace").split("silverboot.efi")[-1]: vm.fail("the one-time Windows request was not deleted")
vm.key("right"); time.sleep(1); vm.shot("chooser-windows-selected"); vm.key("ret")
vm.expect("test stand-in", 30); time.sleep(1); vm.shot("windows")
print("   Windows Boot Manager stand-in started")
vm.off()
print("PASS")
