#!/usr/bin/env python3
"""Idle test in QEMU: with the idle times set to 6 and 14 seconds, the screen dims, then goes dark, and a key
press wakes it (the waking key does nothing else).
Usage: tests/idle-test.py <iso> <disk.img> <ovmf_vars.fd> <outdir>"""
import sys, os, time
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, "dualboot-test.py")).read(); src = src[:src.index('print("== 1.')]
__file__ = os.path.join(here, "dualboot-test.py"); exec(src)
vm = VM(with_iso=True)
vm.expect("Press Enter", 150); vm.send("")
vm.expect("New password:"); vm.send("Admin#Test12"); vm.expect("Retype"); vm.send("Admin#Test12")
vm.expect("login:", 60)
time.sleep(30)
vm.shot("i0-awake")
vm.send("silver"); vm.expect(r"~\$ ")
vm.send("mkdir -p ~/.config/silver; echo '6 14' > ~/.config/silver/idle; echo set"); vm.expect(r"~\$ ")
time.sleep(7)                              # the new times are read within 5 s
vm.key("left"); time.sleep(0.5)            # (touch the keyboard: the idle clock starts from here)
time.sleep(9); vm.shot("i1-dim")           # 6 s idle: dimmed
time.sleep(7); vm.shot("i2-off")           # 14 s idle: dark
vm.key("ret"); time.sleep(3); vm.shot("i3-woken")   # woken; the Enter key must NOT have opened Settings
vm.changed("i0-awake", "i1-dim", 0.05)
vm.changed("i1-dim", "i2-off", 0.01)
vm.changed("i2-off", "i3-woken", 0.05)
print("idle: dimmed, went dark, woke on a key")
vm.off()
