#!/usr/bin/env python3
"""Install from the home screen: on the live USB, Settings > Install SilverOS shows the plan (disk, space,
Windows), Enter installs, a progress bar runs, "Done" appears. Then the disk is started and must boot
SilverOS (the chooser and a login prompt).
Usage: tests/install-ui-test.py <iso> <disk.img> <ovmf_vars.fd> <outdir>"""
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
for _ in range(10): vm.key("down"); time.sleep(0.3)            # to the Install row (where Windows is)
vm.shot("u0-row")
vm.key("ret"); time.sleep(12); vm.shot("u1-plan")
vm.key("ret"); time.sleep(8); vm.shot("u2-working")
time.sleep(240)                                               # copying SilverOS takes a few minutes in the virtual PC
vm.shot("u3-late"); time.sleep(60); vm.shot("u4-done")
vm.changed("u2-working", "u4-done", 0.01)
vm.off()
print("== reboot from disk: SilverOS must boot")
vm = VM(with_iso=False)
vm.expect("silverboot.efi", 60)
vm.expect("login:", 150); print("   installed system reached the login prompt")
vm.off()
print("install-ui: see u1-plan.png, u2-working.png, u4-done.png")
