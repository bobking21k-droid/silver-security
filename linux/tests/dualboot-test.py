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

class VM:
    def __init__(self, with_iso):
        self.mon = os.path.join(outdir, "mon.sock")
        if os.path.exists(self.mon): os.unlink(self.mon)
        args = ["qemu-system-x86_64", "-m", "1024", "-smp", "2", "-machine", "q35",
                "-drive", f"if=pflash,format=raw,readonly=on,file={CODE}",
                "-drive", f"if=pflash,format=raw,file={vars_fd}",
                "-drive", f"file={disk},format=raw,if=none,id=d0", "-device", "nvme,drive=d0,serial=WIN11",
                "-nic", "user,model=e1000", "-vga", "std", "-display", "none",
                "-serial", "stdio", "-monitor", f"unix:{self.mon},server,nowait"]
        if with_iso:
            args += ["-drive", f"file={iso},media=cdrom,readonly=on", "-boot", "order=d"]
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
vm.send("su -c poweroff"); vm.expect("Password:"); vm.send("Admin#Test12"); vm.p.wait(60); print("   powered off cleanly")

print("== 2. power on from disk: chooser must appear")
vm = VM(with_iso=False)
vm.expect("silverboot.efi", 60); time.sleep(4); vm.shot("chooser")
vm.key("ret"); time.sleep(0.9); vm.shot("melt"); time.sleep(3.2); vm.shot("checks")
print("== 3. SilverOS boots from disk: greeter intro, graphical login, admin password persisted")
vm.expect("login:", 120)
t0 = time.time()
for at, name in [(1.6, "intro-1"), (3.0, "intro-2"), (3.7, "intro-3"), (4.1, "intro-4"), (4.7, "intro-5"), (6.6, "intro-6"), (8.5, "login-card")]:
    time.sleep(max(0, t0 + at - time.time())); vm.shot(name)
time.sleep(max(0, t0 + 17 - time.time())); vm.shot("login-card")
vm.key("ret"); time.sleep(2); vm.key("ret"); time.sleep(3); vm.shot("session")   # Enter = signed in, no password
vm.send("silver"); vm.expect(r"~\$ ")                                   # text console: no password either
for c in ["cat /etc/silver-installed", "mount | grep ' / '", "echo persist-check > ~/note.txt; sync"]:
    vm.send(c); print(strip(vm.expect(r"~\$ ")))
vm.send("su -c reboot"); vm.expect("Password:"); vm.send("Admin#Test12")
print("== 4. reboot, choose Windows")
vm.expect("silverboot.efi", 60); time.sleep(4); vm.key("right"); time.sleep(1); vm.shot("chooser-windows-selected"); vm.key("ret")
vm.expect("test stand-in", 30); time.sleep(1); vm.shot("windows")
print("   Windows Boot Manager stand-in started")
vm.off()
print("PASS")
