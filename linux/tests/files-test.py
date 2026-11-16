#!/usr/bin/env python3
"""Files operations test in QEMU: make files, then copy, paste, create a folder, delete and open a
picture through the real interface, checking the results from a shell.
Usage: tests/files-test.py <iso> <disk.img> <ovmf_vars.fd> <outdir>"""
import sys, os, time
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, "dualboot-test.py")).read(); src = src[:src.index('print("== 1.')]
__file__ = os.path.join(here, "dualboot-test.py"); exec(src)
PNG = "iVBORw0KGgoAAAANSUhEUgAAACgAAAAYCAIAAAAH5iiXAAAAXUlEQVR42u3NMREAMAgEQTRRIyciXkmUICIiXkwMXEvHzPYbEUIZRieEbhi9EIqNN56LyyhL6JTRLaFXRhtvPBhLKGV0JHRl9CS08caDcRtlC502ui302mjjjcfiD6mVRkzerd5xAAAAAElFTkSuQmCC"
vm = VM(with_iso=True)
vm.expect("Press Enter", 150); vm.send("")
vm.expect("New password:"); vm.send("Admin#Test12"); vm.expect("Retype"); vm.send("Admin#Test12")
vm.expect("login:", 60)
time.sleep(30)
vm.send("silver"); vm.expect(r"~\$ ")
vm.send("mkdir ~/0t; echo hello > ~/0t/a.txt; seq 1 80 | sed 's/^/this is line /' > ~/0t/z.txt; mkdir ~/0t/sub; echo " + PNG + " | base64 -d > ~/0t/pic.png; ls ~/0t"); print(strip(vm.expect(r"~\$ ")))
def check(cmd, want, what):
    vm.send(cmd); out = strip(vm.expect(r"~\$ ")); ok = want in out
    print(("   ok: " if ok else "   FAILED: ") + what); 
    if not ok: print(out); vm.fail(what)
# Home -> Files (the bottom-right pane), then into the test folder 0t (it sorts first)
vm.key("right"); time.sleep(0.5); vm.key("down"); time.sleep(0.5); vm.key("ret"); time.sleep(3)
vm.key("ret"); time.sleep(2); vm.shot("f0-files")
# entries: sub (folder), a.txt, pic.png
vm.key("down"); time.sleep(0.5)                         # a.txt
vm.key("c"); time.sleep(0.8); vm.shot("f1-copied")
vm.key("up"); time.sleep(0.5); vm.key("ret"); time.sleep(1.5)   # into sub
vm.key("v"); time.sleep(3); vm.shot("f2-pasted")
check("ls ~/0t/sub", "a.txt", "copy and paste put a.txt in sub")
vm.key("backspace"); time.sleep(1)                      # back up
vm.key("n"); time.sleep(0.5)
for ch in "newdir": vm.key(ch); time.sleep(0.2)
vm.shot("f3-new-folder"); vm.key("ret"); time.sleep(1.5)
check("ls -d ~/0t/newdir", "newdir", "new folder was made")
# the list now: newdir, sub, a.txt, pic.png
for _ in range(2): vm.key("down"); time.sleep(0.4)      # a.txt
vm.key("d"); time.sleep(0.8); vm.shot("f4-delete-asks"); vm.key("d"); time.sleep(3)
check("test ! -e ~/0t/a.txt && echo gone", "gone", "delete removed a.txt")
vm.key("ret"); time.sleep(2.5); vm.shot("f5-picture")   # pic.png opens (it is the selected row now)
vm.changed("f4-delete-asks", "f5-picture", 0.05)
vm.key("esc"); time.sleep(1.5)
vm.key("down"); time.sleep(0.5); vm.key("ret"); time.sleep(2); vm.shot("f6-text")         # z.txt opens in the text viewer
vm.key("spc"); time.sleep(1); vm.shot("f7-text-page2")
vm.changed("f6-text", "f7-text-page2", 0.001)
vm.key("esc"); time.sleep(1.5)
print("files: copy, paste, new folder, delete and the picture viewer all worked")
vm.off()
