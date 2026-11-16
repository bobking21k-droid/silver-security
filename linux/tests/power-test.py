#!/usr/bin/env python3
"""Power button test in QEMU: an ACPI power-button press shows the 'Shut down?' panel; Esc cancels it;
a second press shows it again and Enter shuts the machine down.
Usage: tests/power-test.py <iso> <disk.img> <ovmf_vars.fd> <outdir>"""
import sys, os, time
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, "dualboot-test.py")).read(); src = src[:src.index('print("== 1.')]
__file__ = os.path.join(here, "dualboot-test.py"); exec(src)
vm = VM(with_iso=True)
vm.expect("Press Enter", 150); vm.send("")
vm.expect("New password:"); vm.send("Admin#Test12"); vm.expect("Retype"); vm.send("Admin#Test12")
vm.expect("login:", 60)
time.sleep(30)                                   # the intro finishes, the home screen settles
vm.shot("p0-home")
vm.monitor("system_powerdown"); time.sleep(3); vm.shot("p1-panel")
vm.changed("p0-home", "p1-panel", 0.05)
vm.key("esc"); time.sleep(2); vm.shot("p2-cancelled")
vm.changed("p1-panel", "p2-cancelled", 0.05)
vm.monitor("system_powerdown"); time.sleep(3); vm.shot("p3-panel-again")
vm.key("ret")
for i in range(60):
    if vm.p.poll() is not None: break
    time.sleep(1)
if vm.p.poll() is None: vm.fail("the machine did not shut down after Enter on the power panel")
print("power button: panel shown, Esc cancelled, Enter shut the machine down")
