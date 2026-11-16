#!/usr/bin/env python3
"""Mouse test in QEMU: hover selects a pane, left click opens it, right click goes back.
Usage: tests/mouse-test.py <iso> <disk.img> <ovmf_vars.fd> <outdir>   (uses the dual-boot test's VM helper)"""
import sys, os, time
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, "dualboot-test.py")).read(); src = src[:src.index('print("== 1.')]
__file__ = os.path.join(here, "dualboot-test.py"); exec(src)
def say(*a): print(*a, flush=True)
vm = VM(with_iso=True)
vm.expect("Press Enter", 150); vm.send("")
vm.expect("New password:"); vm.send("Admin#Test12"); vm.expect("Retype"); vm.send("Admin#Test12")
vm.expect("login:", 60)
time.sleep(30)                                   # the intro finishes, the home screen settles
vm.shot("m0-home")
vm.monitor("mouse_move 40 0"); vm.monitor("mouse_move 200 -150")        # up and right: the Search pane
time.sleep(1.5); vm.shot("m1-hover-search")
vm.monitor("mouse_button 1"); time.sleep(0.3); vm.monitor("mouse_button 0")   # left click
time.sleep(3); vm.shot("m2-search-opened")
vm.monitor("mouse_button 2"); time.sleep(0.3); vm.monitor("mouse_button 0")   # right click: back
time.sleep(2.5); vm.shot("m3-back-home")
vm.monitor("mouse_move -400 300")                                       # down and left: the Apps pane
time.sleep(1.5); vm.shot("m4-hover-apps")
vm.monitor("mouse_button 1"); time.sleep(0.3); vm.monitor("mouse_button 0")
time.sleep(3); vm.shot("m5-apps-opened")
vm.changed("m0-home", "m1-hover-search", 0.003)
vm.changed("m1-hover-search", "m2-search-opened", 0.01)
vm.changed("m2-search-opened", "m3-back-home", 0.01)
vm.changed("m3-back-home", "m4-hover-apps", 0.003)
vm.changed("m4-hover-apps", "m5-apps-opened", 0.01)
say("mouse: hover, click, right-click all changed the screen")
vm.off()
