#!/usr/bin/env python3
"""Keyboard layout test in QEMU: choose German in Settings > Keyboard, press the physical Y key and
check the try-it box shows z (QWERTZ), proving the layout reached the kernel.
Usage: tests/keyboard-test.py <iso> <disk.img> <ovmf_vars.fd> <outdir>"""
import sys, os, time
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, "dualboot-test.py")).read(); src = src[:src.index('print("== 1.')]
__file__ = os.path.join(here, "dualboot-test.py"); exec(src)
vm = VM(with_iso=True)
vm.expect("Press Enter", 150); vm.send("")
vm.expect("New password:"); vm.send("Admin#Test12"); vm.expect("Retype"); vm.send("Admin#Test12")
vm.expect("login:", 60)
time.sleep(30)
vm.key("ret"); time.sleep(2)                                   # Settings
for _ in range(6): vm.key("down"); time.sleep(0.4)             # to Keyboard
vm.key("ret"); time.sleep(2); vm.shot("k0-page")
vm.key("y"); time.sleep(1); vm.shot("k1-us-y")                 # US layout: y types y
for _ in range(5): vm.key("down"); time.sleep(0.4)             # to German
vm.key("ret"); time.sleep(3)                                   # use it (loads the keymap)
vm.key("y"); time.sleep(1); vm.shot("k2-de-y")                 # German: the Y key types z
vm.changed("k1-us-y", "k2-de-y", 0.003)
vm.off()
print("keyboard: layout chosen; see k1-us-y.png (y) and k2-de-y.png (z)")
