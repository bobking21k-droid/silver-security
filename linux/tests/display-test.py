#!/usr/bin/env python3
"""Display size test in QEMU: Settings > Display, Enter changes the interface size ("size x2 after restart").
Usage: tests/display-test.py <iso> <disk.img> <ovmf_vars.fd> <outdir>"""
import sys, os, time
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, "dualboot-test.py")).read(); src = src[:src.index('print("== 1.')]
__file__ = os.path.join(here, "dualboot-test.py"); exec(src)
vm = VM(with_iso=True)
vm.expect("Press Enter", 150); vm.send("")
vm.expect("New password:"); vm.send("Admin#Test12"); vm.expect("Retype"); vm.send("Admin#Test12")
vm.expect("login:", 60)
time.sleep(30)
vm.key("ret"); time.sleep(2)
vm.key("down"); vm.key("down"); time.sleep(1); vm.shot("d0-display")
vm.key("ret"); time.sleep(1.5); vm.shot("d1-changed")
vm.changed("d0-display", "d1-changed", 0.0001)
vm.off()
print("display: the Display row shows the new size; see d0-display.png and d1-changed.png")
