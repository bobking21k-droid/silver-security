#!/usr/bin/env python3
"""First-run welcome on an installed system: after the intro, Keyboard, Time and Network pages come one after the
other (Esc moves on), then the home screen; the second start goes straight to the home screen.
Needs a disk with SilverOS installed (tests/install-ui-test.py leaves one).
Usage: tests/welcome-test.py <iso> <disk.img> <ovmf_vars.fd> <outdir>"""
import sys, os, time
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, "dualboot-test.py")).read(); src = src[:src.index('print("== 1.')]
__file__ = os.path.join(here, "dualboot-test.py"); exec(src)
vm = VM(with_iso=False)
vm.expect("login:", 150); time.sleep(25); vm.shot("w1-keyboard")
vm.key("esc"); time.sleep(2); vm.shot("w2-time")
vm.key("esc"); time.sleep(2); vm.shot("w3-network")
vm.key("esc"); time.sleep(3); vm.shot("w4-home")
vm.changed("w1-keyboard", "w2-time"); vm.changed("w2-time", "w3-network"); vm.changed("w3-network", "w4-home")
vm.key("ret"); time.sleep(2); vm.shot("w5-settings")                       # home: Enter opens Settings
vm.changed("w4-home", "w5-settings")
vm.off()
print("welcome: three pages then home; see w1..w5")
