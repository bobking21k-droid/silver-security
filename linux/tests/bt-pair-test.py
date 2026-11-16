#!/usr/bin/env python3
"""Bluetooth pairing test in QEMU, without any Bluetooth hardware: BlueZ's own virtual-controller emulator
(btvirt) makes two virtual adapters that talk to each other. One is the computer's; the other pretends to be a
device ("SilverPeer"). The home screen's Settings > Bluetooth must list it and pair with it.
Needs the games layer in the ISO (BlueZ) and tests/bt-img.sh's image.
Usage: tests/bt-pair-test.py <iso> <disk.img> <ovmf_vars.fd> <outdir> <bt.img>"""
import sys, os, time, subprocess
here = os.path.dirname(os.path.abspath(__file__))
bt_img = sys.argv[5]
src = open(os.path.join(here, "dualboot-test.py")).read(); src = src[:src.index('print("== 1.')]
__file__ = os.path.join(here, "dualboot-test.py"); exec(src)
_Popen = subprocess.Popen
def P(args, **k):
    return _Popen(list(args) + ["-drive", "file=%s,format=raw,if=none,id=s0,snapshot=on" % bt_img, "-device", "nvme,drive=s0,serial=BTTEST"], **k)
subprocess.Popen = P
vm = VM(with_iso=True)
vm.expect("Press Enter", 150); vm.send("")
vm.expect("New password:"); vm.send("Admin#Test12"); vm.expect("Retype"); vm.send("Admin#Test12")
vm.expect("login:", 60); time.sleep(20)
vm.send("silver"); vm.expect(r"~\$ ")
vm.send("su"); vm.expect("Password:"); vm.send("Admin#Test12"); vm.expect(r"# ")
def run(c, t=60):
    vm.send(c); return strip(vm.expect(r"# ", t))
run("mkdir -p /run/bt && mount -o ro $(findfs LABEL=BTTEST) /run/bt && cp /run/bt/btvirt /tmp/btvirt && chmod +x /tmp/btvirt")
print(run("modprobe hci_vhci; ls -l /dev/vhci"))
run("(/tmp/btvirt -l2 > /tmp/btvirt.log 2>&1 &); sleep 4; cat /tmp/btvirt.log | head -5")
print(run("bluetoothctl list; ls /sys/class/bluetooth"))
# the second adapter becomes the "device": discoverable, accepts pairing without a PIN
run("(printf 'agent NoInputNoOutput\\ndefault-agent\\nlist\\n'; sleep 3; for a in $(bluetoothctl list | awk '{print $2}' | tail -1); do printf \"select $a\\npower on\\nsystem-alias SilverPeer\\ndiscoverable on\\npairable on\\n\"; done; sleep 300) | bluetoothctl > /tmp/peer.log 2>&1 &")
time.sleep(8)
print(run("tail -15 /tmp/peer.log"))
vm.send("exit"); vm.expect(r"~\$ ")
for k in "exit": vm.key(k)
vm.key("ret"); time.sleep(6); vm.key("spc"); time.sleep(5)                                   # leave the terminal: home screen
vm.key("ret"); time.sleep(2); vm.key("down"); time.sleep(0.5); vm.key("ret"); time.sleep(15); vm.shot("b1-list")
vm.key("ret"); time.sleep(20); vm.shot("b2-pairing")
time.sleep(15); vm.shot("b3-after")
vm.off()
print("bt: see b1-list.png, b2-pairing.png, b3-after.png")
