#!/usr/bin/env python3
"""Screenshot test in QEMU: F12 saves a PNG into ~/Pictures (checked over the serial console).
Usage: tests/screenshot-test.py <iso> <disk.img> <ovmf_vars.fd> <outdir>"""
import sys, os, time
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, "dualboot-test.py")).read(); src = src[:src.index('print("== 1.')]
__file__ = os.path.join(here, "dualboot-test.py"); exec(src)
vm = VM(with_iso=True)
vm.expect("Press Enter", 150); vm.send("")
vm.expect("New password:"); vm.send("Admin#Test12"); vm.expect("Retype"); vm.send("Admin#Test12")
vm.expect("login:", 60)
time.sleep(30)
vm.send("silver"); vm.expect(r"~\$ ")
vm.key("f12"); time.sleep(3); vm.shot("s1-toast")
vm.send("ls -l ~/Pictures; head -c 8 ~/Pictures/*.png | od -An -tx1; wc -c ~/Pictures/*.png")
out = strip(vm.expect(r"~\$ ")); print(out)
if "89 50 4e 47 0d 0a 1a 0a" not in out: vm.fail("no PNG was saved by F12")
vm.send("cp ~/Pictures/*.png /tmp/s.png; echo copied"); vm.expect(r"~\$ ")
vm.off()
print("screenshot: F12 saved a PNG; see s1-toast.png")
